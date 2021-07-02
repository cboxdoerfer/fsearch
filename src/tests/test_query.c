#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_query.h>

static bool
test_query(const char *needle, const char *haystack, bool enable_regex, bool match_case, bool auto_match_case) {
    FsearchQueryFlags flags = {.enable_regex = enable_regex,
                               .match_case = match_case,
                               .auto_match_case = auto_match_case};
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
    return found;
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

int
main(int argc, char *argv[]) {
    if (set_locale("en_US.UTF-8")) {
        g_assert_false(test_query("i j l", "I J K", false, false, false));
        g_assert_false(test_query("i", "J K", false, false, false));
        g_assert_false(test_query("I", "i j k", false, true, false));
        g_assert_false(test_query("i", "ı", false, false, false));
        g_assert_false(test_query("i", "İ", false, false, false));

        g_assert_true(test_query("i", "I J K", false, false, false));
        g_assert_true(test_query("i j", "I J K", false, false, false));
        g_assert_true(test_query("i j", "İIäój", false, false, false));
    }

    if (set_locale("tr_TR.UTF-8")) {
        g_assert_false(test_query("i", "ı", false, false, false));
        g_assert_false(test_query("i", "I", false, false, false));
        g_assert_false(test_query("ı", "i", false, false, false));
        g_assert_false(test_query("ı", "İ", false, false, false));

        g_assert_true(test_query("ı", "I", false, false, false));
        g_assert_true(test_query("i", "İ", false, false, false));
    }

    if (set_locale("de_DE.UTF-8")) {
        g_assert_false(test_query("ä", "a", false, false, false));
        g_assert_false(test_query("ä", "A", false, false, false));
        g_assert_false(test_query("Ä", "a", false, false, false));
        g_assert_false(test_query("Ä", "A", false, false, false));

        g_assert_false(test_query("ö", "o", false, false, false));
        g_assert_false(test_query("ö", "O", false, false, false));
        g_assert_false(test_query("Ö", "o", false, false, false));
        g_assert_false(test_query("Ö", "O", false, false, false));

        g_assert_false(test_query("ü", "u", false, false, false));
        g_assert_false(test_query("ü", "U", false, false, false));
        g_assert_false(test_query("Ü", "u", false, false, false));
        g_assert_false(test_query("Ü", "U", false, false, false));

        g_assert_true(test_query("ä", "Ä", false, false, false));
        g_assert_true(test_query("ö", "Ö", false, false, false));
        g_assert_true(test_query("ü", "Ü", false, false, false));
    }
}
