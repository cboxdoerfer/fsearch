#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-database-view"

#include "fsearch_database_view.h"
#include "fsearch_database.h"
#include "fsearch_database_search.h"
#include "fsearch_selection.h"
#include "fsearch_task.h"
#include "fsearch_task_ids.h"

#include <string.h>

// A DatabaseView provides a unique view into a registered database
// It provides:
// * filtering
// * searching
// * sorting
// * selection handling

struct FsearchDatabaseView {
    uint32_t id;

    FsearchDatabase *db;
    FsearchThreadPool *pool;

    FsearchQuery *query;

    DynamicArray *files;
    DynamicArray *folders;
    GHashTable *selection;

    FsearchDatabaseIndexType sort_order;
    GtkSortType sort_type;

    char *query_text;
    FsearchFilter *filter;
    FsearchFilterManager *filters;
    FsearchQueryFlags query_flags;
    uint32_t query_id;

    FsearchTaskQueue *task_queue;

    FsearchDatabaseViewNotifyFunc notify_func;
    gpointer notify_func_data;

    GMutex mutex;

    volatile int ref_count;
};

typedef struct {
    FsearchDatabaseView *view;
    FsearchQuery *query;
} FsearchSearchContext;

static void
db_view_search(FsearchDatabaseView *view, bool reset_selection);

static void
db_view_sort(FsearchDatabaseView *view, FsearchDatabaseIndexType sort_order, GtkSortType sort_type);

// Implementation

void
db_view_free(FsearchDatabaseView *view) {
    if (!view) {
        return;
    }

    db_view_lock(view);

    g_clear_pointer(&view->filter, fsearch_filter_unref);
    g_clear_pointer(&view->filters, fsearch_filter_manager_free);
    g_clear_pointer(&view->query_text, free);
    g_clear_pointer(&view->task_queue, fsearch_task_queue_free);
    g_clear_pointer(&view->query, fsearch_query_unref);
    g_clear_pointer(&view->selection, fsearch_selection_free);

    db_view_unlock(view);

    db_view_unregister_database(view);

    g_mutex_clear(&view->mutex);

    g_clear_pointer(&view, free);
}

FsearchDatabaseView *
db_view_ref(FsearchDatabaseView *view) {
    if (!view || g_atomic_int_get(&view->ref_count) <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&view->ref_count);
    return view;
}

void
db_view_unref(FsearchDatabaseView *view) {
    if (!view || g_atomic_int_get(&view->ref_count) <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&view->ref_count)) {
        g_clear_pointer(&view, db_view_free);
    }
}

void
db_view_unregister_database(FsearchDatabaseView *view) {
    g_assert(view);

    db_view_lock(view);
    if (view->selection) {
        fsearch_selection_unselect_all(view->selection);
    }
    g_clear_pointer(&view->files, darray_unref);
    g_clear_pointer(&view->folders, darray_unref);
    if (view->db) {
        db_unregister_view(view->db, view);
        g_clear_pointer(&view->db, db_unref);
    }
    view->pool = NULL;

    db_view_unlock(view);
}

static int32_t
cmp_entries_by_name_and_path(FsearchDatabaseEntry **a, FsearchDatabaseEntry **b, void *data) {
    const int res = db_entry_compare_entries_by_name(a, b);
    if (res == 0) {
        return db_entry_compare_entries_by_path(a, b);
    }
    return res;
}

