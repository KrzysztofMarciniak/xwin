/*
 * xwinhelp - shows the key bindings and settings from config.h (C89).
 *
 * The help text is generated at run time from the keys[] and settings[]
 * tables in config.h, so it can never go out of date: edit config.h,
 * rebuild xwinhelp, done. The text is written to help.txt (in
 * $XDG_RUNTIME_DIR, else /tmp) and shown in a plain X11 window.
 *
 *   q, Escape, Ctrl-c         close
 *   j, Down / k, Up           scroll one line
 *   Space, PageDown / PageUp  scroll one page
 *   Home / End                jump to the top / bottom
 *
 *   xwinhelp -p               only print the text on stdout, no window
 *
 * The window is an ordinary dialog: xwinrouter manages it (floating and
 * centred), and MOD+q closes it like any other window. Bind it in config.h:
 *     { MOD, XK_F1, "spawn xwinhelp" },
 *
 * Needs only xcb, X11/keysym.h (header only) and the core font "fixed".
 */

/* ---------------------------------------------------------------- config --
 * Edit and recompile.
 */
#define FONT        "fixed"     /* core X font (xlsfonts) */
#define BG          0x000000    /* background RRGGBB */
#define FG          0xffffff    /* text RRGGBB */
#define PAD         6           /* space around the text, pixels */
#define MAXROWS     30          /* initial window height in lines, at most */

#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <X11/keysym.h>

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

struct key {
	unsigned int mod;
	unsigned long keysym;
	const char *cmd;
};

#include "config.h"

static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t gc;
static int ascent, descent, cw, row_h, win_w, win_h, maxchars, visible;

static char *text;                 /* the whole help text */
static size_t tlen, tcap;
static char **lines;               /* pointers into text, one per line */
static int nlines, longest, top;

/* ------------------------------------------------------------- the text -- */

static void add(const char *s)
{
	size_t n = strlen(s);

	if (tlen + n + 1 > tcap) {
		tcap = (tlen + n + 1) * 2;
		text = realloc(text, tcap);
		if (!text)
			exit(1);
	}
	memcpy(text + tlen, s, n + 1);
	tlen += n;
}

static const struct { unsigned long sym; const char *name; } names[] = {
	{ XK_Return,    "Return" },   { XK_KP_Enter, "Enter" },
	{ XK_Escape,    "Escape" },   { XK_Tab,      "Tab" },
	{ XK_BackSpace, "BackSpace" },{ XK_Delete,   "Delete" },
	{ XK_space,     "Space" },    { XK_comma,    "Comma" },
	{ XK_period,    "Period" },   { XK_Home,     "Home" },
	{ XK_End,       "End" },      { XK_Prior,    "PageUp" },
	{ XK_Next,      "PageDown" }, { XK_Left,     "Left" },
	{ XK_Right,     "Right" },    { XK_Up,       "Up" },
	{ XK_Down,      "Down" },     { XK_Print,    "Print" },
};

static void keyname(unsigned long s, char *out, size_t n)
{
	size_t i;

	for (i = 0; i < NELEM(names); i++)
		if (names[i].sym == s) {
			snprintf(out, n, "%s", names[i].name);
			return;
		}
	if (s >= XK_F1 && s <= XK_F12)
		snprintf(out, n, "F%lu", s - XK_F1 + 1);
	else if (s > 0x20 && s < 0x7f)
		snprintf(out, n, "%c", (char)s);
	else
		snprintf(out, n, "0x%lx", s);
}

static void combo(unsigned int mod, unsigned long sym, char *out, size_t n)
{
	char k[32];

	out[0] = 0;
	if (mod & XCB_MOD_MASK_4)       strncat(out, "Super+", n - strlen(out) - 1);
	if (mod & XCB_MOD_MASK_1)       strncat(out, "Alt+", n - strlen(out) - 1);
	if (mod & XCB_MOD_MASK_CONTROL) strncat(out, "Ctrl+", n - strlen(out) - 1);
	if (mod & XCB_MOD_MASK_SHIFT)   strncat(out, "Shift+", n - strlen(out) - 1);
	if (mod & XCB_MOD_MASK_3)       strncat(out, "Mod3+", n - strlen(out) - 1);
	if (mod & XCB_MOD_MASK_5)       strncat(out, "Mod5+", n - strlen(out) - 1);
	keyname(sym, k, sizeof k);
	strncat(out, k, n - strlen(out) - 1);
}

