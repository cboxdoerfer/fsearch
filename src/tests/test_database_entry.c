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

   SPDX-License-Identifier: GPL-2.0-or-later
   SPDX-FileCopyrightText: 2026 Christian Boxdörfer
*/

/*
 * Extensive test suite for FsearchDatabaseEntry.
 *
 * Covers the public API declared in fsearch_database_entry.h:
 *   - db_entry_new / db_entry_new_with_attributes
 *   - db_entry_is_folder / db_entry_is_file / db_entry_get_type
 *   - db_entry_is_sibling / db_entry_is_descendant
 *   - db_entry_folder_get_num_children / _num_files / _num_folders
 *   - db_entry_increment_childcount
 *   - db_entry_set_parent / _set_parent_no_update / _set_parent_update_childcount / db_entry_get_parent
 *   - db_entry_set_mtime / db_entry_get_mtime
 *   - db_entry_set_size / db_entry_get_size
 *   - db_entry_set_mark / db_entry_get_mark
 *   - db_entry_set_name (no-op stub)
 *   - db_entry_get_attribute_flags / db_entry_get_flags
 *   - db_entry_get_depth
 *   - db_entry_get_path / db_entry_get_path_full / db_entry_get_root_path
 *   - db_entry_append_path / db_entry_append_full_path / db_entry_append_content_type
 *   - db_entry_get_extension / db_entry_get_name_raw / db_entry_get_name_raw_for_display /
 * db_entry_get_name_for_display
 *   - db_entry_get_attribute_name / db_entry_get_attribute_name_for_offset
 *   - db_entry_get_attribute / db_entry_set_attribute / db_entry_get_attribute_for_offset /
 * db_entry_set_attribute_for_offset
 *   - db_entry_get_attribute_offset / db_entry_get_attribute_offsets
 *   - db_entry_get_deep_copy
 *   - db_entry_free / db_entry_free_no_unparent / db_entry_free_full
 *   - db_entry_compare_context_new / db_entry_compare_context_free
 *   - db_entry_compare_entries_by_{name,size,extension,type,modification_time,position,path,full_path,chain}
 *   - db_entry_{set,is}_monitored_{fanotify,inotify} / db_entry_set_monitored_failed / db_entry_is_monitored_failed
 *
 * Not exercised: db_entry_get_idx, db_entry_get_member_flags and
 * db_entry_get_dummy_for_name_and_parent are declared in the header but have
 * no definition in fsearch_database_entry.c, so calling them would fail to link.
 */

#include "fsearch_database_entry.h"
#include "fsearch_database_entry_flags.h"
#include "fsearch_database_index_properties.h"

#include <glib.h>
#include <string.h>

/* ------------------------------------------------------------------------ *
 * Helpers
 * ------------------------------------------------------------------------ */

static FsearchDatabaseEntry *
new_folder(FsearchDatabaseIndexPropertyFlags flags, const char *name, FsearchDatabaseEntry *parent) {
    return db_entry_new(flags, name, parent, DATABASE_ENTRY_TYPE_FOLDER);
}

static FsearchDatabaseEntry *
new_file(FsearchDatabaseIndexPropertyFlags flags, const char *name, FsearchDatabaseEntry *parent) {
    return db_entry_new(flags, name, parent, DATABASE_ENTRY_TYPE_FILE);
}

/* ------------------------------------------------------------------------ *
 * Creation & type
 * ------------------------------------------------------------------------ */

static void
test_new_file_has_file_type(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "foo.txt", NULL);
    g_assert_true(db_entry_is_file(e));
    g_assert_false(db_entry_is_folder(e));
    g_assert_cmpint(db_entry_get_type(e), ==, DATABASE_ENTRY_TYPE_FILE);
    db_entry_free(e);
}

static void
test_new_folder_has_folder_type(void) {
    FsearchDatabaseEntry *e = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    g_assert_true(db_entry_is_folder(e));
    g_assert_false(db_entry_is_file(e));
    g_assert_cmpint(db_entry_get_type(e), ==, DATABASE_ENTRY_TYPE_FOLDER);
    db_entry_free(e);
}

static void
test_new_stores_name_even_without_name_flag(void) {
    // The NAME attribute is stored unconditionally: db_entry_get_attribute_offset()
    // always succeeds for DATABASE_INDEX_PROPERTY_NAME, regardless of attribute_flags.
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "unflagged.txt", NULL);
    g_assert_cmpstr(db_entry_get_name_raw(e), ==, "unflagged.txt");
    db_entry_free(e);
}

static void
test_new_folder_forces_childcount_flags(void) {
    // Folders always get NUM_FILES|NUM_FOLDERS storage, even if the caller didn't ask for it.
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    const uint32_t flags = db_entry_get_attribute_flags(folder);
    g_assert_true(flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES);
    g_assert_true(flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS);
    db_entry_free(folder);
}

static void
test_new_file_does_not_force_childcount_flags(void) {
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    const uint32_t flags = db_entry_get_attribute_flags(file);
    g_assert_false(flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES);
    g_assert_false(flags & DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS);
    db_entry_free(file);
}

static void
test_new_empty_name_is_valid(void) {
    // An empty (but non-NULL) name is used to represent e.g. a filesystem root.
    FsearchDatabaseEntry *e = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "", NULL);
    g_assert_cmpstr(db_entry_get_name_raw(e), ==, "");
    db_entry_free(e);
}

static void
test_new_with_attributes_stores_all_values(void) {
    FsearchDatabaseEntry *e = db_entry_new_with_attributes(DATABASE_INDEX_PROPERTY_FLAG_SIZE
                                                               | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME,
                                                           "file.bin",
                                                           NULL,
                                                           DATABASE_ENTRY_TYPE_FILE,
                                                           DATABASE_INDEX_PROPERTY_SIZE,
                                                           (int64_t)1234,
                                                           DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
                                                           (int64_t)5678,
                                                           DATABASE_INDEX_PROPERTY_NONE);
    g_assert_cmpint(db_entry_get_size(e), ==, 1234);
    g_assert_cmpint(db_entry_get_mtime(e), ==, 5678);
    db_entry_free(e);
}

static void
test_new_with_attributes_no_varargs(void) {
    FsearchDatabaseEntry *e = db_entry_new_with_attributes(DATABASE_INDEX_PROPERTY_FLAG_NONE,
                                                           "plain",
                                                           NULL,
                                                           DATABASE_ENTRY_TYPE_FILE,
                                                           DATABASE_INDEX_PROPERTY_NONE);
    g_assert_cmpstr(db_entry_get_name_raw(e), ==, "plain");
    db_entry_free(e);
}

