#!/usr/bin/env bash

##	- Purpose: Check that a failing installer or prefix check stops a full run
##	  in the packages stage, before dogfood and publish, while a failing
##	  packager still only warns. Runs the stage's own functions out of cicd.bash
##	  with stand-in entries, and reads the real lists from config.bash.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-package-checks.bash
##	- Test ID: rj3yttvt

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Both checks belong in the list that stops the run, and not in the packagers.
lists="$(bash -c 'source "$1"
	for e in "${PACKAGE_CMDS[@]:-}";   do echo "cmd ${e%%|*}"; done
	for e in "${PACKAGE_CHECKS[@]:-}"; do echo "check ${e%%|*}"; done' _ "${root}/cicd/config.bash")"
for label in "Installer check" "Prefix check"; do
	grep -qxF "check ${label}" <<<"$lists" || fFail "${label} is not in PACKAGE_CHECKS"
	if grep -qxF "cmd ${label}" <<<"$lists"; then fFail "${label} is still in PACKAGE_CMDS, where a failure only warns"; fi
done

funcs="$(awk '/^(build_packages|run_package_checks)\(\)\{/ { on = 1 } on { print } on && /^\}/ { on = 0 }' "${root}/cicd/cicd.bash")"
[[ "$funcs" == *"run_package_checks(){"* ]] || fFail "cicd.bash has no run_package_checks"

## $1 "runs on" or "stops", $2 text the output must hold, rest the checks. A
## packager that fails is always first, and must never stop the run.
fStage(){
	local want="$1" text="$2"; shift 2
	local out rc=0
	out="$(FUNCS="$funcs" bash -c '
		set -Eeuo pipefail
		fEcho(){ echo "[ $* ]"; }
		fEcho_Clean(){ echo "$*"; }
		fDie(){ echo "FAILED: $*" >&2; exit 1; }
		write_sums(){ :; }
		RELEASE_ARTIFACT_DIR=x
		PACKAGE_ENABLE=1
		PACKAGE_CMDS=("Broken packager|bash -c \"exit 1\"")
		PACKAGE_CHECKS=("$@")
		eval "$FUNCS"
		build_packages
		run_package_checks
		echo "on to dogfood"' _ "$@" 2>&1)" || rc=$?
	if [[ "$out" != *"WARNING: Broken packager failed"* ]]; then
		fFail "[$*]: the failing packager did not warn; said: ${out}"
	fi
	case "$want" in
		"runs on") [[ "$rc" == 0 && "$out" == *"on to dogfood"* ]] || fFail "[$*]: stopped (exit ${rc}); said: ${out}" ;;
		stops)     [[ "$rc" != 0 && "$out" != *"on to dogfood"* ]] || fFail "[$*]: ran on (exit ${rc}); said: ${out}" ;;
	esac
	[[ -z "$text" || "$out" == *"$text"* ]] || fFail "[$*]: no '${text}'; said: ${out}"
}

fStage "runs on" ""                          "Installer check|true" "Prefix check|true"
fStage stops     "FAILED: Installer check"   "Installer check|bash -c \"exit 1\"" "Prefix check|true"
fStage stops     "FAILED: Prefix check"      "Installer check|true" "Prefix check|bash -c \"exit 2\""
fStage "runs on" "WARNING: Prefix check skipped" "Installer check|true" "Prefix check|bash -c \"exit 77\""

if ((failures)); then
	fEcho "FAILED: package checks, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: package checks stop the run, packagers warn"
