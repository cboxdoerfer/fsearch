//! Core data model for FSearch file index
//!
//! This module provides memory-efficient data structures for storing millions of file entries.
//! Key optimizations:
//! - Uses `Arc<str>` for string interning to share path components
//! - Compact representation of file metadata using packed structs
//! - Lock-free concurrent access patterns for read-heavy workloads
//! - String pool to reduce memory fragmentation

use std::sync::Arc;
use std::fmt;
use std::mem::size_of;
use std::sync::atomic::{AtomicU32, Ordering};
use compact_str::CompactString;
use time::OffsetDateTime;

/// A cheaply cloneable string type that stores up to 24 bytes on the stack.
/// Falls back to heap storage for longer strings.
pub type FastString = CompactString;

/// File entry type classification
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum EntryType {
    File = 0,
    Folder = 1,
}

impl EntryType {
    #[inline]
    pub fn is_file(self) -> bool {
        matches!(self, EntryType::File)
    }

    #[inline]
    pub fn is_folder(self) -> bool {
        matches!(self, EntryType::Folder)
    }
}

/// A globally unique entry ID assigned during indexing
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(transparent)]
pub struct EntryId(u64);

impl EntryId {
    #[inline]
    pub fn new(id: u64) -> Self {
        Self(id)
    }

    #[inline]
    pub fn as_u64(self) -> u64 {
        self.0
    }
}

/// File metadata attributes stored in a compact form
#[derive(Debug, Clone, Copy)]
pub struct FileAttributes {
    /// File size in bytes (0 for directories if not tracked)
    pub size: u64,
    /// Last modification time (Unix timestamp)
    pub mtime: i64,
    /// Creation time (Unix timestamp, may be 0 if unavailable)
    pub ctime: i64,
    /// Entry ID (for fast indexing)
    pub entry_id: u32,
    /// Entry type flags
    pub flags: EntryFlags,
}

impl FileAttributes {
    /// Size of this struct in bytes - should be exactly 32 bytes
    pub const SIZE: usize = size_of::<Self>();

    #[inline]
    pub fn new(size: u64, mtime: i64, entry_id: u32, entry_type: EntryType) -> Self {
        Self {
            size,
            mtime,
            ctime: 0,
            entry_id,
            flags: EntryFlags::from_entry_type(entry_type),
        }
    }

    #[inline]
    pub fn modification_time(&self) -> Option<OffsetDateTime> {
        OffsetDateTime::from_unix_timestamp(self.mtime).ok()
    }
}

/// Bit flags for entry attributes
#[derive(Debug, Clone, Copy, Default)]
pub struct EntryFlags(u8);

impl EntryFlags {
    const IS_FOLDER: u8 = 1 << 0;
    const IS_HIDDEN: u8 = 1 << 1;
    const IS_SYMLINK: u8 = 1 << 2;
    const NEEDS_UPDATE: u8 = 1 << 3;

    #[inline]
    pub fn from_entry_type(entry_type: EntryType) -> Self {
        match entry_type {
            EntryType::Folder => Self(Self::IS_FOLDER),
            EntryType::File => Self(0),
        }
    }

    #[inline]
    pub fn is_folder(&self) -> bool {
        self.0 & Self::IS_FOLDER != 0
    }

    #[inline]
    pub fn is_hidden(&self) -> bool {
        self.0 & Self::IS_HIDDEN != 0
    }

    #[inline]
    pub fn set_hidden(&mut self, hidden: bool) {
        if hidden {
            self.0 |= Self::IS_HIDDEN;
        } else {
            self.0 &= !Self::IS_HIDDEN;
        }
    }

    #[inline]
    pub fn is_symlink(&self) -> bool {
        self.0 & Self::IS_SYMLINK != 0
    }

    #[inline]
    pub fn entry_type(&self) -> EntryType {
        if self.is_folder() {
            EntryType::Folder
        } else {
            EntryType::File
        }
    }
}

/// A file or directory entry in the database
///
/// Memory layout considerations:
/// - Parent is stored as an Option<Arc<Entry>> to share ownership in tree
/// - Name uses CompactString which uses 24 bytes inline (no allocation for most names)
/// - Attributes are stored separately to improve cache locality during searches
pub struct Entry {
    /// Shared ownership of parent entry (None for root entries)
    pub parent: Option<Arc<Entry>>,
    /// Entry name (just the file/directory name, not full path)
    pub name: FastString,
    /// Extended name storage for very long names (rare)
    pub name_ext: Option<Arc<str>>,
    /// File metadata
    pub attrs: FileAttributes,
    /// Child counters (only meaningful for folders)
    pub child_stats: ChildStats,
}

