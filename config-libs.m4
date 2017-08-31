
PKG_CHECK_MODULES(
  [libfreetype2],
  [freetype2],
  [AS_IF([test "x$libpixelif_reqs" != "x"], [
     libpixelif_reqs+=", "
   ])
   libpixelif_reqs+="freetype2"])

