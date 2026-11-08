#!/usr/bin/env bash

##	- Purpose: Check the Linux side of the native Windows gate, with no box.
##	  A copy of lane.bash runs in a scratch repo, with an ssh, scp and VM state
##	  helper on PATH that answer as a Windows box would and log what they're
##	  asked.
##	  - b29w is taken through the lock's wrap, by that name. A bundle of HEAD
##	    goes to a folder of its own under the box's %ProgramData%, which
##	    wintest can reach too, named <YYYYmmDD-HHMMSS>_<NNN>, counting up past
##	    a name that's taken.
##	  - The gate gets -BacklogRunQuick -Yes -NoSync, its log is printed here, and its exit
##	    code is the lane's. The folder is removed after, on a failure too.
##	  - With b29w gone, vm925w is taken through the VM state helper, even
##	    while it's off. With neither there, the lane exits 77 before any lock
##	    is taken or anything is sent.
##	  - A TERM mid-gate removes the folder, which stops the gate there.
##	  - An uncommitted edit is named as left out.
##	  - After a passing gate, the meson tests its Tests step skipped, and only
##	    those, go to the box and run as wintest in its desktop session. Their
##	    exit code is the lane's. With no wintest session there, or a locked
##	    one, it's a warning and the gate's pass stands. A failing gate runs
##	    none. NEMO_WIN_DESKTOP_ELSEWHERE=1 leaves them for the next box, and
##	    a passing gate then exits 75, having written them to the file
##	    NEMO_WIN_DESKTOP_LIST names. Given that file without
##	    NEMO_WIN_DESKTOP_ELSEWHERE, the next box runs the tests named there,
##	    and a missing or empty one fails the lane.
##	- Needs git; exit 77 without it.
##	- Runs in the lint stage.
##	- Syntax: cicd/win/test-lane.bash
##	- Test ID: depg0

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../utility/include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"

if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "Windows lane check skipped: Linux only"; exit 77; fi
if ! command -v git >/dev/null 2>&1; then fEcho "Windows lane check skipped: no git"; exit 77; fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/win-lane-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_CONFIG_COUNT GIT_DIR GIT_WORK_TREE

repo="${scratch}/repo"
log="${scratch}/calls.log"
mkdir -p "${repo}/cicd/win" "${repo}/cicd/utility/include" "${scratch}/bin" "${scratch}/sent"
cp "${root}/cicd/win/lane.bash" "${root}/cicd/win/lane-box.ps1" "${repo}/cicd/win/"
for inc in echo box-wrap jobs; do cp "${root}/cicd/utility/include/${inc}.bash" "${repo}/cicd/utility/include/"; done
cp "${root}/cicd/utility/include/run-temp.ps1" "${repo}/cicd/utility/include/"
git -C "$repo" -c user.name=test -c user.email=test@example.invalid init -q
git -C "$repo" add -A
git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null commit -q -m base
head="$(git -C "$repo" rev-parse HEAD)"

## FAKE_GONE: the hosts that never answer. FAKE_TAKEN: folder names already
## there. FAKE_RC: what run.rc holds, none while the gate runs. FAKE_DRC: the
## same for desktop.rc. FAKE_NOSESSION: wintest has no session. FAKE_LOCKED:
## its session is locked.
cat > "${scratch}/bin/ssh" <<-EOF
	#!/usr/bin/env bash
	while [[ "\${1:-}" == -o ]]; do shift 2; done
	host="\$1"; shift
	if [[ " \${FAKE_GONE:-} " == *" \${host} "* ]]; then echo "ssh: connect to host \${host} port 22: Connection timed out" >&2; exit 255; fi
	cmd="\$*"
	echo "ssh \${host} \${cmd}" >> "${log}"
	case "\$cmd" in
		exit) ;;
		'echo %ProgramData%') printf 'C:\\\\ProgramData\\r\\n' ;;
		mkdir*)
			for t in \${FAKE_TAKEN:-}; do [[ "\$cmd" != *"_\${t}" ]] || exit 1; done ;;
		*' -Start '*) printf 'pid=4242\\r\\n' ;;
		*' -Desktop '*) if [[ -n "\${FAKE_NOSESSION:-}" ]]; then printf 'nosession\\r\\n'; elif [[ -n "\${FAKE_LOCKED:-}" ]]; then printf 'locked\\r\\n'; else printf 'task=nemo-anywhere-x_001\\r\\n'; fi ;;
		'if exist '*'desktop.rc') [[ -z "\${FAKE_DRC:-}" ]] || printf 'rc=%s secs=3\\r\\n' "\${FAKE_DRC}" ;;
		schtasks*) printf 'Status: Running\\r\\n' ;;
		'if exist '*'run.rc') [[ -z "\${FAKE_RC:-}" ]] || printf 'rc=%s secs=7\\r\\n' "\${FAKE_RC}" ;;
		tasklist*) printf 'pwsh.exe   4242 Services   0   90,000 K\\r\\n' ;;
	esac
	exit 0
