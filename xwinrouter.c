/*
 * xwinrouter - X11 window router (C89).
 *
 * It is the window manager mechanism and nothing else: no layout, no focus
 * policy. Commands come in on stdin, events go out on stdout, one per line.
 * Key bindings live in xwinkeys, which writes commands into the same FIFO.
 *
 * Commands (ID is hex like 0x400003, or "@" for the focused window):
 *   geom ID X Y W H        move/resize (outer size, border included)
 *   focus ID|next|prev|none
 *   close [ID]
 *   tag ID N               move window to tag N (1-9)
 *   view N                 show tag N
 *   float ID [0|1]         no value = toggle
 *   fullscreen ID [0|1]
 *   raise ID
 *   spawn COMMAND...       run through sh -c
 *   set KEY VALUE...       change a setting (see below)
 *   dump                   re-send the whole state as events
 *   quit
 *   anything else          forwarded to stdout as "bind LINE"
 *
 * Events:
 *   map ID TAG FLOAT | unmap ID | focus ID|none | tag ID N | view N
 *   float ID 0|1 | fullscreen ID 0|1 | workarea X Y W H
 *   enter ID | activate ID | desktop N | bind LINE
 *
 * Settings (xwinkeys sends them at startup; see config.h there):
 *   set border N | color_focus RRGGBB | color_normal RRGGBB
 *   set floating 0|1 | fullscreen 0|1 | tags 1..9
 *   set <anything else>    kept, forwarded as "bind set ..." and replayed
 *                          to the composer on "dump"
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define MAXFSET 16

/* Runtime settings. These are only defaults: xwinkeys sends the real ones
 * with "set" at startup. */
static int border_w = 2;
static unsigned long col_focus = 0x5e81ac, col_normal = 0x3b4252;
static int en_floating = 1, en_fullscreen = 1, ntags = 9;

/* "set" lines meant for xwincomposer, remembered so "dump" can replay them */
static char fset[MAXFSET][128];
static int nfset;

struct client {
	xcb_window_t win;
	int x, y, w, h;            /* outer geometry, border included */
	int sx, sy, sw, sh;        /* saved while fullscreen */
	int tag;
	int floating, fullscreen, dock;
	int strut[4];              /* left right top bottom (docks) */
	struct client *next;
};

enum {
	A_PROTOCOLS, A_DELETE, A_TAKEFOCUS, A_SUPPORTED, A_CHECK, A_CLIENTLIST,
	A_ACTIVE, A_NDESK, A_CURDESK, A_WMNAME, A_UTF8, A_STRUTP, A_TYPE,
	A_TYPE_DOCK, A_TYPE_DIALOG, A_TYPE_SPLASH, A_TYPE_UTILITY, A_STATE,
	A_FULLSCREEN, A_CLOSE, A_COUNT
};
static const char *aname[A_COUNT] = {
	"WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "_NET_SUPPORTED",
	"_NET_SUPPORTING_WM_CHECK", "_NET_CLIENT_LIST", "_NET_ACTIVE_WINDOW",
	"_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP", "_NET_WM_NAME",
	"UTF8_STRING", "_NET_WM_STRUT_PARTIAL", "_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DIALOG",
	"_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_UTILITY",
	"_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN", "_NET_CLOSE_WINDOW"
};

static xcb_atom_t atom[A_COUNT];
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t checkwin;
static struct client *clients, *focused;
static int view = 1, quiet, all_visible;
static int wax, way, waw, wah;
static volatile sig_atomic_t quit;

/* ---------- helpers ---------- */

