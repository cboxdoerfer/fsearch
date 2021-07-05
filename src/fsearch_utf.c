#include "fsearch_utf.h"

#include <glib.h>
#include <stdlib.h>

#include <unicode/ustring.h>

void
fsearch_utf_conversion_buffer_init(FsearchUtfConversionBuffer *buffer, int32_t num_characters) {
    if (!buffer) {
        return;
    }
    buffer->init = true;
    buffer->ready = false;
    buffer->num_characters = num_characters;
    buffer->string_utf8_folded = calloc(buffer->num_characters, sizeof(char));
    buffer->string_utf8_folded_len = 0;
    buffer->string_folded = calloc(buffer->num_characters, sizeof(UChar));
    buffer->string_folded_len = 0;
    buffer->string_normalized_folded = calloc(buffer->num_characters, sizeof(UChar));
    buffer->string_normalized_folded_len = 0;
}

void
fsearch_utf_conversion_buffer_clear(FsearchUtfConversionBuffer *buffer) {
    if (!buffer) {
        return;
    }
    buffer->init = false;
    g_clear_pointer(&buffer->string_utf8_folded, free);
    g_clear_pointer(&buffer->string_folded, free);
    g_clear_pointer(&buffer->string_normalized_folded, free);
}

bool
fsearch_utf_normalize_and_fold_case(const UNormalizer2 *normalizer,
                                    UCaseMap *case_map,
                                    FsearchUtfConversionBuffer *buffer,
                                    const char *string) {
    if (!buffer || !buffer->init) {
        goto fail;
    }

    UErrorCode status = U_ZERO_ERROR;

    // first perform case folding (this can be done while our string is still in UTF8 form)
    buffer->string_utf8_folded_len =
        ucasemap_utf8FoldCase(case_map, buffer->string_utf8_folded, buffer->num_characters, string, -1, &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }

    // then convert folded UTF8 string to UTF16 for normalizer
    u_strFromUTF8(buffer->string_folded,
                  buffer->num_characters,
                  &buffer->string_folded_len,
                  buffer->string_utf8_folded,
                  buffer->string_utf8_folded_len,
                  &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }

    // check how much of the string needs to be normalized (if anything at all)
    const int32_t span_end =
        unorm2_spanQuickCheckYes(normalizer, buffer->string_folded, buffer->string_folded_len, &status);
    if (G_UNLIKELY(U_FAILURE(status))) {
        goto fail;
    }

    if (G_LIKELY(span_end == buffer->string_folded_len)) {
        // the string is already normalized
        // this should be the most common case and fortunately is the quickest (simple memcpy)
        memcpy(buffer->string_normalized_folded, buffer->string_folded, span_end * sizeof(UChar));
        buffer->string_normalized_folded_len = buffer->string_folded_len;
        buffer->ready = true;
        return true;
    }
    else if (span_end < buffer->string_folded_len) {
        // the string isn't fully normalized
        // normalize everything after string_folded + span_end
        u_strncpy(buffer->string_normalized_folded, buffer->string_folded, span_end);
        buffer->string_normalized_folded_len = unorm2_normalizeSecondAndAppend(normalizer,
                                                                               buffer->string_normalized_folded,
                                                                               span_end,
                                                                               buffer->num_characters,
                                                                               buffer->string_folded + span_end,
                                                                               buffer->string_folded_len - span_end,
                                                                               &status);
        if (G_UNLIKELY(U_FAILURE(status))) {
            goto fail;
        }
        buffer->ready = true;
        return true;
    }
    else {
        // span_end is reported to be after string_folded_len, there's likely a bug in our code
        g_assert_not_reached();
    }
fail:
    buffer->string_utf8_folded_len = 0;
    buffer->string_folded_len = 0;
    buffer->string_normalized_folded_len = 0;
    buffer->ready = false;
    return false;
}
