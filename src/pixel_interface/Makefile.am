AM_CFLAGS = $(libfizmo_CFLAGS) $(freetype2_CFLAGS)
AM_CPPFLAGS =
LDADD = $(libfizmo_LIBS) $(freetype2_LIBS)

localedir = $(datarootdir)/fizmo/locales
fontdir = $(datarootdir)/fizmo/fonts

AM_CFLAGS += -DFONT_DEFAULT_SEARCH_PATH=\"$(fontdir):.:$(fontdir)/source-code-pro-1_017:$(fontdir)/source-sans-pro-1_038\"

noinst_LIBRARIES = libpixelif.a
libpixelif_a_SOURCES = ../locales/libpixelif_locales.c pixel_interface.c \
  true_type_wordwrapper.c true_type_font.c true_type_factory.c

if ENABLE_TRACING
AM_CFLAGS += -DENABLE_TRACING=
endif

