#!/usr/bin/env bash

##	- Purpose: Fail unless a build dir compiles with warnings as errors. Every
##	  pipeline lane sets its dir up with -Dwerror=true and then runs this, so a
##	  reused dir that kept older options, or a lane that lost the flag, stops
##	  the build rather than letting a warning through.
##	- Reads build.ninja, which is what the compiler is handed, rather than what
##	  meson says it recorded.
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

## search-helpers asks for -Werror=address on its own, so only the bare flag
## counts.
if ! grep -q -E -- '(^|[[:space:]])-Werror([[:space:]]|$)' "${build}/build.ninja"; then
	echo "check-werror: ${build} does not build with -Werror. Run its meson setup again with -Dwerror=true" >&2
	exit 1
fi
