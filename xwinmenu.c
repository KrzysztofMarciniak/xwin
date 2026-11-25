/*
 * xwinmenu - a simple dmenu-style program launcher (C89).
 *
 * Lists every executable found in $PATH in a box in the centre of the
 * screen. Type to filter, Enter to launch, Escape to quit.
 *
 *   Up / Down, Tab, Ctrl-p / Ctrl-n   move the selection
 *   Enter                             run the selected entry (or, if nothing
 *                                     matches, the text you typed)
 *   Backspace, Ctrl-u                 edit the query
 *   Escape, Ctrl-c                    quit
 *
 * The filtering is done by SEARCH_TOOL, called as
 *     SEARCH_TOOL -i -F -e QUERY FILE
 * so anything grep-compatible works.
 *
 * The window is override-redirect: xwinrouter does not manage or tile it.
 * Bind it in config.h:  { MOD, XK_d, "spawn xwinmenu" },
 */

/* ---------------------------------------------------------------- config --
 * Edit and recompile.
 */
#define SEARCH_TOOL "grep"      /* grep-compatible filter, found in $PATH */
#define FONT        "fixed"     /* core X font (xlsfonts) */
#define BG          0x000000    /* background RRGGBB */
#define FG          0xffffff    /* text RRGGBB */
#define WIDTH       500         /* window width, pixels */
#define LINES       10          /* visible entries below the input line */
#define PAD         4           /* space around the text, pixels */
#define BORDER      2           /* border width, pixels */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <X11/keysym.h>

#define MAXQ 256

static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t gc_norm, gc_sel;
static int ascent, descent, cw, row_h, win_w, win_h, maxchars;

static char **all;                 /* every executable name, sorted */
static int nall;
static char **match;               /* what is shown */
static int nmatch;
static char *matchbuf;             /* owns the strings behind match[] */
static char query[MAXQ];
static int qlen, sel, top;
static char listfile[64];

static int cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* collect executables from every directory in $PATH */
static void scan_path(void)
{
	char *path, *copy, *dir, *save = NULL, full[4096];
	struct dirent *de;
	struct stat sb;
	DIR *d;
	int cap = 1024, i, j;

	path = getenv("PATH");
	if (!path || !*path)
		path = "/usr/local/bin:/usr/bin:/bin";
	copy = strdup(path);
	all = malloc(cap * sizeof *all);
	if (!copy || !all)
		exit(1);
	for (dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		d = opendir(dir);
		if (!d)
			continue;
		while ((de = readdir(d)) != NULL) {
			if (de->d_name[0] == '.')
				continue;
			if (strlen(dir) + strlen(de->d_name) + 2 > sizeof full)
				continue;
			sprintf(full, "%s/%s", dir, de->d_name);
			if (stat(full, &sb) < 0 || !S_ISREG(sb.st_mode) ||
			    access(full, X_OK) < 0)
				continue;
			if (nall == cap) {
				cap *= 2;
				all = realloc(all, cap * sizeof *all);
				if (!all)
					exit(1);
			}
			all[nall++] = strdup(de->d_name);
		}
		closedir(d);
	}
	free(copy);
	qsort(all, nall, sizeof *all, cmp);
	for (i = j = 0; i < nall; i++)          /* drop duplicates */
		if (j == 0 || strcmp(all[j - 1], all[i]) != 0)
			all[j++] = all[i];
	nall = j;
}

/* the list goes to a temp file once; the search tool reads it every time */
static void write_list(void)
{
	FILE *f;
	int fd, i;

	strcpy(listfile, "/tmp/xwinmenu.XXXXXX");
	fd = mkstemp(listfile);
	if (fd < 0)
		exit(1);
	f = fdopen(fd, "w");
	if (!f)
		exit(1);
	for (i = 0; i < nall; i++)
		fprintf(f, "%s\n", all[i]);
	fclose(f);
}

static void set_matches(char *buf, int len)
{
	char *p;
	int n = 0, i;

	free(matchbuf);
	free(match);
	matchbuf = buf;
	for (i = 0; i < len; i++)
		if (buf[i] == '\n')
			n++;
	match = malloc((n + 1) * sizeof *match);
	if (!match)
		exit(1);
	nmatch = 0;
	for (p = buf; p < buf + len; ) {
		char *nl = memchr(p, '\n', buf + len - p);

		if (!nl)
			break;
		*nl = 0;
		match[nmatch++] = p;
		p = nl + 1;
	}
}

