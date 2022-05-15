#define _GNU_SOURCE

#include "fsearch_time_utils.h"

#include <glib.h>
#include <stdint.h>
#include <time.h>

typedef enum FsearchTimeIntervalType {
    FSEARCH_TIME_INTERVAL_DAY,
    FSEARCH_TIME_INTERVAL_MONTH,
    FSEARCH_TIME_INTERVAL_YEAR,
    NUM_FSEARCH_TIME_INTERVALS,
} FsearchTimeIntervalType;

typedef struct FsearchTimeFormat {
    const char *format;
    FsearchTimeIntervalType dtime;
} FsearchTimeFormat;

typedef struct FsearchTimeConstant {
    const char *format;
    int32_t val;
} FsearchTimeConstant;

FsearchTimeConstant relative_day_constants[] = {
    {"today", 0},
    {"yesterday", 1},
};

FsearchTimeConstant weekday_constants[] = {
    {"monday", 1},
    {"mon", 1},
    {"tuesday", 2},
    {"tue", 2},
    {"wednesday", 3},
    {"wed", 3},
    {"thursday", 4},
    {"thu", 4},
    {"friday", 5},
    {"fri", 5},
    {"saturday", 6},
    {"sat", 6},
    {"sunday", 7},
    {"sun", 7},
};

FsearchTimeConstant month_constants[] = {
    {"january", 1}, {"jan", 1},       {"february", 2}, {"feb", 2},       {"march", 3}, {"mar", 3},
    {"april", 4},   {"apr", 4},       {"may", 5},      {"june", 6},      {"jun", 6},   {"july", 7},
    {"jul", 7},     {"august", 8},    {"aug", 8},      {"september", 9}, {"sep", 9},   {"october", 10},
    {"oct", 10},    {"november", 11}, {"nov", 11},     {"december", 12}, {"dec", 12},
};

static time_t
get_unix_time_for_timezone(struct tm *tm) {
    time_t res = timegm(tm);
    if (res < 0) {
        // tm refers to a point in time before 1970-01-01 00:00 UTC
        // that's not a valid time
        return -1;
    }
    // adjust for the timezone, but don't go below zero
    return MAX(0, res + timezone);
}

static int32_t
get_weekday_from_gdate(GDate *date) {
    g_assert(date);
    switch (g_date_get_weekday(date)) {
    case G_DATE_BAD_WEEKDAY:
        return 0;
    case G_DATE_MONDAY:
        return 1;
    case G_DATE_TUESDAY:
        return 2;
    case G_DATE_WEDNESDAY:
        return 3;
    case G_DATE_THURSDAY:
        return 4;
    case G_DATE_FRIDAY:
        return 5;
    case G_DATE_SATURDAY:
        return 6;
    case G_DATE_SUNDAY:
        return 7;
    }
    return 0;
}

static bool
parse_relative_day_constants(const char *str, struct tm *start, struct tm *end, char **end_ptr) {
    for (uint32_t i = 0; i < G_N_ELEMENTS(relative_day_constants); ++i) {
        if (g_str_has_prefix(str, relative_day_constants[i].format)) {
            GDate *date = g_date_new();
            g_date_set_time_t(date, time(NULL));
            g_date_subtract_days(date, relative_day_constants[i].val);
            g_date_to_struct_tm(date, start);
            g_date_add_days(date, 1);
            g_date_to_struct_tm(date, end);
            g_clear_pointer(&date, g_date_free);
            *end_ptr = (char *)(str + strlen(relative_day_constants[i].format));
            return true;
        }
    }
    return false;
}

static bool
parse_weekday_constants(const char *str, struct tm *start, struct tm *end, char **end_ptr) {
    for (uint32_t i = 0; i < G_N_ELEMENTS(weekday_constants); ++i) {
        if (g_str_has_prefix(str, weekday_constants[i].format)) {
            GDate *date = g_date_new();
            g_date_set_time_t(date, time(NULL));
            // The amount of days which have passed since the requested weekday
            int32_t days_diff = get_weekday_from_gdate(date) - weekday_constants[i].val;
            if (days_diff < 0) {
                days_diff += 7;
            }
            g_date_subtract_days(date, days_diff);
            g_date_to_struct_tm(date, start);
            g_date_add_days(date, 1);
            g_date_to_struct_tm(date, end);
            g_clear_pointer(&date, g_date_free);
            *end_ptr = (char *)(str + strlen(weekday_constants[i].format));
            return true;
        }
    }
    return false;
}

