#!/usr/bin/env bash

##	- Purpose: Build the fuzz targets against libFuzzer and run each one for a
##	  bounded time over its seed corpus. Handed to docker-run.bash by cicd's fuzz
##	  stage; not meant to be run on the host.
##	- Needs clang. Without it, or without the libFuzzer runtime, --probe fails and
##	  the stage skips with a warning instead of aborting the run. The ordinary gcc
##	  build still compiles the same targets and replays the seeds as tests.
##	- A time budget running out is NOT a failure: libFuzzer stops and exits 0. A
##	  real find, whether a crash, a hang, a leak or running out of memory, saves
##	  its input under findings/, and the other targets still run. Those are told
##	  apart by the exit code and the saved input, never by the run having ended.
##	- FUZZ_SECS sets the per-target budget (default 60). FUZZ_TIMEOUT is how long
##	  one input may run before it counts as a hang (default 25). BUILD_DIR
##	  overrides the build directory.
##	- Syntax: fuzz.bash [--probe]

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

build="${BUILD_DIR:-/build-fuzz}"
secs="${FUZZ_SECS:-60}"
hangSecs="${FUZZ_TIMEOUT:-25}"
src="/src/source"

## Distinct on purpose: 1 is a build or usage problem, 77 is the tree's "could
## not run", and this is a crasher. Nothing else uses it.
declare -i FUZZ_FIND_CODE=86

## Each target's test ID, the target, and where its seeds live.
targets=(
	"rh0808bb|fuzz-shcl|shcl"
	"rh0808bc|fuzz-dnd|dnd"
	"rh0808bd|fuzz-command-template|command-template"
	"rhatwe0v|fuzz-xls|xls"
	"rhatwe0w|fuzz-ppt|ppt"
	"rhatwe0x|fuzz-doc|doc"
	"rhe0xz33|fuzz-psd|psd"
	"rhqmm0as|fuzz-raw|raw"
	"rhmxm5aj|fuzz-lnk|lnk"
	"rjfa5fnh|fuzz-lnk-edit|lnk-edit"
)

case "${1:-}" in
	-h|--help) sed -n '/^##\t- Purpose:/,/^##\tCopyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##\t\{0,1\}//'; exit 0 ;;
esac

fProbe() {
	command -v clang >/dev/null 2>&1 || { echo "clang not installed" >&2; return 1; }

	## Having clang is not the same as having the libFuzzer runtime, which is a
	## separate package on some distros. Compile the smallest thing that needs it.
	local probeDir
	probeDir="$(mktemp -d)"
	printf '#include <stdint.h>\n#include <stddef.h>\nint LLVMFuzzerTestOneInput(const uint8_t *d, size_t n){(void)d;(void)n;return 0;}\n' > "${probeDir}/probe.c"

	local ok=0
	clang -fsanitize=fuzzer -o "${probeDir}/probe" "${probeDir}/probe.c" >/dev/null 2>&1 || ok=1

	rm -rf -- "${probeDir}"

	if ((ok)); then
		echo "clang is present but cannot link -fsanitize=fuzzer (libFuzzer runtime missing?)" >&2
		return 1
	fi

	return 0
}

if [[ "${1:-}" == "--probe" ]]; then
	fProbe
	exit $?
fi

fProbe || { echo "[ fuzz stage cannot run here ]" >&2; exit 77; }

case "${secs}" in
	''|*[!0-9]*|0*) secs=60 ;;
esac
## libFuzzer's own default is 20 minutes, far past the whole stage's budget.
case "${hangSecs}" in
	''|*[!0-9]*|0*) hangSecs=25 ;;
esac

## A sanitized tree of its own, so the ordinary /build is left alone and the
## next plain ninja does not have to rebuild the world. Coverage has to reach the
## code a target calls into, so fuzzer-no-link goes on the whole project and the
## targets add the linker half. It is passed here because meson warns about any
## -fsanitize set from meson.build.
setupArgs=(-Dfuzzing=true -Db_sanitize=address -Db_lundef=false -Dc_args=-fsanitize=fuzzer-no-link)
if [[ -f "${build}/build.ninja" ]] && grep -qF -- '-fsanitize=fuzzer-no-link' "${build}/build.ninja"; then
	CC=clang meson setup --reconfigure "${build}" "${src}" "${setupArgs[@]}"
