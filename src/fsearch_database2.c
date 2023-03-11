#include "fsearch_database2.h"

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_file.h"
#include "fsearch_database_index.h"
#include "fsearch_database_info.h"
#include "fsearch_database_scan.h"
#include "fsearch_database_search.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_sort.h"
#include "fsearch_database_work.h"
#include "fsearch_enums.h"
#include "fsearch_memory_pool.h"
#include "fsearch_selection.h"
#include "fsearch_thread_pool.h"

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

struct _FsearchDatabase2 {
    GObject parent_instance;

    // The file the database will be loaded from and saved to
    GFile *file;

    GCancellable *work_queue_thread_cancellable;
    GThread *work_queue_thread;
    GAsyncQueue *work_trigger_queue;
    GAsyncQueue *work_queue;

    GHashTable *search_results;

    FsearchThreadPool *thread_pool;

    FsearchDatabaseIndexStore *store;

    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    FsearchDatabaseIndexPropertyFlags flags;

    GMutex mutex;
};

typedef struct FsearchDatabaseSearchView {
    FsearchQuery *query;
    DynamicArray *files;
    DynamicArray *folders;
    GtkSortType sort_type;
    FsearchDatabaseIndexProperty sort_order;
    GHashTable *file_selection;
    GHashTable *folder_selection;
} FsearchDatabaseSearchView;

typedef enum FsearchDatabase2EventType {
    EVENT_LOAD_STARTED,
    EVENT_LOAD_FINISHED,
    EVENT_ITEM_INFO_READY,
    EVENT_SAVE_STARTED,
    EVENT_SAVE_FINISHED,
    EVENT_SCAN_STARTED,
    EVENT_SCAN_FINISHED,
    EVENT_SEARCH_STARTED,
    EVENT_SEARCH_FINISHED,
    EVENT_SORT_STARTED,
    EVENT_SORT_FINISHED,
    EVENT_SELECTION_CHANGED,
    NUM_EVENTS,
} FsearchDatabase2EventType;

G_DEFINE_TYPE(FsearchDatabase2, fsearch_database2, G_TYPE_OBJECT)

enum { PROP_0, PROP_FILE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];
static guint signals[NUM_EVENTS];

typedef struct FsearchSignalEmitContext {
    FsearchDatabase2 *db;
    FsearchDatabase2EventType type;
    gpointer arg1;
    gpointer arg2;
    GDestroyNotify arg1_free_func;
    GDestroyNotify arg2_free_func;
    guint n_args;
} FsearchSignalEmitContext;

static void
search_view_free(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);
    g_clear_pointer(&view->query, fsearch_query_unref);
    g_clear_pointer(&view->files, darray_unref);
    g_clear_pointer(&view->folders, darray_unref);
    g_clear_pointer(&view->file_selection, fsearch_selection_free);
    g_clear_pointer(&view->folder_selection, fsearch_selection_free);
    g_clear_pointer(&view, free);
}

static FsearchDatabaseSearchView *
search_view_new(FsearchQuery *query,
                DynamicArray *files,
                DynamicArray *folders,
                GHashTable *old_selection,
                FsearchDatabaseIndexProperty sort_order,
                GtkSortType sort_type) {
    FsearchDatabaseSearchView *view = calloc(1, sizeof(FsearchDatabaseSearchView));
    g_assert(view);
    view->query = fsearch_query_ref(query);
    view->files = darray_ref(files);
    view->folders = darray_ref(folders);
    view->sort_order = sort_order;
    view->sort_type = sort_type;
    view->file_selection = fsearch_selection_new();
    view->folder_selection = fsearch_selection_new();
    return view;
}

static void
wakeup_work_queue(FsearchDatabase2 *self) {
    g_async_queue_push(self->work_trigger_queue, GUINT_TO_POINTER(1));
}

static void
signal_emit_context_free(FsearchSignalEmitContext *ctx) {
    if (ctx->arg1_free_func) {
        g_clear_pointer(&ctx->arg1, ctx->arg1_free_func);
    }
    if (ctx->arg2_free_func) {
        g_clear_pointer(&ctx->arg2, ctx->arg2_free_func);
    }
    g_clear_object(&ctx->db);
    g_clear_pointer(&ctx, free);
}

static FsearchSignalEmitContext *
signal_emit_context_new(FsearchDatabase2 *db,
                        FsearchDatabase2EventType type,
                        gpointer arg1,
                        gpointer arg2,
                        guint n_args,
                        GDestroyNotify arg1_free_func,
                        GDestroyNotify arg2_free_func) {
    FsearchSignalEmitContext *ctx = calloc(1, sizeof(FsearchSignalEmitContext));
    g_assert(ctx != NULL);

    ctx->db = g_object_ref(db);
    ctx->type = type;
    ctx->arg1 = arg1;
    ctx->arg2 = arg2;
    ctx->n_args = n_args;
    ctx->arg1_free_func = arg1_free_func;
    ctx->arg2_free_func = arg2_free_func;
    return ctx;
}

