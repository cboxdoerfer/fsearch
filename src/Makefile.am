EXTRA_DIST = fsearch_window.ui \
	     fsearch_filter_editor.ui \
	     fsearch_overlay.ui \
	     fsearch_preferences.ui \
         fsearch_statusbar.ui \
	     gresource.xml \
	     shared.css \
	     menus.ui

if DEBUG
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE2_CFLAGS) $(ICU_CFLAGS) -lm -Wall -std=c11 -D_FILE_OFFSET_BITS=64 -g -Ofast -DDEBUG=1
else
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE2_CFLAGS) $(ICU_CFLAGS) -lm -Wall -std=c11 -D_FILE_OFFSET_BITS=64 -Ofast
endif

bin_PROGRAMS = fsearch

fsearch_NOINST_H_FILES = fsearch.h \
			 fsearch_array.h \
			 fsearch_clipboard.h \
			 fsearch_config.h \
			 fsearch_database.h \
			 fsearch_database_entry.h \
			 fsearch_database_index.h \
			 fsearch_database_search.h \
			 fsearch_database_view.h \
			 fsearch_exclude_path.h \
			 fsearch_file_utils.h \
			 fsearch_filter.h \
			 fsearch_filter_editor.h \
			 fsearch_filter_manager.h \
			 fsearch_index.h \
			 fsearch_limits.h \
			 fsearch_list_view.h \
			 fsearch_listview_popup.h \
			 fsearch_memory_pool.h \
			 fsearch_preferences_ui.h \
			 fsearch_preferences_widgets.h \
			 fsearch_query.h \
			 fsearch_query_match_data.h \
			 fsearch_query_node.h \
		     fsearch_query_parser.h \
			 fsearch_query_flags.h \
             fsearch_result_view.h \
			 fsearch_selection.h \
			 fsearch_size_utils.h \
			 fsearch_statusbar.h \
			 fsearch_string_utils.h \
			 fsearch_task.h \
			 fsearch_task_ids.h \
			 fsearch_thread_pool.h \
			 fsearch_time_utils.h \
			 fsearch_ui_utils.h \
			 fsearch_utf.h \
			 fsearch_window.h \
			 fsearch_window_actions.h

fsearch_SOURCES = main.c \
		  fsearch.c \
		  fsearch_array.c \
		  fsearch_clipboard.c \
		  fsearch_config.c \
		  fsearch_database.c \
		  fsearch_database_entry.c \
		  fsearch_database_index.c \
		  fsearch_database_search.c \
		  fsearch_database_view.c \
		  fsearch_exclude_path.c \
		  fsearch_file_utils.c \
		  fsearch_filter.c \
		  fsearch_filter_editor.c \
		  fsearch_filter_manager.c \
		  fsearch_index.c \
		  fsearch_list_view.c \
		  fsearch_listview_popup.c \
		  fsearch_memory_pool.c \
		  fsearch_preferences_ui.c \
		  fsearch_preferences_widgets.c \
		  fsearch_query.c \
		  fsearch_query_match_data.c \
		  fsearch_query_node.c \
		  fsearch_query_parser.c \
          fsearch_result_view.c \
          fsearch_selection.c \
          fsearch_size_utils.c \
		  fsearch_statusbar.c \
		  fsearch_string_utils.c \
		  fsearch_task.c \
		  fsearch_thread_pool.c \
		  fsearch_time_utils.c \
		  fsearch_ui_utils.c \
		  fsearch_utf.c \
		  fsearch_window.c \
		  fsearch_window_actions.c \
		  icon_resources.c \
		  ui_resources.c \
		  $(fsearch_NOINST_H_FILES)

BUILT_SOURCES=ui_resources.c ui_resources.h icon_resources.c icon_resources.h

CLEANFILES = $(BUILT_SOURCES)

fsearch_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\"

ui_resource_files=$(shell $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_ui --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)

ui_resources.h:
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_ui --target=$@ --sourcedir=$(srcdir) --generate-header $(srcdir)/gresource.xml

ui_resources.c: gresource.xml $(resource_files) ui_resources.h
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_ui --target=$@ --sourcedir=$(srcdir) --generate-source $(srcdir)/gresource.xml

icon_resource_files=$(shell $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_icons --sourcedir=$(top_srcdir)/data --generate-dependencies $(top_srcdir)/data/gresource.xml)

icon_resources.h:
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_icons --target=$@ --sourcedir=$(top_srcdir)/data --generate-header $(top_srcdir)/data/gresource.xml

icon_resources.c: gresource.xml $(resource_files) icon_resources.h
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --c-name=fsearch_icons --target=$@ --sourcedir=$(top_srcdir)/data --generate-source $(top_srcdir)/data/gresource.xml

fsearch_LDADD = $(FSEARCH_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(PCRE2_LIBS) $(ICU_LIBS) -lm

