#define G_LOG_DOMAIN "fsearch-database"

#include "fsearch_database.h"

#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_file.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_info.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_work.h"
#include "fsearch_query.h"
#include "fsearch_selection_type.h"
#include "fsearch_result.h"

#include <config.h>
#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#include <glib.h>
#include <glibconfig.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtkenums.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <glib/gi18n.h>

struct _FsearchDatabase {
    GObject parent_instance;

    // The database will be loaded from and saved to this file
    GFile *file;

    GThread *work_queue_thread;
    GAsyncQueue *work_queue;

    GThreadPool *io_pool;

    GCancellable *cancellable;

    FsearchDatabaseIndexStore *store;

    GMutex mutex;
};

G_DEFINE_TYPE(FsearchDatabase, fsearch_database, G_TYPE_OBJECT)

enum { PROP_0, PROP_FILE, NUM_PROPERTIES };

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
index_store_event_cb(FsearchDatabaseIndexStore *store, FsearchDatabaseIndexStoreEventKind kind, gpointer data, gpointer user_data);

// endregion

// region Signaling
typedef struct FsearchSignalEmitContext {
    FsearchDatabase *db;
    FsearchDatabaseSignalType type;
    gpointer arg1;
    gpointer arg2;
    GDestroyNotify arg1_free_func;
    GDestroyNotify arg2_free_func;
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
    g_clear_object(&ctx->db);
    g_clear_pointer(&ctx, free);
}

static FsearchSignalEmitContext *
signal_emit_context_new(FsearchDatabase *db,
                        FsearchDatabaseSignalType type,
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
signal_emit0(FsearchDatabase *self, FsearchDatabaseSignalType type) {
    if (g_cancellable_is_cancelled(self->cancellable)) {
        g_debug("signal_emit0: cancelling signal %s", signal_type_to_name(type));
        return;
    }
    g_idle_add(signal_emit_cb, signal_emit_context_new(self, type, NULL, NULL, 0, NULL, NULL));
}

static void
signal_emit(FsearchDatabase *self,
            FsearchDatabaseSignalType type,
            gpointer arg1,
            gpointer arg2,
            guint n_args,
            GDestroyNotify arg1_free_func,
            GDestroyNotify arg2_free_func) {
    if (g_cancellable_is_cancelled(self->cancellable)) {
        g_debug("signal_emit: cancelling signal %s", signal_type_to_name(type));
        return;
    }
    g_idle_add(signal_emit_cb, signal_emit_context_new(self, type, arg1, arg2, n_args, arg1_free_func, arg2_free_func));
}

static void
signal_emit_item_info_ready(FsearchDatabase *self, guint id, FsearchDatabaseEntryInfo *info) {
    signal_emit(self,
                SIGNAL_ITEM_INFO_READY,
                GUINT_TO_POINTER(id),
                info,
                2,
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
static void
database_unlock(FsearchDatabase *self) {
    g_assert(FSEARCH_IS_DATABASE(self));
    g_mutex_unlock(&self->mutex);
}

static void
database_lock(FsearchDatabase *self) {
    g_assert(FSEARCH_IS_DATABASE(self));
    g_mutex_lock(&self->mutex);
}

static FsearchDatabaseIndexPropertyFlags
database_get_flags(FsearchDatabase *self) {
    return self->store ? fsearch_database_index_store_get_flags(self->store) : DATABASE_INDEX_PROPERTY_FLAG_NONE;
}

static FsearchDatabaseExcludeManager *
database_get_exclude_manager(FsearchDatabase *self) {
    return self->store ? fsearch_database_index_store_get_exclude_manager(self->store) : NULL;
}

static FsearchDatabaseIncludeManager *
database_get_include_manager(FsearchDatabase *self) {
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

    const bool result = fsearch_database_index_store_search(self->store,
                                                            id,
                                                            query,
                                                            sort_order,
                                                            sort_type,
                                                            cancellable);

    signal_emit_search_finished(self, id, fsearch_database_index_store_get_search_info(self->store, id));

    return result;
}

static void
index_store_event_cb(FsearchDatabaseIndexStore *store, FsearchDatabaseIndexStoreEventKind kind, gpointer data, gpointer user_data) {
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

    signal_emit_selection_changed(self, fsearch_database_index_store_get_search_info(self->store, view_id));
}

static void
database_save(FsearchDatabase *self) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->file);
    g_return_if_fail(self->store);

    g_autofree char *file_path = g_file_get_path(self->file);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);
    fsearch_database_file_save(self->store, file_path);
}

