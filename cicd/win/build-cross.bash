#!/usr/bin/env bash

##	- Purpose: Run the Windows cross build in the nemo-winbuild container with the
##	  build stamp set, so the exe is reproducible. The linker writes a timestamp
##	  into the PE header, and left alone it writes the clock - meaning two builds
##	  of the same commit differ. SOURCE_DATE_EPOCH replaces it with HEAD's commit
##	  date, and docker exec does not carry the host environment across, so it has
##	  to be handed over here.
##	- Release buildtype, same as the Linux release lane. Left off, meson defaults
##	  to debug, and every exe this lane produced before 20260919 carried DWARF at
##	  five times the size it needs. cicd/utility/check-win-build-flags.bash holds
##	  that, and runs below once the exe is linked.
##	- Syntax: build-cross.bash [--clean]

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
CONTAINER="${NEMO_WIN_CONTAINER:-nemo-winbuild}"
BUILD="${NEMO_WIN_BUILD:-/build-win}"
## Read from the mount, not the copy the image baked in at /opt, which goes stale
## the moment the file changes and needs an image rebuild to catch up.
CROSS="${NEMO_WIN_CROSSFILE:-/src/cicd/win/win64.cross.txt}"

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"

clean=0
case "${1:-}" in
	--clean)   clean=1 ;;
	-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	"") ;;
	*) fDie "unknown option: $1 (try --help)" ;;
esac

## A fresh clone has none yet, so it is made from cicd/win/Dockerfile on first
## use, the same way nemo-build is. --init reaps orphans (wine leaves plenty);
## --ulimit core=0 keeps crash dumps out of the mounted tree.
names="$(docker ps -a --format '{{.Names}}' 2>/dev/null || true)"
if ! grep -qx "$CONTAINER" <<<"$names"; then
	if ! docker image inspect nemo-winbuild-deps:latest >/dev/null 2>&1; then
		fEcho "building image nemo-winbuild-deps (first run only, takes a while)"
		docker build -t nemo-winbuild-deps:latest "${ROOT}/cicd/win/" >/dev/null || fDie "could not build image nemo-winbuild-deps"
	fi
	fEcho "creating container ${CONTAINER}"
	docker run -d --init --ulimit core=0 --shm-size=2g --cpus "$(( $(nproc) / 2 ))" --name "$CONTAINER" \
		-v "${ROOT}:/src" nemo-winbuild-deps:latest sleep infinity >/dev/null || fDie "could not create container '${CONTAINER}'"
fi
## A reboot leaves it stopped, with no restart policy.
docker exec "$CONTAINER" true 2>/dev/null || docker start "$CONTAINER" >/dev/null 2>&1 || true
docker exec "$CONTAINER" true 2>/dev/null || fDie "cross-build container '${CONTAINER}' is not running and would not start"

fSetSourceDate "$ROOT"
fWarnIfSourceDateIsAGuess "$ROOT"

cores="$(nproc 2>/dev/null || echo 2)"
jobs="${CICD_MAX_JOBS:-$(( cores / 2 ))}"
(( jobs >= 1 )) || jobs=1

