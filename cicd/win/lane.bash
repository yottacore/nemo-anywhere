#!/usr/bin/env bash

##	- Purpose: The native Windows gate, run from the Linux box: cicd-win.ps1
##	  -Gate on a Windows box, from a git bundle of the commit this run is on.
##	  Only what is committed goes; an uncommitted edit here is named and left
##	  out.
##	- The boxes are tried in the order given, b29w then vm925w by default. The
##	  first one there is taken for the whole gate through the VM state helper
##	  or the host lock, whichever is on this box (include/box-wrap.bash), so a
##	  VM that is off or paused is started and put back after. None there exits
##	  77, a skip, before any lock is taken, and so does a box with no host
##	  lock here.
##	- The gate runs in its own folder,
##	  %ProgramData%\nemo-anywhere\<YYYYmmDD-HHMMSS>_<NNN>, started there outside
##	  the ssh session (win/lane-box.ps1) and polled, with its log printed here
##	  as it goes. At the end, a failure and a TERM included, the gate is
##	  stopped and the folder removed, along with the project's stale ones
##	  there. Not under the ssh login's %TEMP%, since the test user's desktop
##	  tests run in the same folder, and PowerShell as that user reads every
##	  folder above a path it works in, which another user's profile refuses.
##	- The ssh login is in session 0, with no desktop, so the tests that put a
##	  window on screen skip there. After a passing gate, the meson tests it
##	  skipped run again against its build, in the test user's desktop
##	  session on the same box, through a schtasks /it task. A test user with
##	  no session there is a warning, and the gate's pass stands.
##	- NEMO_WIN_READY_WAIT is the seconds to wait for the login after a boot
##	  (default 600), NEMO_WIN_POLL the seconds between looks at the log
##	  (default 30), NEMO_WIN_GATE_ARGS what cicd-win.ps1 gets (default
##	  "-Gate -Yes -NoSync"), NEMO_WIN_TEST_USER the user whose desktop the
##	  skipped tests run in (default wintest), NEMO_WIN_DESKTOP=0 leaves that
##	  part out.
##	- Exit: the gate's own, 77 with no box there, 1 for a lane failure.
##	- Syntax: win/lane.bash [<box>...]

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly SLUG="nemo-anywhere"
readyWaitSec="${NEMO_WIN_READY_WAIT:-600}"
pollSec="${NEMO_WIN_POLL:-30}"
gateArgs="${NEMO_WIN_GATE_ARGS:--Gate -Yes -NoSync}"
testUser="${NEMO_WIN_TEST_USER:-wintest}"
desktopOn="${NEMO_WIN_DESKTOP:-1}"
## A Windows build plus the suite runs well past the lock's default lease, and
## the wait is for whoever holds the box to finish.
readonly lockWaitSec=1800

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/box-wrap.bash
source "${ROOT}/cicd/utility/include/box-wrap.bash"
lockTool="$(fBoxWrap_Tool)"

## 8s was too short for a VM that had just booted on a busy host.
sshOpts=(-o ConnectTimeout=30 -o BatchMode=yes -o ServerAliveInterval=30 -o LogLevel=ERROR)

## fEcho_Clean's echo -e would read a Windows path's \U and \c as escapes.
fSay(){ printf '%s\n' "$*"; }
fDieW(){ { printf '[ FAILED: %s ]\n' "$*"; } >&2; exit 1; }

## Set once the box has a folder, for the EXIT trap.
box=""; dirW=""; dirS=""; scratch=""; sent=0

## Every command here is put together on this side, for cmd.exe on the box.
# shellcheck disable=SC2029
fOnBox(){ ssh "${sshOpts[@]}" "$box" "$@" < /dev/null; }
fBoxPs(){ fOnBox "pwsh -NoProfile -ExecutionPolicy Bypass -File ${dirW}\\lane-box.ps1 -Dir ${dirW} $*" | tr -d '\r'; }

fCleanUp(){
	local rc=$?
	trap - EXIT INT TERM HUP
	## Before lane-box.ps1 is there, the folder is still empty.
	if ((sent)); then
		fBoxPs -Remove || fSay "could not remove ${box}:${dirW}"
	elif [[ -n "$dirW" ]]; then
		fOnBox "rmdir ${dirW}" || fSay "could not remove ${box}:${dirW}"
	fi
	if [[ -n "$scratch" ]]; then rm -f -- "${scratch}/round.bundle" "${scratch}/gate.log" "${scratch}/desktop.log" "${scratch}/desktop.txt"; rmdir -- "$scratch"; fi
	fLaneStep_OnExit "$rc"
	exit "$rc"
}

