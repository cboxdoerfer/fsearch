#define G_LOG_DOMAIN "fsearch-database"

#include "fsearch_database.h"

#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_file.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_info.h"
#include "fsearch_database_rescan_manager.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_work.h"
#include "fsearch_query.h"
#include "fsearch_result.h"
#include "fsearch_selection_type.h"

#include <config.h>
#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtkenums.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct _FsearchDatabase {
    GObject parent_instance;

    // The database will be loaded from and saved to this file
    GFile *file;
    FsearchDatabaseIncludeManager *include_manager;
    FsearchDatabaseExcludeManager *exclude_manager;

    GThread *worker_thread;
    GMainContext *worker_ctx;
    GMainLoop *worker_loop;

    GThreadPool *io_pool;

    GCancellable *cancellable;

    // Cancellable of the most recently queued scan
    GCancellable *scan_cancellable;
    GMutex scan_mutex;

    FsearchDatabaseIndexStore *store;
    FsearchDatabaseIndexStore *pending_store;
    FsearchDatabaseRescanManager *rescan_manager;

    GMutex mutex;

    bool disposed;
};

G_DEFINE_TYPE(FsearchDatabase, fsearch_database, G_TYPE_OBJECT)

enum { PROP_0, PROP_FILE, PROP_INCLUDE_MANAGER, PROP_EXCLUDE_MANAGER, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

typedef enum FsearchDatabaseSignalType {
    SIGNAL_LOAD_STARTED,
    SIGNAL_LOAD_FINISHED,
    SIGNAL_ITEM_INFO_READY,
    SIGNAL_SAVE_STARTED,
    SIGNAL_SAVE_FINISHED,
    SIGNAL_SCAN_STARTED,
    SIGNAL_SCAN_FINISHED,
    SIGNAL_SEARCH_STARTED,
    SIGNAL_SEARCH_FINISHED,
    SIGNAL_SORT_STARTED,
    SIGNAL_SORT_FINISHED,
    SIGNAL_SELECTION_CHANGED,
    SIGNAL_DATABASE_CHANGED,
    SIGNAL_DATABASE_PROGRESS,
    NUM_DATABASE_SIGNALS,
} FsearchDatabaseSignalType;

static guint signals[NUM_DATABASE_SIGNALS];

static void
index_store_event_cb(FsearchDatabaseIndexStore *store,
                     FsearchDatabaseIndexStoreEventKind kind,
                     gpointer data,
                     gpointer user_data);

// endregion

// region Signaling
typedef struct FsearchSignalEmitContext {
    // Weak, so queued emissions can't resurrect a database that's already being disposed
    GWeakRef db;
    FsearchDatabaseSignalType type;
    gpointer arg1;
    gpointer arg2;
    gpointer arg3;
    GDestroyNotify arg1_free_func;
    GDestroyNotify arg2_free_func;
    GDestroyNotify arg3_free_func;
    guint n_args;
} FsearchSignalEmitContext;

static void
signal_emit_context_free(FsearchSignalEmitContext *ctx) {
    if (ctx->arg1_free_func) {
        g_clear_pointer(&ctx->arg1, ctx->arg1_free_func);
    }
    if (ctx->arg2_free_func) {
        g_clear_pointer(&ctx->arg2, ctx->arg2_free_func);
    }
    if (ctx->arg3_free_func) {
        g_clear_pointer(&ctx->arg3, ctx->arg3_free_func);
    }
    g_weak_ref_clear(&ctx->db);
    g_clear_pointer(&ctx, free);
}

static FsearchSignalEmitContext *
signal_emit_context_new(FsearchDatabase *db,
                        FsearchDatabaseSignalType type,
                        gpointer arg1,
                        gpointer arg2,
                        gpointer arg3,
                        guint n_args,
                        GDestroyNotify arg1_free_func,
                        GDestroyNotify arg2_free_func,
                        GDestroyNotify arg3_free_func) {
    FsearchSignalEmitContext *ctx = calloc(1, sizeof(FsearchSignalEmitContext));
    g_assert(ctx != NULL);

    g_weak_ref_init(&ctx->db, db);
    ctx->type = type;
    ctx->arg1 = arg1;
    ctx->arg2 = arg2;
    ctx->arg3 = arg3;
    ctx->n_args = n_args;
    ctx->arg1_free_func = arg1_free_func;
    ctx->arg2_free_func = arg2_free_func;
    ctx->arg3_free_func = arg3_free_func;
    return ctx;
}

const char *
signal_type_to_name(FsearchDatabaseSignalType type) {
    switch (type) {
    case SIGNAL_LOAD_STARTED:
        return "SIGNAL_LOAD_STARTED";
    case SIGNAL_LOAD_FINISHED:
        return "SIGNAL_LOAD_FINISHED";
    case SIGNAL_ITEM_INFO_READY:
        return "SIGNAL_ITEM_INFO_READY";
    case SIGNAL_SAVE_STARTED:
        return "SIGNAL_SAVE_STARTED";
    case SIGNAL_SAVE_FINISHED:
        return "SIGNAL_SAVE_FINISHED";
    case SIGNAL_SCAN_STARTED:
        return "SIGNAL_SCAN_STARTED";
    case SIGNAL_SCAN_FINISHED:
        return "SIGNAL_SCAN_FINISHED";
    case SIGNAL_SEARCH_STARTED:
        return "SIGNAL_SEARCH_STARTED";
    case SIGNAL_SEARCH_FINISHED:
        return "SIGNAL_SEARCH_FINISHED";
    case SIGNAL_SORT_STARTED:
        return "SIGNAL_SORT_STARTED";
    case SIGNAL_SORT_FINISHED:
        return "SIGNAL_SORT_FINISHED";
    case SIGNAL_SELECTION_CHANGED:
        return "SIGNAL_SELECTION_CHANGED";
    case SIGNAL_DATABASE_CHANGED:
        return "SIGNAL_DATABASE_CHANGED";
    case SIGNAL_DATABASE_PROGRESS:
        return "SIGNAL_DATABASE_PROGRESS";
    case NUM_DATABASE_SIGNALS:
        return "UNKNOWN";
    default:
        g_assert_not_reached();
    }
}

static gboolean
signal_emit_cb(gpointer user_data) {
    FsearchSignalEmitContext *ctx = user_data;

    g_autoptr(FsearchDatabase) db = g_weak_ref_get(&ctx->db);
    if (!db) {
        // The database is already gone, drop the emission
        g_clear_pointer(&ctx, signal_emit_context_free);
        return G_SOURCE_REMOVE;
    }

    switch (ctx->n_args) {
    case 0:
        g_signal_emit(db, signals[ctx->type], 0);
        break;
    case 1:
        g_signal_emit(db, signals[ctx->type], 0, ctx->arg1);
        break;
    case 2:
        g_signal_emit(db, signals[ctx->type], 0, ctx->arg1, ctx->arg2);
        break;
    case 3:
        g_signal_emit(db, signals[ctx->type], 0, ctx->arg1, ctx->arg2, ctx->arg3);
        break;
    default:
        g_assert_not_reached();
    }

    g_clear_pointer(&ctx, signal_emit_context_free);

    return G_SOURCE_REMOVE;
}

static void
signal_emit0(FsearchDatabase *self, FsearchDatabaseSignalType type) {
    if (g_cancellable_is_cancelled(self->cancellable)) {
        g_debug("signal_emit0: cancelling signal %s", signal_type_to_name(type));
        return;
    }
    g_idle_add(signal_emit_cb, signal_emit_context_new(self, type, NULL, NULL, NULL, 0, NULL, NULL, NULL));
}

static void
signal_emit(FsearchDatabase *self,
            FsearchDatabaseSignalType type,
            gpointer arg1,
            gpointer arg2,
            guint n_args,
            GDestroyNotify arg1_free_func,
            GDestroyNotify arg2_free_func) {
    FsearchSignalEmitContext
        *ctx = signal_emit_context_new(self, type, arg1, arg2, NULL, n_args, arg1_free_func, arg2_free_func, NULL);
    if (g_cancellable_is_cancelled(self->cancellable)) {
        g_debug("signal_emit: cancelling signal %s", signal_type_to_name(type));
        // Still free the ownership-transferred args (worker thread keeps emitting until it joins)
        g_clear_pointer(&ctx, signal_emit_context_free);
        return;
    }
    g_idle_add(signal_emit_cb, ctx);
}

static void
signal_emit3(FsearchDatabase *self,
             FsearchDatabaseSignalType type,
             gpointer arg1,
             gpointer arg2,
             gpointer arg3,
             GDestroyNotify arg1_free_func,
             GDestroyNotify arg2_free_func,
             GDestroyNotify arg3_free_func) {
    FsearchSignalEmitContext
        *ctx = signal_emit_context_new(self, type, arg1, arg2, arg3, 3, arg1_free_func, arg2_free_func, arg3_free_func);
    if (g_cancellable_is_cancelled(self->cancellable)) {
        g_debug("signal_emit3: cancelling signal %s", signal_type_to_name(type));
        g_clear_pointer(&ctx, signal_emit_context_free);
        return;
    }
    g_idle_add(signal_emit_cb, ctx);
}

static void
signal_emit_item_info_ready(FsearchDatabase *self, guint id, guint idx, FsearchDatabaseEntryInfo *info) {
    // `info` may be NULL: the requested row no longer exists in the current search view (e.g. it
    // was replaced by a newer/cancelled search while this request was in flight). Emitting the
    // signal regardless of success lets callers clear any placeholder they cached for `idx`
    // instead of it being stuck unresolved until their whole cache is later reset wholesale.
    signal_emit3(self,
                 SIGNAL_ITEM_INFO_READY,
                 GUINT_TO_POINTER(id),
                 GUINT_TO_POINTER(idx),
                 info,
                 NULL,
                 NULL,
                 (GDestroyNotify)fsearch_database_entry_info_unref);
}

static void
signal_emit_search_finished(FsearchDatabase *self, guint id, FsearchDatabaseSearchInfo *info) {
    signal_emit(self,
                SIGNAL_SEARCH_FINISHED,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
signal_emit_sort_finished(FsearchDatabase *self, guint id, FsearchDatabaseSearchInfo *info) {
    signal_emit(self,
                SIGNAL_SORT_FINISHED,
                GUINT_TO_POINTER(id),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
signal_emit_selection_changed(FsearchDatabase *self, FsearchDatabaseSearchInfo *info) {
    signal_emit(self,
                SIGNAL_SELECTION_CHANGED,
                GUINT_TO_POINTER(fsearch_database_search_info_get_id(info)),
                info,
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
signal_emit_database_changed(FsearchDatabase *self, FsearchDatabaseInfo *info) {
    signal_emit(self, SIGNAL_DATABASE_CHANGED, info, NULL, 1, (GDestroyNotify)fsearch_database_info_unref, NULL);
}

static void
signal_emit_database_progress(FsearchDatabase *self, char *text) {
    signal_emit(self, SIGNAL_DATABASE_PROGRESS, text, NULL, 1, (GDestroyNotify)free, NULL);
}

// endregion

// region Database private
static FsearchDatabaseExcludeManager *
database_get_exclude_manager(FsearchDatabase *self) {
    if (self->exclude_manager) {
        return g_object_ref(self->exclude_manager);
    }
    return self->store ? fsearch_database_index_store_get_exclude_manager(self->store) : NULL;
}

static FsearchDatabaseIncludeManager *
database_get_include_manager(FsearchDatabase *self) {
    if (self->include_manager) {
        return g_object_ref(self->include_manager);
    }
    return self->store ? fsearch_database_index_store_get_include_manager(self->store) : NULL;
}

static uint32_t
database_get_num_files(FsearchDatabase *self) {
    return self->store ? fsearch_database_index_store_get_num_files(self->store) : 0;
}

static uint32_t
database_get_num_folders(FsearchDatabase *self) {
    return self->store ? fsearch_database_index_store_get_num_folders(self->store) : 0;
}

static FsearchDatabaseInfo *
database_get_info(FsearchDatabase *self) {
    g_return_val_if_fail(self, NULL);
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = database_get_include_manager(self);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = database_get_exclude_manager(self);
    return fsearch_database_info_new(include_manager,
                                     exclude_manager,
                                     database_get_num_files(self),
                                     database_get_num_folders(self));
}

static GFile *
database_get_file_default() {
    return g_file_new_build_filename(g_get_user_data_dir(), "fsearch", "fsearch.db", NULL);
}

static FsearchResult
database_get_entry_info_non_blocking(FsearchDatabase *self,
                                     FsearchDatabaseWork *work,
                                     FsearchDatabaseEntryInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(work, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    const uint32_t idx = fsearch_database_work_item_info_get_index(work);
    const uint32_t id = fsearch_database_work_get_view_id(work);
    const FsearchDatabaseEntryInfoFlags flags = fsearch_database_work_item_info_get_flags(work);

    *info_out = fsearch_database_index_store_get_entry_info(self->store, idx, id, flags);
    return FSEARCH_RESULT_SUCCESS;
}

static FsearchResult
database_get_entry_info(FsearchDatabase *self, FsearchDatabaseWork *work, FsearchDatabaseEntryInfo **info_out) {
    // DB must be locked
    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);
    return database_get_entry_info_non_blocking(self, work, info_out);
}

static void
database_sort(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->store);

    const uint32_t id = fsearch_database_work_get_view_id(work);
    const FsearchDatabaseIndexProperty sort_order = fsearch_database_work_sort_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_sort_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    signal_emit(self, SIGNAL_SORT_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    fsearch_database_index_store_sort_results(self->store, id, sort_order, sort_type, cancellable);

    signal_emit_sort_finished(self, id, fsearch_database_index_store_get_search_info(self->store, id));
}

static bool
database_search(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_val_if_fail(self, false);
    g_return_val_if_fail(self->store, false);

    const uint32_t id = fsearch_database_work_get_view_id(work);

    g_autoptr(FsearchQuery) query = fsearch_database_work_search_get_query(work);
    FsearchDatabaseIndexProperty sort_order = fsearch_database_work_search_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_search_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    signal_emit(self, SIGNAL_SEARCH_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    const bool result = fsearch_database_index_store_search(self->store, id, query, sort_order, sort_type, cancellable);

    signal_emit_search_finished(self, id, fsearch_database_index_store_get_search_info(self->store, id));

    return result;
}

static void
index_store_event_cb(FsearchDatabaseIndexStore *store,
                     FsearchDatabaseIndexStoreEventKind kind,
                     gpointer data,
                     gpointer user_data) {
    g_assert(user_data);
    FsearchDatabase *self = FSEARCH_DATABASE(user_data);

    switch (kind) {
    case FSEARCH_DATABASE_INDEX_STORE_EVENT_CONTENT_CHANGED:
        signal_emit_database_changed(self, database_get_info(self));
        break;
    case FSEARCH_DATABASE_INDEX_STORE_EVENT_PROGRESS:
        signal_emit_database_progress(self, (char *)data);
        break;
    case FSEARCH_DATABASE_INDEX_STORE_EVENT_VIEW_CHANGED:
        signal_emit_selection_changed(self, (FsearchDatabaseSearchInfo *)data);
        break;
    default:
        g_assert_not_reached();
    }
}

static void
database_modify_selection(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->store);
    g_return_if_fail(work);
    const uint32_t view_id = fsearch_database_work_get_view_id(work);
    const FsearchSelectionType type = fsearch_database_work_modify_selection_get_type(work);
    const int32_t start_idx = fsearch_database_work_modify_selection_get_start_idx(work);
    const int32_t end_idx = fsearch_database_work_modify_selection_get_end_idx(work);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    fsearch_database_index_store_modify_selection(self->store, view_id, type, start_idx, end_idx);

    FsearchDatabaseSearchInfo *info = fsearch_database_index_store_get_search_info(self->store, view_id);
    if (!info) {
        // No search view for this id (e.g. the search matched nothing) - nothing was selected
        return;
    }
    signal_emit_selection_changed(self, info);
}

static void
database_save(FsearchDatabase *self, gboolean notify) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->file);
    g_return_if_fail(self->store);

    if (notify) {
        signal_emit0(self, SIGNAL_SAVE_STARTED);
    }

    g_autofree char *file_path = g_file_get_path(self->file);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);
    fsearch_database_file_save(self->store, file_path);

    if (notify) {
        signal_emit0(self, SIGNAL_SAVE_FINISHED);
    }
}

static void
on_index_scan_requested(const char *path, gpointer user_data) {
    FsearchDatabase *self = FSEARCH_DATABASE(user_data);
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_rescan_index(path);
    fsearch_database_queue_work(self, work);
}

static void
on_full_scan_requested(gpointer user_data) {
    FsearchDatabase *self = FSEARCH_DATABASE(user_data);
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_rescan();
    fsearch_database_queue_work(self, work);
}

static void
io_thread_cb(gpointer data, gpointer user_data) {
    g_autoptr(FsearchDatabaseWork) work = data;
    FsearchDatabase *db = user_data;
    g_return_if_fail(work);
    g_return_if_fail(db);

    bool queue_work = false;

    FsearchDatabaseWorkKind kind = fsearch_database_work_get_kind(work);
    switch (kind) {
    case FSEARCH_DATABASE_WORK_SCAN_FINISHED: {
        g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_work_scan_finished_get_index_store(work);
        g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
        fsearch_database_index_store_start(store, cancellable);
        queue_work = true;
        break;
    }
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED: {
        g_autoptr(FsearchDatabaseIndex) new_index = fsearch_database_work_rescan_index_finished_get_index(work);
        g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
        if (!fsearch_database_index_scan(new_index, cancellable)) {
            if (g_cancellable_is_cancelled(cancellable)) {
                queue_work = true;
                break;
            }
            g_debug("[db] index rescan failed: %s", fsearch_database_index_get_path(new_index));

            if (db->rescan_manager) {
                fsearch_database_rescan_manager_notify_index_offline(db->rescan_manager,
                                                                     fsearch_database_index_get_path(new_index));
            }
            break;
        }
        queue_work = true;
        break;
    }
    default:
        g_assert_not_reached();
    }
    if (queue_work && !g_cancellable_is_cancelled(db->cancellable)) {
        fsearch_database_queue_work(db, work);
    }
}

static void
database_set_store(FsearchDatabase *self, FsearchDatabaseIndexStore *store) {
    g_return_if_fail(self);

    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = store ? fsearch_database_index_store_ref(store) : NULL;

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_index_store_get_include_manager(
        self->store);
    g_set_object(&self->include_manager, include_manager);

    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_index_store_get_exclude_manager(
        self->store);
    g_set_object(&self->exclude_manager, exclude_manager);

    if (self->rescan_manager) {
        fsearch_database_rescan_manager_notify_new_config(self->rescan_manager, include_manager);
    }
}

static void
database_remove_items(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(DynamicArray) item_paths = fsearch_database_work_notify_items_removed_get_item_paths(work);
    g_return_if_fail(item_paths);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    fsearch_database_index_store_remove_paths(self->store, item_paths, self->rescan_manager);
}

// Clears self->scan_cancellable, unless a newer scan has already replaced it.
static void
database_clear_scan_cancellable_if_current(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->scan_mutex);
    if (self->scan_cancellable == cancellable) {
        g_clear_object(&self->scan_cancellable);
    }
}

static void
database_scan_finished(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_work_scan_finished_get_index_store(work);

    if (store == self->pending_store) {
        g_clear_pointer(&self->pending_store, fsearch_database_index_store_unref);
    }

    // If the scan was cancelled, fsearch_database_index_store_start() never finished building
    // `store`. leave the current, still-intact store in place.
    if (fsearch_database_index_store_is_running(store)) {
        database_set_store(self, store);

        g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
        g_assert_nonnull(locker);

        fsearch_database_index_store_start_monitoring(self->store);

#ifdef HAVE_MALLOC_TRIM
        malloc_trim(0);
#endif
    }

    database_clear_scan_cancellable_if_current(self, work);

    signal_emit(self,
                SIGNAL_SCAN_FINISHED,
                database_get_info(self),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

static void
database_rescan_sync(FsearchDatabase *db,
                     FsearchDatabaseIncludeManager *include_manager,
                     FsearchDatabaseExcludeManager *exclude_manager,
                     FsearchDatabaseIndexPropertyFlags flags) {
    // DB must be locked
    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
                                                                                  exclude_manager,
                                                                                  flags,
                                                                                  index_store_event_cb,
                                                                                  db);
    g_return_if_fail(store);

    database_set_store(db, store);
    g_clear_pointer(&db->pending_store, fsearch_database_index_store_unref);

    fsearch_database_index_store_start(store, NULL);
}

static void
database_rescan(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);
    g_return_if_fail(self->store || self->pending_store);

    // Shutting down: don't push a scan the io pool would have to finish before it can be freed
    if (g_cancellable_is_cancelled(self->cancellable)) {
        return;
    }

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = NULL;
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = NULL;

    // If there is already another scan running, we must use the index store config of the pending store
    // This ensures that the rescan we are about to queue uses this new config instead of the current one.
    FsearchDatabaseIndexStore *source_store = self->pending_store ? self->pending_store : self->store;

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(source_store);
    g_assert_nonnull(locker);
    include_manager = fsearch_database_index_store_get_include_manager(source_store);
    exclude_manager = fsearch_database_index_store_get_exclude_manager(source_store);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_index_store_get_flags(source_store);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
                                                                                  exclude_manager,
                                                                                  flags,
                                                                                  index_store_event_cb,
                                                                                  self);
    g_return_if_fail(store);

    g_clear_pointer(&self->pending_store, fsearch_database_index_store_unref);
    self->pending_store = fsearch_database_index_store_ref(store);

    // The IO thread is expecting work objects -> use scan_finished kind to wrap the index store
    g_autoptr(FsearchDatabaseWork) new_work = fsearch_database_work_new_scan_finished(
        store,
        (void *(*)(void *))fsearch_database_index_store_ref,
        (GDestroyNotify)fsearch_database_index_store_unref,
        cancellable);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&new_work), NULL);
}

