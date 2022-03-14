#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include <src/fsearch_string_utils.h>

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

void
test_str_get_extension(void) {
    typedef struct {
        const char *file_name;
        const char *extension;
    } FsearchTestExtensionContext;

    FsearchTestExtensionContext file_names[] = {
        {".hidden_file", ""},
        {"ends_with_dot.", ""},
        {"no_extension", ""},
        {"has_extension.ext", "ext"},
        {"has_short_extension.1", "1"},
        {"has.extension.and.dots.in.name.txt", "txt"},
        {"", ""},
    };

    for (gint i = 0; i < G_N_ELEMENTS(file_names); ++i) {
        FsearchTestExtensionContext *ctx = &file_names[i];
        const char *ext = fs_str_get_extension(ctx->file_name);
        g_assert_cmpstr(ext, ==, ctx->extension);
    }
}

void
test_str_is_empty(void) {
    typedef struct {
        const char *string;
        gboolean string_is_empty;
    } FsearchTestIsEmptyContext;

    FsearchTestIsEmptyContext strings[] = {
        {"non_empty_string", FALSE},
        {"  non_empty_string_surrounded_by_space  ", FALSE},
        {" \\     ", FALSE},
        {"        ", TRUE},
        {"", TRUE},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestIsEmptyContext *ctx = &strings[i];
        const gboolean is_empty = fs_str_is_empty(ctx->string);
        g_assert_true(is_empty == ctx->string_is_empty);
    }
}

void
test_str_utf8_has_upper(void) {
    set_locale("en_US.UTF-8");

    typedef struct {
        const char *string;
        gboolean string_has_upper;
    } FsearchTestHasUpperContext;

    FsearchTestHasUpperContext strings[] = {
        {"has_no_upper_character", FALSE},
        {"  ", FALSE},
        {"123abc", FALSE},
        {"", FALSE},
        {"ä", FALSE},
        {"ı", FALSE},
        {"Ä", TRUE},
        {"İ", TRUE},
        {"ABC", TRUE},
        {"aBc", TRUE},
        {"  B  ", TRUE},
        {"  B", TRUE},
        {"A   ", TRUE},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestHasUpperContext *ctx = &strings[i];
        const gboolean has_upper = fs_str_utf8_has_upper(ctx->string);
        if (has_upper != ctx->string_has_upper) {
            g_print("Expected '%s' to%s have upper case characters!\n", ctx->string, has_upper ? "" : " not");
        }
        g_assert_true(has_upper == ctx->string_has_upper);
    }
}

void
test_str_has_upper(void) {
    set_locale("en_US.UTF-8");

    typedef struct {
        const char *string;
        gboolean string_has_upper;
    } FsearchTestHasUpperContext;

    FsearchTestHasUpperContext strings[] = {
        {"has_no_upper_character", FALSE},
        {"  ", FALSE},
        {"123abc", FALSE},
        {"", FALSE},
        {"ä", FALSE}, // non-ascii -> no upper case
        {"Ä", FALSE}, // non-ascii -> no upper case
        {"ı", FALSE}, // non-ascii -> no upper case
        {"İ", FALSE}, // non-ascii -> no upper case
        {"ABC", TRUE},
        {"aBc", TRUE},
        {"  B  ", TRUE},
        {"  B", TRUE},
        {"A   ", TRUE},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestHasUpperContext *ctx = &strings[i];
        const gboolean has_upper = fs_str_has_upper(ctx->string);
        if (has_upper != ctx->string_has_upper) {
            g_print("Expected '%s' to%s have upper case characters!\n", ctx->string, has_upper ? "" : " not");
        }
        g_assert_true(has_upper == ctx->string_has_upper);
    }
}

