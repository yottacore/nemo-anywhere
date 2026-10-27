#!/usr/bin/env bash

##	- Purpose: Run the cross-built Windows nemo-anywhere.exe as a GUI app on the
##	  host's live X display via host wine (not the container's - that one is headless).
##	  Stages a runtime snapshot (GTK DLLs, pixbuf loaders, icons, schemas, the exe -
##	  extension lib folded in) from a cross-build step container (cicd/win/win-box.bash)
##	  into cicd/artifacts/win-run,
##	  then launches wine against it. The mingw64/ layout is preserved so GTK's
##	  relative lookups (loaders, schemas, icons) resolve without extra env.
##	- The staged copy is a snapshot: the exe is re-copied on every
##	  run when docker is up, so a fresh cross build is picked up automatically.
##	  Pass --restage to rebuild the whole snapshot (after a sysroot change).
##	- Each run replaces the last: a previous instance of this staged build is killed
##	  first (matched by path, so a nemo launched elsewhere and wineserver are safe).
##	- Zombie note: the build containers now run with --init (a reaping PID 1), so wine
##	  service processes no longer pile up as zombies - no docker restart needed here.
##	- Syntax: run-windows-build-via-wine.bash [--restage] [URI]

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -euo pipefail

## No crash dumps: wine runs with its cwd inside the repo and core_pattern is a bare
## relative name, so a crash would litter the tree (and abort the next rar backup).
ulimit -c 0

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/cicd/artifacts/win-run"

fEcho(){ echo "[ $* ]"; }

# shellcheck source=../cicd/win/win-box.bash
source "${ROOT}/cicd/win/win-box.bash"

restage=0
uri=""
for arg in "$@"; do
	case "$arg" in
		--restage) restage=1 ;;
		*) uri="$arg" ;;
	esac
done

# Each run replaces the last: kill a previous instance of THIS staged build before
# restaging/launching. Matched by our exact path so a nemo the user launched from
# elsewhere is left alone; wineserver is shared with any other wine app, so it is
# never touched. (pkill returns 1 when nothing matched - fine under set -e.)
pkill -f "${DEST}/app/nemo-anywhere.exe" 2>/dev/null || true

## Gone before wine starts, since the app outlives this script.
trap 'fStepBox_Stop' EXIT
boxUp=0
fWinBox_Start "wine run" && boxUp=1

if [[ ! -x "${DEST}/app/nemo-anywhere.exe" ]] || (( restage )); then
	(( boxUp )) || { fEcho "FAILED: no staged copy, and no cross-build container to stage it from"; exit 1; }
	fEcho "Staging wine runtime from $CONTAINER"
	docker exec "$CONTAINER" sh -c '
		set -e
		D=/src/cicd/artifacts/win-run
		rm -rf "$D"
		mkdir -p "$D/mingw64/bin" "$D/mingw64/lib" "$D/mingw64/share/glib-2.0" "$D/app"
		cd /opt/win-sysroot/mingw64
		cp bin/*.dll "$D/mingw64/bin/"
		# Thumbnailer descriptors come below: share/thumbnailers is a data dir.
		# Pictures, SVG and office files are drawn by the app itself, as in the
		# bundles.
		cp -r lib/gdk-pixbuf-2.0 "$D/mingw64/lib/"
		cp -r share/glib-2.0/schemas "$D/mingw64/share/glib-2.0/"
		cp -r share/icons share/themes "$D/mingw64/share/"
		cp -r etc "$D/mingw64/"
		cp -r /src/cicd/win/bundle-etc/. "$D/mingw64/etc/"'
	docker exec "$CONTAINER" bash -c 'source /src/cicd/utility/include/thumbnailers.bash &&
		fStageThumbnailers /opt/win-sysroot/mingw64/share/thumbnailers /src/cicd/artifacts/win-run/mingw64/share/thumbnailers'
	docker exec "$CONTAINER" chown -R "$(id -u):$(id -g)" /src/cicd/artifacts/win-run
fi

# fresh build -> fresh exe, every run docker is up
if (( boxUp )); then
	docker exec "$CONTAINER" sh -c '
		cp /build-win/src/nemo-anywhere.exe /src/cicd/artifacts/win-run/app/
		# Nothing starts a session bus on Windows; a snapshot staged before that
		# still has one.
		rm -f /src/cicd/artifacts/win-run/mingw64/bin/gdbus.exe'
	docker exec "$CONTAINER" chown -R "$(id -u):$(id -g)" /src/cicd/artifacts/win-run/app /src/cicd/artifacts/win-run/mingw64/bin
	fStepBox_Stop
else
	fEcho "WARNING: no cross-build container - using the staged snapshot as-is"
	[[ -f "${DEST}/mingw64/share/glib-2.0/schemas/gschemas.compiled" ]] || glib-compile-schemas "${DEST}/mingw64/share/glib-2.0/schemas"
fi

to_win(){ echo "Z:${1//\//\\}"; }

export WINEDEBUG="${WINEDEBUG:--all}"
WINEPATH="$(to_win "${DEST}/mingw64/bin");$(to_win "${DEST}/app")"
GSETTINGS_SCHEMA_DIR="$(to_win "${DEST}/mingw64/share/glib-2.0/schemas")"
export WINEPATH GSETTINGS_SCHEMA_DIR
export DISPLAY="${DISPLAY:-:0.0}"

# No explicit target -> open a location that actually exists under wine, so
# nemo doesn't start in an invalid dir. Z: maps to the unix root, so probe the
# host path behind the candidate. %USERNAME% under wine defaults to the unix
# login name, so Z:\home\<user> == /home/<user>.
if [[ -z "$uri" ]]; then
	winuser="${USER:-$(id -un)}"
	if [[ -d "/home/${winuser}" ]]; then
		uri="file:///Z:/home/${winuser}"
	else
		uri="file:///Z:/"
	fi
fi

logfile="${DEST}/wine-run.log"
fEcho "Launching nemo-anywhere.exe under wine (DISPLAY=$DISPLAY, start=$uri)"

# Detach: new session, no controlling tty, own fds -> the script returns at once
# and the app outlives it. Log path rides an env var to keep quotes off argv.
# shellcheck disable=SC2016  # $@/$WINE_LOG must expand in the inner sh, not here
WINE_LOG="$logfile" setsid /bin/sh -c \
	'exec "$@" </dev/null >>"$WINE_LOG" 2>&1' \
	sh wine "${DEST}/app/nemo-anywhere.exe" "$uri" &
disown 2>/dev/null || true