static bool
parse_month_constants(const char *str, struct tm *start, struct tm *end, char **end_ptr) {
    for (uint32_t i = 0; i < G_N_ELEMENTS(month_constants); ++i) {
        if (g_str_has_prefix(str, month_constants[i].format)) {
            GDate *date = g_date_new();
            g_date_set_time_t(date, time(NULL));
            g_date_subtract_days(date, date->day - 1);
            // The amount of months which have passed since the requested month
            int32_t months_diff = date->month - month_constants[i].val;
            if (months_diff < 0) {
                months_diff += 12;
            }
            g_date_subtract_months(date, months_diff);
            g_date_to_struct_tm(date, start);
            g_date_add_months(date, 1);
            g_date_to_struct_tm(date, end);
            g_clear_pointer(&date, g_date_free);
            *end_ptr = (char *)(str + strlen(month_constants[i].format));
            return true;
        }
    }
    return false;
}

static bool
parse_time_constants(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr) {
    const time_t t = time(NULL);
    struct tm tm_start = *localtime(&t);
    struct tm tm_end = tm_start;

    time_t time_start = 0;
    time_t time_end = 0;

    if (parse_relative_day_constants(str, &tm_start, &tm_end, end_ptr)) {
        goto found_constant;
    }
    else if (parse_weekday_constants(str, &tm_start, &tm_end, end_ptr)) {
        goto found_constant;
    }
    else if (parse_month_constants(str, &tm_start, &tm_end, end_ptr)) {
        goto found_constant;
    }
    else {
        return false;
    }

found_constant:
    time_start = get_unix_time_for_timezone(&tm_start);
    if (time_start < 0) {
        return false;
    }

    time_end = get_unix_time_for_timezone(&tm_end);
    if (time_end < 0) {
        return false;
    }

    if (time_start_out) {
        *time_start_out = time_start;
    }
    if (time_end_out) {
        *time_end_out = time_end;
    }
    return true;
}

bool
fsearch_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr) {
    if (parse_time_constants(str, time_start_out, time_end_out, end_ptr)) {
        return true;
    }

    FsearchTimeFormat formats[] = {
        {"%Y-%m-%d", FSEARCH_TIME_INTERVAL_DAY},
        {"%y-%m-%d", FSEARCH_TIME_INTERVAL_DAY},
        {"%Y-%m", FSEARCH_TIME_INTERVAL_MONTH},
        {"%y-%m", FSEARCH_TIME_INTERVAL_MONTH},
        {"%Y", FSEARCH_TIME_INTERVAL_YEAR},
        {"%y", FSEARCH_TIME_INTERVAL_YEAR},
    };

    for (uint32_t i = 0; i < G_N_ELEMENTS(formats); ++i) {
        struct tm tm_start = {0};
        char *date_suffix = strptime(str, formats[i].format, &tm_start);
        if (!date_suffix) {
            continue;
        }
        g_print("Found date: %d\n", i);
        tm_start.tm_sec = tm_start.tm_min = tm_start.tm_hour = 0;
        tm_start.tm_isdst = 0;
        struct tm tm_end = tm_start;

        switch (formats[i].dtime) {
        case FSEARCH_TIME_INTERVAL_YEAR:
            // start from first day and month of the parsed year
            tm_start.tm_mday = 1;
            tm_start.tm_mon = 0;
            // end at the first day and month of the following year
            tm_end.tm_mday = 1;
            tm_end.tm_mon = 0;
            tm_end.tm_year++;
            break;
        case FSEARCH_TIME_INTERVAL_MONTH:
            // start at the first day of the parse month
            tm_start.tm_mday = 1;
            // end at the first day of the following month
            tm_end.tm_mday = 1;
            tm_end.tm_mon++;
            break;
        case FSEARCH_TIME_INTERVAL_DAY:
            // start at 0:00 of the parsed day
            // end at 0:00 of the following day
            tm_end.tm_mday++;
            break;
        default:
            continue;
        }
        const time_t time_start = get_unix_time_for_timezone(&tm_start);
        if (time_start < 0) {
            // invalid start time, try different format
            continue;
        }
        time_t time_end = get_unix_time_for_timezone(&tm_end);
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
