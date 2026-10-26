#!/usr/bin/env bash

##	- Purpose: The run folder every pipeline run works in, and the guard every
##	  recursive removal goes through.
##	- fRunTemp_Make <project> <YYYYmmDD-HHMMSS> -> makes
##	  ${HOST_TEMP}/<project>/<stamp>_<NNN>/, writes run.pid there, and points
##	  TMPDIR, TEMP and TMP at it. Sets runTempDir, and exports CICD_PROJECT
##	  and CICD_RUN (the folder's name) for docker labels. HOST_TEMP falls back
##	  to TMPDIR, then /tmp.
##	- fRunTemp_Sweep -> removes the project's run folders older than 2 days
##	  whose run.pid names no live process. A folder with no run.pid wasn't made
##	  by a run, so it stays.
##	- fRunTemp_Done -> for the EXIT trap. Removes the run folder, or with
##	  runTempKeep=1 keeps it and says where it is.
##	- fSafeRm <path> -> rm -rf of one absolute path under a folder named tmp, or
##	  under ${HOST_TEMP}/<project>/. Refuses .., //, a newline, a link, a path
##	  through a link, or one that isn't there. Stays on one file system.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


runTempDir=""
runTempKeep=0

fSafeRm(){
	local p="${1:-}" real='' own="${HOST_TEMP:?}/${projectName:?}"
	if [[ "${#}" -ne 1 || -z "${p}" || "${p}" == *..* || "${p}" == *//* || "${p}" == *$'\n'* ]]; then
		printf 'safe-rm: refused, bad path: "%s"\n' "${p}" >&2; return 1
	fi
	p="${p%/}"
	if ! [[ "${p}" =~ ^/(.+/)?tmp/[^/]+(/[^/]+)*$ || "${p}" == "${own}"/?* ]]; then
		printf 'safe-rm: refused, not under a tmp folder: %s\n' "${p}" >&2; return 1
	fi
	if [[ -L "${p}" ]]; then printf 'safe-rm: refused, a link: %s\n' "${p}" >&2; return 1; fi
	real="$(realpath -e -- "${p}" 2> /dev/null)" || { printf 'safe-rm: refused, not there: %s\n' "${p}" >&2; return 1; }
	if [[ "${real}" != "${p}" ]]; then printf 'safe-rm: refused, goes through a link: %s\n' "${p}" >&2; return 1; fi
	rm -rf --one-file-system -- "${p}" && printf 'removed %s\n' "${p}"
}

fRunTemp_Make(){
	local stamp="${2:?}" base dir n=1
	projectName="${1:?}"
	## Resolved once, since macOS's temp folder sits behind the /var link and
	## the guard compares real paths.
	HOST_TEMP="$(realpath -e -- "${HOST_TEMP:-${TMPDIR:-/tmp}}")" || return 1
	export HOST_TEMP
	base="${HOST_TEMP}/${projectName}"
	mkdir -p -- "${base}" || return 1
	## A plain mkdir, so 2 runs started in the same second can't share one.
	while :; do
		printf -v dir '%s/%s_%03d' "${base}" "${stamp}" "${n}"
		mkdir -- "${dir}" 2> /dev/null && break
		[[ -e "${dir}" ]] && ((n < 999)) || return 1
		n=$((n + 1))
	done
	printf '%s\n' "$$" > "${dir}/run.pid" || return 1
	runTempDir="${dir}"
	export TMPDIR="${dir}" TEMP="${dir}" TMP="${dir}"
	export CICD_PROJECT="${projectName}" CICD_RUN="${dir##*/}"
}

fRunTemp_Sweep(){
	local base="${HOST_TEMP:?}/${projectName:?}" cutoff dir name pid
	cutoff="$(date -d '2 days ago' +%Y%m%d-%H%M%S 2> /dev/null || date -v-2d +%Y%m%d-%H%M%S)" || return 0
	for dir in "${base}"/[0-9]*_[0-9][0-9][0-9]; do
		[[ -d "${dir}" && ! -L "${dir}" && -f "${dir}/run.pid" && "${dir}" != "${runTempDir}" ]] || continue
		name="${dir##*/}"
		[[ "${name}" =~ ^[0-9]{8}-[0-9]{6}_[0-9]{3}$ && "${name%_*}" < "${cutoff}" ]] || continue
		pid="$(< "${dir}/run.pid")" 2> /dev/null || continue
		[[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2> /dev/null && continue
		fSafeRm "${dir}" || true
	done
}

fRunTemp_Done(){
	[[ -n "${runTempDir}" && -d "${runTempDir}" ]] || return 0
	if ((runTempKeep)); then
		printf '[ kept the run folder: %s ]\n' "${runTempDir}"
	elif ! fSafeRm "${runTempDir}" > /dev/null; then
		printf '[ WARNING: could not remove the run folder %s ]\n' "${runTempDir}" >&2
	fi
}


##	History:
##		- 2026-10-25: Created.
