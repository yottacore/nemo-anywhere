#!/usr/bin/env bash

##	- Purpose: The Windows cross build's step container: one of its own for each
##	  step, from the cross-build image, with the repo at /src and the cross
##	  build dir at /build-win in the cache volume <prefix>-build-win, where
##	  fStepBox_VolPrefix gives the prefix. Named, labeled, and removed the way
##	  docker-run.bash's are, through utility/include/step-box.bash.
##	- fWinBox_Tag -> nemo-winbuild:<hash>, the hash over the pinned recipe,
##	  cicd/win/Dockerfile, and the files it copies in.
##	- fWinBox_Image -> the image a step would run, or 1 when there's none yet.
##	  NEMO_WIN_IMAGE names one; by default the fWinBox_Tag one.
##	- fWinBox_Start <label> [--no-build-win] -> starts it and sets CONTAINER.
##	  Builds the fWinBox_Tag image the first time its hash is seen. The run's
##	  docker sweep keeps the newest 2 tags.
##	  --no-build-win leaves the cache volume out, for a step that reads only
##	  the packed artifacts. Says what's wrong in one line and returns 1.
##	  Stop it with fStepBox_Stop from the EXIT trap.
##	- No host fallback: the cross build and what reads it keep to their
##	  containers (2026102617145688).
##	- Backlog 2026102617253214.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

# shellcheck source=../utility/include/step-box.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../utility/include" && pwd)/step-box.bash"

# shellcheck disable=SC2034  ## read by the scripts that source this
CONTAINER=""

fWinBox_Tag(){
	local root
	root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	fStepBox_ImageTag nemo-winbuild "${root}/cicd/win/Dockerfile" "${root}/cicd/win/fetch-sysroot.bash" \
		"${root}/cicd/win/sysroot-pins.txt" "${root}/cicd/win/win64.cross.txt" \
		"${root}/cicd/utility/include/run-temp.bash" 2> /dev/null
}

fWinBox_Image(){
	local image="${NEMO_WIN_IMAGE:-}"
	[[ -n "${image}" ]] || image="$(fWinBox_Tag)" || return 1
	docker image inspect "${image}" > /dev/null 2>&1 || return 1
	printf '%s\n' "${image}"
}

fWinBox_Start(){
	local label="$1" root image vol vols=() rc=0
	root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	vol="$(fStepBox_VolPrefix)-build-win"
	if [[ "${CICD_NO_CONTAINER:-}" == "1" ]]; then
		echo "[ FAILED: ${label} needs a container, and --no-container runs on the host ]" >&2; return 1
	fi
	if ! command -v docker > /dev/null 2>&1 || ! timeout 10 docker info > /dev/null 2>&1; then
		echo "[ FAILED: ${label}: docker daemon is not reachable ]" >&2; return 1
	fi
	if ! image="$(fWinBox_Image)"; then
		[[ -z "${NEMO_WIN_IMAGE:-}" ]] || { echo "[ FAILED: ${label}: image '${NEMO_WIN_IMAGE}' not found ]" >&2; return 1; }
		image="$(fWinBox_Tag)" || { echo "[ FAILED: ${label}: no win/Dockerfile to build the image from ]" >&2; return 1; }
		echo "[ building image ${image} from win/Dockerfile (once per change to it, takes a few minutes) ]" >&2
		nice -n 19 docker build --label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" -t "${image}" -f "${root}/cicd/win/Dockerfile" "${root}/cicd/" > /dev/null \
			|| { echo "[ FAILED: ${label}: could not build image ${image} ]" >&2; return 1; }
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
	# shellcheck disable=SC2034
	CONTAINER="${stepBox}"
}


##	History:
##		- 2026-10-26: Created. The steps ran in the long-lived nemo-winbuild before.
##		- 2026-10-31: Builds with cicd/ as the context, which the pinned recipe needs.
##		- 2026-10-31: Tagged by the recipe's hash, in place of nemo-winbuild:init.
