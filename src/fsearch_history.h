#pragma once

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <time.h>
#include "fsearch_config.h"
#include "fsearch_limits.h"

void
fsearch_history_add(GtkListStore *history, const gchar *query, int sort_by);

int
fsearch_history_exists();

void
write_liststore_to_csv(GtkListStore *liststore);

void
write_csv_to_liststore(GtkListStore *liststore);