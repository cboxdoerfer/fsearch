#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_limits.h>
#include <src/fsearch_memory_pool.h>
#include <src/fsearch_query.h>

typedef struct QueryTest {
    const char *needle;
    const char *haystack;
    bool is_dir;
    off_t size;
    FsearchQueryFlags flags;
    bool result;
} QueryTest;

static void
test_query(QueryTest *t) {
    FsearchFilterManager *manager = fsearch_filter_manager_new_with_defaults();
    FsearchMemoryPool *file_pool =
        fsearch_memory_pool_new(100, db_entry_get_sizeof_file_entry(), (GDestroyNotify)db_entry_destroy);
    FsearchMemoryPool *folder_pool =
        fsearch_memory_pool_new(100, db_entry_get_sizeof_folder_entry(), (GDestroyNotify)db_entry_destroy);

    FsearchQuery *q = fsearch_query_new(t->needle, NULL, manager, t->flags, "debug_query");

    FsearchDatabaseEntry *entry = NULL;
    if (g_str_has_prefix(t->haystack, "/")) {
        const char *haystack = t->haystack + 1;
        g_auto(GStrv) names = g_strsplit(haystack, "/", -1);
        const guint names_len = g_strv_length(names);

        entry = fsearch_memory_pool_malloc(folder_pool);
        db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
        db_entry_set_name(entry, "");

        if (names_len > 0) {
            for (int i = 0; i < names_len - 1; i++) {
                FsearchDatabaseEntry *old = entry;
                entry = fsearch_memory_pool_malloc(folder_pool);
                db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FOLDER);
                db_entry_set_name(entry, names[i]);
                db_entry_set_parent(entry, (FsearchDatabaseEntryFolder *)old);
            }
            FsearchDatabaseEntry *old = entry;
            entry = fsearch_memory_pool_malloc(t->is_dir ? folder_pool : file_pool);
            db_entry_set_name(entry, names[names_len - 1]);
            db_entry_set_type(entry, t->is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);
            db_entry_set_size(entry, t->size);
            db_entry_set_parent(entry, (FsearchDatabaseEntryFolder *)old);
        }
    }
    else {
        entry = fsearch_memory_pool_malloc(t->is_dir ? folder_pool : file_pool);
        db_entry_set_name(entry, t->haystack);
        db_entry_set_size(entry, t->size);
        db_entry_set_type(entry, t->is_dir ? DATABASE_ENTRY_TYPE_FOLDER : DATABASE_ENTRY_TYPE_FILE);
    }

    FsearchQueryMatchData *match_data = fsearch_query_match_data_new();
    fsearch_query_match_data_set_entry(match_data, entry);

    const bool found = fsearch_query_match(q, match_data);
    g_clear_pointer(&manager, fsearch_filter_manager_free);
    g_clear_pointer(&q, fsearch_query_unref);
    g_clear_pointer(&match_data, fsearch_query_match_data_free);

    if (found != t->result) {
        g_printerr("[%s] should%s match [name:%s, size:%ld]\n", t->needle, t->result ? "" : " NOT", t->haystack, t->size);
    }
    g_assert_true(found == t->result);

    g_clear_pointer(&file_pool, fsearch_memory_pool_free_pool);
    g_clear_pointer(&folder_pool, fsearch_memory_pool_free_pool);
}

static bool
set_locale(const char *locale) {
    char *current_locale = setlocale(LC_CTYPE, NULL);

    if (strcmp(locale, current_locale) != 0) {
        setlocale(LC_CTYPE, locale);
        current_locale = setlocale(LC_CTYPE, NULL);

        if (strncmp(current_locale, locale, 2) != 0) {
            g_printerr("Failed to set locale to %s. Skipping test.\n", locale);
            return false;
        }
    }
    return true;
}

