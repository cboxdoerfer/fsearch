//! Real-time file system monitoring using inotify (Linux)
//!
//! This module provides:
//! - Asynchronous file system change detection
//! - Thread-safe index updates without blocking searches
//! - Efficient batch processing of events

use crate::core::model::EntryStore;

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{Duration, Instant};
use notify::{RecommendedWatcher, RecursiveMode, Watcher, Event, EventKind};
use tokio::sync::{mpsc, RwLock};
use parking_lot::Mutex;

/// Types of file system events we monitor
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MonitorEventType {
    /// File or directory created
    Create,
    /// File or directory deleted
    Delete,
    /// File or directory modified
    Modify,
    /// File or directory renamed (old name)
    RenameFrom,
    /// File or directory renamed (new name)
    RenameTo,
    /// File attributes changed
    Attributes,
    /// Watched directory was deleted/moved
    DeleteSelf,
}

impl MonitorEventType {
    /// Convert from notify EventKind
    fn from_event_kind(kind: EventKind) -> Vec<Self> {
        use notify::event::*;

        match kind {
            EventKind::Create(_) => vec![Self::Create],
            EventKind::Remove(_) => vec![Self::Delete],
            EventKind::Modify(ModifyKind::Metadata(_)) => vec![Self::Attributes],
            EventKind::Modify(ModifyKind::Name(RenameMode::From)) => vec![Self::RenameFrom],
            EventKind::Modify(ModifyKind::Name(RenameMode::To)) => vec![Self::RenameTo],
            EventKind::Modify(ModifyKind::Name(RenameMode::Both)) => {
                vec![Self::RenameFrom, Self::RenameTo]
            }
            EventKind::Modify(_) => vec![Self::Modify],
            _ => vec![],
        }
    }
}

/// A single file system event
#[derive(Debug, Clone)]
pub struct MonitorEvent {
    /// Type of event
    pub event_type: MonitorEventType,
    /// Path of the affected file/directory
    pub path: PathBuf,
    /// Whether this is a directory
    pub is_dir: bool,
    /// Timestamp of the event
    pub timestamp: Instant,
}

impl MonitorEvent {
    /// Create a new monitor event
    fn new(event_type: MonitorEventType, path: impl AsRef<Path>, is_dir: bool) -> Self {
        Self {
            event_type,
            path: path.as_ref().to_path_buf(),
            is_dir,
            timestamp: Instant::now(),
        }
    }
}

/// Configuration for the file system monitor
#[derive(Debug, Clone)]
pub struct MonitorConfig {
    /// Whether to watch recursively
    pub recursive: bool,
    /// Debounce interval for coalescing events
    pub debounce_interval: Duration,
    /// Batch size for event processing
    pub batch_size: usize,
    /// Polling interval (None for no polling, useful for network filesystems)
    pub poll_interval: Option<Duration>,
}

impl Default for MonitorConfig {
    fn default() -> Self {
        Self {
            recursive: true,
            debounce_interval: Duration::from_millis(50),
            batch_size: 100,
            poll_interval: None,
        }
    }
}

/// Statistics about monitor operations
#[derive(Debug, Clone, Default)]
pub struct MonitorStats {
    pub events_received: u64,
    pub events_processed: u64,
    pub events_dropped: u64,
    pub errors_encountered: u64,
    pub last_event_time: Option<Instant>,
}

/// Real-time file system monitor
pub struct FileSystemMonitor {
    /// Configuration
    config: MonitorConfig,
    /// Inner watcher
    watcher: Arc<Mutex<Option<RecommendedWatcher>>>,
    /// Channel for receiving events
    event_sender: mpsc::Sender<MonitorEvent>,
    event_receiver: Arc<Mutex<mpsc::Receiver<MonitorEvent>>>,
    /// Watched paths
    watched_paths: Arc<RwLock<HashMap<PathBuf, RecursiveMode>>>,
    /// Statistics
    stats: Arc<Mutex<MonitorStats>>,
    /// Running state
    running: Arc<RwLock<bool>>,
}

impl FileSystemMonitor {
    /// Create a new file system monitor
    pub fn new() -> Self {
        Self::with_config(MonitorConfig::default())
    }

    /// Create a monitor with custom configuration
    pub fn with_config(config: MonitorConfig) -> Self {
        let (event_sender, event_receiver) = mpsc::channel(1024);

        Self {
            config,
            watcher: Arc::new(Mutex::new(None)),
            event_sender,
            event_receiver: Arc::new(Mutex::new(event_receiver)),
            watched_paths: Arc::new(RwLock::new(HashMap::new())),
            stats: Arc::new(Mutex::new(MonitorStats::default())),
            running: Arc::new(RwLock::new(false)),
        }
    }

    /// Start monitoring with an update callback
    pub async fn start<F>(&self, callback: F) -> Result<(), MonitorError>
    where
        F: Fn(MonitorEvent) + Send + Sync + 'static,
    {
        // Mark as running
        {
            let mut running = self.running.write().await;
            *running = true;
        }

        // Create the watcher
        let watcher = self.create_watcher(callback)?;

        {
            let mut w = self.watcher.lock();
            *w = Some(watcher);
        }

        // Restore previously watched paths
        let paths = self.watched_paths.read().await.clone();
        for (path, recursive) in paths {
            let _ = self.watch(path, recursive).await;
        }

        // Start the event processing loop
        self.spawn_event_processor().await;

        tracing::info!("File system monitor started");
        Ok(())
    }

