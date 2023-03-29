#pragma once

#define DATABASE_INDEX_PROPERTY_NAME_STRING "Name"
#define DATABASE_INDEX_PROPERTY_PATH_STRING "Path"
#define DATABASE_INDEX_PROPERTY_SIZE_STRING "Size"
#define DATABASE_INDEX_PROPERTY_MODIFICATION_TIME_STRING "Date Modified"
#define DATABASE_INDEX_PROPERTY_FILETYPE_STRING "Type"
#define DATABASE_INDEX_PROPERTY_EXTENSION_STRING "Extension"

typedef enum {
    DATABASE_INDEX_PROPERTY_FLAG_NAME = 1 << 0,
    DATABASE_INDEX_PROPERTY_FLAG_PATH = 1 << 1,
    DATABASE_INDEX_PROPERTY_FLAG_SIZE = 1 << 2,
    DATABASE_INDEX_PROPERTY_FLAG_MODIFICATION_TIME = 1 << 3,
    DATABASE_INDEX_PROPERTY_FLAG_ACCESS_TIME = 1 << 4,
    DATABASE_INDEX_PROPERTY_FLAG_CREATION_TIME = 1 << 5,
    DATABASE_INDEX_PROPERTY_FLAG_STATUS_CHANGE_TIME = 1 << 6,
    DATABASE_INDEX_PROPERTY_FLAG_NONE = 0,
} FsearchDatabaseIndexPropertyFlags;

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
    DATABASE_INDEX_PROPERTY_FILETYPE,
    DATABASE_INDEX_PROPERTY_EXTENSION,
    NUM_DATABASE_INDEX_PROPERTIES,
} FsearchDatabaseIndexProperty;