static void
database_rescan_index(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->store);
    g_return_if_fail(work);

    // Shutting down: don't push a scan the io pool would have to finish before it can be freed
    if (g_cancellable_is_cancelled(self->cancellable)) {
        return;
    }

    const char *path = fsearch_database_work_rescan_index_get_path(work);

    g_autoptr(FsearchDatabaseIndex) new_index = fsearch_database_index_store_create_index_for_rescan(self->store, path);

    if (!new_index) {
        g_warning("[db] rescan_index: failed to create index for rescan: %s", path);
        database_clear_scan_cancellable_if_current(self, work);
        return;
    }

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    // Push to the IO pool to perform the actual scan
    g_autoptr(FsearchDatabaseWork) new_work = fsearch_database_work_new_rescan_index_finished(new_index, cancellable);
    g_thread_pool_push(self->io_pool, g_steal_pointer(&new_work), NULL);
}

static void
database_rescan_index_finished(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIndex) new_index = fsearch_database_work_rescan_index_finished_get_index(work);

    if (!self->store) {
        // The store was fully replaced by a concurrent full rescan - nothing to do.
        return;
    }

    // If cancelled, don't apply `new_index` -- but always emit SIGNAL_SCAN_FINISHED below so the
    // UI's cancel state clears.
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
    if (!g_cancellable_is_cancelled(cancellable)) {
        signal_emit_database_progress(self, g_strdup(_("Index rescan: applying changes…")));

        if (fsearch_database_index_store_replace_index(self->store, new_index)) {
#ifdef HAVE_MALLOC_TRIM
            malloc_trim(0);
#endif

            if (self->rescan_manager) {
                fsearch_database_rescan_manager_notify_index_finished(self->rescan_manager,
                                                                      fsearch_database_index_get_path(new_index));
            }
        }
    }

    database_clear_scan_cancellable_if_current(self, work);

    g_autoptr(GMutexLocker) store_locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(store_locker);

    signal_emit(self,
                SIGNAL_SCAN_FINISHED,
                database_get_info(self),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

static void
database_scan(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    // Shutting down: don't push a scan the io pool would have to finish before it can be freed
    if (g_cancellable_is_cancelled(self->cancellable)) {
        return;
    }

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_work_scan_get_include_manager(work);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_work_scan_get_exclude_manager(work);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_work_scan_get_flags(work);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    if (self->store && fsearch_database_include_manager_equal(database_get_include_manager(self), include_manager)
        && fsearch_database_exclude_manager_equal(database_get_exclude_manager(self), exclude_manager)) {
        g_debug("[scan] new config is identical to the current one. No scan necessary.");
        database_clear_scan_cancellable_if_current(self, work);
        return;
    }

    g_clear_pointer(&locker, g_mutex_locker_free);

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
                                                                                  exclude_manager,
                                                                                  flags,
                                                                                  index_store_event_cb,
                                                                                  self);
    g_return_if_fail(store);

    g_clear_pointer(&self->pending_store, fsearch_database_index_store_unref);
    self->pending_store = fsearch_database_index_store_ref(store);

    // The IO thread is expecting work objects -> use scan_finished kind to wrap the index store
    g_autoptr(FsearchDatabaseWork) new_work = fsearch_database_work_new_scan_finished(
        store,
        (void *(*)(void *))fsearch_database_index_store_ref,
        (GDestroyNotify)fsearch_database_index_store_unref,
        cancellable);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&new_work), NULL);
}