## The build log used to go straight into `tail -1`, which threw away every
## warning on the way past. This is the one release-flags build that runs on an
## ordinary pipeline run, so it is where an unused-code warning gets caught: a
## release build compiles the DEBUG lines out, and anything that exists only for
## one of them then has no reader. Four of those went unseen until this lane
## became a release build. Only what ninja recompiles is in the log, which is
## enough - the commit that adds one compiles the file it is in.
fBuild(){
	local log unused serial
	log="$(mktemp)"

	if ! docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" sh -c "
		set -e
		bash /src/cicd/utility/meson-setup.bash ${BUILD} /src/source --cross-file ${CROSS} --buildtype=release -Dstrip=true -Db_lto=true -Db_lto_threads=4 -Dxmp=false -Dwerror=true >/dev/null
		bash /src/cicd/utility/check-werror.bash ${BUILD}
		ninja -C ${BUILD} -j ${jobs}" >"$log" 2>&1
	then
		tail -40 "$log" >&2
		rm -f "$log"
		fDie "cross build failed"
	fi

	tail -1 "$log"

	unused="$(grep -E 'Wunused-function|Wunused-but-set-variable' "$log" || true)"
	## gcc's lto-wrapper runs the LTRANS jobs by writing a makefile and calling
	## make, so with no make on PATH it quietly falls back to one at a time -
	## 34s against 10s on the exe link here. Same bytes either way, so this is
	## only ever about time, but a silent fallback is how the container drifted
	## in the first place.
	serial="$(grep -m1 'serial compilation of' "$log" || true)"
	rm -f "$log"
	if [[ -n "$unused" ]]; then
		printf '%s\n' "$unused" >&2
		fDie "cross build has unused-code warnings"
	fi
	if [[ -n "$serial" ]]; then
		printf '%s\n' "$serial" >&2
		fDie "link-time optimization is running serially - install make in ${CONTAINER}"
	fi
}

fStamp(){ fPeTimestamp <(docker exec "$CONTAINER" head -c 4096 "${BUILD}/src/nemo-anywhere.exe") ;}

## meson's -Dstrip=true only runs on install, and this lane never installs -
## pack-zip.bash copies the exe straight out of the build directory. So the
## strip happens here. It has to carry SOURCE_DATE_EPOCH: binutils rewrites the
## PE Time/Date field when it writes the file, and left to itself it writes the
## clock, which is what the whole stamp exists to avoid.
fStrip(){
	docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" \
		sh -c "x86_64-w64-mingw32-strip --strip-debug ${BUILD}/src/*.exe" \
		|| fDie "could not strip the cross-built exes"
}

fEcho_Clean ""
fEcho "Cross build (stamped $(date -u -d "@${SOURCE_DATE_EPOCH}" '+%Y-%m-%d %H:%M:%S UTC'))"
((clean)) && docker exec "$CONTAINER" rm -rf "$BUILD" || true
fBuild

## The stamp is part of the output, but ninja does not know that, so an exe left
## over from a build of an earlier commit looks up to date. Drop it and relink.
## Read before the strip: the strip writes SOURCE_DATE_EPOCH over whatever is
## there, so afterwards every exe looks current and this never fires.
if [[ "$(fStamp)" != "${SOURCE_DATE_EPOCH}" ]]; then
	fEcho_Clean "restamping (the existing exe is from another commit)"
	docker exec "$CONTAINER" rm -f "${BUILD}/src/nemo-anywhere.exe"
	fBuild
fi

linked="$(fStamp)"
[[ "$linked" == "${SOURCE_DATE_EPOCH}" ]] || fDie "the linker stamped the exe ${linked:-nothing}, not ${SOURCE_DATE_EPOCH} - it is ignoring SOURCE_DATE_EPOCH"

fStrip

## Checked again, because the strip rewrites the field and this is the file the
## lane goes on to pack.
pe="$(fStamp)"
[[ "$pe" == "${SOURCE_DATE_EPOCH}" ]] || fDie "the strip left the exe stamped ${pe:-nothing}, not ${SOURCE_DATE_EPOCH} - it is ignoring SOURCE_DATE_EPOCH"
fEcho_Clean "exe stamped ${pe}"

## Read the exe back rather than trusting the flags above. Copied out first
## because the check runs on the host and the exe lives in the container.
tmpExe="$(mktemp)"
trap 'rm -f "${tmpExe}"' EXIT
docker exec "$CONTAINER" cat "${BUILD}/src/nemo-anywhere.exe" > "$tmpExe"
bash "${ROOT}/cicd/utility/check-win-build-flags.bash" --shipped "$tmpExe" || fDie "the cross build is not a stripped release build"
fEcho_Clean ""


##	History:
##		- 2026-08-26: Created.
