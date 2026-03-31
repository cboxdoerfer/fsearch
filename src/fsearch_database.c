#define G_LOG_DOMAIN "fsearch-database"

#include "fsearch_database.h"

#include "fsearch_array.h"
#include "fsearch_database_exclude.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_entries_container.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_include.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_database_info.h"
#include "fsearch_database_search.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_database_sort.h"
#include "fsearch_database_work.h"
#include "fsearch_query.h"
#include "fsearch_query_match_data.h"
#include "fsearch_selection.h"
#include "fsearch_selection_type.h"
#include "fsearch_result.h"
#include "fsearch_thread_pool.h"

#include <config.h>
#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#include <glib.h>
#include <glibconfig.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtkenums.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

struct _FsearchDatabase {
    GObject parent_instance;

    // The database will be loaded from and saved to this file
    GFile *file;

    GThread *work_queue_thread;
    GAsyncQueue *work_queue;

    GThreadPool *io_pool;
    FsearchThreadPool *thread_pool;

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
index_event_cb(FsearchDatabaseIndex *index, FsearchDatabaseIndexEvent *event, gpointer user_data);

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
                fsearch_database_search_info_ref(info),
                2,
                NULL,
                (GDestroyNotify)fsearch_database_search_info_unref);
}

static void
signal_emit_database_changed(FsearchDatabase *self, FsearchDatabaseInfo *info) {
    signal_emit(self, SIGNAL_DATABASE_CHANGED, info, NULL, 1, (GDestroyNotify)fsearch_database_info_unref, NULL);
}

// endregion

// region Database file

#define DATABASE_MAJOR_VERSION 2
#define DATABASE_MINOR_VERSION 2
#define DATABASE_MAGIC_NUMBER "FSDB"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FILE, fclose)

#define DB_WRITE_VAL_SIZED(fp, val, size, msg, msg_arg, failed_ptr, bytes_count)   \
    do {                                                                       \
        (bytes_count) +=                                                       \
            database_file_write_data(fp, &(val), size, 1, failed_ptr);  \
        if (*(failed_ptr)) {                                                   \
            g_debug("[db_save] " msg, msg_arg);                                \
            return bytes_count;                                                       \
        }                                                                      \
    } while (0)

#define DB_WRITE_VAL(fp, val, msg, msg_arg, failed_ptr, bytes_count)   \
    do {                                                                       \
        (bytes_count) +=                                                       \
            database_file_write_data(fp, &(val), sizeof(val), 1, failed_ptr);  \
        if (*(failed_ptr)) {                                                   \
            g_debug("[db_save] " msg, msg_arg);                                \
            return bytes_count;                                                       \
        }                                                                      \
    } while (0)

#define DB_WRITE_VAL_GOTO(fp, val, msg, msg_arg, failed_ptr, bytes_count, glabel)   \
    do {                                                                       \
        (bytes_count) +=                                                       \
            database_file_write_data(fp, &(val), sizeof(val), 1, failed_ptr);  \
        if (*(failed_ptr)) {                                                   \
            g_debug("[db_save] " msg, msg_arg);                                \
            goto glabel;                                                       \
        }                                                                      \
    } while (0)

#define DB_WRITE_STRING(fp, str, str_len, msg, msg_arg, failed_ptr, bytes_count)   \
    do {                                                                       \
        (bytes_count) +=                                                       \
            database_file_write_data(fp, &(str_len), sizeof(str_len), 1, failed_ptr);  \
        if (*(failed_ptr)) {                                                   \
            g_debug("[db_save] (str_len) " msg, msg_arg);                                \
            return bytes_count;                                                       \
        }                                                                      \
        (bytes_count) +=                                                       \
            database_file_write_data(fp, str, str_len, 1, failed_ptr);  \
        if (*(failed_ptr)) {                                                   \
            g_debug("[db_save] (str) " msg, msg_arg);                                \
            return bytes_count;                                                       \
        }                                                                      \
    } while (0)


typedef struct {
    DynamicArray *files[NUM_DATABASE_INDEX_PROPERTIES];
    DynamicArray *folders[NUM_DATABASE_INDEX_PROPERTIES];
    FsearchDatabaseIndexPropertyFlags flags;
} LoadSaveContext;

static void
update_folder_indices(DynamicArray *folders) {
    g_assert(folders);
    const uint32_t num_folders = darray_get_num_items(folders);
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders, i);
        if (!folder) {
            continue;
        }
        db_entry_set_index(folder, i);
    }
}

static uint16_t
get_name_offset(const char *old, const char *new) {
    if (!old || !new) {
        return 0;
    }

    uint16_t offset = 0;
    while (offset < UINT16_MAX
           && old[offset]
           && old[offset] == new[offset]
    ) {
        offset++;
    }
    return offset;
}

static FILE *
file_open_locked(const char *file_path, const char *mode) {
    FILE *file_pointer = fopen(file_path, mode);
    if (!file_pointer) {
        g_debug("[db_file] can't open database file: %s", file_path);
        return NULL;
    }

    int file_descriptor = fileno(file_pointer);
    if (flock(file_descriptor, LOCK_EX | LOCK_NB) == -1) {
        g_debug("[db_file] database file is already locked by a different process: %s", file_path);

        g_clear_pointer(&file_pointer, fclose);
    }

    return file_pointer;
}

static const uint8_t *
copy_bytes_and_return_new_src(void *dest, const uint8_t *src, size_t len) {
    memcpy(dest, src, len);
    return src + len;
}

static const uint8_t *
database_file_load_entry(const uint8_t *data_block,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         GString *previous_entry_name,
                         FsearchDatabaseEntry **entry_out,
                         FsearchDatabaseEntryType type) {
    // name_offset: character position after which previous_entry_name and entry_name differ
    uint16_t name_offset = 0;
    data_block = copy_bytes_and_return_new_src(&name_offset, data_block, sizeof(name_offset));

    // name_len: length of the new name characters
    uint16_t name_len = 0;
    data_block = copy_bytes_and_return_new_src(&name_len, data_block, sizeof(name_len));

    // erase previous name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);

    // name: new characters to be appended to previous_entry_name
    if (name_len > 0) {
        g_string_append_len(previous_entry_name, (const char *)data_block, name_len);
        data_block += name_len;
    }

    *entry_out = db_entry_new(index_flags, previous_entry_name->str, NULL, type);
    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        // size: size of file/folder
        int64_t size = 0;
        data_block = copy_bytes_and_return_new_src(&size, data_block, sizeof(size));

        db_entry_set_size(*entry_out, (off_t)size);
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time file/folder
        int64_t mtime = 0;
        data_block = copy_bytes_and_return_new_src(&mtime, data_block, sizeof(mtime));

        db_entry_set_mtime(*entry_out, (time_t)mtime);
    }

    return data_block;
}

static bool
database_file_read_element(void *restrict ptr, size_t size, FILE *restrict stream) {
    return fread(ptr, size, 1, stream) == 1 ? true : false;
}