static void emit(const char *fmt, ...)
{
	va_list ap;

	if (quiet)
		return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

static struct client *find(xcb_window_t w)
{
	struct client *c;

	for (c = clients; c; c = c->next)
		if (c->win == w)
			return c;
	return NULL;
}

static int visible(const struct client *c)
{
	return all_visible || c->tag == view;
}

static struct client *lookup(const char *s)
{
	if (!s || s[0] == '@')
		return focused;
	return find((xcb_window_t)strtoul(s, NULL, 0));
}

/* read up to n 32-bit values of a property; returns how many */
static int prop32(xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, uint32_t *out, int n)
{
	xcb_get_property_reply_t *r;
	int got = 0;

	r = xcb_get_property_reply(conn, xcb_get_property(conn, 0, w, prop, type, 0, n), NULL);
	if (r) {
		got = xcb_get_property_value_length(r) / 4;
		if (got > n)
			got = n;
		memcpy(out, xcb_get_property_value(r), got * 4);
		free(r);
	}
	return got;
}

static int has_atom(xcb_window_t w, xcb_atom_t prop, xcb_atom_t a)
{
	uint32_t v[32];
	int i, n = prop32(w, prop, XCB_ATOM_ATOM, v, 32);

	for (i = 0; i < n; i++)
		if (v[i] == a)
			return 1;
	return 0;
}

static void send_proto(xcb_window_t w, xcb_atom_t a)
{
	xcb_client_message_event_t ev;

	memset(&ev, 0, sizeof ev);
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 32;
	ev.window = w;
	ev.type = atom[A_PROTOCOLS];
	ev.data.data32[0] = a;
	ev.data.data32[1] = XCB_CURRENT_TIME;
	xcb_send_event(conn, 0, w, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
}

/* ---------- geometry, focus ---------- */

static void calc_workarea(void)
{
	struct client *c;
	int i, s[4], ox = wax, oy = way, ow = waw, oh = wah;

	s[0] = s[1] = s[2] = s[3] = 0;
	for (c = clients; c; c = c->next)
		if (c->dock)
			for (i = 0; i < 4; i++)
				if (c->strut[i] > s[i])
					s[i] = c->strut[i];
	wax = s[0];
	way = s[2];
	waw = scr->width_in_pixels - s[0] - s[1];
	wah = scr->height_in_pixels - s[2] - s[3];
	if (waw < 1)
		waw = 1;
	if (wah < 1)
		wah = 1;
	if (ox != wax || oy != way || ow != waw || oh != wah)
		emit("workarea %d %d %d %d", wax, way, waw, wah);
}

static void ewmh_clients(void)
{
	uint32_t ids[1024];
	struct client *c;
	int n = 0;

	for (c = clients; c && n < 1024; c = c->next)
		if (!c->dock)
			ids[n++] = c->win;
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
	    atom[A_CLIENTLIST], XCB_ATOM_WINDOW, 32, n, ids);
}

static void apply(struct client *c)
{
	int bw = c->fullscreen ? 0 : border_w;
	int x = visible(c) ? c->x : -2 * (int)scr->width_in_pixels;
	int w = c->w - 2 * bw, h = c->h - 2 * bw;
	uint32_t v[5];

	v[0] = x;
	v[1] = c->y;
	v[2] = w > 0 ? w : 1;
	v[3] = h > 0 ? h : 1;
	v[4] = bw;
	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
	    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
	    XCB_CONFIG_WINDOW_BORDER_WIDTH, v);
}

static void raise_win(struct client *c)
{
	uint32_t m = XCB_STACK_MODE_ABOVE;

	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, &m);
}

static void border(struct client *c, uint32_t col)
{
	xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, &col);
}

static void set_focus(struct client *c)
{
	uint32_t w;

	if (focused && focused != c)
		border(focused, col_normal);
	focused = c;
	if (c) {
		border(c, col_focus);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		if (has_atom(c->win, atom[A_PROTOCOLS], atom[A_TAKEFOCUS]))
			send_proto(c->win, atom[A_TAKEFOCUS]);
		if (c->floating || c->fullscreen)
			raise_win(c);
		w = c->win;
		emit("focus 0x%lx", (unsigned long)c->win);
	} else {
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, scr->root, XCB_CURRENT_TIME);
		w = XCB_NONE;
		emit("focus none");
	}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
	    atom[A_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &w);
	xcb_flush(conn);
}

static void cycle(int dir)
{
	struct client *v[256], *c;
	int n = 0, cur = -1;

	for (c = clients; c && n < 256; c = c->next)
		if (!c->dock && visible(c)) {
			if (c == focused)
				cur = n;
			v[n++] = c;
		}
	if (n)
		set_focus(v[cur < 0 ? 0 : (cur + dir + n) % n]);
}

