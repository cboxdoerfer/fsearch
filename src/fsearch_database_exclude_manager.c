#include "fsearch_database_exclude_manager.h"

#include <glib.h>

struct _FsearchDatabaseExcludeManager {
    GObject parent_instance;

    GPtrArray *paths;
    GPtrArray *file_patterns;
    GPtrArray *directory_patterns;

    gboolean exclude_hidden;
};

G_DEFINE_TYPE(FsearchDatabaseExcludeManager, fsearch_database_exclude_manager, G_TYPE_OBJECT)

static gint
compare_str(gconstpointer a, gconstpointer b) {
    const char *aa = *((char **)a);
    const char *bb = *((char **)b);

    return g_strcmp0(aa, bb);
}

static void
add_str_sorted_if_not_already_present(GPtrArray *array, const char *str) {
    if (!g_ptr_array_find_with_equal_func(array, str, g_str_equal, NULL)) {
        g_ptr_array_add(array, g_strdup(str));
        g_ptr_array_sort(array, compare_str);
    }
}

static void
remove_str(GPtrArray *array, const char *str) {
    guint index = 0;
    if (g_ptr_array_find_with_equal_func(array, str, g_str_equal, &index)) {
        g_ptr_array_remove_index(array, index);
    }
}

static void
fsearch_database_exclude_manager_finalize(GObject *object) {
    FsearchDatabaseExcludeManager *self = (FsearchDatabaseExcludeManager *)object;
    g_clear_object(&self->paths);
    g_clear_object(&self->file_patterns);
    g_clear_object(&self->directory_patterns);

    G_OBJECT_CLASS(fsearch_database_exclude_manager_parent_class)->finalize(object);
}

static void
fsearch_database_exclude_manager_class_init(FsearchDatabaseExcludeManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = fsearch_database_exclude_manager_finalize;
}

static void
fsearch_database_exclude_manager_init(FsearchDatabaseExcludeManager *self) {
    self->paths = g_ptr_array_new_with_free_func(g_free);
    self->file_patterns = g_ptr_array_new_with_free_func(g_free);
    self->directory_patterns = g_ptr_array_new_with_free_func(g_free);
}

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new() {
    return g_object_new(FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER, NULL);
}

void
fsearch_database_exclude_manager_add_path(FsearchDatabaseExcludeManager *self, const char *path) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    add_str_sorted_if_not_already_present(self->paths, path);
}

void
fsearch_database_exclude_manager_add_file_pattern(FsearchDatabaseExcludeManager *self, const char *pattern) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    add_str_sorted_if_not_already_present(self->file_patterns, pattern);
}

void
fsearch_database_exclude_manager_add_directory_pattern(FsearchDatabaseExcludeManager *self, const char *pattern) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    add_str_sorted_if_not_already_present(self->directory_patterns, pattern);
}

void
fsearch_database_exclude_manager_set_exclude_hidden(FsearchDatabaseExcludeManager *self, gboolean exclude_hidden) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    self->exclude_hidden = exclude_hidden;
}

void
fsearch_database_exclude_manager_remove_path(FsearchDatabaseExcludeManager *self, const char *path) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    remove_str(self->paths, path);
}

void
fsearch_database_exclude_manager_remove_file_pattern(FsearchDatabaseExcludeManager *self, const char *pattern) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    remove_str(self->file_patterns, pattern);
}

void
fsearch_database_exclude_manager_remove_directory_pattern(FsearchDatabaseExcludeManager *self, const char *pattern) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    remove_str(self->directory_patterns, pattern);
}

gboolean
fsearch_database_exclude_manager_excludes(FsearchDatabaseExcludeManager *self,
                                          const char *path,
                                          const char *basename,
                                          gboolean is_dir) {
    g_return_val_if_fail(self, FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), FALSE);

    if (self->exclude_hidden && g_str_has_prefix(basename, ".")) {
        return TRUE;
    }

    if (is_dir) {
        if (g_ptr_array_find_with_equal_func(self->paths, path, g_str_equal, NULL)) {
            return TRUE;
        }
        else if (g_ptr_array_find_with_equal_func(self->directory_patterns,
                                                  basename,
                                                  (GEqualFunc)g_pattern_match_simple,
                                                  NULL)) {
            return TRUE;
        }
    }
    else {
        if (g_ptr_array_find_with_equal_func(self->file_patterns, basename, (GEqualFunc)g_pattern_match_simple, NULL)) {
            return TRUE;
        }
    }
    return FALSE;
}

gboolean
fsearch_database_exclude_manager_equal(FsearchDatabaseExcludeManager *m1, FsearchDatabaseExcludeManager *m2) {
    g_return_val_if_fail(m1, FALSE);
    g_return_val_if_fail(m2, FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(m1), FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(m2), FALSE);

    if (m1->exclude_hidden != m2->exclude_hidden) {
        return FALSE;
    }

    if (m1->paths->len != m2->paths->len || m1->file_patterns->len != m2->file_patterns->len
        || m1->directory_patterns->len != m2->directory_patterns->len) {
        return FALSE;
    }

    for (guint i = 0; i < m1->paths->len; ++i) {
        const char *p1 = g_ptr_array_index(m1->paths, i);
        const char *p2 = g_ptr_array_index(m2->paths, i);
        if (g_strcmp0(p1, p2) != 0) {
            return FALSE;
        }
    }
    for (guint i = 0; i < m1->file_patterns->len; ++i) {
        const char *p1 = g_ptr_array_index(m1->file_patterns, i);
        const char *p2 = g_ptr_array_index(m2->file_patterns, i);
        if (g_strcmp0(p1, p2) != 0) {
            return FALSE;
        }
    }
    for (guint i = 0; i < m1->directory_patterns->len; ++i) {
        const char *p1 = g_ptr_array_index(m1->directory_patterns, i);
        const char *p2 = g_ptr_array_index(m2->directory_patterns, i);
        if (g_strcmp0(p1, p2) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

GPtrArray *
fsearch_database_exclude_manager_get_paths(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), NULL);
    return g_ptr_array_ref(self->paths);
}

GPtrArray *
fsearch_database_exclude_manager_get_file_patterns(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), NULL);
    return g_ptr_array_ref(self->file_patterns);
}

GPtrArray *
fsearch_database_exclude_manager_get_directory_patterns(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), NULL);
    return g_ptr_array_ref(self->directory_patterns);
}

gboolean
fsearch_database_exclude_manager_get_exclude_hidden(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), FALSE);
    return self->exclude_hidden;
}
