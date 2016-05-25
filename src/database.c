/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <err.h>
#include <glib/gstdio.h>

#include "database.h"
#include "database_node.h"
#include "config.h"

#define WS_NONE		0
#define WS_RECURSIVE	(1 << 0)
#define WS_DEFAULT	WS_RECURSIVE
#define WS_FOLLOWLINK	(1 << 1)	/* follow symlinks */
#define WS_DOTFILES	(1 << 2)	/* per unix convention, .file is hidden */

struct _Database
{
    GList *locations;
    GList *searches;
    DynamicArray *entries;
    GPtrArray *filtered_entries;
    uint32_t num_entries;

    GMutex mutex;
    bool busy;
};

struct _DatabaseSearch
{
    GPtrArray *results;

    gchar *query;
    uint32_t search_mode;
    uint32_t search_in_path;
};

typedef GNode DatabaseItem;

struct _DatabaseLocation
{
    // B+ tree of entry nodes
    GNode *entries;
    uint32_t num_items;
};

enum {
    WALK_OK = 0,
    WALK_BADPATTERN,
    WALK_NAMETOOLONG,
    WALK_BADIO,
};

// Forward declarations
static void
db_clear_full (Database *db);

static DatabaseLocation *
db_location_get_for_path (Database *db, const char *path);

static DatabaseLocation *
db_location_build_tree (const char *dname, int spec);

static DatabaseLocation *
db_location_new (void);

static void
db_list_add_location (Database *db, DatabaseLocation *location);

static bool
db_has_location (Database *db, const char *path);

// Implemenation
DatabaseLocation *
db_location_load_from_file (const char *fname)
{
    g_assert (fname != NULL);

    FILE *fp = fopen (fname, "rb");
    if (!fp) {
        return NULL;
    }

    GNode *root = NULL;

    char magic[4];
    if (fread (magic, 1, 4, fp) != 4) {
        printf ("failed to read magic\n");
        goto load_fail;
    }
    if (strncmp (magic, "FSDB", 4)) {
        printf ("bad signature\n");
        goto load_fail;
    }

    uint8_t majorver = 0;
    if (fread (&majorver, 1, 1, fp) != 1) {
        goto load_fail;
    }
    if (majorver != 0) {
        printf ("bad majorver=%d\n", majorver);
        goto load_fail;
    }

    uint8_t minorver = 0;
    if (fread (&minorver, 1, 1, fp) != 1) {
        goto load_fail;
    }
    if (minorver != 1) {
        printf ("bad minorver=%d\n", minorver);
        goto load_fail;
    }
    printf ("database version=%d.%d\n", majorver, minorver);

    uint32_t num_items = 0;
    if (fread (&num_items, 1, 4, fp) != 4) {
        goto load_fail;
    }

    uint32_t num_items_read = 0;
    GNode *prev = NULL;
    while (true) {
        uint16_t name_len = 0;
        if (fread (&name_len, 1, 2, fp) != 2) {
            printf("failed to read name length\n");
            goto load_fail;
        }

        if (name_len == 0) {
            // reached end of child marker
            if (!prev) {
                goto load_fail;
            }
            prev = prev->parent;
            if (!prev) {
                // prev was root node, we're done
                printf("reached root node. done\n");
                break;
            }
            continue;
        }

        // read name
        char name[name_len + 1];
        if (fread (&name, 1, name_len, fp) != name_len) {
            printf("failed to read name\n");
            goto load_fail;
        }
        name[name_len] = '\0';

        // read is_dir
        uint8_t is_dir = 0;
        if (fread (&is_dir, 1, 1, fp) != 1) {
            printf("failed to read is_dir\n");
            goto load_fail;
        }

        // read size
        uint64_t size = 0;
        if (fread (&size, 1, 8, fp) != 8) {
            printf("failed to read size\n");
            goto load_fail;
        }

        // read mtime
        uint64_t mtime = 0;
        if (fread (&mtime, 1, 8, fp) != 8) {
            printf("failed to read mtime\n");
            goto load_fail;
        }

        // read mtime
        uint32_t pos = 0;
        if (fread (&pos, 1, 4, fp) != 4) {
            printf("failed to read sort position\n");
            goto load_fail;
        }

        GNode *new = db_node_new (name, size, mtime, is_dir, pos);
        if (!prev) {
            prev = new;
            root = new;
            continue;
        }
        prev = g_node_prepend (prev, new);
        num_items_read++;
    }
    printf("read database: %d/%d\n", num_items_read, num_items);

    DatabaseLocation *location = db_location_new ();
    location->entries = root;

    return location;

load_fail:
    fprintf (stderr, "database load fail (%s)!\n", fname);
    if (fp) {
        fclose (fp);
    }
    if (root) {
        db_node_free_tree (root);
    }
    return NULL;
}

