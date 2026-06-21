/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#define G_LOG_DOMAIN "fsearch-database-scan-ntfs"

#include "fsearch_database_scan_ntfs.h"

#include "fsearch_database_entry.h"
#include "fsearch_database_entry_flags.h"

#include <config.h>

#include <errno.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Prevent ntfs-3g from redefining struct timespec */
#define __timespec_defined
#include <ntfs-3g/attrib.h>
#include <ntfs-3g/layout.h>
#include <ntfs-3g/mft.h>
#include <ntfs-3g/ntfstime.h>
#include <ntfs-3g/unistr.h>
#include <ntfs-3g/volume.h>

/* Maximum file name length to accept */
#define NTFS_MAX_NAME_LEN 255

/* MFT record numbers for system files */
#define NTFS_LAST_SYSTEM_MFT 11
#define NTFS_EXTEND_DIR_MFT  11

/*
 * Batch read size for MFT records.
 * Reading 1024 records at once (typically 1MB) reduces system call
 * overhead significantly on mechanical HDDs where the MFT is contiguous.
 */
#define NTFS_MFT_BATCH_SIZE 1024

typedef struct NtfsEntry NtfsEntry;

struct NtfsEntry {
    u64 mft_no;
    u64 parent_mft;
    char *name;
    bool is_dir;
    off_t size;
    time_t mtime;
    FsearchDatabaseEntry *entry;
};

typedef struct NtfsScanContext {
    ntfs_volume *vol;
    const char *mount_point;
    const char *device_path;
    GHashTable *mft_table;           /* mft_no (guint64) → NtfsEntry* */
    FsearchDatabaseEntry *root_entry; /* Mount point folder entry */
    DynamicArray *folders;
    DynamicArray *files;
    uint32_t index_id;
    GCancellable *cancellable;

    /* Statistics */
    uint64_t total_records;
    uint64_t used_records;
    uint64_t dir_count;
    uint64_t file_count;
    uint64_t system_skipped;
} NtfsScanContext;

static void
ntfs_entry_free(NtfsEntry *entry) {
    if (!entry) return;
    g_clear_pointer(&entry->name, g_free);
    /* entry->entry ownership is transferred to DynamicArray */
    entry->entry = NULL;
    g_free(entry);
}

static void
ntfs_scan_context_free(NtfsScanContext *ctx) {
    if (!ctx) return;
    if (ctx->mft_table) {
        g_hash_table_destroy(ctx->mft_table);
    }
    if (ctx->root_entry) {
        db_entry_free(ctx->root_entry);
    }
    if (ctx->vol) {
        ntfs_umount(ctx->vol, FALSE);
    }
}

/* Check if an MFT record number refers to a system file */
static inline bool
is_ntfs_system_file(u64 mft_no, u64 parent_mft) {
    if (mft_no <= NTFS_LAST_SYSTEM_MFT) return true;
    if (parent_mft == NTFS_EXTEND_DIR_MFT) return true;
    return false;
}

/* Convert NTFS time to Unix time_t */
static inline time_t
ntfs_time_to_unix(ntfs_time ntfs_t) {
    struct timespec ts = ntfs2timespec(ntfs_t);
    return (time_t)ts.tv_sec;
}

/* Convert UTF-16 file name from MFT to UTF-8 string */
static char *
ntfs_name_to_utf8(const ntfschar *name, u8 name_len) {
    /* ntfschar is uint16_t; MFT stores names in host byte order after ntfs_mount */
    gunichar2 *ucs2 = g_new(gunichar2, name_len);
    memcpy(ucs2, name, name_len * sizeof(uint16_t));

    gsize len_out = 0;
    gsize chars = name_len;
    GError *error = NULL;
    char *utf8 = g_utf16_to_utf8(ucs2, chars, &len_out, NULL, &error);
    g_free(ucs2);

    if (error) {
        g_debug("[ntfs_scan] UTF conversion failed: %s", error->message);
        g_error_free(error);
        /* Fallback: create a replacement string */
        return g_strdup_printf("<invalid-name-%d>", name_len);
    }
    return utf8;
}

/* Create FsearchDatabaseEntry for a folder */
static FsearchDatabaseEntry *
ntfs_create_folder_entry(const char *name,
                         time_t mtime,
                         FsearchDatabaseEntry *parent,
                         uint32_t index_id) {
    return db_entry_new_with_attributes(
        DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME | DATABASE_INDEX_PROPERTY_FLAG_SIZE,
        name, parent, DATABASE_ENTRY_TYPE_FOLDER,
        DATABASE_INDEX_PROPERTY_DB_INDEX, index_id,
        DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, mtime,
        DATABASE_INDEX_PROPERTY_NONE);
}

