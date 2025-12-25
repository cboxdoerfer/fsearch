# FSearch inotify Implementation Plan

## Overview

This document describes the implementation plan for adding inotify-based file change detection to FSearch using **Option A: Coalesced Incremental Updates**. This approach batches inotify events over a short window (1-2 seconds) before applying changes to the database, trading minimal latency for implementation simplicity.

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────────────┐
│                        FsearchApplication                            │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────────┐ │
│  │  db_pool     │  │  FsearchDB   │  │  FsearchMonitor (NEW)      │ │
│  │  (scan/load) │  │  (in-memory) │  │  - inotify thread          │ │
│  └──────────────┘  └──────────────┘  │  - event queue             │ │
│                           ▲          │  - coalesce timer          │ │
│                           │          │  - watch descriptor table  │ │
│                           │          └─────────────┬──────────────┘ │
│                           │                        │                │
│                           │    (batched updates)   │                │
│                           └────────────────────────┘                │
└─────────────────────────────────────────────────────────────────────┘
```

## Data Structure Analysis

### Current Limitations

1. **DynamicArray** (`src/fsearch_array.c`):
   - Has `darray_add_item()` (append only)
   - Has `darray_binary_search_with_data()` (can find insertion point)
   - **Missing**: `darray_remove_item()`, `darray_insert_at()`

2. **Nine Sorted Arrays**: Each database maintains 9 orderings:
   - `DATABASE_INDEX_TYPE_NAME` (primary)
   - `DATABASE_INDEX_TYPE_PATH`
   - `DATABASE_INDEX_TYPE_SIZE`
   - `DATABASE_INDEX_TYPE_MODIFICATION_TIME`
   - `DATABASE_INDEX_TYPE_EXTENSION`
   - (and others for access/creation/status change time, filetype)

3. **Memory Pool** (`src/fsearch_memory_pool.c`):
   - Has `fsearch_memory_pool_free()` which adds to reuse free-list
   - Entries can be recycled efficiently

4. **Parent-Child Relationships**:
   - `db_entry_set_parent()` increments `parent->num_files` or `parent->num_folders`
   - `db_entry_update_parent_size()` propagates size up the tree
   - **Need**: Functions to decrement counts and subtract sizes on deletion

---

## Implementation Phases

### Phase 1: DynamicArray Extensions

**File**: `src/fsearch_array.c` / `src/fsearch_array.h`

Add new functions for incremental operations:

```c
// Remove item at index, shift remaining items left
// Returns the removed item (caller must handle cleanup)
void *darray_remove_at(DynamicArray *array, uint32_t idx);

// Insert item at specific index, shift existing items right
void darray_insert_at(DynamicArray *array, uint32_t idx, void *item);

// Find insertion point for sorted array (returns index where item should go)
uint32_t darray_find_insertion_point(DynamicArray *array,
                                      void *item,
                                      DynamicArrayCompareDataFunc comp_func,
                                      void *data);

// Insert item maintaining sort order
void darray_insert_sorted(DynamicArray *array,
                          void *item,
                          DynamicArrayCompareDataFunc comp_func,
                          void *data);

// Remove item from sorted array (uses binary search to find it)
void *darray_remove_sorted(DynamicArray *array,
                           void *item,
                           DynamicArrayCompareDataFunc comp_func,
                           void *data);
```

**Implementation Notes**:
- `darray_remove_at()`: Use `memmove()` to shift elements, decrement `num_items`
- `darray_insert_at()`: Expand if needed, `memmove()` to make room, increment `num_items`
- `darray_find_insertion_point()`: Modified binary search that returns insertion index
- These operations are O(n) but acceptable for coalesced batch updates

---

### Phase 2: Database Incremental Update API

**Files**: `src/fsearch_database.c` / `src/fsearch_database.h`

Add new functions for entry management:

```c
// Add a single file entry to the database
// Inserts into all sorted arrays, updates parent counts/sizes
bool db_add_file_entry(FsearchDatabase *db,
                       FsearchDatabaseEntryFolder *parent,
                       const char *name,
                       off_t size,
                       time_t mtime);

