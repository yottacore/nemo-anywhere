#!/usr/bin/env bash

##	- Purpose: Keep warnings fatal in every pipeline build, and only there. Each
##	  lane that sets up a build dir has to pass -Dwerror=true and read it back
##	  with check-werror.bash. The tree's own defaults and the hosted release
##	  workflow leave it off, since both meet compilers nobody here picked.
##	- Also runs check-werror.bash over made-up build dirs, so a read-back that
##	  passes everything is caught.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-werror.bash
##	- Test ID: rjh7qnxw

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
checker="${here}/check-werror.bash"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

for lane in cicd/config.bash cicd/linux/run-tests.bash cicd/linux/fuzz.bash cicd/win/build-cross.bash cicd/cicd-win.ps1; do
	text="$(cat "${root}/${lane}")"
	[[ "$text" == *"-Dwerror=true"* ]] || fFail "${lane} does not pass -Dwerror=true"
	[[ "$text" == *"check-werror.bash"* ]] || fFail "${lane} does not read werror back"
done
## The release lane reads its options back itself.
text="$(cat "${root}/cicd/linux/release-setup.bash")"
[[ "$text" == *"-Dwerror=true"* && "$text" == *"werror=True"* ]] || fFail "cicd/linux/release-setup.bash does not set and read back werror"

project="$(sed -n '/^project(/,/)/p' "${root}/source/meson.build")"
[[ "$project" != *werror* ]] || fFail "source/meson.build makes werror a default"
[[ "$(cat "${root}/.github/workflows/release-win.yml")" != *werror* ]] || fFail "release-win.yml builds with werror on a compiler it does not pin"

scratch="$(mktemp -d)"
trap 'rm -rf -- "${scratch}"' EXIT
mkdir -p "${scratch}/on" "${scratch}/off" "${scratch}/empty" "${scratch}/winon" "${scratch}/winoff" "${scratch}/winhalf"
printf 'build a.o: c_COMPILER a.c\n ARGS = -Wall -Winvalid-pch -Wextra -Werror -std=c17\n' > "${scratch}/on/build.ninja"
printf 'build a.o: c_COMPILER a.c\n ARGS = -Wall -Winvalid-pch -Wextra -std=c17 -Werror=address\n' > "${scratch}/off/build.ninja"
## On Windows meson wraps every argument in double quotes.
printf 'build a.c.obj: c_COMPILER ../a.c\n ARGS = "-I." "-fdiagnostics-color=always" "-Wall" "-Winvalid-pch" "-Wextra" "-Werror" "-std=c17"\n' > "${scratch}/winon/build.ninja"
printf 'build a.c.obj: c_COMPILER ../a.c\n ARGS = "-I." "-Wall" "-Wextra" "-std=c17" "-Werror=address"\n' > "${scratch}/winoff/build.ninja"
printf 'build a.c.obj: c_COMPILER ../a.c\n ARGS = "-I." "-Wall" "-Werror -std=c17"\n' > "${scratch}/winhalf/build.ninja"

bash "$checker" "${scratch}/on" 2>/dev/null || fFail "check-werror.bash refused a dir with -Werror"
if bash "$checker" "${scratch}/off" 2>/dev/null; then fFail "check-werror.bash passed a dir with only -Werror=address"; fi
bash "$checker" "${scratch}/winon" 2>/dev/null || fFail "check-werror.bash refused a Windows dir with \"-Werror\""
if bash "$checker" "${scratch}/winoff" 2>/dev/null; then fFail "check-werror.bash passed a Windows dir with only \"-Werror=address\""; fi
if bash "$checker" "${scratch}/winhalf" 2>/dev/null; then fFail "check-werror.bash passed -Werror inside a quoted argument"; fi
if bash "$checker" "${scratch}/empty" 2>/dev/null; then fFail "check-werror.bash passed a dir with no build.ninja"; fi

if ((failures)); then fEcho "FAILED: werror check, ${failures} problem(s)"; exit 1; fi
fEcho "OK: warnings are fatal in every pipeline build"
