#!/usr/bin/env bash

##	- Purpose: Check the Linux side of the FreeBSD lane, with no FreeBSD box.
##	  A copy of lane.bash runs in a scratch repo, with an ssh and scp on PATH
##	  that run the box's side in a scratch home here, and stand-ins for the two
##	  scripts the box would run.
##	  - What is sent is the tracked and untracked files, not the ignored ones,
##	    and nothing from an earlier run is left beside them.
##	  - --tests hands run-tests.bash its build dir and job count, and fails
##	    when the suite does.
##	  - --release brings the pkg back and packs the prefix as the -bsd- tarball
##	    with the commit's stamp on every entry, so two runs give the same bytes
##	    whatever times the box left on the files. The sums file covers both.
##	  - The run goes through the host lock's wrap, for vmFreeBSD, and through
##	    the VM state helper's when that is on the box too.
##	  - A VM still booting is waited for. One that never answers, or a box that
##	    isn't FreeBSD, fails before anything is sent, the second without the
##	    wait. A box that isn't there, and isn't a VM here, exits 77 before the
##	    lock is taken.
##	  - A box whose clock is hours behind, as a VM resumed from paused is,
##	    fails before anything is sent, and so does one that is ahead. One whose
##	    clock is set a moment after the resume is waited for.
##	- Needs git and python3; exit 77 without them.
##	- Runs in the lint stage.
##	- Syntax: cicd/bsd/test-lane.bash
##	- Test ID: ddmn7

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../utility/include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"

if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "FreeBSD lane check skipped: Linux only"; exit 77; fi
for tool in git python3; do
	if ! command -v "$tool" >/dev/null 2>&1; then fEcho "FreeBSD lane check skipped: no ${tool}"; exit 77; fi
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/bsd-lane-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_CONFIG_COUNT GIT_DIR GIT_WORK_TREE

repo="${scratch}/repo"
mkdir -p "${repo}/cicd/bsd" "${repo}/cicd/utility/include" "${repo}/source"
cp "${root}/cicd/bsd/lane.bash" "${repo}/cicd/bsd/"
cp "${root}/cicd/utility/include/echo.bash" "${root}/cicd/utility/include/source-date.bash" \
	"${root}/cicd/utility/include/release-files.bash" "${root}/cicd/utility/include/box-wrap.bash" \
	"${repo}/cicd/utility/include/"
printf "project('nemo-anywhere', 'c', version : '9.9.9-rc1')\n" > "${repo}/source/meson.build"
printf 'cicd/artifacts/\nignored.txt\n' > "${repo}/.gitignore"
printf 'tracked\n' > "${repo}/tracked.txt"
commitDate=1790000000
fGit(){ git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null "$@"; }
fGit init -q -b dev
fGit add -A
GIT_AUTHOR_DATE="@${commitDate}" GIT_COMMITTER_DATE="@${commitDate}" fGit commit -q -m base
printf 'untracked\n' > "${repo}/untracked.txt"
printf 'ignored\n' > "${repo}/ignored.txt"

## The box's side: a home here, a uname that says FreeBSD, and the two scripts
## the lane runs there swapped for stand-ins.
box="${scratch}/box"
shim="${scratch}/shim"
mkdir -p "$box" "$shim" "${scratch}/boxbin"
cat > "${scratch}/boxbin/uname" <<'EOF'
#!/usr/bin/env bash
case "${1:-}" in -s) echo "${FAKE_OS:-FreeBSD}" ;; -m) echo amd64 ;; *) echo "${FAKE_OS:-FreeBSD}" ;; esac
EOF
## FAKE_CLOCK_BEHIND=S: the box's clock is S seconds behind, for its first
## FAKE_CLOCK_READS reads, or all of them.
realDate="$(command -v date)"
cat > "${scratch}/boxbin/date" <<EOF
#!/usr/bin/env bash
if [[ -n "\${FAKE_CLOCK_BEHIND:-}" && "\${1:-}" == +%s ]]; then
	n=\$(( \$(cat "${scratch}/clock-reads" 2>/dev/null || echo 0) + 1 ))
	echo "\$n" > "${scratch}/clock-reads"
	if [[ -z "\${FAKE_CLOCK_READS:-}" ]] || (( n <= FAKE_CLOCK_READS )); then echo \$(( \$("${realDate}" +%s) - FAKE_CLOCK_BEHIND )); exit 0; fi