// Add a single folder entry to the database
// Returns the new folder entry for use as parent
FsearchDatabaseEntryFolder *db_add_folder_entry(FsearchDatabase *db,
                                                 FsearchDatabaseEntryFolder *parent,
                                                 const char *name,
                                                 time_t mtime);

// Remove a file entry from the database
// Removes from all sorted arrays, updates parent counts/sizes
bool db_remove_file_entry(FsearchDatabase *db, FsearchDatabaseEntry *entry);

// Remove a folder entry and all its children recursively
bool db_remove_folder_entry(FsearchDatabase *db, FsearchDatabaseEntryFolder *folder);

// Update file entry metadata (size, mtime)
// Re-sorts affected arrays if sort keys changed
bool db_update_file_entry(FsearchDatabase *db,
                          FsearchDatabaseEntry *entry,
                          off_t new_size,
                          time_t new_mtime);

// Find folder entry by path (for inotify watch descriptor → folder mapping)
FsearchDatabaseEntryFolder *db_find_folder_by_path(FsearchDatabase *db,
                                                    const char *path);

// Batch update: apply multiple changes atomically
// More efficient than individual calls - re-sorts once at end
typedef struct {
    enum { DB_CHANGE_ADD_FILE, DB_CHANGE_ADD_FOLDER,
           DB_CHANGE_REMOVE, DB_CHANGE_MODIFY } type;
    char *path;           // Full path for new entries
    off_t size;           // For add/modify
    time_t mtime;         // For add/modify
    FsearchDatabaseEntry *entry;  // For remove/modify (lookup result)
} FsearchDatabaseChange;

bool db_apply_changes_batch(FsearchDatabase *db,
                            GArray *changes,  // Array of FsearchDatabaseChange
                            GCancellable *cancellable);
```

**Key Implementation Details**:

1. **Adding entries**:
   - Allocate from memory pool
   - Set name, size, mtime, parent, type
   - For each of 9 sorted arrays: find insertion point, insert
   - Update parent->num_files/num_folders
   - Propagate size to parent chain

2. **Removing entries**:
   - For each of 9 sorted arrays: binary search to find, remove
   - Decrement parent->num_files/num_folders
   - Subtract size from parent chain
   - Return entry to memory pool free list

3. **Batch operations**:
   - Collect all adds/removes
   - Apply to primary NAME array
   - Re-sort all secondary arrays at once (more efficient than individual inserts)
   - This is the key optimization for coalesced updates

---

### Phase 3: Database Entry Extensions

**Files**: `src/fsearch_database_entry.c` / `src/fsearch_database_entry.h`

Add functions for cleanup operations:

```c
// Decrement parent file/folder count when removing entry
void db_entry_unset_parent(FsearchDatabaseEntry *entry);

// Subtract size from parent chain (reverse of db_entry_update_parent_size)
void db_entry_subtract_parent_size(FsearchDatabaseEntry *entry);

// Clear entry for reuse (called before returning to memory pool)
void db_entry_clear(FsearchDatabaseEntry *entry);
```

---

### Phase 4: Monitor Subsystem (Core inotify Logic)

**New Files**: `src/fsearch_monitor.c` / `src/fsearch_monitor.h`

```c
typedef struct FsearchMonitor FsearchMonitor;

// Lifecycle
FsearchMonitor *fsearch_monitor_new(FsearchDatabase *db, GList *index_paths);
void fsearch_monitor_free(FsearchMonitor *monitor);

// Control
bool fsearch_monitor_start(FsearchMonitor *monitor);
void fsearch_monitor_stop(FsearchMonitor *monitor);
bool fsearch_monitor_is_running(FsearchMonitor *monitor);

// Configuration
void fsearch_monitor_set_coalesce_interval_ms(FsearchMonitor *monitor, uint32_t ms);
void fsearch_monitor_set_excluded_paths(FsearchMonitor *monitor, GList *excludes);
void fsearch_monitor_set_exclude_patterns(FsearchMonitor *monitor, char **patterns);
```

**Internal Structure**:

```c
struct FsearchMonitor {
    FsearchDatabase *db;           // Database to update

