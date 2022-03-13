test_query = executable('test_query', 'test_query.c', dependencies: libfsearch_dep)
test_array = executable('test_array', 'test_array.c', dependencies: libfsearch_dep)

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
