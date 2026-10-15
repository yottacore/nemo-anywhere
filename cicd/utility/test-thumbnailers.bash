#!/usr/bin/env bash

##	- Purpose: Check the thumbnailer descriptors every Windows bundle gets.
##	  The ones run by gdk-pixbuf-thumbnailer (gdk-pixbuf's and librsvg's) are
##	  left out, since the app draws those pictures itself, and so is
##	  gsf-office's, since the app reads office files itself. The rest name
##	  bare programs. MSYS2's say `/mingw64/bin/...`, which the app never finds.
##	  Runs fStageThumbnailers on descriptors written like MSYS2's, and
##	  fetch-sysroot.bash's own rules on the same, and checks that the native
##	  stager, the zip and the wine runner go through the shared step.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-thumbnailers.bash
##	- Test ID: rjnyer4p

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=include/thumbnailers.bash
source "${root}/cicd/utility/include/thumbnailers.bash"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d)"
trap 'rm -rf "${scratch}"' EXIT
src="${scratch}/src"
mkdir -p "$src"

cat > "${src}/gdk-pixbuf-thumbnailer.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=/mingw64/bin/gdk-pixbuf-thumbnailer
Exec=/mingw64/bin/gdk-pixbuf-thumbnailer -s %s %u %o
MimeType=image/bmp;image/png;
END
cat > "${src}/librsvg.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=gdk-pixbuf-thumbnailer
Exec=gdk-pixbuf-thumbnailer -s %s %u %o
MimeType=image/svg+xml;image/svg+xml-compressed;
END
cat > "${src}/webp.thumbnailer" <<'END'
[Thumbnailer Entry]
Exec=C:\msys64\mingw64\bin\gdk-pixbuf-thumbnailer.exe -s %s %u %o
MimeType=image/webp;
END
cat > "${src}/gsf-office.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=gsf-office-thumbnailer
Exec=gsf-office-thumbnailer -i %i -o %o -s %s
MimeType=application/vnd.oasis.opendocument.text;
END
cat > "${src}/gsf-office-msys.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=/mingw64/bin/gsf-office-thumbnailer.exe
Exec=/mingw64/bin/gsf-office-thumbnailer.exe -i %i -o %o -s %s
MimeType=application/msword;
END
cat > "${src}/other.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=/ucrt64/bin/some-thumbnailer
Exec=/ucrt64/bin/some-thumbnailer --in /tmp/x/%i %o
MimeType=x/y;
END
## Only that name counts, not a longer one that starts the same.
cat > "${src}/lookalike.thumbnailer" <<'END'
[Thumbnailer Entry]
TryExec=gdk-pixbuf-thumbnailer-plus
Exec=gdk-pixbuf-thumbnailer-plus %i %o
MimeType=x/z;
END

dest="${scratch}/dest"
if ! out="$(fStageThumbnailers "$src" "$dest" 2>&1)"; then
	fFail "fStageThumbnailers refused the MSYS2 descriptors: ${out}"
fi

fWant(){
	local file="$1" line="$2"
	grep -qxF "$line" "${dest}/${file}" || fFail "${file}: no line '${line}'; has: $(tr '\n' '|' < "${dest}/${file}" 2>/dev/null || true)"
}
fGone(){
	[[ ! -e "${1}/${2}" ]] || fFail "${3}: ${2} was not left out"
}
fGone "$dest" gdk-pixbuf-thumbnailer.thumbnailer fStageThumbnailers
fGone "$dest" librsvg.thumbnailer fStageThumbnailers
fGone "$dest" webp.thumbnailer fStageThumbnailers
## Kept with a bare name until 2026100909260549, which leaves it out.
# fWant gsf-office.thumbnailer "TryExec=gsf-office-thumbnailer"
# fWant gsf-office.thumbnailer "Exec=gsf-office-thumbnailer -i %i -o %o -s %s"
fGone "$dest" gsf-office.thumbnailer fStageThumbnailers
fGone "$dest" gsf-office-msys.thumbnailer fStageThumbnailers
## Only the program loses its folder, not an argument.
fWant other.thumbnailer "Exec=some-thumbnailer --in /tmp/x/%i %o"
fWant other.thumbnailer "MimeType=x/y;"
fWant lookalike.thumbnailer "TryExec=gdk-pixbuf-thumbnailer-plus"

