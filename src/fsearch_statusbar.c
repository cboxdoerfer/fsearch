#define G_LOG_DOMAIN "fsearch-window"

#include "fsearch_statusbar.h"
#include "fsearch.h"
#include "fsearch_database_info.h"

#include <glib/gi18n.h>

struct _FsearchStatusbar {
    GtkRevealer parent_instance;

    GtkWidget *statusbar_database_stack;
    GtkWidget *statusbar_database_status_box;
    GtkWidget *statusbar_database_status_label;
    GtkWidget *statusbar_database_updating_box;
    GtkWidget *statusbar_database_updating_label;
    GtkWidget *statusbar_database_updating_spinner;
    GtkWidget *statusbar_match_case_revealer;
    GtkWidget *statusbar_scan_label;
    GtkWidget *statusbar_scan_status_label;
    GtkWidget *statusbar_search_stack;
    GtkWidget *statusbar_search_status_box;
    GtkWidget *statusbar_search_task_box;
    GtkWidget *statusbar_search_task_spinner;
    GtkWidget *statusbar_search_task_label;
    GtkWidget *statusbar_search_filter_revealer;
    GtkWidget *statusbar_search_in_path_revealer;
    GtkWidget *statusbar_search_filter_label;
    GtkWidget *statusbar_search_label;
    GtkWidget *statusbar_search_mode_revealer;
    GtkWidget *statusbar_selection_num_files_label;
    GtkWidget *statusbar_selection_num_folders_label;
    GtkWidget *statusbar_selection_revealer;
    GtkWidget *statusbar_smart_case_revealer;
    GtkWidget *statusbar_smart_path_revealer;

    guint statusbar_timeout_id;
};

G_DEFINE_TYPE(FsearchStatusbar, fsearch_statusbar, GTK_TYPE_REVEALER)

static void
statusbar_remove_status_update_timeout(FsearchStatusbar *sb) {
    if (sb->statusbar_timeout_id) {
        g_source_remove(sb->statusbar_timeout_id);
        sb->statusbar_timeout_id = 0;
    }
}

void
fsearch_statusbar_set_num_search_results(FsearchStatusbar *sb, uint32_t num_results) {
    statusbar_remove_status_update_timeout(sb);
    gtk_stack_set_visible_child(GTK_STACK(sb->statusbar_search_stack), sb->statusbar_search_status_box);
    gtk_spinner_stop(GTK_SPINNER(sb->statusbar_search_task_spinner));

    gchar sb_text[100] = "";
    snprintf(sb_text, sizeof(sb_text), num_results == 1 ? _("%'d Item") : _("%'d Items"), num_results);
    gtk_label_set_text(GTK_LABEL(sb->statusbar_search_label), sb_text);
}

static void
set_task_status(FsearchStatusbar *sb, const char *label) {
    gtk_label_set_text(GTK_LABEL(sb->statusbar_search_task_label), label);
    gtk_spinner_start(GTK_SPINNER(sb->statusbar_search_task_spinner));
    gtk_stack_set_visible_child(GTK_STACK(sb->statusbar_search_stack), sb->statusbar_search_task_box);
    sb->statusbar_timeout_id = 0;
}

static gboolean
on_statusbar_set_sort_status(gpointer user_data) {
    FsearchStatusbar *sb = user_data;
    set_task_status(sb, _("Sorting…"));
    return G_SOURCE_REMOVE;
}

static gboolean
on_statusbar_set_query_status(gpointer user_data) {
    FsearchStatusbar *sb = user_data;
    set_task_status(sb, _("Querying…"));
    return G_SOURCE_REMOVE;
}

void
fsearch_statusbar_set_sort_status_delayed(FsearchStatusbar *sb) {
    statusbar_remove_status_update_timeout(sb);
    sb->statusbar_timeout_id = g_timeout_add(100, on_statusbar_set_sort_status, sb);
}

void
fsearch_statusbar_set_query_status_delayed(FsearchStatusbar *sb) {
    statusbar_remove_status_update_timeout(sb);
    sb->statusbar_timeout_id = g_timeout_add(200, on_statusbar_set_query_status, sb);
}

void
fsearch_statusbar_set_revealer_visibility(FsearchStatusbar *sb, FsearchStatusbarRevealer revealer, gboolean visible) {
    GtkRevealer *r = NULL;
    switch (revealer) {
    case FSEARCH_STATUSBAR_REVEALER_MATCH_CASE:
        r = GTK_REVEALER(sb->statusbar_match_case_revealer);
        break;
    case FSEARCH_STATUSBAR_REVEALER_SMART_MATCH_CASE:
        r = GTK_REVEALER(sb->statusbar_smart_case_revealer);
        break;
    case FSEARCH_STATUSBAR_REVEALER_SEARCH_IN_PATH:
        r = GTK_REVEALER(sb->statusbar_search_in_path_revealer);
        break;
    case FSEARCH_STATUSBAR_REVEALER_SMART_SEARCH_IN_PATH:
        r = GTK_REVEALER(sb->statusbar_smart_path_revealer);
        break;
    case FSEARCH_STATUSBAR_REVEALER_REGEX:
        r = GTK_REVEALER(sb->statusbar_search_mode_revealer);
        break;
    default:
        g_debug("unknown revealer");
    }
    if (r) {
        gtk_revealer_set_reveal_child(r, visible);
    }
}

