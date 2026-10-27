#!/usr/bin/env bash

##	- Purpose: Check that docker-run.bash runs each step in a container of its
##	  own: named <project>-<run folder>-<step>, labeled, with --rm unless
##	  CICD_KEEP_TEMP=1, the next number when the name is taken, the 2 cache
##	  volumes made for this clone and refused for another, and the cache cap
##	  run before the step. A copy runs in a scratch dir with a stand-in docker.
##	  Then cache-cap.bash empties a dir past its cap, keeps one under it, and
##	  leaves a link or a relative path alone.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-docker-run-step.bash
##	- Test ID: rm7z5v9s

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
HOST_TEMP="${TMPDIR:-/tmp}"
fRunTemp_Make docker-run-step-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: could not make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/docker-run-step-check" 2> /dev/null || true' EXIT

repo="${scratch}/repo"
log="${scratch}/docker.log"
mkdir -p "${repo}/cicd/utility/include" "${scratch}/bin"
cp "${here}/docker-run.bash" "${repo}/cicd/utility/"
for inc in source-date jobs; do cp "${here}/include/${inc}.bash" "${repo}/cicd/utility/include/"; done
realRepo="$(cd "${repo}" && pwd -P)"
## STUB_IMAGES the images there are, STUB_VOL_SRC the clone the volumes
## belong to (none: no volumes yet), STUB_NAMES the containers there are.
cat > "${scratch}/bin/docker" <<-'EOF'
	#!/usr/bin/env bash
	printf '%s\n' "$*" >> "$DOCKER_LOG"
	case "$1 ${2:-}" in
		"info "*)         exit 0 ;;
		"image inspect")  [[ " ${STUB_IMAGES:-} " == *" $3 "* ]]; exit ;;
		"volume inspect") [[ -n "${STUB_VOL_SRC:-}" ]] || exit 1; printf '%s\n' "$STUB_VOL_SRC"; exit 0 ;;
		"ps "*)           printf '%s\n' ${STUB_NAMES:-}; exit 0 ;;
		"build "*)        exit 1 ;;
	esac
	exit 0
EOF
printf '#!/usr/bin/env bash\nexit 1\n' > "${scratch}/bin/systemctl"
chmod +x "${scratch}/bin/"*

## $1 the label, the rest env. Sets out, rc and runLine.
out=""; rc=0; runLine=""
fRun(){
	local label="$1"; shift
	: > "$log"
	rc=0
	out="$(env -u CICD_NO_CONTAINER -u CICD_KEEP_TEMP -u CICD_RUN -u CICD_PROJECT -u NEMO_IMAGE -u STUB_VOL_SRC -u STUB_NAMES \
		PATH="${scratch}/bin:${PATH}" DOCKER_LOG="$log" SOURCE_DATE_EPOCH=946684800 CICD_MAX_JOBS=1 STUB_IMAGES="nemo-build-deps:init" "$@" \
		bash "${repo}/cicd/utility/docker-run.bash" "$label" 'echo the-step' 2>&1 < /dev/null)" || rc=$?
	runLine="$(grep '^run ' "$log" || true)"
}
inRun=(CICD_PROJECT=nemo-anywhere CICD_RUN=20261026-120000_001 STUB_VOL_SRC="$realRepo")

fRun "debug build" "${inRun[@]}"
[[ "$rc" == 0 ]] || fFail "[run]: exit ${rc}, said: ${out}"
[[ "$runLine" == "run --rm "* ]] || fFail "[run]: not docker run --rm: ${runLine:-no run}"
[[ "$runLine" == *" --name nemo-anywhere-20261026-120000_001-debug-build "* ]] || fFail "[run]: wrong name: ${runLine}"
[[ "$runLine" == *" --label dev.project=nemo-anywhere --label dev.run=20261026-120000_001 "* ]] || fFail "[run]: labels: ${runLine}"
[[ "$runLine" == *" -v nemo-anywhere-build:/build -v nemo-anywhere-build-fuzz:/build-fuzz "* ]] || fFail "[run]: cache volumes: ${runLine}"
[[ "$runLine" == *" -v ${repo}:/src "* ]] || fFail "[run]: the clone isn't /src: ${runLine}"
[[ "$runLine" == *" nemo-build-deps:init nice -n 19 sh -c "* ]] || fFail "[run]: image or nice: ${runLine}"
[[ "$runLine" == *"cache-cap.bash /build /build-fuzz || true; echo the-step" ]] || fFail "[run]: cache cap not before the step: ${runLine}"
[[ "$runLine" == *" -i "* ]] || fFail "[run]: stdin from a file not passed in: ${runLine}"
grep -q '^volume create' "$log" && fFail "[run]: made a volume that was there"

