#CFLAGS += -c -Wall -Wno-deprecated-declarations
CFLAGS += -Wno-deprecated-declarations
#LDFLAGS=
EXTENSION_DIR=$(DESTDIR)/usr/lib/wyebrowser
ifndef DEBUG
DEBUG = 0
endif
DDEBUG=-DDEBUG=${DEBUG}

all: wyeb ext.so

wyeb: main.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		-DEXTENSION_DIR=\"$(EXTENSION_DIR)\" \
		$(DDEBUG) -lm

ext.so: ext.c general.c makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -shared -fPIC \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0` \
		$(DDEBUG)

clean:
	rm -f wyeb ext.so

install: all
	install -Dm755 wyeb   $(DESTDIR)/bin/wyeb
	install -Dm755 ext.so   $(EXTENSION_DIR)/ext.so
	install -Dm644 wyeb.png   $(DESTDIR)/usr/share/pixmaps/wyeb.png
	install -Dm644 wyeb.desktop $(DESTDIR)/usr/share/applications/wyeb.desktop