void
io_thread_cb(gpointer data, gpointer user_data) {
    FsearchDatabaseWork *work = data;
    FsearchDatabase *db = user_data;
    g_return_if_fail(work);
    g_return_if_fail(db);

    bool queue_work = false;

    FsearchDatabaseWorkKind kind = fsearch_database_work_get_kind(work);
    switch (kind) {
    case FSEARCH_DATABASE_WORK_SCAN_FINISHED: {
        g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_work_scan_finished_get_index_store(work);
        fsearch_database_index_store_start(store, db->cancellable);
        queue_work = true;
        break;
    }
    case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED: {
        g_autoptr(FsearchDatabaseIndex) new_index = fsearch_database_work_rescan_index_finished_get_index(work);
        if (!fsearch_database_index_scan(new_index, db->cancellable)) {
            g_debug("[db] index rescan failed for index %u",
                    fsearch_database_index_get_id(new_index));
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
database_scan_finished(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_work_scan_finished_get_index_store(work);

    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = g_steal_pointer(&store);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    fsearch_database_index_store_start_monitoring(self->store);

#ifdef HAVE_MALLOC_TRIM
    malloc_trim(0);
#endif

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
                     const char *file_path,
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

    fsearch_database_index_store_start(store, NULL);

    g_clear_pointer(&db->store, fsearch_database_index_store_unref);
    db->store = fsearch_database_index_store_ref(store);
}

static void
database_rescan(FsearchDatabase *self) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->store);

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = NULL;
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = NULL;
    if (self->store) {
        g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
        g_assert_nonnull(locker);
        include_manager = fsearch_database_index_store_get_include_manager(self->store);
        exclude_manager = fsearch_database_index_store_get_exclude_manager(self->store);
    }
    else {
        include_manager = fsearch_database_include_manager_new_with_defaults();
        exclude_manager = fsearch_database_exclude_manager_new_with_defaults();
    }

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
        exclude_manager,
        database_get_flags(self),
        index_store_event_cb,
        self);
    g_return_if_fail(store);

    // The IO thread is expecting work objects -> use scan_finished kind to wrap the index store
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_scan_finished(
        store,
        (void *(*)(void *))fsearch_database_index_store_ref,
        (GDestroyNotify)fsearch_database_index_store_unref);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&work), NULL);
}

static void
database_rescan_index(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->store);
    g_return_if_fail(work);

    const uint32_t index_id = fsearch_database_work_rescan_index_get_id(work);

    g_autoptr(FsearchDatabaseIndex) new_index =
        fsearch_database_index_store_create_index_for_rescan(self->store, index_id);

    if (!new_index) {
        g_warning("[db] rescan_index: failed to create index for rescan (id=%u)", index_id);
        return;
    }

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    // Push to the IO pool to perform the actual scan
    g_autoptr(FsearchDatabaseWork) new_work = fsearch_database_work_new_rescan_index_finished(new_index);
    g_thread_pool_push(self->io_pool, g_steal_pointer(&new_work), NULL);
}

static void
database_rescan_index_finished(FsearchDatabase *self, FsearchDatabaseWork *work) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIndex) new_index =
        fsearch_database_work_rescan_index_finished_get_index(work);

    if (!self->store) {
        // The store was fully replaced by a concurrent full rescan - nothing to do.
        return;
    }
    signal_emit_database_progress(self, g_strdup(_("Index rescan: applying changes…")));

    if (!fsearch_database_index_store_replace_index(self->store, new_index)) {
        return;
    }

#ifdef HAVE_MALLOC_TRIM
    malloc_trim(0);
#endif

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

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_work_scan_get_include_manager(work);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_work_scan_get_exclude_manager(work);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_work_scan_get_flags(work);

    g_autoptr(GMutexLocker) locker = fsearch_database_index_store_get_locker(self->store);
    g_assert_nonnull(locker);

    if (self->store && fsearch_database_include_manager_equal(database_get_include_manager(self), include_manager)
        && fsearch_database_exclude_manager_equal(database_get_exclude_manager(self), exclude_manager)) {
        g_debug("[scan] new config is identical to the current one. No scan necessary.");
        return;
    }

    g_clear_pointer(&locker, g_mutex_locker_free);

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
        exclude_manager,
        flags,
        index_store_event_cb,
        self);
    g_return_if_fail(store);

    // The IO thread is expecting work objects -> use scan_finished kind to wrap the index store
    g_autoptr(FsearchDatabaseWork) new_work = fsearch_database_work_new_scan_finished(
        store,
        (void *(*)(void *))fsearch_database_index_store_ref,
        (GDestroyNotify)fsearch_database_index_store_unref);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&new_work), NULL);

}

