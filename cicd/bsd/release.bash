#!/usr/bin/env bash

##	- Purpose: Build the FreeBSD release on the FreeBSD box itself: a release
##	  build, the --version smoke and the extension load test, the relocatable
##	  prefix, and a pkg file of it with its dependencies in the manifest.
##	  lane.bash runs it there and brings both back.
##	- Leaves in cicd/artifacts/release, in the tree it was run from:
##	  nemo-anywhere-<ver>-bsd-<arch>/      the prefix, packed into the
##	                                       -bsd- tarball on the Linux side,
##	                                       where GNU tar can stamp and sort it
##	  nemo-anywhere-<ver>-bsd-<arch>.pkg   for pkg add
##	- The pkg puts the prefix at /usr/local/nemo-anywhere, where install.bash
##	  puts a system install on BSD, plus the command in /usr/local/bin, the menu
##	  entry and the app icon, which goes in the shared theme only. pkg's own
##	  triggers refresh the icon cache.
##	- Dependencies are the packages that own each library the binaries link,
##	  read off this box with pkg which, plus gdk-pixbuf-extra for the picture
##	  formats gdk-pixbuf leaves out (design.md, Building on FreeBSD). Base
##	  system libraries are left out, since those come with the OS.
##	- Native build with the base clang, no container, so the versions are what
##	  pkg had here at the time.
##	- SOURCE_DATE_EPOCH must be set; there is no git history here to read it
##	  from. CICD_MAX_JOBS caps the build (default 4). NEMO_BSD_BUILD is the
##	  build dir (default: build-release beside the tree), set up from empty
##	  every run.
##	- Syntax: release.bash

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SLUG="nemo-anywhere"
OUT="${ROOT}/cicd/artifacts/release"
BUILD="${NEMO_BSD_BUILD:-$(cd "${ROOT}/.." && pwd)/build-release}"
PKG_PREFIX="/usr/local"
APP_DIR="${PKG_PREFIX}/${SLUG}"

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/release-files.bash
source "${ROOT}/cicd/utility/include/release-files.bash"
# shellcheck source=../utility/include/xvfb.bash
source "${ROOT}/cicd/utility/include/xvfb.bash"

case "${1:-}" in
	-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	"") ;;
	*) fDie "unknown option: $1 (try --help)" ;;
esac

[[ "$(uname -s)" == "FreeBSD" ]] || fDie "this runs on FreeBSD; lane.bash sends it there"
[[ "${SOURCE_DATE_EPOCH:-}" =~ ^[0-9]+$ ]] || fDie "SOURCE_DATE_EPOCH is not set"
export SOURCE_DATE_EPOCH
jobs="${CICD_MAX_JOBS:-4}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || jobs=4

## No grep -P here.
ver="$(sed -n "s/^project(.*[[:space:]]version[[:space:]]*:[[:space:]]*'\([^']*\)'.*/\1/p" "${ROOT}/source/meson.build" | head -1)"
[[ -n "$ver" ]] || fDie "no version in source/meson.build"
arch="$(fReleaseArch "$(uname -m)")" || fDie "no release name for a $(uname -m) build"
os="$(fReleaseOs "$(uname -s)")" || fDie "no release name for a $(uname -s) build"
name="${SLUG}-${ver}-${os}-${arch}"
## pkg splits name from version at the last dash, and sorts 1.0.0.beta2 below 1.0.0.
pkgVer="${ver//-/.}"

work="$(mktemp -d /tmp/${SLUG}-bsd-release.XXXXXX)"
fCleanup(){
	if [[ -n "${XVFB_PID:-}" ]]; then kill "${XVFB_PID}" 2>/dev/null || true; fi
	rm -rf "${work}"
}
trap fCleanup EXIT


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Build, smoke