/// Statistics about an entry's children (only used for folders)
#[derive(Debug, Clone, Copy, Default)]
pub struct ChildStats {
    pub num_files: u32,
    pub num_folders: u32,
}

impl ChildStats {
    #[inline]
    pub fn total(&self) -> u32 {
        self.num_files.saturating_add(self.num_folders)
    }
}

impl Entry {
    /// Create a new file entry
    pub fn new_file(
        name: impl AsRef<str>,
        parent: Option<Arc<Entry>>,
        size: u64,
        mtime: i64,
        entry_id: u32,
    ) -> Arc<Self> {
        let name_str = name.as_ref();
        let (name, name_ext) = Self::make_name(name_str);

        Arc::new(Self {
            parent,
            name,
            name_ext,
            attrs: FileAttributes::new(size, mtime, entry_id, EntryType::File),
            child_stats: ChildStats::default(),
        })
    }

    /// Create a new folder entry
    pub fn new_folder(
        name: impl AsRef<str>,
        parent: Option<Arc<Entry>>,
        mtime: i64,
        entry_id: u32,
    ) -> Arc<Self> {
        let name_str = name.as_ref();
        let (name, name_ext) = Self::make_name(name_str);

        Arc::new(Self {
            parent,
            name,
            name_ext,
            attrs: FileAttributes::new(0, mtime, entry_id, EntryType::Folder),
            child_stats: ChildStats::default(),
        })
    }

    /// Split name into inline (CompactString) and heap (Arc<str>) parts if needed
    #[inline]
    fn make_name(name: &str) -> (FastString, Option<Arc<str>>) {
        // CompactString can store up to 24 bytes inline.
        const INLINE_LIMIT: usize = 23;

        if name.len() > INLINE_LIMIT {
            // Find a safe character boundary at or before INLINE_LIMIT
            let mut end = INLINE_LIMIT;
            while !name.is_char_boundary(end) {
                end -= 1;
            }
            
            // For long names, we keep a safe prefix in FastString and the full name in Arc
            let inline = FastString::from(&name[..end]);
            let full = Arc::from(name);
            (inline, Some(full))
        } else {
            (FastString::from(name), None)
        }
    }

    /// Get the full name (handles long names stored in Arc)
    #[inline]
    pub fn full_name(&self) -> &str {
        match &self.name_ext {
            Some(arc) => arc.as_ref(),
            None => self.name.as_str(),
        }
    }

    /// Get the extension of this entry (if any)
    #[inline]
    pub fn extension(&self) -> Option<&str> {
        let name = self.full_name();
        name.rfind('.').map(|i| &name[i + 1..])
    }

    /// Check if this entry is a folder
    #[inline]
    pub fn is_folder(&self) -> bool {
        self.attrs.flags.is_folder()
    }

    /// Check if this entry is a file
    #[inline]
    pub fn is_file(&self) -> bool {
        !self.attrs.flags.is_folder()
    }

    /// Get entry type
    #[inline]
    pub fn entry_type(&self) -> EntryType {
        self.attrs.flags.entry_type()
    }

    /// Check if this entry is a descendant of another entry
    pub fn is_descendant_of(&self, ancestor: &Arc<Entry>) -> bool {
        let mut current = self.parent.as_ref();
        while let Some(parent) = current {
            if Arc::ptr_eq(parent, ancestor) {
                return true;
            }
            current = parent.parent.as_ref();
        }
        false
    }

    /// Build full path by walking up parent chain
    pub fn build_path(&self, buf: &mut String) {
        if let Some(ref parent) = self.parent {
            parent.build_path(buf);
            if !buf.ends_with('/') {
                buf.push('/');
            }
        }
        buf.push_str(self.full_name());
    }

    /// Get full path as a new string (use sparingly - prefer build_path for batch operations)
    pub fn full_path(&self) -> String {
        let mut buf = String::with_capacity(256);
        self.build_path(&mut buf);
        buf
    }

    /// Get the parent path component
    pub fn parent_path(&self) -> Option<String> {
        self.parent.as_ref().map(|p| p.full_path())
    }

    /// Get entry ID
    #[inline]
    pub fn id(&self) -> u32 {
        self.attrs.entry_id
    }

    /// Get file size
    #[inline]
    pub fn size(&self) -> u64 {
        self.attrs.size
    }

