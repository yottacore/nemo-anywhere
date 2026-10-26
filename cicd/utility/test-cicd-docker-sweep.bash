#!/usr/bin/env bash

##	- Purpose: Check the docker cleanup at the end of a cicd.bash run. It
##	  removes the run's labeled containers but the long-lived ones the config
##	  names, the project's dangling images and its tags past the newest 2 of
##	  each repository, and build cache over a week old, then says what went in
##	  one line. A failed step and SIGTERM get the same. --keep-temp keeps the
##	  containers. Never rmi -f or a prune other than the build cache's. Off
##	  without DOCKER_SWEEP, and quiet when docker isn't there. A refused
##	  docker system df leaves the size unknown and the rest going. A copy of
##	  cicd.bash runs in a scratch dir with a stand-in docker.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-docker-sweep.bash
##	- Test ID: rm5b2v7n

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

# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
HOST_TEMP="${TMPDIR:-/tmp}"
fRunTemp_Make cicd-sweep-check "$(date +%Y%m%d-%H%M%S)" || { fEcho "FAILED: could not make a scratch folder"; exit 1; }
scratch="${runTempDir}"
trap 'fRunTemp_Done; rmdir "${HOST_TEMP}/cicd-sweep-check" 2> /dev/null || true' EXIT

lastOut=""
repo="${scratch}/repo"
log="${scratch}/docker.log"
mkdir -p "${repo}/cicd/utility/include" "${scratch}/bin" "${scratch}/host-temp"
cp "${root}/cicd/cicd.bash" "${repo}/cicd/"
for inc in gfs-rotate source-date run-temp docker-sweep jobs; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${repo}/cicd/utility/include/"
done
printf "project('app', 'c', version: '1.2.3')\n" > "${repo}/meson.build"
## The stand-in docker logs every call. system df gives 3.8 GB, then 2.5 GB.
cat > "${scratch}/bin/docker" <<-'EOF'
	#!/usr/bin/env bash
	printf '%s\n' "$*" >> "$DOCKER_LOG"
	case "$1" in
		info)   [[ -z "${DOCKER_DOWN:-}" ]]; exit ;;
		system) [[ -z "${DF_FAIL:-}" ]] || exit 1
		        if [[ -e "${DOCKER_LOG}.df" ]]; then printf '1GB\n0B\n0B\n1.5GB\n'; else touch "${DOCKER_LOG}.df"; printf '1.5GB\n300MB\n0B\n2GB\n'; fi ;;
		ps)     printf 'c1 app-run-step\nc2 app-box\n' ;;
		images) if [[ " $* " == *" -q "* ]]; then echo d1; else printf 'app-x new\napp-x prev\napp-x <none>\napp-x old\napp-y only\napp-x older\n'; fi ;;
	esac
	exit 0
EOF
chmod +x "${scratch}/bin/docker"
cat > "${repo}/step.bash" <<-'EOF'
	case "${STEP_DO:-}" in
		fail) exit 1 ;;
		term) kill -TERM "$PPID" ;;
	esac
	exit 0
EOF
cat > "${repo}/cicd/config.bash" <<-EOF
	APP_NAME="App"
	EXE_NAME="app"
	CICD_CONTAINERS=(app-box)
	DOCKER_SWEEP="\${STUB_SWEEP:-1}"
	BUILD_CROSS=0
	TEST_CMD=(bash "${repo}/step.bash")
EOF

## $1 what the step does, $2 the exit wanted, rest the options after the mode.
fRun(){
	local what="$1" want="$2" rc=0
	shift 2
	rm -f "$log" "${log}.df"
	lastOut="$(cd "$repo" && PATH="${scratch}/bin:${PATH}" DOCKER_LOG="$log" HOST_TEMP="${scratch}/host-temp" \
		STEP_DO="$what" CICD_CPU_CAPPED=1 bash cicd/cicd.bash --backlog-run-quick tests "$@" 2>&1 </dev/null)" || rc=$?
	[[ "$rc" == "$want" ]] || fFail "[${what:-ok} $*]: exit ${rc}, not ${want}: $(tail -3 <<< "$lastOut")"
	touch "$log"
}
fHas(){ grep -q -x -E -- "$2" "$log" || fFail "[$1]: no docker call like: $2"; }
fNot(){ ! grep -q -E -- "$2" "$log" || fFail "[$1]: a docker call like: $2"; }
fSwept(){
	local tag="$1"
	fHas "$tag" "ps -a --filter label=dev\.project=app --filter label=dev\.run=[0-9]{8}-[0-9]{6}_[0-9]{3} .*"
	fHas "$tag" "rm -f c1"
	fNot "$tag" "^rm .*c2"
	fHas "$tag" "rmi d1"
	fHas "$tag" "rmi app-x:old"
	fHas "$tag" "rmi app-x:older"
	fNot "$tag" "^rmi (app-x:new|app-x:prev|app-y:only)$"
	fNot "$tag" "^rmi .*-f|prune -a|system prune|image prune|container prune|volume prune"
	fHas "$tag" "builder prune -f --filter until=168h"
	[[ "$lastOut" == *"[ docker: removed 1 container(s) and 3 image(s), and build cache over a week old; about 1.3 GB freed ]"* ]] \
		|| fFail "[${tag}]: no summary line: $(tail -3 <<< "$lastOut")"
}

fRun "" 0;      fSwept ok
fRun fail 1;    fSwept fail
fRun term 143;  fSwept term
fRun "" 0 --keep-temp
fNot keep "^rm "
fHas keep "rmi d1"
STUB_SWEEP=0 fRun "" 0
fNot off "^(system|ps|rm|rmi|images|builder) "
DOCKER_DOWN=1 fRun "" 0
fNot down "^(system|ps|rm|rmi|images|builder) "
[[ "$lastOut" != *"[ docker:"* ]] || fFail "[down]: printed a summary"
## A df the daemon refuses leaves the size unknown, and the rest goes ahead.
DF_FAIL=1 fRun "" 0
fHas df "rm -f c1"
[[ "$lastOut" == *"[ docker: removed 1 container(s) and 3 image(s), and build cache over a week old; size not known ]"* ]] \
	|| fFail "[df]: no summary line with the size not known: $(tail -3 <<< "$lastOut")"
runName="$(sed -n 's/.*label=dev\.run=\([^ ]*\).*/\1/p' "$log" | head -1)"
[[ -n "$runName" && ! -e "${scratch}/host-temp/app/${runName}" ]] || fFail "[df]: the run folder ${runName} is still there"

if ((failures)); then
	fEcho "FAILED: docker cleanup, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: a run removes its own containers, the project's old images and old build cache, and nothing else"
