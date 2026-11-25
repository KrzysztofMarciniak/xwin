/*
 * config.h - the one configuration file. Only xwinkeys includes it.
 *
 * At startup xwinkeys sends every line of settings[] to the FIFO. The router
 * applies the ones it knows (border, colors, floating, fullscreen, tags) and
 * passes the rest on to xwincomposer, so nothing else needs recompiling and
 * nothing else reads a config file. Restart xwinkeys to apply a new config.
 *
 * Every key sends one command line to the FIFO. Commands the router knows are
 * executed there; anything else (zoom, layout, ratio, master, gap) is passed
 * on to xwincomposer. "@" means the focused window.
 */

static const char *settings[] = {
	/* --- xwinrouter --- */
	"set border 2",                 /* pixels */
	"set color_focus 5e81ac",       /* RRGGBB */
	"set color_normal 3b4252",
	"set floating 1",               /* 0: every window is tiled, "float" does nothing */
	"set fullscreen 1",             /* 0: ignore fullscreen requests and command */
	"set tags 9",                   /* 1..9; 1 switches tagging off */

	/* --- xwincomposer --- */
	"set layouts tile bstack spiral grid mono",   /* the ones "layout next" cycles; drop names to disable */
	"set layout tile",              /* layout to start with */
	"set ratio 55",                 /* master area in percent */
	"set master 1",                 /* windows in the master area */
	"set gap 0",                    /* pixels around every window */
	"set focus_follows_mouse 1",
	"set focus_new_window 1",
};

/* --- keys --- */

#define MOD   XCB_MOD_MASK_4        /* super */
#define SHIFT XCB_MOD_MASK_SHIFT

#define TAGKEY(k, n) \
	{ MOD,         k, "view " #n }, \
	{ MOD | SHIFT, k, "tag @ " #n },

/* modifier mask, unshifted keysym (XK_*), command line */
static const struct key keys[] = {
	{ MOD | SHIFT, XK_Return, "spawn xxt" },
	{ MOD,         XK_d,      "spawn xwinmenu"},
	{ MOD,         XK_Return, "zoom" },
	{ MOD,         XK_q,      "close" },
	{ MOD | SHIFT, XK_q,      "quit" },
	{ MOD,         XK_j,      "focus next" },
	{ MOD,         XK_k,      "focus prev" },
	{ MOD,         XK_h,      "ratio -5" },
	{ MOD,         XK_l,      "ratio +5" },
	{ MOD,         XK_comma,  "master +1" },
	{ MOD,         XK_period, "master -1" },
	{ MOD,         XK_space,  "layout next" },
	{ MOD,         XK_t,      "layout tile" },
	{ MOD,         XK_b,      "layout bstack" },
	{ MOD,         XK_s,      "layout spiral" },
	{ MOD,         XK_g,      "layout grid" },
	{ MOD,         XK_m,      "layout mono" },
	{ MOD,         XK_f,      "fullscreen @" },
	{ MOD | SHIFT, XK_space,  "float @" },
	TAGKEY(XK_1, 1)
	TAGKEY(XK_2, 2)
	TAGKEY(XK_3, 3)
	TAGKEY(XK_4, 4)
	TAGKEY(XK_5, 5)
	TAGKEY(XK_6, 6)
	TAGKEY(XK_7, 7)
	TAGKEY(XK_8, 8)
	TAGKEY(XK_9, 9)
};