/* Create FsearchDatabaseEntry for a file */
static FsearchDatabaseEntry *
ntfs_create_file_entry(const char *name,
                       off_t size,
                       time_t mtime,
                       FsearchDatabaseEntry *parent) {
    return db_entry_new_with_attributes(
        DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME | DATABASE_INDEX_PROPERTY_FLAG_SIZE,
        name, parent, DATABASE_ENTRY_TYPE_FILE,
        DATABASE_INDEX_PROPERTY_SIZE, size,
        DATABASE_INDEX_PROPERTY_MODIFICATION_TIME, mtime,
        DATABASE_INDEX_PROPERTY_NONE);
}

/* Process a single MFT record; return true if a FILE_NAME was found and added */
static bool
process_mft_record(NtfsScanContext *ctx, MFT_RECORD *mrec, u64 mft_no) {
    /* Check magic */
    if (!ntfs_is_mft_record(mrec->magic)) {
        return false;
    }

    /* Check if in use */
    u16 flags = le16_to_cpu(mrec->flags);
    if (!(flags & MFT_RECORD_IN_USE)) {
        return false;
    }

    ctx->used_records++;
    bool is_dir = (flags & MFT_RECORD_IS_DIRECTORY) ? true : false;

    /* Walk attributes to find FILE_NAME */
    u32 bytes_in_use = le32_to_cpu(mrec->bytes_in_use);
    u16 attrs_offset = le16_to_cpu(mrec->attrs_offset);
    u8 *base = (u8 *)mrec;

    for (u32 ao = attrs_offset; ao < bytes_in_use; ) {
        ATTR_RECORD *attr = (ATTR_RECORD *)(base + ao);
        ATTR_TYPES attr_type = attr->type;
        u32 attr_len = le32_to_cpu(attr->length);

        if (attr_type == AT_END || attr_len == 0 || attr_len > bytes_in_use) {
            break;
        }

        if (attr_type != AT_FILE_NAME || attr->non_resident != 0) {
            ao += attr_len;
            continue;
        }

        u16 value_offset = le16_to_cpu(attr->value_offset);
        FILE_NAME_ATTR *fn = (FILE_NAME_ATTR *)(base + ao + value_offset);

        u8 name_len = fn->file_name_length;
        u8 name_type = fn->file_name_type;

        /* Skip pure DOS short names (type 2) */
        if (name_type == 2) {
            ao += attr_len;
            continue;
        }

        u64 parent_mft = MREF_LE(fn->parent_directory);

        /* Skip system files */
        if (is_ntfs_system_file(mft_no, parent_mft)) {
            ctx->system_skipped++;
            return false;
        }

        /* Skip overly long names */
        if (name_len == 0 || name_len > NTFS_MAX_NAME_LEN) {
            g_debug("[ntfs_scan] invalid name length %d at mft %" G_GUINT64_FORMAT,
                    name_len, mft_no);
            return false;
        }

        /* Convert name to UTF-8 */
        char *name = ntfs_name_to_utf8(fn->file_name, name_len);
        if (!name) {
            return false;
        }

        /* Extract file attributes from FILE_NAME_ATTR */
        off_t size = is_dir ? 0 : (off_t)sle64_to_cpu(fn->data_size);
        time_t mtime = ntfs_time_to_unix(fn->last_data_change_time);

        /* Create NtfsEntry */
        NtfsEntry *entry = g_new0(NtfsEntry, 1);
        entry->mft_no = mft_no;
        entry->parent_mft = parent_mft;
        entry->name = name;
        entry->is_dir = is_dir;
        entry->size = size;
        entry->mtime = mtime;
        entry->entry = NULL;

        g_hash_table_insert(ctx->mft_table,
                            GUINT_TO_POINTER(mft_no),
                            entry);

        if (is_dir) {
            ctx->dir_count++;
        } else {
            ctx->file_count++;
        }

        return true;
    }

    return false;
}

/* Phase 1: Scan all MFT records and populate hash table */
static bool
ntfs_scan_mft_phase1(NtfsScanContext *ctx) {
    ntfs_volume *vol = ctx->vol;

    /* Estimate total MFT records */
    ctx->total_records = vol->mft_na->data_size / vol->mft_record_size;
    g_debug("[ntfs_scan] estimated MFT records: %" G_GUINT64_FORMAT, ctx->total_records);

    size_t record_size = vol->mft_record_size;
    MFT_RECORD *batch = g_malloc(record_size * NTFS_MFT_BATCH_SIZE);
    if (!batch) {
        g_warning("[ntfs_scan] failed to allocate MFT batch buffer");
        return false;
    }

    for (u64 mft_no = 0; mft_no < ctx->total_records; ) {
        /* Check cancellation each batch */
        if (ctx->cancellable && g_cancellable_is_cancelled(ctx->cancellable)) {
            g_debug("[ntfs_scan] cancelled at mft_no %" G_GUINT64_FORMAT, mft_no);
            g_free(batch);
            return false;
        }

        s64 remaining = (s64)(ctx->total_records - mft_no);
        s64 count = remaining < NTFS_MFT_BATCH_SIZE ? remaining : NTFS_MFT_BATCH_SIZE;

        /* Batch read MFT records */
        if (ntfs_mft_records_read(vol, (MFT_REF)mft_no, count, batch) != 0) {
            mft_no += 1;
            continue;
        }

        /* Process each record in the batch */
        for (s64 i = 0; i < count; i++) {
            process_mft_record(ctx, &batch[i], mft_no + i);
        }

        mft_no += count;
    }

    g_free(batch);
    return true;
}

