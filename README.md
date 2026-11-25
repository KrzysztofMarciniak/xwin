# xwin

A collection of lightweight, modular tools that apply Unix philosophy to tiling window management.

Small programs, plain text, one job each. Written in C89, linked only against `libxcb`.

| Program | Job |
| --- | --- |
| `xwinrouter` | Window manager mechanism. The only program with the WM role. Manages windows, applies the geometry it is told to apply, reports what happens. No layout, no keys, no focus policy. |
| `xwincomposer` | Layout and focus policy. Reads router events, writes router commands. |
| `xwinkeys` | Key bindings and **the only configuration**. Sends its settings at startup, then turns key presses into commands. |
| `xwinbar` | Status bar. Reads lines from a FIFO: status lines and `!notifications`. |
| `xwinmenu` | Program launcher, a dmenu alternative. Lists the executables in `$PATH` in the centre of the screen. |

Everything they say to each other is one line of plain text.

1. `xwinkeys` grabs the shortcut keys listed in `config.h`. Only those: all other typing goes straight to the focused window. At startup it writes its settings to the FIFO, then one command line (for example `focus next`) for every shortcut you press.
2. `xwinrouter` reads the FIFO. Commands it knows (`focus`, `close`, `tag`, `view`, `spawn`, `geom`, ...) it executes itself. Anything else it prints on its stdout as a `bind` event. It also prints an event for everything that happens on screen: window opened, window closed, focus changed, usable area changed.
3. `xwincomposer` reads those events from the router's stdout, decides where every window goes, and writes `geom`, `focus` and `raise` commands to the FIFO. The router reads them there and applies them.
4. `xwinbar` is independent. It reads lines from its own FIFO (and optionally runs a script that prints lines), shows the last status line, shows `!` lines as timed notifications, and tells the router to keep that strip of the screen free.

The loop is closed by the last line of `xinitrc.example`: the router's stdout is piped into the composer, and the composer's stdout goes back into the FIFO that the router reads.

Only `xwinkeys` includes `config.h`. The router and the composer have no config file and no compile-time options. They are configured at runtime by `set` commands, so changing a setting never means rebuilding them.

## Requirements

- C compiler (C89 is enough) and `pkg-config`
- `libxcb` and its headers (the only linked library)
- `X11/keysym.h` from xorgproto, for `xwinkeys` (header only, nothing from libX11 is linked)
- a core X font for `xwinbar` and `xwinmenu` (the built-in `fixed` works)
- a grep-compatible search tool for `xwinmenu` (`grep`)

`XDG_RUNTIME_DIR` is optional. `xinitrc.example` uses it for the FIFO and falls back to `/tmp`. The programs never read it.

## Build

```
make
make install        # PREFIX=/usr/local by default
```

Or by hand:

```
pkg-config --cflags --libs xcb

cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) \
   -o xwinrouter xwinrouter.c $(pkg-config --libs xcb)

cc -std=c89 -pedantic -Wall -Wextra -o xwincomposer xwincomposer.c

cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) \
   -o xwinkeys xwinkeys.c $(pkg-config --libs xcb)

cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) \
   -o xwinbar xwinbar.c $(pkg-config --libs xcb)

cc -std=c89 -pedantic -Wall -Wextra $(pkg-config --cflags xcb) \
   -o xwinmenu xwinmenu.c $(pkg-config --libs xcb)

install -m 755 xwinrouter xwincomposer xwinkeys xwinbar xwinmenu /usr/local/bin/
install -d /usr/local/share/xwinrouter
install -m 755 xwinbar.sh /usr/local/share/xwinrouter/
install -m 755 xwinnotify.sh /usr/local/bin/xwinnotify

cp xinitrc.example ~/.xinitrc
```

After editing `config.h`, rebuild only `xwinkeys`.

## Run

`~/.xinitrc` (this is `xinitrc.example`):

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

Then `startx`. Windows already open when the router starts are adopted. The bar can start before or after the router.

Try it in a nested server:

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

Anything that can write a line to the FIFO can control the window manager: `echo "view 2" > "$fifo"`.

## Configuration

`config.h` has two tables and is read by `xwinkeys` only.

### `settings[]`

Each line is sent to the FIFO when `xwinkeys` starts. To apply a change: edit, rebuild `xwinkeys`, restart it.

