resources = gnome.compile_resources('resources',
    'gresource.xml',
    source_dir: '.',
    c_name: 'fsearch',
)

libfsearch_sources = [
    resources,
    'fsearch.c',
    'fsearch_array.c',
    'fsearch_clipboard.c',
    'fsearch_config.c',
    'fsearch_database.c',
    'fsearch_database_entry.c',
    'fsearch_database_index.c',
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
    'fsearch_result_view.c',
    'fsearch_selection.c',
    'fsearch_statusbar.c',
    'fsearch_string_utils.c',
    'fsearch_task.c',
    'fsearch_thread_pool.c',
    'fsearch_token.c',
    'fsearch_ui_utils.c',
    'fsearch_window.c',
    'fsearch_window_actions.c',
]

fsearch_deps = [
    cc.find_library('m', required: true),
    dependency('gio-2.0', version: '>= 2.50'),
    dependency('gtk+-3.0', version: '>= 3.18'),
    dependency('libpcre', version: '>= 8.31'),
]

libfsearch = static_library(
    'fsearch',
    libfsearch_sources,
    dependencies: fsearch_deps,
    include_directories: fsearch_include_dirs,
)

libfsearch_include_dirs = include_directories('.')

libfsearch_dep = declare_dependency(
    link_with: libfsearch,
    include_directories: [
        fsearch_include_dirs,
        libfsearch_include_dirs
    ],
    dependencies: fsearch_deps,
    sources: resources
)

fsearch = executable('fsearch', 'main.c',
    include_directories: fsearch_include_dirs,
    dependencies: libfsearch_dep,
    install: true,
)

subdir('tests')
