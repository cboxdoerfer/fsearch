#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_VIEW2 fsearch_database_view2_get_type()
G_DECLARE_FINAL_TYPE(FsearchDatabaseView2, fsearch_database_view2, FSEARCH, DATABASE_VIEW2, GObject)

FsearchDatabaseView2 *
fsearch_database_view2_new(GObject *db);

guint32
fsearch_database_view2_get_id(FsearchDatabaseView2 *view);

G_END_DECLS
