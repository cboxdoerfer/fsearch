EXTRA_DIST = fsearch.glade \
	     shared.css \
	     gresource.xml \
	     preferences.ui \
	     overlay.ui \
	     menus.ui

if DEBUG
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE_CFLAGS) -lm -Wall -std=c11 -g -Ofast -DDEBUG=1
else
AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE_CFLAGS) -lm -Wall -std=c11 -Ofast
endif

bin_PROGRAMS = fsearch

fsearch_NOINST_H_FILES = fsearch.h \
			 array.h \
			 btree.h \
			 clipboard.h \
			 database.h \
			 database_search.h \
			 debug.h \
			 fsearch_config.h \
		     fsearch_db_entry.h \
			 fsearch_exclude_path.h \
			 fsearch_filter.h \
			 fsearch_include_path.h \
			 fsearch_limits.h \
			 fsearch_list_view.h \
			 fsearch_preferences_widgets.h \
			 fsearch_task.h \
			 fsearch_thread_pool.h \
			 fsearch_timer.h \
			 fsearch_window.h \
			 fsearch_window_actions.h \
			 listview_popup.h \
			 memory_pool.h \
			 preferences_ui.h \
			 query.h \
			 string_utils.h \
			 token.h \
			 ui_utils.h \
			 utf8.h \
			 utils.h

fsearch_SOURCES = main.c \
		  array.c \
		  btree.c \
		  clipboard.c \
		  database.c \
		  database_search.c \
		  fsearch.c \
		  fsearch_config.c \
		  fsearch_db_entry.c \
		  fsearch_exclude_path.c \
		  fsearch_filter.c \
		  fsearch_include_path.c \
		  fsearch_list_view.c \
		  fsearch_preferences_widgets.c \
		  fsearch_task.c \
		  fsearch_thread_pool.c \
		  fsearch_timer.c \
		  fsearch_window.c \
		  fsearch_window_actions.c \
		  listview_popup.c \
		  memory_pool.c \
		  preferences_ui.c \
		  query.c \
		  resources.c \
		  string_utils.c \
		  token.c \
		  ui_utils.c \
		  utils.c \
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