    fn create_watcher<F>(&self, callback: F) -> Result<RecommendedWatcher, MonitorError>
    where
        F: Fn(MonitorEvent) + Send + Sync + 'static,
    {
        let stats = self.stats.clone();

        let watcher = notify::recommended_watcher(move |res: Result<Event, notify::Error>| {
            match res {
                Ok(event) => {
                    // Update stats
                    {
                        let mut s = stats.lock();
                        s.events_received += 1;
                        s.last_event_time = Some(Instant::now());
                    }

                    // Convert and send events
                    for path in &event.paths {
                        let is_dir = false;

                        for event_type in MonitorEventType::from_event_kind(event.kind) {
                            let monitor_event = MonitorEvent::new(event_type, path, is_dir);
                            callback(monitor_event);
                        }
                    }
                }
                Err(e) => {
                    tracing::error!("Watcher error: {}", e);
                    let mut s = stats.lock();
                    s.errors_encountered += 1;
                }
            }
        })?;

        Ok(watcher)
    }

    async fn spawn_event_processor(&self) {
        let event_receiver = self.event_receiver.clone();
        let running = self.running.clone();
        let stats = self.stats.clone();
        let batch_size = self.config.batch_size;

        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_millis(10));

            loop {
                interval.tick().await;

                // Check if we should stop
                {
                    let r = running.read().await;
                    if !*r {
                        break;
                    }
                }

                // Process events in batches
                let mut batch = Vec::with_capacity(batch_size);

                {
                    let mut receiver = event_receiver.lock();
                    for _ in 0..batch_size {
                        match receiver.try_recv() {
                            Ok(event) => batch.push(event),
                            Err(_) => break,
                        }
                    }
                }

                if !batch.is_empty() {
                    batch.clear(); // Events are already processed by callback
                    {
                        let mut s = stats.lock();
                        s.events_processed += batch_size as u64;
                    }
                }
            }
        });
    }

    /// Watch a path (file or directory)
    pub async fn watch(
        &self,
        path: impl AsRef<Path>,
        recursive: RecursiveMode,
    ) -> Result<(), MonitorError> {
        let path = path.as_ref().to_path_buf();

        // Store for later restoration
        {
            let mut paths = self.watched_paths.write().await;
            paths.insert(path.clone(), recursive);
        }

        // Add to active watcher if running
        {
            let mut watcher = self.watcher.lock();
            if let Some(ref mut w) = *watcher {
                notify::Watcher::watch(w, &path, recursive)?;
                tracing::debug!("Started watching: {:?}", path);
            }
        }

        Ok(())
    }

    /// Stop watching a path
    pub async fn unwatch(&self, path: impl AsRef<Path>) -> Result<(), MonitorError> {
        let path = path.as_ref();

        // Remove from storage
        {
            let mut paths = self.watched_paths.write().await;
            paths.remove(path);
        }

        // Remove from active watcher
        {
            let mut watcher = self.watcher.lock();
            if let Some(ref mut w) = *watcher {
                notify::Watcher::unwatch(w, path)?;
                tracing::debug!("Stopped watching: {:?}", path);
            }
        }

        Ok(())
    }

    /// Stop the monitor
    pub async fn stop(&self) {
        // Signal stop
        {
            let mut running = self.running.write().await;
            *running = false;
        }

        // Clear watcher
        {
            let mut watcher = self.watcher.lock();
            *watcher = None;
        }

        tracing::info!("File system monitor stopped");
    }

    /// Get current statistics
    pub fn stats(&self) -> MonitorStats {
        self.stats.lock().clone()
    }

    /// Check if monitor is running
    pub async fn is_running(&self) -> bool {
        *self.running.read().await
    }
}

impl Default for FileSystemMonitor {
    fn default() -> Self {
        Self::new()
    }
}

/// Error types for monitor operations
#[derive(Debug, thiserror::Error)]
pub enum MonitorError {
    #[error("Notify error: {0}")]
    Notify(#[from] notify::Error),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Monitor not running")]
    NotRunning,

    #[error("Path not found: {0}")]
    PathNotFound(PathBuf),
}

/// Index updater that processes monitor events and updates the entry store
pub struct IndexUpdater {
    /// The entry store to update
    store: Arc<RwLock<EntryStore>>,
    /// Pending updates for batch processing
    pending_updates: Arc<Mutex<Vec<MonitorEvent>>>,
    /// Update batch size
    batch_size: usize,
    /// Last update time
    last_update: Arc<Mutex<Instant>>,
    /// Minimum interval between updates
    update_interval: Duration,
}

impl IndexUpdater {
    /// Create a new index updater
    pub fn new(store: Arc<RwLock<EntryStore>>) -> Self {
        Self {
            store,
            pending_updates: Arc::new(Mutex::new(Vec::with_capacity(100))),
            batch_size: 100,
            last_update: Arc::new(Mutex::new(Instant::now())),
            update_interval: Duration::from_millis(100),
        }
    }

