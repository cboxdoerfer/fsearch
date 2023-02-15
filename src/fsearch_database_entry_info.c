#include "fsearch_database_entry_info.h"

#include "fsearch_file_utils.h"

#include <glib.h>
#include <string.h>

struct _FsearchDatabaseEntryInfo {
    GArray *infos;
    FsearchDatabaseEntryInfoFlags flags;
    volatile gint ref_count;
};

typedef enum {
    ENTRY_INFO_ID_NAME,
    ENTRY_INFO_ID_PATH,
    ENTRY_INFO_ID_PATH_FULL,
    ENTRY_INFO_ID_SIZE,
    ENTRY_INFO_ID_MODIFICATION_TIME,
    ENTRY_INFO_ID_ACCESS_TIME,
    ENTRY_INFO_ID_CREATION_TIME,
    ENTRY_INFO_ID_STATUS_CHANGE_TIME,
    ENTRY_INFO_ID_ICON,
    ENTRY_INFO_ID_SELECTED,
    ENTRY_INFO_ID_INDEX,
    ENTRY_INFO_ID_EXTENSION,
    ENTRY_INFO_ID_HIGHLIGHTS,
    NUM_ENTRY_INFO_IDS,
} FsearchDatabaseEntryInfoID;

typedef struct {
    FsearchDatabaseEntryInfoID id;
    union {
        FsearchDatabaseEntryType type;
        GHashTable *highlights;
        GString *str;
        GIcon *icon;
        size_t size;
        time_t time;
        uint32_t uint;
        bool selected;
    };
} FsearchDatabaseEntryInfoValue;

G_DEFINE_BOXED_TYPE(FsearchDatabaseEntryInfo,
                    fsearch_database_entry_info,
                    fsearch_database_entry_info_ref,
                    fsearch_database_entry_info_unref)

static void
entry_info_value_clear(FsearchDatabaseEntryInfoValue *value) {
    switch (value->id) {
    case ENTRY_INFO_ID_NAME:
    case ENTRY_INFO_ID_PATH:
    case ENTRY_INFO_ID_PATH_FULL:
    case ENTRY_INFO_ID_EXTENSION:
        g_string_free(g_steal_pointer(&value->str), TRUE);
        break;
    case ENTRY_INFO_ID_ICON:
        g_clear_object(&value->icon);
        break;
    case ENTRY_INFO_ID_HIGHLIGHTS:
        g_clear_pointer(&value->highlights, g_hash_table_unref);
        break;
    default:
        break;
    }
}

static FsearchDatabaseEntryInfoValue *
get_value(FsearchDatabaseEntryInfo *info, FsearchDatabaseEntryInfoID id) {
    for (uint32_t i = 0; i < info->infos->len; i++) {
        FsearchDatabaseEntryInfoValue *val = &g_array_index(info->infos, FsearchDatabaseEntryInfoValue, i);
        if (id == val->id) {
            return val;
        }
    }
    return NULL;
}

static uint32_t
num_flags_set(FsearchDatabaseEntryInfoFlags flags) {
    uint32_t num_flags = 0;
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_NAME) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH_FULL) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SIZE) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_MODIFICATION_TIME) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_ACCESS_TIME) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_CREATION_TIME) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_STATUS_CHANGE_TIME) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_ICON) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_INDEX) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_EXTENSION) {
        num_flags++;
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_HIGHLIGHTS) {
        num_flags++;
    }
    return num_flags;
}

static void
entry_info_free(FsearchDatabaseEntryInfo *info) {
    g_clear_pointer(&info->infos, g_array_unref);
    g_clear_pointer(&info, free);
}

FsearchDatabaseEntryInfo *
fsearch_database_entry_info_ref(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info != NULL, NULL);
    g_return_val_if_fail(info->ref_count > 0, NULL);

    g_atomic_int_inc(&info->ref_count);

    return info;
}

void
fsearch_database_entry_info_unref(FsearchDatabaseEntryInfo *info) {
    g_return_if_fail(info != NULL);
    g_return_if_fail(info->ref_count > 0);

    if (g_atomic_int_dec_and_test(&info->ref_count)) {
        g_clear_pointer(&info, entry_info_free);
    }
}

