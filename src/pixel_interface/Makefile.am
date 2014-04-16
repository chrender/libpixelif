
AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = $(libfizmo_CFLAGS) $(freetype2_CFLAGS)
AM_CPPFLAGS =
LDADD = $(libfizmo_LIBS) $(freetype2_LIBS)

localedir = $(datarootdir)/fizmo/locales
fontdir = $(datarootdir)/fizmo/fonts

# Path elements are seperated by colons.
#AM_CFLAGS += -DFONT_DEFAULT_SEARCH_PATH=\"$(fontdir):.\"
AM_CFLAGS += -DFONT_DEFAULT_SEARCH_PATH=\"$(fontdir)\"

noinst_LIBRARIES = libpixelif.a
libpixelif_a_SOURCES = ../locales/libpixelif_locales.c pixel_interface.c \
  true_type_wordwrapper.c true_type_font.c true_type_factory.c

if ENABLE_TRACING
AM_CFLAGS += -DENABLE_TRACING=
endif

