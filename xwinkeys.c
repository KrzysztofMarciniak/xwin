/*
 * xwinkeys - key bindings for xwinrouter (C89).
 *
 * Grabs the keys listed in config.h and writes the bound command, one line,
 * to stdout on every press. Point stdout at the router's FIFO:
 *
 *     xwinkeys > "$fifo" &
 *
 * Each line goes out in a single write(), so it never interleaves with lines
 * from the other FIFO writer (xwincomposer). Exits when the display goes away
 * or when nobody reads the FIFO any more.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <X11/keysym.h>

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define LOCKS (XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)
#define MODS (XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | \
              XCB_MOD_MASK_3 | XCB_MOD_MASK_4 | XCB_MOD_MASK_5)

struct key {
	unsigned int mod;
	unsigned long keysym;
	const char *cmd;
};

#define XWR_KEYS
#include "config.h"

static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_keycode_t keycodes[NELEM(keys)];

static void grab_keys(void)
{
	xcb_get_keyboard_mapping_reply_t *r;
	xcb_keysym_t *syms;
	unsigned int locks[4];
	int min = xcb_get_setup(conn)->min_keycode;
	int max = xcb_get_setup(conn)->max_keycode;
	int per, k, j;
	size_t i;

	locks[0] = 0;
	locks[1] = XCB_MOD_MASK_LOCK;
	locks[2] = XCB_MOD_MASK_2;
	locks[3] = XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2;
	r = xcb_get_keyboard_mapping_reply(conn,
	    xcb_get_keyboard_mapping(conn, min, max - min + 1), NULL);
	if (!r)
		return;
	syms = xcb_get_keyboard_mapping_keysyms(r);
	per = r->keysyms_per_keycode;
	xcb_ungrab_key(conn, XCB_GRAB_ANY, scr->root, XCB_MOD_MASK_ANY);
	for (i = 0; i < NELEM(keys); i++) {
		keycodes[i] = 0;
		for (k = 0; k <= max - min; k++)
			if (syms[k * per] == keys[i].keysym) {
				keycodes[i] = min + k;
				break;
			}
		if (!keycodes[i])
			continue;
		for (j = 0; j < 4; j++)
			xcb_grab_key(conn, 1, scr->root, keys[i].mod | locks[j], keycodes[i],
			    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	}
	free(r);
	xcb_flush(conn);
}

/* returns 0 on success, -1 when the reader is gone */
static int send_cmd(const char *cmd)
{
	char buf[1024];
	size_t n = strlen(cmd);

	if (n > sizeof buf - 2)
		return 0;                     /* too long to be atomic: skip */
	memcpy(buf, cmd, n);
	buf[n++] = '\n';
	return write(1, buf, n) < 0 && errno != EINTR ? -1 : 0;
}

int main(void)
{
	xcb_generic_event_t *ev;
	xcb_key_press_event_t *e;
	xcb_screen_iterator_t it;
	size_t i;
	int screen = 0, n;

	signal(SIGPIPE, SIG_IGN);
	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinkeys: cannot open display\n");
		return 1;
	}
	it = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (n = 0; n < screen; n++)
		xcb_screen_next(&it);
	scr = it.data;
	grab_keys();

	while ((ev = xcb_wait_for_event(conn)) != NULL) {
		switch (ev->response_type & ~0x80) {
		case XCB_KEY_PRESS:
			e = (xcb_key_press_event_t *)ev;
			for (i = 0; i < NELEM(keys); i++)
				if (keycodes[i] == e->detail && (e->state & MODS) == keys[i].mod)
					if (send_cmd(keys[i].cmd) < 0) {
						free(ev);
						return 0;
					}
			break;
		case XCB_MAPPING_NOTIFY:
			grab_keys();            /* keyboard layout changed */
			break;
		default:
			break;
		}
		free(ev);
	}
	return 0;
}
