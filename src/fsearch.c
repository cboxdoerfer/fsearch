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

    bool startup_finished;

    bool db_thread_cancel;
    int num_database_update_active;
    GMutex mutex;
};

typedef struct {
    bool rescan;
    void (*callback_started)(void *);
    void *callback_started_data;
    void (*callback_finished)(void *);
    void (*callback_cancelled)(void *);
    void *callback_cancelled_data;
} DatabaseUpdateContext;

enum { DATABASE_SCAN_STARTED, DATABASE_UPDATE_FINISHED, DATABASE_LOAD_STARTED, NUM_SIGNALS };

static guint signals[NUM_SIGNALS];

G_DEFINE_TYPE(FsearchApplication, fsearch_application, GTK_TYPE_APPLICATION)

static FsearchDatabase *
database_update(FsearchApplication *app, bool rescan);

static void
fsearch_action_enable(const char *action_name);

static void
fsearch_action_disable(const char *action_name);

GList *
fsearch_application_get_filters(FsearchApplication *fsearch) {
    g_assert(FSEARCH_IS_APPLICATION(fsearch));
    return fsearch->filters;
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

static gint
fsearch_options_handler(GApplication *gapp, GVariantDict *options, gpointer data) {
    gboolean version = FALSE, updatedb = FALSE;
    g_variant_dict_lookup(options, "version", "b", &version);
    g_variant_dict_lookup(options, "updatedb", "b", &updatedb);

    if (version) {
        g_printf(PACKAGE_NAME " " VERSION "\n");
    }

    if (updatedb) {
        g_printf("Updating database... ");
        fflush(stdout);

        FsearchApplication *fsearch = FSEARCH_APPLICATION(gapp);
        fsearch->config->update_database_on_launch = true;

        FsearchDatabase *db = database_update(fsearch, true);
        if (db) {
            db_unref(db);
        }

        printf("done!\n");
    }

    return (version || updatedb) ? 0 : -1;
}

static void
fsearch_application_init(FsearchApplication *app) {
    config_make_dir();
    db_make_data_dir();
    app->config = calloc(1, sizeof(FsearchConfig));
    if (!config_load(app->config)) {
        if (!config_load_default(app->config)) {
        }
    }
    app->db = NULL;
    app->startup_finished = false;
    app->filters = fsearch_filter_get_default();
    g_mutex_init(&app->mutex);

    g_application_add_main_option(G_APPLICATION(app),
                                  "version",
                                  '\0',
                                  G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_NONE,
                                  _("Show version information"),
                                  NULL);
    g_application_add_main_option(G_APPLICATION(app),
                                  "updatedb",
                                  'u',
                                  G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_NONE,
                                  _("Update the database"),
                                  NULL);

    g_signal_connect(app, "handle-local-options", G_CALLBACK(fsearch_options_handler), app);
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
        fsearch->db_thread_cancel = true;
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
    g_idle_add(database_load_started_notify, user_data);
}

static void
database_scan_started_cb(gpointer user_data) {
    g_idle_add(database_scan_started_notify, user_data);
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

    if (ctx->callback_started) {
        ctx->callback_started(ctx->callback_started_data);
    }

    FsearchDatabase *db = database_update(app, ctx->rescan);

    if (ctx->callback_finished) {
        ctx->callback_finished(db);
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
preferences_activated(GSimpleAction *action, GVariant *parameter, gpointer gapp) {
    g_assert(FSEARCH_IS_APPLICATION(gapp));
    FsearchApplication *app = FSEARCH_APPLICATION(gapp);

    FsearchPreferencesPage page = g_variant_get_uint32(parameter);
    bool update_db = false;
    bool update_list = false;
    bool update_search = false;

    GtkWindow *win_active = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (!win_active) {
        return;
    }
    FsearchConfig *new_config =
        preferences_ui_launch(app->config, win_active, page, &update_db, &update_list, &update_search);
    if (!new_config) {
        return;
    }

    if (app->config) {
        config_free(app->config);
    }
    app->config = new_config;
    config_save(app->config);

    if (update_db) {
        fsearch_database_update(true);
    }

    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
    for (GList *w = windows; w; w = w->next) {
        FsearchApplicationWindow *window = w->data;
        if (update_search) {
            fsearch_application_window_update_search(window);
        }
        if (update_list) {
            fsearch_application_window_update_listview_config(window);
        }
    }
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
        ctx->callback_started = database_scan_started_cb;
        ctx->callback_started_data = app;
        ctx->callback_finished = database_update_finished_cb;
    }
    else {
        ctx->callback_started = database_load_started_cb;
        ctx->callback_started_data = app;
        ctx->callback_finished = database_update_finished_cb;
    }
    g_thread_pool_push(app->db_pool, ctx, NULL);
    return;
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

static GActionEntry app_entries[] = {{"new_window", new_window_activated, NULL, NULL, NULL},
                                     {"about", about_activated, NULL, NULL, NULL},
                                     {"update_database", update_database_activated, NULL, NULL, NULL},
                                     {"cancel_update_database", cancel_update_database_activated, NULL, NULL, NULL},
                                     {"preferences", preferences_activated, "u", NULL, NULL},
                                     {"quit", quit_activated, NULL, NULL, NULL}};

static void
fsearch_application_startup(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));
    G_APPLICATION_CLASS(fsearch_application_parent_class)->startup(app);

    FsearchApplication *fsearch = FSEARCH_APPLICATION(app);
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
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app);

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
    static const gchar *quit[] = {"<control>q", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit);

    FSEARCH_APPLICATION(app)->pool = fsearch_thread_pool_init();
    FSEARCH_APPLICATION(app)->db_pool = g_thread_pool_new(database_pool_func, app, 1, TRUE, NULL);
}

static void
fsearch_application_activate(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));

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
    window = GTK_WINDOW(fsearch_application_window_new(FSEARCH_APPLICATION(app)));
    gtk_window_present(window);
    FsearchApplication *fapp = FSEARCH_APPLICATION(app);
    fapp->db_thread_cancel = false;
    fsearch_database_update(false);
    if (fapp->config->update_database_on_launch) {
        fsearch_database_update(true);
    }
}

static void
fsearch_application_class_init(FsearchApplicationClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GApplicationClass *g_app_class = G_APPLICATION_CLASS(klass);

    object_class->finalize = fsearch_application_finalize;

    g_app_class->activate = fsearch_application_activate;
    g_app_class->startup = fsearch_application_startup;
    g_app_class->shutdown = fsearch_application_shutdown;

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
    return g_object_new(FSEARCH_APPLICATION_TYPE,
                        "application-id",
                        "org.fsearch.fsearch",
                        "flags",
                        G_APPLICATION_FLAGS_NONE,
                        NULL);
}

