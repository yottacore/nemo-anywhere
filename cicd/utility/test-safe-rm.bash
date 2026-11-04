#!/usr/bin/env bash

##	- Purpose: Check that recursive removals in the repo's bash scripts go
##	  through the guard in include/run-temp.bash. Any rm -r outside the list
##	  below fails, and so does a list entry whose count is off, so the list
##	  shrinks as the rest move over. A script that calls the guard must source
##	  it, or its trap fails quietly and leaves the folder. Also checks
##	  fSafeRm_IfThere, fSafeRm_Under, that fSafeRm works with no HOST_TEMP
##	  set, and both without GNU realpath and rm. install.bash has its own
##	  copy of fSafeRm_Under, checked the same way, with no realpath at all.
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

## The guards' own rm lines. install.bash runs alone, from a download, so it
## has its own copy.
declare -A left=(
	[cicd/utility/include/run-temp.bash]=1
	[install.bash]=1
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
		&& ! grep -q -E '^fSafeRm(_IfThere|_Under)?\(\)' "${root}/${f}" \
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
trap 'unset -f realpath rm; fSafeRm_IfThere "${scratch}"' EXIT
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

## MSYS2's ln -s copies by default, so ask it for a real link, which needs
## the symlink privilege, and else make a junction, which doesn't. Both read
## as a link there, as on Windows they are one. Where neither can be made, the
## link cases are skipped rather than run against a copy.
fLink(){ ## target name
	MSYS="${MSYS:+${MSYS} }winsymlinks:nativestrict" ln -s "$1" "$2" 2> /dev/null && [[ -L "$2" ]] && return 0
	[[ ! -e "$2" && ! -L "$2" && -d "$1" ]] && command -v cygpath > /dev/null || return 1
	cmd //c mklink //J "$(cygpath -w "$2")" "$(cygpath -w "$1")" > /dev/null 2>&1 && [[ -L "$2" ]]
}

## fSafeRm_Under: only strictly under the one parent, never through a link.
par="${scratch}/par"
mkdir -p "${par}/build/sub" "${par}/keep" "${scratch}/outside"
touch "${par}/build/sub/f" "${par}/file" "${scratch}/outside/f"
haveLinks=0
fLink "${scratch}/outside" "${par}/link" && fLink "${par}" "${scratch}/parlink" && haveLinks=1
((haveLinks)) || fEcho "SKIP: the link cases, no symlink or junction could be made here"
fUnder(){ rc=0; out="$(fSafeRm_Under "$@" 2>&1)" || rc=$?; }
fUnder "${par}" "${par}/build" "${par}/file" "${par}/nosuch"
[[ "$rc" == 0 && -z "$out" && ! -e "${par}/build" && ! -e "${par}/file" && -d "${par}/keep" ]] || fFail "fSafeRm_Under over a folder, a file and a missing path: exit ${rc}, ${out}"
fUnder "${par}" "${scratch}/outside"
[[ "$rc" == 1 && "$out" == *"not under"* && -d "${scratch}/outside" ]] || fFail "fSafeRm_Under took a path beside its parent: exit ${rc}, ${out}"
fUnder "${par}" "${par}"
[[ "$rc" == 1 && -d "${par}" ]] || fFail "fSafeRm_Under took its parent itself: exit ${rc}, ${out}"
fUnder "${par}" "${par}/keep/../../outside"
[[ "$rc" == 1 && -d "${scratch}/outside" ]] || fFail "fSafeRm_Under took a path with ..: exit ${rc}, ${out}"
if ((haveLinks)); then
	fUnder "${par}" "${par}/link"
	[[ "$rc" == 1 && "$out" == *"refused, a link"* && -L "${par}/link" ]] || fFail "fSafeRm_Under took a link: exit ${rc}, ${out}"
	fUnder "${par}" "${par}/link/f"
	[[ "$rc" == 1 && "$out" == *"through a link"* && -f "${scratch}/outside/f" ]] || fFail "fSafeRm_Under went through a link: exit ${rc}, ${out}"
fi
fUnder / /nonexistent-safe-rm-check
[[ "$rc" == 1 && "$out" == *"bad parent"* ]] || fFail "fSafeRm_Under took / as its parent: exit ${rc}, ${out}"
fUnder par "par/keep"
[[ "$rc" == 1 && "$out" == *"bad parent"* && -d "${par}/keep" ]] || fFail "fSafeRm_Under took a relative parent: exit ${rc}, ${out}"
## A parent behind a link is fine, as with macOS's /var or a synced folder.
if ((haveLinks)); then
	fUnder "${scratch}/parlink" "${scratch}/parlink/keep"
	[[ "$rc" == 0 && ! -e "${par}/keep" ]] || fFail "fSafeRm_Under refused a parent behind a link: exit ${rc}, ${out}"
fi

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

## install.bash's copy, renamed so it sits beside the one above. No realpath,
## since macOS before 13 has none.
inst="$(sed -n -e '/^fCheckPrefix(){/,/^}/p' -e '/^fSafeRm_Under(){/,/^}/p' -e '/^__rmOneFs=/p' -e '/^if rm --version/p' -e '/^fRmBesidePrefix(){/p' "${root}/install.bash")"
inst="${inst//fSafeRm_Under/fInst_SafeRm_Under}"
eval "${inst}"
for f in fCheckPrefix fInst_SafeRm_Under fRmBesidePrefix; do declare -F "$f" > /dev/null || fFail "install.bash has no ${f/fInst_/f}"; done
if command rm --version > /dev/null 2>&1; then oneFs=--one-file-system; else oneFs=-x; fi
[[ "${__rmOneFs:-}" == "${oneFs}" ]] || fFail "install.bash doesn't keep rm on one file system: '${__rmOneFs:-}', not ${oneFs}"
# shellcheck disable=SC2034  ## read by the eval'd functions
priv='' EXE_NAME=nemo-anywhere
fWarn(){ echo "WARNING: $*" >&2; }
fDie(){ echo "FAILED: $*" >&2; exit 1; }
# shellcheck disable=SC2329  ## stands in for any realpath the guard might call
realpath(){ return 64; }
fIUnder(){ rc=0; out="$(fInst_SafeRm_Under "$@" 2>&1)" || rc=$?; }
ipar="${scratch}/ipar"
mkdir -p "${ipar}/build/sub" "${ipar}/keep" "${scratch}/ioutside"
touch "${ipar}/build/sub/f" "${ipar}/file" "${scratch}/ioutside/f"
((! haveLinks)) || { fLink "${scratch}/ioutside" "${ipar}/link" && fLink "${ipar}" "${scratch}/iparlink"; } || fFail "made links above, but not for the installer guard"
fIUnder "${ipar}" "${ipar}/build" "${ipar}/file" "${ipar}/nosuch"
[[ "$rc" == 0 && -z "$out" && ! -e "${ipar}/build" && ! -e "${ipar}/file" && -d "${ipar}/keep" ]] || fFail "installer guard over a folder, a file and a missing path: exit ${rc}, ${out}"
fIUnder "${ipar}" "${scratch}/ioutside"
[[ "$rc" == 1 && -d "${scratch}/ioutside" ]] || fFail "installer guard took a path beside its parent: exit ${rc}, ${out}"
fIUnder "${ipar}" "${ipar}"
[[ "$rc" == 1 && -d "${ipar}" ]] || fFail "installer guard took its parent itself: exit ${rc}, ${out}"
fIUnder "${ipar}" "${ipar}/keep/../../ioutside"
[[ "$rc" == 1 && -d "${scratch}/ioutside" ]] || fFail "installer guard took a path with ..: exit ${rc}, ${out}"
if ((haveLinks)); then
	fIUnder "${ipar}" "${ipar}/link"
	[[ "$rc" == 1 && -L "${ipar}/link" ]] || fFail "installer guard took a link: exit ${rc}, ${out}"
	fIUnder "${ipar}" "${ipar}/link/f"
	[[ "$rc" == 1 && "$out" == *"through a link"* && -f "${scratch}/ioutside/f" ]] || fFail "installer guard went through a link: exit ${rc}, ${out}"
fi
fIUnder / /nonexistent-safe-rm-check
[[ "$rc" == 1 ]] || fFail "installer guard took / as its parent: exit ${rc}, ${out}"
fIUnder ipar "ipar/keep"
[[ "$rc" == 1 && -d "${ipar}/keep" ]] || fFail "installer guard took a relative parent: exit ${rc}, ${out}"
if ((haveLinks)); then
	fIUnder "${scratch}/iparlink" "${scratch}/iparlink/keep"
	[[ "$rc" == 0 && ! -e "${ipar}/keep" ]] || fFail "installer guard refused a parent behind a link: exit ${rc}, ${out}"
fi
## The prefix and what sits beside it, as root through priv.
mkdir -p "${ipar}/nemo-anywhere/bin" "${ipar}/.nemo-anywhere-install.1" "${ipar}/nemo-anywhere.old.1"
fPrivLog(){ echo "$*" >> "${scratch}/priv.log"; "$@"; }
rc=0
# shellcheck disable=SC2034  ## read by fRmBesidePrefix
out="$(priv=fPrivLog prefix="${ipar}/nemo-anywhere"; fRmBesidePrefix "${ipar}/.nemo-anywhere-install.1" "${ipar}/nemo-anywhere.old.1" "${prefix}" 2>&1)" || rc=$?
[[ "$rc" == 0 && ! -e "${ipar}/nemo-anywhere" && ! -e "${ipar}/.nemo-anywhere-install.1" && ! -e "${ipar}/nemo-anywhere.old.1" ]] || fFail "fRmBesidePrefix over the prefix, staging and backup: exit ${rc}, ${out}"
[[ "$(cat "${scratch}/priv.log" 2> /dev/null)" == "rm -rf ${oneFs} -- ${ipar}/.nemo-anywhere-install.1"*"rm -rf ${oneFs} -- ${ipar}/nemo-anywhere" ]] || fFail "the installer guard's rm didn't go through priv: $(cat "${scratch}/priv.log" 2> /dev/null)"
if ((haveLinks)); then
	fLink "${scratch}/ioutside" "${ipar}/nemo-anywhere" || fFail "made links above, but not for the prefix"
	rc=0
	out="$(prefix="${ipar}/nemo-anywhere"; fRmBesidePrefix "${prefix}" 2>&1)" || rc=$?
	[[ "$rc" == 1 && "$out" == *"prefix that's a link"* && -L "${ipar}/nemo-anywhere" && -f "${scratch}/ioutside/f" ]] || fFail "fRmBesidePrefix took a prefix that's a link: exit ${rc}, ${out}"
fi
rc=0
out="$(prefix="${ipar}/other"; fRmBesidePrefix "${ipar}/link" 2>&1)" || rc=$?
[[ "$rc" == 1 && "$out" == *"not named"* ]] || fFail "fRmBesidePrefix took a prefix not named nemo-anywhere: exit ${rc}, ${out}"
## BSD and macOS rm, then busybox's, which has neither --version nor -x.
for form in bsd busybox; do
	mkdir -p "${ipar}/${form}/sub"
	rc=0
	out="$(
		# shellcheck disable=SC2329  ## stands in for the guard's rm
		rm(){ case "$1" in --version) return 64 ;; -fx) [[ "$form" == bsd ]] || return 64 ;; -rf) shift; [[ "$1" != -x || "$form" == bsd ]] || return 64; [[ "$1" != -x ]] || shift; command rm -rf "$@" ;; *) return 64 ;; esac; }
		eval "$(sed -n -e '/^__rmOneFs=/p' -e '/^if rm --version/p' "${root}/install.bash")"
		[[ "${__rmOneFs}" == "$([[ "$form" == bsd ]] && echo -x)" ]] || { echo "rm flag ${__rmOneFs}"; exit 1; }
		fInst_SafeRm_Under "${ipar}" "${ipar}/${form}" 2>&1
	)" || rc=$?
	[[ "$rc" == 0 && ! -e "${ipar}/${form}" ]] || fFail "the installer guard with ${form} rm: exit ${rc}, ${out}"
done
unset -f realpath

if ((failures)); then
	fEcho "FAILED: safe removal, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: every rm -r goes through the guard, but the ${#left[@]} scripts listed"


##	History:
##		- 2026-10-26: Created.
##		- 2026-11-03: fSafeRm_Under, and the removals outside a temp root off the list.
##		- 2026-11-03: install.bash's own guard; the rest off the list.
##		- 2026-11-03: Real links or junctions under MSYS2, where ln -s copies,
##		  and the link cases skipped where neither can be made.