static void set_fullscreen(struct client *c, int v)
{
	uint32_t fs = atom[A_FULLSCREEN];

	if (!en_fullscreen)
		return;
	if (v < 0)
		v = !c->fullscreen;
	if (v == c->fullscreen)
		return;
	if (v) {
		c->sx = c->x; c->sy = c->y; c->sw = c->w; c->sh = c->h;
		c->x = c->y = 0;
		c->w = scr->width_in_pixels;
		c->h = scr->height_in_pixels;
		c->fullscreen = 1;
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win,
		    atom[A_STATE], XCB_ATOM_ATOM, 32, 1, &fs);
		raise_win(c);
	} else {
		c->fullscreen = 0;
		c->x = c->sx; c->y = c->sy; c->w = c->sw; c->h = c->sh;
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win,
		    atom[A_STATE], XCB_ATOM_ATOM, 32, 0, &fs);
	}
	apply(c);
	emit("fullscreen 0x%lx %d", (unsigned long)c->win, c->fullscreen);
}

static void set_view(int n)
{
	struct client *c;
	uint32_t v;

	if (n < 1 || n > ntags)
		return;
	view = n;
	for (c = clients; c; c = c->next)
		if (!c->dock)
			apply(c);
	v = n - 1;
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
	    atom[A_CURDESK], XCB_ATOM_CARDINAL, 32, 1, &v);
	emit("view %d", n);
	if (focused && !visible(focused))
		set_focus(NULL);
	xcb_flush(conn);
}

/* ---------- managing windows ---------- */

static void read_strut(struct client *c)
{
	uint32_t v[12];
	int i, n;

	memset(v, 0, sizeof v);
	n = prop32(c->win, atom[A_STRUTP], XCB_ATOM_CARDINAL, v, 12);
	for (i = 0; i < 4; i++)
		c->strut[i] = n >= 4 ? (int)v[i] : 0;
}

static void manage(xcb_window_t w)
{
	xcb_get_window_attributes_reply_t *a;
	xcb_get_geometry_reply_t *g;
	struct client *c, **p;
	uint32_t mask, tr;

	if (find(w))
		return;
	a = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, w), NULL);
	g = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, w), NULL);
	if (!a || !g || a->override_redirect) {
		free(a);
		free(g);
		return;
	}
	free(a);
	c = calloc(1, sizeof *c);
	if (!c) {
		free(g);
		return;
	}
	c->win = w;
	for (p = &clients; *p; p = &(*p)->next)
		;
	*p = c;

	if (has_atom(w, atom[A_TYPE], atom[A_TYPE_DOCK])) {
		c->dock = 1;
		read_strut(c);
		mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
		xcb_change_window_attributes(conn, w, XCB_CW_EVENT_MASK, &mask);
		xcb_map_window(conn, w);
		free(g);
		calc_workarea();
		ewmh_clients();
		return;
	}

	c->tag = view;
	c->floating = en_floating &&
	    (prop32(w, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, &tr, 1) == 1
	    || has_atom(w, atom[A_TYPE], atom[A_TYPE_DIALOG])
	    || has_atom(w, atom[A_TYPE], atom[A_TYPE_SPLASH])
	    || has_atom(w, atom[A_TYPE], atom[A_TYPE_UTILITY]));
	if (c->floating) {
		c->w = g->width + 2 * border_w;
		c->h = g->height + 2 * border_w;
		c->x = g->x;
		c->y = g->y;
		if ((c->x <= 0 && c->y <= 0) || c->x + c->w <= 0) {    /* centre */
			c->x = wax + (waw - c->w) / 2;
			c->y = way + (wah - c->h) / 2;
		}
	} else {                 /* sane default until the layout answers */
		c->x = wax;
		c->y = way;
		c->w = waw;
		c->h = wah;
	}
	free(g);

	mask = XCB_EVENT_MASK_ENTER_WINDOW;
	xcb_change_window_attributes(conn, w, XCB_CW_EVENT_MASK, &mask);
	border(c, col_normal);
	apply(c);
	xcb_map_window(conn, w);
	ewmh_clients();
	emit("map 0x%lx %d %d", (unsigned long)w, c->tag, c->floating);
	if (has_atom(w, atom[A_STATE], atom[A_FULLSCREEN]))
		set_fullscreen(c, 1);
	xcb_flush(conn);
}