fRun "tests" "${inRun[@]}" CICD_KEEP_TEMP=1
[[ "$rc" == 0 && "$runLine" == "run "* && "$runLine" != *"--rm"* ]] || fFail "[--keep-temp]: kept no container: ${runLine:-no run}"

fRun "tests" "${inRun[@]}" STUB_NAMES="nemo-anywhere-20261026-120000_001-tests other-box"
[[ "$runLine" == *" --name nemo-anywhere-20261026-120000_001-tests-2 "* ]] || fFail "[name taken]: ${runLine:-no run}"

fRun "check" CICD_PROJECT=nemo-anywhere CICD_RUN=20261026-120000_001
[[ "$rc" == 0 ]] || fFail "[no volumes]: exit ${rc}, said: ${out}"
for vol in nemo-anywhere-build nemo-anywhere-build-fuzz; do
	grep -qxF "volume create --label dev.project=nemo-anywhere --label dev.run=20261026-120000_001 --label dev.src=${realRepo} ${vol}" "$log" \
		|| fFail "[no volumes]: ${vol} not made for this clone: $(grep '^volume' "$log" | tr '\n' ' ')"
done

fRun "check" "${inRun[@]}" STUB_VOL_SRC=/some/other/clone
[[ "$rc" == 1 && "$out" == *"FAILED: check: nemo-anywhere-build builds /some/other/clone, not ${realRepo}"* ]] || fFail "[other clone]: exit ${rc}, said: ${out}"
[[ -z "$runLine" ]] || fFail "[other clone]: ran anyway"

fRun "check" STUB_VOL_SRC="$realRepo"
[[ "$runLine" == *" --name nemo-anywhere-none-check-"[0-9]*" --label dev.project=nemo-anywhere --label dev.run=none "* ]] || fFail "[no run]: ${runLine:-no run}"

fRun "check" "${inRun[@]}" STUB_IMAGES="nemo-build-deps:latest"
[[ "$runLine" == *" nemo-build-deps:latest nice "* ]] || fFail "[fresh clone]: not the Dockerfile.dev image: ${runLine:-no run}"

## The cap, on the host, at 1 MB.
cache="${scratch}/cache"
mkdir -p "${cache}/big/sub" "${cache}/small" "${cache}/linked-to" "${scratch}/rel/keep"
head -c 2097152 /dev/urandom > "${cache}/big/sub/blob"
head -c 2097152 /dev/urandom > "${cache}/linked-to/blob"
printf 'x' > "${cache}/small/file"
ln -s "${cache}/linked-to" "${cache}/link"
head -c 2097152 /dev/urandom > "${scratch}/rel/keep/blob"
rc=0
out="$(cd "${scratch}" && CICD_CACHE_CAP_KB=1024 bash "${here}/../linux/cache-cap.bash" "${cache}/big" "${cache}/small" "${cache}/link" rel 2>&1)" || rc=$?
[[ "$rc" == 0 ]] || fFail "[cap]: exit ${rc}, said: ${out}"
[[ -d "${cache}/big" && -z "$(ls -A "${cache}/big")" ]] || fFail "[cap]: the dir past its cap wasn't emptied, or went itself"
[[ "$out" == *"${cache}/big cache was 2 MB, past its 1 MB cap, so it starts empty"* ]] || fFail "[cap]: said: ${out}"
[[ "$(grep -c 'starts empty' <<< "$out")" == 1 ]] || fFail "[cap]: not one line for the one dir: ${out}"
[[ -f "${cache}/small/file" ]] || fFail "[cap]: emptied a dir under its cap"
[[ -f "${cache}/linked-to/blob" ]] || fFail "[cap]: followed a link"
[[ -f "${scratch}/rel/keep/blob" ]] || fFail "[cap]: took a relative path"
rc=0; CICD_CACHE_CAP_KB='x[$(echo 1)]' bash "${here}/../linux/cache-cap.bash" "${cache}/small" > /dev/null 2>&1 || rc=$?
[[ "$rc" == 1 && -f "${cache}/small/file" ]] || fFail "[cap]: took a cap that isn't a number"

if ((failures)); then
	fEcho "FAILED: docker-run step containers, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: docker-run runs each step in a named container of its own, on this clone's cache volumes, under the cap"


##	History:
##		- 2026-10-26: Created.