## A missing source folder is not an error; MSYS2 may have no thumbnailers.
fStageThumbnailers "${scratch}/none" "${scratch}/dest2" || fFail "a missing source folder failed"

## fetch-sysroot.bash runs alone in the image build, so it has its own copy of
## the rules. Its block runs here on descriptors as the sysroot has them.
fetch="${root}/cicd/win/fetch-sysroot.bash"
block="$(awk '/^thumbDir="\$SYSROOT/ { on = 1 } on && /^#/ { exit } on' "$fetch")"
if [[ -z "$block" ]]; then
	fFail "fetch-sysroot.bash has no thumbnailer block"
else
	thumbs="${scratch}/sysroot/mingw64/share/thumbnailers"
	mkdir -p "$thumbs"
	cp "${src}/gdk-pixbuf-thumbnailer.thumbnailer" "${src}/librsvg.thumbnailer" \
		"${src}/gsf-office.thumbnailer" "${src}/lookalike.thumbnailer" "$thumbs/"
	if ! out="$(SYSROOT="${scratch}/sysroot" bash -c 'set -euo pipefail; fEcho(){ :; }; eval "$1"' _ "$block" 2>&1)"; then
		fFail "fetch-sysroot.bash's thumbnailer block failed: ${out}"
	fi
	fGone "$thumbs" gdk-pixbuf-thumbnailer.thumbnailer fetch-sysroot.bash
	fGone "$thumbs" librsvg.thumbnailer fetch-sysroot.bash
	fGone "$thumbs" gsf-office.thumbnailer fetch-sysroot.bash
	## gsf-office.thumbnailer was compared here too until 2026100909260549.
	cmp -s "${thumbs}/lookalike.thumbnailer" "${dest}/lookalike.thumbnailer" \
		|| fFail "fetch-sysroot.bash and fStageThumbnailers differ on lookalike.thumbnailer"
fi

## Every bundle has to use the shared step. None may copy the folder as is, or
## carry gdk-pixbuf-thumbnailer.exe or gsf-office-thumbnailer.exe, which
## nothing would start.
fUses(){
	local script="$1" pattern="$2" rel="${1#"${root}"/}"
	grep -qE "$pattern" "$script" || fFail "${rel} does not stage descriptors through fStageThumbnailers"
	if grep -nE 'cp .*share/(\$\{d\}|thumbnailers)' "$script" | grep -v fStageThumbnailers; then
		fFail "${rel} still copies share/thumbnailers as is"
	fi
	if grep -nF 'gdk-pixbuf-thumbnailer.exe' "$script"; then
		fFail "${rel} still bundles gdk-pixbuf-thumbnailer.exe"
	fi
	if grep -nF 'gsf-office-thumbnailer.exe' "$script"; then
		fFail "${rel} still bundles gsf-office-thumbnailer.exe"
	fi
}
fUses "${root}/cicd/win/stage-native.bash" '^fStageThumbnailers "\$\{MINGW\}/share/thumbnailers" "\$\{DEST\}/mingw64/share/thumbnailers"'
# shellcheck disable=SC2016  # the script's own text, $1 and all
fUses "${root}/cicd/win/pack-zip.bash" 'fStageThumbnailers "\$1" "\$2"'
fUses "${root}/utility/run-windows-build-via-wine.bash" 'fStageThumbnailers /opt/win-sysroot/mingw64/share/thumbnailers '

if ((failures)); then
	fEcho "FAILED: thumbnailer staging, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: Windows bundles leave out the gdk-pixbuf and gsf-office descriptors and name bare programs"
