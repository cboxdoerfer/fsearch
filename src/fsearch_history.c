#include "fsearch_history.h"

const char *history_file_name = ".fsearch_history.csv";

static void
sort_list_store_by_string(GtkListStore *liststore) {
    gtk_tree_sortable_set_sort_column_id(
        GTK_TREE_SORTABLE(liststore), 0, FALSE);
}

static void
sort_list_store_by_date(GtkListStore *liststore) {
    gtk_tree_sortable_set_sort_column_id(
        GTK_TREE_SORTABLE(liststore), 1, TRUE);
}

static void
fsearch_history_get_path(char *path) {
    g_assert(path);

    const gchar *xdg_home_dir = g_get_home_dir();
    snprintf(path, 4096, "%s/%s", xdg_home_dir, history_file_name);
    return;
}

static gboolean
string_exists_in_history(GtkListStore *history, const gchar *string_to_check) {
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(history), &iter)) {
        do {
            gchar *existing_string = NULL;
            gtk_tree_model_get(GTK_TREE_MODEL(history), &iter, 0, &existing_string, -1);

            if (existing_string) {
                g_strstrip(existing_string);
                if (g_strcmp0(existing_string, string_to_check) == 0) {
                    g_free(existing_string);
                    return TRUE;
                }
                g_free(existing_string);
            }
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(history), &iter));
    }
    return FALSE;
}

void fsearch_history_add(GtkListStore *history, const gchar *query, int sort_by) {
    gchar *stripped_new_string = g_strdup(query);
    g_strstrip(stripped_new_string);

    if (stripped_new_string != NULL && stripped_new_string[0] == '\0') {
        g_free(stripped_new_string);
        return;
    }

    if (string_exists_in_history(history, stripped_new_string)) {
        g_free(stripped_new_string);
        return;
    }

    GtkTreeIter iter;
    GtkTreeIter last_iter;

    gint item_count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(history), NULL);
    if (item_count >= MAX_HISTORY_SPACE) {
        gtk_list_store_remove(history, &last_iter);
    }

    gtk_list_store_append(history, &iter);
    gtk_list_store_set(history, &iter, 0, stripped_new_string, 1, time(NULL), -1);
    g_free(stripped_new_string);

    if (sort_by == SORT_BY_NAME) {
        sort_list_store_by_string(history);
    } else {
        sort_list_store_by_date(history);
    }

    write_liststore_to_csv(history);
}

int
fsearch_history_exists() {
    char path[PATH_MAX];
    fsearch_history_get_path(path);
    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

void
write_liststore_to_csv(GtkListStore *liststore) {
    char path[PATH_MAX];
    fsearch_history_get_path(path);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Error opening history file");
        return;
    }

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(liststore), &iter)) {
        do {
            gchar *name;
            gint timestamp;

            gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, 0, &name, 1, &timestamp, -1);

            fprintf(file, "%s,%d\n", name, timestamp);
            g_free(name);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(liststore), &iter));
    }

    fclose(file);
}

void
write_csv_to_liststore(GtkListStore *liststore) {
    char path[PATH_MAX];
    fsearch_history_get_path(path);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        perror("Error opening history file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        gchar *name = NULL;
        gint timestamp = 0;

        line[strcspn(line, "\n")] = 0;

        char *token = strtok(line, ",");
        if (token != NULL) {
            name = g_strdup(token);
            token = strtok(NULL, ",");
            if (token != NULL) {
                timestamp = atoi(token);
            }
        }

        GtkTreeIter iter;
        gtk_list_store_append(liststore, &iter);
        gtk_list_store_set(liststore, &iter, 0, name, 1, timestamp, -1);

        g_free(name);
    }

    fclose(file);
}