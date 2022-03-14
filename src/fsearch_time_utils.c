#define _XOPEN_SOURCE 700

#include "fsearch_time_utils.h"

#include <glib.h>
#include <stdint.h>

typedef enum FsearchTimeRangeType {
    FSEARCH_TIME_RANGE_DAY,
    FSEARCH_TIME_RANGE_MONTH,
    FSEARCH_TIME_RANGE_YEAR,
    NUM_FSEARCH_TIME_RANGES,
} FsearchTimeRangeType;

typedef struct FsearchTimeFormat {
    const char *format;
    FsearchTimeRangeType dtime;
} FsearchTimeFormat;

static bool
parse_time_constants(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr) {
    time_t t = time(NULL);
    struct tm time_start = *localtime(&t);
    struct tm time_end = time_start;
    size_t prefix_len = 0;
    const char *today = "today";
    const char *yesterday = "yesterday";
    if (g_str_has_prefix(str, today)) {
        time_start.tm_sec = time_start.tm_min = time_start.tm_hour = 0;
        time_end.tm_sec = 59;
        time_end.tm_min = 59;
        time_end.tm_hour = 23;
        prefix_len = strlen(today);
    }
    else if (g_str_has_prefix(str, yesterday)) {
        time_start.tm_mday--;
        time_start.tm_sec = time_start.tm_min = time_start.tm_hour = 0;
        time_end.tm_mday--;
        time_end.tm_sec = 59;
        time_end.tm_min = 59;
        time_end.tm_hour = 23;
        prefix_len = strlen(yesterday);
    }
    else {
        return false;
    }

    time_t time_start_res = mktime(&time_start);
    if (time_start_res < 0) {
        return false;
    }
    time_t time_end_res = mktime(&time_end);
    if (time_end_res < 0) {
        return false;
    }

    if (time_start_out) {
        *time_start_out = time_start_res;
    }
    if (time_end_out) {
        *time_end_out = time_end_res;
    }
    if (end_ptr) {
        *end_ptr = (char *)(str + prefix_len);
    }
    return true;
}

bool
fsearch_time_parse_range(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr) {
    if (parse_time_constants(str, time_start_out, time_end_out, end_ptr)) {
        return true;
    }

    FsearchTimeFormat formats[] = {
        {"%Y-%m-%d", FSEARCH_TIME_RANGE_DAY},
        {"%y-%m-%d", FSEARCH_TIME_RANGE_DAY},
        {"%Y-%m", FSEARCH_TIME_RANGE_MONTH},
        {"%y-%m", FSEARCH_TIME_RANGE_MONTH},
        {"%Y", FSEARCH_TIME_RANGE_YEAR},
        {"%y", FSEARCH_TIME_RANGE_YEAR},
    };

    for (uint32_t i = 0; i < G_N_ELEMENTS(formats); ++i) {
        struct tm tm_start = {0};
        char *date_suffix = strptime(str, formats[i].format, &tm_start);
        if (!date_suffix) {
            continue;
        }
        tm_start.tm_sec = tm_start.tm_min = tm_start.tm_hour = 0;
        tm_start.tm_isdst = -1;
        struct tm tm_end = tm_start;

        switch (formats[i].dtime) {
        case FSEARCH_TIME_RANGE_YEAR:
            // start from first day and month of the parsed year
            tm_start.tm_mday = 1;
            tm_start.tm_mon = 0;
            // end at the first day and month of the following year
            tm_end.tm_mday = 1;
            tm_end.tm_mon = 0;
            tm_end.tm_year++;
            break;
        case FSEARCH_TIME_RANGE_MONTH:
            // start at the first day of the parse month
            tm_start.tm_mday = 1;
            // end at the first day of the following month
            tm_end.tm_mday = 1;
            tm_end.tm_mon++;
            break;
        case FSEARCH_TIME_RANGE_DAY:
            // start at 0:00 of the parsed day
            // end at 0:00 of the following day
            tm_end.tm_mday++;
            break;
        default:
            continue;
        }
        time_t time_start = mktime(&tm_start);
        if (time_start < 0) {
            // invalid start time, try different format
            continue;
        }
        time_t time_end = mktime(&tm_end);
        if (time_end < 0) {
            // invalid end time, set it to a reasonably large value
            time_end = INT32_MAX;
        }
        if (time_start_out) {
            *time_start_out = time_start;
        }
        if (time_end_out) {
            *time_end_out = time_end;
        }
        if (end_ptr) {
            *end_ptr = date_suffix;
        }
        return true;
    }
    if (end_ptr) {
        *end_ptr = (char *)str;
    }
    return false;
}