static gboolean
emit_signal_cb(gpointer user_data) {
    FsearchSignalEmitContext *ctx = user_data;

    switch (ctx->n_args) {
    case 0:
        g_signal_emit(ctx->db, signals[ctx->type], 0);
        break;
    case 1:
        g_signal_emit(ctx->db, signals[ctx->type], 0, ctx->arg1);
        break;
    case 2:
        g_signal_emit(ctx->db, signals[ctx->type], 0, ctx->arg1, ctx->arg2);
        break;
    default:
        g_assert_not_reached();
    }

    g_clear_pointer(&ctx, signal_emit_context_free);

    return G_SOURCE_REMOVE;
}

static void
emit_signal0(FsearchDatabase2 *self, FsearchDatabase2EventType type) {
    g_idle_add(emit_signal_cb, signal_emit_context_new(self, type, NULL, NULL, 0, NULL, NULL));
}

static void
emit_signal(FsearchDatabase2 *self,
            FsearchDatabase2EventType type,
            gpointer arg1,
            gpointer arg2,
            guint n_args,
            GDestroyNotify arg1_free_func,
            GDestroyNotify arg2_free_func) {
    g_idle_add(emit_signal_cb, signal_emit_context_new(self, type, arg1, arg2, n_args, arg1_free_func, arg2_free_func));
}

static void
emit_item_info_ready_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseEntryInfo *info) {
    emit_signal(self,
                EVENT_ITEM_INFO_READY,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_entry_info_unref);
}

