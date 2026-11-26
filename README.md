# xwin

Small, modular tiling window management for X11. Plain text between the parts, one job per program, C89, linked only against `libxcb`.

```
             key presses                          events (stdout)
 xwinkeys ──────────────────►  FIFO  ──►  xwinrouter ─────────────► xwincomposer
 (config.h)   command lines      ▲        (the WM)                  (layout, focus)
                                 │                                        │
                                 └────────────────────────────────────────┘
                                        geom / focus / raise commands
```

**Stuck? Press Super+F1.** `xwinhelp` shows every key binding and setting of your current `config.h`.

| Program | Job |
| --- | --- |
| `xwinrouter` | Window manager mechanism. The only program with the WM role. Manages windows, applies the geometry it is told to apply, reports what happens. No layout, no keys, no focus policy. |
| `xwincomposer` | Layout and focus policy. Reads router events, writes router commands. |
| `xwinkeys` | Key bindings and **the only configuration**. Sends its settings at startup, then turns key presses into commands. |
| `xwinbar` | Status bar. Reads lines from a FIFO: status lines and `!notifications`. |
| `xwinmenu` | Program launcher, a dmenu alternative. Lists the executables in `$PATH`. |
| `xwinhelp` | Help viewer. Builds the key and settings list from `config.h`, writes `help.txt` and shows it in a window. |

Everything they say to each other is one line of plain text. Anything that can write a line to a FIFO can control the window manager: `echo "view 2" > "$fifo"`.

