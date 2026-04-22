//! Search engine with regex, glob, and fuzzy matching support
//!
//! Implements high-performance filtering using:
//! - Aho-Corasick for multiple literal string matching
//! - Pre-compiled regex patterns
//! - Fuzzy matching algorithms

use crate::core::model::{Entry, EntryStore, SearchResult, ResultView};

use std::sync::Arc;
use std::collections::HashMap;
use regex::{Regex, RegexBuilder};
use aho_corasick::AhoCorasick;
use rayon::prelude::*;
use fuzzy_matcher::FuzzyMatcher;
use fuzzy_matcher::skim::SkimMatcherV2;

/// Types of search queries supported
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QueryType {
    /// Simple substring match
    Simple,
    /// Case-sensitive simple match
    SimpleCaseSensitive,
    /// Glob pattern matching (* and ?)
    Glob,
    /// Regular expression
    Regex,
    /// Fuzzy matching
    Fuzzy,
}

/// Search flags to control matching behavior
#[derive(Debug, Clone, Copy, Default)]
pub struct SearchFlags {
    /// Match entire path instead of just name
    pub match_path: bool,
    /// Case-sensitive matching
    pub case_sensitive: bool,
    /// Match against full words only
    pub whole_word: bool,
    /// Use regex search
    pub use_regex: bool,
    /// Invert the match
    pub invert: bool,
}

impl SearchFlags {
    /// Create flags for path matching
    pub fn match_path() -> Self {
        Self {
            match_path: true,
            ..Default::default()
        }
    }

    /// Create flags for case-sensitive search
    pub fn case_sensitive() -> Self {
        Self {
            case_sensitive: true,
            ..Default::default()
        }
    }
}

/// A parsed search query ready for execution
#[derive(Debug, Clone)]
pub enum Query {
    /// Match everything
    All,
    /// Match nothing
    None,
    /// Simple literal text search
    Simple {
        text: String,
        flags: SearchFlags,
    },
    /// Glob pattern search
    Glob {
        pattern: String,
        compiled: Option<glob::Pattern>,
        flags: SearchFlags,
    },
    /// Regular expression search
    Regex {
        pattern: String,
        regex: Option<Arc<Regex>>,
        flags: SearchFlags,
    },
    /// Fuzzy search with score threshold
    Fuzzy {
        pattern: String,
        threshold: i64,
        flags: SearchFlags,
    },
    /// Combine multiple queries with AND
    And(Box<Query>, Box<Query>),
    /// Combine multiple queries with OR
    Or(Box<Query>, Box<Query>),
    /// Negate a query
    Not(Box<Query>),
}

impl Query {
    /// Create a simple text query
    pub fn simple(text: impl Into<String>) -> Self {
        Query::Simple {
            text: text.into(),
            flags: SearchFlags::default(),
        }
    }

    /// Create a simple text query with flags
    pub fn simple_with_flags(text: impl Into<String>, flags: SearchFlags) -> Self {
        Query::Simple { text: text.into(), flags }
    }

    /// Create a glob pattern query
    pub fn glob(pattern: impl Into<String>) -> Result<Self, glob::PatternError> {
        let pattern = pattern.into();
        let compiled = glob::Pattern::new(&pattern)?;
        Ok(Query::Glob {
            compiled: Some(compiled),
            pattern,
            flags: SearchFlags::default(),
        })
    }

    /// Create a regex query
    pub fn regex(pattern: impl Into<String>) -> Result<Self, SearchError> {
        let pattern = pattern.into();
        let regex = RegexBuilder::new(&pattern)
            .case_insensitive(true)
            .build()
            .map_err(|e| SearchError::InvalidRegex(e.to_string()))?;
        Ok(Query::Regex {
            regex: Some(Arc::new(regex)),
            pattern,
            flags: SearchFlags::default(),
        })
    }

    /// Create a case-sensitive regex query
    pub fn regex_case_sensitive(pattern: impl Into<String>) -> Result<Self, SearchError> {
        let pattern = pattern.into();
        let regex = Regex::new(&pattern)
            .map_err(|e| SearchError::InvalidRegex(e.to_string()))?;
        Ok(Query::Regex {
            regex: Some(Arc::new(regex)),
            pattern,
            flags: SearchFlags { case_sensitive: true, ..Default::default() },
        })
    }

