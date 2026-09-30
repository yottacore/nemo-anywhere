#!/usr/bin/env bash

##	- Purpose: Check that docker-run.bash refuses to run from a clone other
##	  than the one the build container has mounted, such as a second worktree.
##	  Else the gate would pass or fail the mounted clone, not the one pushing.
##	- Needs docker and the nemo-build container; exit 77 without them.
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

fEcho(){ echo "[ $* ]"; }

## Without the container, docker-run.bash would make one, mounted on the copy.
if ! command -v docker >/dev/null 2>&1 || ! timeout 10 docker inspect nemo-build >/dev/null 2>&1; then
	fEcho "docker-run clone check skipped: no nemo-build container"
	exit 77
fi

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/docker-run-test.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT
mkdir -p "${scratch}/cicd/utility/include"
cp "${here}/docker-run.bash" "${scratch}/cicd/utility/"
cp "${here}/include/source-date.bash" "${scratch}/cicd/utility/include/"

rc=0
out="$(NEMO_CONTAINER=nemo-build bash "${scratch}/cicd/utility/docker-run.bash" check 'echo inside-the-box' 2>&1)" || rc=$?
if [[ "$out" == *"inside-the-box"* || "$rc" == 0 ]]; then
	fFail "ran from another clone (exit ${rc}); said: ${out}"
elif [[ "$out" != *"not $(realpath "$scratch")"* ]]; then
	fFail "refused without naming the clone; said: ${out}"
fi

rc=0
out="$(NEMO_CONTAINER=nemo-build bash "${here}/docker-run.bash" check 'echo inside-the-box' 2>&1)" || rc=$?
[[ "$rc" == 0 && "$out" == *"inside-the-box"* ]] || fFail "refused the mounted clone (exit ${rc}); said: ${out}"

if ((failures)); then
	fEcho "FAILED: docker-run clone check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: docker-run clone check"
