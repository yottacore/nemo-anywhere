#!/usr/bin/env bash

##	- Purpose: Check what docker-run.bash does with no container to run in. With
##	  the daemon down or the image not built, it says so in one line and runs the
##	  command on the host, with CICD_SRC at the clone and CICD_BUILD_ROOT under
##	  its build/host, and the command's exit code is its own. A host without the
##	  build stack stops it with one line and exit 1, running nothing. With
##	  CICD_NO_CONTAINER=1 it stops at once, and never calls docker. A copy runs
##	  in a scratch dir with a stand-in docker, meson, ninja and pkg-config.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-docker-run-host.bash
##	- Test ID: defky

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
fRunTemp_Make docker-run-host-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: could not make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/docker-run-host-check" 2> /dev/null || true' EXIT

repo="${scratch}/repo"
log="${scratch}/docker.log"
mkdir -p "${repo}/cicd/utility/include" "${repo}/cicd/linux" "${scratch}/bin"
cp "${here}/docker-run.bash" "${repo}/cicd/utility/"
cp "${here}/../linux/Dockerfile.dev" "${repo}/cicd/linux/"
for inc in source-date jobs step-box; do cp "${here}/include/${inc}.bash" "${repo}/cicd/utility/include/"; done
## DOCKER_MODE down: no daemon. nobuild: a daemon, but no container, no image,
## and a build that fails.
cat > "${scratch}/bin/docker" <<-'EOF'
	#!/usr/bin/env bash
	printf '%s\n' "$*" >> "$DOCKER_LOG"
	case "$1" in
		info)  [[ "${DOCKER_MODE:-}" != down ]]; exit ;;
		ps)    exit 0 ;;
		image) exit 1 ;;
		build) exit 1 ;;
	esac
	exit 0
EOF
# shellcheck disable=SC2016  ## expanded by the stand-in when it runs
printf '#!/usr/bin/env bash\n[[ "$*" != *gtk+-3.0* || -n "${HOST_GTK:-}" ]]\n' > "${scratch}/bin/pkg-config"
printf '#!/usr/bin/env bash\nexit 0\n' > "${scratch}/bin/meson"
cp "${scratch}/bin/meson" "${scratch}/bin/ninja"
## So the nudge never reaches the real user services.
printf '#!/usr/bin/env bash\nexit 1\n' > "${scratch}/bin/systemctl"
chmod +x "${scratch}/bin/"*

## $1 the case, $2 the command, the rest env. Sets out and rc.
out=""; rc=0
fRun(){
	local cmd="$2"; shift 2
	: > "$log"; rm -f "${scratch}/ran"
	rc=0
	out="$(env -u CICD_NO_CONTAINER -u CICD_SRC -u CICD_BUILD_ROOT -u HOST_GTK \
		PATH="${scratch}/bin:${PATH}" DOCKER_LOG="$log" SOURCE_DATE_EPOCH=946684800 CICD_MAX_JOBS=1 "$@" \
		bash "${repo}/cicd/utility/docker-run.bash" check "$cmd" 2>&1)" || rc=$?
}
mark="echo \"\${CICD_SRC}|\${CICD_BUILD_ROOT}\" > ${scratch}/ran"

fRun "no-container" "$mark" CICD_NO_CONTAINER=1 DOCKER_MODE=down HOST_GTK=1
[[ "$rc" == 1 && "$out" == *"STOPPED: check needs a container, and --no-container runs on the host"* ]] || fFail "[--no-container]: exit ${rc}, said: ${out}"
[[ ! -e "${scratch}/ran" ]] || fFail "[--no-container]: the command ran"
[[ ! -s "$log" ]] || fFail "[--no-container]: called docker: $(head -3 "$log")"

fRun "down, no GTK" "$mark" DOCKER_MODE=down
[[ "$rc" == 1 && "$out" == *"STOPPED: check: docker daemon not reachable"*"and the host has no gtk+-3.0 to run it on"* ]] || fFail "[down, no GTK]: exit ${rc}, said: ${out}"
[[ ! -e "${scratch}/ran" ]] || fFail "[down, no GTK]: the command ran"

fRun "down" "$mark" DOCKER_MODE=down HOST_GTK=1
[[ "$rc" == 0 && "$out" == *"check: docker daemon not reachable"*", so it runs on the host"* ]] || fFail "[down]: exit ${rc}, said: ${out}"
[[ "$(grep -c 'runs on the host' <<< "$out")" == 1 ]] || fFail "[down]: not one line saying so: ${out}"
[[ "$(cat "${scratch}/ran" 2>/dev/null)" == "${repo}|${repo}/build/host" ]] || fFail "[down]: ran with $(cat "${scratch}/ran" 2>/dev/null || echo nothing), not ${repo}|${repo}/build/host"

fRun "down, failing" "exit 5" DOCKER_MODE=down HOST_GTK=1
[[ "$rc" == 5 ]] || fFail "[down, failing]: exit ${rc}, not the command's 5"

fRun "no image" "$mark" DOCKER_MODE=nobuild HOST_GTK=1
[[ "$rc" == 0 && "$out" == *"check: could not build image nemo-build-deps:"*", so it runs on the host"* ]] || fFail "[no image]: exit ${rc}, said: ${out}"
[[ -e "${scratch}/ran" ]] || fFail "[no image]: the command didn't run"
grep -q '^build ' "$log" || fFail "[no image]: never tried to build the image"

if ((failures)); then
	fEcho "FAILED: docker-run host fallback, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: docker-run runs on the host without a container, stops without the build stack or under --no-container"
