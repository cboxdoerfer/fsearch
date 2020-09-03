#pragma once

#include <glib.h>

GTimer *
fsearch_timer_start ();

void
fsearch_timer_stop (GTimer *t, const char *format);

void
fsearch_timer_elapsed (GTimer *t, const char *format);

