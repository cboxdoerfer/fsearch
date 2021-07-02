#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_query.h>

static void
test_query(const char *needle, const char *haystack, bool result) {
    FsearchQueryFlags flags = {};
    FsearchQuery *q = fsearch_query_new(needle, NULL, 0, NULL, NULL, flags, 0, 0, NULL);
    bool found = true;

    for (uint32_t i = 0; i < q->num_token; i++) {
        FsearchToken *t = q->token[i];
        if (!t->search_func(haystack, t->text, NULL)) {
            found = false;
            break;
        }
    }
    g_clear_pointer(&q, fsearch_query_unref);

    if (found != result) {
        g_printerr("Finding %s in %s should %s.\n", needle, haystack, result ? "succeed" : "fail");
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
    bool result;
} QueryTest;

int
main(int argc, char *argv[]) {
    if (set_locale("en_US.UTF-8")) {
        QueryTest us_tests[] = {
            // Mismatches
            {"i j l", "I J K", false},
            {"i", "J K", false},
            {"i", "ı", false},
            {"i", "İ", false},
            {"abc", "ab_c", false},

            {"é", "e", false},
            {"ó", "o", false},
            {"å", "a", false},

            // Matches
            {"é", "É", true},
            {"ó", "Ó", true},
            {"å", "Å", true},

            {"i", "I J K", true},
            {"i j", "I J K", true},
            {"i j", "İIäój", true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(us_tests); i++) {
            QueryTest *t = &us_tests[i];
            test_query(t->needle, t->haystack, t->result);
        }
    }

    if (set_locale("tr_TR.UTF-8")) {
        QueryTest tr_tests[] = {
            // Mismatches
            {"i", "ı", false},
            {"i", "I", false},
            {"ı", "i", false},
            {"ı", "İ", false},

            // Matches
            {"ı", "I", true},
            {"i", "İ", true},
            {"I", "ı", true},
            {"İ", "i", true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(tr_tests); i++) {
            QueryTest *t = &tr_tests[i];
            test_query(t->needle, t->haystack, t->result);
        }
    }

    if (set_locale("de_DE.UTF-8")) {
        QueryTest de_tests[] = {
            // Mismatches
            {"ä", "a", false},
            {"ä", "A", false},
            {"Ä", "a", false},
            {"Ä", "A", false},
            {"ö", "o", false},
            {"ö", "O", false},
            {"Ö", "o", false},
            {"Ö", "O", false},
            {"ü", "u", false},
            {"ü", "U", false},
            {"Ü", "u", false},
            {"Ü", "U", false},

            // Matches
            {"ä", "Ä", true},
            {"ö", "Ö", true},
            {"ü", "Ü", true},

            {"ß", "ẞ", true},
        };

        for (uint32_t i = 0; i < G_N_ELEMENTS(de_tests); i++) {
            QueryTest *t = &de_tests[i];
            test_query(t->needle, t->haystack, t->result);
        }
    }
}
