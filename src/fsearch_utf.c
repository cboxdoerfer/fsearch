#include "fsearch_utf.h"

#include <glib.h>
#include <stdlib.h>

#include <unicode/ustring.h>

void
fsearch_utf_conversion_buffer_init(FsearchUtfConversionBuffer *buffer, int32_t string_capacity) {
    if (!buffer) {
        return;
    }
    buffer->init = true;
    buffer->capacity = string_capacity;
    buffer->string_utf8_folded = calloc(buffer->capacity, sizeof(char));
    buffer->string_utf8_folded_len = 0;
    buffer->string_folded = calloc(buffer->capacity, sizeof(UChar));
    buffer->string_folded_len = 0;
    buffer->string_normalized_folded = calloc(buffer->capacity, sizeof(UChar));
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
    buffer->string_utf8_folded_len =
        ucasemap_utf8FoldCase(case_map, buffer->string_utf8_folded, buffer->capacity, string, -1, &status);
    if (U_FAILURE(status)) {
        goto fail;
    }

    u_strFromUTF8(buffer->string_folded,
                  buffer->capacity,
                  &buffer->string_folded_len,
                  buffer->string_utf8_folded,
                  buffer->string_utf8_folded_len,
                  &status);
    if (U_FAILURE(status)) {
        goto fail;
    }

    buffer->string_normalized_folded_len = unorm2_normalize(normalizer,
                                                            buffer->string_folded,
                                                            buffer->string_folded_len,
                                                            buffer->string_normalized_folded,
                                                            buffer->capacity,
                                                            &status);
    if (U_FAILURE(status)) {
        goto fail;
    }

    return true;
fail:
    buffer->string_utf8_folded_len = 0;
    buffer->string_folded_len = 0;
    buffer->string_normalized_folded_len = 0;
    return false;
}
