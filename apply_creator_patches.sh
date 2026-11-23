#!/bin/sh
# apply_creator_patches.sh - creator's settings: tile only, black/white, minimal keys.
# Run from the xwin source directory. Then: make xwinkeys, restart xwinkeys.
set -e
patch -p1 <<'PATCH'
--- a/config.h	2026-11-23 12:27:06.345135687 +0000
+++ b/config.h	2026-11-23 12:27:06.392062512 +0000
@@ -14,14 +14,14 @@
 static const char *settings[] = {
 	/* --- xwinrouter --- */
 	"set border 2",                 /* pixels */
-	"set color_focus 5e81ac",       /* RRGGBB */
-	"set color_normal 3b4252",
+	"set color_focus ffffff",       /* RRGGBB */
+	"set color_normal 000000",
 	"set floating 1",               /* 0: every window is tiled, "float" does nothing */
 	"set fullscreen 1",             /* 0: ignore fullscreen requests and command */
 	"set tags 9",                   /* 1..9; 1 switches tagging off */
 
 	/* --- xwincomposer --- */
-	"set layouts tile bstack spiral grid mono",   /* the ones "layout next" cycles; drop names to disable */
+	"set layouts tile",             /* the ones "layout next" cycles; drop names to disable */
 	"set layout tile",              /* layout to start with */
 	"set ratio 55",                 /* master area in percent */
 	"set master 1",                 /* windows in the master area */
@@ -41,26 +41,15 @@
 
 /* modifier mask, unshifted keysym (XK_*), command line */
 static const struct key keys[] = {
-	{ MOD | SHIFT, XK_Return, "spawn xterm" },
-	{ MOD,         XK_d,      "spawn dmenu_run -m '0' -nb '#000000' -nf '#ffffff' "
-	                          "-sb '#ffffff' -sf '#000000'" },
-	{ MOD,         XK_Return, "zoom" },
+	{ MOD,         XK_Return, "spawn xxt" },
 	{ MOD,         XK_q,      "close" },
 	{ MOD | SHIFT, XK_q,      "quit" },
-	{ MOD,         XK_j,      "focus next" },
-	{ MOD,         XK_k,      "focus prev" },
-	{ MOD,         XK_h,      "ratio -5" },
-	{ MOD,         XK_l,      "ratio +5" },
-	{ MOD,         XK_comma,  "master +1" },
-	{ MOD,         XK_period, "master -1" },
-	{ MOD,         XK_space,  "layout next" },
-	{ MOD,         XK_t,      "layout tile" },
-	{ MOD,         XK_b,      "layout bstack" },
-	{ MOD,         XK_s,      "layout spiral" },
-	{ MOD,         XK_g,      "layout grid" },
-	{ MOD,         XK_m,      "layout mono" },
-	{ MOD,         XK_f,      "fullscreen @" },
-	{ MOD | SHIFT, XK_space,  "float @" },
+	{ MOD,         XK_j,      "focus prev" },
+	{ MOD,         XK_k,      "focus next" },
+	{ MOD | SHIFT, XK_l,      "ratio +5" },
+	{ MOD | SHIFT, XK_h,      "ratio -5" },
+	{ MOD,         XK_p,      "spawn dmenu_run -nb '#000000' -nf '#ffffff' "
+	                          "-sb '#ffffff' -sf '#000000'" },
 	TAGKEY(XK_1, 1)
 	TAGKEY(XK_2, 2)
 	TAGKEY(XK_3, 3)
PATCH
echo "creator patch applied: run make xwinkeys and restart xwinkeys"
