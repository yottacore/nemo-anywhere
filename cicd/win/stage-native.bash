#!/usr/bin/env bash

##	- Purpose: Stage a self-contained Windows runtime bundle for the NATIVE
##	  (MSYS2/MinGW-w64) build of nemo-anywhere, so it runs on a box that has no
##	  MSYS2 - and can ride the dogfood sync. The native analog of the cross
##	  stager in utility/run-windows-build-via-wine.bash, but sourced from the
##	  host's /mingw64 tree instead of the container sysroot.
##	- Native /mingw64/bin holds EVERY installed package's DLLs, so a blind
##	  bin/*.dll copy would be enormous. We copy only the dependency closure:
##	  ldd is recursive on PE, so one pass per binary yields its full static
##	  import set; we union the closures of the app exes, the pixbuf loaders
##	  (dlopen'd, so not in the app's own ldd), and the runtime helper exes.
##	- Layout produced under DEST (matches the win-run snapshot n8runfm reads):
##	    app/       nemo-anywhere.exe (single exe on Windows; extension lib is folded in)
##	    mingw64/bin, lib/gdk-pixbuf-2.0, share/{glib-2.0/schemas,icons,themes,thumbnailers}, etc
##	- Run under the mingw64 environment: MSYSTEM=MINGW64 bash cicd/win/stage-native.bash <build-dir> <dest-dir>
##	- Syntax: stage-native.bash <build-dir> <dest-dir>   (dest is wiped and rebuilt)

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

BUILD="${1:?usage: stage-native.bash <build-dir> <dest-dir>}"
DEST="${2:?usage: stage-native.bash <build-dir> <dest-dir>}"
MINGW="${MINGW_PREFIX:-/mingw64}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"	# .../github, for vendor/

fEcho(){ echo "[ $* ]"; }

## The strip below rewrites the PE Time/Date field, and left to itself it writes
## the clock, so two builds of one tag would differ. The workflow exports this in
## the build step only, and that is a different shell, so compute it here - which
## covers the local cicd-win.ps1 path too.
# shellcheck source=../utility/include/source-date.bash
source "${REPO}/cicd/utility/include/source-date.bash"
# shellcheck source=../utility/include/pixbuf-loaders.bash
source "${REPO}/cicd/utility/include/pixbuf-loaders.bash"
# shellcheck source=../utility/include/thumbnailers.bash
source "${REPO}/cicd/utility/include/thumbnailers.bash"
fSetSourceDate "$REPO"

[[ -f "${BUILD}/src/nemo-anywhere.exe" ]] || { fEcho "FAILED: no exe at ${BUILD}/src/nemo-anywhere.exe"; exit 1; }

fEcho "Staging native runtime -> ${DEST}"
rm -rf "${DEST}"
mkdir -p "${DEST}/app" "${DEST}/mingw64/bin" "${DEST}/mingw64/lib" \
	"${DEST}/mingw64/share/glib-2.0" "${DEST}/mingw64/etc"

## App: the main exe (the extension lib is statically linked in, so no sibling dll)
## plus the search converters, which sit beside it so a program lookup finds them
## before PATH is even consulted. Their definitions go under share the way the
## Linux install lays them out.
cp "${BUILD}/src/"*.exe "${DEST}/app/"
cp "${BUILD}/search-helpers/"*.exe "${DEST}/app/"
mkdir -p "${DEST}/mingw64/share/nemo-anywhere/search-helpers"
cp "${REPO}/source/search-helpers/"*.nemo_search_helper \
	"${REPO}/source/search-helpers/third-party/"*.nemo_search_helper \
	"${DEST}/mingw64/share/nemo-anywhere/search-helpers/"

## Runtime helper exes that GLib/GTK spawn or that nemo discovers as thumbnailers.
## bin is on PATH in the launched app, so these resolve; the thumbnailer .thumbnailer
## descriptors come across with share/thumbnailers below. No gdk-pixbuf-thumbnailer,
## since its descriptors are left out (include/thumbnailers.bash).
helper_exes=(gdbus.exe gspawn-win64-helper.exe gspawn-win64-helper-console.exe
	gsf-office-thumbnailer.exe)
for h in "${helper_exes[@]}"; do
	[[ -f "${MINGW}/bin/${h}" ]] && cp "${MINGW}/bin/${h}" "${DEST}/mingw64/bin/"
done

## gdk-pixbuf loaders (dlopen'd at runtime - not in the app's ldd), then rebuild the
## cache so it points at these staged loaders rather than the host's absolute paths.
cp -r "${MINGW}/lib/gdk-pixbuf-2.0" "${DEST}/mingw64/lib/"

## Dependency closure. ldd is recursive on PE; keep only /mingw64 paths (System32 and
## the app-local dlls stay out of the bundle's mingw64/bin). Collect over every binary
## that gets loaded: the app exe, the helper exes, and each pixbuf loader.
closure_bins=("${DEST}/app/"*.exe)
for h in "${helper_exes[@]}"; do [[ -f "${DEST}/mingw64/bin/${h}" ]] && closure_bins+=("${DEST}/mingw64/bin/${h}"); done
while IFS= read -r loader; do closure_bins+=("$loader"); done < <(find "${DEST}/mingw64/lib/gdk-pixbuf-2.0" -name '*.dll')

