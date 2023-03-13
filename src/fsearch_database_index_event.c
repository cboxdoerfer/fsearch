#include "fsearch_database_index_event.h"

FsearchDatabaseIndexEvent *
fsearch_database_index_event_new(FsearchDatabaseIndexEventKind kind,
                                 DynamicArray *folders,
                                 DynamicArray *files,
                                 FsearchDatabaseEntry *entry) {
    FsearchDatabaseIndexEvent *event = calloc(1, sizeof(FsearchDatabaseIndexEvent));
    g_assert(event);

    event->kind = kind;
    event->folders = darray_ref(folders);
    event->files = darray_ref(files);
    event->entry = entry;

    return event;
}

void
fsearch_database_index_event_free(FsearchDatabaseIndexEvent *event) {
    g_return_if_fail(event);

    g_clear_pointer(&event->folders, darray_unref);
    g_clear_pointer(&event->files, darray_unref);

    g_clear_pointer(&event, free);
}
