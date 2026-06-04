#pragma once

#include "fsearch_database_include_manager.h"

#include <glib.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct _FsearchDatabaseRescanManager FsearchDatabaseRescanManager;

typedef void (*FsearchDatabaseRescanIndexFunc)(uint32_t index_id, gpointer user_data);
typedef void (*FsearchDatabaseRescanFullFunc)(gpointer user_data);

FsearchDatabaseRescanManager *
fsearch_database_rescan_manager_new(FsearchDatabaseIncludeManager *include_manager,
                                    FsearchDatabaseRescanIndexFunc index_cb,
                                    FsearchDatabaseRescanFullFunc full_cb,
                                    gpointer user_data,
                                    GMainContext *context);

void
fsearch_database_rescan_manager_free(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_reschedule(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_request_index_scan(FsearchDatabaseRescanManager *self, uint32_t index_id);
void
fsearch_database_rescan_manager_request_full_scan(FsearchDatabaseRescanManager *self);
void
fsearch_database_rescan_manager_trigger_startup_scans(FsearchDatabaseRescanManager *self);

void
fsearch_database_rescan_manager_notify_index_finished(FsearchDatabaseRescanManager *self, uint32_t index_id);
void
fsearch_database_rescan_manager_notify_new_config(FsearchDatabaseRescanManager *self,
                                                  FsearchDatabaseIncludeManager *include_manager);

void
fsearch_database_rescan_manager_notify_index_offline(FsearchDatabaseRescanManager *self, uint32_t index_id);

G_END_DECLS