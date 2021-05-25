#define _GNU_SOURCE

#define G_LOG_DOMAIN "fsearch-database-view"

#include "fsearch_database_view.h"
#include "fsearch_database.h"
#include "fsearch_database_search.h"
#include "fsearch_selection.h"
#include "fsearch_task.h"

#include <assert.h>
#include <string.h>

struct FsearchDatabaseView {
    uint32_t id;

    FsearchDatabase *db;
    FsearchThreadPool *pool;

    FsearchQuery *query;

    DynamicArray *files;
    DynamicArray *folders;
    GHashTable *selection;

    FsearchDatabaseIndexType sort_order;

    char *query_text;
    FsearchFilter *filter;
    FsearchQueryFlags query_flags;
    uint32_t query_id;

    FsearchTaskQueue *task_queue;

    FsearchDatabaseViewNotifyFunc view_changed_func;
    FsearchDatabaseViewNotifyFunc selection_changed_func;
    FsearchDatabaseViewNotifyFunc search_started_func;
    FsearchDatabaseViewNotifyFunc search_finished_func;
    FsearchDatabaseViewNotifyFunc sort_started_func;
    FsearchDatabaseViewNotifyFunc sort_finished_func;

    gpointer user_data;

    GMutex mutex;

    volatile int ref_count;
};

static void
db_view_update_entries(FsearchDatabaseView *view);

static void
db_view_update_sort(FsearchDatabaseView *view);

// Implementation

void
db_view_free(FsearchDatabaseView *view) {
    if (!view) {
        return;
    }

    db_view_lock(view);

    if (view->filter) {
        fsearch_filter_unref(view->filter);
        view->filter = NULL;
    }

    if (view->query_text) {
        free(view->query_text);
        view->query_text = NULL;
    }

    if (view->task_queue) {
        fsearch_task_queue_free(view->task_queue);
        view->task_queue = NULL;
    }

    if (view->query) {
        fsearch_query_unref(view->query);
        view->query = NULL;
    }

    if (view->selection) {
        fsearch_selection_free(view->selection);
        view->selection = NULL;
    }

    db_view_unlock(view);

    db_view_unregister(view);

    g_mutex_clear(&view->mutex);

    free(view);
    view = NULL;
}

FsearchDatabaseView *
db_view_ref(FsearchDatabaseView *view) {
    if (!view || view->ref_count <= 0) {
        return NULL;
    }
    g_atomic_int_inc(&view->ref_count);
    return view;
}

void
db_view_unref(FsearchDatabaseView *view) {
    if (!view || view->ref_count <= 0) {
        return;
    }
    if (g_atomic_int_dec_and_test(&view->ref_count)) {
        db_view_free(view);
        view = NULL;
    }
}

void
db_view_unregister(FsearchDatabaseView *view) {
    assert(view != NULL);

    db_view_lock(view);
    if (view->selection) {
        fsearch_selection_unselect_all(view->selection);
    }
    if (view->files) {
        darray_unref(view->files);
        view->files = NULL;
    }
    if (view->folders) {
        darray_unref(view->folders);
        view->folders = NULL;
    }
    if (view->db) {
        db_unregister_view(view->db, view);
        db_unref(view->db);
        view->db = NULL;
    }
    view->pool = NULL;

    db_view_unlock(view);
}

void
db_view_register(FsearchDatabase *db, FsearchDatabaseView *view) {
    assert(view != NULL);
    assert(db != NULL);

    if (!db_register_view(db, view)) {
        return;
    }

    db_view_lock(view);

    view->db = db_ref(db);
    view->pool = db_get_thread_pool(db);
    view->files = db_get_files(db);
    view->folders = db_get_folders(db);

    db_view_update_entries(view);
    db_view_update_sort(view);

    db_view_unlock(view);
}

FsearchDatabaseEntry *
db_view_get_entry(FsearchDatabaseView *view, uint32_t idx) {
    uint32_t num_entries = db_view_get_num_entries(view);
    if (idx >= num_entries) {
        return NULL;
    }
    uint32_t num_folders = db_view_get_num_folders(view);
    if (idx < num_folders) {
        return darray_get_item(view->folders, idx);
    }
    else {
        return darray_get_item(view->files, idx - num_folders);
    }
}