fi
exec "${realDate}" "\$@"
EOF
cat > "${scratch}/fake-run-tests.bash" <<'EOF'
#!/usr/bin/env bash
printf 'BUILD_DIR=%s NEMO_TEST_JOBS=%s\n' "${BUILD_DIR:-}" "${NEMO_TEST_JOBS:-}" > "${HOME}/run-tests.log"
exit "${FAKE_TESTS_RC:-0}"
EOF
## Leaves the prefix and a pkg the way release.bash does, with whatever file
## times the box happens to have.
cat > "${scratch}/fake-release.bash" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
printf '%s\n' "${SOURCE_DATE_EPOCH:-}" > "${HOME}/release-sde.log"
out="cicd/artifacts/release"
name="nemo-anywhere-9.9.9-rc1-bsd-x86_64"
mkdir -p "${out}/${name}/bin" "${out}/${name}/share/b" "${out}/${name}/share/a"
printf '#!/bin/sh\necho bsd\n' > "${out}/${name}/bin/nemo-anywhere"
chmod 755 "${out}/${name}/bin/nemo-anywhere"
printf 'a\n' > "${out}/${name}/share/a/f"
printf 'b\n' > "${out}/${name}/share/b/f"
touch -d "@$(( $(date +%s) - RANDOM ))" "${out}/${name}/share/a/f"
printf 'pkg\n' > "${out}/${name}.pkg"
EOF
## FAKE_SSH_DOWN=N: the first N connections time out, as on a box still booting.
cat > "${shim}/ssh" <<EOF
#!/usr/bin/env bash
if [[ -n "\${FAKE_SSH_DOWN:-}" ]]; then
	n=\$(( \$(cat "${scratch}/ssh-tries" 2>/dev/null || echo 0) + 1 ))
	echo "\$n" > "${scratch}/ssh-tries"
	if (( n <= FAKE_SSH_DOWN )); then echo "ssh: connect to host box port 22: Connection timed out" >&2; exit 255; fi
fi
while [[ "\${1:-}" == -* ]]; do shift; [[ "\${1:-}" != -* ]] && shift; done
shift
cmd="\$*"
cmd="\${cmd//bash cicd\/linux\/run-tests.bash/bash ${scratch}/fake-run-tests.bash}"
cmd="\${cmd//bash cicd\/bsd\/release.bash/bash ${scratch}/fake-release.bash}"
cd "${box}" && HOME="${box}" PATH="${scratch}/boxbin:\${PATH}" bash -c "\$cmd"
EOF
cat > "${shim}/scp" <<EOF
#!/usr/bin/env bash
args=(); for a in "\$@"; do case "\$a" in -q) ;; *) args+=("\$a") ;; esac; done
set -- "\${args[@]}"
while [[ "\${1:-}" == -o ]]; do shift 2; done
cp "${box}/\${1#*:}" "\$2"
EOF
cat > "${shim}/lock.bash" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "${scratch}/lock.log"
while [[ "\${1:-}" != -- ]]; do shift; done
shift
exec "\$@"
EOF
## The VM state helper: its status row says whether the box is a VM here.
cat > "${shim}/x_vm-state.bash" <<EOF
#!/usr/bin/env bash
if [[ "\${1:-}" == status ]]; then printf 'host guest state note\n%s %s %s\n' "\$2" "\$2" "\${FAKE_VM_STATE:--}"; exit 0; fi
printf '%s\n' "\$*" >> "${scratch}/vm-state.log"
while [[ "\${1:-}" != -- ]]; do shift; done
shift
exec "\$@"
EOF
chmod +x "${shim}/ssh" "${shim}/scp" "${scratch}/boxbin/uname" "${scratch}/boxbin/date"

fLane(){ (cd "$repo" && env -u SOURCE_DATE_EPOCH PATH="${shim}:${PATH}" WINDOWS_HOST_LOCK="${shim}/lock.bash" bash cicd/bsd/lane.bash "$@") >"${scratch}/lane.log" 2>&1; }

## Left from an earlier run; must not survive the send.
mkdir -p "${box}/nemo-anywhere-ci/src"
printf 'stale\n' > "${box}/nemo-anywhere-ci/src/stale.txt"

if fLane --tests; then fEcho "OK: --tests ran"; else fFail "--tests failed: $(tail -3 "${scratch}/lane.log")"; fi
src="${box}/nemo-anywhere-ci/src"
[[ -f "${src}/tracked.txt" && -f "${src}/untracked.txt" ]] || fFail "tracked or untracked file not sent"
[[ ! -e "${src}/ignored.txt" ]] || fFail "an ignored file was sent"
[[ ! -e "${src}/stale.txt" ]] || fFail "a file from an earlier run was left in the source dir"
if [[ "$(cat "${box}/run-tests.log" 2>/dev/null || true)" == "BUILD_DIR=${box}/nemo-anywhere-ci/build NEMO_TEST_JOBS=4" ]]; then
	fEcho "OK: run-tests.bash got its build dir and jobs"