fEcho_Clean ""
fEcho "Building ${SLUG} ${ver} (${os}-${arch}) on $(uname -r), $(cc --version | head -1)"
## Only ever clear a meson build dir.
if [[ -e "$BUILD" ]]; then
	[[ "$BUILD" == /*/* ]] || fDie "build dir must be an absolute path below /: ${BUILD}"
	[[ -d "${BUILD}/meson-private" ]] || fDie "${BUILD} is not a meson build dir; not clearing it"
	rm -rf "$BUILD"
fi
meson setup --buildtype=release -Dstrip=true -Db_lto=true -Dextension_library=static -Dwerror=true \
	"-Dprefix=${APP_DIR}" "$BUILD" "${ROOT}/source" >/dev/null
bash "${ROOT}/cicd/utility/check-werror.bash" "$BUILD"
ninja -C "$BUILD" -j "$jobs" | tail -1

fEcho_Clean ""
fEcho "Smoke test"
fXvfbStart 120 1280x1024x24 "${work}/xvfb.log" || fDie "no free X display from :120 up"
export DISPLAY="${XVFB_DISPLAY}" DBUS_SESSION_BUS_ADDRESS='disabled:'
smoke="$("${BUILD}/src/${SLUG}" --version)"
## Prefix match: the build number moves on every reconfigure.
[[ "$smoke" == "${SLUG} v${ver} build "* ]] || fDie "smoke test said '${smoke}', expected '${SLUG} v${ver} build <n>'"
fEcho_Clean "$smoke"
meson test -C "$BUILD" --no-rebuild "rh5f9h18 Extension load test" >/dev/null \
	|| fDie "extension load test failed; see ${BUILD}/meson-logs/testlog.txt"
kill "${XVFB_PID}" 2>/dev/null || true
XVFB_PID=""

fEcho_Clean ""
fEcho "Staging prefix"
bash "${ROOT}/cicd/linux/stage-prefix.bash" "$BUILD" "${work}/${SLUG}"
mkdir -p "$OUT"
rm -rf "${OUT:?}/${name}" "${OUT}/${name}.pkg"
cp -a "${work}/${SLUG}" "${OUT}/${name}"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# pkg

fEcho_Clean ""
fEcho "Packing ${name}.pkg"
root="${work}/root"
app="${root}${APP_DIR}"
mkdir -p "${root}${PKG_PREFIX}/bin" "${root}${PKG_PREFIX}/share/applications"
cp -a "${work}/${SLUG}" "$app"
ln -s "${APP_DIR}/bin/${SLUG}" "${root}${PKG_PREFIX}/bin/${SLUG}"
desktop="${app}/share/applications/${SLUG}.desktop"
[[ -f "$desktop" ]] || fDie "no menu entry at share/applications/${SLUG}.desktop in the prefix"
sed -e "s|^Exec=.*|Exec=${APP_DIR}/bin/${SLUG} %U|" \
    -e "s|^TryExec=.*|TryExec=${APP_DIR}/bin/${SLUG}|" \
    -e "s|^Icon=.*|Icon=${SLUG}|" "$desktop" > "${root}${PKG_PREFIX}/share/applications/${SLUG}.desktop"
icons=0
for src in "${app}"/share/icons/hicolor/*/apps/"${SLUG}".*; do
	[[ -f "$src" ]] || continue
	size="${src%/apps/*}"; size="${size##*/}"
	mkdir -p "${root}${PKG_PREFIX}/share/icons/hicolor/${size}/apps"
	cp -a "$src" "${root}${PKG_PREFIX}/share/icons/hicolor/${size}/apps/"
	icons=$((icons + 1))
done
((icons)) || fDie "no app icon in the prefix"
## Only the shared theme's copy. pkg's icon cache trigger writes a cache into
## any share/icons dir it installs, and on delete removes it only after pkg has
## left the dirs above it, so a second copy in the app folder left that behind.
rm -rf "${app}/share/icons"

## name|origin|version per line, for every package that owns a library some
## binary in the prefix links directly.
fLinkedPackages(){
	local elf lib path owner links
	while IFS= read -r elf; do
		[[ "$(file -b "$elf")" == ELF* ]] || continue
		links="$(ldd "$elf")"
		while IFS= read -r lib; do
			path="$(LIB="$lib" awk '$1 == ENVIRON["LIB"] && $2 == "=>" && !done { print $3; done = 1 }' <<<"$links")"
			[[ -n "$path" && "$path" != "not" ]] || fDie "$(basename "$elf") needs ${lib}, which is not on this box"
			## Base libraries belong to the OS, or to FreeBSD-* packages under pkgbase.
			[[ "$path" == "${PKG_PREFIX}"/* ]] || continue
			owner="$(pkg which -q "$path" || true)"
			[[ -n "$owner" ]] || fDie "no package owns ${path}, which $(basename "$elf") needs"
			pkg query '%n|%o|%v' "$owner"
		done < <(readelf -d "$elf" | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p')
	done < <(find "$app" -type f -perm -u+x; find "$app" -type f -name '*.so*')
}
deps="$(fLinkedPackages | LC_ALL=C sort -u)"
## Loaded by gdk-pixbuf at run time, so no binary links it.
extra="$(pkg query '%n|%o|%v' gdk-pixbuf-extra || true)"
[[ -n "$extra" ]] || fDie "gdk-pixbuf-extra is not installed here, and the package depends on it"
deps="$(printf '%s\n%s\n' "$deps" "$extra" | LC_ALL=C sort -u | sed '/^$/d')"
grep -q '^gtk3|' <<<"$deps" || fDie "no gtk3 among the dependencies read off the binaries"

## UCL takes JSON, which python writes without any quoting to get wrong.
meta="${work}/meta"
mkdir -p "$meta"
flatsize="$(find "${root}" -type f -exec stat -f %z {} + | awk '{ s += $1 } END { print s + 0 }')"
DEPS="$deps" python3 - "$meta/+MANIFEST" "$SLUG" "$pkgVer" "$PKG_PREFIX" "$(pkg config abi)" "$flatsize" <<-'EOF'
	import json, os, sys
	out, name, version, prefix, abi, flatsize = sys.argv[1:]
	deps = {}
	for line in os.environ["DEPS"].splitlines():
	    n, origin, v = line.split("|")
	    deps[n] = {"origin": origin, "version": v}
	manifest = {
	    "name": name,
	    "version": version,
	    "origin": "x11-fm/" + name,
	    "comment": "Portable fork of the Nemo file manager",
	    "desc": "A portable fork of Nemo that runs on any desktop, or none.\nInstalls alongside an existing Nemo without conflicting with it.",
	    "maintainer": "t00mietum@users.noreply.github.com",
	    "www": "https://github.com/yottacore/" + name,
	    "prefix": prefix,
	    "abi": abi,
	    "flatsize": int(flatsize),
	    "licenselogic": "single",
	    "licenses": ["GPLv2"],
	    "categories": ["x11-fm"],
	    "deps": deps,
	    "messages": [{"message": "The action layout editor needs PyGObject for the default Python, such as py312-pygobject."}],
	}
	with open(out, "w") as f:
	    json.dump(manifest, f, indent=1, sort_keys=True)
	    f.write("\n")
EOF
( cd "${root}${PKG_PREFIX}" && find . \( -type f -o -type l \) | sed 's|^\./||' | LC_ALL=C sort ) > "${work}/plist"

mkdir -p "${work}/pkgout"
pkg create -m "$meta" -r "$root" -p "${work}/plist" -o "${work}/pkgout" -t "$SOURCE_DATE_EPOCH"
made="${work}/pkgout/${SLUG}-${pkgVer}.pkg"
[[ -f "$made" ]] || fDie "pkg create made no ${made##*/}"
mv "$made" "${OUT}/${name}.pkg"

fEcho_Clean "depends on: $(cut -d'|' -f1 <<<"$deps" | tr '\n' ' ')"
fEcho_Clean "$(cd "$OUT" && ls -l "${name}.pkg")"
fEcho_Clean ""


##	History:
##		- 2026-10-07: Created.
