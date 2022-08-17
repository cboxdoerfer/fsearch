#include "fsearch_size_utils.h"

#include <glib.h>
#include <stdlib.h>

bool
fsearch_size_parse(const char *str, int64_t *size_out, int64_t *size_end_out) {
    g_assert(str);

    char *size_suffix = NULL;
    int64_t size = strtoll(str, &size_suffix, 10);
    if (size_suffix == str) {
        return false;
    }

    int64_t plus = 0;
    if (size_suffix && *size_suffix != '\0') {
        switch (*size_suffix) {
        case 'k':
        case 'K':
            size *= 1000;
            plus = 1000 - 50 - 1;
            break;
        case 'm':
        case 'M':
            size *= 1000 * 1000;
            plus = 1000 * (1000 - 50) - 1;
            break;
        case 'g':
        case 'G':
            size *= 1000 * 1000 * 1000;
            plus = 1000 * 1000 * (1000 - 50) - 1;
            break;
        case 't':
        case 'T':
            size *= (int64_t)1000 * 1000 * 1000 * 1000;
            plus = (int64_t)1000 * 1000 * 1000 * (1000 - 50) - 1;
            break;
        default:
            goto out;
        }
        size_suffix++;

        switch (*size_suffix) {
        case 'b':
        case 'B':
            size_suffix++;
            break;
        default:
            goto out;
        }
    }
out:
    if (size_suffix && size_suffix[0] != '\0') {
        return false;
    }
    if (size_end_out) {
        *size_end_out = size + plus;
    }
    if (size_out) {
        *size_out = size;
    }
    return true;
}
