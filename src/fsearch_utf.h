/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

#pragma once

#include <stdbool.h>
#include <unicode/ucasemap.h>
#include <unicode/unorm2.h>
#include <unicode/utypes.h>

typedef struct FsearchUtfBuilder {
    UCaseMap *case_map;
    const UNormalizer2 *normalizer;

    char *string;
    char *string_utf8_folded;
    UChar *string_folded;
    UChar *string_normalized_folded;

    int32_t string_folded_len;
    int32_t string_normalized_folded_len;
    int32_t string_utf8_folded_len;

    uint32_t fold_options;

    int32_t num_characters;
    bool initialized;
    bool string_is_folded_and_normalized;
    bool string_utf8_is_folded;
} FsearchUtfBuilder;

void
fsearch_utf_builder_init(FsearchUtfBuilder *builder, int32_t str_len);

void
fsearch_utf_builder_clear(FsearchUtfBuilder *builder);

bool
fsearch_utf_fold_case_utf8(UCaseMap *case_map, FsearchUtfBuilder *builder, const char *string);

bool
fsearch_utf_builder_normalize_and_fold_case(FsearchUtfBuilder *builder,
                                            const char *string);