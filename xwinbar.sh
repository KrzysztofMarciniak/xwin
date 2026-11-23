#!/bin/sh
# xwinbar.sh - example status script for xwinbar: date, time, battery.
#
# xwinbar shows the last line this script prints, so print one line per
# update and sleep in between:
#
#     xwinbar /path/to/xwinbar.sh
#
# Edit it freely; anything that prints lines will do.

ps=${POWER_SUPPLY:-/sys/class/power_supply}

battery() {
	for b in "$ps"/BAT*; do
		[ -r "$b/capacity" ] || continue
		read -r cap < "$b/capacity"
		read -r st < "$b/status"
		case $st in
		Charging)    sign='+' ;;
		Discharging) sign='-' ;;
		Full)        sign='=' ;;
		*)           sign='' ;;
		esac
		printf 'BAT %s%%%s' "$cap" "$sign"
		return
	done
}

while :; do
	bat=$(battery)
	printf '%s  %s' "$(date '+%a %d %b')" "$(date '+%H:%M')"
	[ -n "$bat" ] && printf '  %s' "$bat"
	printf '\n'
	sleep 5
done
