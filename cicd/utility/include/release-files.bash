#!/usr/bin/env bash

##	- Purpose: The names and the sums file the Linux release lanes share, so the
##	  x86_64 and arm64 lanes cannot drift apart from each other or from what
##	  the installers ask for (design.md, Delivery).
##	- fReleaseArch <uname -m> -> the arch part of an asset name: x86_64 or
##	  arm64. Returns 1 for anything else. The installers map the same way.
##	- fWriteReleaseSums <dir> <slug> <ver> -> rewrites <slug>-<ver>-sha256sums.txt
##	  in <dir> over every <slug>-<ver>-* file there.
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


##	History:
##		- 2026-10-05: Created, out of release.bash, for the arm64 lane.
