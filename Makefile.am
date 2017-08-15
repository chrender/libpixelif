
# Makefile.am
#
# This file is part of fizmo.
#
# Copyright (c) 2009-2017 Christoph Ender.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


AUTOMAKE_OPTIONS = -Wno-override

localedir = $(datarootdir)/fizmo/locales
fontdir = $(datarootdir)/fizmo/fonts

if FIZMO_DIST_BUILD
  dev_prefix=$(FIZMO_DIST_BUILD_PREFIX)
else
  dev_prefix=$(DESTDIR)/$(prefix)
endif

pkg_dir = $(dev_prefix)/lib/pkgconfig
pkgfile = $(pkg_dir)/libpixelif.pc

SUBDIRS = src/pixel_interface .
noinst_LIBRARIES = libpixelif.a

libpixelif.a::
	cd src/pixel_interface ; make
	mv src/pixel_interface/libpixelif.a .

install-dev:: libpixelif.a
	mkdir -p $(dev_prefix)/lib/fizmo
	mkdir -p $(dev_prefix)/include/fizmo/pixel_interface
	cp src/pixel_interface/*.h \
	  $(dev_prefix)/include/fizmo/pixel_interface
	cp libpixelif.a $(dev_prefix)/lib/fizmo
	mkdir -p $(dev_prefix)/include/fizmo/screen_interface
	cp src/screen_interface/screen_pixel_interface.h \
	  "$(dev_prefix)"/include/fizmo/screen_interface
	mkdir -p $(pkg_dir)
	echo 'prefix=$(dev_prefix)' >$(pkgfile)
	echo 'exec_prefix=$${prefix}' >>$(pkgfile)
	echo 'libdir=$${exec_prefix}/lib/fizmo' >>$(pkgfile)
	echo 'includedir=$${prefix}/include/fizmo' >>$(pkgfile)
	echo >>$(pkgfile)
	echo 'Name: libpixelif' >>$(pkgfile)
	echo 'Description: libpixelif' >>$(pkgfile)
	echo 'Version: 0.8.4' >>$(pkgfile)
	echo 'Requires: libfizmo >= 0.7.15, libdrilbo >= 0.2.11, $(LIBPIXELIF_REQS)' >>$(pkgfile)
	echo 'Requires.private:' >>$(pkgfile)
	echo 'Cflags: -I$(dev_prefix)/include/fizmo' >>$(pkgfile)
	echo 'Libs: -L$(dev_prefix)/lib/fizmo -lpixelif -ldrilbo' >>$(pkgfile)
	echo >>$(pkgfile)

install-data-local:
	mkdir -p "$(DESTDIR)$(localedir)"
	for l in `cd src/locales ; ls -d ??_??`; \
        do \
          mkdir -p "$(DESTDIR)$(localedir)/$$l" ; \
          cp src/locales/$$l/*.txt "$(DESTDIR)$(localedir)/$$l" ; \
        done
	mkdir -p $(DESTDIR)$(fontdir)
	cp -r fonts/* $(DESTDIR)$(fontdir)

# About "-rmdir": make ignores errors of commands which are prefixed with
# a - sign. rmdir may fail in clean-dev in case other libs have installed
# development files, or in case clean-dev has been run before.
clean-dev::
	-rm "$(dev_prefix)/lib/fizmo/libpixelif.a"
	-rmdir "$(dev_prefix)/lib/fizmo"
	rm -rf "$(dev_prefix)/include/fizmo/pixel_interface"
	-rm "$(dev_prefix)/include/fizmo/screen_interface/screen_pixel_interface.h"
	-rmdir "$(dev_prefix)/include/fizmo/screen_interface"
	-rmdir "$(dev_prefix)/include/fizmo"
	-rm "$(pkgfile)"

