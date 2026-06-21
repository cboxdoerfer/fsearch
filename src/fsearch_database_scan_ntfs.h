#pragma once

#include "fsearch_array.h"
#include "fsearch_database_exclude_manager.h"

#include <glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

/**
 * db_scan_ntfs:
 * @device_path:    Block device path (e.g. "/dev/sda1")
 * @mount_point:    NTFS mount point path (e.g. "/mnt/data")
 * @folders:        Output DynamicArray for folder entries
 * @files:          Output DynamicArray for file entries
 * @exclude_manager: Exclude manager for filtering
 * @index_id:       Database index ID
 * @cancellable:    GCancellable for cancellation support
 *
 * Scan an NTFS volume by reading the MFT directly via libntfs-3g,
 * bypassing the FUSE layer. Creates FsearchDatabaseEntry objects
 * and adds them to @folders and @files.
 *
 * Returns: true on success, false on error.
 */
bool
db_scan_ntfs(const char *device_path,
             const char *mount_point,
             DynamicArray *folders,
             DynamicArray *files,
             FsearchDatabaseExcludeManager *exclude_manager,
             uint32_t index_id,
             GCancellable *cancellable);

G_END_DECLS