static void
emit_search_finished_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseSearchInfo *info) {
    emit_signal(self,
                EVENT_SEARCH_FINISHED,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
emit_sort_finished_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseSearchInfo *info) {
    emit_signal(self,
                EVENT_SORT_FINISHED,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
emit_selection_changed_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseSearchInfo *info) {
    emit_signal(self,
                EVENT_SELECTION_CHANGED,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
database_unlock(FsearchDatabase2 *self) {
    g_assert(FSEARCH_IS_DATABASE2(self));
    g_mutex_unlock(&self->mutex);
}

static void
database_lock(FsearchDatabase2 *self) {
    g_assert(FSEARCH_IS_DATABASE2(self));
    g_mutex_lock(&self->mutex);
}

static uint32_t
get_num_database_files(FsearchDatabase2 *self) {
    return self->store ? fsearch_database_index_store_get_num_files(self->store) : 0;
}

static uint32_t
get_num_database_folders(FsearchDatabase2 *self) {
    return self->store ? fsearch_database_index_store_get_num_folders(self->store) : 0;
}

static uint32_t
get_num_database_entries(FsearchDatabase2 *self) {
    return get_num_database_files(self) + get_num_database_folders(self);
}

static GFile *
get_default_database_file() {
    return g_file_new_build_filename(g_get_user_data_dir(), "fsearch", "fsearch.db", NULL);
}

static uint32_t
get_idx_for_sort_type(uint32_t idx, uint32_t num_files, uint32_t num_folders, GtkSortType sort_type) {
    if (sort_type == GTK_SORT_DESCENDING) {
        return num_folders + num_files - (idx + 1);
    }
    return idx;
}

static FsearchDatabaseEntry *
get_entry_for_idx(FsearchDatabaseSearchView *view, uint32_t idx) {
    if (!view->files) {
        return NULL;
    }
    if (!view->folders) {
        return NULL;
    }
    const uint32_t num_folders = darray_get_num_items(view->folders);
    const uint32_t num_files = darray_get_num_items(view->files);

    idx = get_idx_for_sort_type(idx, num_files, num_folders, view->sort_type);

    if (idx < num_folders) {
        return darray_get_item(view->folders, idx);
    }
    idx -= num_folders;
    if (idx < num_files) {
        return darray_get_item(view->files, idx);
    }
    return NULL;
}

static bool
is_selected(FsearchDatabaseSearchView *view, FsearchDatabaseEntry *entry) {
    if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FILE) {
        return fsearch_selection_is_selected(view->file_selection, entry);
    }
    else {
        return fsearch_selection_is_selected(view->folder_selection, entry);
    }
}

static FsearchDatabaseSearchView *
get_search_view(FsearchDatabase2 *self, uint32_t view_id) {
    return g_hash_table_lookup(self->search_results, GUINT_TO_POINTER(view_id));
}

static FsearchResult
get_entry_info_non_blocking(FsearchDatabase2 *self, FsearchDatabaseWork *work, FsearchDatabaseEntryInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(work, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    const uint32_t idx = fsearch_database_work_item_info_get_index(work);
    const uint32_t id = fsearch_database_work_get_view_id(work);

    FsearchDatabaseSearchView *view = get_search_view(self, id);
    if (!view) {
        return FSEARCH_RESULT_DB_UNKOWN_SEARCH_VIEW;
    }

    const FsearchDatabaseEntryInfoFlags flags = fsearch_database_work_item_info_get_flags(work);

    FsearchDatabaseEntry *entry = get_entry_for_idx(view, idx);
    if (!entry) {
        return FSEARCH_RESULT_DB_ENTRY_NOT_FOUND;
    }

    *info_out = fsearch_database_entry_info_new(entry, view->query, idx, is_selected(view, entry), flags);
    return FSEARCH_RESULT_SUCCESS;
}

static FsearchResult
get_entry_info(FsearchDatabase2 *self, FsearchDatabaseWork *work, FsearchDatabaseEntryInfo **info_out) {
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    return get_entry_info_non_blocking(self, work, info_out);
}

static void
sort_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);

    const uint32_t id = fsearch_database_work_get_view_id(work);
    const FsearchDatabaseIndexProperty sort_order = fsearch_database_work_sort_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_sort_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    emit_signal(self, EVENT_SORT_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    FsearchDatabaseSearchView *view = get_search_view(self, id);
    if (!view) {
        return;
    }

    DynamicArray *files_new = NULL;
    DynamicArray *folders_new = NULL;

    g_autoptr(DynamicArray) files_fast_sort_index = fsearch_database_index_store_get_files(self->store, sort_order);
    g_autoptr(DynamicArray) folders_fast_sort_index = fsearch_database_index_store_get_folders(self->store, sort_order);

    fsearch_database_sort_results(view->sort_order,
                                  sort_order,
                                  view->files,
                                  view->folders,
                                  files_fast_sort_index,
                                  folders_fast_sort_index,
                                  &files_new,
                                  &folders_new,
                                  &view->sort_order,
                                  cancellable);

    if (files_new) {
        g_clear_pointer(&view->files, darray_unref);
        view->files = files_new;
        view->sort_type = sort_type;
    }
    if (folders_new) {
        g_clear_pointer(&view->folders, darray_unref);
        view->folders = folders_new;
        view->sort_type = sort_type;
    }

    emit_sort_finished_signal(self,
                              id,
                              fsearch_database_search_info_new(fsearch_query_ref(view->query),
                                                               darray_get_num_items(view->files),
                                                               darray_get_num_items(view->folders),
                                                               fsearch_selection_get_num_selected(view->file_selection),
                                                               fsearch_selection_get_num_selected(view->folder_selection),
                                                               view->sort_order,
                                                               view->sort_type));
}

static bool
is_valid_fast_sort_type(FsearchDatabaseIndexProperty sort_type) {
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_PROPERTIES) {
        return true;
    }
    return false;
}

static bool
has_entries_sorted_by_type(DynamicArray **sorted_entries, FsearchDatabaseIndexProperty sort_type) {
    if (!is_valid_fast_sort_type(sort_type)) {
        return false;
    }
    return sorted_entries[sort_type] ? true : false;
}

static bool
search_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_val_if_fail(self, false);

    const uint32_t id = fsearch_database_work_get_view_id(work);

    g_autoptr(FsearchQuery) query = fsearch_database_work_search_get_query(work);
    FsearchDatabaseIndexProperty sort_order = fsearch_database_work_search_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_search_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
    uint32_t num_files = 0;
    uint32_t num_folders = 0;

    database_lock(self);

    if (!self->store) {
        database_unlock(self);
        return false;
    }

    emit_signal(self, EVENT_SEARCH_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    bool result = false;

    g_autoptr(DynamicArray) files = fsearch_database_index_store_get_files(self->store, sort_order);
    g_autoptr(DynamicArray) folders = fsearch_database_index_store_get_folders(self->store, sort_order);

    if (!files && !folders) {
        files = fsearch_database_index_store_get_files(self->store, DATABASE_INDEX_PROPERTY_NAME);
        folders = fsearch_database_index_store_get_folders(self->store, DATABASE_INDEX_PROPERTY_NAME);
        sort_order = DATABASE_INDEX_PROPERTY_NAME;
    }
    else if (files && folders) {
    }
    else {
        g_assert_not_reached();
    }

    DatabaseSearchResult *search_result = db_search(query, self->thread_pool, folders, files, sort_order, cancellable);
    if (search_result) {
        num_files = search_result->files ? darray_get_num_items(search_result->files) : 0;
        num_folders = search_result->folders ? darray_get_num_items(search_result->folders) : 0;

        FsearchDatabaseSearchView *view =
            search_view_new(query, search_result->files, search_result->folders, NULL, sort_order, sort_type);
        g_hash_table_insert(self->search_results, GUINT_TO_POINTER(id), view);

        g_clear_pointer(&search_result->files, darray_unref);
        g_clear_pointer(&search_result->folders, darray_unref);
        g_clear_pointer(&search_result, free);
        result = true;
    }

    database_unlock(self);

    emit_search_finished_signal(
        self,
        id,
        fsearch_database_search_info_new(query, num_files, num_folders, 0, 0, sort_order, sort_type));

    return result;
}

static void
toggle_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx) {
    int32_t tmp = start_idx;
    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = tmp;
    }
    for (int32_t i = start_idx; i <= end_idx; ++i) {
        FsearchDatabaseEntry *entry = get_entry_for_idx(view, i);
        if (!entry) {
            continue;
        }
        FsearchDatabaseEntryType type = db_entry_get_type(entry);
        if (type == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select_toggle(view->file_selection, entry);
        }
        else {
            fsearch_selection_select_toggle(view->folder_selection, entry);
        }
    }
}

