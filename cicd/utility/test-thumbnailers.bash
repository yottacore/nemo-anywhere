#!/usr/bin/env bash

##	- Purpose: Check that the native Windows stager copies thumbnailer
##	  descriptors with bare program names. MSYS2's gdk-pixbuf and librsvg ones
##	  say `/mingw64/bin/gdk-pixbuf-thumbnailer`, which the app never finds.
##	  Runs fStageThumbnailers on copies of those descriptors, and checks that
##	  stage-native.bash goes through it.
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

cat > "${src}/gdk-pixbuf-thumbnailer.thumbnailer" <<'EOF'
[Thumbnailer Entry]
TryExec=/mingw64/bin/gdk-pixbuf-thumbnailer
Exec=/mingw64/bin/gdk-pixbuf-thumbnailer -s %s %u %o
MimeType=image/bmp;image/png;
EOF
cat > "${src}/gsf-office.thumbnailer" <<'EOF'
[Thumbnailer Entry]
TryExec=gsf-office-thumbnailer
Exec=gsf-office-thumbnailer -i %i -o %o -s %s
MimeType=application/vnd.oasis.opendocument.text;
EOF
cat > "${src}/other.thumbnailer" <<'EOF'
[Thumbnailer Entry]
TryExec=/ucrt64/bin/some-thumbnailer
Exec=/ucrt64/bin/some-thumbnailer --in /tmp/x/%i %o
MimeType=x/y;
EOF

dest="${scratch}/dest"
if ! out="$(fStageThumbnailers "$src" "$dest" 2>&1)"; then
	fFail "fStageThumbnailers refused the MSYS2 descriptors: ${out}"
fi

fWant(){
	local file="$1" line="$2"
	grep -qxF "$line" "${dest}/${file}" || fFail "${file}: no line '${line}'; has: $(tr '\n' '|' < "${dest}/${file}" 2>/dev/null || true)"
}
fWant gdk-pixbuf-thumbnailer.thumbnailer "TryExec=gdk-pixbuf-thumbnailer"
fWant gdk-pixbuf-thumbnailer.thumbnailer "Exec=gdk-pixbuf-thumbnailer -s %s %u %o"
fWant gsf-office.thumbnailer "TryExec=gsf-office-thumbnailer"
fWant gsf-office.thumbnailer "Exec=gsf-office-thumbnailer -i %i -o %o -s %s"
## Only the program loses its folder, not an argument.
fWant other.thumbnailer "Exec=some-thumbnailer --in /tmp/x/%i %o"
fWant other.thumbnailer "MimeType=x/y;"

## A missing source folder is not an error; MSYS2 may have no thumbnailers.
fStageThumbnailers "${scratch}/none" "${scratch}/dest2" || fFail "a missing source folder failed"

## The stager has to use it, after its last copy of share/.
stager="${root}/cicd/win/stage-native.bash"
grep -qE '^fStageThumbnailers "\$\{MINGW\}/share/thumbnailers" "\$\{DEST\}/mingw64/share/thumbnailers"' "$stager" \
	|| fFail "stage-native.bash does not stage descriptors through fStageThumbnailers"
if grep -nE 'cp .*share/(\$\{d\}|thumbnailers)' "$stager" | grep -v fStageThumbnailers; then
	fFail "stage-native.bash still copies share/thumbnailers as is"
fi

if ((failures)); then
	fEcho "FAILED: thumbnailer staging, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: staged thumbnailer descriptors name bare programs"
