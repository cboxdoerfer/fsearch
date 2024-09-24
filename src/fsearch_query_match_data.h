#pragma once

#include "fsearch_database_entry.h"
#include "fsearch_database_index.h"
#include "fsearch_utf.h"

#include <pango/pango-attributes.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FsearchQueryMatchData FsearchQueryMatchData;

FsearchQueryMatchData *
fsearch_query_match_data_new(size_t *file_attr_offsets, size_t *folder_attr_offsets);

void
fsearch_query_match_data_free(FsearchQueryMatchData *match_data);

void
fsearch_query_match_data_set_entry(FsearchQueryMatchData *match_data, FsearchDatabaseEntryBase *entry);

void
fsearch_query_match_data_add_highlight(FsearchQueryMatchData *match_data,
                                       PangoAttribute *attribute,
                                       FsearchDatabaseIndexProperty idx);

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchData *match_data, FsearchDatabaseIndexProperty idx);

GHashTable *
fsearch_query_match_data_get_highlights(FsearchQueryMatchData *match_data);

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
fsearch_query_match_data_get_parent_path_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_path_str(FsearchQueryMatchData *match_data);

const char *
fsearch_query_match_data_get_content_type_str(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_parent_path_builder(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_path_builder(FsearchQueryMatchData *match_data);

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_name_builder(FsearchQueryMatchData *match_data);

FsearchDatabaseEntryBase *
fsearch_query_match_data_get_entry(FsearchQueryMatchData *match_data);
