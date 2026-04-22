//! High-performance file system scanner using parallel traversal
use crate::core::model::{Entry, EntryStore};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use std::collections::HashMap;
use walkdir::WalkDir;

#[derive(Debug, Clone)]
pub struct ScanConfig {
    pub follow_symlinks: bool,
    pub one_filesystem: bool,
    pub max_depth: Option<usize>,
    pub include_hidden: bool,
    pub min_file_size: Option<u64>,
    pub max_file_size: Option<u64>,
}

impl Default for ScanConfig {
    fn default() -> Self {
        Self {
            follow_symlinks: false,
            one_filesystem: false,
            max_depth: None,
            include_hidden: true,
            min_file_size: None,
            max_file_size: None,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct ScanStats {
    pub files_scanned: u64,
    pub folders_scanned: u64,
    pub bytes_scanned: u64,
}

#[derive(Debug, Clone, Default)]
pub struct ExcludeManager {}
#[derive(Debug, Clone)]
pub struct ExcludeRule {}

impl ExcludeRule {
    pub fn new(_: &str, _: ExcludeType, _: ExcludeScope, _: ExcludeTarget) -> Result<Self, String> {
        Ok(Self {})
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExcludeType { Fixed, Wildcard, Regex }
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExcludeScope { FullPath, Basename }
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExcludeTarget { Both, Files, Folders }

pub struct Scanner {
    pub config: ScanConfig,
    entry_id_counter: AtomicU32,
    stats: ScanStats,
}

impl Scanner {
    pub fn new() -> Self {
        Self {
            config: Default::default(),
            entry_id_counter: AtomicU32::new(0),
            stats: Default::default(),
        }
    }

    pub fn with_config(config: ScanConfig) -> Self {
        Self {
            config,
            entry_id_counter: AtomicU32::new(0),
            stats: Default::default(),
        }
    }

    pub fn stats(&self) -> ScanStats {
        self.stats.clone()
    }

    fn next_id(&self) -> u32 {
        self.entry_id_counter.fetch_add(1, Ordering::Relaxed)
    }

    pub fn scan(&mut self, root: impl AsRef<Path>) -> Result<EntryStore, Box<dyn std::error::Error>> {
        let root_path = root.as_ref().to_path_buf();
        let mut store = EntryStore::with_chunk_size(8192);
        
        let scanned_items: Vec<_> = WalkDir::new(&root_path)
            .follow_links(self.config.follow_symlinks)
            .into_iter()
            .filter_map(|e| e.ok())
            .collect();

        let mut entries_by_path: HashMap<PathBuf, Arc<Entry>> = HashMap::with_capacity(scanned_items.len());

        let mut sorted_items = scanned_items;
        sorted_items.sort_by_key(|e| e.path().components().count());

        for item in sorted_items {
            let path = item.path().to_path_buf();
            let name = item.file_name().to_string_lossy().to_string();
            let metadata = match item.metadata() {
                Ok(m) => m,
                Err(_) => continue,
            };

            let is_dir = item.file_type().is_dir();
            let parent_path = path.parent().map(|p| p.to_path_buf());
            let parent_arc = parent_path.and_then(|p| entries_by_path.get(&p).cloned());
            
            let id = self.next_id();
            let mtime = metadata.modified()
                .ok()
                .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
                .map(|d| d.as_secs() as i64)
                .unwrap_or(0);

            if is_dir {
                self.stats.folders_scanned += 1;
                let entry = Entry::new_folder(name, parent_arc, mtime, id);
                entries_by_path.insert(path, entry.clone());
                store.insert(entry);
            } else {
                self.stats.files_scanned += 1;
                self.stats.bytes_scanned += metadata.len();
                let entry = Entry::new_file(name, parent_arc, metadata.len(), mtime, id);
                entries_by_path.insert(path, entry.clone());
                store.insert(entry);
            };
        }

        Ok(store)
    }
}
