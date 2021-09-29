man_MANS = fsearch.1

desktopdir = $(datadir)/applications
desktop_in_in_files = fsearch.desktop.in.in
desktop_in_files = fsearch.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

iconsdir = $(datadir)/icons/hicolor/scalable/apps
icons_DATA = \
	     io.github.cboxdoerfer.FSearch.svg

EXTRA_DIST = \
         $(man_MANS) \
         $(icons_DATA) \
	     $(desktop_in_in_files) \
	     gresource.xml \
	     $(NULL)

CLEANFILES = \
	     $(desktop_DATA) \
	     $(desktop_in_files) \
	     $(NULL)

fsearch.desktop: fsearch.desktop.in
	    $(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
