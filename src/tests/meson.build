test_array = executable('test_array', 'test_array.c', dependencies: libfsearch_dep)
test_query = executable('test_query', 'test_query.c', dependencies: libfsearch_dep)
test_size_utils = executable('test_size_utils', 'test_size_utils.c', dependencies: libfsearch_dep)
test_string_utils = executable('test_string_utils', 'test_string_utils.c', dependencies: libfsearch_dep)
test_time_utils = executable('test_time_utils', 'test_time_utils.c', dependencies: libfsearch_dep)
test_database_entry = executable('test_database_entry', 'test_database_entry.c', dependencies: libfsearch_dep)
test_database_include = executable('test_database_include', 'test_database_include.c', dependencies : libfsearch_dep)
test_database_exclude = executable('test_database_exclude', 'test_database_exclude.c', dependencies : libfsearch_dep)
test_database_chunked_array = executable('test_database_chunked_array', 'test_database_chunked_array.c', dependencies : libfsearch_dep)
test_database_index_store = executable('test_database_index_store', 'test_database_index_store.c', dependencies : libfsearch_dep)
test_database = executable('test_database', 'test_database.c', dependencies : libfsearch_dep)

test('test_database',
     test_database,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
     timeout : 60,
)
test('test_database_chunked_array',
     test_database_chunked_array,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_database_index_store',
     test_database_index_store,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_database_include',
     test_database_include,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_database_exclude',
     test_database_exclude,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_database_entry',
     test_database_entry,
     env : [
         'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
         'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_array',
     test_array,
     env: [
       'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
       'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_query',
     test_query,
     env: [
       'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
       'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_size_utils',
     test_size_utils,
     env: [
       'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
       'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_string_utils',
     test_string_utils,
     env: [
       'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
       'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
test('test_time_utils',
     test_time_utils,
     env: [
       'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
       'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
     ],
)
