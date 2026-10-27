#!/usr/bin/env bash

##	- Purpose: Run one shell command in a Windows cross-build step container of
##	  its own (win-box.bash), and remove the container after. The command's
##	  exit code is the result, 77 included. stdin reaches it when it isn't a
##	  terminal. SOURCE_DATE_EPOCH and the job count go in with it.
##	- Syntax: win-run.bash [--no-build-win] <label> <sh-command>
##	  <label> names the step, and the container after it.
##	  --no-build-win: no /build-win, for a step that reads only the packed artifacts.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=win-box.bash
source "${here}/win-box.bash"
# shellcheck source=../utility/include/source-date.bash
source "${here}/../utility/include/source-date.bash"

opt=""
[[ "${1:-}" != --no-build-win ]] || { opt="$1"; shift; }
label="${1:?win-run.bash [--no-build-win] <label> <sh-command>}"
cmd="${2:?win-run.bash [--no-build-win] <label> <sh-command>}"

fSetSourceDate "$(cd "${here}/../.." && pwd)"
trap 'fStepBox_Stop' EXIT
fWinBox_Start "${label}" ${opt:+"${opt}"} || exit 1

stdin=(); [[ -t 0 ]] || stdin=(-i)
rc=0
## nice, since the container doesn't inherit ours. ulimit -c 0: the workdir is
## the mounted repo, and a core there would be root's.
docker exec "${stdin[@]}" -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}" -e "CICD_MAX_JOBS=${CICD_MAX_JOBS}" "${CONTAINER}" \
	nice -n 19 sh -c "ulimit -c 0; ionice -c 3 -p \$\$ 2>/dev/null; ${cmd}" || rc=$?
exit "${rc}"


##	History:
##		- 2026-10-26: Created.
