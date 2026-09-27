#!/usr/bin/env bash

##	- Purpose: Keep the two Windows lanes asking for a release build. Every tag
##	  cut before 20260919 published a debug exe: neither lane passed a buildtype,
##	  and meson's default is debug, so the signed artifact carried full DWARF at
##	  15.7 MB against 3.0 MB for the Linux release binary beside it.
##	- Two halves. The text half reads the meson setup line out of each lane and
##	  fails on a missing flag; it is the only thing that can watch the hosted
##	  workflow, which runs on a release tag and nowhere else. The artifact half
##	  reads a built exe and fails on a size only a debug build reaches, so the
##	  check sits on the path that makes the file.
##	- --shipped adds the stricter half: no debug sections at all. meson's
##	  -Dstrip=true only runs on install and neither Windows lane installs, so
##	  build-cross.bash and stage-native.bash strip what they produce and call
##	  this on it. mingw's own static objects carry debug information even in a
##	  release link, so an unstripped exe will not pass --shipped.
##	- The artifact half also reads back what the exe has to carry, each of which
##	  has gone missing before: the compiled-in resource bundle (without it there
##	  is no menu bar and every .ui lookup fails), the manifest with long paths
##	  and the UTF-8 code page, the GUI subsystem (a console one opens a console
##	  window behind the app), and the version resource with a FileVersion that
##	  matches source/meson.build and a CompanyName.
##	- The artifact half is skipped when no exe is named, and when objdump or
##	  windres cannot be found, the same way lint-c.bash skips a missing
##	  cppcheck. STRICT=1 turns any skip into a failure. Nothing is looked for by
##	  default: the stale artifacts of an earlier run are not what a bare run
##	  should be judging.
##	- Syntax: check-win-build-flags.bash [--shipped] [path-to-exe]
##	- Test ID: rh8vbs38

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

shipped=0
case "${1:-}" in
	--shipped) shipped=1; shift ;;
esac
exe="${1:-}"
strict="${STRICT:-0}"
## A debug link is 15.7 MB. Stripped release is 8.3 MB. Anything past this is
## debug information, whatever the section names say.
maxBytes=$(( 12 * 1024 * 1024 ))
failures=0

fEcho(){ echo "[ $* ]"; }
fFail(){ fEcho "FAIL: $*"; failures=$(( failures + 1 )); }
fSkip(){ if [[ "$strict" == 1 ]]; then fFail "$*"; else fEcho "SKIP: $*"; fi ;}

## Pull the meson setup line out of a lane and check it carries every flag. The
## lanes are written by hand in two different languages, so matching the command
## rather than the surrounding syntax is what keeps this working across both.
fCheckLane(){
	local file="$1"; shift
	local line flag

	[[ -f "$file" ]] || { fFail "${file}: not found"; return; }

	line="$(grep -n -F 'meson setup' "$file" | head -1 || true)"
	[[ -n "$line" ]] || { fFail "${file}: no meson setup line"; return; }

	for flag in "$@"; do
		case "$line" in
			*"$flag"*) ;;
			*) fFail "${file}:${line%%:*}: meson setup is missing ${flag}" ;;
		esac
	done
}

## The manifest and the version block, read back through windres. Held in a
## variable and searched there: a reader that stops at its first match would
## kill the writer upstream of it.
fCheckResources(){
	local path="$1"
	local windres="" candidate rc manifest ver

	for candidate in x86_64-w64-mingw32-windres windres; do
		if command -v "$candidate" >/dev/null 2>&1; then windres="$candidate"; break; fi
	done
	if [[ -z "$windres" ]]; then
		fSkip "windres not found, so the resources in ${path} were not read"
		return 0
	fi

	rc="$("$windres" -i "$path" -O rc 2>/dev/null || true)"
	## Resource 1 of type 24 is the only manifest Windows reads from an exe.
	manifest="$(awk '/^1 24 /{ inside = 1 } inside { print } inside && /^END/{ exit }' <<<"$rc")"
	if [[ -z "$manifest" ]]; then
		fFail "${path}: no application manifest (resource 1, type 24)"
	else
		[[ "$manifest" == *'>true</longPathAware>'* ]] || fFail "${path}: manifest does not declare longPathAware"
		[[ "$manifest" == *'>UTF-8</activeCodePage>'* ]] || fFail "${path}: manifest does not set the UTF-8 code page"
	fi

	ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" source/meson.build | head -1 || true)"
	grep -q -F "VALUE \"FileVersion\", \"${ver}\"" <<<"$rc" \
		|| fFail "${path}: version resource has no FileVersion ${ver}"
	grep -q -E 'VALUE "CompanyName", L?"[^"]+"' <<<"$rc" \
		|| fFail "${path}: version resource has no CompanyName"
	return 0
}

fCheckExe(){
	local path="$1"
	local bytes sections headers subsystem

	if ! command -v objdump >/dev/null 2>&1; then
		fSkip "objdump not found, so ${path} was not read"
		return
	fi

	bytes="$(stat -c %s "$path" 2>/dev/null || wc -c < "$path")"
	if (( bytes > maxBytes )); then
		fFail "${path}: ${bytes} bytes, over the ${maxBytes} ceiling - this is a debug link"
	fi

	headers="$(objdump -p "$path" 2>/dev/null || true)"
	subsystem="$(awk '$1 == "Subsystem" { print $2 }' <<<"$headers")"
	[[ "$subsystem" == "00000002" ]] || fFail "${path}: PE subsystem is ${subsystem:-unreadable}, expected 00000002 (Windows GUI)"

	## Content of nemo-shell-ui.xml, which only the bundle carries. Its resource
	## path is no proof: the code that looks it up spells that out too.
	LC_ALL=C grep -a -q -F '<menubar name="MenuBar">' "$path" \
		|| fFail "${path}: the compiled-in resource bundle is missing (no nemo-shell-ui.xml content)"

	fCheckResources "$path"

	(( shipped )) || return 0

	sections="$(objdump -h "$path" 2>/dev/null | grep -c '\.debug_' || true)"
	if [[ "$sections" != 0 ]]; then
		fFail "${path}: ${sections} debug sections, expected none in a shipped exe"
		objdump -h "$path" 2>/dev/null | grep '\.debug_' || true
	fi
}

fCheckLane 'cicd/win/build-cross.bash' '--buildtype=release' '-Dstrip=true' '-Db_lto=true'
fCheckLane '.github/workflows/release-win.yml' '--buildtype=release' '-Dstrip=true' '-Db_lto=true'
## The Linux lane has always passed the first two. Checked here so all three stay together.
fCheckLane 'cicd/linux/release.bash' '--buildtype=release' '-Dstrip=true' '-Db_lto=true'

if [[ -n "$exe" && -f "$exe" ]]; then
	fCheckExe "$exe"
elif [[ -n "$exe" ]]; then
	fFail "${exe}: not found"
else
	fSkip "no built exe to read"
fi

if (( failures > 0 )); then
	fEcho "FAILED: Windows build flags: ${failures} problem(s)"
	exit 1
fi
fEcho "OK: Windows build flags"

##	History:
##		- 2026-09-19: Created.
