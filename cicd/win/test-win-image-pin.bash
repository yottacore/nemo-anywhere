#!/usr/bin/env bash

##	- Purpose: Check that the Windows cross build image is pinned: its base by
##	  digest and apt on one dated snapshot, by the same checks as the day-to-day
##	  image's (dejma), and its sysroot from sysroot-pins.txt alone. That list is
##	  well formed, has every root, and is part of msys2-pins.txt line for line,
##	  so the cross build packs the hosted build's DLLs. fetch-sysroot.bash reads
##	  no package database, unpacks the pinned files from a stand-in repo, and
##	  refuses a bad checksum or line before it unpacks anything.
##	- Runs in the lint stage.
##	- Syntax: cicd/win/test-win-image-pin.bash
##	- Test ID: depf5

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
dockerfile="${WIN_DOCKERFILE:-${here}/Dockerfile}"
fetch="${WIN_FETCH_SYSROOT:-${here}/fetch-sysroot.bash}"
sysPins="${WIN_SYSROOT_PINS:-${here}/sysroot-pins.txt}"
pins="${here}/msys2-pins.txt"
# shellcheck source=../utility/include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d)"
trap 'fSafeRm_IfThere "${scratch}"' EXIT

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	The Dockerfile
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

if ! out="$(DEV_DOCKERFILE="${dockerfile}" bash "${root}/cicd/linux/test-dev-image-pin.bash" 2>&1)"; then
	while IFS= read -r line; do line="${line#*FAILED: }"; fFail "${line% ]}"; done < <(grep 'FAILED: ' <<<"${out}" | grep -v 'check(s) failed')
fi
text="$(grep -v '^[[:space:]]*#' "${dockerfile}")"
grep -qE '^COPY .*win/sysroot-pins\.txt' <<<"${text}" || fFail "the Dockerfile doesn't copy sysroot-pins.txt"
grep -qE '^RUN .*fetch-sysroot\.bash' <<<"${text}" || fFail "the Dockerfile doesn't run fetch-sysroot.bash"
! grep -qE 'mingw64\.db|\.db"' "${fetch}" || fFail "fetch-sysroot.bash reads a package database"

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	The list
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

fPkgName(){ sed -E 's/-[^-]+-[^-]+-any\.pkg\.tar\.zst$//' <<<"$1"; }
declare -A names=()
n=0
[[ -r "${sysPins}" ]] || { fFail "no ${sysPins}"; sysPins=/dev/null; }
while read -r repo file sha; do
	[[ -z "${repo}" || "${repo}" == \#* ]] && continue
	n=$((n + 1))
	[[ "${repo}" == mingw/mingw64 && "${file}" == mingw-w64-x86_64-*-any.pkg.tar.zst && "${sha}" =~ ^[0-9a-f]{64}$ ]] \
		|| fFail "bad line in sysroot-pins.txt: ${repo} ${file} ${sha}"
	grep -qxF "${repo} ${file} ${sha}" "${pins}" || fFail "not in msys2-pins.txt as it is here: ${file}"
	name="$(fPkgName "${file}")"
	[[ -z "${names[$name]:-}" ]] || fFail "listed twice: ${name}"
	names[$name]=1
done < "${sysPins}"
((n > 0)) || fFail "sysroot-pins.txt lists no packages"
# shellcheck disable=SC2016 # the array is read in the subshell, on purpose
roots="$(bash -c 'source "$1"; printf "%s\n" "${SYSROOT_ROOTS[@]}"' _ "${here}/msys2-pins.bash" 2>/dev/null || true)"
[[ -n "${roots}" ]] || fFail "msys2-pins.bash names no SYSROOT_ROOTS"
while IFS= read -r r; do
	[[ -z "${r}" ]] && continue
	[[ -n "${names[mingw-w64-x86_64-${r}]:-}" ]] || fFail "root not in sysroot-pins.txt: ${r}"
done <<<"${roots}"

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	fetch-sysroot.bash against a stand-in repo
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

repoDir="${scratch}/repo/mingw/mingw64"
mkdir -p "${repoDir}" "${scratch}/pkg-a/mingw64/bin" "${scratch}/pkg-b/mingw64/lib"
echo a > "${scratch}/pkg-a/mingw64/bin/a.dll"
echo 'pkgname = a' > "${scratch}/pkg-a/.PKGINFO"
echo b > "${scratch}/pkg-b/mingw64/lib/b.a"
tar --zstd -cf "${repoDir}/mingw-w64-x86_64-a-1.0-1-any.pkg.tar.zst" -C "${scratch}/pkg-a" .PKGINFO mingw64
tar --zstd -cf "${repoDir}/mingw-w64-x86_64-b-2.0-1-any.pkg.tar.zst" -C "${scratch}/pkg-b" mingw64
shaA="$(sha256sum "${repoDir}/mingw-w64-x86_64-a-1.0-1-any.pkg.tar.zst" | cut -d' ' -f1)"
shaB="$(sha256sum "${repoDir}/mingw-w64-x86_64-b-2.0-1-any.pkg.tar.zst" | cut -d' ' -f1)"
good="${scratch}/good.txt"
printf '# a comment\nmingw/mingw64 mingw-w64-x86_64-a-1.0-1-any.pkg.tar.zst %s\nmingw/mingw64 mingw-w64-x86_64-b-2.0-1-any.pkg.tar.zst %s\n' "${shaA}" "${shaB}" > "${good}"

fFetch(){ MSYS2_REPO_ROOT="file://${scratch}/repo" bash "${fetch}" "$1" "$2" > "${scratch}/fetch.log" 2>&1; }

if fFetch "${scratch}/sys-good" "${good}"; then
	[[ -f "${scratch}/sys-good/mingw64/bin/a.dll" && -f "${scratch}/sys-good/mingw64/lib/b.a" ]] || fFail "[good list]: not unpacked"
	[[ ! -e "${scratch}/sys-good/.PKGINFO" ]] || fFail "[good list]: pacman metadata unpacked"
else
	fFail "[good list]: failed: $(tail -1 "${scratch}/fetch.log")"
fi

sed "s/${shaB}/${shaA}/" "${good}" > "${scratch}/badsum.txt"
if fFetch "${scratch}/sys-badsum" "${scratch}/badsum.txt"; then fFail "[bad checksum]: passed"; fi
[[ ! -e "${scratch}/sys-badsum" ]] || fFail "[bad checksum]: unpacked something"

## Fetchable and with the right checksum, so only the line check can stop it.
cp "${repoDir}/mingw-w64-x86_64-a-1.0-1-any.pkg.tar.zst" "${scratch}/repo/mingw/evil.pkg.tar.zst"
printf 'mingw/mingw64 ../evil.pkg.tar.zst %s\n' "${shaA}" > "${scratch}/badline.txt"
if fFetch "${scratch}/sys-badline" "${scratch}/badline.txt"; then fFail "[bad line]: passed"; fi

printf '# nothing\n' > "${scratch}/empty.txt"
if fFetch "${scratch}/sys-empty" "${scratch}/empty.txt"; then fFail "[empty list]: passed"; fi

if ((failures)); then fEcho "${failures} Windows image pin check(s) failed"; exit 1; fi
fEcho "Windows image pin checks passed"


##	History:
##		- 2026-10-31: Created.
