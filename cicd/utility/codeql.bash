#!/usr/bin/env bash

##	- Purpose: CodeQL, under --leakcheck. The C and C++ queries run over both
##	  compile lists, the debug build's /build and the cross build's
##	  /build-win, and the Python queries over the tree, each in a step
##	  container of its own. The CLI and query packs are the ones pinned in
##	  linux/Dockerfile.dev and win/Dockerfile, run locally, with no hosted
##	  service. codeql-c.qls and codeql-python.qls pick the queries.
##	- Fails when the C queries miss the planted leak or null dereference in
##	  either container (test de8ep), when codeql is missing from an image,
##	  when a lane fails, or when the count can't read a lane's SARIF. Findings
##	  are counted per area and kept, and are report only for now (backlog
##	  2026111915091061).
##	- The last run's SARIF and logs are in cicd/artifacts/codeql/last/. Each
##	  run also keeps counts_<stamp>.txt and findings_<stamp>.tgz there,
##	  GFS-rotated.
##	- The lanes run one after the other, each with all of CICD_MAX_JOBS.
##	  CODEQL_RAM (8192) is each one's memory in MB.
##	- --self-test runs codeql.py's own self-test and the planted faults only.
##	- Syntax: cicd/utility/codeql.bash [--self-test]
##	- Test ID: de8ep

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

set -Eeuo pipefail

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
# shellcheck source=include/echo.bash
source "${here}/include/echo.bash"
# shellcheck source=include/gfs-rotate.bash
source "${here}/include/gfs-rotate.bash"
# shellcheck source=include/jobs.bash
source "${here}/include/jobs.bash"

fJobs_Default hard
jobs="${CICD_MAX_JOBS}"
ram="${CODEQL_RAM:-8192}"
tool="/src/cicd/utility/codeql.py"
selfTest=0
for a in "$@"; do [[ "${a}" != --self-test ]] || selfTest=1; done
## name|container|lane args
lanes=(
	"c-linux|step|--lang c --db /build/compile_commands.json"
	"c-win|winstep|--lang c --db /build-win/compile_commands.json"
	"python|step|--lang python"
)
failed=0

## In a container of the lane's own, by label, as analyze.bash does.
fInBox(){
	local container="$1" step="$2"; shift 2
	case "${container}" in
		step)    bash "${here}/docker-run.bash" "codeql ${step}" "$(printf '%q ' "$@")" ;;
		winstep) bash "${root}/cicd/win/win-run.bash" "codeql win ${step}" "$(printf '%q ' "$@")" ;;
	esac
}

out="${root}/cicd/artifacts/codeql"
last="${out}/last"
mkdir -p "${last}"

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
python3 "${here}/codeql.py" --self-test || failed=1
## container|compiler
for p in "step|gcc" "winstep|x86_64-w64-mingw32-gcc"; do
	IFS='|' read -r container cc <<< "${p}"
	rc=0; fInBox "${container}" planted env nice -n 19 python3 "${tool}" planted --cc "${cc}" --jobs "${jobs}" 2> "${last}/planted-${container}.log" | grep -E '^\[|^### SKIP|planted\.c:' || rc="${PIPESTATUS[0]}"
	case "${rc}" in
		0)  ;;
		77) fEcho "FAILED: ${container}: not run, no codeql in its image"; failed=1 ;;
		*)  tail -5 "${last}/planted-${container}.log"; fEcho "FAILED: ${container}: CodeQL missed a planted fault, or its run failed"; failed=1 ;;
	esac
done
if ((failed)); then echo "FAIL de8ep CodeQL planted faults"; else echo "PASS de8ep CodeQL planted faults"; fi
if ((selfTest)); then
	((failed)) && fDie "CodeQL self-test"
	fEcho "OK: CodeQL self-test"; exit 0
fi
((! failed)) || fDie "CodeQL planted faults"

stamp="$(date +%Y%m%d-%H%M%S)"
logs=(planted-step.log planted-winstep.log)
for l in "${lanes[@]}"; do
	IFS='|' read -r name container args <<< "${l}"
	read -ra laneArgs <<< "${args}"
	fSection "CodeQL: ${name} (${jobs} jobs)"
	t0=${SECONDS}
	rc=0; fInBox "${container}" "${name}" env nice -n 19 python3 "${tool}" lane "${laneArgs[@]}" --jobs "${jobs}" --ram "${ram}" > "${last}/${name}.sarif" 2> "${last}/${name}.log" || rc=$?
	case "${rc}" in
		0)  ;;
		77) fEcho "FAILED: ${name}: not run, no codeql in ${container}"; failed=1 ;;
		*)  fEcho "FAILED: ${name} exited ${rc}"; grep -h '^\[ FAILED' "${last}/${name}.log" || tail -5 "${last}/${name}.log"; failed=1 ;;
	esac
	grep -h 'CodeQL scanned' "${last}/${name}.log" || true
	fEcho "${name} took $(( SECONDS - t0 )) s"
	logs+=("${name}.sarif" "${name}.log")
done

countRc=0
sarifs=(); for l in "${lanes[@]}"; do sarifs+=("${last}/${l%%|*}.sarif"); done
python3 "${here}/codeql.py" count "${root}" "${sarifs[@]}" | tee "${out}/counts_${stamp}.txt" || countRc="${PIPESTATUS[0]}"
case "${countRc}" in
	0)  ;;
	1)  fEcho "The CodeQL findings above don't fail the run yet (backlog 2026111915091061)" ;;
	*)  fEcho "FAILED: the count exited ${countRc}"; failed=1 ;;
esac
tar -C "${last}" -czf "${out}/findings_${stamp}.tgz" "${logs[@]}"
gfs_rotate "${out}" counts txt   >/dev/null 2>&1 || true
gfs_rotate "${out}" findings tgz >/dev/null 2>&1 || true

((failed)) && fDie "CodeQL stage"
fEcho "OK: CodeQL stage"

##	History:
##		- 2026-11-19: First version, report only.