static void
test_new_with_attributes_folder_num_files_folders(void) {
    FsearchDatabaseEntry *e = db_entry_new_with_attributes(DATABASE_INDEX_PROPERTY_FLAG_NONE,
                                                           "dir",
                                                           NULL,
                                                           DATABASE_ENTRY_TYPE_FOLDER,
                                                           DATABASE_INDEX_PROPERTY_NUM_FILES,
                                                           (int32_t)3,
                                                           DATABASE_INDEX_PROPERTY_NUM_FOLDERS,
                                                           (int32_t)2,
                                                           DATABASE_INDEX_PROPERTY_NONE);
    g_assert_cmpuint(db_entry_folder_get_num_files(e), ==, 3);
    g_assert_cmpuint(db_entry_folder_get_num_folders(e), ==, 2);
    g_assert_cmpuint(db_entry_folder_get_num_children(e), ==, 5);
    db_entry_free(e);
}

/* ------------------------------------------------------------------------ *
 * Attribute flags & generic attribute get/set
 * ------------------------------------------------------------------------ */

static void
test_get_attribute_flags_null_returns_zero(void) {
    g_assert_cmpuint(db_entry_get_attribute_flags(NULL), ==, 0);
}

static void
test_get_attribute_missing_flag_returns_false(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    off_t size = 99;
    g_assert_false(db_entry_get_attribute(e, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size)));
    // dest is left untouched on failure
    g_assert_cmpint(size, ==, 99);
    db_entry_free(e);
}

static void
test_set_attribute_missing_flag_returns_false(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    off_t size = 42;
    g_assert_false(db_entry_set_attribute(e, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size)));
    db_entry_free(e);
}

static void
test_get_set_attribute_size_roundtrip(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);
    off_t size = 777;
    g_assert_true(db_entry_set_attribute(e, DATABASE_INDEX_PROPERTY_SIZE, &size, sizeof(size)));
    off_t out = 0;
    g_assert_true(db_entry_get_attribute(e, DATABASE_INDEX_PROPERTY_SIZE, &out, sizeof(out)));
    g_assert_cmpint(out, ==, 777);
    db_entry_free(e);
}

static void
test_creation_time_is_never_stored(void) {
    // DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME is declared but
    // db_entry_get_attribute_offset() never checks for it, so the attribute
    // can never be located, even when the flag is requested at creation time.
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME, "f", NULL);
    int64_t ctime = 123;
    g_assert_false(db_entry_get_attribute(e, DATABASE_INDEX_PROPERTY_CREATION_TIME, &ctime, sizeof(ctime)));
    g_assert_false(db_entry_set_attribute(e, DATABASE_INDEX_PROPERTY_CREATION_TIME, &ctime, sizeof(ctime)));
    db_entry_free(e);
}

static void
test_get_attribute_offset_full_layout(void) {
    const FsearchDatabaseIndexPropertyFlags flags = DATABASE_INDEX_PROPERTY_FLAG_SIZE
                                                  | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME
                                                  | DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME
                                                  | DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME
                                                  | DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES
                                                  | DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS
                                                  | DATABASE_INDEX_PROPERTY_FLAG_NAME;
    size_t offset = 0;
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_SIZE, &offset));
    g_assert_cmpuint(offset, ==, 0);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, &offset));
    g_assert_cmpuint(offset, ==, 8);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_ACCESS_TIME, &offset));
    g_assert_cmpuint(offset, ==, 16);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME, &offset));
    g_assert_cmpuint(offset, ==, 24);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_NUM_FILES, &offset));
    g_assert_cmpuint(offset, ==, 32);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_NUM_FOLDERS, &offset));
    g_assert_cmpuint(offset, ==, 36);
    g_assert_true(db_entry_get_attribute_offset(flags, DATABASE_INDEX_PROPERTY_NAME, &offset));
    g_assert_cmpuint(offset, ==, 40);
}

static void
test_get_attribute_offset_none_property_fails(void) {
    size_t offset = 0;
    g_assert_false(db_entry_get_attribute_offset(DATABASE_INDEX_PROPERTY_FLAG_ALL, DATABASE_INDEX_PROPERTY_NONE, &offset));
}

static void
test_get_attribute_offsets_computed_properties_are_absent(void) {
    // PATH, PATH_FULL, FILETYPE and EXTENSION are computed on demand and never
    // stored inline, so they must always report an invalid (-1) offset, even
    // with every flag set.
    g_autofree size_t *offsets = db_entry_get_attribute_offsets(DATABASE_INDEX_PROPERTY_FLAG_ALL);
    const size_t invalid = (size_t)-1;
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_NONE], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_PATH], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_PATH_FULL], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_CREATION_TIME], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_FILETYPE], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_EXTENSION], ==, invalid);
    g_assert_cmpuint(offsets[DATABASE_INDEX_PROPERTY_NAME], ==, 40);
}

static void
test_get_attribute_name_for_offset(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myname", NULL);
    size_t offset = 0;
    g_assert_true(db_entry_get_attribute_offset(db_entry_get_attribute_flags(e), DATABASE_INDEX_PROPERTY_NAME, &offset));
    g_assert_cmpstr(db_entry_get_attribute_name_for_offset(e, offset), ==, "myname");
    db_entry_free(e);
}

static void
test_get_attribute_for_offset_and_set_attribute_for_offset(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);
    size_t offset = 0;
    g_assert_true(db_entry_get_attribute_offset(db_entry_get_attribute_flags(e), DATABASE_INDEX_PROPERTY_SIZE, &offset));

    off_t value = 555;
    db_entry_set_attribute_for_offset(e, offset, &value, sizeof(value));

    off_t out = 0;
    db_entry_get_attribute_for_offset(e, offset, &out, sizeof(out));
    g_assert_cmpint(out, ==, 555);
    db_entry_free(e);
}

/* ------------------------------------------------------------------------ *
 * Name & extension
 * ------------------------------------------------------------------------ */

static void
test_get_name_raw(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "hello.c", NULL);
    g_assert_cmpstr(db_entry_get_name_raw(e), ==, "hello.c");
    db_entry_free(e);
}

static void
test_get_name_raw_for_display_regular_name(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "hello.c", NULL);
    g_assert_cmpstr(db_entry_get_name_raw_for_display(e), ==, "hello.c");
    db_entry_free(e);
}

static void
test_get_name_raw_for_display_empty_name_is_separator(void) {
    FsearchDatabaseEntry *e = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "", NULL);
    g_assert_cmpstr(db_entry_get_name_raw_for_display(e), ==, G_DIR_SEPARATOR_S);
    db_entry_free(e);
}

static void
test_get_name_raw_for_display_null_entry(void) {
    g_assert_null(db_entry_get_name_raw_for_display(NULL));
}

static void
test_get_name_for_display_returns_gstring(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "abc", NULL);
    GString *s = db_entry_get_name_for_display(e);
    g_assert_cmpstr(s->str, ==, "abc");
    g_string_free(s, TRUE);
    db_entry_free(e);
}

static void
test_get_extension_regular(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "archive.tar.gz", NULL);
    g_assert_cmpstr(db_entry_get_extension(e), ==, "gz");
    db_entry_free(e);
}