static void
copy_selection(DynamicArray *old_entries, DynamicArray *new_entries, GHashTable *old_selection, GHashTable *new_selection) {
    if (!fsearch_selection_get_num_selected(old_selection) || darray_get_num_items(old_entries) == 0
        || darray_get_num_items(new_entries) == 0) {
        goto out;
    }

    uint32_t num_selected = 0;
    const uint32_t num_new_entries = darray_get_num_items(new_entries);

    for (uint32_t i = 0; i < darray_get_num_items(old_entries); ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(old_entries, i);
        if (fsearch_selection_is_selected(old_selection, entry)) {
            uint32_t found_idx = 0;
            // We have to perform a binary search to find the matching item in the new database.
            // That's not a huge issue for small selections, but when millions of items have been selected
            // in the old database, it can take quite a few seconds.
            // We should consider running this in a non-blocking way for the main thread.
            if (darray_binary_search_with_data(new_entries,
                                               entry,
                                               (DynamicArrayCompareDataFunc)cmp_entries_by_name_and_path,
                                               NULL,
                                               &found_idx)) {
                FsearchDatabaseEntry *entry_to_be_selected = darray_get_item(new_entries, found_idx);
                fsearch_selection_select(new_selection, entry_to_be_selected);
                num_selected++;

                if (num_selected >= num_new_entries) {
                    // This should almost never happen, but we've selected every element in new_entries,
                    // so we're done here. Even when there are still more selected items in old_entries.
                    break;
                }
            }
        }
    }
out:
    g_clear_pointer(&old_entries, darray_unref);
    g_clear_pointer(&new_entries, darray_unref);
}

static GHashTable *
migrate_selection(FsearchDatabase *db_old, FsearchDatabase *db_new, GHashTable *old_selection) {
    db_lock(db_old);
    db_lock(db_new);

    GHashTable *new_selection = fsearch_selection_new();
    copy_selection(db_get_files(db_old), db_get_files(db_new), old_selection, new_selection);
    copy_selection(db_get_folders(db_old), db_get_folders(db_new), old_selection, new_selection);

    db_unlock(db_old);
    db_unlock(db_new);

    return new_selection;
}

void
db_view_register_database(FsearchDatabaseView *view, FsearchDatabase *db) {
    g_assert(view);
    g_assert(db);

    if (db == view->db) {
        return;
    }

    if (!db_register_view(db, view)) {
        return;
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);
    GHashTable *new_selection = NULL;
    if (view->db) {
        db_view_lock(view);
        new_selection = migrate_selection(view->db, db, view->selection);
        db_view_unlock(view);
        g_debug("[db_view_register_database] old_selection_count: %d",
                fsearch_selection_get_num_selected(view->selection));
        g_debug("[db_view_register_database] new_selection_count: %d", fsearch_selection_get_num_selected(new_selection));
    }
    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_reset(timer);
    g_debug("[db_view_register_database] migrated selection in %f seconds", seconds);

    db_view_unregister_database(view);

    db_view_lock(view);

    view->db = db_ref(db);
    if (new_selection) {
        g_clear_pointer(&view->selection, fsearch_selection_free);
        view->selection = new_selection;
    }
    view->pool = db_get_thread_pool(db);
    view->files = db_get_files(db);
    view->folders = db_get_folders(db);

    db_view_search(view, false);
    db_view_sort(view, view->sort_order, view->sort_type);

    db_view_unlock(view);
}

FsearchDatabaseView *
db_view_new(const char *query_text,
            FsearchQueryFlags flags,
            FsearchFilter *filter,
            FsearchFilterManager *filters,
            FsearchDatabaseIndexType sort_order,
            GtkSortType sort_type,
            FsearchDatabaseViewNotifyFunc notify_func,
            gpointer notify_func_data) {
    FsearchDatabaseView *view = calloc(1, sizeof(struct FsearchDatabaseView));
    g_assert(view);

    view->task_queue = fsearch_task_queue_new("fsearch_db_task_queue");

    view->selection = fsearch_selection_new();

    view->query_text = strdup(query_text ? query_text : "");
    view->query_flags = flags;
    view->filter = fsearch_filter_ref(filter);
    view->filters = fsearch_filter_manager_copy(filters);
    view->sort_order = sort_order;
    view->sort_type = sort_type;

    view->notify_func = notify_func;
    view->notify_func_data = notify_func_data;

    view->ref_count = 1;

    static int id = 0;
    view->id = id++;

    g_mutex_init(&view->mutex);

    return view;
}