| Setting | For | Values |
| --- | --- | --- |
| `set border N` | router | border width in pixels |
| `set color_focus RRGGBB` | router | focused border color |
| `set color_normal RRGGBB` | router | other borders |
| `set floating 0\|1` | router | `0`: everything is tiled, dialogs too, `float` does nothing |
| `set fullscreen 0\|1` | router | `0`: fullscreen requests and the command are ignored |
| `set tags N` | router | `1`..`9`. `1` turns tagging off: `view` and `tag` do nothing |
| `set layouts NAME...` | composer | layouts `layout next` cycles through; omit a name to disable it |
| `set layout NAME` | composer | starting layout |
| `set ratio N` | composer | master area in percent (10..90) |
| `set master N` | composer | windows in the master area |
| `set gap N` | composer | pixels around every window |
| `set focus_follows_mouse 0\|1` | composer | focus the window under the pointer |
| `set focus_new_window 0\|1` | composer | new windows take focus |

Layouts:

- `tile`: master column left, stack right
- `bstack`: master row on top, stack row below
- `spiral`: each window takes a share of what is left
- `grid`: equal cells
- `mono`: one window at a time, full size

The router remembers the settings it forwards and replays them on `dump`, so restarting the composer alone loses nothing.

There is no click-to-focus. With `focus_follows_mouse 0`, use `focus next` / `focus prev`.

### `keys[]`

A binding is a modifier mask, an unshifted `XK_*` keysym, and a command line: `{ MOD, XK_w, "spawn firefox" }`. Put `SHIFT` in the mask for shifted keys. Bindings for disabled features are harmless; the command is sent and ignored.

## Default keys

`MOD` is Super.

| Key | Command |
| --- | --- |
| MOD+Shift+Return | `spawn xterm` |
| MOD+d | `spawn xwinmenu` |
| MOD+Return | `zoom` (focused window to master) |
| MOD+q / MOD+Shift+q | `close` / `quit` |
| MOD+j / MOD+k | `focus next` / `focus prev` |
| MOD+Space | `layout next` |
| MOD+t / b / s / g / m | `layout tile` / `bstack` / `spiral` / `grid` / `mono` |
| MOD+h / MOD+l | `ratio -5` / `ratio +5` |
| MOD+, / MOD+. | `master +1` / `master -1` |
| MOD+f | `fullscreen @` |
| MOD+Shift+Space | `float @` |
| MOD+1..9 | `view N` |
| MOD+Shift+1..9 | `tag @ N` |

## Creator's settings patch

`apply_creator_patches.sh` patches `config.h` to a minimal setup: tile layout only, black and white, terminal is `xxt`. Run it in the source directory, then `make xwinkeys` and restart `xwinkeys`.

`MOD` is Super.

| Key | Action |
| --- | --- |
| MOD+Enter | launch terminal (`xxt`) |
| MOD+q | close focused window |
| MOD+Shift+q | exit |
| MOD+j / MOD+k | focus left / right window |
| MOD+Shift+l / MOD+Shift+h | increase / decrease window size |
| MOD+[1-9] | switch to desktop |
| MOD+Shift+[1-9] | move focused window to desktop |
| MOD+p | launch `xwinmenu` |

Desktops are 1-9 (the router supports `1..9`, there is no desktop 0).

## xwinbar

```
xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad] [-i fifo] [-t secs] [FIFO]
```

The bar reads lines from a FIFO (default `$XDG_RUNTIME_DIR/xwinbar.fifo`, else `/tmp/xwinbar.fifo`). It creates the FIFO if missing and removes it on exit. Anything can write to it:

| Line | Effect |
| --- | --- |
| `text` | status line, stays until replaced |
| `!text` | notification, shown for `-t` seconds, then the status returns |
| `!` | clears the notification now |

`xwinnotify "build finished"` is a one-line wrapper that writes `!build finished`. The optional `FIFO` argument is the path to read from (same as `-i`; it wins if both are given). Point a status script at it, like `xwinbar.sh > "$bfifo" &`. The example `xwinbar.sh` shows date, time and battery:

```
Mon 23 Nov  06:46  BAT 73%-
```

`+` charging, `-` discharging, `=` full. Nothing is shown without a battery.

| Option | Meaning |
| --- | --- |
| `-b` | bar at the bottom (default: top) |
| `-a l\|c\|r` | text alignment (default `r`) |
| `-f font` | core X font (default `fixed`; list with `xlsfonts`) |
| `-B` / `-F` | background / text color, hex (default `000000` / `ffffff`) |
| `-p pad` | vertical padding in pixels (default 3) |
| `-i fifo` | FIFO path, same as the `FIFO` argument (`xwinnotify` follows `$XWINBAR_FIFO`) |
| `-t secs` | notification time (default 5) |

The bar is a dock window with `_NET_WM_STRUT_PARTIAL`. The router shrinks the work area and the composer lays windows out around it. Core X fonts are Latin-1 only. The FIFO is opened read-write so writers closing it never end the bar's input.

Your own source is anything that writes lines: `while :; do uptime; sleep 10; done > "$bfifo" &`, or `echo '!disk full' > $XDG_RUNTIME_DIR/xwinbar.fifo`.

