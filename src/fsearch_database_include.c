#include "fsearch_database_include.h"

#include <glib.h>

struct _FsearchDatabaseInclude {
    char *path;
    gboolean monitor;
    gboolean one_file_system;
    gboolean scan_after_launch;

    FsearchDatabaseIncludeKind kind;

    gint id;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseInclude,
                    fsearch_database_include,
                    fsearch_database_include_ref,
                    fsearch_database_include_unref)

FsearchDatabaseInclude *
fsearch_database_include_new(const char *path, gboolean one_file_system, gboolean monitor, gboolean scan_after_load, gint id) {
    FsearchDatabaseInclude *self;

    g_return_val_if_fail(path, NULL);

    self = g_slice_new0(FsearchDatabaseInclude);

    self->path = g_strdup(path);
    self->one_file_system = one_file_system;
    self->monitor = monitor;
    self->scan_after_launch = scan_after_load;
    self->id = id;
    self->ref_count = 1;

    return self;
}

FsearchDatabaseInclude *
fsearch_database_include_ref(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_include_unref(FsearchDatabaseInclude *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self->path, g_free);
        g_slice_free(FsearchDatabaseInclude, self);
    }
}

gboolean
fsearch_database_include_equal(FsearchDatabaseInclude *i1, FsearchDatabaseInclude *i2) {
    g_return_val_if_fail(i1 != NULL, FALSE);
    g_return_val_if_fail(i2 != NULL, FALSE);
    g_return_val_if_fail(i1->ref_count > 0, FALSE);
    g_return_val_if_fail(i2->ref_count > 0, FALSE);

    if (i1->monitor != i2->monitor || i1->one_file_system != i2->one_file_system
        || i1->scan_after_launch != i2->scan_after_launch || g_strcmp0(i1->path, i2->path) != 0) {
        return FALSE;
    }
    return TRUE;
}

gint
fsearch_database_include_compare(gconstpointer i1, gconstpointer i2) {
    FsearchDatabaseInclude *include1 = *(FsearchDatabaseInclude **)i1;
    FsearchDatabaseInclude *include2 = *(FsearchDatabaseInclude **)i2;

    return include1->id - include2->id;
}

FsearchDatabaseInclude *
fsearch_database_include_copy(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_include_new(self->path, self->one_file_system, self->monitor, self->scan_after_launch, self->id);
}

FsearchDatabaseIncludeKind
fsearch_database_include_get_kind(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, 0);
    g_return_val_if_fail(self->ref_count > 0, 0);

    return self->kind;
}

const char *
fsearch_database_include_get_path(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    return self->path;
}

gboolean
fsearch_database_include_get_one_file_system(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);

    return self->one_file_system;
}

gboolean
fsearch_database_include_get_monitored(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);

    return self->monitor;
}

gboolean
fsearch_database_include_get_scan_after_launch(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);

    return self->scan_after_launch;
}

gint
fsearch_database_include_get_id(FsearchDatabaseInclude *self) {
    g_return_val_if_fail(self != NULL, -1);
    g_return_val_if_fail(self->ref_count > 0, -1);

    return self->id;
}
