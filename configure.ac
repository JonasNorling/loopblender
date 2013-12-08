dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(loopblender, 1.0)


AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE()

AC_PROG_CC_C99

PKG_CHECK_MODULES(JACK, jack)

LIBS="$LIBS -lm $JACK_LIBS"
CFLAGS="$CFLAGS -Wall -Werror $JACK_CFLAGS"

AC_CONFIG_FILES(Makefile src/Makefile)
AC_OUTPUT

