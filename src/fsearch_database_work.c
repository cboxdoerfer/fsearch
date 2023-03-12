#include "fsearch_database_work.h"

#include <glib.h>
#include <string.h>

struct FsearchDatabaseWork {
    FsearchDatabaseWorkKind kind;
    union {
        // FSEARCH_DATABASE_WORK_SCAN
        struct {
            FsearchDatabaseIncludeManager *include_manager;
            FsearchDatabaseExcludeManager *exclude_manager;
            FsearchDatabaseIndexPropertyFlags index_flags;
        };
        // FSEARCH_DATABASE_WORK_SEARCH
        struct {
            FsearchQuery *query;
            FsearchDatabaseIndexProperty sort_order;
            GtkSortType sort_type;
        };
        // FSEARCH_DATABASE_WORK_GET_ITEM_INFO
        struct {
            guint idx;
            FsearchDatabaseEntryInfoFlags entry_info_flags;
        };
        // FSEARCH_DATABASE_WORK_MODIFY_SELECTION
        struct {
            FsearchSelectionType selection_type;
            int32_t idx_1;
            int32_t idx_2;
        };
        // FSEARCH_DATABASE_WORK_MONITOR_EVENT
        struct {
            FsearchDatabaseIndex *monitored_index;
            FsearchDatabaseIndexEventKind event_kind;
            FsearchDatabaseEntry *entry_1;
            FsearchDatabaseEntry *entry_2;
            GString *path;
            int32_t watch_descriptor;
        };
    };

    guint view_id;

    GCancellable *cancellable;

    volatile gint ref_count;
};

static FsearchDatabaseWork *
work_new() {
    FsearchDatabaseWork *work = calloc(1, sizeof(FsearchDatabaseWork));
    g_assert(work);

    work->cancellable = g_cancellable_new();

    work->ref_count = 1;

    return work;
}

static void
work_free(FsearchDatabaseWork *work) {
    switch (work->kind) {
    case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
        break;
    case FSEARCH_DATABASE_WORK_GET_ITEM_INFO:
        break;
    case FSEARCH_DATABASE_WORK_RESCAN:
        break;
    case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
        break;
    case FSEARCH_DATABASE_WORK_SCAN:
        g_clear_object(&work->include_manager);
        g_clear_object(&work->exclude_manager);
        break;
    case FSEARCH_DATABASE_WORK_SEARCH:
        g_clear_pointer(&work->query, fsearch_query_unref);
        break;
    case FSEARCH_DATABASE_WORK_SORT:
        break;
    case FSEARCH_DATABASE_WORK_MODIFY_SELECTION:
        break;
    case FSEARCH_DATABASE_WORK_MONITOR_EVENT:
        g_clear_pointer(&work->monitored_index, fsearch_database_index_unref);
        if (work->path) {
            g_string_free(g_steal_pointer(&work->path), TRUE);
        }
        break;
    case NUM_FSEARCH_DATABASE_WORK_KINDS:
        g_assert_not_reached();
    }

    g_clear_object(&work->cancellable);

    g_clear_pointer(&work, free);
}

FsearchDatabaseWork *
fsearch_database_work_ref(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work != NULL, NULL);
    g_return_val_if_fail(work->ref_count > 0, NULL);

    g_atomic_int_inc(&work->ref_count);

    return work;
}

void
fsearch_database_work_unref(FsearchDatabaseWork *work) {
    g_return_if_fail(work != NULL);
    g_return_if_fail(work->ref_count > 0);

    if (g_atomic_int_dec_and_test(&work->ref_count)) {
        g_clear_pointer(&work, work_free);
    }
}

