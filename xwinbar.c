/*
 * xwinbar - a simple status bar (C89).
 *
 * Runs COMMAND with sh -c and shows the last line it prints. COMMAND is
 * normally a shell script that loops: print one line, sleep, repeat (see
 * xwinbar.sh). The bar itself knows nothing about dates or batteries.
 *
 * usage: xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad] COMMAND
 *   -b  put the bar at the bottom (default: top)
 *   -a  text alignment: l, c or r (default r)
 *   -f  core X font name (default "fixed"; list them with xlsfonts)
 *   -B  background color, -F text color (hex, default 000000 / ffffff)
 *   -p  vertical padding in pixels (default 3)
 *
 * The bar is a dock window with _NET_WM_STRUT_PARTIAL, so xwinrouter keeps
 * tiled windows out of its way. Core X fonts are Latin-1 only.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define SIDE 8                    /* horizontal padding, pixels */

static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t gc;
static xcb_font_t font;
static int ascent, align = 'r';
static char text[256];
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
			x = ((int)scr->width_in_pixels - w) / 2;
		else
			x = (int)scr->width_in_pixels - w - SIDE;
		xcb_image_text_8(conn, n, win, gc, x, ascent + 3, text);
	}
	xcb_flush(conn);
}

static int start(const char *cmd, pid_t *pid)
{
	int fd[2];

	if (pipe(fd) < 0)
		return -1;
	*pid = fork();
	if (*pid < 0)
		return -1;
	if (*pid == 0) {
		close(fd[0]);
		dup2(fd[1], 1);
		close(fd[1]);
		close(xcb_get_file_descriptor(conn));
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	close(fd[1]);
	return fd[0];
}

static void onsig(int s)
{
	(void)s;
	quit = 1;
}

int main(int argc, char **argv)
{
	const char *fontname = "fixed", *cmd;
	unsigned long bg = 0x000000, fg = 0xffffff;
	int bottom = 0, pad = 3, bad = 0, o, fd, n, len = 0, screen = 0, bar_h, y, got;
	char buf[1024], *s, *nl, *p;
	uint32_t wv[2], gv[3], st[12], st4[4], type;
	xcb_query_font_reply_t *fr;
	xcb_void_cookie_t ck;
	xcb_generic_error_t *err;
	xcb_generic_event_t *ev;
	struct pollfd pf[2];
	struct sigaction sa;
	pid_t pid = 0;

	while ((o = getopt(argc, argv, "ba:f:B:F:p:")) != -1) {
		switch (o) {
		case 'b': bottom = 1; break;
		case 'a': align = optarg[0]; break;
		case 'f': fontname = optarg; break;
		case 'B': bg = strtoul(optarg, NULL, 16); break;
		case 'F': fg = strtoul(optarg, NULL, 16); break;
		case 'p': pad = atoi(optarg); break;
		default: bad = 1; break;
		}
		if (bad)
			break;
	}
	if (bad || optind >= argc) {
		fprintf(stderr, "usage: xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] "
		    "[-F RRGGBB] [-p pad] COMMAND\n");
		return 2;
	}
	cmd = argv[optind];

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onsig;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinbar: cannot open display\n");
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
		return 1;
	}
	fr = xcb_query_font_reply(conn, xcb_query_font(conn, font), NULL);
	if (!fr)
		return 1;
	ascent = fr->font_ascent;
	bar_h = fr->font_ascent + fr->font_descent + 2 * (pad < 0 ? 0 : pad);
	free(fr);

	y = bottom ? (int)scr->height_in_pixels - bar_h : 0;
	win = xcb_generate_id(conn);
	wv[0] = bg;
	wv[1] = XCB_EVENT_MASK_EXPOSURE;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root, 0, y,
	    scr->width_in_pixels, bar_h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
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
		st[3] = st4[3] = bar_h;
		st[10] = 0;
		st[11] = scr->width_in_pixels - 1;
	} else {
		st[2] = st4[2] = bar_h;
		st[8] = 0;
		st[9] = scr->width_in_pixels - 1;
	}
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, intern("_NET_WM_STRUT_PARTIAL"),
	    XCB_ATOM_CARDINAL, 32, 12, st);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, intern("_NET_WM_STRUT"),
	    XCB_ATOM_CARDINAL, 32, 4, st4);
	xcb_map_window(conn, win);
	xcb_flush(conn);

	fd = start(cmd, &pid);
	if (fd < 0) {
		fprintf(stderr, "xwinbar: cannot run \"%s\"\n", cmd);
		return 1;
	}

	while (!quit) {
		while ((ev = xcb_poll_for_event(conn)) != NULL) {
			if ((ev->response_type & ~0x80) == XCB_EXPOSE)
				draw();
			free(ev);
		}
		if (xcb_connection_has_error(conn))
			break;
		xcb_flush(conn);
		pf[0].fd = xcb_get_file_descriptor(conn);
		pf[0].events = POLLIN;
		pf[0].revents = 0;
		pf[1].fd = fd;                 /* -1 after the script exits: skipped */
		pf[1].events = POLLIN;
		pf[1].revents = 0;
		if (poll(pf, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (fd >= 0 && (pf[1].revents & (POLLIN | POLLHUP))) {
			n = (int)read(fd, buf + len, sizeof buf - 1 - len);
			if (n <= 0) {
				if (n < 0 && (errno == EINTR || errno == EAGAIN))
					continue;
				close(fd);
				fd = -1;               /* script ended; keep the last text */
				continue;
			}
			len += n;
			buf[len] = 0;
			got = 0;
			s = buf;
			while ((nl = strchr(s, '\n')) != NULL) {
				*nl = 0;
				strncpy(text, s, sizeof text - 1);
				text[sizeof text - 1] = 0;
				for (p = text; *p; p++)
					if ((unsigned char)*p < 32)
						*p = ' ';
				got = 1;
				s = nl + 1;
			}
			len = (int)strlen(s);
			memmove(buf, s, len + 1);
			if (len >= (int)sizeof buf - 1)
				len = 0;                /* overlong line: drop it */
			if (got)
				draw();
		}
	}
	if (pid > 0)
		kill(pid, SIGTERM);
	xcb_disconnect(conn);
	return 0;
}
