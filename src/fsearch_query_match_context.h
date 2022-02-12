#pragma once

#include "fsearch_database_entry.h"
#include "fsearch_utf.h"

#include <stdbool.h>

typedef struct FsearchQueryMatchContext FsearchQueryMatchContext;

FsearchQueryMatchContext *
fsearch_query_match_context_new(void);

void
fsearch_query_match_context_free(FsearchQueryMatchContext *matcher);

void
fsearch_query_match_context_set_entry(FsearchQueryMatchContext *matcher, FsearchDatabaseEntry *entry);

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
