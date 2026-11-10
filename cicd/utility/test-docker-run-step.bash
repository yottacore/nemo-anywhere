#!/usr/bin/env bash

##	- Purpose: Check that docker-run.bash runs each step in a container of its
##	  own: named <project>-<run folder>-<step>, labeled, with --rm unless
##	  CICD_KEEP_TEMP=1, the next number when the name is taken, the 2 cache
##	  volumes made for this clone and refused for another, a set of its own
##	  for a linked worktree or CICD_VOLUME_PREFIX, the cache cap
##	  run before the step, and a memory cap with no swap, set by CICD_MEM_CAP.
##	  A container over its cap goes first when the host runs short. The image is tagged by a hash of Dockerfile.dev, and
##	  built from it with the labels when that tag isn't there, so an edit to the
##	  recipe gives a new image. A copy runs in a scratch dir with a stand-in docker.
##	  Then cache-cap.bash empties a dir past its cap, keeps one under it, and
##	  leaves a link or a relative path alone.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-docker-run-step.bash
##	- Test ID: defmw

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
HOST_TEMP="${TMPDIR:-/tmp}"
fRunTemp_Make docker-run-step-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: couldn't make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/docker-run-step-check" 2> /dev/null || true' EXIT

repo="${scratch}/repo"
log="${scratch}/docker.log"
mkdir -p "${repo}/cicd/utility/include" "${repo}/cicd/linux" "${scratch}/bin"
cp "${here}/docker-run.bash" "${repo}/cicd/utility/"
cp "${here}/../linux/Dockerfile.dev" "${repo}/cicd/linux/"
hash="$(sha256sum < "${repo}/cicd/linux/Dockerfile.dev")"; tag="nemo-build-deps:${hash:0:12}"
for inc in source-date jobs step-box; do cp "${here}/include/${inc}.bash" "${repo}/cicd/utility/include/"; done
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
		"build "*)        [[ -n "${STUB_BUILD_OK:-}" ]]; exit ;;
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
	out="$(env -u CICD_NO_CONTAINER -u CICD_KEEP_TEMP -u CICD_RUN -u CICD_PROJECT -u NEMO_IMAGE -u CICD_VOLUME_PREFIX -u CICD_MEM_CAP -u STUB_VOL_SRC -u STUB_NAMES -u STUB_BUILD_OK \
		PATH="${scratch}/bin:${PATH}" DOCKER_LOG="$log" SOURCE_DATE_EPOCH=946684800 CICD_MAX_JOBS=1 STUB_IMAGES="${tag}" "$@" \
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
[[ "$runLine" == *" ${tag} nice -n 19 sh -c "* ]] || fFail "[run]: image or nice: ${runLine}"
grep -q '^build ' "$log" && fFail "[run]: built an image that was there"
[[ "$runLine" == *"cache-cap.bash /build /build-fuzz || true; echo the-step" ]] || fFail "[run]: cache cap not before the step: ${runLine}"
[[ "$runLine" == *" -i "* ]] || fFail "[run]: stdin from a file not passed in: ${runLine}"
grep -q '^volume create' "$log" && fFail "[run]: made a volume that was there"
[[ "$runLine" =~ " --cpu-shares 256 --memory "([0-9]+)g" --memory-swap "([0-9]+)g" --oom-score-adj 1000 --name " \
	&& "${BASH_REMATCH[1]}" == "${BASH_REMATCH[2]}" && "${BASH_REMATCH[1]}" -ge 4 && "${BASH_REMATCH[1]}" -le 32 ]] \
	|| fFail "[run]: no memory cap of 4g to 32g with no swap: ${runLine}"

