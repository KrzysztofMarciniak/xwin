/*
 * config.h - edit, then recompile (make). Used by all three programs:
 * xwinrouter (looks, features), xwincomposer (layouts, focus) and
 * xwinkeys (key bindings).
 *
 * Every key sends one command line to the router's FIFO. Commands the router
 * knows are executed there; anything else (zoom, layout, ratio, master, gap)
 * is forwarded on to xwincomposer. "@" means the focused window.
 */

/* ------------------------------------------------------------------ */
/* Features. Set to 0 to switch a feature off completely.             */
/* ------------------------------------------------------------------ */

/* 0: every window is tiled (dialogs too); "float" does nothing */
#define ENABLE_FLOATING     1

/* 0: ignore fullscreen requests from programs and the "fullscreen" command */
#define ENABLE_FULLSCREEN   1

/* Number of tags, 1..9. 1 switches tagging off: "view" and "tag" do nothing
 * and no tag keys are bound. */
#define NTAGS               9

/* ------------------------------------------------------------------ */
/* Layouts (xwincomposer). Disabled layouts are not compiled in.      */
/* At least one must stay on. "layout next" cycles through the ones   */
/* that are enabled, in this order.                                   */
/* ------------------------------------------------------------------ */

#define LAYOUT_TILE     1   /* master column on the left, stack on the right */
#define LAYOUT_BSTACK   1   /* master row on top, stack row below */
#define LAYOUT_SPIRAL   1   /* each window takes a share of what is left */
#define LAYOUT_GRID     1   /* equal cells */
#define LAYOUT_MONO     1   /* one window at a time, full size */

#define DEFAULT_LAYOUT  "tile"   /* tile, bstack, spiral, grid or mono */
#define MASTER_RATIO    55       /* percent of the screen for the master area */
#define NMASTER         1        /* windows in the master area */
#define GAP             0        /* pixels around every window */

/* ------------------------------------------------------------------ */
/* Focus policy (xwincomposer)                                        */
/* ------------------------------------------------------------------ */

#define FOCUS_FOLLOWS_MOUSE 1    /* focus the window under the pointer */
#define FOCUS_NEW_WINDOW    1    /* new windows take focus */

/* ------------------------------------------------------------------ */
/* Sanity checks                                                      */
/* ------------------------------------------------------------------ */

#if NTAGS < 1 || NTAGS > 9
#error "NTAGS must be between 1 and 9"
#endif
#if !LAYOUT_TILE && !LAYOUT_BSTACK && !LAYOUT_SPIRAL && !LAYOUT_GRID && !LAYOUT_MONO
#error "enable at least one layout"
#endif

/* ------------------------------------------------------------------ */
/* Looks (xwinrouter only)                                            */
/* ------------------------------------------------------------------ */

#ifdef XWR_ROUTER

#define BORDER     2
#define COL_FOCUS  0x5e81ac
#define COL_NORMAL 0x3b4252

#endif /* XWR_ROUTER */

/* ------------------------------------------------------------------ */
/* Keys (xwinkeys only). A binding is a modifier mask, an unshifted   */
/* keysym (XK_*) and the command line to send.                        */
/* ------------------------------------------------------------------ */

#ifdef XWR_KEYS

#define MOD   XCB_MOD_MASK_4        /* super */
#define SHIFT XCB_MOD_MASK_SHIFT

#define TAGKEY(k, n) \
	{ MOD,         k, "view " #n }, \
	{ MOD | SHIFT, k, "tag @ " #n },

static const struct key keys[] = {
	{ MOD | SHIFT, XK_Return, "spawn xterm" },
	{ MOD,         XK_d,      "spawn dmenu_run -m '0' -nb '#000000' -nf '#ffffff' "
	                          "-sb '#ffffff' -sf '#000000'" },
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
#if LAYOUT_TILE
	{ MOD,         XK_t,      "layout tile" },
#endif
#if LAYOUT_BSTACK
	{ MOD,         XK_b,      "layout bstack" },
#endif
#if LAYOUT_SPIRAL
	{ MOD,         XK_s,      "layout spiral" },
#endif
#if LAYOUT_GRID
	{ MOD,         XK_g,      "layout grid" },
#endif
#if LAYOUT_MONO
	{ MOD,         XK_m,      "layout mono" },
#endif
#if ENABLE_FULLSCREEN
	{ MOD,         XK_f,      "fullscreen @" },
#endif
#if ENABLE_FLOATING
	{ MOD | SHIFT, XK_space,  "float @" },
#endif
#if NTAGS >= 2
	TAGKEY(XK_1, 1)
	TAGKEY(XK_2, 2)
#endif
#if NTAGS >= 3
	TAGKEY(XK_3, 3)
#endif
#if NTAGS >= 4
	TAGKEY(XK_4, 4)
#endif
#if NTAGS >= 5
	TAGKEY(XK_5, 5)
#endif
#if NTAGS >= 6
	TAGKEY(XK_6, 6)
#endif
#if NTAGS >= 7
	TAGKEY(XK_7, 7)
#endif
#if NTAGS >= 8
	TAGKEY(XK_8, 8)
#endif
#if NTAGS >= 9
	TAGKEY(XK_9, 9)
#endif
};

#endif /* XWR_KEYS */
