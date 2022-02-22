#pragma once

#include "fsearch_database_entry.h"
#include "fsearch_database_index.h"
#include "fsearch_utf.h"

#include <pango/pango-attributes.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct FsearchQueryMatchContext FsearchQueryMatchContext;

FsearchQueryMatchContext *
fsearch_query_match_context_new(void);

void
fsearch_query_match_context_free(FsearchQueryMatchContext *matcher);

void
fsearch_query_match_context_set_entry(FsearchQueryMatchContext *matcher, FsearchDatabaseEntry *entry);

void
fsearch_query_match_context_add_highlight(FsearchQueryMatchContext *matcher,
                                          PangoAttribute *attribute,
                                          FsearchDatabaseIndexType idx);

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchContext *matcher, FsearchDatabaseIndexType idx);

void
fsearch_query_match_context_set_thread_id(FsearchQueryMatchContext *matcher, int32_t thread_id);

int32_t
fsearch_query_match_context_get_thread_id(FsearchQueryMatchContext *matcher);

void
fsearch_query_match_context_set_result(FsearchQueryMatchContext *matcher, bool result);

bool
fsearch_query_match_context_get_result(FsearchQueryMatchContext *matcher);

const char *
fsearch_query_match_context_get_name_str(FsearchQueryMatchContext *matcher);

const char *
fsearch_query_match_context_get_path_str(FsearchQueryMatchContext *matcher);

FsearchUtfConversionBuffer *
fsearch_query_match_context_get_utf_path_buffer(FsearchQueryMatchContext *matcher);

FsearchUtfConversionBuffer *
fsearch_query_match_context_get_utf_name_buffer(FsearchQueryMatchContext *matcher);

FsearchDatabaseEntry *
fsearch_query_match_context_get_entry(FsearchQueryMatchContext *matcher);
