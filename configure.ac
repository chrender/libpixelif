
AC_ARG_ENABLE([tracing],
 [AS_HELP_STRING([--enable-tracing],
                 [enable tracelog for debugging])],
 [],
 [enable_tracing=no])

AC_INIT([libpixelif], [0.1.0])

libpixelif_reqs="freetype2"
PKG_CHECK_MODULES([libfizmo], [libfizmo >= 0.7.10])
m4_include(config-libs.m4)

AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_PROG_RANLIB
AM_PROG_AR

AC_SUBST([DEV_BUILD_PREFIX], "$DESTDIR$prefix")
AC_SUBST([LIBPIXELIF_REQS], $libpixelif_reqs)

AM_CONDITIONAL([ENABLE_TRACING],
                [test "$enable_tracing" = "yes"])

AC_CONFIG_FILES([Makefile src/pixel_interface/Makefile])
AC_OUTPUT

