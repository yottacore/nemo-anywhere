#!/usr/bin/env bash

##	- Purpose: run one script test or tool self-test, and print one line for it
##	  that starts with its status, then its ID and name, as in
##	  "PASS defnq test-safe-rm.bash". The ID comes from the "Test ID:" line in
##	  the script, the one test-id.py checks.
##	- fTest <command...> -> runs the command. The script is the first argument
##	  that is a .bash, .ps1 or .py file, and the name is that file's name plus
##	  the arguments after it. Exit 0 is PASS and 77 SKIP, and both return 0.
##	  Anything else prints FAIL and exits the caller with that code.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fTest(){
	local rc=0 arg='' script='' name='' id='' i=0
	for arg in "$@"; do
		i=$((i + 1))
		if [[ "${arg}" == *.bash || "${arg}" == *.ps1 || "${arg}" == *.py ]] && [[ -f "${arg}" ]]; then
			script="${arg}"; name="${arg##*/}"
			(( i < $# )) && name+=" ${*:$((i + 1))}"
			break
		fi
	done
	[[ -n "${script}" ]] && id="$(sed -n -E 's/^(#+[[:space:]]+(- )?)?Test ID: ([0-9a-z]+)$/\3/p' "${script}" | head -n 1)"
	"$@" || rc=$?
	case "${rc}" in
		0)  echo "PASS ${id:-?} ${name:-$*}" ;;
		77) echo "SKIP ${id:-?} ${name:-$*}" ;;
		*)  echo "FAIL ${id:-?} ${name:-$*}"; exit "${rc}" ;;
	esac
}
