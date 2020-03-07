AC_INIT([FSearch],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]),
        [christian.boxdoerfer@posteo.de],
        [fsearch],
        [https://cboxdoerfer.github.io/fsearch])
AM_INIT_AUTOMAKE([-Wall foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/fsearch.c])
AM_SILENT_RULES([yes])

GETTEXT_PACKAGE=fsearch
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [GETTEXT package name])
AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT([external])

AC_PROG_CC

AC_ARG_ENABLE(debug,
              AS_HELP_STRING([--enable-debug],
                             [enable debugging, default: no]),
                             [case "${enableval}" in
                              yes) debug=true;;
                              no)  debug=false;;
                              *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
                          esac],
                          [debug=false])

AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")

CFLAGS+=" -Os"

AX_CHECK_COMPILE_FLAG([-std=c11], [CFLAGS+=" -std=c11" ],
                      [echo "C compiler cannot compile C11 code. Abort"
                       exit -1
                      ])

AC_FUNC_ALLOCA

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