FsearchDatabaseView *
db_view_new(const char *query_text,
            FsearchQueryFlags flags,
            FsearchFilter *filter,
            FsearchDatabaseIndexType sort_order,
            FsearchDatabaseViewNotifyFunc view_changed_func,
            FsearchDatabaseViewNotifyFunc selection_changed_func,
            FsearchDatabaseViewNotifyFunc search_started_func,
            FsearchDatabaseViewNotifyFunc search_finished_func,
            FsearchDatabaseViewNotifyFunc sort_started_func,
            FsearchDatabaseViewNotifyFunc sort_finished_func,
            gpointer user_data) {
    FsearchDatabaseView *view = calloc(1, sizeof(struct FsearchDatabaseView));
    assert(view != NULL);

    view->task_queue = fsearch_task_queue_new("fsearch_db_task_queue");

    view->selection = fsearch_selection_new();

    view->query_text = strdup(query_text ? query_text : "");
    view->query_flags = flags;
    view->filter = fsearch_filter_ref(filter);
    view->sort_order = sort_order;

    view->view_changed_func = view_changed_func;
    view->selection_changed_func = selection_changed_func;
    view->search_started_func = search_started_func;
    view->search_finished_func = search_finished_func;
    view->sort_started_func = sort_started_func;
    view->sort_finished_func = sort_finished_func;
    view->user_data = user_data;

    view->ref_count = 1;

    static int id = 0;
    view->id = id++;

    g_mutex_init(&view->mutex);

    return view;
}

static void
db_view_task_query_cancelled(FsearchTask *task, gpointer data) {
    FsearchQuery *query = data;
    FsearchDatabaseView *view = query->data;

    if (view->search_finished_func) {
        view->search_finished_func(view, view->user_data);
    }

    db_view_unref(view);
    view = NULL;

    fsearch_query_unref(query);
    query = NULL;

    fsearch_task_free(task);
    task = NULL;
}

static void
db_view_task_query_finished(FsearchTask *task, gpointer result, gpointer data) {
    FsearchQuery *query = data;
    FsearchDatabaseView *view = query->data;

    db_view_lock(view);

    if (view->query) {
        fsearch_query_unref(view->query);
    }
    view->query = query;

    if (result) {
        DatabaseSearchResult *res = result;

        if (view->selection) {
            fsearch_selection_unselect_all(view->selection);
        }
        if (view->files) {
            darray_unref(view->files);
        }
        view->files = db_search_result_get_files(res);

        if (view->folders) {
            darray_unref(view->folders);
        }
        view->folders = db_search_result_get_folders(res);

        db_search_result_unref(res);
        res = NULL;
    }

    db_view_unlock(view);

    if (view->search_finished_func) {
        view->search_finished_func(view, view->user_data);
    }
    if (view->view_changed_func) {
        view->view_changed_func(view, view->user_data);
    }
    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }

    db_view_unref(view);
    view = NULL;

    fsearch_task_free(task);
    task = NULL;
}

typedef struct {
    FsearchDatabaseView *view;
    DynamicArrayCompareDataFunc compare_func;
    bool parallel_sort;
} FsearchSortContext;

static void
db_sort_array(DynamicArray *array, DynamicArrayCompareDataFunc sort_func, bool parallel_sort) {
    if (!array) {
        return;
    }
    if (parallel_sort) {
        darray_sort_multi_threaded(array, (DynamicArrayCompareFunc)sort_func);
    }
    else {
        darray_sort(array, (DynamicArrayCompareFunc)sort_func);
    }
}

static gpointer
db_sort_task(gpointer data, GCancellable *cancellable) {
    FsearchSortContext *ctx = data;
    FsearchDatabaseView *view = ctx->view;

    if (view->sort_started_func) {
        view->sort_started_func(view, view->user_data);
    }

    GTimer *timer = g_timer_new();
    g_timer_start(timer);

    db_sort_array(view->folders, ctx->compare_func, ctx->parallel_sort);
    db_sort_array(view->files, ctx->compare_func, ctx->parallel_sort);

    g_timer_stop(timer);
    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_destroy(timer);
    timer = NULL;

    g_debug("[sort] finished in %2.fms", seconds * 1000);

    if (view->sort_finished_func) {
        view->sort_finished_func(view, view->user_data);
    }

    return NULL;
}

static void
db_sort_task_cancelled(FsearchTask *task, gpointer data) {
    FsearchSortContext *ctx = data;

    free(ctx);
    ctx = NULL;

    fsearch_task_free(task);
    task = NULL;
}

static void
db_sort_task_finished(FsearchTask *task, gpointer result, gpointer data) {
    db_sort_task_cancelled(task, data);
}