## app/ on PATH so ldd resolves any app-local deps; collect the unique /mingw64 dlls.
tmplist="$(mktemp)"
for bin in "${closure_bins[@]}"; do
	PATH="${DEST}/app:${MINGW}/bin:${PATH}" ldd "$bin" 2>/dev/null \
		| grep -oiE "${MINGW}/bin/[^ ]+\.dll" || true
done | sort -u > "$tmplist"
while IFS= read -r dll; do [[ -f "$dll" ]] && cp -n "$dll" "${DEST}/mingw64/bin/"; done < "$tmplist"
ndll="$(wc -l < "$tmplist")"; rm -f "$tmplist"

## Rebuild the loader cache in-place (arch-independent text; the query tool writes
## paths relative to the loader dir, so the bundle is relocatable). A failed
## query leaves an empty cache behind, which the check below refuses.
if [[ -x "${MINGW}/bin/gdk-pixbuf-query-loaders.exe" ]]; then
	( cd "${DEST}/mingw64" && GDK_PIXBUF_MODULEDIR="lib/gdk-pixbuf-2.0/2.10.0/loaders" \
		"${MINGW}/bin/gdk-pixbuf-query-loaders.exe" > "lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" ) || true
fi
fCheckLoadersCache "${DEST}/mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" \
	|| { fEcho "FAILED: the bundle cannot load images without its gdk-pixbuf loaders.cache"; exit 1; }

## Schemas: nemo's own merged with the GTK ones, compiled (the app hard-aborts on a
## missing schema). glib-compile-schemas output is arch-independent.
mkdir -p "${DEST}/mingw64/share/glib-2.0/schemas"
cp "${MINGW}/share/glib-2.0/schemas/"*.gschema.xml "${DEST}/mingw64/share/glib-2.0/schemas/" 2>/dev/null || true
cp "${MINGW}/share/glib-2.0/schemas/gschema.dtd"   "${DEST}/mingw64/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "${DEST}/mingw64/share/glib-2.0/schemas" >/dev/null 2>&1 || true

## Data: themes and the thumbnailer descriptors.
[[ -d "${MINGW}/share/themes" ]] && cp -r "${MINGW}/share/themes" "${DEST}/mingw64/share/"
fStageThumbnailers "${MINGW}/share/thumbnailers" "${DEST}/mingw64/share/thumbnailers" \
	|| { fEcho "FAILED: thumbnailer descriptors"; exit 1; }

## Icons: hicolor only. The sysroot's Adwaita and AdwaitaLegacy are 2693 files
## (including 33 X11 cursors that do nothing on Windows) to answer the ~180
## names we ask of them, and the packed exe pays for every file it carries at
## every launch. Our own trimmed Adwaita is in vendor/icons with the rest.
mkdir -p "${DEST}/mingw64/share/icons"
[[ -d "${MINGW}/share/icons/hicolor" ]] && cp -r "${MINGW}/share/icons/hicolor" "${DEST}/mingw64/share/icons/"

## The bundled theme set is NOT staged here - it is compiled into the exe as a
## resource (source/gresources/nemo-themes.gresource.xml). It used to be a
## couple of thousand loose files under share/, and the packed exe charges
## about 2.8 ms of launch time for every file it carries.

## etc: fontconfig + gtk settings the runtime reads relative to the prefix.
for d in fonts gtk-3.0; do
	[[ -d "${MINGW}/etc/${d}" ]] && cp -r "${MINGW}/etc/${d}" "${DEST}/mingw64/etc/"
done

## GTK settings. The stock mingw64 etc/gtk-3.0 ships no settings.ini, so GTK falls
## back to defaults: the Adwaita-Sans alias (not bundled -> a thin host substitute)
## rendered with slight hinting and grayscale AA. That reads thin and mushy on
## Windows. Pin the actual Windows UI font and turn on full hinting + subpixel so
## text looks like the rest of the desktop. (Runtime query of the user's *chosen*
## UI font could refine this later; Segoe UI 9 is the Windows default.)
mkdir -p "${DEST}/mingw64/etc/gtk-3.0"
cat > "${DEST}/mingw64/etc/gtk-3.0/settings.ini" <<-'INI'
	[Settings]
	gtk-font-name = Segoe UI 9
	gtk-xft-antialias = 1
	gtk-xft-hinting = 1
	gtk-xft-hintstyle = hintfull
	gtk-xft-rgba = rgb
	gtk-theme-name = Fluent
	gtk-icon-theme-name = Mica
INI

