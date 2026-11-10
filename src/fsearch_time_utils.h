#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum FsearchDateFormat {
    FSEARCH_DATE_FORMAT_YYYY_MM_DD = 0,
    FSEARCH_DATE_FORMAT_MM_DD_YYYY,
    FSEARCH_DATE_FORMAT_DD_MM_YYYY_SLASH,
    FSEARCH_DATE_FORMAT_DD_MM_YYYY_DOT,
    N_DATE_FORMATS,
} FsearchDateFormat;

bool
fsearch_date_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out);

const char *
fsearch_time_utils_get_date_format_pattern(FsearchDateFormat format);

void
fsearch_time_utils_format_time(time_t mtime, FsearchDateFormat format, char *buf, size_t buf_len);