## The folder name is made with a plain mkdir, counting up when it's taken.
fMakeBoxDir(){
	local rootW stamp n
	rootW="$(fOnBox 'echo %ProgramData%' | tr -d '\r' | head -n 1)"
	[[ "$rootW" =~ ^[A-Za-z]:\\ && "$rootW" != *%* ]] || fDieW "${box} gave an odd ProgramData folder: ${rootW}"
	fOnBox "if not exist ${rootW}\\${SLUG} mkdir ${rootW}\\${SLUG}" || fDieW "could not make ${rootW}\\${SLUG} on ${box}"
	stamp="$(date +%Y%m%d-%H%M%S)"
	for n in 001 002 003 004 005 006 007 008 009; do
		if fOnBox "mkdir ${rootW}\\${SLUG}\\${stamp}_${n}" 2> /dev/null; then
			dirW="${rootW}\\${SLUG}\\${stamp}_${n}"
			dirS="${dirW//\\//}"
			fSay "work folder ${box}:${dirW}"
			return 0
		fi
	done
	fDieW "no free work folder name under ${rootW}\\${SLUG} on ${box}"
}

## lane-box.ps1 takes "-Gate -Yes" as Gate,Yes.
fGateArgList(){
	local a out=""
	for a in $gateArgs; do
		[[ "$a" =~ ^-[A-Za-z]+$ ]] || fDie "NEMO_WIN_GATE_ARGS takes only switches, such as -Gate: ${a}"
		out+="${out:+,}${a#-}"
	done
	printf '%s' "$out"
}

## Prints the lines of the box's log $1 (default gate.log) past the first
## logLines, and moves logLines on.
logLines=0
fShowLog(){
	local name="${1:-gate.log}" log n
	log="${scratch}/${name}"
	scp -q "${sshOpts[@]}" "${box}:${dirS}/${name}" "$log" 2> /dev/null < /dev/null || return 0
	n="$(wc -l < "$log")"
	((n > logLines)) || return 0
	tail -n "+$((logLines + 1))" "$log" | head -n "$((n - logLines))" | tr -d '\r'
	logLines="$n"
}

## The meson tests the gate's Tests step skipped, by meson's name, one a line.
## A meson line has its time after a double space; a script test's has none.
fSkippedTests(){
	tr -d '\r' < "${scratch}/gate.log" | awk '
		/; Starting Tests \]$/ { on = 1; next }
		/; (PASS|FAIL) Tests \]$/ || /; Starting / { on = 0 }
		on && /^SKIP [0-9a-z]+ / && match($0, /  +[0-9.]+s( |$)/) { print substr($0, 6, RSTART - 6) }'
}

## Waits for the box's file $1 and sets waitRc to what it holds; fails when
## task $2 stopped without writing it.
waitRc=""
fWaitRc(){
	local rcFile="$1" task="$2"
	waitRc=""
	while :; do
		sleep "$pollSec"
		fShowLog desktop.log
		waitRc="$(fOnBox "if exist ${dirW}\\${rcFile} type ${dirW}\\${rcFile}" 2> /dev/null | tr -d '\r' || true)"
		[[ -z "$waitRc" ]] || break
		if ! grep -q 'Running' <<< "$(fOnBox "schtasks /query /tn ${task} /fo list" 2> /dev/null || true)"; then
			waitRc="$(fOnBox "if exist ${dirW}\\${rcFile} type ${dirW}\\${rcFile}" 2> /dev/null | tr -d '\r' || true)"
			[[ -n "$waitRc" ]] || fDie "the task ${task} on ${box} ended with no exit code"
			break
		fi
	done
	fShowLog desktop.log
}

## The tests the gate skipped, again in the test user's desktop session.
fDesktop(){
	local names out task rc code
	fLaneStep "Desktop tests"
	names="$(fSkippedTests)"
	if [[ -z "$names" ]]; then fEcho_Clean "the gate skipped no tests"; return 0; fi
	printf '%s\n' "$names" > "${scratch}/desktop.txt"
	fEcho_Clean "$(wc -l < "${scratch}/desktop.txt") skipped tests, again as ${testUser} in a desktop session on ${box}"
	scp -q "${sshOpts[@]}" "${scratch}/desktop.txt" "${box}:${dirS}/" < /dev/null || fDie "could not send the test list to ${box}"
	out="$(fBoxPs -Desktop -TestUser "$testUser")"
	if [[ "$out" == *nosession* ]]; then
		fEcho "WARNING: ${testUser} has no session on ${box}, so its windowed tests did not run"
		return 0
	fi
	task="$(sed -n 's/^task=\([A-Za-z0-9_-]*\)$/\1/p' <<< "$out" | head -n 1)"
	[[ -n "$task" ]] || fDie "the desktop tests did not start on ${box}: ${out}"
	logLines=0
	fWaitRc desktop.rc "$task"
	rc="$waitRc"
	code="$(sed -n 's/^rc=\(-\{0,1\}[0-9][0-9]*\).*/\1/p' <<< "$rc" | head -n 1)"
	[[ -n "$code" ]] || fDie "the desktop tests on ${box} left an unreadable exit code: ${rc}"
	fEcho_Clean "the desktop tests on ${box} exited ${code} (${rc#* })"
	((code != 77)) || code=1
	return "$code"
}

