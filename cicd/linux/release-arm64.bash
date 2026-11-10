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
##	  have the same date.
##	- They go to a run folder of this run's own on the box,
##	  <temp>/nemo-anywhere/<YYYYmmDD-HHMMSS>_<NNN>/src, removed at the end, a
##	  failure included, along with the project's stale ones there
##	  (include/box-wrap.bash). The build dir is in the container, so it starts
##	  empty either way. --keep-temp on cicd.bash keeps the folder.
##	- The box is shared, so it is taken for the run through the VM state helper
##	  or the host lock, whichever is on this box (include/box-wrap.bash). The
##	  helper starts a VM that is off, waits for its ssh, and puts it back after.
##	  The lane then waits for its own user to log in and for docker there,
##	  since both can lag well behind ssh on an emulated boot, and for its clock
##	  to agree with this one, since a VM resumed from paused keeps the time it
##	  stopped at. A box that doesn't respond in time, or whose clock stays off,
##	  is an error. One that isn't there at all, and isn't a VM here, exits 77,
##	  a skip.
##	- What runs there runs under nice 19 and idle I/O, with jobs worked out
##	  from that box's own CPU count and load, the same way as for a step here
##	  (include/jobs.bash), right before the build. Its containers get that count
##	  as their CPU cap, with a low share, as here.
##	- Its steps print the way cicd.bash's do, as "arm64 N of M", since the
##	  lane is itself one step of that run.
##	- NEMO_ARM_HOST is the ssh target (default tester@vmDebARM64), NEMO_ARM_LOCK
##	  its lock name (default vmDebARM64), NEMO_ARM_JOBS pins the build jobs,
##	  NEMO_ARM_READY_WAIT the seconds to wait for the login and docker
##	  (default 1200). The default box is emulated, where each core it uses costs
##	  the host several, and the helper's boot wait is raised to 1200s for it
##	  unless CLAUDE_VM_BOOT_WAIT is set.
##	- Syntax: release-arm64.bash

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
OUT="${ROOT}/cicd/artifacts/release"
armHost="${NEMO_ARM_HOST:-tester@vmDebARM64}"
lockName="${NEMO_ARM_LOCK:-vmDebARM64}"
jobsPin="${NEMO_ARM_JOBS:-}"
readyWaitSec="${NEMO_ARM_READY_WAIT:-1200}"
## Set once the box has its run folder.
remoteDir=""
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
remoteNice="ionice -c 3 nice -n 19"

## The step closes first, so its line says how the work went, not the cleanup.
fOnExit(){
	local rc=$?
	trap - EXIT INT TERM HUP
	fLaneStep_OnExit "$rc"
	fBoxWrap_RunDirDone "$armHost" "${sshOpts[@]}" || true
	exit "$rc"
}
fTraps(){ trap fOnExit EXIT; trap 'exit 130' INT; trap 'exit 143' TERM; trap 'exit 129' HUP; }

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
	fLaneStep_Plan arm64 "Box ready" "Send" "Release build" "Bring back"
	fLaneStep "Box ready"
	## Runs there.
	# shellcheck disable=SC2016
	fBoxWrap_WaitReady "$armHost" "$readyWaitSec" 'test "$(uname -m)" = aarch64 || exit 3; docker info >/dev/null 2>&1' "${sshOpts[@]}" || rc=$?
	case "$rc" in
		0) ;;
		3) fDie "${armHost} is not an arm64 box" ;;
		*) fDie "${armHost} did not answer as that user with docker up in ${readyWaitSec}s" ;;
	esac
	fBoxWrap_ClockReady "$armHost" "${sshOpts[@]}" || fDie "${armHost} has the wrong time, and the build would stop on it"

	fLaneStep "Send"
	fBoxWrap_RunDir "$SLUG" "$armHost" "${sshOpts[@]}" || fDie "couldn't make a run folder on ${armHost}"
	remoteDir="${boxRunDir}/src"
	ssh "${sshOpts[@]}" "$armHost" "mkdir ${remoteDir}"
	## --ignore-failed-read: a file deleted in the tree but not yet in git is
	## still listed, and the x86_64 build wouldn't see it either.
	( cd "$ROOT" && git ls-files -z --cached --others --exclude-standard \
		| tar --null -T - --ignore-failed-read -cf - ) \
		| ssh "${sshOpts[@]}" "$armHost" "tar -xf - -C ${remoteDir}"
	fEcho_Clean "sent $(cd "$ROOT" && git ls-files --cached --others --exclude-standard | wc -l) files to ${armHost}:${remoteDir}"

	fLaneStep "Release build"
	fBoxWrap_Jobs "$jobsPin" "$armHost" "${sshOpts[@]}" || fDie "couldn't read the CPU count and load on ${armHost}"
	started="$(date +%s)"
	ssh "${sshOpts[@]}" "$armHost" "$(fBoxWrap_RunCmd) SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH} CICD_MAX_JOBS=${boxJobs} ${remoteNice} bash cicd/linux/release.bash"
	secs=$(( $(date +%s) - started ))

	fLaneStep "Bring back"

	mkdir -p "$OUT"
	rm -f "${OUT:?}/${name}"
	scp -q "${sshOpts[@]}" "${armHost}:${remoteDir}/cicd/artifacts/release/${name}" "${OUT}/${name}"

	## A failed read costs only the versions in the .deb, not the hour's build.
	depsFile="$(fDebDependsFile "$ROOT" "$name")"
	rm -f "$depsFile"
	if deps="$(ssh "${sshOpts[@]}" "$armHost" "$(fBoxWrap_RunCmd) CICD_MAX_JOBS=${boxJobs} ${remoteNice} bash cicd/linux/package.bash --depends-only --from cicd/artifacts/release/${name}")" && [[ -n "$deps" ]]; then
		mkdir -p "$(dirname "$depsFile")"
		{ (cd "$OUT" && sha256sum "$name"); printf '%s\n' "$deps"; } > "$depsFile"
		fEcho_Clean "arm64 .deb depends: ${deps}"
	else
		fEcho "WARNING: could not read the arm64 dependencies on ${armHost}; the arm64 .deb will get a conservative list"
	fi

	fWriteReleaseSums "$OUT" "$SLUG" "$ver"

	fEcho_Clean "arm64 build took $(( secs / 60 ))m $(( secs % 60 ))s on ${armHost}"
	fEcho_Clean "$(cd "$OUT" && sha256sum "$name")"
}

fMain(){
	case "${1:-}" in
		--locked) fTraps; fRun; return ;;
		-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; return 0 ;;
		"") ;;
		*) fDie "unknown option: $1 (try --help)" ;;
	esac
	if ! fBoxWrap_IsThere "$lockName" "$armHost" "$lockTool" "${sshOpts[@]}"; then
		fEcho "SKIP: ${armHost} isn't there"
		exit 77
	fi
	if [[ -f "$lockTool" ]]; then
		CLAUDE_VM_BOOT_WAIT="${CLAUDE_VM_BOOT_WAIT:-1200}" bash "$lockTool" wrap "$lockName" --wait "$lockWaitSec" -- bash "${BASH_SOURCE[0]}" --locked
	else
		fTraps
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
##		  connect timeouts. A box that isn't there is a skip. A box whose clock is
##		  off fails before the send.
##		- 2026-10-28: Prints its steps as cicd.bash does. The jobs come from the
##		  box's own CPU count and load, and its side runs under nice and ionice.
##		- 2026-11-04: Works in a run folder under the box's temp root, removed
##		  at the end, rather than a fixed folder in its home.
