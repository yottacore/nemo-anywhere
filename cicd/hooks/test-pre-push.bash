#!/usr/bin/env bash

##	- Purpose: Check the pre-push hook's version guard for main. A release push
##	  must carry a strictly higher version, with a prerelease below its own
##	  release - which plain sort -V gets backwards, so a beta to final push was
##	  once refused. First the compare on its own, then the whole hook run
##	  against a throwaway repo, with a stand-in for the gate it calls.
##	- Runs in the lint stage.
##	- Syntax: cicd/hooks/test-pre-push.bash
##	- Test ID: rhtrxr80

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hook="${here}/pre-push"

## The hook may be what is running this, and git can hand its hooks a GIT_DIR.
## Left set, every git call below would write to the real repo.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR GIT_CONFIG_COUNT

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Not followed: the hook ends in an exit, which would mark all below unreachable.
# shellcheck source=/dev/null
source "$hook"

## old new expected: 0 newer, 1 equal, 2 lower.
cases=(
	"1.0.0-beta2 1.0.0 0"
	"1.0.0 1.0.0-rc.1 2"
	"1.0.0-beta2 1.0.0-rc.1 0"
	"1.0.0-rc.1 1.0.0-beta2 2"
	"1.0.0 1.0.0 1"
	"1.0.0-beta2 1.0.0-beta2 1"
	"1.0.0-alpha.2 1.0.0 0"
	"1.0.0 1.0.0-alpha.2 2"
	"1.0.0-alpha 1.0.0-alpha.1 0"
	"1.0.0-beta2 1.0.0-beta10 0"
	"1.0.0-rc.1 1.0.0-rc.10 0"
	"1.0.0 1.0.1-beta1 0"
	"1.9.0 1.10.0 0"
	"1.10.0 1.9.0 2"
	"1.0.0 0.9.9 2"
)
for entry in "${cases[@]}"; do
	read -r old new want <<<"$entry"
	got=0
	fVersionOrder "$old" "$new" || got=$?
	[[ "$got" == "$want" ]] || fFail "fVersionOrder ${old} -> ${new}: returned ${got}, expected ${want}"
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/pre-push-test.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

## meson_version sits first on the line, so a match on it would read >=0.56.0.
printf "project('x', 'c', meson_version : '>=0.56.0', version : '2.1.0-rc.3')\n" > "${scratch}/meson.build"
got="$(_ver_from "${scratch}/meson.build" || true)"
[[ "$got" == "2.1.0-rc.3" ]] || fFail "_ver_from read '${got}', expected 2.1.0-rc.3"

## A repo whose gate always passes, so only the version guard can refuse. The
## hook picks the PowerShell gate on a Windows checkout.
repo="${scratch}/repo"
mkdir -p "${repo}/source" "${repo}/cicd"
printf '#!/usr/bin/env bash\nexit 0\n' > "${repo}/cicd/cicd.bash"
chmod +x "${repo}/cicd/cicd.bash"
printf 'exit 0\n' > "${repo}/cicd/cicd-win.ps1"
## The lint stage runs under MSYS2 on Windows, where pwsh is not on PATH. The
## gate is a stand-in anyway, so pwsh can be one too.
mkdir -p "${scratch}/bin"
printf '#!/usr/bin/env bash\nexit 0\n' > "${scratch}/bin/pwsh"
chmod +x "${scratch}/bin/pwsh"
PATH="${scratch}/bin:${PATH}"
git -C "$repo" init -q
git -C "$repo" config user.name test
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config commit.gpgsign false

fCommitVersion(){
	printf "project('nemo-anywhere', 'c', version : '%s', meson_version : '>=0.56.0')\n" "$1" > "${repo}/source/meson.build"
	git -C "$repo" add -A
	git -C "$repo" commit -q --allow-empty -m "$1"
	git -C "$repo" rev-parse HEAD
}

## old new expected-exit expected-text. The hook reads the new version from the
## work tree and the old one from the remote sha git hands it.
hookCases=(
	"1.0.0-beta2|1.0.0|0|"
	"1.0.0|1.0.0-rc.1|1|is not greater than"
	"1.0.0-beta2|1.0.0-rc.1|0|"
	"1.0.0|1.0.0|1|does not bump"
	"1.0.0-alpha.2|1.0.0|0|"
)
for entry in "${hookCases[@]}"; do
	IFS='|' read -r old new want text <<<"$entry"
	oldsha="$(fCommitVersion "$old")"
	newsha="$(fCommitVersion "$new")"
	rc=0
	out="$(cd "$repo" && printf 'refs/heads/main %s refs/heads/main %s\n' "$newsha" "$oldsha" \
		| bash "$hook" origin example.invalid 2>&1)" || rc=$?
	if [[ "$rc" != "$want" ]]; then
		fFail "hook ${old} -> ${new}: exit ${rc}, expected ${want}; said: ${out}"
	elif [[ -n "$text" && "$out" != *"$text"* ]]; then
		fFail "hook ${old} -> ${new}: refused without '${text}'; said: ${out}"
	fi
done

## Anything but main is not guarded, whatever the versions.
rc=0
out="$(cd "$repo" && printf 'refs/heads/dev %s refs/heads/dev %s\n' "$newsha" "$oldsha" \
	| bash "$hook" origin example.invalid 2>&1)" || rc=$?
[[ "$rc" == "0" ]] || fFail "hook refused a push to dev: ${out}"

if ((failures)); then
	fEcho "FAILED: pre-push version guard, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: pre-push version guard (${#cases[@]} compares, $(( ${#hookCases[@]} + 1 )) hook runs)"
