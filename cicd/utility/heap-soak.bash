#!/usr/bin/env bash

##	- Purpose: The heap growth soak, under --leakcheck. The debug build runs on a
##	  headless display under heaptrack, with a probe preloaded that opens and
##	  closes tabs, folders, and thumbnails in a loop
##	  (source/test/test-nemo-heap-soak-probe.c). It fails when the heap after
##	  round 2N is more than SOAK_MARGIN bytes above the heap after round N.
##	- First the same run with a planted loop that keeps SOAK_PLANT bytes a
##	  round, which has to fail, so a soak that can't see growth never passes.
##	- Plain build, in the dev image: heaptrack and ASan can't share a build.
##	- Each run's heaptrack data, report and app log are in
##	  cicd/artifacts/heap-soak/last/, and each real run also keeps its report
##	  as cicd/artifacts/heap-soak/report_<stamp>.txt, GFS-rotated.
##	- Env: SOAK_ROUNDS (N, default 12), SOAK_MARGIN (default 393216),
##	  SOAK_PLANT (default 98304). The real run grew 74 to 192 KB from round
##	  12 to 24, and 74 KB from 30 to 60, so that is caches filling, not a
##	  leak a round. A leak smaller than the margin is the leak cases' to find.
##	- --planted-only runs only the planted run, and --soak-only only the real
##	  one, for a look by hand.
##	- Syntax: cicd/utility/heap-soak.bash [--planted-only|--soak-only]
##	- Test ID: de8er
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
# shellcheck source=include/run-temp.bash
source "${here}/include/run-temp.bash"

rounds="${SOAK_ROUNDS:-12}"
margin="${SOAK_MARGIN:-393216}"
plant="${SOAK_PLANT:-98304}"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# In the container: one soak run, its logs on stderr and a tar of its results
# on stdout.

fInner(){
	local plantBytes="$1" rounds="$2" work disp app probe rc=0
	# shellcheck source=include/xvfb.bash
	source "${here}/include/xvfb.bash"
	command -v heaptrack >/dev/null || { echo "no heaptrack in the image" >&2; exit 77; }

	nice -n 19 ninja -C /build -j "${CICD_MAX_JOBS:-1}" src/nemo-anywhere test/libtest-nemo-heap-soak-probe.so >&2
	app=/build/src/nemo-anywhere
	probe=/build/test/libtest-nemo-heap-soak-probe.so

	work="$(mktemp -d)"
	mkdir -p "${work}/tree/pics" "${work}/home" "${work}/out"
	## Mixed sizes, so the thumbnailer scales each one.
	local i
	for i in $(seq 1 24); do
		convert -size "$((40 + i * 9))x$((30 + i * 7))" "plasma:" "${work}/tree/pics/pic-${i}.png" 2>/dev/null \
			|| printf 'P1\n2 2\n1 0 0 1\n' > "${work}/tree/pics/pic-${i}.pbm"
	done
	printf 'text\n' > "${work}/tree/note.txt"

	fXvfbStart :120 1280x900x24 "${work}/xvfb.log" >&2 || { echo "no headless display" >&2; exit 1; }
	disp="$XVFB_DISPLAY"

	DISPLAY="$disp" HOME="${work}/home" XDG_CONFIG_HOME="${work}/home/.config" \
		XDG_CACHE_HOME="${work}/home/.cache" XDG_DATA_HOME="${work}/home/.local/share" \
		DBUS_SESSION_BUS_ADDRESS="disabled:" NEMO_TESTGUARD_ALL_DELETES=0 \
		GLIBC_TUNABLES="glibc.malloc.tcache_count=0:glibc.malloc.arena_max=1" \
		G_SLICE=always-malloc G_DEBUG=gc-friendly \
		NEMO_SOAK_OUT="${work}/out/soak.txt" NEMO_SOAK_ROUNDS="$rounds" NEMO_SOAK_PLANT="$plantBytes" \
		LD_PRELOAD="$probe" \
		timeout 900 nice -n 19 heaptrack -o "${work}/out/heaptrack" "$app" --geometry=900x700 "${work}/tree/pics" \
		> "${work}/out/app.log" 2>&1 || rc=$?
	kill "$XVFB_PID" 2>/dev/null || true
	echo "exit ${rc}" >> "${work}/out/soak.txt"
	local data
	for data in "${work}/out/heaptrack.zst" "${work}/out/heaptrack.gz" ""; do
		[[ ! -f "$data" ]] || break
	done
	if [[ -n "$data" ]]; then
		heaptrack_print --print-leaks 1 --peak-limit 20 --print-allocators 0 --print-temporary 0 \
			"$data" > "${work}/out/report.txt" 2>&1 || true
	fi
	tar -C "${work}/out" -cf - .
}

