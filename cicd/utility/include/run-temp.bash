#!/usr/bin/env bash

##	- Purpose: The run folder every pipeline run works in, and the guard every
##	  recursive removal goes through.
##	- fRunTemp_Make <project> <YYYYmmDD-HHMMSS> -> makes
##	  ${HOST_TEMP}/<project>/<stamp>_<NNN>/, writes run.pid there, and points
##	  TMPDIR, TEMP and TMP at it. Sets runTempDir, and exports CICD_PROJECT
##	  and CICD_RUN (the folder's name) for docker labels, and CICD_KEEP_TEMP
##	  (runTempKeep) so a step's container stays too. HOST_TEMP falls back
##	  to TMPDIR, then /tmp.
##	- fRunTemp_Sweep -> removes the project's run folders older than 2 days
##	  whose run.pid names no live process. A folder with no run.pid wasn't made
##	  by a run, so it stays.
##	- fRunTemp_Done -> for the EXIT trap. Removes the run folder, or with
##	  runTempKeep=1 keeps it and says where it is.
##	- fSafeRm <path> -> rm -rf of one absolute path under a folder named tmp, or
##	  under ${HOST_TEMP}/<project>/. Refuses .., //, a newline, a link, a path
##	  through a link, or one that isn't there. Stays on one file system.
##	  The project is projectName, or CICD_PROJECT in a step's child.
##	- fSafeRm_IfThere <path>... -> fSafeRm of each one that's there, with no
##	  "removed" line. For a script's scratch dir and the paths in it.
##	- fSafeRm_Under <parent> <path>... -> the same for a folder outside a temp
##	  root, such as a build or artifact folder: each path strictly under the
##	  one parent named, with no link between them.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


runTempDir=""
runTempKeep=0

fSafeRm(){
	local p="${1:-}" real='' own="${projectName:-${CICD_PROJECT:-}}"
	## Outside a run there's no project root, so only the tmp folder rule holds.
	if [[ -n "${HOST_TEMP:-}" && -n "${own}" ]]; then own="${HOST_TEMP}/${own}"; else own=""; fi
	if [[ "${#}" -ne 1 || -z "${p}" || "${p}" == *..* || "${p}" == *//* || "${p}" == *$'\n'* ]]; then
		printf 'safe-rm: refused, bad path: "%s"\n' "${p}" >&2; return 1
	fi
	p="${p%/}"
	if ! [[ "${p}" =~ ^/(.+/)?tmp/[^/]+(/[^/]+)*$ || ( -n "${own}" && "${p}" == "${own}"/?* ) ]]; then
		printf 'safe-rm: refused, not under a tmp folder: %s\n' "${p}" >&2; return 1
	fi
	if [[ -L "${p}" ]]; then printf 'safe-rm: refused, a link: %s\n' "${p}" >&2; return 1; fi
	if [[ ! -e "${p}" ]] || ! real="$(realpath -- "${p}" 2> /dev/null)"; then printf 'safe-rm: refused, not there: %s\n' "${p}" >&2; return 1; fi
	if [[ "${real}" != "${p}" ]]; then printf 'safe-rm: refused, goes through a link: %s\n' "${p}" >&2; return 1; fi
	fSafeRm__Rm "${p}" && printf 'removed %s\n' "${p}"
}

## FreeBSD and macOS have neither realpath -e nor rm --one-file-system; their
## rm -x is the same thing.
fSafeRm__Rm(){
	if [[ -z "${__safeRmGnu:-}" ]]; then if rm --version > /dev/null 2>&1; then __safeRmGnu=1; else __safeRmGnu=0; fi; fi
	if ((__safeRmGnu)); then rm -rf --one-file-system -- "$@"; else rm -rfx -- "$@"; fi
}

## For a removal outside a temp root, such as a build or artifact folder: each
## path must sit strictly under <parent>, with no link between the two. The
## parent itself may sit behind one. Quiet, and one that's gone is skipped, as
## with rm -rf. Returns 1 if any was refused.
fSafeRm_Under(){
	local parent="${1:-}" p real realParent='' rc=0
	(($#)) && shift
	parent="${parent%/}"
	if [[ "${parent}" != /?* || "${parent}" == *..* || "${parent}" == *//* || "${parent}" == *$'\n'* ]]; then
		printf 'safe-rm: refused, bad parent: "%s"\n' "${parent}" >&2; return 1
	fi
	for p in "$@"; do
		p="${p%/}"
		if [[ "${p}" != "${parent}"/?* || "${p}" == *..* || "${p}" == *//* || "${p}" == *$'\n'* ]]; then
			printf 'safe-rm: refused, not under %s: "%s"\n' "${parent}" "${p}" >&2; rc=1; continue
		fi
		if [[ -L "${p}" ]]; then printf 'safe-rm: refused, a link: %s\n' "${p}" >&2; rc=1; continue; fi
		[[ -e "${p}" ]] || continue
		if [[ -z "${realParent}" ]] && ! realParent="$(realpath -- "${parent}" 2> /dev/null)"; then
			printf 'safe-rm: refused, no parent: %s\n' "${parent}" >&2; return 1
		fi
		real="$(realpath -- "${p}" 2> /dev/null)" || real=''
		if [[ "${real}" != "${realParent%/}${p#"${parent}"}" ]]; then
			printf 'safe-rm: refused, goes through a link: %s\n' "${p}" >&2; rc=1; continue
		fi
		fSafeRm__Rm "${p}" || rc=1
	done
	return "${rc}"
}

## For a script's own scratch: each path through fSafeRm, quietly, and one
## that's already gone is fine, as with rm -rf. Returns 1 if any was refused.
fSafeRm_IfThere(){
	local p rc=0
	for p in "$@"; do
		[[ -e "${p}" || -L "${p}" ]] || continue
		fSafeRm "${p}" > /dev/null || rc=1
	done
	return "${rc}"
}

fRunTemp_Make(){
	local stamp="${2:?}" base dir n=1
	projectName="${1:?}"
	## Resolved once, since macOS's temp folder sits behind the /var link and
	## the guard compares real paths. No realpath -e, which FreeBSD lacks.
	HOST_TEMP="${HOST_TEMP:-${TMPDIR:-/tmp}}"
	[[ -d "${HOST_TEMP}" ]] || return 1
	HOST_TEMP="$(realpath -- "${HOST_TEMP}")" || return 1
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
	export CICD_PROJECT="${projectName}" CICD_RUN="${dir##*/}" CICD_KEEP_TEMP="${runTempKeep:-0}"
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
##		- 2026-10-26: Exports CICD_KEEP_TEMP.
##		- 2026-10-26: fSafeRm_IfThere; fSafeRm works outside a run.
##		- 2026-11-03: fSafeRm_Under; works on FreeBSD and macOS.
##		- 2026-11-04: fRunTemp_Make works on FreeBSD, for the lanes' run folder
##		  there.
