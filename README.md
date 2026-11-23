# xwinrouter

A tiling window manager for X11 built from small programs that talk through
plain text. Written in C89.

| Program | Job |
| --- | --- |
| `xwinrouter` | The window manager mechanism and the only program with the WM role. Manages windows, applies geometry it is told to apply, reports what happens. No layout, no keys, no focus policy. |
| `xwincomposer` | Layout and focus policy. Reads router events, writes router commands. |
| `xwinkeys` | Key bindings and **the only configuration**. Sends its settings to the others at startup, then turns key presses into commands. |
| `xwinbar` | A simple status bar. Shows the last line printed by an `sh` script. |

```
xwinkeys --settings, keys--> FIFO --> xwinrouter --events--> xwincomposer
                              ^                                  |
                              +-----------commands---------------+

xwinbar <-- sh script          (independent: a dock window the router tiles around)
```

Only `xwinkeys` includes `config.h`. The router and the composer have no config
file and no compile-time options; they are configured at runtime by `set`
commands, so changing a setting never means rebuilding them.

## Requirements

- a C compiler (C89 is enough) and `pkg-config`
- `libxcb` with its headers (the only library that gets linked)
- `X11/keysym.h` from xorgproto, used by `xwinkeys` (header only, gives the
  `XK_*` key names; nothing from libX11 is linked)
- for `xwinbar`: a core X font. The X server's built-in `fixed` works.

Nothing else. `XDG_RUNTIME_DIR` is optional: `xinitrc.example` uses it for the
FIFO and falls back to `/tmp` if it is not set. The programs never read it.

## Build

With make:

```
make
make install        # PREFIX=/usr/local by default
```

By hand, one step at a time:

```
# 1. check that xcb is found
pkg-config --cflags --libs xcb

# 2. router (needs xcb)
cc -std=c89 -pedantic -Wall -Wextra \
   $(pkg-config --cflags xcb) \
   -o xwinrouter xwinrouter.c \
   $(pkg-config --libs xcb)

# 3. composer (no libraries)
cc -std=c89 -pedantic -Wall -Wextra -o xwincomposer xwincomposer.c

# 4. key daemon (needs xcb, X11/keysym.h and config.h)
cc -std=c89 -pedantic -Wall -Wextra \
   $(pkg-config --cflags xcb) \
   -o xwinkeys xwinkeys.c \
   $(pkg-config --libs xcb)

# 5. status bar (needs xcb)
cc -std=c89 -pedantic -Wall -Wextra \
   $(pkg-config --cflags xcb) \
   -o xwinbar xwinbar.c \
   $(pkg-config --libs xcb)

# 6. install
install -m 755 xwinrouter xwincomposer xwinkeys xwinbar /usr/local/bin/
install -d /usr/local/share/xwinrouter
install -m 755 xwinbar.sh /usr/local/share/xwinrouter/

# 7. session file
cp xinitrc.example ~/.xinitrc
```

After editing `config.h` only step 4 has to be redone.

## Run

`~/.xinitrc` (this is `xinitrc.example`):

```sh
#!/bin/sh
# ~/.xinitrc
fifo=${XDG_RUNTIME_DIR:-/tmp}/xwr.fifo
rm -f "$fifo"
mkfifo -m 600 "$fifo"

xsetroot -cursor_name left_ptr &
xwinbar /usr/local/share/xwinrouter/xwinbar.sh &

# sends the settings and key bindings from config.h to the router
xwinkeys > "$fifo" &

# last command: the session ends when the router exits
xwinrouter < "$fifo" | xwincomposer > "$fifo"
```

Then `startx`. Programs already open when the router starts are adopted. The
bar can be started before or after the router.

To try it without leaving your session, in a nested X server:

```
mkfifo fifo
Xephyr :1 -screen 1280x800 &
export DISPLAY=:1
xwinkeys > fifo &
xwinrouter < fifo | xwincomposer > fifo &
xwinbar ./xwinbar.sh &
xterm &
```

Send a command by hand with `echo "view 2" > "$fifo"`. Anything that can write
a line to the FIFO can control the window manager.

## Configuration

`config.h` has two tables and is read by `xwinkeys` only.

### `settings[]`

Every line is sent to the FIFO when `xwinkeys` starts. To apply a changed
config: edit, rebuild `xwinkeys` (step 4), restart it.

| Setting | For | Values |
| --- | --- | --- |
| `set border N` | router | border width in pixels |
| `set color_focus RRGGBB` | router | focused border color |
| `set color_normal RRGGBB` | router | other borders |
| `set floating 0\|1` | router | `0`: every window is tiled, dialogs too, `float` does nothing |
| `set fullscreen 0\|1` | router | `0`: fullscreen requests and the command are ignored |
| `set tags N` | router | `1`..`9`. `1` switches tagging off: `view` and `tag` do nothing |
| `set layouts NAME...` | composer | layouts that `layout next` cycles through; leave a name out to disable it |
| `set layout NAME` | composer | layout to start with |
| `set ratio N` | composer | master area in percent (10..90) |
| `set master N` | composer | windows in the master area |
| `set gap N` | composer | pixels around every window |
| `set focus_follows_mouse 0\|1` | composer | focus the window under the pointer |
| `set focus_new_window 0\|1` | composer | new windows take focus |

Layouts: `tile` (master column left, stack right), `bstack` (master row on top,
stack row below), `spiral` (each window takes a share of what is left), `grid`
(equal cells), `mono` (one window at a time, full size).