static void
select_range(FsearchDatabaseSearchView *view, int32_t start_idx, int32_t end_idx) {
    int32_t tmp = start_idx;
    if (start_idx > end_idx) {
        start_idx = end_idx;
        end_idx = tmp;
    }
    for (int32_t i = start_idx; i <= end_idx; ++i) {
        FsearchDatabaseEntry *entry = get_entry_for_idx(view, i);
        if (!entry) {
            continue;
        }
        FsearchDatabaseEntryType type = db_entry_get_type(entry);
        if (type == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select(view->file_selection, entry);
        }
        else {
            fsearch_selection_select(view->folder_selection, entry);
        }
    }
}

static void
update_views(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabase2 *self = user_data;
    g_return_if_fail(self);
    g_return_if_fail(FSEARCH_IS_DATABASE2(self));

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    emit_selection_changed_signal(
        self,
        GPOINTER_TO_INT(key),
        fsearch_database_search_info_new(fsearch_query_ref(view->query),
                                         darray_get_num_items(view->files),
                                         darray_get_num_items(view->folders),
                                         fsearch_selection_get_num_selected(view->file_selection),
                                         fsearch_selection_get_num_selected(view->folder_selection),
                                         view->sort_order,
                                         view->sort_type));
}

static bool
entry_matches_query(FsearchDatabaseSearchView *view, FsearchDatabaseEntry *entry) {
    FsearchQueryMatchData *match_data = fsearch_query_match_data_new();
    fsearch_query_match_data_set_entry(match_data, entry);

    const bool found = fsearch_query_match(view->query, match_data);
    g_clear_pointer(&match_data, fsearch_query_match_data_free);
    return found;
}

static void
add_result(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseEntry *entry = user_data;
    g_return_if_fail(entry);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    if (!entry_matches_query(view, entry)) {
        return;
    }

    // Remove it from search results
    DynamicArray *array = db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER ? view->folders : view->files;
    DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(view->sort_order);
    if (array && comp_func) {
        darray_insert_item_sorted(array, entry, comp_func, NULL);
    }
}

static void
move_result(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseWork *work = user_data;
    g_return_if_fail(work);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    FsearchDatabaseEntry *entry_moved_from = fsearch_database_work_monitor_event_get_entry_1(work);
    FsearchDatabaseEntry *entry_moved_to = fsearch_database_work_monitor_event_get_entry_2(work);

    DynamicArray *array = NULL;
    GHashTable *selection = NULL;
    if (db_entry_is_folder(entry_moved_from) && db_entry_is_folder(entry_moved_to)) {
        array = view->folders;
        selection = view->folder_selection;
    }
    else if (db_entry_is_file(entry_moved_from) && db_entry_is_file(entry_moved_to)) {
        array = view->files;
        selection = view->file_selection;
    }
    else {
        // You can't move/rename a file, and it becomes a folder (or vice versa), right?
        g_assert_not_reached();
    }

    // First remove the old moved_from entry
    if (entry_matches_query(view, entry_moved_from)) {
        uint32_t idx = 0;
        DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(view->sort_order);
        if (darray_get_item_idx(array, entry_moved_from, comp_func, NULL, &idx)) {
            darray_remove(array, idx, 1);
        }
    }

    // If the moved/renamed entry in its new state doesn't match the current query we're done here.
    if (!entry_matches_query(view, entry_moved_to)) {
        return;
    }

    // Otherwise add it to the results ...
    DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(view->sort_order);
    if (array && comp_func) {
        darray_insert_item_sorted(array, entry_moved_to, comp_func, NULL);
    }

    // ... and keep the selection alive in case the old entry was selected
    if (fsearch_selection_is_selected(selection, entry_moved_from)) {
        fsearch_selection_unselect(selection, entry_moved_from);
        fsearch_selection_select(selection, entry_moved_to);
    }
}

