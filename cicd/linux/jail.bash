#!/usr/bin/env bash

##	- Purpose: Run one test in a cell of its own, as meson test's --wrapper. The
##	  cell is a fresh HOME and temp dir under the run's TMPDIR, and it's the only
##	  place the test can write.
##	- On Linux the test runs under bubblewrap: every file system read-only, the
##	  cell a tmpfs of its own, and a private /tmp, /dev/shm and network. The X
##	  server's socket, lock and cookie come in read-only. A Linux box where the
##	  jail can't start fails the test. It never runs unjailed.
##	- Elsewhere (FreeBSD, MSYS2 on Windows) there's no jail, so the cell is only
##	  HOME and the temp dir, on disk. The test library's fence refuses a delete
##	  outside the cell everywhere, Linux included (source/test/test-fence.c).
##	- The cell goes when the test ends. When the test left something in its temp
##	  dir, a folder named after the test stays, with the list in it, for
##	  run-tests.bash to name.
##	- --check starts the jail once and says what's missing if it can't.
##	- Syntax: jail.bash --check | jail.bash <test command...>

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

## 99 is meson's "test could not run", which is what this is.
fDie(){ echo "[ jail: $* ]" >&2; exit 99; }

## What a container needs before bwrap can make namespaces in it. Docker's
## default seccomp and AppArmor profiles both refuse them, even to root.
dockerOpts="--security-opt seccomp=unconfined --security-opt apparmor=unconfined"

## No process list of its own. A new procfs can't be mounted under docker's
## masked /proc, and with the container's in its place dbus-daemon names a
## peer by the container's pid, which the tests' own pids never match.
bwrapBase=(
	--unshare-user --unshare-net --die-with-parent
	--ro-bind / / --dev /dev --tmpfs /dev/shm --ro-bind /proc /proc --tmpfs /tmp
)

## The cell is a tmpfs rather than a folder bound in, so it's a file system of
## its own. While something watches the mount table, as the app does, GLib
## answers "read-only?" once per device. A cell on the root's device then read
## as read-only whenever the root was asked about first, and New folder failed
## on it. The largest cell in the suite peaked near 120 MB.
cellBytes=$(( 4 * 1024 * 1024 * 1024 ))

fCheck(){
	local err
	[[ "${OSTYPE}" == linux* ]] || { echo "[ jail: no jail on ${OSTYPE}; cells and the fence only ]"; return 0; }
	command -v bwrap >/dev/null 2>&1 || fDie "no bwrap here. Install bubblewrap (Dockerfile.dev and Dockerfile have it)"
	if ! err="$(bwrap "${bwrapBase[@]}" -- true 2>&1)"; then
		fDie "bwrap can't start here: ${err%.}. In docker, run the container with ${dockerOpts}"
	fi
	echo "[ jail: bwrap $(bwrap --version | sed 's/^bubblewrap //') starts here ]"
}

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
	--check) fCheck; exit 0 ;;
	"") fDie "no test command" ;;
esac

base="${TMPDIR:-/tmp}"
name="${1##*/}"
name="${name##*\\}"
name="${name%.exe}"
cell="$(mktemp -d "${base}/${name}.XXXXXX")" || fDie "could not make a cell in ${base}"
jailed=0
[[ "${OSTYPE}" == linux* ]] && jailed=1
((jailed)) || mkdir "${cell}/home" "${cell}/tmp"

## Only the cell this run made, still a real folder where it was made. Tests can
## leave read-only folders behind, and chmod -R never follows a link.
fTidy(){
	[[ -d "${cell}" && ! -L "${cell}" && "${cell}" == "${base}/${name}."* ]] || return 0
	if ((jailed)); then
		if [[ -s "${cell}/left" ]]; then
			echo "[ jail: ${name} left files in its temp dir; the list is in ${cell}/left ]" >&2
			return 0
		fi
		rm -f "${cell}/left"
	else
		chmod -R u+rwx "${cell}/home" 2>/dev/null || true
		rm -rf "${cell:?}/home" || true
		if ! rmdir "${cell}/tmp" 2>/dev/null; then
			echo "[ jail: ${name} left files in ${cell}/tmp ]" >&2
			return 0
		fi
	fi
	rmdir "${cell}" 2>/dev/null || true
}
trap fTidy EXIT

