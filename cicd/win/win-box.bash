#!/usr/bin/env bash
# shellcheck disable=SC2034  ## CONTAINER is for the scripts that source this.

##	- Purpose: The Windows cross build's step container: one of its own for each
##	  step, from the cross-build image, with the repo at /src and the cross
##	  build dir at /build-win in the cache volume <project>-build-win. Named,
##	  labeled and removed the way docker-run.bash's are (utility/include/step-box.bash).
##	- fWinBox_Image -> the image a step would run, or 1 when there's none yet.
##	  NEMO_WIN_IMAGE names one; by default nemo-winbuild:init, or a fresh
##	  clone's nemo-winbuild-deps:latest.
##	- fWinBox_Start <label> [--no-build-win] -> starts it and sets CONTAINER.
##	  Builds nemo-winbuild-deps:latest from cicd/win/Dockerfile on first use.
##	  --no-build-win leaves the cache volume out, for a step that reads only
##	  the packed artifacts. Says what's wrong in one line and returns 1.
##	  Stop it with fStepBox_Stop from the EXIT trap.
##	- No host fallback: the cross build and what reads it keep to their
##	  containers (2026102617145688).
##	- Backlog 2026102617253214.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

# shellcheck source=../utility/include/step-box.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../utility/include" && pwd)/step-box.bash"

CONTAINER=""

fWinBox_Image(){
	local image
	for image in "${NEMO_WIN_IMAGE:-nemo-winbuild:init}" nemo-winbuild-deps:latest; do
		if docker image inspect "${image}" > /dev/null 2>&1; then printf '%s\n' "${image}"; return 0; fi
		[[ -z "${NEMO_WIN_IMAGE:-}" ]] || return 1
	done
	return 1
}

fWinBox_Start(){
	local label="$1" root image vol vols=() rc=0
	root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	vol="${CICD_PROJECT:-nemo-anywhere}-build-win"
	if [[ "${CICD_NO_CONTAINER:-}" == "1" ]]; then
		echo "[ FAILED: ${label} needs a container, and --no-container runs on the host ]" >&2; return 1
	fi
	if ! command -v docker > /dev/null 2>&1 || ! timeout 10 docker info > /dev/null 2>&1; then
		echo "[ FAILED: ${label}: docker daemon is not reachable ]" >&2; return 1
	fi
	if ! image="$(fWinBox_Image)"; then
		[[ -z "${NEMO_WIN_IMAGE:-}" ]] || { echo "[ FAILED: ${label}: image '${NEMO_WIN_IMAGE}' not found ]" >&2; return 1; }
		image="nemo-winbuild-deps:latest"
		echo "[ building image nemo-winbuild-deps (first run only, takes a while) ]" >&2
		docker build --label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" -t "${image}" "${root}/cicd/win/" > /dev/null \
			|| { echo "[ FAILED: ${label}: could not build image nemo-winbuild-deps ]" >&2; return 1; }
	fi
	if [[ "${2:-}" != --no-build-win ]]; then
		fStepBox_Volume "${vol}" "${label}" || rc=$?
		case "${rc}" in
			0) ;;
			1) return 1 ;;
			*) echo "[ FAILED: ${label}: could not make volume ${vol} ]" >&2; return 1 ;;
		esac
		vols=(-v "${vol}:/build-win")
	fi
	fStepBox_Start "${label}" "${image}" -v "${root}:/src" "${vols[@]}" -w /src \
		|| { echo "[ FAILED: ${label}: could not start a container from ${image} ]" >&2; return 1; }
	CONTAINER="${stepBox}"
}


##	History:
##		- 2026-10-26: Created. The steps ran in the long-lived nemo-winbuild before.