static void unmanage(struct client *c)
{
	struct client **p;
	xcb_window_t id = c->win;
	int dock = c->dock, was_focused = (focused == c);

	for (p = &clients; *p && *p != c; p = &(*p)->next)
		;
	if (*p)
		*p = c->next;
	free(c);
	if (was_focused)
		focused = NULL;
	ewmh_clients();
	if (dock) {
		calc_workarea();
	} else {
		emit("unmap 0x%lx", (unsigned long)id);
		if (was_focused)
			set_focus(NULL);
	}
	xcb_flush(conn);
}

/* ---------- commands ---------- */

static void spawn(const char *cmd)
{
	int fd;

	if (fork() != 0)
		return;
	if (conn)
		close(xcb_get_file_descriptor(conn));
	setsid();
	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, 0);
		dup2(fd, 1);
	}
	execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
	_exit(127);
}

static void kill_win(struct client *c)
{
	if (!c || c->dock)
		return;
	if (has_atom(c->win, atom[A_PROTOCOLS], atom[A_DELETE]))
		send_proto(c->win, atom[A_DELETE]);
	else
		xcb_kill_client(conn, c->win);
	xcb_flush(conn);
}

static void dump(void)
{
	struct client *c;
	int i;

	for (i = 0; i < nfset; i++)
		emit("bind %s", fset[i]);
	emit("workarea %d %d %d %d", wax, way, waw, wah);
	emit("view %d", view);
	for (c = clients; c; c = c->next) {
		if (c->dock)
			continue;
		emit("map 0x%lx %d %d", (unsigned long)c->win, c->tag, c->floating);
		if (c->fullscreen)
			emit("fullscreen 0x%lx 1", (unsigned long)c->win);
	}
	if (focused)
		emit("focus 0x%lx", (unsigned long)focused->win);
	else
		emit("focus none");
}

static void recolor(void)
{
	struct client *c;

	for (c = clients; c; c = c->next)
		if (!c->dock)
			border(c, c == focused ? col_focus : col_normal);
}

static void set_ntags(int n)
{
	struct client *c;
	uint32_t v = n;

	if (n < 1 || n > 9)
		return;
	ntags = n;
	for (c = clients; c; c = c->next)
		if (!c->dock && c->tag > n) {
			c->tag = n;
			apply(c);
			emit("tag 0x%lx %d", (unsigned long)c->win, n);
		}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
	    atom[A_NDESK], XCB_ATOM_CARDINAL, 32, 1, &v);
	if (view > n)
		set_view(n);
}

/* keep the latest "set KEY ..." line per KEY */
static void remember(const char *line, const char *key)
{
	int i, kl = (int)strlen(key);
	size_t n;

	for (i = 0; i < nfset; i++)
		if (!strncmp(fset[i] + 4, key, kl) && fset[i][4 + kl] == ' ')
			break;
	if (i == nfset) {
		if (nfset == MAXFSET)
			return;
		nfset++;
	}
	n = strlen(line);
	if (n > sizeof fset[i] - 1)
		n = sizeof fset[i] - 1;
	memcpy(fset[i], line, n);
	fset[i][n] = 0;
}

/* set KEY VALUE...: router settings apply here, the rest goes to the composer */
static void setting(char **v, int n)
{
	struct client *c;
	char b[128];
	int i, a = atoi(v[2]);

	if (!strcmp(v[1], "border")) {
		if (a >= 0 && a <= 50) {
			border_w = a;
			for (c = clients; c; c = c->next)
				if (!c->dock)
					apply(c);
		}
	} else if (!strcmp(v[1], "color_focus")) {
		col_focus = strtoul(v[2], NULL, 16);
		recolor();
	} else if (!strcmp(v[1], "color_normal")) {
		col_normal = strtoul(v[2], NULL, 16);
		recolor();
	} else if (!strcmp(v[1], "floating")) {
		en_floating = !!a;
	} else if (!strcmp(v[1], "fullscreen")) {
		en_fullscreen = !!a;
	} else if (!strcmp(v[1], "tags")) {
		set_ntags(a);
	} else {
		strcpy(b, "set");
		for (i = 1; i < n; i++) {
			strncat(b, " ", sizeof b - strlen(b) - 1);
			strncat(b, v[i], sizeof b - strlen(b) - 1);
		}
		remember(b, v[1]);
		emit("bind %s", b);
	}
}

