//! Core module - Contains fundamental data structures and indexing logic

pub mod model;
pub mod scanner;
pub mod search;
pub mod monitor;

pub use model::{Entry, EntryStore, EntryType, SearchResult, ResultView, FileAttributes, FastString};
pub use scanner::{Scanner, ScanConfig, ExcludeManager, ExcludeRule, ExcludeType, ExcludeScope, ExcludeTarget};
pub use search::{SearchEngine, Query, QueryParser, SearchIndex, SortOrder, QueryType, SearchFlags};
pub use monitor::{FileSystemMonitor, MonitoredIndex, IndexUpdater, MonitorEvent, MonitorConfig, MonitorStats, MonitorEventType};
