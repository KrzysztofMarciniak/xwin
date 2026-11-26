CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c89 -pedantic -Wall -Wextra
XCB_CFLAGS = $(shell pkg-config --cflags xcb)
XCB_LIBS   = $(shell pkg-config --libs xcb)
PREFIX  ?= /usr/local

all: xwinrouter xwincomposer xwinkeys xwinbar xwinmenu xwinhelp

xwinrouter: xwinrouter.c
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinrouter.c $(XCB_LIBS)

xwincomposer: xwincomposer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ xwincomposer.c

xwinkeys: xwinkeys.c config.h
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinkeys.c $(XCB_LIBS)

xwinbar: xwinbar.c
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinbar.c $(XCB_LIBS)

xwinmenu: xwinmenu.c
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinmenu.c $(XCB_LIBS)

xwinhelp: xwinhelp.c config.h
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinhelp.c $(XCB_LIBS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/xwinrouter
	install -m 755 xwinrouter xwincomposer xwinkeys xwinbar xwinmenu xwinhelp $(DESTDIR)$(PREFIX)/bin
	install -m 755 xwinbar.sh $(DESTDIR)$(PREFIX)/share/xwinrouter
	install -m 755 xwinnotify.sh $(DESTDIR)$(PREFIX)/bin/xwinnotify

clean:
	rm -f xwinrouter xwincomposer xwinkeys xwinbar xwinmenu xwinhelp

.PHONY: all install clean