elif [[ -f "${build}/build.ninja" ]]; then
	## meson 1.7 ignores a c_args added on reconfigure, so a tree set up
	## without it starts over.
	CC=clang meson setup --wipe "${build}" "${src}" "${setupArgs[@]}"
else
	CC=clang meson setup "${build}" "${src}" "${setupArgs[@]}"
fi

## Only the targets themselves. Building the whole tree here costs minutes and
## drags in the extension library, which has no business in a fuzzing build.
ninjaTargets=()
for entry in "${targets[@]}"; do
	rest="${entry#*|}"
	ninjaTargets+=("fuzz/${rest%%|*}")
done

ninja -C "${build}" -j "${NEMO_TEST_JOBS:-2}" "${ninjaTargets[@]}"

findings="${build}/findings"
logs="${build}/logs"
mkdir -p "${findings}" "${logs}"

## The shortcut edit target writes a file per input, and GLib syncs the file it
## replaces. On the container's disk that held it to about 90 runs a second,
## against about 4000 on tmpfs.
if [[ -d /dev/shm && -w /dev/shm ]]; then
	export TMPDIR=/dev/shm
fi

declare -i found=0 num=0

## libFuzzer's own output runs to thousands of lines a target, so it goes to a
## log and the console gets one line per target, the way meson reports a test.
## The end of the log is shown only when something went wrong.
fStat(){ sed -n "s/^stat::${1}: *//p" "${2}" | tail -n 1; }

for entry in "${targets[@]}"; do
	id="${entry%%|*}"
	rest="${entry#*|}"
	name="${rest%%|*}"
	seedName="${rest##*|}"
	seeds="${src}/fuzz/corpus/${seedName}"
	bin="${build}/fuzz/${name}"
	log="${logs}/${name}.log"
	num=$((num + 1))

	## libFuzzer writes back into the first corpus directory it is given, and the
	## seeds are checked in, so it gets a copy to scribble on.
	work="${build}/work/${seedName}"
	rm -rf -- "${work}"
	mkdir -p "${work}"
	cp -- "${seeds}"/* "${work}/"

	## Each kind of find has its own exit code. Without these a crash and a bad
	## argument both come back as 1, and the stage cannot tell which happened:
	## -error_exitcode covers what libFuzzer reports itself, -timeout_exitcode a
	## hang (70 otherwise), and ASan's exitcode a memory error (1 otherwise).
	## Running out of memory is always 71 and has no setting, so a failed run
	## that saved an input counts as a find too.
	touch "${log}.start"
	set +e
	ASAN_OPTIONS="${ASAN_OPTIONS:+${ASAN_OPTIONS}:}exitcode=${FUZZ_FIND_CODE}" "${bin}" "${work}" \
		-max_total_time="${secs}" \
		-timeout="${hangSecs}" \
		-error_exitcode="${FUZZ_FIND_CODE}" \
		-timeout_exitcode="${FUZZ_FIND_CODE}" \
		-artifact_prefix="${findings}/${name}-" \
		-print_final_stats=1 >"${log}" 2>&1
	rc=$?
	set -e
	saved=""
	((rc == 0)) || saved="$(find "${findings}" -maxdepth 1 -type f -name "${name}-*" -newer "${log}.start" -print -quit 2>/dev/null || true)"

	runs="$(fStat number_of_executed_units "${log}" || true)"
	added="$(fStat new_units_added "${log}" || true)"
	rss="$(fStat peak_rss_mb "${log}" || true)"
	printf -v head '%d/%d %s %-24s' "${num}" "${#targets[@]}" "${id}" "${name}"

	if ((rc == 0)); then
		echo "${head} OK      ${secs}s  ${runs:-?} runs  ${added:-?} new inputs  ${rss:-?} MB peak"
	elif ((rc == FUZZ_FIND_CODE)) || [[ -n "${saved}" ]]; then
		echo "${head} FOUND   ${saved:-input under ${findings}}" >&2
		tail -n 40 "${log}" >&2
		found=$((found + 1))
	else
		echo "${head} ERROR   exited ${rc}, which is neither a clean run nor a find" >&2
		tail -n 40 "${log}" >&2
		exit 1
	fi
done

if ((found)); then
	echo "[ ${found} target(s) found a crash or hang; the input files are under ${findings} ]" >&2
	exit "${FUZZ_FIND_CODE}"
fi

echo "OK: fuzz targets clean for ${secs}s each"
