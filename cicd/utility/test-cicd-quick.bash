#!/usr/bin/env bash

##	- Purpose: Check which steps a cicd.bash run reaches with no options, with
##	  --long and --slow, with --quick, with --quick plus --include-arm or
##	  --include-bsd, with --backlog-run-quick and its areas, with
##	  --backlog-run-full and --merge-gate, and with the other names --gate and
##	  --ci. No options runs every step but arm64, and --long or --slow every
##	  step. --quick runs the quick ones on this box and skips fuzzing, packages
##	  and every step that needs another box. --backlog-run-full and
##	  --merge-gate run every check a plain run does, and never sync, format in
##	  place, dogfood or publish. --no-remote leaves out every step on another
##	  box. Each step opens with a Starting line and closes with PASS, or FAIL
##	  for one that fails, numbered out of the steps that run, under a rule of
##	  79 dots. The plan lists what's turned off, and a hard step gets a quarter
##	  of the cpus. A copy of cicd.bash runs in a scratch git repo
##	  with every step a stand-in that leaves a marker file.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-quick.bash
##	- Test ID: rkmqprx6

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
lastOut=""
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/cicd-quick-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT
## Run folders go in the scratch dir, not the box's own temp root.
export HOST_TEMP="${scratch}/host-temp"
mkdir -p "${HOST_TEMP}"

mkdir -p "${scratch}/cicd/utility/include"
cp "${root}/cicd/cicd.bash" "${scratch}/cicd/"
for inc in gfs-rotate source-date run-temp docker-sweep jobs; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${scratch}/cicd/utility/include/"
done
printf "project('app', 'c', version: '1.2.3')\n" > "${scratch}/meson.build"

## Every step drops a file named for itself in out/.
mark(){ printf 'mkdir -p out && touch out/%s' "$1"; }
for stub in private publish scroll; do
	printf '#!/usr/bin/env bash\nmkdir -p "%s/out" && touch "%s/out/%s"\n' "$scratch" "$scratch" "$stub" > "${scratch}/${stub}.bash"
	chmod +x "${scratch}/${stub}.bash"
done
## Screenshots and the demo video are found by path, not by setting.
printf '#!/usr/bin/env bash\nmkdir -p "%s/out" && touch "%s/out/shots"\n' "$scratch" "$scratch" > "${scratch}/cicd/utility/screenshots.bash"
chmod +x "${scratch}/cicd/utility/screenshots.bash"
mkdir -p "${scratch}/cicd/utility/demo-video"
printf 'import pathlib\npathlib.Path("%s/out/demo").touch()\n' "$scratch" > "${scratch}/cicd/utility/demo-video/demo-video.py"
cat > "${scratch}/cicd/config.bash" <<-EOF
	APP_NAME="App"
	EXE_NAME="app"
	FMT_CMD=(bash -c '$(mark fmt)')
	FMT_CHECK_CMD=(bash -c '$(mark fmt-check)')
	CICD_CONTAINERS=()
	DEBUG_BUILD_CMD=(bash -c '$(mark build)')
	TEST_CMD=(bash -c '[ -z "\${CHECK_FAIL_TESTS:-}" ] || exit 3; $(mark tests)')
	LINT_PROBE=(true)
	LINT_CMD=(bash -c '$(mark lint)')
	FUZZ_PROBE=(true)
	FUZZ_CMD=(bash -c '$(mark fuzz)')
	SANITIZE_CMD=(bash -c '$(mark sanitize)')
	ANALYZE_CMD=(bash -c '$(mark analyze)')
	SCROLL_HARNESS=(scroll.bash)
	BSD_TEST_CMD=(bash -c '$(mark bsd-tests)')
	BSD_RELEASE_CMD=(bash -c '$(mark bsd-release)')
	PROFILE_ENABLE=1
	PROFILE_SECS=1
	PROFILE_PROBE=(true)
	PROFILE_CMD=(bash -c '$(mark profile) && echo "<svg/>" > "\$1"' _)
	PROFILE_OUT_DIR="out/profiling"
	PROFILE_STRICT=1
	RELEASE_ENABLE=1
	RELEASE_NATIVE_CMD=(bash -c '$(mark native) && touch native.bin')
	RELEASE_NATIVE_BIN="native.bin"
	BUILD_CROSS=1
	CROSS_TARGETS=(
		"Windows x86_64 (stand-in)|windows-x86_64|out/windows|$(mark windows)"
		"Linux arm64 (stand-in)|linux-arm64|out/arm64|$(mark arm64)"
	)
	RELEASE_ARTIFACT_DIR="out"
	RELEASE_COLLECT=0
	VERSION_MANIFEST="meson.build"
	PACKAGE_ENABLE=1
	PACKAGE_CMDS=("Packages (stand-in)|$(mark packages)")
	PACKAGE_CHECKS=("Check (stand-in)|$(mark package-check)")
	PRIVATE_RUNNER="${scratch}/private.bash"
	PRIVATE_DOGFOOD=()
	SHOTS_ENABLE=0
	DEMO_ENABLE=0
	LINT_LOG_DIR=""
	DOGFOOD_PREFIX_SRC=""
	DOGFOOD_FIXED_DESTS=("${scratch}/out")
	DOGFOOD_ROTATING_DESTS=()
	DOGFOOD_CROSS_DESTS=()
	DOGFOOD_HOOK=(bash -c '$(mark dogfood-hook)')
	DOGFOOD_REMOTE=(bash -c '$(mark remote-dogfood)')
	DOGFOOD_PREFIX=""
	DOGFOOD_TAG=""
	GIT_PUBLISH=("${scratch}/publish.bash")
	PUBLISH_AUTO_MESSAGE="unused"
