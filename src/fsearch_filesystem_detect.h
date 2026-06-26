#pragma once

#include <glib.h>

/**
 * FsearchPartitionInfo:
 * @device: Block device path (e.g., "/dev/sda1")
 * @mountpoint: Mount point path (e.g., "/mnt/data")
 * @fstype: File system type (e.g., "fuseblk")
 *
 * Holds information about a detected partition.
 */
typedef struct {
    char *device;
    char *mountpoint;
    char *fstype;
} FsearchPartitionInfo;

/**
 * fs_detect_ntfs_partitions:
 *
 * Detects NTFS partitions by finding the intersection of:
 * - fstab configured entries
 * - Currently mounted file systems
 * - ntfs-3g FUSE driver (fuse.ntfs-3g)
 *
 * Returns: (element-type FsearchPartitionInfo): A newly allocated
 *          #GPtrArray of #FsearchPartitionInfo structures, or %NULL
 *          if detection failed. The caller is responsible for freeing
 *          the array and its elements using fs_partition_info_free().
 */
GPtrArray *
fs_detect_ntfs_partitions(void);

/**
 * fs_partition_info_free:
 * @info: (not nullable): A #FsearchPartitionInfo to free
 *
 * Frees a single #FsearchPartitionInfo structure.
 */
void
fs_partition_info_free(FsearchPartitionInfo *info);

/**
 * fs_partition_array_free:
 * @array: (not nullable): A #GPtrArray of #FsearchPartitionInfo to free
 *
 * Frees a #GPtrArray and all its #FsearchPartitionInfo elements.
 */
void
fs_partition_array_free(GPtrArray *array);

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
fs_path_is_on_ntfs_mount(const char *path);