    /// Get modification time
    #[inline]
    pub fn mtime(&self) -> i64 {
        self.attrs.mtime
    }

    /// Get depth in the tree (number of ancestors)
    pub fn depth(&self) -> usize {
        let mut depth = 0;
        let mut current = self.parent.as_ref();
        while let Some(parent) = current {
            depth += 1;
            current = parent.parent.as_ref();
        }
        depth
    }

    /// Compare two entries by their full paths (for sorting)
    pub fn compare_by_full_path(a: &Arc<Entry>, b: &Arc<Entry>) -> std::cmp::Ordering {
        // Optimization: first compare parents
        match (&a.parent, &b.parent) {
            (None, None) => a.full_name().cmp(b.full_name()),
            (None, Some(_)) => std::cmp::Ordering::Less,
            (Some(_), None) => std::cmp::Ordering::Greater,
            (Some(p1), Some(p2)) => {
                let parent_cmp = Entry::compare_by_full_path(p1, p2);
                if parent_cmp != std::cmp::Ordering::Equal {
                    return parent_cmp;
                }
                a.full_name().cmp(b.full_name())
            }
        }
    }

    /// Compare two entries by name only
    pub fn compare_by_name(a: &Arc<Entry>, b: &Arc<Entry>) -> std::cmp::Ordering {
        a.full_name().cmp(b.full_name())
    }

    /// Compare by file size
    pub fn compare_by_size(a: &Arc<Entry>, b: &Arc<Entry>) -> std::cmp::Ordering {
        a.attrs.size.cmp(&b.attrs.size)
    }

    /// Compare by modification time
    pub fn compare_by_mtime(a: &Arc<Entry>, b: &Arc<Entry>) -> std::cmp::Ordering {
        a.attrs.mtime.cmp(&b.attrs.mtime)
    }
}

impl fmt::Debug for Entry {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry")
            .field("name", &self.full_name())
            .field("type", &self.entry_type())
            .field("size", &self.attrs.size)
            .field("id", &self.attrs.entry_id)
            .finish()
    }
}

/// Memory-optimized storage for file entries using a chunked approach
///
/// Similar to the C version's chunked array, this allows:
/// - Efficient bulk insertions
/// - Memory-stable growth without frequent reallocations
/// - Good cache locality during searches
pub struct EntryStore {
    /// Chunks of file entries
    file_chunks: Vec<Vec<Arc<Entry>>>,
    /// Chunks of folder entries
    folder_chunks: Vec<Vec<Arc<Entry>>>,
    /// Total count of files
    num_files: AtomicU32,
    /// Total count of folders
    num_folders: AtomicU32,
    /// Chunk size for new allocations
    chunk_size: usize,
}

impl Default for EntryStore {
    fn default() -> Self {
        Self::with_chunk_size(4096)
    }
}

impl EntryStore {
    /// Create a new entry store with specified chunk size
    pub fn with_chunk_size(chunk_size: usize) -> Self {
        Self {
            file_chunks: Vec::new(),
            folder_chunks: Vec::new(),
            num_files: AtomicU32::new(0),
            num_folders: AtomicU32::new(0),
            chunk_size,
        }
    }

    /// Insert a single entry into the store
    pub fn insert(&mut self, entry: Arc<Entry>) {
        if entry.is_folder() {
            self.insert_folder(entry);
        } else {
            self.insert_file(entry);
        }
    }

    fn insert_file(&mut self, entry: Arc<Entry>) {
        // Fast path: try to add to current chunk
        if let Some(last) = self.file_chunks.last_mut() {
            if last.len() < self.chunk_size {
                last.push(entry);
                self.num_files.fetch_add(1, Ordering::Relaxed);
                return;
            }
        }
        // Slow path: create new chunk
        let mut new_chunk = Vec::with_capacity(self.chunk_size);
        new_chunk.push(entry);
        self.file_chunks.push(new_chunk);
        self.num_files.fetch_add(1, Ordering::Relaxed);
    }

    fn insert_folder(&mut self, entry: Arc<Entry>) {
        if let Some(last) = self.folder_chunks.last_mut() {
            if last.len() < self.chunk_size {
                last.push(entry);
                self.num_folders.fetch_add(1, Ordering::Relaxed);
                return;
            }
        }
        let mut new_chunk = Vec::with_capacity(self.chunk_size);
        new_chunk.push(entry);
        self.folder_chunks.push(new_chunk);
        self.num_folders.fetch_add(1, Ordering::Relaxed);
    }