bool
db_location_write_to_file (DatabaseLocation *location, const char *path)
{
    g_assert (path != NULL);
    g_assert (location != NULL);

    if (!location->entries) {
        return false;
    }
    g_mkdir_with_parents (path, 0700);

    gchar tempfile[PATH_MAX] = "";
    snprintf (tempfile, sizeof (tempfile), "%s/database.db", path);

    FILE *fp = fopen (tempfile, "w+b");
    if (!fp) {
        return false;
    }

    const char magic[] = "FSDB";
    if (fwrite (magic, 1, 4, fp) != 4 ) {
        goto save_fail;
    }

    const uint8_t majorver = 0;
    if (fwrite (&majorver, 1, 1, fp) != 1) {
        goto save_fail;
    }

    const uint8_t minorver = 1;
    if (fwrite (&minorver, 1, 1, fp) != 1) {
        goto save_fail;
    }

    uint32_t num_items = g_node_n_nodes (location->entries, G_TRAVERSE_ALL);
    if (fwrite (&num_items, 1, 4, fp) != 4) {
        goto save_fail;
    }

    const uint16_t del = 0;

    GNode *root = location->entries;
    GNode *node = root;
    while (node) {
        const char *name = db_node_get_name (node);
        uint16_t len = strlen (name);
        if (len) {
            // write length of node name
            if (fwrite (&len, 1, 2, fp) != 2) {
                goto save_fail;
            }
            // write node name
            if (fwrite (name, 1, len, fp) != len) {
                goto save_fail;
            }
            // write node name
            uint8_t is_dir = db_node_is_dir (node);
            if (fwrite (&is_dir, 1, 1, fp) != 1) {
                goto save_fail;
            }

            // write node name
            uint64_t size = db_node_get_size (node);
            if (fwrite (&size, 1, 8, fp) != 8) {
                goto save_fail;
            }

            // write node name
            uint64_t mtime = db_node_get_mtime (node);
            if (fwrite (&mtime, 1, 8, fp) != 8) {
                goto save_fail;
            }

            // write node name
            uint32_t pos = db_node_get_pos (node);
            if (fwrite (&pos, 1, 4, fp) != 4) {
                goto save_fail;
            }

            GNode *temp = g_node_first_child (node);
            if (!temp) {
                // reached end of children, write delimiter
                if (fwrite (&del, 1, 2, fp) != 2) {
                    goto save_fail;
                }
                GNode *current = node;
                while (true) {
                    temp = g_node_next_sibling (current);
                    if (temp) {
                        // found next, sibling add that
                        node = temp;
                        break;
                    }

                    if (fwrite (&del, 1, 2, fp) != 2) {
                        goto save_fail;
                    }
                    temp = current->parent;
                    if (!temp) {
                        // reached last node, abort
                        node = NULL;
                        break;
                    }
                    else {
                        current = temp;
                    }
                }
            }
            else {
                node = temp;
            }
        }
        else {
            goto save_fail;
        }

    }

    fclose (fp);
    return true;

save_fail:

    fclose (fp);
    unlink (tempfile);
    return false;
}

