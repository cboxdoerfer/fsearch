#include "fsearch_database_exclude_manager.h"

#include <glib.h>
#include <stdint.h>

struct _FsearchDatabaseExcludeManager {
    GObject parent_instance;

    GPtrArray *excludes;

    gboolean exclude_hidden;
};

G_DEFINE_TYPE(FsearchDatabaseExcludeManager, fsearch_database_exclude_manager, G_TYPE_OBJECT)

static void
add_exclude_if_not_already_present(GPtrArray *excludes, FsearchDatabaseExclude *exclude) {
    if (!g_ptr_array_find_with_equal_func(excludes, exclude, (GEqualFunc)fsearch_database_exclude_equal, NULL)) {
        g_ptr_array_add(excludes, fsearch_database_exclude_ref(exclude));
    }
}

static void
remove_exclude(GPtrArray *excludes, FsearchDatabaseExclude *exclude) {
    guint index = 0;
    if (g_ptr_array_find_with_equal_func(excludes, exclude, (GEqualFunc)fsearch_database_exclude_equal, &index)) {
        g_ptr_array_remove_index(excludes, index);
    }
}

static void
fsearch_database_exclude_manager_finalize(GObject *object) {
    FsearchDatabaseExcludeManager *self = (FsearchDatabaseExcludeManager *)object;
    g_clear_pointer(&self->excludes, g_ptr_array_unref);

    G_OBJECT_CLASS(fsearch_database_exclude_manager_parent_class)->finalize(object);
}

static void
fsearch_database_exclude_manager_class_init(FsearchDatabaseExcludeManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = fsearch_database_exclude_manager_finalize;
}

static void
fsearch_database_exclude_manager_init(FsearchDatabaseExcludeManager *self) {
    self->excludes = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_exclude_unref);
}

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new() {
    return g_object_new(FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER, NULL);
}

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new_with_defaults() {
    FsearchDatabaseExcludeManager *self = fsearch_database_exclude_manager_new();
    g_return_val_if_fail(self, NULL);

    g_ptr_array_add(self->excludes,
                    fsearch_database_exclude_new("/.snapshots",
                                                 TRUE,
                                                 FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED,
                                                 FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
                                                 FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS));
    g_ptr_array_add(self->excludes,
                    fsearch_database_exclude_new("/proc",
                                                 TRUE,
                                                 FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED,
                                                 FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
                                                 FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS));
    g_ptr_array_add(self->excludes,
                    fsearch_database_exclude_new("/sys",
                                                 TRUE,
                                                 FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED,
                                                 FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH,
                                                 FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS));

    return self;
}

void
fsearch_database_exclude_manager_add(FsearchDatabaseExcludeManager *self, FsearchDatabaseExclude *exclude) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    add_exclude_if_not_already_present(self->excludes, exclude);
}

void
fsearch_database_exclude_manager_set_exclude_hidden(FsearchDatabaseExcludeManager *self, gboolean exclude_hidden) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    self->exclude_hidden = exclude_hidden;
}

void
fsearch_database_exclude_manager_remove(FsearchDatabaseExcludeManager *self, FsearchDatabaseExclude *exclude) {
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self));

    remove_exclude(self->excludes, exclude);
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

    for (uint32_t i = 0; i < self->excludes->len; ++i) {
        FsearchDatabaseExclude *exclude = g_ptr_array_index(self->excludes, i);
        if (fsearch_database_exclude_get_active(exclude) == FALSE) {
            continue;
        }
        if (fsearch_database_exclude_matches(exclude, path, basename, is_dir)) {
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

    if (m1->excludes->len != m2->excludes->len) {
        return FALSE;
    }

    for (guint i = 0; i < m1->excludes->len; ++i) {
        FsearchDatabaseExclude *e1 = g_ptr_array_index(m1->excludes, i);
        FsearchDatabaseExclude *e2 = g_ptr_array_index(m2->excludes, i);
        if (!fsearch_database_exclude_equal(e1, e2)) {
            return FALSE;
        }
    }
    return TRUE;
}

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_copy(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, NULL);
    FsearchDatabaseExcludeManager *copy = fsearch_database_exclude_manager_new();
    copy->exclude_hidden = self->exclude_hidden;
    g_clear_pointer(&copy->excludes, g_ptr_array_unref);
    copy->excludes = g_ptr_array_copy(self->excludes, (GCopyFunc)fsearch_database_exclude_copy, NULL);

    return copy;
}

GPtrArray *
fsearch_database_exclude_manager_get_excludes(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), NULL);
    return g_ptr_array_ref(self->excludes);
}

gboolean
fsearch_database_exclude_manager_get_exclude_hidden(FsearchDatabaseExcludeManager *self) {
    g_return_val_if_fail(self, FALSE);
    g_return_val_if_fail(FSEARCH_IS_DATABASE_EXCLUDE_MANAGER(self), FALSE);
    return self->exclude_hidden;
}
