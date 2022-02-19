#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_limits.h>
#include <src/fsearch_query.h>

static void
test_query(const char *needle, const char *haystack, off_t size, FsearchQueryFlags flags, bool result) {
    bool found = true;

    FsearchQuery *q = fsearch_query_new(needle, NULL, 0, NULL, NULL, flags, "debug_query", NULL);

    FsearchDatabaseEntry *entry = calloc(1, db_entry_get_sizeof_file_entry());
    db_entry_set_name(entry, haystack);
    db_entry_set_size(entry, size);

    FsearchQueryMatchContext *matcher = fsearch_query_match_context_new();
    fsearch_query_match_context_set_entry(matcher, entry);

    found = fsearch_query_match(q, matcher);
    g_clear_pointer(&q, fsearch_query_unref);
    g_clear_pointer(&matcher, fsearch_query_match_context_free);
    db_entry_destroy(entry);
    g_clear_pointer(&entry, free);

    if (found != result) {
        g_printerr("[%s] should%s match [name:%s, size:%ld]\n", needle, result ? "" : " NOT", haystack, size);
    }
    g_assert(found == result);
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

int
main(int argc, char *argv[]) {
    if (set_locale("en_US.UTF-8")) {
        QueryTest us_tests[] = {
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

            // fields
            {"size:>300", "test", 301, 0, true},
            {"size:>300", "test", 300, 0, false},
            {"size:>=300", "test", 300, 0, true},
            {"size:>300 size:<400", "test", 350, 0, true},
            {"size:>300 size:<400", "test", 250, 0, false},
            {"size:>300 size:<400", "test", 450, 0, false},
            {"size:>1MB", "test", 1000001, 0, true},
            {"size:>1MB", "test", 1000000, 0, false},
            {"regex:suffix$", "suffix prefix", 0, 0, false},
            {"regex:suffix$", "prefix suffix", 0, 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(us_tests); i++) {
            QueryTest *t = &us_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
        }
    }

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
            {"I", "ı", 0, true},
            {"İ", "i", 0, true},
            // trigger 0, wildcard search
            //{"ı*", "I", 0, true},
            //{"i*", "İ", 0, true},
            //{"I*", "ı", 0, true},
            //{"İ*", "i", 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(tr_tests); i++) {
            QueryTest *t = &tr_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
        }
    }

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

            {"ä", "a", 0, 0, false},
            {"ä", "A", 0, 0, false},
            {"Ä", "a", 0, 0, false},
            {"Ä", "A", 0, 0, false},
            {"ö", "o", 0, 0, false},
            {"ö", "O", 0, 0, false},
            {"Ö", "o", 0, 0, false},
            {"Ö", "O", 0, 0, false},
            {"ü", "u", 0, 0, false},
            {"ü", "U", 0, 0, false},
            {"Ü", "u", 0, 0, false},
            {"Ü", "U", 0, 0, false},

            // Matches
            {"ä", "ä", 0, 0, true},
            {"ö", "ö", 0, 0, true},
            {"ü", "ü", 0, 0, true},
            {"Ä", "ä", 0, 0, true},
            {"Ö", "ö", 0, 0, true},
            {"Ü", "ü", 0, 0, true},
            {"ä", "Ä", 0, 0, true},
            {"ö", "Ö", 0, 0, true},
            {"ü", "Ü", 0, 0, true},

            {"ß", "ẞ", 0, 0, true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(de_tests); i++) {
            QueryTest *t = &de_tests[i];
            test_query(t->needle, t->haystack, t->size, t->flags, t->result);
        }
    }
}
