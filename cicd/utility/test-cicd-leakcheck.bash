#!/usr/bin/env bash

##	- Purpose: Check cicd.bash --leakcheck and --no-leakcheck. A plain run has
##	  the leak checks on and --quick has them off. --leakcheck turns them on and
##	  --no-leakcheck off, under --quick or not, with -q too, and the last of the
##	  two on the command line wins. Both are in --help. A copy of cicd.bash runs
##	  with a debug build that fails, so the run stops right after its plan.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-leakcheck.bash
##	- Test ID: desha

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
cicd="${here}/../cicd.bash"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

help="$(bash "$cicd" --help)"
for flag in --leakcheck --no-leakcheck; do
	grep -q -E "^ +${flag} " <<< "$help" || fFail "${flag} is not in --help"
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/cicd-leakcheck-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
export HOST_TEMP="${scratch}/host-temp"
mkdir -p "${HOST_TEMP}" "${scratch}/cicd/utility/include"
cp "$cicd" "${scratch}/cicd/"
for inc in gfs-rotate source-date run-temp docker-sweep jobs; do
	cp "${here}/include/${inc}.bash" "${scratch}/cicd/utility/include/"
done
cat > "${scratch}/cicd/config.bash" <<-CFG
	APP_NAME="App"; EXE_NAME="app"; CICD_CONTAINERS=()
	FMT_CMD=(true); FMT_CHECK_CMD=(true); DEBUG_BUILD_CMD=(false); TEST_CMD=(true)
	PROFILE_ENABLE=0; PROFILE_OUT_DIR="out"; PROFILE_SECS=1; PROFILE_CMD=(true); RELEASE_ENABLE=0; RELEASE_NATIVE_CMD=(true); RELEASE_NATIVE_BIN="x"; BUILD_CROSS=0; CROSS_TARGETS=(); PACKAGE_ENABLE=0
	VERSION_MANIFEST="meson.build"; PRIVATE_RUNNER=""; LINT_LOG_DIR=""
	DOGFOOD_FIXED_DESTS=(); DOGFOOD_ROTATING_DESTS=(); DOGFOOD_CROSS_DESTS=(); DOGFOOD_HOOK=()
	DOGFOOD_REMOTE=(); GIT_PUBLISH=(true); PUBLISH_AUTO_MESSAGE="unused"
CFG

## $1 on or off, rest the options.
fCase(){
	local want="$1" out got; shift
	out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash -y --no-sync "$@" 2>&1 </dev/null || true)"
	got="$(sed -n 's/^ *Leak checks \.* *: //p' <<< "$out")"
	[[ "$got" == "$want" ]] || fFail "[${*:-no options}]: leak checks '${got:-none}', want ${want}"
}

fCase on
fCase off --quick
fCase on  --leakcheck
fCase off --no-leakcheck
fCase on  --quick --leakcheck
fCase on  --leakcheck --quick
fCase off --quick --no-leakcheck
fCase off --leakcheck --no-leakcheck
fCase on  --no-leakcheck --leakcheck
fCase on  --quick --no-leakcheck --leakcheck
fCase off --quick --leakcheck --no-leakcheck
fCase off -q --no-leakcheck
fCase on  -q --quick --leakcheck

if ((failures)); then exit 1; fi
fEcho "OK: --leakcheck and --no-leakcheck"
