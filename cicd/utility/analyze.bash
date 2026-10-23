#!/usr/bin/env bash

##	- Purpose: The static analyzer stage. gcc's -fanalyzer and the clang analyzer
##	  run over every C file in both compile lists: nemo-build's /build and
##	  nemo-winbuild's /build-win, which has the Windows-only files. Run on the
##	  host, after the cross build, so both lists are current.
##	- Report only for now (backlog 2026102114400584): findings are counted per
##	  area and kept, and don't fail the run. The stage fails when a tracked .c
##	  outside source/unused/ is in neither list, when one under it is in either,
##	  when an analyzer misses the planted leak or null dereference, or when a run
##	  itself fails.
##	- The last run's logs are in cicd/artifacts/analyze/last/. Each run also keeps
##	  counts_<stamp>.txt and findings_<stamp>.tgz there, GFS-rotated.
##	- An analyzer missing from its container shows as "not run" in the counts.
##	  nemo-winbuild has no clang yet.
##	- CICD_MAX_JOBS caps the jobs of all 4 runs together. gcc spends many minutes
##	  on a few files (the blake3 SIMD ones and nemo-config.c, at -O0), and
##	  ANALYZE_TU_TIMEOUT (900 s) caps each file; one cut off counts as failed.
##	- --self-test runs analyze.py's own self-test and the planted faults only.
##	- Syntax: cicd/utility/analyze.bash [--self-test]
##	- Test ID: rktsh3gd

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

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

jobs="${CICD_MAX_JOBS:-$(( $(nproc) / 2 ))}"
tool="/src/cicd/utility/analyze.py"
## lane|container|compile list|compiler
lanes=(
	"linux|nemo-build|/build/compile_commands.json|cc"
	"win|nemo-winbuild|/build-win/compile_commands.json|x86_64-w64-mingw32-gcc"
)
failed=0

fPlanted(){
	local l lane container db cc a rc
	for l in "${lanes[@]}"; do
		IFS='|' read -r lane container db cc <<< "${l}"
		for a in gcc clang; do
			rc=0; docker exec "${container}" python3 "${tool}" planted --tool "${a}" --cc "${cc}" | grep -E '^\[|^### SKIP' || rc="${PIPESTATUS[0]}"
			case "${rc}" in
				0)  ;;
				77) fEcho "${a}-${lane}: not run, no ${a} in ${container}" ;;
				*)  fEcho "FAILED: ${a}-${lane} missed a planted fault"; failed=1 ;;
			esac
		done
	done
}

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
python3 "${root}/cicd/utility/analyze.py" --self-test || failed=1
fPlanted
if [[ "${1:-}" == "--self-test" ]]; then
	((failed)) && fDie "analyzer self-test"
	fEcho "OK: analyzer self-test"; exit 0
fi

out="${root}/cicd/artifacts/analyze"
last="${out}/last"
mkdir -p "${last}"
stamp="$(date +%Y%m%d-%H%M%S)"

fSection "Compile list coverage"
for l in "${lanes[@]}"; do
	IFS='|' read -r lane container db cc <<< "${l}"
	docker exec "${container}" cat "${db}" > "${last}/compile-${lane}.json" || fDie "no compile list in ${container}: ${db}"
done
python3 "${root}/cicd/utility/analyze.py" cover "${root}" "${last}/compile-linux.json" "${last}/compile-win.json" || failed=1

## All 4 at once, a quarter of the jobs each. A few files take gcc many minutes
## alone, so the rest overlap that tail.
each=$(( jobs / 4 )); ((each)) || each=1
fSection "Analyzers (${each} jobs each, 4 at once)"
t0=${SECONDS}
pids=(); names=()
for l in "${lanes[@]}"; do
	IFS='|' read -r lane container db cc <<< "${l}"
	for a in gcc clang; do
		docker exec -e "ANALYZE_TU_TIMEOUT=${ANALYZE_TU_TIMEOUT:-900}" "${container}" python3 "${tool}" run --tool "${a}" --db "${db}" --jobs "${each}" > "${last}/${a}-${lane}.log" 2>&1 &
		pids+=("$!"); names+=("${a}-${lane}")
	done
done
for i in "${!pids[@]}"; do
	rc=0; wait "${pids[i]}" || rc=$?
	[[ "${rc}" == 0 || "${rc}" == 77 ]] || { fEcho "FAILED: ${names[i]} exited ${rc}"; failed=1; }
done
fEcho "analyzers took $(( SECONDS - t0 )) s"

python3 "${root}/cicd/utility/analyze.py" count "${root}" "${last}"/{gcc,clang}-{linux,win}.log | tee "${out}/counts_${stamp}.txt"
tar -C "${last}" -czf "${out}/findings_${stamp}.tgz" gcc-linux.log clang-linux.log gcc-win.log clang-win.log
gfs_rotate "${out}" counts txt   >/dev/null 2>&1 || true
gfs_rotate "${out}" findings tgz >/dev/null 2>&1 || true

((failed)) && fDie "analyzer stage"
fEcho "OK: analyzer stage (report only)"

##	History:
##		- 2026-10-21: First version, report only.
