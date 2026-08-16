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

#define G_LOG_DOMAIN "fsearch-query-dbus"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fsearch_query_cli_dbus.h"
#include "fsearch.h"
#include "fsearch_database.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_search_view.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_entry.h"
#include "fsearch_query.h"
#include "fsearch_query_flags.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FSEARCH_QUERY_DBUS_INTERFACE "io.github.cboxdoerfer.FSearch.Query"
#define FSEARCH_QUERY_DBUS_OBJECT    "/io/github/cboxdoerfer/FSearch/Query"
#define FSEARCH_QUERY_DBUS_TIMEOUT   30000 /* ms */

/* ───────── Format search results as JSON string ───────── */

static const char *
human_size(int64_t bytes) {
    static char buf[32];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%" PRId64 " B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KiB", bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MiB", bytes / (1024.0 * 1024.0));
    else
        snprintf(buf, sizeof(buf), "%.2f GiB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

static char *
format_results_json(FsearchDatabaseIndexStore *store,
                    FsearchDatabaseSearchView *view,
                    const char *search_term,
                    int limit,
                    int total_results,
                    uint32_t num_files,
                    uint32_t num_folders)
{
    g_autoptr(GString) buf = g_string_new("");
    g_string_append(buf, "{\n");
    g_string_append_printf(buf, "  \"tool\": \"fsearch\",\n");
    g_string_append_printf(buf, "  \"version\": \"0.3\",\n");
    g_string_append_printf(buf, "  \"via\": \"dbus\",\n");
    g_string_append_printf(buf, "  \"query\": {\n");
    g_string_append_printf(buf, "    \"term\": ");
    // Need to escape: write to temp FILE* then read back
    // Simpler: use our json_escape on a temp file
    g_string_append(buf, "\"");
    {   // escape search_term
        const char *p;
        for (p = search_term ? search_term : ""; *p; p++) {
            switch (*p) {
            case '"':  g_string_append(buf, "\\\""); break;
            case '\\': g_string_append(buf, "\\\\"); break;
            case '\n': g_string_append(buf, "\\n");  break;
            case '\r': g_string_append(buf, "\\r");  break;
            case '\t': g_string_append(buf, "\\t");  break;
            default:
                if ((unsigned char)*p < 0x20)
                    g_string_append_printf(buf, "\\u%04x", (unsigned char)*p);
                else
                    g_string_append_c(buf, *p);
                break;
            }
        }
    }
    g_string_append(buf, "\",\n");
    g_string_append_printf(buf, "    \"limit\": %d,\n", limit);
    g_string_append_printf(buf, "    \"total_results\": %d\n", total_results);
    g_string_append(buf, "  },\n");
    g_string_append(buf, "  \"results\": [\n");

    int count = 0;
    int max = (limit > 0) ? MIN(limit, total_results) : total_results;
    for (int i = 0; i < max; i++) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) break;

        const char *name = db_entry_get_name_raw(entry);
        const char *path = db_entry_get_path_full(entry)->str;
        int64_t sz = db_entry_get_size(entry);
        time_t mtime = db_entry_get_mtime(entry);
        bool is_dir = db_entry_is_folder(entry);

        if (count > 0) g_string_append(buf, ",\n");
        g_string_append(buf, "    {\n");

        g_string_append(buf, "      \"name\": \"");
        // escape name
        for (const char *p = name ? name : ""; *p; p++) {
            switch (*p) {
            case '"':  g_string_append(buf, "\\\""); break;
            case '\\': g_string_append(buf, "\\\\"); break;
            default:   g_string_append_c(buf, *p);  break;
            }
        }
        g_string_append(buf, "\",\n");

        g_string_append(buf, "      \"path\": \"");
        for (const char *p = path ? path : ""; *p; p++) {
            switch (*p) {
            case '"':  g_string_append(buf, "\\\""); break;
            case '\\': g_string_append(buf, "\\\\"); break;
            default:   g_string_append_c(buf, *p);  break;
            }
        }
        g_string_append(buf, "\",\n");

        const char *ext = strrchr(name ? name : "", '.');
        g_string_append_printf(buf, "      \"extension\": \"%s\",\n", ext ? ext + 1 : "");
        g_string_append_printf(buf, "      \"type\": \"%s\",\n", is_dir ? "folder" : "file");
        g_string_append_printf(buf, "      \"size\": %" PRId64 ",\n", sz);
        g_string_append_printf(buf, "      \"size_human\": \"%s\",\n", human_size(sz));

        struct tm *tm = localtime(&mtime);
        char tbuf[64];
        if (tm && strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S%z", tm))
            g_string_append_printf(buf, "      \"modified\": \"%s\",\n", tbuf);
        else
            g_string_append(buf, "      \"modified\": null,\n");

        g_string_append_printf(buf, "      \"mtime\": %ld\n", (long)mtime);
        g_string_append(buf, "    }");
        count++;
    }

    g_string_append(buf, "\n  ],\n");
    g_string_append_printf(buf, "  \"stats\": {\n");
    g_string_append_printf(buf, "    \"db_files\": %u,\n", num_files);
    g_string_append_printf(buf, "    \"db_folders\": %u,\n", num_folders);
    g_string_append_printf(buf, "    \"results_shown\": %d\n", count);
    g_string_append(buf, "  }\n");
    g_string_append(buf, "}\n");

    return g_string_free(g_steal_pointer(&buf), FALSE);
}