Looking for a terminal? See [xxt](https://github.com/KrzysztofMarciniak/xxt), a terminal emulator in about 400 lines of C with patch support (vi mode, libvterm and more).

## Contents

- [Quick start](#quick-start)
- [How it works](#how-it-works)
- [Configuration](#configuration)
- [Default keys](#default-keys)
- [Floating windows](#floating-windows)
- [xwinbar and xwinnotify](#xwinbar-and-xwinnotify)
- [xwinmenu](#xwinmenu)
- [xwinhelp](#xwinhelp)
- [Talking to the router](#talking-to-the-router)
- [Writing your own parts](#writing-your-own-parts)
- [Troubleshooting](#troubleshooting)

## Quick start

Requirements:

- C compiler (C89 is enough) and `pkg-config`
- `libxcb` and its headers (the only linked library)
- `X11/keysym.h` from xorgproto, for `xwinkeys` and `xwinhelp` (header only, nothing from libX11 is linked)
- a core X font for `xwinbar`, `xwinmenu` and `xwinhelp` (the built-in `fixed` works)
- `grep` for `xwinmenu`

```
make
make install            # PREFIX=/usr/local by default
cp xinitrc.example ~/.xinitrc
startx
```

Windows that are already open when the router starts are adopted. The bar can start before or after the router.

To try it first in a nested server:

```
mkfifo fifo
Xephyr :1 -screen 1280x800 &
export DISPLAY=:1
xwinkeys > fifo &
xwinrouter < fifo | xwincomposer > fifo &
xwinbar &
sleep 1; ./xwinbar.sh > "${XDG_RUNTIME_DIR:-/tmp}/xwinbar.fifo" &
xterm &
```

<details>
<summary>Building by hand, without make</summary>

```
cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) -o xwinrouter xwinrouter.c $(pkg-config --libs xcb)
cc -std=c89 -pedantic -Wall -Wextra -o xwincomposer xwincomposer.c
cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) -o xwinkeys xwinkeys.c $(pkg-config --libs xcb)
cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) -o xwinbar xwinbar.c $(pkg-config --libs xcb)
cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) -o xwinmenu xwinmenu.c $(pkg-config --libs xcb)
cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) -o xwinhelp xwinhelp.c $(pkg-config --libs xcb)

install -m 755 xwinrouter xwincomposer xwinkeys xwinbar xwinmenu xwinhelp /usr/local/bin/
install -d /usr/local/share/xwinrouter
install -m 755 xwinbar.sh /usr/local/share/xwinrouter/
install -m 755 xwinnotify.sh /usr/local/bin/xwinnotify
```

</details>

### `~/.xinitrc`

This is `xinitrc.example`:

```sh
#!/bin/sh
fifo=${XDG_RUNTIME_DIR:-/tmp}/xwr.fifo
rm -f "$fifo"
mkfifo -m 600 "$fifo"

xsetroot -cursor_name left_ptr &

# the bar's own FIFO: plain line = status, "!text" = notification
bfifo=${XDG_RUNTIME_DIR:-/tmp}/xwinbar.fifo
rm -f "$bfifo"
mkfifo -m 600 "$bfifo"
xwinbar "$bfifo" &
/usr/local/share/xwinrouter/xwinbar.sh > "$bfifo" &

# sends settings and key bindings from config.h
xwinkeys > "$fifo" &

# last command: the session ends when the router exits
xwinrouter < "$fifo" | xwincomposer > "$fifo"
```

`XDG_RUNTIME_DIR` is optional. The scripts use it for the FIFOs and fall back to `/tmp`; the programs never read it, except `xwinhelp`, which puts `help.txt` there.

## How it works

1. `xwinkeys` grabs the shortcut keys listed in `config.h`. Only those: all other typing goes straight to the focused window. At startup it writes its settings to the FIFO, then one command line (for example `focus next`) for every shortcut you press.
2. `xwinrouter` reads the FIFO. Commands it knows (`focus`, `close`, `tag`, `view`, `spawn`, `geom`, ...) it executes itself. Anything else it prints on stdout as a `bind` event. It also prints an event for everything that happens on screen: window opened, window closed, focus changed, usable area changed.
3. `xwincomposer` reads those events, decides where every window goes, and writes `geom`, `focus` and `raise` commands to the FIFO. The router reads them there and applies them.
4. `xwinbar` is independent. It reads lines from its own FIFO, shows the last status line, shows `!` lines as timed notifications, and tells the router to keep its strip of the screen free.

The loop is closed by the last line of `~/.xinitrc`: the router's stdout goes into the composer, and the composer's stdout goes back into the FIFO the router reads.

Only `xwinkeys` and `xwinhelp` include `config.h`. The router and the composer have no config file and no compile-time options: they are configured at runtime by `set` commands, so changing a setting never means rebuilding them.

## Configuration

`config.h` is the only configuration. It has two tables.

After editing it, rebuild **`xwinkeys`** (and `xwinhelp`, so the help matches) and restart `xwinkeys`.

### `settings[]`

Each line is sent to the FIFO when `xwinkeys` starts.

| Setting | For | Values |
| --- | --- | --- |
| `set border N` | router | border width in pixels |
| `set color_focus RRGGBB` | router | focused border color |
| `set color_normal RRGGBB` | router | other borders |
| `set floating 0\|1` | router | `0`: everything is tiled, dialogs too, `float` does nothing |
| `set fullscreen 0\|1` | router | `0`: fullscreen requests and the command are ignored |
| `set mouse_mod super\|alt\|ctrl\|off` | router | modifier for mouse drag: MOD+left button moves, MOD+right button resizes. `off` releases the buttons |
| `set tags N` | router | `1`..`9`. `1` turns tagging off: `view` and `tag` do nothing |
| `set layouts NAME...` | composer | layouts `layout next` cycles through; omit a name to disable it |
| `set layout NAME` | composer | starting layout |
| `set ratio N` | composer | master area in percent (10..90) |
| `set master N` | composer | windows in the master area |
| `set gap N` | composer | pixels around every window |
| `set focus_follows_mouse 0\|1` | composer | focus the window under the pointer |
| `set focus_new_window 0\|1` | composer | new windows take focus |

Layouts:

| Name | Shape |
| --- | --- |
| `tile` | master column left, stack right |
| `bstack` | master row on top, stack row below |
| `spiral` | each window takes a share of what is left |
| `grid` | equal cells |
| `mono` | one window at a time, full size |

The router remembers the settings it forwards and replays them on `dump`, so restarting the composer alone loses nothing. There is no click-to-focus: with `focus_follows_mouse 0`, use `focus next` / `focus prev`.

### `keys[]`

A binding is a modifier mask, an unshifted `XK_*` keysym and a command line:

```c
{ MOD,         XK_w, "spawn firefox" },
{ MOD | SHIFT, XK_w, "spawn firefox -private-window" },
```

Put `SHIFT` in the mask for shifted keys. Bindings for disabled features are harmless: the command is sent and ignored. `MOD` is Super (`XCB_MOD_MASK_4`); change the `#define` at the top of the table to use another modifier.

### Minimal setup patch

`apply_creator_patches.sh` applies `creator.patch` to `config.h`: tile layout only, black and white borders, terminal `xxt` on MOD+Return, `xwinmenu` on MOD+p, `xwinhelp` on MOD+F1. Run it in the source directory, then `make xwinkeys xwinhelp` and restart `xwinkeys`.

## Default keys

`MOD` is Super.

| Key | Command |
| --- | --- |
| MOD+Shift+Return | `spawn xxt` |
| MOD+d | `spawn xwinmenu` |
| **MOD+F1** | `spawn xwinhelp` |
| MOD+Return | `zoom` (focused window to master) |
| MOD+q / MOD+Shift+q | `close` / `quit` |
| MOD+j / MOD+k | `focus next` / `focus prev` |
| MOD+Space | `layout next` |
| MOD+t / b / s / g / m | `layout tile` / `bstack` / `spiral` / `grid` / `mono` |
| MOD+h / MOD+l | `ratio -5` / `ratio +5` |
| MOD+, / MOD+. | `master +1` / `master -1` |
| MOD+f | `fullscreen @` |
| MOD+Shift+Space | `float @` |
| MOD+arrows | `move @ DX DY` (30 px, floating windows) |
| MOD+Shift+arrows | `resize @ DW DH` (30 px, floating windows) |
| MOD+c | `center @` |
| MOD+1..9 | `view N` |
| MOD+Shift+1..9 | `tag @ N` |

Desktops (tags) are 1-9. There is no desktop 0.

## Floating windows

Dialogs, splash screens, utility windows, anything with `WM_TRANSIENT_FOR`, and windows with a fixed size (min size equals max size) float on their own. Any other window floats with `float @`, or by grabbing it with the mouse.

- Floating windows are always above tiled ones, in every layout. Fullscreen windows are above those.
- A dialog opens centred over its parent. A program that asks for a position (`USPosition`/`PPosition`) gets it. Everything else opens centred in the work area, and no window opens larger than the work area.
- MOD+left drag moves, MOD+right drag resizes (bottom-right corner). Grabbing a tiled window floats it first. See `set mouse_mod`.
- Move and resize keep at least 40 px of the window on screen. Resizing respects the program's min and max size.
- The router remembers each window's floating geometry. Tile it and float it again and it comes back where it was.

## xwinbar and xwinnotify

```
xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad] [-i fifo] [-t secs] [FIFO]
```

The bar reads lines from a FIFO (default `$XDG_RUNTIME_DIR/xwinbar.fifo`, else `/tmp/xwinbar.fifo`). It creates the FIFO if missing and removes it on exit. Anything can write to it:

| Line | Effect |
| --- | --- |
| `text` | status line, stays until replaced |
| `!text` | notification, shown for `-t` seconds, then the status returns |
| `!` | clears the notification now |

| Option | Meaning |
| --- | --- |
| `-b` | bar at the bottom (default: top) |
| `-a l\|c\|r` | text alignment (default `r`) |
| `-f font` | core X font (default `fixed`; list with `xlsfonts`) |
| `-B` / `-F` | background / text color, hex (default `000000` / `ffffff`) |
| `-p pad` | vertical padding in pixels (default 3) |
| `-i fifo` | FIFO path, same as the `FIFO` argument (the argument wins if both are given) |
| `-t secs` | notification time (default 5) |

The example script `xwinbar.sh` prints date, time and battery, one line every 5 seconds:

```
Mon 23 Nov  06:46  BAT 73%-
```

`+` charging, `-` discharging, `=` full. Nothing is shown without a battery. Run it as `xwinbar.sh > "$bfifo" &`, or use any source that writes lines: `while :; do uptime; sleep 10; done > "$bfifo" &`.

The bar is a dock window with `_NET_WM_STRUT_PARTIAL`: the router shrinks the work area and the composer lays windows out around it. Core X fonts are Latin-1 only. The FIFO is opened read-write, so writers closing it never end the bar's input. Do not run two bars on the same FIFO; give the second one its own `-i`.

### xwinnotify

```
xwinnotify [MESSAGE...]
```

Shows MESSAGE in the bar for `-t` seconds, then the status line comes back. It joins its arguments with spaces and writes `!MESSAGE` to the bar's FIFO. With no arguments it writes `!` and clears the notification.

```
xwinnotify "build finished"
xwinnotify disk is 95% full
xwinnotify                     # clear
```

- A new notification replaces the one showing. There is no queue.
- It uses `$XWINBAR_FIFO` if set, else the bar's default path. If you started the bar with `-i path`, `export XWINBAR_FIFO=path`.
- It exits with an error if the FIFO does not exist, which usually means the bar is not running.
- It opens the FIFO read-write, so it never hangs waiting for a reader.

Use it from anything: `make && xwinnotify done || xwinnotify build FAILED`, a cron job, or a key binding (`{ MOD, XK_n, "spawn xwinnotify hello" }`).

## xwinmenu

```
xwinmenu
```

A program launcher. It lists every executable in `$PATH`, sorted, in a box in the centre of the screen. Type to filter, press Enter to launch. It takes no options: its settings are at the top of `xwinmenu.c`. Edit, then `make xwinmenu`.

| Setting | Meaning |
| --- | --- |
| `SEARCH_TOOL` | program used to filter the list (default `"grep"`) |
| `FONT` | core X font (default `"fixed"`) |
| `BG` / `FG` | background / text color, `0xRRGGBB` (default black / white) |
| `WIDTH` | window width in pixels |
| `LINES` | visible entries below the input line |
| `PAD` / `BORDER` | text padding / border width in pixels |

| Key | Effect |
| --- | --- |
| type | filter the list |
| Backspace / Ctrl-u | delete a character / clear the query |
| Down, Tab, Ctrl-n | next entry |
| Up, Ctrl-p | previous entry |
| Enter | run the selected entry; if nothing matches, run the typed text through `sh -c` |
| Escape, Ctrl-c | quit |

- At startup it scans `$PATH` and writes the names to a temp file in `/tmp`, removed on exit.
- On every keystroke it runs `SEARCH_TOOL -i -F -e QUERY FILE` and shows the output. Any grep-compatible tool works. The match is case-insensitive and literal, not a regular expression.
- The window is override-redirect, so `xwinrouter` does not manage or tile it. It grabs the keyboard while open.
- Launched programs are detached (`setsid`), so they keep running after the menu closes.

Limits: no history, no descriptions, no mouse, no `.desktop` entries, no UTF-8, and the list is read once when the menu opens.

## xwinhelp

```
xwinhelp [-p]
```

Shows the key bindings and settings that are really in your `config.h`. The text is generated by the program itself from the `keys[]` and `settings[]` tables, so it cannot go out of date. Rebuild `xwinhelp` after editing `config.h`.

- It writes the text to `help.txt` in `$XDG_RUNTIME_DIR` (else `/tmp`) and shows it in a window. The file is created readable by the owner only and symlinks are refused.
- `-p` prints the text on stdout and opens no window.
- The window is a normal dialog: the router floats and centres it, and MOD+q closes it like any other window.
- Keys in the window: `q`, Escape or Ctrl-c close; `j`/`k` and Up/Down scroll one line; Space/PageDown and PageUp scroll a page; Home and End jump.
- It needs nothing beyond what the other programs need: xcb, the keysym header and the `fixed` font. Font and colors are set at the top of `xwinhelp.c`.
- Bound to MOD+F1 in `config.h`: `{ MOD, XK_F1, "spawn xwinhelp" }`.

## Talking to the router

There are two FIFOs. Both take one command per line and can be written with plain `echo`.

| FIFO | Path | Read by | Gets |
| --- | --- | --- | --- |
| router | `${XDG_RUNTIME_DIR:-/tmp}/xwr.fifo` (made by `~/.xinitrc`) | `xwinrouter` | window manager commands |
| bar | `${XDG_RUNTIME_DIR:-/tmp}/xwinbar.fifo` (made by `xwinbar` or `~/.xinitrc`) | `xwinbar` | status lines and notifications |

```sh
rt=${XDG_RUNTIME_DIR:-/tmp}

echo "view 2"        > $rt/xwr.fifo
echo "layout mono"   > $rt/xwr.fifo      # unknown to the router, forwarded to the composer
echo "set gap 6"     > $rt/xwr.fifo
echo "spawn xterm"   > $rt/xwr.fifo
echo "close"         > $rt/xwr.fifo      # focused window

echo "my status"     > $rt/xwinbar.fifo  # stays until replaced
echo "!look at me"   > $rt/xwinbar.fifo  # shown for -t seconds
echo "!"             > $rt/xwinbar.fifo  # clear the notification
```

Several lines in one write work too: `printf 'a\nb\n!c\n' > $rt/xwinbar.fifo`.

- A line written in one `write` of up to 4096 bytes is not mixed with another writer's.
- Bar limits: text is cut at 255 characters, control characters become spaces, a line over 1023 bytes is dropped.
- If the program reading a FIFO is not running, a plain `echo > fifo` can wait forever. To never wait, open it read-write: `echo "!hi" 1<> $rt/xwinbar.fifo` (what `xwinnotify` does). The bar removes its FIFO on a normal exit, so a missing FIFO means the bar is not running; after a crash a stale one can remain.

### Commands

Written to the router FIFO. `ID` is hex such as `0x400003`, or `@` for the focused window.

| Command | Effect |
| --- | --- |
| `geom ID X Y W H` | move/resize, outer size including border |
| `focus ID\|next\|prev\|none` | set focus |
| `close [ID]` | ask the window to close (default: focused) |
| `tag ID N` | move window to tag N |
| `view N` | show tag N |
| `float ID [0\|1]` | set or toggle floating. The window gets back the geometry it had the last time it floated; one that fills the screen is shrunk to 70% and centred |
| `move ID DX DY` | move a floating window by DX, DY. At least 40 px stay on screen |
| `resize ID DW DH` | grow or shrink a floating window, within its min/max size hints |
| `center ID` | centre a floating window in the work area |
| `fullscreen ID [0\|1]` | set or toggle fullscreen |
| `raise ID` | raise window |
| `spawn COMMAND...` | run through `sh -c` |
| `set KEY VALUE...` | change a setting |
| `dump` | re-send the full state as events |
| `quit` | exit |
| anything else | forwarded as a `bind LINE` event |

Keep lines under 512 bytes (1000 for `spawn`).

### Events

Printed by `xwinrouter` on stdout.

```
map ID TAG FLOAT        unmap ID            focus ID|none
tag ID N                view N              float ID 0|1
fullscreen ID 0|1       workarea X Y W H    enter ID
activate ID             desktop N           bind LINE
```

- `workarea`: the screen minus dock struts
- `enter`: the pointer entered a window
- `activate`, `desktop`: EWMH requests from other programs
- `dump` replies with the remembered `bind set ...` lines, then `workarea`, `view`, one `map` per window, and a final `focus`

To watch events live: `xwinrouter < fifo | tee /dev/stderr | xwincomposer > fifo`.

### xwincomposer

```
xwincomposer [-l layout] [-r master%] [-n nmaster] [-g gap]
```

The options only set defaults until `xwinkeys` sends its settings. At runtime it handles these `bind` lines: `set ...`, `layout NAME|next`, `ratio +N|-N`, `master +N|-N`, `gap +N|-N`, `zoom`. Floating and fullscreen windows are not tiled. On startup it sends `dump`, so it can be restarted alone without losing windows.

## Writing your own parts

Any program works as the composer if it prints `dump` first, reads the events above, and prints `geom`, `focus` and `raise` commands. Likewise for `xwinkeys`: anything that writes command lines into the FIFO can replace it.

### A composer in 36 lines of sh

Master column on the left, the rest stacked on the right. It ignores tags, fullscreen, settings and layouts, and leaves floating windows alone.

```sh
#!/bin/sh
# Bare composer: master column on the left, the rest stacked on the right.
# Usage: xwinrouter < fifo | ./composer.sh > fifo
wins= x=0 y=0 w=800 h=600

relayout() {
	set -- $wins
	n=$#
	[ "$n" -eq 0 ] && return
	if [ "$n" -eq 1 ]; then
		echo "geom $1 $x $y $w $h"
		return
	fi
	mw=$((w * 55 / 100))
	ch=$((h / (n - 1)))
	echo "geom $1 $x $y $mw $h"
	shift
	i=0
	for id; do
		echo "geom $id $((x + mw)) $((y + i * ch)) $((w - mw)) $ch"
		i=$((i + 1))
	done
}

echo dump
while read -r ev a b c d; do
	case $ev in
	map)	[ "$c" = 0 ] && wins="$wins $a"      # a=id c=floating
		relayout; echo "focus $a" ;;
	unmap)	new=
		for id in $wins; do [ "$id" != "$a" ] && new="$new $id"; done
		wins=$new; relayout ;;
	workarea) x=$a y=$b w=$c h=$d; relayout ;;
	enter)	echo "focus $a" ;;
	esac
done
```

Use it by replacing the last line of `~/.xinitrc`: `xwinrouter < "$fifo" | ./composer.sh > "$fifo"`

### A key program in 56 lines of C

Grabs five Super shortcuts and prints the command bound to each. It sends no settings, so the router and composer run on their defaults. Caps Lock and Num Lock are not handled: with either on, the shortcuts do not fire.

```c
/* Bare xwinkeys: grabs Super+key, prints the bound command as one line.
 * cc -o xwinkeys xwinkeys.c -lxcb      (needs X11/keysym.h, header only) */
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <X11/keysym.h>

#define SUPER XCB_MOD_MASK_4
#define SHIFT XCB_MOD_MASK_SHIFT

static const struct { unsigned int mod; unsigned long sym; const char *cmd; } keys[] = {
	{ SUPER,         XK_Return, "spawn xterm" },
	{ SUPER,         XK_j,      "focus next" },
	{ SUPER,         XK_k,      "focus prev" },
	{ SUPER,         XK_q,      "close" },
	{ SUPER | SHIFT, XK_q,      "quit" },
};
#define N (sizeof keys / sizeof keys[0])

int main(void)
{
	xcb_connection_t *c = xcb_connect(NULL, NULL);
	const xcb_setup_t *setup = xcb_get_setup(c);
	xcb_window_t root = xcb_setup_roots_iterator(setup).data->root;
	int min = setup->min_keycode, max = setup->max_keycode, k;
	xcb_get_keyboard_mapping_reply_t *m = xcb_get_keyboard_mapping_reply(c,
	    xcb_get_keyboard_mapping(c, min, max - min + 1), NULL);
	xcb_keysym_t *sym = xcb_get_keyboard_mapping_keysyms(m);
	xcb_keycode_t code[N] = { 0 };
	xcb_generic_event_t *ev;
	size_t i;

	for (i = 0; i < N; i++)
		for (k = 0; k <= max - min; k++)
			if (sym[k * m->keysyms_per_keycode] == keys[i].sym) {
				code[i] = min + k;
				xcb_grab_key(c, 1, root, keys[i].mod, code[i],
				    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				break;
			}
	xcb_flush(c);

	while ((ev = xcb_wait_for_event(c)) != NULL) {
		if ((ev->response_type & ~0x80) == XCB_KEY_PRESS) {
			xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;
			for (i = 0; i < N; i++)
				if (code[i] == e->detail && keys[i].mod == e->state) {
					printf("%s\n", keys[i].cmd);
					fflush(stdout);
				}
		}
		free(ev);
	}
	return 0;
}
```

Run it like the real one: `./xwinkeys > "$fifo" &`. A shell script that does `echo "view 2" > "$fifo"` would do just as well.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Keys do nothing | Is `xwinkeys` running and writing into the router's FIFO? Rebuilt and restarted after editing `config.h`? Caps Lock / Num Lock are handled by the real `xwinkeys`, but not by the 56-line example. |
| Windows open but are not tiled | The composer is not running or not connected: the last line of `~/.xinitrc` must pipe the router into it and back into the FIFO. |
| `echo > fifo` hangs | Nothing is reading that FIFO. Start the router (or the bar) first. |
| No bar notifications | `xwinnotify` needs the bar's FIFO. Set `XWINBAR_FIFO` if you started the bar with `-i`. |
| MOD+F1 does nothing | `xwinhelp` is not in `$PATH`, or `xwinkeys` was built before the binding was added. |
| Help is out of date | Rebuild `xwinhelp` after editing `config.h`. |
| `xwinmenu` / `xwinhelp`: cannot open font | Pick another core font from `xlsfonts` and set `FONT` in the source. |
