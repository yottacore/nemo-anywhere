#!/usr/bin/env bash

##	- Purpose: Copy thumbnailer descriptors into a Windows bundle.
##	  - The ones run by gdk-pixbuf-thumbnailer (gdk-pixbuf's own, librsvg's)
##	    are left out. The app draws the same pictures and SVG files itself,
##	    with no program per file, and in the single exe a program packed
##	    inside it can hit the packer's error box (2026100617051745).
##	  - The rest get bare program names. MSYS2 ships some with
##	    `/mingw64/bin/...`, which names nothing on a real Windows box.
##	  - fetch-sysroot.bash does the same to the cross sysroot with its own
##	    commands, since it runs alone inside the image build.
##	- fIsPixbufThumbnailer <file> -> true when its TryExec or Exec program is
##	  gdk-pixbuf-thumbnailer, with or without a folder or `.exe`.
##	- fStageThumbnailers <src-dir> <dest-dir> -> copies every other
##	  *.thumbnailer, strips the folder off TryExec and Exec, and returns 1 when
##	  one still names a folder.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fIsPixbufThumbnailer(){
	grep -q -i -E '^(TryExec|Exec)=([^ ]*[/\\])?gdk-pixbuf-thumbnailer(\.exe)?([[:space:]]|$)' "$1"
}

fStageThumbnailers(){
	local src="$1" dest="$2"
	local -a descriptors=()
	local d
	[[ -d "$src" ]] || return 0
	mkdir -p "$dest"
	for d in "$src"/*.thumbnailer; do
		[[ -f "$d" ]] || continue
		if fIsPixbufThumbnailer "$d"; then continue; fi
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
