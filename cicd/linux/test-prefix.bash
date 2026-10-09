#!/usr/bin/env bash

##	- Purpose: Checks on the Linux release tarball that need no display.
##	  Every file it installs carries the app's name somewhere in its path, so
##	  nothing can end up where upstream Nemo installs; the .deb and .rpm are held
##	  to the same rule when dpkg-deb and rpm are on the box. And the action
##	  layout editor's launcher, copied into a prefix somewhere else, runs the
##	  editor beside it rather than the one it was configured with, under the
##	  name the program spawns it by. And none of the tarball, .deb and .rpm has
##	  the office programs and libgsf the app no longer ships, through
##	  cicd/utility/test-bundle-left-out.bash.
##	- A missing tarball skips with exit 77.
##	- Syntax: cicd/linux/test-prefix.bash [tarball | --arch ARCH]
##	  The default is this version's x86_64 tarball; --arch picks another
##	  arch's. The only thing run is the editor's shell launcher, so any arch
##	  checks on any box.
##	- Test ID: rhtq57n5

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

readonly slug="nemo-anywhere"
readonly editor="${slug}-action-layout-editor"
ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" source/meson.build | head -1 || true)"
if [[ "${1:-}" == --arch ]]; then
	tarball="${root}/cicd/artifacts/release/${slug}-${ver}-linux-${2:?--arch needs an arch}.tar.gz"
else
	tarball="${1:-${root}/cicd/artifacts/release/${slug}-${ver}-linux-x86_64.tar.gz}"
fi
if [[ ! -f "$tarball" ]]; then
	fEcho "prefix check skipped: no ${tarball##*/} (run the release lane first)"
	exit 77
fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/${slug}-prefix-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## $1 label; stdin one path per line, directories already left out. A path
## passes when one of its parts starts with the app's name, or with the bus
## name it goes by.
fCheckNames(){
	local label="$1" path part named count=0
	local -a parts
	while IFS= read -r path; do
		path="${path#./}"; path="${path#/}"
		[[ -n "$path" ]] || continue
		count=$((count + 1))
		named=0
		IFS=/ read -r -a parts <<<"$path"
		for part in "${parts[@]}"; do
			case "$part" in
				"${slug}"*|"lib${slug}"*|org.NemoAnywhere*) named=1; break ;;
			esac
		done
		((named)) || fFail "${label}: ${path} does not carry the name ${slug}"
	done
	((count > 0)) || fFail "${label}: no files listed"
	fEcho_Clean "${label}: ${count} file(s) looked at"
	return 0
}

fEcho "Prefix check against ${tarball##*/}"

mkdir -p "${scratch}/unpacked"
tar -xzf "$tarball" -C "${scratch}/unpacked"
prefix="$(find "${scratch}/unpacked" -mindepth 1 -maxdepth 1 -type d -print -quit)"
[[ -n "$prefix" ]] || { fEcho "FAILED: ${tarball##*/} has no top-level folder"; exit 1; }

fCheckNames "tarball" < <(cd "$prefix" && find . ! -type d)

deb="${tarball%.tar.gz}.deb"
if [[ -f "$deb" ]] && command -v dpkg-deb >/dev/null 2>&1; then
	fCheckNames "deb" < <(dpkg-deb --fsys-tarfile "$deb" | tar -t | { grep -v '/$' || true; })
else
	fEcho_Clean "no .deb or no dpkg-deb; the .deb not checked"
fi

rpm_file="${tarball%.tar.gz}.rpm"
if [[ -f "$rpm_file" ]] && command -v rpm >/dev/null 2>&1; then
	## Directories the package owns are listed too, and are left out by mode.
	fCheckNames "rpm" < <(rpm -qp --qf '[%{FILEMODES:perms} %{FILENAMES}\n]' "$rpm_file" 2>/dev/null | { grep -v '^d' || true; } | cut -d' ' -f2-)
else
	fEcho_Clean "no .rpm or no rpm; the .rpm not checked"
fi

## The office programs the app no longer ships, and libgsf (2026100909260549).
kind="${tarball##*/}"; kind="${kind%.tar.gz}"; kind="linux-${kind##*-linux-}"
rc=0
bash "${root}/cicd/utility/test-bundle-left-out.bash" --dir "$(dirname "$tarball")" "$kind" || rc=$?
[[ "$rc" == 0 || "$rc" == 77 ]] || fFail "bundle check on ${kind} (exit ${rc})"

## The program spawns the editor by this name out of its own bin folder, so
## the name has to be in the program and on the file.
[[ -x "${prefix}/bin/${editor}" ]] || fFail "no bin/${editor}"
LC_ALL=C grep -q -a -F -- "$editor" "${prefix}/bin/${slug}" || fFail "bin/${slug} does not name ${editor}"

## A prefix somewhere it was never configured for, with a space in the path,
## and the editor swapped for a stub that says where it is.
moved="${scratch}/moved here/${slug}"
mkdir -p "${moved}/bin" "${moved}/share/${slug}"
cp "${prefix}/bin/${editor}" "${moved}/bin/"
cp -R "${prefix}/share/${slug}/layout-editor" "${moved}/share/${slug}/"
stub="${moved}/share/${slug}/layout-editor/nemo_action_layout_editor.py"
cat >"$stub" <<'STUB'
#!/bin/sh
echo "$0"
STUB
chmod +x "$stub"
want="$(realpath "$stub")"

## No display, in case a wrong turn reaches a real editor installed on the box.
ran="$(env -u DISPLAY -u WAYLAND_DISPLAY "${moved}/bin/${editor}" 2>&1 || true)"
[[ "$(realpath -m "$ran")" == "$want" ]] || fFail "the moved launcher ran ${ran:-nothing}, not ${want}"

## Reached through a link, as it is from a folder on PATH.
ln -s "${moved}/bin/${editor}" "${scratch}/${editor}"
ran="$(env -u DISPLAY -u WAYLAND_DISPLAY "${scratch}/${editor}" 2>&1 || true)"
[[ "$(realpath -m "$ran")" == "$want" ]] || fFail "the launcher through a link ran ${ran:-nothing}, not ${want}"

if ((failures)); then
	fEcho "FAILED: prefix check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: prefix check"
