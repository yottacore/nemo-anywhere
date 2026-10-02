#!/usr/bin/env bash

##	- Purpose: Check that cicd.bash --help names every stage --quick skips.
##	  The list is read from the run itself: the stages that report "skipped
##	  (--quick)" and the switches the --quick option turns off.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-cicd-help.bash
##	- Test ID: rj9v18rx


##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cicd="${here}/../cicd.bash"

fEcho(){ echo "[ $* ]"; }

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## The --quick entry and its continuation lines, joined into one.
help="$(bash "$cicd" --help)"
quickHelp="$(awk '/^ +--quick / { on = 1; print; next } on && /^ +--/ { exit } on { print }' <<< "$help" | tr -s ' \n' '  ')"
if [[ -z "$quickHelp" ]]; then
	fEcho "FAILED: no --quick entry in cicd.bash --help"
	exit 1
fi

## What the switches --quick turns off are called in the help.
declare -A switchNames=([BUILD_CROSS]="cross" [PROFILE_ENABLE]="profiler" [PACKAGE_ENABLE]="packages")

skipped=()
while IFS= read -r what; do skipped+=("$what"); done < <(grep -o -E '"[a-z][a-z ]*[a-z] skipped \(--quick\)"' "$cicd" | sed 's/^"//; s/ skipped (--quick)"$//' | sort -u)
optLine="$(grep -E '^[[:space:]]+--quick\)' "$cicd" || true)"
while IFS= read -r var; do
	[[ -n "$var" ]] || continue
	if [[ -z "${switchNames[$var]+x}" ]]; then
		fFail "--quick turns off ${var}; add its help name to switchNames"
		continue
	fi
	skipped+=("${switchNames[$var]}")
done < <(grep -o -E '[A-Z_]+=0' <<< "$optLine" | sed 's/=0$//')

if ((${#skipped[@]} < 3)); then
	fEcho "FAILED: found only ${#skipped[@]} stages skipped by --quick; the patterns no longer match cicd.bash"
	exit 1
fi

for what in "${skipped[@]}"; do
	if ! grep -q -i -F -- "$what" <<< "$quickHelp"; then
		fFail "--quick skips ${what}, but --help does not say so"
	fi
done

if ((failures)); then exit 1; fi
fEcho "OK: --quick help names all ${#skipped[@]} skipped stages"
