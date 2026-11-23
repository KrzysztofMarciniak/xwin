CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c89 -pedantic -Wall -Wextra
XCB_CFLAGS = $(shell pkg-config --cflags xcb)
XCB_LIBS   = $(shell pkg-config --libs xcb)
PREFIX  ?= /usr/local

all: xwinrouter xwinkeys xwincomposer

xwinrouter: xwinrouter.c config.h
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinrouter.c $(XCB_LIBS)

xwinkeys: xwinkeys.c config.h
	$(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) -o $@ xwinkeys.c $(XCB_LIBS)

xwincomposer: xwincomposer.c config.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ xwincomposer.c

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 xwinrouter xwinkeys xwincomposer $(DESTDIR)$(PREFIX)/bin

clean:
	rm -f xwinrouter xwinkeys xwincomposer

.PHONY: all install clean
