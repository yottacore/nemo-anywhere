#!/usr/bin/env bash

##	- Purpose: One check on the gdk-pixbuf loaders.cache a Windows bundle
##	  carries. Missing or empty, gdk-pixbuf finds no loaders at all, so every
##	  SVG, and with it every symbolic icon, fails to draw. The packing scripts
##	  used to pass that with a warning, or with nothing said.
##	- fCheckLoadersCache <file> -> prints why and returns 1 when the cache is
##	  missing, empty, or names no loader.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fCheckLoadersCache(){
	local cache="$1"
	if [[ ! -s "$cache" ]]; then
		echo "FAILED: gdk-pixbuf loaders.cache is missing or empty: ${cache}" >&2
		return 1
	fi
	## A loader entry opens with its quoted dll path. Written on Windows, the
	## line may end in a carriage return.
	if ! grep -q -i -E '\.dll"[[:space:]]*$' "$cache"; then
		echo "FAILED: gdk-pixbuf loaders.cache names no loader: ${cache}" >&2
		return 1
	fi
	return 0
}
