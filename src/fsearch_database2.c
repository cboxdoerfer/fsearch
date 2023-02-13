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

    FsearchDatabaseIndex *index;
    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    FsearchDatabaseIndexFlags flags;

    GMutex mutex;
};

typedef struct FsearchDatabaseSearchView {
    FsearchQuery *query;
    DynamicArray *files;
    DynamicArray *folders;
    GtkSortType sort_type;
    FsearchDatabaseIndexType sort_order;
    GHashTable *selection;
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
    NUM_EVENTS,
} FsearchDatabase2EventType;

G_DEFINE_TYPE(FsearchDatabase2, fsearch_database2, G_TYPE_OBJECT)

enum { PROP_0, PROP_FILE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];
static guint signals[NUM_EVENTS];

typedef struct FsearchSignalEmitContext {
    FsearchDatabase2 *db;
    FsearchDatabase2EventType type;
    gpointer data;
    gpointer data2;
} FsearchSignalEmitContext;

static void
search_view_free(FsearchDatabaseSearchView *view) {
    g_return_if_fail(view);
    g_clear_pointer(&view->query, fsearch_query_unref);
    g_clear_pointer(&view->files, darray_unref);
    g_clear_pointer(&view->folders, darray_unref);
    g_clear_pointer(&view->selection, fsearch_selection_free);
    g_clear_pointer(&view, free);
}

static FsearchDatabaseSearchView *
search_view_new(FsearchQuery *query,
                DynamicArray *files,
                DynamicArray *folders,
                GHashTable *old_selection,
                FsearchDatabaseIndexType sort_order,
                GtkSortType sort_type) {
    FsearchDatabaseSearchView *view = calloc(1, sizeof(FsearchDatabaseSearchView));
    g_assert(view);
    view->query = fsearch_query_ref(query);
    view->files = darray_ref(files);
    view->folders = darray_ref(folders);
    view->sort_order = sort_order;
    view->sort_type = sort_type;
    view->selection = fsearch_selection_new();
    return view;
}

static void
wakeup_work_queue(FsearchDatabase2 *self) {
    g_async_queue_push(self->work_trigger_queue, GUINT_TO_POINTER(1));
}

static void
signal_emit_context_free(FsearchSignalEmitContext *ctx) {
    g_clear_object(&ctx->db);
    g_clear_pointer(&ctx, free);
}

static FsearchSignalEmitContext *
signal_emit_context_new(FsearchDatabase2 *db, FsearchDatabase2EventType type, gpointer data, gpointer data2) {
    FsearchSignalEmitContext *ctx = calloc(1, sizeof(FsearchSignalEmitContext));
    g_assert(ctx != NULL);

    ctx->db = g_object_ref(db);
    ctx->type = type;
    ctx->data = data;
    ctx->data2 = data2;
    return ctx;
}

static gboolean
emit_signal_cb(gpointer user_data) {
    FsearchSignalEmitContext *ctx = user_data;
    g_signal_emit(ctx->db, signals[ctx->type], 0, ctx->data);

    g_clear_pointer(&ctx, signal_emit_context_free);

    return G_SOURCE_REMOVE;
}

static gboolean
emit_item_info_signal_cb(gpointer user_data) {
    FsearchSignalEmitContext *ctx = user_data;
    g_signal_emit(ctx->db, signals[ctx->type], 0, ctx->data, ctx->data2);

    g_clear_pointer(&ctx->data2, fsearch_database_entry_info_unref);
    g_clear_pointer(&ctx, signal_emit_context_free);

    return G_SOURCE_REMOVE;
}

static gboolean
emit_search_finished_signal_cb(gpointer user_data) {
    FsearchSignalEmitContext *ctx = user_data;
    g_signal_emit(ctx->db, signals[ctx->type], 0, ctx->data, ctx->data2);

    g_clear_pointer(&ctx->data2, fsearch_database_search_info_unref);
    g_clear_pointer(&ctx, signal_emit_context_free);

    return G_SOURCE_REMOVE;
}

static void
emit_signal(FsearchDatabase2 *self, FsearchDatabase2EventType type, gpointer data) {
    g_idle_add(emit_signal_cb, signal_emit_context_new(self, type, data, NULL));
}

