#!/usr/bin/env bash

##	- Purpose: meson setup for a pipeline build dir. A new dir is set up, an old
##	  one reconfigured, and one set up before c_std was named is wiped and set up
##	  again, since meson.build refuses it and meson never changes a stored default.
##	- SETUP_FRESH adds words used only on a new or wiped dir. MESON_WIPE=1 always
##	  wipes, for a lane whose options meson won't take on reconfigure.
##	- Syntax: meson-setup.bash <build-dir> <source-dir> [meson setup options]

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

build="${1:?usage: meson-setup.bash <build-dir> <source-dir> [options]}"
src="${2:?usage: meson-setup.bash <build-dir> <source-dir> [options]}"
shift 2
# shellcheck disable=SC2206  ## words on purpose, as SETUP_ARGS in run-tests.bash
fresh=(${SETUP_FRESH:-})

if [[ ! -f "${build}/build.ninja" ]]; then
	exec meson setup "${build}" "${src}" "${fresh[@]}" "$@"
fi
if [[ "${MESON_WIPE:-0}" == "1" ]]; then
	exec meson setup --wipe "${build}" "${src}" "${fresh[@]}" "$@"
fi

log="$(mktemp)"
if meson setup --reconfigure "${build}" "${src}" "$@" >"${log}" 2>&1; then
	cat "${log}"; rm -f "${log}"; exit 0
fi
if grep -qF 'predates c_std' "${log}"; then
	rm -f "${log}"
	echo "meson-setup: ${build} predates c_std, wiping it"
	exec meson setup --wipe "${build}" "${src}" "${fresh[@]}" "$@"
fi
cat "${log}" >&2; rm -f "${log}"
exit 1
