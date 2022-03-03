#include "fsearch_size_utils.h"

#include <assert.h>
#include <stdlib.h>

bool
fsearch_size_parse(const char *str, int64_t *size_out, char **end_ptr) {
    assert(size_out != NULL);
    char *size_suffix = NULL;
    int64_t size = strtoll(str, &size_suffix, 10);
    if (size_suffix == str) {
        return false;
    }
    if (size_suffix && *size_suffix != '\0') {
        switch (*size_suffix) {
        case 'k':
        case 'K':
            size *= 1000;
            break;
        case 'm':
        case 'M':
            size *= 1000 * 1000;
            break;
        case 'g':
        case 'G':
            size *= 1000 * 1000 * 1000;
            break;
        case 't':
        case 'T':
            size *= (int64_t)1000 * 1000 * 1000 * 1000;
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
    if (end_ptr) {
        *end_ptr = size_suffix;
    }
    *size_out = size;
    return true;
}
