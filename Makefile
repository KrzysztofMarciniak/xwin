CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c89 -pedantic -Wall -Wextra
XCB_CFLAGS = $(shell pkg-config --cflags xcb)
XCB_LIBS   = $(shell pkg-config --libs xcb)
PREFIX  ?= /usr/local

all: xwinrouter xwincomposer xwinkeys xwinbar

xwinrouter: xwinrouter.c
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinrouter.c $(XCB_LIBS)

xwincomposer: xwincomposer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ xwincomposer.c

xwinkeys: xwinkeys.c config.h
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinkeys.c $(XCB_LIBS)

xwinbar: xwinbar.c
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinbar.c $(XCB_LIBS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/xwinrouter
	install -m 755 xwinrouter xwincomposer xwinkeys xwinbar $(DESTDIR)$(PREFIX)/bin
	install -m 755 xwinbar.sh $(DESTDIR)$(PREFIX)/share/xwinrouter

clean:
	rm -f xwinrouter xwincomposer xwinkeys xwinbar

.PHONY: all install clean
