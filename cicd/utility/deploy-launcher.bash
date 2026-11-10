#!/bin/bash

##	Purpose:
##		- Copies the launcher and its wrappers out of utility/ into the synced util
##		  dirs people run them from. The repo has the masters, and this stops the
##		  copies from drifting.
##		- Run from cicd.bash stage 7 (DOGFOOD_HOOK) and standalone. Only writes a
##		  destination whose parent dir already exists, so a box with a different
##		  layout is skipped rather than given a new tree it never asked for.
##	History: At bottom of script.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -euo pipefail

repoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly repoRoot
readonly srcDir="${repoRoot}/utility"

## Roots the synced 0-0 tree may sit under. Both name the same directory here; a
## box with only one of them still resolves.
readonly syncedRoots=(
	"${HOME}/synced/0-0"
	"${HOME}/.synced/Dropbox/0-0"
)

## master|destination relative to a synced root. The bash wrapper goes to 3 homes;
## the .ps1 is the one file all of them and the .cmd hand off to. The wsl copy was
## drifting because nothing kept it in step. It's the same file as the linux one.
readonly deployments=(
	"n8runfm.ps1|common/exec/util/0_crossplatform/n8runfm.ps1"
	"runfm|common/exec/util/linux/bash/runfm"
	"runfm|common/exec/util/macos/bash/runfm"
	"runfm|common/exec/util/wsl/bash/runfm"
	"runfm.cmd|common/exec/util/mswin/cli/by-self/cmd/runfm.cmd"
)

fEcho_Clean(){ echo "${@}"; }
fEcho(){       if [[ -n "${*}" ]]; then fEcho_Clean "[ ${*} ]"; else fEcho_Clean ""; fi; }

fMain(){
	local syncedRoot=""
	local root
	for root in "${syncedRoots[@]}"; do
		[[ -d "${root}" ]] && { syncedRoot="${root}"; break; }
	done
	if [[ -z "${syncedRoot}" ]]; then
		fEcho "no synced tree here (${syncedRoots[*]}); launcher not deployed"
		return 0
	fi

	local -i copied=0 skipped=0
	local entry master relative src dst
	for entry in "${deployments[@]}"; do
		master="${entry%%|*}"; relative="${entry#*|}"
		src="${srcDir}/${master}"
		dst="${syncedRoot}/${relative}"
		if [[ ! -f "${src}" ]]; then
			fEcho_Clean "  missing master: ${src}"
			skipped+=1
			continue
		fi
		if [[ ! -d "$(dirname "${dst}")" ]]; then
			fEcho_Clean "  skipped (no ${dst%/*}/): ${relative##*/}"
			skipped+=1
			continue
		fi
		if cmp -s "${src}" "${dst}"; then
			continue
		fi
		## Written alongside and moved into place: these are on PATH, and a
		## half-written script that someone runs mid-copy is worse than a stale one.
		cp -f "${src}" "${dst}.new"
		chmod 755 "${dst}.new"
		mv -f "${dst}.new" "${dst}"
		fEcho_Clean "  updated: ${dst}"
		copied+=1
	done

	if ((copied)); then fEcho "OK: deployed ${copied} launcher file(s) to ${syncedRoot}"
	else                fEcho "OK: launcher already current in ${syncedRoot}"; fi
	((skipped)) && fEcho_Clean "  (${skipped} destination(s) not present on this box)"
	return 0
}

fMain "${@}"


##	History:
##		- 2026-09-08: Keep the wsl wrapper in step too.
##		- 2026-09-07: Created.
