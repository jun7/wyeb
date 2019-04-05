PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATAROOTDIR ?= $(PREFIX)/share
EXTENSION_DIR ?= $(PREFIX)/lib/wyebrowser
DISTROURI ?= https://www.archlinux.org/
DISTRONAME ?= "Arch Linux"

PKG_CONFIG ?= pkg-config

ifeq ($(DEBUG), 1)
	CFLAGS += -Wall -Wno-deprecated-declarations
else
	CFLAGS += -Wno-deprecated-declarations
endif

all: wyeb ext.so

wyeb: main.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< \
		`$(PKG_CONFIG) --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		-DEXTENSION_DIR=\"$(EXTENSION_DIR)\" \
		-DDISTROURI=\"$(DISTROURI)\" \
		-DDISTRONAME=\"$(DISTRONAME)\" \
		-DDEBUG=${DEBUG} -lm

ext.so: ext.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -shared -fPIC \
		`$(PKG_CONFIG) --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		-DDEBUG=${DEBUG} -DJSC=${JSC}

clean:
	rm -f wyeb ext.so

install: all
	install -Dm755 wyeb   $(DESTDIR)$(BINDIR)/wyeb
	install -Dm755 ext.so   $(DESTDIR)$(EXTENSION_DIR)/ext.so
	install -Dm644 wyeb.png   $(DESTDIR)$(DATAROOTDIR)/pixmaps/wyeb.png
	install -Dm644 wyeb.desktop $(DESTDIR)$(DATAROOTDIR)/applications/wyeb.desktop

uninstall:
	rm -f  $(BINDIR)/wyeb
	rm -f  $(EXTENSION_DIR)/ext.so
	-rmdir $(EXTENSION_DIR)
	rm -f  $(DATAROOTDIR)/pixmaps/wyeb.png
	rm -f  $(DATAROOTDIR)/applications/wyeb.desktop


re: clean all
#	$(MAKE) clean
#	$(MAKE) all

full: re install
