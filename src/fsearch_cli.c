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
#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_search.h"
#include "fsearch_query.h"

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
print_entries(DynamicArray *entries) {
    if (!entries) {
        return;
    }
    const uint32_t num_entries = darray_get_num_items(entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
        FsearchDatabaseEntry *entry = darray_get_item(entries, i);
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
    FsearchDatabase *db = db_new(NULL, NULL, NULL, false);
    if (!db_load(db, db_path, NULL)) {
        g_printerr("[fsearch] failed to load database from '%s'\n"
                   "[fsearch] run 'fsearch --update-database' to create it\n",
                   db_path);
        g_clear_pointer(&db, db_unref);
        g_clear_pointer(&config, config_free);
        return EXIT_FAILURE;
    }

    FsearchQuery *query = fsearch_query_new(search_term, NULL, config->filters, get_query_flags(config), "cli");

    DynamicArray *folders = db_get_folders(db);
    DynamicArray *files = db_get_files(db);

    DatabaseSearchResult *result = NULL;
    if (fsearch_query_matches_everything(query)) {
        result = db_search_empty(folders, files, DATABASE_INDEX_TYPE_NAME);
    }
    else {
        result = db_search(query, db_get_thread_pool(db), folders, files, DATABASE_INDEX_TYPE_NAME, NULL);
    }

    if (result) {
        print_entries(result->folders);
        print_entries(result->files);
        g_clear_pointer(&result->folders, darray_unref);
        g_clear_pointer(&result->files, darray_unref);
        g_clear_pointer(&result, free);
    }

    g_clear_pointer(&folders, darray_unref);
    g_clear_pointer(&files, darray_unref);
    g_clear_pointer(&query, fsearch_query_unref);
    g_clear_pointer(&db, db_unref);
    g_clear_pointer(&config, config_free);

    return EXIT_SUCCESS;
}
