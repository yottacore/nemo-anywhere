#!/usr/bin/env bash

##	- Purpose: The exact MSYS2 packages the hosted Windows release build
##	  installs. They're listed in msys2-pins.txt beside this script, so a tag
##	  builds with the same compiler, libraries and DLLs on whatever day it runs.
##	- update: resolves the dependency closure of the roots below from today's
##	  package databases and writes the list. The msys side leaves out what the
##	  base install already has. That base comes from the dated installer the
##	  pinned setup-msys2 commit unpacks, so BASE_DATE, SETUP_MSYS2 and the
##	  `uses:` line in release-win.yml move together. Run by hand, then commit.
##	- install: under MSYS2, fetches every listed file, checks each sha256 and
##	  installs them all in one pacman -U. A file the repo no longer has fails
##	  the run, which means it's time to move the pins.
##	- Syntax: cicd/win/msys2-pins.bash update|install [PINS_FILE]

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

REPO_ROOT="https://repo.msys2.org"
## setup-msys2 v2.33.0, which unpacks the 2026-09-27 base installer.
SETUP_MSYS2="ec48f7c5447b3140e2b088413ae3a55687bccb6e"
BASE_DATE="2026-09-27"
## As the hosted build had them, less desktop-file-utils, which MSYS2 dropped
## and nothing here runs.
MINGW_ROOTS=(gcc gtk3 libarchive json-glib libexif meson ninja pkgconf)
MSYS_ROOTS=(intltool)

msys2PinsHere="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../utility/include/run-temp.bash
source "${msys2PinsHere}/../utility/include/run-temp.bash"

fEcho(){ echo "[ $* ]"; }
fDie(){ echo "msys2-pins: $*" >&2; exit 1; }

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	update
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

