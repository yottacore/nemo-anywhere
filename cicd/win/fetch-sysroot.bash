#!/usr/bin/env bash

##	- Purpose: Assemble a mingw-w64 GTK3 sysroot for cross-building the Windows target,
##	  from the MSYS2 packages pinned in sysroot-pins.txt by file name and sha256.
##	  msys2-pins.bash update writes that list from the same package database as the
##	  hosted build's, so the cross build links and packs the same DLLs, whatever
##	  day the image is built.
##	- No pacman needed: each .pkg.tar.zst comes straight from repo.msys2.org, which
##	  keeps older versions, and is unpacked into the sysroot. That keeps the whole
##	  GTK3 Windows stack out of the host and the repo, and only in the build
##	  image. A file the repo no longer has fails the build, which means it's
##	  time to move the pins.
##	- MSYS2_REPO_ROOT points it at another copy of the repo, such as a test's.
##	- Syntax: fetch-sysroot.bash [SYSROOT_DIR [PINS_FILE]]
##	  (defaults /opt/win-sysroot and sysroot-pins.txt beside this script)
##	  Result layout: $SYSROOT/mingw64/{include,lib,bin,...}; .pc files keep prefix=/mingw64,
##	  so the meson cross file points PKG_CONFIG_SYSROOT_DIR at $SYSROOT.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${MSYS2_REPO_ROOT:-https://repo.msys2.org}"
SYSROOT="${1:-/opt/win-sysroot}"
PINS="${2:-${here}/sysroot-pins.txt}"
# shellcheck source=../utility/include/run-temp.bash
source "${here}/../utility/include/run-temp.bash"

work="$(mktemp -d)"
trap 'fSafeRm_IfThere "$work"' EXIT

fEcho(){ echo "[ $* ]"; }
fDie(){ fEcho "FAILED: $*"; exit 1; }

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Fetch every pinned file and check it before anything is unpacked
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

files=()
while read -r repo file sha; do
	[[ -z "${repo}" || "${repo}" == \#* ]] && continue
	[[ "${repo}" == mingw/mingw64 && "${file}" != */* && "${file}" == *.pkg.tar.zst && "${sha}" =~ ^[0-9a-f]{64}$ ]] \
		|| fDie "bad line in ${PINS}: ${repo} ${file} ${sha}"
	printf 'url = "%s/%s/%s"\noutput = "%s/%s"\n' "${REPO_ROOT}" "${repo}" "${file}" "${work}" "${file}" >> "${work}/curl.cfg"
	printf '%s  %s\n' "${sha}" "${file}" >> "${work}/sums"
	files+=("${file}")
done < "${PINS}"
((${#files[@]})) || fDie "no packages in ${PINS}"

## Under MSYSTEM=MINGW64 a native mingw64 curl can come first, and it can't open MSYS paths like /tmp.
curlBin="$(command -v curl || true)"
[[ "${curlBin}" == /mingw* && -x /usr/bin/curl ]] && curlBin=/usr/bin/curl
fEcho "Fetching ${#files[@]} pinned packages"
"${curlBin:-curl}" -fsSL --retry 3 --parallel --parallel-max 8 -K "${work}/curl.cfg" || fDie "couldn't fetch the pinned packages"
## These libraries go out in the release, so none is unpacked unverified.
(cd "${work}" && sha256sum --quiet --strict -c sums) || fDie "checksum mismatch"

mkdir -p "$SYSROOT"
for file in "${files[@]}"; do
	tar --zstd -xf "${work}/${file}" -C "$SYSROOT" \
		--exclude='.PKGINFO' --exclude='.BUILDINFO' --exclude='.MTREE' --exclude='.INSTALL'
done

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Normalize thumbnailer descriptors. Some bake an absolute /mingw64/bin/ exec
#	path, which is dead in every real Windows layout. Others already use a bare
#	name. Strip the prefix so the exec resolves off PATH, where the thumbnailer
#	exes always are.
#	The ones run by gdk-pixbuf-thumbnailer go, as in every Windows bundle: the app
#	draws those pictures itself. So does gsf-office's, which no root here pulls in
#	now, since the app reads office files itself. cicd/utility/include/thumbnailers.bash
#	has the same rules for the bundles; this script runs alone in the image build.
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

thumbDir="$SYSROOT/mingw64/share/thumbnailers"
if [[ -d "$thumbDir" ]]; then
	## nullglob-safe: an unmatched glob would hand sed the literal pattern and
	## abort the whole sysroot build after every package had already downloaded.
	shopt -s nullglob
	thumbFiles=( "$thumbDir"/*.thumbnailer )
	shopt -u nullglob
	if ((${#thumbFiles[@]})); then
		fEcho "Normalizing thumbnailer exec paths"
		sed -i 's#/mingw64/bin/##g' "${thumbFiles[@]}"
		for t in "${thumbFiles[@]}"; do
			if grep -q -E '^(TryExec|Exec)=(gdk-pixbuf|gsf-office)-thumbnailer([[:space:]]|$)' "$t"; then
				fEcho "Leaving out ${t##*/}"
				rm -f "$t"
			fi
		done
	fi
fi

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	gdk-pixbuf loaders.cache. Pacman's post-install normally builds this; we skip
#	pacman, so without it only the built-in PNG/JPEG loaders work and every SVG -
#	i.e. all the symbolic toolbar/sidebar icons - fails to load. Generate it with
#	the mingw query-loaders tool under wine. The tool anchors the loader paths at
#	the mingw64 prefix (relative), so the cache resolves wherever the tree sits.
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

pixLoaders="$SYSROOT/mingw64/lib/gdk-pixbuf-2.0/2.10.0"
queryLoaders="$SYSROOT/mingw64/bin/gdk-pixbuf-query-loaders.exe"
if [[ -f "$queryLoaders" ]]; then
	fEcho "Building gdk-pixbuf loaders.cache"
	binWin="Z:${SYSROOT//\//\\}\\mingw64\\bin"
	## Write to a temp and only move it into place once it looks right. The
	## redirection truncates its target before wine even starts, so on failure
	## this used to leave a zero-byte cache - worse than none, because
	## gdk-pixbuf then finds no loaders at all instead of falling back.
	cacheTmp="$pixLoaders/loaders.cache.new"
	if WINEDEBUG=-all WINEPATH="$binWin" wine "$queryLoaders" > "$cacheTmp" 2>/dev/null \
	   && grep -q pixbufloader_svg "$cacheTmp"; then
		mv -f "$cacheTmp" "$pixLoaders/loaders.cache"
	else
		rm -f "$cacheTmp"
		fEcho "warn: loaders.cache generation failed - SVG/symbolic icons may not render"
	fi
fi

fEcho "Sysroot ready: $SYSROOT ($(du -sh "$SYSROOT" | cut -f1))"
