fsearch_sources = [
    'array.c',
    'btree.c',
    'clipboard.c',
    'database.c',
    'database_search.c',
    'fsearch.c',
    'fsearch_config.c',
    'fsearch_exclude_path.c',
    'fsearch_filter.c',
    'fsearch_include_path.c',
    'fsearch_list_view.c',
    'fsearch_preferences_widgets.c',
    'fsearch_task.c',
    'fsearch_thread_pool.c',
    'fsearch_timer.c',
    'fsearch_window.c',
    'fsearch_window_actions.c',
    'listview_popup.c',
    'main.c',
    'memory_pool.c',
    'preferences_ui.c',
    'query.c',
    'string_utils.c',
    'token.c',
    'ui_utils.c',
    'utils.c',
]

fsearch_deps = [
  cc.find_library('m', required: true),
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk+-3.0', version: '>= 3.18'),
  dependency('libpcre', version: '>= 8.31'),
]

fsearch_sources += gnome.compile_resources('resources',
  'gresource.xml',
  source_dir: '.',
  c_name: 'fsearch'
)

fsearch = executable('fsearch', fsearch_sources,
         include_directories: [include_directories('..') ],
         dependencies: fsearch_deps,
              install: true,
)