static void
test_get_extension_no_dot(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "Makefile", NULL);
    g_assert_cmpstr(db_entry_get_extension(e), ==, "");
    db_entry_free(e);
}

static void
test_get_extension_hidden_file(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, ".bashrc", NULL);
    g_assert_cmpstr(db_entry_get_extension(e), ==, "");
    db_entry_free(e);
}

static void
test_get_extension_trailing_dot(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "weird.", NULL);
    g_assert_cmpstr(db_entry_get_extension(e), ==, "");
    db_entry_free(e);
}

static void
test_get_extension_folder_is_null(void) {
    FsearchDatabaseEntry *e = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir.d", NULL);
    g_assert_null(db_entry_get_extension(e));
    db_entry_free(e);
}

static void
test_get_extension_null_entry(void) {
    g_assert_null(db_entry_get_extension(NULL));
}

/* ------------------------------------------------------------------------ *
 * Mark
 * ------------------------------------------------------------------------ */

static void
test_mark_default_is_unset(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    g_assert_cmpuint(db_entry_get_mark(e), ==, 0);
    db_entry_free(e);
}

static void
test_mark_set_and_clear(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    db_entry_set_mark(e, 1);
    g_assert_cmpuint(db_entry_get_mark(e), ==, 1);
    db_entry_set_mark(e, 0);
    g_assert_cmpuint(db_entry_get_mark(e), ==, 0);
    db_entry_free(e);
}

static void
test_mark_null_entry_returns_zero(void) {
    g_assert_cmpuint(db_entry_get_mark(NULL), ==, 0);
}

/* ------------------------------------------------------------------------ *
 * set_name stub
 * ------------------------------------------------------------------------ */

static void
test_set_name_is_currently_a_noop(void) {
    // db_entry_set_name() is an unimplemented stub (see TODO in the implementation);
    // calling it must not crash, and the name must remain unchanged.
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "original", NULL);
    db_entry_set_name(e, "renamed");
    g_assert_cmpstr(db_entry_get_name_raw(e), ==, "original");
    db_entry_free(e);
}

/* ------------------------------------------------------------------------ *
 * Depth, parent, sibling, descendant
 * ------------------------------------------------------------------------ */

static void
test_depth_root_is_zero(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    g_assert_cmpuint(db_entry_get_depth(root), ==, 0);
    db_entry_free(root);
}

static void
test_depth_increases_with_nesting(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *mid = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "mid", root);
    FsearchDatabaseEntry *leaf = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "leaf", mid);

    g_assert_cmpuint(db_entry_get_depth(mid), ==, 1);
    g_assert_cmpuint(db_entry_get_depth(leaf), ==, 2);

    db_entry_free(leaf);
    db_entry_free(mid);
    db_entry_free(root);
}

static void
test_get_parent_of_root_is_null(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    g_assert_null(db_entry_get_parent(root));
    db_entry_free(root);
}

static void
test_get_parent_null_entry_returns_null(void) {
    g_assert_null(db_entry_get_parent(NULL));
}

static void
test_get_parent_returns_actual_parent(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *child = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "child", root);
    g_assert_true(db_entry_get_parent(child) == root);
    db_entry_free(child);
    db_entry_free(root);
}

static void
test_is_sibling_same_parent(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", root);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", root);

    g_assert_true(db_entry_is_sibling(a, b));
    g_assert_true(db_entry_is_sibling(b, a));

    db_entry_free(a);
    db_entry_free(b);
    db_entry_free(root);
}

static void
test_is_sibling_different_parent(void) {
    FsearchDatabaseEntry *root1 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root1", NULL);
    FsearchDatabaseEntry *root2 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root2", NULL);
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", root1);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", root2);

    g_assert_false(db_entry_is_sibling(a, b));

    db_entry_free(a);
    db_entry_free(b);
    db_entry_free(root1);
    db_entry_free(root2);
}

static void
test_is_sibling_two_roots_are_not_siblings(void) {
    // Both entries have parent == NULL; db_entry_is_sibling() requires a truthy
    // (non-NULL) shared parent, so two roots never count as siblings.
    FsearchDatabaseEntry *root1 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root1", NULL);
    FsearchDatabaseEntry *root2 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root2", NULL);

    g_assert_false(db_entry_is_sibling(root1, root2));

    db_entry_free(root1);
    db_entry_free(root2);
}

static void
test_is_descendant_direct_child(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *child = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "child", root);

    g_assert_true(db_entry_is_descendant(child, root));
    g_assert_false(db_entry_is_descendant(root, child));

    db_entry_free(child);
    db_entry_free(root);
}

static void
test_is_descendant_deeply_nested(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *mid = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "mid", root);
    FsearchDatabaseEntry *leaf = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "leaf", mid);

    g_assert_true(db_entry_is_descendant(leaf, root));
    g_assert_true(db_entry_is_descendant(leaf, mid));

    db_entry_free(leaf);
    db_entry_free(mid);
    db_entry_free(root);
}

static void
test_is_descendant_unrelated_entries(void) {
    FsearchDatabaseEntry *root1 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root1", NULL);
    FsearchDatabaseEntry *root2 = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root2", NULL);
    FsearchDatabaseEntry *child = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "child", root1);

    g_assert_false(db_entry_is_descendant(child, root2));

    db_entry_free(child);
    db_entry_free(root1);
    db_entry_free(root2);
}

static void
test_is_descendant_entry_is_not_its_own_descendant(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    g_assert_false(db_entry_is_descendant(root, root));
    db_entry_free(root);
}

/* ------------------------------------------------------------------------ *
 * Parenting: set_parent / set_parent_no_update / set_parent_update_childcount
 * ------------------------------------------------------------------------ */

static void
test_set_parent_updates_childcount(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);

    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 0);
    db_entry_set_parent(file, root);
    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 1);
    g_assert_true(db_entry_get_parent(file) == root);

    db_entry_free(file);
    db_entry_free(root);
}

static void
test_set_parent_reparenting_decrements_old_increments_new(void) {
    FsearchDatabaseEntry *root_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    FsearchDatabaseEntry *root_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", root_a);

    g_assert_cmpuint(db_entry_folder_get_num_files(root_a), ==, 1);

    db_entry_set_parent(file, root_b);

    g_assert_cmpuint(db_entry_folder_get_num_files(root_a), ==, 0);
    g_assert_cmpuint(db_entry_folder_get_num_files(root_b), ==, 1);
    g_assert_true(db_entry_get_parent(file) == root_b);

    db_entry_free(file);
    db_entry_free(root_a);
    db_entry_free(root_b);
}

static void
test_set_parent_folder_updates_num_folders(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *sub = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "sub", NULL);

    db_entry_set_parent(sub, root);
    g_assert_cmpuint(db_entry_folder_get_num_folders(root), ==, 1);
    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 0);

    db_entry_free(sub);
    db_entry_free(root);
}

