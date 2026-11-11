#!/usr/bin/env bash

##	- Purpose: The static analyzer stage. gcc's -fanalyzer and the clang analyzer
##	  run over every C file in both compile lists: the debug build's /build,
##	  and the cross build's /build-win, which has the Windows-only files, each
##	  read in step containers of their own. Run on the host, after the cross build,
##	  so both lists are current.
##	- Findings are counted per area and kept. Vendored findings are listed in
##	  analyze-vendored.txt, each with why it's safe, and count as suppressed.
##	  Ours and inherited ones are suppressed at the line, with EEL_ANALYZER_SKIP.
##	- The stage fails on any finding left, a system header's included, on an
##	  entry in analyze-vendored.txt that matches nothing, or is malformed
##	  (backlog 2026102114400591, test de8en), and on a file not analyzed, such
##	  as one cut off at the cap (2026110815102912). It also fails when a tracked .c
##	  outside source/unused/ is in neither list, when one under it is in either,
##	  when an analyzer misses the planted leak or null dereference, or when a run
##	  itself fails.
##	- The last run's logs are in cicd/artifacts/analyze/last/. Each run also keeps
##	  counts_<stamp>.txt and findings_<stamp>.tgz there, GFS-rotated.
##	- Both images have both analyzers, so one missing from its container fails
##	  the stage, and shows as "not run" in the counts. The Linux lane's
##	  container is "step", run through docker-run.bash, and the Windows lane's
##	  "winstep", through win/win-run.bash.
##	- CICD_MAX_JOBS caps the jobs of all 4 runs together. ANALYZE_TU_TIMEOUT
##	  (900 s) caps each file. A plain cicd run sets 120 and --long 900, in
##	  config.bash. The few files gcc can't finish inside that at the build's
##	  flags, blake3's SIMD ones and the 2 that build SHCL, are on
##	  analyze-slow.txt, each with how its lane runs it and what still covers it.
##	- --self-test runs analyze.py's own self-test and the planted faults only.
##	- --ctu is the cross-file clang run, under --leakcheck: clang alone over
##	  both lists, following calls into the other .c files of the same list,
##	  with logs in cicd/artifacts/analyze-ctu/. It fails when either lane misses
##	  the planted pair, whose leak and NULL come from the other file, or reports
##	  a NULL read the other file suppresses (test de8eq), or a run fails. Its
##	  findings and cut-off files are report only for now (backlog
##	  2026110920251244).
##	- Syntax: cicd/utility/analyze.bash [--self-test] [--ctu]
##	- Test ID: de8es

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
tool="/src/cicd/utility/analyze.py"
ctu=0; selfTest=0
for a in "$@"; do case "${a}" in --ctu) ctu=1 ;; --self-test) selfTest=1 ;; esac; done
tools=(gcc clang); ctuArg=(); tag=""
((! ctu)) || { tools=(clang); ctuArg=(--ctu); tag="-ctu"; }
## lane|container|compile list|compiler
lanes=(
	"linux|step|/build/compile_commands.json|cc"
	"win|winstep|/build-win/compile_commands.json|x86_64-w64-mingw32-gcc"
)
failed=0

## In a container of the lane's own, by label. Env goes in through env.
fInLane(){
	local container="$1" step="$2"; shift 2
	case "${container}" in
		step)    bash "${here}/docker-run.bash" "analyze ${step}" "$(printf '%q ' "$@")" ;;
		winstep) bash "${root}/cicd/win/win-run.bash" "analyze win ${step}" "$(printf '%q ' "$@")" ;;
	esac
}

fPlanted(){
	local l lane container db cc a rc
	for l in "${lanes[@]}"; do
		IFS='|' read -r lane container db cc <<< "${l}"
		for a in "${tools[@]}"; do
			rc=0; fInLane "${container}" "planted-${a}${tag}" python3 "${tool}" planted --tool "${a}" --cc "${cc}" "${ctuArg[@]}" | grep -E '^\[|^### SKIP' || rc="${PIPESTATUS[0]}"
			case "${rc}" in
				0)  ;;
				77) fEcho "FAILED: ${a}${tag}-${lane}: not run, no ${a}${tag:+ or clang-extdef-mapping} in ${container}"; failed=1 ;;
				*)  fEcho "FAILED: ${a}${tag}-${lane} missed a planted fault"; failed=1 ;;
			esac
		done
	done
}

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
python3 "${root}/cicd/utility/analyze.py" --self-test || failed=1
fPlanted
if ((ctu)); then
	if ((failed)); then echo "FAIL de8eq cross-file clang planted faults"; else echo "PASS de8eq cross-file clang planted faults"; fi
