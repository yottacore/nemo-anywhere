#!/usr/bin/env bash

##	- Purpose: Check that the Windows cross build's steps each run in a
##	  container of their own (win-run.bash, win-box.bash): named
##	  <project>-<run folder>-<step>, labeled, --rm unless CICD_KEEP_TEMP=1,
##	  the /build-win cache volume made for this clone and refused for another,
##	  left out with --no-build-win, the command's exit code passed back, and
##	  the container stopped after, on a failure too. No docker, the daemon
##	  down, --no-container or no image to be had stops with one line. A copy
##	  runs in a scratch dir with a stand-in docker.
##	- Then no script uses the long-lived nemo-build, nemo-winbuild or
##	  nemo-build-jammy boxes.
##	- Runs in the lint stage.
##	- Syntax: cicd/win/test-win-run.bash
##	- Test ID: defps

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

# shellcheck source=../utility/include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"
HOST_TEMP="${TMPDIR:-/tmp}"
fRunTemp_Make win-run-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: could not make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/win-run-check" 2> /dev/null || true' EXIT

repo="${scratch}/repo"
log="${scratch}/docker.log"
mkdir -p "${repo}/cicd/win" "${repo}/cicd/utility/include" "${scratch}/bin"
cp "${root}/cicd/win/win-run.bash" "${root}/cicd/win/win-box.bash" "${repo}/cicd/win/"
for inc in source-date jobs step-box; do cp "${root}/cicd/utility/include/${inc}.bash" "${repo}/cicd/utility/include/"; done
realRepo="$(cd "${repo}" && pwd -P)"
## STUB_IMAGES the images there are, STUB_VOL_SRC the clone the volume belongs
## to (none: no volume yet), STUB_NAMES the containers there are, STUB_DOWN
## the daemon down, STUB_EXEC_RC what the step exits with.
cat > "${scratch}/bin/docker" <<-'EOF2'
	#!/usr/bin/env bash
	printf '%s\n' "$*" >> "$DOCKER_LOG"
	case "$1 ${2:-}" in
		"info "*)         [[ -z "${STUB_DOWN:-}" ]]; exit ;;
		"image inspect")  [[ " ${STUB_IMAGES:-} " == *" $3 "* ]]; exit ;;
		"volume inspect") [[ -n "${STUB_VOL_SRC:-}" ]] || exit 1; printf '%s\n' "$STUB_VOL_SRC"; exit 0 ;;
		"ps "*)           printf '%s\n' ${STUB_NAMES:-}; exit 0 ;;
		"build "*)        exit 1 ;;
		"exec "*)         [[ "$*" != *" -i "* ]] || cat > /dev/null; exit "${STUB_EXEC_RC:-0}" ;;
	esac
	exit 0
EOF2
chmod +x "${scratch}/bin/docker"

## $1 the args to win-run.bash, as one string, the rest env. Sets out, rc and
## runLine, execLine and stopAt.
out=""; rc=0; runLine=""; execLine=""
fRun(){
	local args="$1"; shift
	: > "$log"
	rc=0
	# shellcheck disable=SC2086  ## args split on purpose
	out="$(env -u CICD_NO_CONTAINER -u CICD_KEEP_TEMP -u CICD_RUN -u CICD_PROJECT -u NEMO_WIN_IMAGE -u STUB_VOL_SRC -u STUB_NAMES -u STUB_DOWN -u STUB_EXEC_RC \
		PATH="${scratch}/bin:${PATH}" DOCKER_LOG="$log" SOURCE_DATE_EPOCH=946684800 CICD_MAX_JOBS=1 STUB_IMAGES="nemo-winbuild:init" "$@" \
		bash "${repo}/cicd/win/win-run.bash" ${args} 'echo the-step' 2>&1 < /dev/null)" || rc=$?
	runLine="$(grep '^run ' "$log" || true)"
	execLine="$(grep '^exec ' "$log" || true)"
}
## The line number of the first call that starts with $1, or 0.
fAt(){ local n; n="$(grep -n "^$1" "$log" | head -1 | cut -d: -f1)"; printf '%s' "${n:-0}"; }
inRun=(CICD_PROJECT=nemo-anywhere CICD_RUN=20261026-120000_001 STUB_VOL_SRC="$realRepo")
box="nemo-anywhere-20261026-120000_001-gui-smoke"