static void
db_view_search_task_cancelled(gpointer data) {
    FsearchSearchContext *ctx = data;

    if (ctx->view->notify_func) {
        ctx->view->notify_func(ctx->view, DATABASE_VIEW_NOTIFY_SEARCH_FINISHED, ctx->view->notify_func_data);
    }

    g_clear_pointer(&ctx->view, db_view_unref);
    g_clear_pointer(&ctx->query, fsearch_query_unref);
    g_clear_pointer(&ctx, free);
}

static void
db_view_search_task_finished(gpointer result, gpointer data) {
    FsearchSearchContext *ctx = data;

    db_view_lock(ctx->view);

    g_clear_pointer(&ctx->view->query, fsearch_query_unref);
    ctx->view->query = g_steal_pointer(&ctx->query);

    if (result) {
        DatabaseSearchResult *res = result;

        FsearchDatabase *db = db_search_result_get_db(res);
        if (ctx->view->db == db) {
            if (ctx->view->selection && ctx->view->query->reset_selection) {
                fsearch_selection_unselect_all(ctx->view->selection);
            }
            g_clear_pointer(&ctx->view->files, darray_unref);
            ctx->view->files = db_search_result_get_files(res);

            g_clear_pointer(&ctx->view->folders, darray_unref);
            ctx->view->folders = db_search_result_get_folders(res);

            ctx->view->sort_order = db_search_result_get_sort_type(res);
        }

        g_clear_pointer(&db, db_unref);
        g_clear_pointer(&res, db_search_result_unref);
    }

    db_view_unlock(ctx->view);

    if (ctx->view->notify_func) {
        ctx->view->notify_func(ctx->view, DATABASE_VIEW_NOTIFY_SEARCH_FINISHED, ctx->view->notify_func_data);
        ctx->view->notify_func(ctx->view, DATABASE_VIEW_NOTIFY_CONTENT_CHANGED, ctx->view->notify_func_data);
        ctx->view->notify_func(ctx->view, DATABASE_VIEW_NOTIFY_SELECTION_CHANGED, ctx->view->notify_func_data);
    }

    g_clear_pointer(&ctx->view, db_view_unref);
    g_clear_pointer(&ctx, free);
}

typedef struct {
    FsearchDatabaseView *view;
    FsearchDatabaseIndexType sort_order;
    GtkSortType sort_type;
} FsearchSortContext;

static void
sort_array(DynamicArray *array,
           DynamicArrayCompareDataFunc sort_func,
           GCancellable *cancellable,
           bool parallel_sort,
           void *data) {
    if (!array) {
        return;
    }
    if (parallel_sort) {
        darray_sort_multi_threaded(array, sort_func, cancellable, data);
    }
    else {
        darray_sort(array, sort_func, cancellable, data);
    }
}

static DynamicArrayCompareDataFunc
get_sort_func(FsearchDatabaseIndexType sort_order) {
    DynamicArrayCompareDataFunc func = NULL;
    switch (sort_order) {
    case DATABASE_INDEX_TYPE_NAME:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_name;
        break;
    case DATABASE_INDEX_TYPE_PATH:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_path;
        break;
    case DATABASE_INDEX_TYPE_SIZE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_size;
        break;
    case DATABASE_INDEX_TYPE_EXTENSION:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_extension;
        break;
    case DATABASE_INDEX_TYPE_FILETYPE:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_type;
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_modification_time;
        break;
    default:
        func = (DynamicArrayCompareDataFunc)db_entry_compare_entries_by_position;
    }
    return func;
}

static DynamicArray *
get_entries_sorted_from_reference_list(DynamicArray *old_list, DynamicArray *sorted_reference_list) {
    const uint32_t num_items = darray_get_num_items(old_list);
    DynamicArray *new = darray_new(num_items);
    for (uint32_t i = 0; i < num_items; ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(old_list, i);
        db_entry_set_mark(entry, 1);
    }
    uint32_t num_marked_found = 0;
    const uint32_t num_items_in_sorted_reference_list = darray_get_num_items(sorted_reference_list);
    for (uint32_t i = 0; i < num_items_in_sorted_reference_list && num_marked_found < num_items; ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(sorted_reference_list, i);
        if (db_entry_get_mark(entry)) {
            db_entry_set_mark(entry, 0);
            darray_add_item(new, entry);
            num_marked_found++;
        }
    }
    return new;
}