static void
remove_result(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseEntry *entry = user_data;
    g_return_if_fail(entry);

    FsearchDatabaseSearchView *view = value;
    g_return_if_fail(view);

    if (!entry_matches_query(view, entry)) {
        return;
    }

    // Remove it from search results
    DynamicArray *array = db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER ? view->folders : view->files;
    if (array) {
        uint32_t idx = 0;
        DynamicArrayCompareDataFunc comp_func = fsearch_database_sort_get_compare_func_for_property(view->sort_order);
        if (darray_get_item_idx(array, entry, comp_func, NULL, &idx)) {
            darray_remove(array, idx, 1);
        }
    }
    // Remove it from the selection
    GHashTable *selection = db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FOLDER ? view->folder_selection
                                                                                   : view->file_selection;
    fsearch_selection_unselect(selection, entry);
}

static void
process_monitor_event(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    const FsearchDatabaseIndexEventKind event_kind = fsearch_database_work_monitor_event_get_kind(work);
    GString *path = fsearch_database_work_monitor_event_get_path(work);
    FsearchDatabaseIndex *index = fsearch_database_work_monitor_event_get_index(work);
    FsearchDatabaseEntry *entry_1 = fsearch_database_work_monitor_event_get_entry_1(work);
    FsearchDatabaseEntry *entry_2 = fsearch_database_work_monitor_event_get_entry_2(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    bool views_changed_maybe = false;

    switch (event_kind) {
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED:
        fsearch_database_index_store_add_entry(self->store, entry_1, index);
        fsearch_database_index_lock(index);
        g_hash_table_foreach(self->search_results, add_result, entry_1);
        fsearch_database_index_unlock(index);
        views_changed_maybe = true;
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED:
        fsearch_database_index_store_remove_entry(self->store, entry_1, index);
        fsearch_database_index_lock(index);
        g_hash_table_foreach(self->search_results, remove_result, entry_1);
        fsearch_database_index_unlock(index);
        fsearch_database_index_free_entry(index, entry_1);
        views_changed_maybe = true;
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_RENAMED:
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_MOVED:
        // Remove the old moved_from entry from the index store ...
        fsearch_database_index_store_remove_entry(self->store, entry_1, index);
        // ... and add the new moved_to entry to it instead
        fsearch_database_index_store_add_entry(self->store, entry_2, index);

        fsearch_database_index_lock(index);
        g_hash_table_foreach(self->search_results, move_result, work);
        fsearch_database_index_unlock(index);

        fsearch_database_index_free_entry(index, entry_1);
        views_changed_maybe = true;
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CHANGED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_ATTRIBUTE_CHANGED:
        break;
    case NUM_FSEARCH_DATABASE_INDEX_EVENTS:
        break;
    }

    if (views_changed_maybe) {
        g_hash_table_foreach(self->search_results, update_views, self);
    }
}

static void
modify_selection(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);
    const uint32_t view_id = fsearch_database_work_get_view_id(work);
    const FsearchSelectionType type = fsearch_database_work_modify_selection_get_type(work);
    const int32_t start_idx = fsearch_database_work_modify_selection_get_start_idx(work);
    const int32_t end_idx = fsearch_database_work_modify_selection_get_end_idx(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    FsearchDatabaseSearchView *view = get_search_view(self, view_id);
    if (!view) {
        return;
    }

    FsearchDatabaseEntry *entry = get_entry_for_idx(view, start_idx);
    if (!entry) {
        return;
    }

    switch (type) {
    case FSEARCH_SELECTION_TYPE_CLEAR:
        fsearch_selection_unselect_all(view->file_selection);
        fsearch_selection_unselect_all(view->folder_selection);
        break;
    case FSEARCH_SELECTION_TYPE_ALL:
        fsearch_selection_select_all(view->folder_selection, view->folders);
        fsearch_selection_select_all(view->file_selection, view->files);
        break;
    case FSEARCH_SELECTION_TYPE_INVERT:
        fsearch_selection_invert(view->folder_selection, view->folders);
        fsearch_selection_invert(view->folder_selection, view->files);
        break;
    case FSEARCH_SELECTION_TYPE_SELECT:
        if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select(view->file_selection, entry);
        }
        else {
            fsearch_selection_select(view->folder_selection, entry);
        }
        break;
    case FSEARCH_SELECTION_TYPE_TOGGLE:
        if (db_entry_get_type(entry) == DATABASE_ENTRY_TYPE_FILE) {
            fsearch_selection_select_toggle(view->file_selection, entry);
        }
        else {
            fsearch_selection_select_toggle(view->folder_selection, entry);
        }
        break;
    case FSEARCH_SELECTION_TYPE_SELECT_RANGE:
        select_range(view, start_idx, end_idx);
        break;
    case FSEARCH_SELECTION_TYPE_TOGGLE_RANGE:
        toggle_range(view, start_idx, end_idx);
        break;
    case NUM_FSEARCH_SELECTION_TYPES:
        g_assert_not_reached();
    }

    emit_selection_changed_signal(
        self,
        view_id,
        fsearch_database_search_info_new(fsearch_query_ref(view->query),
                                         darray_get_num_items(view->files),
                                         darray_get_num_items(view->folders),
                                         fsearch_selection_get_num_selected(view->file_selection),
                                         fsearch_selection_get_num_selected(view->folder_selection),
                                         view->sort_order,
                                         view->sort_type));
}