static void
db_view_update_sort(FsearchDatabaseView *view) {
    if (!view->db) {
        return;
    }

    if (!view->query || fsearch_query_matches_everything(view->query)) {
        // we're matching everything, so if the database has the entries already sorted we don't need
        // to sort again
        darray_unref(view->files);
        darray_unref(view->folders);

        if (db_has_entries_sorted_by_type(view->db, view->sort_order)) {
            if (view->sort_started_func) {
                view->sort_started_func(view, view->user_data);
            }
            view->files = db_get_files_sorted(view->db, view->sort_order);
            view->folders = db_get_folders_sorted(view->db, view->sort_order);
            if (view->sort_finished_func) {
                view->sort_finished_func(view, view->user_data);
            }
            return;
        }

        view->files = db_get_files_copy(view->db);
        view->folders = db_get_folders_copy(view->db);
    }

    bool parallel_sort = true;

    g_debug("[sort] started: %d", view->sort_order);
    DynamicArrayCompareFunc func = NULL;
    switch (view->sort_order) {
    case DATABASE_INDEX_TYPE_NAME:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_name;
        break;
    case DATABASE_INDEX_TYPE_PATH:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_path;
        break;
    case DATABASE_INDEX_TYPE_SIZE:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_size;
        break;
    case DATABASE_INDEX_TYPE_FILETYPE:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_type;
        parallel_sort = false;
        break;
    case DATABASE_INDEX_TYPE_MODIFICATION_TIME:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_modification_time;
        break;
    default:
        func = (DynamicArrayCompareFunc)db_entry_compare_entries_by_position;
    }

    FsearchSortContext *ctx = calloc(1, sizeof(FsearchSortContext));
    g_assert(ctx != NULL);

    ctx->view = view;
    ctx->compare_func = (DynamicArrayCompareDataFunc)func;
    ctx->parallel_sort = parallel_sort;

    FsearchTask *task = fsearch_task_new(1, db_sort_task, db_sort_task_finished, db_sort_task_cancelled, ctx);
    fsearch_task_queue(view->task_queue, task, FSEARCH_TASK_CLEAR_SAME_ID);
}

static void
db_view_update_entries(FsearchDatabaseView *view) {
    if (!view->db || !view->pool) {
        return;
    }

    if (view->search_started_func) {
        view->search_started_func(view, view->user_data);
    }

    DynamicArray *files = NULL;
    DynamicArray *folders = NULL;

    if (db_has_entries_sorted_by_type(view->db, view->sort_order)) {
        files = db_get_files_sorted(view->db, view->sort_order);
        folders = db_get_folders_sorted(view->db, view->sort_order);
    }
    else {
        files = db_get_files(view->db);
        folders = db_get_folders(view->db);
        view->sort_order = DATABASE_INDEX_TYPE_NAME;
    }

    FsearchQuery *q = fsearch_query_new(view->query_text,
                                        files,
                                        folders,
                                        view->sort_order,
                                        view->filter,
                                        view->pool,
                                        view->query_flags,
                                        view->query_id++,
                                        view->id,
                                        db_view_ref(view));

    db_search_queue(view->task_queue, q, db_view_task_query_finished, db_view_task_query_cancelled);
}

void
db_view_set_filter(FsearchDatabaseView *view, FsearchFilter *filter) {
    if (!view) {
        return;
    }
    db_view_lock(view);
    if (view->filter) {
        fsearch_filter_unref(view->filter);
    }
    view->filter = fsearch_filter_ref(filter);

    db_view_update_entries(view);

    db_view_unlock(view);
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

    db_view_update_entries(view);

    db_view_unlock(view);
}

void
db_view_set_query_text(FsearchDatabaseView *view, const char *query_text) {
    if (!view) {
        return;
    }
    db_view_lock(view);
    if (view->query_text) {
        free(view->query_text);
    }
    view->query_text = strdup(query_text ? query_text : "");

    db_view_update_entries(view);

    db_view_unlock(view);
}

void
db_view_set_sort_order(FsearchDatabaseView *view, FsearchDatabaseIndexType sort_order) {
    if (!view) {
        return;
    }
    db_view_lock(view);
    bool needs_update = view->sort_order != sort_order;
    view->sort_order = sort_order;

    if (needs_update) {
        db_view_update_sort(view);
    }

    db_view_unlock(view);
}

uint32_t
db_view_get_num_folders(FsearchDatabaseView *view) {
    assert(view != NULL);
    return view->folders ? darray_get_num_items(view->folders) : 0;
}

