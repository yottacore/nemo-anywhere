#!/usr/bin/env bash

##	- Purpose: Check the one line each test gets in a run's log, status first,
##	  then its ID and name. include/result-lines.awk rewrites meson's result
##	  lines and leaves the rest, and include/result-line.bash's fTest reads the
##	  ID from a script's "Test ID:" line and passes 77 as a skip.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-result-lines.bash
##	- Test ID: dehyx

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"
# shellcheck source=include/result-line.bash
source "${here}/include/result-line.bash"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## A name with a status word in it, and a test's own output with a number in
## front, must come through right.
in='ninja: no work to do.
  1/236 rhmmh Small file copy test                                     OK               0.39s
 27/236 rkney Sanitizer leak suppressions test                         SKIP             0.02s   exit status 77
 75/236 rjbmh Stopped zip leak test                                    FAIL            15.96s   exit status 1
 76/236 rjbmi OK FAIL name test                                        TIMEOUT         30.01s   killed by signal 15 SIGTERM
 77/236 rjbmj Expected fail test                                       EXPECTEDFAIL     0.10s   exit status 1
 78/236 rjbmk Unexpected pass test                                     UNEXPECTEDPASS   0.10s
 79/236 rjbmm Error test                                               ERROR            0.10s   exit status 99
 3/10 rows sorted
Ok:                 234'
want='ninja: no work to do.
PASS rhmmh Small file copy test  0.39s
SKIP rkney Sanitizer leak suppressions test  0.02s exit status 77
FAIL rjbmh Stopped zip leak test  15.96s exit status 1
FAIL rjbmi OK FAIL name test  30.01s killed by signal 15 SIGTERM
PASS rjbmj Expected fail test  0.10s exit status 1
FAIL rjbmk Unexpected pass test  0.10s
FAIL rjbmm Error test  0.10s exit status 99
 3/10 rows sorted
Ok:                 234'
got="$(awk -f "${here}/include/result-lines.awk" <<< "${in}")"
[[ "${got}" == "${want}" ]] || fFail "result-lines.awk gave:"$'\n'"${got}"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/result-lines-test.XXXXXX")"
trap 'fSafeRm_IfThere "${scratch}"' EXIT
# shellcheck disable=SC2016  ## expanded by the fake test script
printf '#!/usr/bin/env bash\n##\t- Test ID: abcde\nexit "${1:-0}"\n' > "${scratch}/test-fake.bash"
printf '#!/usr/bin/env python3\n"""Fake.\n\nTest ID: abcdf\n"""\n' > "${scratch}/fake.py"

## $1 expected exit, $2 expected output, rest the fTest command.
fExpect(){
	local want="$1" line="$2" rc=0 out=''
	shift 2
	out="$(fTest "$@")" || rc=$?
	[[ "${rc}" == "${want}" ]] || fFail "fTest $*: exit ${rc}, not ${want}"
	[[ "${out}" == "${line}" ]] || fFail "fTest $*: printed '${out}', not '${line}'"
	return 0
}
fExpect 0 'PASS abcde test-fake.bash'      bash "${scratch}/test-fake.bash"
fExpect 0 'SKIP abcde test-fake.bash 77'   bash "${scratch}/test-fake.bash" 77
fExpect 3 'FAIL abcde test-fake.bash 3'    bash "${scratch}/test-fake.bash" 3
fExpect 0 'PASS abcdf fake.py --self-test' true "${scratch}/fake.py" --self-test
fExpect 0 'PASS ? true'                    true

if ((failures)); then
	fEcho "FAILED: result lines, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: result lines"


##	History:
##		- 2026-10-28: Created.
