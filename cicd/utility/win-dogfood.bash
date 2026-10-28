#!/usr/bin/env bash

##	- Purpose: refresh the Windows dogfood exe from a Linux pipeline run. That exe
##	  is the Enigma-packed one, and only Windows can pack it, so the work runs on
##	  whichever Windows box answers first: its clone is moved to the commit this
##	  run is on, and cicd-win.ps1 builds, tests, packs and drops it into the
##	  synced app dir there.
##	- The boxes are shared with other sessions, so the box is taken for the whole
##	  run through the VM state helper or the host lock, whichever is on this box
##	  (include/box-wrap.bash). b29w goes first. vm925w, a VM here, is started when
##	  it's off or paused, waited for, and put back after, when the helper is
##	  here. No box there is not an error.
##	- NEMO_WIN_READY_WAIT is the seconds to wait for the login after a boot
##	  (default 600).
##	- The clone there has no key for the repo, so the commits go over as a git
##	  bundle. Only what is committed goes; an uncommitted edit here does not.
##	- Syntax: win-dogfood.bash

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho_Clean(){ echo "${@}"; }
fEcho(){       if [[ -n "${*}" ]]; then fEcho_Clean "[ ${*} ]"; else fEcho_Clean ""; fi; }

# shellcheck source=include/box-wrap.bash
source cicd/utility/include/box-wrap.bash

## Lock name and ssh name both, in the order they are tried.
readonly boxes=(b29w vm925w)
lockTool="$(fBoxWrap_Tool)"
readonly lockTool
readyWaitSec="${NEMO_WIN_READY_WAIT:-600}"
readonly clone='%USERPROFILE%\source\repos\nemo-anywhere'
readonly bundleName='nemo-anywhere-dogfood.bundle'
## A Windows build plus the suite runs well past the lock's default lease, and
## the wait is for whoever holds the box to finish.
readonly lockWaitSec=1800

## 8s was too short for a VM that had just booted on a busy host.
sshOpts=(-o ConnectTimeout=30 -o BatchMode=yes -o LogLevel=ERROR)

## Every command here is put together on this side for cmd.exe on the box.
# shellcheck disable=SC2029
fRun(){
	local sshHost="$1"
	local sha haveSha scratch bundle

	sha="$(git rev-parse HEAD)"
	scratch="$(mktemp -d)"
	bundle="${scratch}/${bundleName}"

	## Send only what the box lacks, when it is on a commit this repo knows.
	haveSha="$(ssh "${sshOpts[@]}" "$sshHost" "cd /d ${clone} && git rev-parse HEAD" 2>/dev/null | tr -d '\r' || true)"
	if [[ "$haveSha" == "$sha" ]]; then
		fEcho_Clean "${sshHost} clone already at ${sha:0:7}"
	elif [[ -n "$haveSha" ]] && git merge-base --is-ancestor "$haveSha" HEAD 2>/dev/null; then
		git bundle create -q "$bundle" HEAD "^${haveSha}"
	else
		git bundle create -q "$bundle" HEAD
	fi

	if [[ -f "$bundle" ]]; then
		scp -q "${sshOpts[@]}" "$bundle" "${sshHost}:${bundleName}"
		## checkout -B refuses to run over an edit someone left in that clone,
		## which is the right answer: it stops here rather than losing it.
		ssh "${sshOpts[@]}" "$sshHost" "cd /d ${clone} && git fetch -q %USERPROFILE%\\${bundleName} HEAD && git checkout -q -B dogfood FETCH_HEAD && del %USERPROFILE%\\${bundleName}"
	fi
	rm -f "$bundle"
	rmdir "$scratch"

	## Lint ran here already, and the box cannot reach the remote to sync or
	## publish.
	fEcho_Clean "building and packing on ${sshHost} at ${sha:0:7} ..."
	ssh "${sshOpts[@]}" "$sshHost" "cd /d ${clone} && pwsh -NoProfile -File cicd\\cicd-win.ps1 -Yes -NoSync -NoFmt -NoPublish"
}

fMain(){
	local entry lockName sshHost

	if [[ "${1:-}" == "--locked" ]]; then
		fBoxWrap_WaitReady "$2" "$readyWaitSec" exit "${sshOpts[@]}" || { fEcho "${2} did not answer in ${readyWaitSec}s"; return 1; }
		fRun "$2"
		return
	fi

	if [[ ! -f "$lockTool" ]]; then
		fEcho "no Windows host lock here (${lockTool}); Windows dogfood skipped"
		return 0
	fi

	for entry in "${boxes[@]}"; do
		lockName="$entry"; sshHost="$entry"
		if fBoxWrap_IsThere "$lockName" "$sshHost" "$lockTool" "${sshOpts[@]}" > /dev/null; then
			fEcho_Clean "Windows dogfood on ${sshHost}"
			bash "$lockTool" wrap "$lockName" --wait "$lockWaitSec" -- bash "${BASH_SOURCE[0]}" --locked "$sshHost"
			return
		fi
	done

	fEcho "no Windows box answered; Windows dogfood skipped"
}

fMain "${@}"


##	History:
##		- 2026-10-28: b29w by that name, and vm925w through the VM state helper,
##		  which starts it and puts it back.