/* Phase 2: Create FsearchDatabaseEntry objects and build tree */
static bool
ntfs_scan_phase2(NtfsScanContext *ctx) {
    /* Create root folder entry (mount point) */
    ctx->root_entry = ntfs_create_folder_entry(ctx->mount_point,
                                                0,
                                                NULL,
                                                ctx->index_id);
    if (!ctx->root_entry) {
        g_warning("[ntfs_scan] failed to create root entry");
        return false;
    }
    darray_add_item(ctx->folders, ctx->root_entry);

    /* First pass: create all entries and add to arrays */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, ctx->mft_table);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        NtfsEntry *entry = (NtfsEntry *)value;

        /* Create database entry with NULL parent (will set in second pass) */
        if (entry->is_dir) {
            entry->entry = ntfs_create_folder_entry(entry->name,
                                                     entry->mtime,
                                                     NULL,
                                                     ctx->index_id);
        } else {
            entry->entry = ntfs_create_file_entry(entry->name,
                                                   entry->size,
                                                   entry->mtime,
                                                   NULL);
        }

        if (!entry->entry) {
            g_warning("[ntfs_scan] failed to create entry for \"%s\" (mft %" G_GUINT64_FORMAT ")",
                      entry->name, entry->mft_no);
            continue;
        }

        if (entry->is_dir) {
            darray_add_item(ctx->folders, entry->entry);
        } else {
            darray_add_item(ctx->files, entry->entry);
        }
    }

    /* Second pass: set parent pointers */
    g_hash_table_iter_init(&iter, ctx->mft_table);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        NtfsEntry *entry = (NtfsEntry *)value;
        if (!entry->entry) continue;

        /* Look up parent entry */
        NtfsEntry *parent_ntfs = g_hash_table_lookup(ctx->mft_table,
                                                       GUINT_TO_POINTER(entry->parent_mft));

        FsearchDatabaseEntry *parent;
        if (parent_ntfs && parent_ntfs->entry) {
            parent = parent_ntfs->entry;
        } else {
            /* Parent is system file or missing; attach to mount point root */
            parent = ctx->root_entry;
        }

        db_entry_set_parent_no_update(entry->entry, parent);
    }

    return true;
}

bool
db_scan_ntfs(const char *device_path,
             const char *mount_point,
             DynamicArray *folders,
             DynamicArray *files,
             uint32_t index_id,
             GCancellable *cancellable) {
    g_assert(g_path_is_absolute(device_path));
    g_assert(g_path_is_absolute(mount_point));
    g_debug("[ntfs_scan] scanning device %s mounted at %s", device_path, mount_point);

    NtfsScanContext ctx = {
        .vol = NULL,
        .mount_point = mount_point,
        .device_path = device_path,
        .mft_table = NULL,
        .root_entry = NULL,
        .folders = folders,
        .files = files,
        .index_id = index_id,
        .cancellable = cancellable,
    };

    /* Mount NTFS volume */
    ctx.vol = ntfs_mount(device_path, NTFS_MNT_NONE);
    if (!ctx.vol) {
        g_warning("[ntfs_scan] ntfs_mount failed for %s: %s", device_path, strerror(errno));
        return false;
    }
    g_debug("[ntfs_scan] NTFS version: %u.%u, MFT record size: %u",
            ctx.vol->major_ver, ctx.vol->minor_ver, ctx.vol->mft_record_size);

    /* Create hash table for MFT entries */
    ctx.mft_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                           (GDestroyNotify)ntfs_entry_free);

    /* Phase 1: Scan MFT records */
    if (!ntfs_scan_mft_phase1(&ctx)) {
        g_warning("[ntfs_scan] MFT scan failed");
        ntfs_scan_context_free(&ctx);
        return false;
    }

    g_debug("[ntfs_scan] phase 1 complete: %" G_GUINT64_FORMAT " entries "
            "(%" G_GUINT64_FORMAT " dirs, %" G_GUINT64_FORMAT " files, "
            "%" G_GUINT64_FORMAT " system skipped)",
            ctx.dir_count + ctx.file_count,
            ctx.dir_count, ctx.file_count, ctx.system_skipped);

    /* Phase 2: Create entries and build tree */
    if (!ntfs_scan_phase2(&ctx)) {
        g_warning("[ntfs_scan] entry creation failed");
        ntfs_scan_context_free(&ctx);
        return false;
    }

    g_debug("[ntfs_scan] complete: %" G_GUINT64_FORMAT " dirs, %" G_GUINT64_FORMAT " files",
            ctx.dir_count, ctx.file_count);

    /* Don't free root_entry — it's already in the folders array and
       entries reference it as their parent. Clear the pointer first. */
    ctx.root_entry = NULL;
    ntfs_scan_context_free(&ctx);
    return true;
}
