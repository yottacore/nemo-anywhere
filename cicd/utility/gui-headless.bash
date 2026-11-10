#!/usr/bin/env bash

## The template's file-wide shellcheck header is gone: none of those rules
## fired here. A rule that needs to be off goes off at the line that
## needs it, with a reason.

##	Purpose:
##		Run / screenshot a GUI app on a private Xvfb display, never touching the
##		visible :0 session. No desktop environment is involved, so this sidesteps the
##		"XFCE won't run twice per user" limit - Xvfb is a bare in-memory framebuffer,
##		not a session. Optional standalone xfwm4 (--wm) only if an app needs a WM.
##	Syntax:
##		gui-headless.bash start [--wm]      ## bring up Xvfb (+ optional xfwm4)
##		gui-headless.bash launch <cmd...>   ## run cmd in the background on it
##		gui-headless.bash shot <out.png>    ## capture the whole virtual screen
##		gui-headless.bash status
##		gui-headless.bash stop              ## kill only what we started
##	Notes:
##		Display/size is overridable via CICD_HEADLESS_DISPLAY / CICD_HEADLESS_SIZE
##		The legacy RPD_* names still work as fallbacks.
##		A server belongs to the process that ran `start` (the calling script).
##		While that process lives, only it may stop the server, and another run's
##		`start` on the same number is refused rather than shared. Once it has
##		exited, as after a start by hand, anyone may stop the server or take it
##		over. A number some other X server has is refused outright.
##	History: At bottom of script.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -euo pipefail

display="${CICD_HEADLESS_DISPLAY:-${RPD_HEADLESS_DISPLAY:-:99}}"
size="${CICD_HEADLESS_SIZE:-${RPD_HEADLESS_SIZE:-1920x1080x24}}"
num="${display#:}"
## ${USER} is unset in a cron or ssh context, and `set -u` then reports this
## as "headless display failed to start".
run_dir="/tmp/cicd-gui-headless-${USER:-$(id -un)}"
## The name is predictable, so somebody else can get there first. Refuse anything
## we don't own, and anything that is a link to somewhere else - the pid files
## under here are read and acted on, and the auth cookie is a key to the display.
mkdir -p "$run_dir"
if [[ -L "$run_dir" || ! -d "$run_dir" || ! -O "$run_dir" ]]; then
	echo "${run_dir} isn't ours; refusing to use it" >&2
	exit 1
fi
chmod 700 "$run_dir"

## Run something on our private display. Clearing the Wayland vars matters as much
## as setting DISPLAY: winit and GTK both prefer Wayland when they see it, so on a
## Wayland session (WSLg included) the window opens on the real desktop instead and
## nothing here can find it.
onX(){ env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE DISPLAY="$display" "$@"; }

xvfb_pid="$run_dir/xvfb-${num}.pid"
wm_pid="$run_dir/wm-${num}.pid"
apps_pids="$run_dir/apps-${num}.pids"
auth="$run_dir/Xauthority-${num}"

owner_file="$run_dir/owner-${num}"
x_lock="/tmp/.X${num}-lock"

## A pid alone isn't a process: a run killed before `stop` leaves its pid file,
## and the number can come back as anything. The start time from /proc goes with
## it, so a record names one process and no other.
# shellcheck disable=2086  ## splitting $s into positional parameters is the point - field 20 is the start time
started_at() { local s; s="$(cat "/proc/$1/stat" 2>/dev/null)" || return 1; s="${s##*) }"; set -- $s; echo "${20}"; }
record() { echo "$1 $(started_at "$1")"; }
same() { local p t; read -r p t <<<"$1"; [[ -n "$t" && "$(started_at "$p" || true)" == "$t" ]]; }
alive() { [[ -f "$1" ]] && same "$(cat "$1")"; }
pid_of() { local p _; read -r p _ <"$1"; echo "$p"; }

## Who is asking: the script that ran this one.
me="$(record "$PPID")"
owned_by_other() { [[ -f "$owner_file" ]] && [[ "$(cat "$owner_file")" != "$me" ]] && same "$(cat "$owner_file")"; }

