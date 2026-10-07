#!/usr/bin/env bash

##	- Purpose: Check that the arm64 release build runs only with --include-arm,
##	  that --no-arm still leaves it out, and that the engine finds an artifact
##	  whose name has the version in it. The arm64 packages and their check run
##	  only when the arm64 build did. A copy of cicd.bash runs a whole pipeline
##	  in a scratch dir, every stage a stand-in, with one arm64 and one other
##	  cross target, packager and check. Then the real config.bash is read: its
##	  arm64 entry has to name release-arm64.bash and the tarball that script
##	  writes, and its arm64 packager and check have to ask for arm64.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-arm.bash
##	- Test ID: rjph39cv

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

scratch="$(mktemp -d "${TMPDIR:-/tmp}/cicd-arm-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

ver="4.5.6"
mkdir -p "${scratch}/cicd/utility/include"
cp "${root}/cicd/cicd.bash" "${scratch}/cicd/"
for inc in gfs-rotate source-date; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${scratch}/cicd/utility/include/"
done
printf "project('app', 'c', version: '%s')\n" "$ver" > "${scratch}/meson.build"
cat > "${scratch}/cicd/config.bash" <<-EOF
	APP_NAME="App"
	EXE_NAME="app"
	FMT_CMD=()
	FMT_CHECK_CMD=()
	CICD_CONTAINERS=()
	DEBUG_BUILD_CMD=(true)
	TEST_CMD=(true)
	LINT_PROBE=(true)
	LINT_CMD=()
	SANITIZE_CMD=()
	RELEASE_ENABLE=1
	RELEASE_NATIVE_CMD=(touch native.bin)
	RELEASE_NATIVE_BIN="native.bin"
	BUILD_CROSS=1
	CROSS_TARGETS=(
		"Windows x86_64 (stand-in)|windows-x86_64|out/app.exe|mkdir -p out && touch out/app.exe"
		"Linux arm64 (stand-in)|linux-arm64|out/app-@VER@-linux-arm64.tar.gz|mkdir -p out && touch out/app-${ver}-linux-arm64.tar.gz"
	)
	RELEASE_ARTIFACT_DIR="out"
	RELEASE_COLLECT=0
	VERSION_MANIFEST="meson.build"
	PACKAGE_ENABLE=1
	PACKAGE_CMDS=(
		"Other packages (stand-in)|mkdir -p out && touch out/pkg-other"
		"Linux arm64 packages (stand-in)|mkdir -p out && touch out/pkg-arm64"
	)
	PACKAGE_CHECKS=(
		"Other check (stand-in)|mkdir -p out && touch out/check-other"
		"Check (arm64, stand-in)|mkdir -p out && touch out/check-arm64"
	)
	PRIVATE_RUNNER=""
	PROFILE_ENABLE=0
	PROFILE_OUT_DIR="profiling"
	SHOTS_ENABLE=0
	DEMO_ENABLE=0
	LINT_LOG_DIR=""
	DOGFOOD_FIXED_DESTS=()
	DOGFOOD_ROTATING_DESTS=()
	DOGFOOD_CROSS_DESTS=()
	DOGFOOD_HOOK=()
	DOGFOOD_REMOTE=()
	DOGFOOD_PREFIX=""
	DOGFOOD_TAG=""
	GIT_PUBLISH=()
	PUBLISH_AUTO_MESSAGE="unused"
EOF

## $1 "arm" or "no arm", $2 text the output must have, rest the options.
fPipeline(){
	local want="$1" text="$2"; shift 2
	local out rc=0
	rm -rf "${scratch}/out" "${scratch}/native.bin"
	out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash -y --no-sync "$@" 2>&1 </dev/null)" || rc=$?
	if [[ "$rc" != 0 ]]; then
		fFail "[$*]: the run failed (exit ${rc}): $(tail -5 <<< "$out")"
		return 0
	fi
	[[ -f "${scratch}/out/app.exe" ]] || fFail "[$*]: the other cross target did not run"
	[[ -f "${scratch}/out/pkg-other" && -f "${scratch}/out/check-other" ]] || fFail "[$*]: the other packager or check did not run"
	case "$want" in
		arm)
			[[ -f "${scratch}/out/app-${ver}-linux-arm64.tar.gz" ]] || fFail "[$*]: the arm64 build did not run"
			[[ "$out" == *"OK: Linux arm64 (stand-in): out/app-${ver}-linux-arm64.tar.gz"* ]] || fFail "[$*]: the arm64 artifact was not found under its versioned name"
			[[ -f "${scratch}/out/pkg-arm64" ]] || fFail "[$*]: the arm64 packages were not made"
			[[ -f "${scratch}/out/check-arm64" ]] || fFail "[$*]: the arm64 check did not run"
			;;
		"no arm")
			[[ ! -e "${scratch}/out/app-${ver}-linux-arm64.tar.gz" ]] || fFail "[$*]: the arm64 build ran"
			[[ ! -e "${scratch}/out/pkg-arm64" ]] || fFail "[$*]: arm64 packages were made with no arm64 build"
			[[ ! -e "${scratch}/out/check-arm64" ]] || fFail "[$*]: the arm64 check ran with no arm64 build"
			[[ "$out" != *"Linux arm64 packages"* ]] || fFail "[$*]: the plan lists the arm64 packages"
			;;
	esac
	[[ -z "$text" || "$out" == *"$text"* ]] || fFail "[$*]: no '${text}' in the plan"
}

