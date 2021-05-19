#pragma once

#include "fsearch_array.h"

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

void
fsearch_selection_free(GHashTable *selection);

GHashTable *
fsearch_selection_new(void);

void
fsearch_selection_select_toggle(GHashTable *selection, gpointer item);

void
fsearch_selection_select(GHashTable *selection, gpointer item);

bool
fsearch_selection_is_selected(GHashTable *selection, gpointer item);

void
fsearch_selection_select_all(GHashTable *selection, DynamicArray *items);

void
fsearch_selection_unselect_all(GHashTable *selection);

void
fsearch_selection_invert(GHashTable *selection, DynamicArray *items);

uint32_t
fsearch_selection_get_num_selected(GHashTable *selection);

void
fsearch_selection_for_each(GHashTable *selection, GHFunc func, gpointer user_data);
