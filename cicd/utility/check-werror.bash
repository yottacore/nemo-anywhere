#!/usr/bin/env bash

##	- Purpose: Fail unless a build dir compiles with warnings as errors. Every
##	  pipeline lane sets its dir up with -Dwerror=true and then runs this, so a
##	  reused dir that kept older options, or a lane that lost the flag, stops
##	  the build rather than letting a warning through.
##	- Reads build.ninja, which is what the compiler is handed, rather than what
##	  meson says it recorded.
##	- Every C compile in it has to have the flag, so a target that turns it off
##	  for itself, the way the vendored libraries once did, fails too.
##	- Syntax: check-werror.bash <build-dir>

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

build="${1:?usage: check-werror.bash <build-dir>}"

if [[ ! -f "${build}/build.ninja" ]]; then
	echo "check-werror: ${build} has no build.ninja" >&2
	exit 1
fi

## Only the bare flag counts, not a -Werror=<warning> one. On Windows meson
## double-quotes every argument in build.ninja.
if ! grep -q -E -- '(^|[[:space:]])(-Werror|"-Werror")([[:space:]]|$)' "${build}/build.ninja"; then
	echo "check-werror: ${build} does not build with -Werror. Run its meson setup again with -Dwerror=true" >&2
	exit 1
fi

## A compile is a "build ...: c_COMPILER" line followed by its own ARGS line.
lacking=()
while IFS= read -r src; do lacking+=("$src"); done < <(awk '
	/^build .*: c_COMPILER[A-Za-z_]* / { src = $4; next }
	/^build / { src = "" }
	src != "" && /^ ARGS = / {
		if ($0 !~ /[[:space:]](-Werror|"-Werror")([[:space:]]|$)/) print src
		src = ""
	}' "${build}/build.ninja")
if ((${#lacking[@]})); then
	echo "check-werror: ${build} compiles these without -Werror. Nothing may turn it off for itself:" >&2
	printf '  %s\n' "${lacking[@]}" >&2
	exit 1
fi
