#!/usr/bin/env bash

##	- Purpose: Check the nemo-lint image helper with a stand-in docker: the
##	  tag follows Dockerfile.lint, old tags go, every way of not having the
##	  image falls back to the host tools, and a worktree's git dir is mounted.
##	  Also that Dockerfile.lint pins its base and checks every download, and
##	  that lint.bash runs each tool checker through the helper.
##	- Needs git only.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-lint-image.bash
##	- Test ID: rjxyddbk

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
dockerfile="${root}/cicd/linux/Dockerfile.lint"

fEcho(){ echo "[ $* ]"; }
failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

text="$(cat "$dockerfile")"
grep -qE '^FROM [^ ]+@sha256:[0-9a-f]{64}$' <<<"$text" || fFail "Dockerfile.lint: base image not pinned by digest"
downloads="$(grep -c 'curl -fsSL' <<<"$text" || true)"
checked="$(grep -c 'sha256sum -c' <<<"$text" || true)"
[[ "$downloads" == "$checked" && "$downloads" != "0" ]] || fFail "Dockerfile.lint: ${downloads} download(s), ${checked} checked"
grep -q -- '--require-hashes' <<<"$text" || fFail "Dockerfile.lint: pip without --require-hashes"
unhashed="$(grep -E "^[[:space:]]*'[A-Za-z_]+==" <<<"$text" | grep -v -- '--hash=sha256:' || true)"
[[ -z "$unhashed" ]] || fFail "Dockerfile.lint: requirement with no hash: ${unhashed}"

lint="$(cat "${here}/lint.bash")"
for c in lint-c lint-bash lint-python lint-powershell; do
	grep -qF "fLintRun \"\$root\" \"\$img\" bash \"\${here}/${c}.bash\"" <<<"$lint" || fFail "lint.bash: ${c} not run through fLintRun"
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/test_lint-image_$(date +%Y%m%d-%H%M%S%2N).XXXXXX")"
trap '[[ "$scratch" == */test_lint-image_* ]] && rm -rf "${scratch}"' EXIT
mkdir -p "${scratch}/bin" "${scratch}/repo/cicd/linux"
cp "$dockerfile" "${scratch}/repo/cicd/linux/"
log="${scratch}/docker.log"

## Answers from env: STUB_UP, STUB_HAVE (tags present), STUB_BUILD, STUB_IMAGES.
cat >"${scratch}/bin/docker" <<'EOF'
#!/usr/bin/env bash
echo "$*" >>"$STUB_LOG"
case "$1" in
	info) [[ "${STUB_UP:-1}" == 1 ]] ;;
	image) [[ " ${STUB_HAVE:-} " == *" $3 "* ]] ;;
	build) cat >/dev/null; [[ "${STUB_BUILD:-1}" == 1 ]] ;;
	images) printf '%s\n' ${STUB_IMAGES:-} ;;
	*) exit 0 ;;
esac
EOF
chmod +x "${scratch}/bin/docker"

# shellcheck source=include/lint-image.bash
source "${here}/include/lint-image.bash"
hash="$(sha256sum "${scratch}/repo/cicd/linux/Dockerfile.lint")"
want="nemo-lint:${hash:0:12}"

fCase(){
	local name="$1" expect="$2"; shift 2
	: >"$log"
	local got
	# shellcheck disable=2016  ## $1 and $2 belong to the inner bash
	got="$(env PATH="${scratch}/bin:${PATH}" STUB_LOG="$log" "$@" bash -c 'source "$1"; fLintImage "$2"' _ "${here}/include/lint-image.bash" "${scratch}/repo" 2>/dev/null || true)"
	[[ "$got" == "$expect" ]] || fFail "${name}: got '${got}', wanted '${expect}'"
}

fCase "image there" "$want" STUB_HAVE="$want"
grep -q '^build' "$log" && fFail "image there: built anyway"

fCase "first run builds" "$want" STUB_IMAGES="nemo-lint:0ld0ld0ld0ld ${want}"
grep -q "^build -q -t ${want} -" "$log" || fFail "first run builds: no build of ${want}"
grep -q '^rmi nemo-lint:0ld0ld0ld0ld$' "$log" || fFail "first run builds: old tag left"
grep -q "^rmi ${want}$" "$log" && fFail "first run builds: removed the new tag"
grep -q '^rmi -f' "$log" && fFail "first run builds: forced removal"

fCase "build fails" "" STUB_BUILD=0
fCase "daemon down" "" STUB_UP=0
fCase "NEMO_LINT_HOST=1" "" NEMO_LINT_HOST=1 STUB_HAVE="$want"

## A worktree's git dir lives in the main clone, outside what the run mounts.
git init -q "${scratch}/main"
git -C "${scratch}/main" -c user.name=t -c user.email=t@t commit -q --allow-empty -m x
git -C "${scratch}/main" worktree add -q --detach "${scratch}/wt"
for r in main wt; do
	: >"$log"
	PATH="${scratch}/bin:${PATH}" STUB_LOG="$log" fLintRun "${scratch}/${r}" img:x true
	seen="$(grep -o -- "-v [^ ]*" "$log" | tr '\n' ' ')"
	case "$r" in
		main) want_m="-v ${scratch}/main:${scratch}/main " ;;
		wt) want_m="-v ${scratch}/wt:${scratch}/wt -v ${scratch}/main/.git:${scratch}/main/.git " ;;
	esac
	[[ "$seen" == "$want_m" ]] || fFail "fLintRun ${r}: mounts '${seen}', wanted '${want_m}'"
done

## With no image the command runs here.
out="$(fLintRun "${scratch}/main" "" echo on-host)"
[[ "$out" == "on-host" ]] || fFail "fLintRun with no image: '${out}'"

((failures == 0)) || exit 1
fEcho "OK: lint image helper"