static void
database_load(FsearchDatabase *self) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    signal_emit0(self, SIGNAL_LOAD_STARTED);

    g_autoptr(FsearchDatabaseIndexStore) store = NULL;
    g_autofree char *file_path = g_file_get_path(self->file);
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = database_get_include_manager(self);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = database_get_exclude_manager(self);
    if (!include_manager) {
        include_manager = fsearch_database_include_manager_new_with_defaults();
    }
    if (!exclude_manager) {
        exclude_manager = fsearch_database_exclude_manager_new_with_defaults();
    }

    const bool res = fsearch_database_file_load(file_path,
                                                NULL,
                                                &store,
                                                include_manager,
                                                exclude_manager,
                                                index_store_event_cb,
                                                self);

    if (!res) {
        // On a failed load we use the default flags
        store = fsearch_database_index_store_new(include_manager,
                                                 exclude_manager,
                                                 DATABASE_INDEX_PROPERTY_FLAG_DEFAULT,
                                                 index_store_event_cb,
                                                 self);
    }

    database_set_store(self, store);
    g_clear_pointer(&self->pending_store, fsearch_database_index_store_unref);

    if (self->rescan_manager) {
        if (!res) {
            fsearch_database_rescan_manager_request_full_scan(self->rescan_manager);
        }
        else {
            fsearch_database_rescan_manager_trigger_startup_scans(self->rescan_manager);
        }
    }

    signal_emit(self,
                SIGNAL_LOAD_FINISHED,
                database_get_info(self),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

// region DatabaseWorkWrapper
typedef struct {
    FsearchDatabase *db;
    FsearchDatabaseWork *work;
} DatabaseWorkWrapper;

static DatabaseWorkWrapper *
db_work_wrapper_new(FsearchDatabase *db, FsearchDatabaseWork *work) {
    DatabaseWorkWrapper *wrapper = g_new0(DatabaseWorkWrapper, 1);
    wrapper->db = db;
    wrapper->work = fsearch_database_work_ref(work);
    return wrapper;
}

static void
db_work_wrapper_free(DatabaseWorkWrapper *wrapper) {
    g_return_if_fail(wrapper);
    g_clear_pointer(&wrapper->work, fsearch_database_work_unref);
    g_free(wrapper);
}

// endregion
//
static gboolean
handle_work_in_worker_thread_cb(gpointer user_data) {
    DatabaseWorkWrapper *wrapper = user_data;
    FsearchDatabase *self = wrapper->db;
    FsearchDatabaseWork *work = wrapper->work;

    g_autoptr(GTimer) timer = g_timer_new();
    bool quit = false;

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    switch (fsearch_database_work_get_kind(work)) {
    case FSEARCH_DATABASE_WORK_QUIT:
        database_save(self, FALSE);
        quit = true;
        break;
    case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
        database_save(self, TRUE);
        break;
    case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
        database_load(self);
        break;
    case FSEARCH_DATABASE_WORK_RESCAN:
        database_rescan(self, work);
        break;
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX:
        database_rescan_index(self, work);
        break;
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED:
        database_rescan_index_finished(self, work);
        break;
    case FSEARCH_DATABASE_WORK_SCAN:
        database_scan(self, work);
        break;
    case FSEARCH_DATABASE_WORK_SCAN_FINISHED:
        database_scan_finished(self, work);
        break;
    case FSEARCH_DATABASE_WORK_NOTIFY_ITEMS_REMOVED:
        database_remove_items(self, work);
        break;
    case FSEARCH_DATABASE_WORK_SEARCH:
        database_search(self, work);
        break;
    case FSEARCH_DATABASE_WORK_SORT:
        database_sort(self, work);
        break;
    case FSEARCH_DATABASE_WORK_MODIFY_SELECTION:
        database_modify_selection(self, work);
        break;
    case FSEARCH_DATABASE_WORK_GET_ITEM_INFO: {
        FsearchDatabaseEntryInfo *info = NULL;
        database_get_entry_info(self, work, &info);
        // Emit regardless of whether info was found: a NULL result (e.g. the row no longer
        // exists in the current search view) still needs to reach the caller so it can stop
        // treating this index as "pending" and retry later instead of getting stuck.
        signal_emit_item_info_ready(self,
                                    fsearch_database_work_get_view_id(work),
                                    fsearch_database_work_item_info_get_index(work),
                                    g_steal_pointer(&info));
        break;
    }
    default:
        g_assert_not_reached();
    }

    if (quit) {
        g_main_loop_quit(self->worker_loop);
    }

    return G_SOURCE_REMOVE;
}

static gpointer
database_worker_thread(gpointer data) {
    FsearchDatabase *self = FSEARCH_DATABASE(data);
    g_main_context_push_thread_default(self->worker_ctx);
    g_main_loop_run(self->worker_loop);
    g_main_context_pop_thread_default(self->worker_ctx);
    return NULL;
}

// endregion

// region Database GObject
static void
fsearch_database_constructed(GObject *object) {
    FsearchDatabase *self = (FsearchDatabase *)object;

    g_assert(FSEARCH_IS_DATABASE(self));

    G_OBJECT_CLASS(fsearch_database_parent_class)->constructed(object);

    if (self->file == NULL) {
        self->file = database_get_file_default();
    }
}

static void
fsearch_database_dispose(GObject *object) {
    FsearchDatabase *self = (FsearchDatabase *)object;

    // Dispose can run more than once
    if (self->disposed) {
        G_OBJECT_CLASS(fsearch_database_parent_class)->dispose(object);
        return;
    }
    self->disposed = true;

    // Cancel ongoing work
    g_cancellable_cancel(self->cancellable);
    fsearch_database_cancel_scan(self);

    // Notify worker  thread to exit itself
    g_autoptr(FsearchDatabaseWork) quit_work = fsearch_database_work_new_quit();
    fsearch_database_queue_work(self, quit_work);

    // Wait for the worker thread to exit
    if (self->worker_thread) {
        g_thread_join(g_steal_pointer(&self->worker_thread));
    }

    g_clear_pointer(&self->rescan_manager, fsearch_database_rescan_manager_free);

    // Exit io thread pool
    if (self->io_pool) {
        g_thread_pool_free(g_steal_pointer(&self->io_pool), TRUE, TRUE);
    }

    // Clean up the worker loop and context
    g_clear_pointer(&self->worker_loop, g_main_loop_unref);
    g_clear_pointer(&self->worker_ctx, g_main_context_unref);

    // Don't use g_clear_pointer here since index_store_unref will access self->store while terminating
    fsearch_database_index_store_unref(self->store);
    self->store = NULL;

    if (self->pending_store) {
        fsearch_database_index_store_unref(self->pending_store);
        self->pending_store = NULL;
    }

    G_OBJECT_CLASS(fsearch_database_parent_class)->dispose(object);
}

static void
fsearch_database_finalize(GObject *object) {
    FsearchDatabase *self = (FsearchDatabase *)object;

    g_clear_object(&self->include_manager);
    g_clear_object(&self->exclude_manager);
    g_clear_object(&self->file);

    g_clear_object(&self->cancellable);
    g_clear_object(&self->scan_cancellable);

    g_mutex_clear(&self->mutex);
    g_mutex_clear(&self->scan_mutex);

    G_OBJECT_CLASS(fsearch_database_parent_class)->finalize(object);
}

static void
fsearch_database_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchDatabase *self = FSEARCH_DATABASE(object);

    switch (prop_id) {
    case PROP_FILE:
        g_value_set_object(value, self->file);
        break;
    case PROP_INCLUDE_MANAGER:
        g_value_set_object(value, self->include_manager);
        break;
    case PROP_EXCLUDE_MANAGER:
        g_value_set_object(value, self->exclude_manager);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchDatabase *self = FSEARCH_DATABASE(object);

    switch (prop_id) {
    case PROP_FILE:
        self->file = g_value_get_object(value);
        break;
    case PROP_INCLUDE_MANAGER:
        self->include_manager = g_value_dup_object(value);
        break;
    case PROP_EXCLUDE_MANAGER:
        self->exclude_manager = g_value_dup_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_class_init(FsearchDatabaseClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = fsearch_database_constructed;
    object_class->dispose = fsearch_database_dispose;
    object_class->finalize = fsearch_database_finalize;
    object_class->set_property = fsearch_database_set_property;
    object_class->get_property = fsearch_database_get_property;

    properties[PROP_FILE] = g_param_spec_object("file",
                                                "File",
                                                "The file where the database will be loaded from or saved to by "
                                                "default",
                                                G_TYPE_FILE,
                                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    properties[PROP_INCLUDE_MANAGER] = g_param_spec_object("include-manager",
                                                           "Include Manager",
                                                           "The list of includes this database is initialized with",
                                                           FSEARCH_TYPE_DATABASE_INCLUDE_MANAGER,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
                                                               | G_PARAM_STATIC_STRINGS);
    properties[PROP_EXCLUDE_MANAGER] = g_param_spec_object("exclude-manager",
                                                           "Exclude Manager",
                                                           "The list of excludes this database is initialized with",
                                                           FSEARCH_TYPE_DATABASE_EXCLUDE_MANAGER,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
                                                               | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    signals[SIGNAL_LOAD_STARTED] =
        g_signal_new("load-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_LOAD_FINISHED] = g_signal_new("load-finished",
                                                 G_TYPE_FROM_CLASS(klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 G_TYPE_NONE,
                                                 1,
                                                 FSEARCH_TYPE_DATABASE_INFO);
    signals[SIGNAL_SAVE_STARTED] =
        g_signal_new("save-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_SAVE_FINISHED] =
        g_signal_new("save-finished", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_SCAN_STARTED] =
        g_signal_new("scan-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_SCAN_FINISHED] = g_signal_new("scan-finished",
                                                 G_TYPE_FROM_CLASS(klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 G_TYPE_NONE,
                                                 1,
                                                 FSEARCH_TYPE_DATABASE_INFO);
    signals[SIGNAL_SEARCH_STARTED] = g_signal_new("search-started",
                                                  G_TYPE_FROM_CLASS(klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  1,
                                                  G_TYPE_UINT);
    signals[SIGNAL_SEARCH_FINISHED] = g_signal_new("search-finished",
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
    signals[SIGNAL_SORT_STARTED] = g_signal_new("sort-started",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                1,
                                                G_TYPE_UINT);
    signals[SIGNAL_SORT_FINISHED] = g_signal_new("sort-finished",
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
    signals[SIGNAL_DATABASE_PROGRESS] = g_signal_new("database-progress",
                                                     G_TYPE_FROM_CLASS(klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     0,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     G_TYPE_NONE,
                                                     1,
                                                     G_TYPE_STRING);
    signals[SIGNAL_DATABASE_CHANGED] = g_signal_new("database-changed",
                                                    G_TYPE_FROM_CLASS(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    1,
                                                    FSEARCH_TYPE_DATABASE_INFO);
    signals[SIGNAL_SELECTION_CHANGED] = g_signal_new("selection-changed",
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
    signals[SIGNAL_ITEM_INFO_READY] = g_signal_new("item-info-ready",
                                                   G_TYPE_FROM_CLASS(klass),
                                                   G_SIGNAL_RUN_LAST,
                                                   0,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   G_TYPE_NONE,
                                                   3,
                                                   G_TYPE_UINT,
                                                   G_TYPE_UINT,
                                                   FSEARCH_TYPE_DATABASE_ENTRY_INFO);
}

static void
fsearch_database_init(FsearchDatabase *self) {
    g_mutex_init(&self->mutex);
    g_mutex_init(&self->scan_mutex);
    self->cancellable = g_cancellable_new();
#if GLIB_CHECK_VERSION(2, 70, 0)
    self->io_pool = g_thread_pool_new_full(io_thread_cb, self, (GDestroyNotify)fsearch_database_work_unref, 1, TRUE, NULL);
#else
    self->io_pool = g_thread_pool_new(io_thread_cb, self, 1, TRUE, NULL);
#endif
    self->worker_ctx = g_main_context_new();
    self->worker_loop = g_main_loop_new(self->worker_ctx, FALSE);
    self->worker_thread = g_thread_new("FsearchDatabaseWorker", database_worker_thread, self);

    self->rescan_manager = fsearch_database_rescan_manager_new(NULL,
                                                               on_index_scan_requested,
                                                               on_full_scan_requested,
                                                               self,
                                                               self->worker_ctx);
}

FsearchDatabase *
fsearch_database_new(GFile *file,
                     FsearchDatabaseIncludeManager *include_manager,
                     FsearchDatabaseExcludeManager *exclude_manager) {
    return g_object_new(FSEARCH_TYPE_DATABASE,
                        "file",
                        file,
                        "include_manager",
                        include_manager,
                        "exclude_manager",
                        exclude_manager,
                        NULL);
}

// endregion

// region Database public
void
fsearch_database_queue_work(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    // Publish scan cancellables at queue time, so cancel_scan() also reaches scans still waiting
    // in the work queue.
    const FsearchDatabaseWorkKind kind = fsearch_database_work_get_kind(work);
    if (kind == FSEARCH_DATABASE_WORK_SCAN || kind == FSEARCH_DATABASE_WORK_RESCAN
        || kind == FSEARCH_DATABASE_WORK_RESCAN_INDEX) {
        g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->scan_mutex);
        g_set_object(&self->scan_cancellable, cancellable);
    }

    DatabaseWorkWrapper *wrapper = db_work_wrapper_new(self, work);

    g_autoptr(GSource) idle_source = g_idle_source_new();
    g_source_set_priority(idle_source, G_PRIORITY_DEFAULT);
    g_source_set_callback(idle_source, handle_work_in_worker_thread_cb, wrapper, (GDestroyNotify)db_work_wrapper_free);
    g_source_attach(idle_source, self->worker_ctx);
}

void
fsearch_database_cancel_scan(FsearchDatabase *self) {
    g_return_if_fail(self);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->scan_mutex);

    if (self->scan_cancellable) {
        g_cancellable_cancel(self->scan_cancellable);
    }
}

FsearchResult
fsearch_database_try_get_search_info(FsearchDatabase *self, uint32_t view_id, FsearchDatabaseSearchInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_return_val_if_fail(self->store, FSEARCH_RESULT_FAILED);

    if (!fsearch_database_index_store_trylock(self->store)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    FsearchResult res = FSEARCH_RESULT_FAILED;
    g_autoptr(FsearchDatabaseSearchInfo) info = fsearch_database_index_store_get_search_info(self->store, view_id);
    if (!info) {
        res = FSEARCH_RESULT_DB_UNKNOWN_SEARCH_VIEW;
    }
    else {
        *info_out = g_steal_pointer(&info);
        res = FSEARCH_RESULT_SUCCESS;
    }

    fsearch_database_index_store_unlock(self->store);

    return res;
}

FsearchResult
fsearch_database_try_get_item_info(FsearchDatabase *self,
                                   uint32_t view_id,
                                   uint32_t idx,
                                   FsearchDatabaseEntryInfoFlags flags,
                                   FsearchDatabaseEntryInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    g_return_val_if_fail(self->store, FSEARCH_RESULT_FAILED);

    if (!fsearch_database_index_store_trylock(self->store)) {
        g_mutex_unlock(&self->mutex);
        return FSEARCH_RESULT_DB_BUSY;
    }

    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_get_item_info(view_id, idx, flags);
    FsearchResult res = database_get_entry_info_non_blocking(self, work, info_out);

    fsearch_database_index_store_unlock(self->store);
    g_mutex_unlock(&self->mutex);

    return res;
}

FsearchResult
fsearch_database_try_get_database_info(FsearchDatabase *self, FsearchDatabaseInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    g_return_val_if_fail(self->store, FSEARCH_RESULT_FAILED);

    if (!fsearch_database_index_store_trylock(self->store)) {
        g_mutex_unlock(&self->mutex);
        return FSEARCH_RESULT_DB_BUSY;
    }

    *info_out = database_get_info(self);

    fsearch_database_index_store_unlock(self->store);
    g_mutex_unlock(&self->mutex);

    return FSEARCH_RESULT_SUCCESS;
}

FsearchResult
fsearch_database_rescan_blocking(FsearchDatabase *self) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    database_rescan_sync(self,
                         self->include_manager,
                         self->exclude_manager,
                         DATABASE_INDEX_PROPERTY_FLAG_DEFAULT);

    return FSEARCH_RESULT_SUCCESS;
}

typedef struct {
    FsearchDatabaseForeachFunc func;
    gpointer user_data;
} FsearchDatabaseSelectionForeachContext;

static void
selection_foreach_cb(gpointer key, gpointer value, gpointer user_data) {
    FsearchDatabaseEntry *entry = value;
    if (G_UNLIKELY(!entry)) {
        return;
    }
    FsearchDatabaseSelectionForeachContext *ctx = user_data;
    ctx->func(entry, ctx->user_data);
}

void
fsearch_database_selection_foreach(FsearchDatabase *self,
                                   uint32_t view_id,
                                   FsearchDatabaseForeachFunc func,
                                   gpointer user_data) {
    g_return_if_fail(FSEARCH_IS_DATABASE(self));
    g_return_if_fail(func);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_return_if_fail(self->store);

    g_autoptr(GMutexLocker) store_locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(store_locker);

    FsearchDatabaseSelectionForeachContext ctx = {.func = func, .user_data = user_data};

    fsearch_database_index_store_selection_foreach(self->store, view_id, selection_foreach_cb, &ctx);
}

// endregion