int
db_location_walk_tree_recursive (DatabaseLocation *location, const char *dname, GNode *parent, int spec)
{
    int res = WALK_OK;
    int len = strlen (dname);
    if (len >= FILENAME_MAX - 1)
        return WALK_NAMETOOLONG;

    char fn[FILENAME_MAX];
    strcpy (fn, dname);
    fn[len++] = '/';

    DIR *dir = NULL;
    if (!(dir = opendir (dname))) {
        //warn("can't open %s", dname);
        return WALK_BADIO;
    }

    struct stat st;
    struct dirent *dent = NULL;
    errno = 0;
    while ((dent = readdir (dir))) {
        //if (!(spec & WS_DOTFILES) && dent->d_name[0] == '.') {
        //    // file is dotfile, skip
        //    continue;
        //}
        if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, "..")) {
            continue;
        }

        strncpy (fn + len, dent->d_name, FILENAME_MAX - len);
        if (lstat (fn, &st) == -1) {
            //warn("Can't stat %s", fn);
            res = WALK_BADIO;
            continue;
        }

        /* don't follow symlink unless told so */
        if (S_ISLNK (st.st_mode) && !(spec & WS_FOLLOWLINK)) {
            continue;
        }

        /* will be false for symlinked dirs */

        const bool is_dir = S_ISDIR (st.st_mode);
        GNode *node = db_node_new (dent->d_name,
                                   st.st_size,
                                   st.st_mtime,
                                   is_dir,
                                   0);
        db_node_append (parent, node);
        location->num_items++;
        if (is_dir) {
            /* recursively follow dirs */
            if ((spec & WS_RECURSIVE))
                db_location_walk_tree_recursive (location, fn, node, spec);

        }
    }

    if (dir) {
        closedir (dir);
    }
    return res ? res : errno ? WALK_BADIO : WALK_OK;
}

static DatabaseLocation *
db_location_build_tree (const char *dname, int spec)
{
    GNode *root = db_node_new (dname, 0, 0, true, 0);
    DatabaseLocation *location = db_location_new ();
    location->entries = root;
    uint32_t res = db_location_walk_tree_recursive (location, dname, root, spec);
    if (res == WALK_OK) {
        return location;
    }
    else {
        db_location_free (location);
    }
    return NULL;
}

static DatabaseLocation *
db_location_new (void)
{
    DatabaseLocation *location = g_new0 (DatabaseLocation, 1);
    return location;
}

gboolean
db_list_insert_node (GNode *node, gpointer data)
{
    Database *db = data;
    uint32_t pos = db_node_get_pos (node);
    darray_set_item (db->entries, node, pos);
    db->num_entries++;
    return FALSE;
}

static void
db_traverse_tree_insert (GNode *node, gpointer user_data)
{
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1, db_list_insert_node, user_data);
}

static uint32_t temp_index = 0;

gboolean
db_list_add_node (GNode *node, gpointer data)
{
    Database *db = data;
    darray_set_item (db->entries, node, temp_index++);
    db->num_entries++;
    return FALSE;
}

static void
db_traverse_tree_add (GNode *node, gpointer user_data)
{
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_ALL, -1, db_list_add_node, user_data);
}

static void
db_list_insert_location (Database *db, DatabaseLocation *location)
{
    g_assert (db != NULL);
    g_assert (location != NULL);
    g_assert (location->entries != NULL);

    g_node_children_foreach (location->entries,
                             G_TRAVERSE_ALL,
                             db_traverse_tree_insert,
                             db);
}


static void
db_list_add_location (Database *db, DatabaseLocation *location)
{
    g_assert (db != NULL);
    g_assert (location != NULL);
    g_assert (location->entries != NULL);

    g_node_children_foreach (location->entries,
                             G_TRAVERSE_ALL,
                             db_traverse_tree_add,
                             db);
}

static DatabaseLocation *
db_location_get_for_path (Database *db, const char *path)
{
    g_assert (db != NULL);
    g_assert (path != NULL);

    GList *locations = db->locations;
    for (GList *l = locations; l != NULL; l = l->next) {
        DatabaseLocation *location = (DatabaseLocation *)l->data;
        const char *location_path = db_node_get_root_path (location->entries);
        if (!strcmp (location_path, path)) {
            return location;
        }
    }
    return NULL;
}

static bool
db_has_location (Database *db, const char *path)
{
    g_assert (db != NULL);
    g_assert (path != NULL);

    GList *locations = db->locations;
    for (GList *l = locations; l != NULL; l = l->next) {
        DatabaseLocation *location = (DatabaseLocation *)l->data;
        const char *location_path = db_node_get_root_path (location->entries);
        if (!strcmp (location_path, path)) {
            return true;
        }
    }
    return false;
}

void
db_location_free (DatabaseLocation *location)
{
    g_assert (location != NULL);

    if (location->entries) {
        db_node_free_tree (location->entries);
    }
    g_free (location);
    location = NULL;
}

