#!/usr/bin/env bash

##	- Purpose: Check that docker-run.bash refuses to run from a clone other
##	  than the one its build cache volumes belong to, such as a second
##	  worktree. Else the gate would build on the other clone's objects.
##	- Needs docker and the build image; exit 77 without them.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-docker-run.bash
##	- Test ID: rj3ytv0b

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"

fEcho(){ echo "[ $* ]"; }

## Without the image, docker-run.bash would spend minutes building one.
if [[ "${CICD_NO_CONTAINER:-}" == "1" ]] || ! command -v docker >/dev/null 2>&1 \
	|| ! { timeout 10 docker image inspect nemo-build-deps:init || timeout 10 docker image inspect nemo-build-deps:latest; } >/dev/null 2>&1; then
	fEcho "docker-run clone check skipped: no build image"
	exit 77
fi

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/docker-run-test.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
mkdir -p "${scratch}/cicd/utility/include"
cp "${here}/docker-run.bash" "${scratch}/cicd/utility/"
cp "${here}/include/source-date.bash" "${scratch}/cicd/utility/include/"
cp "${here}/include/jobs.bash" "${scratch}/cicd/utility/include/"

## This clone first, which makes the volumes for it on a fresh box.
rc=0
out="$(bash "${here}/docker-run.bash" check 'echo inside-the-box' 2>&1)" || rc=$?
[[ "$rc" == 0 && "$out" == *"inside-the-box"* ]] || fFail "refused the clone the volumes belong to (exit ${rc}); said: ${out}"

rc=0
out="$(bash "${scratch}/cicd/utility/docker-run.bash" check 'echo inside-the-box' 2>&1)" || rc=$?
if [[ "$out" == *"inside-the-box"* || "$rc" == 0 ]]; then
	fFail "ran from another clone (exit ${rc}); said: ${out}"
elif [[ "$out" != *"not $(realpath "$scratch")"* ]]; then
	fFail "refused without naming the clone; said: ${out}"
fi

if ((failures)); then
	fEcho "FAILED: docker-run clone check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: docker-run clone check"
