#!/usr/bin/env bash

##	- Purpose: Check that the release script refuses artifacts built from a
##	  commit other than the one it tags. Artifacts built on dev and then merged
##	  have dev's commit date, so a rebuild of the tag would not match them and
##	  the build number in the notes would be one no binary has.
##	- Runs a copy of release.bash, tag only, in a scratch repo with a dev commit
##	  merged --no-ff into main at a later date. Stand-in artifacts are stamped
##	  with one date or the other, as the real lanes stamp them.
##	- Needs git and python3; exit 77 without them. The .deb case needs dpkg-deb,
##	  the FreeBSD pkg case zstd.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-release-stamp.bash
##	- Test ID: rjcma0tt

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

## Releases are cut on Linux, and a Windows box may only have the python3 stub.
if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "release stamp check skipped: Linux only"; exit 77; fi
for tool in git python3; do
	if ! command -v "$tool" >/dev/null 2>&1; then fEcho "release stamp check skipped: no ${tool}"; exit 77; fi
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/release-stamp-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Nothing from this box's git config: no hooks, no signing, no identity.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_CONFIG_COUNT GIT_DIR GIT_WORK_TREE

repo="${scratch}/repo"
mkdir -p "${repo}/cicd/utility/include" "${repo}/source"
cp "${root}/cicd/config.bash" "${repo}/cicd/"
cp "${root}/cicd/utility/release.bash" "${root}/cicd/utility/release-stamps.py" "${root}/cicd/utility/changelog-notes.bash" "${repo}/cicd/utility/"
cp "${root}/cicd/utility/include/source-date.bash" "${repo}/cicd/utility/include/"
printf "project('nemo-anywhere', 'c', version : '9.9.9')\n" > "${repo}/source/meson.build"
printf 'stand-in\n' > "${repo}/README.md"
printf 'cicd/artifacts/\n' > "${repo}/.gitignore"

devDate=1790000000
mergeDate=1790086400
fGit(){ git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false -c core.hooksPath=/dev/null "$@"; }
fGit init -q -b main
fGit add -A
GIT_AUTHOR_DATE="@$((devDate - 86400))" GIT_COMMITTER_DATE="@$((devDate - 86400))" fGit commit -q -m base
fGit checkout -q -b dev
printf 'dev change\n' >> "${repo}/README.md"
GIT_AUTHOR_DATE="@${devDate}" GIT_COMMITTER_DATE="@${devDate}" fGit commit -q -am change
fGit checkout -q main
GIT_AUTHOR_DATE="@${mergeDate}" GIT_COMMITTER_DATE="@${mergeDate}" fGit merge -q --no-ff -m "Merge dev" dev
[[ "$(fGit log -1 --format=%ct)" == "$mergeDate" ]] || { fEcho "FAILED: scratch merge has the wrong date"; exit 1; }

art="${repo}/cicd/artifacts/release"
ver="9.9.9"

