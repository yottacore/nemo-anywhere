#!/usr/bin/env bash

##	- Purpose: A container of a step's own, named <project>-<run folder>-<step>,
##	  labeled, and gone when the step ends, unless --keep-temp (CICD_KEEP_TEMP=1).
##	  docker-run.bash runs one command in one. A script that runs several in a
##	  row starts one with fStepBox_Start and stops it from its EXIT trap.
##	- fStepBox_Name <label> -> a free name for the step. Outside a run there's
##	  no run folder, so the pid keeps 2 apart. A kept container or a step run
##	  twice takes the next number.
##	- fStepBox_Volume <volume> [label] -> makes the cache volume for this clone, or
##	  checks it's this clone's. 1 with one line when another clone has it, 2 when
##	  it can't be made.
##	- fStepBox_Start <label> <image> [docker run args]... -> starts it detached,
##	  with the step's CPU cap and share, and sets stepBox to its name.
##	- fStepBox_Stop -> for the EXIT trap. --rm makes the stop a removal.
##	- fStepBox_ImageTag <repository> <recipe file>... -> <repository>:<hash>, the
##	  hash over the recipe and the files it copies in, so an edit gives a new tag.
##	- Backlog 2026102617253209, 2026102617253214 and 2026102617253220.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

# shellcheck source=jobs.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/jobs.bash"

stepBox=""

fStepBox_Name(){
	local step name base names n=1
	step="${1,,}"; step="${step//[^a-z0-9]/-}"
	name="${CICD_PROJECT:-nemo-anywhere}-${CICD_RUN:-none}-${step}"
	[[ "${CICD_RUN:-none}" != none ]] || name+="-$$"
	names=$'\n'"$(docker ps -a --format '{{.Names}}' 2> /dev/null || true)"$'\n'
	base="${name}"
	while [[ "${names}" == *$'\n'"${name}"$'\n'* ]]; do n=$((n + 1)); name="${base}-${n}"; done
	printf '%s\n' "${name}"
}

## Each cache volume belongs to the clone that made it. Used from any other,
## such as a second worktree, the step would build on that clone's objects.
fStepBox_Volume(){
	local vol="$1" label="${2:+${2}: }" src here
	here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd -P)"
	if src="$(docker volume inspect "${vol}" --format '{{index .Labels "dev.src"}}' 2> /dev/null)"; then
		[[ "${src}" == "${here}" ]] && return 0
		echo "[ FAILED: ${label}${vol} builds ${src:-another clone}, not ${here} (docker volume rm ${vol} hands it over) ]" >&2
		return 1
	fi
	docker volume create --label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" --label "dev.src=${here}" "${vol}" > /dev/null || return 2
}

fStepBox_Start(){
	local label="$1" image="$2" keep=(--rm); shift 2
	[[ "${CICD_KEEP_TEMP:-0}" != 1 ]] || keep=()
	fJobs_Default
	stepBox="$(fStepBox_Name "${label}")"
	## --init reaps orphans (wine leaves plenty); --ulimit core=0 keeps crash
	## dumps out of the mounted tree.
	docker run -d "${keep[@]}" --init --ulimit core=0 --shm-size=2g --cpus "${CICD_MAX_JOBS}" --cpu-shares 256 --name "${stepBox}" \
		--label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" \
		"$@" "${image}" sleep infinity > /dev/null || { stepBox=""; return 1; }
}

fStepBox_Stop(){
	[[ -n "${stepBox}" ]] || return 0
	docker stop -t 2 "${stepBox}" > /dev/null 2>&1 || true
	stepBox=""
}


fStepBox_ImageTag(){
	local repository="$1" hash; shift
	hash="$(cat -- "$@" | sha256sum)" || return 1
	printf '%s:%s\n' "${repository}" "${hash:0:12}"
}

##	History:
##		- 2026-10-26: Created, from docker-run.bash.
##		- 2026-10-28: fStepBox_ImageTag, for images built from a pinned recipe.
