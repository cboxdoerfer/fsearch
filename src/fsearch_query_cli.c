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

#define G_LOG_DOMAIN "fsearch-query-cli"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fsearch_query_cli.h"
#include "fsearch_config.h"
#include "fsearch_query.h"
#include "fsearch_query_flags.h"
#include "fsearch_database.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_search_view.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_entry_info.h"
#include "fsearch_database_search_info.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
print_json_string(FILE *out, const char *str) {
    if (!str) {
        fprintf(out, "null");
        return;
    }
    fputc('"', out);
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if ((unsigned char)*p < 0x20) {
                fprintf(out, "\\u%04x", (unsigned char)*p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
    fputc('"', out);
}

static const char *
size_to_human(int64_t bytes) {
    static char buf[32];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%" PRId64 " B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KiB", bytes / 1024.0);
    } else if (bytes < 1024LL * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MiB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.2f GiB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

static FsearchQueryFlags
get_query_flags(bool use_regex, bool match_case, bool search_in_path, bool files_only, bool folders_only) {
    FsearchQueryFlags flags = 0;
    if (use_regex) {
        flags |= QUERY_FLAG_REGEX;
    }
    if (match_case) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }
    if (search_in_path) {
        flags |= QUERY_FLAG_SEARCH_IN_PATH;
    }
    if (files_only) {
        flags |= QUERY_FLAG_FILES_ONLY;
    } else if (folders_only) {
        flags |= QUERY_FLAG_FOLDERS_ONLY;
    }
    return flags;
}

static FsearchDatabaseIndexProperty
get_sort_order(FsearchQueryCliSortProperty sort_prop) {
    switch (sort_prop) {
    case FSEARCH_QUERY_CLI_SORT_NAME:
        return DATABASE_INDEX_PROPERTY_NAME;
    case FSEARCH_QUERY_CLI_SORT_PATH:
        return DATABASE_INDEX_PROPERTY_PATH;
    case FSEARCH_QUERY_CLI_SORT_SIZE:
        return DATABASE_INDEX_PROPERTY_SIZE;
    case FSEARCH_QUERY_CLI_SORT_MTIME:
        return DATABASE_INDEX_PROPERTY_MODIFICATION_TIME;
    case FSEARCH_QUERY_CLI_SORT_EXTENSION:
        return DATABASE_INDEX_PROPERTY_EXTENSION;
    default:
        return DATABASE_INDEX_PROPERTY_NAME;
    }
}

static void
output_results_json(FILE *out,
                    const char *search_term,
                    int limit,
                    int total_results,
                    FsearchDatabaseIndexStore *store,
                    FsearchDatabaseSearchView *view,
                    uint32_t num_files,
                    uint32_t num_folders) {
    fprintf(out, "{\n");
    fprintf(out, "  \"tool\": \"fsearch\",\n");
    fprintf(out, "  \"version\": \"0.3\",\n");
    fprintf(out, "  \"query\": {\n");
    fprintf(out, "    \"term\": ");
    print_json_string(out, search_term);
    fprintf(out, ",\n");
    fprintf(out, "    \"limit\": %d,\n", limit);
    fprintf(out, "    \"total_results\": %d\n", total_results);
    fprintf(out, "  },\n");
    fprintf(out, "  \"results\": [\n");

    int count = 0;
    int max_results = (limit > 0) ? MIN(limit, total_results) : total_results;

    for (int i = 0; i < max_results; i++) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) {
            break;
        }

        const char *name = db_entry_get_name_raw(entry);
        const char *path = db_entry_get_path_full(entry)->str;
        int64_t size = db_entry_get_size(entry);
        time_t mtime = db_entry_get_mtime(entry);
        const char *ext = strrchr(name ? name : "", '.');
        ext = ext ? ext + 1 : "";
        bool is_dir = db_entry_is_folder(entry);

        if (count > 0) {
            fprintf(out, ",\n");
        }
        fprintf(out, "    {\n");
        fprintf(out, "      \"name\": ");
        print_json_string(out, name);
        fprintf(out, ",\n");
        fprintf(out, "      \"path\": ");
        print_json_string(out, path);
        fprintf(out, ",\n");
        fprintf(out, "      \"extension\": ");
        print_json_string(out, ext);
        fprintf(out, ",\n");
        fprintf(out, "      \"type\": \"%s\",\n", is_dir ? "folder" : "file");
        fprintf(out, "      \"size\": %" PRId64 ",\n", size);
        fprintf(out, "      \"size_human\": ");
        print_json_string(out, size_to_human(size));
        fprintf(out, ",\n");
        fprintf(out, "      \"modified\": ");
        char timebuf[64];
        struct tm *tm = localtime(&mtime);
        if (tm && strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S%z", tm)) {
            print_json_string(out, timebuf);
        } else {
            fprintf(out, "null");
        }
        fprintf(out, ",\n");
        fprintf(out, "      \"mtime\": %ld\n", (long)mtime);
        fprintf(out, "    }");

        count++;
    }

    fprintf(out, "\n  ],\n");
    fprintf(out, "  \"stats\": {\n");
    fprintf(out, "    \"db_files\": %u,\n", num_files);
    fprintf(out, "    \"db_folders\": %u,\n", num_folders);
    fprintf(out, "    \"results_shown\": %d\n", count);
    fprintf(out, "  }\n");
    fprintf(out, "}\n");
}

