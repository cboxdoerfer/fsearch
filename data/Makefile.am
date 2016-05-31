@INTLTOOL_DESKTOP_RULE@

@INTLTOOL_XML_RULE@
desktopdir = $(datadir)/applications
desktop_in_in_files = fsearch.desktop.in.in
desktop_in_files = fsearch.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = \
	     $(desktop_in_in_files) \
	     $(NULL)

CLEANFILES = \
	     $(desktop_DATA) \
	     $(desktop_in_files) \
	     $(NULL)
