#pragma once

#include <stdbool.h>
#include <time.h>

bool
fsearch_time_parse_interval(const char *str, time_t *time_start_out, time_t *time_end_out, char **end_ptr);