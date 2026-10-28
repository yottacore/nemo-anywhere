#!/usr/bin/env bash

##	- Purpose: Finds the tool a lane runs through to hold a shared test box.
##	  The VM state helper is first: it takes the host lock, starts or resumes
##	  a VM that is off or paused, waits for its ssh, and puts it back after.
##	  The bare host lock is next, which only takes the lock. Both take
##	  "wrap <box> --wait <sec> -- <command>".
##	- Each is found by the tail of its name, which is the part that stays put.
##	  BOX_WRAP_TOOL names the tool outright, and WINDOWS_HOST_LOCK names a
##	  bare host lock.
##	- fBoxWrap_Tool -> prints the path. The caller checks it with -f, since
##	  neither tool may be on this box.
##	- fBoxWrap_WaitReady <target> <sec> <probe> [ssh opt]... -> 0 once <probe>
##	  exits 0 there, 3 at once when it exits 3 (the wrong box), 1 when <sec> runs
##	  out or the name doesn't resolve. The helper's own wait is for ssh as the
##	  person running it, which can answer while the lane's user can't log in
##	  yet, or before what the lane needs there, such as docker, is up.
##	  BOX_READY_POLL is the seconds between tries (default 5).
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

fBoxWrap_Tool(){
	local dir="${HOME}/synced/0-0/common/exec/util/linux/bash" tool
	if [[ -n "${BOX_WRAP_TOOL:-}" ]]; then echo "$BOX_WRAP_TOOL"; return; fi
	if [[ -n "${WINDOWS_HOST_LOCK:-}" ]]; then echo "$WINDOWS_HOST_LOCK"; return; fi
	for tool in "$dir"/*vm-state.bash "$dir"/*windows-host-lock.bash; do
		if [[ -f "$tool" ]]; then echo "$tool"; return; fi
	done
	echo "$tool"
}

## The probe is put together by the caller, to run there.
# shellcheck disable=SC2029
fBoxWrap_WaitReady(){
	local target="$1" probe="$3" err='' rc=0
	local -i waitSec="$2" started=$SECONDS poll="${BOX_READY_POLL:-5}" said=0
	shift 3
	while :; do
		rc=0; err="$(ssh "$@" "$target" "$probe" 2>&1 > /dev/null < /dev/null)" || rc=$?
		case "$rc" in
			0) ((said)) && fEcho_Clean "${target} answered after $((SECONDS - started))s"; return 0 ;;
			3) return 3 ;;
		esac
		if [[ "$err" == *"Could not resolve hostname"* ]] || ((SECONDS - started + poll > waitSec)); then
			[[ -z "$err" ]] || fEcho_Clean "${err}"
			return 1
		fi
		((said)) || fEcho_Clean "waiting up to ${waitSec}s for ${target} to answer"
		said=1
		sleep "$poll"
	done
}


##	History:
##		- 2026-10-27: Created.
##		- 2026-10-28: fBoxWrap_WaitReady, so a lane waits for its own login
##		  after a boot.