bool
db_location_remove (Database *db, const char *path)
{
    g_assert (db != NULL);
    g_assert (path != NULL);

    DatabaseLocation *location = db_location_get_for_path (db, path);
    if (location) {
        db->locations = g_list_remove (db->locations, location);
        db_location_free (location);
        db_sort (db);
    }

    return true;
}

static void
build_location_path (char *path, size_t path_len, const char *location_name)
{
    g_assert (path != NULL);
    g_assert (location_name != NULL);

    gchar *path_checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA256,
                                                          location_name,
                                                          -1);

    g_assert (path_checksum != NULL);

    gchar config_dir[PATH_MAX] = "";
    build_config_dir (config_dir, sizeof (config_dir));

    snprintf (path,
              path_len,
              "%s/database/%s",
              config_dir,
              path_checksum);
    g_free (path_checksum);
    return;
}

void
db_location_delete (DatabaseLocation *location, const char *location_name)
{
    g_assert (location != NULL);
    g_assert (location_name != NULL);

    gchar database_path[PATH_MAX] = "";
    build_location_path (database_path,
                         sizeof (database_path),
                         location_name);

    gchar database_file_path[PATH_MAX] = "";
    snprintf (database_file_path,
              sizeof (database_file_path),
              "%s/%s", database_path, "database.db");

    g_remove (database_file_path);
    g_remove (database_path);
}

bool
db_save_location (Database *db, const char *location_name)
{
    g_assert (db != NULL);

//    if (db_has_location (db, location_name)) {
//        printf("save location: location not found: %s\n", location_name);
//        return false;
//    }
//
    gchar database_path[PATH_MAX] = "";
    build_location_path (database_path,
                         sizeof (database_path),
                         location_name);
    printf("%s\n", database_path);

    gchar database_fname[PATH_MAX] = "";
    snprintf (database_fname,
              sizeof (database_fname),
              "%s/database.db", database_path);
    DatabaseLocation *location = db_location_get_for_path (db, location_name);
    if (location) {
        db_location_write_to_file (location, database_path);
    }

    return true;
}

bool
db_save_locations (Database *db)
{
    g_assert (db != NULL);
    g_return_val_if_fail (db->locations != NULL, false);

    //db_update_sort_index (db);
    GList *locations = db->locations;
    for (GList *l = locations; l != NULL; l = l->next) {
        DatabaseLocation *location = (DatabaseLocation *)l->data;
        const char *location_path = db_node_get_root_path (location->entries);
        db_save_location (db, location_path);
    }
    return true;
}

static gchar *
db_location_get_path (const char *location_name)
{
    gchar database_path[PATH_MAX] = "";
    build_location_path (database_path,
                         sizeof (database_path),
                         location_name);
    printf("%s\n", database_path);

    gchar database_fname[PATH_MAX] = "";
    snprintf (database_fname,
              sizeof (database_fname),
              "%s/database.db", database_path);

    return g_strdup (database_fname);
}

bool
db_location_load (Database *db, const char *location_name)
{
    db_lock (db);
    db->busy = true;
    gchar *load_path = db_location_get_path (location_name);
    if (!load_path) {
        db->busy = false;
        db_unlock (db);
        return false;
    }
    DatabaseLocation *location = db_location_load_from_file (load_path);
    g_free (load_path);
    load_path = NULL;

    if (location) {
        location->num_items = g_node_n_nodes (location->entries, G_TRAVERSE_ALL);
        db->locations = g_list_append (db->locations, location);
        db->num_entries += location->num_items;
        db->busy = false;
        db_unlock (db);
        return true;
    }
    db->busy = false;
    db_unlock (db);
    return false;
}

bool
db_location_build_new (Database *db, const char *location_name)
{
    g_assert (db != NULL);
    db_lock (db);
    printf("load location: %s\n", location_name);

    DatabaseLocation *location = db_location_build_tree (location_name, WS_DEFAULT);

    if (location) {
        printf("loation num entries: %d\n", location->num_items);
        db->locations = g_list_append (db->locations, location);
        db->num_entries += location->num_items;
        db_unlock (db);
        return true;
    }

    db_unlock (db);
    return false;
}