uint32_t
db_view_get_num_files(FsearchDatabaseView *view) {
    assert(view != NULL);
    return view->files ? darray_get_num_items(view->files) : 0;
}

uint32_t
db_view_get_num_entries(FsearchDatabaseView *view) {
    assert(view != NULL);
    return db_view_get_num_folders(view) + db_view_get_num_files(view);
}

FsearchDatabaseIndexType
db_view_get_sort_order(FsearchDatabaseView *view) {
    assert(view != NULL);
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
    assert(view != NULL);

    GString *res = NULL;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_path(entry);
    }
    db_view_unlock(view);
    return res;
}

GString *
db_view_entry_get_path_full_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    GString *res = NULL;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_path_full(entry);
    }
    db_view_unlock(view);
    return res;
}

void
db_view_entry_append_path_for_idx(FsearchDatabaseView *view, uint32_t idx, GString *str) {
    assert(view != NULL);

    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        db_entry_append_path(entry, str);
    }
    db_view_unlock(view);
}

time_t
db_view_entry_get_mtime_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    time_t res = 0;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_mtime(entry);
    }
    db_view_unlock(view);
    return res;
}

off_t
db_view_entry_get_size_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    off_t res = 0;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_size(entry);
    }
    db_view_unlock(view);
    return res;
}

GString *
db_view_entry_get_name_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    GString *res = NULL;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = g_string_new(db_entry_get_name(entry));
    }
    db_view_unlock(view);
    return res;
}

GString *
db_view_entry_get_name_raw_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    GString *res = NULL;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = g_string_new(db_entry_get_name_raw(entry));
    }
    db_view_unlock(view);
    return res;
}

int32_t
db_view_entry_get_parent_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    int32_t res = -1;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        FsearchDatabaseEntryFolder *folder = db_entry_get_parent(entry);
        res = folder ? (int32_t)db_entry_get_idx((FsearchDatabaseEntry *)folder) : -1;
    }
    db_view_unlock(view);
    return res;
}

FsearchDatabaseEntryType
db_view_entry_get_type_for_idx(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);

    FsearchDatabaseEntryType res = DATABASE_ENTRY_TYPE_NONE;
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        res = db_entry_get_type(entry);
    }
    db_view_unlock(view);
    return res;
}

void
db_view_select_toggle(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        fsearch_selection_select_toggle(view->selection, entry);
    }
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

void
db_view_select(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);
    db_view_lock(view);
    FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, idx);
    if (entry) {
        fsearch_selection_select(view->selection, entry);
    }
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

bool
db_view_is_selected(FsearchDatabaseView *view, uint32_t idx) {
    assert(view != NULL);
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
db_view_select_range(FsearchDatabaseView *view, uint32_t start_idx, uint32_t end_idx) {
    assert(view != NULL);
    db_view_lock(view);
    for (uint32_t i = start_idx; i <= end_idx; i++) {
        FsearchDatabaseEntry *entry = db_view_get_entry_for_idx(view, i);
        fsearch_selection_select(view->selection, entry);
    }
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

void
db_view_select_all(FsearchDatabaseView *view) {
    assert(view != NULL);
    db_view_lock(view);
    fsearch_selection_select_all(view->selection, view->folders);
    fsearch_selection_select_all(view->selection, view->files);
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

void
db_view_unselect_all(FsearchDatabaseView *view) {
    assert(view != NULL);
    db_view_lock(view);
    fsearch_selection_unselect_all(view->selection);
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

void
db_view_invert_selection(FsearchDatabaseView *view) {
    assert(view != NULL);
    db_view_lock(view);
    fsearch_selection_invert(view->selection, view->folders);
    fsearch_selection_invert(view->selection, view->files);
    db_view_unlock(view);

    if (view->selection_changed_func) {
        view->selection_changed_func(view, view->user_data);
    }
}

uint32_t
db_view_get_num_selected(FsearchDatabaseView *view) {
    assert(view != NULL);
    db_view_lock(view);
    const uint32_t num_selected = fsearch_selection_get_num_selected(view->selection);
    db_view_unlock(view);
    return num_selected;
}

void
db_view_selection_for_each(FsearchDatabaseView *view, GHFunc func, gpointer user_data) {
    assert(view != NULL);
    g_hash_table_foreach(view->selection, func, user_data);
}

void
db_view_unlock(FsearchDatabaseView *view) {
    g_mutex_unlock(&view->mutex);
}

void
db_view_lock(FsearchDatabaseView *view) {
    g_mutex_lock(&view->mutex);
}
