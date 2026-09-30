#!/usr/bin/env bash

##	- Purpose: Check which C files the C lint covers. A feature branch gets what
##	  it changed since dev. dev and main, where that range is always empty, get
##	  what their latest merge brought in, so the release merge the pre-push gate
##	  checks on main is linted too. Runs lint-c.bash --list-files in a
##	  throwaway repo.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-lint-scope.bash
##	- Test ID: rj3ytty2

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

## Run from the pre-push hook, git may hand down a GIT_DIR, and every git call
## below would then write to the real repo.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR GIT_CONFIG_COUNT

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/lint-scope-test.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT
repo="${scratch}/repo"
mkdir -p "${repo}/cicd/utility"
cp "${here}/lint-c.bash" "${repo}/cicd/utility/"
g(){ git -C "$repo" "$@"; }
g init -q -b main
g config user.name test
g config user.email test@example.invalid
g config commit.gpgsign false

fCommit(){ local msg="$1"; shift; for f in "$@"; do mkdir -p "${repo}/$(dirname "$f")"; echo "int x_${RANDOM};" >> "${repo}/${f}"; done; g add -A; g commit -qm "$msg"; }

## $1 label, $2 expected list (space separated, sorted), rest passed on.
fExpect(){
	local label="$1" want="$2"; shift 2
	local got
	got="$(bash "${repo}/cicd/utility/lint-c.bash" --list-files "$@" | tr '\n' ' ')"
	got="${got% }"
	[[ "$got" == "$want" ]] || fFail "${label}: listed [${got}], expected [${want}]"
}

fCommit base source/a.c source/a.h
g checkout -q -b dev
g checkout -q -b feat
fCommit feature source/b.c vendor/v.c notes.md
fExpect "feature branch" "source/b.c"
g checkout -q dev
g merge -q --no-ff feat -m "Merge feat"
fExpect "dev after the merge" "source/b.c"
## A publish commit on top still leaves the merge in range.
fCommit publish notes.md
fExpect "dev after a later commit" "source/b.c"
g checkout -q main
g merge -q --no-ff dev -m "Merge dev"
fExpect "main after the release merge" "source/b.c"
fExpect "main, base named" "source/b.c" dev
## Uncommitted work counts everywhere.
echo "int y;" >> "${repo}/source/a.h"
fExpect "main with an uncommitted edit" "source/a.h source/b.c"
g checkout -q source/a.h
g checkout -q -b docs dev
fCommit docs notes.md
fExpect "feature branch with no C" ""

if ((failures)); then
	fEcho "FAILED: C lint scope, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: C lint scope"
