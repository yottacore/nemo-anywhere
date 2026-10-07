#!/usr/bin/env bash

##	- Purpose: PSScriptAnalyzer over the project's own PowerShell. Whole-tree,
##	  the same way the Bash check is: none of it came from upstream.
##	- Rules are in PSScriptAnalyzerSettings.psd1 at the repo root, so an editor
##	  and this stage see the same set. A rule that has to be off for one place
##	  goes off there with a SuppressMessageAttribute, not in that file.
##	- A missing pwsh or a missing module skips with a warning, so a box without
##	  either can't hard-block a push; PSA_STRICT=1 turns that miss into a
##	  failure.
##	- Syntax: lint-powershell.bash

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

strict="${PSA_STRICT:-0}"
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

fSkip(){
	if [[ "$strict" == "1" ]]; then
		fEcho "FAILED: PowerShell lint: $1" >&2
		exit 1
	fi
	fEcho "WARNING: PowerShell lint SKIPPED: $1" >&2
	exit 0
}

## Windows can refuse to run a script with non-ASCII bytes in it, a BOM included.
## Runs ahead of the pwsh check, so a box without pwsh still gets it.
## Test ID: rjmn9fh2
nonAscii="$(git ls-files -z '*.ps1' '*.psm1' '*.psd1' | xargs -0 -r grep -nP '[^\x00-\x7F]' || true)"
if [[ -n "$nonAscii" ]]; then
	printf '%s\n' "$nonAscii"
	fEcho "FAILED: PowerShell lint: non-ASCII bytes in a PowerShell file" >&2
	exit 1
fi

command -v pwsh >/dev/null 2>&1 || fSkip "pwsh not installed"
pwsh -NoProfile -Command 'exit ([int](-not (Get-Module -ListAvailable PSScriptAnalyzer)))' \
	|| fSkip "PSScriptAnalyzer module not installed"

mapfile -t files < <(git ls-files '*.ps1' '*.psm1')
((${#files[@]})) || { fEcho "OK: PowerShell lint: nothing to check"; exit 0; }

## -Path takes one file at a time here: handed an array it tries to coerce the
## whole thing to a string and fails.
fEcho "PowerShell lint (PSScriptAnalyzer) over ${#files[@]} script(s)..."
# shellcheck disable=2016  ## the $ signs below are PowerShell's, and must not be expanded by bash
PSA_FILES="$(printf '%s\n' "${files[@]}")" pwsh -NoProfile -Command '
	$settings = Join-Path (Get-Location) "PSScriptAnalyzerSettings.psd1"
	$found = foreach ($f in ($env:PSA_FILES -split "`n")) {
		Invoke-ScriptAnalyzer -Path $f -Settings $settings
	}
	if ($found) {
		$found | ForEach-Object {
			"{0}:{1}: {2}: {3}" -f $_.ScriptName, $_.Line, $_.RuleName, $_.Message
		}
		exit 1
	}
	exit 0
' || { fEcho "FAILED: PowerShell lint" >&2; exit 1; }
fEcho "OK: PowerShell lint: no findings"
