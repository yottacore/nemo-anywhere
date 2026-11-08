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
##	- fBoxWrap_ClockReady <target> [ssh opt]... -> 0 once the box's clock is
##	  within BOX_CLOCK_MAX_SKEW seconds of this one's (default 5), 1 if it isn't
##	  by BOX_CLOCK_WAIT seconds (default 60). A guest resumed from paused keeps
##	  the clock it stopped with, which puts every file sent from here in its
##	  future, and meson stops on that. One that's ahead would see old objects as
##	  newer than the sources. The VM state helper sets the clock as it wakes a
##	  guest; this catches the cases it can't, or the bare host lock.
##	- fBoxWrap_IsThere <name> <target> <tool> [ssh opt]... -> 1 when the box
##	  isn't there: ssh gets no answer at all, and it's neither a paused or off
##	  VM the VM state helper <tool> can wake, nor held by another session in
##	  the host lock. So a VM that's running but has no network is a skip in
##	  seconds, not a 10 min wait. A login or command that fails still counts as
##	  there, so the lane fails on it rather than skipping.
##	- fBoxWrap_Jobs <pin> <target> [ssh opt]... -> sets boxJobs, the build and
##	  test jobs for that box, from its own CPU count and load by the rules in
##	  jobs.bash, or <pin> when that's set, and prints the count the way
##	  cicd.bash does for a step. 1 when the box won't say.
##	- fBoxWrap_RunDir <project> <target> [ssh opt]... -> makes the run's
##	  folder on the box, <temp>/<project>/<YYYYmmDD-HHMMSS>_<NNN>/ with a
##	  run.pid, and sweeps the project's stale ones there, the way a run here
##	  does (run-temp.bash). <temp> is the box's HOST_TEMP, then TMPDIR, then
##	  /tmp. Sets boxRunDir, and prints it. 1 when the box won't make one.
##	- fBoxWrap_RunDirDone <target> [ssh opt]... -> for the EXIT trap: removes
##	  boxRunDir there through the guard, or with CICD_KEEP_TEMP=1 keeps it and
##	  says where it is. Run once; a second call does nothing.
##	- fBoxWrap_RunCmd -> prints the start of a command to run in boxRunDir/src,
##	  with TMPDIR, TEMP and TMP pointed at boxRunDir and run.pid naming the
##	  process, which the words after it replace.
##	- run-temp.bash goes over on stdin for both ends, since the tree isn't
##	  there yet when the folder is made, and may never get there.
##	- Syntax: source this file; it defines functions, boxJobs and boxRunDir
##	  only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

# shellcheck source=jobs.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/jobs.bash"