EOF
## What goes to the box is kept, and the gate's log comes back from it.
cat > "${scratch}/bin/scp" <<-EOF
	#!/usr/bin/env bash
	while [[ "\${1:-}" == -q || "\${1:-}" == -o ]]; do [[ "\$1" == -o ]] && shift; shift; done
	echo "scp \$*" >> "${log}"
	if [[ "\$1" == *:*/gate.log ]]; then
		{
			printf 'SKIP ddzzl a lint script test\\r\\n'
			printf '[ 20261031-120000.00: Step 3 of 4; Starting Tests ]\\r\\n'
			printf 'PASS ddzzz a test on the box  0.10s\\r\\n'
			printf 'SKIP ddzzw a windowed test  0.02s   exit status 77\\r\\n'
			printf 'SKIP ddzzi test-install-holders.ps1\\r\\n'
			printf '[ 20261031-120001.00: Step 4 of 4; Starting Smoke checks ]\\r\\n'
			printf 'SKIP ddzzs a smoke  0.02s\\r\\n'
			printf 'OK: 1 tests\\r\\n'
		} > "\$2"; exit 0
	fi
	if [[ "\$1" == *:*/desktop.log ]]; then
		printf 'PASS ddzzw a windowed test  1.20s\\r\\n' > "\$2"; exit 0
	fi
	n=\$((\$# - 1))
	cp "\${@:1:n}" "${scratch}/sent/"
EOF
## FAKE_VMS: the hosts that are VMs here, all of them off.
cat > "${scratch}/x_vm-state.bash" <<-EOF
	if [[ "\${1:-}" == status ]]; then
		state=-; [[ " \${FAKE_VMS:-} " == *" \$2 "* ]] && state=off
		printf 'host guest state note\n%s %s %s\n' "\$2" "\$2" "\$state"; exit 0
	fi
	## Nobody holds anything.
	[[ "\${1:-}" != check ]] || exit 1
	echo "lock \$*" >> "${log}"
	while ((\$#)) && [[ "\$1" != -- ]]; do shift; done
	shift
	exec "\$@"
EOF
cp "${scratch}/x_vm-state.bash" "${scratch}/x_windows-host-lock.bash"
chmod +x "${scratch}/bin/"*

laneRc=0
## $1 the lock tool, then env settings, then -- and the lane's own args.
fLane(){
	local tool="$1" envs=()
	shift
	while (($#)) && [[ "$1" != -- ]]; do envs+=("$1"); shift; done
	shift
	rm -f "$log" "${scratch}/sent/"*; touch "$log"
	laneRc=0
	## A home of its own, so the real lock tools are never found.
	(cd "$repo" && env -u WINDOWS_HOST_LOCK "${envs[@]}" HOME="${scratch}/home" PATH="${scratch}/bin:${PATH}" BOX_WRAP_TOOL="$tool" BOX_READY_POLL=0 NEMO_WIN_POLL=0 \
		bash cicd/win/lane.bash "$@") > "${scratch}/out.log" 2>&1 || laneRc=$?
}
dirRe='C:\\ProgramData\\nemo-anywhere\\[0-9]{8}-[0-9]{6}_'

fLane "${scratch}/x_vm-state.bash" FAKE_VMS=vm925w FAKE_RC=0 FAKE_DRC=0 -- b29w vm925w
if ((laneRc == 0)); then
	grep -q '^lock wrap b29w ' "$log" || fFail "b29w was not taken through the wrap: $(cat "$log")"
	! grep -q 'b29w-wif\|vm925w' "$log" || fFail "another box was touched while b29w was there"
	grep -qE "^ssh b29w mkdir ${dirRe}001\$" "$log" || fFail "no work folder _001 under the box's ProgramData"
	# shellcheck disable=SC1003  ## a Windows folder, trailing backslash and all
	grep -qF 'work folder b29w:C:\ProgramData\nemo-anywhere\' "${scratch}/out.log" || fFail "the work folder's path was not printed as it is"
	if [[ -f "${scratch}/sent/round.bundle" ]]; then
		git -C "$repo" bundle verify -q "${scratch}/sent/round.bundle" > /dev/null 2>&1 || fFail "what was sent is not a bundle of this repo"
		grep -q "^${head} HEAD\$" <<< "$(git bundle list-heads "${scratch}/sent/round.bundle")" || fFail "the bundle is not of HEAD"
	else
		fFail "no bundle was sent"
	fi
	cmp -s "${scratch}/sent/lane-box.ps1" "${repo}/cicd/win/lane-box.ps1" || fFail "lane-box.ps1 was not sent"
	cmp -s "${scratch}/sent/run-temp.ps1" "${repo}/cicd/utility/include/run-temp.ps1" || fFail "run-temp.ps1 was not sent"
	grep -qE "^ssh b29w pwsh .*lane-box\\.ps1 -Dir ${dirRe}001 -Start -GateArgs BacklogRunQuick,Yes,NoSync\$" "$log" || fFail "the gate was not started with -BacklogRunQuick -Yes -NoSync"
	grep -q '^PASS ddzzz a test on the box  0.10s$' "${scratch}/out.log" || fFail "the gate's log was not printed"
	grep -qE "^ssh b29w pwsh .*lane-box\\.ps1 -Dir ${dirRe}001 -Remove\$" "$log" || fFail "the work folder was not removed"
	grep -q 'Windows 3 of 4; PASS Gate ]' "${scratch}/out.log" || fFail "no PASS line for the gate"
	[[ "$(cat "${scratch}/sent/desktop.txt" 2> /dev/null)" == 'ddzzw a windowed test' ]] || fFail "the desktop list is not the one test the Tests step skipped: $(cat "${scratch}/sent/desktop.txt" 2> /dev/null)"
	grep -qE "^ssh b29w pwsh .*lane-box\\.ps1 -Dir ${dirRe}001 -Desktop -TestUser wintest\$" "$log" || fFail "the desktop tests were not started as wintest"
	grep -q '^PASS ddzzw a windowed test  1.20s$' "${scratch}/out.log" || fFail "the desktop log was not printed"
	grep -q 'Windows 4 of 4; PASS Desktop tests ]' "${scratch}/out.log" || fFail "no PASS line for the desktop tests"
	grep -qE "^ssh b29w pwsh .*-Desktop .*" <<< "$(sed -n '/-Remove$/,$p' "$log")" && fFail "the desktop tests ran after the folder was removed"
	! grep -q 'uncommitted' "${scratch}/out.log" || fFail "a clean tree was said to have uncommitted edits"
	fEcho "OK: b29w runs the gate in its own folder, removed after"
else
	fFail "the lane failed with b29w there (exit ${laneRc}): $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=1 FAKE_TAKEN="001 002" -- b29w
if ((laneRc == 1)); then
	grep -qE "^ssh b29w mkdir ${dirRe}003\$" "$log" || fFail "a taken folder name was not counted past"
	grep -qE "^ssh b29w pwsh .*-Dir ${dirRe}003 -Remove\$" "$log" || fFail "the work folder was not removed after a failure"
	grep -q 'Windows 3 of 4; FAIL Gate ]' "${scratch}/out.log" || fFail "no FAIL line for the gate"
	! grep -q ' -Desktop ' "$log" || fFail "the desktop tests ran after a failing gate"
	fEcho "OK: a failing gate fails the lane, and its folder still goes"
else
	fFail "a failing gate gave exit ${laneRc}, not 1: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_VMS=vm925w FAKE_GONE=b29w FAKE_RC=0 FAKE_DRC=0 -- b29w vm925w
if ((laneRc == 0)); then
	grep -q '^lock wrap vm925w ' "$log" || fFail "vm925w, off, was not taken through the VM state helper: $(cat "$log")"
	! grep -q '^lock wrap b29w' "$log" || fFail "b29w was taken while gone"
	grep -q '^ssh vm925w pwsh .* -Start ' "$log" || fFail "the gate did not start on vm925w"
	grep -q '^ssh vm925w pwsh .* -Desktop -TestUser wintest$' "$log" || fFail "the desktop tests did not run as wintest on vm925w"
	fEcho "OK: with b29w gone, vm925w is started through the VM state helper"
else
	fFail "the lane failed with vm925w off (exit ${laneRc}): $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_windows-host-lock.bash" FAKE_GONE="b29w vm925w" FAKE_RC=0 --
if ((laneRc == 77)); then
	! grep -q '^lock ' "$log" || fFail "a box was taken with none there"
	! grep -qv ' exit$' "$log" || fFail "more than the probe went to a box that isn't there: $(cat "$log")"
	grep -q 'SKIP: no Windows box there (b29w vm925w)' "${scratch}/out.log" || fFail "no line saying the gate was skipped"
	fEcho "OK: no box there exits 77, before the lock"
else
	fFail "no box there gave exit ${laneRc}, not 77: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_DRC=1 -- b29w
if ((laneRc == 1)); then
	grep -q 'Windows 4 of 4; FAIL Desktop tests ]' "${scratch}/out.log" || fFail "no FAIL line for the desktop tests"
	grep -qE "^ssh b29w pwsh .*-Dir ${dirRe}001 -Remove\$" "$log" || fFail "the work folder was not removed after the desktop tests failed"
	fEcho "OK: a failing desktop test fails the lane"
else
	fFail "a failing desktop test gave exit ${laneRc}, not 1: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_NOSESSION=1 -- b29w
if ((laneRc == 0)); then
	grep -q 'WARNING: wintest has no session on b29w' "${scratch}/out.log" || fFail "no warning for wintest with no session"
	! grep -q 'if exist .*desktop.rc' "$log" || fFail "desktop.rc was waited for with no session"
	fEcho "OK: with no wintest session, a warning and the gate's pass"
else
	fFail "no wintest session gave exit ${laneRc}, not 0: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_LOCKED=1 -- b29w
if ((laneRc == 0)); then
	grep -q "WARNING: wintest's session on b29w is locked" "${scratch}/out.log" || fFail "no warning for a locked wintest session"
	! grep -q 'if exist .*desktop.rc' "$log" || fFail "desktop.rc was waited for in a locked session"
	fEcho "OK: with wintest's session locked, a warning and the gate's pass"
else
	fFail "a locked wintest session gave exit ${laneRc}, not 0: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_DRC=0 NEMO_WIN_DESKTOP_ELSEWHERE=1 -- b29w
if ((laneRc == 75)); then
	! grep -q ' -Desktop ' "$log" || fFail "NEMO_WIN_DESKTOP_ELSEWHERE=1 still ran the desktop tests on b29w"
	grep -q '1 skipped tests are left for the next box' "${scratch}/out.log" || fFail "no line saying the windowed tests go to the next box"
	grep -q 'Windows 4 of 4; SKIP Desktop tests ]' "${scratch}/out.log" || fFail "no SKIP line for the desktop tests"
	! grep -q 'FAIL Desktop tests' "${scratch}/out.log" || fFail "the desktop tests were called failed"
	grep -qE "^ssh b29w pwsh .*-Dir ${dirRe}001 -Remove\$" "$log" || fFail "the work folder was not removed"
	fEcho "OK: NEMO_WIN_DESKTOP_ELSEWHERE=1 leaves the windowed tests to the next box, exit 75"
else
	fFail "NEMO_WIN_DESKTOP_ELSEWHERE=1 gave exit ${laneRc}, not 75: $(tail -3 "${scratch}/out.log")"
fi

handList="${scratch}/hand-on.txt"
fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 NEMO_WIN_DESKTOP_ELSEWHERE=1 NEMO_WIN_DESKTOP_LIST="$handList" -- b29w
if ((laneRc == 75)); then
	[[ "$(cat "$handList" 2> /dev/null)" == 'ddzzw a windowed test' ]] || fFail "the tests left for the next box were not written to NEMO_WIN_DESKTOP_LIST: $(cat "$handList" 2> /dev/null)"
	! grep -q ' -Desktop ' "$log" || fFail "NEMO_WIN_DESKTOP_LIST with NEMO_WIN_DESKTOP_ELSEWHERE=1 ran the desktop tests"
	fEcho "OK: the tests left for the next box go to the NEMO_WIN_DESKTOP_LIST file"
else
	fFail "NEMO_WIN_DESKTOP_ELSEWHERE=1 with a list file gave exit ${laneRc}, not 75: $(tail -3 "${scratch}/out.log")"
fi

printf 'ddzzq one handed on\n\nddzzr another\n' > "$handList"
fLane "${scratch}/x_vm-state.bash" FAKE_VMS=vm925w FAKE_RC=0 FAKE_DRC=0 NEMO_WIN_DESKTOP_LIST="$handList" NEMO_WIN_GATE_ARGS="-BacklogRunQuick -Yes -NoSync -NoFmt -NoTests" -- vm925w
if ((laneRc == 0)); then
	grep -qE "^ssh vm925w pwsh .*lane-box\.ps1 -Dir ${dirRe}001 -Start -GateArgs BacklogRunQuick,Yes,NoSync,NoFmt,NoTests\$" "$log" || fFail "the next box's gate did not get -NoTests: $(grep -- '-Start' "$log")"
	[[ "$(cat "${scratch}/sent/desktop.txt" 2> /dev/null)" == $'ddzzq one handed on\nddzzr another' ]] || fFail "the desktop list is not the one handed on: $(cat "${scratch}/sent/desktop.txt" 2> /dev/null)"
	grep -q '^ssh vm925w pwsh .* -Desktop -TestUser wintest$' "$log" || fFail "the handed-on tests did not run as wintest"
	grep -q 'Windows 4 of 4; PASS Desktop tests ]' "${scratch}/out.log" || fFail "no PASS line for the handed-on tests"
	fEcho "OK: given NEMO_WIN_DESKTOP_LIST, the next box runs the tests named there"
else
	fFail "a lane given NEMO_WIN_DESKTOP_LIST gave exit ${laneRc}: $(tail -3 "${scratch}/out.log")"
fi

: > "$handList"
fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_DRC=0 NEMO_WIN_DESKTOP_LIST="$handList" -- b29w
if ((laneRc == 1)); then
	! grep -q ' -Desktop ' "$log" || fFail "an empty NEMO_WIN_DESKTOP_LIST still ran the desktop tests"
	grep -qE "^ssh b29w pwsh .*-Dir ${dirRe}001 -Remove\$" "$log" || fFail "the work folder was not removed after an empty list"
	fEcho "OK: an empty NEMO_WIN_DESKTOP_LIST fails the lane"
else
	fFail "an empty NEMO_WIN_DESKTOP_LIST gave exit ${laneRc}, not 1: $(tail -3 "${scratch}/out.log")"
fi

fLane "${scratch}/x_vm-state.bash" FAKE_RC=1 NEMO_WIN_DESKTOP_ELSEWHERE=1 -- b29w
((laneRc == 1)) || fFail "a failing gate with NEMO_WIN_DESKTOP_ELSEWHERE=1 gave exit ${laneRc}, not 1"

fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 NEMO_WIN_DESKTOP=0 -- b29w
if ((laneRc == 0)); then
	! grep -q ' -Desktop ' "$log" || fFail "NEMO_WIN_DESKTOP=0 still ran the desktop tests"
	grep -q 'Windows 3 of 3; PASS Gate ]' "${scratch}/out.log" || fFail "NEMO_WIN_DESKTOP=0 still planned 4 steps"
	fEcho "OK: NEMO_WIN_DESKTOP=0 leaves the desktop tests out"
else
	fFail "NEMO_WIN_DESKTOP=0 gave exit ${laneRc}: $(tail -3 "${scratch}/out.log")"
fi

## A gate that never ends, stopped from here, the way cicd.bash stops it.
if command -v setsid > /dev/null 2>&1; then
	rm -f "$log"; touch "$log"
	(cd "$repo" && exec env -u WINDOWS_HOST_LOCK HOME="${scratch}/home" PATH="${scratch}/bin:${PATH}" BOX_WRAP_TOOL="${scratch}/x_vm-state.bash" BOX_READY_POLL=0 NEMO_WIN_POLL=1 \
		setsid bash cicd/win/lane.bash b29w) > "${scratch}/out.log" 2>&1 &
	lanePid=$!
	for _ in $(seq 1 100); do grep -q ' -Start ' "$log" && break; sleep 0.1; done
	sleep 0.5
	kill -TERM -- "-${lanePid}" 2> /dev/null || true
	laneRc=0; wait "$lanePid" || laneRc=$?
	((laneRc != 0)) || fFail "a TERM mid-gate gave exit 0"
	grep -qE "^ssh b29w pwsh .*-Dir ${dirRe}001 -Remove\$" "$log" || fFail "a TERM mid-gate left the work folder: $(tail -3 "${scratch}/out.log")"
	fEcho "OK: a TERM mid-gate removes the work folder"
fi

echo edit >> "${repo}/cicd/win/lane-box.ps1"
fLane "${scratch}/x_vm-state.bash" FAKE_RC=0 FAKE_DRC=0 -- b29w
grep -q 'WARNING: uncommitted edits here are not in the native Windows gate' "${scratch}/out.log" || fFail "an uncommitted edit was not named"
cmp -s "${scratch}/sent/lane-box.ps1" "${repo}/cicd/win/lane-box.ps1" || fFail "the lane-box.ps1 sent is not this tree's"

if ((failures)); then fEcho "${failures} Windows lane check(s) failed"; exit 1; fi
fEcho "Windows lane checks passed"
