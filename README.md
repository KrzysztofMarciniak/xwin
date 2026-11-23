# xwinrouter

A tiling window manager for X11 built from three small programs that talk
through plain text. Written in C89.

| Program | Job |
| --- | --- |
| `xwinrouter` | The window manager mechanism and the only program with the WM role. Manages windows, applies geometry it is told to apply, reports what happens. No layout, no keys, no focus policy. |
| `xwincomposer` | Layout and focus policy. Reads router events, writes router commands. |
| `xwinkeys` | Key bindings. Grabs keys and writes one command line per key press. |

```
xwinkeys ---commands---> FIFO ---> xwinrouter ---events---> xwincomposer
                           ^                                     |
                           +-------------commands----------------+
```

All three include `config.h`, which is the one place for configuration.

## Requirements

- a C compiler (C89 is enough) and `pkg-config`
- `libxcb` with its headers (the only library that gets linked)
- `X11/keysym.h` from xorgproto, used by `xwinkeys` (header only, gives the
  `XK_*` key names; nothing from libX11 is linked)

Nothing else. `XDG_RUNTIME_DIR` is optional: `xinitrc.example` uses it for the
FIFO and falls back to `/tmp` if it is not set. The programs never read it.

## Build

With make:

```
make
make install        # PREFIX=/usr/local by default
```

By hand:

```
# 1. check that xcb is found
pkg-config --cflags --libs xcb

# 2. compile the router (needs xcb)
cc -std=c89 -pedantic -Wall -Wextra \
   $(pkg-config --cflags xcb) \
   -o xwinrouter xwinrouter.c \
   $(pkg-config --libs xcb)

# 3. compile the key daemon (needs xcb and X11/keysym.h)
cc -std=c89 -pedantic -Wall -Wextra \
   $(pkg-config --cflags xcb) \
   -o xwinkeys xwinkeys.c \
   $(pkg-config --libs xcb)

# 4. compile the composer (no libraries)
cc -std=c89 -pedantic -Wall -Wextra -o xwincomposer xwincomposer.c

# 5. install all three somewhere on your PATH
install -m 755 xwinrouter xwinkeys xwincomposer /usr/local/bin/

# 6. install the session file
cp xinitrc.example ~/.xinitrc
```

All three programs include `config.h`, so after editing it redo steps 2 to 4.

## Run

`~/.xinitrc` (this is `xinitrc.example`):

```sh
#!/bin/sh
# ~/.xinitrc
fifo=${XDG_RUNTIME_DIR:-/tmp}/xwr.fifo
rm -f "$fifo"
mkfifo -m 600 "$fifo"

xsetroot -cursor_name left_ptr &
# start bars, wallpaper, etc. here, each with "&"

xwinkeys > "$fifo" &

# last command: the session ends when the router exits
xwinrouter < "$fifo" | xwincomposer -g 4 > "$fifo"
```

Then `startx`. Programs that are already open when the router starts are
adopted.

To try it without leaving your session, in a nested X server:

```
mkfifo fifo
Xephyr :1 -screen 1280x800 &
export DISPLAY=:1
xwinkeys > fifo &
xwinrouter < fifo | xwincomposer > fifo &
xterm &
```

Send a command by hand with `echo "view 2" > "$fifo"`. Anything that can write
a line to the FIFO can control the window manager.

## Configuration

Everything is in `config.h` and is compiled in. Edit, then rebuild.

### Features

| Setting | Effect |
| --- | --- |
| `ENABLE_FLOATING` | `0`: every window is tiled, dialogs too, and `float` does nothing |
| `ENABLE_FULLSCREEN` | `0`: fullscreen requests from programs and the command are ignored |
| `NTAGS` | `1`..`9`. `1` turns tagging off: `view` and `tag` do nothing, no tag keys are bound |

### Layouts

Each layout has an on/off switch and disabled ones are not compiled in. At
least one must stay on.

| Setting | Layout |
| --- | --- |
| `LAYOUT_TILE` | master column on the left, stack on the right |
| `LAYOUT_BSTACK` | master row on top, stack row below |
| `LAYOUT_SPIRAL` | each window takes a share of what is left |
| `LAYOUT_GRID` | equal cells |
| `LAYOUT_MONO` | one window at a time, full size |

Also: `DEFAULT_LAYOUT` (by name), `MASTER_RATIO`, `NMASTER`, `GAP`. The command
line overrides them: `xwincomposer -l spiral -r 60 -n 1 -g 6`.

`layout next` cycles through the enabled layouts in the order above, and
`layout NAME` for a layout that is not compiled in is ignored.

### Focus

| Setting | Effect |
| --- | --- |
| `FOCUS_FOLLOWS_MOUSE` | focus the window under the pointer |
| `FOCUS_NEW_WINDOW` | new windows take focus |

There is no click-to-focus. With `FOCUS_FOLLOWS_MOUSE 0`, move focus with the
`focus next` / `focus prev` keys.

### Looks

`BORDER`, `COL_FOCUS`, `COL_NORMAL` (used by `xwinrouter`).

### Keys

The `keys[]` table, `MOD` and `SHIFT` (used by `xwinkeys`). A binding is a
modifier mask, an unshifted `XK_*` keysym and a command line, so
`{ MOD, XK_w, "spawn firefox" }` is valid. Use `SHIFT` in the modifier for
shifted keys. Keys for disabled features are wrapped in `#if` so they
disappear with the feature.

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
| `dump` | re-send the full state as events |
| `quit` | exit |
| anything else | forwarded as a `bind LINE` event |

Keep each line under 512 bytes. Lines up to 4096 bytes are written atomically,
so several writers can share the FIFO safely.

## Events (xwinrouter stdout)

```
map ID TAG FLOAT        unmap ID            focus ID|none
tag ID N                view N              float ID 0|1
fullscreen ID 0|1       workarea X Y W H    enter ID
activate ID             desktop N           bind LINE
```

`workarea` is the screen minus dock struts (bars). `enter` is the pointer
entering a window. `activate` and `desktop` are EWMH requests from other
programs. A `dump` replies with `workarea`, `view`, one `map` per window, and
a final `focus`.

## xwincomposer

```
xwincomposer [-l layout] [-r master%] [-n nmaster] [-g gap]
```

Handles these `bind` lines at runtime: `layout NAME|next`, `ratio +N|-N`,
`master +N|-N`, `gap +N|-N`, `zoom`.

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

- Key bindings are compiled in; there is no runtime key file.
- Only one X client can grab a given key combination.
- No click-to-focus, no window rules, no multi-monitor awareness (the screen
  is one rectangle), no mouse bindings.
- With focus following the mouse, a new window can lose focus to whichever
  window ends up under the pointer after the relayout.
- If the router restarts, restart `xwincomposer` as well (the pipeline in
  `xinitrc.example` does both). `xwinkeys` exits when nobody reads the FIFO any
  more, so restart it too.
- The router writes events to a pipe; if the composer hangs and stops reading
  for long enough, the router will block on output.
- Tested under Xvfb with test windows, including injected key presses. Real
  applications and dock struts have not been tested.
