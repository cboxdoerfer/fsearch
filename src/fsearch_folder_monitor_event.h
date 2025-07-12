#pragma once

#include <glib.h>

#include "fsearch_database_entry.h"

typedef enum {
    FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB,
    FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE,
    FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM,
    FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO,
    FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF,
    FSEARCH_FOLDER_MONITOR_EVENT_DELETE,
    FSEARCH_FOLDER_MONITOR_EVENT_CREATE,
    FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF,
    FSEARCH_FOLDER_MONITOR_EVENT_RESCAN,
    FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT,
    NUM_FSEARCH_FOLDER_MONITOR_EVENTS,
} FsearchFolderMonitorEventKind;

typedef enum {
    FSEARCH_FOLDER_MONITOR_NONE,
    FSEARCH_FOLDER_MONITOR_INOTIFY,
    FSEARCH_FOLDER_MONITOR_FANOTIFY,
} FsearchFolderMonitorKind;

typedef struct {
    GString *name;
    GString *path;

    FsearchDatabaseEntry *watched_entry;
    FsearchDatabaseEntry *watched_entry_copy;

    FsearchFolderMonitorEventKind event_kind;
    bool is_dir;
    FsearchFolderMonitorKind monitor_kind;
} FsearchFolderMonitorEvent;

FsearchFolderMonitorEvent *
fsearch_folder_monitor_event_new(const char *file_name,
                                 FsearchDatabaseEntry *watched_entry,
                                 FsearchFolderMonitorEventKind event_kind,
                                 FsearchFolderMonitorKind monitor_kind,
                                 bool is_dir);

void
fsearch_folder_monitor_event_free(FsearchFolderMonitorEvent *self);

const char *
fsearch_folder_monitor_event_kind_to_string(FsearchFolderMonitorEventKind kind);
