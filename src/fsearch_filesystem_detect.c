#define G_LOG_DOMAIN "fsearch-filesystem-detect"

#include "fsearch_filesystem_detect.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>

/* Maximum line length for /proc/mounts lines */
#define MAX_LINE_LENGTH 1024

/**
 * fs_partition_info_new:
 * @device: Block device path
 * @mountpoint: Mount point path
 * @fstype: File system type
 *
 * Creates a new #FsearchPartitionInfo structure.
 *
 * Returns: (transfer full): A newly allocated #FsearchPartitionInfo
 */
static FsearchPartitionInfo *
fs_partition_info_new(const char *device, const char *mountpoint, const char *fstype) {
    FsearchPartitionInfo *info = g_new0(FsearchPartitionInfo, 1);
    info->device = g_strdup(device);
    info->mountpoint = g_strdup(mountpoint);
    info->fstype = g_strdup(fstype);
    return info;
}

void
fs_partition_info_free(FsearchPartitionInfo *info) {
    if (!info) return;
    g_free(info->device);
    g_free(info->mountpoint);
    g_free(info->fstype);
    g_free(info);
}

void
fs_partition_array_free(GPtrArray *array) {
    if (!array) return;
    /* g_ptr_array_new_with_free_func already set up auto-destruction */
    g_ptr_array_free(array, TRUE);
}

/**
 * Verifies whether a block device is an NTFS volume using blkid.
 *
 * Runs `blkid <device>` and checks for TYPE="ntfs" in the output.
 *
 * Returns: %TRUE if the device is an NTFS volume, %FALSE otherwise
 */
static gboolean
verify_ntfs_with_blkid(const char *device) {
    g_autofree char *cmd = g_strdup_printf("blkid -s TYPE -o value %s 2>/dev/null", device);
    g_autofree char *output = NULL;
    gint exit_status;

    if (!g_spawn_command_line_sync(cmd, &output, NULL, &exit_status, NULL)) {
        g_debug("[fs_detect] failed to spawn blkid for %s", device);
        return FALSE;
    }

    /* g_spawn_command_line_sync appends a newline; strip trailing whitespace */
    g_strchomp(output);

    return g_str_equal(output, "ntfs");
}

/**
 * Parses /proc/mounts to extract currently mounted fuseblk entries.
 *
 * Returns: (element-type FsearchPartitionInfo) (transfer full): A #GPtrArray
 *          of currently mounted fuseblk partitions, or %NULL on failure.
 */
static GPtrArray *
parse_proc_mounts(void) {
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)fs_partition_info_free);
    FILE *file = fopen("/proc/mounts", "r");
    if (!file) {
        g_warning("[fs_detect] Failed to open /proc/mounts");
        return result;
    }

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        char device[MAX_LINE_LENGTH] = {0};
        char mountpoint[MAX_LINE_LENGTH] = {0};
        char fstype[MAX_LINE_LENGTH] = {0};

        /* Parse: device mountpoint fstype options dump pass */
        if (sscanf(line, "%s %s %s", device, mountpoint, fstype) == 3) {
            /* Only include fuseblk (what ntfs-3g appears as in /proc/mounts) */
            if (g_str_equal(fstype, "fuseblk")) {
                FsearchPartitionInfo *info = fs_partition_info_new(device, mountpoint, fstype);
                g_ptr_array_add(result, info);
            }
        }
    }

    fclose(file);
    return result;
}

/**
 * Detects NTFS partitions by scanning /proc/mounts for fuseblk entries
 * and verifying each one with blkid.
 *
 * Returns: (element-type FsearchPartitionInfo) (transfer full): A #GPtrArray
 *          of NTFS partitions that are currently mounted via ntfs-3g FUSE,
 *          or an empty array if none found.
 */
GPtrArray *
fs_detect_ntfs_partitions(void) {
    GPtrArray *fuseblk_entries = parse_proc_mounts();
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)fs_partition_info_free);

    for (guint i = 0; i < fuseblk_entries->len; i++) {
        FsearchPartitionInfo *entry = g_ptr_array_index(fuseblk_entries, i);

        /* Verify this fuseblk device is actually NTFS */
        if (verify_ntfs_with_blkid(entry->device)) {
            g_debug("[fs_detect] NTFS partition found: %s -> %s",
                    entry->device, entry->mountpoint);
            FsearchPartitionInfo *info = fs_partition_info_new(
                entry->device,
                entry->mountpoint,
                "fuseblk"
            );
            g_ptr_array_add(result, info);
        } else {
            g_debug("[fs_detect] skipping non-NTFS fuseblk: %s -> %s",
                    entry->device, entry->mountpoint);
        }
    }

    fs_partition_array_free(fuseblk_entries);
    return result;
}

/**
 * fs_path_is_on_ntfs_mount:
 * @path: (not nullable): An absolute file system path
 *
 * Checks whether @path is on an NTFS mount point by calling
 * fs_detect_ntfs_partitions() and matching against mount points.
 *
 * Returns: A newly allocated #FsearchPartitionInfo if the path is on
 *          an NTFS mount, or %NULL otherwise. The caller must free
 *          the result using fs_partition_info_free().
 */
FsearchPartitionInfo *
fs_path_is_on_ntfs_mount(const char *path) {
    g_return_val_if_fail(path, NULL);

    GPtrArray *partitions = fs_detect_ntfs_partitions();
    if (!partitions || partitions->len == 0) {
        return NULL;
    }

    /* Ensure path is normalized (no trailing slash, unless root) */
    g_autofree char *normalized = g_strdup(path);
    gsize len = strlen(normalized);
    while (len > 1 && normalized[len - 1] == '/') {
        normalized[--len] = '\0';
    }

    /* Find matching mount point (longest prefix match) */
    FsearchPartitionInfo *best_match = NULL;
    gsize best_len = 0;

    for (guint i = 0; i < partitions->len; i++) {
        FsearchPartitionInfo *info = g_ptr_array_index(partitions, i);
        gsize mp_len = strlen(info->mountpoint);

        /* Path must equal or start with mount point */
        if (strlen(normalized) >= mp_len &&
            strncmp(normalized, info->mountpoint, mp_len) == 0) {
            /* Check it's a proper path boundary: mountpoint is exact match,
               or next char in path is '/' */
            if (strlen(normalized) == mp_len || normalized[mp_len] == '/') {
                if (mp_len > best_len) {
                    best_len = mp_len;
                    best_match = info;
                }
            }
        }
    }

    FsearchPartitionInfo *result = NULL;
    if (best_match) {
        result = fs_partition_info_new(best_match->device,
                                        best_match->mountpoint,
                                        best_match->fstype);
    }

    fs_partition_array_free(partitions);
    return result;
}