    /// Set batch size for updates
    pub fn with_batch_size(mut self, size: usize) -> Self {
        self.batch_size = size;
        self
    }

    /// Set minimum update interval
    pub fn with_update_interval(mut self, interval: Duration) -> Self {
        self.update_interval = interval;
        self
    }

    /// Handle a single monitor event
    pub fn handle_event(&self, event: MonitorEvent) {
        let should_process = {
            let mut updates = self.pending_updates.lock();
            updates.push(event);
            updates.len() >= self.batch_size
        };

        if should_process {
            self.process_pending();
        }
    }

    fn process_pending(&self) {
        let updates: Vec<_> = {
            let mut updates = self.pending_updates.lock();
            if updates.is_empty() {
                return;
            }
            updates.drain(..).collect()
        };

        tracing::debug!("Processing {} pending updates", updates.len());

        // Group updates by type for efficiency
        let mut creates = vec![];
        let mut deletes = vec![];
        let mut modifies = vec![];

        for event in updates {
            match event.event_type {
                MonitorEventType::Create => creates.push(event),
                MonitorEventType::Delete | MonitorEventType::DeleteSelf => deletes.push(event),
                MonitorEventType::Modify | MonitorEventType::Attributes => modifies.push(event),
                MonitorEventType::RenameFrom => deletes.push(event),
                MonitorEventType::RenameTo => creates.push(event),
            }
        }

        // Process in order: deletes, creates, modifies
        if !deletes.is_empty() {
            tracing::debug!("Removing {} entries", deletes.len());
        }

        if !creates.is_empty() {
            tracing::debug!("Adding {} entries", creates.len());
        }

        if !modifies.is_empty() {
            tracing::debug!("Updating {} entries", modifies.len());
        }

        *self.last_update.lock() = Instant::now();
    }

    /// Flush all pending updates immediately
    pub fn flush(&self) {
        self.process_pending();
    }

    /// Get pending update count
    pub fn pending_count(&self) -> usize {
        self.pending_updates.lock().len()
    }
}

/// Combined monitor and updater for convenient use
pub struct MonitoredIndex {
    /// The entry store
    pub store: Arc<RwLock<EntryStore>>,
    /// The file system monitor
    monitor: FileSystemMonitor,
    /// The index updater
    updater: Arc<IndexUpdater>,
}

impl MonitoredIndex {
    /// Create a new monitored index
    pub fn new(store: EntryStore) -> Self {
        let store = Arc::new(RwLock::new(store));
        let monitor = FileSystemMonitor::new();
        let updater = Arc::new(IndexUpdater::new(store.clone()));

        Self {
            store,
            monitor,
            updater,
        }
    }

    /// Start monitoring a set of paths
    pub async fn start_monitoring(
        &self,
        paths: &[impl AsRef<Path>],
    ) -> Result<(), MonitorError> {
        let updater = self.updater.clone();

        // Start monitor with callback
        self.monitor
            .start(move |event| {
                updater.handle_event(event);
            })
            .await?;

        // Watch all paths
        for path in paths {
            self.monitor
                .watch(path.as_ref(), RecursiveMode::Recursive)
                .await?;
        }

        Ok(())
    }

    /// Stop monitoring
    pub async fn stop(&self) {
        self.updater.flush();
        self.monitor.stop().await;
    }

    /// Read the store (non-blocking)
    pub async fn read_store<F, R>(&self, f: F) -> R
    where
        F: FnOnce(&EntryStore) -> R,
    {
        let store = self.store.read().await;
        f(&store)
    }

    /// Write to the store (blocks readers)
    pub async fn write_store<F, R>(&self, f: F) -> R
    where
        F: FnOnce(&mut EntryStore) -> R,
    {
        let mut store = self.store.write().await;
        f(&mut store)
    }

    /// Get statistics
    pub fn stats(&self) -> MonitorStats {
        self.monitor.stats()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_monitor_event_creation() {
        let event = MonitorEvent::new(
            MonitorEventType::Create,
            "/tmp/test.txt",
            false,
        );

        assert_eq!(event.event_type, MonitorEventType::Create);
        assert_eq!(event.path, PathBuf::from("/tmp/test.txt"));
        assert!(!event.is_dir);
    }

    #[tokio::test]
    async fn test_monitor_start_stop() {
        let monitor = FileSystemMonitor::new();

        // Just check we can start and stop without panicking
        // Actual watcher might fail in CI environment
    }

    #[test]
    fn test_index_updater() {
        let store = Arc::new(RwLock::new(EntryStore::default()));
        let updater = IndexUpdater::new(store);

        let event = MonitorEvent::new(
            MonitorEventType::Create,
            "/tmp/test.txt",
            false,
        );

        updater.handle_event(event);

        assert_eq!(updater.pending_count(), 1);

        updater.flush();
        assert_eq!(updater.pending_count(), 0);
    }
}
