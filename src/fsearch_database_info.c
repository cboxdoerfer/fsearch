#include "fsearch_database_info.h"

struct _FsearchDatabaseInfo {
    uint32_t num_files;
    uint32_t num_folders;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseInfo, fsearch_database_info, fsearch_database_info_ref, fsearch_database_info_unref)

FsearchDatabaseInfo *
fsearch_database_info_ref(FsearchDatabaseInfo *info) {
    g_return_val_if_fail(info != NULL, NULL);
    g_return_val_if_fail(info->ref_count > 0, NULL);

    g_atomic_int_inc(&info->ref_count);

    return info;
}

void
fsearch_database_info_unref(FsearchDatabaseInfo *info) {
    g_return_if_fail(info != NULL);
    g_return_if_fail(info->ref_count > 0);

    if (g_atomic_int_dec_and_test(&info->ref_count)) {
        g_clear_pointer(&info, g_free);
    }
}

FsearchDatabaseInfo *
fsearch_database_info_new(uint32_t num_files, uint32_t num_folders) {
    FsearchDatabaseInfo *info = calloc(1, sizeof(FsearchDatabaseInfo));
    g_assert(info);

    info->num_files = num_files;
    info->num_folders = num_folders;

    info->ref_count = 1;

    return info;
}

uint32_t
fsearch_database_info_get_num_files(FsearchDatabaseInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files;
}

uint32_t
fsearch_database_info_get_num_folders(FsearchDatabaseInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files;
}

uint32_t
fsearch_database_info_get_num_entries(FsearchDatabaseInfo *info) {
    g_return_val_if_fail(info, 0);
    return info->num_files + info->num_folders;
}