static bool
sort_order_affects_folders(FsearchDatabaseIndexType sort_order) {
    if (sort_order == DATABASE_INDEX_TYPE_EXTENSION || sort_order == DATABASE_INDEX_TYPE_FILETYPE) {
        // Folders are stored in a different array than files, so they all have the same sort_type and extension (none),
        // therefore we don't need to sort them in such cases.
        return false;
    }
    return true;
}

static gpointer
db_view_sort_task(gpointer data, GCancellable *cancellable) {
    FsearchSortContext *ctx = data;
    FsearchDatabaseView *view = ctx->view;

    if (!view->db) {
        return NULL;
    }

    DynamicArray *files = NULL;
    DynamicArray *folders = NULL;

    if (view->notify_func) {
        view->notify_func(view, DATABASE_VIEW_NOTIFY_SORT_STARTED, view->notify_func_data);
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    db_view_lock(view);
    db_lock(view->db);

    if (view->sort_order == ctx->sort_order) {
        // Sort order didn't change, use the old results
        files = darray_ref(view->files);
        folders = darray_ref(view->folders);
        goto out;
    }

    if (db_has_entries_sorted_by_type(view->db, ctx->sort_order)) {
        if (!view->query || fsearch_query_matches_everything(view->query)) {
            // We're matching everything, and we have the entries already sorted in our index.
            // So we can just return references to the sorted indices.
            files = db_get_files_sorted(view->db, ctx->sort_order);
            folders = db_get_folders_sorted(view->db, ctx->sort_order);
        }
        else {
            // Another fast path. First we mark all entries we have currently in the view, then we walk the sorted
            // index in order and add all marked entries to a new array.
            DynamicArray *sorted_folders = db_get_folders_sorted(view->db, ctx->sort_order);
            DynamicArray *sorted_files = db_get_files_sorted(view->db, ctx->sort_order);
            folders = get_entries_sorted_from_reference_list(view->folders, sorted_folders);
            files = get_entries_sorted_from_reference_list(view->files, sorted_files);
            g_clear_pointer(&sorted_folders, darray_unref);
            g_clear_pointer(&sorted_files, darray_unref);
        }
        goto out;
    }
    else {
        files = darray_copy(view->files);
        folders = darray_copy(view->folders);
    }

    DynamicArrayCompareDataFunc func = get_sort_func(ctx->sort_order);
    bool parallel_sort = true;

    FsearchDatabaseEntryCompareContext *comp_ctx = NULL;
    if (ctx->sort_order == DATABASE_INDEX_TYPE_FILETYPE) {
        // Sorting by type can be really slow, because it accesses the filesystem to determine the type of files
        // To mitigate that issue to a certain degree we cache the filetype for each file
        // To avoid duplicating the filetype in memory for each file, we also store each filetype only once in
        // a separate hash table.
        // We also disable parallel sorting.
        comp_ctx = calloc(1, sizeof(FsearchDatabaseEntryCompareContext));
        comp_ctx->file_type_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        comp_ctx->entry_to_file_type_table = g_hash_table_new(NULL, NULL);
        parallel_sort = false;
    }

    g_debug("[sort] started: %d", ctx->sort_order);

    db_view_unlock(view);
    if (sort_order_affects_folders(ctx->sort_order)) {
        sort_array(folders, func, cancellable, parallel_sort, comp_ctx);
    }
    sort_array(files, func, cancellable, parallel_sort, comp_ctx);
    db_view_lock(view);

    if (comp_ctx) {
        g_clear_pointer(&comp_ctx->entry_to_file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx->file_type_table, g_hash_table_unref);
        g_clear_pointer(&comp_ctx, free);
    }

out:
    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);

    if (!g_cancellable_is_cancelled(cancellable)) {
        g_clear_pointer(&view->folders, darray_unref);
        g_clear_pointer(&view->files, darray_unref);
        view->folders = g_steal_pointer(&folders);
        view->files = g_steal_pointer(&files);
        view->sort_order = ctx->sort_order;
        view->sort_type = ctx->sort_type;
        g_debug("[sort] finished in %2.fms", seconds * 1000);
    }
    else {
        g_clear_pointer(&folders, darray_unref);
        g_clear_pointer(&files, darray_unref);
        g_debug("[sort] cancelled after %2.fms", seconds * 1000);
    }
    db_unlock(view->db);
    db_view_unlock(view);

    if (view->notify_func) {
        view->notify_func(view, DATABASE_VIEW_NOTIFY_SORT_FINISHED, view->notify_func_data);
    }

    return NULL;
}

