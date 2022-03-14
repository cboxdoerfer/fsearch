#include <glib.h>
#include <stdlib.h>

#include <src/fsearch_string_utils.h>

void
test_get_extension(void) {
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

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/string_utils/get_extension", test_get_extension);
    return g_test_run();
}