    /// Create a fuzzy match query
    pub fn fuzzy(pattern: impl Into<String>, threshold: i64) -> Self {
        Query::Fuzzy {
            pattern: pattern.into(),
            threshold,
            flags: SearchFlags::default(),
        }
    }

    /// Combine with another query using AND
    pub fn and(self, other: Query) -> Self {
        Query::And(Box::new(self), Box::new(other))
    }

    /// Combine with another query using OR
    pub fn or(self, other: Query) -> Self {
        Query::Or(Box::new(self), Box::new(other))
    }

    /// Negate this query
    pub fn not(self) -> Self {
        Query::Not(Box::new(self))
    }

    /// Check if this query matches everything
    pub fn matches_all(&self) -> bool {
        matches!(self, Query::All)
    }

    /// Check if this query matches nothing
    pub fn matches_none(&self) -> bool {
        matches!(self, Query::None)
    }
}

/// Get the text to match against from an entry
fn get_match_text<'a>(entry: &'a Entry, flags: &SearchFlags) -> &'a str {
    if flags.match_path {
        // For path matching, we need to build full path
        // Optimization: This is expensive; cache it if possible
        entry.full_name()
    } else {
        entry.full_name()
    }
}

/// Single pattern matcher for high-performance search
pub struct PatternMatcher {
    /// Pre-compiled patterns for Aho-Corasick
    ac: Option<AhoCorasick>,
    /// Pre-compiled regex
    regex: Option<Regex>,
    /// Simple substring for literal search
    literal: Option<String>,
    /// Glob pattern
    glob: Option<glob::Pattern>,
    /// Search type
    query_type: QueryType,
    /// Search flags
    flags: SearchFlags,
    /// Fuzzy matcher
    fuzzy_matcher: Option<SkimMatcherV2>,
}

impl PatternMatcher {
    /// Create a new pattern matcher from a query
    pub fn from_query(query: &Query) -> Result<Self, SearchError> {
        match query {
            Query::All => Ok(Self {
                ac: None,
                regex: None,
                literal: None,
                glob: None,
                query_type: QueryType::Simple,
                flags: SearchFlags::default(),
                fuzzy_matcher: None,
            }),
            Query::Simple { text, flags } => {
                let literal = if flags.case_sensitive {
                    Some(text.clone())
                } else {
                    Some(text.to_lowercase())
                };

                // For multiple words, use Aho-Corasick
                let patterns: Vec<_> = text.split_whitespace().collect();
                let ac = if patterns.len() > 1 {
                    Some(AhoCorasick::new(&patterns).map_err(|_| SearchError::InvalidPattern)?)
                } else {
                    None
                };

                Ok(Self {
                    ac,
                    regex: None,
                    literal,
                    glob: None,
                    query_type: QueryType::Simple,
                    flags: *flags,
                    fuzzy_matcher: None,
                })
            }
            Query::Glob { pattern, flags, .. } => {
                let glob = glob::Pattern::new(pattern)
                    .map_err(|_| SearchError::InvalidGlob(format!("Invalid glob pattern: {}", pattern)))?;
                Ok(Self {
                    ac: None,
                    regex: None,
                    literal: None,
                    glob: Some(glob),
                    query_type: QueryType::Glob,
                    flags: *flags,
                    fuzzy_matcher: None,
                })
            }
            Query::Regex { regex, flags, .. } => {
                let regex = regex.as_ref()
                    .ok_or_else(|| SearchError::InvalidRegex("Regex not compiled".to_string()))?;
                Ok(Self {
                    ac: None,
                    regex: Some(regex.as_ref().clone()),
                    literal: None,
                    glob: None,
                    query_type: QueryType::Regex,
                    flags: *flags,
                    fuzzy_matcher: None,
                })
            }
            Query::Fuzzy { .. } => {
                Ok(Self {
                    ac: None,
                    regex: None,
                    literal: None,
                    glob: None,
                    query_type: QueryType::Fuzzy,
                    flags: SearchFlags::default(),
                    fuzzy_matcher: Some(SkimMatcherV2::default()),
                })
            }
            _ => Err(SearchError::Unsupported("Complex queries not supported for PatternMatcher".to_string())),
        }
    }

