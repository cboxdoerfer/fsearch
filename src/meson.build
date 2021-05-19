fsearch_sources = [
    'fsearch.c',
    'fsearch_array.c',
    'fsearch_clipboard.c',
    'fsearch_config.c',
    'fsearch_database.c',
    'fsearch_database_entry.c',
    'fsearch_database_search.c',
    'fsearch_database_view.c',
    'fsearch_exclude_path.c',
    'fsearch_file_utils.c',
    'fsearch_filter.c',
    'fsearch_highlight_token.c',
    'fsearch_index.c',
    'fsearch_list_view.c',
    'fsearch_listview_popup.c',
    'fsearch_memory_pool.c',
    'fsearch_preferences_ui.c',
    'fsearch_preferences_widgets.c',
    'fsearch_query.c',
    'fsearch_statusbar.c',
    'fsearch_string_utils.c',
    'fsearch_task.c',
    'fsearch_thread_pool.c',
    'fsearch_token.c',
    'fsearch_ui_utils.c',
    'fsearch_window.c',
    'fsearch_window_actions.c',
    'main.c',
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
