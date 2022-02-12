#include <assert.h>
#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include "fsearch_query_match_context.h"
#include "fsearch_utf.h"

struct FsearchQueryMatchContext {
    FsearchDatabaseEntry *entry;

    FsearchUtfConversionBuffer *utf_name_buffer;
    FsearchUtfConversionBuffer *utf_path_buffer;
    GString *path_buffer;

    UCaseMap *case_map;
    const UNormalizer2 *normalizer;
    uint32_t fold_options;

    bool utf_name_ready;
    bool utf_path_ready;
    bool path_ready;
};

FsearchUtfConversionBuffer *
fsearch_query_match_context_get_utf_name_buffer(FsearchQueryMatchContext *matcher) {
    if (!matcher->utf_name_ready) {
        matcher->utf_name_ready =
            fsearch_utf_converion_buffer_normalize_and_fold_case(matcher->utf_name_buffer,
                                                                 matcher->case_map,
                                                                 matcher->normalizer,
                                                                 db_entry_get_name_raw_for_display(matcher->entry));
    }
    return matcher->utf_name_buffer;
}

FsearchUtfConversionBuffer *
fsearch_query_match_context_get_utf_path_buffer(FsearchQueryMatchContext *matcher) {
    if (!matcher->utf_path_ready) {
        matcher->utf_path_ready =
            fsearch_utf_converion_buffer_normalize_and_fold_case(matcher->utf_path_buffer,
                                                                 matcher->case_map,
                                                                 matcher->normalizer,
                                                                 fsearch_query_match_context_get_path_str(matcher));
    }
    return matcher->utf_path_buffer;
}

const char *
fsearch_query_match_context_get_name_str(FsearchQueryMatchContext *matcher) {
    return db_entry_get_name_raw_for_display(matcher->entry);
}

const char *
fsearch_query_match_context_get_path_str(FsearchQueryMatchContext *matcher) {
    if (!matcher->entry) {
        return NULL;
    }
    if (!matcher->path_ready) {
        g_string_truncate(matcher->path_buffer, 0);
        db_entry_append_path(matcher->entry, matcher->path_buffer);
        g_string_append_c(matcher->path_buffer, G_DIR_SEPARATOR);
        g_string_append(matcher->path_buffer, db_entry_get_name_raw(matcher->entry));

        matcher->path_ready = true;
    }

    return matcher->path_buffer->str;
}

FsearchDatabaseEntry *
fsearch_query_match_context_get_entry(FsearchQueryMatchContext *matcher) {
    return matcher->entry;
}

FsearchQueryMatchContext *
fsearch_query_match_context_new(void) {
    FsearchQueryMatchContext *matcher = calloc(1, sizeof(FsearchQueryMatchContext));
    assert(matcher != NULL);
    matcher->utf_name_buffer = calloc(1, sizeof(FsearchUtfConversionBuffer));
    matcher->utf_path_buffer = calloc(1, sizeof(FsearchUtfConversionBuffer));
    fsearch_utf_conversion_buffer_init(matcher->utf_name_buffer, 4 * PATH_MAX);
    fsearch_utf_conversion_buffer_init(matcher->utf_path_buffer, 4 * PATH_MAX);
    matcher->path_buffer = g_string_sized_new(PATH_MAX);

    matcher->utf_name_ready = false;
    matcher->utf_path_ready = false;
    matcher->path_ready = false;

    matcher->fold_options = U_FOLD_CASE_DEFAULT;
    const char *current_locale = setlocale(LC_CTYPE, NULL);
    if (current_locale && (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2))) {
        // Use special case mapping for Turkic languages
        matcher->fold_options = U_FOLD_CASE_EXCLUDE_SPECIAL_I;
    }

    UErrorCode status = U_ZERO_ERROR;
    matcher->case_map = ucasemap_open(current_locale, matcher->fold_options, &status);
    assert(U_SUCCESS(status));

    matcher->normalizer = unorm2_getNFDInstance(&status);
    assert(U_SUCCESS(status));

    return matcher;
}

void
fsearch_query_match_context_free(FsearchQueryMatchContext *matcher) {
    if (!matcher) {
        return;
    }

    fsearch_utf_conversion_buffer_clear(matcher->utf_name_buffer);
    g_clear_pointer(&matcher->utf_name_buffer, free);
    fsearch_utf_conversion_buffer_clear(matcher->utf_path_buffer);
    g_clear_pointer(&matcher->utf_path_buffer, free);

    g_clear_pointer(&matcher->case_map, ucasemap_close);

    g_string_free(g_steal_pointer(&matcher->path_buffer), TRUE);

    g_clear_pointer(&matcher, free);
}

void
fsearch_query_match_context_set_entry(FsearchQueryMatchContext *matcher, FsearchDatabaseEntry *entry) {
    if (!matcher) {
        return;
    }

    // invalidate string buffers
    matcher->utf_name_ready = false;
    matcher->utf_path_ready = false;
    matcher->path_ready = false;

    matcher->entry = entry;
}