static bool
database_file_load_header(FILE *fp) {
    char magic[5] = "";
    if (!database_file_read_element(magic, strlen(DATABASE_MAGIC_NUMBER), fp)) {
        return false;
    }
    magic[4] = '\0';
    if (strcmp(magic, DATABASE_MAGIC_NUMBER) != 0) {
        g_debug("[db_load] invalid magic number: %s", magic);
        return false;
    }

    uint8_t majorver = 0;
    if (!database_file_read_element(&majorver, 1, fp)) {
        return false;
    }
    if (majorver != DATABASE_MAJOR_VERSION) {
        g_debug("[db_load] invalid major version: %d", majorver);
        g_debug("[db_load] expected major version: %d", DATABASE_MAJOR_VERSION);
        return false;
    }

    uint8_t minorver = 0;
    if (!database_file_read_element(&minorver, 1, fp)) {
        return false;
    }
    if (minorver > DATABASE_MINOR_VERSION) {
        g_debug("[db_load] invalid minor version: %d", minorver);
        g_debug("[db_load] expected minor version: <= %d", DATABASE_MINOR_VERSION);
        return false;
    }

    return true;
}

static void
database_file_load_add_to_index_array(GHashTable *index_table, FsearchDatabaseEntry *entry) {
    const uint32_t db_index = db_entry_get_db_index(entry);
    DynamicArray *index_array = g_hash_table_lookup(index_table, GUINT_TO_POINTER(db_index));

    if (!index_array) {
        index_array = darray_new(1024);
        g_hash_table_insert(index_table, GUINT_TO_POINTER(db_index), index_array);
    }
    darray_add_item(index_array, entry);
}

