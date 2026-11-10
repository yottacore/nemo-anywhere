#!/usr/bin/env bash

##	- Purpose: 3 rules from project/style-guide_code.md and
##	  project/style-guide_ux.md that nothing else checks: American spelling, no
##	  banner dividers in C, and ASCII only outside the copyright marker.
##	- Only the fork's own lines are checked. Most of source/ came from upstream
##	  and has its own spellings, so a line that is byte for byte the same as
##	  the one in the root commit is left alone. That way the check needs no
##	  growing list of exceptions.
##	- Needs git for that comparison, which is also true of the other checks in
##	  this stage.
##	- Syntax: lint-prose.bash

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

if ! command -v git >/dev/null 2>&1; then
	fEcho "WARN: no git, skipping the prose check"
	exit 0
fi

baseline="$(git rev-list --max-parents=0 HEAD)"

## This file is left out of its own scan, the same way lint-identity.bash is:
## the word list below names the things being looked for.
self='cicd/utility/lint-prose.bash'

## Print every hit that isn't verbatim in the root commit. $1 is a description,
## $2 an extended regex, the rest are paths.
fReport(){
	local what="$1" pattern="$2"; shift 2
	local hits line file text found=0
	hits="$(git grep -niIE "$pattern" -- "$@" ":!:${self}" || true)"
	[[ -n "$hits" ]] || return 0
	while IFS= read -r line; do
		file="${line%%:*}"
		text="${line#*:}"; text="${text#*:}"
		## A Windows checkout ends every line in CR, which the root commit's
		## text doesn't have.
		text="${text%$'\r'}"
		## An empty match is nothing to compare; treat it as ours.
		if [[ -n "$text" ]] && git grep -qIF -e "$text" "$baseline" -- "*${file##*/}"; then
			continue
		fi
		(( found == 0 )) && fEcho "FAIL: $what"
		found=1
		echo "$line"
	done <<< "$hits"
	return "$found"
}

rc=0

## The UI style guide asks for American spelling, so the code and the docs that
## go with it say the same thing.
british='\b([a-z_]*colour[a-z_]*|behaviour[a-z]*|honour[a-z]*|flavour[a-z]*|[a-z]*grey[a-z]*|recognis[a-z]+|normalis[a-z]+|organis[a-z]+|initialis[a-z]+|centred|towards|licence|defence|neighbour[a-z]*|favourite[a-z]*)\b'
fReport "a British spelling" "$british" source cicd utility README.md install.md building.md changelog.md project/design.md project/design_docs \
	':!:cicd/utility/lint-ui-case.py' ':!:cicd/utility/include' || rc=1

## "No banner dividers" - style-guide_code.md. There were 3 forms before anyone
## wrote that down. The pattern wants 4 characters, so the emacs mode line's
## -*- stays out of it. The bullet-rule form is caught by the ASCII check below.
fReport "a banner divider" '^[[:space:]]*/?\*+[[:space:]]*[-=_.*#]{4,}' \
	'source/*.c' 'source/*.h' || rc=1

## ASCII only, except the copyright symbol and the marker's own characters.
## Upstream files are skipped whole: several have an accented author name in
## the header, which is theirs to keep.
mapfile -t cfiles < <(git ls-files 'source/*.c' 'source/*.h')
bad=""
for f in "${cfiles[@]}"; do
	if git grep -qI -e '' "$baseline" -- "*${f##*/}"; then
		continue
	fi
	## The copyright symbol is wanted anywhere; the marker's other characters
	## only on the copyright line itself.
	out="$(sed 's/\xc2\xa9//g' "$f" | grep -nP '[^\x00-\x7F]' | grep -v 'Copyright ' || true)"
	if [[ -n "$out" ]]; then
		bad+="${f}: ${out}"$'\n'
	fi
done
if [[ -n "$bad" ]]; then
	fEcho "FAIL: a non-ASCII byte outside the copyright line"
	printf '%s' "$bad"
	rc=1
fi

(( rc == 0 )) && fEcho "OK: prose"
exit "$rc"