static void
emit_item_info_ready_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseEntryInfo *info) {
    g_idle_add(emit_item_info_signal_cb,
               signal_emit_context_new(self,
                                       EVENT_ITEM_INFO_READY,
                                       GUINT_TO_POINTER(id),
                                       info ? fsearch_database_entry_info_ref(info) : NULL));
}

static void
emit_search_finished_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseSearchInfo *info) {
    g_idle_add(emit_search_finished_signal_cb,
               signal_emit_context_new(self, EVENT_SEARCH_FINISHED, GUINT_TO_POINTER(id), info));
}

static void
emit_sort_finished_signal(FsearchDatabase2 *self, guint id, FsearchDatabaseSearchInfo *info) {
    g_idle_add(emit_search_finished_signal_cb,
               signal_emit_context_new(self, EVENT_SORT_FINISHED, GUINT_TO_POINTER(id), info));
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

static GFile *
get_default_database_file() {
    return g_file_new_build_filename(g_get_user_data_dir(), "fsearch", "fsearch.db", NULL);
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
    if (view->sort_type == GTK_SORT_DESCENDING) {
        idx = num_folders + num_files - (idx + 1);
    }
    if (idx < num_folders) {
        return darray_get_item(view->folders, idx);
    }
    idx -= num_folders;
    if (idx < num_files) {
        return darray_get_item(view->files, idx);
    }
    return NULL;
}

static void
get_entry_info(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    database_lock(self);

    const uint32_t idx = fsearch_database_work_item_info_get_index(work);
    const int32_t id = fsearch_database_work_item_info_get_view_id(work);

    FsearchDatabaseSearchView *view = g_hash_table_lookup(self->search_results, GUINT_TO_POINTER(id));
    if (!view) {
        database_unlock(self);
        return;
    }

    const FsearchDatabaseEntryInfoFlags flags = fsearch_database_work_item_info_get_flags(work);

    FsearchDatabaseEntry *entry = get_entry_for_idx(view, idx);
    if (!entry) {
        database_unlock(self);
        return;
    }

    g_autoptr(FsearchDatabaseEntryInfo) info = fsearch_database_entry_info_new(entry, idx, flags);

    database_unlock(self);

    emit_item_info_ready_signal(self, fsearch_database_work_item_info_get_view_id(work), info);

    return;
}

static bool
sort_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_val_if_fail(self, false);

    const guint id = fsearch_database_work_sort_get_view_id(work);
    const FsearchDatabaseIndexType sort_order = fsearch_database_work_sort_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_sort_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    emit_signal(self, EVENT_SORT_STARTED, GUINT_TO_POINTER(id));

    database_lock(self);

    FsearchDatabaseSearchView *view = g_hash_table_lookup(self->search_results, GUINT_TO_POINTER(id));
    DynamicArray *files_new = NULL;
    DynamicArray *folders_new = NULL;
    if (view) {
        fsearch_database_sort_results(view->sort_order,
                                      sort_order,
                                      view->files,
                                      view->folders,
                                      self->index->files,
                                      self->index->folders,
                                      &files_new,
                                      &folders_new,
                                      &view->sort_order,
                                      cancellable);
    }
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

    FsearchDatabaseSearchInfo *info = fsearch_database_search_info_new(fsearch_query_ref(view->query),
                                                                       darray_get_num_items(view->files),
                                                                       darray_get_num_items(view->folders),
                                                                       view->sort_order,
                                                                       view->sort_type);

    database_unlock(self);

    emit_sort_finished_signal(self, id, info);

    return true;
}

static bool
is_valid_fast_sort_type(FsearchDatabaseIndexType sort_type) {
    if (0 <= sort_type && sort_type < NUM_DATABASE_INDEX_TYPES) {
        return true;
    }
    return false;
}

static bool
has_entries_sorted_by_type(DynamicArray **sorted_entries, FsearchDatabaseIndexType sort_type) {
    if (!is_valid_fast_sort_type(sort_type)) {
        return false;
    }
    return sorted_entries[sort_type] ? true : false;
}

