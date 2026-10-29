#!/usr/bin/env bash

##	- Purpose: Check which box the Windows dogfood refresh takes, with no box.
##	  A copy of win-dogfood.bash runs in a scratch repo, with an ssh, scp and VM
##	  state helper on PATH that only log what they're asked.
##	  - b29w goes first, by that name.
##	  - With b29w gone, vm925w is taken through the VM state helper when it's a
##	    VM here, even while it's off, and its login is waited for.
##	  - With neither there, the refresh is skipped, not failed.
##	- Needs git; exit 77 without it.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-win-dogfood.bash
##	- Test ID: dehyb

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=include/run-temp.bash
source "${root}/cicd/utility/include/run-temp.bash"

if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "Windows dogfood check skipped: Linux only"; exit 77; fi
if ! command -v git >/dev/null 2>&1; then fEcho "Windows dogfood check skipped: no git"; exit 77; fi

scratch="$(mktemp -d "${TMPDIR:-/tmp}/win-dogfood-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_CONFIG_COUNT GIT_DIR GIT_WORK_TREE

repo="${scratch}/repo"
log="${scratch}/calls.log"
mkdir -p "${repo}/cicd/utility/include" "${scratch}/bin"
cp "${root}/cicd/utility/win-dogfood.bash" "${repo}/cicd/utility/"
cp "${root}/cicd/utility/include/box-wrap.bash" "${root}/cicd/utility/include/jobs.bash" "${repo}/cicd/utility/include/"
git -C "$repo" -c user.name=test -c user.email=test@example.invalid init -q
git -C "$repo" add -A
git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null commit -q -m base

## FAKE_GONE: the hosts that never answer. FAKE_BOOTING: the first N
## connections time out, as on a VM still booting.
cat > "${scratch}/bin/ssh" <<-EOF
	#!/usr/bin/env bash
	while [[ "\${1:-}" == -o ]]; do shift 2; done
	host="\$1"; shift
	if [[ " \${FAKE_GONE:-} " == *" \${host} "* ]]; then echo "ssh: connect to host \${host} port 22: Connection timed out" >&2; exit 255; fi
	if [[ -n "\${FAKE_BOOTING:-}" ]]; then
		n=\$(( \$(cat "${scratch}/tries" 2>/dev/null || echo 0) + 1 ))
		echo "\$n" > "${scratch}/tries"
		(( n > FAKE_BOOTING )) || { echo "ssh: connect to host \${host} port 22: Connection refused" >&2; exit 255; }
	fi
	echo "ssh \${host} \$*" >> "${log}"
EOF
cat > "${scratch}/bin/scp" <<-EOF
	#!/usr/bin/env bash
	echo "scp \$*" >> "${log}"
EOF
## FAKE_VMS: the hosts that are VMs here, all of them off.
cat > "${scratch}/x_vm-state.bash" <<-EOF
	if [[ "\${1:-}" == status ]]; then
		state=-; [[ " \${FAKE_VMS:-} " == *" \$2 "* ]] && state=off
		printf 'host guest state note\n%s %s %s\n' "\$2" "\$2" "\$state"; exit 0
	fi
	echo "lock \$*" >> "${log}"
	while ((\$#)) && [[ "\$1" != -- ]]; do shift; done
	shift
	exec "\$@"
EOF
cp "${scratch}/x_vm-state.bash" "${scratch}/x_windows-host-lock.bash"
chmod +x "${scratch}/bin/"*

## $1 the lock tool, rest env settings.
fDogfood(){
	local tool="$1"; shift
	rm -f "$log" "${scratch}/tries"; touch "$log"
	## A home of its own, so the real lock tools are never found.
	(cd "$repo" && env -u WINDOWS_HOST_LOCK "$@" HOME="${scratch}/home" PATH="${scratch}/bin:${PATH}" BOX_WRAP_TOOL="$tool" BOX_READY_POLL=0 \
		bash cicd/utility/win-dogfood.bash) > "${scratch}/out.log" 2>&1
}

if fDogfood "${scratch}/x_vm-state.bash" FAKE_VMS=vm925w; then
	grep -q '^lock wrap b29w ' "$log" || fFail "b29w was not taken first: $(cat "$log")"
	grep -q '^ssh b29w .*pwsh .*cicd-win.ps1' "$log" || fFail "the build did not run on b29w"
	! grep -q 'b29w-wif' "$log" "${scratch}/out.log" || fFail "b29w went by another name"
	! grep -q 'vm925w' "$log" || fFail "vm925w was touched while b29w was there"
	fEcho "OK: b29w goes first, by that name"
else
	fFail "the refresh failed with b29w there: $(tail -3 "${scratch}/out.log")"
fi

if fDogfood "${scratch}/x_vm-state.bash" FAKE_VMS=vm925w FAKE_GONE=b29w; then
	grep -q '^lock wrap vm925w ' "$log" || fFail "vm925w, off, was not taken through the VM state helper: $(cat "$log")"
	grep -q '^ssh vm925w .*pwsh .*cicd-win.ps1' "$log" || fFail "the build did not run on vm925w"
	! grep -q '^lock wrap b29w' "$log" || fFail "b29w was taken while gone"
	fEcho "OK: vm925w is started through the VM state helper"
else
	fFail "the refresh failed with vm925w off: $(tail -3 "${scratch}/out.log")"
fi

## The helper's own wait is for another user, so the refresh waits for its login.
if fDogfood "${scratch}/x_vm-state.bash" FAKE_VMS="b29w vm925w" FAKE_BOOTING=2; then
	grep -q 'answered after' "${scratch}/out.log" || fFail "no line saying the box was waited for"
	grep -q '^ssh b29w .*pwsh' "$log" || fFail "the build did not run once the box answered"
	fEcho "OK: a VM still booting is waited for"
else
	fFail "the refresh failed on a VM that came up on the third try: $(tail -3 "${scratch}/out.log")"
fi

if fDogfood "${scratch}/x_windows-host-lock.bash" FAKE_GONE="b29w vm925w"; then
	grep -q 'no Windows box answered' "${scratch}/out.log" || fFail "no line saying the refresh was skipped"
	! grep -q '^lock ' "$log" || fFail "a box was taken with none there"
	fEcho "OK: no box there is a skip"
else
	fFail "the refresh failed with no box there: $(tail -3 "${scratch}/out.log")"
fi

if ((failures)); then fEcho "${failures} Windows dogfood check(s) failed"; exit 1; fi
fEcho "Windows dogfood checks passed"