## Fontconfig belt-and-suspenders: map the generic sans-serif to Segoe UI and force
## the same full-hint + subpixel rendering at the fc layer, overriding the stock
## conf.d (10-hinting-slight, 10-sub-pixel-none) that ships thin/grayscale.
mkdir -p "${DEST}/mingw64/etc/fonts/conf.d"
cat > "${DEST}/mingw64/etc/fonts/conf.d/99-nemo-anywhere.conf" <<-'FC'
	<?xml version="1.0"?>
	<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
	<fontconfig>
		<match target="pattern">
			<test name="family"><string>sans-serif</string></test>
			<edit name="family" mode="prepend" binding="strong"><string>Segoe UI</string></edit>
		</match>
		<match target="font">
			<edit name="antialias" mode="assign"><bool>true</bool></edit>
			<edit name="hinting" mode="assign"><bool>true</bool></edit>
			<edit name="hintstyle" mode="assign"><const>hintfull</const></edit>
			<edit name="rgba" mode="assign"><const>rgb</const></edit>
			<edit name="lcdfilter" mode="assign"><const>lcddefault</const></edit>
		</match>
	</fontconfig>
FC

## Launcher at the bundle root. app/nemo-anywhere.exe can't be double-clicked
## directly: Windows only searches the exe's own dir + System32 + PATH for the 50+
## runtime dlls, never mingw64\bin, so a bare launch throws a wall of missing-dll
## dialogs. This wires PATH/schemas/data at the prefix and starts the real exe with
## no console window (wscript). CRLF so Windows Script Host is happy.
launcher="${DEST}/nemo-anywhere.vbs"
{
	printf '%s\r\n' "' Portable launcher - sets the GTK runtime env, then starts nemo-anywhere."
	printf '%s\r\n' "Set sh = CreateObject(\"WScript.Shell\")"
	printf '%s\r\n' "base = Left(WScript.ScriptFullName, InStrRev(WScript.ScriptFullName, \"\\\"))"
	printf '%s\r\n' "Set env = sh.Environment(\"PROCESS\")"
	printf '%s\r\n' "env(\"PATH\") = base & \"mingw64\\bin;\" & env(\"PATH\")"
	printf '%s\r\n' "env(\"GSETTINGS_SCHEMA_DIR\") = base & \"mingw64\\share\\glib-2.0\\schemas\""
	printf '%s\r\n' "env(\"XDG_DATA_DIRS\") = base & \"mingw64\\share\""
	printf '%s\r\n' "' Classic v35 TrueType interpreter = GDI/ClearType grid-fitted hinting."
	printf '%s\r\n' "' Freetype's v40 default renders lighter/thinner than native Windows."
	printf '%s\r\n' "env(\"FREETYPE_PROPERTIES\") = \"truetype:interpreter-version=35\""
	printf '%s\r\n' "sh.CurrentDirectory = base & \"app\""
	printf '%s\r\n' "sh.Run \"\"\"\" & base & \"app\\nemo-anywhere.exe\"\"\", 1, False"
} > "$launcher"

## meson's -Dstrip=true only runs on install, and neither Windows lane installs -
## both copy out of the build directory, which is here. So the shipped exes are
## stripped here instead. Until 20260919 they went out with full DWARF.
strip_cmd=""
for candidate in strip x86_64-w64-mingw32-strip llvm-strip; do
	command -v "$candidate" >/dev/null 2>&1 && { strip_cmd="$candidate"; break; }
done
if [[ -n "$strip_cmd" ]]; then
	nstripped=0
	## Not an && list: a failed strip on the right of one is skipped, the count
	## stays where it was, and the stage goes on to report how many it stripped.
	## That is how an unstripped exe used to ship.
	for binary in "${DEST}/app/"*.exe; do
		[[ -f "$binary" ]] || continue
		if strip_err="$("$strip_cmd" --strip-debug "$binary" 2>&1)"; then
			nstripped=$(( nstripped + 1 ))
		else
			fEcho "FAILED: could not strip ${binary}: ${strip_err:-no error text}"
			exit 1
		fi
	done
	(( nstripped > 0 )) || { fEcho "FAILED: no app exe to strip in ${DEST}/app"; exit 1; }
	fEcho "stripped ${nstripped} app exe(s) with ${strip_cmd}"
	## The strip is the last thing to write the exe, so this is the stamp that
	## ships. A miss here means the bundle is not reproducible from its tag.
	pe="$(fPeTimestamp "${DEST}/app/nemo-anywhere.exe")"
	[[ "$pe" == "${SOURCE_DATE_EPOCH}" ]] \
		|| { fEcho "FAILED: the staged exe is stamped ${pe:-nothing}, not ${SOURCE_DATE_EPOCH} - the strip is ignoring SOURCE_DATE_EPOCH"; exit 1; }
	fEcho "staged exe stamped ${pe}"
	bash "${REPO}/cicd/utility/check-win-build-flags.bash" --shipped "${DEST}/app/nemo-anywhere.exe" \
		|| { fEcho "FAILED: the staged exe is not a stripped release build"; exit 1; }
else
	fEcho "WARNING: no strip found, so the staged exes keep their debug sections"
fi

bundle_mb="$(du -sm "${DEST}" 2>/dev/null | cut -f1)"
fEcho "OK: staged ${ndll} runtime dll(s); bundle ~${bundle_mb} MB -> ${DEST}"