static void
output_results_pretty(FILE *out,
                      const char *search_term,
                      int limit,
                      int total_results,
                      FsearchDatabaseIndexStore *store,
                      FsearchDatabaseSearchView *view) {
    fprintf(out, "Search term: %s\n", search_term ? search_term : "");
    fprintf(out, "Total results: %d\n\n", total_results);

    int max_results = (limit > 0) ? MIN(limit, total_results) : total_results;

    // Header
    fprintf(out, "%-4s %-40s %-10s %-12s  %s\n", " #", "Name", "Type", "Size", "Path");
    fprintf(out, "%-4s %-40s %-10s %-12s  %s\n",
            "----", "----------------------------------------", "----------", "------------",
            "-------------------------------------------------------------------");

    for (int i = 0; i < max_results; i++) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) {
            break;
        }

        const char *name = db_entry_get_name_raw(entry);
        const char *path = db_entry_get_path_full(entry)->str;
        int64_t size = db_entry_get_size(entry);
        bool is_dir = db_entry_is_folder(entry);

        // Truncate name to 40 chars
        char name_buf[41];
        if (name) {
            g_strlcpy(name_buf, name, sizeof(name_buf));
        } else {
            g_strlcpy(name_buf, "(null)", sizeof(name_buf));
        }

        fprintf(out, "%-4d %-40s %-10s %12s  %s\n",
                i + 1,
                name_buf,
                is_dir ? "folder" : "file",
                size_to_human(size),
                path ? path : "");
    }
    fputc('\n', out);
}

int
fsearch_query_cli_run(const char *search_term,
                      int limit,
                      bool use_regex,
                      bool match_case,
                      bool search_in_path,
                      bool files_only,
                      bool folders_only,
                      FsearchQueryCliSortProperty sort_prop,
                      bool sort_desc,
                      FsearchQueryCliOutputFormat output_format) {

    if (!search_term || strlen(search_term) == 0) {
        fprintf(stderr, "{\"error\": \"empty search term\"}\n");
        return FSEARCH_QUERY_CLI_ERROR_EMPTY_QUERY;
    }

    // Load config
    FsearchConfig *config = calloc(1, sizeof(FsearchConfig));
    if (!config) {
        fprintf(stderr, "{\"error\": \"failed to allocate config\"}\n");
        return FSEARCH_QUERY_CLI_ERROR_DB_LOAD;
    }

    if (!config_load(config)) {
        fprintf(stderr, "{\"error\": \"failed to load config\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_DB_LOAD;
    }

    // Build database file path
    g_autofree char *db_file_path = g_build_filename(g_get_user_data_dir(), "fsearch", "fsearch.db", NULL);
    g_autoptr(GFile) db_file = g_file_new_for_path(db_file_path);

    if (!g_file_query_exists(db_file, NULL)) {
        fprintf(stderr, "{\"error\": \"database not found\", \"path\": ");
        print_json_string(stderr, db_file_path);
        fprintf(stderr, "}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_DB_NOT_FOUND;
    }

    // Create and load database
    g_autoptr(FsearchDatabase) db = fsearch_database_new(g_steal_pointer(&db_file),
                                                         config->includes,
                                                         config->excludes);
    FsearchResult load_result = fsearch_database_rescan_blocking(db);

    if (load_result != FSEARCH_RESULT_SUCCESS) {
        fprintf(stderr, "{\"error\": \"failed to load database\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_DB_LOAD;
    }

    // Get the index store
    FsearchDatabaseIndexStore *store = fsearch_database_get_store(db);
    if (!store) {
        fprintf(stderr, "{\"error\": \"failed to get index store\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_DB_LOAD;
    }

    uint32_t num_files = fsearch_database_index_store_get_num_files(store);
    uint32_t num_folders = fsearch_database_index_store_get_num_folders(store);

    if (num_files + num_folders == 0) {
        fprintf(stderr, "{\"error\": \"database is empty, run fsearch --update-database first\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_DB_EMPTY;
    }

    // Create query
    FsearchQueryFlags flags = get_query_flags(use_regex, match_case, search_in_path, files_only, folders_only);
    g_autoptr(FsearchQuery) query = fsearch_query_new(search_term, NULL, NULL, flags, "cli");

    if (!query) {
        fprintf(stderr, "{\"error\": \"failed to create query\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_SEARCH;
    }

    // Execute search
    const uint32_t view_id = 0;
    FsearchDatabaseIndexProperty sort_order = get_sort_order(sort_prop);
    GtkSortType sort_type = sort_desc ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    g_autoptr(GCancellable) cancellable = g_cancellable_new();

    bool search_ok = fsearch_database_index_store_search(store, view_id, query, sort_order, sort_type, cancellable);

    if (!search_ok) {
        fprintf(stderr, "{\"error\": \"search failed\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_SEARCH;
    }

    // Get results
    FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, view_id);
    if (!view) {
        fprintf(stderr, "{\"error\": \"failed to get search results\"}\n");
        config_free(config);
        return FSEARCH_QUERY_CLI_ERROR_SEARCH;
    }

    FsearchDatabaseSearchInfo *info = fsearch_database_index_store_get_search_info(store, view_id);
    int total_results = info ? (int)fsearch_database_search_info_get_num_entries(info) : 0;

    // Output results
    if (output_format == FSEARCH_QUERY_CLI_OUTPUT_JSON) {
        output_results_json(stdout, search_term, limit, total_results, store, view, num_files, num_folders);
    } else {
        output_results_pretty(stdout, search_term, limit, total_results, store, view);
    }

    // Cleanup
    config_free(config);
    return FSEARCH_QUERY_CLI_SUCCESS;
}
