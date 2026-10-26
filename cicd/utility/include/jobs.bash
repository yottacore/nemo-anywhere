#!/usr/bin/env bash
# shellcheck disable=SC2034  ## jobsCpus, jobsLoad and jobsBusy are for the scripts that source this.

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

fJobs(){
	local hard="${1:-}" rest
	jobsCpus="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
	jobsLoad="0.00"
	if [[ -r /proc/loadavg ]]; then read -r jobsLoad rest < /proc/loadavg
	elif rest="$(sysctl -n vm.loadavg 2>/dev/null)"; then rest="${rest#\{ }"; jobsLoad="${rest%% *}"
	fi
	jobsBusy=0; fJobs_IsBusy "${jobsCpus}" "${jobsLoad}" && jobsBusy=1
	if [[ -n "${CICD_JOBS_PIN:-}" ]]; then CICD_MAX_JOBS="${CICD_JOBS_PIN}"
	else CICD_MAX_JOBS="$(fJobs_Count "${jobsCpus}" "${jobsLoad}" "${hard}")"; fi
	export CICD_MAX_JOBS
}

fJobs_Default(){ [[ -n "${CICD_MAX_JOBS:-}" ]] || fJobs "${1:-}"; }


##	History:
##		- 2026-10-25: Created, from the half-the-cores lines in cicd.bash and the
##		  scripts it runs.