static void
db_view_sort_task_cancelled(gpointer data) {
    FsearchSortContext *ctx = data;
    g_clear_pointer(&ctx, free);
}

static void
db_view_sort_task_finished(gpointer result, gpointer data) {
    db_view_sort_task_cancelled(data);
}

static void
db_view_sort(FsearchDatabaseView *view, FsearchDatabaseIndexType sort_order, GtkSortType sort_type) {
    FsearchSortContext *ctx = calloc(1, sizeof(FsearchSortContext));
    g_assert(ctx);

    ctx->view = view;
    ctx->sort_order = sort_order;
    ctx->sort_type = sort_type;

    fsearch_task_queue(view->task_queue,
                       FSEARCH_TASK_ID_SORT,
                       db_view_sort_task,
                       db_view_sort_task_finished,
                       db_view_sort_task_cancelled,
                       FSEARCH_TASK_CLEAR_SAME_ID,
                       g_steal_pointer(&ctx));
}

static gpointer
db_view_search_task(gpointer data, GCancellable *cancellable) {
    FsearchSearchContext *ctx = data;

    if (ctx->view->notify_func) {
        ctx->view->notify_func(ctx->view, DATABASE_VIEW_NOTIFY_SEARCH_STARTED, ctx->view->notify_func_data);
    }

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    DatabaseSearchResult *result = NULL;
    if (fsearch_query_matches_everything(ctx->query)) {
        result = db_search_empty(ctx->query);
    }
    else {
        result = db_search(ctx->query, cancellable);
    }

    const char *debug_message = NULL;
    const double seconds = g_timer_elapsed(timer, NULL);
    if (!g_cancellable_is_cancelled(cancellable)) {
        debug_message = "[%s] finished in %.2f ms";
    }
    else {
        debug_message = "[%s] aborted after %.2f ms";
    }
    g_timer_stop(timer);

    g_debug(debug_message, ctx->query->query_id, seconds * 1000);

    return result;
}

static void
db_view_search(FsearchDatabaseView *view, bool reset_selection) {
    if (!view->db || !view->pool) {
        return;
    }

    FsearchSearchContext *ctx = calloc(1, sizeof(FsearchSearchContext));
    g_assert(ctx);

    ctx->view = db_view_ref(view);

    g_autoptr(GString) query_id = g_string_new(NULL);
    g_string_printf(query_id, "query:%02d.%04d", view->id, view->query_id++);
    ctx->query = fsearch_query_new(view->query_text,
                                   view->db,
                                   view->sort_order,
                                   view->filter,
                                   view->filters,
                                   view->pool,
                                   view->query_flags,
                                   query_id->str,
                                   reset_selection);
    g_assert(ctx->query);

    fsearch_task_queue(view->task_queue,
                       FSEARCH_TASK_ID_SEARCH,
                       db_view_search_task,
                       db_view_search_task_finished,
                       db_view_search_task_cancelled,
                       FSEARCH_TASK_CLEAR_SAME_ID,
                       g_steal_pointer(&ctx));
}

void
db_view_set_filters(FsearchDatabaseView *view, FsearchFilterManager *filters) {
    if (!view) {
        return;
    }
    db_view_lock(view);

    g_clear_pointer(&view->filters, fsearch_filter_manager_free);
    view->filters = fsearch_filter_manager_copy(filters);

    db_view_search(view, true);

    db_view_unlock(view);
}

