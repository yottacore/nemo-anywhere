#!/usr/bin/env bash

##	- Purpose: Check that a test in the jail (jail.bash) can write its own cell
##	  and nothing else: not the source tree, not the build dir, not the real
##	  HOME. Also that the cell is gone after. run-tests.bash runs it before the
##	  suite.
##	- Off Linux there's no jail, so only the cell half is checked there.
##	- Syntax: cicd/linux/test-jail.bash [build dir]
##	- Test ID: rknkav2r

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$(cd "${here}/../.." && pwd)"
build="${1:-${BUILD_DIR:-/build}}"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

run="$(mktemp -d "${TMPDIR:-/tmp}/nemo-jail-test-XXXXXX")"
probe=".jail-probe-${run##*-}"

## Each try says what happened, and takes back a write that went through, so a
## broken jail leaves nothing behind in the real tree.
# shellcheck disable=SC2016  ## expanded by the shell in the jail
said="$(TMPDIR="${run}" bash "${here}/jail.bash" sh -c '
	probe="$1"; shift
	for dir in "$@" "$HOME" "$TMPDIR"; do
		if ( : > "${dir}/${probe}" ) 2>/dev/null; then
			echo "wrote ${dir}"
			rm -f "${dir}/${probe}"
		else
			echo "refused ${dir}"
		fi
	done
	echo "home ${HOME}"
' sh "${probe}" "${src}" "${build}" "${HOME}" 2>&1)" || fFail "the jailed shell failed: ${said}"

fSaid(){ grep -qxF "$1" <<<"${said}" || fFail "wanted '$1', got:"$'\n'"${said}"; }

cellHome="$(sed -n 's/^home //p' <<<"${said}")"
[[ "${cellHome}" == "${run}/sh."*"/home" ]] || fFail "HOME in the jail was '${cellHome}', not a cell under ${run}"
fSaid "wrote ${cellHome}"
fSaid "wrote ${cellHome%/home}/tmp"

if [[ "${OSTYPE}" == linux* ]]; then
	fSaid "refused ${src}"
	[[ -d "${build}" ]] && fSaid "refused ${build}"
	fSaid "refused ${HOME}"
fi

## mktemp made it for this run, and every cell in it should be gone.
if ! rmdir "${run}" 2>/dev/null; then
	fFail "the cell was left behind: $(ls -A "${run}")"
fi

if (( failures > 0 )); then
	fEcho "FAILED: ${failures} problem(s)"
	exit 1
fi
fEcho "OK: the jail holds"


##	History:
##		- 2026-10-19: Created.