static void
database_rescan_indices_if_needed(FsearchDatabase *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->store);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager =
        fsearch_database_index_store_get_include_manager(self->store);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);

    uint32_t num_active_monitored = 0;
    uint32_t num_active = 0;
    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        if (fsearch_database_include_get_active(include)) {
            num_active++;
            if (fsearch_database_include_get_monitored(include)) {
                num_active_monitored++;
            }
        }
    }

    // Don't rescan if there are no monitored indices
    if (num_active_monitored == 0) {
        return;
    }

    // If all indices are monitored, it's more efficient to rescan everything
    if (num_active_monitored == num_active) {
        g_debug("[db] all indices monitored, rescan everything.");
        database_rescan(self);
        return;
    }

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        if (fsearch_database_include_get_monitored(include) && fsearch_database_include_get_active(include)) {
            g_debug("[db] rescan index: %s", fsearch_database_include_get_path(include));
            g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_rescan_index(
                fsearch_database_include_get_id(include));
            database_rescan_index(self, work);
        }
    }
}

static void
database_load(FsearchDatabase *self) {
    // DB must be locked
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    signal_emit0(self, SIGNAL_LOAD_STARTED);

    g_autoptr(FsearchDatabaseIndexStore) store = NULL;
    g_autofree char *file_path = g_file_get_path(self->file);
    const bool res = fsearch_database_file_load(file_path, NULL, &store, index_store_event_cb, self);

    if (!res) {
        store = fsearch_database_index_store_new(fsearch_database_include_manager_new_with_defaults(),
                                                 fsearch_database_exclude_manager_new_with_defaults(),
                                                 database_get_flags(self),
                                                 index_store_event_cb,
                                                 self);
    }
    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = g_steal_pointer(&store);

    signal_emit(self,
                SIGNAL_LOAD_FINISHED,
                database_get_info(self),
                NULL,
                1,
                (GDestroyNotify)fsearch_database_info_unref,
                NULL);
}

static gpointer
database_work_queue_thread(gpointer data) {
    g_debug("[db_worker] worker thread started");
    FsearchDatabase *self = data;

    while (TRUE) {
        g_autoptr(FsearchDatabaseWork) work = g_async_queue_pop(self->work_queue);

        g_autoptr(GTimer) timer = g_timer_new();
        g_timer_start(timer);

        bool quit = false;

        // Ensure the database is locked while processing work
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
        g_assert_nonnull(locker);

        switch (fsearch_database_work_get_kind(work)) {
        case FSEARCH_DATABASE_WORK_QUIT:
            // Don't emit any signals here, as FsearchDatabase is about to be destroyed
            database_save(self);
            quit = true;
            break;
        case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
            database_load(self);
            database_rescan_indices_if_needed(self);
            break;
        case FSEARCH_DATABASE_WORK_GET_ITEM_INFO: {
            FsearchDatabaseEntryInfo *info = NULL;
            database_get_entry_info(self, work, &info);
            if (info) {
                signal_emit_item_info_ready(self, fsearch_database_work_get_view_id(work), g_steal_pointer(&info));
            }
            break;
        }
        case FSEARCH_DATABASE_WORK_RESCAN:
            database_rescan(self);
            break;
        case FSEARCH_DATABASE_WORK_RESCAN_INDEX:
            database_rescan_index(self, work);
            break;
        case FSEARCH_DATABASE_WORK_RESCAN_INDEX_FINISHED:
            database_rescan_index_finished(self, work);
            break;
        case FSEARCH_DATABASE_WORK_SAVE_TO_FILE:
            signal_emit0(self, SIGNAL_SAVE_STARTED);
            database_save(self);
            signal_emit0(self, SIGNAL_SAVE_FINISHED);
            break;
        case FSEARCH_DATABASE_WORK_SCAN:
            database_scan(self, work);
            break;
        case FSEARCH_DATABASE_WORK_SCAN_FINISHED:
            database_scan_finished(self, work);
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
        default:
            g_assert_not_reached();
        }

        g_debug("[db_worker] finished work '%s' in: %fs.",
                fsearch_database_work_to_string(work),
                g_timer_elapsed(timer, NULL));

        if (quit) {
            break;
        }
    }

    g_debug("[db_worker] worker thread returning");
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

    // Cancel ongoing work
    g_cancellable_cancel(self->cancellable);

    // Notify work queue thread to exit itself
    g_async_queue_push(self->work_queue, fsearch_database_work_new_quit());

    // Wait for the work queue thread to exit
    if (self->work_queue_thread) {
        g_thread_join(g_steal_pointer(&self->work_queue_thread));
    }

    // Exit io thread pool
    if (self->io_pool) {
        g_thread_pool_free(g_steal_pointer(&self->io_pool), TRUE, TRUE);
    }

    G_OBJECT_CLASS(fsearch_database_parent_class)->dispose(object);
}

