#!/usr/bin/env bash

##	- Purpose: Check cicd.bash --list. It prints the steps the other options
##	  give, each with the test IDs its command runs, and runs nothing: no
##	  step, CPU scope, container cap, tool pin probe, run folder or log. A
##	  plain run of the same copy does each of those, so the markers can fail.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-list.bash
##	- Test ID: dez4x


##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"

fEcho(){ echo "[ $* ]"; }

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/cicd-list-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
export HOST_TEMP="${scratch}/host-temp"
mark="${scratch}/ran"
mkdir -p "${HOST_TEMP}" "${mark}" "${scratch}/bin" "${scratch}/cicd/utility/include" "${scratch}/cicd/linux" "${scratch}/source"
cp "${here}/../cicd.bash" "${scratch}/cicd/"
cp "${here}/test-id.py" "${scratch}/cicd/utility/"
for inc in gfs-rotate source-date run-temp docker-sweep jobs; do
	cp "${here}/include/${inc}.bash" "${scratch}/cicd/utility/include/"
done

## The CPU scope and the container cap would call these.
printf '#!/usr/bin/env bash\ntouch "%s/systemd-run"\nexit 1\n' "${mark}" > "${scratch}/bin/systemd-run"
printf '#!/usr/bin/env bash\ntouch "%s/docker"\nexit 1\n' "${mark}" > "${scratch}/bin/docker"
chmod +x "${scratch}/bin/systemd-run" "${scratch}/bin/docker"

## A tree with one test of each kind. Every script leaves a mark if it runs.
cat > "${scratch}/source/meson.build" <<-'MESON'
	test('d0001 Named in the release smoke', exe)
	test('d0006 Not in the release smoke', exe)
MESON
printf '#!/usr/bin/env bash\ntouch "%s/tests"\nmeson test -C build\n' "${mark}" > "${scratch}/cicd/linux/run-tests.bash"
printf '#!/usr/bin/env bash\ntouch "%s/release"\nmeson test -C build "d0001 Named in the release smoke"\n' "${mark}" > "${scratch}/cicd/linux/release.bash"
printf '#!/usr/bin/env bash\ntouch "%s/fuzz"\ntargets=(\n\t"d0005|fuzz-one|one"\n)\n' "${mark}" > "${scratch}/cicd/linux/fuzz.bash"
cat > "${scratch}/cicd/utility/lint.bash" <<-LINT
	#!/usr/bin/env bash
	touch "${mark}/lint"
	here="\$(dirname "\${BASH_SOURCE[0]}")"
	bash "\${here}/test-run.bash"
	echo "[ test-pkg.bash skipped: it runs in Packages ]"
	bash "\${here}/lint-c.bash"
LINT
printf '#!/usr/bin/env bash\n\n## Checks a thing.\n##	Test ID: d0004\nfCheckThing(){ :; }\n' > "${scratch}/cicd/utility/lint-c.bash"
printf '#!/usr/bin/env bash\n## Test ID: d0002\ntouch "%s/test-run"\n' "${mark}" > "${scratch}/cicd/utility/test-run.bash"
printf '#!/usr/bin/env bash\n## Test ID: d0003\ntouch "%s/test-pkg"\n' "${mark}" > "${scratch}/cicd/utility/test-pkg.bash"
cat > "${scratch}/cicd/config.bash" <<-CFG
	APP_NAME="App"; EXE_NAME="app"; CICD_CONTAINERS=("app-build"); DOCKER_SWEEP=0
	TOOL_PINS=("pin|1.0|touch ${mark}/pin")
	FMT_CMD=(); FMT_CHECK_CMD=(); DEBUG_BUILD_CMD=(false); TEST_CMD=(bash cicd/linux/run-tests.bash)
	LINT_PROBE=(true); LINT_CMD=(bash cicd/utility/lint.bash); FUZZ_PROBE=(true); FUZZ_CMD=(bash cicd/linux/fuzz.bash)
	FUZZ_QUICK_CMD=(); SANITIZE_CMD=(bash cicd/linux/run-tests.bash --sanitize); ANALYZE_CMD=()
	PROFILE_ENABLE=0; PROFILE_OUT_DIR="out"
	RELEASE_ENABLE=1; RELEASE_NATIVE_CMD=(bash cicd/linux/release.bash); RELEASE_NATIVE_BIN="x"; BUILD_CROSS=0; CROSS_TARGETS=()
	VERSION_MANIFEST="meson.build"; PACKAGE_ENABLE=1; PACKAGE_CMDS=(); PACKAGE_CHECKS=("Pkg check|bash cicd/utility/test-pkg.bash")
	PRIVATE_RUNNER=""; PRIVATE_DOGFOOD=(); SHOTS_ENABLE=0; DEMO_ENABLE=0; LINT_LOG_DIR="logs"
	DOGFOOD_FIXED_DESTS=(); DOGFOOD_ROTATING_DESTS=(); DOGFOOD_CROSS_DESTS=(); DOGFOOD_HOOK=()
	DOGFOOD_REMOTE=(); GIT_PUBLISH=(); PUBLISH_AUTO_MESSAGE="unused"
