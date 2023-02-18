#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_EXCLUDE (fsearch_database_exclude_get_type())

typedef struct _FsearchDatabaseExclude FsearchDatabaseExclude;

GType
fsearch_database_exclude_get_type(void);

FsearchDatabaseExclude *
fsearch_database_exclude_new(const char *path, gboolean active);

FsearchDatabaseExclude *
fsearch_database_exclude_ref(FsearchDatabaseExclude *self);

void
fsearch_database_exclude_unref(FsearchDatabaseExclude *self);

FsearchDatabaseExclude *
fsearch_database_exclude_copy(FsearchDatabaseExclude *self);

const char *
fsearch_database_exclude_get_path(FsearchDatabaseExclude *self);

gboolean
fsearch_database_exclude_get_active(FsearchDatabaseExclude *self);

gboolean
fsearch_database_exclude_equal(FsearchDatabaseExclude *e1, FsearchDatabaseExclude *e2);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseExclude, fsearch_database_exclude_unref)

G_END_DECLS
