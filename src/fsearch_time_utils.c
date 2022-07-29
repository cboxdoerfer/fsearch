#define _GNU_SOURCE

#include "fsearch_time_utils.h"

#include <glib.h>
#include <stdint.h>
#include <time.h>

typedef enum FsearchTimeIntervalType {
    FSEARCH_TIME_INTERVAL_SECOND,
    FSEARCH_TIME_INTERVAL_MINUTE,
    FSEARCH_TIME_INTERVAL_HOUR,
    FSEARCH_TIME_INTERVAL_WEEK,
    FSEARCH_TIME_INTERVAL_DAY,
    FSEARCH_TIME_INTERVAL_MONTH,
    FSEARCH_TIME_INTERVAL_YEAR,
    NUM_FSEARCH_TIME_INTERVALS,
} FsearchTimeIntervalType;

typedef struct FsearchDateConstant {
    const char *format;
    int32_t val;
    FsearchTimeIntervalType dtime;
} FsearchDateConstant;

static time_t
get_unix_time_from_tm(struct tm *tm) {
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

FsearchDateConstant date_constants[] = {
    // relative to today
    {"today", 0, FSEARCH_TIME_INTERVAL_DAY},
    {"yesterday", 1, FSEARCH_TIME_INTERVAL_DAY},
    // weekdays
    {"monday", 1, FSEARCH_TIME_INTERVAL_WEEK},
    {"mon", 1, FSEARCH_TIME_INTERVAL_WEEK},
    {"tuesday", 2, FSEARCH_TIME_INTERVAL_WEEK},
    {"tue", 2, FSEARCH_TIME_INTERVAL_WEEK},
    {"wednesday", 3, FSEARCH_TIME_INTERVAL_WEEK},
    {"wed", 3, FSEARCH_TIME_INTERVAL_WEEK},
    {"thursday", 4, FSEARCH_TIME_INTERVAL_WEEK},
    {"thu", 4, FSEARCH_TIME_INTERVAL_WEEK},
    {"friday", 5, FSEARCH_TIME_INTERVAL_WEEK},
    {"fri", 5, FSEARCH_TIME_INTERVAL_WEEK},
    {"saturday", 6, FSEARCH_TIME_INTERVAL_WEEK},
    {"sat", 6, FSEARCH_TIME_INTERVAL_WEEK},
    {"sunday", 7, FSEARCH_TIME_INTERVAL_WEEK},
    {"sun", 7, FSEARCH_TIME_INTERVAL_WEEK},
    // months
    {"january", 1, FSEARCH_TIME_INTERVAL_MONTH},
    {"jan", 1, FSEARCH_TIME_INTERVAL_MONTH},
    {"february", 2, FSEARCH_TIME_INTERVAL_MONTH},
    {"feb", 2, FSEARCH_TIME_INTERVAL_MONTH},
    {"march", 3, FSEARCH_TIME_INTERVAL_MONTH},
    {"mar", 3, FSEARCH_TIME_INTERVAL_MONTH},
    {"april", 4, FSEARCH_TIME_INTERVAL_MONTH},
    {"apr", 4, FSEARCH_TIME_INTERVAL_MONTH},
    {"may", 5, FSEARCH_TIME_INTERVAL_MONTH},
    {"june", 6, FSEARCH_TIME_INTERVAL_MONTH},
    {"jun", 6, FSEARCH_TIME_INTERVAL_MONTH},
    {"july", 7, FSEARCH_TIME_INTERVAL_MONTH},
    {"jul", 7, FSEARCH_TIME_INTERVAL_MONTH},
    {"august", 8, FSEARCH_TIME_INTERVAL_MONTH},
    {"aug", 8, FSEARCH_TIME_INTERVAL_MONTH},
    {"september", 9, FSEARCH_TIME_INTERVAL_MONTH},
    {"sep", 9, FSEARCH_TIME_INTERVAL_MONTH},
    {"october", 10, FSEARCH_TIME_INTERVAL_MONTH},
    {"oct", 10, FSEARCH_TIME_INTERVAL_MONTH},
    {"november", 11, FSEARCH_TIME_INTERVAL_MONTH},
    {"nov", 11, FSEARCH_TIME_INTERVAL_MONTH},
    {"december", 12, FSEARCH_TIME_INTERVAL_MONTH},
    {"dec", 12, FSEARCH_TIME_INTERVAL_MONTH},
};

static bool
parse_explicit_date_constants(const char *str, struct tm *start, struct tm *end) {
    const time_t t_now = time(NULL);
    for (uint32_t i = 0; i < G_N_ELEMENTS(date_constants); ++i) {
        if (g_strcmp0(str, date_constants[i].format) == 0) {
            GDate *date = g_date_new();
            g_date_set_time_t(date, t_now);

            int32_t diff = 0;
            switch (date_constants[i].dtime) {
            case FSEARCH_TIME_INTERVAL_WEEK: {
                // The amount of days which have passed since the requested weekday
                diff = get_weekday_from_gdate(date) - date_constants[i].val;
                if (diff < 0) {
                    diff += 7;
                }
                g_date_subtract_days(date, diff);
                g_date_to_struct_tm(date, start);

                g_date_add_days(date, 1);
                g_date_to_struct_tm(date, end);
            } break;
            case FSEARCH_TIME_INTERVAL_DAY:
                g_date_subtract_days(date, date_constants[i].val);
                g_date_to_struct_tm(date, start);

                g_date_add_days(date, 1);
                g_date_to_struct_tm(date, end);
                break;
            case FSEARCH_TIME_INTERVAL_MONTH:
                g_date_subtract_days(date, date->day - 1);
                // The amount of months which have passed since the requested month
                diff = date->month - date_constants[i].val;
                if (diff < 0) {
                    diff += 12;
                }
                g_date_subtract_months(date, diff);
                g_date_to_struct_tm(date, start);

                g_date_add_months(date, 1);
                g_date_to_struct_tm(date, end);
                break;
            default:
                break;
            }

            g_clear_pointer(&date, g_date_free);
            return true;
        }
    }
    return false;
}

static struct tm
subtract_from_tm(struct tm tm, FsearchTimeIntervalType type, gint num) {
    struct tm tm_res = tm;

    mktime(&tm);

    switch (type) {
    case FSEARCH_TIME_INTERVAL_YEAR:
        tm_res.tm_year -= num;
        break;
    case FSEARCH_TIME_INTERVAL_MONTH:
        tm_res.tm_mon -= num;
        break;
    case FSEARCH_TIME_INTERVAL_WEEK:
        tm_res.tm_mday -= 7 * num;
        break;
    case FSEARCH_TIME_INTERVAL_DAY:
        tm_res.tm_mday -= num;
        break;
    case FSEARCH_TIME_INTERVAL_HOUR:
        tm_res.tm_hour -= num;
        break;
    case FSEARCH_TIME_INTERVAL_MINUTE:
        tm_res.tm_min -= num;
        break;
    default:
        break;
    }

    mktime(&tm_res);

    return tm_res;
}

static struct tm
round_down_tm(struct tm tm, FsearchTimeIntervalType type) {
    struct tm tm_res = {};
    tm_res.tm_mday = 1;
    tm_res.tm_isdst = tm.tm_isdst;

    mktime(&tm);

    switch (type) {
    case FSEARCH_TIME_INTERVAL_MINUTE:
        tm_res.tm_min = tm.tm_min;
    case FSEARCH_TIME_INTERVAL_HOUR:
        tm_res.tm_hour = tm.tm_hour;
    case FSEARCH_TIME_INTERVAL_DAY:
        tm_res.tm_mday = tm.tm_mday;
    case FSEARCH_TIME_INTERVAL_MONTH:
        tm_res.tm_mon = tm.tm_mon;
    case FSEARCH_TIME_INTERVAL_YEAR:
        tm_res.tm_year = tm.tm_year;
        break;
    case FSEARCH_TIME_INTERVAL_WEEK:
        // tm_wday starts with 0 for Sunday
        // shift one to the left to have 0 start at Monday instead
        tm_res.tm_mday = tm.tm_mday - (tm.tm_wday == 0 ? 6 : tm.tm_wday - 1);
        tm_res.tm_mon = tm.tm_mon;
        tm_res.tm_year = tm.tm_year;
        break;
    default:
        break;
    }

    mktime(&tm_res);
    return tm_res;
}

FsearchDateConstant num_constants[] = {
    {"one", 1},
    {"two", 2},
    {"three", 3},
    {"four", 4},
    {"five", 5},
    {"six", 6},
    {"seven", 7},
    {"eight", 8},
    {"nine", 9},
    {"ten", 10},
    {"dozen", 12},
    {"hundred", 100},
};

enum {
    // when it's 14 August, then ...
    FSEARCH_DATE_REFERS_TO_PAST_FINAL, // "last month" means July (1-31)
    FSEARCH_DATE_REFERS_TO_PAST,       // "past month" means 14 July - 14 August
    FSEARCH_DATE_REFERS_TO_NOW,        // "this month" means August (1-14)
};

FsearchDateConstant prefix_constants[] = {
    {"last", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"inthelast", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"past", FSEARCH_DATE_REFERS_TO_PAST},
    {"previous", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"prev", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"this", FSEARCH_DATE_REFERS_TO_NOW},
};

FsearchDateConstant suffix_singular_constants[] = {
    {"year", .dtime = FSEARCH_TIME_INTERVAL_YEAR},
    {"month", .dtime = FSEARCH_TIME_INTERVAL_MONTH},
    {"week", .dtime = FSEARCH_TIME_INTERVAL_WEEK},
    {"day", .dtime = FSEARCH_TIME_INTERVAL_DAY},
    {"hour", .dtime = FSEARCH_TIME_INTERVAL_HOUR},
    {"min", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
    {"minute", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
    {"sec", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
    {"second", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
};

FsearchDateConstant suffix_plural_constants[] = {
    {"years", .dtime = FSEARCH_TIME_INTERVAL_YEAR},
    {"months", .dtime = FSEARCH_TIME_INTERVAL_MONTH},
    {"weeks", .dtime = FSEARCH_TIME_INTERVAL_WEEK},
    {"days", .dtime = FSEARCH_TIME_INTERVAL_DAY},
    {"hours", .dtime = FSEARCH_TIME_INTERVAL_HOUR},
    {"min", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
    {"minutes", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
    {"sec", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
    {"seconds", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
};

static void
print_tm(struct tm *tm) {
    if (!tm) {
        return;
    }
    g_print("%04d/%02d/%02d %02d:%02d:%02d (%d)\n",
            tm->tm_year + 1900,
            tm->tm_mon,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec,
            tm->tm_isdst);
}

static bool
parse_implicit_date_constants(const char *str, struct tm *start, struct tm *end) {
    g_assert(start);
    g_assert(end);

    const char *s = str;
    int32_t date_type = FSEARCH_DATE_REFERS_TO_PAST;

    // Check if there's a prefix (e.g. last, this)
    for (uint32_t i = 0; i < G_N_ELEMENTS(prefix_constants); ++i) {
        if (g_str_has_prefix(s, prefix_constants[i].format)) {
            date_type = prefix_constants[i].val;
            s += strlen(prefix_constants[i].format);
        }
    }

    // Check if there's a number
    FsearchTimeIntervalType type = NUM_FSEARCH_TIME_INTERVALS;
    int64_t num = 1;
    if (date_type != FSEARCH_DATE_REFERS_TO_NOW) {
        char *end_of_num = (char *)s;
        num = strtoll(s, &end_of_num, 10);
        if (end_of_num == s) {
            num = 1;
            for (uint32_t i = 0; i < G_N_ELEMENTS(num_constants); ++i) {
                if (g_str_has_prefix(s, num_constants[i].format)) {
                    num = num_constants[i].val;
                    end_of_num += strlen(num_constants[i].format);
                }
            }
        }
        s = end_of_num;
    }

    if (num > 1) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(suffix_plural_constants); ++i) {
            if (g_strcmp0(s, suffix_plural_constants[i].format) == 0) {
                type = suffix_plural_constants[i].dtime;
            }
        }
    }
    else if (num == 1) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(suffix_singular_constants); ++i) {
            if (g_strcmp0(s, suffix_singular_constants[i].format) == 0) {
                type = suffix_singular_constants[i].dtime;
            }
        }
    }
    else {
        return false;
    }

    if (type != NUM_FSEARCH_TIME_INTERVALS) {
        const time_t t_now = time(NULL);
        struct tm tm_now = *localtime(&t_now);

        struct tm tm_start = {};
        struct tm tm_end = {};

        if (date_type == FSEARCH_DATE_REFERS_TO_NOW) {
            // this...
            tm_start = round_down_tm(tm_now, type);
            tm_end = tm_now;
        }
        else {
            if (date_type == FSEARCH_DATE_REFERS_TO_PAST_FINAL) {
                tm_end = round_down_tm(tm_now, type);
            }
            else {
                tm_end = tm_now;
            }
            tm_start = subtract_from_tm(tm_end, type, (gint)num);
        }

        *start = tm_start;
        *end = tm_end;

        return true;
    }
    return false;
}

static bool
parse_time_constants(const char *str, time_t *time_start_out, time_t *time_end_out) {
    struct tm tm_start = {};
    struct tm tm_end = {};

    if (parse_explicit_date_constants(str, &tm_start, &tm_end) || parse_implicit_date_constants(str, &tm_start, &tm_end)) {
        if (time_start_out) {
            *time_start_out = get_unix_time_from_tm(&tm_start);
        }
        if (time_end_out) {
            *time_end_out = get_unix_time_from_tm(&tm_end);
        }
        return true;
    }
    return false;
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
fsearch_date_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out) {
    if (parse_time_constants(str, time_start_out, time_end_out)) {
        return true;
    }

    FsearchDateConstant formats[] = {
        {"%Y-%m-%d %H:%M:%S", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
        {"%y-%m-%d %H:%M:%S", .dtime = FSEARCH_TIME_INTERVAL_SECOND},
        {"%Y-%m-%d %H:%M", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
        {"%y-%m-%d %H:%M", .dtime = FSEARCH_TIME_INTERVAL_MINUTE},
        {"%Y-%m-%d %H", .dtime = FSEARCH_TIME_INTERVAL_HOUR},
        {"%y-%m-%d %H", .dtime = FSEARCH_TIME_INTERVAL_HOUR},
        {"%Y-%m-%d", .dtime = FSEARCH_TIME_INTERVAL_DAY},
        {"%y-%m-%d", .dtime = FSEARCH_TIME_INTERVAL_DAY},
        {"%Y-%m", .dtime = FSEARCH_TIME_INTERVAL_MONTH},
        {"%y-%m", .dtime = FSEARCH_TIME_INTERVAL_MONTH},
        {"%Y", .dtime = FSEARCH_TIME_INTERVAL_YEAR},
        {"%y", .dtime = FSEARCH_TIME_INTERVAL_YEAR},
    };

    // Get a struct tm of timestamp 0
    // This is used to determine if our parsed time would result in a timestamp < 0
    time_t time_zero = 0;
    struct tm *tm_zero = localtime(&time_zero);

    for (uint32_t i = 0; i < G_N_ELEMENTS(formats); ++i) {
        struct tm tm_start = {0};
        tm_start.tm_mday = 1;
        char *date_suffix = strptime(str, formats[i].format, &tm_start);
        if (!date_suffix || date_suffix[0] != '\0') {
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

        if (time_start_out) {
            *time_start_out = get_unix_time_from_tm(&tm_start);
        }
        if (time_end_out) {
            *time_end_out = get_unix_time_from_tm(&tm_end);
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
    return false;
}
