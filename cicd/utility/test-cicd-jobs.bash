#!/usr/bin/env bash
# shellcheck disable=SC2016  ## the patterns match literal ${...} text in the scripts.

##	- Purpose: Check the job counts. Half the CPUs, a quarter when the 1-minute
##	  load is over half the CPU count or for a hard step, never under 1. And
##	  that the scripts the pipeline runs take the count from include/jobs.bash
##	  rather than working out their own, that config.bash hands it to the
##	  containers unexpanded, so a step reads the count it was given, and that
##	  docker-run.bash passes it in under nice.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-jobs.bash
##	- Test ID: rm5cn5p8

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

# shellcheck source=include/jobs.bash
source "${here}/include/jobs.bash"

## cpus load hard -> want
while read -r cpus load hard want; do
	[[ "$hard" == - ]] && hard=""
	got="$(fJobs_Count "$cpus" "$load" "$hard")"
	[[ "$got" == "$want" ]] || fFail "fJobs_Count ${cpus} ${load} ${hard:-(not hard)}: ${got}, want ${want}"
done <<-'EOF'
	32 0.00   -    16
	32 16.00  -    16
	32 16.01  -    8
	32 35.70  -    8
	32 0.10   hard 8
	32 40.00  hard 8
	16 08.50  -    4
	16 7.99   -    8
	2  0.00   -    1
	2  5.00   -    1
	1  0.00   hard 1
	3  1.40   -    1
	32 junk   -    16
	33 0.00   -    16
EOF

## fJobs reads the box, and a pin wins over it.
unset CICD_MAX_JOBS CICD_JOBS_PIN
fJobs
[[ "$CICD_MAX_JOBS" =~ ^[1-9][0-9]*$ ]] || fFail "fJobs gave '${CICD_MAX_JOBS}'"
(( CICD_MAX_JOBS <= (jobsCpus / 2 > 1 ? jobsCpus / 2 : 1) )) || fFail "fJobs gave ${CICD_MAX_JOBS} on ${jobsCpus} cpus"
CICD_JOBS_PIN=3 fJobs hard
[[ "$CICD_MAX_JOBS" == 3 ]] || fFail "CICD_JOBS_PIN=3 gave ${CICD_MAX_JOBS}"
unset CICD_JOBS_PIN
CICD_MAX_JOBS=5; fJobs_Default
[[ "$CICD_MAX_JOBS" == 5 ]] || fFail "fJobs_Default changed a count it was given"

## Nothing under cicd/ works out its own count. cicd.bash keeps nproc for the
## run's CPU quota only.
while IFS= read -r hit; do
	case "$hit" in
		cicd/utility/include/jobs.bash:*|cicd/utility/test-cicd-jobs.bash:*) ;;
		'cicd/cicd.bash:'*'cores="$(nproc'*) ;;
		*) fFail "works out its own job count: ${hit}" ;;
	esac
done < <(cd "$root" && git ls-files -co --exclude-standard -- 'cicd/*.bash' 'cicd/**/*.bash' | sort -u \
	| xargs grep -n -E 'nproc|_NPROCESSORS|hw\.ncpu' 2>/dev/null || true)

## The container commands in config.bash read the count when they run.
while IFS= read -r hit; do
	fFail "config.bash expands CICD_MAX_JOBS when it is read, not when the step runs: ${hit}"
done < <(grep -n -E '(^|[^\\])\$\{?CICD_MAX_JOBS' "${root}/cicd/config.bash" | grep -v -E '^[0-9]+:[[:space:]]*#' || true)

## docker-run.bash hands the count across and runs the command under nice.
lastExec="$(sed -n '/^exec docker exec/,/[^\\]$/p' "${root}/cicd/utility/docker-run.bash")"
[[ "$lastExec" == *'-e "CICD_MAX_JOBS=${CICD_MAX_JOBS}"'* ]] || fFail "docker-run.bash does not pass CICD_MAX_JOBS into the container"
[[ "$lastExec" == *'nice -n 19'* ]] || fFail "docker-run.bash does not run the command under nice"

if ((failures)); then
	fEcho "FAILED: job counts, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: half the cpus, a quarter when busy or hard, never 0; one place works them out, and the containers read them per step under nice"


##	History:
##		- 2026-10-25: Created.
