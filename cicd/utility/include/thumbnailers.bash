#!/usr/bin/env bash

##	- Purpose: Copy thumbnailer descriptors into a Windows bundle with bare
##	  program names. MSYS2 ships some with `/mingw64/bin/...`, which names
##	  nothing on a real Windows box, so the app skips them as missing.
##	  fetch-sysroot.bash does the same for the cross sysroot with its own sed,
##	  since it runs alone inside the image build.
##	- fStageThumbnailers <src-dir> <dest-dir> -> copies every *.thumbnailer,
##	  strips the folder off TryExec and Exec, and returns 1 when one still
##	  names a folder.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fStageThumbnailers(){
	local src="$1" dest="$2"
	local -a descriptors=()
	local d
	[[ -d "$src" ]] || return 0
	mkdir -p "$dest"
	for d in "$src"/*.thumbnailer; do
		[[ -f "$d" ]] || continue
		cp "$d" "$dest/"
		descriptors+=("$dest/${d##*/}")
	done
	((${#descriptors[@]})) || return 0
	## The program is found beside the app or on PATH, the same as a bare name
	## in the cross build.
	sed -i -E 's#^(TryExec|Exec)=/[^ ]*/#\1=#' "${descriptors[@]}"
	if grep -l -E '^(TryExec|Exec)=/' "${descriptors[@]}" >&2; then
		echo "FAILED: thumbnailer descriptors above still name a folder" >&2
		return 1
	fi
	return 0
}
