#pragma once

#include "fsearch_array.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include.h"
#include "fsearch_database_index_event.h"
#include "fsearch_database_index_properties.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define FSEARCH_DATABASE_INDEX (fsearch_database_index_get_type())

typedef struct _FsearchDatabaseIndex FsearchDatabaseIndex;

typedef void
(*FsearchDatabaseIndexEventFunc)(FsearchDatabaseIndex *, FsearchDatabaseIndexEvent *event, gpointer);

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
                           GMainContext *monitor_ctx,
                           FsearchDatabaseIndexEventFunc event_func,
                           gpointer event_func_data);

FsearchDatabaseIndex *
fsearch_database_index_new_with_content(uint32_t id,
                                        FsearchDatabaseInclude *include,
                                        FsearchDatabaseExcludeManager *exclude_manager,
                                        DynamicArray *folders,
                                        DynamicArray *files,
                                        FsearchDatabaseIndexPropertyFlags flags);

void
fsearch_database_index_set_event_func(FsearchDatabaseIndex *self, FsearchDatabaseIndexEventFunc event_func, gpointer event_func_data);

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

bool
fsearch_database_index_wants_root_reappear_poll(FsearchDatabaseIndex *self);

void
fsearch_database_index_lock(FsearchDatabaseIndex *self);

void
fsearch_database_index_unlock(FsearchDatabaseIndex *self);

bool
fsearch_database_index_scan(FsearchDatabaseIndex *self, GCancellable *cancellable);

void
fsearch_database_index_start_monitoring(FsearchDatabaseIndex *self, bool start);

gboolean
fsearch_database_index_process_events(FsearchDatabaseIndex *self);

bool
fsearch_database_index_remove_path(FsearchDatabaseIndex *self, const char *path, bool *root_removed);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseIndex, fsearch_database_index_unref)