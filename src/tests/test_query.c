#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_limits.h>
#include <src/fsearch_query.h>

static void
test_query(const char *needle, const char *haystack, off_t size, FsearchQueryFlags flags, bool result) {
    bool found = true;

    FsearchFilterManager *manager = fsearch_filter_manager_new_with_defaults();
    FsearchQuery *q = fsearch_query_new(needle, NULL, 0, NULL, manager, NULL, flags, "debug_query", true);

    g_autofree FsearchDatabaseEntry *entry = calloc(1, db_entry_get_sizeof_file_entry());
    db_entry_set_name(entry, haystack);
    db_entry_set_size(entry, size);
    db_entry_set_type(entry, DATABASE_ENTRY_TYPE_FILE);

    FsearchQueryMatchData *match_data = fsearch_query_match_data_new();
    fsearch_query_match_data_set_entry(match_data, entry);

    found = fsearch_query_match(q, match_data);
    g_clear_pointer(&manager, fsearch_filter_manager_free);
    g_clear_pointer(&q, fsearch_query_unref);
    g_clear_pointer(&match_data, fsearch_query_match_data_free);
    db_entry_destroy(entry);

    if (found != result) {
        g_printerr("[%s] should%s match [name:%s, size:%ld]\n", needle, result ? "" : " NOT", haystack, size);
    }
    g_assert_true(found == result);
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

typedef struct QueryTest {
    const char *needle;
    const char *haystack;
    off_t size;
    FsearchQueryFlags flags;
    bool result;
} QueryTest;

static void
test_main(void) {
    if (set_locale("en_US.UTF-8")) {
        QueryTest main_tests[] = {
            // Mismatches
            {"i j l", "I J K", 0, 0, false},
            {"i", "j", 0, 0, false},
            {"i", "ı", 0, 0, false},
            {"abc", "ab_c", 0, 0, false},

            {"é", "e", 0, 0, false},
            {"ó", "o", 0, 0, false},
            {"å", "a", 0, 0, false},

            // ensure that we don't match turkic "i" mappings
            {"ı", "i", 0, 0, false},
            {"ı", "I", 0, 0, false},
            {"i", "ı", 0, 0, false},
            {"i", "İ", 0, 0, false},
            {"I", "ı", 0, 0, false},
            {"İ", "i", 0, 0, false},
            // wildcards
            {"?", "aa", 0, 0, false},
            {"*.txt", "testtxt", 0, 0, false},
            // regex
            {"^a", "ba", 0, QUERY_FLAG_REGEX, false},
            // match case
            {"a", "A", 0, QUERY_FLAG_MATCH_CASE, false},
            // auto match case
            {"A", "a", 0, QUERY_FLAG_AUTO_MATCH_CASE, false},

            // Matches
            {"é", "É", 0, 0, true},
            {"ó", "Ó", 0, 0, true},
            {"å", "Å", 0, 0, true},
            {"É", "é", 0, 0, true},
            {"Ó", "Ó", 0, 0, true},
            {"Å", "å", 0, 0, true},

            {"ﬀ", "affe", 0, 0, true},
            {"i", "I J K", 0, 0, true},
            {"j i", "I J K", 0, 0, true},
            {"i j", "İIäój", 0, 0, true},
            {"abc", "abcdef", 0, 0, true},
            {"ab cd", "abcdef", 0, 0, true},
            // wildcards
            {"?", "ı", 0, 0, true},
            {"*c*f", "abcdef", 0, 0, true},
            {"ab*ef", "abcdef", 0, 0, true},
            {"abc?ef", "abcdef", 0, 0, true},
            // regex
            {"^b", "ba", 0, QUERY_FLAG_REGEX, true},
            {"^B", "ba", 0, QUERY_FLAG_REGEX, true},
            // match case
            {"a", "a", 0, QUERY_FLAG_MATCH_CASE, true},
            // auto match case
            {"A", "A", 0, QUERY_FLAG_AUTO_MATCH_CASE, true},

            // boolean logic
            {"a && (b || c)", "ab", 0, 0, true},
            {"a && (b || c)", "ac", 0, 0, true},
            {"a && (b || c)", "ad", 0, 0, false},
            {"a && (b || c)", "bc", 0, 0, false},
            {"a && (b || c || d || e)", "ae", 0, 0, true},
            {"a && (b || (c && d))", "bc", 0, 0, false},
            {"a && (b || (c && d))", "ac", 0, 0, false},
            {"a && (b || (c && d))", "bcd", 0, 0, false},
            {"a && (b || (c && d))", "acd", 0, 0, true},
            {"a && (b || (c && d))", "ab", 0, 0, true},
            {"!a", "b", 0, 0, true},
            {"!b", "b", 0, 0, false},
            {"!!b", "b", 0, 0, true},
            {"a && !(b || c)", "abc", 0, 0, false},
            {"a && !(b || !c)", "ac", 0, 0, true},
            {"a && !(b || !c)", "ac", 0, 0, true},
            {"a (b || c)", "ac", 0, 0, true},
            {"a (b || c)", "ab", 0, 0, true},
            {"a (b || c)", "a", 0, 0, false},
            {"a (b || c)", "b", 0, 0, false},
            {"a (b || c)", "c", 0, 0, false},
            {"a (b || c)", "bc", 0, 0, false},
            // Closing bracket without corresponding open bracket
            //{"a)", "a", 0, 0, false},
            {"a !b || c)", "ad", 0, 0, false},
            {"a !b || c)", "c", 0, 0, false},
            {"a !b || c)", "ac", 0, 0, false},
            {"a !b || c)", "ab", 0, 0, false},
            {"a !b || c)", "b", 0, 0, false},

            // fields
            {"size:300..", "test", 1000, 0, true},
            {"size:300-", "test", 1000, 0, true},
            {"size:300-", "test", 200, 0, false},
            {"size:>300", "test", 301, 0, true},
            {"size:>300", "test", 300, 0, false},
            {"size:>=300", "test", 300, 0, true},
            {"size:>300 size:<400", "test", 350, 0, true},
            {"size:>300 size:<400", "test", 250, 0, false},
            {"size:>300 size:<400", "test", 450, 0, false},
            {"size:>1MB", "test", 1000001, 0, true},
            {"size:>1MB", "test", 1000000, 0, false},
            {"size:abc", "test", 1000000, 0, true},
            {"size:abc test", "test", 1000000, 0, true},
            {"size:abc abc", "test", 1000000, 0, false},

            {"regex:suffix$", "suffix prefix", 0, 0, false},
            {"regex:suffix$", "prefix suffix", 0, 0, true},
            {"exact:ABC", "aBc", 0, 0, true},
            {"exact:ABC", "aBcd", 0, 0, false},
            {"case:exact:ABC", "aBc", 0, 0, false},
            {"exact:Ȁ", "Ȁ", 0, 0, true},
            {"exact:ȁ", "Ȁ", 0, 0, true},
            {"exact:Ȁ", "ȁ", 0, 0, true},
            {"case:exact:ȁ", "Ȁ", 0, 0, false},
            {"case:exact:Ȁ", "ȁ", 0, 0, false},
            {"case:exact:Ȁ", "Ȁ", 0, 0, true},
            {"exact:Ȁ", "Ȁb", 0, 0, false},
            {"case:(A (b || c)) d", "AbD", 0, 0, true},
            {"D case:(A (b || c))", "Acd", 0, 0, true},
            {"case:(A (b || c)) d", "ab", 0, 0, false},
            {"case:(A (b || c)) d", "AC", 0, 0, false},
            {"!case:(A || B) c", "ac", 0, 0, true},
            {"!case:(A || B) c", "bc", 0, 0, true},
            {"!case:(A || B) c", "abc", 0, 0, true},
            {"!case:(A || B) c", "Ac", 0, 0, false},
            {"!case:(A || B) c", "Bc", 0, 0, false},
            {"!case:(A || B) c", "ABc", 0, 0, false},
            {"!case:(A || B) c", "abd", 0, 0, false},
            {"ext:pdf;jpg", "test.pdf", 0, 0, true},
            {"ext:pdf;jpg", "test.jpg", 0, 0, true},
            {"ext:pdf;jpg", "test.c", 0, 0, false},
            {"ext:", "test.c", 0, 0, false},
            {"ext:", "test", 0, 0, true},
            {"case:(TE || AB) cd", "TEcd", 0, 0, true},
            {"case:(TE || AB) cd", "ABcd", 0, 0, true},
            {"case:(TE || AB) cd", "AB", 0, 0, false},
            {"case:(TE || AB) cd", "TE", 0, 0, false},
            {"case:(TE || AB) cd", "ABTE", 0, 0, false},
            {"case:(TE || AB) cd", "cd", 0, 0, false},
            // macros
            {"test || (pic: video:)", "test.jpg", 0, 0, true},
            {"test || (pic: video:)", "test.mp4", 0, 0, true},
            {"test || (pic: video:)", "test.mp4", 0, 0, true},
            {"test || (pic: video:)", "test.doc", 0, 0, true},
            {"test || (pic: video:)", "test.doc", 0, 0, true},

        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(main_tests); i++) {
            QueryTest *t = &main_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
        }
    }
}

static void
test_turkic_case_mapping(void) {
    if (set_locale("tr_TR.UTF-8")) {
        QueryTest tr_tests[] = {
            // Mismatches
            {"i", "ı", 0, false},
            {"i", "I", 0, false},
            {"ı", "i", 0, false},
            {"ı", "İ", 0, false},
            {"İ", "ı", 0, false},
            {"İ", "I", 0, false},
            {"I", "i", 0, false},
            {"I", "İ", 0, false},

            // Matches
            {"ı", "I", 0, true},
            {"i", "İ", 0, true},
            // trigger 0, wildcard search
            //{"ı*", "I", 0, true},
            //{"i*", "İ", 0, true},
            //{"I*", "ı", 0, true},
            //{"İ*", "i", 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(tr_tests); i++) {
            QueryTest *t = &tr_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
            // the tests still need to pass if haystack and needle are swapped, since they're all single characters
            test_query(t->haystack, t->needle, t->size, t->flags, t->result);
        }
    }
}

static void
test_german_case_mapping(void) {
    if (set_locale("de_DE.UTF-8")) {
        QueryTest de_tests[] = {
            // Mismatches
            {"a", "ä", 0, 0, false},
            {"A", "ä", 0, 0, false},
            {"a", "Ä", 0, 0, false},
            {"A", "Ä", 0, 0, false},
            {"o", "ö", 0, 0, false},
            {"O", "ö", 0, 0, false},
            {"o", "Ö", 0, 0, false},
            {"O", "Ö", 0, 0, false},
            {"u", "ü", 0, 0, false},
            {"U", "ü", 0, 0, false},
            {"u", "Ü", 0, 0, false},
            {"U", "Ü", 0, 0, false},

            // Matches
            {"ä", "ä", 0, 0, true},
            {"ö", "ö", 0, 0, true},
            {"ü", "ü", 0, 0, true},
            {"Ä", "ä", 0, 0, true},
            {"Ö", "ö", 0, 0, true},
            {"Ü", "ü", 0, 0, true},

            {"ß", "ẞ", 0, 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(de_tests); i++) {
            QueryTest *t = &de_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
            // the tests still need to pass if haystack and needle are swapped, since they're all single characters
            test_query(t->haystack, t->needle, t->size, t->flags, t->result);
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