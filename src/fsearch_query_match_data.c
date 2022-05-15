#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include "fsearch_limits.h"
#include "fsearch_query_match_data.h"
#include "fsearch_utf.h"

struct FsearchQueryMatchData {
    FsearchDatabaseEntry *entry;

    FsearchUtfBuilder *utf_name_builder;
    FsearchUtfBuilder *utf_path_builder;
    FsearchUtfBuilder *utf_parent_path_builder;
    GString *path_buffer;
    GString *parent_path_buffer;
    GString *content_type_buffer;

    PangoAttrList **highlights;

    int32_t thread_id;

    bool utf_name_ready;
    bool utf_path_ready;
    bool utf_parent_path_ready;
    bool path_ready;
    bool parent_path_ready;
    bool content_type_ready;
    bool matches;
    bool has_highlights;
};

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_parent_path_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_parent_path_ready) {
        match_data->utf_parent_path_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_parent_path_builder,
                                                        fsearch_query_match_data_get_parent_path_str(match_data));
    }
    return match_data->utf_parent_path_builder;
}

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_name_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_name_ready) {
        match_data->utf_name_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_name_builder,
                                                        db_entry_get_name_raw_for_display(match_data->entry));
    }
    return match_data->utf_name_builder;
}

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_path_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_path_ready) {
        match_data->utf_path_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_path_builder,
                                                        fsearch_query_match_data_get_path_str(match_data));
    }
    return match_data->utf_path_builder;
}

const char *
fsearch_query_match_data_get_name_str(FsearchQueryMatchData *match_data) {
    return db_entry_get_name_raw_for_display(match_data->entry);
}

const char *
fsearch_query_match_data_get_parent_path_str(FsearchQueryMatchData *match_data) {
    if (!match_data->entry) {
        return NULL;
    }
    if (!match_data->parent_path_ready) {
        g_string_truncate(match_data->parent_path_buffer, 0);
        db_entry_append_path(match_data->entry, match_data->parent_path_buffer);

        match_data->parent_path_ready = true;
    }

    return match_data->parent_path_buffer->str;
}

const char *
fsearch_query_match_data_get_path_str(FsearchQueryMatchData *match_data) {
    if (!match_data->entry) {
        return NULL;
    }
    if (!match_data->path_ready) {
        g_string_truncate(match_data->path_buffer, 0);
        db_entry_append_full_path(match_data->entry, match_data->path_buffer);

        match_data->path_ready = true;
    }

    return match_data->path_buffer->str;
}

const char *
fsearch_query_match_data_get_content_type_str(FsearchQueryMatchData *match_data) {
    if (!match_data->entry) {
        return NULL;
    }
    if (!match_data->content_type_ready) {
        g_string_truncate(match_data->content_type_buffer, 0);
        db_entry_append_content_type(match_data->entry, match_data->content_type_buffer);

        match_data->content_type_ready = true;
    }

    return match_data->content_type_buffer->str;
}

FsearchDatabaseEntry *
fsearch_query_match_data_get_entry(FsearchQueryMatchData *match_data) {
    return match_data->entry;
}

FsearchQueryMatchData *
fsearch_query_match_data_new(void) {
    FsearchQueryMatchData *match_data = calloc(1, sizeof(FsearchQueryMatchData));
    g_assert(match_data);
    match_data->utf_name_builder = calloc(1, sizeof(FsearchUtfBuilder));
    match_data->utf_path_builder = calloc(1, sizeof(FsearchUtfBuilder));
    match_data->utf_parent_path_builder = calloc(1, sizeof(FsearchUtfBuilder));
    fsearch_utf_builder_init(match_data->utf_name_builder, 4 * PATH_MAX);
    fsearch_utf_builder_init(match_data->utf_path_builder, 4 * PATH_MAX);
    fsearch_utf_builder_init(match_data->utf_parent_path_builder, 4 * PATH_MAX);
    match_data->path_buffer = g_string_sized_new(PATH_MAX);
    match_data->parent_path_buffer = g_string_sized_new(PATH_MAX);
    match_data->content_type_buffer = g_string_sized_new(PATH_MAX);

    match_data->highlights = calloc(NUM_DATABASE_INDEX_TYPES, sizeof(PangoAttrList *));
    match_data->has_highlights = false;

    match_data->utf_name_ready = false;
    match_data->utf_path_ready = false;
    match_data->utf_parent_path_ready = false;
    match_data->path_ready = false;
    match_data->parent_path_ready = false;
    match_data->content_type_ready = false;

    return match_data;
}

static void
free_highlights(FsearchQueryMatchData *match_data) {
    if (!match_data->has_highlights) {
        return;
    }
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (match_data->highlights[i]) {
            g_clear_pointer(&match_data->highlights[i], pango_attr_list_unref);
        }
    }
    match_data->has_highlights = false;
}

void
fsearch_query_match_data_free(FsearchQueryMatchData *match_data) {
    if (!match_data) {
        return;
    }

    free_highlights(match_data);
    g_clear_pointer(&match_data->highlights, free);

    fsearch_utf_builder_clear(match_data->utf_name_builder);
    g_clear_pointer(&match_data->utf_name_builder, free);
    fsearch_utf_builder_clear(match_data->utf_path_builder);
    g_clear_pointer(&match_data->utf_path_builder, free);
    fsearch_utf_builder_clear(match_data->utf_parent_path_builder);
    g_clear_pointer(&match_data->utf_parent_path_builder, free);

    g_string_free(g_steal_pointer(&match_data->path_buffer), TRUE);
    g_string_free(g_steal_pointer(&match_data->parent_path_buffer), TRUE);
    g_string_free(g_steal_pointer(&match_data->content_type_buffer), TRUE);

    g_clear_pointer(&match_data, free);
}

void
fsearch_query_match_data_set_entry(FsearchQueryMatchData *match_data, FsearchDatabaseEntry *entry) {
    if (!match_data) {
        return;
    }

    // invalidate string buffers
    free_highlights(match_data);
    match_data->utf_name_ready = false;
    match_data->utf_path_ready = false;
    match_data->utf_parent_path_ready = false;
    match_data->path_ready = false;
    match_data->parent_path_ready = false;
    match_data->content_type_ready = false;

    match_data->entry = entry;
}

void
fsearch_query_match_data_set_result(FsearchQueryMatchData *match_data, bool result) {
    match_data->matches = result;
}

bool
fsearch_query_match_data_get_result(FsearchQueryMatchData *match_data) {
    return match_data->matches;
}

void
fsearch_query_match_data_set_thread_id(FsearchQueryMatchData *match_data, int32_t thread_id) {
    match_data->thread_id = thread_id;
}

int32_t
fsearch_query_match_data_get_thread_id(FsearchQueryMatchData *match_data) {
    return match_data->thread_id;
}

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchData *match_data, FsearchDatabaseIndexType idx) {
    g_assert(idx < NUM_DATABASE_INDEX_TYPES);
    return match_data->highlights[idx];
}

void
fsearch_query_match_data_add_highlight(FsearchQueryMatchData *match_data,
                                       PangoAttribute *attribute,
                                       FsearchDatabaseIndexType idx) {
    g_assert(idx < NUM_DATABASE_INDEX_TYPES);
    if (!match_data->highlights[idx]) {
        match_data->highlights[idx] = pango_attr_list_new();
    }
    pango_attr_list_change(match_data->highlights[idx], attribute);
    match_data->has_highlights = true;
}
