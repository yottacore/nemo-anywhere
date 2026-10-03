#!/usr/bin/env bash

##	- Purpose: Start a private Xvfb on a display number proven to be ours.
##	  Xvfb on a number another server holds quits at once, and xdpyinfo then
##	  answers from that other server. So a number counts as ours only when
##	  nothing answered on it first, the X lock names the server started here,
##	  and that server answers while it is still alive.
##	- fXvfbStart <first> <screen> <log> [Xvfb args...] -> tries <first> (":N" or
##	  "N") and the 19 numbers after it. Sets XVFB_DISPLAY to ":N" and XVFB_PID,
##	  or returns 1 when none of them was free. XVFB_PID is set while a server is
##	  still being tried, so an exit trap that kills it leaves nothing behind.
##	- :88 to :99 are claimed by other tooling on the build box, so callers
##	  start above them.
##	- Needs Xvfb and xdpyinfo.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fXvfbStart(){
	local first="${1:-}" screen="${2:?}" log="${3:?}"
	shift 3
	XVFB_DISPLAY=""; XVFB_PID=""
	first="${first#:}"
	## It goes into arithmetic below, which would run a $(...) hidden in a subscript.
	[[ "${first}" =~ ^[0-9]+$ ]] || return 1
	first=$((10#${first}))
	local num
	for ((num = first; num < first + 20; num++)); do
		[[ -e "/tmp/.X${num}-lock" ]] && continue
		DISPLAY=":${num}" xdpyinfo >/dev/null 2>&1 && continue
		Xvfb ":${num}" -screen 0 "${screen}" -nolisten tcp "$@" >"${log}" 2>&1 &
		XVFB_PID=$!
		for _ in {1..50}; do
			kill -0 "${XVFB_PID}" 2>/dev/null || break
			if [[ "$(tr -dc '0-9' 2>/dev/null <"/tmp/.X${num}-lock" || true)" == "${XVFB_PID}" ]] \
				&& DISPLAY=":${num}" xdpyinfo >/dev/null 2>&1; then
				# shellcheck disable=SC2034  ## read by the caller
				XVFB_DISPLAY=":${num}"
				return 0
			fi
			sleep 0.1
		done
		kill "${XVFB_PID}" 2>/dev/null || true
		XVFB_PID=""
	done
	return 1
}