static void run(const char *line)
{
	char buf[512], *v[8];
	struct client *c;
	int n, t;

	if (strncmp(line, "spawn ", 6) == 0) {
		spawn(line + 6);
		return;
	}
	strncpy(buf, line, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	n = 0;
	v[0] = strtok(buf, " \t\r");
	while (v[n] && n < 7) {
		n++;
		v[n] = strtok(NULL, " \t\r");
	}
	if (!n)
		return;

	if (!strcmp(v[0], "geom") && n >= 6) {
		c = lookup(v[1]);
		if (c && !c->dock && !c->fullscreen) {
			c->x = atoi(v[2]);
			c->y = atoi(v[3]);
			c->w = atoi(v[4]);
			c->h = atoi(v[5]);
			apply(c);
		}
	} else if (!strcmp(v[0], "focus") && n >= 2) {
		if (!strcmp(v[1], "next"))
			cycle(1);
		else if (!strcmp(v[1], "prev"))
			cycle(-1);
		else if (!strcmp(v[1], "none"))
			set_focus(NULL);
		else if ((c = lookup(v[1])) && !c->dock && visible(c))
			set_focus(c);
	} else if (!strcmp(v[0], "close")) {
		kill_win(n >= 2 ? lookup(v[1]) : focused);
	} else if (!strcmp(v[0], "tag") && n >= 3) {
		c = lookup(v[1]);
		t = atoi(v[2]);
		if (c && !c->dock && t >= 1 && t <= ntags) {
			c->tag = t;
			apply(c);
			emit("tag 0x%lx %d", (unsigned long)c->win, t);
			if (c == focused && !visible(c))
				set_focus(NULL);
		}
	} else if (!strcmp(v[0], "view") && n >= 2) {
		set_view(atoi(v[1]));
	} else if (!strcmp(v[0], "float")) {
		c = lookup(n >= 2 ? v[1] : NULL);
		if (en_floating && c && !c->dock) {
			c->floating = n >= 3 ? !!atoi(v[2]) : !c->floating;
			if (c->floating)
				raise_win(c);
			emit("float 0x%lx %d", (unsigned long)c->win, c->floating);
		}
	} else if (!strcmp(v[0], "fullscreen")) {
		c = lookup(n >= 2 ? v[1] : NULL);
		if (c && !c->dock)
			set_fullscreen(c, n >= 3 ? !!atoi(v[2]) : -1);
	} else if (!strcmp(v[0], "raise") && n >= 2) {
		c = lookup(v[1]);
		if (c && !c->dock)
			raise_win(c);
	} else if (!strcmp(v[0], "set") && n >= 3) {
		setting(v, n);
	} else if (!strcmp(v[0], "dump")) {
		dump();
	} else if (!strcmp(v[0], "quit")) {
		quit = 1;
	} else {
		emit("bind %s", line);
	}
	xcb_flush(conn);
}

/* ---------- X events ---------- */

static void synth_configure(struct client *c)
{
	xcb_configure_notify_event_t ev;
	int bw = c->fullscreen ? 0 : border_w;

	memset(&ev, 0, sizeof ev);
	ev.response_type = XCB_CONFIGURE_NOTIFY;
	ev.event = ev.window = c->win;
	ev.x = visible(c) ? c->x : -2 * (int)scr->width_in_pixels;
	ev.y = c->y;
	ev.width = c->w - 2 * bw;
	ev.height = c->h - 2 * bw;
	ev.border_width = bw;
	xcb_send_event(conn, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&ev);
}

static void configure_request(xcb_configure_request_event_t *e)
{
	struct client *c = find(e->window);
	uint32_t v[7];
	int i = 0;

	if (c && !c->dock && (c->fullscreen || !c->floating)) {
		synth_configure(c);                    /* the layout decides */
	} else if (c && !c->dock) {                    /* floating: honour it */
		if (e->value_mask & XCB_CONFIG_WINDOW_X)      c->x = e->x;
		if (e->value_mask & XCB_CONFIG_WINDOW_Y)      c->y = e->y;
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)  c->w = e->width + 2 * border_w;
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) c->h = e->height + 2 * border_w;
		apply(c);
	} else {                                       /* not ours: pass through */
		if (e->value_mask & XCB_CONFIG_WINDOW_X)            v[i++] = e->x;
		if (e->value_mask & XCB_CONFIG_WINDOW_Y)            v[i++] = e->y;
		if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH)        v[i++] = e->width;
		if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT)       v[i++] = e->height;
		if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) v[i++] = e->border_width;
		if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING)      v[i++] = e->sibling;
		if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)   v[i++] = e->stack_mode;
		xcb_configure_window(conn, e->window, e->value_mask, v);
	}
}