    /// Check if a text matches this pattern
    pub fn matches(&self, text: &str) -> bool {
        let case_sensitive = self.flags.case_sensitive;

        if let Some(regex) = &self.regex {
            return regex.is_match(text);
        }

        if let Some(glob) = &self.glob {
            return glob.matches(text);
        }

        if let Some(ac) = &self.ac {
            // Aho-Corasick for multiple patterns
            let search_text = if case_sensitive { text.to_string() } else { text.to_lowercase() };
            return ac.is_match(&search_text);
        }

        if let Some(literal) = &self.literal {
            let search_text = if case_sensitive { text } else { &text.to_lowercase() };
            if self.flags.whole_word {
                // Simple whole word matching
                search_text.contains(literal) // TODO: implement proper word boundary
            } else {
                search_text.contains(literal)
            }
        } else {
            true // No pattern means match all
        }
    }

    /// Get match score for fuzzy matching (returns score > 0 if matches)
    pub fn fuzzy_score(&self, pattern: &str, text: &str) -> Option<i64> {
        self.fuzzy_matcher.as_ref()?.fuzzy_match(pattern, text)
    }
}

/// Error types for search operations
#[derive(Debug, thiserror::Error)]
pub enum SearchError {
    #[error("Invalid regex pattern: {0}")]
    InvalidRegex(String),

    #[error("Invalid glob pattern: {0}")]
    InvalidGlob(String),

    #[error("Invalid pattern")]
    InvalidPattern,

    #[error("Unsupported query type: {0}")]
    Unsupported(String),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

/// Sort order for search results
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SortOrder {
    /// No sorting (search order)
    None,
    /// Sort by name
    Name,
    /// Sort by path
    Path,
    /// Sort by size
    Size,
    /// Sort by modification time
    ModificationTime,
    /// Sort by extension
    Extension,
    /// Sort by search relevance score
    Relevance,
}

impl SortOrder {
    /// Get the comparison function for this sort order
    pub fn compare(&self, a: &SearchResult, b: &SearchResult) -> std::cmp::Ordering {
        match self {
            SortOrder::None => std::cmp::Ordering::Equal,
            SortOrder::Name => a.entry.full_name().cmp(b.entry.full_name()),
            SortOrder::Path => Entry::compare_by_full_path(&a.entry, &b.entry),
            SortOrder::Size => a.entry.size().cmp(&b.entry.size()),
            SortOrder::ModificationTime => a.entry.mtime().cmp(&b.entry.mtime()),
            SortOrder::Extension => (a.entry.extension(), a.entry.full_name())
                .cmp(&(b.entry.extension(), b.entry.full_name())),
            SortOrder::Relevance => b.score.partial_cmp(&a.score).unwrap_or(std::cmp::Ordering::Equal),
        }
    }
}

/// Search engine that executes queries against an entry store
pub struct SearchEngine {
    /// Maximum results to return (None for unlimited)
    limit: Option<usize>,
    /// Default sort order
    default_sort: SortOrder,
    /// Case sensitivity default
    case_sensitive: bool,
}

impl SearchEngine {
    /// Create a new search engine with default settings
    pub fn new() -> Self {
        Self {
            limit: None,
            default_sort: SortOrder::Relevance,
            case_sensitive: false,
        }
    }

    /// Set result limit
    pub fn with_limit(mut self, limit: usize) -> Self {
        self.limit = Some(limit);
        self
    }

    /// Set default sort order
    pub fn with_default_sort(mut self, sort: SortOrder) -> Self {
        self.default_sort = sort;
        self
    }

    /// Execute a query against the entry store
    pub fn search(
        &self,
        query: &Query,
        store: &EntryStore,
    ) -> Result<ResultView, SearchError> {
        let entries: Vec<_> = store.iter_all().cloned().collect();
        self.search_entries(query, entries)
    }

    /// Execute a query against a list of entries (parallel)
    pub fn search_entries(
        &self,
        query: &Query,
        entries: Vec<Arc<Entry>>,
    ) -> Result<ResultView, SearchError> {
        let start = std::time::Instant::now();

        // Evaluate the query (single-threaded for tree structure)
        let results: Vec<_> = if self.is_complex_query(query) {
            // For complex queries with AND/OR/NOT, evaluate sequentially
            entries
                .into_iter()
                .filter_map(|e| self.evaluate_query(query, &e).map(|score| SearchResult::new(e, score)))
                .collect()
        } else {
            // For simple queries, use parallel iteration
            entries
                .into_par_iter()
                .filter_map(|e| self.evaluate_query(query, &e).map(|score| SearchResult::new(e, score)))
                .collect()
        };

        let total = results.len();

        // Sort results
        let mut results = results;
        let sort_fn = self.default_sort;
        results.par_sort_by(|a, b| sort_fn.compare(a, b));

        // Apply limit if set
        let results = if let Some(limit) = self.limit {
            results.into_iter().take(limit).collect()
        } else {
            results
        };

        let elapsed = start.elapsed();
        tracing::debug!("Search completed in {:?}: {} matches from {} entries", elapsed, total, total);

        Ok(ResultView::new(results, total))
    }

