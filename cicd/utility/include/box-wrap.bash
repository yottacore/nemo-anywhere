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


##	History:
##		- 2026-10-27: Created.