    /// Insert multiple entries efficiently
    pub fn insert_batch(&mut self, entries: Vec<Arc<Entry>>) {
        // Separate files and folders for better locality
        let (folders, files): (Vec<_>, Vec<_>) = entries
            .into_iter()
            .partition(|e| e.is_folder());

        self.insert_files_batch(files);
        self.insert_folders_batch(folders);
    }

    fn insert_files_batch(&mut self, mut files: Vec<Arc<Entry>>) {
        if files.is_empty() {
            return;
        }

        // Extend current chunk if there's room
        if let Some(last) = self.file_chunks.last_mut() {
            let remaining = self.chunk_size.saturating_sub(last.len());
            if remaining > 0 {
                let to_add = remaining.min(files.len());
                last.extend(files.drain(..to_add));
            }
        }

        // Add remaining as new chunks
        for chunk in files.chunks(self.chunk_size) {
            self.file_chunks.push(chunk.to_vec());
        }

        self.num_files.fetch_add(files.len() as u32, Ordering::Relaxed);
    }

    fn insert_folders_batch(&mut self, mut folders: Vec<Arc<Entry>>) {
        if folders.is_empty() {
            return;
        }

        if let Some(last) = self.folder_chunks.last_mut() {
            let remaining = self.chunk_size.saturating_sub(last.len());
            if remaining > 0 {
                let to_add = remaining.min(folders.len());
                last.extend(folders.drain(..to_add));
            }
        }

        for chunk in folders.chunks(self.chunk_size) {
            self.folder_chunks.push(chunk.to_vec());
        }

        self.num_folders.fetch_add(folders.len() as u32, Ordering::Relaxed);
    }

    /// Get total number of files
    #[inline]
    pub fn file_count(&self) -> u32 {
        self.num_files.load(Ordering::Relaxed)
    }

    /// Get total number of folders
    #[inline]
    pub fn folder_count(&self) -> u32 {
        self.num_folders.load(Ordering::Relaxed)
    }

    /// Get total entry count
    #[inline]
    pub fn total_count(&self) -> u64 {
        self.file_count() as u64 + self.folder_count() as u64
    }

    /// Iterate over all files (in chunk order)
    pub fn iter_files(&self) -> impl Iterator<Item = &Arc<Entry>> {
        self.file_chunks.iter().flat_map(|chunk| chunk.iter())
    }

    /// Iterate over all folders (in chunk order)
    pub fn iter_folders(&self) -> impl Iterator<Item = &Arc<Entry>> {
        self.folder_chunks.iter().flat_map(|chunk| chunk.iter())
    }

    /// Iterate over all entries (folders first, then files)
    pub fn iter_all(&self) -> impl Iterator<Item = &Arc<Entry>> {
        self.iter_folders().chain(self.iter_files())
    }

    /// Get a flat vector of all files (for sorting)
    pub fn get_all_files(&self) -> Vec<Arc<Entry>> {
        self.file_chunks
            .iter()
            .flat_map(|c| c.iter().cloned())
            .collect()
    }

    /// Get a flat vector of all folders (for sorting)
    pub fn get_all_folders(&self) -> Vec<Arc<Entry>> {
        self.folder_chunks
            .iter()
            .flat_map(|c| c.iter().cloned())
            .collect()
    }

    /// Sort all entries by full path using the provided comparison function
    pub fn sort_by<F>(&mut self, mut compare: F)
    where
        F: FnMut(&Arc<Entry>, &Arc<Entry>) -> std::cmp::Ordering,
    {
        // Sort within each chunk first
        for chunk in &mut self.file_chunks {
            chunk.sort_by(&mut compare);
        }
        for chunk in &mut self.folder_chunks {
            chunk.sort_by(&mut compare);
        }

        // If multiple chunks, need to merge-sort them
        if self.file_chunks.len() > 1 {
            let mut all = self.get_all_files();
            all.sort_by(&mut compare);
            self.file_chunks.clear();
            for chunk in all.chunks(self.chunk_size) {
                self.file_chunks.push(chunk.to_vec());
            }
        }

        if self.folder_chunks.len() > 1 {
            let mut all = self.get_all_folders();
            all.sort_by(&mut compare);
            self.folder_chunks.clear();
            for chunk in all.chunks(self.chunk_size) {
                self.folder_chunks.push(chunk.to_vec());
            }
        }
    }

