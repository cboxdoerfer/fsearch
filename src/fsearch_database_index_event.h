#pragma once

#include "fsearch_array.h"
#include "fsearch_database_entry.h"

typedef enum {
    FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED,
    FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED,
    FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED,
    FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED,
    FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING,
    FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING,
    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED,
    FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED,
    NUM_FSEARCH_DATABASE_INDEX_EVENTS,
} FsearchDatabaseIndexEventKind;

typedef struct {
    FsearchDatabaseIndexEventKind kind;
    DynamicArray *folders;
    DynamicArray *files;
    FsearchDatabaseEntry *entry;
} FsearchDatabaseIndexEvent;

FsearchDatabaseIndexEvent *
fsearch_database_index_event_new(FsearchDatabaseIndexEventKind kind,
                                 DynamicArray *folders,
                                 DynamicArray *files,
                                 FsearchDatabaseEntry *entry);

void
fsearch_database_index_event_free(FsearchDatabaseIndexEvent *event);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndexEvent, fsearch_database_index_event_free);
