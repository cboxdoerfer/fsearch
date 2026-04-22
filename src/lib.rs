//! Library exports for FSearch

pub mod core;
pub mod gui;

pub use core::*;

// Re-export commonly used types
pub use core::model::{Entry, EntryStore, EntryType, SearchResult, ResultView};
pub use core::scanner::{Scanner, ScanConfig};
pub use core::search::{Query, QueryParser, SearchEngine};
pub use core::monitor::{FileSystemMonitor, MonitoredIndex};
