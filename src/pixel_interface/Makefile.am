AM_CFLAGS = $(libfizmo_CFLAGS) $(freetype2_CFLAGS)
AM_CPPFLAGS =
LDADD = $(libfizmo_LIBS) $(freetype2_LIBS)

localedir = $(datarootdir)/fizmo/locales

noinst_LIBRARIES = libpixelif.a
libpixelif_a_SOURCES = ../locales/libpixelif_locales.c pixel_interface.c

if ENABLE_TRACING
AM_CFLAGS += -DENABLE_TRACING=
endif

