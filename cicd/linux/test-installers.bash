#!/usr/bin/env bash

##	- Purpose: Run both installers against the Linux release tarball just built:
##	  install, install again over it, then uninstall, each into a scratch home.
##	  Offline, through --from, so it tests the install steps and not the network.
##	- A missing tarball or a missing pwsh skips that part with a note, exit 77
##	  when nothing could run.
##	- Syntax: cicd/linux/test-installers.bash
##	- Test ID: rhqmz9n8

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

fEcho_Clean(){ echo "${@}"; }
fEcho(){       if [[ -n "${*}" ]]; then fEcho_Clean "[ ${*} ]"; else fEcho_Clean ""; fi; }

readonly exeName="nemo-anywhere"
ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" source/meson.build | head -1 || true)"
readonly tarball="${root}/cicd/artifacts/release/${exeName}-${ver}-linux-x86_64.tar.gz"
if [[ -z "$ver" || ! -f "$tarball" ]]; then
	fEcho "installer check skipped: no ${tarball##*/} (run the release lane first)"
	exit 77
fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/${exeName}-installer-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## $1 label, rest the command. Everything the installer touches sits under a
## home of its own, which is emptied between installers.
fRound(){
	local label="$1"; shift
	local home="${scratch}/${label}"
	local prefix="${home}/.local/share/${exeName}"
	local launcher="${home}/.local/share/applications/${exeName}.desktop"
	local symlink="${home}/.local/bin/${exeName}"
	local pass leftovers
	mkdir -p "$home"

	for pass in install reinstall; do
		if ! env -u DISPLAY HOME="$home" XDG_DATA_HOME="${home}/.local/share" "$@" >"${home}/${pass}.log" 2>&1; then
			fFail "${label} ${pass} exited non-zero; last lines:"
			tail -5 "${home}/${pass}.log"
			return 0
		fi
		[[ -x "${prefix}/bin/${exeName}" ]] || fFail "${label} ${pass}: no ${prefix}/bin/${exeName}"
		[[ -f "$launcher" ]] || fFail "${label} ${pass}: no launcher"
		[[ "$(readlink "$symlink" || true)" == "${prefix}/bin/${exeName}" ]] || fFail "${label} ${pass}: symlink does not point into the prefix"
		leftovers="$(find "${home}/.local/share" -maxdepth 1 \( -name ".${exeName}-install.*" -o -name "${exeName}.old.*" -o -name "${exeName}.new.*" \) || true)"
		[[ -z "$leftovers" ]] || fFail "${label} ${pass}: staging left behind: ${leftovers}"
	done

	if ! env -u DISPLAY HOME="$home" XDG_DATA_HOME="${home}/.local/share" "$@" --uninstall >"${home}/uninstall.log" 2>&1; then
		fFail "${label} uninstall exited non-zero"
		tail -5 "${home}/uninstall.log"
		return 0
	fi
	[[ ! -e "$prefix" && ! -e "$launcher" && ! -L "$symlink" ]] || fFail "${label} uninstall left files behind"
	fEcho_Clean "${label}: install, reinstall and uninstall OK"
	return 0
}

fEcho "Installer check against ${tarball##*/}"
fRound bash bash install.bash --from "$tarball" --yes
ran=1
if command -v pwsh >/dev/null 2>&1; then
	## pwsh takes -Uninstall; the helper appends the bash spelling, which pwsh's
	## parameter binding accepts as the same switch.
	fRound pwsh pwsh -NoProfile -File install.ps1 -From "$tarball" -Yes
	ran=2
else
	fEcho_Clean "pwsh not installed; install.ps1 not checked"
fi

if ((failures)); then
	fEcho "FAILED: installer check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: installer check (${ran} installer(s))"