static void
fsearch_database_finalize(GObject *object) {
    FsearchDatabase *self = (FsearchDatabase *)object;

    g_clear_pointer(&self->work_queue, g_async_queue_unref);

    g_clear_object(&self->file);

    // Don't use g_clear_pointer here since index_store_unref will access self->store while terminating
    fsearch_database_index_store_unref(self->store);
    self->store = NULL;

    g_clear_object(&self->cancellable);

    g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(fsearch_database_parent_class)->finalize(object);
}

static void
fsearch_database_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchDatabase *self = FSEARCH_DATABASE(object);

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
fsearch_database_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchDatabase *self = FSEARCH_DATABASE(object);

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
    signals[SIGNAL_SAVE_STARTED] = g_signal_new("save-started",
                                                G_TYPE_FROM_CLASS(klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_TYPE_NONE,
                                                0);
    signals[SIGNAL_SAVE_FINISHED] = g_signal_new("save-finished",
                                                 G_TYPE_FROM_CLASS(klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 G_TYPE_NONE,
                                                 0);
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
                                                   2,
                                                   G_TYPE_UINT,
                                                   FSEARCH_TYPE_DATABASE_ENTRY_INFO);
}

static void
fsearch_database_init(FsearchDatabase *self) {
    g_mutex_init(&self->mutex);
    self->cancellable = g_cancellable_new();
#if GLIB_CHECK_VERSION(2, 70, 0)
    self->io_pool = g_thread_pool_new_full(io_thread_cb,
                                           self,
                                           (GDestroyNotify)fsearch_database_work_unref,
                                           1,
                                           TRUE,
                                           NULL);
#else
    self->io_pool = g_thread_pool_new(io_thread_cb,
                                      self,
                                      1,
                                      TRUE,
                                      NULL);
#endif
    self->work_queue = g_async_queue_new_full((GDestroyNotify)fsearch_database_work_unref);
    self->work_queue_thread = g_thread_new("FsearchDatabaseWorkQueue", database_work_queue_thread, self);
}

FsearchDatabase *
fsearch_database_new(GFile *file) {
    return g_object_new(FSEARCH_TYPE_DATABASE, "file", file, NULL);
}

// endregion

// region Database public
void
fsearch_database_queue_work(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_async_queue_push(self->work_queue, fsearch_database_work_ref(work));
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

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_return_val_if_fail(self->store, FSEARCH_RESULT_FAILED);

    if (!fsearch_database_index_store_trylock(self->store)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_get_item_info(view_id, idx, flags);
    FsearchResult res = database_get_entry_info_non_blocking(self, work, info_out);

    fsearch_database_index_store_unlock(self->store);

    return res;
}

FsearchResult
fsearch_database_try_get_database_info(FsearchDatabase *self, FsearchDatabaseInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_return_val_if_fail(self->store, FSEARCH_RESULT_FAILED);

    if (!fsearch_database_index_store_trylock(self->store)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    *info_out = database_get_info(self);

    fsearch_database_index_store_unlock(self->store);

    return FSEARCH_RESULT_SUCCESS;
}

FsearchResult
fsearch_database_rescan_blocking(FsearchDatabase *self) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = NULL;
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = NULL;
    FsearchDatabaseIndexPropertyFlags flags = DATABASE_INDEX_PROPERTY_FLAG_NONE;
    fsearch_database_file_load_config(g_file_get_path(self->file), &include_manager, &exclude_manager, &flags);
    g_autofree char *path = g_file_get_path(self->file);
    database_rescan_sync(self, path, include_manager, exclude_manager, flags);

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