EXTENSION_DIR=$(DESTDIR)/usr/lib/wyebrowser
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
	install -Dm755 wyeb   $(DESTDIR)/usr/bin/wyeb
	install -Dm755 ext.so   $(EXTENSION_DIR)/ext.so
	install -Dm644 wyeb.png   $(DESTDIR)/usr/share/pixmaps/wyeb.png
	install -Dm644 wyeb.desktop $(DESTDIR)/usr/share/applications/wyeb.desktop

uninstall:
	rm -f  $(DESTDIR)/usr/bin/wyeb
	rm -f  $(EXTENSION_DIR)/ext.so
	-rmdir $(EXTENSION_DIR)
	rm -f  $(DESTDIR)/usr/share/pixmaps/wyeb.png
	rm -f  $(DESTDIR)/usr/share/applications/wyeb.desktop


re: clean all
#	$(MAKE) clean
#	$(MAKE) all

full: re install
