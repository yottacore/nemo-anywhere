#!/usr/bin/env bash

##	- Purpose: Build the Linux release artifacts under the names the installers
##	  look for: nemo-anywhere-<ver>-linux-<arch>.tar.gz plus a shared
##	  nemo-anywhere-<ver>-sha256sums.txt (see design.md, Delivery).
##	- Built from the nemo-build-jammy image (Ubuntu 22.04), NOT the day-to-day
##	  nemo-build-deps one: the release binary's glibc floor is whatever it was
##	  built against, and trixie's 2.41 would rule out every distro older than
##	  2025. See the Dockerfile beside this script.
##	- In a container of its own, named and removed the way every step's is
##	  (utility/include/step-box.bash). The image is built on demand, so a fresh
##	  clone can cut a release with one command.
##	- The sums file covers every artifact sitting in the release dir, so a Windows
##	  exe dropped in beside the tarball (the CI builds and signs that one) is
##	  covered by the same file the installers verify against.
##	- Every build starts from an empty build dir, in the container, so no cache
##	  volume; release-setup.bash says why.
##	- Syntax: release.bash

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
IMAGE="${NEMO_RELEASE_IMAGE:-nemo-build-jammy:latest}"
OUT="${ROOT}/cicd/artifacts/release"
DOGFOOD="${ROOT}/cicd/artifacts/dogfood/${SLUG}"
BUILD=/build-release
STAGE=/build-prefix/nemo-anywhere   # stage-prefix.bash guards its rm -rf on the dest being named after the app

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"
# shellcheck source=../utility/include/release-files.bash
source "${ROOT}/cicd/utility/include/release-files.bash"
# shellcheck source=../utility/include/step-box.bash
source "${ROOT}/cicd/utility/include/step-box.bash"

case "${1:-}" in
	--clean) ;;	# every build is clean now; still taken so old command lines run
	-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	"") ;;
	*) fDie "unknown option: $1 (try --help)" ;;
esac

ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${ROOT}/source/meson.build" | head -1)"
[[ -n "$ver" ]] || fDie "no version in source/meson.build"

## Every timestamp the build and the archive would otherwise take from the clock.
fSetSourceDate "$ROOT"
fWarnIfSourceDateIsAGuess "$ROOT"

## The pipeline's job count, from cicd.bash, or worked out the same way when run
## alone - a release build should not make the box unusable either.
fJobs_Default
jobs="${CICD_MAX_JOBS}"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Build box

fEcho_Clean ""
fEcho "Release build box"
docker info >/dev/null 2>&1 || fDie "docker daemon is not reachable"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	fEcho_Clean "building image ${IMAGE}"
	docker build --label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" -t "$IMAGE" "${ROOT}/cicd/linux/" >/dev/null
fi
## -j alone does not hold the build to that many cores: every LTO link runs 4
## jobs of its own, and ninja runs several links at once. So the container
## gets the cap too. The --security-opt pair lets the test's jail start (jail.bash).
trap 'fStepBox_Stop' EXIT
fStepBox_Start "release build" "$IMAGE" \
	--security-opt seccomp=unconfined --security-opt apparmor=unconfined \
	-v "${ROOT}:/src" \
	|| fDie "could not start a container from ${IMAGE}"
CONTAINER="${stepBox}"
## Before the build, since on the arm64 box that's an hour.
docker exec "$CONTAINER" bash /src/cicd/linux/jail.bash --check >/dev/null \
	|| fDie "the test jail can't start in a container from ${IMAGE}. Rebuild it from cicd/linux/Dockerfile if it has no bwrap"
fEcho_Clean "$(docker exec "$CONTAINER" sh -c '. /etc/os-release; printf "%s, glibc %s, gtk %s" "$PRETTY_NAME" "$(ldd --version | head -1 | grep -oE "[0-9]+\.[0-9]+$")" "$(pkg-config --modversion gtk+-3.0)"')"

## The asset name says arm64 where the kernel says aarch64.
machine="$(docker exec "$CONTAINER" uname -m)"
arch="$(fReleaseArch "$machine")" || fDie "no release name for a ${machine} build"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Build, smoke, stage