static void
test_set_parent_propagates_size_to_all_ancestors(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "root", NULL);
    FsearchDatabaseEntry *mid = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "mid", root);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);

    db_entry_set_size(file, 100);
    db_entry_set_parent(file, mid);

    g_assert_cmpint(db_entry_get_size(mid), ==, 100);
    g_assert_cmpint(db_entry_get_size(root), ==, 100);

    db_entry_free(file);
    db_entry_free(mid);
    db_entry_free(root);
}

static void
test_set_parent_size_propagation_stops_at_ancestor_without_size_flag(void) {
    // db_entry_update_folder_size() only recurses into folder->parent from
    // *inside* the branch where the current folder has the SIZE attribute;
    // if an intermediate ancestor lacks DATABASE_INDEX_PROPERTY_FLAG_SIZE,
    // propagation silently stops there instead of skipping past it.
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "root", NULL);
    FsearchDatabaseEntry *mid_no_size = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "mid", root);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);

    db_entry_set_size(file, 100);
    db_entry_set_parent(file, mid_no_size);

    g_assert_cmpint(db_entry_get_size(root), ==, 0);

    db_entry_free(file);
    db_entry_free(mid_no_size);
    db_entry_free(root);
}

static void
test_set_parent_reparent_updates_size_on_both_sides(void) {
    FsearchDatabaseEntry *root_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "a", NULL);
    FsearchDatabaseEntry *root_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "b", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);

    db_entry_set_size(file, 50);
    db_entry_set_parent(file, root_a);
    g_assert_cmpint(db_entry_get_size(root_a), ==, 50);

    db_entry_set_parent(file, root_b);
    g_assert_cmpint(db_entry_get_size(root_a), ==, 0);
    g_assert_cmpint(db_entry_get_size(root_b), ==, 50);

    db_entry_free(file);
    db_entry_free(root_a);
    db_entry_free(root_b);
}

static void
test_set_parent_no_update_does_not_touch_counts(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);

    db_entry_set_parent_no_update(file, root);

    g_assert_true(db_entry_get_parent(file) == root);
    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 0);

    // Undo the manual parenting before freeing to avoid touching root's counts on free.
    db_entry_set_parent_no_update(file, NULL);
    db_entry_free(file);
    db_entry_free(root);
}

static void
test_set_parent_update_childcount_does_not_touch_size(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", NULL);
    db_entry_set_size(file, 500);

    db_entry_set_parent_update_childcount(file, root);

    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 1);
    // Unlike db_entry_set_parent(), this variant never calls db_entry_update_folder_size().
    g_assert_cmpint(db_entry_get_size(root), ==, 0);

    // no unparent on free because root size is in an invalid state
    db_entry_free_no_unparent(file);
    db_entry_free(root);
}

static void
test_set_parent_update_childcount_reparent_decrements_old(void) {
    FsearchDatabaseEntry *root_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    FsearchDatabaseEntry *root_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);

    db_entry_set_parent_update_childcount(file, root_a);
    g_assert_cmpuint(db_entry_folder_get_num_files(root_a), ==, 1);

    db_entry_set_parent_update_childcount(file, root_b);
    g_assert_cmpuint(db_entry_folder_get_num_files(root_a), ==, 0);
    g_assert_cmpuint(db_entry_folder_get_num_files(root_b), ==, 1);

    db_entry_free(file);
    db_entry_free(root_a);
    db_entry_free(root_b);
}

static void
test_increment_childcount_file_and_folder(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);

    db_entry_increment_childcount(root, DATABASE_ENTRY_TYPE_FILE);
    db_entry_increment_childcount(root, DATABASE_ENTRY_TYPE_FILE);
    db_entry_increment_childcount(root, DATABASE_ENTRY_TYPE_FOLDER);

    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 2);
    g_assert_cmpuint(db_entry_folder_get_num_folders(root), ==, 1);
    g_assert_cmpuint(db_entry_folder_get_num_children(root), ==, 3);

    db_entry_free(root);
}

static void
test_increment_childcount_null_entry_is_safe(void) {
    // Must silently no-op for a NULL entry.
    db_entry_increment_childcount(NULL, DATABASE_ENTRY_TYPE_FILE);
}

/* ------------------------------------------------------------------------ *
 * Paths
 * ------------------------------------------------------------------------ */

static void
test_get_root_path_single_level(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    g_assert_cmpstr(db_entry_get_root_path(root), ==, "myroot");
    db_entry_free(root);
}

static void
test_get_root_path_nested(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    FsearchDatabaseEntry *mid = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "mid", root);
    FsearchDatabaseEntry *leaf = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "leaf", mid);

    g_assert_cmpstr(db_entry_get_root_path(leaf), ==, "myroot");

    db_entry_free(leaf);
    db_entry_free(mid);
    db_entry_free(root);
}

static void
test_get_root_path_null_entry(void) {
    g_assert_null(db_entry_get_root_path(NULL));
}

static void
test_get_path_of_root_entry_is_empty(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    GString *path = db_entry_get_path(root);
    g_assert_cmpstr(path->str, ==, "");
    g_string_free(path, TRUE);
    db_entry_free(root);
}

static void
test_get_path_full_of_root_entry_is_its_name(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    GString *path = db_entry_get_path_full(root);
    g_assert_cmpstr(path->str, ==, "myroot");
    g_string_free(path, TRUE);
    db_entry_free(root);
}

static void
test_get_path_excludes_entry_own_name(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    FsearchDatabaseEntry *sub = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "sub", root);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.txt", sub);

    GString *sub_path = db_entry_get_path(sub);
    g_assert_cmpstr(sub_path->str, ==, "myroot");
    g_string_free(sub_path, TRUE);

    GString *file_path = db_entry_get_path(file);
    g_assert_cmpstr(file_path->str, ==, "myroot" G_DIR_SEPARATOR_S "sub");
    g_string_free(file_path, TRUE);

    db_entry_free(file);
    db_entry_free(sub);
    db_entry_free(root);
}

static void
test_get_path_full_includes_entry_own_name(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    FsearchDatabaseEntry *sub = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "sub", root);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.txt", sub);

    GString *path = db_entry_get_path_full(file);
    g_assert_cmpstr(path->str, ==, "myroot" G_DIR_SEPARATOR_S "sub" G_DIR_SEPARATOR_S "a.txt");
    g_string_free(path, TRUE);

    db_entry_free(file);
    db_entry_free(sub);
    db_entry_free(root);
}