scratch="${cell}"
## Elsewhere the XDG dirs follow HOME once unset.
xdgUnset=(-u XDG_CONFIG_HOME -u XDG_CACHE_HOME -u XDG_DATA_HOME -u XDG_STATE_HOME -u XDG_RUNTIME_DIR)
if [[ "${OSTYPE}" == msys* || "${OSTYPE}" == cygwin* ]]; then
	## MSYS2 turns HOME and the temp vars into Windows paths for a native
	## program on its own; the rest it doesn't know are paths. GLib takes its
	## config, data and cache dirs from the shell's real LocalAppData unless
	## the XDG vars say otherwise.
	scratch="$(cygpath -m "${cell}")"
	localDir="${scratch}/home/AppData/Local"
	export TMP="${cell}/tmp" TEMP="${cell}/tmp"
	export APPDATA="${scratch}/home/AppData/Roaming" LOCALAPPDATA="${localDir}"
	export XDG_CONFIG_HOME="${localDir}" XDG_CACHE_HOME="${localDir}" XDG_DATA_HOME="${localDir}" XDG_STATE_HOME="${localDir}"
	xdgUnset=(-u XDG_RUNTIME_DIR)
	mkdir -p "${cell}/home/AppData/Roaming" "${cell}/home/AppData/Local"
fi

set +e
if ((jailed)); then
	args=("${bwrapBase[@]}")
	## bubblewrap before 0.10, jammy's among them, has no --size.
	[[ "$(bwrap --help 2>&1)" == *"--size BYTES"* ]] && args+=(--size "${cellBytes}")
	args+=(--tmpfs "${cell}" --dir "${cell}/home" --dir "${cell}/tmp")
	## The shared server's lock comes in too, so a test that starts its own
	## server picks a number of its own.
	if [[ "${DISPLAY:-}" =~ ^:([0-9]+)(\.[0-9]+)?$ ]]; then
		for f in "/tmp/.X11-unix/X${BASH_REMATCH[1]}" "/tmp/.X${BASH_REMATCH[1]}-lock"; do
			[[ -e "${f}" ]] && args+=(--ro-bind "${f}" "${f}")
		done
	fi
	[[ -n "${XAUTHORITY:-}" && -f "${XAUTHORITY}" ]] && args+=(--ro-bind "${XAUTHORITY}" "${XAUTHORITY}")
	## The cell's tmpfs goes with the jail, so what the test left is listed from
	## inside, onto a descriptor the test itself doesn't get.
	# shellcheck disable=SC2016  ## expanded by the shell in the jail
	bwrap "${args[@]}" \
		--setenv HOME "${cell}/home" --setenv TMPDIR "${cell}/tmp" --setenv NEMO_TEST_SCRATCH "${scratch}" \
		--unsetenv XDG_CONFIG_HOME --unsetenv XDG_CACHE_HOME --unsetenv XDG_DATA_HOME \
		--unsetenv XDG_STATE_HOME --unsetenv XDG_RUNTIME_DIR \
		-- /bin/sh -c '"$@" 9>&-; rc=$?; [ -z "$(ls -A "$TMPDIR")" ] || ls -AlR "$TMPDIR" >&9; exit $rc' sh "$@" \
		9>"${cell}/left"
else
	HOME="${cell}/home" TMPDIR="${cell}/tmp" NEMO_TEST_SCRATCH="${scratch}" env "${xdgUnset[@]}" "$@"
fi
rc=$?
set -e

trap - EXIT
fTidy

## Die the way the test did, so meson still says which signal. The test made
## its own core, if any; this shell doesn't need one beside it.
if (( rc > 128 && rc < 160 )); then
	ulimit -c 0 2>/dev/null || true
	trap - "$(( rc - 128 ))" 2>/dev/null || true
	kill -n "$(( rc - 128 ))" "$$" 2>/dev/null || true
fi
exit "${rc}"


##	History:
##		- 2026-10-19: Created.
