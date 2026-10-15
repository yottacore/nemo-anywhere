#!/usr/bin/env bash

##	- Purpose: The names and the sums file the release lanes share, so the
##	  Linux x86_64 and arm64 lanes and the FreeBSD one cannot drift apart from
##	  each other or from what the installers ask for (design.md, Delivery).
##	- fReleaseArch <uname -m> -> the arch part of an asset name: x86_64 or
##	  arm64. Returns 1 for anything else. The installers map the same way.
##	- fReleaseOs <uname -s> -> the OS part of a unix asset name: linux or bsd.
##	  Returns 1 for anything else. Every BSD maps to bsd, as in the installers,
##	  though only FreeBSD is built.
##	- fWriteReleaseSums <dir> <slug> <ver> -> rewrites <slug>-<ver>-sha256sums.txt
##	  in <dir> over every <slug>-<ver>-* file there.
##	- fDebDependsFile <repo root> <tarball name> -> where the .deb's Depends
##	  line for a tarball built on another box is kept. Outside the release dir,
##	  so it is never summed or uploaded.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fReleaseArch(){
	case "${1:-}" in
		x86_64|amd64)  echo "x86_64" ;;
		aarch64|arm64) echo "arm64" ;;
		*) return 1 ;;
	esac
}

fReleaseOs(){
	case "${1,,}" in
		linux*) echo "linux" ;;
		freebsd*|openbsd*|netbsd*|dragonfly*) echo "bsd" ;;
		*) return 1 ;;
	esac
}

## One sums file per release covering this version's artifacts - the installers grep
## it for their own asset's line, so extra lines (the Windows exe) are free. Matching
## on the version keeps a leftover build for another version out of it.
fWriteReleaseSums(){
	local dir="$1" slug="$2" ver="$3"
	local sums="${slug}-${ver}-sha256sums.txt" tmpsums
	tmpsums="$(mktemp)"
	rm -f "${dir:?}/${sums}"
	## -0/-r: without them an empty dir still runs sha256sum, which then reads stdin
	## and writes a bogus "-" line into the file the installers verify against, and a
	## name with a space would be split into two arguments.
	( cd "$dir" && find . -maxdepth 1 -type f -name "${slug}-${ver}-*" -printf '%P\0' | sort -z | xargs -0 -r sha256sum ) > "$tmpsums"
	mv "$tmpsums" "${dir}/${sums}"
	chmod 644 "${dir}/${sums}"	# mktemp makes it 0600
}

## The arm64 lane writes it, package.bash reads it. First line the tarball's
## sha256, so a list read off another build is never used.
fDebDependsFile(){ printf '%s/cicd/artifacts/deb-depends/%s.txt\n' "$1" "${2%.tar.gz}"; }


##	History:
##		- 2026-10-05: Created, out of release.bash, for the arm64 lane.
##		- 2026-10-07: fReleaseOs, for the FreeBSD lane.
##		- 2026-10-07: fDebDependsFile, for arm64 packages.
