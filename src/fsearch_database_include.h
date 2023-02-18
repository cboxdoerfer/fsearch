#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_INCLUDE (fsearch_database_include_get_type())

typedef struct _FsearchDatabaseInclude FsearchDatabaseInclude;

GType
fsearch_database_include_get_type(void);

FsearchDatabaseInclude *
fsearch_database_include_new(const char *path,
                             gboolean active,
                             gboolean one_file_system,
                             gboolean monitor,
                             gboolean scan_after_load,
                             gint id);

FsearchDatabaseInclude *
fsearch_database_include_ref(FsearchDatabaseInclude *self);

void
fsearch_database_include_unref(FsearchDatabaseInclude *self);

FsearchDatabaseInclude *
fsearch_database_include_copy(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_active(FsearchDatabaseInclude *self);

const char *
fsearch_database_include_get_path(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_one_file_system(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_monitored(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_scan_after_launch(FsearchDatabaseInclude *self);

gint
fsearch_database_include_get_id(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_equal(FsearchDatabaseInclude *i1, FsearchDatabaseInclude *i2);

gint
fsearch_database_include_compare(gconstpointer i1, gconstpointer i2);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseInclude, fsearch_database_include_unref)

G_END_DECLS
