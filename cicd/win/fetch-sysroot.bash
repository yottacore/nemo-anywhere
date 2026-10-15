#!/usr/bin/env bash

##	- Purpose: Assemble a mingw-w64 GTK3 sysroot for cross-building the Windows target.
##	  Resolves the transitive dependency closure of a few root packages from the MSYS2
##	  pacman database, then downloads and unpacks each prebuilt package into the sysroot.
##	- No pacman needed: we parse the .db (a tarball of 'desc' files) ourselves and pull
##	  the .pkg.tar.zst files straight from the mirror. Keeps the whole GTK3 Windows stack
##	  out of both the host and the repo - it lives only inside the build image.
##	- Syntax: fetch-sysroot.bash [SYSROOT_DIR]   (default /opt/win-sysroot)
##	  Result layout: $SYSROOT/mingw64/{include,lib,bin,...}; .pc files keep prefix=/mingw64,
##	  so the meson cross file points PKG_CONFIG_SYSROOT_DIR at $SYSROOT.

set -euo pipefail

MIRROR="https://mirror.msys2.org/mingw/mingw64"
SYSROOT="${1:-/opt/win-sysroot}"
PFX="mingw-w64-x86_64-"

##	Roots: gtk3 drags in the bulk of the stack (glib2, atk, cairo, gdk-pixbuf2, pango,
##	json-glib, libepoxy, gettext, icon theme). The rest are our extra meson deps.
##	Exempi (XMP) is not packaged for mingw - we cross-build with -Dxmp=false instead.
##	sqlite3 already arrives through the gtk3 chain, but the thumbnail cache links it
##	directly, so name it here rather than let a refresh of that chain drop it.
ROOTS=(gtk3 json-glib libexif libarchive sqlite3)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fEcho(){ echo "[ $* ]"; }

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Pull + unpack the pacman database
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

fEcho "Fetching package database"
curl -fsSL "$MIRROR/mingw64.db" -o "$work/db.tar.gz"
mkdir -p "$work/db"
tar -xf "$work/db.tar.gz" -C "$work/db"

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Parse every 'desc' into name -> filename, name -> depends, provides -> name.
#	One gawk pass over all desc files (BEGINFILE/ENDFILE) - version constraints stripped.
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

fEcho "Indexing packages"
declare -A FILEOF DEPSOF PROVIDER SHA256OF
while IFS='|' read -r kind a b; do
	case "$kind" in
		F) FILEOF[$a]=$b ;;
		D) DEPSOF[$a]=$b ;;
		V) PROVIDER[$a]=$b ;;
		S) SHA256OF[$a]=$b ;;
	esac
done < <(gawk '
	BEGINFILE { name=""; file=""; deps=""; provs=""; sha=""; sec="" }
	/^%FILENAME%$/ { sec="F"; next }
	/^%NAME%$/     { sec="N"; next }
	/^%DEPENDS%$/  { sec="D"; next }
	/^%PROVIDES%$/ { sec="V"; next }
	/^%SHA256SUM%$/{ sec="S"; next }
	/^%/           { sec="";  next }
	sec=="F" && NF { file=$0 }
	sec=="N" && NF { name=$0 }
	sec=="S" && NF { sha=$0 }
	sec=="D" && NF { s=$0; sub(/[<>=].*/,"",s); deps=deps" "s }
	sec=="V" && NF { s=$0; sub(/[<>=].*/,"",s); provs=provs" "s }
	ENDFILE {
		if (name!="") {
			print "F|" name "|" file
			print "D|" name "|" deps
			print "S|" name "|" sha
			n=split(provs,pv," "); for(i=1;i<=n;i++) print "V|" pv[i] "|" name
		}
	}
' "$work"/db/*/desc)

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Breadth-first closure from the roots. Unknown names resolve via PROVIDES.
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

declare -A SEEN
queue=()
for r in "${ROOTS[@]}"; do queue+=("$PFX$r"); done

resolved=()
while [[ ${#queue[@]} -gt 0 ]]; do
	pkg="${queue[0]}"; queue=("${queue[@]:1}")
	[[ -n "${SEEN[$pkg]:-}" ]] && continue
	real="$pkg"
	[[ -z "${FILEOF[$real]:-}" && -n "${PROVIDER[$pkg]:-}" ]] && real="${PROVIDER[$pkg]}"
	if [[ -z "${FILEOF[$real]:-}" ]]; then
		echo "  warn: no package for '$pkg' (skipped)" >&2
		continue
	fi
	SEEN[$pkg]=1; SEEN[$real]=1
	resolved+=("$real")
	for d in ${DEPSOF[$real]:-}; do
		[[ -n "${SEEN[$d]:-}" ]] || queue+=("$d")
	done
done

fEcho "Closure: ${#resolved[@]} packages"

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Download + unpack each into the sysroot (skip pacman metadata members).
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

mkdir -p "$SYSROOT"
for pkg in "${resolved[@]}"; do
	file="${FILEOF[$pkg]}"
	fEcho "$pkg"
	curl -fsSL "$MIRROR/$file" -o "$work/$file"

	## The database carries a checksum for every package and these libraries ship
	## in the release, so unpacking one unverified is not on.
	want="${SHA256OF[$pkg]:-}"
	if [[ -z "$want" ]]; then
		rm -f "$work/$file"
		fEcho "FAILED: no checksum in the database for $pkg"
		exit 1
	fi
	got="$(sha256sum "$work/$file" | cut -d' ' -f1)"
	if [[ "$got" != "$want" ]]; then
		rm -f "$work/$file"
		fEcho "FAILED: checksum mismatch for $file"
		fEcho "  expected $want"
		fEcho "  got      $got"
		exit 1
	fi

	tar --zstd -xf "$work/$file" -C "$SYSROOT" \
		--exclude='.PKGINFO' --exclude='.BUILDINFO' --exclude='.MTREE' --exclude='.INSTALL'
	rm -f "$work/$file"
done

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Normalize thumbnailer descriptors. Some bake an absolute /mingw64/bin/ exec
#	path (dead in every real Windows layout); others already use a bare name. Strip
#	the prefix so the exec resolves off PATH, where the thumbnailer exes always sit.
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