static bool
database_file_load_folders(FILE *fp,
                           FsearchDatabaseIndexPropertyFlags index_flags,
                           DynamicArray *folders,
                           uint32_t num_folders,
                           uint64_t folder_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(1024);

    g_autofree uint8_t *folder_block = calloc(folder_block_size + 1, sizeof(uint8_t));
    g_assert(folder_block);

    if (fread(folder_block, sizeof(uint8_t), folder_block_size, fp) != folder_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    const uint8_t *fb = folder_block;
    // load folders
    uint32_t idx = 0;
    for (idx = 0; idx < num_folders; idx++) {
        FsearchDatabaseEntry *folder = NULL;

        // db_index: the database index this folder belongs to
        uint16_t db_index = 0;
        fb = copy_bytes_and_return_new_src(&db_index, fb, 2);

        fb = database_file_load_entry(fb,
                                      index_flags,
                                      previous_entry_name,
                                      &folder,
                                      DATABASE_ENTRY_TYPE_FOLDER);

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        fb = copy_bytes_and_return_new_src(&parent_idx, fb, 4);

        if (index_flags & DATABASE_INDEX_PROPERTY_FLAG_DB_INDEX) {
            db_entry_set_db_index(folder, db_index);
        }

        if (parent_idx != idx) {
            // Until all folders are ready, we have to reference parents by their index
            db_entry_set_parent_no_update(folder, GINT_TO_POINTER(parent_idx));
        }
        else {
            // parent_idx and idx are the same (i.e. folder is a root index) so it has no parent
            db_entry_set_parent_no_update(folder, GINT_TO_POINTER(-1));
        }
        darray_add_item(folders, folder);
    }

    // fail if we didn't read the correct number of bytes
    if (fb - folder_block != folder_block_size) {
        g_debug("[db_load] wrong amount of memory read: %zd != %" PRIu64, fb - folder_block, folder_block_size);
        return false;
    }

    // fail if we didn't read the correct number of folders
    if (idx != num_folders) {
        g_debug("[db_load] failed to read folders (read %d of %d)", idx, num_folders);
        return false;
    }

    return true;
}

static bool
database_file_load_files(FILE *fp,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         DynamicArray *folders,
                         DynamicArray *files,
                         uint32_t num_files,
                         uint64_t file_block_size) {
    g_autoptr(GString) previous_entry_name = g_string_sized_new(1024);
    g_autofree uint8_t *file_block = calloc(file_block_size + 1, sizeof(uint8_t));
    g_assert(file_block);

    if (fread(file_block, sizeof(uint8_t), file_block_size, fp) != file_block_size) {
        g_debug("[db_load] failed to read file block");
        return false;
    }

    const uint8_t *fb = file_block;
    // load files
    uint32_t idx = 0;
    for (idx = 0; idx < num_files; idx++) {
        FsearchDatabaseEntry *entry = NULL;
        fb = database_file_load_entry(fb,
                                      index_flags,
                                      previous_entry_name,
                                      &entry,
                                      DATABASE_ENTRY_TYPE_FILE);

        // parent_idx: index of parent folder
        uint32_t parent_idx = 0;
        fb = copy_bytes_and_return_new_src(&parent_idx, fb, 4);

        FsearchDatabaseEntry *parent = darray_get_item(folders, parent_idx);
        db_entry_set_parent(entry, parent);

        darray_add_item(files, entry);
    }
    if (fb - file_block != file_block_size) {
        g_debug("[db_load] wrong amount of memory read: %zd != %" PRIu64, fb - file_block, file_block_size);
        return false;
    }

    // fail if we didn't read the correct number of files
    if (idx != num_files) {
        g_debug("[db_load] failed to read files (read %d of %d)", idx, num_files);
        return false;
    }

    return true;
}

static bool
database_file_load_sorted_entries(FILE *fp, DynamicArray *src, uint32_t num_src_entries, DynamicArray *dest) {
    g_autofree uint32_t *indexes = calloc(num_src_entries + 1, sizeof(uint32_t));
    g_assert(indexes);

    if (fread(indexes, 4, num_src_entries, fp) != num_src_entries) {
        return false;
    }

    for (uint32_t i = 0; i < num_src_entries; i++) {
        uint32_t idx = indexes[i];
        void *entry = darray_get_item(src, idx);
        if (!entry) {
            return false;
        }
        darray_add_item(dest, entry);
    }

    return true;
}

static bool
database_file_load_sorted_arrays(FILE *fp, DynamicArray **sorted_folders, DynamicArray **sorted_files) {
    uint32_t num_sorted_arrays = 0;

    DynamicArray *files = sorted_files[DATABASE_INDEX_PROPERTY_NAME];
    DynamicArray *folders = sorted_folders[DATABASE_INDEX_PROPERTY_NAME];

    if (!database_file_read_element(&num_sorted_arrays, 4, fp)) {
        g_debug("[db_load] failed to load number of sorted arrays");
        return false;
    }

    for (uint32_t i = 0; i < num_sorted_arrays; i++) {
        uint32_t sorted_array_id = 0;
        if (!database_file_read_element(&sorted_array_id, 4, fp)) {
            g_debug("[db_load] failed to load sorted array id");
            return false;
        }

        if (sorted_array_id < 1 || sorted_array_id >= NUM_DATABASE_INDEX_PROPERTIES) {
            g_debug("[db_load] sorted array id is not supported: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_folders = darray_get_num_items(folders);
        sorted_folders[sorted_array_id] = darray_new(num_folders);
        if (!database_file_load_sorted_entries(fp, folders, num_folders, sorted_folders[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted folder indexes: %d", sorted_array_id);
            return false;
        }

        const uint32_t num_files = darray_get_num_items(files);
        sorted_files[sorted_array_id] = darray_new(num_files);
        if (!database_file_load_sorted_entries(fp, files, num_files, sorted_files[sorted_array_id])) {
            g_debug("[db_load] failed to load sorted file indexes: %d", sorted_array_id);
            return false;
        }
    }

    return true;
}

static char *
database_file_read_string(FILE *fp, size_t max_size) {
    uint32_t string_len = 0;
    if (!database_file_read_element(&string_len, sizeof(string_len), fp)) {
        return NULL;
    }
    if (string_len > max_size) {
        return NULL;
    }

    g_autofree char *path = calloc(string_len + 1, sizeof(char));
    if (!database_file_read_element(path, string_len, fp)) {
        return NULL;
    }
    return g_steal_pointer(&path);
}

static bool
database_file_load_includes(FILE *fp, FsearchDatabaseIncludeManager *include_manager) {
    uint32_t num_includes = 0;
    if (!database_file_read_element(&num_includes, sizeof(num_includes), fp)) {
        g_debug("[db_load] failed to read number of includes");
        return false;
    }

    for (int i = 0; i < num_includes; ++i) {
        uint32_t type = 0;
        if (!database_file_read_element(&type, sizeof(type), fp)) {
            g_debug("[db_load] failed to read type of include");
            return false;
        }

        int32_t id = 0;
        if (!database_file_read_element(&id, sizeof(id), fp)) {
            g_debug("[db_load] failed to read id of include");
            return false;
        }

        g_autofree char *path = database_file_read_string(fp, 4 * PATH_MAX);
        if (!path) {
            g_debug("[db_load] failed to read path of include");
            return false;
        }

        uint8_t one_file_system = 0;
        if (!database_file_read_element(&one_file_system, sizeof(one_file_system), fp)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t is_active = 0;
        if (!database_file_read_element(&is_active, sizeof(is_active), fp)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t is_monitored = 0;
        if (!database_file_read_element(&is_monitored, sizeof(is_monitored), fp)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        uint8_t scan_after_launch = 0;
        if (!database_file_read_element(&scan_after_launch, sizeof(scan_after_launch), fp)) {
            g_debug("[db_load] failed to read path_len of include");
            return false;
        }

        g_autoptr(FsearchDatabaseInclude) include = fsearch_database_include_new(
            path,
            is_active,
            one_file_system,
            is_monitored,
            scan_after_launch,
            id);
        fsearch_database_include_manager_add(include_manager, include);
    }
    return true;
}

static bool
database_file_load_excludes(FILE *fp, FsearchDatabaseExcludeManager *exclude_manager) {
    uint32_t num_excludes = 0;
    if (!database_file_read_element(&num_excludes, sizeof(num_excludes), fp)) {
        g_debug("[db_load] failed to read number of excludes");
        return false;
    }

    for (int i = 0; i < num_excludes; ++i) {
        uint32_t record_type = 0;
        if (!database_file_read_element(&record_type, sizeof(record_type), fp)) {
            g_debug("[db_load] failed to read type of exclude");
            return false;
        }

        uint8_t exclude_type = FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED;
        uint8_t scope = FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH;
        uint8_t target = FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH;
        if (!database_file_read_element(&exclude_type, sizeof(exclude_type), fp)) {
            g_debug("[db_load] failed to read exclude type");
            return false;
        }
        if (!database_file_read_element(&scope, sizeof(scope), fp)) {
            g_debug("[db_load] failed to read exclude scope");
            return false;
        }
        if (!database_file_read_element(&target, sizeof(target), fp)) {
            g_debug("[db_load] failed to read exclude target");
            return false;
        }

        g_autofree char *pattern = database_file_read_string(fp, 4 * PATH_MAX);
        if (!pattern) {
            g_debug("[db_load] failed to read pattern of exclude");
            return false;
        }

        uint8_t is_active = 0;
        if (!database_file_read_element(&is_active, sizeof(is_active), fp)) {
            g_debug("[db_load] failed to read path_len of exclude");
            return false;
        }

        g_autoptr(FsearchDatabaseExclude) exclude = fsearch_database_exclude_new(pattern,
            is_active,
            (FsearchDatabaseExcludeType)exclude_type,
            (FsearchDatabaseExcludeMatchScope)scope,
            (FsearchDatabaseExcludeTarget)target);
        fsearch_database_exclude_manager_add(exclude_manager, exclude);
    }

    uint8_t exclude_hidden = 0;
    if (!database_file_read_element(&exclude_hidden, sizeof(exclude_hidden), fp)) {
        g_debug("[db_load] failed to read exclude hidden setting");
        return false;
    }
    fsearch_database_exclude_manager_set_exclude_hidden(exclude_manager, exclude_hidden);

    return true;
}

static size_t
database_file_write_data(FILE *fp, const void *data, size_t data_size, size_t num_elements, bool *write_failed) {
    if (data_size == 0 || num_elements == 0) {
        return 0;
    }
    if (fwrite(data, data_size, num_elements, fp) != num_elements) {
        *write_failed = true;
        return 0;
    }
    return data_size * num_elements;
}

static size_t
database_file_save_entry(FILE *fp,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         FsearchDatabaseEntry *entry,
                         uint32_t parent_idx,
                         GString *previous_entry_name,
                         GString *new_entry_name,
                         bool *write_failed) {
    // init new_entry_name with the name of the current entry
    g_string_assign(new_entry_name, db_entry_get_name_raw(entry));

    size_t bytes_written = 0;
    // name_offset: character position after which previous_entry_name and new_entry_name differ
    const uint16_t name_offset = get_name_offset(previous_entry_name->str, new_entry_name->str);
    DB_WRITE_VAL(fp, name_offset, "failed to write name_offset: %d", name_offset, write_failed, bytes_written);

    // name_len: length of the new name characters
    const uint16_t name_len = new_entry_name->len - name_offset;
    bytes_written += database_file_write_data(fp, &name_len, sizeof(name_len), 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save name length");
        return bytes_written;
    }

    // append new unique characters to previous_entry_name starting at name_offset
    g_string_erase(previous_entry_name, name_offset, -1);
    g_string_append(previous_entry_name, new_entry_name->str + name_offset);

    if (name_len > 0) {
        // name: new characters to be written to file
        const char *name = previous_entry_name->str + name_offset;
        bytes_written += database_file_write_data(fp, name, name_len, 1, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save name");
            return bytes_written;
        }
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_SIZE) != 0) {
        // size: file or folder size (folder size: sum of all children sizes)
        const uint64_t size = db_entry_get_size(entry);
        DB_WRITE_VAL(fp, size, "failed to write size: %d", size, write_failed, bytes_written);
    }

    if ((index_flags & DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME) != 0) {
        // mtime: modification time of file/folder
        const uint64_t mtime = db_entry_get_mtime(entry);
        DB_WRITE_VAL(fp, mtime, "failed to write mtime: %d", mtime, write_failed, bytes_written);
    }

    // parent_idx: index of parent folder
    DB_WRITE_VAL(fp, parent_idx, "failed to write parent_idx: %d", parent_idx, write_failed, bytes_written);

    return bytes_written;
}

static size_t
database_file_save_header(FILE *fp, bool *write_failed) {
    size_t bytes_written = 0;

    const char magic[] = DATABASE_MAGIC_NUMBER;
    bytes_written += database_file_write_data(fp, magic, strlen(magic), 1, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save magic number");
        return bytes_written;
    }

    const uint8_t majorver = DATABASE_MAJOR_VERSION;
    DB_WRITE_VAL(fp, majorver, "failed to write major version: %d", majorver, write_failed, bytes_written);

    const uint8_t minorver = DATABASE_MINOR_VERSION;
    DB_WRITE_VAL(fp, minorver, "failed to write minor version: %d", minorver, write_failed, bytes_written);

    return bytes_written;
}

static size_t
database_file_save_files(FILE *fp,
                         FsearchDatabaseIndexPropertyFlags index_flags,
                         DynamicArray *files,
                         uint32_t num_files,
                         bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(GString) name_prev = g_string_sized_new(1024);
    g_autoptr(GString) name_new = g_string_sized_new(1024);

    for (uint32_t i = 0; i < num_files; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(files, i);

        db_entry_set_index(entry, i);
        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = db_entry_get_index(parent);
        bytes_written += database_file_save_entry(fp,
                                                  index_flags,
                                                  entry,
                                                  parent_idx,
                                                  name_prev,
                                                  name_new,
                                                  write_failed);
        if (*write_failed == true)
            return bytes_written;
    }
    return bytes_written;
}

static uint32_t *
build_sorted_entry_index_list(DynamicArray *entries, uint32_t num_entries) {
    if (num_entries < 1) {
        return NULL;
    }
    uint32_t *indexes = calloc(num_entries + 1, sizeof(uint32_t));
    g_assert(indexes);

    for (int i = 0; i < num_entries; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
        indexes[i] = db_entry_get_index(entry);
    }
    return indexes;
}

static size_t
database_file_save_sorted_entries(FILE *fp, DynamicArray *entries, uint32_t num_entries, bool *write_failed) {
    if (num_entries < 1) {
        // nothing to write, we're done here
        return 0;
    }

    g_autofree uint32_t *sorted_entry_index_list = build_sorted_entry_index_list(entries, num_entries);
    if (!sorted_entry_index_list) {
        *write_failed = true;
        g_debug("[db_save] failed to create sorted index list");
        return 0;
    }

    size_t bytes_written = database_file_write_data(fp, sorted_entry_index_list, 4, num_entries, write_failed);
    if (*write_failed == true) {
        g_debug("[db_save] failed to save sorted index list");
    }

    return bytes_written;
}

static size_t
database_file_save_sorted_arrays(FILE *fp,
                                 FsearchDatabaseIndexStore *store,
                                 uint32_t num_files,
                                 uint32_t num_folders,
                                 bool *write_failed) {
    size_t bytes_written = 0;
    uint32_t num_sorted_arrays = fsearch_database_index_store_get_num_fast_sort_indices(store);

    DB_WRITE_VAL(fp,
                 num_sorted_arrays,
                 "failed to write number of sorted arrays: %d",
                 num_sorted_arrays,
                 write_failed,
                 bytes_written);

    if (num_sorted_arrays < 1) {
        return bytes_written;
    }

    for (uint32_t id = DATABASE_INDEX_PROPERTY_NAME; id < NUM_DATABASE_INDEX_PROPERTIES; id++) {
        g_autoptr(FsearchDatabaseEntriesContainer) folder_container =
            fsearch_database_index_store_get_folders(store, id);
        g_autoptr(FsearchDatabaseEntriesContainer) file_container = fsearch_database_index_store_get_files(store, id);
        if (!folder_container || !file_container) {
            continue;
        }
        g_autoptr(DynamicArray) folders = fsearch_database_entries_container_get_joined(folder_container);
        g_autoptr(DynamicArray) files = fsearch_database_entries_container_get_joined(file_container);
        if (!files || !folders) {
            continue;
        }

        // id: this is the id of the sorted files
        DB_WRITE_VAL(fp, id, "failed to write sorted arrays id: %d", id, write_failed, bytes_written);

        bytes_written += database_file_save_sorted_entries(fp, folders, num_folders, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save sorted folders");
            return bytes_written;
        }
        bytes_written += database_file_save_sorted_entries(fp, files, num_files, write_failed);
        if (*write_failed == true) {
            g_debug("[db_save] failed to save sorted files");
            return bytes_written;
        }
    }

    return bytes_written;
}

static size_t
database_file_save_folders(FILE *fp,
                           FsearchDatabaseIndexPropertyFlags index_flags,
                           DynamicArray *folders,
                           uint32_t num_folders,
                           bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(GString) name_prev = g_string_sized_new(1024);
    g_autoptr(GString) name_new = g_string_sized_new(1024);

    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *entry = darray_get_item(folders, i);

        const uint16_t db_index = db_entry_get_db_index(entry);
        DB_WRITE_VAL(fp, db_index, "failed to write db_index: %d", db_index, write_failed, bytes_written);

        FsearchDatabaseEntry *parent = db_entry_get_parent(entry);
        const uint32_t parent_idx = parent ? db_entry_get_index(parent) : db_entry_get_index(entry);
        bytes_written += database_file_save_entry(fp,
                                                  index_flags,
                                                  entry,
                                                  parent_idx,
                                                  name_prev,
                                                  name_new,
                                                  write_failed);
        if (*write_failed == true) {
            return bytes_written;
        }
    }

    return bytes_written;
}

static size_t
database_file_save_includes(FILE *fp, FsearchDatabaseIndexStore *store, bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_index_store_get_include_manager(store);
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    const uint32_t num_includes = includes->len;
    DB_WRITE_VAL(fp, num_includes, "failed to write num_includes: %d", num_includes, write_failed, bytes_written);

    for (int i = 0; i < num_includes; ++i) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);

        const uint32_t type = 0;
        DB_WRITE_VAL(fp, type, "failed to write index type: %d", type, write_failed, bytes_written);

        const int32_t id = fsearch_database_include_get_id(include);
        DB_WRITE_VAL(fp, id, "failed to write include id: %d", id, write_failed, bytes_written);

        const char *path = fsearch_database_include_get_path(include);
        const uint32_t path_len = strlen(path);
        DB_WRITE_STRING(fp, path, path_len, "failed to write include path: %s", path, write_failed, bytes_written);

        const uint8_t one_file_system = fsearch_database_include_get_one_file_system(include);
        DB_WRITE_VAL(fp,
                     one_file_system,
                     "failed to write include one_file_system: %d",
                     one_file_system,
                     write_failed,
                     bytes_written);
        const uint8_t is_active = fsearch_database_include_get_active(include);
        DB_WRITE_VAL(fp, is_active, "failed to write include is_active: %d", is_active, write_failed, bytes_written);
        const uint8_t is_monitored = fsearch_database_include_get_monitored(include);
        DB_WRITE_VAL(fp,
                     is_monitored,
                     "failed to write include is_monitored: %d",
                     is_monitored,
                     write_failed,
                     bytes_written);
        const uint8_t scan_after_launch = fsearch_database_include_get_scan_after_launch(include);
        DB_WRITE_VAL(fp,
                     scan_after_launch,
                     "failed to write include scan_after_launch: %d",
                     scan_after_launch,
                     write_failed,
                     bytes_written);
    }
    return bytes_written;
}

static size_t
database_file_save_excludes(FILE *fp, FsearchDatabaseIndexStore *store, bool *write_failed) {
    size_t bytes_written = 0;

    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_index_store_get_exclude_manager(store);
    g_autoptr(GPtrArray) excludes = fsearch_database_exclude_manager_get_excludes(exclude_manager);
    const uint32_t num_excludes = excludes->len;
    DB_WRITE_VAL(fp, num_excludes, "failed to write num_excludes: %d", num_excludes, write_failed, bytes_written);

    for (int i = 0; i < num_excludes; ++i) {
        FsearchDatabaseExclude *exclude = g_ptr_array_index(excludes, i);

        const uint32_t record_type = 0;
        DB_WRITE_VAL(fp,
                     record_type,
                     "failed to write exclude record type: %d",
                     record_type,
                     write_failed,
                     bytes_written);

        const uint8_t exclude_type = fsearch_database_exclude_get_exclude_type(exclude);
        DB_WRITE_VAL(fp,
                     exclude_type,
                     "failed to write exclude type: %d",
                     exclude_type,
                     write_failed,
                     bytes_written);
        const uint8_t match_scope = fsearch_database_exclude_get_match_scope(exclude);
        DB_WRITE_VAL(fp,
                     match_scope,
                     "failed to write exclude scope: %d",
                     match_scope,
                     write_failed,
                     bytes_written);
        const uint8_t target = fsearch_database_exclude_get_target(exclude);
        DB_WRITE_VAL(fp, target, "failed to write exclude target: %d", target, write_failed, bytes_written);

        const char *pattern = fsearch_database_exclude_get_pattern(exclude);
        const uint32_t pattern_len = strlen(pattern);
        DB_WRITE_STRING(fp,
                        pattern,
                        pattern_len,
                        "failed to write exclude pattern: %s",
                        pattern,
                        write_failed,
                        bytes_written);

        const uint8_t is_active = fsearch_database_exclude_get_active(exclude);
        DB_WRITE_VAL(fp, is_active, "failed to write exclude is_active: %d", is_active, write_failed, bytes_written);
    }

    const uint8_t exclude_hidden = fsearch_database_exclude_manager_get_exclude_hidden(exclude_manager);
    DB_WRITE_VAL(fp,
                 exclude_hidden,
                 "failed to write exclude_hidden setting: %d",
                 exclude_hidden,
                 write_failed,
                 bytes_written);

    return bytes_written;
}

static bool
database_file_save(FsearchDatabaseIndexStore *store, const char *file_path) {
    g_return_val_if_fail(file_path, false);
    g_return_val_if_fail(store, false);

    g_debug("[db_save] saving database to file...");

    g_autoptr(GTimer) timer = g_timer_new();
    g_timer_start(timer);

    g_autoptr(GString) file_tmp_path = g_string_new(file_path);
    g_string_append(file_tmp_path, ".tmp");

    g_autoptr(FsearchDatabaseEntriesContainer) folder_container = NULL;
    g_autoptr(FsearchDatabaseEntriesContainer) file_container = NULL;

    g_autoptr(DynamicArray) files = NULL;
    g_autoptr(DynamicArray) folders = NULL;

    g_debug("[db_save] trying to open temporary database file: %s", file_tmp_path->str);

    g_autoptr(FILE) fp = file_open_locked(file_tmp_path->str, "wb");
    if (!fp) {
        g_debug("[db_save] failed to open temporary database file: %s", file_tmp_path->str);
        goto save_fail;
    }

    bool write_failed = false;

    size_t bytes_written = 0;

    g_debug("[db_save] saving database header...");
    bytes_written += database_file_save_header(fp, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    g_debug("[db_save] saving database index flags...");
    const uint64_t index_flags = fsearch_database_index_store_get_flags(store);
    DB_WRITE_VAL_GOTO(fp,
                      index_flags,
                      "failed to write index flags: %d",
                      index_flags,
                      &write_failed,
                      bytes_written,
                      save_fail);

    g_debug("[db_save] updating folder indices...");
    folder_container = fsearch_database_index_store_get_folders(store, DATABASE_INDEX_PROPERTY_NAME);
    folders = fsearch_database_entries_container_get_joined(folder_container);
    update_folder_indices(folders);

    const uint32_t num_folders = darray_get_num_items(folders);
    g_debug("[db_save] saving number of folders: %d", num_folders);
    DB_WRITE_VAL_GOTO(fp,
                      num_folders,
                      "failed to write num folders: %d",
                      num_folders,
                      &write_failed,
                      bytes_written,
                      save_fail);

    file_container = fsearch_database_index_store_get_files(store, DATABASE_INDEX_PROPERTY_NAME);
    files = fsearch_database_entries_container_get_joined(file_container);

    const uint32_t num_files = darray_get_num_items(files);
    g_debug("[db_save] saving number of files: %d", num_files);
    DB_WRITE_VAL_GOTO(fp,
                      num_files,
                      "failed to write num files: %d",
                      num_files,
                      &write_failed,
                      bytes_written,
                      save_fail);

    uint64_t folder_block_size = 0;
    const uint64_t folder_block_size_offset = bytes_written;
    g_debug("[db_save] saving folder block size...");
    DB_WRITE_VAL_GOTO(fp,
                      folder_block_size,
                      "failed to write folder block size: %d",
                      folder_block_size,
                      &write_failed,
                      bytes_written,
                      save_fail);

    uint64_t file_block_size = 0;
    const uint64_t file_block_size_offset = bytes_written;
    g_debug("[db_save] saving file block size...");
    DB_WRITE_VAL_GOTO(fp,
                      file_block_size,
                      "failed to write file block size: %d",
                      file_block_size,
                      &write_failed,
                      bytes_written,
                      save_fail);

    g_debug("[db_save] saving indices...");
    bytes_written += database_file_save_includes(fp, store, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving excludes...");
    bytes_written += database_file_save_excludes(fp, store, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving folders...");
    folder_block_size = database_file_save_folders(fp, index_flags, folders, num_folders, &write_failed);
    bytes_written += folder_block_size;
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving files...");
    file_block_size = database_file_save_files(fp, index_flags, files, num_files, &write_failed);
    bytes_written += file_block_size;
    if (write_failed == true) {
        goto save_fail;
    }
    g_debug("[db_save] saving sorted arrays...");
    bytes_written += database_file_save_sorted_arrays(fp, store, num_files, num_folders, &write_failed);
    if (write_failed == true) {
        goto save_fail;
    }

    // now that we know the size of the file/folder block we've written, store it in the file header
    if (fseek(fp, (long int)folder_block_size_offset, SEEK_SET) != 0) {
        goto save_fail;
    }
    g_debug("[db_save] updating file and folder block size: %" PRIu64 ", %" PRIu64, folder_block_size, file_block_size);
    DB_WRITE_VAL_GOTO(fp,
                      folder_block_size,
                      "failed to update folder block size: %d",
                      folder_block_size,
                      &write_failed,
                      bytes_written,
                      save_fail);
    DB_WRITE_VAL_GOTO(fp,
                      file_block_size,
                      "failed to update file block size: %d",
                      file_block_size,
                      &write_failed,
                      bytes_written,
                      save_fail);

    g_debug("[db_save] removing current database file...");
    // remove current database file
    unlink(file_path);

    g_clear_pointer(&fp, fclose);

    g_debug("[db_save] renaming temporary database file: %s -> %s", file_tmp_path->str, file_path);
    // rename temporary fsearch.db.tmp to fsearch.db
    if (rename(file_tmp_path->str, file_path) != 0) {
        goto save_fail;
    }

    const double seconds = g_timer_elapsed(timer, NULL);
    g_timer_stop(timer);

    g_debug("[db_save] database file saved in: %f ms", seconds * 1000);

    return true;

save_fail:
    g_warning("[db_save] saving failed");

    // remove temporary fsearch.db.tmp file
    unlink(file_tmp_path->str);

    return false;
}

static bool
database_file_load_config(const char *file_path,
                          FsearchDatabaseIncludeManager **include_manager_out,
                          FsearchDatabaseExcludeManager **exclude_manager_out,
                          FsearchDatabaseIndexPropertyFlags *flags_out) {
    g_return_val_if_fail(file_path, false);

    g_autoptr(FILE) fp = file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();

    if (!database_file_load_header(fp)) {
        goto load_fail;
    }

    uint64_t index_flags = 0;
    if (!database_file_read_element(&index_flags, sizeof(index_flags), fp)) {
        g_debug("[db_load] failed to read index flags");
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (!database_file_read_element(&num_folders, sizeof(num_folders), fp)) {
        g_debug("[db_load] failed to read num_folders");
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (!database_file_read_element(&num_files, sizeof(num_files), fp)) {
        g_debug("[db_load] failed to read num_files");
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    uint64_t folder_block_size = 0;
    if (!database_file_read_element(&folder_block_size, sizeof(folder_block_size), fp)) {
        g_debug("[db_load] failed to read folder block size");
        goto load_fail;
    }

    uint64_t file_block_size = 0;
    if (!database_file_read_element(&file_block_size, sizeof(file_block_size), fp)) {
        g_debug("[db_load] loading file block size: %" PRIu64, file_block_size);
        goto load_fail;
    }
    g_debug("[db_load] folder size: %" PRIu64 ", file size: %" PRIu64, folder_block_size, file_block_size);

    if (!database_file_load_includes(fp, include_manager)) {
        g_debug("[db_load] failed to load includes");
        goto load_fail;
    }
    if (!database_file_load_excludes(fp, exclude_manager)) {
        g_debug("[db_load] excludes not loaded");
        goto load_fail;
    }

    *include_manager_out = g_steal_pointer(&include_manager);
    *exclude_manager_out = g_steal_pointer(&exclude_manager);
    *flags_out = index_flags;

    g_clear_pointer(&fp, fclose);

    return true;

load_fail:
    g_debug("[db_load] load failed");

    return false;

}

static bool
database_file_load(FsearchDatabase *self,
                   const char *file_path,
                   void (*status_cb)(const char *),
                   FsearchDatabaseIndexStore **store_out) {
    g_return_val_if_fail(file_path, false);
    g_return_val_if_fail(store_out, false);

    g_autoptr(FILE) fp = file_open_locked(file_path, "rb");
    if (!fp) {
        return false;
    }

    DynamicArray *folders = NULL;
    DynamicArray *files = NULL;
    DynamicArray *sorted_folders[NUM_DATABASE_INDEX_PROPERTIES] = {NULL};
    DynamicArray *sorted_files[NUM_DATABASE_INDEX_PROPERTIES] = {NULL};
    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_include_manager_new();
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_exclude_manager_new();
    g_autoptr(GPtrArray) includes = fsearch_database_include_manager_get_includes(include_manager);
    g_autoptr(GPtrArray) indices = g_ptr_array_new_with_free_func((GDestroyNotify)fsearch_database_index_unref);
    g_autoptr(GHashTable) folder_index_arrays = g_hash_table_new_full(g_direct_hash,
                                                                      g_direct_equal,
                                                                      NULL,
                                                                      (GDestroyNotify)darray_unref);
    g_autoptr(GHashTable) file_index_arrays = g_hash_table_new_full(g_direct_hash,
                                                                    g_direct_equal,
                                                                    NULL,
                                                                    (GDestroyNotify)darray_unref);

    if (!database_file_load_header(fp)) {
        goto load_fail;
    }

    uint64_t index_flags = 0;
    if (!database_file_read_element(&index_flags, sizeof(index_flags), fp)) {
        g_debug("[db_load] failed to read index flags");
        goto load_fail;
    }

    uint32_t num_folders = 0;
    if (!database_file_read_element(&num_folders, sizeof(num_folders), fp)) {
        g_debug("[db_load] failed to read num_folders");
        goto load_fail;
    }

    uint32_t num_files = 0;
    if (!database_file_read_element(&num_files, sizeof(num_files), fp)) {
        g_debug("[db_load] failed to read num_files");
        goto load_fail;
    }
    g_debug("[db_load] load %d folders, %d files", num_folders, num_files);

    uint64_t folder_block_size = 0;
    if (!database_file_read_element(&folder_block_size, sizeof(folder_block_size), fp)) {
        g_debug("[db_load] failed to read folder block size");
        goto load_fail;
    }

    uint64_t file_block_size = 0;
    if (!database_file_read_element(&file_block_size, sizeof(file_block_size), fp)) {
        g_debug("[db_load] loading file block size: %" PRIu64, file_block_size);
        goto load_fail;
    }
    g_debug("[db_load] folder size: %" PRIu64 ", file size: %" PRIu64, folder_block_size, file_block_size);

    if (!database_file_load_includes(fp, include_manager)) {
        g_debug("[db_load] failed to load includes");
        goto load_fail;
    }
    if (!database_file_load_excludes(fp, exclude_manager)) {
        g_debug("[db_load] excludes not loaded");
        goto load_fail;
    }

    // pre-allocate the folders array, so we can later map parent indices to the corresponding pointers
    sorted_folders[DATABASE_INDEX_PROPERTY_NAME] = darray_new(num_folders);
    folders = sorted_folders[DATABASE_INDEX_PROPERTY_NAME];

    if (status_cb) {
        status_cb(_("Loading folders…"));
    }
    // load folders
    if (!database_file_load_folders(fp, index_flags, folders, num_folders, folder_block_size)) {
        g_debug("[db_load] failed to load folders");
        goto load_fail;
    }
    for (uint32_t i = 0; i < num_folders; i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders, i);
        const int32_t parent_idx = GPOINTER_TO_INT(db_entry_get_parent(folder));
        //g_print("loading: %d - %d\n", i, parent_idx);
        db_entry_set_parent_no_update(folder, parent_idx == -1 ? NULL : darray_get_item(folders, parent_idx));
    }

    if (status_cb) {
        status_cb(_("Loading files…"));
    }
    // load files
    sorted_files[DATABASE_INDEX_PROPERTY_NAME] = darray_new(num_files);
    files = sorted_files[DATABASE_INDEX_PROPERTY_NAME];
    if (!database_file_load_files(fp, index_flags, folders, files, num_files, file_block_size)) {
        g_debug("[db_load] failed to load files");
        goto load_fail;
    }

    if (!database_file_load_sorted_arrays(fp, sorted_folders, sorted_files)) {
        g_debug("[db_load] failed to load sorted arrays");
        goto load_fail;
    }

    DynamicArray *folders_sorted_by_path = sorted_folders[DATABASE_INDEX_PROPERTY_PATH];
    for (uint32_t i = 0; i < darray_get_num_items(folders_sorted_by_path); i++) {
        FsearchDatabaseEntry *folder = darray_get_item(folders_sorted_by_path, i);
        database_file_load_add_to_index_array(folder_index_arrays, folder);
    }
    DynamicArray *files_sorted_by_path = sorted_files[DATABASE_INDEX_PROPERTY_PATH];
    for (uint32_t i = 0; i < darray_get_num_items(files_sorted_by_path); i++) {
        FsearchDatabaseEntry *file = darray_get_item(files_sorted_by_path, i);
        database_file_load_add_to_index_array(file_index_arrays, file);
    }

    for (uint32_t i = 0; i < includes->len; i++) {
        FsearchDatabaseInclude *include = g_ptr_array_index(includes, i);
        const uint32_t id = fsearch_database_include_get_id(include);
        DynamicArray *folder_array_index = g_hash_table_lookup(folder_index_arrays, GINT_TO_POINTER(id));
        DynamicArray *file_array_index = g_hash_table_lookup(file_index_arrays, GINT_TO_POINTER(id));
        if (folder_array_index && file_array_index) {
            FsearchDatabaseIndex *index = fsearch_database_index_new_with_content(
                id,
                include,
                exclude_manager,
                folder_array_index,
                file_array_index,
                index_flags);
            g_ptr_array_add(indices, index);
        }
    }
    *store_out = fsearch_database_index_store_new_with_content(indices,
                                                               sorted_files,
                                                               sorted_folders,
                                                               include_manager,
                                                               exclude_manager,
                                                               index_flags,
                                                               index_event_cb,
                                                               self);

    g_clear_pointer(&fp, fclose);

    return true;

load_fail:
    g_debug("[db_load] load failed");

    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_PROPERTIES; i++) {
        g_clear_pointer(&sorted_folders[i], darray_unref);
        g_clear_pointer(&sorted_files[i], darray_unref);
    }
    return false;
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
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    return database_get_entry_info_non_blocking(self, work, info_out);
}

static void
database_sort(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);

    const uint32_t id = fsearch_database_work_get_view_id(work);
    const FsearchDatabaseIndexProperty sort_order = fsearch_database_work_sort_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_sort_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    signal_emit(self, SIGNAL_SORT_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    fsearch_database_index_store_sort_results(self->store, id, sort_order, sort_type, cancellable);

    signal_emit_sort_finished(self, id, fsearch_database_index_store_get_search_info(self->store, id));
}

static bool
database_search(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_val_if_fail(self, false);

    const uint32_t id = fsearch_database_work_get_view_id(work);

    g_autoptr(FsearchQuery) query = fsearch_database_work_search_get_query(work);
    FsearchDatabaseIndexProperty sort_order = fsearch_database_work_search_get_sort_order(work);
    const GtkSortType sort_type = fsearch_database_work_search_get_sort_type(work);
    g_autoptr(GCancellable) cancellable = fsearch_database_work_get_cancellable(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (!self->store) {
        return false;
    }

    signal_emit(self, SIGNAL_SEARCH_STARTED, GUINT_TO_POINTER(id), NULL, 1, NULL, NULL);

    const bool result = fsearch_database_index_store_search(self->store,
                                                            id,
                                                            query,
                                                            sort_order,
                                                            sort_type,
                                                            self->thread_pool,
                                                            cancellable);

    signal_emit_search_finished(self, id, fsearch_database_index_store_get_search_info(self->store, id));

    return result;
}

static void
update_search_views(FsearchDatabase *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->store);

    g_autoptr(GPtrArray) infos = fsearch_database_index_store_get_search_infos(self->store);
    g_return_if_fail(infos);

    for (uint32_t i = 0; i < infos->len; i++) {
        FsearchDatabaseSearchInfo *info = g_ptr_array_index(infos, i);
        signal_emit_selection_changed(self, info);
    }
}

static void
index_event_cb(FsearchDatabaseIndex *index, FsearchDatabaseIndexEvent *event, gpointer user_data) {
    FsearchDatabase *self = FSEARCH_DATABASE(user_data);

    switch (event->kind) {
    case FSEARCH_DATABASE_INDEX_EVENT_START_MODIFYING:
        database_lock(self);
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_END_MODIFYING:
        update_search_views(self);
        signal_emit_database_changed(self, database_get_info(self));
        database_unlock(self);
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_STARTED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCAN_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_STARTED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_MONITORING_FINISHED:
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_CREATED:
        fsearch_database_index_store_add_entries(self->store, event->entries.files, event->entries.folders);
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_ENTRY_DELETED:
        fsearch_database_index_store_remove_entries(self->store, event->entries.files, event->entries.folders);
        break;
    case FSEARCH_DATABASE_INDEX_EVENT_SCANNING:
        signal_emit(self, SIGNAL_DATABASE_PROGRESS, g_steal_pointer(&event->path), NULL, 1, (GDestroyNotify)free, NULL);
        break;
    case NUM_FSEARCH_DATABASE_INDEX_EVENTS:
        break;
    }
}

static void
database_modify_selection(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);
    const uint32_t view_id = fsearch_database_work_get_view_id(work);
    const FsearchSelectionType type = fsearch_database_work_modify_selection_get_type(work);
    const int32_t start_idx = fsearch_database_work_modify_selection_get_start_idx(work);
    const int32_t end_idx = fsearch_database_work_modify_selection_get_end_idx(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    fsearch_database_index_store_modify_selection(self->store, view_id, type, start_idx, end_idx);

    signal_emit_selection_changed(self, fsearch_database_index_store_get_search_info(self->store, view_id));
}

static void
database_save(FsearchDatabase *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    // g_autoptr(GFile) db_directory = g_file_get_parent(self->file);
    // g_autofree gchar *db_directory_path = g_file_get_path(db_directory);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);
    g_autofree char *file_path = g_file_get_path(self->file);
    database_file_save(self->store, file_path);
}

void
scan_thread_cb(gpointer data, gpointer user_data) {
    FsearchDatabaseIndexStore *store = data;
    FsearchDatabase *db = user_data;
    g_return_if_fail(store);
    g_return_if_fail(db);

    fsearch_database_index_store_start(store, NULL);
    g_autoptr(FsearchDatabaseWork) finished_work = fsearch_database_work_new_scan_finished(
        store,
        (void *(*)(void *))fsearch_database_index_store_ref,
        (GDestroyNotify)fsearch_database_index_store_unref);
    fsearch_database_queue_work(db, finished_work);
    g_clear_pointer(&store, fsearch_database_index_store_unref);
}

static void
database_scan_finished(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_work_scan_finished_get_index_store(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    self->store = g_steal_pointer(&store);

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

    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
        exclude_manager,
        flags,
        index_event_cb,
        db);
    g_return_if_fail(store);

    fsearch_database_index_store_start(store, NULL);

    g_clear_pointer(&db->store, fsearch_database_index_store_unref);
    db->store = fsearch_database_index_store_ref(store);
}

static void
database_rescan(FsearchDatabase *self) {
    g_return_if_fail(self);

    signal_emit0(self, SIGNAL_SCAN_STARTED);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager =
        fsearch_database_index_store_get_include_manager(self->store);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager =
        fsearch_database_index_store_get_exclude_manager(self->store);
    if (!include_manager) {
        include_manager = fsearch_database_include_manager_new_with_defaults();
    }
    if (!exclude_manager) {
        exclude_manager = fsearch_database_exclude_manager_new_with_defaults();
    }
    g_autoptr(FsearchDatabaseIndexStore) store = fsearch_database_index_store_new(include_manager,
        exclude_manager,
        database_get_flags(self),
        index_event_cb,
        self);
    g_return_if_fail(store);

    g_clear_pointer(&locker, g_mutex_locker_free);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&store), NULL);
}

static void
database_scan(FsearchDatabase *self, FsearchDatabaseWork *work) {
    g_return_if_fail(self);
    g_return_if_fail(work);

    g_autoptr(FsearchDatabaseIncludeManager) include_manager = fsearch_database_work_scan_get_include_manager(work);
    g_autoptr(FsearchDatabaseExcludeManager) exclude_manager = fsearch_database_work_scan_get_exclude_manager(work);
    const FsearchDatabaseIndexPropertyFlags flags = fsearch_database_work_scan_get_flags(work);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
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
        index_event_cb,
        self);
    g_return_if_fail(store);

    g_thread_pool_push(self->io_pool, g_steal_pointer(&store), NULL);

}

static void
database_load(FsearchDatabase *self) {
    g_return_if_fail(self);
    g_return_if_fail(self->file);

    signal_emit0(self, SIGNAL_LOAD_STARTED);

    g_autoptr(FsearchDatabaseIndexStore) store = NULL;
    const bool res = database_file_load(self, g_file_get_path(self->file), NULL, &store);

    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    g_assert_nonnull(locker);

    if (!res) {
        store = fsearch_database_index_store_new(fsearch_database_include_manager_new_with_defaults(),
                                                 fsearch_database_exclude_manager_new_with_defaults(),
                                                 database_get_flags(self),
                                                 index_event_cb,
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

        switch (fsearch_database_work_get_kind(work)) {
        case FSEARCH_DATABASE_WORK_QUIT:
            // Don't emit any signals here, as FsearchDatabase is about to be destroyed
            database_save(self);
            quit = true;
            break;
        case FSEARCH_DATABASE_WORK_LOAD_FROM_FILE:
            database_load(self);
            database_rescan(self);
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

    // Notify work queue thread to exit itself
    g_async_queue_push(self->work_queue, fsearch_database_work_new_quit());
    g_thread_join(self->work_queue_thread);

    G_OBJECT_CLASS(fsearch_database_parent_class)->dispose(object);
}

static void
fsearch_database_finalize(GObject *object) {
    FsearchDatabase *self = (FsearchDatabase *)object;

    database_lock(self);
    g_clear_pointer(&self->thread_pool, fsearch_thread_pool_free);
    g_thread_pool_free(g_steal_pointer(&self->io_pool), TRUE, TRUE);

    g_clear_pointer(&self->work_queue, g_async_queue_unref);

    g_clear_object(&self->file);

    g_clear_pointer(&self->store, fsearch_database_index_store_unref);
    database_unlock(self);

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
    self->thread_pool = fsearch_thread_pool_init();
    self->io_pool = g_thread_pool_new_full(scan_thread_cb,
                                           self,
                                           (GDestroyNotify)fsearch_database_index_store_unref,
                                           1,
                                           TRUE,
                                           NULL);
    self->work_queue = g_async_queue_new();
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

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    FsearchResult res = FSEARCH_RESULT_FAILED;
    g_autoptr(FsearchDatabaseSearchInfo) info = fsearch_database_index_store_get_search_info(self->store, view_id);
    if (!info) {
        res = FSEARCH_RESULT_DB_UNKOWN_SEARCH_VIEW;
    }
    else {
        *info_out = g_steal_pointer(&info);
        res = FSEARCH_RESULT_SUCCESS;
    }

    database_unlock(self);

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
    g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_get_item_info(view_id, idx, flags);
    FsearchResult res = database_get_entry_info_non_blocking(self, work, info_out);

    database_unlock(self);

    return res;
}

FsearchResult
fsearch_database_try_get_database_info(FsearchDatabase *self, FsearchDatabaseInfo **info_out) {
    g_return_val_if_fail(self, FSEARCH_RESULT_FAILED);
    g_return_val_if_fail(info_out, FSEARCH_RESULT_FAILED);

    if (!g_mutex_trylock(&self->mutex)) {
        return FSEARCH_RESULT_DB_BUSY;
    }

    *info_out = database_get_info(self);

    database_unlock(self);

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
    database_file_load_config(g_file_get_path(self->file), &include_manager, &exclude_manager, &flags);
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

    FsearchDatabaseSelectionForeachContext ctx = {.func = func, .user_data = user_data};

    fsearch_database_index_store_selection_foreach(self->store, view_id, selection_foreach_cb, &ctx);
}

// endregion