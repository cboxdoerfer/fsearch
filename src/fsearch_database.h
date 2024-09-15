#pragma once

#include <gio/gio.h>

#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_info.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_work.h"
#include "fsearch_result.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE fsearch_database_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabase, fsearch_database, FSEARCH, DATABASE, GObject)

typedef void (*FsearchDatabaseForeachFunc)(FsearchDatabaseEntryBase *entry, gpointer user_data);

void
fsearch_database_queue_work(FsearchDatabase *self, FsearchDatabaseWork *work);

FsearchResult
fsearch_database_try_get_search_info(FsearchDatabase *self, uint32_t view_id, FsearchDatabaseSearchInfo **info_out);

FsearchResult
fsearch_database_try_get_database_info(FsearchDatabase *self, FsearchDatabaseInfo **info_out);

void
fsearch_database_selection_foreach(FsearchDatabase *self,
                                   uint32_t view_id,
                                   FsearchDatabaseForeachFunc func,
                                   gpointer user_data);

FsearchResult
fsearch_database_try_get_item_info(FsearchDatabase *self,
                                   uint32_t view_id,
                                   uint32_t idx,
                                   FsearchDatabaseEntryInfoFlags flags,
                                   FsearchDatabaseEntryInfo **info_out);

FsearchDatabase *
fsearch_database_new(GFile *file);

G_END_DECLS
