#pragma once

#include "fsearch_database_entry.h"
#include "fsearch_database_index.h"
#include "fsearch_utf.h"

#include <pango/pango-attributes.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchQueryMatchData FsearchQueryMatchData;

FsearchQueryMatchData *
fsearch_query_match_data_new(void);

void
fsearch_query_match_data_free(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_entry(FsearchQueryMatchData *match_data, FsearchDatabaseEntry *entry);

void
fsearch_query_match_data_add_highlight(FsearchQueryMatchData *match_data,
                                       PangoAttribute *attribute,
                                       FsearchDatabaseIndexType idx);

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchData *match_data, FsearchDatabaseIndexType idx);

void
fsearch_query_match_data_set_thread_id(FsearchQueryMatchData *match_data, int32_t thread_id);

int32_t
fsearch_query_match_data_get_thread_id(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_result(FsearchQueryMatchData *match_data, bool result);

bool
fsearch_query_match_data_get_result(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_name_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_path_str(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_path_builder(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_name_builder(FsearchQueryMatchData *match_data);

FsearchDatabaseEntry *
fsearch_query_match_data_get_entry(FsearchQueryMatchData *match_data);