### xwinnotify

```
xwinnotify [MESSAGE...]
```

Shows MESSAGE in the bar for `-t` seconds (default 5), then the status line comes back. It joins its arguments with spaces and writes `!MESSAGE` to the bar's FIFO. With no arguments it writes `!`, which clears the current notification at once.

```
xwinnotify "build finished"
xwinnotify disk is 95% full
xwinnotify                     # clear
```

- A new notification replaces the one showing. There is no queue.
- It only sends notifications. To change the status line, write a plain line to the FIFO (see below).
- It uses `$XWINBAR_FIFO` if set, else the same default path as the bar. If you started the bar with `-i path`, `export XWINBAR_FIFO=path`.
- It exits with an error if the FIFO does not exist, which usually means the bar is not running.
- It opens the FIFO read-write, so it never hangs waiting for a reader.

Use it from anything: `make && xwinnotify done || xwinnotify build FAILED`, a cron job, a key binding (`spawn xwinnotify hello` in `config.h`).

## xwinmenu

```
xwinmenu
```

A simple program launcher. It lists every executable found in `$PATH`, sorted, in a box in the centre of the screen. Type to filter, press Enter to launch. It takes no options: its settings are at the top of `xwinmenu.c`. Edit, then `make xwinmenu`.

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

How it works:

- At startup it scans `$PATH` and writes the names to a temp file in `/tmp`. The file is removed on exit.
- On every keystroke it runs `SEARCH_TOOL -i -F -e QUERY FILE` and shows the output. Any grep-compatible tool works. The match is case-insensitive and literal, not a regular expression. With an empty query the whole list is shown.
- The window is override-redirect, so `xwinrouter` does not manage or tile it. It grabs the keyboard while open.
- Launched programs are detached (`setsid`), so they keep running after the menu closes.
- It is bound to MOD+d in `config.h`: `{ MOD, XK_d, "spawn xwinmenu" }`. It can also be started by hand: `echo "spawn xwinmenu" > $rt/xwr.fifo`.

Limits: no history, no descriptions, no mouse, no `.desktop` entries, no UTF-8 (core X fonts are Latin-1 only), and the list is read once when the menu opens.

## Sending lines by hand

There are two FIFOs. Both take one command per line, and both can be written with plain `echo`.