case "${1:-}" in
	--inner) fInner "${2:?}" "${3:?}"; exit 0 ;;
esac


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# On the host

out="${root}/cicd/artifacts/heap-soak"
last="${out}/last"
mkdir -p "$out"

## Prints the growth from round N to 2N, or fails with why.
fRun(){
	local name="$1" plantBytes="$2" dir="${last}/$1" n a b
	fSafeRm_Under "${root}/cicd/artifacts" "$dir"; mkdir -p "$dir"
	# shellcheck disable=SC2016  ## expanded in the step's container
	bash "${here}/docker-run.bash" "heap soak ${name}" \
		"bash /src/cicd/utility/heap-soak.bash --inner ${plantBytes} ${rounds}" | tar -C "$dir" -xf - \
		|| { fEcho "FAILED: ${name}: the soak run itself failed" >&2; return 2; }
	grep -q '^done$' "${dir}/soak.txt" 2>/dev/null \
		|| { fEcho "FAILED: ${name}: the probe didn't finish; last lines of the app:" >&2; tail -5 "${dir}/app.log" >&2; return 2; }
	n="$(awk -v r="$rounds" '$1 == "heap" && $2 == r { print $3 }' "${dir}/soak.txt")"
	a="$(awk -v r="$((2 * rounds))" '$1 == "heap" && $2 == r { print $3 }' "${dir}/soak.txt")"
	[[ -n "$n" && -n "$a" ]] || { fEcho "FAILED: ${name}: no heap readings" >&2; return 2; }
	b=$((a - n))
	fEcho_Clean "${name}: the heap grew ${b} bytes from round ${rounds} to $((2 * rounds)) (margin ${margin})" >&2
	echo "$b"
}

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
failed=0

if [[ "${1:-}" != "--soak-only" ]]; then
	fSection "Heap soak: planted growth of ${plant} bytes a round"
	grew="$(fRun planted "$plant")" || failed=1
	if ((! failed)) && ((grew <= margin)); then
		fEcho "FAILED: the soak missed a planted growth of ${plant} bytes a round"
		failed=1
	fi
fi

if [[ "${1:-}" != "--planted-only" ]] && ((! failed)); then
	fSection "Heap soak: ${rounds} rounds, then ${rounds} more"
	if grew="$(fRun soak 0)"; then
		((grew <= margin)) || { fEcho "FAILED: the heap grew ${grew} bytes, over the margin of ${margin}"; failed=1; }
		stamp="$(date +%Y%m%d-%H%M%S)"
		cp -f "${last}/soak/report.txt" "${out}/report_${stamp}.txt" 2>/dev/null || true
		# shellcheck source=include/gfs-rotate.bash
		source "${here}/include/gfs-rotate.bash"
		gfs_rotate "${out}" report txt >/dev/null 2>&1 || true
	else
		failed=1
	fi
fi

if ((failed)); then
	echo "FAIL de8er heap growth soak"
	fDie "heap soak"
fi
echo "PASS de8er heap growth soak"
fEcho "OK: heap soak"
fEcho_Clean
