#!/usr/bin/env bash

##	- Purpose: Check the static analyzer stage's gate, analyze.py count, which
##	  analyze.bash fails the stage on. A planted finding with no suppression
##	  fails it, wherever it is, a system header included, and so does an entry
##	  on the suppression list that matches nothing. A listed finding alone
##	  passes. Runs count on made-up logs and lists, against this repo.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-analyze-gate.bash
##	- Test ID: de8en

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
py="$(command -v python3 || command -v python)"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/analyze-gate-check.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
supp="${scratch}/supp.txt"
log="${scratch}/clang-linux.log"
printf '%s\n' "vendor/x/y.h:2 unix.Malloc freed by its owner" > "${supp}"
listed="vendor/x/y.h:2:9: warning: Potential leak of memory pointed to by 'r' [unix.Malloc]"

## $1 want exit 0 or 1, $2 what the output must have, $3 the case, rest the log's lines.
fCase(){
	local want="$1" has="$2" name="$3" out rc=0; shift 3
	{ echo "### source/src/a.c rc=0"; printf '%s\n' "$@"; } > "${log}"
	out="$("$py" "${here}/analyze.py" count --supp "${supp}" "${root}" "${log}" 2>&1)" || rc=$?
	[[ "${rc}" == "${want}" ]] || fFail "${name}: exit ${rc}, want ${want}"
	grep -qF -- "${has}" <<< "${out}" || fFail "${name}: no '${has}' in: $(tail -n 3 <<< "${out}")"
}

fCase 0 "suppressed" "listed finding only" "${listed}"
fCase 1 "not suppressed (clang-linux): source/src/a.c:3 core.NullDereference" "planted finding" "${listed}" \
	"source/src/a.c:3:5: warning: Dereference of null pointer (loaded from variable 'q') [core.NullDereference]"
fCase 1 "not suppressed (clang-linux): /usr/include/glib-2.0/glib/gmessages.h:354 core.NullDereference" "system header" "${listed}" \
	"/usr/include/glib-2.0/glib/gmessages.h:354:1: warning: Dereference of null pointer [core.NullDereference]"
fCase 1 "not suppressed (clang-linux): source/test/t.c:9 analyzer-malloc-leak" "gcc's -Werror form" "${listed}" \
	"source/test/t.c:9:1: error: leak of 'q' [CWE-401] [-Werror=analyzer-malloc-leak]"
fCase 1 "stale suppression: vendor/x/y.h:2 unix.Malloc" "stale entry"
## A note that names a line isn't a finding.
fCase 0 "suppressed" "notes only" "${listed}" "source/src/a.c:7:3: note: Calling 'f'"

((failures)) && { fEcho "FAILED: ${failures} analyzer gate check(s)"; exit 1; }
fEcho "OK: analyzer gate"