void
db_view_set_filter(FsearchDatabaseView *view, FsearchFilter *filter) {
    if (!view) {
        return;
    }
    db_view_lock(view);

    g_clear_pointer(&view->filter, fsearch_filter_unref);
    view->filter = fsearch_filter_ref(filter);

    db_view_search(view, true);

    db_view_unlock(view);
}

void
db_view_cancel_current_task(FsearchDatabaseView *view) {
    if (!view) {
        return;
    }
    fsearch_task_queue_cancel_current(view->task_queue);
}

FsearchQuery *
db_view_get_query(FsearchDatabaseView *view) {
    return fsearch_query_ref(view->query);
}

FsearchQueryFlags
db_view_get_query_flags(FsearchDatabaseView *view) {
    return view->query_flags;
}

void
db_view_set_query_flags(FsearchDatabaseView *view, FsearchQueryFlags query_flags) {
    if (!view) {
        return;
    }
    db_view_lock(view);
    view->query_flags = query_flags;

    db_view_search(view, true);

    db_view_unlock(view);
}

void
db_view_set_query_text(FsearchDatabaseView *view, const char *query_text) {
    if (!view) {
        return;
    }
    db_view_lock(view);

    g_clear_pointer(&view->query_text, free);
    view->query_text = strdup(query_text ? query_text : "");

    db_view_search(view, true);

    db_view_unlock(view);
}

void
db_view_set_sort_order(FsearchDatabaseView *view, FsearchDatabaseIndexType sort_order, GtkSortType sort_type) {
    if (!view) {
        return;
    }
    db_view_lock(view);
    if (view->sort_order != sort_order || view->sort_type != sort_type) {
        db_view_sort(view, sort_order, sort_type);
    }
    db_view_unlock(view);
}

uint32_t
db_view_get_num_folders(FsearchDatabaseView *view) {
    g_assert(view);
    return view->folders ? darray_get_num_items(view->folders) : 0;
}

uint32_t
db_view_get_num_files(FsearchDatabaseView *view) {
    g_assert(view);
    return view->files ? darray_get_num_items(view->files) : 0;
}

uint32_t
db_view_get_num_entries(FsearchDatabaseView *view) {
    g_assert(view);
    return db_view_get_num_folders(view) + db_view_get_num_files(view);
}

GtkSortType
db_view_get_sort_type(FsearchDatabaseView *view) {
    g_assert(view);
    return view->sort_type;
}

FsearchDatabaseIndexType
db_view_get_sort_order(FsearchDatabaseView *view) {
    g_assert(view);
    return view->sort_order;
}

static FsearchDatabaseEntry *
db_view_get_entry_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    const uint32_t num_folders = darray_get_num_items(view->folders);
    if (idx < num_folders) {
        return darray_get_item(view->folders, idx);
    }
    idx -= num_folders;
    const uint32_t num_files = darray_get_num_items(view->files);
    if (idx < num_files) {
        return darray_get_item(view->files, idx);
    }
    return NULL;
}

GString *
db_view_entry_get_path_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    GString *res = NULL;
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_path(entry);
    }
    return res;
}

GString *
db_view_entry_get_path_full_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? db_entry_get_path_full(entry) : NULL;
}

void
db_view_entry_append_path_for_idx(FsearchDatabaseView *view, uint32_t idx, GString *str) {
    g_assert(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        db_entry_append_path(entry, str);
    }
}

time_t
db_view_entry_get_mtime_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? db_entry_get_mtime(entry) : 0;
}

off_t
db_view_entry_get_size_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? db_entry_get_size(entry) : 0;
}

char *
db_view_entry_get_extension_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (!entry) {
        return NULL;
    }
    const char *ext = db_entry_get_extension(entry);
    return g_strdup(ext ? ext : "");
}

GString *
db_view_entry_get_name_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? g_string_new(db_entry_get_name_raw_for_display(entry)) : NULL;
}

