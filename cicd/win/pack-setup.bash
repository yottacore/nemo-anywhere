#!/usr/bin/env bash

##	- Purpose: Build the Windows setup exe from the release zip, so it installs
##	  exactly the files install.ps1 would. Run after pack-zip.bash.
##	- makensis runs in the cross-build container (the nsis package, see the
##	  Dockerfile beside this script). The script is setup.nsi.
##	- Output: nemo-anywhere-<ver>-windows-x86_64-setup.exe beside the zip, so
##	  the sums file and the release pick it up with the rest.
##	- Unsigned, until there is a signing identity.
##	- Syntax:
##	  cicd/win/pack-setup.bash [options]
##	  Options:
##	   --out DIR   read the zip from and write the exe to DIR (default: cicd/artifacts/release)

##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
SLUG="nemo-anywhere"
CONTAINER="${NEMO_WIN_CONTAINER:-nemo-winbuild}"
OUT="${ROOT}/cicd/artifacts/release"

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"

while (($#)); do case "$1" in
	--out)     OUT="${2:?--out needs a path}"; shift 2 ;;
	--out=*)   OUT="${1#*=}"; shift ;;
	-h|--help) sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	*)         fDie "unknown option: $1 (try --help)" ;;
esac; done

command -v unzip >/dev/null 2>&1 || fDie "unzip is not installed"
docker exec "$CONTAINER" true 2>/dev/null || docker start "$CONTAINER" >/dev/null 2>&1 || true
docker exec "$CONTAINER" true 2>/dev/null || fDie "cross-build container '${CONTAINER}' is not running and would not start"
docker exec "$CONTAINER" sh -c 'command -v makensis' >/dev/null 2>&1 \
	|| fDie "no makensis in ${CONTAINER} - rebuild it from cicd/win/Dockerfile, or: docker exec ${CONTAINER} apt-get install -y nsis"

ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${ROOT}/source/meson.build" | head -1)"
[[ -n "$ver" ]] || fDie "no version in source/meson.build"
## The version resource takes 4 numbers only, so 1.0.0-rc.1 goes in as 1.0.0.0.
IFS=. read -r v1 v2 v3 _ <<<"${ver%%-*}.0.0.0"
viver="$((10#${v1:-0})).$((10#${v2:-0})).$((10#${v3:-0})).0"

name="${SLUG}-${ver}-windows-x86_64"
zip="${OUT}/${name}.zip"
setup="${name}-setup.exe"
[[ -f "$zip" ]] || fDie "no ${name}.zip in ${OUT} - run pack-zip.bash first"

fSetSourceDate "$ROOT"
fWarnIfSourceDateIsAGuess "$ROOT"

fEcho_Clean
fEcho "Packing ${setup}"

work="$(mktemp -d)"
box="/tmp/${SLUG}-setup.$$"
trap 'rm -rf "${work}"; docker exec "$CONTAINER" rm -rf "${box}" >/dev/null 2>&1 || true' EXIT

unzip -q "$zip" -d "${work}/payload"
[[ -f "${work}/payload/${name}/${SLUG}.exe" ]] || fDie "${name}.zip has no ${name}/${SLUG}.exe"
## NSIS keeps each file's time and puts it back on install, so they get the
## commit's time, as in the zip. unzip alone sets the folders' to now.
find "${work}/payload" -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +

docker exec "$CONTAINER" mkdir -p "$box"
docker cp "${work}/payload/${name}" "${CONTAINER}:${box}/payload" >/dev/null
docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH}" "$CONTAINER" makensis -V2 -NOCD \
	"-DVERSION=${ver}" "-DVIVERSION=${viver}" \
	"-DPAYLOAD=${box}/payload" "-DICON=/src/source/src/${SLUG}.ico" \
	"-DOUTFILE=${box}/${setup}" /src/cicd/win/setup.nsi \
	|| fDie "makensis failed"

mkdir -p "$OUT"
rm -f "${OUT:?}/${setup}"
docker cp "${CONTAINER}:${box}/${setup}" "${OUT}/${setup}" >/dev/null
touch -d "@${SOURCE_DATE_EPOCH}" "${OUT}/${setup}"

fEcho "OK: ${setup} ($(du -h --apparent-size "${OUT}/${setup}" | cut -f1))"
fEcho_Clean


##	History:
##		- 2026-10-07: Created.