EOF
## A repo with no upstream, so the sync stage runs and finds nothing to do, and
## a clean tree, so publish goes ahead.
printf '/out/\n/native.bin\n/host-temp/\n' > "${scratch}/.gitignore"
git -C "$scratch" init -q
git -C "$scratch" add -A
git -C "$scratch" -c user.name=check -c user.email=check@example.invalid -c commit.gpgsign=false commit -q -m init

allSteps=(fmt fmt-check build tests lint fuzz sanitize scroll bsd-tests profile native windows arm64 bsd-release analyze packages package-check private dogfood dogfood-hook remote-dogfood shots demo publish)

## The step lines of a run: each step opens with Starting and closes with PASS,
## or FAIL for the last one when $3 is 1, numbered 1 to M with M the same on
## every line, under a rule of 79 dots, and with local time to the hundredth.
## M counts only the steps that run, and $2 is how many must have.
rule="$(printf '•%.0s' {1..79})"
fSteps(){
	local out="$1" want="$2" failed="${3:-0}" label="$4" line n=0 total="" state="" name="" re
	re='^\[ ([0-9]{8}-[0-9]{6}\.[0-9]{2}): Step ([0-9]+) of ([0-9]+); (Starting|PASS|FAIL) (.+) \]$'
	while IFS= read -r line; do
		if [[ "$line" == •* ]]; then
			[[ "$line" == "$rule" ]] || { fFail "[${label}]: a rule that isn't 79 dots: ${line}"; return 0; }
			continue
		fi
		[[ "$line" =~ $re ]] || continue
		total="${total:-${BASH_REMATCH[3]}}"
		[[ "${BASH_REMATCH[3]}" == "$total" ]] || { fFail "[${label}]: step totals differ: ${line}"; return 0; }
		case "${BASH_REMATCH[4]}" in
			Starting)
				[[ "$state" != open ]] || { fFail "[${label}]: step ${n} never closed"; return 0; }
				n=$((n + 1)); name="${BASH_REMATCH[5]}"; state=open
				[[ "${BASH_REMATCH[2]}" == "$n" ]] || { fFail "[${label}]: step ${BASH_REMATCH[2]} where ${n} was due"; return 0; } ;;
			*)
				[[ "$state" == open && "${BASH_REMATCH[2]}" == "$n" && "${BASH_REMATCH[5]}" == "$name" ]] \
					|| { fFail "[${label}]: closed a step that wasn't open: ${line}"; return 0; }
				state="${BASH_REMATCH[4]}" ;;
		esac
	done <<< "$out"
	if ((failed)); then
		[[ "$state" == FAIL ]] || fFail "[${label}]: the failed step didn't close as FAIL (${state:-none})"
	else
		[[ "$state" == PASS && "$n" == "$total" ]] || fFail "[${label}]: ran ${n} of ${total:-?} steps, last ${state:-none}"
		[[ "$total" == "$want" ]] || fFail "[${label}]: counted ${total:-no} steps, want ${want}"
	fi
	! grep -q -E 'Step [0-9]+ of [0-9]+; FAIL' <<< "$out" || ((failed)) || fFail "[${label}]: a step failed"
}

