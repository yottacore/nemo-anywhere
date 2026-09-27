#!/usr/bin/env bash

##	- Purpose: Build the Linux release artifacts under the names the installers
##	  look for: nemo-anywhere-<ver>-linux-<arch>.tar.gz plus a shared
##	  nemo-anywhere-<ver>-sha256sums.txt (see design.md, Delivery).
##	- Build box is nemo-build-jammy (Ubuntu 22.04), NOT the day-to-day nemo-build
##	  container: the release binary's glibc floor is whatever it was built against,
##	  and trixie's 2.41 would rule out every distro older than 2025. See the
##	  Dockerfile beside this script.
##	- The image and container are created on demand, so a fresh clone can cut a
##	  release with one command.
##	- The sums file covers every artifact sitting in the release dir, so a Windows
##	  exe dropped in beside the tarball (the CI builds and signs that one) is
##	  covered by the same file the installers verify against.
##	- Syntax: release.bash [--clean]      (--clean forces a from-scratch build)

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
IMAGE="${NEMO_RELEASE_IMAGE:-nemo-build-jammy:latest}"
CONTAINER="${NEMO_RELEASE_CONTAINER:-nemo-build-jammy}"
OUT="${ROOT}/cicd/artifacts/release"
DOGFOOD="${ROOT}/cicd/artifacts/dogfood/${SLUG}"
BUILD=/build-release
STAGE=/build-prefix/nemo-anywhere   # stage-prefix.bash guards its rm -rf on the dest being named after the app

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"

clean=0
case "${1:-}" in
	--clean) clean=1 ;;
	-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	"") ;;
	*) fDie "unknown option: $1 (try --help)" ;;
esac

ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${ROOT}/source/meson.build" | head -1)"
[[ -n "$ver" ]] || fDie "no version in source/meson.build"

## Every timestamp the build and the archive would otherwise take from the clock.
fSetSourceDate "$ROOT"
fWarnIfSourceDateIsAGuess "$ROOT"

## At most half the cores, matching the pipeline engine - a release build should
## not make the box unusable either. Inherited from cicd.bash when called from it.
cores="$(nproc 2>/dev/null || echo 2)"
jobs="${CICD_MAX_JOBS:-$(( cores / 2 ))}"
(( jobs >= 1 )) || jobs=1


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Build box

fEcho_Clean ""
fEcho "Release build box"
docker info >/dev/null 2>&1 || fDie "docker daemon is not reachable"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
	fEcho_Clean "building image ${IMAGE}"
	docker build -t "$IMAGE" "${ROOT}/cicd/linux/" >/dev/null
fi
if ! docker exec "$CONTAINER" true 2>/dev/null; then
	fEcho_Clean "starting container ${CONTAINER}"
	docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
	## --init reaps orphans; --ulimit core=0 keeps crash dumps out of the mounted tree.
	docker run -d --init --ulimit core=0 --name "$CONTAINER" --shm-size=2g \
		-v "${ROOT}:/src" "$IMAGE" sleep infinity >/dev/null
fi
fEcho_Clean "$(docker exec "$CONTAINER" sh -c '. /etc/os-release; printf "%s, glibc %s, gtk %s" "$PRETTY_NAME" "$(ldd --version | head -1 | grep -oE "[0-9]+\.[0-9]+$")" "$(pkg-config --modversion gtk+-3.0)"')"

arch="$(docker exec "$CONTAINER" uname -m)"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Build, smoke, stage

fEcho_Clean ""
fEcho "Building ${SLUG} ${ver} (linux-${arch})"
((clean)) && docker exec "$CONTAINER" rm -rf "$BUILD" || true
docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" sh -c "
	set -e
	if [ -d ${BUILD} ]; then reconf=--reconfigure; else reconf=; fi
	meson setup \$reconf --buildtype=release -Dstrip=true -Db_lto=true -Db_lto_threads=4 -Dextension_library=static -Dprefix=/opt/${SLUG} ${BUILD} /src/source >/dev/null
	ninja -C ${BUILD} -j ${jobs}" | tail -1

fEcho_Clean ""
fEcho "Smoke test"
smoke="$(docker exec "$CONTAINER" xvfb-run -a "${BUILD}/src/${SLUG}" --version)"
## Prefix match, not equality: the version string carries a build number that moves
## on every reconfigure. An exact match here silently failed every release for a week.
[[ "$smoke" == "${SLUG} v${ver} build "* ]] || fDie "smoke test said '${smoke}', expected '${SLUG} v${ver} build <n>'"
fEcho_Clean "$smoke"
## The extension API lives in the exe in this build, and nothing else in the
## lane would notice if an extension could no longer reach it.
docker exec "$CONTAINER" meson test -C "$BUILD" --no-rebuild "rh5f9h18 Extension load test" >/dev/null \
	|| fDie "extension load test failed; see ${BUILD}/meson-logs/testlog.txt in ${CONTAINER}"

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

## One sums file per release covering this version's artifacts - the installers grep
## it for their own asset's line, so extra lines (the Windows exe) are free. Matching
## on the version keeps a leftover build for another version out of it.
sums="${SLUG}-${ver}-sha256sums.txt"
tmpsums="$(mktemp)"
rm -f "${OUT:?}/${sums}"
## -0/-r: without them an empty dir still runs sha256sum, which then reads stdin
## and writes a bogus "-" line into the file the installers verify against, and a
## name with a space would be split into two arguments.
( cd "$OUT" && find . -maxdepth 1 -type f -name "${SLUG}-${ver}-*" -printf '%P\0' | sort -z | xargs -0 -r sha256sum ) > "$tmpsums"
mv "$tmpsums" "${OUT}/${sums}"
chmod 644 "${OUT}/${sums}"	# mktemp makes it 0600

fEcho_Clean ""
fEcho "Artifacts in cicd/artifacts/release"
( cd "$OUT" && find . -maxdepth 1 -type f -name "${SLUG}-${ver}-*" -printf '  %-52f %10s bytes\n' | sort )
fEcho_Clean ""
fEcho_Clean "$(cat "${OUT}/${sums}")"
fEcho_Clean ""


##	History:
##		- 2026-08-04 JC: Created (Linux half of the v1.0.0-beta1 release assets).