fRun "gui-smoke" "${inRun[@]}"
[[ "$rc" == 0 ]] || fFail "[run]: exit ${rc}, said: ${out}"
[[ "$runLine" == "run -d --rm "* ]] || fFail "[run]: not docker run -d --rm: ${runLine:-no run}"
[[ "$runLine" == *" --name ${box} "* ]] || fFail "[run]: wrong name: ${runLine}"
[[ "$runLine" == *" --label dev.project=nemo-anywhere --label dev.run=20261026-120000_001 "* ]] || fFail "[run]: labels: ${runLine}"
[[ "$runLine" == *" --cpus 1 --cpu-shares 256 "* ]] || fFail "[run]: CPU cap: ${runLine}"
[[ "$runLine" == *" -v ${repo}:/src -v nemo-anywhere-build-win:/build-win -w /src nemo-winbuild:init sleep infinity" ]] || fFail "[run]: mounts or image: ${runLine}"
[[ "$execLine" == "exec -i -e SOURCE_DATE_EPOCH=946684800 -e CICD_MAX_JOBS=1 ${box} nice -n 19 sh -c "*"; echo the-step" ]] || fFail "[run]: the step: ${execLine:-no exec}"
grep -q '^volume create' "$log" && fFail "[run]: made a volume that was there"
(( $(fAt "stop -t 2 ${box}") > $(fAt "exec ") )) || fFail "[run]: not stopped after the step: $(tr '\n' '|' < "$log")"

fRun "gui-smoke" "${inRun[@]}" CICD_KEEP_TEMP=1
[[ "$rc" == 0 && "$runLine" == "run -d "* && "$runLine" != *"--rm"* ]] || fFail "[--keep-temp]: kept no container: ${runLine:-no run}"

fRun "gui-smoke" "${inRun[@]}" STUB_NAMES="${box} other-box"
[[ "$runLine" == *" --name ${box}-2 "* ]] || fFail "[name taken]: ${runLine:-no run}"

fRun "--no-build-win setup-check" CICD_PROJECT=nemo-anywhere CICD_RUN=20261026-120000_001
[[ "$rc" == 0 && "$runLine" == *" -v ${repo}:/src -w /src nemo-winbuild:init "* ]] || fFail "[--no-build-win]: exit ${rc}: ${runLine:-no run}"
grep -q '^volume' "$log" && fFail "[--no-build-win]: looked at the volume"

fRun "check" CICD_PROJECT=nemo-anywhere CICD_RUN=20261026-120000_001
grep -qxF "volume create --label dev.project=nemo-anywhere --label dev.run=20261026-120000_001 --label dev.src=${realRepo} nemo-anywhere-build-win" "$log" \
	|| fFail "[no volume]: not made for this clone: $(grep '^volume' "$log" | tr '\n' ' ')"

fRun "check" "${inRun[@]}" STUB_VOL_SRC=/some/other/clone
[[ "$rc" == 1 && "$out" == *"FAILED: check: nemo-anywhere-build-win builds /some/other/clone, not ${realRepo}"* && -z "$runLine" ]] || fFail "[other clone]: exit ${rc}, said: ${out}"

fRun "gui-smoke" "${inRun[@]}" STUB_EXEC_RC=77
[[ "$rc" == 77 ]] || fFail "[exit code]: 77 came back as ${rc}"
(( $(fAt "stop -t 2 ${box}") > 0 )) || fFail "[exit code]: not stopped after a failed step"

fRun "check" "${inRun[@]}" STUB_IMAGES="nemo-winbuild-deps:latest"
[[ "$runLine" == *" nemo-winbuild-deps:latest sleep infinity" ]] || fFail "[fresh clone]: not the Dockerfile image: ${runLine:-no run}"

fRun "check" "${inRun[@]}" STUB_IMAGES=""
[[ "$rc" == 1 && "$out" == *"FAILED: check: could not build image nemo-winbuild-deps"* && -z "$runLine" ]] || fFail "[no image]: exit ${rc}, said: ${out}"

fRun "check" "${inRun[@]}" STUB_DOWN=1
[[ "$rc" == 1 && "$out" == *"FAILED: check: docker daemon is not reachable"* && -z "$runLine" ]] || fFail "[daemon down]: exit ${rc}, said: ${out}"

fRun "check" "${inRun[@]}" CICD_NO_CONTAINER=1
[[ "$rc" == 1 && "$out" == *"FAILED: check needs a container"* && ! -s "$log" ]] || fFail "[--no-container]: exit ${rc}, said: ${out}"

## Comments aside, no docker call names a long-lived box, and no override
## points at one.
boxes='(nemo-build|nemo-winbuild|nemo-build-jammy)'
uses="$(cd "${root}" && git ls-files -co --exclude-standard -- '*.bash' '*.sh' '*.ps1' \
	| grep -v -x 'cicd/win/test-win-run.bash' \
	| xargs grep -nE "docker +(exec|cp|start|stop|update|inspect|rm)( +-[a-z]+)* +\"?${boxes}(\"|:/| |$)|NEMO_(WIN|RELEASE)_CONTAINER|CICD_CONTAINERS=\(.*${boxes}" 2> /dev/null \
	| grep -vE '^[^:]+:[0-9]+:[[:space:]]*#' || true)"
[[ -z "$uses" ]] || fFail "[long-lived boxes]: still used: ${uses}"

if ((failures)); then
	fEcho "FAILED: Windows step containers, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: each Windows cross-build step runs in a named container of its own, on this clone's cache volume"


##	History:
##		- 2026-10-26: Created.