## $1 the steps that must run, space separated, rest the options. Every other
## step must not.
fRun(){
	local want=" $1 " out rc=0 step ran
	shift
	rm -rf "${scratch}/out" "${scratch}/native.bin"
	mkdir -p "${scratch}/out"
	out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 env -u CICD_MAX_JOBS bash cicd/cicd.bash -y "$@" 2>&1 </dev/null)" || rc=$?
	lastOut="$out"
	if [[ "$rc" != 0 ]]; then
		fFail "[$*]: the run failed (exit ${rc}): $(tail -5 <<< "$out")"
		return 0
	fi
	## A step per marker, less the ones that share a step: the package check
	## with packages, the hook with dogfood, and sync, which leaves no marker.
	local steps=0 w
	for w in $want; do case "$w" in package-check|dogfood-hook) ;; *) steps=$((steps + 1)) ;; esac; done
	fSteps "$out" "$steps" 0 "${*:-no options}"
	for step in "${allSteps[@]}"; do
		ran=0
		case "$step" in
			dogfood) [[ -f "${scratch}/out/app" ]] && ran=1 ;;
			*)       [[ -f "${scratch}/out/${step}" ]] && ran=1 ;;
		esac
		if [[ "$want" == *" ${step} "* ]]; then
			((ran)) || fFail "[${*:-no options}]: ${step} did not run"
		else
			((! ran)) || fFail "[${*:-no options}]: ${step} ran"
		fi
	done
	[[ -z "$(git -C "$scratch" status --porcelain)" ]] || fFail "[${*:-no options}]: the run changed the tree"
	if [[ "$want" == *" sync "* ]]; then
		[[ "$out" == *"nothing to sync"* ]] || fFail "[${*:-no options}]: remote sync did not run"
	else
		[[ "$out" != *"nothing to sync"* ]] || fFail "[${*:-no options}]: remote sync ran"
	fi
}

fullSteps="fmt build tests lint fuzz sanitize scroll bsd-tests profile native windows arm64 bsd-release analyze packages package-check private dogfood dogfood-hook remote-dogfood publish"
quickSteps="sync fmt build tests lint sanitize profile native windows dogfood dogfood-hook publish"
## Every check of a plain run, with the format check in place of the formatter.
checkSteps="fmt-check build tests lint fuzz sanitize scroll bsd-tests profile native windows bsd-release analyze packages package-check private"
## A plain run leaves arm64 out since 2026-10-24, to stay near an hour.
fRun "sync ${fullSteps/ arm64 / }"
fRun "sync ${fullSteps}"                 --long
fRun "sync ${fullSteps}"                 --slow
fRun "sync ${fullSteps/ arm64 / } shots demo" --shots --demo
fRun "${quickSteps}"                     --quick
fRun "${quickSteps} arm64"               --quick --include-arm
fRun "${quickSteps} bsd-tests bsd-release" --quick --include-bsd
## --no-remote: nothing on another box, whatever else is named.
remoteSteps=" arm64 bsd-tests bsd-release private remote-dogfood "
localSteps=" ${fullSteps} "
for step in $remoteSteps; do localSteps="${localSteps/ ${step} / }"; done
fRun "sync ${localSteps}"                --no-remote
fRun "sync ${localSteps}"                --long --include-arm --include-bsd --no-remote
fRun "sync ${localSteps}"                --no-remote --include-arm --include-bsd

