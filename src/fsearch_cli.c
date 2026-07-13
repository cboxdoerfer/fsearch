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

#include "fsearch_cli.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "fsearch.h"
#include "fsearch_config.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_file.h"
#include "fsearch_database_index_store.h"
#include "fsearch_database_search_info.h"
#include "fsearch_database_search_view.h"
#include "fsearch_query.h"

#define FSEARCH_CLI_VIEW_ID 0

static FsearchQueryFlags
get_query_flags(FsearchConfig *config) {
    FsearchQueryFlags flags = 0;
    if (config->match_case) {
        flags |= QUERY_FLAG_MATCH_CASE;
    }
    if (config->auto_match_case) {
        flags |= QUERY_FLAG_AUTO_MATCH_CASE;
    }
    if (config->enable_regex) {
        flags |= QUERY_FLAG_REGEX;
    }
    if (config->search_in_path) {
        flags |= QUERY_FLAG_SEARCH_IN_PATH;
    }
    if (config->auto_search_in_path) {
        flags |= QUERY_FLAG_AUTO_SEARCH_IN_PATH;
    }
    return flags;
}

static void
index_store_event_cb(FsearchDatabaseIndexStore *store,
                     FsearchDatabaseIndexStoreEventKind kind,
                     gpointer data,
                     gpointer user_data) {
    // The CLI performs a single synchronous search; store events don't matter here.
}

static void
print_view_entries(FsearchDatabaseSearchView *view) {
    g_autoptr(FsearchDatabaseSearchInfo) info = fsearch_database_search_view_get_info(view);
    if (!info) {
        return;
    }
    const uint32_t num_entries = fsearch_database_search_info_get_num_entries(info);
    for (uint32_t i = 0; i < num_entries; ++i) {
        FsearchDatabaseEntry *entry = fsearch_database_search_view_get_entry_for_idx(view, i);
        if (!entry) {
            continue;
        }
        GString *path = db_entry_get_path_full(entry);
        if (path) {
            printf("%s\n", path->str);
            g_string_free(path, TRUE);
        }
    }
}

int
fsearch_cli_search(const char *search_term) {
    FsearchConfig *config = calloc(1, sizeof(FsearchConfig));
    g_assert(config);

    if (!config_load(config)) {
        if (!config_load_default(config)) {
            g_printerr("[fsearch] failed to load config\n");
            g_clear_pointer(&config, config_free);
            return EXIT_FAILURE;
        }
    }

    g_autofree char *db_path = fsearch_application_get_database_file_path();
    g_autoptr(FsearchDatabaseIndexStore) store = NULL;
    if (!fsearch_database_file_load(db_path, NULL, &store, config->includes, config->excludes, index_store_event_cb, NULL)
        || !store) {
        g_printerr("[fsearch] failed to load database from '%s'\n"
                   "[fsearch] run 'fsearch --update-database' to create it\n",
                   db_path);
        g_clear_pointer(&config, config_free);
        return EXIT_FAILURE;
    }

    g_autoptr(FsearchQuery) query =
        fsearch_query_new(search_term, NULL, config->filters, get_query_flags(config), "cli");

    int res = EXIT_FAILURE;
    if (fsearch_database_index_store_search(store,
                                            FSEARCH_CLI_VIEW_ID,
                                            query,
                                            DATABASE_INDEX_PROPERTY_NAME,
                                            GTK_SORT_ASCENDING,
                                            NULL)) {
        FsearchDatabaseSearchView *view = fsearch_database_index_store_get_search_view(store, FSEARCH_CLI_VIEW_ID);
        if (view) {
            print_view_entries(view);
            res = EXIT_SUCCESS;
        }
    }

    g_clear_pointer(&config, config_free);

    return res;
}