CFG

fCicd(){ (cd "$scratch" && env -u CICD_CPU_CAPPED PATH="${scratch}/bin:${PATH}" SOURCE_DATE_EPOCH=1791244781 bash cicd/cicd.bash "$@" 2>&1 </dev/null); }
## The IDs listed under a step, one a line.
fUnder(){ awk -v s="$1" '/^Step / { on = (substr($0, index($0, "; ") + 2) == s); next } on && /^    / { print $1 }' <<< "$2"; }
fMarks(){ find "$mark" -mindepth 1 -printf '%f\n' | sort | tr '\n' ' '; }

rc=0; out="$(fCicd --list --no-sync)" || rc=$?
((rc == 0)) || fFail "--list exited ${rc}"
[[ "$(fUnder "Tests" "$out" | tr '\n' ' ')" == "d0001 d0006 " ]] || fFail "Tests should list d0001 d0006, got: $(fUnder "Tests" "$out" | tr '\n' ' ')"
[[ "$(fUnder "Lints" "$out" | tr '\n' ' ')" == "d0002 d0004 " ]] || fFail "Lints should list d0002 d0004 and not the echoed d0003, got: $(fUnder "Lints" "$out" | tr '\n' ' ')"
[[ "$(fUnder "Fuzzing" "$out")" == "d0005" ]] || fFail "Fuzzing should list d0005"
[[ "$(fUnder "Release build (native)" "$out")" == "d0001" ]] || fFail "the release smoke should list only the meson test it names"
[[ "$(fUnder "Packages" "$out")" == "d0003" ]] || fFail "Packages should list d0003"
grep -q '^    the 2 meson tests, as in Tests$' <<< "$out" || fFail "the sanitizer suite should point back to the meson tests under Tests"
grep -q '^Step 1 of 7; Build (debug)$' <<< "$out" || fFail "the plan's steps should be numbered as a run numbers them"
grep -q '^    - Remote sync (--no-sync)$' <<< "$out" || fFail "the steps turned off should be listed"
[[ -z "$(fMarks)" ]] || fFail "--list ran something: $(fMarks)"
[[ -z "$(ls -A "${HOST_TEMP}")" ]] || fFail "--list made a run folder"
[[ ! -e "${scratch}/logs" ]] || fFail "--list started a run log"

out="$(fCicd --backlog-run-quick lint --list)" || fFail "--backlog-run-quick lint --list exited non-zero"
grep -q '^Step 1 of 1; Lints$' <<< "$out" || fFail "--backlog-run-quick lint --list should list only Lints"
out="$(fCicd --list --no-container --no-sync)" || fFail "--list --no-container exited non-zero"
grep -q '^Step 1 of 1; Lints$' <<< "$out" || fFail "--list --no-container should list only the lints"
[[ -z "$(fMarks)" ]] || fFail "--list ran something: $(fMarks)"

## The control: a plain run of the same copy leaves every mark the list must not.
fCicd -y --no-sync >/dev/null || true
for want in systemd-run docker pin; do
	[[ -e "${mark}/${want}" ]] || fFail "a plain run left no ${want} mark, so the check above proves nothing"
done
[[ -e "${scratch}/logs" ]] || fFail "a plain run started no log, so the check above proves nothing"

if ((failures)); then exit 1; fi
fEcho "OK: --list lists each step's tests and runs nothing"
