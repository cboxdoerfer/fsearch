#pragma once

#include <stdbool.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>
#include <unicode/utypes.h>

typedef struct FsearchUtfConversionBuffer {
    char *string_utf8_folded;
    UChar *string_folded;
    UChar *string_normalized_folded;

    int32_t string_folded_len;
    int32_t string_normalized_folded_len;
    int32_t string_utf8_folded_len;

    int32_t num_characters;
    bool initialized;
    bool string_is_folded_and_normalized;
    bool string_utf8_is_folded;
} FsearchUtfConversionBuffer;

void
fsearch_utf_conversion_buffer_init(FsearchUtfConversionBuffer *buffer, int32_t num_characters);

void
fsearch_utf_conversion_buffer_clear(FsearchUtfConversionBuffer *buffer);

bool
fsearch_utf_fold_case_utf8(UCaseMap *case_map, FsearchUtfConversionBuffer *buffer, const char *string);

bool
fsearch_utf_normalize_and_fold_case(const UNormalizer2 *normalizer,
                                    UCaseMap *case_map,
                                    FsearchUtfConversionBuffer *buffer,
                                    const char *string);
