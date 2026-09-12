#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#define DATABASE_INDEX_PROPERTY_NAME_STRING "Name"
#define DATABASE_INDEX_PROPERTY_PATH_STRING "Path"
#define DATABASE_INDEX_PROPERTY_SIZE_STRING "Size"
#define DATABASE_INDEX_PROPERTY_MODIFICATION_TIME_STRING "Date Modified"
#define DATABASE_INDEX_PROPERTY_FILETYPE_STRING "Type"
#define DATABASE_INDEX_PROPERTY_EXTENSION_STRING "Extension"

// WARNING: Do not change the order!
// Changing the order will break the database file format
typedef enum {
    DATABASE_INDEX_PROPERTY_FLAG_NAME = 1 << 0,
    DATABASE_INDEX_PROPERTY_FLAG_PATH = 1 << 1,
    DATABASE_INDEX_PROPERTY_FLAG_SIZE = 1 << 2,
    DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME = 1 << 3,
    DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME = 1 << 4,
    DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME = 1 << 5,
    DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME = 1 << 6,
    DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES = 1 << 7,
    DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS = 1 << 8,
    DATABASE_INDEX_PROPERTY_FLAG_FILETYPE = 1 << 9,
    DATABASE_INDEX_PROPERTY_FLAG_EXTENSION = 1 << 10,
    DATABASE_INDEX_PROPERTY_FLAG_ALL = DATABASE_INDEX_PROPERTY_FLAG_NAME | DATABASE_INDEX_PROPERTY_FLAG_PATH
                                     | DATABASE_INDEX_PROPERTY_FLAG_SIZE | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME
                                     | DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME | DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME
                                     | DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME
                                     | DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES | DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS
                                     | DATABASE_INDEX_PROPERTY_FLAG_FILETYPE | DATABASE_INDEX_PROPERTY_FLAG_EXTENSION,
    DATABASE_INDEX_PROPERTY_FLAG_NONE = 0,
} FsearchDatabaseIndexPropertyFlags;

// The default set of indexed properties.
// TODO: At one point this might be worth making configurable
#define DATABASE_INDEX_PROPERTY_FLAG_DEFAULT                                                                            \
    (DATABASE_INDEX_PROPERTY_FLAG_NAME | DATABASE_INDEX_PROPERTY_FLAG_PATH | DATABASE_INDEX_PROPERTY_FLAG_SIZE          \
     | DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME)

typedef enum {
    DATABASE_INDEX_PROPERTY_NONE,
    DATABASE_INDEX_PROPERTY_NAME,
    DATABASE_INDEX_PROPERTY_PATH,
    DATABASE_INDEX_PROPERTY_PATH_FULL,
    DATABASE_INDEX_PROPERTY_SIZE,
    DATABASE_INDEX_PROPERTY_MODIFICATION_TIME,
    DATABASE_INDEX_PROPERTY_ACCESS_TIME,
    DATABASE_INDEX_PROPERTY_CREATION_TIME,
    DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME,
    DATABASE_INDEX_PROPERTY_NUM_FILES,
    DATABASE_INDEX_PROPERTY_NUM_FOLDERS,
    DATABASE_INDEX_PROPERTY_FILETYPE,
    DATABASE_INDEX_PROPERTY_EXTENSION,
    NUM_DATABASE_INDEX_PROPERTIES,
} FsearchDatabaseIndexProperty;

// An explicit, ordered chain of properties describing the full comparator a sorted array is
// actually ordered by, e.g. [TYPE, SIZE, NAME, PATH]. Used instead of a single "secondary sort
// order" property because a manual (non-fast-indexed) sort layers on top of whatever order the
// array already had, which can itself be several properties deep.
#define FSEARCH_DATABASE_SORT_ORDER_CHAIN_MAX_LENGTH NUM_DATABASE_INDEX_PROPERTIES

typedef struct FsearchDatabaseSortOrderChain {
    FsearchDatabaseIndexProperty properties[FSEARCH_DATABASE_SORT_ORDER_CHAIN_MAX_LENGTH];
    uint32_t length;
} FsearchDatabaseSortOrderChain;