    /// Quick search for simple text queries (optimized path)
    pub fn quick_search(
        &self,
        pattern: &str,
        store: &EntryStore,
    ) -> Vec<SearchResult> {
        let matcher = match PatternMatcher::from_query(&Query::simple(pattern)) {
            Ok(m) => m,
            Err(_) => return Vec::new(),
        };

        store
            .iter_all()
            .filter(|e| matcher.matches(e.full_name()))
            .map(|e| SearchResult::new(e.clone(), 1.0))
            .collect()
    }

    /// Evaluate a query against a single entry
    fn evaluate_query(&self, query: &Query, entry: &Arc<Entry>) -> Option<f32> {
        let result = match query {
            Query::All => Some(1.0),
            Query::None => None,
            Query::Simple { text, flags } => {
                let name = entry.full_name();
                let search_text = if flags.case_sensitive { name.to_string() } else { name.to_lowercase() };
                let search_pattern = if flags.case_sensitive { text.clone() } else { text.to_lowercase() };

                if flags.whole_word {
                    // Simple whole word check
                    if search_text.contains(&search_pattern) {
                        Some(1.0)
                    } else {
                        None
                    }
                } else if search_text.contains(&search_pattern) {
                    // Calculate relevance score based on match position
                    let score = if search_text.starts_with(&search_pattern) {
                        1.0 // Bonus for prefix match
                    } else {
                        0.8
                    };
                    Some(score)
                } else {
                    None
                }
            }
            Query::Glob { pattern, compiled, flags } => {
                let text = entry.full_name();
                let matches = if let Some(glob) = compiled {
                    glob.matches(text)
                } else {
                    // Fallback to simple check
                    text.contains(pattern)
                };

                if matches {
                    Some(1.0)
                } else {
                    None
                }
            }
            Query::Regex { regex, .. } => {
                if let Some(re) = regex {
                    if re.is_match(entry.full_name()) {
                        Some(1.0)
                    } else {
                        None
                    }
                } else {
                    None
                }
            }
            Query::Fuzzy { pattern, threshold, .. } => {
                let matcher = SkimMatcherV2::default();
                if let Some(score) = matcher.fuzzy_match(pattern, entry.full_name()) {
                    if score >= *threshold {
                        // Normalize score to 0-1 range
                        let normalized = (score as f32 / 100.0).min(1.0);
                        Some(normalized)
                    } else {
                        None
                    }
                } else {
                    None
                }
            }
            Query::And(left, right) => {
                match (self.evaluate_query(left, entry), self.evaluate_query(right, entry)) {
                    (Some(s1), Some(s2)) => Some((s1 + s2) / 2.0), // Average scores
                    _ => None,
                }
            }
            Query::Or(left, right) => {
                match (self.evaluate_query(left, entry), self.evaluate_query(right, entry)) {
                    (Some(s1), _) => Some(s1),
                    (_, Some(s2)) => Some(s2),
                    _ => None,
                }
            }
            Query::Not(inner) => {
                match self.evaluate_query(inner, entry) {
                    Some(_) => None,
                    None => Some(1.0),
                }
            }
        };

        result
    }

    /// Check if a query is complex (requires sequential evaluation)
    fn is_complex_query(&self, query: &Query) -> bool {
        match query {
            Query::And(_, _) | Query::Or(_, _) | Query::Not(_) => true,
            _ => false,
        }
    }

