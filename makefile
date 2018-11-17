PREFIX ?= /usr
EXTENSION_DIR ?= $(PREFIX)/lib/wyebrowser
ifeq ($(DEBUG), 1)
	CFLAGS += -Wall -Wno-deprecated-declarations
else
	CFLAGS += -Wno-deprecated-declarations
endif

all: wyeb ext.so

wyeb: main.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		-DEXTENSION_DIR=\"$(EXTENSION_DIR)\" \
		-DDEBUG=${DEBUG} -lm

ext.so: ext.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -shared -fPIC \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		-DDEBUG=${DEBUG} -DJSC=${JSC}

clean:
	rm -f wyeb ext.so

install: all
	install -Dm755 wyeb   $(DESTDIR)$(PREFIX)/bin/wyeb
	install -Dm755 ext.so   $(DESTDIR)$(EXTENSION_DIR)/ext.so
	install -Dm644 wyeb.png   $(DESTDIR)$(PREFIX)/share/pixmaps/wyeb.png
	install -Dm644 wyeb.desktop $(DESTDIR)$(PREFIX)/share/applications/wyeb.desktop

uninstall:
	rm -f  $(PREFIX)/bin/wyeb
	rm -f  $(EXTENSION_DIR)/ext.so
	-rmdir $(EXTENSION_DIR)
	rm -f  $(PREFIX)/share/pixmaps/wyeb.png
	rm -f  $(PREFIX)/share/applications/wyeb.desktop


re: clean all
#	$(MAKE) clean
#	$(MAKE) all

full: re install