void
fsearch_statusbar_set_filter(FsearchStatusbar *sb, const char *filter_name) {
    gtk_label_set_text(GTK_LABEL(sb->statusbar_search_filter_label), filter_name);
    gtk_revealer_set_reveal_child(GTK_REVEALER(sb->statusbar_search_filter_revealer), filter_name ? TRUE : FALSE);
}

void
fsearch_statusbar_set_database_index_text(FsearchStatusbar *sb, const char *text) {
    if (!text) {
        gtk_widget_hide(sb->statusbar_scan_label);
        gtk_widget_hide(sb->statusbar_scan_status_label);
    }
    else {
        gtk_widget_show(sb->statusbar_scan_label);
        gtk_widget_show(sb->statusbar_scan_status_label);
        gtk_label_set_text(GTK_LABEL(sb->statusbar_scan_status_label), text);
    }
}

void
fsearch_statusbar_set_selection(FsearchStatusbar *sb,
                                uint32_t num_files_selected,
                                uint32_t num_folders_selected,
                                uint32_t num_files,
                                uint32_t num_folders) {
    if (!num_folders_selected && !num_files_selected) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(sb->statusbar_selection_revealer), FALSE);
    }
    else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(sb->statusbar_selection_revealer), TRUE);
        char text[100] = "";
        snprintf(text, sizeof(text), "%'d/%'d", num_folders_selected, num_folders);
        gtk_label_set_text(GTK_LABEL(sb->statusbar_selection_num_folders_label), text);
        snprintf(text, sizeof(text), "%'d/%'d", num_files_selected, num_files);
        gtk_label_set_text(GTK_LABEL(sb->statusbar_selection_num_files_label), text);
    }
}

static void
fsearch_statusbar_set_database_updating(FsearchStatusbar *sb, const char *text) {
    gtk_stack_set_visible_child(GTK_STACK(sb->statusbar_database_stack), sb->statusbar_database_updating_box);
    gtk_spinner_start(GTK_SPINNER(sb->statusbar_database_updating_spinner));
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), "%s", text);
    gtk_label_set_text(GTK_LABEL(sb->statusbar_database_updating_label), db_text);
}

static void
fsearch_statusbar_set_database_loading(FsearchStatusbar *sb) {
    fsearch_statusbar_set_database_updating(sb, _("Loading…"));
}

static void
fsearch_statusbar_set_database_scanning(FsearchStatusbar *sb) {
    fsearch_statusbar_set_database_updating(sb, _("Scanning…"));
}

static void
fsearch_statusbar_set_num_db_entries(FsearchStatusbar *sb, uint32_t num_entries) {
    gchar db_text[100] = "";
    snprintf(db_text, sizeof(db_text), _("%'d Items"), num_entries);
    gtk_label_set_text(GTK_LABEL(sb->statusbar_database_status_label), db_text);
}

static void
fsearch_statusbar_set_database_idle(FsearchStatusbar *sb, uint32_t num_entries) {
    fsearch_statusbar_set_num_search_results(sb, 0);

    gtk_spinner_stop(GTK_SPINNER(sb->statusbar_database_updating_spinner));
    gtk_widget_hide(sb->statusbar_scan_label);
    gtk_widget_hide(sb->statusbar_scan_status_label);

    gtk_stack_set_visible_child(GTK_STACK(sb->statusbar_database_stack), sb->statusbar_database_status_box);

    fsearch_statusbar_set_num_db_entries(sb, num_entries);
}

static void
on_database_scan_started(FsearchDatabase *db, gpointer user_data) {
    FsearchStatusbar *statusbar = FSEARCH_STATUSBAR(user_data);
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);

    if (config->show_indexing_status) {
        gtk_widget_show(statusbar->statusbar_scan_label);
        gtk_widget_show(statusbar->statusbar_scan_status_label);
    }
    fsearch_statusbar_set_database_scanning(statusbar);
}

static void
on_database_load_started(FsearchDatabase *db, gpointer user_data) {
    FsearchStatusbar *statusbar = FSEARCH_STATUSBAR(user_data);
    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    FsearchConfig *config = fsearch_application_get_config(app);
    if (config->show_indexing_status) {
        gtk_widget_show(statusbar->statusbar_scan_label);
        gtk_widget_show(statusbar->statusbar_scan_status_label);
    }
    fsearch_statusbar_set_database_loading(statusbar);
}

