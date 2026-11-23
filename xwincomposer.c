/*
 * xwincomposer - layout and focus policy for xwinrouter (C89).
 * stdin : router events    stdout: router commands
 *
 * Wire it up with a fifo:  xwinrouter < fifo | xwincomposer > fifo
 *
 * usage: xwincomposer [-l layout] [-r master%] [-n nmaster] [-g gap]
 *
 * It has no config file. Defaults are below; xwinkeys sends the real
 * settings, which arrive here as "bind set ..." lines:
 *   set layouts NAME...      layouts that "layout next" cycles through
 *   set layout NAME          current layout
 *   set ratio N              master area in percent (10..90)
 *   set master N             windows in the master area
 *   set gap N                pixels around every window
 *   set focus_follows_mouse 0|1
 *   set focus_new_window 0|1
 * and key commands:
 *   layout NAME|next   ratio +N|-N   master +N|-N   gap +N|-N   zoom
 *
 * Layouts: tile, bstack, spiral, grid, mono.
 */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXW 256
#define NLAYOUTS 5

enum { L_TILE, L_BSTACK, L_SPIRAL, L_GRID, L_MONO };

static const char *lname[NLAYOUTS] = { "tile", "bstack", "spiral", "grid", "mono" };

struct win {
	unsigned long id;
	int tag, floating, fullscreen;
};

static struct win ws[MAXW];
static int nw, view = 1, ready;
static int enabled[NLAYOUTS] = { 1, 1, 1, 1, 1 };   /* which layouts may be used */
static int mode = L_TILE;                            /* current layout */
static int gap = 0, ratio = 55, nmaster = 1;
static int follow_mouse = 1, focus_new = 1;
static int wx, wy, ww, wh;
static unsigned long focus;

static int find(unsigned long id)
{
	int i;

	for (i = 0; i < nw; i++)
		if (ws[i].id == id)
			return i;
	return -1;
}

static int layout_index(const char *name)
{
	int i;

	for (i = 0; i < NLAYOUTS; i++)
		if (!strcmp(lname[i], name))
			return i;
	return -1;
}

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void put(unsigned long id, int x, int y, int w, int h)
{
	printf("geom 0x%lx %d %d %d %d\n", id, x + gap, y + gap, w - 2 * gap, h - 2 * gap);
}

/* ---------- layouts: idx[0..n-1] are indexes into ws[] ---------- */

static void lay_tile(const int *idx, int n)
{
	int i, m = nmaster < n ? nmaster : n, s = n - m;
	int mw = (m > 0 && s > 0) ? ww * ratio / 100 : ww;
	int sx = m > 0 ? wx + mw : wx, sw = m > 0 ? ww - mw : ww;
	int y0, y1;

	for (i = 0; i < m; i++) {
		y0 = wy + i * wh / m;
		y1 = wy + (i + 1) * wh / m;
		put(ws[idx[i]].id, wx, y0, mw, y1 - y0);
	}
	for (i = 0; i < s; i++) {
		y0 = wy + i * wh / s;
		y1 = wy + (i + 1) * wh / s;
		put(ws[idx[m + i]].id, sx, y0, sw, y1 - y0);
	}
}

static void lay_bstack(const int *idx, int n)
{
	int i, m = nmaster < n ? nmaster : n, s = n - m;
	int mh = (m > 0 && s > 0) ? wh * ratio / 100 : wh;
	int sy = m > 0 ? wy + mh : wy, sh = m > 0 ? wh - mh : wh;
	int x0, x1;

	for (i = 0; i < m; i++) {
		x0 = wx + i * ww / m;
		x1 = wx + (i + 1) * ww / m;
		put(ws[idx[i]].id, x0, wy, x1 - x0, mh);
	}
	for (i = 0; i < s; i++) {
		x0 = wx + i * ww / s;
		x1 = wx + (i + 1) * ww / s;
		put(ws[idx[m + i]].id, x0, sy, x1 - x0, sh);
	}
}

static void lay_spiral(const int *idx, int n)
{
	int i, x = wx, y = wy, w = ww, h = wh, d;

	for (i = 0; i < n; i++) {
		if (i == n - 1) {
			put(ws[idx[i]].id, x, y, w, h);
		} else if (i % 2 == 0) {            /* split left | right */
			d = w * ratio / 100;
			put(ws[idx[i]].id, x, y, d, h);
			x += d;
			w -= d;
		} else {                            /* split top / bottom */
			d = h * ratio / 100;
			put(ws[idx[i]].id, x, y, w, d);
			y += d;
			h -= d;
		}
	}
}

