/*
 * xwinbar - a simple status bar (C89).
 *
 * The bar reads lines from a FIFO and shows the last one. Anything can write
 * to the FIFO, so the same bar takes the status line and notifications:
 *
 *   text      status line, stays until replaced
 *   !text     notification: shown for -t seconds, then the status comes back
 *   !         clears the notification now
 *
 * The FIFO path is the optional argument. Feed it yourself:
 *
 *   xwinbar "$bfifo" &
 *   xwinbar.sh > "$bfifo" &
 *   xwinnotify "build finished"
 *
 * Defaults for all of these, plus gaps and height, are in the config block below.
 *
 * usage: xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad]
 *                [-i fifo] [-t secs] [FIFO]
 *   -b  put the bar at the bottom
 *   -a  text alignment: l, c or r
 *   -f  core X font name (list them with xlsfonts)
 *   -B  background color, -F text color (hex)
 *   -p  vertical padding in pixels
 *   -i  FIFO path, same as the FIFO argument (which wins if both are given).
 *       Default $XDG_RUNTIME_DIR/xwinbar.fifo, else /tmp/...
 *       Created if missing, removed on exit if xwinbar created it.
 *   -t  seconds a notification stays
 *
 * The bar is a dock window with _NET_WM_STRUT_PARTIAL, so xwinrouter keeps
 * tiled windows out of its way. Core X fonts are Latin-1 only. The FIFO is
 * opened read-write so it never sees EOF when a writer closes (Linux/BSD).
 */

/* ---------------------------------------------------------------- config --
 * Edit and recompile. Command-line options override these.
 */
#define BOTTOM      0           /* 1: bar at the bottom of the screen */
#define ALIGN       'r'         /* text: 'l' left, 'c' centre, 'r' right */
#define FONT        "fixed"     /* core X font (xlsfonts); a smaller font = a smaller bar */
#define BG          0x000000    /* background RRGGBB */
#define FG          0xffffff    /* text RRGGBB */
#define NOTE_SECS   5           /* seconds a notification stays */

#define PAD         3           /* space above and below the text, pixels */
#define HEIGHT      0           /* bar height in pixels; 0: font height + 2 * PAD */
#define SIDE        8           /* space between the text and the bar's left/right end */
#define MARGIN_X    12           /* gap between the bar and the left AND right screen edge */
#define MARGIN_Y    12           /* gap between the bar and the top (or bottom) screen edge */
#define WIN_GAP     6           /* extra space between the bar and the windows below (or above) it */

#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>


struct src {
	int fd;
	int len;
	char buf[1024];
};

static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t gc;
static xcb_font_t font;
static int bar_h;
static int ascent, descent, bar_w, align = ALIGN, ffd = -1, secs = NOTE_SECS;
static char status[256], note[256], text[256];
static time_t note_until;         /* 0: no notification showing */
static volatile sig_atomic_t quit;

