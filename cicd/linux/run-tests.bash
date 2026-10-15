#!/usr/bin/env bash

##	- Purpose: Run the regression suite and the launch smoke inside the reference
##	  build container. Handed to docker-run.bash by cicd's test stage; not meant to
##	  be run on the host. The FreeBSD lane runs it on that box too (bsd/lane.bash).
##	- Builds first, since the gate has no build stage of its own. What gets built
##	  is the working tree, not the commit being pushed, so an unfinished edit
##	  sitting there stops the gate here. Building the pushed sha in a detached
##	  worktree instead would be correct and would also be a cold build every time,
##	  which is why it is not done.
##	- NEMO_TEST_JOBS caps both the build and the number of tests at once, so a run
##	  leaves the box usable. BUILD_DIR overrides the build directory, and
##	  SETUP_ARGS adds words to its meson setup. Arguments go to meson test.
##	- Warnings are errors here, as in every pipeline build, and not in a plain
##	  meson setup of the tree. Someone building from source with a newer compiler
##	  gets a warning, not a failed build.
##	- The source is the tree this script sits in. Where there is no xvfb-run, as
##	  on FreeBSD, the run starts an X server of its own for both steps.
##	- Syntax: run-tests.bash [meson test arguments]

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$(cd "${here}/../../source" && pwd)"
build="${BUILD_DIR:-/build}"
jobs="${NEMO_TEST_JOBS:-2}"

## Arithmetic comparison here would evaluate whatever it was handed, and 0 means
## "no limit" to both tools below - the opposite of the point.
case "${jobs}" in
	''|*[!0-9]*|0*) jobs=2 ;;
esac
# shellcheck disable=SC2206  ## word splitting is the point; the sanitizer lane passes -D options.
setupArgs=(-Dwerror=true ${SETUP_ARGS:-})

bash "${here}/../utility/meson-setup.bash" "${build}" "${src}" "${setupArgs[@]}"
bash "${here}/../utility/check-werror.bash" "${build}"

if ! ninja -C "${build}" -j "${jobs}"; then
	echo "[ the build here is the working tree, unfinished edits included ]" >&2
	exit 1
fi

## About a third of the suite fails on "cannot open display" with no server, and
## several of those read like real assertion failures. The bus is disabled so a
## test that sends --quit cannot reach a copy someone is actually using.
export DBUS_SESSION_BUS_ADDRESS='disabled:'

## The run gets a temp dir of its own, so anything a test leaves behind is named
## here instead of piling up in /tmp. A failed run leaves it for a look.
TMPDIR="$(mktemp -d /tmp/nemo-suite-XXXXXX)"
export TMPDIR
xvfbLog=""
fCleanup(){
	rmdir "${TMPDIR}" 2>/dev/null || true
	if [[ -n "${XVFB_PID:-}" ]]; then kill "${XVFB_PID}" 2>/dev/null || true; fi
	if [[ -n "${xvfbLog}" ]]; then rm -f "${xvfbLog}"; fi
}
trap fCleanup EXIT

if command -v xvfb-run >/dev/null 2>&1; then
	fUnderX(){ xvfb-run -a "$@"; }
else
	# shellcheck source=../utility/include/xvfb.bash
	source "${here}/../utility/include/xvfb.bash"
	## Outside TMPDIR, or the leftover check below would count it.
	xvfbLog="$(mktemp /tmp/nemo-suite-xvfb-XXXXXX)"
	fXvfbStart 120 1280x1024x24 "${xvfbLog}" || { echo "[ no free display from :120 up ]" >&2; exit 1; }
	export DISPLAY="${XVFB_DISPLAY}"
	fUnderX(){ "$@"; }
fi

fUnderX meson test -C "${build}" --no-rebuild --num-processes "${jobs}" --print-errorlogs "$@"

## ls, since BSD find has no -printf.
leftover="$(ls -A "${TMPDIR}" || true)"
if [[ -n "${leftover}" ]]; then
	echo "[ tests left these behind in ${TMPDIR} ]" >&2
	echo "${leftover}" >&2
	exit 1
fi

## The suite covers the program's insides; this covers the argument that has to
## answer before any of them are reached.
fUnderX "${build}/src/nemo-anywhere" --version