void
db_update_sort_index (Database *db)
{
    g_assert (db != NULL);
    g_assert (db->entries != NULL);

    for (uint32_t i = 0; i < db->num_entries; ++i) {
        GNode *node = darray_get_item (db->entries, i);
        db_node_set_pos (node, i);
    }
}

static uint32_t
db_locations_get_num_entries (Database *db)
{
    g_assert (db != NULL);
    g_assert (db->locations != NULL);

    uint32_t num_entries = 0;
    GList *locations = db->locations;
    for (GList *l = locations; l != NULL; l = l->next) {
        DatabaseLocation *location = l->data;
        num_entries += location->num_items;
    }
    return num_entries;
}

void
db_build_initial_entries_list (Database *db)
{
    g_assert (db != NULL);
    g_assert (db->num_entries >= 0);

    db_lock (db);
    db_clear_full (db);
    uint32_t num_entries = db_locations_get_num_entries (db);
    printf("update list: %d\n", num_entries);
    db->entries = darray_new (num_entries);

    GList *locations = db->locations;
    temp_index = 0;
    for (GList *l = locations; l != NULL; l = l->next) {
        db_list_add_location (db, l->data);
    }
    db_sort (db);
    db_update_sort_index (db);
    db_unlock (db);
}

void
db_update_entries_list (Database *db)
{
    g_assert (db != NULL);
    g_assert (db->num_entries >= 0);

    db_lock (db);
    db_clear_full (db);
    uint32_t num_entries = db_locations_get_num_entries (db);
    printf("update list: %d\n", num_entries);
    db->entries = darray_new (num_entries);

    GList *locations = db->locations;
    for (GList *l = locations; l != NULL; l = l->next) {
        db_list_insert_location (db, l->data);
    }
    db_unlock (db);
}

GNode *
db_location_get_entries (DatabaseLocation *location)
{
    g_assert (location != NULL);
    return location->entries;
}
// Forward declarations
//bool
//db_remove_node (Database *db, GNode *node)
//{
//    g_assert (db != NULL);
//    g_assert (node != NULL);
//
//    bool res = g_ptr_array_remove (db->entries, node);
//    if (res) {
//        // TODO: remove entry from tree
//    }
//    return res;
//}

static void
db_database_new_array (Database *db)
{
    g_assert (db != NULL);
    db->entries = NULL;
    //g_ptr_array_set_free_func (db->entries, (GDestroyNotify)db_entry_free);
}

Database *
db_database_new ()
{
    Database *db = g_new0 (Database, 1);
    db_database_new_array (db);
    g_mutex_init (&db->mutex);
    return db;
}

//uint32_t
//db_save (Database *db, const char *path)
//{
//    FILE *fp = fopen (path, "w");
//    if (G_LIKELY (fp != NULL))
//    {
//        for (uint32_t i = 0; i < db->entries->len; ++i) {
//            DatabaseEntry *record = g_ptr_array_index (db->entries, i);
//            fprintf (fp, "\"%s/%s\" %d %ld %ld\n", record->path, record->name, record->is_dir, record->size, record->mtime);
//        }
//        /* cleanup */
//        fclose (fp);
//        return 1;
//    }
//    return 0;
//}
//
//Database *
//db_load (const char *path)
//{
//    FILE *fp = fopen (path, "r");
//    gchar line[PATH_MAX];
//
//    if (G_LIKELY (fp != NULL)) {
//        Database *db = db_database_new ();
//        while (fgets (line, sizeof (line), fp) != NULL) {
//            gchar path[PATH_MAX] = "";
//            off_t mtime;
//            time_t size;
//            gint is_dir;
//
//            uint32_t res = sscanf (line, "\"%[^\"]\" %d %ld %ld\n", path, &is_dir, &size, &mtime);
//            if (res == 4) {
//                gchar *name = strrchr (path, '/');
//                if (name) {
//                    *name = '\0';
//                    name++;
//                    DatabaseEntry *new = db_entry_new (name, path, size, mtime, is_dir);
//                    db_append_entry (db, new);
//                }
//            }
//        }
//
//        fclose (fp);
//        return db;
//    }
//    return NULL;
//}

static void
db_entries_clear (Database *db)
{
    // free entries
    g_assert (db != NULL);

    if (db->entries) {
        darray_clear (db->entries);
        db->entries = NULL;
    }
}