static void event(xcb_generic_event_t *ev)
{
	struct client *c;

	switch (ev->response_type & ~0x80) {
	case XCB_MAP_REQUEST: {
		xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
		if (!find(e->window))
			manage(e->window);
		else
			xcb_map_window(conn, e->window);
		break;
	}
	case XCB_CONFIGURE_REQUEST:
		configure_request((xcb_configure_request_event_t *)ev);
		break;
	case XCB_UNMAP_NOTIFY: {
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;
		if ((c = find(e->window)))
			unmanage(c);
		break;
	}
	case XCB_DESTROY_NOTIFY: {
		xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
		if ((c = find(e->window)))
			unmanage(c);
		break;
	}
	case XCB_ENTER_NOTIFY: {
		xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
		if (e->mode == XCB_NOTIFY_MODE_NORMAL && (c = find(e->event)) && !c->dock)
			emit("enter 0x%lx", (unsigned long)c->win);
		break;
	}
	case XCB_PROPERTY_NOTIFY: {
		xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;
		if ((c = find(e->window)) && c->dock && e->atom == atom[A_STRUTP]) {
			read_strut(c);
			calc_workarea();
		}
		break;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
		const uint32_t *d = e->data.data32;
		c = find(e->window);
		if (e->type == atom[A_CURDESK]) {
			emit("desktop %lu", (unsigned long)d[0] + 1);
		} else if (!c || c->dock) {
			break;
		} else if (e->type == atom[A_ACTIVE]) {
			emit("activate 0x%lx", (unsigned long)c->win);
		} else if (e->type == atom[A_CLOSE]) {
			kill_win(c);
		} else if (e->type == atom[A_STATE] &&
		    (d[1] == atom[A_FULLSCREEN] || d[2] == atom[A_FULLSCREEN])) {
			set_fullscreen(c, d[0] == 2 ? -1 : (int)d[0]);
		}
		break;
	}
	default:
		break;
	}
	xcb_flush(conn);
}

/* ---------- setup / main ---------- */

