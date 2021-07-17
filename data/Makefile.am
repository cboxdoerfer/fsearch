man_MANS = fsearch.1

desktopdir = $(datadir)/applications
desktop_in_in_files = fsearch.desktop.in.in
desktop_in_files = fsearch.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = \
         $(man_MANS) \
	     $(desktop_in_in_files) \
	     $(NULL)

CLEANFILES = \
	     $(desktop_DATA) \
	     $(desktop_in_files) \
	     $(NULL)

fsearch.desktop: fsearch.desktop.in
	    $(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
