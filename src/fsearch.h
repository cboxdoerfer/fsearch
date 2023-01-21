/*
FSearch - A fast file search utility
Copyright © 2020 Christian Boxdörfer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "fsearch_config.h"
#include "fsearch_database2.h"
#include "fsearch_thread_pool.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <inttypes.h>
#include <stdbool.h>

G_BEGIN_DECLS

#define FSEARCH_APPLICATION_TYPE (fsearch_application_get_type())
#define FSEARCH_APPLICATION_DEFAULT (FSEARCH_APPLICATION(g_application_get_default()))

G_DECLARE_FINAL_TYPE(FsearchApplication, fsearch_application, FSEARCH, APPLICATION, GtkApplication)

FsearchApplication *
fsearch_application_new(void);

G_END_DECLS

typedef enum {
    FSEARCH_DATABASE_STATE_SCANNING,
    FSEARCH_DATABASE_STATE_LOADING,
    FSEARCH_DATABASE_STATE_IDLE,
    NUM_FSEARCH_DATABASE_STATES
} FsearchDatabaseState;

FsearchDatabaseState
fsearch_application_get_db_state(FsearchApplication *fsearch);

uint32_t
fsearch_application_get_num_db_entries(FsearchApplication *fsearch);

FsearchDatabase2 *
fsearch_application_get_db(FsearchApplication *fsearch);

FsearchConfig *
fsearch_application_get_config(FsearchApplication *fsearch);

char *
fsearch_application_get_database_file_path(void);

char *
fsearch_application_get_database_dir(void);

gboolean
fsearch_application_has_file_manager_on_bus(FsearchApplication *fsearch);
