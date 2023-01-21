#pragma once

#include <gio/gio.h>

#include "fsearch_database_entry_info.h"
#include "fsearch_database_exclude_manager.h"
#include "fsearch_database_include_manager.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_work.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE2 fsearch_database2_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabase2, fsearch_database2, FSEARCH, DATABASE2, GObject)

void
fsearch_database2_queue_work(FsearchDatabase2 *self, FsearchDatabaseWork *work);

void
fsearch_database2_process_work_now(FsearchDatabase2 *self);

bool
fsearch_database2_try_get_search_info(FsearchDatabase2 *self, uint32_t view_id, FsearchDatabaseSearchInfo **info_out);

bool
fsearch_database2_try_get_item_info(FsearchDatabase2 *self,
                                    uint32_t view_id,
                                    uint32_t idx,
                                    FsearchDatabaseEntryInfoFlags flags,
                                    FsearchDatabaseEntryInfo **info_out);

FsearchDatabase2 *
fsearch_database2_new(GFile *file);

G_END_DECLS
