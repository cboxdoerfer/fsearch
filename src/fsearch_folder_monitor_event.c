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
    g_clear_pointer((FsearchDatabaseEntry **)&self->watched_entry_copy, db_entry_free_full);
    g_clear_pointer(&self, free);
}

FsearchFolderMonitorEvent *
fsearch_folder_monitor_event_new(const char *file_name,
                                 FsearchDatabaseEntry *watched_entry,
                                 FsearchFolderMonitorEventKind event_kind,
                                 FsearchFolderMonitorKind monitor_kind,
                                 bool is_dir) {
    FsearchFolderMonitorEvent *ctx = calloc(1, sizeof(FsearchFolderMonitorEvent));
    g_assert(ctx);

    ctx->name = file_name ? g_string_new(file_name) : NULL;
    ctx->watched_entry_copy = (FsearchDatabaseEntry *)db_entry_get_deep_copy((FsearchDatabaseEntry *)watched_entry);

    if (ctx->name) {
        ctx->path = db_entry_get_path_full((FsearchDatabaseEntry *)ctx->watched_entry_copy);
        g_string_append_c(ctx->path, G_DIR_SEPARATOR);
        g_string_append(ctx->path, ctx->name->str);
    }

    ctx->event_kind = event_kind;
    ctx->monitor_kind = monitor_kind;

    ctx->is_dir = is_dir;

    return ctx;
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
