#define _GNU_SOURCE

#include "fsearch_time_utils.h"

#include <glib.h>
#include <stdint.h>
#include <time.h>

typedef enum FsearchTimeIntervalType {
    FSEARCH_TIME_INTERVAL_SECOND,
    FSEARCH_TIME_INTERVAL_MINUTE,
    FSEARCH_TIME_INTERVAL_HOUR,
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
    return MAX(0, mktime(tm));
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

static int
cmp_int(int64_t i1, int64_t i2) {
    if (i1 < i2) {
        return -1;
    }
    else if (i1 > i2) {
        return 1;
    }
    return 0;
}

static int
cmp_tm(struct tm *tm1, struct tm *tm2, FsearchTimeIntervalType interval) {
    g_assert(tm1);
    g_assert(tm2);

    int res = cmp_int(tm1->tm_year, tm2->tm_year);
    if (res != 0 || interval == FSEARCH_TIME_INTERVAL_YEAR) {
        return res;
    }
    res = cmp_int(tm1->tm_mon, tm2->tm_mon);
    if (res != 0 || interval == FSEARCH_TIME_INTERVAL_MONTH) {
        return res;
    }
    res = cmp_int(tm1->tm_mday, tm2->tm_mday);
    if (res != 0 || interval == FSEARCH_TIME_INTERVAL_DAY) {
        return res;
    }
    res = cmp_int(tm1->tm_hour, tm2->tm_hour);
    if (res != 0 || interval == FSEARCH_TIME_INTERVAL_HOUR) {
        return res;
    }
    res = cmp_int(tm1->tm_min, tm2->tm_min);
    if (res != 0 || interval == FSEARCH_TIME_INTERVAL_MINUTE) {
        return res;
    }
    return cmp_int(tm1->tm_sec, tm2->tm_sec);
}

static void
lower_clamp_tm(struct tm *tm_to_clamp, struct tm *tm_lower_bound) {
    g_return_if_fail(tm_to_clamp);
    g_return_if_fail(tm_lower_bound);

    int cmp_res = cmp_tm(tm_to_clamp, tm_lower_bound, FSEARCH_TIME_INTERVAL_SECOND);
    if (cmp_res >= 0) {
        return;
    }
    tm_to_clamp->tm_year = tm_lower_bound->tm_year;
    tm_to_clamp->tm_mon = tm_lower_bound->tm_mon;
    tm_to_clamp->tm_mday = tm_lower_bound->tm_mday;
    tm_to_clamp->tm_hour = tm_lower_bound->tm_hour;
    tm_to_clamp->tm_min = tm_lower_bound->tm_min;
    tm_to_clamp->tm_sec = tm_lower_bound->tm_sec;
}

bool
fsearch_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr) {
    if (parse_time_constants(str, time_start_out, time_end_out, end_ptr)) {
        return true;
    }

    FsearchTimeFormat formats[] = {
        {"%Y-%m-%d %H:%M:%S", FSEARCH_TIME_INTERVAL_SECOND},
        {"%y-%m-%d %H:%M:%S", FSEARCH_TIME_INTERVAL_SECOND},
        {"%Y-%m-%d %H:%M", FSEARCH_TIME_INTERVAL_MINUTE},
        {"%y-%m-%d %H:%M", FSEARCH_TIME_INTERVAL_MINUTE},
        {"%Y-%m-%d %H", FSEARCH_TIME_INTERVAL_HOUR},
        {"%y-%m-%d %H", FSEARCH_TIME_INTERVAL_HOUR},
        {"%Y-%m-%d", FSEARCH_TIME_INTERVAL_DAY},
        {"%y-%m-%d", FSEARCH_TIME_INTERVAL_DAY},
        {"%Y-%m", FSEARCH_TIME_INTERVAL_MONTH},
        {"%y-%m", FSEARCH_TIME_INTERVAL_MONTH},
        {"%Y", FSEARCH_TIME_INTERVAL_YEAR},
        {"%y", FSEARCH_TIME_INTERVAL_YEAR},
    };

    // Get a struct tm of timestamp 0
    // This is used to determine if our parsed time would result in a timestamp < 0
    time_t time_zero = 0;
    struct tm *tm_zero = localtime(&time_zero);

    for (uint32_t i = 0; i < G_N_ELEMENTS(formats); ++i) {
        struct tm tm_start = {0};
        tm_start.tm_mday = 1;
        char *date_suffix = strptime(str, formats[i].format, &tm_start);
        if (!date_suffix) {
            continue;
        }

        if (tm_start.tm_year < tm_zero->tm_year) {
            // Try again with a different format.
            // This is necessary to handle cases like the following:
            // dm:14 will be successfully parsed by %Y, with tm_year set to -1886
            // But we want tm_year to be 2014, so we'll let the next format (%y) handle this case,
            // which will parse the year as expect.
            continue;
        }

        tm_start.tm_isdst = -1;
        struct tm tm_end = tm_start;

        bool invalid_start_time = false;
        bool invalid_end_time = false;

        const int start_zero_cmp_result = cmp_tm(&tm_start, tm_zero, formats[i].dtime);
        if (start_zero_cmp_result == 0) {
            // make sure tm_start is not less than tm_zero
            lower_clamp_tm(&tm_start, tm_zero);
        }
        else if (start_zero_cmp_result < 0) {
            invalid_start_time = true;
        }

        switch (formats[i].dtime) {
        case FSEARCH_TIME_INTERVAL_YEAR:
            tm_end.tm_year++;
            break;
        case FSEARCH_TIME_INTERVAL_MONTH:
            tm_end.tm_mon++;
            break;
        case FSEARCH_TIME_INTERVAL_DAY:
            tm_end.tm_mday++;
            break;
        case FSEARCH_TIME_INTERVAL_HOUR:
            tm_end.tm_hour++;
            break;
        case FSEARCH_TIME_INTERVAL_MINUTE:
            tm_end.tm_min++;
            break;
        case FSEARCH_TIME_INTERVAL_SECOND:
            tm_end.tm_sec++;
            break;
        default:
            continue;
        }

        const int end_zero_cmp_result = cmp_tm(&tm_end, tm_zero, formats[i].dtime);
        if (end_zero_cmp_result == 0) {
            lower_clamp_tm(&tm_end, tm_zero);
        }
        else if (end_zero_cmp_result < 0) {
            invalid_end_time = true;
        }

        if (invalid_start_time && invalid_end_time) {
            goto out;
        }

        const time_t time_start = get_unix_time_for_timezone(&tm_start);
        const time_t time_end = get_unix_time_for_timezone(&tm_end);

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

out:
    if (time_start_out) {
        *time_start_out = 0;
    }
    if (time_end_out) {
        *time_end_out = 0;
    }
    if (end_ptr) {
        *end_ptr = (char *)str;
    }
    return false;
}
