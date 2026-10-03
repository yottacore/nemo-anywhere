#!/usr/bin/env bash

##	- Purpose: Check that the .deb's Installed-Size is the one dpkg-gencontrol
##	  would write, and that the same tarball packs to the same .deb whatever
##	  filesystem the work dir is on.
##	- Packs a small stand-in prefix with a copy of package.bash, .deb only, once
##	  under the temp dir and once under /dev/shm when that is another filesystem.
##	  The release container is named as one that does not exist, so the
##	  dependency line falls back to its fixed list.
##	- Needs dpkg-deb; exit 77 without it. Without dpkg-gencontrol the expected
##	  size is counted here instead.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-deb-size.bash
##	- Test ID: rjcma0se

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

if ! command -v dpkg-deb >/dev/null 2>&1; then
	fEcho "deb size check skipped: no dpkg-deb"
	exit 77
fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/deb-size-check.XXXXXX")"
shm=""
cleanup(){ rm -rf "${scratch}"; if [[ -n "$shm" ]]; then rm -rf "${shm}"; fi; }
trap cleanup EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## A copy of the lane, so nothing lands in the real release dir.
lane="${scratch}/repo"
mkdir -p "${lane}/cicd/linux" "${lane}/cicd/utility/include" "${lane}/cicd/artifacts/release"
cp "${root}/cicd/linux/package.bash" "${lane}/cicd/linux/"
cp "${root}/cicd/utility/include/echo.bash" "${root}/cicd/utility/include/source-date.bash" "${lane}/cicd/utility/include/"

## Sizes either side of a KiB and of a disk block, a symlink and a hardlink pair,
## so a count of blocks, or one that counts a hardlink twice, comes out different.
name="nemo-anywhere-9.9.9-linux-x86_64"
pfx="${scratch}/src/${name}"
mkdir -p "${pfx}/bin" "${pfx}/share/applications" "${pfx}/share/icons/hicolor/48x48/apps" "${pfx}/share/doc"
head -c 5000 /dev/zero > "${pfx}/bin/nemo-anywhere"; chmod 755 "${pfx}/bin/nemo-anywhere"
printf '[Desktop Entry]\nName=Nemo Anywhere\nExec=nemo-anywhere %%U\nIcon=nemo-anywhere\n' > "${pfx}/share/applications/nemo-anywhere.desktop"
head -c 300 /dev/zero > "${pfx}/share/icons/hicolor/48x48/apps/nemo-anywhere.png"
for size in 0 1 1024 1025 4096 4097 70000; do head -c "$size" /dev/zero > "${pfx}/share/doc/f${size}"; done
ln -s f70000 "${pfx}/share/doc/link"
ln "${pfx}/share/doc/f4097" "${pfx}/share/doc/hard"
tar -czf "${lane}/cicd/artifacts/release/${name}.tar.gz" -C "${scratch}/src" \
	--owner=0 --group=0 --numeric-owner --sort=name --mtime=@1700000000 "${name}"

deb="${lane}/cicd/artifacts/release/${name}.deb"

## $1 work dir for package.bash, $2 where to keep the .deb.
fPack(){
	local out rc=0
	out="$(TMPDIR="$1" SOURCE_DATE_EPOCH=1700000000 NEMO_RELEASE_CONTAINER=no-such-container-deb-size \
		bash "${lane}/cicd/linux/package.bash" --no-rpm 2>&1)" || rc=$?
	if [[ "$rc" != 0 || ! -f "$deb" ]]; then
		fFail "package.bash with TMPDIR=$1 made no .deb (exit ${rc}): ${out}"
		return 0
	fi
	mv "$deb" "$2"
}

mkdir -p "${scratch}/work-a"
fPack "${scratch}/work-a" "${scratch}/a.deb"

## What dpkg-gencontrol would say for the same tree: unpacked, with the empty
## DEBIAN dir it is measured with. Counted by hand only where the tool is missing.
fExpected(){
	local tree="${scratch}/unpacked" ctl="${scratch}/ctl"
	rm -rf "$tree" "$ctl"; mkdir -p "$tree" "${ctl}/debian"
	dpkg-deb -x "$1" "$tree"
	mkdir -p "${tree}/DEBIAN"
	if command -v dpkg-gencontrol >/dev/null 2>&1; then
		printf 'Source: pkg\nMaintainer: x <x@example.com>\n\nPackage: pkg\nArchitecture: any\nDescription: x\n x\n' > "${ctl}/debian/control"
		printf 'pkg (1.0) unstable; urgency=low\n\n  * x\n\n -- x <x@example.com>  Thu, 01 Jan 2026 00:00:00 +0000\n' > "${ctl}/debian/changelog"
		( cd "$ctl" && dpkg-gencontrol -P"$tree" -O 2>/dev/null ) | sed -n 's/^Installed-Size: //p'
	else
		python3 - "$tree" <<-'EOF'
			import os, sys
			kb, seen = 0, set()
			for top, dirs, files in os.walk(sys.argv[1]):
			    kb += 1
			    for f in files + [d for d in dirs if os.path.islink(os.path.join(top, d))]:
			        st = os.lstat(os.path.join(top, f))
			        if (st.st_dev, st.st_ino) in seen:
			            continue
			        if st.st_nlink > 1:
			            seen.add((st.st_dev, st.st_ino))
			        kb += -(-st.st_size // 1024)
			print(kb)
		EOF
	fi
}

if [[ -f "${scratch}/a.deb" ]]; then
	want="$(fExpected "${scratch}/a.deb")"
	got="$(dpkg-deb -f "${scratch}/a.deb" Installed-Size)"
	if [[ -z "$want" ]]; then
		fFail "could not work out the expected Installed-Size"
	elif [[ "$got" != "$want" ]]; then
		fFail "Installed-Size is ${got}, dpkg would write ${want}"
	else
		fEcho "OK: Installed-Size ${got}"
	fi

	## The same tarball again with its work dir on another filesystem.
	if [[ -d /dev/shm && -w /dev/shm && "$(stat -c %d /dev/shm)" != "$(stat -c %d "${scratch}")" ]]; then
		shm="$(mktemp -d /dev/shm/deb-size-check.XXXXXX)"
		fPack "$shm" "${scratch}/b.deb"
		if [[ -f "${scratch}/b.deb" ]]; then
			if cmp -s "${scratch}/a.deb" "${scratch}/b.deb"; then
				fEcho "OK: same .deb from $(stat -f -c %T "${scratch}") and $(stat -f -c %T /dev/shm)"
			else
				fFail "the .deb differs between $(stat -f -c %T "${scratch}") and $(stat -f -c %T /dev/shm): Installed-Size $(dpkg-deb -f "${scratch}/a.deb" Installed-Size) and $(dpkg-deb -f "${scratch}/b.deb" Installed-Size)"
			fi
		fi
	else
		fEcho "second filesystem pass skipped: no /dev/shm apart from the temp dir"
	fi
fi

if ((failures)); then fEcho "${failures} deb size check(s) failed"; exit 1; fi
fEcho "deb size checks passed"
