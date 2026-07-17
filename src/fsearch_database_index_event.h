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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include "fsearch_array.h"
#include "fsearch_database_index_properties.h"

typedef enum {
    FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED,
    FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED,
    FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED,
    FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED,
    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
    FSEARCH_DATABASE_INDEX_EVENT_SCANNING,
    NUM_FSEARCH_DATABASE_INDEX_EVENTS,
} FsearchDatabaseIndexEventKind;

typedef struct {
    FsearchDatabaseIndexEventKind kind;

    union {
        struct {
            DynamicArray *folders;
            DynamicArray *files;
            FsearchDatabaseIndexPropertyFlags affected_sort_orders;
            bool marked;
        } entries;

        char *path;
    };
} FsearchDatabaseIndexEvent;

FsearchDatabaseIndexEvent *
fsearch_database_index_event_new(FsearchDatabaseIndexEventKind kind,
                                 DynamicArray *folders,
                                 DynamicArray *files,
                                 const char *path,
                                 FsearchDatabaseIndexPropertyFlags affected_sort_orders,
                                 bool marked);

void
fsearch_database_index_event_free(FsearchDatabaseIndexEvent *event);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndexEvent, fsearch_database_index_event_free);