| FIFO | Path | Read by | Gets |
| --- | --- | --- | --- |
| router | `${XDG_RUNTIME_DIR:-/tmp}/xwr.fifo` (made by `~/.xinitrc`) | `xwinrouter` | window manager commands, see [Commands](#commands) |
| bar | `${XDG_RUNTIME_DIR:-/tmp}/xwinbar.fifo` (made by `xwinbar`, or by `~/.xinitrc`) | `xwinbar` | status lines and notifications |

```sh
rt=${XDG_RUNTIME_DIR:-/tmp}

# router: any command from the Commands table
echo "view 2"        > $rt/xwr.fifo
echo "layout mono"   > $rt/xwr.fifo      # unknown to the router, forwarded to the composer
echo "set gap 6"     > $rt/xwr.fifo
echo "spawn xterm"   > $rt/xwr.fifo
echo "close"         > $rt/xwr.fifo      # focused window

# bar: status line, notification, clear
echo "my status"     > $rt/xwinbar.fifo  # stays until replaced
echo "!look at me"   > $rt/xwinbar.fifo  # shown for -t seconds
echo "!"             > $rt/xwinbar.fifo  # clear the notification
```

Several lines in one write work too: `printf 'a\nb\n!c\n' > $rt/xwinbar.fifo`.

Notes:

- Lines are plain text, one per line, ended by a newline. Several programs can write at once; a line written in one `write` of up to 4096 bytes is not mixed with another writer's.
- Bar limits: text is cut at 255 characters, control characters become spaces, a line over 1023 bytes is dropped. Core X fonts are Latin-1 only, so other characters will look wrong.
- Router limits: see the note under [Commands](#commands).
- If the program that reads a FIFO is not running, a plain `echo > fifo` can wait forever for a reader. Check that the bar or router is up first. To make a write to the bar's FIFO never wait, open it read-write: `echo "!hi" 1<> $rt/xwinbar.fifo` (this is what `xwinnotify` does). The bar removes its FIFO when it exits normally, so a missing FIFO means the bar is not running; after a crash a stale one can be left behind.
- Do not run two bars on the same FIFO path; give the second one its own `-i`.
- To watch what the router sends back (events), run it from a terminal: `xwinrouter < fifo | tee /dev/stderr | xwincomposer > fifo`, or write `dump` to the router FIFO and read its stdout.

## Commands

Written to the router FIFO, read by `xwinrouter`. `ID` is hex such as `0x400003`, or `@` for the focused window.

| Command | Effect |
| --- | --- |
| `geom ID X Y W H` | move/resize, outer size including border |
| `focus ID\|next\|prev\|none` | set focus |
| `close [ID]` | ask the window to close (default: focused) |
| `tag ID N` | move window to tag N |
| `view N` | show tag N |
| `float ID [0\|1]` | set or toggle floating |
| `fullscreen ID [0\|1]` | set or toggle fullscreen |
| `raise ID` | raise window |
| `spawn COMMAND...` | run through `sh -c` |
| `set KEY VALUE...` | change a setting |
| `dump` | re-send the full state as events |
| `quit` | exit |
| anything else | forwarded as a `bind LINE` event |

Keep lines under 512 bytes (1000 for `spawn`). Lines up to 4096 bytes are written atomically, so several writers can share the FIFO.

## Events

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

## xwincomposer

```
xwincomposer [-l layout] [-r master%] [-n nmaster] [-g gap]
```

The options only set defaults until `xwinkeys` sends its settings. At runtime it handles these `bind` lines: `set ...`, `layout NAME|next`, `ratio +N|-N`, `master +N|-N`, `gap +N|-N`, `zoom`.

Floating and fullscreen windows are not tiled. On startup it sends `dump`, so it can be restarted alone without losing windows.

### Writing your own

Any program works if it prints `dump` first, reads the events above, and prints `geom`, `focus` and `raise` commands. Replace `xwincomposer` to change the policy completely. Likewise `xwinkeys`: anything that writes command lines into the FIFO can replace it.

## Minimal examples

Two bare replacements, to show how little a part needs to be. Both were tested: the composer by feeding it events, the key program under Xvfb with injected key presses.

### A composer in 36 lines of sh

Master column on the left, everything else stacked on the right. It ignores tags, fullscreen, settings and layouts. Floating windows are left alone. It prints `dump` first, reads events, prints commands, and nothing else.

```sh
#!/bin/sh
# Bare composer: master column on the left, the rest stacked on the right.
# Ignores tags, fullscreen and layouts. Usage: xwinrouter < fifo | ./composer.sh > fifo
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

Use it by replacing the last line of `~/.xinitrc`:

```sh
xwinrouter < "$fifo" | ./composer.sh > "$fifo"
```

### A key program in 56 lines of C

Grabs five Super shortcuts and prints the command bound to each, one line per press. It sends no settings, so the router and composer run on their defaults. Caps Lock and Num Lock are not handled: with either on, the shortcuts do not fire.

```c
/* Bare xwinkeys: grabs Super+key, prints the bound command as one line.
 * cc -o xwinkeys xwinkeys.c -lxcb      (needs X11/keysym.h, header only)
 * Does not handle Caps Lock / Num Lock and sends no settings. */
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

```
cc -o xwinkeys xwinkeys.c -lxcb
```

Run it like the real one: `./xwinkeys > "$fifo" &`. Any program that writes the same lines would do, including a shell script: `echo "view 2" > "$fifo"`.

## Design notes

- Windows are not reparented. Borders are the client's own. Windows on other tags are moved off-screen, not unmapped.
- A router crash leaves windows alive. Restarting re-adopts them, all on the current tag.
- On shutdown, hidden windows are moved back on screen.
- New tiled windows first get the full work area, then the layout moves them.
- Floating windows and dialogs (transient, dialog, splash, utility) are centered if they ask for position 0,0.
- Dock windows (`_NET_WM_WINDOW_TYPE_DOCK`) are not tiled. Their struts shrink the work area.
- Minimal EWMH: client list, active window, desktop count and current desktop, supporting WM check, fullscreen state.
- Only the router holds the WM role. Key grabs work from any X client, which is why `xwinkeys` is a separate process.

## Limits

- Bindings and settings are compiled into `xwinkeys`. No runtime config file.
- Only one X client can grab a given key combination.
- No click-to-focus, window rules, multi-monitor awareness (the screen is one rectangle), or mouse bindings.
- With focus following the mouse, a new window can lose focus to whatever ends up under the pointer after relayout.
- If the router restarts, restart `xwincomposer` and `xwinkeys` too. `xinitrc.example` starts all three together, and `xwinkeys` resends settings on startup. `xwinkeys` exits on the next key press once nobody reads the FIFO.
- If the composer hangs and stops reading, the router eventually blocks on output.
- `xwinbar` has no tag or window list, no clickable parts, and no UTF-8.
- `xwinmenu` is only a launcher for programs in `$PATH`. See its section for limits.
- Tested under Xvfb with test windows, injected key presses, and the bar (struts, top and bottom, started before and after the router). Real applications have not been tested.
