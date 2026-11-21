#!/usr/bin/env bash

##	- Purpose: Run both installers against the Linux release tarball the run built:
##	  install, install again over it, then uninstall, each into a scratch home,
##	  and again with the prefix a link to a folder elsewhere. A user install
##	  into a folder it can't write has to stop before the plan.
##	  Offline, through --from, so it tests the install steps and not the network.
##	- install.bash runs once more under Bash 3.2, the one macOS still has, in a
##	  step container of the pinned bash:3.2 image, whose tools are busybox's.
##	- A missing tarball, pwsh or docker skips that part with a note, exit 77
##	  when nothing could run.
##	- Syntax: cicd/linux/test-installers.bash
##	- Test ID: dd3s7

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../utility/include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"
# shellcheck source=../utility/include/step-box.bash
source "${root}/cicd/utility/include/step-box.bash"
cd "${root}"

## 3.2.57, the last 3.2, which is what macOS has.
readonly bash32Image="bash:3.2@sha256:3a13e5da38baa575985778cd09ce8ac736d4b4dafc91a430e71271f6e5311b89"

fEcho_Clean(){ echo "${@}"; }
fEcho(){       if [[ -n "${*}" ]]; then fEcho_Clean "[ ${*} ]"; else fEcho_Clean ""; fi; }

readonly exeName="nemo-anywhere"
ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" source/meson.build | head -1 || true)"
readonly tarball="${root}/cicd/artifacts/release/${exeName}-${ver}-linux-x86_64.tar.gz"
if [[ -z "${ver}" || ! -f "${tarball}" ]]; then
	fEcho "installer check skipped: no ${tarball##*/} (run the release lane first)"
	exit 77
fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/${exeName}-installer-check.XXXXXX")"
trap 'fStepBox_Stop; fSafeRm_IfThere "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## $1 label, rest the command. Everything the installer touches is under a
## home of its own, which is emptied between installers.
fRound(){
	local label="$1"; shift
	local home="${scratch}/${label}"
	local prefix="${home}/.local/share/${exeName}"
	local launcher="${home}/.local/share/applications/${exeName}.desktop"
	local symlink="${home}/.local/bin/${exeName}"
	local pass leftovers
	mkdir -p "${home}"

	for pass in install reinstall; do
		if ! env -u DISPLAY HOME="${home}" XDG_DATA_HOME="${home}/.local/share" "$@" >"${home}/${pass}.log" 2>&1; then
			fFail "${label} ${pass} exited non-zero; last lines:"
			tail -5 "${home}/${pass}.log"
			return 0
		fi
		[[ -x "${prefix}/bin/${exeName}" ]] || fFail "${label} ${pass}: no ${prefix}/bin/${exeName}"
		[[ -f "${launcher}" ]] || fFail "${label} ${pass}: no launcher"
		[[ "$(readlink "${symlink}" || true)" == "${prefix}/bin/${exeName}" ]] || fFail "${label} ${pass}: symlink doesn't point into the prefix"
		leftovers="$(find "${home}/.local/share" -maxdepth 1 \( -name ".${exeName}-install.*" -o -name "${exeName}.old.*" -o -name "${exeName}.new.*" \) || true)"
		[[ -z "${leftovers}" ]] || fFail "${label} ${pass}: staging left behind: ${leftovers}"
	done

	if ! env -u DISPLAY HOME="${home}" XDG_DATA_HOME="${home}/.local/share" "$@" --uninstall >"${home}/uninstall.log" 2>&1; then
		fFail "${label} uninstall exited non-zero"
		tail -5 "${home}/uninstall.log"
		return 0
	fi
	[[ ! -e "${prefix}" && ! -e "${launcher}" && ! -L "${symlink}" ]] || fFail "${label} uninstall left files behind"
	fEcho_Clean "${label}: install, reinstall and uninstall OK"
	return 0
}