fPipeline "no arm" "(no arm64; --include-arm adds it)"
fPipeline arm      ""                                   --include-arm
fPipeline "no arm" "(no arm64, --no-arm)"               --include-arm --no-arm
fPipeline "no arm" "(no arm64, --no-arm)"               --no-arm --include-arm
fPipeline "no arm" "(no arm64, --no-arm)"               --no-arm

## --no-cross leaves out every cross target, arm64 too.
rm -rf "${scratch}/out"
out="$(cd "$scratch" && CICD_CPU_CAPPED=1 SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash -y --no-sync --include-arm --no-cross 2>&1 </dev/null)" || fFail "--include-arm --no-cross failed"
[[ ! -e "${scratch}/out/app.exe" && ! -e "${scratch}/out/app-${ver}-linux-arm64.tar.gz" ]] || fFail "--no-cross still ran a cross target"
[[ ! -e "${scratch}/out/pkg-arm64" && ! -e "${scratch}/out/check-arm64" ]] || fFail "--include-arm --no-cross still made or checked arm64 packages"
[[ -f "${scratch}/out/pkg-other" ]] || fFail "--include-arm --no-cross left out the other packages"
[[ "$out" == *"(skipped, arm64 too)"* ]] || fFail "--include-arm --no-cross does not say arm64 is skipped"

help="$(bash "${root}/cicd/cicd.bash" --help)"
for opt in --include-arm --no-arm; do
	grep -qE "^ +${opt} " <<< "$help" || fFail "--help has no ${opt}"
done

## The real entry: only the arm64 one may match the engine's arm64 filter, and
## it has to point at the file release-arm64.bash writes.
entries="$(bash -c 'root="$1"; source "$1/cicd/config.bash"; printf "%s\n" "${CROSS_TARGETS[@]}"' _ "$root")"
armEntries="$(grep -E 'arm64|aarch64' <<< "$entries" || true)"
if [[ "$(grep -c . <<< "$armEntries")" != 1 || -z "$armEntries" ]]; then
	fFail "config.bash should have one arm64 cross target, has: ${armEntries:-none}"
else
	rest="${armEntries#*|}"; osarch="${rest%%|*}"; rest="${rest#*|}"; art="${rest%%|*}"; cmd="${rest#*|}"
	realVer="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${root}/source/meson.build" | head -1)"
	# shellcheck source=include/release-files.bash
	source "${root}/cicd/utility/include/release-files.bash"
	wantArt="cicd/artifacts/release/nemo-anywhere-${realVer}-linux-$(fReleaseArch aarch64).tar.gz"
	[[ "${art//@VER@/${realVer}}" == "$wantArt" ]] || fFail "the arm64 entry looks for ${art}, the lane writes ${wantArt}"
	[[ "$osarch" == "linux-arm64" ]] || fFail "the arm64 entry is for ${osarch}"
	[[ "$cmd" == "bash cicd/linux/release-arm64.bash" ]] || fFail "the arm64 entry runs '${cmd}'"
	grep -qF "\"\${SLUG}-\${ver}-linux-arm64.tar.gz\"" "${root}/cicd/linux/release-arm64.bash" || fFail "release-arm64.bash no longer names its tarball the way this check expects"
fi

## The real packagers and checks: one arm64 entry each, asking for arm64.
for list in PACKAGE_CMDS PACKAGE_CHECKS; do
	entries="$(bash -c 'root="$1"; source "$1/cicd/config.bash"; declare -n l="$2"; printf "%s\n" "${l[@]}"' _ "$root" "$list")"
	armEntries="$(grep -E 'arm64|aarch64' <<< "$entries" || true)"
	if [[ "$(grep -c . <<< "$armEntries")" != 1 || -z "$armEntries" ]]; then
		fFail "${list} should have one arm64 entry, has: ${armEntries:-none}"
	elif [[ "${armEntries#*|}" != *" --arch arm64" ]]; then
		fFail "the arm64 entry in ${list} runs '${armEntries#*|}'"
	fi
done

if ((failures)); then
	fEcho "FAILED: arm64 in the pipeline, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: arm64 release build runs only with --include-arm"
