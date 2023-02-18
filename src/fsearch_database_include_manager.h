#pragma once

#include <gio/gio.h>

#include "fsearch_database_include.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_INCLUDE_MANAGER fsearch_database_include_manager_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabaseIncludeManager, fsearch_database_include_manager, FSEARCH, DATABASE_INCLUDE_MANAGER, GObject)

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_new(void);

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_new_with_defaults(void);

GPtrArray *
fsearch_database_include_manager_get_includes(FsearchDatabaseIncludeManager *self);

void
fsearch_database_include_manager_add(FsearchDatabaseIncludeManager *manager, FsearchDatabaseInclude *include);

void
fsearch_database_include_manager_remove(FsearchDatabaseIncludeManager *manager, FsearchDatabaseInclude *include);

gboolean
fsearch_database_include_manager_equal(FsearchDatabaseIncludeManager *m1, FsearchDatabaseIncludeManager *m2);

FsearchDatabaseIncludeManager *
fsearch_database_include_manager_copy(FsearchDatabaseIncludeManager *self);

G_END_DECLS