## fMsys2Pins_Closure <repo> <desc dir> <base list> <root>... -> "repo file sha"
## lines, sorted. Every desc under the dir is read in one gawk pass. A dep that
## names nothing, by name or by provides, fails, since a list with a hole in it
## would let pacman fill the hole from whatever is current.
fMsys2Pins_Closure(){
	local repo="$1" descDir="$2" baseList="$3"; shift 3
	local -A fileOf=() depsOf=() provider=() shaOf=() inBase=() seen=()
	local kind a b pkg real d name
	while IFS='|' read -r kind a b; do
		case "$kind" in
			F) fileOf[$a]=$b ;;
			D) depsOf[$a]=$b ;;
			V) provider[$a]=$b ;;
			S) shaOf[$a]=$b ;;
		esac
	done < <(gawk '
		BEGINFILE { name=""; file=""; deps=""; provs=""; sha=""; sec="" }
		/^%FILENAME%$/ { sec="F"; next }
		/^%NAME%$/     { sec="N"; next }
		/^%DEPENDS%$/  { sec="D"; next }
		/^%PROVIDES%$/ { sec="V"; next }
		/^%SHA256SUM%$/{ sec="S"; next }
		/^%/           { sec="";  next }
		sec=="F" && NF { file=$0 }
		sec=="N" && NF { name=$0 }
		sec=="S" && NF { sha=$0 }
		sec=="D" && NF { s=$0; sub(/[<>=].*/,"",s); deps=deps" "s }
		sec=="V" && NF { s=$0; sub(/[<>=].*/,"",s); provs=provs" "s }
		ENDFILE {
			if (name!="") {
				print "F|" name "|" file
				print "D|" name "|" deps
				print "S|" name "|" sha
				n=split(provs,pv," "); for(i=1;i<=n;i++) print "V|" pv[i] "|" name
			}
		}
	' "${descDir}"/*/desc)
	if [[ -n "${baseList}" ]]; then
		while read -r name _; do [[ -n "${name}" ]] && inBase[$name]=1; done < "${baseList}"
	fi

	local queue=("$@") out=()
	while [[ ${#queue[@]} -gt 0 ]]; do
		pkg="${queue[0]}"; queue=("${queue[@]:1}")
		[[ -n "${seen[$pkg]:-}" ]] && continue
		seen[$pkg]=1
		real="${pkg}"
		[[ -z "${fileOf[$real]:-}" && -z "${inBase[$real]:-}" && -n "${provider[$pkg]:-}" ]] && real="${provider[$pkg]}"
		[[ -n "${inBase[$real]:-}" ]] && continue
		[[ -n "${fileOf[$real]:-}" ]] || fDie "${repo}: nothing provides ${pkg}"
		[[ -n "${shaOf[$real]:-}" ]] || fDie "${repo}: no sha256 for ${real}"
		[[ "${real}" != "${pkg}" && -n "${seen[$real]:-}" ]] && continue
		seen[$real]=1
		out+=("${repo} ${fileOf[$real]} ${shaOf[$real]}")
		for d in ${depsOf[$real]:-}; do [[ -n "${seen[$d]:-}" ]] || queue+=("${d}"); done
	done
	printf '%s\n' "${out[@]}" | LC_ALL=C sort
}

fMsys2Pins_Update(){
	local pins="$1" work r
	work="$(mktemp -d)"
	# shellcheck disable=SC2064 # the path is fixed now, on purpose
	trap "fSafeRm_IfThere '${work}'" EXIT
	mkdir "${work}/mingw" "${work}/msys"
	fEcho "Fetching package databases and the ${BASE_DATE} base list"
	curl -fsSL --retry 3 "${REPO_ROOT}/mingw/mingw64/mingw64.db" -o "${work}/mingw.db"
	curl -fsSL --retry 3 "${REPO_ROOT}/msys/x86_64/msys.db" -o "${work}/msys.db"
	curl -fsSL --retry 3 "https://github.com/msys2/msys2-installer/releases/download/${BASE_DATE}/msys2-base-x86_64-${BASE_DATE//-/}.packages.txt" -o "${work}/base.txt"
	tar -xf "${work}/mingw.db" -C "${work}/mingw"
	tar -xf "${work}/msys.db" -C "${work}/msys"

	local mingwRoots=()
	for r in "${MINGW_ROOTS[@]}"; do mingwRoots+=("mingw-w64-x86_64-${r}"); done
	{
		echo "# The MSYS2 packages release-win.yml installs, written by msys2-pins.bash update."
		echo "# setup-msys2 ${SETUP_MSYS2}, base ${BASE_DATE}"
		fMsys2Pins_Closure mingw/mingw64 "${work}/mingw" "" "${mingwRoots[@]}"
		fMsys2Pins_Closure msys/x86_64 "${work}/msys" "${work}/base.txt" "${MSYS_ROOTS[@]}"
	} > "${work}/pins.txt"
	mv -f "${work}/pins.txt" "${pins}"
	fEcho "Wrote ${pins}: $(grep -vc '^#' "${pins}") packages"
}

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	install
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

fMsys2Pins_Install(){
	local pins="$1" work repo file sha files=()
	[[ "$(uname -o 2>/dev/null)" == "Msys" ]] || fDie "install runs under MSYS2 only"
	work="$(mktemp -d)"
	# shellcheck disable=SC2064 # the path is fixed now, on purpose
	trap "fSafeRm_IfThere '${work}'" EXIT
	while read -r repo file sha; do
		[[ -z "${repo}" || "${repo}" == \#* ]] && continue
		[[ "${file}" != */* && "${sha}" =~ ^[0-9a-f]{64}$ ]] || fDie "bad line: ${repo} ${file} ${sha}"
		printf 'url = "%s/%s/%s"\noutput = "%s/%s"\n' "${REPO_ROOT}" "${repo}" "${file}" "${work}" "${file}" >> "${work}/curl.cfg"
		printf '%s  %s\n' "${sha}" "${file}" >> "${work}/sums"
		files+=("${work}/${file}")
	done < "${pins}"
	[[ ${#files[@]} -gt 0 ]] || fDie "no packages in ${pins}"
	fEcho "Fetching ${#files[@]} pinned packages"
	curl -fsSL --retry 3 --parallel --parallel-max 8 -K "${work}/curl.cfg"
	(cd "${work}" && sha256sum --quiet --strict -c sums) || fDie "checksum mismatch"
	pacman -U --noconfirm "${files[@]}"
}

fMain(){
	local pins="${2:-${msys2PinsHere}/msys2-pins.txt}"
	case "${1:-}" in
		update)  fMsys2Pins_Update "${pins}" ;;
		install) fMsys2Pins_Install "${pins}" ;;
		*) sed -n 's/^##\t- Syntax: //p' "${BASH_SOURCE[0]}" >&2; exit 2 ;;
	esac
}

## Sourced by its test for the functions alone.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then fMain "$@"; fi

##	History:
##		- 20261028: Created.