/* run SEARCH_TOOL on the list, read its output */
static void filter(void)
{
	int fd[2], cap = 4096, len = 0, n;
	char *buf;
	pid_t pid;

	sel = top = 0;
	if (qlen == 0) {                        /* nothing typed: everything */
		FILE *f = fopen(listfile, "r");

		buf = malloc(1);
		cap = 1;
		len = 0;
		if (f) {
			char tmp[4096];

			while ((n = (int)fread(tmp, 1, sizeof tmp, f)) > 0) {
				buf = realloc(buf, len + n + 1);
				memcpy(buf + len, tmp, n);
				len += n;
			}
			fclose(f);
		}
		(void)cap;
		set_matches(buf, len);
		return;
	}
	if (pipe(fd) < 0)
		return;
	pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return;
	}
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], 1);
		close(fd[1]);
		execlp(SEARCH_TOOL, SEARCH_TOOL, "-i", "-F", "-e", query,
		    listfile, (char *)NULL);
		_exit(127);
	}
	close(fd[1]);
	buf = malloc(cap);
	if (!buf)
		exit(1);
	while ((n = (int)read(fd[0], buf + len, cap - len)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		len += n;
		if (len == cap) {
			cap *= 2;
			buf = realloc(buf, cap);
			if (!buf)
				exit(1);
		}
	}
	close(fd[0]);
	waitpid(pid, NULL, 0);
	set_matches(buf, len);
}

static void text(xcb_gcontext_t gc, int row, const char *s, int n)
{
	if (n > maxchars)
		n = maxchars;
	if (n < 0)
		n = 0;
	xcb_image_text_8(conn, n, win, gc, PAD,
	    row * row_h + PAD + ascent, s);
}

static void draw(void)
{
	char line[MAXQ + 4], blank[512];
	int i, n, w = maxchars;

	if (w > (int)sizeof blank - 1)
		w = (int)sizeof blank - 1;
	memset(blank, ' ', w);
	blank[w] = 0;

	sprintf(line, "> %s_", query);          /* the input line */
	xcb_image_text_8(conn, w, win, gc_norm, PAD, PAD + ascent, blank);
	text(gc_norm, 0, line, (int)strlen(line));

	for (i = 0; i < LINES; i++) {
		xcb_gcontext_t gc = (top + i == sel) ? gc_sel : gc_norm;

		xcb_image_text_8(conn, w, win, gc, PAD,
		    (i + 1) * row_h + PAD + ascent, blank);
		if (top + i < nmatch) {
			n = (int)strlen(match[top + i]);
			text(gc, i + 1, match[top + i], n);
		}
	}
	xcb_flush(conn);
}