## $1 tarball stamp, $2 zip exe stamp, $3 .deb stamp or empty for none, $4
## FreeBSD pkg stamp or empty for none.
fArtifacts(){
	rm -rf "$art"; mkdir -p "$art" "${scratch}/pfx/nemo-anywhere-${ver}-linux-x86_64/bin"
	printf 'x\n' > "${scratch}/pfx/nemo-anywhere-${ver}-linux-x86_64/bin/nemo-anywhere"
	tar -czf "${art}/nemo-anywhere-${ver}-linux-x86_64.tar.gz" -C "${scratch}/pfx" \
		--owner=0 --group=0 --numeric-owner --sort=name --mtime="@$1" "nemo-anywhere-${ver}-linux-x86_64"
	## A stand-in PE header is enough: the check reads the stamp and nothing else.
	python3 - "${art}/nemo-anywhere-${ver}-windows-x86_64.zip" "$2" <<-'EOF'
		import struct, sys, zipfile
		pe = bytearray(0x80)
		pe[0:2] = b"MZ"
		struct.pack_into("<I", pe, 0x3c, 0x40)
		pe[0x40:0x44] = b"PE\0\0"
		struct.pack_into("<I", pe, 0x48, int(sys.argv[2]))
		with zipfile.ZipFile(sys.argv[1], "w") as z:
		    z.writestr("nemo-anywhere-9.9.9-windows-x86_64/nemo-anywhere.exe", bytes(pe))
		    other = bytearray(pe)
		    struct.pack_into("<I", other, 0x48, 1000)
		    z.writestr("nemo-anywhere-9.9.9-windows-x86_64/gdbus.exe", bytes(other))
	EOF
	if [[ -n "${3:-}" ]]; then
		rm -rf "${scratch}/deb"; mkdir -p "${scratch}/deb/DEBIAN" "${scratch}/deb/opt"
		printf 'x\n' > "${scratch}/deb/opt/f"
		printf 'Package: nemo-anywhere\nVersion: %s\nArchitecture: amd64\nMaintainer: x <x@example.invalid>\nDescription: x\n' "$ver" > "${scratch}/deb/DEBIAN/control"
		SOURCE_DATE_EPOCH="$3" dpkg-deb --build --root-owner-group "${scratch}/deb" "${art}/nemo-anywhere-${ver}-linux-x86_64.deb" >/dev/null
	fi
	## A pkg is a tar.zst with its manifest first. pkg create dates the files
	## and leaves its own +MANIFEST at 0.
	if [[ -n "${4:-}" ]]; then
		rm -rf "${scratch}/pkg"; mkdir -p "${scratch}/pkg/usr/local/bin"
		printf '{}\n' > "${scratch}/pkg/+MANIFEST"
		printf 'x\n' > "${scratch}/pkg/usr/local/bin/nemo-anywhere"
		tar -cf "${scratch}/pkg.tar" -C "${scratch}/pkg" --owner=0 --group=0 --numeric-owner --mtime="@0" +MANIFEST
		tar -rf "${scratch}/pkg.tar" -C "${scratch}/pkg" --owner=0 --group=0 --numeric-owner --mtime="@$4" usr
		zstd -q -f "${scratch}/pkg.tar" -o "${art}/nemo-anywhere-${ver}-bsd-x86_64.pkg"
	fi
	## Named .pkg too, but no tar: the check has no reader for it and must not fail it.
	printf 'xar!stand-in\n' > "${art}/nemo-anywhere-${ver}-macos-x86_64.pkg"
	( cd "$art" && sha256sum nemo-anywhere-${ver}-* > "nemo-anywhere-${ver}-sha256sums.txt" )
}

## $1 "refuses" or "tags", $2 what the case is.
fRelease(){
	local out rc=0
	out="$(cd "$repo" && bash cicd/utility/release.bash -y 2>&1)" || rc=$?
	if [[ "$1" == refuses ]]; then
		if [[ "$rc" == 0 ]]; then fFail "${2}: tagged anyway; said: ${out}"
		elif fGit rev-parse -q --verify "refs/tags/v${ver}" >/dev/null; then fFail "${2}: failed but left a tag"
		elif [[ "$out" != *"not stamped ${mergeDate}"* ]]; then fFail "${2}: failed for another reason: ${out}"
		else fEcho "OK: ${2}: refused"
		fi
	else
		if [[ "$rc" != 0 ]]; then fFail "${2}: refused (exit ${rc}): ${out}"
		elif ! fGit rev-parse -q --verify "refs/tags/v${ver}" >/dev/null; then fFail "${2}: no tag made"
		else fEcho "OK: ${2}: tagged"
		fi
	fi
}

fArtifacts "$devDate" "$devDate"
fRelease refuses "built on dev before the merge"
fArtifacts "$mergeDate" "$devDate"
fRelease refuses "Windows exe from dev, tarball from main"
haveDeb=""
if command -v dpkg-deb >/dev/null 2>&1; then
	haveDeb="$mergeDate"
	fArtifacts "$mergeDate" "$mergeDate" "$devDate"
	fRelease refuses ".deb from dev, the rest from main"
else
	fEcho ".deb case skipped: no dpkg-deb"
fi
havePkg=""
if command -v zstd >/dev/null 2>&1; then
	havePkg="$mergeDate"
	fArtifacts "$mergeDate" "$mergeDate" "$haveDeb" "$devDate"
	fRelease refuses "FreeBSD pkg from dev, the rest from main"
else
	fEcho "FreeBSD pkg case skipped: no zstd"
fi
fArtifacts "$mergeDate" "$mergeDate" "$haveDeb" "$havePkg"
fRelease tags "all built from the merge"

if ((failures)); then fEcho "${failures} release stamp check(s) failed"; exit 1; fi
fEcho "release stamp checks passed"