static int setup(void)
{
	xcb_screen_iterator_t it;
	xcb_void_cookie_t ck;
	xcb_generic_error_t *err;
	xcb_intern_atom_cookie_t ic[A_COUNT];
	xcb_query_tree_reply_t *t;
	xcb_window_t *ch;
	xcb_atom_t sup[10];
	uint32_t mask, v;
	int i, n, screen = 0;

	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinrouter: cannot open display\n");
		return -1;
	}
	it = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (i = 0; i < screen; i++)
		xcb_screen_next(&it);
	scr = it.data;

	mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
	    XCB_EVENT_MASK_PROPERTY_CHANGE;
	ck = xcb_change_window_attributes_checked(conn, scr->root, XCB_CW_EVENT_MASK, &mask);
	if ((err = xcb_request_check(conn, ck))) {
		free(err);
		fprintf(stderr, "xwinrouter: another window manager is running\n");
		return -1;
	}

	for (i = 0; i < A_COUNT; i++)
		ic[i] = xcb_intern_atom(conn, 0, strlen(aname[i]), aname[i]);
	for (i = 0; i < A_COUNT; i++) {
		xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, ic[i], NULL);
		atom[i] = r ? r->atom : XCB_NONE;
		free(r);
	}

	/* the EWMH minimum that bars and apps expect */
	checkwin = xcb_generate_id(conn);
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, checkwin, scr->root, -1, -1, 1, 1, 0,
	    XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, NULL);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, atom[A_CHECK],
	    XCB_ATOM_WINDOW, 32, 1, &checkwin);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, checkwin, atom[A_CHECK],
	    XCB_ATOM_WINDOW, 32, 1, &checkwin);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, checkwin, atom[A_WMNAME],
	    atom[A_UTF8], 8, 10, "xwinrouter");
	n = 0;
	sup[n++] = atom[A_CLIENTLIST];
	sup[n++] = atom[A_ACTIVE];
	sup[n++] = atom[A_NDESK];
	sup[n++] = atom[A_CURDESK];
	sup[n++] = atom[A_WMNAME];
	sup[n++] = atom[A_STRUTP];
	sup[n++] = atom[A_STATE];
	sup[n++] = atom[A_FULLSCREEN];
	sup[n++] = atom[A_CLOSE];
	sup[n++] = atom[A_TYPE];
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, atom[A_SUPPORTED],
	    XCB_ATOM_ATOM, 32, n, sup);
	v = ntags;
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, atom[A_NDESK],
	    XCB_ATOM_CARDINAL, 32, 1, &v);
	v = 0;
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, atom[A_CURDESK],
	    XCB_ATOM_CARDINAL, 32, 1, &v);

	/* adopt whatever is already mapped, silently: the layout asks for "dump" */
	quiet = 1;
	wax = way = 0;
	waw = scr->width_in_pixels;
	wah = scr->height_in_pixels;
	t = xcb_query_tree_reply(conn, xcb_query_tree(conn, scr->root), NULL);
	if (t) {
		ch = xcb_query_tree_children(t);
		n = xcb_query_tree_children_length(t);
		for (i = 0; i < n; i++) {
			xcb_get_window_attributes_reply_t *a = xcb_get_window_attributes_reply(
			    conn, xcb_get_window_attributes(conn, ch[i]), NULL);
			int ok = a && !a->override_redirect &&
			    a->map_state == XCB_MAP_STATE_VIEWABLE;
			free(a);
			if (ok)
				manage(ch[i]);
		}
		free(t);
	}
	calc_workarea();
	quiet = 0;
	xcb_flush(conn);
	return 0;
}

static void cleanup(void)
{
	struct client *c;

	all_visible = 1;                 /* bring hidden windows back on screen */
	for (c = clients; c; c = c->next)
		if (!c->dock)
			apply(c);
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, scr->root, XCB_CURRENT_TIME);
	xcb_destroy_window(conn, checkwin);
	xcb_flush(conn);
	xcb_disconnect(conn);
}

static void onsig(int s)
{
	(void)s;
	quit = 1;
}

int main(void)
{
	struct pollfd p[2];
	struct sigaction sa;
	xcb_generic_event_t *ev;
	char ibuf[1024], *s, *nl;
	int ilen = 0, ifd = 0, n;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onsig;          /* no SA_RESTART: poll must wake up */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);       /* spawned programs are reaped for us */

	if (setup() < 0)
		return 1;

	while (!quit) {
		while ((ev = xcb_poll_for_event(conn)) != NULL) {
			event(ev);
			free(ev);
		}
		if (xcb_connection_has_error(conn))
			break;
		xcb_flush(conn);
		p[0].fd = xcb_get_file_descriptor(conn);
		p[0].events = POLLIN;
		p[0].revents = 0;
		p[1].fd = ifd;               /* -1 once stdin is gone: poll skips it */
		p[1].events = POLLIN;
		p[1].revents = 0;
		if (poll(p, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (ifd >= 0 && (p[1].revents & (POLLIN | POLLHUP))) {
			n = (int)read(ifd, ibuf + ilen, sizeof ibuf - 1 - ilen);
			if (n <= 0) {
				if (n < 0 && (errno == EINTR || errno == EAGAIN))
					continue;
				ifd = -1;            /* stdin gone: keep managing windows */
			} else {
				ilen += n;
				ibuf[ilen] = 0;
				s = ibuf;
				while ((nl = strchr(s, '\n')) != NULL) {
					*nl = 0;
					run(s);
					s = nl + 1;
				}
				ilen = (int)strlen(s);
				memmove(ibuf, s, ilen + 1);
				if (ilen >= (int)sizeof ibuf - 1)
					ilen = 0;       /* overlong line: drop it */
			}
		}
	}
	cleanup();
	return 0;
}
