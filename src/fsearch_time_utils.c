#define _GNU_SOURCE

#include "fsearch_time_utils.h"

#include <glib.h>
#include <stdint.h>
#include <time.h>

typedef enum FsearchDateTimeType {
    FSEARCH_DATE_TIME_TYPE_SECOND,
    FSEARCH_DATE_TIME_TYPE_MINUTE,
    FSEARCH_DATE_TIME_TYPE_HOUR,
    FSEARCH_DATE_TIME_TYPE_WEEK,
    FSEARCH_DATE_TIME_TYPE_DAY,
    FSEARCH_DATE_TIME_TYPE_MONTH,
    FSEARCH_DATE_TIME_TYPE_YEAR,
    NUM_FSEARCH_DATE_TIME_TYPES,
} FsearchDateTimeType;

typedef struct FsearchDateTimeConstant {
    const char *format;
    int32_t val;
    FsearchDateTimeType type;
} FsearchDateTimeConstant;

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

FsearchDateTimeConstant date_constants[] = {
    // relative to today
    {"today", 0, FSEARCH_DATE_TIME_TYPE_DAY},
    {"yesterday", 1, FSEARCH_DATE_TIME_TYPE_DAY},
    // weekdays
    {"monday", 1, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"mon", 1, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"tuesday", 2, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"tue", 2, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"wednesday", 3, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"wed", 3, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"thursday", 4, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"thu", 4, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"friday", 5, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"fri", 5, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"saturday", 6, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"sat", 6, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"sunday", 7, FSEARCH_DATE_TIME_TYPE_WEEK},
    {"sun", 7, FSEARCH_DATE_TIME_TYPE_WEEK},
    // months
    {"january", 1, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"jan", 1, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"february", 2, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"feb", 2, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"march", 3, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"mar", 3, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"april", 4, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"apr", 4, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"may", 5, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"june", 6, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"jun", 6, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"july", 7, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"jul", 7, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"august", 8, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"aug", 8, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"september", 9, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"sep", 9, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"october", 10, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"oct", 10, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"november", 11, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"nov", 11, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"december", 12, FSEARCH_DATE_TIME_TYPE_MONTH},
    {"dec", 12, FSEARCH_DATE_TIME_TYPE_MONTH},
};