static bool
search_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_val_if_fail(self, false);

    const guint id = fsearch_database_work_search_get_view_id(work);
    emit_signal(self, EVENT_SEARCH_STARTED, GUINT_TO_POINTER(id));

    g_autoptr(FsearchQuery) query = fsearch_database_work_search_get_query(work);
    FsearchDatabaseIndexType sort_order = fsearch_database_work_search_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_search_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
    uint32_t num_files = 0;
    uint32_t num_folders = 0;

    database_lock(self);

    bool result = false;
    DynamicArray *files = NULL;
    DynamicArray *folders = NULL;

    if (has_entries_sorted_by_type(self->index->folders, sort_order)
        && has_entries_sorted_by_type(self->index->files, sort_order)) {
        files = self->index->files[sort_order];
        folders = self->index->folders[sort_order];
    }
    else {
        files = self->index->files[DATABASE_INDEX_TYPE_NAME];
        folders = self->index->folders[DATABASE_INDEX_TYPE_NAME];
        sort_order = DATABASE_INDEX_TYPE_NAME;
    }

    DatabaseSearchResult *search_result = db_search(query, self->thread_pool, folders, files, sort_order, cancellable);
    if (search_result) {
        num_files = darray_get_num_items(search_result->files);
        num_folders = darray_get_num_items(search_result->folders);

        FsearchDatabaseSearchView *view =
            search_view_new(query, search_result->files, search_result->folders, NULL, sort_order, sort_type);
        g_hash_table_insert(self->search_results, GUINT_TO_POINTER(id), view);

        g_print("found: %d/%d\n", num_files, num_folders);

        g_clear_pointer(&search_result->files, darray_unref);
        g_clear_pointer(&search_result->folders, darray_unref);
        g_clear_pointer(&search_result, free);
        result = true;
    }

    database_unlock(self);

    FsearchDatabaseSearchInfo *info =
        fsearch_database_search_info_new(query, num_files, num_folders, sort_order, sort_type);
    emit_search_finished_signal(self, fsearch_database_work_search_get_view_id(work), info);

    return result;
}

static bool
save_database_to_file(FsearchDatabase2 *self) {
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(self->file, false);

    g_autoptr(GFile) db_directory = g_file_get_parent(self->file);
    g_autofree gchar *db_directory_path = g_file_get_path(db_directory);

    database_lock(self);

    const bool res = db_file_save(self->index, db_directory_path);

    database_unlock(self);

    return res;
}

static bool
rescan_database(FsearchDatabase2 *self) {
    g_return_val_if_fail(self, false);

    database_lock(self);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = g_object_ref(self->include_manager);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = g_object_ref(self->exclude_manager);
    const FsearchDatabaseIndexFlags flags = self->flags;

    database_unlock(self);

    g_autoptr(FsearchDatabaseIndex) index = db_scan2(include_manager, exclude_manager, flags, NULL);

    if (!index) {
        return false;
    }

    database_lock(self);

    g_set_object(&self->include_manager, include_manager);
    g_set_object(&self->exclude_manager, exclude_manager);
    self->flags = flags;
    g_clear_pointer(&self->index, fsearch_database_index_free);
    self->index = g_steal_pointer(&index);

    database_unlock(self);

    return true;
}

static bool
scan_database(FsearchDatabase2 *self, FsearchDatabaseWork *work) {
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(work, false);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_work_scan_get_include_manager(work);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_work_scan_get_exclude_manager(work);
    const FsearchDatabaseIndexFlags flags = fsearch_database_work_scan_get_flags(work);

    g_autoptr(FsearchDatabaseIndex) index = db_scan2(include_manager, exclude_manager, flags, NULL);

    if (!index) {
        return false;
    }
    database_lock(self);

    g_set_object(&self->include_manager, include_manager);
    g_set_object(&self->exclude_manager, exclude_manager);
    self->flags = flags;
    g_clear_pointer(&self->index, fsearch_database_index_free);
    self->index = g_steal_pointer(&index);

    database_unlock(self);

    return true;
}

static bool
load_database_from_file(FsearchDatabase2 *self) {
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(self->file, false);

    emit_signal(self, EVENT_LOAD_STARTED, NULL);

    g_autofree gchar *file_path = g_file_get_path(self->file);
    g_return_val_if_fail(file_path, false);

    g_autofree FsearchDatabaseIndex *index = db_file_load(file_path, NULL);

    if (!index) {
        return false;
    }
    database_lock(self);

    g_clear_pointer(&self->index, fsearch_database_index_free);
    self->index = g_steal_pointer(&index);

    FsearchDatabaseInfo *info =
        fsearch_database_info_new(darray_get_num_items(self->index->files[DATABASE_INDEX_TYPE_NAME]),
                                  darray_get_num_items(self->index->folders[DATABASE_INDEX_TYPE_NAME]));

    database_unlock(self);

    emit_signal(self, EVENT_LOAD_FINISHED, info);

    return true;
}

