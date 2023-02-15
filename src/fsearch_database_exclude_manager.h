#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER fsearch_database_exclude_manager_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabaseExcludeManager, fsearch_database_exclude_manager, FSEARCH, DATABASE_EXCLUDE_MANAGER, GObject)

FsearchDatabaseExcludeManager *
fsearch_database_exclude_manager_new(void);

GPtrArray *
fsearch_database_exclude_manager_get_paths(FsearchDatabaseExcludeManager *manager);

GPtrArray *
fsearch_database_exclude_manager_get_file_patterns(FsearchDatabaseExcludeManager *manager);

GPtrArray *
fsearch_database_exclude_manager_get_directory_patterns(FsearchDatabaseExcludeManager *manager);

gboolean
fsearch_database_exclude_manager_get_exclude_hidden(FsearchDatabaseExcludeManager *self);

void
fsearch_database_exclude_manager_add_path(FsearchDatabaseExcludeManager *manager, const char *path);

void
fsearch_database_exclude_manager_add_file_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

void
fsearch_database_exclude_manager_add_directory_pattern(FsearchDatabaseExcludeManager *manager, const char *pattern);

void
fsearch_database_exclude_manager_set_exclude_hidden(FsearchDatabaseExcludeManager *self, gboolean exclude_hidden);

void
fsearch_database_exclude_manager_remove_path(FsearchDatabaseExcludeManager *manager, const char *path);

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

G_END_DECLS