boxJobs=""
boxRunDir=""
__boxWrapHere="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fBoxWrap_Tool(){
	local dir="${HOME}/synced/0-0/common/exec/util/linux/bash" tool
	if [[ -n "${BOX_WRAP_TOOL:-}" ]]; then echo "$BOX_WRAP_TOOL"; return; fi
	if [[ -n "${WINDOWS_HOST_LOCK:-}" ]]; then echo "$WINDOWS_HOST_LOCK"; return; fi
	for tool in "$dir"/*vm-state.bash "$dir"/*windows-host-lock.bash; do
		if [[ -f "$tool" ]]; then echo "$tool"; return; fi
	done
	echo "$tool"
}

## Held by some other session. A box mid-reboot under someone else is waited for, not skipped.
fBoxWrap_HeldElsewhere(){
	local name="$1" tool="$2" lock='' st=''
	if [[ "$tool" == *windows-host-lock.bash ]]; then lock="$tool"
	else for lock in "${tool%/*}"/*windows-host-lock.bash; do break; done; fi
	[[ -f "$lock" ]] || return 1
	bash "$lock" check "$name" > /dev/null 2>&1 && return 1
	st="$(bash "$lock" status 2> /dev/null | awk -v h="$name" '$1 == h {print $2; exit}')" || true
	[[ "$st" == held ]]
}

fBoxWrap_IsThere(){
	local name="$1" target="$2" tool="$3" err='' st=''
	shift 3
	if [[ "$tool" == *vm-state.bash && -f "$tool" ]]; then
		st="$(bash "$tool" status "$name" 2> /dev/null | awk -v h="$name" '$1 == h {print $3}')" || true
		## Paused or off gets woken by the helper. Running has to answer now.
		[[ -z "$st" || "$st" == - || "$st" == running ]] || return 0
	fi
	fBoxWrap_HeldElsewhere "$name" "$tool" && return 0
	## The first ConnectTimeout given wins, so a paused box isn't waited on long.
	err="$(ssh -o ConnectTimeout=8 "$@" "$target" exit 2>&1 > /dev/null < /dev/null)" && return 0
	case "$err" in
		*"Could not resolve hostname"*|*"timed out"*|*"No route to host"*|*"Connection refused"*|*"Network is unreachable"*)
			fEcho_Clean "${err}"; return 1 ;;
	esac
	return 0
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

fBoxWrap_ClockReady(){
	local target="$1" theirs='' how=''
	local -i maxSkew="${BOX_CLOCK_MAX_SKEW:-5}" waitSec="${BOX_CLOCK_WAIT:-60}" poll="${BOX_READY_POLL:-5}" started=$SECONDS said=0 t0 skew=0
	shift
	while :; do
		t0="$(date +%s)"
		theirs="$(ssh "$@" "$target" 'date +%s' 2> /dev/null < /dev/null)" || theirs=''
		if [[ "$theirs" =~ ^[0-9]+$ ]]; then
			## Against the middle of the round trip, since a cold ssh takes a second or two.
			skew=$(( (2 * theirs - t0 - $(date +%s)) / 2 ))
			if (( skew <= maxSkew && skew >= -maxSkew )); then
				((said)) && fEcho_Clean "${target} clock is right after $((SECONDS - started))s"
				return 0
			fi
			if ((skew < 0)); then how="$((-skew))s behind"; else how="${skew}s ahead"; fi
		else
			how="unreadable"
		fi
		if ((SECONDS - started + poll > waitSec)); then
			fEcho_Clean "${target} clock is ${how}, still after ${waitSec}s"
			return 1
		fi
		((said)) || fEcho_Clean "${target} clock is ${how}, waiting up to ${waitSec}s for it"
		said=1
		sleep "$poll"
	done
}

## The probe runs there.
# shellcheck disable=SC2029
fBoxWrap_Jobs(){
	local pin="$1" target="$2" out='' cpus='' line='' why=''
	shift 2
	[[ -z "${pin}" || "${pin}" =~ ^[1-9][0-9]*$ ]] || { fEcho_Clean "the jobs pin for ${target} is not a count: ${pin}"; return 1; }
	out="$(ssh "$@" "$target" "${jobsProbe}" 2> /dev/null < /dev/null)" || out=''
	{ read -r cpus; read -r line; } <<< "${out}" || true
	[[ "${cpus}" =~ ^[1-9][0-9]*$ && -n "${line}" ]] || { fEcho_Clean "${target} did not say its CPU count and load"; return 1; }
	fJobs_Read "${cpus}" "${line}"
	if [[ -n "${pin}" ]]; then boxJobs="${pin}"; why="pinned, "
	else boxJobs="$(fJobs_Count "${jobsCpus}" "${jobsLoad}")"; ((jobsBusy)) && why="busy box, "; fi
	fEcho_Clean "jobs ${boxJobs} (${why}load ${jobsLoad} on ${jobsCpus} cpus on ${target})"
}

## The script is put together here, to run there.
# shellcheck disable=SC2029,SC2016
fBoxWrap_RunDir(){
	local project="$1" target="$2" out='' line
	shift 2
	boxRunDir=""
	out="$({ cat -- "${__boxWrapHere}/run-temp.bash"
		## The box's own clock names it, since its sweep reads the name by that clock.
		printf 'fRunTemp_Make %q "$(date +%%Y%%m%%d-%%H%%M%%S)" > /dev/null || exit 1\n' "${project}"
		printf 'fRunTemp_Sweep\nprintf "dir=%%s\\n" "${runTempDir}"\n'
	} | ssh "$@" "$target" 'bash -s' 2>&1)" || { [[ -z "${out}" ]] || fEcho_Clean "${out}"; return 1; }
	while IFS= read -r line; do
		case "${line}" in
			dir=*) boxRunDir="${line#dir=}" ;;
			removed\ *) fEcho_Clean "removed stale ${target}:${line#removed }" ;;
			*) [[ -z "${line}" ]] || fEcho_Clean "${line}" ;;
		esac
	done <<< "${out}"
	## Every remote command takes it bare, so nothing that needs quoting.
	if ! [[ "${boxRunDir}" =~ ^/[A-Za-z0-9._/-]+/${project}/[0-9]{8}-[0-9]{6}_[0-9]{3}$ ]]; then
		fEcho_Clean "${target} gave an odd run folder: ${boxRunDir}"; boxRunDir=""; return 1
	fi
	fEcho_Clean "work folder ${target}:${boxRunDir}"
}

# shellcheck disable=SC2029
fBoxWrap_RunDirDone(){
	local target="$1" dir="${boxRunDir}" base
	shift
	[[ -n "${dir}" ]] || return 0
	boxRunDir=""
	if [[ "${CICD_KEEP_TEMP:-0}" == 1 ]]; then fEcho_Clean "[ kept the run folder: ${target}:${dir} ]"; return 0; fi
	base="${dir%/*}"
	if { cat -- "${__boxWrapHere}/run-temp.bash"
		printf 'HOST_TEMP=%q projectName=%q\nfSafeRm %q > /dev/null\n' "${base%/*}" "${base##*/}" "${dir}"
	} | ssh "$@" "$target" 'bash -s'; then
		return 0
	fi
	fEcho_Clean "[ WARNING: could not remove the run folder ${target}:${dir} ]"
	return 1
}

fBoxWrap_RunCmd(){
	local d="${boxRunDir:?}"
	printf 'cd %s/src && echo $$ > %s/run.pid && exec env TMPDIR=%s TEMP=%s TMP=%s' "$d" "$d" "$d" "$d" "$d"
}


##	History:
##		- 2026-10-27: Created.
##		- 2026-10-28: fBoxWrap_WaitReady, so a lane waits for its own login
##		  after a boot. fBoxWrap_IsThere, so a box that isn't there is a skip.
##		  fBoxWrap_ClockReady, since a resumed guest keeps its old clock.
##		- 2026-10-28: fBoxWrap_Jobs, so a lane works out its box's jobs there.
##		- 2026-10-31: A running VM that doesn't answer, and that nobody else
##		  holds, isn't there. vmFreeBSD cost 2 10-minute waits that way.
##		- 2026-11-04: fBoxWrap_RunDir, fBoxWrap_RunDirDone and fBoxWrap_RunCmd,
##		  so a lane works in a run folder under the box's temp root.