static void
save_database_to_file(FsearchDatabase2 *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    g_autoptr(GFile) db_directory = g_file_get_parent(self->file);
    g_autofree gchar *db_directory_path = g_file_get_path(db_directory);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    db_file_save(self->store, NULL);
}

static void
rescan_database(FsearchDatabase2 *self) {
    g_return_if_fail(self);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = g_object_ref(self->include_manager);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = g_object_ref(self->exclude_manager);
    const FsearchDatabaseIndexPropertyFlags flags = self->flags;

    g_clear_pointer(&locker, g_mutex_locker_free);

    g_autoptr(FsearchDatabaseIndexStore)
        store = db_scan2(include_manager, exclude_manager, flags, NULL, self->work_queue);
    g_return_if_fail(store);

    locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_set_object(&self->include_manager, include_manager);
    g_set_object(&self->exclude_manager, exclude_manager);
    self->flags = flags;
    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = g_steal_pointer(&store);
}

static void
scan_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    emit_signal0(self, EVENT_SCAN_STARTED);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_work_scan_get_include_manager(work);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_work_scan_get_exclude_manager(work);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_work_scan_get_flags(work);

    g_autoptr(FsearchDatabaseIndexStore)
        store = db_scan2(include_manager, exclude_manager, flags, NULL, self->work_queue);
    g_return_if_fail(store);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_set_object(&self->include_manager, include_manager);
    g_set_object(&self->exclude_manager, exclude_manager);
    self->flags = flags;
    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = g_steal_pointer(&store);

    g_hash_table_remove_all(self->search_results);

    emit_signal(self,
                EVENT_SCAN_FINISHED,
                fsearch_database_info_new(self->include_manager,
                                          self->exclude_manager,
                                          get_num_database_files(self),
                                          get_num_database_folders(self)),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

static void
load_database_from_file(FsearchDatabase2 *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    emit_signal0(self, EVENT_LOAD_STARTED);

    g_autofree gchar *file_path = g_file_get_path(self->file);
    g_return_if_fail(file_path);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = NULL;
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = NULL;
    g_autoptr(FsearchDatabaseIndexStore) store = NULL;
    bool res = db_file_load(file_path, NULL, &store, &include_manager, &exclude_manager);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (res) {
        g_clear_pointer(&self->store, fsearch_database_index_store_unref);
        self->store = g_steal_pointer(&store);
        g_set_object(&self->include_manager, include_manager);
        g_set_object(&self->exclude_manager, exclude_manager);
    }
    else {
        g_set_object(&self->include_manager, fsearch_database_include_manager_new_with_defaults());
        g_set_object(&self->exclude_manager, fsearch_database_exclude_manager_new_with_defaults());
    }

    emit_signal(self,
                EVENT_LOAD_FINISHED,
                fsearch_database_info_new(self->include_manager,
                                          self->exclude_manager,
                                          get_num_database_files(self),
                                          get_num_database_folders(self)),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

static gpointer
work_queue_thread(gpointer data) {
    g_debug("manager thread started");
    FsearchDatabase2 *self = data;

    while (TRUE) {
        g_async_queue_timeout_pop(self->work_trigger_queue, 1000000);

        while (TRUE) {
            g_autoptr(FsearchDatabaseWork) work = g_async_queue_try_pop(self->work_queue);
            if (!work) {
                break;
            }

            g_autoptr(GTimer) timer = g_timer_new();
            g_timer_start(timer);

            switch (fsearch_database_work_get_kind(work)) {
            case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
                load_database_from_file(self);
                break;
            case FSEARCH_DATABASE_WORK_GET_ITEM_INFO: {
                FsearchDatabaseEntryInfo *info = NULL;
                get_entry_info(self, work, &info);
                if (info) {
                    emit_item_info_ready_signal(self, fsearch_database_work_get_view_id(work), g_steal_pointer(&info));
                }
                break;
            }
            case FSEARCH_DATABASE_WORK_RESCAN:
                emit_signal0(self, EVENT_SCAN_STARTED);
                rescan_database(self);
                emit_signal0(self, EVENT_SCAN_FINISHED);
                break;
            case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
                emit_signal0(self, EVENT_SAVE_STARTED);
                save_database_to_file(self);
                emit_signal0(self, EVENT_SAVE_FINISHED);
                break;
            case FSEARCH_DATABASE_WORK_SCAN:
                scan_database(self, work);
                break;
            case FSEARCH_DATABASE_WORK_SEARCH:
                search_database(self, work);
                break;
            case FSEARCH_DATABASE_WORK_SORT:
                sort_database(self, work);
                break;
            case FSEARCH_DATABASE_WORK_MODIFY_SELECTION:
                modify_selection(self, work);
                break;
            case FSEARCH_DATABASE_WORK_MONITOR_EVENT:
                process_monitor_event(self, work);
                break;
            default:
                g_assert_not_reached();
            }

            g_debug("finished work in: %fs.", g_timer_elapsed(timer, NULL));
        }

        if (g_cancellable_is_cancelled(self->work_queue_thread_cancellable)) {
            g_debug("thread cancelled...");
            break;
        }
    }

    g_debug("manager thread returning");
    return NULL;
}

static void
fsearch_database2_constructed(GObject *object) {
    FsearchDatabase2 *self = (FsearchDatabase2 *)object;

    g_assert(FSEARCH_IS_DATABASE2(self));

    G_OBJECT_CLASS(fsearch_database2_parent_class)->constructed(object);

    if (self->file == NULL) {
        self->file = get_default_database_file();
    }

    g_async_queue_push(self->work_queue, fsearch_database_work_new_load());
}

static void
fsearch_database2_dispose(GObject *object) {
    FsearchDatabase2 *self = (FsearchDatabase2 *)object;

    // Notify work queue thread to exit itself
    g_cancellable_cancel(self->work_queue_thread_cancellable);
    wakeup_work_queue(self);
    g_thread_join(self->work_queue_thread);

    G_OBJECT_CLASS(fsearch_database2_parent_class)->dispose(object);
}

static void
fsearch_database2_finalize(GObject *object) {
    FsearchDatabase2 *self = (FsearchDatabase2 *)object;

    database_lock(self);
    g_clear_object(&self->work_queue_thread_cancellable);
    g_clear_pointer(&self->work_trigger_queue, g_async_queue_unref);
    g_clear_pointer(&self->work_queue, g_async_queue_unref);
    g_clear_pointer(&self->thread_pool, fsearch_thread_pool_free);

    g_clear_object(&self->file);

    g_clear_pointer(&self->search_results, g_hash_table_unref);
    g_clear_object(&self->include_manager);
    g_clear_object(&self->exclude_manager);
    database_unlock(self);

    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(fsearch_database2_parent_class)->finalize(object);
}

static void
fsearch_database2_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchDatabase2 *self = FSEARCH_DATABASE2(object);

    switch (prop_id) {
    case PROP_FILE:
        g_value_set_object(value, self->file);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database2_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchDatabase2 *self = FSEARCH_DATABASE2(object);

    switch (prop_id) {
    case PROP_FILE:
        self->file = g_value_get_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database2_class_init(FsearchDatabase2Class *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = fsearch_database2_constructed;
    object_class->dispose = fsearch_database2_dispose;
    object_class->finalize = fsearch_database2_finalize;
    object_class->set_property = fsearch_database2_set_property;
    object_class->get_property = fsearch_database2_get_property;

    properties[PROP_FILE] = g_param_spec_object("file",
                                                "File",
                                                "The file where the database will be loaded from or saved to by "
                                                "default",
                                                G_TYPE_FILE,
                                                (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    signals[EVENT_LOAD_STARTED] =
        g_signal_new("load-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[EVENT_LOAD_FINISHED] = g_signal_new("load-finished",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                1,
                                                FSEARCH_TYPE_DATABASE_INFO);
    signals[EVENT_SAVE_STARTED] = g_signal_new("save-started",
                                               G_TYPE_FROM_CLASS(klass),
                                               G_SIGNAL_RUN_LAST,
                                               0,
                                               NULL,
                                               NULL,
                                               NULL,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_POINTER);
    signals[EVENT_SAVE_FINISHED] = g_signal_new("save-finished",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                1,
                                                G_TYPE_POINTER);
    signals[EVENT_SCAN_STARTED] =
        g_signal_new("scan-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[EVENT_SCAN_FINISHED] = g_signal_new("scan-finished",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                1,
                                                FSEARCH_TYPE_DATABASE_INFO);
    signals[EVENT_SEARCH_STARTED] = g_signal_new("search-started",
                                                 G_TYPE_FROM_CLASS(klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 G_TYPE_NONE,
                                                 1,
                                                 G_TYPE_UINT);
    signals[EVENT_SEARCH_FINISHED] = g_signal_new("search-finished",
                                                  G_TYPE_FROM_CLASS(klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  2,
                                                  G_TYPE_UINT,
                                                  FSEARCH_TYPE_DATABASE_SEARCH_INFO);
    signals[EVENT_SORT_STARTED] = g_signal_new("sort-started",
                                               G_TYPE_FROM_CLASS(klass),
                                               G_SIGNAL_RUN_LAST,
                                               0,
                                               NULL,
                                               NULL,
                                               NULL,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_UINT);
    signals[EVENT_SORT_FINISHED] = g_signal_new("sort-finished",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                2,
                                                G_TYPE_UINT,
                                                FSEARCH_TYPE_DATABASE_SEARCH_INFO);
    signals[EVENT_SELECTION_CHANGED] = g_signal_new("selection-changed",
                                                    G_TYPE_FROM_CLASS(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    2,
                                                    G_TYPE_UINT,
                                                    FSEARCH_TYPE_DATABASE_SEARCH_INFO);
    signals[EVENT_ITEM_INFO_READY] = g_signal_new("item-info-ready",
                                                  G_TYPE_FROM_CLASS(klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  2,
                                                  G_TYPE_UINT,
                                                  FSEARCH_TYPE_DATABASE_ENTRY_INFO);
}

static void
fsearch_database2_init(FsearchDatabase2 *self) {
    g_mutex_init((&self->mutex));
    self->thread_pool = fsearch_thread_pool_init();
    self->search_results = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)search_view_free);
    self->work_queue = g_async_queue_new();
    self->work_trigger_queue = g_async_queue_new();
    self->work_queue_thread = g_thread_new("FsearchDatabaseWorkQueue", work_queue_thread, self);
    self->work_queue_thread_cancellable = g_cancellable_new();
}

void
fsearch_database2_queue_work(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_async_queue_push(self->work_queue, fsearch_database_work_ref(work));
}

void
fsearch_database2_process_work_now(FsearchDatabase2 *self) {
    g_return_if_fail(self);

    wakeup_work_queue(self);
}

FsearchResult
fsearch_database2_try_get_search_info(FsearchDatabase2 *self, uint32_t view_id, FsearchDatabaseSearchInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    FsearchResult res = FSEARCH_RESULT_FAILED;
    FsearchDatabaseSearchView *view = get_search_view(self, view_id);
    if (!view) {
        res = FSEARCH_RESULT_DB_UNKOWN_SEARCH_VIEW;
    }
    else {
        *info_out = fsearch_database_search_info_new(fsearch_query_ref(view->query),
                                                     darray_get_num_items(view->files),
                                                     darray_get_num_items(view->folders),
                                                     fsearch_selection_get_num_selected(view->file_selection),
                                                     fsearch_selection_get_num_selected(view->folder_selection),
                                                     view->sort_order,
                                                     view->sort_type);
        res = FSEARCH_RESULT_SUCCESS;
    }

    database_unlock(self);

    return res;
}

FsearchResult
fsearch_database2_try_get_item_info(FsearchDatabase2 *self,
                                    uint32_t view_id,
                                    uint32_t idx,
                                    FsearchDatabaseEntryInfoFlags flags,
                                    FsearchDatabaseEntryInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_get_item_info(view_id, idx, flags);
    FsearchResult res = get_entry_info_non_blocking(self, work, info_out);

    database_unlock(self);

    return res;
}

FsearchResult
fsearch_database2_try_get_database_info(FsearchDatabase2 *self, FsearchDatabaseInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    *info_out = fsearch_database_info_new(self->include_manager,
                                          self->exclude_manager,
                                          get_num_database_files(self),
                                          get_num_database_folders(self));

    database_unlock(self);

    return FSEARCH_RESULT_SUCCESS;
}

typedef struct {
    FsearchDatabase2ForeachFunc func;
    gpointer user_data;
} FsearchDatabase2SelectionForeachContext;

static void
selection_foreach(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseEntry *entry = value;
    if (G_UNLIKELY(!entry)) {
        return;
    }
    FsearchDatabase2SelectionForeachContext *ctx = user_data;
    ctx->func(entry, ctx->user_data);
}

void
fsearch_database2_selection_foreach(FsearchDatabase2 *self,
                                    uint32_t view_id,
                                    FsearchDatabase2ForeachFunc func,
                                    gpointer user_data) {
    g_return_if_fail(FSEARCH_IS_DATABASE2(self));
    g_return_if_fail(func);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    FsearchDatabaseSearchView *view = get_search_view(self, view_id);
    if (!view) {
        return;
    }

    FsearchDatabase2SelectionForeachContext ctx = {.func = func, .user_data = user_data};

    g_hash_table_foreach(view->folder_selection, selection_foreach, &ctx);
    g_hash_table_foreach(view->file_selection, selection_foreach, &ctx);
}

FsearchDatabase2 *
fsearch_database2_new(GFile *file) {
    return g_object_new(FSEARCH_TYPE_DATABASE2, "file", file, NULL);
}