FsearchDatabaseEntry *
db_view_entry_get_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    return db_view_get_entry_for_idx(view, idx);
}

GString *
db_view_entry_get_name_raw_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? g_string_new(db_entry_get_name_raw(entry)) : NULL;
}

int32_t
db_view_entry_get_parent_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (!entry) {
        return -1;
    }
    FsearchDatabaseEntryFolder *folder = db_entry_get_parent(entry);
    return folder ? (int32_t)db_entry_get_idx((FsearchDatabaseEntry *)folder) : -1;
}

FsearchDatabaseEntryType
db_view_entry_get_type_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);

    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    return entry ? db_entry_get_type(entry) : DATABASE_ENTRY_TYPE_NONE;
}

static void
notify_selection_changed(FsearchDatabaseView *view) {
    if (view->notify_func) {
        view->notify_func(view, DATABASE_VIEW_NOTIFY_SELECTION_CHANGED, view->notify_func_data);
    }
}

void
db_view_select_toggle(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        fsearch_selection_select_toggle(view->selection, entry);
    }
    db_view_unlock(view);

    notify_selection_changed(view);
}

void
db_view_select(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        fsearch_selection_select(view->selection, entry);
    }
    db_view_unlock(view);

    notify_selection_changed(view);
}

bool
db_view_is_selected(FsearchDatabaseView *view, uint32_t idx) {
    g_assert(view);
    bool is_selected = false;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        is_selected = fsearch_selection_is_selected(view->selection, entry);
    }
    db_view_unlock(view);
    return is_selected;
}

void
db_view_toggle_range(FsearchDatabaseView *view, uint32_t start_idx, uint32_t end_idx) {
    g_assert(view);
    db_view_lock(view);
    for (uint32_t i = start_idx; i <= end_idx; i++) {
        FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, i);
        if (entry) {
            fsearch_selection_select_toggle(view->selection, entry);
        }
    }
    db_view_unlock(view);

    notify_selection_changed(view);
}

void
db_view_select_range(FsearchDatabaseView *view, uint32_t start_idx, uint32_t end_idx) {
    g_assert(view);
    db_view_lock(view);
    for (uint32_t i = start_idx; i <= end_idx; i++) {
        FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, i);
        if (entry) {
            fsearch_selection_select(view->selection, entry);
        }
    }
    db_view_unlock(view);

    notify_selection_changed(view);
}

void
db_view_select_all(FsearchDatabaseView *view) {
    g_assert(view);
    db_view_lock(view);
    fsearch_selection_select_all(view->selection, view->folders);
    fsearch_selection_select_all(view->selection, view->files);
    db_view_unlock(view);

    notify_selection_changed(view);
}

void
db_view_unselect_all(FsearchDatabaseView *view) {
    g_assert(view);
    db_view_lock(view);
    fsearch_selection_unselect_all(view->selection);
    db_view_unlock(view);

    notify_selection_changed(view);
}

void
db_view_invert_selection(FsearchDatabaseView *view) {
    g_assert(view);
    db_view_lock(view);
    fsearch_selection_invert(view->selection, view->folders);
    fsearch_selection_invert(view->selection, view->files);
    db_view_unlock(view);

    notify_selection_changed(view);
}

uint32_t
db_view_get_num_selected(FsearchDatabaseView *view) {
    g_assert(view);
    db_view_lock(view);
    const uint32_t num_selected = fsearch_selection_get_num_selected(view->selection);
    db_view_unlock(view);
    return num_selected;
}

void
db_view_selection_for_each(FsearchDatabaseView *view, GHFunc func, gpointer user_data) {
    g_assert(view);
    db_view_lock(view);
    g_hash_table_foreach(view->selection, func, user_data);
    db_view_unlock(view);
}

void
db_view_unlock(FsearchDatabaseView *view) {
    g_mutex_unlock(&view->mutex);
}

void
db_view_lock(FsearchDatabaseView *view) {
    g_mutex_lock(&view->mutex);
}
