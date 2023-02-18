#include "fsearch_database_exclude.h"

#include <glib.h>

struct _FsearchDatabaseExclude {
    char *path;

    gboolean active;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseExclude,
                    fsearch_database_exclude,
                    fsearch_database_exclude_ref,
                    fsearch_database_exclude_unref)

FsearchDatabaseExclude *
fsearch_database_exclude_new(const char *path, gboolean active) {
    FsearchDatabaseExclude *self;

    g_return_val_if_fail(path, NULL);

    self = g_slice_new0(FsearchDatabaseExclude);

    self->path = g_strdup(path);
    self->active = active;

    self->ref_count = 1;

    return self;
}

FsearchDatabaseExclude *
fsearch_database_exclude_ref(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_exclude_unref(FsearchDatabaseExclude *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self->path, g_free);
        g_slice_free(FsearchDatabaseExclude, self);
    }
}

gboolean
fsearch_database_exclude_equal(FsearchDatabaseExclude *e1, FsearchDatabaseExclude *e2) {
    g_return_val_if_fail(e1 != NULL, FALSE);
    g_return_val_if_fail(e2 != NULL, FALSE);
    g_return_val_if_fail(e1->ref_count > 0, FALSE);
    g_return_val_if_fail(e2->ref_count > 0, FALSE);

    if (!g_strcmp0(e1->path, e2->path)) {
        return TRUE;
    }
    return FALSE;
}

FsearchDatabaseExclude *
fsearch_database_exclude_copy(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_exclude_new(self->path, self->active);
}

const char *
fsearch_database_exclude_get_path(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    return self->path;
}

gboolean
fsearch_database_exclude_get_active(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);

    return self->active;
}
