resources = gnome.compile_resources('ui_resources',
    'gresource.xml',
    source_dir: '.',
    c_name: 'fsearch_ui',
)

resources += gnome.compile_resources('icon_resources',
    join_paths(meson.source_root(), 'data', 'gresource.xml'),
    source_dir: join_paths(meson.source_root(), 'data'),
    c_name: 'fsearch_icons',
)

libfsearch_core_sources = [
    'fsearch_array.c',
    'fsearch_config.c',
    'fsearch_database.c',
    'fsearch_database_chunked_array.c',
    'fsearch_database_entry.c',
    'fsearch_database_entry_info.c',
    'fsearch_database_exclude.c',
    'fsearch_database_exclude_manager.c',
    'fsearch_database_file.c',
    'fsearch_database_include.c',
    'fsearch_database_include_manager.c',
    'fsearch_database_index.c',
    'fsearch_database_index_event.c',
    'fsearch_database_index_store.c',
    'fsearch_database_info.c',
    'fsearch_database_rescan_manager.c',
    'fsearch_database_scan.c',
    'fsearch_database_search_info.c',
    'fsearch_database_search_view.c',
    'fsearch_database_sort.c',
    'fsearch_database_work.c',
    'fsearch_file_utils.c',
    'fsearch_filter.c',
    'fsearch_filter_manager.c',
    'fsearch_folder_monitor_event.c',
    'fsearch_main_context_utils.c',
    'fsearch_query.c',
    'fsearch_query_match_data.c',
    'fsearch_query_matchers.c',
    'fsearch_query_node.c',
    'fsearch_query_lexer.c',
    'fsearch_query_parser.c',
    'fsearch_query_tree.c',
    'fsearch_selection.c',
    'fsearch_size_utils.c',
    'fsearch_string_utils.c',
    'fsearch_time_utils.c',
    'fsearch_utf.c',
]

libfsearch_ui_sources = [
    resources,
    'fsearch.c',
    'fsearch_clipboard.c',
    'fsearch_database_preferences_widget.c',
    'fsearch_filter_editor.c',
    'fsearch_filter_preferences_widget.c',
    'fsearch_list_view.c',
    'fsearch_listview_popup.c',
    'fsearch_preferences_dialog.c',
    'fsearch_preview.c',
    'fsearch_result_view.c',
    'fsearch_statusbar.c',
    'fsearch_ui_utils.c',
    'fsearch_window.c',
    'fsearch_window_actions.c',
]

if have_inotify
    libfsearch_core_sources += 'fsearch_folder_monitor_inotify.c'
endif

if have_fanotify
    libfsearch_core_sources += 'fsearch_folder_monitor_fanotify.c'
endif

fsearch_core_deps = [
    cc.find_library('m', required: true),
    dependency('gio-unix-2.0', version: '>= 2.72'),
    dependency('pango', version: '>= 1.0'),
    dependency('libpcre2-8', version: '>= 10.21'),
    dependency('icu-uc', version: '>= 4.4'),
]

fsearch_deps = fsearch_core_deps + [
    dependency('gtk+-3.0', version: '>= 3.24'),
]

fsearch_enums_headers = [
    'fsearch_result.h'
]

fsearch_enums = gnome.mkenums_simple('fsearch_enums',
                                     sources : fsearch_enums_headers,
)

libfsearch_include_dirs = include_directories('.')

libfsearch_core = static_library(
    'fsearch_core',
    libfsearch_core_sources + fsearch_enums,
    dependencies: fsearch_core_deps,
    include_directories: fsearch_include_dirs,
)

# Used by the test suite
libfsearch_core_dep = declare_dependency(
    link_with: libfsearch_core,
    include_directories: [
        fsearch_include_dirs,
        libfsearch_include_dirs
    ],
    dependencies: fsearch_core_deps,
)

libfsearch = static_library(
    'fsearch',
    libfsearch_ui_sources,
    dependencies: fsearch_deps + [libfsearch_core_dep],
    include_directories: fsearch_include_dirs,
)

libfsearch_dep = declare_dependency(
    link_with: libfsearch,
    include_directories: [
        fsearch_include_dirs,
        libfsearch_include_dirs
    ],
    dependencies: fsearch_deps + [libfsearch_core_dep],
    sources: resources
)

fsearch = executable('fsearch', 'main.c',
    include_directories: fsearch_include_dirs,
    dependencies: libfsearch_dep,
    install: true,
)

subdir('tests')
