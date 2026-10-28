#!/usr/bin/env bash

##	- Purpose: Build the Linux arm64 release tarball on an arm64 box, bring it
##	  back to cicd/artifacts/release beside the x86_64 one, and rewrite the sums
##	  file over both.
##	- The .deb's dependency line is read there too, in a container from the same image, off
##	  the arm64 libraries, and kept beside this side's artifacts
##	  (fDebDependsFile). package.bash --arch arm64 makes the packages from both.
##	- Nothing here cross-builds GTK, so the build runs on an arm64 Linux box with
##	  docker. release.bash runs there as it is, in an image built from the same
##	  Dockerfile, so the arm64 build has the same glibc floor and the same
##	  package versions as the x86_64 one.
##	- What goes over is the files git knows about, tracked and untracked but not
##	  ignored, as they are in the working tree: the same files the x86_64 build
##	  container sees. The stamp is this side's SOURCE_DATE_EPOCH, so both arches
##	  carry the same date.
##	- The box is shared, so it is taken for the run through the VM state helper
##	  or the host lock, whichever is on this box (include/box-wrap.bash). The
##	  helper starts a VM that is off, waits for its ssh, and puts it back after.
##	  The lane then waits for its own user to log in and for docker there,
##	  since both can lag well behind ssh on an emulated boot. A box that does
##	  not answer in time is an error.
##	- NEMO_ARM_HOST is the ssh target (default tester@vmDebARM64), NEMO_ARM_LOCK
##	  its lock name (default vmDebARM64), NEMO_ARM_JOBS the build jobs (default
##	  4), NEMO_ARM_READY_WAIT the seconds to wait for the login and docker
##	  (default 1200). The default box is emulated, where each core it uses costs
##	  the host several, and the helper's boot wait is raised to 1200s for it
##	  unless CLAUDE_VM_BOOT_WAIT is set.
##	- Syntax: release-arm64.bash

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
OUT="${ROOT}/cicd/artifacts/release"
armHost="${NEMO_ARM_HOST:-tester@vmDebARM64}"
lockName="${NEMO_ARM_LOCK:-vmDebARM64}"
jobs="${NEMO_ARM_JOBS:-4}"
readyWaitSec="${NEMO_ARM_READY_WAIT:-1200}"
## Under the remote home. Emptied in place every run, never recreated: the build
## container there has it mounted, and a new dir would leave it on the old one.
remoteDir="nemo-anywhere-arm64/src"
## An emulated build runs well past the lock's default lease.
lockWaitSec=1800

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"
# shellcheck source=../utility/include/release-files.bash
source "${ROOT}/cicd/utility/include/release-files.bash"
# shellcheck source=../utility/include/box-wrap.bash
source "${ROOT}/cicd/utility/include/box-wrap.bash"
lockTool="$(fBoxWrap_Tool)"

## 8s was too short for an emulated VM that had just booted on a busy host.
sshOpts=(-o ConnectTimeout=30 -o BatchMode=yes -o LogLevel=ERROR)

## Every remote command is put together on this side.
# shellcheck disable=SC2029
fRun(){
	local ver name started secs depsFile deps rc=0

	ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${ROOT}/source/meson.build" | head -1)"
	[[ -n "$ver" ]] || fDie "no version in source/meson.build"
	name="${SLUG}-${ver}-linux-arm64.tar.gz"

	fSetSourceDate "$ROOT"
	fWarnIfSourceDateIsAGuess "$ROOT"

	fEcho_Clean ""
	fEcho "arm64 release build on ${armHost}"
	## Runs there.
	# shellcheck disable=SC2016
	fBoxWrap_WaitReady "$armHost" "$readyWaitSec" 'test "$(uname -m)" = aarch64 || exit 3; docker info >/dev/null 2>&1' "${sshOpts[@]}" || rc=$?
	case "$rc" in
		0) ;;
		3) fDie "${armHost} is not an arm64 box" ;;
		*) fDie "${armHost} did not answer as that user with docker up in ${readyWaitSec}s" ;;
	esac

	ssh "${sshOpts[@]}" "$armHost" "mkdir -p ${remoteDir} && find ${remoteDir} -mindepth 1 -delete"
	## --ignore-failed-read: a file deleted in the tree but not yet in git is
	## still listed, and the x86_64 build would not see it either.
	( cd "$ROOT" && git ls-files -z --cached --others --exclude-standard \
		| tar --null -T - --ignore-failed-read -cf - ) \
		| ssh "${sshOpts[@]}" "$armHost" "tar -xf - -C ${remoteDir}"
	fEcho_Clean "sent $(cd "$ROOT" && git ls-files --cached --others --exclude-standard | wc -l) files to ${remoteDir}"

	started="$(date +%s)"
	ssh "${sshOpts[@]}" "$armHost" "cd ${remoteDir} && SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH} CICD_MAX_JOBS=${jobs} bash cicd/linux/release.bash"
	secs=$(( $(date +%s) - started ))

	mkdir -p "$OUT"
	rm -f "${OUT:?}/${name}"
	scp -q "${sshOpts[@]}" "${armHost}:${remoteDir}/cicd/artifacts/release/${name}" "${OUT}/${name}"

	## A failed read costs only the versions in the .deb, not the hour's build.
	depsFile="$(fDebDependsFile "$ROOT" "$name")"
	rm -f "$depsFile"
	if deps="$(ssh "${sshOpts[@]}" "$armHost" "cd ${remoteDir} && bash cicd/linux/package.bash --depends-only --from cicd/artifacts/release/${name}")" && [[ -n "$deps" ]]; then
		mkdir -p "$(dirname "$depsFile")"
		{ (cd "$OUT" && sha256sum "$name"); printf '%s\n' "$deps"; } > "$depsFile"
		fEcho_Clean "arm64 .deb depends: ${deps}"
	else
		fEcho "WARNING: could not read the arm64 dependencies on ${armHost}; the arm64 .deb will get a conservative list"
	fi

	fWriteReleaseSums "$OUT" "$SLUG" "$ver"

	fEcho_Clean ""
	fEcho "arm64 build took $(( secs / 60 ))m $(( secs % 60 ))s on ${armHost}"
	fEcho_Clean "$(cd "$OUT" && sha256sum "$name")"
	fEcho_Clean ""
}

fMain(){
	case "${1:-}" in
		--locked) fRun; return ;;
		-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; return 0 ;;
		"") ;;
		*) fDie "unknown option: $1 (try --help)" ;;
	esac
	if [[ -f "$lockTool" ]]; then
		CLAUDE_VM_BOOT_WAIT="${CLAUDE_VM_BOOT_WAIT:-1200}" bash "$lockTool" wrap "$lockName" --wait "$lockWaitSec" -- bash "${BASH_SOURCE[0]}" --locked
	else
		fRun
	fi
}

fMain "${@}"


##	History:
##		- 2026-10-05: Created.
##		- 2026-10-07: Reads the arm64 .deb's dependencies on the box.
##		- 2026-10-26: Nothing to stop after, since release.bash and package.bash
##		  remove their containers there.
##		- 2026-10-27: Takes the box through the VM state helper when it is here,
##		  so a VM that is off is started and waited for.
##		- 2026-10-28: Waits for its own login and docker, with longer boot and
##		  connect timeouts.
