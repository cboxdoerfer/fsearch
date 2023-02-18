#pragma once

#include <gio/gio.h>

#include "fsearch_database_exclude.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER fsearch_database_exclude_manager_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabaseExcludeManager, fsearch_database_exclude_manager, FSEARCH, DATABASE_EXCLUDE_MANAGER, GObject)

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new(void);

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new_with_defaults(void);

GPtrArray *
fsearch_database_exclude_manager_get_excludes(FsearchDatabaseExcludeManager *manager);

GPtrArray *
fsearch_database_exclude_manager_get_file_patterns(FsearchDatabaseExcludeManager *manager);

GPtrArray *
fsearch_database_exclude_manager_get_directory_patterns(FsearchDatabaseExcludeManager *manager);

gboolean
fsearch_database_exclude_manager_get_exclude_hidden(FsearchDatabaseExcludeManager *self);

void
fsearch_database_exclude_manager_add(FsearchDatabaseExcludeManager *manager, FsearchDatabaseExclude *exclude);

void
fsearch_database_exclude_manager_add_file_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

void
fsearch_database_exclude_manager_add_directory_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

void
fsearch_database_exclude_manager_set_exclude_hidden(FsearchDatabaseExcludeManager *self, gboolean exclude_hidden);

void
fsearch_database_exclude_manager_remove(FsearchDatabaseExcludeManager *manager, FsearchDatabaseExclude *exclude);

void
fsearch_database_exclude_manager_remove_file_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

void
fsearch_database_exclude_manager_remove_directory_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

gboolean
fsearch_database_exclude_manager_excludes(FsearchDatabaseExcludeManager *manager,
                                          const char *path,
                                          const char *basename,
                                          gboolean is_dir);

gboolean
fsearch_database_exclude_manager_equal(FsearchDatabaseExcludeManager *m1, FsearchDatabaseExcludeManager *m2);

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_copy(FsearchDatabaseExcludeManager *self);

G_END_DECLS
