/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

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

#include <glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

// Register the D-Bus query interface on the application's bus connection.
// This allows CLI --query to reach the running GUI instance's in-memory database.
// Called from fsearch.c during startup.
void
fsearch_query_cli_dbus_register(void);

// Try to perform a query via D-Bus to a running FSearch instance.
// Returns 0 on success (results printed to stdout), non-zero on failure
// (caller should fall back to standalone mode).
int
fsearch_query_cli_run_via_dbus(const char *search_term,
                               int limit,
                               bool use_regex,
                               bool match_case,
                               bool search_in_path,
                               bool files_only,
                               bool folders_only,
                               const char *sort_prop,
                               bool sort_desc,
                               bool pretty);

G_END_DECLS
