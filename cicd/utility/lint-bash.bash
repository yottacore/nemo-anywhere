#!/usr/bin/env bash

##	- Purpose: shellcheck over the project's own shell scripts. Whole-tree,
##	  unlike the C check: every script here was written for this project, so
##	  there is no inherited noise to drown in.
##	- Anything shellcheck reports fails the stage, notes included. A script that
##	  needs a rule off carries a `shellcheck disable=` line at the site that
##	  needs it, with its reason. Note the directive has to start with a single
##	  '#': a '##' comment reads as prose and is quietly ignored.
##	- Settings (severity, sourced-file handling) are in .shellcheckrc at the
##	  repo root, so an editor and this stage see the same rules.
##	- A missing shellcheck skips with a warning, so an unprovisioned box can't
##	  hard-block a push; SHELLCHECK_STRICT=1 turns that miss into a failure.
##	- Syntax: lint-bash.bash

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

strict="${SHELLCHECK_STRICT:-0}"
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

if ! command -v shellcheck >/dev/null 2>&1; then
	if [[ "$strict" == "1" ]]; then
		fEcho "FAILED: Bash lint: shellcheck not installed" >&2
		exit 1
	fi
	fEcho "WARNING: Bash lint SKIPPED: shellcheck not installed" >&2
	exit 0
fi

## Everything under source/ is upstream's, and n8git_backup-and-publish is a
## shared copy that lives outside this project, so neither is ours to restyle.
## The two extensionless ones are named by what runs them - git wants the hook
## called pre-push, and runfm is a command name - so they can't be found by the
## .bash suffix the rest of the tree uses.
mapfile -t files < <(git ls-files '*.bash' ':!:cicd/utility/n8git_backup-and-publish')
files+=(cicd/hooks/pre-push utility/runfm)

## A disable= above a script's first command covers the whole file, however
## few lines it was meant for. Four scripts carried a block of them from a
## shared template; eleven of the thirteen rules hid nothing and one hid two
## dead variables. config.bash keeps its one: every name in it is read by
## cicd.bash, so the whole file reads as write-only.
fileWide="$(for f in "${files[@]}"; do
	[[ "$f" == 'cicd/config.bash' ]] && continue
	awk '
		/^[[:space:]]*#[[:space:]]*shellcheck[[:space:]]+disable=/ { if (!cmd) print FILENAME ":" FNR ": " $0; next }
		/^[[:space:]]*($|#)/ { next }
		{ cmd = 1 }
	' "$f"
done)"
if [[ -n "$fileWide" ]]; then
	printf '%s\n' "$fileWide"
	fEcho "FAILED: Bash lint: a shellcheck disable= covers a whole file - put it at the line that needs it"
	exit 1
fi

## grep -q and -m quit at their first match. Downstream of a writer under
## pipefail, the writer's SIGPIPE then turns a match into a failed pipeline,
## which read as no window in the GUI smoke check. Give them a here-string.
## Test ID: rjcc6jnz
earlyExit="$(grep -nE '(^|[^|])[|][[:space:]]*grep([[:space:]]+-[^[:space:]]+)*[[:space:]]+-[a-zA-Z]*[qm]' "${files[@]}" || true)"
if [[ -n "$earlyExit" ]]; then
	printf '%s\n' "$earlyExit"
	fEcho "FAILED: Bash lint: grep -q or -m reading a pipe - feed it a here-string"
	exit 1
fi

fEcho "Bash lint (shellcheck) over ${#files[@]} script(s)..."
shellcheck "${files[@]}"
fEcho "OK: Bash lint: no findings"
