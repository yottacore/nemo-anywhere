#!/usr/bin/env bash

##	- Purpose: Run the Windows setup exe under wine, silently, in a scratch
##	  prefix: install, install again over it, then uninstall. Checks the files
##	  match the zip's, the Start menu shortcut, the Settings, Apps entry, and
##	  that the user PATH comes back exactly as it was found, including one too
##	  long to edit and one that only just fits.
##	- Runs inside the cross-build container:
##	  docker exec nemo-winbuild bash /src/cicd/win/test-setup.bash
##	- No setup exe or no zip beside it: exit 77.
##	- What wine can't show is checked on a Windows box by hand: a folder held
##	  open, the pages, and install.ps1 over it and under it.
##	- Test ID: rjphqx48

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
ulimit -c 0

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fEcho(){ echo "[ $* ]"; }

readonly exeName="nemo-anywhere" appName="Nemo Anywhere"
ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${root}/source/meson.build" | head -1 || true)"
rel="${root}/cicd/artifacts/release"
setup="${rel}/${exeName}-${ver}-windows-x86_64-setup.exe"
zip="${rel}/${exeName}-${ver}-windows-x86_64.zip"
if [[ -z "$ver" || ! -f "$setup" || ! -f "$zip" ]]; then
	fEcho "setup check skipped: no ${setup##*/} and zip (run pack-zip.bash and pack-setup.bash first)"
	exit 77
fi

scratch="$(mktemp -d /tmp/test_setup_XXXXXXXX)"
export WINEPREFIX="${scratch}/wine" WINEDEBUG=-all XDG_RUNTIME_DIR="$scratch"
trap 'wineserver -k 2>/dev/null || true; rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

wineboot -i >/dev/null 2>&1 || true
wineserver -w

users="${WINEPREFIX}/drive_c/users/$(whoami)"
prefix="${users}/AppData/Local/Programs/${appName}"
shortcut="${users}/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/${appName}.lnk"
winPrefix="C:\\users\\$(whoami)\\AppData\\Local\\Programs\\${appName}"
arp='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\nemo-anywhere'

