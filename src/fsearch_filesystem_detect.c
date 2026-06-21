#define G_LOG_DOMAIN "fsearch-filesystem-detect"

#include "fsearch_filesystem_detect.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>

/* Maximum line length for fstab/mounts files */
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
 * Parses /etc/fstab to extract NTFS entries.
 *
 * Returns: (element-type FsearchPartitionInfo) (transfer full): A #GPtrArray
 *          of fstab-configured NTFS partitions, or %NULL on failure.
 */
static GPtrArray *
parse_fstab(void) {
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)fs_partition_info_free);
    FILE *file = fopen("/etc/fstab", "r");
    if (!file) {
        g_warning("Failed to open /etc/fstab");
        return result;
    }

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\n') continue;

        char device[MAX_LINE_LENGTH] = {0};
        char mountpoint[MAX_LINE_LENGTH] = {0};
        char fstype[MAX_LINE_LENGTH] = {0};

        /* Parse: device mountpoint fstype options dump pass */
        if (sscanf(line, "%s %s %s", device, mountpoint, fstype) == 3) {
            /* Only include ntfs-3g file systems */
            if (g_str_equal(fstype, "ntfs-3g")) {
                FsearchPartitionInfo *info = fs_partition_info_new(device, mountpoint, fstype);
                g_ptr_array_add(result, info);
            }
        }
    }

    fclose(file);
    return result;
}

/**
 * Parses /proc/mounts to extract currently mounted NTFS entries.
 *
 * Returns: (element-type FsearchPartitionInfo) (transfer full): A #GPtrArray
 *          of currently mounted NTFS partitions, or %NULL on failure.
 */
static GPtrArray *
parse_proc_mounts(void) {
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)fs_partition_info_free);
    FILE *file = fopen("/proc/mounts", "r");
    if (!file) {
        g_warning("Failed to open /proc/mounts");
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
 * Finds the intersection of fstab and currently mounted NTFS partitions.
 *
 * Returns: (element-type FsearchPartitionInfo) (transfer full): A #GPtrArray
 *          containing partitions that are both in fstab and currently mounted,
 *          or an empty array if none found.
 */
GPtrArray *
fs_detect_ntfs_partitions(void) {
    GPtrArray *fstab_entries = parse_fstab();
    GPtrArray *mounted_entries = parse_proc_mounts();
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)fs_partition_info_free);

    /* Find intersection: entries that exist in both fstab and /proc/mounts */
    for (guint i = 0; i < fstab_entries->len; i++) {
        FsearchPartitionInfo *fstab_entry = g_ptr_array_index(fstab_entries, i);

        for (guint j = 0; j < mounted_entries->len; j++) {
            FsearchPartitionInfo *mounted_entry = g_ptr_array_index(mounted_entries, j);

            /* Match by mountpoint */
            if (g_str_equal(fstab_entry->mountpoint, mounted_entry->mountpoint)) {
                /* Use mounted entry's actual device and fstype */
                FsearchPartitionInfo *info = fs_partition_info_new(
                    mounted_entry->device,
                    mounted_entry->mountpoint,
                    mounted_entry->fstype
                );
                g_ptr_array_add(result, info);
                break;
            }
        }
    }

    /* Clean up */
    fs_partition_array_free(fstab_entries);
    fs_partition_array_free(mounted_entries);

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
