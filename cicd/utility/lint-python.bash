#!/usr/bin/env bash

##	- Purpose: ruff over the project's own Python. Rules and exclusions are in
##	  pyproject.toml at the repo root.
##	- A missing ruff skips with a warning, so an unprovisioned box can't
##	  hard-block a push; RUFF_STRICT=1 turns that miss into a failure. Same
##	  bargain the Bash check makes with shellcheck.
##	- Syntax: lint-python.bash

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

strict="${RUFF_STRICT:-0}"
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

if ! command -v ruff >/dev/null 2>&1; then
	if [[ "${strict}" == "1" ]]; then
		fEcho "FAILED: Python lint: ruff not installed" >&2
		exit 1
	fi
	fEcho "WARNING: Python lint SKIPPED: ruff not installed" >&2
	exit 0
fi

mapfile -t files < <(git ls-files '*.py')
(( ${#files[@]} )) || { fEcho "OK: Python lint: nothing to check"; exit 0; }

fEcho "Python lint (ruff) over ${#files[@]} file(s)..."
if ! ruff check --no-cache --force-exclude --output-format concise "${files[@]}"; then
	fEcho "FAILED: Python lint" >&2
	exit 1
fi
fEcho "OK: Python lint: no findings"