static xcb_atom_t intern(const char *name)
{
	xcb_intern_atom_reply_t *r;
	xcb_atom_t a;

	r = xcb_intern_atom_reply(conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
	a = r ? r->atom : XCB_NONE;
	free(r);
	return a;
}

static void draw(void)
{
	xcb_char2b_t s16[256];
	xcb_query_text_extents_reply_t *r;
	int n = (int)strlen(text), i, x, w = 0;

	xcb_clear_area(conn, 0, win, 0, 0, 0, 0);
	if (n) {
		for (i = 0; i < n; i++) {
			s16[i].byte1 = 0;
			s16[i].byte2 = (unsigned char)text[i];
		}
		r = xcb_query_text_extents_reply(conn,
		    xcb_query_text_extents(conn, font, n, s16), NULL);
		if (r) {
			w = r->overall_width;
			free(r);
		}
		if (align == 'l')
			x = SIDE;
		else if (align == 'c')
			x = (bar_w - w) / 2;
		else
			x = bar_w - w - SIDE;
		xcb_image_text_8(conn, n, win, gc, x,
		    (bar_h - (ascent + descent)) / 2 + ascent, text);
	}
	xcb_flush(conn);
}

/* what is shown: the notification while it lasts, else the status */
static void show(void)
{
	strcpy(text, note_until ? note : status);
	draw();
}

static void setstr(char *dst, const char *src)
{
	size_t n = strlen(src);
	char *p;

	if (n > 255)
		n = 255;
	memcpy(dst, src, n);
	dst[n] = 0;
	for (p = dst; *p; p++)
		if ((unsigned char)*p < 32)
			*p = ' ';
}

static void line(const char *l)
{
	if (l[0] == '!') {
		setstr(note, l + 1);
		note_until = note[0] ? time(NULL) + secs : 0;
	} else {
		setstr(status, l);
	}
	show();
}

/* read what is there, handle every complete line; -1 if the source ended */
static int pump(struct src *s)
{
	char *p, *nl, tmp[512];
	int n;

	n = (int)read(s->fd, tmp, sizeof tmp);
	if (n < 0)
		return (errno == EINTR || errno == EAGAIN) ? 0 : -1;
	if (n == 0)
		return -1;
	if (s->len + n > (int)sizeof s->buf - 1)
		s->len = 0;                     /* overlong line: drop it */
	memcpy(s->buf + s->len, tmp, n);
	s->len += n;
	s->buf[s->len] = 0;
	p = s->buf;
	while ((nl = strchr(p, '\n')) != NULL) {
		*nl = 0;
		line(p);
		p = nl + 1;
	}
	s->len = (int)strlen(p);
	memmove(s->buf, p, s->len + 1);
	if (s->len >= (int)sizeof s->buf - 1)
		s->len = 0;                     /* overlong line: drop it */
	return 0;
}

static void onsig(int s)
{
	(void)s;
	quit = 1;
}

int main(int argc, char **argv)
{
	const char *fontname = FONT, *fifo = NULL;
	char fifobuf[256];
	unsigned long bg = BG, fg = FG;
	int bottom = BOTTOM, pad = PAD, bad = 0, o, n, screen = 0, y, timeout, made = 0;
	uint32_t wv[2], gv[3], st[12], st4[4], type;
	xcb_query_font_reply_t *fr;
	xcb_void_cookie_t ck;
	xcb_generic_error_t *err;
	xcb_generic_event_t *ev;
	struct pollfd pf[2];
	struct sigaction sa;
	struct src in;
	struct stat sb;
	time_t now;

	while ((o = getopt(argc, argv, "ba:f:B:F:p:i:t:")) != -1) {
		switch (o) {
		case 'b': bottom = 1; break;
		case 'a': align = optarg[0]; break;
		case 'f': fontname = optarg; break;
		case 'B': bg = strtoul(optarg, NULL, 16); break;
		case 'F': fg = strtoul(optarg, NULL, 16); break;
		case 'p': pad = atoi(optarg); break;
		case 'i': fifo = optarg; break;
		case 't': secs = atoi(optarg); break;
		default: bad = 1; break;
		}
		if (bad)
			break;
	}
	if (bad || optind + 1 < argc) {
		fprintf(stderr, "usage: xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] "
		    "[-F RRGGBB] [-p pad] [-i fifo] [-t secs] [FIFO]\n");
		return 2;
	}
	if (optind < argc)
		fifo = argv[optind];
	if (secs < 1)
		secs = 1;
	if (!fifo) {
		const char *d = getenv("XDG_RUNTIME_DIR");

		sprintf(fifobuf, "%.200s/xwinbar.fifo", d && *d ? d : "/tmp");
		fifo = fifobuf;
	}

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onsig;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	if (stat(fifo, &sb) < 0) {
		if (mkfifo(fifo, 0600) < 0) {
			fprintf(stderr, "xwinbar: cannot create fifo \"%s\"\n", fifo);
			return 1;
		}
		made = 1;
	} else if (!S_ISFIFO(sb.st_mode)) {
		fprintf(stderr, "xwinbar: \"%s\" is not a fifo\n", fifo);
		return 1;
	}
	ffd = open(fifo, O_RDWR | O_NONBLOCK);
	if (ffd < 0) {
		fprintf(stderr, "xwinbar: cannot open fifo \"%s\"\n", fifo);
		if (made)
			unlink(fifo);
		return 1;
	}

	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinbar: cannot open display\n");
		if (made)
			unlink(fifo);
		return 1;
	}
	{
		xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
		for (n = 0; n < screen; n++)
			xcb_screen_next(&it);
		scr = it.data;
	}

	font = xcb_generate_id(conn);
	ck = xcb_open_font_checked(conn, font, strlen(fontname), fontname);
	if ((err = xcb_request_check(conn, ck))) {
		free(err);
		fprintf(stderr, "xwinbar: cannot open font \"%s\"\n", fontname);
		if (made)
			unlink(fifo);
		return 1;
	}
	fr = xcb_query_font_reply(conn, xcb_query_font(conn, font), NULL);
	if (!fr) {
		if (made)
			unlink(fifo);
		return 1;
	}
	ascent = fr->font_ascent;
	descent = fr->font_descent;
	bar_h = HEIGHT > 0 ? HEIGHT :
	    fr->font_ascent + fr->font_descent + 2 * (pad < 0 ? 0 : pad);
	bar_w = (int)scr->width_in_pixels - 2 * MARGIN_X;
	if (bar_w < 1)
		bar_w = 1;
	free(fr);

	y = bottom ? (int)scr->height_in_pixels - bar_h - MARGIN_Y : MARGIN_Y;
	win = xcb_generate_id(conn);
	wv[0] = bg;
	wv[1] = XCB_EVENT_MASK_EXPOSURE;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root, MARGIN_X, y,
	    bar_w, bar_h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	    scr->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, wv);
	gc = xcb_generate_id(conn);
	gv[0] = fg;
	gv[1] = bg;
	gv[2] = font;
	xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gv);

	/* dock window + strut: the router reads these when the window is mapped */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME,
	    XCB_ATOM_STRING, 8, 7, "xwinbar");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS,
	    XCB_ATOM_STRING, 8, 16, "xwinbar\0xwinbar");
	type = intern("_NET_WM_WINDOW_TYPE_DOCK");
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, intern("_NET_WM_WINDOW_TYPE"),
	    XCB_ATOM_ATOM, 32, 1, &type);
	memset(st, 0, sizeof st);
	memset(st4, 0, sizeof st4);
	if (bottom) {
		st[3] = st4[3] = bar_h + MARGIN_Y + WIN_GAP;
		st[10] = MARGIN_X;
		st[11] = scr->width_in_pixels - 1 - MARGIN_X;
	} else {
		st[2] = st4[2] = bar_h + MARGIN_Y + WIN_GAP;
		st[8] = MARGIN_X;
		st[9] = scr->width_in_pixels - 1 - MARGIN_X;
	}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, intern("_NET_WM_STRUT_PARTIAL"),
	    XCB_ATOM_CARDINAL, 32, 12, st);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, intern("_NET_WM_STRUT"),
	    XCB_ATOM_CARDINAL, 32, 4, st4);
	xcb_map_window(conn, win);
	xcb_flush(conn);

	memset(&in, 0, sizeof in);
	in.fd = ffd;

	while (!quit) {
		while ((ev = xcb_poll_for_event(conn)) != NULL) {
			if ((ev->response_type & ~0x80) == XCB_EXPOSE)
				draw();
			free(ev);
		}
		if (xcb_connection_has_error(conn))
			break;
		xcb_flush(conn);

		timeout = -1;
		if (note_until) {
			now = time(NULL);
			if (now >= note_until) {
				note_until = 0;
				show();
			} else {
				timeout = (int)(note_until - now) * 1000;
			}
		}
		pf[0].fd = xcb_get_file_descriptor(conn);
		pf[0].events = POLLIN;
		pf[0].revents = 0;
		pf[1].fd = in.fd;
		pf[1].events = POLLIN;
		pf[1].revents = 0;
		if (poll(pf, 2, timeout) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pf[1].revents & POLLIN)
			pump(&in);
	}
	xcb_disconnect(conn);
	close(ffd);
	if (made)
		unlink(fifo);
	return 0;
}