fRun "tests" "${inRun[@]}" CICD_MEM_CAP=7g
[[ "$runLine" == *" --memory 7g --memory-swap 7g --oom-score-adj 1000 "* ]] || fFail "[CICD_MEM_CAP]: ${runLine:-no run}"
fRun "tests" "${inRun[@]}" CICD_MEM_CAP=none
[[ "$rc" == 0 && "$runLine" == *" --cpu-shares 256 --name "* ]] || fFail "[CICD_MEM_CAP=none]: exit ${rc}: ${runLine:-no run}"
# shellcheck disable=SC2016  ## a cap that would run code if read as a number.
fRun "tests" "${inRun[@]}" CICD_MEM_CAP='x[$(echo 1)]'
[[ "$rc" == 1 && "$out" == *"FAILED: CICD_MEM_CAP 'x[\$(echo 1)]' isn't a size"* && -z "$runLine" ]] || fFail "[CICD_MEM_CAP bad]: exit ${rc}, said: ${out}"

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

fRun "check" "${inRun[@]}" CICD_VOLUME_PREFIX=pick
[[ "$runLine" == *" -v pick-build:/build -v pick-build-fuzz:/build-fuzz "* ]] || fFail "[CICD_VOLUME_PREFIX]: ${runLine:-no run}"

## A linked worktree's .git is a file.
printf 'gitdir: /elsewhere\n' > "${repo}/.git"
fRun "check" "${inRun[@]}"
[[ "$runLine" == *" -v nemo-anywhere-wt-repo-build:/build -v nemo-anywhere-wt-repo-build-fuzz:/build-fuzz "* ]] || fFail "[worktree]: ${runLine:-no run}"
rm -f "${repo}/.git"

fRun "check" "${inRun[@]}" STUB_IMAGES="nemo-build-deps:init nemo-build-deps:latest" STUB_BUILD_OK=1
grep -qxF "build --label dev.project=nemo-anywhere --label dev.run=20261026-120000_001 -t ${tag} -f ${repo}/cicd/linux/Dockerfile.dev ${repo}/cicd/linux/" "$log" \
	|| fFail "[fresh clone]: not built from Dockerfile.dev: $(grep '^build' "$log" || echo no build)"
[[ "$runLine" == *" ${tag} nice "* ]] || fFail "[fresh clone]: not the Dockerfile.dev image: ${runLine:-no run}"

printf '\n# moved on\n' >> "${repo}/cicd/linux/Dockerfile.dev"
fRun "check" "${inRun[@]}" STUB_BUILD_OK=1
[[ "$runLine" != *" ${tag} "* && "$runLine" == *" nemo-build-deps:"????????????" nice "* ]] || fFail "[recipe edit]: the old tag still runs: ${runLine:-no run}"
grep -q "^build .* -t nemo-build-deps:" "$log" || fFail "[recipe edit]: no new image built"

fRun "check" "${inRun[@]}" NEMO_IMAGE=nemo-build-deps:init STUB_IMAGES=nemo-build-deps:init
[[ "$runLine" == *" nemo-build-deps:init nice "* ]] || fFail "[NEMO_IMAGE]: not the image named: ${runLine:-no run}"

## The cap, on the host, at 1 MB.
cache="${scratch}/cache"
mkdir -p "${cache}/big/sub" "${cache}/small" "${cache}/linked-to" "${scratch}/rel/keep"
head -c 2097152 /dev/urandom > "${cache}/big/sub/blob"
head -c 2097152 /dev/urandom > "${cache}/linked-to/blob"
printf 'x' > "${cache}/small/file"
head -c 2097152 /dev/urandom > "${scratch}/rel/keep/blob"
capArgs=("${cache}/big" "${cache}/small" "${cache}/link" rel)
## MSYS2's ln -s copies the dir when it can't make a link, which is a dir of
## its own past the cap. Leave that case out there.
ln -s "${cache}/linked-to" "${cache}/link" 2> /dev/null || true
if [[ ! -L "${cache}/link" ]]; then
	echo "[ cap: link case skipped: ln -s made a copy, no symlink privilege here ]"
	capArgs=("${cache}/big" "${cache}/small" rel)
fi
rc=0
out="$(cd "${scratch}" && CICD_CACHE_CAP_KB=1024 bash "${here}/../linux/cache-cap.bash" "${capArgs[@]}" 2>&1)" || rc=$?
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
##		- 2026-10-28: The image is tagged by the recipe's hash, and built from it.
##		- 2026-10-31: The link case is left out where ln -s makes a copy.
##		- 2026-11-09: The memory cap.
