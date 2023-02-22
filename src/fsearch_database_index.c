#include "fsearch_database_index.h"
#include "fsearch_database_entry.h"
#include "fsearch_memory_pool.h"

#include <glib.h>

#define NUM_DB_ENTRIES_FOR_POOL_BLOCK 10000

struct _FsearchDatabaseIndex {
    FsearchDatabaseInclude *include;
    FsearchDatabaseExcludeManager *exclude_manager;
    FsearchMemoryPool *file_pool;
    FsearchMemoryPool *folder_pool;
    DynamicArray *files;
    DynamicArray *folders;

    FsearchDatabaseIndexPropertyFlags flags;

    FsearchDatabaseIndexEventFunc event_func;
    gpointer event_user_data;

    uint32_t id;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseIndex, fsearch_database_index, fsearch_database_index_ref, fsearch_database_index_unref)

static void
index_free(FsearchDatabaseIndex *index) {
    g_return_if_fail(index);

    g_clear_pointer(&index->include, fsearch_database_include_unref);
    g_clear_object(&index->exclude_manager);

    g_clear_pointer(&index->files, darray_unref);
    g_clear_pointer(&index->folders, darray_unref);

    g_clear_pointer(&index->file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&index->folder_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&index, free);
}

FsearchDatabaseIndex *
fsearch_database_index_new(uint32_t id,
                           FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer user_data) {
    FsearchDatabaseIndex *self = g_slice_new0(FsearchDatabaseIndex);
    g_assert(self);

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->files = darray_new(1024);
    self->folders = darray_new(1024);

    self->event_func = event_func;
    self->event_user_data = user_data;

    self->file_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                              db_entry_get_sizeof_file_entry(),
                                              (GDestroyNotify)db_entry_destroy);
    self->folder_pool = fsearch_memory_pool_new(NUM_DB_ENTRIES_FOR_POOL_BLOCK,
                                                db_entry_get_sizeof_folder_entry(),
                                                (GDestroyNotify)db_entry_destroy);

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(uint32_t id,
                                        FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        FsearchMemoryPool *file_pool,
                                        FsearchMemoryPool *folder_pool,
                                        DynamicArray *files,
                                        DynamicArray *folders,
                                        FsearchDatabaseIndexPropertyFlags flags,
                                        FsearchDatabaseIndexEventFunc event_func,
                                        gpointer user_data) {
    FsearchDatabaseIndex *self = g_slice_new0(FsearchDatabaseIndex);
    g_assert(self);

    self->id = id;
    self->include = fsearch_database_include_ref(include);
    self->exclude_manager = g_object_ref(exclude_manager);
    self->flags = flags;

    self->files = darray_ref(files);
    self->folders = darray_ref(folders);

    self->file_pool = file_pool;
    self->folder_pool = folder_pool;

    self->event_func = event_func;
    self->event_user_data = user_data;

    self->ref_count = 1;

    return self;
}

FsearchDatabaseIndex *
fsearch_database_index_ref(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_index_unref(FsearchDatabaseIndex *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self, index_free);
    }
}

FsearchDatabaseInclude *
fsearch_database_index_get_include(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_include_ref(self->include);
}

FsearchDatabaseExcludeManager *
fsearch_database_index_get_exclude_manager(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return g_object_ref(self->exclude_manager);
}

DynamicArray *
fsearch_database_index_get_files(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return darray_ref(self->files);
}

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self) {
    g_return_val_if_fail(self, NULL);
    return darray_ref(self->folders);
}

uint32_t
fsearch_database_index_get_id(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->id;
}

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_get_flags(FsearchDatabaseIndex *self) {
    g_assert(self);
    return self->flags;
}

FsearchDatabaseEntry *
fsearch_database_index_add_file(FsearchDatabaseIndex *self,
                                const char *name,
                                off_t size,
                                time_t mtime,
                                FsearchDatabaseEntryFolder *parent) {
    g_return_val_if_fail(self, NULL);

    FsearchDatabaseEntry *file_entry = fsearch_memory_pool_malloc(self->file_pool);
    db_entry_set_name(file_entry, name);
    db_entry_set_size(file_entry, size);
    db_entry_set_mtime(file_entry, mtime);
    db_entry_set_type(file_entry, DATABASE_ENTRY_TYPE_FILE);
    db_entry_set_parent(file_entry, parent);
    db_entry_update_parent_size(file_entry);

    darray_add_item(self->files, file_entry);

    return file_entry;
}

FsearchDatabaseEntryFolder *
fsearch_database_index_add_folder(FsearchDatabaseIndex *self,
                                  const char *name,
                                  time_t mtime,
                                  FsearchDatabaseEntryFolder *parent) {
    g_return_val_if_fail(self, NULL);

    FsearchDatabaseEntry *entry = fsearch_memory_pool_malloc(self->folder_pool);
    db_entry_set_name(entry, name);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
    db_entry_set_mtime(entry, mtime);
    db_entry_set_parent(entry, parent);

    darray_add_item(self->folders, entry);

    return (FsearchDatabaseEntryFolder *)entry;
}