static void generate(void)
{
	char c[96], l[512];
	size_t i;

	add("xwin help\n\n");
	add("Close: q, Escape or MOD+q.  Scroll: j/k, arrows, Space, PageUp/PageDown.\n\n");
	add("KEYS\n");
	for (i = 0; i < NELEM(keys); i++) {
		combo(keys[i].mod, keys[i].keysym, c, sizeof c);
		snprintf(l, sizeof l, "  %-22s %s\n", c, keys[i].cmd);
		add(l);
	}
	add("\nSETTINGS\n");
	for (i = 0; i < NELEM(settings); i++) {
		snprintf(l, sizeof l, "  %s\n", settings[i]);
		add(l);
	}
}

/* help.txt: refuses symlinks, readable by the owner only */
static void write_file(void)
{
	char path[1024];
	const char *dir = getenv("XDG_RUNTIME_DIR");
	int fd;

	if (!dir || !*dir)
		dir = "/tmp";
	snprintf(path, sizeof path, "%s/help.txt", dir);
	unlink(path);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (fd < 0)
		return;
	if (write(fd, text, tlen) < 0)
		perror("xwinhelp: help.txt");
	close(fd);
}

static void split(void)
{
	char *copy, *p;
	int n = 0;
	size_t i;

	copy = malloc(tlen + 1);                /* text itself stays intact */
	for (i = 0; i < tlen; i++)
		if (text[i] == '\n')
			n++;
	lines = malloc((n + 1) * sizeof *lines);
	if (!copy || !lines)
		exit(1);
	memcpy(copy, text, tlen + 1);
	for (p = copy; p < copy + tlen; ) {
		char *nl = memchr(p, '\n', copy + tlen - p);
		int len;

		if (!nl)
			break;
		*nl = 0;
		lines[nlines++] = p;
		len = (int)(nl - p);
		if (len > longest)
			longest = len;
		p = nl + 1;
	}
}

/* ----------------------------------------------------------------- X11 -- */

static void clamp_top(void)
{
	if (top > nlines - visible)
		top = nlines - visible;
	if (top < 0)
		top = 0;
}

static void draw(void)
{
	char blank[512];
	int i, w = maxchars, n;

	if (w > (int)sizeof blank - 1)
		w = (int)sizeof blank - 1;
	if (w < 0)
		w = 0;
	memset(blank, ' ', w);
	blank[w] = 0;
	for (i = 0; i < visible; i++) {
		int y = i * row_h + PAD + ascent;

		xcb_image_text_8(conn, w, win, gc, PAD, y, blank);
		if (top + i < nlines) {
			n = (int)strlen(lines[top + i]);
			if (n > w)
				n = w;
			if (n > 0)
				xcb_image_text_8(conn, n, win, gc, PAD, y, lines[top + i]);
		}
	}
	xcb_flush(conn);
}

static void resized(int w, int h)
{
	win_w = w;
	win_h = h;
	maxchars = (w - 2 * PAD) / cw;
	visible = (h - 2 * PAD) / row_h;
	if (visible < 1)
		visible = 1;
	clamp_top();
}

static xcb_atom_t intern(const char *s)
{
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn,
	    xcb_intern_atom(conn, 0, strlen(s), s), NULL);
	xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;

	free(r);
	return a;
}

/* returns 1 when the window should close */
static int key(xcb_keysym_t sym, unsigned int state)
{
	if ((state & XCB_MOD_MASK_CONTROL) && (sym == XK_c || sym == XK_g))
		return 1;
	switch (sym) {
	case XK_q: case XK_Escape:
		return 1;
	case XK_j: case XK_Down:
		top++;
		break;
	case XK_k: case XK_Up:
		top--;
		break;
	case XK_space: case XK_Next:
		top += visible - 1;
		break;
	case XK_Prior:
		top -= visible - 1;
		break;
	case XK_Home:
		top = 0;
		break;
	case XK_End:
		top = nlines;
		break;
	default:
		return 0;
	}
	clamp_top();
	return 0;
}

