AC_INIT([FSearch],
        m4_esyscmd([build-aux/git-version-gen --fallback "0.1.1" .tarball-version]),
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
AM_GNU_GETTEXT_VERSION([0.19.7])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.7])
AM_GNU_GETTEXT([external])

AC_PROG_CC
AC_PROG_INSTALL

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

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([inttypes.h limits.h locale.h stddef.h stdint.h stdlib.h string.h sys/param.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([atexit floor gettimeofday memset setlocale strchr strdup strpbrk strstr strverscmp])

PKG_PROG_PKG_CONFIG
AC_PATH_PROG([GLIB_COMPILE_RESOURCES], [glib-compile-resources])

PKG_CHECK_MODULES(GLIB, glib-2.0 > 2.36.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GTK, gtk+-3.0 > 3.12.0)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(GIO, gio-unix-2.0)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(PCRE, libpcre)
AC_SUBST(PCRE_CFLAGS)
AC_SUBST(PCRE_LIBS)

PKG_CHECK_MODULES(ICU, icu-uc > 3.8)
AC_SUBST(ICU_CFLAGS)
AC_SUBST(ICU_LIBS)

AC_CONFIG_FILES([Makefile
                 data/Makefile
                 data/fsearch.desktop.in
                 po/Makefile.in
                 src/Makefile])
AC_OUTPUT