static void
test_main(void) {
    if (set_locale("en_US.UTF-8")) {
        QueryTest main_tests[] = {
            // Mismatches
            {"i j l", "I J K", false, 0, 0, false},
            {"i", "j", false, 0, 0, false},
            {"i", "ı", false, 0, 0, false},
            {"abc", "ab_c", false, 0, 0, false},

            {"é", "e", false, 0, 0, false},
            {"ó", "o", false, 0, 0, false},
            {"å", "a", false, 0, 0, false},

            // ensure that we don't match turkic "i" mappings
            {"ı", "i", false, 0, 0, false},
            {"ı", "I", false, 0, 0, false},
            {"i", "ı", false, 0, 0, false},
            {"i", "İ", false, 0, 0, false},
            {"I", "ı", false, 0, 0, false},
            {"İ", "i", false, 0, 0, false},
            // wildcards
            {"?", "aa", false, 0, 0, false},
            {"*.txt", "testtxt", false, 0, 0, false},
            // regex
            {"^a", "ba", false, 0, QUERY_FLAG_REGEX, false},
            // match case
            {"a", "A", false, 0, QUERY_FLAG_MATCH_CASE, false},
            // auto match case
            {"A", "a", false, 0, QUERY_FLAG_AUTO_MATCH_CASE, false},

            // Matches
            {"é", "É", false, 0, 0, true},
            {"ó", "Ó", false, 0, 0, true},
            {"å", "Å", false, 0, 0, true},
            {"É", "é", false, 0, 0, true},
            {"Ó", "Ó", false, 0, 0, true},
            {"Å", "å", false, 0, 0, true},

            {"ﬀ", "affe", false, 0, 0, true},
            {"i", "I J K", false, 0, 0, true},
            {"j i", "I J K", false, 0, 0, true},
            {"i j", "İIäój", false, 0, 0, true},
            {"abc", "abcdef", false, 0, 0, true},
            {"ab cd", "abcdef", false, 0, 0, true},
            // wildcards
            {"?", "ı", false, 0, 0, true},
            {"*c*f", "abcdef", false, 0, 0, true},
            {"ab*ef", "abcdef", false, 0, 0, true},
            {"abc?ef", "abcdef", false, 0, 0, true},
            // regex
            {"^b", "ba", false, 0, QUERY_FLAG_REGEX, true},
            {"^B", "ba", false, 0, QUERY_FLAG_REGEX, true},
            // match case
            {"a", "a", false, 0, QUERY_FLAG_MATCH_CASE, true},
            // auto match case
            {"A", "A", false, 0, QUERY_FLAG_AUTO_MATCH_CASE, true},

            // boolean logic
            {"a && (b || c)", "ab", false, 0, 0, true},
            {"a && (b || c)", "ac", false, 0, 0, true},
            {"a && (b || c)", "ad", false, 0, 0, false},
            {"a && (b || c)", "bc", false, 0, 0, false},
            {"a && (b || c || d || e)", "ae", false, 0, 0, true},
            {"a && (b || (c && d))", "bc", false, 0, 0, false},
            {"a && (b || (c && d))", "ac", false, 0, 0, false},
            {"a && (b || (c && d))", "bcd", false, 0, 0, false},
            {"a && (b || (c && d))", "acd", false, 0, 0, true},
            {"a && (b || (c && d))", "ab", false, 0, 0, true},
            {"!a", "b", false, 0, 0, true},
            {"!b", "b", false, 0, 0, false},
            {"!!b", "b", false, 0, 0, true},
            {"a && !(b || c)", "abc", false, 0, 0, false},
            {"a && !(b || !c)", "ac", false, 0, 0, true},
            {"a && !(b || !c)", "ac", false, 0, 0, true},
            {"a (b || c)", "ac", false, 0, 0, true},
            {"a (b || c)", "ab", false, 0, 0, true},
            {"a (b || c)", "a", false, 0, 0, false},
            {"a (b || c)", "b", false, 0, 0, false},
            {"a (b || c)", "c", false, 0, 0, false},
            {"a (b || c)", "bc", false, 0, 0, false},
            {"a !b", "ac", false, 0, 0, true},
            {"a !b", "ab", false, 0, 0, false},
            {"a !b", "cd", false, 0, 0, false},
            {"a b !c", "abc", false, 0, 0, false},
            {"a b !c", "abd", false, 0, 0, true},
            {"a b c !d", "abcd", false, 0, 0, false},
            {"a b c !d", "abce", false, 0, 0, true},
            // Closing bracket without corresponding open bracket
            //{"a)", "a", 0, 0, false},
            {"a !b || c)", "ad", false, 0, 0, false},
            {"a !b || c)", "c", false, 0, 0, false},
            {"a !b || c)", "ac", false, 0, 0, false},
            {"a !b || c)", "ab", false, 0, 0, false},
            {"a !b || c)", "b", false, 0, 0, false},

            // fields
            {"size:1", "test", false, 1, 0, true},
            {"size:300..", "test", false, 1000, 0, true},
            {"size:300..", "test", false, 200, 0, false},
            {"size:>300", "test", false, 301, 0, true},
            {"size:>300", "test", false, 300, 0, false},
            {"size:>=300", "test", false, 300, 0, true},
            {"size:>300 size:<400", "test", false, 350, 0, true},
            {"size:>300 size:<400", "test", false, 250, 0, false},
            {"size:>300 size:<400", "test", false, 450, 0, false},
            {"size:>1MB", "test", false, 1000001, 0, true},
            {"size:>1MB", "test", false, 1000000, 0, false},
            {"size:abc", "test", false, 1000000, 0, false},
            {"size:abc test", "test", false, 1000000, 0, false},
            {"size:abc abc", "test", false, 1000000, 0, false},
            // bug #388
            {"size:1kb..2kb", "test", false, 1000, 0, true},

            {"regex:suffix$", "suffix prefix", false, 0, 0, false},
            {"regex:suffix$", "prefix suffix", false, 0, 0, true},
            {"exact:ABC", "aBc", false, 0, 0, true},
            {"exact:ABC", "aBcd", false, 0, 0, false},
            {"case:exact:ABC", "aBc", false, 0, 0, false},
            {"exact:Ȁ", "Ȁ", false, 0, 0, true},
            {"exact:ȁ", "Ȁ", false, 0, 0, true},
            {"exact:Ȁ", "ȁ", false, 0, 0, true},
            {"case:exact:ȁ", "Ȁ", false, 0, 0, false},
            {"case:exact:Ȁ", "ȁ", false, 0, 0, false},
            {"case:exact:Ȁ", "Ȁ", false, 0, 0, true},
            {"exact:Ȁ", "Ȁb", false, 0, 0, false},
            {"case:(A (b || c)) d", "AbD", false, 0, 0, true},
            {"D case:(A (b || c))", "Acd", false, 0, 0, true},
            {"case:(A (b || c)) d", "ab", false, 0, 0, false},
            {"case:(A (b || c)) d", "AC", false, 0, 0, false},
            {"!case:(A || B) c", "ac", false, 0, 0, true},
            {"!case:(A || B) c", "bc", false, 0, 0, true},
            {"!case:(A || B) c", "abc", false, 0, 0, true},
            {"!case:(A || B) c", "Ac", false, 0, 0, false},
            {"!case:(A || B) c", "Bc", false, 0, 0, false},
            {"!case:(A || B) c", "ABc", false, 0, 0, false},
            {"!case:(A || B) c", "abd", false, 0, 0, false},
            {"ext:pdf;jpg", "test.pdf", false, 0, 0, true},
            {"ext:pdf;jpg", "test.jpg", false, 0, 0, true},
            {"ext:pdf;jpg", "test.c", false, 0, 0, false},
            {"ext:", "test.c", false, 0, 0, false},
            {"ext:", "test", false, 0, 0, true},
            {"case:(TE || AB) cd", "TEcd", false, 0, 0, true},
            {"case:(TE || AB) cd", "ABcd", false, 0, 0, true},
            {"case:(TE || AB) cd", "AB", false, 0, 0, false},
            {"case:(TE || AB) cd", "TE", false, 0, 0, false},
            {"case:(TE || AB) cd", "ABTE", false, 0, 0, false},
            {"case:(TE || AB) cd", "cd", false, 0, 0, false},
            {"nocase:a", "A", false, 0, QUERY_FLAG_MATCH_CASE, true},

            {"depth:0", "/", false, 0, 0, true},
            {"depth:2", "/1/2/3", false, 0, 0, false},
            {"depth:3", "/1/2/3", false, 0, 0, true},

            {"path:d", "/a/b/c", false, 0, 0, false},
            {"path:a", "/a/b/c", false, 0, 0, true},
            {"path:b", "/a/b/c", false, 0, 0, true},
            {"path:c", "/a/b/c", false, 0, 0, true},
            {"path:/", "/a/b/c", false, 0, 0, true},
            {"path:/a/b/c", "/a/b/c", false, 0, 0, true},
            {"path:(a && b && c && d)", "/a/b/c", false, 0, 0, false},
            {"path:(a && b && c)", "/a/b/c", false, 0, 0, true},

            {"parent:/b/a", "/a/b/c", false, 0, 0, false},
            {"parent:/a/b", "/a/b/c", false, 0, 0, true},

            // macros
            {"test || (pic: video:)", "test.jpg", false, 0, 0, true},
            {"test || (pic: video:)", "test.mp4", false, 0, 0, true},
            {"test || (pic: video:)", "test.mp4", false, 0, 0, true},
            {"test || (pic: video:)", "test.doc", false, 0, 0, true},
            {"test || (pic: video:)", "test.doc", false, 0, 0, true},

            // bug reports:
            // #360
            {"(", "test", false, 0, QUERY_FLAG_REGEX, false},
            {"folder:", "", false, 0, 0, false},

        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(main_tests); i++) {
            QueryTest *t = &main_tests[i];
            test_query(t);
        }
    }
}

