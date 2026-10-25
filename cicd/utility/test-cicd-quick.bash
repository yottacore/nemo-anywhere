#!/usr/bin/env bash

##	- Purpose: Check which steps a cicd.bash run reaches with no options, with
##	  --long and --slow, with --quick, with --quick plus --include-arm or
##	  --include-bsd, and with --gate. No options runs every step but arm64, and
##	  --long or --slow every step. --quick runs the quick ones on this box and
##	  skips fuzzing, packages and every step that needs another box. A copy of
##	  cicd.bash runs in a scratch git repo with every step a stand-in that
##	  leaves a marker file.
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
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/cicd-quick-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

mkdir -p "${scratch}/cicd/utility/include"
cp "${root}/cicd/cicd.bash" "${scratch}/cicd/"
for inc in gfs-rotate source-date; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${scratch}/cicd/utility/include/"
done
printf "project('app', 'c', version: '1.2.3')\n" > "${scratch}/meson.build"

## Every step drops a file named for itself in out/.
mark(){ printf 'mkdir -p out && touch out/%s' "$1"; }
for stub in private publish scroll; do
	printf '#!/usr/bin/env bash\nmkdir -p "%s/out" && touch "%s/out/%s"\n' "$scratch" "$scratch" "$stub" > "${scratch}/${stub}.bash"
	chmod +x "${scratch}/${stub}.bash"
done
cat > "${scratch}/cicd/config.bash" <<-EOF
	APP_NAME="App"
	EXE_NAME="app"
	FMT_CMD=()
	FMT_CHECK_CMD=()
	CICD_CONTAINERS=()
	DEBUG_BUILD_CMD=(bash -c '$(mark build)')
	TEST_CMD=(bash -c '$(mark tests)')
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
printf '/out/\n/native.bin\n' > "${scratch}/.gitignore"
git -C "$scratch" init -q
git -C "$scratch" add -A
git -C "$scratch" -c user.name=check -c user.email=check@example.invalid -c commit.gpgsign=false commit -q -m init

allSteps=(build tests lint fuzz sanitize scroll bsd-tests profile native windows arm64 bsd-release analyze packages package-check private dogfood dogfood-hook remote-dogfood publish)

## $1 the steps that must run, space separated, rest the options. Every other
## step must not.
fRun(){
	local want=" $1 " out rc=0 step ran
	shift
	rm -rf "${scratch}/out" "${scratch}/native.bin"
	mkdir -p "${scratch}/out"
	out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash -y "$@" 2>&1 </dev/null)" || rc=$?
	if [[ "$rc" != 0 ]]; then
		fFail "[$*]: the run failed (exit ${rc}): $(tail -5 <<< "$out")"
		return 0
	fi
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
	if [[ "$want" == *" sync "* ]]; then
		[[ "$out" == *"nothing to sync"* ]] || fFail "[${*:-no options}]: remote sync did not run"
	else
		[[ "$out" != *"nothing to sync"* ]] || fFail "[${*:-no options}]: remote sync ran"
	fi
}

quickSteps="sync build tests lint sanitize profile native windows dogfood dogfood-hook publish"
## A plain run leaves arm64 out since 2026-10-24, to stay near an hour.
fRun "sync ${allSteps[*]/arm64}"
fRun "sync ${allSteps[*]}"               --long
fRun "sync ${allSteps[*]}"               --slow
fRun "${quickSteps}"                     --quick
fRun "${quickSteps} arm64"               --quick --include-arm
fRun "${quickSteps} bsd-tests bsd-release" --quick --include-bsd
fRun "tests lint"                        --gate

if ((failures)); then
	fEcho "FAILED: step selection, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: no options runs every step but arm64, --long all, --quick the quick local ones, --gate tests and lints"