static void launch(const char *cmd)
{
	pid_t pid;

	if (!cmd || !*cmd)
		return;
	pid = fork();
	if (pid == 0) {
		setsid();
		execlp("sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

static void cleanup(void)
{
	unlink(listfile);
}

static void onsig(int s)
{
	(void)s;
	cleanup();
	_exit(1);
}

static void move(int d)
{
	sel += d;
	if (sel < 0)
		sel = 0;
	if (sel >= nmatch)
		sel = nmatch - 1;
	if (sel < 0)
		sel = 0;
	if (sel < top)
		top = sel;
	if (sel >= top + LINES)
		top = sel - LINES + 1;
}

/* returns 1 when the menu should close */
static int key(xcb_keysym_t sym, unsigned int state)
{
	if ((state & XCB_MOD_MASK_CONTROL) && (sym == XK_c || sym == XK_g))
		return 1;
	if ((state & XCB_MOD_MASK_CONTROL) && sym == XK_u) {
		qlen = query[0] = 0;
		filter();
		return 0;
	}
	if ((state & XCB_MOD_MASK_CONTROL) && sym == XK_n) {
		move(1);
		return 0;
	}
	if ((state & XCB_MOD_MASK_CONTROL) && sym == XK_p) {
		move(-1);
		return 0;
	}
	switch (sym) {
	case XK_Escape:
		return 1;
	case XK_Return:
	case XK_KP_Enter:
		if (nmatch > 0)
			launch(match[sel]);
		else
			launch(query);
		return 1;
	case XK_Down:
	case XK_Tab:
		move(1);
		return 0;
	case XK_Up:
		move(-1);
		return 0;
	case XK_BackSpace:
		if (qlen > 0) {
			query[--qlen] = 0;
			filter();
		}
		return 0;
	}
	if (sym >= 0x20 && sym <= 0x7e && qlen < MAXQ - 1) {
		query[qlen++] = (char)sym;
		query[qlen] = 0;
		filter();
	}
	return 0;
}

int main(void)
{
	xcb_screen_iterator_t it;
	xcb_query_font_reply_t *fr;
	xcb_get_keyboard_mapping_reply_t *km;
	xcb_grab_keyboard_reply_t *gr;
	xcb_generic_event_t *ev;
	xcb_key_press_event_t *e;
	xcb_keysym_t *syms, sym;
	xcb_font_t font;
	struct timespec ts;
	uint32_t wv[4], gv[3];
	int screen = 0, i, per, min, max, col, done = 0, grabbed = 0;
	const char *fontname = FONT;

	conn = xcb_connect(NULL, &screen);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "xwinmenu: cannot open display\n");
		return 1;
	}
	it = xcb_setup_roots_iterator(xcb_get_setup(conn));
	for (i = 0; i < screen; i++)
		xcb_screen_next(&it);
	scr = it.data;

	scan_path();
	write_list();
	signal(SIGINT, onsig);
	signal(SIGTERM, onsig);
	signal(SIGCHLD, SIG_IGN);

	font = xcb_generate_id(conn);
	xcb_open_font(conn, font, strlen(fontname), fontname);
	fr = xcb_query_font_reply(conn, xcb_query_font(conn, font), NULL);
	if (!fr) {
		fprintf(stderr, "xwinmenu: cannot open font \"%s\"\n", fontname);
		cleanup();
		return 1;
	}
	ascent = fr->font_ascent;
	descent = fr->font_descent;
	cw = fr->max_bounds.character_width;
	if (cw < 1)
		cw = 1;
	free(fr);
	row_h = ascent + descent + PAD;
	win_w = WIDTH;
	win_h = (LINES + 1) * row_h + PAD;
	maxchars = (win_w - 2 * PAD) / cw;

	win = xcb_generate_id(conn);
	wv[0] = BG;
	wv[1] = FG;
	wv[2] = 1;                              /* override-redirect */
	wv[3] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
	    ((int)scr->width_in_pixels - win_w) / 2 - BORDER,
	    ((int)scr->height_in_pixels - win_h) / 2 - BORDER,
	    win_w, win_h, BORDER, XCB_WINDOW_CLASS_INPUT_OUTPUT,
	    scr->root_visual,
	    XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
	    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, wv);

	gc_norm = xcb_generate_id(conn);
	gv[0] = FG;
	gv[1] = BG;
	gv[2] = font;
	xcb_create_gc(conn, gc_norm, win,
	    XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gv);
	gc_sel = xcb_generate_id(conn);
	gv[0] = BG;
	gv[1] = FG;
	xcb_create_gc(conn, gc_sel, win,
	    XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gv);

	xcb_map_window(conn, win);
	xcb_flush(conn);

	/* take the keyboard; retry briefly, the launching key may be held */
	ts.tv_sec = 0;
	ts.tv_nsec = 10 * 1000 * 1000;
	for (i = 0; i < 100 && !grabbed; i++) {
		gr = xcb_grab_keyboard_reply(conn, xcb_grab_keyboard(conn, 1, win,
		    XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), NULL);
		if (gr && gr->status == XCB_GRAB_STATUS_SUCCESS)
			grabbed = 1;
		free(gr);
		if (!grabbed)
			nanosleep(&ts, NULL);
	}
	if (!grabbed) {
		fprintf(stderr, "xwinmenu: cannot grab keyboard\n");
		cleanup();
		return 1;
	}

	min = xcb_get_setup(conn)->min_keycode;
	max = xcb_get_setup(conn)->max_keycode;
	km = xcb_get_keyboard_mapping_reply(conn,
	    xcb_get_keyboard_mapping(conn, min, max - min + 1), NULL);
	if (!km) {
		cleanup();
		return 1;
	}
	syms = xcb_get_keyboard_mapping_keysyms(km);
	per = km->keysyms_per_keycode;

	filter();
	draw();

	while (!done && (ev = xcb_wait_for_event(conn)) != NULL) {
		switch (ev->response_type & ~0x80) {
		case XCB_EXPOSE:
			draw();
			break;
		case XCB_KEY_PRESS:
			e = (xcb_key_press_event_t *)ev;
			col = (e->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
			sym = syms[(e->detail - min) * per + (col < per ? col : 0)];
			if (!sym)
				sym = syms[(e->detail - min) * per];
			if ((e->state & XCB_MOD_MASK_LOCK) && sym >= 'a' && sym <= 'z')
				sym -= 32;
			else if ((e->state & XCB_MOD_MASK_LOCK) && sym >= 'A' && sym <= 'Z')
				sym += 32;
			done = key(sym, e->state);
			if (!done)
				draw();
			break;
		}
		free(ev);
	}
	xcb_ungrab_keyboard(conn, XCB_CURRENT_TIME);
	xcb_disconnect(conn);
	cleanup();
	return 0;
}