    int inotify_fd;                // inotify file descriptor
    GThread *watch_thread;         // Thread reading inotify events

    GHashTable *wd_to_folder;      // watch descriptor → FsearchDatabaseEntryFolder*
    GHashTable *path_to_wd;        // path string → watch descriptor

    GMutex event_queue_mutex;
    GQueue *event_queue;           // Pending FsearchMonitorEvent items

    guint coalesce_timer_id;       // GLib timer for batching
    uint32_t coalesce_interval_ms; // Default: 1500ms

    GList *index_paths;            // Paths being monitored
    GList *exclude_paths;          // Excluded directories
    char **exclude_patterns;       // fnmatch patterns

    GCancellable *cancellable;     // For clean shutdown
    volatile bool running;
};

typedef struct {
    enum {
        MONITOR_EVENT_CREATE,
        MONITOR_EVENT_DELETE,
        MONITOR_EVENT_MODIFY,
        MONITOR_EVENT_MOVED_FROM,
        MONITOR_EVENT_MOVED_TO,
    } type;
    int wd;                        // Watch descriptor
    char *name;                    // Filename (not full path)
    uint32_t cookie;               // For matching MOVED_FROM/TO pairs
    bool is_dir;
} FsearchMonitorEvent;
```

**inotify Watch Thread Logic**:

```c
static void *monitor_thread_func(void *data) {
    FsearchMonitor *monitor = data;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    while (monitor->running) {
        // Use poll() with timeout to allow clean shutdown
        struct pollfd pfd = { .fd = monitor->inotify_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 100);  // 100ms timeout

        if (ret <= 0 || !monitor->running) continue;

        ssize_t len = read(monitor->inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        for (char *ptr = buf; ptr < buf + len; ) {
            struct inotify_event *event = (struct inotify_event *)ptr;

            // Skip excluded patterns
            if (event->len > 0 && should_exclude(monitor, event->name)) {
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Queue event for processing
            FsearchMonitorEvent *ev = create_event(event);

            g_mutex_lock(&monitor->event_queue_mutex);
            g_queue_push_tail(monitor->event_queue, ev);

            // Start/reset coalesce timer
            if (monitor->coalesce_timer_id == 0) {
                monitor->coalesce_timer_id = g_timeout_add(
                    monitor->coalesce_interval_ms,
                    on_coalesce_timer,
                    monitor);
            }
            g_mutex_unlock(&monitor->event_queue_mutex);

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
    return NULL;
}
```

**Coalesce Timer Callback**:

```c
static gboolean on_coalesce_timer(gpointer user_data) {
    FsearchMonitor *monitor = user_data;

    g_mutex_lock(&monitor->event_queue_mutex);
    GQueue *events = monitor->event_queue;
    monitor->event_queue = g_queue_new();
    monitor->coalesce_timer_id = 0;
    g_mutex_unlock(&monitor->event_queue_mutex);

    if (g_queue_is_empty(events)) {
        g_queue_free(events);
        return G_SOURCE_REMOVE;
    }

    // Process events on main thread via idle callback
    g_idle_add(process_coalesced_events, events);

    return G_SOURCE_REMOVE;
}
```

**Event Processing (runs on main thread)**:

```c
static gboolean process_coalesced_events(gpointer user_data) {
    GQueue *events = user_data;
    FsearchMonitor *monitor = ...; // Get from global or closure

    // Step 1: Coalesce events
    GHashTable *path_events = coalesce_events(events);

    // Step 2: Convert to database changes
    GArray *db_changes = g_array_new(FALSE, FALSE, sizeof(FsearchDatabaseChange));

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, path_events);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const char *path = key;
        CoalescedEvent *ev = value;

        FsearchDatabaseChange change = {};

        switch (ev->final_state) {
        case COALESCED_CREATED:
            change.type = ev->is_dir ? DB_CHANGE_ADD_FOLDER : DB_CHANGE_ADD_FILE;
            change.path = g_strdup(path);
            stat(path, &st);  // Get current size/mtime
            change.size = st.st_size;
            change.mtime = st.st_mtime;
            break;

        case COALESCED_DELETED:
            change.type = DB_CHANGE_REMOVE;
            change.entry = db_find_entry_by_path(monitor->db, path);
            break;

        case COALESCED_MODIFIED:
            change.type = DB_CHANGE_MODIFY;
            change.entry = db_find_entry_by_path(monitor->db, path);
            stat(path, &st);
            change.size = st.st_size;
            change.mtime = st.st_mtime;
            break;

        case COALESCED_NOOP:
            // Created then deleted, or vice versa - skip
            continue;
        }

        g_array_append_val(db_changes, change);
    }

    // Step 3: Apply batch to database
    db_lock(monitor->db);
    db_apply_changes_batch(monitor->db, db_changes, NULL);
    db_unlock(monitor->db);

    // Step 4: Notify views to refresh
    // (emit signal or call registered callbacks)

    // Cleanup
    g_array_unref(db_changes);
    g_hash_table_unref(path_events);
    g_queue_free_full(events, (GDestroyNotify)free_monitor_event);

    return G_SOURCE_REMOVE;
}
```

**Event Coalescing Logic**:

```c
typedef enum {
    COALESCED_CREATED,   // Net effect: new file/folder exists
    COALESCED_DELETED,   // Net effect: file/folder removed
    COALESCED_MODIFIED,  // Net effect: file/folder modified (size/mtime changed)
    COALESCED_NOOP,      // Net effect: nothing (created then deleted, etc.)
} CoalescedState;

static GHashTable *coalesce_events(GQueue *events) {
    // path → CoalescedEvent
    GHashTable *result = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                g_free, g_free);

    while (!g_queue_is_empty(events)) {
        FsearchMonitorEvent *ev = g_queue_pop_head(events);

        // Build full path from wd + name
        char *path = build_path(ev->wd, ev->name);

        CoalescedEvent *existing = g_hash_table_lookup(result, path);

        if (!existing) {
            // First event for this path
            existing = g_new0(CoalescedEvent, 1);
            existing->is_dir = ev->is_dir;

            switch (ev->type) {
            case MONITOR_EVENT_CREATE:
            case MONITOR_EVENT_MOVED_TO:
                existing->final_state = COALESCED_CREATED;
                break;
            case MONITOR_EVENT_DELETE:
            case MONITOR_EVENT_MOVED_FROM:
                existing->final_state = COALESCED_DELETED;
                break;
            case MONITOR_EVENT_MODIFY:
                existing->final_state = COALESCED_MODIFIED;
                break;
            }
            g_hash_table_insert(result, g_strdup(path), existing);
        } else {
            // Combine with existing event
            switch (ev->type) {
            case MONITOR_EVENT_CREATE:
            case MONITOR_EVENT_MOVED_TO:
                if (existing->final_state == COALESCED_DELETED) {
                    // Was deleted, now created again → modified
                    existing->final_state = COALESCED_MODIFIED;
                }
                // else: already created, stay created
                break;

            case MONITOR_EVENT_DELETE:
            case MONITOR_EVENT_MOVED_FROM:
                if (existing->final_state == COALESCED_CREATED) {
                    // Created then deleted → noop
                    existing->final_state = COALESCED_NOOP;
                } else {
                    existing->final_state = COALESCED_DELETED;
                }
                break;

            case MONITOR_EVENT_MODIFY:
                if (existing->final_state == COALESCED_CREATED) {
                    // Still created (will pick up current state)
                } else if (existing->final_state != COALESCED_DELETED) {
                    existing->final_state = COALESCED_MODIFIED;
                }
                break;
            }
        }

        g_free(path);
        free_monitor_event(ev);
    }

    return result;
}
```

---

### Phase 5: Watch Descriptor Management

**Key Challenges**:
1. inotify requires a watch on each directory (no recursive option)
2. Watch descriptor limit (default ~8192, can be increased)
3. New directories must get watches added
4. Removed directories must have watches removed

**Watch Setup (during initial scan or on folder creation)**:

```c
static int add_watch_recursive(FsearchMonitor *monitor,
                                FsearchDatabaseEntryFolder *folder,
                                const char *path) {
    // Add watch for this directory
    int wd = inotify_add_watch(monitor->inotify_fd, path,
                               IN_CREATE | IN_DELETE | IN_MODIFY |
                               IN_MOVED_FROM | IN_MOVED_TO |
                               IN_DONT_FOLLOW | IN_EXCL_UNLINK);

    if (wd < 0) {
        if (errno == ENOSPC) {
            g_warning("inotify watch limit reached for %s", path);
        }
        return -1;
    }

    // Store mappings
    g_hash_table_insert(monitor->wd_to_folder, GINT_TO_POINTER(wd), folder);
    g_hash_table_insert(monitor->path_to_wd, g_strdup(path), GINT_TO_POINTER(wd));

    return wd;
}

static void remove_watch(FsearchMonitor *monitor, const char *path) {
    gpointer wd_ptr = g_hash_table_lookup(monitor->path_to_wd, path);
    if (wd_ptr) {
        int wd = GPOINTER_TO_INT(wd_ptr);
        inotify_rm_watch(monitor->inotify_fd, wd);
        g_hash_table_remove(monitor->wd_to_folder, wd_ptr);
        g_hash_table_remove(monitor->path_to_wd, path);
    }
}
```

**Handling New Directories**:

When a `IN_CREATE` or `IN_MOVED_TO` event has `IN_ISDIR` set:
1. Add inotify watch for the new directory
2. Scan the directory for existing contents (it might not be empty if moved)
3. Recursively add watches for subdirectories

---

### Phase 6: Application Integration

**File**: `src/fsearch.c`

```c
struct _FsearchApplication {
    // ... existing fields ...

    FsearchMonitor *monitor;           // NEW
    bool monitor_enabled;              // From config
};

// In fsearch_application_startup():
static void fsearch_application_startup(GApplication *app) {
    // ... existing code ...

    fsearch->monitor = NULL;
    fsearch->monitor_enabled = fsearch->config->enable_file_monitor;  // NEW config
}

// After database load/scan completes:
static void on_database_update_finished(gpointer user_data) {
    // ... existing code ...

    FsearchApplication *self = FSEARCH_APPLICATION_DEFAULT;

    // Start monitoring if enabled and we have a database
    if (self->monitor_enabled && self->db && !self->monitor) {
        self->monitor = fsearch_monitor_new(self->db, self->config->indexes);
        fsearch_monitor_set_excluded_paths(self->monitor, self->config->exclude_locations);
        fsearch_monitor_set_exclude_patterns(self->monitor, self->config->exclude_files);
        fsearch_monitor_start(self->monitor);
    }
}

// In fsearch_application_shutdown():
static void fsearch_application_shutdown(GApplication *app) {
    FsearchApplication *fsearch = FSEARCH_APPLICATION(app);

    // Stop monitor before database cleanup
    if (fsearch->monitor) {
        fsearch_monitor_stop(fsearch->monitor);
        g_clear_pointer(&fsearch->monitor, fsearch_monitor_free);
    }

    // ... existing shutdown code ...
}
```

---

### Phase 7: Configuration

**File**: `src/fsearch_config.h` / `src/fsearch_config.c`

Add new configuration options:

```c
// In FsearchConfig struct:
bool enable_file_monitor;              // Enable inotify monitoring
uint32_t monitor_coalesce_ms;          // Coalesce interval (default: 1500)

// Config file keys:
// [Database]
// enable_file_monitor=true
// monitor_coalesce_ms=1500
```

**File**: `src/fsearch_preferences_ui.c`

Add UI controls:
- Checkbox: "Enable real-time file monitoring"
- Spinner: "Update delay (ms)" [500-5000, step 100]

---

## Testing Strategy

### Unit Tests

1. **DynamicArray extensions**:
   - `darray_insert_at()` with various positions (beginning, middle, end)
   - `darray_remove_at()` with various positions
   - `darray_insert_sorted()` maintains order
   - Edge cases: empty array, single element, duplicates

2. **Database incremental operations**:
   - Add file → verify in all sorted arrays
   - Remove file → verify removed from all arrays
   - Verify parent counts updated correctly
   - Verify parent sizes updated correctly

3. **Event coalescing**:
   - CREATE → final: CREATED
   - DELETE → final: DELETED
   - CREATE + DELETE → final: NOOP
   - DELETE + CREATE → final: MODIFIED
   - MODIFY + MODIFY → final: MODIFIED
   - Complex sequences

### Integration Tests

1. **Basic operations**:
   - Create file → appears in search results within 2s
   - Delete file → disappears from search results
   - Rename file → old name gone, new name appears
   - Move file → correct path update

2. **Directory operations**:
   - Create directory → can search for it
   - Create directory with files → all contents searchable
   - Delete directory → all contents removed from index
   - Move directory → all contents update paths

3. **Stress tests**:
   - Bulk file creation (1000+ files)
   - Rapid create/delete cycles
   - Large file copies

4. **Edge cases**:
   - Excluded directory changes
   - Symlink handling
   - Permission denied directories
   - Watch limit exhaustion

---

## Resource Considerations

### Watch Descriptor Limits

Default: `/proc/sys/fs/inotify/max_user_watches` ≈ 8192

For large file systems, users may need to increase:
```bash
sudo sysctl fs.inotify.max_user_watches=524288
```

**Mitigation strategies**:
1. Log warning when approaching limit
2. Graceful degradation: stop adding watches, fall back to periodic scan
3. Document in README/help

### Memory Overhead

Per watch descriptor:
- ~1KB kernel memory
- ~16 bytes in our hash tables

For 100,000 directories: ~100MB additional memory

### CPU Usage

- inotify thread: minimal (mostly sleeping on read())
- Coalesced updates: brief spike every 1-2s when changes occur
- Sorted array operations: O(n) per insert/remove, but batched

---

## File Summary

| File | Status | Description |
|------|--------|-------------|
| `src/fsearch_array.c` | Modify | Add insert/remove functions |
| `src/fsearch_array.h` | Modify | Declare new functions |
| `src/fsearch_database.c` | Modify | Add incremental update functions |
| `src/fsearch_database.h` | Modify | Declare new functions |
| `src/fsearch_database_entry.c` | Modify | Add unset_parent, subtract_size |
| `src/fsearch_database_entry.h` | Modify | Declare new functions |
| `src/fsearch_monitor.c` | **New** | inotify monitoring implementation |
| `src/fsearch_monitor.h` | **New** | Monitor public interface |
| `src/fsearch.c` | Modify | Integrate monitor lifecycle |
| `src/fsearch_config.c` | Modify | Add config options |
| `src/fsearch_config.h` | Modify | Declare config fields |
| `src/fsearch_preferences_ui.c` | Modify | Add UI controls |
| `src/meson.build` | Modify | Add new source files |

---

## Implementation Order

1. **Phase 1**: DynamicArray extensions (foundation)
2. **Phase 3**: Database entry cleanup functions
3. **Phase 2**: Database incremental update API
4. **Phase 4**: Monitor subsystem (core inotify)
5. **Phase 5**: Watch management
6. **Phase 6**: Application integration
7. **Phase 7**: Configuration UI

This order ensures each phase builds on completed foundations.

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Watch limit exhaustion | Medium | High | Graceful degradation + documentation |
| Event queue overflow | Low | Medium | Coalescing reduces volume; monitor queue size |
| Race conditions | Medium | High | Careful mutex usage; main-thread-only DB updates |
| Performance regression | Low | Medium | Benchmark before/after; optimize hot paths |
| inotify bugs/limitations | Low | Medium | Fall back to periodic scan |

---

## Success Criteria

1. File changes detected and reflected in search within 2 seconds
2. No memory leaks after extended operation
3. CPU usage < 1% when idle (no file changes)
4. Graceful handling of watch limit
5. Clean shutdown without hangs
6. All existing tests pass
7. New unit tests for incremental operations
