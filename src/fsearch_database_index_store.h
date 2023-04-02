#pragma once

#include <gio/gio.h>

#include "fsearch_database_entries_container.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_index.h"

G_BEGIN_DECLS

#define FSEARCH_DATABASE_INDEX_STORE (fsearch_database_index_store_get_type())

typedef struct _FsearchDatabaseIndexStore FsearchDatabaseIndexStore;

GType
fsearch_database_index_store_get_type(void);

FsearchDatabaseIndexStore *
fsearch_database_index_store_new(FsearchDatabaseIncludeManager *include_manager,
                                 FsearchDatabaseExcludeManager *exclude_manager,
                                 FsearchDatabaseIndexPropertyFlags flags,
                                 FsearchDatabaseIndexEventFunc event_func,
                                 gpointer event_func_data);

FsearchDatabaseIndexStore *
fsearch_database_index_store_ref(FsearchDatabaseIndexStore *self);

void
fsearch_database_index_store_unref(FsearchDatabaseIndexStore *self);

bool
fsearch_database_index_store_has_container(FsearchDatabaseIndexStore *self, FsearchDatabaseEntriesContainer *container);

FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_files(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order);

FsearchDatabaseEntriesContainer *
fsearch_database_index_store_get_folders(FsearchDatabaseIndexStore *self, FsearchDatabaseIndexProperty sort_order);

uint32_t
fsearch_database_index_store_get_num_fast_sort_indices(FsearchDatabaseIndexStore *self);

uint32_t
fsearch_database_index_store_get_num_files(FsearchDatabaseIndexStore *self);

uint32_t
fsearch_database_index_store_get_num_folders(FsearchDatabaseIndexStore *self);

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_store_get_flags(FsearchDatabaseIndexStore *self);

void
fsearch_database_index_store_start(FsearchDatabaseIndexStore *self, GCancellable *cancellable);

void
fsearch_database_index_store_start_monitoring(FsearchDatabaseIndexStore *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndexStore, fsearch_database_index_store_unref)

G_END_DECLS
