
.PHONY : clean

include ../../config.mk

ifneq ($(ENABLE_GDB_SYMBOLS),)
override CFLAGS += -g
endif

ifneq ($(ENABLE_OPTIMIZATION),)
override CFLAGS += -O2
endif

ifneq ($(ENABLE_TRACING),)
override CFLAGS += -DENABLE_TRACING=
endif

LIBFIZMO_CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config  \
 --cflags libfizmo)

libpixelif.a: pixel_interface.o ../locales/libpixelif_locales.o
	"$(AR)" rcs libpixelif.a pixel_interface.o ../locales/libpixelif_locales.o

pixel_interface.o: pixel_interface.h pixel_interface.c
	"$(CC)" $(CFLAGS) -c pixel_interface.c $(LIBFIZMO_CFLAGS)

../locales/libpixelif_locales.o::
	cd ../locales ; make

clean:
	rm -f *.o

distclean: clean
	rm -f libpixelif.a

