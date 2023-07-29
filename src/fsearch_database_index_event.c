#include "fsearch_database_index_event.h"

FsearchDatabaseIndexEvent *
fsearch_database_index_event_new(FsearchDatabaseIndexEventKind kind,
                                 DynamicArray *folders,
                                 DynamicArray *files,
                                 const char *path) {
    FsearchDatabaseIndexEvent *event = calloc(1, sizeof(FsearchDatabaseIndexEvent));
    g_assert(event);

    event->kind = kind;
    switch (event->kind) {
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED:
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED:
    case FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING:
    case FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED:
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED:
        event->entries.folders = darray_ref(folders);
        event->entries.files = darray_ref(files);
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
    case FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING:
    case FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING:
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
