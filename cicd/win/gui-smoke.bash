#!/usr/bin/env bash

##	- Purpose: Headless GUI smoke test for the cross-built Windows target. Launches
##	  nemo-anywhere.exe under wine on a throwaway Xvfb display, confirms the main
##	  window comes up, and drops a screenshot. The --version smoke does not exercise
##	  GTK/gvfs at all, so this is what actually proves the app runs and browses.
##	- Runs INSIDE the nemo-winbuild container: docker exec nemo-winbuild bash /src/cicd/win/gui-smoke.bash
##	- Syntax: gui-smoke.bash [SHOT_PNG] [SECONDS]   (defaults /tmp/shot.png, 16)
##	- Wine needs its DLLs and schemas on-path: WINEPATH -> sysroot bin (the extension
##	  lib is folded into the exe); GSETTINGS_SCHEMA_DIR -> a dir holding nemo's schema merged
##	  with the sysroot GTK schemas, compiled here (glib-compile-schemas output is
##	  arch-independent, so the Linux tool's result works for the wine build).
##	- Test ID: rcdybfj0

set -euo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

## No crash dumps: the workdir is the mounted repo and core_pattern is relative, so a
## wine/GTK crash would leave a root-owned core.<pid> the host user can't even delete.
ulimit -c 0

SYSROOT="/opt/win-sysroot"
BUILD="/build-win"
SHOT="${1:-/tmp/shot.png}"
DWELL="${2:-16}"
URI="${3:-}"
SCHEMAS="/tmp/nemo-schemas"
DISP=":99"

fEcho(){ echo "[ $* ]"; }

fEcho "Compiling schemas"
mkdir -p "$SCHEMAS"
cp "$SYSROOT"/mingw64/share/glib-2.0/schemas/*.gschema.xml "$SCHEMAS/" 2>/dev/null || true
cp "$SYSROOT"/mingw64/share/glib-2.0/schemas/gschema.dtd "$SCHEMAS/" 2>/dev/null || true
glib-compile-schemas "$SCHEMAS"

export WINEDEBUG=-all
## A crash would otherwise put a modal box on a display nobody is watching.
export NEMO_NO_CRASH_DIALOG=1
export DISPLAY="$DISP"
export WINEPATH="Z:\\opt\\win-sysroot\\mingw64\\bin"
export GSETTINGS_SCHEMA_DIR="Z:\\tmp\\nemo-schemas"

fEcho "Starting Xvfb $DISP"
Xvfb "$DISP" -screen 0 1280x900x24 >/tmp/xvfb.log 2>&1 &
xpid=$!
trap 'kill $xpid 2>/dev/null || true' EXIT
sleep 2

fEcho "Launching nemo-anywhere.exe under wine"
if [[ -n $URI ]]; then
	wine "$BUILD/src/nemo-anywhere.exe" "$URI" >/tmp/nemo-gui.out 2>&1 &
else
	wine "$BUILD/src/nemo-anywhere.exe" >/tmp/nemo-gui.out 2>&1 &
fi
wpid=$!
sleep "$DWELL"

rc=0
if xwininfo -root -tree 2>/dev/null | grep -qiE '0x[0-9a-f]+ "(Home|File System|Trash|Network|nemo)'; then
	fEcho "Main window present"
	import -window root "$SHOT" 2>/dev/null && fEcho "Screenshot -> $SHOT"
else
	fEcho "FAILED: no main window - see /tmp/nemo-gui.out"
	rc=1
fi

kill "$wpid" 2>/dev/null || true
exit "$rc"
