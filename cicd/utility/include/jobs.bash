#!/usr/bin/env bash

##	- Purpose: The one place job counts are worked out. Half the CPUs, or a
##	  quarter on a busy box or for a hard step (sanitizers, fuzzing, the
##	  analyzers, leak checks). Busy is a 1-minute load over half the CPU count.
##	  Never less than 1.
##	- fJobs_Count <cpus> <load> [hard] -> prints the count. load as in
##	  /proc/loadavg, such as 12.34. fJobs_IsBusy <cpus> <load> is its test.
##	- fJobs [hard] -> sets and exports CICD_MAX_JOBS, and sets jobsCpus,
##	  jobsLoad and jobsBusy (0 or 1). With CICD_JOBS_PIN set, that number wins.
##	- fJobs_Default -> for a script that may run alone: CICD_MAX_JOBS as
##	  cicd.bash set it, or fJobs when it's unset.
##	- For another box, with its own CPUs and load: jobsProbe is the sh command
##	  that prints both there, a line each, on Linux or FreeBSD.
##	  fJobs_Read <cpus> <load line> sets jobsCpus, jobsLoad and jobsBusy from
##	  them, and fJobs_Count gives the count.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


jobsCpus=0; jobsLoad="0.00"; jobsBusy=0

## True when load (as in /proc/loadavg, such as 12.34) is over half of cpus.
## Compared as load * 100 against cpus * 50, so no floating point.
fJobs_IsBusy(){
	local cpus="${1:-2}" load="${2:-0}" whole frac
	whole="${load%%.*}"; frac="${load#"${whole}"}"; frac="${frac#.}00"; frac="${frac:0:2}"
	[[ "${whole}" =~ ^[0-9]+$ ]] || whole=0
	[[ "${frac}"  =~ ^[0-9]+$ ]] || frac=00
	(( 10#${whole} * 100 + 10#${frac} > cpus * 50 ))
}

fJobs_Count(){
	local cpus="${1:-2}" load="${2:-0}" hard="${3:-}" div=2
	if [[ -n "${hard}" ]] || fJobs_IsBusy "${cpus}" "${load}"; then div=4; fi
	printf '%s' "$(( cpus / div >= 1 ? cpus / div : 1 ))"
}

## sysctl first, since FreeBSD may have a /proc without loadavg.
# shellcheck disable=SC2034  ## for the scripts that source this.
jobsProbe='getconf _NPROCESSORS_ONLN; sysctl -n vm.loadavg 2>/dev/null || cat /proc/loadavg'

## The load line is /proc/loadavg's, or FreeBSD's vm.loadavg, "{ 0.12 0.30 0.25 }".
fJobs_Read(){
	local cpus="${1:-}" line="${2:-}" rest
	[[ "${cpus}" =~ ^[1-9][0-9]*$ ]] || cpus=2
	line="${line#*\{}"
	read -r jobsLoad rest <<< "${line}"
	[[ "${jobsLoad}" =~ ^[0-9]+(\.[0-9]+)?$ ]] || jobsLoad="0.00"
	jobsCpus="${cpus}"; jobsBusy=0
	# shellcheck disable=SC2034  ## jobsBusy is for the scripts that source this.
	fJobs_IsBusy "${jobsCpus}" "${jobsLoad}" && jobsBusy=1
	return 0
}

fJobs(){
	local hard="${1:-}" line=""
	if [[ -r /proc/loadavg ]]; then read -r line < /proc/loadavg
	else line="$(sysctl -n vm.loadavg 2>/dev/null)" || line=""; fi
	fJobs_Read "$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" "${line}"
	if [[ -n "${CICD_JOBS_PIN:-}" ]]; then CICD_MAX_JOBS="${CICD_JOBS_PIN}"
	else CICD_MAX_JOBS="$(fJobs_Count "${jobsCpus}" "${jobsLoad}" "${hard}")"; fi
	export CICD_MAX_JOBS
}

fJobs_Default(){ [[ -n "${CICD_MAX_JOBS:-}" ]] || fJobs "${1:-}"; }


##	History:
##		- 2026-10-25: Created, from the half-the-cores lines in cicd.bash and the
##		  scripts it runs.
##		- 2026-10-28: jobsProbe and fJobs_Read, for the lanes on other boxes.
