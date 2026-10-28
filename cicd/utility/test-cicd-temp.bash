#!/usr/bin/env bash

##	- Purpose: Check the run folder. A cicd.bash run works in
##	  ${HOST_TEMP}/<project>/<stamp>_<NNN>/ with TMPDIR, TEMP and TMP pointed
##	  at it and its pid in run.pid, and removes it at the end, on a failed step
##	  and on SIGINT or SIGTERM too. --keep-temp keeps it and prints its path.
##	  A run removes the project's run folders older than 2 days whose run.pid
##	  names no live process, and leaves the rest. fSafeRm refuses what it
##	  should. A copy of cicd.bash runs in a scratch dir with a stand-in step.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-temp.bash
##	- Test ID: dee6e

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## The check's own scratch is a run folder too, under the caller's TMPDIR.
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
HOST_TEMP="${TMPDIR:-/tmp}"
fRunTemp_Make cicd-temp-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: could not make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/cicd-temp-check" 2> /dev/null || true' EXIT
lastDir=""; lastOut=""

## MSYS2's ln -s copies unless told otherwise, so the link cases need real links.
haveLinks=0
ln -s "${scratch}" "${scratch}/probe-link" 2> /dev/null && [[ -L "${scratch}/probe-link" ]] && haveLinks=1
((haveLinks)) || fEcho "no symlinks here, the link cases are skipped"

repo="${scratch}/repo"
hostTemp="${scratch}/host-temp"
seen="${scratch}/seen"
mkdir -p "${repo}/cicd/utility/include" "${hostTemp}"
cp "${root}/cicd/cicd.bash" "${repo}/cicd/"
for inc in gfs-rotate source-date run-temp docker-sweep jobs; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${repo}/cicd/utility/include/"
done
printf "project('app', 'c', version: '1.2.3')\n" > "${repo}/meson.build"
## The step says what it saw, leaves a file in the run folder, then does what
## STEP_DO asks.
cat > "${repo}/step.bash" <<-'EOF'
	printf '%s\n' "$TMPDIR" "$TEMP" "$TMP" "$PPID" "$(cat "$TMPDIR/run.pid")" > "$SEEN"
	mkdir -p "$TMPDIR/left/deeper" && touch "$TMPDIR/left/deeper/file"
	case "${STEP_DO:-}" in
		fail) exit 1 ;;
		int)  kill -INT "$PPID" ;;
		term) kill -TERM "$PPID" ;;
	esac
	exit 0
EOF
cat > "${repo}/cicd/config.bash" <<-EOF
	APP_NAME="App"
	EXE_NAME="app"
	CICD_CONTAINERS=()
	BUILD_CROSS=0
	TEST_CMD=(bash "${repo}/step.bash")
EOF

## $1 what the step does, $2 the exit wanted, rest the options after the mode.
fRun(){
	local what="$1" want="$2" rc=0 out dir
	local -a got
	shift 2
	rm -f "$seen"
	out="$(cd "$repo" && HOST_TEMP="$hostTemp" SEEN="$seen" STEP_DO="$what" CICD_CPU_CAPPED=1 \
		bash cicd/cicd.bash --backlog-run-quick tests "$@" 2>&1 </dev/null)" || rc=$?
	lastOut="$out"
	[[ "$rc" == "$want" ]] || fFail "[${what:-ok} $*]: exit ${rc}, not ${want}: $(tail -3 <<< "$out")"
	if [[ ! -s "$seen" ]]; then
		fFail "[${what:-ok} $*]: the step did not run"
		return 0
	fi
	mapfile -t got < "$seen"
	dir="${got[0]}"
	lastDir="$dir"
	[[ "$dir" =~ ^${hostTemp}/app/[0-9]{8}-[0-9]{6}_[0-9]{3}$ ]] || fFail "[${what:-ok} $*]: TMPDIR is ${dir}"
	[[ "${got[1]}" == "$dir" && "${got[2]}" == "$dir" ]] || fFail "[${what:-ok} $*]: TEMP ${got[1]} and TMP ${got[2]}, not ${dir}"
	[[ "${got[4]}" == "${got[3]}" ]] || fFail "[${what:-ok} $*]: run.pid holds ${got[4]}, the run is ${got[3]}"
}

fRun ""   0
[[ ! -e "$lastDir" ]] || fFail "[ok]: the run folder is still there"
fRun fail 1
[[ ! -e "$lastDir" ]] || fFail "[fail]: the run folder is still there"
fRun int  130
[[ ! -e "$lastDir" ]] || fFail "[int]: the run folder is still there"
fRun term 143
[[ ! -e "$lastDir" ]] || fFail "[term]: the run folder is still there"
fRun ""   0 --keep-temp
if [[ -f "${lastDir}/left/deeper/file" ]]; then
	[[ "$lastOut" == *"[ kept the run folder: ${lastDir} ]"* ]] || fFail "[--keep-temp]: does not say where the run folder is"
	HOST_TEMP="$hostTemp" projectName=app fSafeRm "$lastDir" > /dev/null
else
	fFail "[--keep-temp]: the run folder is gone"
fi
[[ -z "$(ls -A "${hostTemp}/app")" ]] || fFail "runs left these: $(ls -A "${hostTemp}/app")"