fEcho_Clean ""
fEcho "Building ${SLUG} ${ver} (linux-${arch})"
docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" bash /src/cicd/linux/release-setup.bash "$BUILD" /src/source "/opt/${SLUG}" \
	|| fDie "could not set up ${BUILD} in ${CONTAINER}"
docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" nice -n 19 ninja -C "$BUILD" -j "$jobs" | tail -1

fEcho_Clean ""
fEcho "Smoke test"
smoke="$(docker exec "$CONTAINER" xvfb-run -a "${BUILD}/src/${SLUG}" --version)"
## Prefix match, not equality: the version string carries a build number that moves
## on every reconfigure. An exact match here silently failed every release for a week.
[[ "$smoke" == "${SLUG} v${ver} build "* ]] || fDie "smoke test said '${smoke}', expected '${SLUG} v${ver} build <n>'"
fEcho_Clean "$smoke"
## The extension API lives in the exe in this build, and nothing else in the
## lane would notice if an extension could no longer reach it.
docker exec "$CONTAINER" meson test -C "$BUILD" --no-rebuild --wrapper "bash /src/cicd/linux/jail.bash" "dcsvh Extension load test" >/dev/null \
	|| fDie "extension load test failed; see ${BUILD}/meson-logs/testlog-bash.txt in ${CONTAINER} (kept with --keep-temp)"

fEcho_Clean ""
fEcho "Staging prefix"
docker exec "$CONTAINER" bash "/src/cicd/linux/stage-prefix.bash" "$BUILD" "$STAGE"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Pack

name="${SLUG}-${ver}-linux-${arch}"
fEcho_Clean ""
fEcho "Packing ${name}.tar.gz"
mkdir -p "$OUT"
rm -rf "${OUT:?}/${name}" "${OUT}/${name}.tar.gz"
## /build* is not host-mounted, so docker cp is the only way out of the container.
docker cp "${CONTAINER}:${STAGE}" "${OUT}/${name}" >/dev/null
## Fixed owner, timestamp and entry order, so the same commit produces the same
## archive on any box. Without --sort the entries come out in readdir order, which
## differs between filesystems. The gzip layer needs nothing: tar feeds it on a
## pipe, and gzip writes a zero mtime when it has no input file to read one from.
tar -czf "${OUT}/${name}.tar.gz" -C "$OUT" \
	--owner=0 --group=0 --numeric-owner --sort=name --mtime="@${SOURCE_DATE_EPOCH}" \
	"${name}"

## Keep the staged tree on disk as well. The dogfood stage installs from it and the
## launcher probes it, and neither should have to unpack the tarball to get at the
## same files. Version-free name, so whatever is here is simply the current build.
rm -rf "${DOGFOOD:?}"
mkdir -p "$(dirname "${DOGFOOD}")"
cp -a "${OUT}/${name}" "${DOGFOOD}"
fEcho_Clean "staged prefix kept at cicd/artifacts/dogfood/${SLUG}"

rm -rf "${OUT:?}/${name}"

sums="${SLUG}-${ver}-sha256sums.txt"
fWriteReleaseSums "$OUT" "$SLUG" "$ver"

fEcho_Clean ""
fEcho "Artifacts in cicd/artifacts/release"
( cd "$OUT" && find . -maxdepth 1 -type f -name "${SLUG}-${ver}-*" -printf '  %-52f %10s bytes\n' | sort )
fEcho_Clean ""
fEcho_Clean "$(cat "${OUT}/${sums}")"
fEcho_Clean ""


##	History:
##		- 2026-08-04 JC: Created (Linux half of the v1.0.0-beta1 release assets).
##		- 2026-10-03: Always a clean build dir.
##		- 2026-10-05: Runs on an arm64 box too, for release-arm64.bash.
##		- 2026-10-19: The extension load test runs in the test jail.
##		- 2026-10-26: A container of its own, not the long-lived nemo-build-jammy.
