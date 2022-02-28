#pragma once

#include <stdbool.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>
#include <unicode/utypes.h>

typedef struct FsearchUtfBuilder {
    char *string;
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
} FsearchUtfBuilder;

void
fsearch_utf_builder_init(FsearchUtfBuilder *builder, int32_t num_characters);

void
fsearch_utf_builder_clear(FsearchUtfBuilder *builder);

bool
fsearch_utf_fold_case_utf8(UCaseMap *case_map, FsearchUtfBuilder *builder, const char *string);

bool
fsearch_utf_builder_normalize_and_fold_case(FsearchUtfBuilder *builder,
                                            UCaseMap *case_map,
                                            const UNormalizer2 *normalizer,
                                            const char *string);
