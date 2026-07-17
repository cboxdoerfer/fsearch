/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

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

FsearchDatabaseExcludeType
fsearch_database_exclude_get_type_from_string(const char *str);

FsearchDatabaseExcludeMatchScope
fsearch_database_exclude_get_match_scope_from_string(const char *str);

FsearchDatabaseExcludeTarget
fsearch_database_exclude_get_target_from_string(const char *str);

const char *
fsearch_database_exclude_type_to_string(FsearchDatabaseExcludeType type);

const char *
fsearch_database_exclude_match_scope_to_string(FsearchDatabaseExcludeMatchScope scope);

const char *
fsearch_database_exclude_target_to_string(FsearchDatabaseExcludeTarget target);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseExclude, fsearch_database_exclude_unref)

G_END_DECLS