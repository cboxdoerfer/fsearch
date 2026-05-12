#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_EXCLUDE (fsearch_database_exclude_get_type())

typedef struct _FsearchDatabaseExclude FsearchDatabaseExclude;

typedef enum FsearchDatabaseExcludeType {
    FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED = 0,
    FSEARCH_DATABASE_EXCLUDE_TYPE_WILDCARD = 1,
    FSEARCH_DATABASE_EXCLUDE_TYPE_REGEX = 2,
} FsearchDatabaseExcludeType;

typedef enum FsearchDatabaseExcludeMatchScope {
    FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH = 0,
    FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_BASENAME = 1,
} FsearchDatabaseExcludeMatchScope;

typedef enum FsearchDatabaseExcludeTarget {
    FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH = 0,
    FSEARCH_DATABASE_EXCLUDE_TARGET_FILES = 1,
    FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS = 2,
} FsearchDatabaseExcludeTarget;

GType
fsearch_database_exclude_get_type(void);

FsearchDatabaseExclude *
fsearch_database_exclude_new(const char *pattern,
                             gboolean active,
                             FsearchDatabaseExcludeType type,
                             FsearchDatabaseExcludeMatchScope scope,
                             FsearchDatabaseExcludeTarget target);

FsearchDatabaseExclude *
fsearch_database_exclude_ref(FsearchDatabaseExclude *self);

void
fsearch_database_exclude_unref(FsearchDatabaseExclude *self);

FsearchDatabaseExclude *
fsearch_database_exclude_copy(FsearchDatabaseExclude *self);

const char *
fsearch_database_exclude_get_pattern(FsearchDatabaseExclude *self);

gboolean
fsearch_database_exclude_get_active(FsearchDatabaseExclude *self);

FsearchDatabaseExcludeType
fsearch_database_exclude_get_exclude_type(FsearchDatabaseExclude *self);

FsearchDatabaseExcludeMatchScope
fsearch_database_exclude_get_match_scope(FsearchDatabaseExclude *self);

FsearchDatabaseExcludeTarget
fsearch_database_exclude_get_target(FsearchDatabaseExclude *self);

gboolean
fsearch_database_exclude_matches(FsearchDatabaseExclude *self, const char *path, const char *basename, gboolean is_dir);

gboolean
fsearch_database_exclude_equal(FsearchDatabaseExclude *e1, FsearchDatabaseExclude *e2);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseExclude, fsearch_database_exclude_unref)

G_END_DECLS
