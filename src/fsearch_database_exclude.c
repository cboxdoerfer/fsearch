#include "fsearch_database_exclude.h"

#include <glib.h>

struct _FsearchDatabaseExclude {
    char *pattern;

    gboolean active;
    FsearchDatabaseExcludeType type;
    FsearchDatabaseExcludeMatchScope scope;
    FsearchDatabaseExcludeTarget target;
    GRegex *regex;

    volatile gint ref_count;
};

G_DEFINE_BOXED_TYPE(FsearchDatabaseExclude,
                    fsearch_database_exclude,
                    fsearch_database_exclude_ref,
                    fsearch_database_exclude_unref)

FsearchDatabaseExclude *
fsearch_database_exclude_new(const char *pattern,
                             gboolean active,
                             FsearchDatabaseExcludeType type,
                             FsearchDatabaseExcludeMatchScope scope,
                             FsearchDatabaseExcludeTarget target) {
    FsearchDatabaseExclude *self;

    g_return_val_if_fail(pattern, NULL);

    self = g_slice_new0(FsearchDatabaseExclude);

    self->pattern = g_strdup(pattern);
    self->active = active;
    self->type = type;
    self->scope = scope;
    self->target = target;

    if (self->type == FSEARCH_DATABASE_EXCLUDE_TYPE_REGEX) {
        g_autoptr(GError) error = NULL;
        self->regex = g_regex_new(self->pattern, 0, 0, &error);
        if (!self->regex) {
            g_debug("[exclude] invalid regex pattern '%s': %s", self->pattern, error ? error->message : "unknown error");
        }
    }

    self->ref_count = 1;

    return self;
}

FsearchDatabaseExclude *
fsearch_database_exclude_ref(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    g_atomic_int_inc(&self->ref_count);

    return self;
}

void
fsearch_database_exclude_unref(FsearchDatabaseExclude *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(self->ref_count > 0);

    if (g_atomic_int_dec_and_test(&self->ref_count)) {
        g_clear_pointer(&self->pattern, g_free);
        g_clear_pointer(&self->regex, g_regex_unref);
        g_slice_free(FsearchDatabaseExclude, self);
    }
}

gboolean
fsearch_database_exclude_equal(FsearchDatabaseExclude *e1, FsearchDatabaseExclude *e2) {
    g_return_val_if_fail(e1 != NULL, FALSE);
    g_return_val_if_fail(e2 != NULL, FALSE);
    g_return_val_if_fail(e1->ref_count > 0, FALSE);
    g_return_val_if_fail(e2->ref_count > 0, FALSE);

    return g_strcmp0(e1->pattern, e2->pattern) == 0 && e1->active == e2->active && e1->type == e2->type
           && e1->scope == e2->scope && e1->target == e2->target;
}

FsearchDatabaseExclude *
fsearch_database_exclude_copy(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self, NULL);
    return fsearch_database_exclude_new(self->pattern, self->active, self->type, self->scope, self->target);
}

const char *
fsearch_database_exclude_get_pattern(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_return_val_if_fail(self->ref_count > 0, NULL);

    return self->pattern;
}

gboolean
fsearch_database_exclude_get_active(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);

    return self->active;
}

FsearchDatabaseExcludeType
fsearch_database_exclude_get_exclude_type(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED);
    g_return_val_if_fail(self->ref_count > 0, FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED);

    return self->type;
}

FsearchDatabaseExcludeMatchScope
fsearch_database_exclude_get_match_scope(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH);
    g_return_val_if_fail(self->ref_count > 0, FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH);

    return self->scope;
}

FsearchDatabaseExcludeTarget
fsearch_database_exclude_get_target(FsearchDatabaseExclude *self) {
    g_return_val_if_fail(self != NULL, FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH);
    g_return_val_if_fail(self->ref_count > 0, FSEARCH_DATABASE_EXCLUDE_TARGET_BOTH);

    return self->target;
}

gboolean
fsearch_database_exclude_matches(FsearchDatabaseExclude *self, const char *path, const char *basename, gboolean is_dir) {
    g_return_val_if_fail(self != NULL, FALSE);
    g_return_val_if_fail(self->ref_count > 0, FALSE);
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(basename != NULL, FALSE);

    if (self->target == FSEARCH_DATABASE_EXCLUDE_TARGET_FILES && is_dir) {
        return FALSE;
    }
    if (self->target == FSEARCH_DATABASE_EXCLUDE_TARGET_FOLDERS && !is_dir) {
        return FALSE;
    }

    const char *input =
        self->scope == FSEARCH_DATABASE_EXCLUDE_MATCH_SCOPE_FULL_PATH ? path : basename;

    switch (self->type) {
    case FSEARCH_DATABASE_EXCLUDE_TYPE_FIXED:
        return g_strcmp0(self->pattern, input) == 0;
    case FSEARCH_DATABASE_EXCLUDE_TYPE_WILDCARD:
        return g_pattern_match_simple(self->pattern, input);
    case FSEARCH_DATABASE_EXCLUDE_TYPE_REGEX:
        return self->regex ? g_regex_match(self->regex, input, 0, NULL) : FALSE;
    default:
        g_assert_not_reached();
    }
}