else
	fFail "run-tests.bash got '$(cat "${box}/run-tests.log" 2>/dev/null || true)'"
fi
grep -q -E -- '^wrap vmFreeBSD .*-- bash .*lane\.bash --tests --locked$' "${scratch}/lock.log" \
	|| fFail "--tests did not go through the lock's wrap for vmFreeBSD: $(cat "${scratch}/lock.log")"

## With both tools on the box and neither named, the VM state helper is the one
## used, since only it starts a VM that is off.
toolDir="${scratch}/home/synced/0-0/common/exec/util/linux/bash"
mkdir -p "$toolDir"
cp "${shim}/x_vm-state.bash" "${toolDir}/x_vm-state.bash"
sed "s|lock\.log|host-lock.log|" "${shim}/lock.bash" > "${toolDir}/x_windows-host-lock.bash"
if (cd "$repo" && env -u SOURCE_DATE_EPOCH -u WINDOWS_HOST_LOCK -u BOX_WRAP_TOOL HOME="${scratch}/home" PATH="${shim}:${PATH}" \
	bash cicd/bsd/lane.bash --tests) >"${scratch}/lane.log" 2>&1; then
	if grep -q -E -- '^wrap vmFreeBSD ' "${scratch}/vm-state.log" 2>/dev/null && [[ ! -e "${scratch}/host-lock.log" ]]; then
		fEcho "OK: the VM state helper is picked over the bare host lock"
	else
		fFail "the VM state helper was not the one used"
	fi
else
	fFail "--tests through the VM state helper failed: $(tail -3 "${scratch}/lane.log")"
fi

## A VM booting, then up: the lane waits rather than failing on the first try.
vmTool="${shim}/x_vm-state.bash"
rm -f "${scratch}/ssh-tries"
if BOX_WRAP_TOOL="$vmTool" FAKE_VM_STATE=off FAKE_SSH_DOWN=2 BOX_READY_POLL=0 fLane --tests; then
	if grep -q 'answered after' "${scratch}/lane.log"; then fEcho "OK: a box still booting is waited for"; else fFail "no line saying the box was waited for"; fi
else
	fFail "--tests failed on a box that came up on the third try: $(tail -3 "${scratch}/lane.log")"
fi

## Never answers, or isn't FreeBSD: both fail before the send.
printf 'stale\n' > "${box}/nemo-anywhere-ci/src/stale.txt"
rm -f "${scratch}/ssh-tries"
if BOX_WRAP_TOOL="$vmTool" FAKE_VM_STATE=off FAKE_SSH_DOWN=99 BOX_READY_POLL=1 NEMO_BSD_READY_WAIT=1 fLane --tests; then fFail "--tests passed on a box that never answered"
elif ! grep -q 'did not answer' "${scratch}/lane.log"; then fFail "no reason for a box that never answered: $(tail -3 "${scratch}/lane.log")"
else fEcho "OK: a box that never answers fails"
fi
rm -f "${scratch}/ssh-tries"
startedAt=$SECONDS
if FAKE_OS=Linux BOX_READY_POLL=5 NEMO_BSD_READY_WAIT=30 fLane --tests; then fFail "--tests passed on a box that isn't FreeBSD"
elif ! grep -q 'is not a FreeBSD box' "${scratch}/lane.log"; then fFail "no reason for a box that isn't FreeBSD: $(tail -3 "${scratch}/lane.log")"
elif ((SECONDS - startedAt >= 5)); then fFail "a box that isn't FreeBSD was waited on"
else fEcho "OK: a box that isn't FreeBSD fails without the wait"
fi
[[ -e "${box}/nemo-anywhere-ci/src/stale.txt" ]] || fFail "the tree on a box that failed the check was touched"

## Resumed from paused with its old clock: fails before the send, rather than
## meson stopping on clock skew there. Ahead fails the same way.
for behind in 60941 -3600; do
	rm -f "${scratch}/clock-reads"
	if BOX_WRAP_TOOL="$vmTool" FAKE_VM_STATE=paused FAKE_CLOCK_BEHIND="$behind" BOX_CLOCK_WAIT=1 BOX_READY_POLL=1 fLane --tests; then fFail "--tests passed on a box ${behind}s off"
	elif ! grep -q 'has the wrong time' "${scratch}/lane.log" || ! grep -q -E 's (behind|ahead), still after' "${scratch}/lane.log"; then fFail "no reason for a box ${behind}s off: $(tail -3 "${scratch}/lane.log")"
	else fEcho "OK: a box ${behind}s off fails"
	fi
done
[[ -e "${box}/nemo-anywhere-ci/src/stale.txt" ]] || fFail "the tree on a box with the wrong time was touched"

