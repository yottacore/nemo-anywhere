#!/usr/bin/env bash

##	- Purpose: Check that gui-smoke.bash finds the main window in a window list
##	  too long for one pipe write, and that it steps over a display another X
##	  server already holds instead of drawing on it.
##	- Runs a copy of gui-smoke.bash in the nemo-winbuild container, with wine and
##	  xwininfo stood in for. Xvfb, xdpyinfo and import are the real ones.
##	- Needs docker and the nemo-winbuild container; exit 77 without them.
##	- Runs in the lint stage.
##	- Syntax: cicd/win/test-gui-smoke.bash
##	- Test ID: rjcbfbx8

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

fEcho(){ echo "[ $* ]"; }

if [[ "${1:-}" != "--inside" ]]; then
	echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
	root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	if ! command -v docker >/dev/null 2>&1 || ! timeout 10 docker inspect nemo-winbuild >/dev/null 2>&1; then
		fEcho "gui-smoke check skipped: no nemo-winbuild container"
		exit 77
	fi
	## Copies, not the container's /src, which may be another clone.
	rc=0
	# shellcheck disable=SC2016  ## expanded by the shell in the container
	tar -C "${root}" -cf - cicd/win/gui-smoke.bash cicd/win/test-gui-smoke.bash cicd/utility/include/xvfb.bash \
		| timeout -k 5 120 docker exec -i nemo-winbuild bash -c \
			'd="$(mktemp -d /tmp/test_gui-smoke.XXXXXX)" && tar -C "$d" -xf - && { rc=0; bash "$d/cicd/win/test-gui-smoke.bash" --inside "$d" || rc=$?; rm -rf -- "$d"; exit "$rc"; }' \
		|| rc=$?
	exit "${rc}"
fi

work="${2:?}"
stubs="${work}/bin"
mkdir -p "${stubs}"

## Records which display it was started on, and which pid held that number then.
cat >"${stubs}/wine" <<'EOF'
#!/usr/bin/env bash
num="${DISPLAY#:}"
echo "${DISPLAY} $(tr -dc '0-9' 2>/dev/null <"/tmp/.X${num}-lock")" >"${STUB_SAW:?}"
exec sleep 60
EOF

## The main window near the top, then far more than a pipe holds.
cat >"${stubs}/xwininfo" <<'EOF'
#!/usr/bin/env bash
echo 'xwininfo: Window id: 0x3ff (the root window) (has no name)'
echo '     0x400001 "Home": ("nemo-anywhere.exe" "nemo-anywhere.exe")  1280x900+0+0  +0+0'
if [[ "${STUB_LONG:-1}" == "1" ]]; then
	yes '        0x400099 (has no name): ()  1x1+0+0  +0+0' | head -n 40000
fi
EOF
chmod +x "${stubs}/wine" "${stubs}/xwininfo"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

lockPid(){ tr -dc '0-9' 2>/dev/null <"/tmp/.X${1}-lock" || true; }

## A stand-in for somebody else's server, on a number proven free first.
foreignPid=""; foreignNum=""
trap '[[ -n $foreignPid ]] && kill "$foreignPid" 2>/dev/null || true' EXIT
for ((num = 150; num < 170; num++)); do
	[[ -e /tmp/.X${num}-lock ]] && continue
	DISPLAY=":${num}" xdpyinfo >/dev/null 2>&1 && continue
	Xvfb ":${num}" -screen 0 640x480x24 -nolisten tcp >/dev/null 2>&1 &
	foreignPid=$!
	for _ in {1..50}; do
		kill -0 "${foreignPid}" 2>/dev/null || break
		if [[ "$(lockPid "${num}")" == "${foreignPid}" ]] && DISPLAY=":${num}" xdpyinfo >/dev/null 2>&1; then foreignNum="${num}"; break; fi
		sleep 0.1
	done
	[[ -n $foreignNum ]] && break
	kill "${foreignPid}" 2>/dev/null || true
	foreignPid=""
done
if [[ -z $foreignNum ]]; then
	fEcho "gui-smoke check skipped: no free display from :150 for the stand-in server"
	exit 77
fi

## Runs one case: <label> <first display> <long list 0|1>
fCase(){
	local label="$1" first="$2" long="$3"
	local saw="${work}/saw-${label}" shot="${work}/shot-${label}.png" out rc=0
	out="$(env PATH="${stubs}:${PATH}" STUB_SAW="${saw}" STUB_LONG="${long}" GUI_SMOKE_DISPLAY=":${first}" \
		bash "${work}/cicd/win/gui-smoke.bash" "${shot}" 1 2>&1)" || rc=$?
	if [[ "${rc}" != "0" || "${out}" != *"Main window present"* ]]; then
		fFail "${label}: exit ${rc}; said: ${out}"
		return 0
	fi
	local disp pid num
	read -r disp pid <"${saw}" || true
	num="${disp#:}"
	if [[ "${disp}" == ":${foreignNum}" || "${pid}" == "${foreignPid}" ]]; then
		fFail "${label}: ran on the other server's display ${disp}"
	elif [[ ! "${num}" =~ ^[0-9]+$ ]] || ((num < first || num >= first + 20)); then
		fFail "${label}: ran on ${disp}, not the first free one from :${first}"
	elif [[ -z "${pid}" ]]; then
		fFail "${label}: no X server held ${disp}"
	fi
	[[ -s "${shot}" ]] || fFail "${label}: no screenshot"
	## Its own server goes when it does; the other one stays.
	if [[ -n "${pid}" ]]; then
		for _ in {1..50}; do kill -0 "${pid}" 2>/dev/null || break; sleep 0.1; done
		kill -0 "${pid}" 2>/dev/null && fFail "${label}: left its Xvfb (pid ${pid}) running"
	fi
	kill -0 "${foreignPid}" 2>/dev/null || fFail "${label}: the other server on :${foreignNum} is gone"
	return 0
}

fCase long-list "$((foreignNum + 1))" 1
fCase taken-display "${foreignNum}" 0

if ((failures)); then
	fEcho "FAILED: gui-smoke check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: gui-smoke check"
