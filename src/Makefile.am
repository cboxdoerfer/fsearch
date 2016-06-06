EXTRA_DIST = fsearch.glade \
	     shared.css \
	     popover.ui \
	     preferences.ui \
	     overlay.ui \
	     menus.ui

AM_CFLAGS = $(FSEARCH_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(PCRE_CFLAGS) -Wall -std=c11 -g -Ofast
bin_PROGRAMS = fsearch
fsearch_SOURCES = main.c \
		  fsearch.c \
		  fsearch_window.c \
		  fsearch_window_actions.c \
		  clipboard.c \
		  clipboard.h \
		  config.c \
		  database.c \
		  database_search.c \
		  iconstore.c \
		  list_model.c \
		  preferences_ui.c \
		  resources.c \
		  ui_utils.c \
		  fsearch_thread_pool.c \
		  array.c \
		  string_utils.c \
		  btree.c \
		  listview.c \
		  utils.c

BUILT_SOURCES=resources.c resources.h

resource_files=$(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)

resources.h:
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-header $(srcdir)/gresource.xml

resources.c: gresource.xml $(resource_files) resources.h
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $(srcdir)/gresource.xml

fsearch_LDADD = $(FSEARCH_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(PCRE_LIBS)