fGate(){
	local sha rc='' pidW='' code
	box="$1"
	trap fCleanUp EXIT
	trap 'exit 143' TERM; trap 'exit 130' INT; trap 'exit 129' HUP
	fEcho_Clean ""
	fEcho "Native Windows gate on ${box}"
	if [[ "$desktopOn" == 0 ]]; then fLaneStep_Plan Windows "Box ready" "Send" "Gate"
	else fLaneStep_Plan Windows "Box ready" "Send" "Gate" "Desktop tests"; fi

	fLaneStep "Box ready"
	fBoxWrap_WaitReady "$box" "$readyWaitSec" exit "${sshOpts[@]}" || fDie "${box} did not answer in ${readyWaitSec}s"

	fLaneStep "Send"
	fMakeBoxDir
	scratch="$(mktemp -d)"
	sha="$(git -C "$ROOT" rev-parse HEAD)"
	git -C "$ROOT" bundle create -q "${scratch}/round.bundle" HEAD
	[[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]] || fEcho "WARNING: uncommitted edits here are not in the native Windows gate, which runs ${sha:0:8}"
	sent=1
	scp -q "${sshOpts[@]}" "${scratch}/round.bundle" "${ROOT}/cicd/win/lane-box.ps1" "${box}:${dirS}/" < /dev/null || fDie "could not send the bundle to ${box}"
	pidW="$(fBoxPs -Start -GateArgs "$(fGateArgList)" | sed -n 's/^pid=\([0-9][0-9]*\)$/\1/p')"
	[[ -n "$pidW" ]] || fDie "the gate did not start on ${box}"
	fEcho_Clean "cicd-win.ps1 ${gateArgs} at ${sha:0:8}, pid ${pidW} on ${box}"

	fLaneStep "Gate"
	while :; do
		sleep "$pollSec"
		fShowLog
		rc="$(fOnBox "if exist ${dirW}\\run.rc type ${dirW}\\run.rc" 2> /dev/null | tr -d '\r' || true)"
		[[ -z "$rc" ]] || break
		## Gone with no rc: it died.
		if ! grep -qw "$pidW" <<< "$(fOnBox "tasklist /fi \"PID eq ${pidW}\" /nh" 2> /dev/null || true)"; then
			rc="$(fOnBox "if exist ${dirW}\\run.rc type ${dirW}\\run.rc" 2> /dev/null | tr -d '\r' || true)"
			[[ -n "$rc" ]] || fDie "the gate on ${box} ended with no exit code"
			break
		fi
	done
	fShowLog
	code="$(sed -n 's/^rc=\(-\{0,1\}[0-9][0-9]*\).*/\1/p' <<< "$rc" | head -n 1)"
	[[ -n "$code" ]] || fDie "the gate on ${box} left an unreadable exit code: ${rc}"
	fEcho_Clean "the gate on ${box} exited ${code} (${rc#* })"
	## 77 is this lane's skip, so a gate that says so is a failure here.
	((code != 77)) || code=1
	((code == 0)) || return "$code"
	[[ "$desktopOn" == 0 ]] || fDesktop
}

fMain(){
	local name boxes=("$@")
	case "${1:-}" in
		-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; return 0 ;;
		--locked) fGate "$2"; return ;;
	esac
	((${#boxes[@]})) || boxes=(b29w vm925w)
	if [[ ! -f "$lockTool" ]]; then
		fEcho "SKIP: no host lock here (${lockTool})"
		exit 77
	fi
	for name in "${boxes[@]}"; do
		if fBoxWrap_IsThere "$name" "$name" "$lockTool" "${sshOpts[@]}"; then
			## exec, so a TERM to this pid reaches the wrap, which waits for the lane's cleanup.
			exec bash "$lockTool" wrap "$name" --wait "$lockWaitSec" -- bash "${BASH_SOURCE[0]}" --locked "$name"
		fi
		fEcho_Clean "${name} isn't there"
	done
	fEcho "SKIP: no Windows box there (${boxes[*]})"
	exit 77
}

fMain "${@}"


##	History:
##		- 2026-11-02: The work folder is under %ProgramData%, not the ssh
##		  login's %TEMP%, so the test user's desktop tests can work in it.
##		- 2026-10-31: The tests the gate skipped run again in the test user's
##		  desktop session.
##		- 2026-10-31: Created.
