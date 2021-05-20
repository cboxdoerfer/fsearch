EXTRA_DIST = fsearch.glade \
         statusbar.glade \
	     shared.css \
	     gresource.xml \
	     preferences.ui \
	     overlay.ui \
	     menus.ui

if DEBUG
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE_CFLAGS) -lm -Wall -std=c11 -D_FILE_OFFSET_BITS=64 -g -Ofast -DDEBUG=1
else
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE_CFLAGS) -lm -Wall -std=c11 -D_FILE_OFFSET_BITS=64 -Ofast
endif

bin_PROGRAMS = fsearch

fsearch_NOINST_H_FILES = fsearch.h \
			 fsearch_array.h \
			 fsearch_clipboard.h \
			 fsearch_config.h \
			 fsearch_database.h \
			 fsearch_database_entry.h \
			 fsearch_database_search.h \
			 fsearch_database_view.h \
			 fsearch_exclude_path.h \
			 fsearch_file_utils.h \
			 fsearch_filter.h \
		     fsearch_highlight_token.h \
			 fsearch_index.h \
			 fsearch_limits.h \
			 fsearch_list_view.h \
			 fsearch_listview_popup.h \
			 fsearch_memory_pool.h \
			 fsearch_preferences_ui.h \
			 fsearch_preferences_widgets.h \
			 fsearch_query.h \
             fsearch_result_view.h \
			 fsearch_selection.h \
			 fsearch_statusbar.h \
			 fsearch_string_utils.h \
			 fsearch_task.h \
			 fsearch_thread_pool.h \
			 fsearch_token.h \
			 fsearch_ui_utils.h \
			 fsearch_window.h \
			 fsearch_window_actions.h \
			 utf8.h

fsearch_SOURCES = main.c \
		  fsearch.c \
		  fsearch_array.c \
		  fsearch_clipboard.c \
		  fsearch_config.c \
		  fsearch_database.c \
		  fsearch_database_entry.c \
		  fsearch_database_search.c \
		  fsearch_database_view.c \
		  fsearch_exclude_path.c \
		  fsearch_file_utils.c \
		  fsearch_filter.c \
		  fsearch_highlight_token.c \
		  fsearch_index.c \
		  fsearch_list_view.c \
		  fsearch_listview_popup.c \
		  fsearch_memory_pool.c \
		  fsearch_preferences_ui.c \
		  fsearch_preferences_widgets.c \
		  fsearch_query.c \
          fsearch_result_view.c \
          fsearch_selection.c \
		  fsearch_statusbar.c \
		  fsearch_string_utils.c \
		  fsearch_task.c \
		  fsearch_thread_pool.c \
		  fsearch_token.c \
		  fsearch_ui_utils.c \
		  fsearch_window.c \
		  fsearch_window_actions.c \
		  resources.c \
		  $(fsearch_NOINST_H_FILES)

BUILT_SOURCES=resources.c resources.h

CLEANFILES = $(BUILT_SOURCES)

fsearch_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\"

resource_files=$(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)

resources.h:
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-header $(srcdir)/gresource.xml

resources.c: gresource.xml $(resource_files) resources.h
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $(srcdir)/gresource.xml

fsearch_LDADD = $(FSEARCH_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(PCRE_LIBS) -lm