FsearchDatabaseWork *
fsearch_database_work_new_rescan() {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_RESCAN;
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_scan(FsearchDatabaseIncludeManager *include_manager,
                               FsearchDatabaseExcludeManager *exclude_manager,
                               FsearchDatabaseIndexPropertyFlags flags) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SCAN;
    work->include_manager = g_object_ref(include_manager);
    work->exclude_manager = g_object_ref(exclude_manager);
    work->index_flags = flags;
    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_modify_selection(guint view_id, FsearchSelectionType selection_type, int32_t idx_1, int32_t idx_2) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_MODIFY_SELECTION;
    work->view_id = view_id;
    work->selection_type = selection_type;
    work->idx_1 = idx_1;
    work->idx_2 = idx_2;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_search(guint view_id,
                                 FsearchQuery *query,
                                 FsearchDatabaseIndexProperty sort_order,
                                 GtkSortType sort_type) {
    g_return_val_if_fail(query, NULL);

    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SEARCH;
    work->view_id = view_id;
    work->sort_order = sort_order;
    work->sort_type = sort_type;
    work->query = fsearch_query_ref(query);

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_sort(guint view_id, FsearchDatabaseIndexProperty sort_order, GtkSortType sort_type) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SORT;
    work->view_id = view_id;
    work->sort_order = sort_order;
    work->sort_type = sort_type;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_get_item_info(guint view_id, guint idx, FsearchDatabaseEntryInfoFlags flags) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_GET_ITEM_INFO;
    work->entry_info_flags = flags;
    work->idx = idx;
    work->view_id = view_id;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_load() {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_LOAD_FROM_FILE;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_save() {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_SAVE_TO_FILE;

    return work;
}

FsearchDatabaseWork *
fsearch_database_work_new_monitor_event(FsearchDatabaseIndex *index,
                                        FsearchDatabaseIndexEventKind event_kind,
                                        FsearchDatabaseEntry *entry,
                                        int32_t watch_descriptor) {
    FsearchDatabaseWork *work = work_new();
    work->kind = FSEARCH_DATABASE_WORK_MONITOR_EVENT;

    work->monitored_index = fsearch_database_index_ref(index);
    work->event_kind = event_kind;
    work->entry_1 = entry;
    work->watch_descriptor = watch_descriptor;

    return work;
}

guint
fsearch_database_work_get_view_id(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH || work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION
                             || work->kind == FSEARCH_DATABASE_WORK_SORT
                             || work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO,
                         0);
    return work->view_id;
}

FsearchDatabaseWorkKind
fsearch_database_work_get_kind(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_FSEARCH_DATABASE_WORK_KINDS);
    return work->kind;
}

GCancellable *
fsearch_database_work_get_cancellable(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    return g_object_ref(work->cancellable);
}

void
fsearch_database_work_cancel(FsearchDatabaseWork *work) {
    g_return_if_fail(work);
    g_cancellable_cancel(work->cancellable);
}

FsearchQuery *
fsearch_database_work_search_get_query(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, NULL);
    return fsearch_query_ref(work->query);
}

FsearchDatabaseIndexProperty
fsearch_database_work_search_get_sort_order(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_DATABASE_INDEX_PROPERTIES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, NUM_DATABASE_INDEX_PROPERTIES);
    return work->sort_order;
}

GtkSortType
fsearch_database_work_search_get_sort_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, GTK_SORT_ASCENDING);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SEARCH, GTK_SORT_ASCENDING);
    return work->sort_type;
}

FsearchDatabaseIndexProperty
fsearch_database_work_sort_get_sort_order(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_DATABASE_INDEX_PROPERTIES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SORT, NUM_DATABASE_INDEX_PROPERTIES);
    return work->sort_order;
}

GtkSortType
fsearch_database_work_sort_get_sort_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, GTK_SORT_ASCENDING);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SORT, GTK_SORT_ASCENDING);
    return work->sort_type;
}

FsearchDatabaseIncludeManager *
fsearch_database_work_scan_get_include_manager(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, NULL);
    return g_object_ref(work->include_manager);
}

FsearchDatabaseExcludeManager *
fsearch_database_work_scan_get_exclude_manager(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, NULL);
    return g_object_ref(work->exclude_manager);
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_work_scan_get_flags(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_SCAN, 0);
    return work->index_flags;
}

guint
fsearch_database_work_item_info_get_index(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO, 0);
    return work->idx;
}

FsearchDatabaseEntryInfoFlags
fsearch_database_work_item_info_get_flags(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_GET_ITEM_INFO, 0);
    return work->entry_info_flags;
}

int32_t
fsearch_database_work_modify_selection_get_start_idx(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, 0);
    return work->idx_1;
}

int32_t
fsearch_database_work_modify_selection_get_end_idx(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, 0);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, 0);
    return work->idx_2;
}

FsearchSelectionType
fsearch_database_work_modify_selection_get_type(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_FSEARCH_SELECTION_TYPES);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MODIFY_SELECTION, NUM_FSEARCH_SELECTION_TYPES);
    return work->selection_type;
}

FsearchDatabaseIndexEventKind
fsearch_database_work_monitor_event_get_kind(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NUM_FSEARCH_DATABASE_INDEX_EVENTS);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, NUM_FSEARCH_DATABASE_INDEX_EVENTS);

    return work->event_kind;
}

int32_t
fsearch_database_work_monitor_event_get_watch_descriptor(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, -1);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, -1);

    return work->watch_descriptor;
}

GString *
fsearch_database_work_monitor_event_get_path(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, NULL);

    return work->path;
}

FsearchDatabaseEntry *
fsearch_database_work_monitor_event_get_entry_1(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, NULL);

    return work->entry_1;
}

FsearchDatabaseEntry *
fsearch_database_work_monitor_event_get_entry_2(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, NULL);

    return work->entry_2;
}

FsearchDatabaseIndex *
fsearch_database_work_monitor_event_get_index(FsearchDatabaseWork *work) {
    g_return_val_if_fail(work, NULL);
    g_return_val_if_fail(work->kind == FSEARCH_DATABASE_WORK_MONITOR_EVENT, NULL);

    return work->monitored_index;
}