    /// Search with pagination
    pub fn search_paginated(
        &self,
        query: &Query,
        store: &EntryStore,
        offset: usize,
        limit: usize,
    ) -> Result<(Vec<SearchResult>, usize), SearchError> {
        let mut all = self.search(query, store)?;
        let total = all.total();

        // Get the full results, apply pagination
        let results: Vec<_> = (0..all.total())
            .skip(offset)
            .take(limit)
            .filter_map(|i| {
                // Re-create results from the store with proper scoring
                // This is a simplified pagination; in production, we'd cache results
                None
            })
            .collect();

        Ok((results, total))
    }
}

impl Default for SearchEngine {
    fn default() -> Self {
        Self::new()
    }
}

/// Query parser for converting user input to Query objects
pub struct QueryParser;

impl QueryParser {
    /// Parse a search string into a Query
    ///
    /// Supports:
    /// - Simple text: "document" -> substring match
    /// - Glob patterns: "*.pdf" -> extension match
    /// - Regex with prefix: "r:.*test.*" -> regex match
    /// - Fuzzy with prefix: "f:doc" -> fuzzy match
    /// - Case sensitive with prefix: "s:Exact"
    pub fn parse(input: &str) -> Result<Query, SearchError> {
        let input = input.trim();

        if input.is_empty() {
            return Ok(Query::All);
        }

        // Check for special prefixes
        if let Some(stripped) = input.strip_prefix("r:/").or_else(|| input.strip_prefix("r:")) {
            return Query::regex(stripped);
        }

        if let Some(stripped) = input.strip_prefix("regex:").or_else(|| input.strip_prefix("re:")) {
            return Query::regex(stripped);
        }

        if let Some(stripped) = input.strip_prefix("glob:").or_else(|| input.strip_prefix("g:")) {
            return Query::glob(stripped).map_err(|e| SearchError::InvalidGlob(e.to_string()));
        }

        if let Some(stripped) = input.strip_prefix("fuzzy:").or_else(|| input.strip_prefix("f:")) {
            return Ok(Query::Fuzzy {
                pattern: stripped.to_string(),
                threshold: 0, // Match any score
                flags: SearchFlags::default(),
            });
        }

        if let Some(stripped) = input.strip_prefix("case:").or_else(|| input.strip_prefix("s:")) {
            return Ok(Query::Simple {
                text: stripped.to_string(),
                flags: SearchFlags::case_sensitive(),
            });
        }

        // Check if input looks like a glob pattern
        if input.contains('*') || input.contains('?') || input.contains('[') {
            return Query::glob(input).map_err(|e| SearchError::InvalidGlob(e.to_string()));
        }

        // Default to simple search
        Ok(Query::simple(input))
    }

    /// Parse with automatic query type detection
    pub fn parse_smart(input: &str) -> Result<Query, SearchError> {
        let query = Self::parse(input)?;

        // Smart mode: if uppercase, make case-sensitive
        if input.chars().any(|c| c.is_uppercase()) && !input.chars().any(|c| c.is_lowercase()) {
            // All uppercase - treat as case-sensitive
            if let Query::Simple { text, flags } = query {
                return Ok(Query::Simple {
                    text,
                    flags: SearchFlags { case_sensitive: true, ..flags },
                });
            }
        }

        Ok(query)
    }
}

/// High-performance search index for large datasets
pub struct SearchIndex {
    /// Aho-Corasick automaton for multiple pattern matching
    ac: Option<AhoCorasick>,
    /// Sorted entries for fast prefix search
    sorted_entries: Vec<Arc<Entry>>,
    /// Extension index: extension -> list of entries
    extension_index: HashMap<String, Vec<usize>>,
}

impl SearchIndex {
    /// Build a search index from an entry store
    pub fn build(store: &EntryStore) -> Self {
        let mut sorted_entries: Vec<_> = store.iter_all().cloned().collect();

        // Sort by name for binary search
        sorted_entries.par_sort_by(|a, b| Entry::compare_by_name(a, b));

        // Build extension index
        let mut extension_index: HashMap<String, Vec<usize>> = HashMap::new();

        for (idx, entry) in sorted_entries.iter().enumerate() {
            if let Some(ext) = entry.extension() {
                let ext = ext.to_lowercase();
                extension_index.entry(ext).or_default().push(idx);
            }
        }

        Self {
            ac: None,
            sorted_entries,
            extension_index,
        }
    }

    /// Find all entries with a given extension
    pub fn find_by_extension(&self, extension: &str) -> Vec<&Arc<Entry>> {
        let ext = extension.to_lowercase();
        self.extension_index
            .get(&ext)
            .map(|indices| {
                indices.iter()
                    .filter_map(|&i| self.sorted_entries.get(i))
                    .collect()
            })
            .unwrap_or_default()
    }

