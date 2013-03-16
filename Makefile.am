
AUTOMAKE_OPTIONS = -Wno-override

localedir = $(datarootdir)/fizmo/locales
fontdir = $(datarootdir)/fizmo/fonts

fizmo_build_prefix=$(DEV_BUILD_PREFIX)

PKG_DIR = $(fizmo_build_prefix)/lib/pkgconfig
PKGFILE = $(PKG_DIR)/libpixelif.pc

SUBDIRS = src/pixel_interface .
noinst_LIBRARIES = libpixelif.a

libpixelif.a::
	cd src/pixel_interface ; make
	mv src/pixel_interface/libpixelif.a .

install-dev:: libpixelif.a
	mkdir -p $(fizmo_build_prefix)/lib/fizmo
	mkdir -p $(fizmo_build_prefix)/include/fizmo/pixel_interface
	cp src/pixel_interface/*.h \
	  $(fizmo_build_prefix)/include/fizmo/pixel_interface
	cp libpixelif.a $(fizmo_build_prefix)/lib/fizmo
	cp src/screen_interface/screen_pixel_interface.h \
	          "$(fizmo_build_prefix)"/include/fizmo/screen_interface
	mkdir -p $(PKG_DIR)
	echo 'prefix=$(fizmo_build_prefix)' >$(PKGFILE)
	echo 'exec_prefix=$${prefix}' >>$(PKGFILE)
	echo 'libdir=$${exec_prefix}/lib/fizmo' >>$(PKGFILE)
	echo 'includedir=$${prefix}/include/fizmo' >>$(PKGFILE)
	echo >>$(PKGFILE)
	echo 'Name: libpixelif' >>$(PKGFILE)
	echo 'Description: libpixelif' >>$(PKGFILE)
	echo 'Version: 0.1.0' >>$(PKGFILE)
	echo 'Requires: libfizmo >= 0.7.5, $(LIBPIXELIF_REQS)' >>$(PKGFILE)
	echo 'Requires.private:' >>$(PKGFILE)
	echo 'Cflags: -I$(fizmo_build_prefix)/include/fizmo' >>$(PKGFILE)
	echo 'Libs: -L$(fizmo_build_prefix)/lib/fizmo -lpixelif' >>$(PKGFILE)
	echo >>$(PKGFILE)

install-data-local:
	mkdir -p "$(DESTDIR)$(localedir)"
	for l in `cd src/locales ; ls -d ??_??`; \
        do \
          mkdir -p "$(DESTDIR)$(localedir)/$$l" ; \
          cp src/locales/$$l/*.txt "$(DESTDIR)$(localedir)/$$l" ; \
        done
	mkdir -p $(DESTDIR)$(fontdir)
	cp -r fonts/* $(DESTDIR)$(fontdir)