static bool
parse_explicit_date_time_constants(const char *str, struct tm *start, struct tm *end) {
    const time_t t_now = time(NULL);
    for (uint32_t i = 0; i < G_N_ELEMENTS(date_constants); ++i) {
        if (g_strcmp0(str, date_constants[i].format) == 0) {
            GDate *date = g_date_new();
            g_date_set_time_t(date, t_now);

            int32_t diff = 0;
            switch (date_constants[i].type) {
            case FSEARCH_DATE_TIME_TYPE_WEEK: {
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
            case FSEARCH_DATE_TIME_TYPE_DAY:
                g_date_subtract_days(date, date_constants[i].val);
                g_date_to_struct_tm(date, start);

                g_date_add_days(date, 1);
                g_date_to_struct_tm(date, end);
                break;
            case FSEARCH_DATE_TIME_TYPE_MONTH:
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
subtract_from_tm(struct tm tm, FsearchDateTimeType type, gint num) {
    struct tm tm_res = tm;

    mktime(&tm);

    switch (type) {
    case FSEARCH_DATE_TIME_TYPE_YEAR:
        tm_res.tm_year -= num;
        break;
    case FSEARCH_DATE_TIME_TYPE_MONTH:
        tm_res.tm_mon -= num;
        break;
    case FSEARCH_DATE_TIME_TYPE_WEEK:
        tm_res.tm_mday -= 7 * num;
        break;
    case FSEARCH_DATE_TIME_TYPE_DAY:
        tm_res.tm_mday -= num;
        break;
    case FSEARCH_DATE_TIME_TYPE_HOUR:
        tm_res.tm_hour -= num;
        break;
    case FSEARCH_DATE_TIME_TYPE_MINUTE:
        tm_res.tm_min -= num;
        break;
    default:
        break;
    }

    mktime(&tm_res);

    return tm_res;
}

static struct tm
round_down_tm_with_date_time_accuracy(struct tm tm, FsearchDateTimeType type) {
    struct tm tm_res = {};
    tm_res.tm_mday = 1;
    tm_res.tm_isdst = tm.tm_isdst;

    mktime(&tm);

    switch (type) {
    case FSEARCH_DATE_TIME_TYPE_MINUTE:
        tm_res.tm_min = tm.tm_min;
    case FSEARCH_DATE_TIME_TYPE_HOUR:
        tm_res.tm_hour = tm.tm_hour;
    case FSEARCH_DATE_TIME_TYPE_DAY:
        tm_res.tm_mday = tm.tm_mday;
    case FSEARCH_DATE_TIME_TYPE_MONTH:
        tm_res.tm_mon = tm.tm_mon;
    case FSEARCH_DATE_TIME_TYPE_YEAR:
        tm_res.tm_year = tm.tm_year;
        break;
    case FSEARCH_DATE_TIME_TYPE_WEEK:
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

FsearchDateTimeConstant num_constants[] = {
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

FsearchDateTimeConstant prefix_constants[] = {
    {"last", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"inthelast", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"past", FSEARCH_DATE_REFERS_TO_PAST},
    {"previous", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"prev", FSEARCH_DATE_REFERS_TO_PAST_FINAL},
    {"this", FSEARCH_DATE_REFERS_TO_NOW},
};

FsearchDateTimeConstant suffix_singular_constants[] = {
    {"year", .type = FSEARCH_DATE_TIME_TYPE_YEAR},
    {"month", .type = FSEARCH_DATE_TIME_TYPE_MONTH},
    {"week", .type = FSEARCH_DATE_TIME_TYPE_WEEK},
    {"day", .type = FSEARCH_DATE_TIME_TYPE_DAY},
    {"hour", .type = FSEARCH_DATE_TIME_TYPE_HOUR},
    {"min", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
    {"minute", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
    {"sec", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
    {"second", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
};

FsearchDateTimeConstant suffix_plural_constants[] = {
    {"years", .type = FSEARCH_DATE_TIME_TYPE_YEAR},
    {"months", .type = FSEARCH_DATE_TIME_TYPE_MONTH},
    {"weeks", .type = FSEARCH_DATE_TIME_TYPE_WEEK},
    {"days", .type = FSEARCH_DATE_TIME_TYPE_DAY},
    {"hours", .type = FSEARCH_DATE_TIME_TYPE_HOUR},
    {"min", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
    {"minutes", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
    {"sec", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
    {"seconds", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
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
parse_implicit_date_time_constants(const char *str, struct tm *start, struct tm *end) {
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
    FsearchDateTimeType type = NUM_FSEARCH_DATE_TIME_TYPES;
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
                type = suffix_plural_constants[i].type;
            }
        }
    }
    else if (num == 1) {
        for (uint32_t i = 0; i < G_N_ELEMENTS(suffix_singular_constants); ++i) {
            if (g_strcmp0(s, suffix_singular_constants[i].format) == 0) {
                type = suffix_singular_constants[i].type;
            }
        }
    }
    else {
        return false;
    }

    if (type == NUM_FSEARCH_DATE_TIME_TYPES) {
        // No date/time type was specified
        return false;
    }

    const time_t t_now = time(NULL);
    struct tm tm_now = *localtime(&t_now);

    struct tm tm_start = {};
    struct tm tm_end = {};

    if (date_type == FSEARCH_DATE_REFERS_TO_NOW) {
        // start of the current year/month/... until now
        tm_start = round_down_tm_with_date_time_accuracy(tm_now, type);
        tm_end = tm_now;
    }
    else {
        if (date_type == FSEARCH_DATE_REFERS_TO_PAST_FINAL) {
            // end at the beginning of the current year/month/...
            tm_end = round_down_tm_with_date_time_accuracy(tm_now, type);
        }
        else {
            // end at now
            tm_end = tm_now;
        }
        // start num * years/months/... before tm_end
        tm_start = subtract_from_tm(tm_end, type, (gint)num);
    }

    *start = tm_start;
    *end = tm_end;

    return true;
}

static bool
parse_date_time_constants(const char *str, time_t *time_start_out, time_t *time_end_out) {
    struct tm tm_start = {};
    struct tm tm_end = {};

    if (parse_explicit_date_time_constants(str, &tm_start, &tm_end)
        || parse_implicit_date_time_constants(str, &tm_start, &tm_end)) {
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
cmp_tm(struct tm *tm1, struct tm *tm2, FsearchDateTimeType interval) {
    g_assert(tm1);
    g_assert(tm2);

    int res = cmp_int(tm1->tm_year, tm2->tm_year);
    if (res != 0 || interval == FSEARCH_DATE_TIME_TYPE_YEAR) {
        return res;
    }
    res = cmp_int(tm1->tm_mon, tm2->tm_mon);
    if (res != 0 || interval == FSEARCH_DATE_TIME_TYPE_MONTH) {
        return res;
    }
    res = cmp_int(tm1->tm_mday, tm2->tm_mday);
    if (res != 0 || interval == FSEARCH_DATE_TIME_TYPE_DAY) {
        return res;
    }
    res = cmp_int(tm1->tm_hour, tm2->tm_hour);
    if (res != 0 || interval == FSEARCH_DATE_TIME_TYPE_HOUR) {
        return res;
    }
    res = cmp_int(tm1->tm_min, tm2->tm_min);
    if (res != 0 || interval == FSEARCH_DATE_TIME_TYPE_MINUTE) {
        return res;
    }
    return cmp_int(tm1->tm_sec, tm2->tm_sec);
}

static void
lower_clamp_tm(struct tm *tm_to_clamp, struct tm *tm_lower_bound) {
    g_return_if_fail(tm_to_clamp);
    g_return_if_fail(tm_lower_bound);

    int cmp_res = cmp_tm(tm_to_clamp, tm_lower_bound, FSEARCH_DATE_TIME_TYPE_SECOND);
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

static bool
round_down_tm_to_reference_with_date_time_accuracy(struct tm *tm, struct tm *tm_reference, FsearchDateTimeType type) {
    const int cmp_result = cmp_tm(tm, tm_reference, type);
    if (cmp_result < 0) {
        // tm is smaller than tm_reference and can't be represented with unix time
        return false;
    }

    if (cmp_result == 0) {
        // Even though they're reported as equal, tm can still point to a date before tm_reference.
        // That's because we're only comparing tm up to the specified accuracy of type
        // E.g. a query like `dm:1970` might translate to tm referring to 1969-12-31 23:00:00 UTC
        // due to the current timezone being one hour ahead of UTC.
        lower_clamp_tm(tm, tm_reference);
    }
    return true;
}

bool
fsearch_date_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out) {
    if (parse_date_time_constants(str, time_start_out, time_end_out)) {
        return true;
    }

    FsearchDateTimeConstant formats[] = {
        {"%Y-%m-%d %H:%M:%S", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
        {"%y-%m-%d %H:%M:%S", .type = FSEARCH_DATE_TIME_TYPE_SECOND},
        {"%Y-%m-%d %H:%M", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
        {"%y-%m-%d %H:%M", .type = FSEARCH_DATE_TIME_TYPE_MINUTE},
        {"%Y-%m-%d %H", .type = FSEARCH_DATE_TIME_TYPE_HOUR},
        {"%y-%m-%d %H", .type = FSEARCH_DATE_TIME_TYPE_HOUR},
        {"%Y-%m-%d", .type = FSEARCH_DATE_TIME_TYPE_DAY},
        {"%y-%m-%d", .type = FSEARCH_DATE_TIME_TYPE_DAY},
        {"%Y-%m", .type = FSEARCH_DATE_TIME_TYPE_MONTH},
        {"%y-%m", .type = FSEARCH_DATE_TIME_TYPE_MONTH},
        {"%Y", .type = FSEARCH_DATE_TIME_TYPE_YEAR},
        {"%y", .type = FSEARCH_DATE_TIME_TYPE_YEAR},
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

        switch (formats[i].type) {
        case FSEARCH_DATE_TIME_TYPE_YEAR:
            tm_end.tm_year++;
            break;
        case FSEARCH_DATE_TIME_TYPE_MONTH:
            tm_end.tm_mon++;
            break;
        case FSEARCH_DATE_TIME_TYPE_DAY:
            tm_end.tm_mday++;
            break;
        case FSEARCH_DATE_TIME_TYPE_HOUR:
            tm_end.tm_hour++;
            break;
        case FSEARCH_DATE_TIME_TYPE_MINUTE:
            tm_end.tm_min++;
            break;
        case FSEARCH_DATE_TIME_TYPE_SECOND:
            tm_end.tm_sec++;
            break;
        default:
            continue;
        }

        if (!round_down_tm_to_reference_with_date_time_accuracy(&tm_start, tm_zero, formats[i].type)
            && !round_down_tm_to_reference_with_date_time_accuracy(&tm_end, tm_zero, formats[i].type)) {
            // both tm_start and tm_end can't be represented with unix time
            // e.g. dm:1965
            return false;
        }

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