static void lay_grid(const int *idx, int n)
{
	int i, cols = 1, rows, cx, cy, x0, x1, y0, y1;

	while (cols * cols < n)
		cols++;
	rows = (n + cols - 1) / cols;
	for (i = 0; i < n; i++) {
		cx = i % cols;
		cy = i / cols;
		x0 = wx + cx * ww / cols;
		x1 = wx + (cx + 1) * ww / cols;
		y0 = wy + cy * wh / rows;
		y1 = wy + (cy + 1) * wh / rows;
		put(ws[idx[i]].id, x0, y0, x1 - x0, y1 - y0);
	}
}

static void lay_mono(const int *idx, int n)
{
	int i, found = 0;

	for (i = 0; i < n; i++) {
		put(ws[idx[i]].id, wx, wy, ww, wh);
		if (ws[idx[i]].id == focus)
			found = 1;
	}
	if (found)
		printf("raise 0x%lx\n", focus);
}

static void relayout(void)
{
	int idx[MAXW], n = 0, i;

	if (!ready || ww <= 0)
		return;
	for (i = 0; i < nw; i++)
		if (ws[i].tag == view && !ws[i].floating && !ws[i].fullscreen)
			idx[n++] = i;
	if (!n)
		return;
	switch (mode) {
	case L_BSTACK: lay_bstack(idx, n); break;
	case L_SPIRAL: lay_spiral(idx, n); break;
	case L_GRID:   lay_grid(idx, n);   break;
	case L_MONO:   lay_mono(idx, n);   break;
	default:       lay_tile(idx, n);   break;
	}
}

/* ---------- focus and input ---------- */

static void pick(void)
{
	int i;

	for (i = 0; i < nw; i++)
		if (ws[i].tag == view) {
			printf("focus 0x%lx\n", ws[i].id);
			return;
		}
}

static void zoom(void)
{
	struct win tmp;
	int i = find(focus);

	if (i > 0) {
		tmp = ws[i];
		memmove(ws + 1, ws, i * sizeof ws[0]);
		ws[0] = tmp;
	}
}

/* switch to layout k if it is enabled; returns 1 if it changed anything */
static int use_layout(int k)
{
	if (k < 0 || !enabled[k])
		return 0;
	mode = k;
	return 1;
}

static int next_layout(void)
{
	int i, k;

	for (i = 1; i <= NLAYOUTS; i++) {
		k = (mode + i) % NLAYOUTS;
		if (enabled[k]) {
			mode = k;
			return 1;
		}
	}
	return 0;
}

/* "set KEY VALUE..." (the text after "set ") */
static int setting(char *l)
{
	char *k = strtok(l, " \t"), *v;
	int e[NLAYOUTS], i, any = 0, x;

	if (!k)
		return 0;
	if (!strcmp(k, "layouts")) {
		for (i = 0; i < NLAYOUTS; i++)
			e[i] = 0;
		while ((v = strtok(NULL, " \t")) != NULL)
			if ((x = layout_index(v)) >= 0) {
				e[x] = 1;
				any = 1;
			}
		if (!any)
			return 0;
		for (i = 0; i < NLAYOUTS; i++)
			enabled[i] = e[i];
		if (!enabled[mode])
			next_layout();
		return 1;
	}
	if ((v = strtok(NULL, " \t")) == NULL)
		return 0;
	if (!strcmp(k, "layout"))
		return use_layout(layout_index(v));
	if (!strcmp(k, "ratio"))
		ratio = clamp(atoi(v), 10, 90);
	else if (!strcmp(k, "master"))
		nmaster = clamp(atoi(v), 0, 50);
	else if (!strcmp(k, "gap"))
		gap = clamp(atoi(v), 0, 200);
	else if (!strcmp(k, "focus_follows_mouse"))
		follow_mouse = !!atoi(v);
	else if (!strcmp(k, "focus_new_window"))
		focus_new = !!atoi(v);
	else
		return 0;
	return 1;
}