fi
if ((selfTest)); then
	((failed)) && fDie "analyzer self-test"
	fEcho "OK: analyzer self-test"; exit 0
fi

out="${root}/cicd/artifacts/analyze${tag}"
last="${out}/last"
mkdir -p "${last}"
stamp="$(date +%Y%m%d-%H%M%S)"

fSection "Compile list coverage"
for l in "${lanes[@]}"; do
	IFS='|' read -r lane container db cc <<< "${l}"
	fInLane "${container}" "compile-list" cat "${db}" > "${last}/compile-${lane}.json" || fDie "no compile list in ${container}: ${db}"
done
python3 "${root}/cicd/utility/analyze.py" cover "${root}" "${last}/compile-linux.json" "${last}/compile-win.json" || failed=1

## All at once, an even share of the jobs each. A few files take gcc many
## minutes alone, so the rest overlap that tail.
nRuns=$(( ${#lanes[@]} * ${#tools[@]} ))
each=$(( jobs / nRuns )); ((each)) || each=1
fSection "Analyzers${tag:+ across files} (${each} jobs each, ${nRuns} at once)"
t0=${SECONDS}
pids=(); names=(); logs=()
for l in "${lanes[@]}"; do
	IFS='|' read -r lane container db cc <<< "${l}"
	for a in "${tools[@]}"; do
		fInLane "${container}" "${a}${tag}" env "ANALYZE_TU_TIMEOUT=${ANALYZE_TU_TIMEOUT:-900}" nice -n 19 python3 "${tool}" run --tool "${a}" --db "${db}" --lane "${lane}" --jobs "${each}" "${ctuArg[@]}" > "${last}/${a}${tag}-${lane}.log" 2>&1 &
		pids+=("$!"); names+=("${a}${tag}-${lane}"); logs+=("${a}${tag}-${lane}.log")
	done
done
for i in "${!pids[@]}"; do
	rc=0; wait "${pids[i]}" || rc=$?
	case "${rc}" in
		0)  ;;
		77) fEcho "FAILED: ${names[i]}: not run, the analyzer is missing in its container"; failed=1 ;;
		*)  fEcho "FAILED: ${names[i]} exited ${rc}"; grep -h '^\[ FAILED' "${last}/${names[i]}.log" || true; failed=1 ;;
	esac
done
fEcho "analyzers took $(( SECONDS - t0 )) s"

countRc=0
python3 "${root}/cicd/utility/analyze.py" count ${ctuArg[@]:+--no-stale} "${root}" "${logs[@]/#/${last}/}" | tee "${out}/counts_${stamp}.txt" || countRc="${PIPESTATUS[0]}"
if ((countRc && ctu)); then
	fEcho "The cross-file findings and cut-off files above don't fail the run yet (backlog 2026110920251244)"
elif ((countRc)); then
	failed=1
fi
tar -C "${last}" -czf "${out}/findings_${stamp}.tgz" "${logs[@]}"
gfs_rotate "${out}" counts txt   >/dev/null 2>&1 || true
gfs_rotate "${out}" findings tgz >/dev/null 2>&1 || true

((failed)) && fDie "analyzer stage"
fEcho "OK: analyzer stage"

##	History:
##		- 2026-10-21: First version, report only.
##		- 2026-10-24: Vendored suppression list.
##		- 2026-10-26: The Linux lane runs in step containers, not nemo-build,
##		  and the Windows lane in its own, not nemo-winbuild.
##		- 2026-11-08: The gate goes live: a finding left, or a stale entry,
##		  fails the stage.
##		- 2026-11-08: The cross-build image has clang, so the clang-win lane
##		  runs, and an analyzer missing from either image fails the stage.
##		- 2026-11-08: analyze-slow.txt for the files gcc can't finish inside the
##		  cap, and a file cut off fails the stage.
##		- 2026-11-10: --ctu, the cross-file clang run.
