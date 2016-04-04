
# This is included from fizmo-dist and not required by libfizmo's own
# configuration. It nevertheless needs to be maintained so fizmo-dist
# will still work.
#
# The $build_prefix, $build_prefix_cflags and $build_prefix_libs are
# pre-defined by fizmo-dist.

AC_SUBST([libpixelif_CFLAGS], "-I$build_prefix_cflags $libdrilbo_CFLAGS $freetype2_CFLAGS")
AC_SUBST([libpixelif_LIBS], "-L$build_prefix_libs -lpixelif $libdrilbo_LIBS $freetype2_LIBS")