/* ───────── Perform search on an already-loaded database ───────── */

static int
do_search(FsearchDatabase *db,
          const char *search_term,
          int limit,
          bool use_regex,
          bool match_case,
          bool search_in_path,
          bool files_only,
          bool folders_only,
          FsearchDatabaseIndexProperty sort_prop,
          bool sort_desc,
          char **out_json)
{
    if (!db) return -1;
    FsearchDatabaseIndexStore *store = fsearch_database_get_store(db);
    if (!store) return -1;

    uint32_t num_files = fsearch_database_index_store_get_num_files(store);
    uint32_t num_folders = fsearch_database_index_store_get_num_folders(store);
    if (num_files + num_folders == 0) return -1;

    FsearchQueryFlags flags = 0;
    if (use_regex)      flags |= QUERY_FLAG_REGEX;
    if (match_case)     flags |= QUERY_FLAG_MATCH_CASE;
    if (search_in_path) flags |= QUERY_FLAG_SEARCH_IN_PATH;
    if (files_only)     flags |= QUERY_FLAG_FILES_ONLY;
    else if (folders_only) flags |= QUERY_FLAG_FOLDERS_ONLY;

    g_autoptr(FsearchQuery) query = fsearch_query_new(search_term, NULL, NULL, flags, "dbus");
    if (!query) return -1;

    uint32_t view_id = 0;
    GtkSortType sort_type = sort_desc ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    g_autoptr(GCancellable) cancellable = g_cancellable_new();

    bool ok = fsearch_database_index_store_search(store, view_id, query, sort_prop, sort_type, cancellable);
    if (!ok) return -1;

    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, view_id);
    if (!view) return -1;

    FsearchDatabaseSearchInfo *info = fsearch_database_index_store_get_search_info(store, view_id);
    int total = info ? (int)fsearch_database_search_info_get_num_entries(info) : 0;

    *out_json = format_results_json(store, view, search_term, limit, total, num_files, num_folders);
    return 0;
}

/* ═══════════════════════════════════════════
 *  SERVER SIDE — registered in running GUI
 * ═══════════════════════════════════════════ */

static const gchar kQueryInterfaceXml[] =
    "<node>"
    "  <interface name='" FSEARCH_QUERY_DBUS_INTERFACE "'>"
    "    <method name='Search'>"
    "      <arg type='s' name='search_term' direction='in'/>"
    "      <arg type='i' name='limit' direction='in'/>"
    "      <arg type='b' name='regex' direction='in'/>"
    "      <arg type='b' name='match_case' direction='in'/>"
    "      <arg type='b' name='search_in_path' direction='in'/>"
    "      <arg type='b' name='files_only' direction='in'/>"
    "      <arg type='b' name='folders_only' direction='in'/>"
    "      <arg type='s' name='sort' direction='in'/>"
    "      <arg type='b' name='desc' direction='in'/>"
    "      <arg type='b' name='pretty' direction='in'/>"
    "      <arg type='s' name='result_json' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GDBusInterfaceInfo *query_iface_info = NULL;
static GDBusNodeInfo *query_node_info = NULL;

