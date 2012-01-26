
CC = gcc
AR = ar
override CFLAGS += -Wall -Wextra

prefix = $(DESTDIR)/usr/local
bindir = $(prefix)/games
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
localedir = $(datarootdir)/fizmo/locales


# -----
# General settings:
ENABLE_OPTIMIZATION = 1
#ENABLE_TRACING = 1
#ENABLE_GDB_SYMBOLS = 1
# -----



# -----
# Settings for libpixelif:
LIBFREETYPE2_CFLAGS = $(shell freetype-config --cflags)
LIBFREETYPE2_LIBS = $(shell freetype-config --libs)
# -----

