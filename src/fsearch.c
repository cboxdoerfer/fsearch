/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

    bool startup_finished;

    GThread *db_thread;
    GMutex mutex;
};

enum { DATABASE_UPDATE_STARTED, DATABASE_UPDATE_FINISHED, DATABASE_LOAD_STARTED, NUM_SIGNALS };

static guint signals[NUM_SIGNALS];

G_DEFINE_TYPE(FsearchApplication, fsearch_application, GTK_TYPE_APPLICATION)

static gpointer
scan_database(gpointer user_data);

static void
fsearch_action_enable(const char *action_name);

static void
fsearch_action_disable(const char *action_name);

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
update_db_cb(gpointer user_data) {
    char *text = user_data;
    if (!text) {
        return FALSE;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        GtkWindow *window = windows->data;
        if (FSEARCH_WINDOW_IS_WINDOW(window)) {
            fsearch_application_window_update_database_label((FsearchApplicationWindow *)window,
                                                             text);
        }
    }

    free(text);
    text = NULL;

    return FALSE;
}

static void
build_location_callback(const char *text) {
    if (text) {
        g_idle_add(update_db_cb, g_strdup(text));
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

        scan_database(fsearch);
        if (fsearch->db) {
            db_unref(fsearch->db);
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

    if (fsearch->db_thread) {
        trace("[exit] waiting for database thread to exit...\n");
        g_thread_join(fsearch->db_thread);
        trace("[exit] database thread finished.\n");
    }

    if (fsearch->db) {
        db_unref(fsearch->db);
    }
    if (fsearch->pool) {
        fsearch_thread_pool_free(fsearch->pool);
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
prepare_windows_for_db_update(FsearchApplication *app) {
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
updated_database_signal_emit_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION_DEFAULT;
    g_mutex_lock(&self->mutex);
    prepare_windows_for_db_update(self);
    if (self->db) {
        db_unref(self->db);
    }
    FsearchDatabase *db = user_data;
    if (db) {
        db_lock(db);
        self->db = db;
        db_unlock(db);
    }
    else {
        self->db = NULL;
    }
    fsearch_action_enable("update_database");
    g_mutex_unlock(&self->mutex);
    g_signal_emit(self, signals[DATABASE_UPDATE_FINISHED], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
load_database_signal_emit_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    g_signal_emit(self, signals[DATABASE_LOAD_STARTED], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
update_database_signal_emit_cb(gpointer user_data) {
    FsearchApplication *self = FSEARCH_APPLICATION(user_data);
    g_signal_emit(self, signals[DATABASE_UPDATE_STARTED], 0);
    return G_SOURCE_REMOVE;
}

static void
update_database_thread(bool rescan) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;

    if (app->startup_finished) {
        return;
    }

    if (rescan) {
        g_idle_add(update_database_signal_emit_cb, app);
    }
    else {
        g_idle_add(load_database_signal_emit_cb, app);
    }

    GTimer *timer = fsearch_timer_start();

    g_mutex_lock(&app->mutex);
    FsearchDatabase *db = db_new(app->config->locations,
                                 app->config->exclude_locations,
                                 app->config->exclude_files,
                                 app->config->exclude_hidden_items);
    g_mutex_unlock(&app->mutex);
    db_lock(db);
    if (rescan) {
        db_scan(db, build_location_callback);
        db_save_locations(db);
    }
    else {
        db_load_from_file(db, NULL, NULL);
    }

    fsearch_timer_stop(timer, "[database_update] finished in %.2f ms\n");
    timer = NULL;

    db_unlock(db);

    g_idle_add(updated_database_signal_emit_cb, db);
}

static gpointer
load_database(gpointer user_data) {

    g_assert(user_data != NULL);
    g_assert(FSEARCH_IS_APPLICATION(user_data));

    update_database_thread(false);

    return NULL;
}

static gpointer
scan_database(gpointer user_data) {

    g_assert(user_data != NULL);
    g_assert(FSEARCH_IS_APPLICATION(user_data));

    update_database_thread(true);

    return NULL;
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

    bool update_db = false;
    bool update_list = false;
    bool update_search = false;

    GtkWindow *win_active = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (!win_active) {
        return;
    }
    FsearchConfig *new_config =
        preferences_ui_launch(app->config, win_active, &update_db, &update_list, &update_search);
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
fsearch_load_database(void) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    if (app->config->locations) {
        fsearch_action_disable("update_database");
        if (app->db_thread) {
            g_thread_join(app->db_thread);
        }
        app->db_thread = g_thread_new("fsearch_db_load_thread", load_database, app);
    }
    return;
}

void
fsearch_database_update(bool scan) {
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    fsearch_action_disable("update_database");
    if (app->db_thread) {
        g_thread_join(app->db_thread);
    }
    if (scan) {
        app->db_thread = g_thread_new("fsearch_db_update_thread", scan_database, app);
    }
    else {
        app->db_thread = g_thread_new("fsearch_db_load_thread", load_database, app);
    }
    return;
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
    GAction *action =
        g_action_map_lookup_action(G_ACTION_MAP(FSEARCH_APPLICATION_DEFAULT), action_name);

    if (action) {
        trace("[application] enable action: %s\n", action_name);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), TRUE);
    }
}

static void
fsearch_action_disable(const char *action_name) {
    GAction *action =
        g_action_map_lookup_action(G_ACTION_MAP(FSEARCH_APPLICATION_DEFAULT), action_name);

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

static GActionEntry app_entries[] = {
    {"new_window", new_window_activated, NULL, NULL, NULL},
    {"about", about_activated, NULL, NULL, NULL},
    {"update_database", update_database_activated, NULL, NULL, NULL},
    {"preferences", preferences_activated, NULL, NULL, NULL},
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

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app);

    static const gchar *toggle_focus[] = {"Tab", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.toggle_focus", toggle_focus);
    static const gchar *search[] = {"<control>f", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.focus_search", search);
    static const gchar *new_window[] = {"<control>n", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.new_window", new_window);
    static const gchar *hide_window[] = {"Escape", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.hide_window", hide_window);
    static const gchar *show_menubar[] = {"<control>m", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.show_menubar", show_menubar);
    static const gchar *match_case[] = {"<control>i", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.match_case", match_case);
    static const gchar *search_mode[] = {"<control>r", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.search_mode", search_mode);
    static const gchar *search_in_path[] = {"<control>u", NULL};
    gtk_application_set_accels_for_action(
        GTK_APPLICATION(app), "win.search_in_path", search_in_path);
    static const gchar *update_database[] = {"<control><shift>r", NULL};
    gtk_application_set_accels_for_action(
        GTK_APPLICATION(app), "app.update_database", update_database);
    static const gchar *preferences[] = {"<control>p", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences", preferences);
    static const gchar *quit[] = {"<control>q", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit);
    FSEARCH_APPLICATION(app)->pool = fsearch_thread_pool_init();
}

static void
fsearch_application_activate(GApplication *app) {
    g_assert(FSEARCH_IS_APPLICATION(app));

    GtkWindow *window = NULL;
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));

    for (; windows; windows = windows->next) {
        window = windows->data;

        if (FSEARCH_WINDOW_IS_WINDOW(window)) {
            GtkWidget *entry = GTK_WIDGET(
                fsearch_application_window_get_search_entry((FsearchApplicationWindow *)window));
            if (entry) {
                gtk_widget_grab_focus(entry);
            }
            gtk_window_present(window);
            return;
        }
    }
    window = GTK_WINDOW(fsearch_application_window_new(FSEARCH_APPLICATION(app)));
    gtk_window_present(window);
    fsearch_database_update(false);
    FsearchApplication *fapp = FSEARCH_APPLICATION(app);
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

    signals[DATABASE_UPDATE_STARTED] = g_signal_new("database-update-started",
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