## Stale folders: only an old one whose pid is gone goes.
app="${hostTemp}/app"
old="20200101-000000"
recent="$(date -d '1 hour ago' +%Y%m%d-%H%M%S 2> /dev/null || date -v-1H +%Y%m%d-%H%M%S)"
mkdir -p "${app}/${old}_001/sub" "${app}/${old}_002" "${app}/${old}_003" "${app}/${recent}_001" "${app}/${old}_005" "${scratch}/outside"
echo 999999999 > "${app}/${old}_001/run.pid"; touch "${app}/${old}_001/sub/file"
echo "$$"      > "${app}/${old}_002/run.pid"
echo 999999999 > "${app}/${recent}_001/run.pid"
echo 999999999 > "${scratch}/outside/run.pid"
((! haveLinks)) || ln -s "${scratch}/outside" "${app}/${old}_004"
# shellcheck disable=SC2016  ## the substitution must stay unexpanded
printf 'x[$(touch %s/pwned)]\n' "$scratch" > "${app}/${old}_005/run.pid"
fRun "" 0
[[ ! -e "${app}/${old}_001" ]]   || fFail "sweep: kept an old folder whose pid is gone"
[[ -d "${app}/${old}_002" ]]     || fFail "sweep: removed an old folder whose pid is live"
[[ -d "${app}/${old}_003" ]]     || fFail "sweep: removed an old folder with no run.pid"
[[ -d "${app}/${recent}_001" ]]  || fFail "sweep: removed a folder less than 2 days old"
if ((haveLinks)); then
	[[ -L "${app}/${old}_004" && -f "${scratch}/outside/run.pid" ]] || fFail "sweep: followed or removed a link"
fi
[[ ! -e "${app}/${old}_005" ]]   || fFail "sweep: kept an old folder whose run.pid holds no pid"
[[ ! -e "${scratch}/pwned" ]]    || fFail "sweep: ran what run.pid held"

## Two runs in one second each get their own folder. With no HOST_TEMP, TMPDIR.
mk="${scratch}/mk"
mkdir -p "${mk}" "${scratch}/real-temp"
made="$(HOST_TEMP="$mk"; for _ in 1 2; do fRunTemp_Make unit 20200202-020202 && echo "${runTempDir} $(cat "${runTempDir}/run.pid") ${TMPDIR}"; done)"
want="${mk}/unit/20200202-020202_001 $$ ${mk}/unit/20200202-020202_001
${mk}/unit/20200202-020202_002 $$ ${mk}/unit/20200202-020202_002"
[[ "$made" == "$want" ]] || fFail "fRunTemp_Make twice in one second gave: ${made}"
if ((haveLinks)); then
	ln -s "${scratch}/real-temp" "${scratch}/link-temp"
	made="$(unset HOST_TEMP; TMPDIR="${scratch}/link-temp"; fRunTemp_Make unit 20200202-020202 && echo "$runTempDir")"
	[[ "$made" == "${scratch}/real-temp/unit/20200202-020202_001" ]] || fFail "fRunTemp_Make with TMPDIR through a link gave: ${made}"
else
	made="$(unset HOST_TEMP; TMPDIR="${scratch}/real-temp"; fRunTemp_Make unit 20200202-020202 && echo "$runTempDir")"
	[[ "$made" == "${scratch}/real-temp/unit/20200202-020202_001" ]] || fFail "fRunTemp_Make with TMPDIR gave: ${made}"
fi

## The guard.
fRefused(){
	local why="$1" rc=0 msg
	shift
	msg="$(HOST_TEMP="$hostTemp" projectName=app fSafeRm "$@" 2>&1)" || rc=$?
	[[ "$rc" != 0 && "$msg" == *"refused, ${why}"* ]] || fFail "fSafeRm $*: not refused as ${why}: exit ${rc}, ${msg}"
}
mkdir -p "${app}/victim/sub" "${scratch}/elsewhere/sub"
if ((haveLinks)); then
	ln -s "${scratch}/elsewhere" "${app}/through"
	ln -s "${scratch}/elsewhere" "${app}/alink"
fi
fRefused "bad path"                ""
fRefused "bad path"                "${app}/victim/../victim"
fRefused "bad path"                "${app}//victim"
fRefused "bad path"                "${app}/victim" "${app}/victim"
fRefused "not under a tmp folder"  "/nonexistent-cicd-temp-check/x"
fRefused "not under a tmp folder"  "relative/tmp/x"
if ((haveLinks)); then
	fRefused "a link"                  "${app}/alink"
	fRefused "goes through a link"     "${app}/through/sub"
fi
fRefused "not there"               "${app}/nosuch"
[[ -d "${scratch}/elsewhere/sub" && -d "${app}/victim/sub" ]] || fFail "fSafeRm removed something it refused"
HOST_TEMP="$hostTemp" projectName=app fSafeRm "${app}/victim/" > /dev/null || fFail "fSafeRm refused a plain folder"
[[ ! -e "${app}/victim" ]] || fFail "fSafeRm left a plain folder"

if ((failures)); then
	fEcho "FAILED: run folder, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: each run works in its own folder and removes it, failure and signals included; --keep-temp keeps it; stale folders go"
