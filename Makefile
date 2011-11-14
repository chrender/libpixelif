
.PHONY : all install install-dev install-locales clean distclean

include config.mk

ifeq ($(fizmo_build_prefix),)
  fizmo_build_prefix="$(prefix)"
endif
PKG_DIR = $(fizmo_build_prefix)/lib/pkgconfig
PKGFILE = $(PKG_DIR)/libpixelif.pc


all: libpixelif.a

libpixelif.a: src/pixel_interface/libpixelif.a
	mv src/pixel_interface/libpixelif.a .

src/pixel_interface/libpixelif.a::
	cd src/pixel_interface ; make

install:: install-locales

install-dev:: libpixelif.a
	mkdir -p "$(fizmo_build_prefix)"/lib/fizmo
	mkdir -p "$(fizmo_build_prefix)"/include/fizmo/pixel_interface
	cp src/pixel_interface/*.h \
	  "$(fizmo_build_prefix)"/include/fizmo/pixel_interface
	cp libpixelif.a "$(fizmo_build_prefix)"/lib/fizmo
	cp src/screen_interface/screen_pixel_interface.h \
	  "$(fizmo_build_prefix)"/include/fizmo/screen_interface
	mkdir -p "$(PKG_DIR)"
	echo 'prefix=$(fizmo_build_prefix)' >"$(PKGFILE)"
	echo 'exec_prefix=$${prefix}' >>"$(PKGFILE)"
	echo 'libdir=$${exec_prefix}/lib/fizmo' >>"$(PKGFILE)"
	echo 'includedir=$${prefix}/include/fizmo' >>"$(PKGFILE)"
	echo >>"$(PKGFILE)"
	echo 'Name: libpixelif' >>"$(PKGFILE)"
	echo 'Description: libpixelif' >>"$(PKGFILE)"
	echo 'Version: 0.1.0' >>"$(PKGFILE)"
	echo 'Requires: libfizmo >= 0.7 ' >>"$(PKGFILE)"
	echo 'Requires.private:' >>"$(PKGFILE)"
	echo 'Cflags: -I$(fizmo_build_prefix)/include/fizmo ' >>"$(PKGFILE)"
	echo 'Libs: -L$(fizmo_build_prefix)/lib/fizmo -lpixelif'  >>"$(PKGFILE)"
	echo >>"$(PKGFILE)"

install-locales::
	mkdir -p "$(localedir)"
	for l in `cd src/locales ; ls -d ??_??`; \
	do \
	  mkdir -p "$(localedir)/$$l" ; \
	  cp src/locales/$$l/*.txt "$(localedir)/$$l" ; \
	done

clean::
	cd src/pixel_interface ; make clean
	cd src/locales ; make clean

distclean:: clean
	rm -f libpixelif.a
	cd src/pixel_interface ; make distclean

