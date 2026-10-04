#!/usr/bin/env bash

##	- Purpose: Check that the Linux release build gets link-time optimization even
##	  when the container already has a build dir set up without it. A reused dir
##	  kept it off, and every release built in it differed from a clean rebuild.
##	- In a throwaway container from the release image, sets up a build dir with
##	  no link-time optimization, runs release-setup.bash over it, and reads the
##	  options back. Also checks that the script will not clear a dir that is not
##	  a meson build dir.
##	- Needs docker and the release image; exit 77 without them. It does not
##	  build the image.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-release-setup.bash
##	- Test ID: rjcpvcyb

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
image="${NEMO_RELEASE_IMAGE:-nemo-build-jammy:latest}"
build=/build-release
prefix=/opt/nemo-anywhere

if ! command -v docker >/dev/null 2>&1 || ! timeout 10 docker image inspect "$image" >/dev/null 2>&1; then
	fEcho "release setup check skipped: no ${image} image"
	exit 77
fi

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

box="nemo-release-setup-test-$$"
trap 'docker rm -f "${box}" >/dev/null 2>&1 || true' EXIT
docker run -d --rm --init --name "$box" -v "${root}:/src:ro" "$image" sleep infinity >/dev/null

fOptions(){
	docker exec "$box" meson introspect --buildoptions "$build" | python3 -c '
import json, sys
opts = {o["name"]: o["value"] for o in json.load(sys.stdin)}
print(" ".join(f"{n}={opts.get(n)}" for n in ("buildtype", "strip", "b_lto", "b_lto_threads")))
'
}

## The dir as the old image and container had it.
docker exec "$box" meson setup --buildtype=release "$build" /src/source >/dev/null
seeded="$(fOptions)"
[[ "$seeded" == *"b_lto=False"* ]] || fFail "could not set up a dir without link-time optimization: ${seeded}"

rc=0
out="$(docker exec -e SOURCE_DATE_EPOCH=946684800 "$box" bash /src/cicd/linux/release-setup.bash "$build" /src/source "$prefix" 2>&1)" || rc=$?
[[ "$rc" == 0 ]] || fFail "release-setup.bash failed over an old dir (exit ${rc}): ${out}"
got="$(fOptions)"
want="buildtype=release strip=True b_lto=True b_lto_threads=4"
if [[ "$got" == "$want" ]]; then
	fEcho "OK: an old dir comes out with ${got}"
else
	fFail "an old dir comes out with ${got}, want ${want}"
fi
docker exec "$box" grep -q -F -e '-flto' "${build}/build.ninja" || fFail "${build}/build.ninja has no -flto"

## Not a meson dir, so not the script's to clear.
docker exec "$box" sh -c 'mkdir -p /tmp/not-a-build && touch /tmp/not-a-build/keep'
rc=0
out="$(docker exec "$box" bash /src/cicd/linux/release-setup.bash /tmp/not-a-build /src/source "$prefix" 2>&1)" || rc=$?
if [[ "$rc" == 0 ]] || ! docker exec "$box" test -f /tmp/not-a-build/keep; then
	fFail "release-setup.bash cleared a dir that was not a build dir (exit ${rc}): ${out}"
else
	fEcho "OK: a dir that is not a build dir is left alone"
fi

if ((failures)); then fEcho "FAILED: release setup check, ${failures} problem(s)"; exit 1; fi
fEcho "OK: release setup check"