static void
test_paths_when_root_name_is_separator(void) {
    // Regression test for the "duplicate path separator when root is /" fix:
    // build_path_recursively() must special-case a root literally named "/"
    // so it doesn't get doubled up with the separator appended after it.
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, G_DIR_SEPARATOR_S, NULL);
    FsearchDatabaseEntry *etc = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "etc", root);
    FsearchDatabaseEntry *passwd = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "passwd", etc);

    g_assert_cmpstr(db_entry_get_root_path(passwd), ==, G_DIR_SEPARATOR_S);

    GString *etc_path = db_entry_get_path(etc);
    g_assert_cmpstr(etc_path->str, ==, G_DIR_SEPARATOR_S);
    g_string_free(etc_path, TRUE);

    GString *etc_path_full = db_entry_get_path_full(etc);
    g_assert_cmpstr(etc_path_full->str, ==, G_DIR_SEPARATOR_S "etc");
    g_string_free(etc_path_full, TRUE);

    GString *passwd_path = db_entry_get_path(passwd);
    g_assert_cmpstr(passwd_path->str, ==, G_DIR_SEPARATOR_S "etc");
    g_string_free(passwd_path, TRUE);

    GString *passwd_path_full = db_entry_get_path_full(passwd);
    g_assert_cmpstr(passwd_path_full->str, ==, G_DIR_SEPARATOR_S "etc" G_DIR_SEPARATOR_S "passwd");
    g_string_free(passwd_path_full, TRUE);

    db_entry_free(passwd);
    db_entry_free(etc);
    db_entry_free(root);
}

static void
test_append_path_appends_to_existing_content(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "myroot", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", root);

    GString *str = g_string_new("prefix:");
    db_entry_append_path(file, str);
    g_assert_cmpstr(str->str, ==, "prefix:myroot");
    g_string_free(str, TRUE);

    db_entry_free(file);
    db_entry_free(root);
}

static void
test_append_content_type_of_nonexistent_path_is_unknown(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "does_not_exist_root_xyz", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "does_not_exist_file_xyz.txt", root);

    GString *str = g_string_new(NULL);
    db_entry_append_content_type(file, str);
    g_assert_cmpstr(str->str, ==, "unknown");
    g_string_free(str, TRUE);

    db_entry_free(file);
    db_entry_free(root);
}

/* ------------------------------------------------------------------------ *
 * Comparators
 * ------------------------------------------------------------------------ */

static void
test_compare_by_name_natural_order(void) {
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "file2.txt", NULL);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "file10.txt", NULL);

    // Natural sort compares numeric runs by value: "2" < "10".
    g_assert_cmpint(db_entry_compare_entries_by_name(&a, &b), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_name(&b, &a), >, 0);
    g_assert_cmpint(db_entry_compare_entries_by_name(&a, &a), ==, 0);

    db_entry_free(a);
    db_entry_free(b);
}

static void
test_compare_by_name_null_entries_are_equal(void) {
    FsearchDatabaseEntry *null_entry = NULL;
    FsearchDatabaseEntry *other = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    g_assert_cmpint(db_entry_compare_entries_by_name(&null_entry, &other), ==, 0);
    g_assert_cmpint(db_entry_compare_entries_by_name(&other, &null_entry), ==, 0);
    db_entry_free(other);
}

static void
test_compare_by_size(void) {
    FsearchDatabaseEntry *small = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "a", NULL);
    FsearchDatabaseEntry *big = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "b", NULL);
    db_entry_set_size(small, 10);
    db_entry_set_size(big, 20);

    g_assert_cmpint(db_entry_compare_entries_by_size(&small, &big), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_size(&big, &small), >, 0);
    g_assert_cmpint(db_entry_compare_entries_by_size(&small, &small), ==, 0);

    db_entry_free(small);
    db_entry_free(big);
}

static void
test_compare_by_size_without_flag_defaults_to_zero(void) {
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", NULL);
    g_assert_cmpint(db_entry_compare_entries_by_size(&a, &b), ==, 0);
    db_entry_free(a);
    db_entry_free(b);
}

static void
test_compare_by_modification_time(void) {
    FsearchDatabaseEntry *older = new_file(DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME, "a", NULL);
    FsearchDatabaseEntry *newer = new_file(DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME, "b", NULL);
    db_entry_set_mtime(older, 100);
    db_entry_set_mtime(newer, 200);

    g_assert_cmpint(db_entry_compare_entries_by_modification_time(&older, &newer), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_modification_time(&newer, &older), >, 0);
    g_assert_cmpint(db_entry_compare_entries_by_modification_time(&older, &older), ==, 0);

    db_entry_free(older);
    db_entry_free(newer);
}

static void
test_compare_by_extension(void) {
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.csv", NULL);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b.txt", NULL);
    g_assert_cmpint(db_entry_compare_entries_by_extension(&a, &b), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_extension(&b, &a), >, 0);
    db_entry_free(a);
    db_entry_free(b);
}

static void
test_compare_by_extension_folder_treated_as_empty(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir.d", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b.txt", NULL);
    // Folder extension is NULL -> treated as "", which sorts before any non-empty extension.
    g_assert_cmpint(db_entry_compare_entries_by_extension(&folder, &file), <, 0);
    db_entry_free(folder);
    db_entry_free(file);
}

static void
test_compare_by_position_always_zero(void) {
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "z", NULL);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    g_assert_cmpint(db_entry_compare_entries_by_position(&a, &b), ==, 0);
    g_assert_cmpint(db_entry_compare_entries_by_position(&b, &a), ==, 0);
    db_entry_free(a);
    db_entry_free(b);
}

static void
test_compare_by_type_folders_always_equal(void) {
    // db_entry_compare_entries_by_type() only ever compares the type itself now; falling back to
    // another property on a tie is the job of db_entry_compare_entries_by_chain(), not this
    // function, so two folders (which always share the "Folder" type string) always compare equal
    // regardless of name.
    FsearchDatabaseEntryCompareContext *ctx = db_entry_compare_context_new((FsearchDatabaseSortOrderChain){});
    FsearchDatabaseEntry *dir_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    FsearchDatabaseEntry *dir_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", NULL);

    g_assert_cmpint(db_entry_compare_entries_by_type(&dir_a, &dir_b, ctx), ==, 0);
    g_assert_cmpint(db_entry_compare_entries_by_type(&dir_b, &dir_a, ctx), ==, 0);
    g_assert_cmpint(db_entry_compare_entries_by_type(&dir_a, &dir_a, ctx), ==, 0);

    // Both folders share the "Folder" type string, so exactly one entry is added to the
    // dedup table, but each entry gets its own cache slot.
    g_assert_cmpuint(g_hash_table_size(ctx->file_type_table), ==, 1);
    g_assert_cmpuint(g_hash_table_size(ctx->entry_to_file_type_table), ==, 2);

    db_entry_compare_context_free(ctx);
    db_entry_free(dir_a);
    db_entry_free(dir_b);
}

static void
test_compare_by_type_folder_and_file_differ(void) {
    FsearchDatabaseEntryCompareContext *ctx = db_entry_compare_context_new((FsearchDatabaseSortOrderChain){});
    FsearchDatabaseEntry *dir = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "file.txt", NULL);

    g_assert_cmpint(db_entry_compare_entries_by_type(&dir, &file, ctx), !=, 0);

    db_entry_compare_context_free(ctx);
    db_entry_free(dir);
    db_entry_free(file);
}

