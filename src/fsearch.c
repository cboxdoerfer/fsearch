/*
   FSearch - A fast file search utility
   Copyright © 2020 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "clipboard.h"
#include "database.h"
#include "database_search.h"
#include "debug.h"
#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_limits.h"
#include "fsearch_timer.h"
#include "fsearch_window.h"
#include "preferences_ui.h"
#include "resources.h"
#include "ui_utils.h"
#include "utils.h"
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct _FsearchApplication {
    GtkApplication parent;
    FsearchDatabase *db;
    FsearchConfig *config;
    FsearchThreadPool *pool;

    GThreadPool *db_pool;
    GList *filters;

    bool activated;
    bool new_window;

    FsearchDatabaseState db_state;
    guint db_timeout_id;

    bool db_thread_cancel;
    int num_database_update_active;
    GMutex mutex;
};

typedef struct {
    bool rescan;
    void (*started_cb)(void *);
    void *started_cb_data;
    void (*finished_cb)(void *);
    void (*cancelled_cb)(void *);
    void *cancelled_cb_data;
} DatabaseUpdateContext;

static const char *fsearch_bus_name = "org.fsearch.fsearch";
static const char *fsearch_db_worker_bus_name = "org.fsearch.database_worker";
static const char *fsearch_object_path = "/org/fsearch/fsearch";

enum { DATABASE_SCAN_STARTED, DATABASE_UPDATE_FINISHED, DATABASE_LOAD_STARTED, NUM_SIGNALS };

static guint signals[NUM_SIGNALS];

G_DEFINE_TYPE(FsearchApplication, fsearch_application, GTK_TYPE_APPLICATION)

static FsearchDatabase *
database_update(FsearchApplication *app, bool rescan);

static void
fsearch_action_enable(const char *action_name);

static void
fsearch_action_disable(const char *action_name);

gboolean
db_auto_update_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    trace("[database] scheduled update started\n");
    g_action_group_activate_action(G_ACTION_GROUP(self), "update_database", NULL);
    return G_SOURCE_CONTINUE;
}

static void
fsearch_application_db_auto_update(FsearchApplication *fsearch) {
    if (fsearch->db_timeout_id != 0) {
        g_source_remove(fsearch->db_timeout_id);
        fsearch->db_timeout_id = 0;
    }
    if (fsearch->config->update_database_every) {
        guint seconds =
            fsearch->config->update_database_every_hours * 3600 + fsearch->config->update_database_every_minutes * 60;
        if (seconds < 60) {
            seconds = 60;
        }

        trace("[database] update every %d seconds\n", seconds);
        fsearch->db_timeout_id = g_timeout_add_seconds(seconds, db_auto_update_cb, fsearch);
    }
}

GList *
fsearch_application_get_filters(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    return fsearch->filters;
}

FsearchDatabaseState
fsearch_application_get_db_state(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    return fsearch->db_state;
}

FsearchDatabase *
fsearch_application_get_db(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    FsearchDatabase *db = NULL;
    if (fsearch->db) {
        db_ref(fsearch->db);
        db = fsearch->db;
    }
    return db;
}

FsearchThreadPool *
fsearch_application_get_thread_pool(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    return fsearch->pool;
}

FsearchConfig *
fsearch_application_get_config(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    return fsearch->config;
}

static gboolean
database_scan_status_notify(gpointer user_data) {
    char *text = user_data;
    if (!text) {
        return FALSE;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        GtkWindow *window = windows->data;
        if (FSEARCH_WINDOW_IS_WINDOW(window)) {
            fsearch_application_window_update_database_label((FsearchApplicationWindow *)window, text);
        }
    }

    free(text);
    text = NULL;

    return FALSE;
}

static void
database_scan_status_cb(const char *text) {
    if (text) {
        g_idle_add(database_scan_status_notify, g_strdup(text));
    }
}

static void
fsearch_application_shutdown(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    FsearchApplication *fsearch = FSEARCH_APPLICATION(app);

    GtkWindow *window = NULL;
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        window = windows->data;
        if (FSEARCH_WINDOW_IS_WINDOW(window)) {
            fsearch_application_window_prepare_shutdown(window);
        }
    }

    if (fsearch->pool) {
        fsearch_thread_pool_free(fsearch->pool);
    }
    if (fsearch->db_pool) {
        trace("[exit] waiting for database thread to exit...\n");
        if (fsearch->activated) {
            // only ask thread to cancel work when the application was activated
            // this allows fsearch --update-database to finish its work
            fsearch->db_thread_cancel = true;
        }
        g_thread_pool_free(fsearch->db_pool, FALSE, TRUE);
        fsearch->db_pool = FALSE;
        trace("[exit] database thread finished.\n");
    }
    if (fsearch->db) {
        db_unref(fsearch->db);
    }
    if (fsearch->filters) {
        g_list_free_full(fsearch->filters, (GDestroyNotify)fsearch_filter_free);
        fsearch->filters = NULL;
    }

    config_save(fsearch->config);
    config_free(fsearch->config);
    g_mutex_clear(&fsearch->mutex);
    G_APPLICATION_CLASS(fsearch_application_parent_class)->shutdown(app);
}

static void
fsearch_application_finalize(GObject *object) {
    G_OBJECT_CLASS(fsearch_application_parent_class)->finalize(object);
}

static void
fsearch_prepare_windows_for_db_update(FsearchApplication *app) {
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        FsearchApplicationWindow *window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW(window)) {
            fsearch_application_window_remove_model(window);
        }
    }
    return;
}

static gboolean
database_update_finished_notify(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION_DEFAULT;
    g_mutex_lock(&self->mutex);
    FsearchDatabase *db = user_data;
    if (!self->db_thread_cancel) {
        fsearch_prepare_windows_for_db_update(self);
        if (self->db) {
            db_unref(self->db);
        }
        if (db) {
            db_lock(db);
            self->db = db;
            db_unlock(db);
        }
        else {
            self->db = NULL;
        }
    }
    else if (db) {
        db_unref(db);
    }
    self->db_thread_cancel = false;
    self->num_database_update_active--;
    if (self->num_database_update_active == 0) {
        fsearch_action_enable("update_database");
        fsearch_action_disable("cancel_update_database");
    }
    g_mutex_unlock(&self->mutex);
    g_signal_emit(self, signals[DATABASE_UPDATE_FINISHED], 0);
    return G_SOURCE_REMOVE;
}

static void
database_update_finished_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION_DEFAULT;
    self->db_state = FSEARCH_DATABASE_STATE_IDLE;
    g_idle_add(database_update_finished_notify, user_data);
}

static gboolean
database_load_started_notify(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    g_signal_emit(self, signals[DATABASE_LOAD_STARTED], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
database_scan_started_notify(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    g_signal_emit(self, signals[DATABASE_SCAN_STARTED], 0);
    return G_SOURCE_REMOVE;
}

static void
database_load_started_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    self->db_state = FSEARCH_DATABASE_STATE_LOADING;
    g_idle_add(database_load_started_notify, self);
}

static void
database_scan_started_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    self->db_state = FSEARCH_DATABASE_STATE_SCANNING;
    g_idle_add(database_scan_started_notify, self);
}

static FsearchDatabase *
database_update(FsearchApplication *app, bool rescan) {
    GTimer *timer = fsearch_timer_start();

    g_mutex_lock(&app->mutex);
    FsearchDatabase *db = db_new(app->config->locations,
                                 app->config->exclude_locations,
                                 app->config->exclude_files,
                                 app->config->exclude_hidden_items);
    g_mutex_unlock(&app->mutex);
    db_lock(db);
    if (rescan) {
        db_scan(db, &app->db_thread_cancel, app->config->show_indexing_status ? database_scan_status_cb : NULL);
        if (!app->db_thread_cancel) {
            db_save_locations(db);
        }
    }
    else {
        db_load_from_file(db, NULL, NULL);
    }
    db_unlock(db);

    fsearch_timer_stop(timer, "[database_update] finished in %.2f ms\n");
    timer = NULL;

    return db;
}

static void
database_pool_func(gpointer data, gpointer user_data) {
    FsearchApplication *app = FSEARCH_APPLICATION(user_data);
    DatabaseUpdateContext *ctx = data;
    if (!ctx) {
        return;
    }

    if (ctx->started_cb) {
        ctx->started_cb(ctx->started_cb_data);
    }

    FsearchDatabase *db = database_update(app, ctx->rescan);

    if (ctx->finished_cb) {
        ctx->finished_cb(db);
    }

    g_free(ctx);
    ctx = NULL;
}

static void
about_activated(GSimpleAction *action, GVariant *parameter, gpointer app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        GtkWindow *window = windows->data;
        gtk_show_about_dialog(GTK_WINDOW(window),
                              "program-name",
                              PACKAGE_NAME,
                              "logo-icon-name",
                              "system-search",
                              "license-type",
                              GTK_LICENSE_GPL_2_0,
                              "copyright",
                              "Christian Boxdörfer",
                              "website",
                              "https://github.com/cboxdoerfer/fsearch",
                              "version",
                              PACKAGE_VERSION,
                              "translator-credits",
                              _("translator-credits"),
                              "comments",
                              _("A search utility focusing on performance and advanced features"),
                              NULL);
        break;
    }
}

static void
quit_activated(GSimpleAction *action, GVariant *parameter, gpointer app) {
    g_application_quit(G_APPLICATION(app));
}

static void
on_preferences_ui_finished(FsearchConfig *new_config) {
    if (!new_config) {
        return;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;

    FsearchConfigCompareResult config_diff = {.database_config_changed = true,
                                              .listview_config_changed = true,
                                              .search_config_changed = true};

    if (app->config) {
        config_diff = config_cmp(app->config, new_config);
        config_free(app->config);
    }
    app->config = new_config;
    config_save(app->config);

    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", new_config->enable_dark_theme, NULL);
    fsearch_application_db_auto_update(app);

    if (config_diff.database_config_changed) {
        fsearch_database_update(true);
    }

    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
    for (GList *w = windows; w; w = w->next) {
        FsearchApplicationWindow *window = w->data;
        if (config_diff.search_config_changed) {
            fsearch_application_window_update_search(window);
        }
        if (config_diff.listview_config_changed) {
            fsearch_application_window_update_listview_config(window);
        }
    }
}

static void
preferences_activated(GSimpleAction *action, GVariant *parameter, gpointer gapp) {
    g_assert(FSEARCH_IS_APPLICATION(gapp));
    FsearchApplication *app = FSEARCH_APPLICATION(gapp);

    FsearchPreferencesPage page = g_variant_get_uint32(parameter);

    GtkWindow *win_active = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (!win_active) {
        return;
    }
    FsearchConfig *copy_config = config_copy(app->config);
    preferences_ui_launch(copy_config, win_active, page, on_preferences_ui_finished);
}

void
fsearch_database_update(bool scan) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    fsearch_action_disable("update_database");
    fsearch_action_enable("cancel_update_database");
    app->db_thread_cancel = false;
    app->num_database_update_active++;

    DatabaseUpdateContext *ctx = calloc(1, sizeof(DatabaseUpdateContext));
    g_assert(ctx != NULL);

    if (scan) {
        ctx->rescan = true;
        ctx->started_cb = database_scan_started_cb;
    }
    else {
        ctx->started_cb = database_load_started_cb;
    }
    ctx->started_cb_data = app;
    ctx->finished_cb = database_update_finished_cb;

    g_thread_pool_push(app->db_pool, ctx, NULL);
}

static void
cancel_update_database_activated(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    app->db_thread_cancel = true;
}

static void
update_database_activated(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    fsearch_database_update(true);
}

static void
new_window_activated(GSimpleAction *action, GVariant *parameter, gpointer app) {
    GtkWindow *window = GTK_WINDOW(fsearch_application_window_new(FSEARCH_APPLICATION(app)));
    gtk_window_present(window);
}

static void
fsearch_action_enable(const char *action_name) {
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(FSEARCH_APPLICATION_DEFAULT), action_name);

    if (action) {
        trace("[application] enable action: %s\n", action_name);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);
    }
}

static void
fsearch_action_disable(const char *action_name) {
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(FSEARCH_APPLICATION_DEFAULT), action_name);

    if (action) {
        trace("[application] disable action: %s\n", action_name);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);
    }
}

void
fsearch_application_state_lock(FsearchApplication *fsearch) {
    g_mutex_lock(&fsearch->mutex);
}

void
fsearch_application_state_unlock(FsearchApplication *fsearch) {
    g_mutex_unlock(&fsearch->mutex);
}

static void
fsearch_application_startup(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    G_APPLICATION_CLASS(fsearch_application_parent_class)->startup(app);

    FsearchApplication *fsearch = FSEARCH_APPLICATION(app);

    g_mutex_init(&fsearch->mutex);
    config_make_dir();
    db_make_data_dir();
    fsearch->config = calloc(1, sizeof(FsearchConfig));
    if (!config_load(fsearch->config)) {
        if (!config_load_default(fsearch->config)) {
        }
    }
    fsearch->db = NULL;
    fsearch->db_state = FSEARCH_DATABASE_STATE_IDLE;
    fsearch->filters = fsearch_filter_get_default();

    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme",
                 fsearch->config->enable_dark_theme,
                 NULL);

    if (fsearch->config->show_menubar) {
        GtkBuilder *menu_builder = gtk_builder_new_from_resource("/org/fsearch/fsearch/menus.ui");
        GMenuModel *menu_model = G_MENU_MODEL(gtk_builder_get_object(menu_builder, "fsearch_main_menu"));
        gtk_application_set_menubar(GTK_APPLICATION(app), menu_model);
        g_object_unref(menu_builder);
    }

    static const gchar *toggle_focus[] = {"Tab", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.toggle_focus", toggle_focus);
    static const gchar *search[] = {"<control>f", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.focus_search", search);
    static const gchar *new_window[] = {"<control>n", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.new_window", new_window);
    static const gchar *select_all[] = {"<control>a", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.select_all", select_all);
    static const gchar *hide_window[] = {"Escape", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.hide_window", hide_window);
    static const gchar *match_case[] = {"<control>i", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.match_case", match_case);
    static const gchar *search_mode[] = {"<control>r", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search_mode", search_mode);
    static const gchar *search_in_path[] = {"<control>u", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search_in_path", search_in_path);
    static const gchar *update_database[] = {"<control><shift>r", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.update_database", update_database);
    static const gchar *preferences[] = {"<control>p", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences(uint32 0)", preferences);
    static const gchar *close_window[] = {"<control>w", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.close_window", close_window);
    static const gchar *quit[] = {"<control>q", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit);

    fsearch->pool = fsearch_thread_pool_init();
    fsearch->db_pool = g_thread_pool_new(database_pool_func, app, 1, TRUE, NULL);
}

static GActionEntry app_entries[] = {{"new_window", new_window_activated, NULL, NULL, NULL},
                                     {"about", about_activated, NULL, NULL, NULL},
                                     {"update_database", update_database_activated, NULL, NULL, NULL},
                                     {"cancel_update_database", cancel_update_database_activated, NULL, NULL, NULL},
                                     {"preferences", preferences_activated, "u", NULL, NULL},
                                     {"quit", quit_activated, NULL, NULL, NULL}};

static void
fsearch_application_init(FsearchApplication *app) {
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app);
}

static void
fsearch_application_activate(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));

    FsearchApplication *self = FSEARCH_APPLICATION(app);

    if (!self->new_window) {
        // If there's already a window make it visible
        GtkWindow *window = NULL;
        GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

        for (; windows; windows = windows->next) {
            window = windows->data;

            if (FSEARCH_WINDOW_IS_WINDOW(window)) {
                GtkWidget *entry =
                    GTK_WIDGET(fsearch_application_window_get_search_entry((FsearchApplicationWindow *)window));
                if (entry) {
                    gtk_widget_grab_focus(entry);
                }
                gtk_window_present(window);
                return;
            }
        }
    }

    g_action_group_activate_action(G_ACTION_GROUP(self), "new_window", NULL);

    fsearch_application_db_auto_update(self);

    if (!self->activated) {
        // first full application start
        self->activated = true;
        self->db_thread_cancel = false;
        fsearch_database_update(false);
        if (self->config->update_database_on_launch) {
            fsearch_database_update(true);
        }
    }
}

static gint
fsearch_application_command_line(GApplication *app, GApplicationCommandLine *cmdline) {
    FsearchApplication *self = FSEARCH_APPLICATION(app);
    g_assert(FSEARCH_IS_APPLICATION(self));
    g_assert(G_IS_APPLICATION_COMMAND_LINE(cmdline));

    GVariantDict *dict = g_application_command_line_get_options_dict(cmdline);

    if (g_variant_dict_contains(dict, "new-window")) {
        self->new_window = true;
    }

    if (g_variant_dict_contains(dict, "preferences")) {
        g_action_group_activate_action(G_ACTION_GROUP(self), "preferences", g_variant_new_uint32(0));
        return 0;
    }

    if (g_variant_dict_contains(dict, "update-database")) {
        g_action_group_activate_action(G_ACTION_GROUP(self), "update_database", NULL);
        return 0;
    }

    g_application_activate(G_APPLICATION(self));
    self->new_window = false;

    return G_APPLICATION_CLASS(fsearch_application_parent_class)->command_line(app, cmdline);
}

typedef struct {
    GMainLoop *loop;
    bool update_called_on_primary;
} FsearchApplicationDatabaseWorker;

static void
on_action_group_changed(GDBusConnection *connection,
                        const gchar *sender_name,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *signal_name,
                        GVariant *parameters,
                        gpointer user_data) {
    return;
}

static void
on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    FsearchApplicationDatabaseWorker *worker_ctx = user_data;

    GDBusActionGroup *dbus_group = g_dbus_action_group_get(connection, fsearch_bus_name, fsearch_object_path);

    guint signal_id = g_dbus_connection_signal_subscribe(connection,
                                                         fsearch_bus_name,
                                                         "org.gtk.Actions",
                                                         "Changed",
                                                         fsearch_object_path,
                                                         NULL,
                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                         on_action_group_changed,
                                                         NULL,
                                                         NULL);

    GVariant *reply = g_dbus_connection_call_sync(connection,
                                                  fsearch_bus_name,
                                                  fsearch_object_path,
                                                  "org.gtk.Actions",
                                                  "DescribeAll",
                                                  NULL,
                                                  G_VARIANT_TYPE("(a{s(bgav)})"),
                                                  G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                  -1,
                                                  NULL,
                                                  NULL);
    g_dbus_connection_signal_unsubscribe(connection, signal_id);

    if (dbus_group && reply) {
        trace("[database] trigger update in primary instance\n");
        g_action_group_activate_action(G_ACTION_GROUP(dbus_group), "update_database", NULL);
        g_object_unref(dbus_group);

        worker_ctx->update_called_on_primary = true;
    }
    if (worker_ctx && worker_ctx->loop) {
        g_main_loop_quit(worker_ctx->loop);
    }
}

static void
on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
    FsearchApplicationDatabaseWorker *worker_ctx = user_data;
    if (worker_ctx && worker_ctx->loop) {
        g_main_loop_quit(worker_ctx->loop);
    }
}

static int
local_database_update() {
    GTimer *timer = fsearch_timer_start();

    FsearchConfig *config = config = calloc(1, sizeof(FsearchConfig));
    if (!config_load(config)) {
        if (!config_load_default(config)) {
            g_printerr("[database_update] failed to load config\n");
            return 1;
        }
    }
    FsearchDatabase *db =
        db_new(config->locations, config->exclude_locations, config->exclude_files, config->exclude_hidden_items);
    if (!db) {
        g_printerr("[database_update] failed allocate database\n");
        config_free(config);
        config = NULL;
        return 1;
    }

    db_lock(db);
    int res = !db_scan(db, NULL, NULL);
    db_unlock(db);
    db_unref(db);

    config_free(config);
    config = NULL;

    if (res == 0) {
        fsearch_timer_stop(timer, "[database_update] finished in %.2f ms\n");
        timer = NULL;
    }
    else {
        fsearch_timer_stop(timer, "[database_update] failed after %.2f ms\n");
        timer = NULL;
    }
    return res;
}

static int
fsearch_application_local_database_update() {
    // First detect if the another instance of fsearch is already registered
    // If yes, trigger update there, so the UI is aware of the update and can display its progress
    FsearchApplicationDatabaseWorker worker_ctx = {};
    worker_ctx.loop = g_main_loop_new(NULL, FALSE);
    guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                    fsearch_db_worker_bus_name,
                                    G_BUS_NAME_OWNER_FLAGS_NONE,
                                    NULL,
                                    on_name_acquired,
                                    on_name_lost,
                                    &worker_ctx,
                                    NULL);
    g_main_loop_run(worker_ctx.loop);
    g_bus_unown_name(owner_id);

    if (worker_ctx.update_called_on_primary) {
        // triggered update in primary instance, we're done here
        return 0;
    }
    else {
        // no primary instance found, perform update
        return local_database_update();
    }
}

static gint
fsearch_application_handle_local_options(GApplication *application, GVariantDict *options) {
    if (g_variant_dict_contains(options, "update-database")) {
        return fsearch_application_local_database_update();
    }
    if (g_variant_dict_contains(options, "version")) {
        g_print("FSearch %s\n", PACKAGE_VERSION);
        return 0;
    }

    return -1;
}

void
fsearch_application_add_option_entries(FsearchApplication *self) {
    static const GOptionEntry main_entries[] = {
        {"new-window", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Open a new application window")},
        {"preferences", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Show the application preferences")},
        {"update-database", 'u', 0, G_OPTION_ARG_NONE, NULL, N_("Update the database and exit")},
        {"version", 'v', 0, G_OPTION_ARG_NONE, NULL, N_("Print version information and exit")},
        {NULL}};

    g_assert(FSEARCH_IS_APPLICATION(self));

    g_application_add_main_option_entries(G_APPLICATION(self), main_entries);
}

static void
fsearch_application_class_init(FsearchApplicationClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GApplicationClass *g_app_class = G_APPLICATION_CLASS(klass);

    object_class->finalize = fsearch_application_finalize;

    g_app_class->activate = fsearch_application_activate;
    g_app_class->startup = fsearch_application_startup;
    g_app_class->shutdown = fsearch_application_shutdown;
    g_app_class->command_line = fsearch_application_command_line;
    g_app_class->handle_local_options = fsearch_application_handle_local_options;

    signals[DATABASE_SCAN_STARTED] = g_signal_new("database-scan-started",
                                                  G_TYPE_FROM_CLASS(klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  0);

    signals[DATABASE_UPDATE_FINISHED] = g_signal_new("database-update-finished",
                                                     G_TYPE_FROM_CLASS(klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     0,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     G_TYPE_NONE,
                                                     0);
    signals[DATABASE_LOAD_STARTED] = g_signal_new("database-load-started",
                                                  G_TYPE_FROM_CLASS(klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  0);
}

FsearchApplication *
fsearch_application_new(void) {
    FsearchApplication *self = g_object_new(FSEARCH_APPLICATION_TYPE,
                                            "application-id",
                                            fsearch_bus_name,
                                            "flags",
                                            G_APPLICATION_HANDLES_COMMAND_LINE,
                                            NULL);
    fsearch_application_add_option_entries(self);
    return self;
}

