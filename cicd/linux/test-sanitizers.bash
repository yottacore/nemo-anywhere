#!/usr/bin/env bash

##	- Purpose: Run the whole suite on a build made with AddressSanitizer and
##	  UndefinedBehaviorSanitizer, leak checks on. Handed to docker-run.bash by
##	  cicd's sanitizer stage; not meant to be run on the host.
##	- Builds into a directory of its own, so /build is left alone. The build and
##	  the run go through run-tests.bash, the same as the ordinary suite.
##	- Any report fails the test that made it: undefined behavior halts as a memory
##	  error does. Leaks in the libraries under GTK are listed in sanitizers.supp;
##	  a leak of ours is never put there.
##	- Stacks are short, since a full one on every allocation slows the GUI tests
##	  past their own time limits. For the whole stack of a leak, rerun the test
##	  with ASAN_OPTIONS=fast_unwind_on_malloc=0, which is added to the lane's own.
##	- The leak tests read the heap through glibc, which the sanitizer's allocator
##	  hides. They see that themselves and answer 77, so here they show as skips.
##	- NEMO_TEST_JOBS caps the build and the tests at once. BUILD_DIR overrides the
##	  build directory. Arguments go to meson test, such as test names.
##	- Syntax: cicd/linux/test-sanitizers.bash [meson test arguments]
##	- Test ID: rjfgk2mp

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${BUILD_DIR:-/build-sanitize}"

## meson can keep a build dir's old options on reconfigure, and a plain build
## here would pass every test and prove nothing.
if [[ -f "${build}/build.ninja" ]] && ! grep -qF -- '-fsanitize=address' "${build}/build.ninja"; then
	echo "[ FAILED: ${build} is not a sanitizer build; remove it and run again ]" >&2
	exit 1
fi

## lundef off, since the extension library is a shared object and only the
## executables carry the sanitizer runtime.
export BUILD_DIR="${build}"
export SETUP_ARGS="-Db_sanitize=address,undefined -Db_lundef=false"

## GTK's CSS transitions leak a value each (gtk_css_value_calc_multiply in
## 3.24.49), and only GTK's own code is on that path. Its settings are read from
## the system config dirs as well, which the tests leave alone.
mkdir -p "${build}/xdg-config/gtk-3.0"
printf '[Settings]\ngtk-enable-animations=false\n' > "${build}/xdg-config/gtk-3.0/settings.ini"
export XDG_CONFIG_DIRS="${build}/xdg-config:${XDG_CONFIG_DIRS:-/etc/xdg}"

## GLib's slice allocator and its lazy clears hide a block from the leak check
## or keep one alive past its owner.
export G_SLICE=always-malloc G_DEBUG=gc-friendly
export ASAN_OPTIONS="detect_leaks=1:verify_asan_link_order=0${ASAN_OPTIONS:+:${ASAN_OPTIONS}}"
export LSAN_OPTIONS="suppressions=${here}/sanitizers.supp:print_suppressions=0"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

## Everything runs two to four times slower.
exec bash "${here}/run-tests.bash" --timeout-multiplier 4 "$@"
