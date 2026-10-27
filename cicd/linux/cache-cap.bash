#!/usr/bin/env bash

##	- Purpose: Hold each cache dir under its cap. One that has grown past it is
##	  emptied, with one line saying so, and the step builds it again from scratch.
##	- Runs in the step's container before the step, from docker-run.bash, over
##	  the cache volumes it mounts.
##	- CICD_CACHE_CAP_KB sets the cap, 2 GB by default.
##	- Syntax: cache-cap.bash <dir>...

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

cap="${CICD_CACHE_CAP_KB:-1953125}"
[[ "${cap}" =~ ^[0-9]+$ ]] || { echo "[ cache-cap: not a size in KB: ${cap} ]" >&2; exit 1; }

for dir in "$@"; do
	## Only a whole absolute dir, never / itself.
	if [[ "${dir}" != /?* || "${dir}" == *..* || ! -d "${dir}" || -L "${dir}" ]]; then continue; fi
	used="$(du -sxk -- "${dir}" 2> /dev/null | cut -f1)" || used=""
	[[ "${used}" =~ ^[0-9]+$ ]] || continue
	if ((used > cap)); then
		echo "[ ${dir} cache was $((used / 1024)) MB, past its $((cap / 1024)) MB cap, so it starts empty ]" >&2
		find "${dir}" -xdev -mindepth 1 -delete
	fi
done


##	History:
##		- 2026-10-26: Created.
