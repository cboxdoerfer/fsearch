#include "fsearch_utf.h"

#include <glib.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include <unicode/ustring.h>

void
fsearch_utf_builder_init(FsearchUtfBuilder *builder, int32_t num_characters) {
    if (!builder) {
        return;
    }
    builder->initialized = true;

    builder->fold_options = U_FOLD_CASE_DEFAULT;
    const char *current_locale = setlocale(LC_CTYPE, NULL);
    if (current_locale && (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2))) {
        // Use special case mapping for Turkic languages
        builder->fold_options = U_FOLD_CASE_EXCLUDE_SPECIAL_I;
    }

    UErrorCode status = U_ZERO_ERROR;
    builder->case_map = ucasemap_open(current_locale, builder->fold_options, &status);
    g_assert(U_SUCCESS(status));

    builder->normalizer = unorm2_getNFDInstance(&status);
    g_assert(U_SUCCESS(status));

    builder->string_utf8_is_folded = false;
    builder->string_is_folded_and_normalized = false;
    builder->num_characters = num_characters;
    builder->string_utf8_folded = calloc(builder->num_characters, sizeof(char));
    builder->string_utf8_folded_len = 0;
    builder->string_folded = calloc(builder->num_characters, sizeof(UChar));
    builder->string_folded_len = 0;
    builder->string_normalized_folded = calloc(builder->num_characters, sizeof(UChar));
    builder->string_normalized_folded_len = 0;
}

void
fsearch_utf_builder_clear(FsearchUtfBuilder *builder) {
    if (!builder) {
        return;
    }
    builder->initialized = false;
    g_clear_pointer(&builder->case_map, ucasemap_close);
    g_clear_pointer(&builder->string, free);
    g_clear_pointer(&builder->string_utf8_folded, free);
    g_clear_pointer(&builder->string_folded, free);
    g_clear_pointer(&builder->string_normalized_folded, free);
}

bool
fsearch_utf_fold_case_utf8(UCaseMap *case_map, FsearchUtfBuilder *builder, const char *string) {
    if (!builder || !builder->initialized) {
        return false;
    }

    UErrorCode status = U_ZERO_ERROR;

    // first perform case folding (this can be done while our string is still in UTF8 form)
    builder->string_utf8_folded_len =
        ucasemap_utf8FoldCase(case_map, builder->string_utf8_folded, builder->num_characters, string, -1, &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        return false;
    }
    builder->string_utf8_is_folded = true;
    builder->string_is_folded_and_normalized = false;

    return true;
}

bool
fsearch_utf_builder_normalize_and_fold_case(FsearchUtfBuilder *builder,
                                            const char *string) {
    g_assert(builder != NULL);
    if (!builder->initialized) {
        goto fail;
    }

    UErrorCode status = U_ZERO_ERROR;

    builder->string = g_strdup(string);
    // first perform case folding (this can be done while our string is still in UTF8 form)
    builder->string_utf8_folded_len =
        ucasemap_utf8FoldCase(builder->case_map, builder->string_utf8_folded, builder->num_characters, string, -1, &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }
    builder->string_utf8_is_folded = true;

    // then convert folded UTF8 string to UTF16 for normalizer
    u_strFromUTF8(builder->string_folded,
                  builder->num_characters,
                  &builder->string_folded_len,
                  builder->string_utf8_folded,
                  builder->string_utf8_folded_len,
                  &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }

    // check how much of the string needs to be normalized (if anything at all)
    const int32_t span_end =
        unorm2_spanQuickCheckYes(builder->normalizer, builder->string_folded, builder->string_folded_len, &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }

    if (G_LIKELY(span_end == builder->string_folded_len)) {
        // the string is already normalized
        // this should be the most common case and fortunately is the quickest (simple memcpy)
        memcpy(builder->string_normalized_folded, builder->string_folded, span_end * sizeof(UChar));
        builder->string_normalized_folded_len = builder->string_folded_len;
    }
    else if (span_end < builder->string_folded_len) {
        // the string isn't fully normalized
        // normalize everything after string_folded + span_end
        u_strncpy(builder->string_normalized_folded, builder->string_folded, span_end);
        builder->string_normalized_folded_len = unorm2_normalizeSecondAndAppend(builder->normalizer,
                                                                                builder->string_normalized_folded,
                                                                                span_end,
                                                                                builder->num_characters,
                                                                                builder->string_folded + span_end,
                                                                                builder->string_folded_len - span_end,
                                                                                &status);
        if (G_UNLIKELY(U_FAILURE(status))) {
            goto fail;
        }
    }
    else {
        // span_end is reported to be after string_folded_len, there's likely a bug in our code
        g_assert_not_reached();
    }

    builder->string_is_folded_and_normalized = true;
    return true;

fail:
    builder->string_utf8_folded_len = 0;
    builder->string_folded_len = 0;
    builder->string_normalized_folded_len = 0;
    builder->string_is_folded_and_normalized = false;
    builder->string_utf8_is_folded = false;
    return false;
}
