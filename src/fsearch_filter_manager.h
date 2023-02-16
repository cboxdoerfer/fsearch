#pragma once

#include <glib.h>
#include <glib-object.h>
#include <stdbool.h>

#include "fsearch_filter.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_FILTER_MANAGER (fsearch_filter_manager_get_type())

typedef struct FsearchFilterManager FsearchFilterManager;

GType
fsearch_database_info_get_type(void);

FsearchFilterManager *
fsearch_filter_manager_ref(FsearchFilterManager *manager);

void
fsearch_filter_manager_unref(FsearchFilterManager *manager);

FsearchFilterManager *
fsearch_filter_manager_new(void);

FsearchFilterManager *
fsearch_filter_manager_new_with_defaults(void);

FsearchFilter *
fsearch_filter_manager_get_filter_for_name(FsearchFilterManager *manager, const char *name);

FsearchFilterManager *
fsearch_filter_manager_copy(FsearchFilterManager *manager);

guint
fsearch_filter_manager_get_num_filters(FsearchFilterManager *manager);

FsearchFilter *
fsearch_filter_manager_get_filter(FsearchFilterManager *manager, guint idx);

void
fsearch_filter_manager_append_filter(FsearchFilterManager *manager, FsearchFilter *filter);

void
fsearch_filter_manager_reorder(FsearchFilterManager *manager, gint *new_order, size_t new_order_len);

void
fsearch_filter_manager_remove(FsearchFilterManager *manager, FsearchFilter *filter);

void
fsearch_filter_manager_edit(FsearchFilterManager *manager,
                            FsearchFilter *filter,
                            const char *name,
                            const char *macro,
                            const char *query,
                            FsearchQueryFlags flags);

bool
fsearch_filter_manager_cmp(FsearchFilterManager *manager_1, FsearchFilterManager *manager_2);

G_END_DECLS