## "<type> <value>" of a value, or nothing when it is not there. reg prints
## four spaces between name, type and value.
fRegGet(){
	local key="$1" name="$2" line
	line="$(wine reg query "$key" /v "$name" 2>/dev/null | tr -d '\r' | grep -F "    ${name}    " || true)"
	[[ -n "$line" ]] || return 0
	line="${line#    "${name}"    }"
	printf '%s %s' "${line%%    *}" "$(if [[ "$line" == *"    "* ]]; then printf '%s' "${line#*    }"; fi)"
}

fSetPath(){
	if [[ -z "${1+x}" ]]; then
		wine reg delete 'HKCU\Environment' /v Path /f >/dev/null 2>&1 || true
	else
		wine reg add 'HKCU\Environment' /v Path /t REG_EXPAND_SZ /d "$1" /f >/dev/null 2>&1
	fi
	wineserver -w
}

fRun(){
	local rc=0
	wine "$@" /S >/dev/null 2>&1 || rc=$?
	wineserver -w
	return "$rc"
}

fNoSiblings(){
	local left
	left="$(find "${prefix%/*}" -maxdepth 1 \( -name "${appName}.new.*" -o -name "${appName}.old.*" \) 2>/dev/null || true)"
	[[ -z "$left" ]] || fFail "$1: staging left behind: ${left}"
}

## The zip's file list under its top folder, against what was installed.
fSameFiles(){
	local want got
	want="$(python3 -c 'import sys, zipfile
for n in zipfile.ZipFile(sys.argv[1]).namelist():
	p = n.split("/", 1)[1] if "/" in n else ""
	if p and not p.endswith("/"): print(p)' "$zip" | LC_ALL=C sort)"
	got="$(cd "$prefix" && find . -type f ! -path ./uninstall.exe | sed 's|^\./||' | LC_ALL=C sort)"
	[[ "$want" == "$got" ]] || fFail "$1: installed files differ from the zip: $(diff <(echo "$want") <(echo "$got") | head -5 | tr '\n' ' ')"
}

## $1 label, $2 the PATH to start from, or <none>. $3 the PATH the install
## should leave, $4 the one the uninstall should, if not $2.
fRound(){
	local label="$1" before="$2" after="$3" gone="${4-$2}" pass
	if [[ "$before" == "<none>" ]]; then fSetPath; else fSetPath "$before"; fi

	for pass in install reinstall; do
		[[ "$pass" == "reinstall" ]] && : >"${prefix}/stray.txt"
		fRun "$setup" || { fFail "${label} ${pass}: setup exited non-zero"; return 0; }
		[[ -f "${prefix}/${exeName}.exe" ]] || fFail "${label} ${pass}: no ${exeName}.exe"
		[[ -f "${prefix}/uninstall.exe" ]] || fFail "${label} ${pass}: no uninstall.exe"
		[[ ! -e "${prefix}/stray.txt" ]] || fFail "${label} ${pass}: the old folder was not replaced"
		[[ -f "$shortcut" ]] || fFail "${label} ${pass}: no Start menu shortcut"
		[[ "$(fRegGet 'HKCU\Environment' Path)" == "REG_EXPAND_SZ ${after}" ]] \
			|| fFail "${label} ${pass}: PATH is '$(fRegGet 'HKCU\Environment' Path)', expected '${after}'"
		[[ "$(fRegGet "$arp" DisplayVersion)" == "REG_SZ ${ver}" ]] || fFail "${label} ${pass}: no Apps entry for ${ver}"
		[[ "$(fRegGet "$arp" UninstallString)" == "REG_SZ \"${winPrefix}\\uninstall.exe\"" ]] \
			|| fFail "${label} ${pass}: Apps entry points at '$(fRegGet "$arp" UninstallString)'"
		fNoSiblings "${label} ${pass}"
	done
	fSameFiles "$label"

	fRun "${prefix}/uninstall.exe" || { fFail "${label} uninstall exited non-zero"; return 0; }
	[[ ! -e "$prefix" ]] || fFail "${label} uninstall: ${prefix} is still there"
	[[ ! -e "$shortcut" ]] || fFail "${label} uninstall: shortcut is still there"
	[[ -z "$(fRegGet "$arp" DisplayName)" ]] || fFail "${label} uninstall: Apps entry is still there"
	if [[ "$before" == "<none>" ]]; then
		## install.ps1 leaves an empty value too.
		[[ "$(fRegGet 'HKCU\Environment' Path)" == "REG_EXPAND_SZ " || -z "$(fRegGet 'HKCU\Environment' Path)" ]] \
			|| fFail "${label} uninstall: PATH is '$(fRegGet 'HKCU\Environment' Path)'"
	else
		[[ "$(fRegGet 'HKCU\Environment' Path)" == "REG_EXPAND_SZ ${gone}" ]] \
			|| fFail "${label} uninstall: PATH is '$(fRegGet 'HKCU\Environment' Path)', expected '${gone}'"
	fi
	fNoSiblings "${label} uninstall"
	fEcho "${label}: install, reinstall and uninstall checked"
}

fEcho "Setup check against ${setup##*/}"

fRound "no PATH" "<none>" "$winPrefix"

## Empty entries, a variable, a trailing backslash on one entry and a trailing
## ';' on the value all have to survive both ways.
mixed='C:\first;%USERPROFILE%\bin;;C:\Last\;'
fRound "mixed PATH" "$mixed" "${mixed%;};${winPrefix};"

## Already there with a trailing backslash: left as is, and the uninstall
## takes it out as install.ps1 would.
fRound "already listed" "C:\\a;${winPrefix}\\;C:\\b" "C:\\a;${winPrefix}\\;C:\\b" "C:\\a;C:\\b"

## NSIS strings hold 1023 characters. One that only just fits with the folder
## added must come back whole; one that doesn't must not be touched.
## Ends on a letter: a trailing '\' would escape reg's closing quote.
fLongPath(){
	local want="$1" out="C:\\a;C:\\b;C:\\"
	while ((${#out} < want)); do out+="x"; done
	printf '%s' "$out"
}
fit="$(fLongPath $((1023 - ${#winPrefix} - 1)))"
fRound "PATH that just fits" "$fit" "${fit};${winPrefix}"
over="$(fLongPath $((1023 - ${#winPrefix})))"
fRound "PATH one over" "$over" "$over"
long="$(fLongPath 1500)"
fRound "PATH too long" "$long" "$long"

if ((failures)); then
	fEcho "FAILED: ${failures} setup check(s)"
	exit 1
fi
fEcho "OK: setup exe"