## A prefix that's a link, as when the folder was moved to another disk and
## linked back: installed where it points, with the link kept, and emptied
## there by an uninstall, with the link left.
fRoundLinked(){
	local label="$1"; shift
	local home="${scratch}/${label}"
	local prefix="${home}/.local/share/${exeName}"
	local real="${home}/disk2/${exeName}"
	local launcher="${home}/.local/share/applications/${exeName}.desktop"
	local symlink="${home}/.local/bin/${exeName}"
	local pass leftovers
	mkdir -p "${home}/.local/share" "${real}"
	ln -s "${real}" "${prefix}"

	for pass in install reinstall; do
		if ! env -u DISPLAY HOME="${home}" XDG_DATA_HOME="${home}/.local/share" "$@" >"${home}/${pass}.log" 2>&1; then
			fFail "${label} ${pass} exited non-zero; last lines:"
			tail -5 "${home}/${pass}.log"
			return 0
		fi
		[[ -L "${prefix}" && "$(readlink "${prefix}")" == "${real}" ]] || fFail "${label} ${pass}: the prefix isn't the link it was"
		[[ -x "${real}/bin/${exeName}" ]] || fFail "${label} ${pass}: no ${real}/bin/${exeName}"
		leftovers="$(find "${home}/.local/share" "${home}/disk2" -maxdepth 1 \( -name ".${exeName}-install.*" -o -name "${exeName}.old.*" -o -name "${exeName}.new.*" \) || true)"
		[[ -z "${leftovers}" ]] || fFail "${label} ${pass}: staging left behind: ${leftovers}"
	done

	if ! env -u DISPLAY HOME="${home}" XDG_DATA_HOME="${home}/.local/share" "$@" --uninstall >"${home}/uninstall.log" 2>&1; then
		fFail "${label} uninstall exited non-zero"
		tail -5 "${home}/uninstall.log"
		return 0
	fi
	[[ -L "${prefix}" && -d "${real}" && -z "$(ls -A "${real}")" ]] || fFail "${label} uninstall: didn't empty where the prefix points and leave the link"
	[[ ! -e "${launcher}" && ! -L "${symlink}" ]] || fFail "${label} uninstall left the launcher or symlink behind"
	fEcho_Clean "${label}: install, reinstall and uninstall through a prefix that's a link OK"
	return 0
}

## A user install into a folder it can't write stops before the plan, and
## names the folder, rather than dying halfway on a bare command. Root can
## write anywhere, so it's skipped there.
fRoundReadOnly(){
	local label="$1"; shift
	local home="${scratch}/${label}"
	[[ "$(id -u)" != 0 ]] || { fEcho_Clean "${label}: skipped as root"; return 0; }
	mkdir -p "${home}/.local/share"
	chmod 555 "${home}/.local/share"
	if env -u DISPLAY HOME="${home}" XDG_DATA_HOME="${home}/.local/share" "$@" >"${home}/install.log" 2>&1; then
		fFail "${label}: installed into a folder it can't write"
	elif ! grep -qF "no write access to ${home}/.local/share" "${home}/install.log" || grep -qF '[ Plan ]' "${home}/install.log"; then
		fFail "${label}: didn't stop before the plan on a folder it can't write; last lines:"
		tail -5 "${home}/install.log"
	else
		fEcho_Clean "${label}: stops before the plan on a folder it can't write OK"
	fi
	chmod 755 "${home}/.local/share"
	return 0
}

fEcho "Installer check against ${tarball##*/}"
fRoundReadOnly bash-readonly bash install.bash --from "${tarball}" --yes
fRound bash bash install.bash --from "${tarball}" --yes
fRoundLinked bash-linked bash install.bash --from "${tarball}" --yes
ran=1
## Mounted at their own paths, so the scratch homes, the linked prefix and the
## tarball read the same inside. HOME and XDG_DATA_HOME come from the env the
## round sets.
if ! command -v docker >/dev/null 2>&1; then
	fEcho_Clean "docker not there; install.bash not checked under Bash 3.2"
elif ! docker image inspect "${bash32Image}" >/dev/null 2>&1 && ! docker pull -q "${bash32Image}" >/dev/null; then
	fEcho_Clean "couldn't pull ${bash32Image%%@*}; install.bash not checked under Bash 3.2"
elif ! fStepBox_Start "installer bash32" "${bash32Image}" --network none -u "$(id -u):$(id -g)" \
	-v "${scratch}:${scratch}" -v "${root}:${root}:ro"; then
	fFail "couldn't start a ${bash32Image%%@*} container"
else
	bash32="$(docker exec "${stepBox}" bash -c 'echo "${BASH_VERSION}"')"
	[[ "${bash32}" == 3.2.* ]] || fFail "the bash:3.2 image runs bash ${bash32}"
	fRound bash32 docker exec -e HOME -e XDG_DATA_HOME "${stepBox}" bash "${root}/install.bash" --from "${tarball}" --yes
	fRoundLinked bash32-linked docker exec -e HOME -e XDG_DATA_HOME "${stepBox}" bash "${root}/install.bash" --from "${tarball}" --yes
	fStepBox_Stop
	ran=$((ran + 1))
fi
if command -v pwsh >/dev/null 2>&1; then
	## pwsh takes -Uninstall; the helper appends the bash spelling, which pwsh's
	## parameter binding accepts as the same switch.
	fRound pwsh pwsh -NoProfile -File install.ps1 -From "${tarball}" -Yes
	fRoundLinked pwsh-linked pwsh -NoProfile -File install.ps1 -From "${tarball}" -Yes
	fRoundReadOnly pwsh-readonly pwsh -NoProfile -File install.ps1 -From "${tarball}" -Yes
	ran=$((ran + 1))
else
	fEcho_Clean "pwsh not installed; install.ps1 not checked"
fi

if ((failures)); then
	fEcho "FAILED: installer check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: installer check (${ran} installer run(s))"