static void
test_compare_by_chain_type_then_name(void) {
    // db_entry_compare_entries_by_chain() is what actually implements "fall back to the next
    // property on a tie" -- here [TYPE, NAME], so two folders (tied on TYPE) fall back to NAME.
    FsearchDatabaseSortOrderChain chain = {
        .properties = {DATABASE_INDEX_PROPERTY_FILETYPE, DATABASE_INDEX_PROPERTY_NAME},
        .length = 2,
    };
    FsearchDatabaseEntryCompareContext *ctx = db_entry_compare_context_new(chain);
    FsearchDatabaseEntry *dir_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", NULL);
    FsearchDatabaseEntry *dir_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", NULL);

    g_assert_cmpint(db_entry_compare_entries_by_chain(&dir_a, &dir_b, ctx), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_chain(&dir_b, &dir_a, ctx), >, 0);
    g_assert_cmpint(db_entry_compare_entries_by_chain(&dir_a, &dir_a, ctx), ==, 0);

    db_entry_compare_context_free(ctx);
    db_entry_free(dir_a);
    db_entry_free(dir_b);
}

static void
test_compare_by_path_siblings_use_name_order(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.txt", root);
    FsearchDatabaseEntry *b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b.txt", root);

    g_assert_cmpint(db_entry_compare_entries_by_path(&a, &b), ==, 0);
    g_assert_cmpint(db_entry_compare_entries_by_full_path(&a, &b), <, 0);

    db_entry_free(a);
    db_entry_free(b);
    db_entry_free(root);
}

static void
test_compare_by_path_different_depth(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *sub = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "sub", root);
    FsearchDatabaseEntry *shallow_file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "z.txt", root);
    FsearchDatabaseEntry *deep_file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.txt", sub);

    // A file directly under root sorts before one nested inside a subfolder of root.
    g_assert_cmpint(db_entry_compare_entries_by_path(&shallow_file, &deep_file), <, 0);
    g_assert_cmpint(db_entry_compare_entries_by_path(&deep_file, &shallow_file), >, 0);

    db_entry_free(shallow_file);
    db_entry_free(deep_file);
    db_entry_free(sub);
    db_entry_free(root);
}

static void
test_compare_by_full_path_orders_by_ancestor_names(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *sub_a = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a", root);
    FsearchDatabaseEntry *sub_b = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "b", root);
    FsearchDatabaseEntry *file_in_a = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "z.txt", sub_a);
    FsearchDatabaseEntry *file_in_b = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "a.txt", sub_b);

    // "root/a/z.txt" sorts before "root/b/a.txt" because "a" < "b" at the shared depth.
    g_assert_cmpint(db_entry_compare_entries_by_full_path(&file_in_a, &file_in_b), <, 0);

    db_entry_free(file_in_a);
    db_entry_free(file_in_b);
    db_entry_free(sub_a);
    db_entry_free(sub_b);
    db_entry_free(root);
}

/* ------------------------------------------------------------------------ *
 * Deep copy
 * ------------------------------------------------------------------------ */

static void
test_deep_copy_root_entry(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "orig.txt", NULL);
    db_entry_set_size(e, 42);

    FsearchDatabaseEntry *copy = db_entry_get_deep_copy(e);
    g_assert_true(copy != e);
    g_assert_cmpstr(db_entry_get_name_raw(copy), ==, "orig.txt");
    g_assert_cmpint(db_entry_get_size(copy), ==, 42);
    g_assert_null(db_entry_get_parent(copy));

    db_entry_free(copy);
    db_entry_free(e);
}

static void
test_deep_copy_is_independent_of_original(void) {
    FsearchDatabaseEntry *e = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "orig.txt", NULL);
    db_entry_set_size(e, 42);
    FsearchDatabaseEntry *copy = db_entry_get_deep_copy(e);

    db_entry_set_size(copy, 999);
    g_assert_cmpint(db_entry_get_size(e), ==, 42);
    g_assert_cmpint(db_entry_get_size(copy), ==, 999);

    db_entry_free(copy);
    db_entry_free(e);
}

static void
test_deep_copy_duplicates_parent_chain(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", root);

    FsearchDatabaseEntry *copy = db_entry_get_deep_copy(file);
    FsearchDatabaseEntry *copy_parent = db_entry_get_parent(copy);

    g_assert_nonnull(copy_parent);
    g_assert_true(copy_parent != root);
    g_assert_cmpstr(db_entry_get_name_raw(copy_parent), ==, "root");

    // The copy owns an entirely separate parent chain, so it can be freed
    // independently without disturbing the original tree.
    db_entry_free(copy);
    db_entry_free(copy_parent);

    db_entry_free(file);
    db_entry_free(root);
}

/* ------------------------------------------------------------------------ *
 * Free variants
 * ------------------------------------------------------------------------ */

static void
test_free_updates_parent_state(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_SIZE, "f", root);
    db_entry_set_size(file, 30);
    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 1);
    g_assert_cmpint(db_entry_get_size(root), ==, 30);

    db_entry_free(file);

    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 0);
    g_assert_cmpint(db_entry_get_size(root), ==, 0);

    db_entry_free(root);
}

static void
test_free_no_unparent_leaves_parent_state_untouched(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", root);
    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 1);

    // Frees just the entry struct; root's bookkeeping is left exactly as-is.
    db_entry_free_no_unparent(file);

    g_assert_cmpuint(db_entry_folder_get_num_files(root), ==, 1);

    db_entry_free(root);
}

static void
test_free_full_frees_entire_chain(void) {
    FsearchDatabaseEntry *root = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "root", NULL);
    FsearchDatabaseEntry *mid = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "mid", root);
    FsearchDatabaseEntry *leaf = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "leaf", mid);

    // Frees leaf, then mid, then root, walking up the parent chain.
    db_entry_free_full(leaf);
}

/* ------------------------------------------------------------------------ *
 * Flags: monitored fanotify / inotify / failed
 * ------------------------------------------------------------------------ */

static void
test_monitored_fanotify_roundtrip_on_folder(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    g_assert_false(db_entry_is_monitored_fanotify(folder));

    db_entry_set_monitored_fanotify(folder);
    g_assert_true(db_entry_is_monitored_fanotify(folder));

    db_entry_set_unmonitored_fanotify(folder);
    g_assert_false(db_entry_is_monitored_fanotify(folder));

    db_entry_free(folder);
}

static void
test_monitored_inotify_roundtrip_on_folder(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    g_assert_false(db_entry_is_monitored_inotify(folder));

    db_entry_set_monitored_inotify(folder);
    g_assert_true(db_entry_is_monitored_inotify(folder));

    db_entry_set_unmonitored_inotify(folder);
    g_assert_false(db_entry_is_monitored_inotify(folder));

    db_entry_free(folder);
}

static void
test_monitored_failed_flag_on_folder(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    g_assert_false(db_entry_is_monitored_failed(folder));

    db_entry_set_monitored_failed(folder);
    g_assert_true(db_entry_is_monitored_failed(folder));

    db_entry_free(folder);
}

