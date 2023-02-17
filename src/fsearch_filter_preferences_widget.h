#pragma once

#include <gtk/gtk.h>

#include "fsearch_filter_manager.h"

G_BEGIN_DECLS

#define FSEARCH_FILTER_PREFERENCES_WIDGET_TYPE (fsearch_filter_preferences_widget_get_type())

G_DECLARE_FINAL_TYPE(FsearchFilterPreferencesWidget,
                     fsearch_filter_preferences_widget,
                     FSEARCH,
                     FILTER_PREFERENCES_WIDGET,
                     GtkBox)

FsearchFilterPreferencesWidget *
fsearch_filter_preferences_widget_new(FsearchFilterManager *filters);

FsearchFilterManager *
fsearch_filter_preferences_widget_get_filter_manager(FsearchFilterPreferencesWidget *widget);

G_END_DECLS
