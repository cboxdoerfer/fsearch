AC_INIT([FSearch], [1], [christian.boxdoerfer@posteo.de])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_SRCDIR([src/fsearch.c])
AM_SILENT_RULES([yes])

AX_REQUIRE_DEFINED([IT_PROG_INTLTOOL])
IT_PROG_INTLTOOL([0.50.1])
GETTEXT_PACKAGE=AC_PACKAGE_TARNAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [GETTEXT package name])
AC_SUBST(GETTEXT_PACKAGE)
AM_GLIB_GNU_GETTEXT

AC_PROG_CC

CFLAGS+=" -Os"

AX_CHECK_COMPILE_FLAG([-std=c11], [CFLAGS+=" -std=c11" ],
                      [echo "C compiler cannot compile C11 code. Abort"
                       exit -1
                      ])

PKG_PROG_PKG_CONFIG
AC_PATH_PROG([GLIB_COMPILE_RESOURCES], [glib-compile-resources])

PKG_CHECK_MODULES(GLIB, glib-2.0 > 2.36.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, gtk+-3.0 > 3.12.0)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GIO, gio-2.0)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(PCRE, libpcre)
AC_SUBST(PCRE_CFLAGS)
AC_SUBST(PCRE_LIBS)

AC_CONFIG_FILES([Makefile
                 data/Makefile
                 data/fsearch.desktop.in
                 po/Makefile.in
                 src/Makefile])
AC_OUTPUT
