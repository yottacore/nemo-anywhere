#!/usr/bin/env bash

##	- Purpose: Set up the Linux release build dir from nothing, inside the release
##	  container. release.bash runs it before every build.
##	- Never a reconfigure. meson 0.61, the one in the release image, takes plain
##	  options on a reconfigure but drops new b_ ones without a word, so a dir first
##	  set up without link-time optimization kept it off for every release built in
##	  it, and none of them matched a clean rebuild of its commit.
##	- Reads back what meson recorded and fails on anything but what was asked.
##	- SOURCE_DATE_EPOCH has to be set by the caller: the build number is worked
##	  out here, at setup time.
##	- Syntax: release-setup.bash <build-dir> <source-dir> <prefix>

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

build="${1:?usage: release-setup.bash <build-dir> <source-dir> <prefix>}"
src="${2:?usage: release-setup.bash <build-dir> <source-dir> <prefix>}"
prefix="${3:?usage: release-setup.bash <build-dir> <source-dir> <prefix>}"

fDie(){ echo "release-setup: $*" >&2; exit 1; }

## Only ever clear a meson build dir. Anything else at that path was put there by
## someone else.
if [[ -e "$build" ]]; then
	[[ "$build" == /* && "$build" != / ]] || fDie "build dir must be an absolute path: ${build}"
	[[ -d "${build}/meson-private" ]] || fDie "${build} is not a meson build dir; not clearing it"
	rm -rf "$build"
fi

meson setup --buildtype=release -Dstrip=true -Db_lto=true -Db_lto_threads=4 -Dextension_library=static "-Dprefix=${prefix}" "$build" "$src" >/dev/null

want="buildtype=release strip=True b_lto=True b_lto_threads=4 extension_library=static prefix=${prefix}"
got="$(meson introspect --buildoptions "$build" | python3 -c '
import json, sys
opts = {o["name"]: o["value"] for o in json.load(sys.stdin)}
print(" ".join(f"{n}={opts.get(n)}" for n in sys.argv[1:]))
' buildtype strip b_lto b_lto_threads extension_library prefix)"
[[ "$got" == "$want" ]] || fDie "${build} has ${got}, asked for ${want}"
grep -q -F -e '-flto' "${build}/build.ninja" || fDie "${build}/build.ninja has no -flto"

##	History:
##		- 2026-10-03: Created, out of release.bash, which reconfigured an old dir.
