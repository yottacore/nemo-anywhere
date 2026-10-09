#!/usr/bin/env bash

##	- Purpose: Check that the fuzz stage reports a hang, a memory error and
##	  running out of memory each as a find with its saved input, and still runs
##	  the targets after one.
##	- Runs a copy of fuzz.bash in the nemo-build container, with meson and ninja
##	  stood in for. The targets are small real libFuzzer programs that hang,
##	  overflow a buffer or ask for too much memory on any input.
##	- Needs docker, the nemo-build container, and clang with libFuzzer in it;
##	  exit 77 without them.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-fuzz-exit.bash
##	- Test ID: rjcbfcx7

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

fEcho(){ echo "[ $* ]"; }

if [[ "${1:-}" != "--inside" ]]; then
	echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"
	here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	if ! command -v docker >/dev/null 2>&1 || ! timeout 10 docker inspect nemo-build >/dev/null 2>&1; then
		fEcho "fuzz exit check skipped: no nemo-build container"
		exit 77
	fi
	## Copies, not the container's /src, which may be another clone.
	rc=0
	# shellcheck disable=SC2016  ## expanded by the shell in the container
	tar -C "${here}/.." -cf - linux/fuzz.bash linux/test-fuzz-exit.bash utility/check-werror.bash utility/meson-setup.bash \
		| timeout -k 5 360 docker exec -i nemo-build bash -c \
			'd="$(mktemp -d /tmp/test_fuzz-exit.XXXXXX)" && tar -C "$d" -xf - && { rc=0; bash "$d/linux/test-fuzz-exit.bash" --inside "$d/linux" || rc=$?; rm -rf -- "$d"; exit "$rc"; }' \
		|| rc=$?
	exit "${rc}"
fi

work="${2:?}"
stubs="${work}/bin"
progs="${work}/progs"
mkdir -p "${stubs}" "${progs}"

if ! bash "${work}/fuzz.bash" --probe >/dev/null 2>&1; then
	fEcho "fuzz exit check skipped: no clang or libFuzzer in nemo-build"
	exit 77
fi

fBuild(){
	printf '#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\nint LLVMFuzzerTestOneInput(const uint8_t *d, size_t n){(void)d;(void)n;%s return 0;}\n' "$2" >"${progs}/$1.c"
	clang -g -fsanitize=fuzzer,address -o "${progs}/$1" "${progs}/$1.c"
}
fBuild clean ''
fBuild hang 'for (;;) pause ();'
fBuild asan 'char *p = malloc (4); volatile size_t i = n + 8; p[i] = 1; free (p);'
fBuild oom 'size_t s = (size_t) 3 << 30; char *p = malloc (s); if (p) memset (p, 1, s); free (p);'

## The stage reads the dir back for -Werror once it is set up.
cat >"${stubs}/meson" <<'EOF'
#!/usr/bin/env bash
mkdir -p "${BUILD_DIR:?}" && printf ' ARGS = -Wextra -Werror\n' > "${BUILD_DIR}/build.ninja"
EOF

## Each target becomes one of the programs above. The hang is second in line,
## so the targets after it show whether the stage carried on.
cat >"${stubs}/ninja" <<'EOF'
#!/usr/bin/env bash
build=""
targets=()
while (($#)); do
	case "$1" in
		-C) build="$2"; shift 2 ;;
		-j) shift 2 ;;
		*) targets+=("$1"); shift ;;
	esac
done
mkdir -p "${build}/fuzz"
for t in "${targets[@]}"; do
	name="${t#fuzz/}"
	case "${name}" in
		fuzz-dnd) kind=hang ;;
		fuzz-xls) kind=asan ;;
		fuzz-psd) kind=oom ;;
		*) kind=clean ;;
	esac
	cp "${STUB_PROGS:?}/${kind}" "${build}/fuzz/${name}"
done
EOF
chmod +x "${stubs}/meson" "${stubs}/ninja"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## A hang the stage misses runs for libFuzzer's own 20 minutes, hence the cap.
## It is here rather than around docker exec, so it takes the fuzzer down too.
build="${work}/build"
rc=0
out="$(env PATH="${stubs}:${PATH}" STUB_PROGS="${progs}" BUILD_DIR="${build}" FUZZ_SECS=1 FUZZ_TIMEOUT=2 \
	timeout -k 5 240 bash "${work}/fuzz.bash" 2>&1)" || rc=$?

[[ "${rc}" == "86" ]] || fFail "stage exited ${rc}, not 86 for a find"

## <target> <OK|FOUND> [kind of saved input]
fExpect(){
	local line
	line="$(grep -E "^[0-9]+/[0-9]+ [a-z0-9]+ +$1 " <<<"${out}" || true)"
	if [[ -z "${line}" ]]; then
		fFail "$1 did not run"
	elif [[ "${line}" != *" $2 "* ]]; then
		fFail "$1 not $2: ${line}"
	elif [[ -n "${3:-}" ]] && [[ "${line}" != *"/$1-$3-"* ]]; then
		fFail "$1 did not name its $3 input: ${line}"
	elif [[ -n "${3:-}" ]] && ! compgen -G "${build}/findings/$1-$3-*" >/dev/null; then
		fFail "$1 left no $3 input under findings"
	fi
	return 0
}
fExpect fuzz-shcl OK
fExpect fuzz-dnd FOUND timeout
fExpect fuzz-command-template OK
fExpect fuzz-xls FOUND crash
fExpect fuzz-ppt OK
fExpect fuzz-doc OK
fExpect fuzz-psd FOUND oom
fExpect fuzz-raw OK
fExpect fuzz-office OK
fExpect fuzz-lnk OK
fExpect fuzz-lnk-edit OK

if ((failures)); then
	grep -E '^[0-9]+/[0-9]+ |^\[' <<<"${out}" || true
	fEcho "FAILED: fuzz exit check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: fuzz exit check"
