test_sources = files('test_token.c')
test_sources += files('../fsearch_token.c', '../fsearch_string_utils.c')

test_token = executable('test_token', test_sources, dependencies: [ fsearch_deps ],
         include_directories: [include_directories('..') ])
test('test_token', test_token)
