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
#include <glib.h>

#include "fsearch_database_entry.h"
#include "fsearch_query.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_ENTRY_INFO (fsearch_database_entry_info_get_type())

typedef struct _FsearchDatabaseEntryInfo FsearchDatabaseEntryInfo;

typedef enum {
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_NAME = 1 << 0,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH = 1 << 1,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_SIZE = 1 << 2,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_MODIFICATION_TIME = 1 << 3,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_ACCESS_TIME = 1 << 4,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_CREATION_TIME = 1 << 5,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_STATUS_CHANGE_TIME = 1 << 6,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH_FULL = 1 << 7,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED = 1 << 8,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_INDEX = 1 << 9,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_EXTENSION = 1 << 10,
    FSEARCH_DATABASE_ENTRY_INFO_FLAG_HIGHLIGHTS = 1 << 11,
} FsearchDatabaseEntryInfoFlags;

#define FSEARCH_DATABASE_ENTRY_INFO_FLAG_ALL                                                                               \
    (FSEARCH_DATABASE_ENTRY_INFO_FLAG_NAME | FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH | FSEARCH_DATABASE_ENTRY_INFO_FLAG_SIZE \
     | FSEARCH_DATABASE_ENTRY_INFO_FLAG_MODIFICATION_TIME | FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH_FULL                     \
     | FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED | FSEARCH_DATABASE_ENTRY_INFO_FLAG_INDEX                                  \
     | FSEARCH_DATABASE_ENTRY_INFO_FLAG_EXTENSION | FSEARCH_DATABASE_ENTRY_INFO_FLAG_HIGHLIGHTS)

GType
fsearch_database_entry_info_get_type(void);

FsearchDatabaseEntryInfo *
fsearch_database_entry_info_ref(FsearchDatabaseEntryInfo *info);

void
fsearch_database_entry_info_unref(FsearchDatabaseEntryInfo *info);

FsearchDatabaseEntryInfo *
fsearch_database_entry_info_new(FsearchDatabaseEntry *entry,
                                FsearchQuery *query,
                                uint32_t idx,
                                bool is_selected,
                                FsearchDatabaseEntryInfoFlags flags);

GString *
fsearch_database_entry_info_get_name(FsearchDatabaseEntryInfo *info);

GString *
fsearch_database_entry_info_get_path(FsearchDatabaseEntryInfo *info);

GString *
fsearch_database_entry_info_get_extension(FsearchDatabaseEntryInfo *info);

GString *
fsearch_database_entry_info_get_path_full(FsearchDatabaseEntryInfo *info);

time_t
fsearch_database_entry_info_get_mtime(FsearchDatabaseEntryInfo *info);

off_t
fsearch_database_entry_info_get_size(FsearchDatabaseEntryInfo *info);

bool
fsearch_database_entry_info_get_selected(FsearchDatabaseEntryInfo *info);

uint32_t
fsearch_database_entry_info_get_index(FsearchDatabaseEntryInfo *info);

GHashTable *
fsearch_database_entry_info_get_highlights(FsearchDatabaseEntryInfo *info);

FsearchDatabaseEntryType
fsearch_database_entry_info_get_entry_type(FsearchDatabaseEntryInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseEntryInfo, fsearch_database_entry_info_unref)

G_END_DECLS