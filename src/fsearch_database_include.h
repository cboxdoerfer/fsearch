#pragma once

#include <gio/gio.h>

#include "fsearch_database_scan_reason.h"

G_BEGIN_DECLS

#define FSEARCH_TYPE_DATABASE_INCLUDE (fsearch_database_include_get_type())

typedef struct _FsearchDatabaseInclude FsearchDatabaseInclude;

GType
fsearch_database_include_get_type(void);

FsearchDatabaseInclude *
fsearch_database_include_new(const char *path,
                             gboolean active,
                             gboolean one_file_system,
                             gboolean monitor,
                             gboolean scan_after_load,
                             int64_t rescan_after,
                             gint id);

FsearchDatabaseInclude *
fsearch_database_include_ref(FsearchDatabaseInclude *self);

void
fsearch_database_include_unref(FsearchDatabaseInclude *self);

FsearchDatabaseInclude *
fsearch_database_include_copy(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_active(FsearchDatabaseInclude *self);

const char *
fsearch_database_include_get_path(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_one_file_system(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_monitored(FsearchDatabaseInclude *self);

gboolean
fsearch_database_include_get_scan_after_launch(FsearchDatabaseInclude *self);

int64_t
fsearch_database_include_get_rescan_after(FsearchDatabaseInclude *self);

int64_t
fsearch_database_include_get_last_scan_time(FsearchDatabaseInclude *self);

uint32_t
fsearch_database_include_get_last_scan_duration(FsearchDatabaseInclude *self);

uint32_t
fsearch_database_include_get_last_error_code(FsearchDatabaseInclude *self);

uint32_t
fsearch_database_include_get_last_scanned_folder_count(FsearchDatabaseInclude *self);

uint32_t
fsearch_database_include_get_last_scanned_file_count(FsearchDatabaseInclude *self);

FsearchDatabaseScanReason
fsearch_database_include_get_last_scan_reason(FsearchDatabaseInclude *self);

gint
fsearch_database_include_get_id(FsearchDatabaseInclude *self);

void
fsearch_database_include_set_last_scan_time(FsearchDatabaseInclude *self, int64_t time);

void
fsearch_database_include_set_last_scan_duration(FsearchDatabaseInclude *self, uint32_t duration);

void
fsearch_database_include_set_last_error_code(FsearchDatabaseInclude *self, uint32_t error_code);

void
fsearch_database_include_set_last_scanned_file_count(FsearchDatabaseInclude *self, uint32_t count);

void
fsearch_database_include_set_last_scanned_folder_count(FsearchDatabaseInclude *self, uint32_t count);

void
fsearch_database_include_set_last_scan_reason(FsearchDatabaseInclude *self, FsearchDatabaseScanReason reason);

gboolean
fsearch_database_include_equal(FsearchDatabaseInclude *i1, FsearchDatabaseInclude *i2);

gint
fsearch_database_include_compare(gconstpointer i1, gconstpointer i2);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsearchDatabaseInclude, fsearch_database_include_unref)

G_END_DECLS