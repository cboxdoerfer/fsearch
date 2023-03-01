#pragma once

#include "fsearch_array.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"
#include "fsearch_memory_pool.h"

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_INDEX (fsearch_database_index_get_type())

typedef struct _FsearchDatabaseIndex FsearchDatabaseIndex;

typedef void (*FsearchDatabaseIndexEventFunc)(FsearchDatabaseIndex *,
                                              FsearchDatabaseIndexEventKind kind,
                                              FsearchDatabaseEntry *parent,
                                              GString *path,
                                              int32_t watch_descriptor,
                                              gpointer user_data);

GType
fsearch_database_index_get_type(void);

FsearchDatabaseIndex *
fsearch_database_index_ref(FsearchDatabaseIndex *self);

void
fsearch_database_index_unref(FsearchDatabaseIndex *self);

FsearchDatabaseIndex *
fsearch_database_index_new(uint32_t id,
                           FsearchDatabaseInclude *include,
                           FsearchDatabaseExcludeManager *exclude_manager,
                           FsearchDatabaseIndexPropertyFlags flags,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer user_data);

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
                                        gpointer user_data);

FsearchDatabaseInclude *
fsearch_database_index_get_include(FsearchDatabaseIndex *self);

FsearchDatabaseExcludeManager *
fsearch_database_index_get_exclude_manager(FsearchDatabaseIndex *self);

DynamicArray *
fsearch_database_index_get_files(FsearchDatabaseIndex *self);

DynamicArray *
fsearch_database_index_get_folders(FsearchDatabaseIndex *self);

uint32_t
fsearch_database_index_get_id(FsearchDatabaseIndex *self);

FsearchDatabaseIndexPropertyFlags
fsearch_database_index_get_flags(FsearchDatabaseIndex *self);

FsearchDatabaseEntry *
fsearch_database_index_add_file(FsearchDatabaseIndex *self,
                                const char *name,
                                off_t size,
                                time_t mtime,
                                FsearchDatabaseEntryFolder *parent);

FsearchDatabaseEntryFolder *
fsearch_database_index_add_folder(FsearchDatabaseIndex *self,
                                  const char *name,
                                  const char *path,
                                  time_t mtime,
                                  FsearchDatabaseEntryFolder *parent);
void
fsearch_database_index_free_entry(FsearchDatabaseIndex *self, FsearchDatabaseEntry *entry);

void
fsearch_database_index_lock(FsearchDatabaseIndex *self);

void
fsearch_database_index_unlock(FsearchDatabaseIndex *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndex, fsearch_database_index_unref)