static void bind(char *l)
{
	char s[32];
	int a;

	if (!strncmp(l, "set ", 4)) {
		if (!setting(l + 4))
			return;
	} else if (sscanf(l, "layout %31s", s) == 1) {
		if (!strcmp(s, "next")) {
			if (!next_layout())
				return;
		} else if (!use_layout(layout_index(s))) {
			return;                       /* unknown or disabled */
		}
	} else if (sscanf(l, "ratio %d", &a) == 1) {
		ratio = clamp(ratio + a, 10, 90);
	} else if (sscanf(l, "master %d", &a) == 1) {
		nmaster = clamp(nmaster + a, 0, 50);
	} else if (sscanf(l, "gap %d", &a) == 1) {
		gap = clamp(gap + a, 0, 200);
	} else if (!strncmp(l, "zoom", 4)) {
		zoom();
	} else {
		return;                           /* unknown: ignore, never loop */
	}
	relayout();
}

static void handle(char *l)
{
	unsigned long id;
	int a, b, c, d, i;

	if (sscanf(l, "map %lx %d %d", &id, &a, &b) == 3) {
		i = find(id);
		if (i < 0 && nw < MAXW)
			i = nw++;
		if (i < 0)
			return;
		ws[i].id = id;
		ws[i].tag = a;
		ws[i].floating = b;
		ws[i].fullscreen = 0;
		relayout();
		if (focus_new && ready && a == view)
			printf("focus 0x%lx\n", id);
	} else if (sscanf(l, "unmap %lx", &id) == 1) {
		i = find(id);
		if (i >= 0) {
			memmove(ws + i, ws + i + 1, (nw - i - 1) * sizeof ws[0]);
			nw--;
		}
		relayout();
	} else if (!strncmp(l, "focus none", 10)) {
		focus = 0;
		ready = 1;
		relayout();
		pick();
	} else if (sscanf(l, "focus %lx", &id) == 1) {
		focus = id;
		if (!ready) {
			ready = 1;
			relayout();
		}
	} else if (sscanf(l, "tag %lx %d", &id, &a) == 2) {
		if ((i = find(id)) >= 0)
			ws[i].tag = a;
		relayout();
	} else if (sscanf(l, "view %d", &a) == 1) {
		view = a;
		relayout();
	} else if (sscanf(l, "float %lx %d", &id, &a) == 2) {
		if ((i = find(id)) >= 0)
			ws[i].floating = a;
		relayout();
	} else if (sscanf(l, "fullscreen %lx %d", &id, &a) == 2) {
		if ((i = find(id)) >= 0)
			ws[i].fullscreen = a;
		relayout();
	} else if (sscanf(l, "workarea %d %d %d %d", &a, &b, &c, &d) == 4) {
		wx = a; wy = b; ww = c; wh = d;
		relayout();
	} else if (sscanf(l, "enter %lx", &id) == 1) {
		if (follow_mouse && id != focus)
			printf("focus 0x%lx\n", id);
	} else if (sscanf(l, "activate %lx", &id) == 1) {
		if ((i = find(id)) >= 0) {
			if (ws[i].tag != view)
				printf("view %d\n", ws[i].tag);
			printf("focus 0x%lx\n", id);
		}
	} else if (sscanf(l, "desktop %d", &a) == 1) {
		printf("view %d\n", a);
	} else if (!strncmp(l, "bind ", 5)) {
		bind(l + 5);
	}
}

int main(int argc, char **argv)
{
	char line[512], *nl;
	int o;

	while ((o = getopt(argc, argv, "l:r:n:g:")) != -1) {
		switch (o) {
		case 'l': use_layout(layout_index(optarg)); break;
		case 'r': ratio = clamp(atoi(optarg), 10, 90); break;
		case 'n': nmaster = clamp(atoi(optarg), 0, 50); break;
		case 'g': gap = clamp(atoi(optarg), 0, 200); break;
		default:
			fprintf(stderr, "usage: xwincomposer [-l layout] [-r pct] [-n nmaster] [-g gap]\n");
			return 2;
		}
	}
	printf("dump\n");                 /* learn the router's current state */
	fflush(stdout);
	while (fgets(line, sizeof line, stdin)) {
		if ((nl = strpbrk(line, "\r\n")) != NULL)
			*nl = 0;
		handle(line);
		fflush(stdout);
	}
	return 0;
}