static void
query_method_call(GDBusConnection *connection,
                  const gchar *sender,
                  const gchar *object_path,
                  const gchar *interface_name,
                  const gchar *method_name,
                  GVariant *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    if (g_strcmp0(method_name, "Search") != 0) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "io.github.cboxdoerfer.FSearch.Query.Error.UnknownMethod",
            "Unknown method");
        return;
    }

    FsearchApplication *app = FSEARCH_APPLICATION_DEFAULT;
    if (!app) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "io.github.cboxdoerfer.FSearch.Query.Error.NoApp",
            "Application not available");
        return;
    }

    FsearchDatabase *db = fsearch_application_get_db(app);
    if (!db) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "io.github.cboxdoerfer.FSearch.Query.Error.NoDatabase",
            "Database not ready");
        return;
    }

    const char *search_term = "";
    int limit = 100;
    bool use_regex = false;
    bool match_case = false;
    bool search_in_path = false;
    bool files_only = false;
    bool folders_only = false;
    const char *sort_val = "name";
    bool sort_desc = false;
    bool pretty = false;

    g_variant_get(parameters,
                  "(&sibbbbb&sbb)",
                  &search_term, &limit,
                  &use_regex, &match_case, &search_in_path,
                  &files_only, &folders_only,
                  &sort_val, &sort_desc, &pretty);
    (void)pretty; // server always returns JSON

    // Map sort string to enum
    FsearchDatabaseIndexProperty sort_prop = DATABASE_INDEX_PROPERTY_NAME;
    if (g_strcmp0(sort_val, "path") == 0)
        sort_prop = DATABASE_INDEX_PROPERTY_PATH;
    else if (g_strcmp0(sort_val, "size") == 0)
        sort_prop = DATABASE_INDEX_PROPERTY_SIZE;
    else if (g_strcmp0(sort_val, "mtime") == 0)
        sort_prop = DATABASE_INDEX_PROPERTY_MODIFICATION_TIME;
    else if (g_strcmp0(sort_val, "extension") == 0)
        sort_prop = DATABASE_INDEX_PROPERTY_EXTENSION;

    char *json = NULL;
    int ret = do_search(db, search_term, limit,
                        use_regex, match_case, search_in_path,
                        files_only, folders_only,
                        sort_prop, sort_desc, &json);

    if (ret != 0 || !json) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "io.github.cboxdoerfer.FSearch.Query.Error.SearchFailed",
            "Search failed");
        g_free(json);
        return;
    }

    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(s)", json));
    g_free(json);
}

static const GDBusInterfaceVTable query_vtable = {
    .method_call = query_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

void
fsearch_query_cli_dbus_register(void) {
    GError *error = NULL;
    g_autoptr(GDBusConnection) conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!conn) {
        g_debug("[query-dbus] failed to connect to session bus: %s", error->message);
        g_clear_error(&error);
        return;
    }

    g_debug("[query-dbus] connected, registering...");

    if (!query_iface_info) {
        query_node_info = g_dbus_node_info_new_for_xml(kQueryInterfaceXml, &error);
        if (!query_node_info) {
            g_warning("[query-dbus] failed to parse interface XML: %s", error->message);
            g_clear_error(&error);
            return;
        }
        // Steal the first interface from the node (node is kept alive statically)
        query_iface_info = query_node_info->interfaces ? query_node_info->interfaces[0] : NULL;
        if (!query_iface_info) {
            g_warning("[query-dbus] no interface in parsed XML");
            return;
        }
    }

    guint reg_id = g_dbus_connection_register_object(conn,
                                                     FSEARCH_QUERY_DBUS_OBJECT,
                                                     query_iface_info,
                                                     &query_vtable,
                                                     NULL,   // user_data
                                                     NULL,   // user_data_free_func
                                                     &error);
    if (reg_id == 0) {
        g_warning("[query-dbus] failed to register object: %s", error->message);
        g_clear_error(&error);
        return;
    }

    g_debug("[query-dbus] registered query interface on session bus");
}

/* ═══════════════════════════════════════════
 *  CLIENT SIDE — called from --query CLI
 * ═══════════════════════════════════════════ */

int
fsearch_query_cli_run_via_dbus(const char *search_term,
                               int limit,
                               bool use_regex,
                               bool match_case,
                               bool search_in_path,
                               bool files_only,
                               bool folders_only,
                               const char *sort_val,
                               bool sort_desc,
                               bool pretty)
{
    // Try to find a running FSearch instance on the session bus
    g_autoptr(GDBusConnection) conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!conn) return -1;

    // Check if the bus owner exists
    g_autoptr(GDBusProxy) proxy = g_dbus_proxy_new_sync(conn,
                                                         G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                                         NULL,
                                                         "io.github.cboxdoerfer.FSearch",
                                                         FSEARCH_QUERY_DBUS_OBJECT,
                                                         FSEARCH_QUERY_DBUS_INTERFACE,
                                                         NULL,
                                                         NULL);
    if (!proxy) {
        g_debug("[query-dbus] no running instance found");
        return -1;
    }

    // Build parameters: (sibbbbb&sbb)
    GVariant *params = g_variant_new("(sibbbbb&sbb)",
                                     search_term ? search_term : "",
                                     limit,
                                     use_regex,
                                     match_case,
                                     search_in_path,
                                     files_only,
                                     folders_only,
                                     sort_val ? sort_val : "name",
                                     sort_desc,
                                     pretty);

    GError *error = NULL;
    g_autoptr(GVariant) result = g_dbus_proxy_call_sync(proxy,
                                                         "Search",
                                                         params,
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         FSEARCH_QUERY_DBUS_TIMEOUT,
                                                         NULL,
                                                         &error);
    if (!result) {
        g_debug("[query-dbus] call failed: %s", error->message);
        g_clear_error(&error);
        return -1;
    }

    const char *json_str = NULL;
    g_variant_get(result, "(&s)", &json_str);

    if (json_str) {
        printf("%s", json_str);
        return 0;
    }

    return -1;
}
