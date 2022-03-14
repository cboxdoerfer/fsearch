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

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/string_utils/get_extension", test_str_get_extension);
    g_test_add_func("/FSearch/string_utils/is_empty", test_str_is_empty);
    g_test_add_func("/FSearch/string_utils/has_upper", test_str_has_upper);
    return g_test_run();
}