static void
test_monitored_flags_on_file_redirect_to_parent(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", folder);

    db_entry_set_monitored_fanotify(folder);
    db_entry_set_monitored_inotify(folder);
    db_entry_set_monitored_failed(folder);

    g_assert_true(db_entry_is_monitored_fanotify(file));
    g_assert_true(db_entry_is_monitored_inotify(file));
    g_assert_true(db_entry_is_monitored_failed(file));

    db_entry_free(file);
    db_entry_free(folder);
}

static void
test_monitored_flags_on_rootless_file_are_false(void) {
    // A file with no parent has nothing to redirect to and must report false
    // rather than crash.
    FsearchDatabaseEntry *file = new_file(DATABASE_INDEX_PROPERTY_FLAG_NONE, "f", NULL);
    g_assert_false(db_entry_is_monitored_fanotify(file));
    g_assert_false(db_entry_is_monitored_inotify(file));
    g_assert_false(db_entry_is_monitored_failed(file));
    db_entry_free(file);
}

/* ------------------------------------------------------------------------ *
 * db_entry_get_flags
 * ------------------------------------------------------------------------ */

static void
test_get_flags_reflects_type_and_mark(void) {
    FsearchDatabaseEntry *folder = new_folder(DATABASE_INDEX_PROPERTY_FLAG_NONE, "dir", NULL);
    g_assert_true(db_entry_get_flags(folder) & FSEARCH_DATABASE_ENTRY_FLAG_TYPE_FOLDER);
    g_assert_false(db_entry_get_flags(folder) & FSEARCH_DATABASE_ENTRY_FLAG_MARKED);

    db_entry_set_mark(folder, 1);
    g_assert_true(db_entry_get_flags(folder) & FSEARCH_DATABASE_ENTRY_FLAG_MARKED);

    db_entry_free(folder);
}

/* ------------------------------------------------------------------------ *
 * Main
 * ------------------------------------------------------------------------ */

