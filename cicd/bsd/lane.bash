#!/usr/bin/env bash

##	- Purpose: The FreeBSD lane, run from the Linux box. Sends the working tree
##	  to the FreeBSD box and runs one of these there:
##	  --tests     build with -Dwerror=true, the suite on an X server of its own,
##	              then --version (linux/run-tests.bash, which runs there too)
##	  --release   the release build and pkg file (bsd/release.bash). The prefix
##	              comes back and is packed here as the -bsd- tarball install.bash
##	              looks for, with the same stamp and order as the Linux one; the
##	              pkg comes back beside it, and the sums file is rewritten.
##	- What goes over is the files git knows about, tracked and untracked but not
##	  ignored, as they are in the working tree, the way release-arm64.bash sends
##	  them. The source dir there is emptied and refilled every run; tar keeps the
##	  file times, so the test build dir beside it stays incremental.
##	- The box is shared, so it is taken through the host lock for the run when
##	  that tool is on this box. A box that does not answer is an error.
##	- NEMO_BSD_HOST is the ssh target (default bsdtest@vmFreeBSD), NEMO_BSD_LOCK
##	  its lock name (default vmFreeBSD), NEMO_BSD_JOBS the build and test jobs
##	  (default 4).
##	- Syntax: lane.bash --tests|--release

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
OUT="${ROOT}/cicd/artifacts/release"
bsdHost="${NEMO_BSD_HOST:-bsdtest@vmFreeBSD}"
lockName="${NEMO_BSD_LOCK:-vmFreeBSD}"
jobs="${NEMO_BSD_JOBS:-4}"
## Under the remote home.
remoteBase="nemo-anywhere-ci"
remoteDir="${remoteBase}/src"
## Found by the tail of its name, like win-dogfood.bash does.
lockTool="${WINDOWS_HOST_LOCK:-}"
if [[ -z "$lockTool" ]]; then
	for lockTool in "${HOME}"/synced/0-0/common/exec/util/linux/bash/*windows-host-lock.bash; do break; done
fi
## A cold build plus the suite runs past the lock's default lease.
lockWaitSec=1800

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"
# shellcheck source=../utility/include/release-files.bash
source "${ROOT}/cicd/utility/include/release-files.bash"

sshOpts=(-o ConnectTimeout=8 -o BatchMode=yes -o LogLevel=ERROR)

## Every remote command is put together on this side.
# shellcheck disable=SC2029
fSend(){
	ssh "${sshOpts[@]}" "$bsdHost" 'test "$(uname -s)" = FreeBSD' \
		|| fDie "${bsdHost} is not a FreeBSD box reachable as that user"
	ssh "${sshOpts[@]}" "$bsdHost" "mkdir -p ${remoteDir} && find ${remoteDir} -mindepth 1 -delete"
	## --ignore-failed-read: a file deleted in the tree but not yet in git is
	## still listed, and the Linux build would not see it either.
	( cd "$ROOT" && git ls-files -z --cached --others --exclude-standard \
		| tar --null -T - --ignore-failed-read -cf - ) \
		| ssh "${sshOpts[@]}" "$bsdHost" "tar -xf - -C ${remoteDir}"
	fEcho_Clean "sent $(cd "$ROOT" && git ls-files --cached --others --exclude-standard | wc -l) files to ${bsdHost}:${remoteDir}"
}

# shellcheck disable=SC2029
fTests(){
	fEcho_Clean ""
	fEcho "FreeBSD build and suite on ${bsdHost}"
	fSend
	ssh "${sshOpts[@]}" "$bsdHost" "cd ${remoteDir} && BUILD_DIR=\$HOME/${remoteBase}/build NEMO_TEST_JOBS=${jobs} bash cicd/linux/run-tests.bash"
}

# shellcheck disable=SC2029
fRelease(){
	local ver name started secs

	ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${ROOT}/source/meson.build" | head -1)"
	[[ -n "$ver" ]] || fDie "no version in source/meson.build"
	fSetSourceDate "$ROOT"
	fWarnIfSourceDateIsAGuess "$ROOT"

	fEcho_Clean ""
	fEcho "FreeBSD release build on ${bsdHost}"
	fSend
	started="$(date +%s)"
	ssh "${sshOpts[@]}" "$bsdHost" "cd ${remoteDir} && SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH} CICD_MAX_JOBS=${jobs} bash cicd/bsd/release.bash"
	secs=$(( $(date +%s) - started ))

	## The arch is whatever the box built for.
	name="$(ssh "${sshOpts[@]}" "$bsdHost" "cd ${remoteDir}/cicd/artifacts/release && ls -d ${SLUG}-${ver}-bsd-*.pkg" | sed 's/\.pkg$//' | head -1)"
	[[ "$name" == "${SLUG}-${ver}-bsd-"* ]] || fDie "no ${SLUG}-${ver}-bsd-*.pkg on ${bsdHost}"

	mkdir -p "$OUT"
	rm -rf "${OUT:?}/${name}" "${OUT}/${name}.tar.gz" "${OUT}/${name}.pkg"
	scp -q "${sshOpts[@]}" "${bsdHost}:${remoteDir}/cicd/artifacts/release/${name}.pkg" "${OUT}/${name}.pkg"
	ssh "${sshOpts[@]}" "$bsdHost" "tar -cf - -C ${remoteDir}/cicd/artifacts/release ${name}" | tar -xf - -C "$OUT"
	[[ -x "${OUT}/${name}/bin/${SLUG}" ]] || fDie "the prefix that came back has no bin/${SLUG}"
	## Same flags as linux/release.bash, so the same commit packs to the same bytes.
	tar -czf "${OUT}/${name}.tar.gz" -C "$OUT" \
		--owner=0 --group=0 --numeric-owner --sort=name --mtime="@${SOURCE_DATE_EPOCH}" \
		"${name}"
	rm -rf "${OUT:?}/${name}"

	fWriteReleaseSums "$OUT" "$SLUG" "$ver"

	fEcho_Clean ""
	fEcho "FreeBSD build took $(( secs / 60 ))m $(( secs % 60 ))s on ${bsdHost}"
	fEcho_Clean "$(cd "$OUT" && sha256sum "${name}.tar.gz" "${name}.pkg")"
	fEcho_Clean ""
}

fMain(){
	local mode=""
	case "${1:-}" in
		--tests|--release) mode="$1" ;;
		-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; return 0 ;;
		*) fDie "need --tests or --release (try --help)" ;;
	esac
	if [[ "${2:-}" == "--locked" || ! -f "$lockTool" ]]; then
		case "$mode" in
			--tests)   fTests ;;
			--release) fRelease ;;
		esac
		return
	fi
	bash "$lockTool" wrap "$lockName" --wait "$lockWaitSec" -- bash "${BASH_SOURCE[0]}" "$mode" --locked
}

fMain "${@}"


##	History:
##		- 2026-10-07: Created.
