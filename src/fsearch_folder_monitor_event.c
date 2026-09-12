#include "fsearch_folder_monitor_event.h"
#include "fsearch_database_entry.h"

void
fsearch_folder_monitor_event_free(FsearchFolderMonitorEvent *self) {
    if (self->name) {
        g_string_free(g_steal_pointer(&self->name), TRUE);
    }
    if (self->path) {
        g_string_free(g_steal_pointer(&self->path), TRUE);
    }
    if (self->watched_entry_handle && self->monitor_kind == FSEARCH_FOLDER_MONITOR_FANOTIFY) {
        g_bytes_unref(self->watched_entry_handle);
    }
    g_clear_pointer((FsearchDatabaseEntry**)&self->watched_entry_copy, db_entry_free_full);
    g_clear_pointer(&self, free);
}

// Never dereferences an entry -- safe to call from the monitor thread.
FsearchFolderMonitorEvent *
fsearch_folder_monitor_event_new(const char *file_name,
                                 gpointer watched_entry_handle,
                                 FsearchFolderMonitorEventKind event_kind,
                                 FsearchFolderMonitorKind monitor_kind,
                                 bool is_dir) {
    FsearchFolderMonitorEvent *ctx = calloc(1, sizeof(FsearchFolderMonitorEvent));
    g_assert(ctx);

    ctx->name = file_name ? g_string_new(file_name) : NULL;
    ctx->watched_entry_handle = watched_entry_handle;
    ctx->event_kind = event_kind;
    ctx->monitor_kind = monitor_kind;
    ctx->is_dir = is_dir;

    return ctx;
}

void
fsearch_folder_monitor_event_set_watched_entry(FsearchFolderMonitorEvent *self, FsearchDatabaseEntry *watched_entry) {
    g_return_if_fail(self);
    g_return_if_fail(watched_entry);

    self->watched_entry_copy = (FsearchDatabaseEntry *)db_entry_get_deep_copy(watched_entry);
    self->path = db_entry_get_path_full(self->watched_entry_copy);

    if (self->name) {
        g_string_append_c(self->path, G_DIR_SEPARATOR);
        g_string_append(self->path, self->name->str);
    }
}

const char *
fsearch_folder_monitor_event_kind_to_string(FsearchFolderMonitorEventKind kind) {
    switch (kind) {
    case FSEARCH_FOLDER_MONITOR_EVENT_ATTRIB:
        return "ATTRIB";
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_FROM:
        return "MOVED_FROM";
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVED_TO:
        return "MOVED_TO";
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE:
        return "DELETE";
    case FSEARCH_FOLDER_MONITOR_EVENT_CREATE:
        return "CREATE";
    case FSEARCH_FOLDER_MONITOR_EVENT_DELETE_SELF:
        return "DELETE_SELF";
    case FSEARCH_FOLDER_MONITOR_EVENT_UNMOUNT:
        return "UNMOUNT";
    case FSEARCH_FOLDER_MONITOR_EVENT_MOVE_SELF:
        return "MOVE_SELF";
    case FSEARCH_FOLDER_MONITOR_EVENT_CLOSE_WRITE:
        return "CLOSE_WRITE";
    default:
        return "INVALID";
    }
}