## Its clock set a moment after the resume: waited for, then the suite runs.
rm -f "${scratch}/clock-reads" "${box}/run-tests.log"
: > "${scratch}/vm-state.log"
if BOX_WRAP_TOOL="$vmTool" FAKE_VM_STATE=paused FAKE_CLOCK_BEHIND=60941 FAKE_CLOCK_READS=2 BOX_READY_POLL=0 fLane --tests; then
	if ! grep -q 'clock is right after' "${scratch}/lane.log"; then fFail "no line saying the clock was waited for"
	elif ! grep -q -E -- '^wrap vmFreeBSD ' "${scratch}/vm-state.log" || [[ ! -f "${box}/run-tests.log" ]]; then fFail "the suite did not run inside the VM state helper's wrap"
	else fEcho "OK: a clock set after the resume is waited for"
	fi
else
	fFail "--tests failed on a box whose clock was set after two reads: $(tail -3 "${scratch}/lane.log")"
fi
printf 'stale\n' > "${box}/nemo-anywhere-ci/src/stale.txt"

## Not there, and not a VM here: a skip, before the lock.
rm -f "${scratch}/ssh-tries"
locks="$(wc -l < "${scratch}/lock.log")"
rc=0; FAKE_SSH_DOWN=99 fLane --tests || rc=$?
if [[ "$rc" != 77 ]]; then fFail "a box that isn't there gave exit ${rc}, not 77: $(tail -3 "${scratch}/lane.log")"
elif ! grep -q "SKIP: bsdtest@vmFreeBSD isn't there" "${scratch}/lane.log"; then fFail "no SKIP line for a box that isn't there"
elif [[ "$(wc -l < "${scratch}/lock.log")" != "$locks" ]]; then fFail "the lock was taken for a box that isn't there"
else fEcho "OK: a box that isn't there is a skip"
fi
[[ -e "${box}/nemo-anywhere-ci/src/stale.txt" ]] || fFail "the tree was touched on a box that isn't there"

if FAKE_TESTS_RC=1 fLane --tests; then fFail "--tests passed with a failing suite"; else fEcho "OK: a failing suite fails --tests"; fi

art="${repo}/cicd/artifacts/release"
name="nemo-anywhere-9.9.9-rc1-bsd-x86_64"
sums=()
for run in 1 2; do
	if ! fLane --release; then fFail "--release run ${run} failed: $(tail -3 "${scratch}/lane.log")"; continue; fi
	sums+=("$(sha256sum "${art}/${name}.tar.gz" | cut -d' ' -f1)")
done
[[ "$(cat "${box}/release-sde.log" 2>/dev/null || true)" == "$commitDate" ]] || fFail "the box was not handed the commit's stamp"
[[ -f "${art}/${name}.pkg" ]] || fFail "no pkg brought back"
[[ ! -e "${art}/${name}" ]] || fFail "the prefix that came back was left in the release dir"
if [[ "${#sums[@]}" == 2 && "${sums[0]}" == "${sums[1]}" ]]; then fEcho "OK: two release runs give the same tarball"
else fFail "the tarball differs between runs: ${sums[*]}"
fi
python3 - "${art}/${name}.tar.gz" "$name" "$commitDate" <<'EOF' || fFail "tarball contents"
import sys, tarfile
path, top, stamp = sys.argv[1], sys.argv[2], int(sys.argv[3])
with tarfile.open(path) as tar:
    members = tar.getmembers()
names = [m.name for m in members]
bad = [m.name for m in members if m.mtime != stamp or m.uid != 0 or m.gid != 0]
problems = []
if bad:
    problems.append("not at the commit's stamp and owner 0: %s" % bad[:3])
if names != sorted(names):
    problems.append("entries not sorted: %s" % names)
if not all(n == top or n.startswith(top + "/") for n in names):
    problems.append("not all under one top folder %s" % top)
if top + "/bin/nemo-anywhere" not in names:
    problems.append("no bin/nemo-anywhere")
for p in problems:
    print("[ FAILED: tarball %s ]" % p)
sys.exit(1 if problems else 0)
EOF
sumsFile="${art}/nemo-anywhere-9.9.9-rc1-sha256sums.txt"
for f in "${name}.tar.gz" "${name}.pkg"; do
	grep -q -F "  ${f}" "$sumsFile" 2>/dev/null || fFail "no sums line for ${f}"
done
if (cd "$art" && sha256sum -c --quiet "${sumsFile##*/}"); then fEcho "OK: sums file verifies"; else fFail "sums file does not verify"; fi

if ((failures)); then fEcho "${failures} FreeBSD lane check(s) failed"; exit 1; fi
fEcho "FreeBSD lane checks passed"
