# xwin

A collection of lightweight, modular tools that apply Unix philosophy to tiling window management.

Small programs, plain text, one job each. Written in C89, linked only against `libxcb`.

| Program | Job |
| --- | --- |
| `xwinrouter` | Window manager mechanism. The only program with the WM role. Manages windows, applies the geometry it is told to apply, reports what happens. No layout, no keys, no focus policy. |
| `xwincomposer` | Layout and focus policy. Reads router events, writes router commands. |
| `xwinkeys` | Key bindings and **the only configuration**. Sends its settings at startup, then turns key presses into commands. |
| `xwinbar` | Status bar. Shows the last line printed by an `sh` script. |

```
xwinkeys --settings, keys--> FIFO --> xwinrouter --events--> xwincomposer
                              ^                                  |
                              +-----------commands---------------+

xwinbar <-- sh script          (independent: a dock window the router tiles around)
```

Only `xwinkeys` includes `config.h`. The router and the composer have no config file and no compile-time options. They are configured at runtime by `set` commands, so changing a setting never means rebuilding them.

## Requirements

- C compiler (C89 is enough) and `pkg-config`
- `libxcb` and its headers (the only linked library)
- `X11/keysym.h` from xorgproto, for `xwinkeys` (header only, nothing from libX11 is linked)
- a core X font for `xwinbar` (the built-in `fixed` works)

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

install -m 755 xwinrouter xwincomposer xwinkeys xwinbar /usr/local/bin/
install -d /usr/local/share/xwin
install -m 755 xwinbar.sh /usr/local/share/xwin/

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
xwinbar /usr/local/share/xwin/xwinbar.sh &

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
xwinbar ./xwinbar.sh &
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
| MOD+p | launch `dmenu` |

Desktops are 1-9 (the router supports `1..9`, there is no desktop 0).

## xwinbar

```
xwinbar [-b] [-a l|c|r] [-f font] [-B RRGGBB] [-F RRGGBB] [-p pad] COMMAND
```

Runs `COMMAND` with `sh -c` and shows the last line it prints. The command is normally a script that prints a line, sleeps, and repeats. The example `xwinbar.sh` shows date, time and battery:

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

The bar is a dock window with `_NET_WM_STRUT_PARTIAL`. The router shrinks the work area and the composer lays windows out around it. Core X fonts are Latin-1 only. If the script exits, the last line stays on screen.

Your own script is any command that prints lines: `xwinbar 'while :; do uptime; sleep 10; done'`.

## Commands

Written to the FIFO, read by `xwinrouter`. `ID` is hex such as `0x400003`, or `@` for the focused window.

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
- Tested under Xvfb with test windows, injected key presses, and the bar (struts, top and bottom, started before and after the router). Real applications have not been tested.
