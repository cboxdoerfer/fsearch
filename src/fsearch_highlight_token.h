#pragma once

#include "fsearch_query_flags.h"

#include <glib.h>
#include <pango/pango.h>

PangoAttrList *
fsearch_highlight_tokens_match(GList *tokens, FsearchQueryFlags flags, const char *input);

GList *
fsearch_highlight_tokens_new(const char *text, FsearchQueryFlags flags);

void
fsearch_highlight_tokens_free(GList *tokens);