static void
test_turkic_case_mapping(void) {
    if (set_locale("tr_TR.UTF-8")) {
        QueryTest tr_tests[] = {
            // Mismatches
            {"i", "ı", false, 0, false},
            {"i", "I", false, 0, false},
            {"ı", "i", false, 0, false},
            {"ı", "İ", false, 0, false},
            {"İ", "ı", false, 0, false},
            {"İ", "I", false, 0, false},
            {"I", "i", false, 0, false},
            {"I", "İ", false, 0, false},

            // Matches
            {"ı", "I", false, 0, true},
            {"i", "İ", false, 0, true},
            // trigger 0, wildcard search
            //{"ı*", "I", 0, true},
            //{"i*", "İ", 0, true},
            //{"I*", "ı", 0, true},
            //{"İ*", "i", 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(tr_tests); i++) {
            QueryTest *t = &tr_tests[i];
            test_query(t);
            // the tests still need to pass if haystack and needle are swapped, since they're all single characters
            test_query(t);
        }
    }
}

static void
test_german_case_mapping(void) {
    if (set_locale("de_DE.UTF-8")) {
        QueryTest de_tests[] = {
            // Mismatches
            {"a", "ä", false, 0, 0, false},
            {"A", "ä", false, 0, 0, false},
            {"a", "Ä", false, 0, 0, false},
            {"A", "Ä", false, 0, 0, false},
            {"o", "ö", false, 0, 0, false},
            {"O", "ö", false, 0, 0, false},
            {"o", "Ö", false, 0, 0, false},
            {"O", "Ö", false, 0, 0, false},
            {"u", "ü", false, 0, 0, false},
            {"U", "ü", false, 0, 0, false},
            {"u", "Ü", false, 0, 0, false},
            {"U", "Ü", false, 0, 0, false},

            // Matches
            {"ä", "ä", false, 0, 0, true},
            {"ö", "ö", false, 0, 0, true},
            {"ü", "ü", false, 0, 0, true},
            {"Ä", "ä", false, 0, 0, true},
            {"Ö", "ö", false, 0, 0, true},
            {"Ü", "ü", false, 0, 0, true},

            {"ß", "ẞ", false, 0, 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(de_tests); i++) {
            QueryTest *t = &de_tests[i];
            test_query(t);
            // the tests still need to pass if haystack and needle are swapped, since they're all single characters
            test_query(t);
        }
    }
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/query/main", test_main);
    g_test_add_func("/FSearch/query/mappings_turkic", test_turkic_case_mapping);
    g_test_add_func("/FSearch/query/mappings_german", test_german_case_mapping);
    return g_test_run();
}