start() {
	if alive "$xvfb_pid"; then
		if owned_by_other; then
			echo "Xvfb on $display belongs to another run (pid $(pid_of "$owner_file")); not sharing it" >&2
			exit 1
		fi
		echo "$me" > "$owner_file"
		echo "Xvfb already on $display (pid $(pid_of "$xvfb_pid"))"
	else
		## Another X server on this number would reply to xdpyinfo in our place,
		## while our own Xvfb quits at once.
		local held; held="$(tr -dc '0-9' 2>/dev/null <"$x_lock" || true)"
		if [[ -n "$held" && -d "/proc/$held" ]]; then
			echo "$display is taken by another X server (pid $held)" >&2
			exit 1
		fi
		## The cookie is a key to the display; the ambient umask left it readable.
		(umask 077; : > "$auth")
		Xvfb "$display" -screen 0 "$size" -nolisten tcp -auth "$auth" \
			>"$run_dir/xvfb-${num}.log" 2>&1 &
		local pid=$!
		record "$pid" > "$xvfb_pid"
		echo "$me" > "$owner_file"
		## Up means ours is still running, has the number, and takes connections.
		local ok=""
		for _ in $(seq 1 50); do
			kill -0 "$pid" 2>/dev/null || break
			if [[ "$(tr -dc '0-9' 2>/dev/null <"$x_lock" || true)" == "$pid" ]] \
				&& DISPLAY="$display" xdpyinfo >/dev/null 2>&1; then ok=1; break; fi
			sleep 0.1
		done
		if [[ -z "$ok" ]]; then
			kill "$pid" 2>/dev/null || true
			rm -f "$xvfb_pid" "$owner_file"
			echo "Xvfb didn't come up on $display; see $run_dir/xvfb-${num}.log" >&2
			exit 1
		fi
		echo "Started Xvfb on $display (pid $pid, $size)"
	fi
	if [[ "${1:-}" == "--wm" ]] && ! alive "$wm_pid"; then
		onX xfwm4 --compositor=off >"$run_dir/wm-${num}.log" 2>&1 &
		record $! > "$wm_pid"
		echo "Started xfwm4 on $display (pid $(pid_of "$wm_pid"))"
	fi
}

launch() {
	[[ $# -gt 0 ]] || { echo "usage: launch <cmd...>" >&2; exit 2; }
	alive "$xvfb_pid" || start
	onX "$@" >"$run_dir/app-${num}.log" 2>&1 &
	record $! >> "$apps_pids"
	echo "Launched on $display (pid $!); log: $run_dir/app-${num}.log"
}

shot() {
	local out="${1:-}"
	[[ -n "$out" ]] || { echo "usage: shot <out.png>" >&2; exit 2; }
	alive "$xvfb_pid" || { echo "no Xvfb on $display - run 'start' first" >&2; exit 1; }
	import -display "$display" -window root "$out"
	echo "Wrote $out"
}

stop() {
	if alive "$xvfb_pid" && owned_by_other; then
		echo "Xvfb on $display belongs to another run (pid $(pid_of "$owner_file")); leaving it" >&2
		return 1
	fi
	local line
	if [[ -f "$apps_pids" ]]; then
		while read -r line; do same "$line" && kill "${line%% *}" 2>/dev/null || true; done < "$apps_pids"
		rm -f "$apps_pids"
	fi
	for f in "$wm_pid" "$xvfb_pid"; do
		[[ -f "$f" ]] || continue
		alive "$f" && { kill "$(pid_of "$f")" 2>/dev/null || true; }
		rm -f "$f"
	done
	rm -f "$owner_file"
	echo "Stopped headless session on $display"
}

case "${1:-}" in
	start)  shift; start "${1:-}" ;;
	launch) shift; launch "$@" ;;
	shot)   shift; shot "${1:-}" ;;
	status) alive "$xvfb_pid" && echo "Xvfb up on $display (pid $(pid_of "$xvfb_pid"))" || echo "no Xvfb on $display" ;;
	stop)   stop ;;
	*) echo "usage: gui-headless.bash {start [--wm]|launch <cmd...>|shot <out.png>|status|stop}" >&2; exit 2 ;;
esac


##	Script history:
##		- 20260701: Created.
##		- 20260917: A pid file has the start time, so a reused pid isn't
##		  ours; start refuses a number another server has and reports success
##		  only for its own; a server belongs to the run that started it.