    /// Clear all entries
    pub fn clear(&mut self) {
        self.file_chunks.clear();
        self.folder_chunks.clear();
        self.num_files.store(0, Ordering::Relaxed);
        self.num_folders.store(0, Ordering::Relaxed);
    }

    /// Reserve capacity for additional entries
    pub fn reserve(&mut self, additional_files: usize, additional_folders: usize) {
        let file_chunks_needed = (additional_files + self.chunk_size - 1) / self.chunk_size;
        self.file_chunks.reserve(file_chunks_needed);

        let folder_chunks_needed = (additional_folders + self.chunk_size - 1) / self.chunk_size;
        self.folder_chunks.reserve(folder_chunks_needed);
    }

    /// Estimate memory usage in bytes
    pub fn estimated_memory_usage(&self) -> usize {
        let entry_size = size_of::<Arc<Entry>>() + size_of::<Entry>() + 64; // rough estimate
        (self.total_count() as usize) * entry_size
    }
}

impl fmt::Debug for EntryStore {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("EntryStore")
            .field("file_chunks", &self.file_chunks.len())
            .field("folder_chunks", &self.folder_chunks.len())
            .field("num_files", &self.file_count())
            .field("num_folders", &self.folder_count())
            .field("chunk_size", &self.chunk_size)
            .finish()
    }
}

unsafe impl Send for EntryStore {}
unsafe impl Sync for EntryStore {}

/// Search result struct containing metadata about a match
#[derive(Debug, Clone)]
pub struct SearchResult {
    pub entry: Arc<Entry>,
    pub score: f32,
    pub highlights: Vec<(usize, usize)>, // Start, end positions for highlights
}

impl SearchResult {
    pub fn new(entry: Arc<Entry>, score: f32) -> Self {
        Self {
            entry,
            score,
            highlights: Vec::new(),
        }
    }

    pub fn with_highlights(entry: Arc<Entry>, score: f32, highlights: Vec<(usize, usize)>) -> Self {
        Self {
            entry,
            score,
            highlights,
        }
    }
}

/// A view into the database for sorted/paginated results
pub struct ResultView {
    results: Vec<SearchResult>,
    total_count: usize,
}

impl ResultView {
    pub fn new(results: Vec<SearchResult>, total_count: usize) -> Self {
        Self {
            results,
            total_count,
        }
    }

    /// Get a page of results
    pub fn get_page(&self, page: usize, page_size: usize) -> &[SearchResult] {
        let start = page * page_size;
        let end = (start + page_size).min(self.results.len());
        if start >= self.results.len() {
            return &[];
        }
        &self.results[start..end]
    }

    #[inline]
    pub fn total(&self) -> usize {
        self.total_count
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.results.len()
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.results.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_entry_creation() {
        let file = Entry::new_file("test.txt", None, 1024, 1234567890, 1);
        assert_eq!(file.full_name(), "test.txt");
        assert!(file.is_file());
        assert!(!file.is_folder());
        assert_eq!(file.size(), 1024);
    }

    #[test]
    fn test_folder_creation() {
        let folder = Entry::new_folder("documents", None, 1234567890, 2);
        assert_eq!(folder.full_name(), "documents");
        assert!(folder.is_folder());
        assert!(!folder.is_file());
    }

    #[test]
    fn test_parent_relationship() {
        let parent = Entry::new_folder("home", None, 0, 1);
        let child = Entry::new_file("file.txt", Some(parent.clone()), 100, 0, 2);

        assert!(child.is_descendant_of(&parent));
        assert!(!parent.is_descendant_of(&child));
    }

    #[test]
    fn test_extension() {
        let file = Entry::new_file("document.pdf", None, 0, 0, 1);
        assert_eq!(file.extension(), Some("pdf"));

        let no_ext = Entry::new_file("README", None, 0, 0, 2);
        assert_eq!(no_ext.extension(), None);
    }

    #[test]
    fn test_entry_store() {
        let mut store = EntryStore::default();

        for i in 0..100 {
            let file = Entry::new_file(format!("file{}.txt", i), None, i as u64, 0, i);
            store.insert(file);
        }

        assert_eq!(store.file_count(), 100);
        assert_eq!(store.folder_count(), 0);
    }

    #[test]
    fn test_large_name_handling() {
        // Create a name longer than 23 bytes
        let long_name = "a".repeat(100);
        let entry = Entry::new_file(&long_name, None, 0, 0, 1);
        assert_eq!(entry.full_name(), long_name);
    }
}