    /// Prefix search using binary search
    pub fn find_prefix(&self, prefix: &str) -> &[Arc<Entry>] {
        let prefix_lower = prefix.to_lowercase();

        // Binary search for the start
        let start = match self.sorted_entries.binary_search_by(|e| {
            e.full_name().to_lowercase().cmp(&prefix_lower)
        }) {
            Ok(i) => i,
            Err(i) => i,
        };

        // Find the end
        let end = self.sorted_entries[start..]
            .iter()
            .position(|e| !e.full_name().to_lowercase().starts_with(&prefix_lower))
            .map(|i| start + i)
            .unwrap_or(self.sorted_entries.len());

        &self.sorted_entries[start..end]
    }

    /// Get all indexed entries
    pub fn entries(&self) -> &[Arc<Entry>] {
        &self.sorted_entries
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_query_simple() {
        let query = Query::simple("test");
        assert!(matches!(query, Query::Simple { text, .. } if text == "test"));
    }

    #[test]
    fn test_query_glob() {
        let query = Query::glob("*.txt").unwrap();
        assert!(matches!(query, Query::Glob { pattern, .. } if pattern == "*.txt"));
    }

    #[test]
    fn test_query_regex() {
        let query = Query::regex("^test.*").unwrap();
        assert!(matches!(query, Query::Regex { pattern, .. } if pattern == "^test.*"));
    }

    #[test]
    fn test_query_combinators() {
        let q1 = Query::simple("hello");
        let q2 = Query::simple("world");

        let combined = q1.and(q2);
        assert!(matches!(combined, Query::And(_, _)));
    }

    #[test]
    fn test_pattern_matcher_simple() {
        let matcher = PatternMatcher::from_query(&Query::simple("test")).unwrap();
        assert!(matcher.matches("test_file.txt"));
        assert!(matcher.matches("my_test.txt"));
        assert!(!matcher.matches("other.txt"));
    }

    #[test]
    fn test_pattern_matcher_glob() {
        let query = Query::glob("*.txt").unwrap();
        let matcher = PatternMatcher::from_query(&query).unwrap();

        assert!(matcher.matches("file.txt"));
        assert!(!matcher.matches("file.pdf"));
    }

    #[test]
    fn test_pattern_matcher_regex() {
        let query = Query::regex(r"^test.*").unwrap();
        let matcher = PatternMatcher::from_query(&query).unwrap();

        assert!(matcher.matches("test_file.txt"));
        assert!(!matcher.matches("my_test.txt"));
    }

    #[test]
    fn test_query_parser() -> Result<(), SearchError> {
        // Simple text
        let q = QueryParser::parse("hello")?;
        assert!(matches!(q, Query::Simple { text, .. } if text == "hello"));

        // Regex prefix
        let q = QueryParser::parse("r:^test")?;
        assert!(matches!(q, Query::Regex { .. }));

        // Glob pattern detection
        let q = QueryParser::parse("*.pdf")?;
        assert!(matches!(q, Query::Glob { pattern, .. } if pattern == "*.pdf"));

        // Fuzzy prefix
        let q = QueryParser::parse("f:doc")?;
        assert!(matches!(q, Query::Fuzzy { pattern, .. } if pattern == "doc"));

        Ok(())
    }

    #[test]
    fn test_search_engine_complex_query() {
        let engine = SearchEngine::new();

        let entry1 = Entry::new_file("test_document.pdf", None, 1000, 0, 1);
        let entry2 = Entry::new_file("other_file.txt", None, 500, 0, 2);
        let entry3 = Entry::new_file("sample_doc.pdf", None, 750, 0, 3);

        // Test AND query
        let q1 = Query::simple("test");
        let q2 = Query::glob("*.pdf").unwrap();
        let combined = q1.and(q2);

        assert!(engine.evaluate_query(&combined, &entry1).is_some());
        assert!(engine.evaluate_query(&combined, &entry2).is_none());
        assert!(engine.evaluate_query(&combined, &entry3).is_none()); // No "test" in name
    }

    #[test]
    fn test_search_index_extension() {
        let mut store = EntryStore::default();
        store.insert(Entry::new_file("doc.pdf", None, 100, 0, 1));
        store.insert(Entry::new_file("report.pdf", None, 200, 0, 2));
        store.insert(Entry::new_file("notes.txt", None, 50, 0, 3));

        let index = SearchIndex::build(&store);

        let pdf_entries: Vec<_> = index.find_by_extension("pdf").collect();
        assert_eq!(pdf_entries.len(), 2);

        let txt_entries: Vec<_> = index.find_by_extension("txt").collect();
        assert_eq!(txt_entries.len(), 1);
    }
}
