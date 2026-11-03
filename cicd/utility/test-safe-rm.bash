#!/usr/bin/env bash

##	- Purpose: Check that recursive removals in the repo's bash scripts go
##	  through the guard in include/run-temp.bash. Any rm -r outside the list
##	  below fails, and so does a list entry whose count is off, so the list
##	  shrinks as the rest move over. A script that calls the guard must source
##	  it, or its trap fails quietly and leaves the folder. Also checks
##	  fSafeRm_IfThere, fSafeRm_Under, that fSafeRm works with no HOST_TEMP
##	  set, and both without GNU realpath and rm.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-safe-rm.bash
##	- Test ID: defnq

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Still to move: the installer and the ones that run in a container
## (2026102617555870). The guard's own rm stays.
declare -A left=(
	[cicd/utility/include/run-temp.bash]=1
	[install.bash]=7
	[cicd/linux/test-fuzz-exit.bash]=1
	[cicd/win/test-gui-smoke.bash]=1
	[cicd/win/pack-zip.bash]=2
)

## rm with an r in its flags, or --recursive, outside a comment.
rmRe='(^|[^[:alnum:]_.-])rm[[:space:]]+(-[[:alnum:]]*[rR]|--recursive)'
declare -A seen=()
mapfile -t scripts < <(cd "$root" && git ls-files -co --exclude-standard -- '*.bash' '*.sh')
((${#scripts[@]} > 50)) || fFail "found only ${#scripts[@]} scripts to check"
for f in "${scripts[@]}"; do
	[[ -f "${root}/${f}" && "$f" != cicd/utility/test-safe-rm.bash ]] || continue
	n="$(sed -E -e '/^[[:space:]]*#/d' -e 's/[[:space:]]#[[:space:]].*$//' "${root}/${f}" | grep -c -E -- "$rmRe" || true)"
	((n)) && seen["$f"]="$n"
	if [[ "$f" != cicd/utility/include/run-temp.bash ]] && grep -q -E '(^|[^[:alnum:]_])fSafeRm(_IfThere|_Under)?[[:space:]]' "${root}/${f}" \
		&& ! grep -q -E '^[[:space:]]*(source|\.)[[:space:]].*include/run-temp\.bash' "${root}/${f}"; then
		fFail "${f} calls the guard but doesn't source include/run-temp.bash"
	fi
done
for f in "${!seen[@]}"; do
	if [[ -z "${left[$f]:-}" ]]; then fFail "${f}: ${seen[$f]} rm -r line(s) that don't go through the guard"
	elif [[ "${left[$f]}" != "${seen[$f]}" ]]; then fFail "${f}: ${seen[$f]} rm -r line(s), the list says ${left[$f]}"
	fi
done
for f in "${!left[@]}"; do
	[[ -n "${seen[$f]:-}" ]] || fFail "${f} is on the list, but has no rm -r left"
done

# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/safe-rm-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
mkdir -p "${scratch}/a/deep" "${scratch}/b"
touch "${scratch}/a/deep/file" "${scratch}/c"
out="$(fSafeRm_IfThere "${scratch}/a" "${scratch}/nosuch" "${scratch}/c" 2>&1)" || fFail "fSafeRm_IfThere failed on a folder, a file and a missing path: ${out}"
[[ -z "$out" ]] || fFail "fSafeRm_IfThere wasn't quiet: ${out}"
[[ ! -e "${scratch}/a" && ! -e "${scratch}/c" && -d "${scratch}/b" ]] || fFail "fSafeRm_IfThere removed the wrong things"
rc=0
out="$(unset HOST_TEMP projectName CICD_PROJECT; fSafeRm /nonexistent-safe-rm-check/x 2>&1)" || rc=$?
[[ "$rc" == 1 && "$out" == *"refused, not under a tmp folder"* ]] || fFail "fSafeRm with no HOST_TEMP: exit ${rc}, ${out}"
rc=0
out="$(unset HOST_TEMP projectName CICD_PROJECT; fSafeRm "${scratch}/b" 2>&1)" || rc=$?
[[ "$rc" == 0 && ! -e "${scratch}/b" ]] || fFail "fSafeRm with no HOST_TEMP didn't remove a folder under tmp: exit ${rc}, ${out}"

## fSafeRm_Under: only strictly under the one parent, never through a link.
par="${scratch}/par"
mkdir -p "${par}/build/sub" "${par}/keep" "${scratch}/outside"
touch "${par}/build/sub/f" "${par}/file" "${scratch}/outside/f"
ln -s "${scratch}/outside" "${par}/link"
ln -s "${par}" "${scratch}/parlink"
fUnder(){ rc=0; out="$(fSafeRm_Under "$@" 2>&1)" || rc=$?; }
fUnder "${par}" "${par}/build" "${par}/file" "${par}/nosuch"
[[ "$rc" == 0 && -z "$out" && ! -e "${par}/build" && ! -e "${par}/file" && -d "${par}/keep" ]] || fFail "fSafeRm_Under over a folder, a file and a missing path: exit ${rc}, ${out}"
fUnder "${par}" "${scratch}/outside"
[[ "$rc" == 1 && "$out" == *"not under"* && -d "${scratch}/outside" ]] || fFail "fSafeRm_Under took a path beside its parent: exit ${rc}, ${out}"
fUnder "${par}" "${par}"
[[ "$rc" == 1 && -d "${par}" ]] || fFail "fSafeRm_Under took its parent itself: exit ${rc}, ${out}"
fUnder "${par}" "${par}/keep/../../outside"
[[ "$rc" == 1 && -d "${scratch}/outside" ]] || fFail "fSafeRm_Under took a path with ..: exit ${rc}, ${out}"
fUnder "${par}" "${par}/link"
[[ "$rc" == 1 && "$out" == *"refused, a link"* && -L "${par}/link" ]] || fFail "fSafeRm_Under took a link: exit ${rc}, ${out}"
fUnder "${par}" "${par}/link/f"
[[ "$rc" == 1 && "$out" == *"through a link"* && -f "${scratch}/outside/f" ]] || fFail "fSafeRm_Under went through a link: exit ${rc}, ${out}"
fUnder / /nonexistent-safe-rm-check
[[ "$rc" == 1 && "$out" == *"bad parent"* ]] || fFail "fSafeRm_Under took / as its parent: exit ${rc}, ${out}"
fUnder par "par/keep"
[[ "$rc" == 1 && "$out" == *"bad parent"* && -d "${par}/keep" ]] || fFail "fSafeRm_Under took a relative parent: exit ${rc}, ${out}"
## A parent behind a link is fine, as with macOS's /var or a synced folder.
fUnder "${scratch}/parlink" "${scratch}/parlink/keep"
[[ "$rc" == 0 && ! -e "${par}/keep" ]] || fFail "fSafeRm_Under refused a parent behind a link: exit ${rc}, ${out}"

## The FreeBSD form: no realpath -e and no rm --one-file-system, as there.
mkdir -p "${par}/bsd/sub" "${scratch}/bsdtmp"
rc=0
out="$(
	realpath(){ [[ "$1" != -e ]] || return 64; command realpath "$@"; }
	rm(){ case "$1" in --version) return 64 ;; -rfx) shift; command rm -rf "$@" ;; *) return 64 ;; esac; }
	unset __safeRmGnu
	fSafeRm_Under "${par}" "${par}/bsd" && fSafeRm "${scratch}/bsdtmp" > /dev/null
)" || rc=$?
[[ "$rc" == 0 && ! -e "${par}/bsd" && ! -e "${scratch}/bsdtmp" ]] || fFail "the guard without GNU realpath and rm: exit ${rc}, ${out}"

if ((failures)); then
	fEcho "FAILED: safe removal, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: every rm -r goes through the guard, but the ${#left[@]} scripts listed"


##	History:
##		- 2026-10-26: Created.
##		- 2026-11-03: fSafeRm_Under, and the removals outside a temp root off the list.