static gpointer
work_queue_thread(gpointer data) {
    g_print("manager thread started\n");
    FsearchDatabase2 *self = data;

    while (TRUE) {
        if (!g_async_queue_timeout_pop(self->work_trigger_queue, 1000000)) {
            continue;
        }

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
            case FSEARCH_DATABASE_WORK_GET_ITEM_INFO:
                get_entry_info(self, work);
                break;
            case FSEARCH_DATABASE_WORK_RESCAN:
                emit_signal(self, EVENT_SCAN_STARTED, NULL);
                rescan_database(self);
                emit_signal(self, EVENT_SCAN_FINISHED, NULL);
                break;
            case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
                emit_signal(self, EVENT_SAVE_STARTED, NULL);
                save_database_to_file(self);
                emit_signal(self, EVENT_SAVE_FINISHED, NULL);
                break;
            case FSEARCH_DATABASE_WORK_SCAN:
                emit_signal(self, EVENT_SCAN_STARTED, NULL);
                scan_database(self, work);
                emit_signal(self, EVENT_SCAN_FINISHED, NULL);
                break;
            case FSEARCH_DATABASE_WORK_SEARCH:
                search_database(self, work);
                break;
            case FSEARCH_DATABASE_WORK_SORT:
                sort_database(self, work);
                break;
            default:
                g_assert_not_reached();
            }

            // g_print("finished work in: %fs.\n", g_timer_elapsed(timer, NULL));
        }

        if (g_cancellable_is_cancelled(self->work_queue_thread_cancellable)) {
            g_print("thread cancelled...\n");
            break;
        }
    }

    g_print("manager thread returning\n");
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

    g_async_queue_push(self->work_queue, fsearch_database_work_new_load(NULL, NULL));

    g_print("constructed...\n");
}

static void
fsearch_database2_dispose(GObject *object) {
    FsearchDatabase2 *self = (FsearchDatabase2 *)object;
    g_print("dispose db2...\n");

    // Notify work queue thread to exit itself
    g_cancellable_cancel(self->work_queue_thread_cancellable);
    wakeup_work_queue(self);
    g_thread_join(self->work_queue_thread);

    G_OBJECT_CLASS(fsearch_database2_parent_class)->dispose(object);

    g_print("disposed db2.\n");
}

static void
fsearch_database2_finalize(GObject *object) {
    FsearchDatabase2 *self = (FsearchDatabase2 *)object;
    g_print("finalize db2...\n");

    database_lock(self);
    g_clear_object(&self->work_queue_thread_cancellable);
    g_clear_pointer(&self->work_trigger_queue, g_async_queue_unref);
    g_clear_pointer(&self->work_queue, g_async_queue_unref);
    g_clear_pointer(&self->thread_pool, fsearch_thread_pool_free);

    g_clear_object(&self->file);

    g_clear_pointer(&self->search_results, g_hash_table_unref);
    g_clear_pointer(&self->index, fsearch_database_index_free);
    g_clear_object(&self->include_manager);
    g_clear_object(&self->exclude_manager);
    database_unlock(self);

    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(fsearch_database2_parent_class)->finalize(object);
    g_print("finalized db2.\n");
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
    g_print("class init....\n");
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

    signals[EVENT_LOAD_STARTED] = g_signal_new("load-started",
                                               G_TYPE_FROM_CLASS(klass),
                                               G_SIGNAL_RUN_LAST,
                                               0,
                                               NULL,
                                               NULL,
                                               NULL,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_POINTER);
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
    signals[EVENT_SCAN_STARTED] = g_signal_new("scan-started",
                                               G_TYPE_FROM_CLASS(klass),
                                               G_SIGNAL_RUN_LAST,
                                               0,
                                               NULL,
                                               NULL,
                                               NULL,
                                               G_TYPE_NONE,
                                               1,
                                               G_TYPE_POINTER);
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

FsearchDatabase2 *
fsearch_database2_new(GFile *file) {
    return g_object_new(FSEARCH_TYPE_DATABASE2, "file", file, NULL);
}