## $1 the other name, or empty: the run says so in one line, and only then.
fNote(){
	local n
	n="$(grep -c 'is another name for' <<< "$lastOut" || true)"
	if [[ -n "$1" ]]; then
		[[ "$n" == 1 && "$lastOut" == *"[ $1 is another name for $2 ]"* ]] || fFail "[$1]: no one-line note that it is another name for $2"
	else
		[[ "$n" == 0 ]] || fFail "[$2]: says it is another name"
	fi
}
fRun "fmt-check tests lint"              --backlog-run-quick;       fNote "" --backlog-run-quick
fRun "fmt-check tests lint"              --gate;                    fNote --gate --backlog-run-quick
fRun "lint"                              --backlog-run-quick lint
fRun "tests lint"                        --backlog-run-quick tests lint
fRun "fmt-check"                         --gate format
fRun "lint"                              --backlog-run-quick lint --no-sanitize
fRun "${checkSteps}"                     --backlog-run-full;        fNote "" --backlog-run-full
fRun "${checkSteps}"                     --merge-gate;              fNote "" --merge-gate
fRun "${checkSteps}"                     --ci;                      fNote --ci --merge-gate
fRun "${checkSteps} arm64"               --merge-gate --long
fRun "fmt-check build tests lint fuzz sanitize scroll profile native windows analyze packages package-check" --backlog-run-full --no-remote
## Nothing named after the mode turns a step that changes things back on.
fRun "${checkSteps}"                     --merge-gate --shots --demo --allow-dirty
## The last mode named wins.
fRun "${checkSteps}"                     --gate --merge-gate
fRun "fmt-check tests lint"              --backlog-run-full --backlog-run-quick
rc=0
out="$(cd "$scratch" && CICD_CPU_CAPPED=1 bash cicd/cicd.bash --backlog-run-quick lint nosuch 2>&1 </dev/null)" || rc=$?
[[ "$rc" == 2 && "$out" == *"unknown area: nosuch"* && "$out" == *"format lint tests"* ]] || fFail "[--backlog-run-quick lint nosuch]: exit ${rc}, not 2 with the areas listed: ${out}"
help="$(bash "${root}/cicd/cicd.bash" --help)"
for name in --backlog-run-quick --backlog-run-full --merge-gate --gate --ci --no-remote "format, lint, tests"; do
	grep -q -F -- "$name" <<< "$help" || fFail "--help does not name ${name}"
done

## A failed step closes as FAIL, and the plan lists what's turned off with why.
rc=0
out="$(cd "$scratch" && CHECK_FAIL_TESTS=1 CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 env -u CICD_MAX_JOBS bash cicd/cicd.bash -y --quick 2>&1 </dev/null)" || rc=$?
[[ "$rc" != 0 ]] || fFail "[failing tests]: the run passed"
fSteps "$out" 0 1 "failing tests"
grep -q -E '; FAIL Tests \]$' <<< "$out" || fFail "[failing tests]: no FAIL line for Tests"
for off in "Fuzzing (--quick)" "Static analyzers (--quick)" "Release build: Linux arm64 (stand-in) (--quick)" "Packages (--quick)"; do
	grep -q -x -F -- "    - ${off}" <<< "$out" || fFail "[--quick]: the plan doesn't list ${off} as turned off"
done
## A hard step gets a quarter of the cpus, busy or not.
# shellcheck source=include/jobs.bash
source "${here}/include/jobs.bash"
quarter="$(unset CICD_MAX_JOBS CICD_JOBS_PIN; fJobs hard; echo "${CICD_MAX_JOBS}")"
fRun "${quickSteps}" --quick
sanJobs="$(grep -A1 -F 'Starting Sanitizer suite' <<< "$lastOut" | tail -1)"
[[ "$sanJobs" == "jobs ${quarter} (hard step,"* ]] || fFail "[--quick]: the sanitizer suite didn't get ${quarter} jobs as a hard step"
fRun "fmt-check tests lint" --gate
grep -q -x -F -- "    - Format check (area not named)" <<< "$(cd "$scratch" && CICD_CPU_CAPPED=1 bash cicd/cicd.bash --gate lint 2>&1 </dev/null)" \
	|| fFail "[--gate lint]: the plan doesn't list the format check as turned off"

## Each run removed its run folder, and a plain run's plan names it.
[[ -z "$(ls -A "${HOST_TEMP}/app" 2>/dev/null)" ]] || fFail "runs left these behind: $(ls -A "${HOST_TEMP}/app")"
out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash -y --quick --no-publish 2>&1 </dev/null)" || true
grep -q -E "^Run folder \.+: ${HOST_TEMP}/app/[0-9]{8}-[0-9]{6}_[0-9]{3}$" <<< "$out" || fFail "the plan does not name the run folder"

if ((failures)); then
	fEcho "FAILED: step selection, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: no options runs every step but arm64, --long all, --quick the quick local ones, --backlog-run-quick its areas, --backlog-run-full and --merge-gate the checks only, --no-remote no other box"