static void
on_database_update_finished(FsearchDatabase *db, FsearchDatabaseInfo *info, gpointer user_data) {
    FsearchStatusbar *statusbar = FSEARCH_STATUSBAR(user_data);
    fsearch_statusbar_set_database_idle(statusbar, fsearch_database_info_get_num_entries(info));
}

static void
on_database_progress(FsearchDatabase *db, char *text, gpointer user_data) {
    FsearchStatusbar *statusbar = FSEARCH_STATUSBAR(user_data);
    fsearch_statusbar_set_database_index_text(statusbar, text);
}

static void
on_database_changed(FsearchDatabase *db, FsearchDatabaseInfo *info, gpointer user_data) {
    FsearchStatusbar *statusbar = FSEARCH_STATUSBAR(user_data);
    fsearch_statusbar_set_num_db_entries(statusbar, fsearch_database_info_get_num_entries(info));
}

static gboolean
toggle_action_on_2button_press(GdkEvent *event, const char *action, gpointer user_data) {
    guint button;
    gdk_event_get_button(event, &button);
    GdkEventType type = gdk_event_get_event_type(event);
    if (button != GDK_BUTTON_PRIMARY || type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }
    GtkWidget *widget = GTK_WIDGET(user_data);
    GActionGroup *group = gtk_widget_get_action_group(widget, "win");
    if (!group) {
        return FALSE;
    }
    GVariant *state = g_action_group_get_action_state(group, action);
    g_action_group_change_action_state(group, action, g_variant_new_boolean(!g_variant_get_boolean(state)));
    g_clear_pointer(&state, g_variant_unref);
    return TRUE;
}

static gboolean
on_search_filter_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    guint button;
    gdk_event_get_button(event, &button);
    GdkEventType type = gdk_event_get_event_type(event);
    if (button != GDK_BUTTON_PRIMARY || type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }
    GActionGroup *group = gtk_widget_get_action_group(widget, "win");
    if (!group) {
        return FALSE;
    }
    GVariant *state = g_action_group_get_action_state(group, "filter");
    g_action_group_change_action_state(group, "filter", g_variant_new_int32(0));
    g_clear_pointer(&state, g_variant_unref);
    return TRUE;
}

static gboolean
on_search_mode_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "search_mode", widget);
}

static gboolean
on_search_in_path_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "search_in_path", widget);
}

static gboolean
on_match_case_label_button_press_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    return toggle_action_on_2button_press(event, "match_case", widget);
}

static void
fsearch_statusbar_init(FsearchStatusbar *self) {
    g_assert(FSEARCH_IS_STATUSBAR(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    switch (fsearch_application_get_db_state(app)) {
    case FSEARCH_DATABASE_STATE_LOADING:
        fsearch_statusbar_set_database_loading(self);
        break;
    case FSEARCH_DATABASE_STATE_SCANNING:
        fsearch_statusbar_set_database_scanning(self);
        break;
    default:
        fsearch_statusbar_set_database_idle(self, 0);
        break;
    }

    fsearch_statusbar_set_selection(self, 0, 0, 0, 0);

    g_autoptr(FsearchDatabase) db = fsearch_application_get_db(app);

    g_signal_connect_object(db, "scan-started", G_CALLBACK(on_database_scan_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(db, "scan-finished", G_CALLBACK(on_database_update_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(db, "load-started", G_CALLBACK(on_database_load_started), self, G_CONNECT_AFTER);
    g_signal_connect_object(db, "load-finished", G_CALLBACK(on_database_update_finished), self, G_CONNECT_AFTER);
    g_signal_connect_object(db, "database-changed", G_CALLBACK(on_database_changed), self, G_CONNECT_AFTER);
    g_signal_connect_object(db, "database-progress", G_CALLBACK(on_database_progress), self, G_CONNECT_AFTER);
}

static void
fsearch_statusbar_class_init(FsearchStatusbarClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_class, "/io/github/cboxdoerfer/fsearch/ui/fsearch_statusbar.ui");
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_status_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_status_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_updating_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_updating_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_database_updating_spinner);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_match_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_scan_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_scan_status_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_filter_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_filter_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_in_path_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_status_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_task_box);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_task_spinner);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_task_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_search_mode_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_selection_num_files_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_selection_num_folders_label);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_selection_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_smart_case_revealer);
    gtk_widget_class_bind_template_child(widget_class, FsearchStatusbar, statusbar_smart_path_revealer);

    gtk_widget_class_bind_template_callback(widget_class, on_match_case_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_filter_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_in_path_label_button_press_event);
    gtk_widget_class_bind_template_callback(widget_class, on_search_mode_label_button_press_event);
}

FsearchStatusbar *
fsearch_statusbar_new() {
    return g_object_new(FSEARCH_STATUSBAR_TYPE, NULL, NULL, NULL);
}