static void
db_clear_full (Database *db)
{
    // free entries
    g_assert (db != NULL);

    if (db->entries) {
        darray_free (db->entries);
        db->entries = NULL;
    }
    db->num_entries = 0;
}

void
db_free (Database *db)
{
    g_assert (db != NULL);

    db_clear_full (db);
    g_mutex_clear (&db->mutex);
    g_free (db);
    db = NULL;
    return;
}

uint32_t
db_get_num_entries (Database *db)
{
    g_assert (db != NULL);
    return db->num_entries;
}

void
db_unlock (Database *db)
{
    g_assert (db != NULL);
    g_mutex_unlock (&db->mutex);
}

void
db_lock (Database *db)
{
    g_assert (db != NULL);
    g_mutex_lock (&db->mutex);
}

bool
db_try_lock (Database *db)
{
    g_assert (db != NULL);
    return g_mutex_trylock (&db->mutex);
}

DynamicArray *
db_get_entries (Database *db)
{
    g_assert (db != NULL);
    return db->entries;
}

//typedef struct search_regex_context_s {
//    ListModel *list;
//    DatabaseEntry **results;
//    const gchar *query;
//    uint32_t num_results;
//    uint32_t start_pos;
//    uint32_t end_pos;
//} search_regex_context_t;
//
//gpointer
//search_regex_thread (gpointer user_data)
//{
//    search_regex_context_t *ctx = (search_regex_context_t *)user_data;
//
//    GRegexCompileFlags regex_compile_flags = G_REGEX_CASELESS | G_REGEX_OPTIMIZE;
//    GError *error = NULL;
//    GRegex *regex = g_regex_new (ctx->query, regex_compile_flags, 0, &error);
//
//    if (regex) {
//        uint32_t num_results = ctx->num_results;
//        uint32_t start = ctx->start_pos;
//        uint32_t end = ctx->end_pos;
//        for (uint32_t i = start; i <= end; ++i) {
//            DatabaseEntry *record = ctx->list->rows[i];
//            GMatchInfo *match_info;
//            if (g_regex_match (regex, record->name, 0, &match_info)) {
//                ctx->results[num_results] = record;
//                num_results++;
//            }
//            g_match_info_free (match_info);
//        }
//        ctx->num_results = num_results;
//        g_regex_unref (regex);
//    }
//    return NULL;
//}

static int
sort_by_name (const void *a, const void *b)
{
    GNode *node_a = *(GNode **)a;
    GNode *node_b = *(GNode **)b;

    const bool is_dir_a = db_node_is_dir (node_a);
    const bool is_dir_b = db_node_is_dir (node_b);
    if (is_dir_a != is_dir_b) {
        return is_dir_a ? -1 : 1;
    }

    return strverscmp (db_node_get_name (node_a), db_node_get_name (node_b));
}

static int
sort_by_path (const void *a, const void *b)
{
    GNode *node_a = *(GNode **)a;
    GNode *node_b = *(GNode **)b;

    const bool is_dir_a = db_node_is_dir (node_a);
    const bool is_dir_b = db_node_is_dir (node_b);
    if (is_dir_a != is_dir_b) {
        return is_dir_a ? -1 : 1;
    }

    char path_a[PATH_MAX] = "";
    char path_b[PATH_MAX] = "";
    db_node_get_path (node_a, path_a, sizeof (path_a));
    db_node_get_path (node_b, path_b, sizeof (path_b));
    //printf("%s\n", path_a);
    //printf("%s\n", path_b);

    return strverscmp (path_a, path_b);
}

void
db_sort (Database *db)
{
    g_assert (db != NULL);
    g_assert (db->entries != NULL);

    printf("start sorting\n");
    darray_sort (db->entries, sort_by_name);
    printf("finished sorting\n");
}

static void
db_location_free_all (Database *db)
{
    g_assert (db != NULL);
    g_return_if_fail (db->locations != NULL);

    GList *l = db->locations;
    while (l) {
        printf("free location\n");
        db_location_free (l->data);
        l = l->next;
    }
    g_list_free (db->locations);
    db->locations = NULL;
}

bool
db_clear (Database *db)
{
    g_assert (db != NULL);

    printf("clear locations\n");
    db_location_free_all (db);
    db_entries_clear (db);
    return true;
}