The router keeps the settings it forwards and replays them whenever the
composer asks for a `dump`, so restarting the composer alone loses nothing.

There is no click-to-focus. With `focus_follows_mouse 0`, move focus with the
`focus next` / `focus prev` keys.

### `keys[]`

A binding is a modifier mask, an unshifted `XK_*` keysym and a command line, so
`{ MOD, XK_w, "spawn firefox" }` is valid. Use `SHIFT` in the modifier for
shifted keys. Keys for features you switched off are harmless: the command is
sent and ignored.

## Default keys

`MOD` is Super.

| Key | Command |
| --- | --- |
| MOD+Shift+Return | `spawn xterm` |
| MOD+d | `spawn dmenu_run -m '0' -nb '#000000' -nf '#ffffff' -sb '#ffffff' -sf '#000000'` |
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

## xwinbar

```
xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad] COMMAND
```

Runs `COMMAND` with `sh -c` and shows the last line it prints, so the command
is normally a script that prints a line, sleeps, and repeats. The bar knows
nothing about dates or batteries; the example `xwinbar.sh` shows date, time and
battery:

```
Mon 23 Nov  06:46  BAT 73%-
```

(`+` charging, `-` discharging, `=` full; nothing is shown without a battery.)

| Option | Meaning |
| --- | --- |
| `-b` | bar at the bottom (default: top) |
| `-a l\|c\|r` | text alignment (default `r`) |
| `-f font` | core X font name (default `fixed`; list them with `xlsfonts`) |
| `-B` / `-F` | background / text color, hex (default `000000` / `ffffff`) |
| `-p pad` | vertical padding in pixels (default 3) |

The bar is a dock window with `_NET_WM_STRUT_PARTIAL`, so the router shrinks
the work area and the composer lays windows out below (or above) it. Core X
fonts are Latin-1 only. If the script exits, the last line stays on screen.

Write your own script: anything that prints lines works, for example
`xwinbar 'while :; do uptime; sleep 10; done'`.

## Commands (the FIFO, read by xwinrouter)

`ID` is hex such as `0x400003`, or `@` for the focused window.

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
| `set KEY VALUE...` | change a setting (table above) |
| `dump` | re-send the full state as events |
| `quit` | exit |
| anything else | forwarded as a `bind LINE` event |

Keep each line under 512 bytes (1000 for `spawn`). Lines up to 4096 bytes are
written atomically, so several writers can share the FIFO safely.

## Events (xwinrouter stdout)

```
map ID TAG FLOAT        unmap ID            focus ID|none
tag ID N                view N              float ID 0|1
fullscreen ID 0|1       workarea X Y W H    enter ID
activate ID             desktop N           bind LINE
```

`workarea` is the screen minus dock struts (bars). `enter` is the pointer
entering a window. `activate` and `desktop` are EWMH requests from other
programs. A `dump` replies with the remembered `bind set ...` lines, then
`workarea`, `view`, one `map` per window, and a final `focus`.

## xwincomposer

```
xwincomposer [-l layout] [-r master%] [-n nmaster] [-g gap]
```

The options only set defaults before `xwinkeys`' settings arrive. It handles
these `bind` lines at runtime: `set ...` (table above), `layout NAME|next`,
`ratio +N|-N`, `master +N|-N`, `gap +N|-N`, `zoom`.

Floating and fullscreen windows are not tiled. On startup it sends `dump`, so
you can restart it alone without losing windows.

### Writing your own

Any program works if it prints `dump` first, then reads the events above and
prints `geom`, `focus` and `raise` commands. Replace `xwincomposer` in the
pipeline to change the policy completely. The same goes for `xwinkeys`: any
program that writes command lines into the FIFO can replace it.

## Design notes

- Windows are not reparented. Borders are the client's own border, and windows
  on other tags are moved off-screen rather than unmapped. A router crash
  leaves windows alive; restarting re-adopts them (all on the current tag).
- On shutdown, hidden windows are moved back on screen.
- New tiled windows first get the full work area, then the layout moves them.
- Floating windows and dialogs (transient, dialog, splash, utility) are
  centered if they ask for position 0,0.
- Dock windows (`_NET_WM_WINDOW_TYPE_DOCK`) are not tiled; their struts
  shrink the work area.
- Minimal EWMH is set so bars and apps behave: client list, active window,
  desktop count and current desktop, supporting WM check, fullscreen state.
- Only the router holds the window-manager role; key grabs work from any X
  client, which is why `xwinkeys` can be a separate process.

## Limits

- Key bindings and settings are compiled into `xwinkeys`; there is no runtime
  config file.
- Only one X client can grab a given key combination.
- No click-to-focus, no window rules, no multi-monitor awareness (the screen
  is one rectangle), no mouse bindings.
- With focus following the mouse, a new window can lose focus to whichever
  window ends up under the pointer after the relayout.
- If the router restarts, restart `xwincomposer` and `xwinkeys` as well
  (`xinitrc.example` starts all three together; `xwinkeys` resends the
  settings on startup). `xwinkeys` exits on the next key press once nobody
  reads the FIFO any more.
- The router writes events to a pipe; if the composer hangs and stops reading
  for long enough, the router will block on output.
- `xwinbar` has no tag or window list, no clickable parts, and no UTF-8.
- Tested under Xvfb with test windows, injected key presses, and the bar
  (dock struts, top and bottom, started before and after the router). Real
  applications have not been tested.