void
test_str_icase_is_ascii(void) {
    set_locale("en_US.UTF-8");

    typedef struct {
        const char *string;
        gboolean is_ascii;
    } FsearchTestIsAsciiContext;

    FsearchTestIsAsciiContext strings[] = {
        {"is_ascii_string", TRUE},
        {"IS_ALSO_ASCII_STRING", TRUE},
        {"  ", TRUE},
        {"123abc", TRUE},
        {"", TRUE},
        {"aäA", FALSE}, // non-ascii
        {"aÄA", FALSE}, // non-ascii
        {"iıI", FALSE}, // non-ascii
        {"iİI", FALSE}, // non-ascii
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestIsAsciiContext *ctx = &strings[i];
        const gboolean is_ascii = fs_str_icase_is_ascii(ctx->string);
        if (is_ascii != ctx->is_ascii) {
            g_print("Expected '%s' to be an %s string!\n", ctx->string, is_ascii ? "ascii" : "non-ascii");
        }
        g_assert_true(is_ascii == ctx->is_ascii);
    }
}

void
test_str_wildcard_to_regex(void) {
    typedef struct {
        const char *wildcard_expression;
        const char *expected_regex_expression;
    } FsearchTestWildcardToRegexContext;

    FsearchTestWildcardToRegexContext strings[] = {
        {"", "^$"},
        {"abc", "^abc$"},
        {"?bc", "^.bc$"},
        {"ab?", "^ab.$"},
        {"ab.", "^ab\\.$"},
        {"abc*", "^abc.*$"},
        {"*abc*", "^.*abc.*$"},
        {"(abc)", "^\\(abc\\)$"},
        {"[abc]", "^\\[abc\\]$"},
        {"{abc}", "^\\{abc\\}$"},
        {"^abc$", "^\\^abc\\$$"},
        {"+abc.", "^\\+abc\\.$"},
        {"|abc|", "^\\|abc\\|$"},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestWildcardToRegexContext *ctx = &strings[i];
        g_autofree char *regex = fs_str_convert_wildcard_to_regex_expression(ctx->wildcard_expression);
        g_assert_cmpstr(regex, ==, ctx->expected_regex_expression);
    }
}

void
test_str_starts_with_range(void) {
    typedef struct {
        const char *string;
        gboolean starts_with_range;
        int end_idx;
    } FsearchTestStartsWithRangeContext;

    FsearchTestStartsWithRangeContext strings[] = {
        {"does_not_start_with_range", FALSE, 0},
        {".does_not_start_with_range", FALSE, 0},
        {"does-not-start-with-range-", FALSE, 0},
        {"does..not..start..with..range..", FALSE, 0},
        {"-does-start-with-range", TRUE, 1},
        {"--does-start-with-range", TRUE, 1},
        {"..does..start..with..range", TRUE, 2},
        {"....does..start..with..range", TRUE, 2},
    };

    for (gint i = 0; i < G_N_ELEMENTS(strings); ++i) {
        FsearchTestStartsWithRangeContext *ctx = &strings[i];
        char *end_ptr = NULL;
        const gboolean starts_with_range = fs_str_starts_with_range((char *)ctx->string, &end_ptr);
        if (starts_with_range != ctx->starts_with_range) {
            g_print("Expected '%s' to%s start with a range!\n", ctx->string, starts_with_range ? "" : " not");
        }
        g_assert_true(starts_with_range == ctx->starts_with_range);
        g_assert_cmpstr(end_ptr, ==, ctx->string + ctx->end_idx);
    }
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/string_utils/get_extension", test_str_get_extension);
    g_test_add_func("/FSearch/string_utils/is_empty", test_str_is_empty);
    g_test_add_func("/FSearch/string_utils/has_upper", test_str_has_upper);
    g_test_add_func("/FSearch/string_utils/has_upper_utf8", test_str_utf8_has_upper);
    g_test_add_func("/FSearch/string_utils/is_ascii_icase", test_str_icase_is_ascii);
    g_test_add_func("/FSearch/string_utils/convert_wildcard_to_regex", test_str_wildcard_to_regex);
    g_test_add_func("/FSearch/string_utils/starts_with_range", test_str_starts_with_range);
    return g_test_run();
}
