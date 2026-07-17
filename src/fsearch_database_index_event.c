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

#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"

FsearchDatabaseIndexEvent *
fsearch_database_index_event_new(FsearchDatabaseIndexEventKind kind,
                                 DynamicArray *folders,
                                 DynamicArray *files,
                                 const char *path,
                                 FsearchDatabaseIndexPropertyFlags affected_sort_orders,
                                 bool marked) {
    FsearchDatabaseIndexEvent *event = calloc(1, sizeof(FsearchDatabaseIndexEvent));
    g_assert(event);

    event->kind = kind;
    switch (event->kind) {
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED:
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED:
        event->entries.folders = darray_ref(folders);
        event->entries.files = darray_ref(files);
        event->entries.affected_sort_orders = affected_sort_orders;
        event->entries.marked = marked;
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCANNING:
        event->path = path ? g_strdup(path) : NULL;
        break;
    case NUM_FSEARCH_DATABASE_INDEX_EVENTS:
        break;
    }
    return event;
}

void
fsearch_database_index_event_free(FsearchDatabaseIndexEvent *event) {
    g_return_if_fail(event);

    switch (event->kind) {
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED:
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED:
        g_clear_pointer(&event->entries.folders, darray_unref);
        g_clear_pointer(&event->entries.files, darray_unref);

        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCANNING:
        g_clear_pointer(&event->path, free);
        break;
    case NUM_FSEARCH_DATABASE_INDEX_EVENTS:
        break;
    }
    g_clear_pointer(&event, free);
}