int
main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    // Creation & type
    g_test_add_func("/FSearch/database/entry/new_file_type", test_new_file_has_file_type);
    g_test_add_func("/FSearch/database/entry/new_folder_type", test_new_folder_has_folder_type);
    g_test_add_func("/FSearch/database/entry/new_stores_name_without_flag", test_new_stores_name_even_without_name_flag);
    g_test_add_func("/FSearch/database/entry/new_folder_forces_childcount_flags",
                    test_new_folder_forces_childcount_flags);
    g_test_add_func("/FSearch/database/entry/new_file_no_childcount_flags",
                    test_new_file_does_not_force_childcount_flags);
    g_test_add_func("/FSearch/database/entry/new_empty_name", test_new_empty_name_is_valid);
    g_test_add_func("/FSearch/database/entry/new_with_attributes_stores_values",
                    test_new_with_attributes_stores_all_values);
    g_test_add_func("/FSearch/database/entry/new_with_attributes_no_varargs", test_new_with_attributes_no_varargs);
    g_test_add_func("/FSearch/database/entry/new_with_attributes_folder_counts",
                    test_new_with_attributes_folder_num_files_folders);

    // Attribute flags & generic get/set
    g_test_add_func("/FSearch/database/entry/attribute_flags_null", test_get_attribute_flags_null_returns_zero);
    g_test_add_func("/FSearch/database/entry/attribute_get_missing_flag", test_get_attribute_missing_flag_returns_false);
    g_test_add_func("/FSearch/database/entry/attribute_set_missing_flag", test_set_attribute_missing_flag_returns_false);
    g_test_add_func("/FSearch/database/entry/attribute_size_roundtrip", test_get_set_attribute_size_roundtrip);
    g_test_add_func("/FSearch/database/entry/creation_time_never_stored", test_creation_time_is_never_stored);
    g_test_add_func("/FSearch/database/entry/attribute_offset_full_layout", test_get_attribute_offset_full_layout);
    g_test_add_func("/FSearch/database/entry/attribute_offset_none_fails", test_get_attribute_offset_none_property_fails);
    g_test_add_func("/FSearch/database/entry/attribute_offsets_computed_properties_absent",
                    test_get_attribute_offsets_computed_properties_are_absent);
    g_test_add_func("/FSearch/database/entry/attribute_name_for_offset", test_get_attribute_name_for_offset);
    g_test_add_func("/FSearch/database/entry/attribute_for_offset_roundtrip",
                    test_get_attribute_for_offset_and_set_attribute_for_offset);

    // Name & extension
    g_test_add_func("/FSearch/database/entry/name_raw", test_get_name_raw);
    g_test_add_func("/FSearch/database/entry/name_raw_for_display", test_get_name_raw_for_display_regular_name);
    g_test_add_func("/FSearch/database/entry/name_raw_for_display_empty_is_separator",
                    test_get_name_raw_for_display_empty_name_is_separator);
    g_test_add_func("/FSearch/database/entry/name_raw_for_display_null", test_get_name_raw_for_display_null_entry);
    g_test_add_func("/FSearch/database/entry/name_for_display_gstring", test_get_name_for_display_returns_gstring);
    g_test_add_func("/FSearch/database/entry/extension_regular", test_get_extension_regular);
    g_test_add_func("/FSearch/database/entry/extension_no_dot", test_get_extension_no_dot);
    g_test_add_func("/FSearch/database/entry/extension_hidden_file", test_get_extension_hidden_file);
    g_test_add_func("/FSearch/database/entry/extension_trailing_dot", test_get_extension_trailing_dot);
    g_test_add_func("/FSearch/database/entry/extension_folder_null", test_get_extension_folder_is_null);
    g_test_add_func("/FSearch/database/entry/extension_null_entry", test_get_extension_null_entry);

    // Mark
    g_test_add_func("/FSearch/database/entry/mark_default_unset", test_mark_default_is_unset);
    g_test_add_func("/FSearch/database/entry/mark_set_clear", test_mark_set_and_clear);
    g_test_add_func("/FSearch/database/entry/mark_null_entry", test_mark_null_entry_returns_zero);

    // set_name stub
    g_test_add_func("/FSearch/database/entry/set_name_noop", test_set_name_is_currently_a_noop);

    // Depth / parent / sibling / descendant
    g_test_add_func("/FSearch/database/entry/depth_root_zero", test_depth_root_is_zero);
    g_test_add_func("/FSearch/database/entry/depth_nesting", test_depth_increases_with_nesting);
    g_test_add_func("/FSearch/database/entry/parent_of_root_null", test_get_parent_of_root_is_null);
    g_test_add_func("/FSearch/database/entry/parent_null_entry", test_get_parent_null_entry_returns_null);
    g_test_add_func("/FSearch/database/entry/parent_returns_actual", test_get_parent_returns_actual_parent);
    g_test_add_func("/FSearch/database/entry/sibling_same_parent", test_is_sibling_same_parent);
    g_test_add_func("/FSearch/database/entry/sibling_different_parent", test_is_sibling_different_parent);
    g_test_add_func("/FSearch/database/entry/sibling_two_roots_false", test_is_sibling_two_roots_are_not_siblings);
    g_test_add_func("/FSearch/database/entry/descendant_direct_child", test_is_descendant_direct_child);
    g_test_add_func("/FSearch/database/entry/descendant_deeply_nested", test_is_descendant_deeply_nested);
    g_test_add_func("/FSearch/database/entry/descendant_unrelated", test_is_descendant_unrelated_entries);
    g_test_add_func("/FSearch/database/entry/descendant_not_self", test_is_descendant_entry_is_not_its_own_descendant);

    // Parenting
    g_test_add_func("/FSearch/database/entry/set_parent_updates_childcount", test_set_parent_updates_childcount);
    g_test_add_func("/FSearch/database/entry/set_parent_reparent_childcount",
                    test_set_parent_reparenting_decrements_old_increments_new);
    g_test_add_func("/FSearch/database/entry/set_parent_folder_num_folders", test_set_parent_folder_updates_num_folders);
    g_test_add_func("/FSearch/database/entry/set_parent_size_propagates_all_ancestors",
                    test_set_parent_propagates_size_to_all_ancestors);
    g_test_add_func("/FSearch/database/entry/set_parent_size_stops_without_flag",
                    test_set_parent_size_propagation_stops_at_ancestor_without_size_flag);
    g_test_add_func("/FSearch/database/entry/set_parent_reparent_size_both_sides",
                    test_set_parent_reparent_updates_size_on_both_sides);
    g_test_add_func("/FSearch/database/entry/set_parent_no_update_no_counts",
                    test_set_parent_no_update_does_not_touch_counts);
    g_test_add_func("/FSearch/database/entry/set_parent_update_childcount_no_size",
                    test_set_parent_update_childcount_does_not_touch_size);
    g_test_add_func("/FSearch/database/entry/set_parent_update_childcount_reparent",
                    test_set_parent_update_childcount_reparent_decrements_old);
    g_test_add_func("/FSearch/database/entry/increment_childcount_file_folder",
                    test_increment_childcount_file_and_folder);
    g_test_add_func("/FSearch/database/entry/increment_childcount_null_safe",
                    test_increment_childcount_null_entry_is_safe);

    // Paths
    g_test_add_func("/FSearch/database/entry/root_path_single_level", test_get_root_path_single_level);
    g_test_add_func("/FSearch/database/entry/root_path_nested", test_get_root_path_nested);
    g_test_add_func("/FSearch/database/entry/root_path_null", test_get_root_path_null_entry);
    g_test_add_func("/FSearch/database/entry/path_of_root_is_empty", test_get_path_of_root_entry_is_empty);
    g_test_add_func("/FSearch/database/entry/path_full_of_root_is_name", test_get_path_full_of_root_entry_is_its_name);
    g_test_add_func("/FSearch/database/entry/path_excludes_own_name", test_get_path_excludes_entry_own_name);
    g_test_add_func("/FSearch/database/entry/path_full_includes_own_name", test_get_path_full_includes_entry_own_name);
    g_test_add_func("/FSearch/database/entry/paths_root_is_separator", test_paths_when_root_name_is_separator);
    g_test_add_func("/FSearch/database/entry/append_path_preserves_prefix", test_append_path_appends_to_existing_content);
    g_test_add_func("/FSearch/database/entry/content_type_nonexistent_is_unknown",
                    test_append_content_type_of_nonexistent_path_is_unknown);

    // Comparators
    g_test_add_func("/FSearch/database/entry/compare_by_name_natural_order", test_compare_by_name_natural_order);
    g_test_add_func("/FSearch/database/entry/compare_by_name_null_entries", test_compare_by_name_null_entries_are_equal);
    g_test_add_func("/FSearch/database/entry/compare_by_size", test_compare_by_size);
    g_test_add_func("/FSearch/database/entry/compare_by_size_no_flag_zero",
                    test_compare_by_size_without_flag_defaults_to_zero);
    g_test_add_func("/FSearch/database/entry/compare_by_modification_time", test_compare_by_modification_time);
    g_test_add_func("/FSearch/database/entry/compare_by_extension", test_compare_by_extension);
    g_test_add_func("/FSearch/database/entry/compare_by_extension_folder_empty",
                    test_compare_by_extension_folder_treated_as_empty);
    g_test_add_func("/FSearch/database/entry/compare_by_position_always_zero", test_compare_by_position_always_zero);
    g_test_add_func("/FSearch/database/entry/compare_by_type_folders_always_equal",
                    test_compare_by_type_folders_always_equal);
    g_test_add_func("/FSearch/database/entry/compare_by_type_folder_file_differ",
                    test_compare_by_type_folder_and_file_differ);
    g_test_add_func("/FSearch/database/entry/compare_by_chain_type_then_name", test_compare_by_chain_type_then_name);
    g_test_add_func("/FSearch/database/entry/compare_by_path_siblings_name_order",
                    test_compare_by_path_siblings_use_name_order);
    g_test_add_func("/FSearch/database/entry/compare_by_path_different_depth", test_compare_by_path_different_depth);
    g_test_add_func("/FSearch/database/entry/compare_by_full_path_ancestor_order",
                    test_compare_by_full_path_orders_by_ancestor_names);

    // Deep copy
    g_test_add_func("/FSearch/database/entry/deep_copy_root", test_deep_copy_root_entry);
    g_test_add_func("/FSearch/database/entry/deep_copy_independent", test_deep_copy_is_independent_of_original);
    g_test_add_func("/FSearch/database/entry/deep_copy_parent_chain", test_deep_copy_duplicates_parent_chain);

    // Free variants
    g_test_add_func("/FSearch/database/entry/free_updates_parent_state", test_free_updates_parent_state);
    g_test_add_func("/FSearch/database/entry/free_no_unparent_leaves_parent_untouched",
                    test_free_no_unparent_leaves_parent_state_untouched);
    g_test_add_func("/FSearch/database/entry/free_full_frees_chain", test_free_full_frees_entire_chain);

    // Monitored flags
    g_test_add_func("/FSearch/database/entry/monitored_fanotify_roundtrip", test_monitored_fanotify_roundtrip_on_folder);
    g_test_add_func("/FSearch/database/entry/monitored_inotify_roundtrip", test_monitored_inotify_roundtrip_on_folder);
    g_test_add_func("/FSearch/database/entry/monitored_failed_folder", test_monitored_failed_flag_on_folder);
    g_test_add_func("/FSearch/database/entry/monitored_flags_file_redirect",
                    test_monitored_flags_on_file_redirect_to_parent);
    g_test_add_func("/FSearch/database/entry/monitored_flags_rootless_file_false",
                    test_monitored_flags_on_rootless_file_are_false);

    // Flags
    g_test_add_func("/FSearch/database/entry/get_flags_type_and_mark", test_get_flags_reflects_type_and_mark);

    return g_test_run();
}