FsearchDatabaseEntryInfo *
fsearch_database_entry_info_new(FsearchDatabaseEntry *entry,
                                FsearchQuery *query,
                                uint32_t idx,
                                bool is_selected,
                                FsearchDatabaseEntryInfoFlags flags) {
    FsearchDatabaseEntryInfo *info = calloc(1, sizeof(FsearchDatabaseEntryInfo));
    g_assert(info);

    info->flags = flags;

    const uint32_t num_flags = num_flags_set(info->flags);
    if (num_flags == 0) {
        return info;
    }

    info->infos = g_array_sized_new(FALSE, TRUE, sizeof(FsearchDatabaseEntryInfoValue), num_flags);
    g_array_set_clear_func(info->infos, (GDestroyNotify)entry_info_value_clear);

    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_NAME) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_NAME;
        val.str = db_entry_get_name_for_display(entry);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_PATH;
        val.str = db_entry_get_path(entry);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH_FULL) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_PATH_FULL;
        val.str = db_entry_get_path_full(entry);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SIZE) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_SIZE;
        val.size = db_entry_get_size(entry);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_MODIFICATION_TIME) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_MODIFICATION_TIME;
        val.time = db_entry_get_mtime(entry);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_ACCESS_TIME) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_ACCESS_TIME;
        // TODO: implement atime
        val.time = 0;
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_CREATION_TIME) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_CREATION_TIME;
        val.time = 0;
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_STATUS_CHANGE_TIME) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_STATUS_CHANGE_TIME;
        val.time = 0;
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_ICON) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_ICON;
        g_autoptr(GString) path = db_entry_get_path_full(entry);
        val.icon = fsearch_file_utils_get_icon_for_path(path->str);
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_SELECTED;
        val.selected = is_selected;
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_INDEX) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_INDEX;
        val.uint = idx;
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_EXTENSION) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_EXTENSION;
        val.str = g_string_new(db_entry_get_extension(entry));
        g_array_append_val(info->infos, val);
    }
    if (flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_HIGHLIGHTS && query) {
        FsearchDatabaseEntryInfoValue val = {0};
        val.id = ENTRY_INFO_ID_HIGHLIGHTS;
        FsearchQueryMatchData *match_data = fsearch_query_match_data_new();
        fsearch_query_match_data_set_entry(match_data, entry);
        fsearch_query_highlight(query, match_data);
        val.highlights = fsearch_query_match_data_get_highlights(match_data);
        g_clear_pointer(&match_data, fsearch_query_match_data_free);
        g_array_append_val(info->infos, val);
    }

    info->ref_count = 1;

    return info;
}

GString *
fsearch_database_entry_info_get_name(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_NAME, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_NAME);
    g_return_val_if_fail(val, NULL);
    return val->str;
}

GString *
fsearch_database_entry_info_get_path(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_PATH);
    g_return_val_if_fail(val, NULL);
    return val->str;
}

GString *
fsearch_database_entry_info_get_extension(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_EXTENSION, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_EXTENSION);
    g_return_val_if_fail(val, NULL);
    return val->str;
}

GString *
fsearch_database_entry_info_get_path_full(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_PATH_FULL, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_PATH_FULL);
    g_return_val_if_fail(val, NULL);
    return val->str;
}

time_t
fsearch_database_entry_info_get_mtime(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, 0);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_MODIFICATION_TIME, 0);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_MODIFICATION_TIME);
    g_return_val_if_fail(val, 0);
    return val->time;
}

size_t
fsearch_database_entry_info_get_size(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, 0);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SIZE, 0);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_SIZE);
    g_return_val_if_fail(val, 0);
    return val->size;
}

GIcon *
fsearch_database_entry_info_get_icon(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_ICON, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_ICON);
    g_return_val_if_fail(val, NULL);
    return val->icon;
}

uint32_t
fsearch_database_entry_info_get_index(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, 0);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_INDEX, 0);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_INDEX);
    g_return_val_if_fail(val, 0);
    return val->uint;
}

bool
fsearch_database_entry_info_get_selected(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, 0);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_SELECTED, 0);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_SELECTED);
    g_return_val_if_fail(val, 0);
    return val->selected;
}

GHashTable *
fsearch_database_entry_info_get_highlights(FsearchDatabaseEntryInfo *info) {
    g_return_val_if_fail(info, NULL);
    g_return_val_if_fail(info->flags & FSEARCH_DATABASE_ENTRY_INFO_FLAG_HIGHLIGHTS, NULL);
    FsearchDatabaseEntryInfoValue *val = get_value(info, ENTRY_INFO_ID_HIGHLIGHTS);
    g_return_val_if_fail(val, NULL);
    return val->highlights;
}
