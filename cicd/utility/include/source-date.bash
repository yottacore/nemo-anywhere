#!/usr/bin/env bash

##	- Purpose: One answer to "what time is this build", so 2 builds of the same
##	  commit produce the same bytes. Everything that would otherwise stamp the
##	  clock reads SOURCE_DATE_EPOCH: the PE header the linker writes into the
##	  Windows exe, dpkg-deb, rpmbuild, and our own tar and zip.
##	- fSetSourceDate [repo-root] -> exports SOURCE_DATE_EPOCH from HEAD's commit
##	  date. Already set wins, so CI or a caller can pin it.
##	- fWarnIfSourceDateIsAGuess [repo-root] -> one warning line when the tree has
##	  changes HEAD doesn't describe. For the release lanes only.
##	- fPeTimestamp <file> -> the timestamp inside a PE image, for checking one.
##	- Syntax: source this file; it defines functions only.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fSetSourceDate(){
	local root="${1:-.}" ct
	[[ -z "${SOURCE_DATE_EPOCH:-}" ]] || { export SOURCE_DATE_EPOCH; return 0 ;}
	ct="$(git -C "${root}" log -1 --format=%ct 2>/dev/null)" || ct=""
	## Outside a checkout there is no commit to read. Zero is still deterministic,
	## and beats a bare empty string that makes tar fail partway through.
	export SOURCE_DATE_EPOCH="${ct:-0}"
}

## A dirty tree still gets HEAD's date, since the clock would be worse, but
## nobody can reproduce it from that commit. Only the release lanes
## call this: day-to-day builds run against a tree with edits in it and the
## warning would be noise.
fWarnIfSourceDateIsAGuess(){
	local root="${1:-.}"
	[[ -n "$(git -C "${root}" status --porcelain 2>/dev/null)" ]] || return 0
	echo "[ WARNING: uncommitted changes - artifacts carry HEAD's date but are not HEAD ]" >&2
}

## The timestamp the linker wrote into a PE image, or nothing if the file isn't
## one. Used to catch an artifact packed from a stale build. The python is flat on
## purpose: a <<- heredoc eats leading tabs, which would break any indented block.
fPeTimestamp(){
	python3 - "$1" <<-'EOF' 2>/dev/null || true
		import struct, sys
		d = open(sys.argv[1], "rb").read()
		p = struct.unpack_from("<I", d, 0x3c)[0] if d[:2] == b"MZ" else 0
		sys.stdout.write(str(struct.unpack_from("<I", d, p + 8)[0]) if d[p:p + 4] == b"PE\0\0" else "")
	EOF
}


##	History:
##		- 2026-08-26: Created.