int main(int argc, char **argv)
{
	xcb_screen_iterator_t it;
	xcb_query_font_reply_t *fr;
	xcb_get_keyboard_mapping_reply_t *km;
	xcb_generic_event_t *ev;
	xcb_key_press_event_t *ke;
	xcb_keysym_t *syms, sym;
	xcb_font_t font;
	xcb_atom_t a_proto, a_delete, a_type, a_dialog, a_name, a_utf8;
	uint32_t wv[3], gv[3];
	int screen = 0, i, per, min, max, col, rows, w, h, done = 0;
	const char *fontname = FONT, *title = "xwinhelp";

	generate();
	split();
	if (argc > 1 && strcmp(argv[1], "-p") == 0) {
		fwrite(text, 1, tlen, stdout);
		return 0;
	}
	write_file();

	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinhelp: cannot open display\n");
		return 1;
	}
	it = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (i = 0; i < screen; i++)
		xcb_screen_next(&it);
	scr = it.data;

	font = xcb_generate_id(conn);
	xcb_open_font(conn, font, strlen(fontname), fontname);
	fr = xcb_query_font_reply(conn, xcb_query_font(conn, font), NULL);
	if (!fr) {
		fprintf(stderr, "xwinhelp: cannot open font \"%s\"\n", fontname);
		return 1;
	}
	ascent = fr->font_ascent;
	descent = fr->font_descent;
	cw = fr->max_bounds.character_width;
	if (cw < 1)
		cw = 1;
	free(fr);
	row_h = ascent + descent + 2;

	rows = nlines < MAXROWS ? nlines : MAXROWS;
	w = (longest + 2) * cw + 2 * PAD;
	h = rows * row_h + 2 * PAD;
	if (w > scr->width_in_pixels * 9 / 10)
		w = scr->width_in_pixels * 9 / 10;
	if (h > scr->height_in_pixels * 9 / 10)
		h = scr->height_in_pixels * 9 / 10;
	resized(w, h);

	win = xcb_generate_id(conn);
	wv[0] = BG;
	wv[1] = FG;
	wv[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
	    XCB_EVENT_MASK_STRUCTURE_NOTIFY;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
	    0, 0, w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
	    XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK, wv);

	gc = xcb_generate_id(conn);
	gv[0] = FG;
	gv[1] = BG;
	gv[2] = font;
	xcb_create_gc(conn, gc, win,
	    XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gv);

	/* name, class, "dialog" type (the router floats and centres it) and
	 * WM_DELETE_WINDOW so that MOD+q closes it cleanly */
	a_proto = intern("WM_PROTOCOLS");
	a_delete = intern("WM_DELETE_WINDOW");
	a_type = intern("_NET_WM_WINDOW_TYPE");
	a_dialog = intern("_NET_WM_WINDOW_TYPE_DIALOG");
	a_name = intern("_NET_WM_NAME");
	a_utf8 = intern("UTF8_STRING");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME,
	    XCB_ATOM_STRING, 8, strlen(title), title);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, a_name,
	    a_utf8, 8, strlen(title), title);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS,
	    XCB_ATOM_STRING, 8, 18, "xwinhelp\0xwinhelp");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, a_type,
	    XCB_ATOM_ATOM, 32, 1, &a_dialog);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, a_proto,
	    XCB_ATOM_ATOM, 32, 1, &a_delete);

	xcb_map_window(conn, win);
	xcb_flush(conn);

	min = xcb_get_setup(conn)->min_keycode;
	max = xcb_get_setup(conn)->max_keycode;
	km = xcb_get_keyboard_mapping_reply(conn,
	    xcb_get_keyboard_mapping(conn, min, max - min + 1), NULL);
	if (!km)
		return 1;
	syms = xcb_get_keyboard_mapping_keysyms(km);
	per = km->keysyms_per_keycode;

	while (!done && (ev = xcb_wait_for_event(conn)) != NULL) {
		switch (ev->response_type & ~0x80) {
		case XCB_EXPOSE:
			draw();
			break;
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *ce = (xcb_configure_notify_event_t *)ev;

			if (ce->width != win_w || ce->height != win_h) {
				resized(ce->width, ce->height);
				draw();
			}
			break;
		}
		case XCB_KEY_PRESS:
			ke = (xcb_key_press_event_t *)ev;
			col = (ke->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
			sym = syms[(ke->detail - min) * per + (col < per ? col : 0)];
			if (!sym)
				sym = syms[(ke->detail - min) * per];
			done = key(sym, ke->state);
			if (!done)
				draw();
			break;
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *me = (xcb_client_message_event_t *)ev;

			if (me->type == a_proto && me->data.data32[0] == a_delete)
				done = 1;
			break;
		}
		case XCB_DESTROY_NOTIFY:
			done = 1;
			break;
		}
		free(ev);
	}
	xcb_disconnect(conn);
	return 0;
}