// not for display in the UI (not translated)
static inline const char *
fsearch_database_index_property_to_string(FsearchDatabaseIndexProperty property) {
    switch (property) {
    case DATABASE_INDEX_PROPERTY_NONE:
        return "none";
    case DATABASE_INDEX_PROPERTY_NAME:
        return "name";
    case DATABASE_INDEX_PROPERTY_PATH:
        return "path";
    case DATABASE_INDEX_PROPERTY_PATH_FULL:
        return "full path";
    case DATABASE_INDEX_PROPERTY_SIZE:
        return "size";
    case DATABASE_INDEX_PROPERTY_MODIFICATION_TIME:
        return "mtime";
    case DATABASE_INDEX_PROPERTY_ACCESS_TIME:
        return "atime";
    case DATABASE_INDEX_PROPERTY_CREATION_TIME:
        return "ctime";
    case DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME:
        return "status change time";
    case DATABASE_INDEX_PROPERTY_NUM_FILES:
        return "number of files";
    case DATABASE_INDEX_PROPERTY_NUM_FOLDERS:
        return "number of folders";
    case DATABASE_INDEX_PROPERTY_FILETYPE:
        return "type";
    case DATABASE_INDEX_PROPERTY_EXTENSION:
        return "extension";
    case NUM_DATABASE_INDEX_PROPERTIES:
        break;
    }
    return "unknown";
}

static inline bool
fsearch_database_index_property_is_set(FsearchDatabaseIndexPropertyFlags flags, FsearchDatabaseIndexProperty property) {
    static const FsearchDatabaseIndexPropertyFlags prop_to_flag[NUM_DATABASE_INDEX_PROPERTIES] = {
        [DATABASE_INDEX_PROPERTY_NAME] = DATABASE_INDEX_PROPERTY_FLAG_NAME,
        [DATABASE_INDEX_PROPERTY_PATH] = DATABASE_INDEX_PROPERTY_FLAG_PATH,
        [DATABASE_INDEX_PROPERTY_SIZE] = DATABASE_INDEX_PROPERTY_FLAG_SIZE,
        [DATABASE_INDEX_PROPERTY_MODIFICATION_TIME] = DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME,
        [DATABASE_INDEX_PROPERTY_ACCESS_TIME] = DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME,
        [DATABASE_INDEX_PROPERTY_CREATION_TIME] = DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME,
        [DATABASE_INDEX_PROPERTY_STATUS_CHANGE_TIME] = DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME,
        [DATABASE_INDEX_PROPERTY_NUM_FILES] = DATABASE_INDEX_PROPERTY_FLAG_NUM_FILES,
        [DATABASE_INDEX_PROPERTY_NUM_FOLDERS] = DATABASE_INDEX_PROPERTY_FLAG_NUM_FOLDERS,
        [DATABASE_INDEX_PROPERTY_FILETYPE] = DATABASE_INDEX_PROPERTY_FLAG_FILETYPE,
        [DATABASE_INDEX_PROPERTY_EXTENSION] = DATABASE_INDEX_PROPERTY_FLAG_EXTENSION,
    };

    if (G_UNLIKELY(property <= DATABASE_INDEX_PROPERTY_NONE || property >= NUM_DATABASE_INDEX_PROPERTIES)) {
        return false;
    }

    const FsearchDatabaseIndexPropertyFlags target_flag = prop_to_flag[property];

    return (target_flag != 0) && ((flags & target_flag) != 0);
}

// Whether an update touching `affected_sort_orders` can affect the position of any entry
// currently ordered by `chain` -- i.e. whether any level of the chain (not just the primary
// property) is among the affected properties.
static inline bool
fsearch_database_sort_order_chain_is_affected(const FsearchDatabaseSortOrderChain *chain,
                                              FsearchDatabaseIndexPropertyFlags affected_sort_orders) {
    for (uint32_t i = 0; i < chain->length; ++i) {
        if (fsearch_database_index_property_is_set(affected_sort_orders, chain->properties[i])) {
            return true;
        }
    }
    return false;
}
