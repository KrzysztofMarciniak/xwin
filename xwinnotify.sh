#!/bin/sh
# xwinnotify - show a message in xwinbar for a few seconds.
#
#     xwinnotify "build finished"
#     xwinnotify                      # no arguments: clear the notification
#
# Writes "!message" to the bar's FIFO. Same path as xwinbar's default; set
# XWINBAR_FIFO if you started the bar with -i.

fifo=${XWINBAR_FIFO:-${XDG_RUNTIME_DIR:-/tmp}/xwinbar.fifo}
[ -p "$fifo" ] || { echo "xwinnotify: no fifo at $fifo (is xwinbar running?)" >&2; exit 1; }
# 1<> opens read-write, so this never blocks waiting for a reader
if [ $# -gt 0 ]; then msg="!$*"; else msg='!'; fi
printf '%s\n' "$msg" 1<>"$fifo"
