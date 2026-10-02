#!/usr/bin/env bash

## The template's file-wide shellcheck header is gone: none of those rules
## fired here, and while it was up it hid two dead variables. A rule that
## really needs to be off goes off at the line that needs it, with a reason.

##	- Purpose: Local CI/CD pipeline. Generic engine, per-project settings live in config.bash.
##	  The stage sequence is the enduring shape; the tools that fill each stage are
##	  per-project (config.bash). A stage self-skips when its config vars are empty,
##	  so stages nemo-anywhere can't do yet are simply left unset (see config.bash).
##	- Stages (fail-fast, any error aborts before the next stage):
##	   0. remote sync (fast-forward from upstream before anything is built)
##	   1. format (source formatter)
##	   2. debug build (this is what the tests + profiler run against)
##	   3. regression tests + lints (+ any headless harness)
##	   4. profiler (flamegraph SVG; non-gating artifact - see failure policy)
##	   5. release build (native + cross targets; optimized, for packaging + dogfood)
##	   6. packages (per-OS distributables)
##	   7. dogfood (install native release locally)
##	   8. backup + publish to git (runs from repo root)
##	- Syntax:
##	  cicd/cicd.bash [options]
##	  Options:
##	   -y, --yes           run unattended (no confirm prompt)
##	   -q, --quiet         same as -y; the publish step runs quiet either way
##	   -m, --message MSG   publish hands-off with this commit message (no editor)
##	       --msg MSG       alias for --message
##	   --no-sync           skip the remote sync stage
##	   --no-fmt            skip the formatter stage
##	   --no-cross          skip cross-target release builds
##	   --no-arm            skip the ARM64 release builds + packages (x86_64 only)
##	   --no-package        skip the packages stage (.deb/.rpm/installer)
##	   --no-private        skip the private runner (PRIVATE_RUNNER in config.bash)
##	   --no-profile        skip the profiler stage
##	   --no-dogfood        skip installing the native release locally
##	   --no-publish        skip the git backup + publish stage
##	   --allow-dirty       let publish commit an uncommitted working tree (it
##	                       refuses one by default)
##	   --shots             refresh README screenshots (off by default)
##	   --demo              re-record the demo video (off by default)
##	   --quick             skip the slow stages: cross builds, packages, private
##	                       runner, profiler, fuzzing, scroll harness, screenshots,
##	                       demo video, remote dogfood. The native build, the full
##	                       test suite and local dogfood still run.
##	   --gate              merge gate only: format-check + lints + tests, then exit
##	                       (fast local stand-in for hosted CI; the pre-push hook runs it)
## - Reuse: copy the cicd/ directory into another project and edit config.bash.

##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

## Help before anything else runs: the CPU scope and the container caps below
## would otherwise go on just to print usage.
for arg in "$@"; do case "$arg" in
	-h|--help) sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
esac; done

## Find the repo root and load project config.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/.." && pwd)"   # the git repo root (cicd/..)
## Per-user bin dirs, so a run over ssh or from cron finds the same helpers an
## interactive shell does - a non-interactive ssh gets a bare PATH and would
## otherwise silently skip the lints (cppcheck) and the profiler (the cargo-installed
## inferno-flamegraph). Cargo's goes last so its toolchain shadows nothing.
export PATH="${HOME}/.local/bin:${PATH}:${HOME}/.cargo/bin"
## Cap build parallelism to at most half the cores so a pipeline run doesn't peg
## every CPU and leaves the machine usable. Computed BEFORE config.bash is read,
## so the build commands there can interpolate it (ninja -j "${CICD_MAX_JOBS}");
## ninja on its own would take cores+2. Override by exporting it beforehand.
cores="$(nproc 2>/dev/null || echo 2)"
: "${CICD_MAX_JOBS:=$(( cores / 2 ))}"
(( CICD_MAX_JOBS >= 1 )) || CICD_MAX_JOBS=1
export CICD_MAX_JOBS

## Job counts alone do not hold a run to half the machine: link-time optimization
## runs threads of its own under each ninja job, and rar uses every core. So the
## whole run goes inside one user scope with a CPU quota, which everything it
## starts inherits. With no user systemd manager only the job counts apply.
if [[ -z "${CICD_CPU_CAPPED:-}" ]] && command -v systemd-run >/dev/null 2>&1 \
	&& systemd-run --user --scope -q true >/dev/null 2>&1; then
	export CICD_CPU_CAPPED=1
	exec systemd-run --user --scope -q -p "CPUQuota=$(( cores * 50 ))%" bash "${BASH_SOURCE[0]}" "${@}"
fi

source "${here}/config.bash"

## A container's processes sit in docker's cgroup, not in this run's scope, so
## each gets the same cap directly. It sticks to the container, which is the
## point: a build run by hand in there is held to it too.
declare -p CICD_CONTAINERS &>/dev/null || CICD_CONTAINERS=()
for ctr in "${CICD_CONTAINERS[@]}"; do
	if docker inspect "$ctr" >/dev/null 2>&1; then
		docker update --cpus "${CICD_MAX_JOBS}" "$ctr" >/dev/null 2>&1 || true
	fi
done
source "${here}/utility/include/gfs-rotate.bash"                  ## gfs_rotate() for the profiler artifacts
source "${here}/utility/include/source-date.bash"                 ## fSetSourceDate() for reproducible builds
declare -p FMT_CMD &>/dev/null || FMT_CMD=()                      ## tolerate a config without the fmt stage
## NOT-READY (Rust origin): the source pipeline also exported CARGO_BUILD_JOBS and
## RUST_TEST_THREADS here to bound cargo's jobserver and the test run. No cargo in a
## meson/ninja build - parallelism is bounded inside the build command instead.
#export CARGO_BUILD_JOBS="${CICD_MAX_JOBS}"
#export RUST_TEST_THREADS="${CICD_MAX_JOBS}"

cd "${root}"
## Log and artifact names are the one place the wall clock still belongs; anything
## a build stamps into its output reads SOURCE_DATE_EPOCH instead.
stamp="$(date +%Y%m%d-%H%M%S)"
fSetSourceDate "${root}"

## Parse options.
assume_yes=0; quick=0; gate=0; no_arm=0; no_sync=0; allow_dirty=0; cli_message=""
while (($#)); do case "$1" in
	-y|--yes)                 assume_yes=1; shift ;;
	-q|--quiet)               assume_yes=1; shift ;;            ## publish runs quiet whatever this says
	--gate)                   gate=1; shift ;;                  ## merge gate only, then exit
	--no-sync)                no_sync=1; shift ;;
	--no-fmt)                 FMT_CMD=(); shift ;;
	--no-cross)               BUILD_CROSS=0; shift ;;
	--no-arm)                 no_arm=1; shift ;;                ## drop ARM64 builds + packages
	--no-package)             PACKAGE_ENABLE=0; shift ;;
	--no-private)             PRIVATE_RUNNER=""; shift ;;
	--no-profile)             PROFILE_ENABLE=0; shift ;;
	--no-dogfood)             DOGFOOD_FIXED_DESTS=(); DOGFOOD_ROTATING_DESTS=(); DOGFOOD_CROSS_DESTS=(); DOGFOOD_HOOK=(); DOGFOOD_REMOTE=(); shift ;;
	--no-publish)             GIT_PUBLISH=(); shift ;;
	--allow-dirty)            allow_dirty=1; shift ;;
	--shots)                  SHOTS_ENABLE=1; shift ;;
	--demo)                   DEMO_ENABLE=1; shift ;;
	--quick)                  quick=1; BUILD_CROSS=0; PROFILE_ENABLE=0; PACKAGE_ENABLE=0; shift ;;   ## skip the slow stages
	--message=*|--msg=*|-m=*) cli_message="${1#*=}"; shift ;;
	-m|--message|--msg)       cli_message="${2-}"; shift; (($#)) && shift ;;
	-h|--help)                sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
esac; done

## --no-arm: drop the ARM64 cross targets so the run (and its packages) stay
## x86_64-only. Native x86_64 is untouched; the Windows/Linux x86_64 crosses stay.
if ((no_arm)) && declare -p CROSS_TARGETS &>/dev/null; then
	kept=()
	for t in "${CROSS_TARGETS[@]}"; do case "$t" in *arm64*|*aarch64*) ;; *) kept+=("$t") ;; esac; done
	CROSS_TARGETS=("${kept[@]}")
fi
declare -p PACKAGE_ENABLE &>/dev/null || PACKAGE_ENABLE=0        ## tolerate a config predating the packages stage
declare -p DOGFOOD_CROSS_DESTS &>/dev/null || DOGFOOD_CROSS_DESTS=()   ## ditto, cross dogfood
declare -p DOGFOOD_HOOK &>/dev/null || DOGFOOD_HOOK=()                 ## ditto, post-dogfood hook
declare -p DOGFOOD_REMOTE &>/dev/null || DOGFOOD_REMOTE=()             ## ditto, dogfood built on another box

## Publish commit message: -m wins, then config, then whatever the auto-message
## helper makes of the tree when unattended - one place owns the wording, so this
## and the publisher cannot disagree about it.
## Empty -> publish interactively (git commit opens an editor); when interactive
## we offer to capture a message at the preflight prompt below.
publish_msg=""
if   [[ -n "$cli_message" ]];              then publish_msg="$cli_message"
elif [[ -n "${PUBLISH_AUTO_MESSAGE:-}" ]]; then publish_msg="$PUBLISH_AUTO_MESSAGE"
elif ((assume_yes));                       then publish_msg="$(bash "${here}/utility/git-auto-msg.bash" --suggest)"
fi

## Output helpers: fEcho / fEcho_Clean, blank-collapsing.
## fEcho "msg" -> "[ msg ]" status line; fEcho_Clean "msg" -> plain line, and a
## bare call collapses repeated blanks. fSection draws the leading-blank + rule
## letterbox before a major stage header; fDie prints a fatal line and exits.
declare -i _wasLastEchoBlank=0
fEcho_ResetBlankCounter(){ _wasLastEchoBlank=0; }
fEcho_Clean(){ if [[ -n "${1:-}" ]]; then echo -e "$*"; _wasLastEchoBlank=0; elif [[ $_wasLastEchoBlank -eq 0 ]] && echo; then _wasLastEchoBlank=1; fi; }
fEcho(){       if [[ -n "$*"     ]]; then fEcho_Clean "[ $* ]"; else fEcho_Clean ""; fi; }
fEcho_Force(){ fEcho_ResetBlankCounter; fEcho "$*"; }
_letterbox="••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••"
fSection(){ fEcho_Clean; fEcho_Clean "${_letterbox}"; fEcho "$*"; }
fDie(){ { fEcho_Force "FAILED: $*"; } >&2; exit 1; }
## Git that talks to the remote goes through gitsby where it is installed, so the
## fetch and pull use the key and account this repo belongs to. A fresh clone
## without it gets plain git.
fGit(){ if command -v gitsby >/dev/null 2>&1; then gitsby raw git "$@"; else git "$@"; fi; }
## True if a running process is executing the given binary (its own exe, not a
## substring match), so an in-use dogfood copy isn't pruned. Checks /proc/*/exe.
in_use(){
	local -r bin="$(realpath -e "$1" 2>/dev/null || true)"
	[[ -n "$bin" ]] || return 1
	## A prefix is in use when a running image sits anywhere inside it; a single
	## binary, only when it IS the image.
	local inside=""
	[[ -d "$bin" ]] && inside="${bin}/"
	local exe
	## readlink is a builtin-cheap single syscall; realpath here forked a process
	## per running pid per candidate. /proc/*/exe is already fully resolved.
	for e in /proc/[0-9]*/exe; do
		exe="$(readlink "$e" 2>/dev/null || true)"
		[[ "$exe" == "$bin" ]] && return 0
		[[ -n "$inside" && "$exe" == "${inside}"* ]] && return 0
	done
	return 1
}
## The project version, from whichever manifest the project uses. Cargo's
##   version = "1.2.3"
## and meson's
##   version: '1.2.3'
## are both accepted, so the engine does not care which build system fills the
## stages below it.
## The lookbehind matters: meson.build carries meson_version and several
## dependency versions on the same line as the project's own, and a pattern
## without it happily returns '>=0.56.0'.
read_version(){
	local file="${root}/${VERSION_MANIFEST}" v
	[[ -f "$file" ]] || return 0
	v="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$file" | head -1)"
	[[ -n "$v" ]] || v="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "$file" | head -1)"
	printf '%s' "$v"
}

## (Re)write the sha256sums file over every artifact in the release dir except the
## sums file itself. Run after stage 5 (binaries) and again after stage 6 (packages),
## so the checksums cover the packages too. Uses the script-scope art_dir/ver/sums.
write_sums(){
	[[ -n "${art_dir:-}" && -d "${art_dir:-/nonexist}" ]] || return 0
	( cd "${art_dir}"
	  shopt -s nullglob
	  files=(); for x in "${EXE_NAME}-${ver}-"*; do [[ "$x" == "$sums" || ! -f "$x" ]] && continue; files+=("$x"); done
	  ((${#files[@]})) && sha256sum "${files[@]}" > "${sums}"
	  true )
}
## Publishing pushes work that was committed deliberately; it is not a place to
## sweep up whatever happens to be lying around. The publish stage runs a bare
## `git add --all`, which cannot tell work in progress from a finished change, so
## refuse instead. Gitignored paths (the artifacts dir the run itself writes to)
## never show up here. --allow-dirty is the one-off way back to the old behavior.
require_clean_tree_for_publish(){
	((${#GIT_PUBLISH[@]})) || return 0
	((allow_dirty)) && return 0
	local dirty
	dirty="$(git -C "${root}" status --porcelain 2>/dev/null)"
	[[ -n "$dirty" ]] || return 0
	fEcho_Clean
	fEcho_Clean "${dirty}"
	fEcho_Clean
	fEcho_Clean "Commit or stash it, or rerun with --no-publish. --allow-dirty commits it anyway."
	fDie "working tree is dirty and the publish stage would commit all of it"
}

## Refresh from upstream BEFORE the build, so what publish pushes is what was
## actually built and tested - the publish stage pulls too, but by then the
## testing is already behind us. Behind-only fast-forwards (wrapping any dirty
## tree in a stash); diverged aborts here rather than at the end; offline just
## warns. Never runs in gate mode: the gate is a pre-push hook, and a hook that
## pulls would rewrite the tree out from under the push that invoked it.
remote_sync(){
	local ahead behind stashed=0

	if ! git rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
		fEcho_Clean "no upstream for $(git rev-parse --abbrev-ref HEAD); nothing to sync"
		return 0
	fi
	if ! fGit fetch --quiet 2>/dev/null; then
		fEcho "WARNING: git fetch failed (offline?); continuing with the local tree"
		return 0
	fi

	ahead="$(git rev-list --count '@{u}..HEAD')"
	behind="$(git rev-list --count 'HEAD..@{u}')"

	if ((behind == 0)); then
		if ((ahead)); then fEcho "OK: up to date with upstream (${ahead} ahead)"
		else               fEcho "OK: up to date with upstream"; fi
		return 0
	fi
	((ahead == 0)) || fDie "diverged from upstream (${ahead} ahead, ${behind} behind) - reconcile first, or rerun with --no-sync"

	## --include-untracked, so the guard has to consider untracked files too or
	## the pull can still abort on one that upstream just added.
	if ! git diff --quiet || ! git diff --cached --quiet || [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
		fEcho_Clean "git stash push --include-untracked ..."
		git stash push --include-untracked -m "auto-stash" >/dev/null && stashed=1
	fi
	fEcho_Clean "git pull --ff-only ..."
	fGit pull --ff-only
	if ((stashed)); then
		fEcho_Clean "git stash pop ..."
		## A conflicting pop leaves the stash held and the tree half-merged. Say
		## exactly that, and how to get back, rather than aborting on the bare
		## git error - the natural rerun with --no-sync would otherwise build and
		## publish a tree missing the stashed work.
		if ! git stash pop >/dev/null; then
			fEcho_Clean
			fEcho "Your changes are still in the stash (git stash list)."
			fEcho "Resolve the conflicts, then: git stash drop"
			fEcho "Or start over:               git checkout -- . && git stash pop"
			fDie "stash pop conflicted after the pull"
		fi
	fi
	fEcho "OK: fast-forwarded ${behind} commit(s) from upstream"
}

# shellcheck disable=SC2154  ## rc is assigned in the trap body, which shellcheck reads as a plain string.
trap 'rc=$?; printf "\n[ CICD ABORTED (exit %s) at line %s: %s ]\n" "$rc" "$LINENO" "$BASH_COMMAND" >&2; exit $rc' ERR

## Warn (non-gating) when a pinned helper tool has drifted from TOOL_PINS, so a
## box update can't silently change pipeline results. Before the gate, so the
## pre-push run sees it too.
if declare -p TOOL_PINS &>/dev/null; then
	for pin in "${TOOL_PINS[@]}"; do
		pin_name="${pin%%|*}"; pin_rest="${pin#*|}"; pin_ver="${pin_rest%%|*}"; pin_cmd="${pin_rest#*|}"
		have="$(bash -c "${pin_cmd}" </dev/null 2>/dev/null | head -1 | sed 's/[^0-9.]*\([0-9][0-9.]*\).*/\1/')" || have=""
		if [[ -z "$have" ]]; then
			fEcho "WARNING: ${pin_name} not found (pinned ${pin_ver})"
		elif [[ "$have" != "$pin_ver" ]]; then
			fEcho "WARNING: ${pin_name} is ${have}, pinned ${pin_ver} (install that version, or update the pin in config.bash)"
		fi
	done
fi

## Gate mode: the local merge gate (what a bare-bones hosted CI would run).
## format-check + lints + tests, fail-fast, no artifacts/log-tee/publish. Wired as
## the pre-push hook for main, so nothing reaches the release branch unverified
## even outside a full run. The test stage builds first, since there is
## no build stage here - so what is compiled is the working tree rather than the
## sha being pushed. The hook refuses a push where the two differ. Source tree
## untouched, build directory written to.
if ((gate)); then
	fSection "Gate 1/3  Format check"
	if declare -p FMT_CHECK_CMD &>/dev/null && ((${#FMT_CHECK_CMD[@]})); then
		"${FMT_CHECK_CMD[@]}" || fDie "format check failed (run: ${FMT_CMD[*]:-cargo fmt})"
		fEcho "OK: formatting clean"
	else
		fEcho_Clean "format check skipped (no FMT_CHECK_CMD)"
	fi
	fSection "Gate 2/3  Lints"
	if [[ -n "${LINT_CMD+x}" ]] && ((${#LINT_CMD[@]})) && "${LINT_PROBE[@]}" >/dev/null 2>&1; then
		"${LINT_CMD[@]}"
		fEcho "OK: lints clean"
	else
		fEcho_Clean "lints skipped (no lint stage configured)"
	fi
	fSection "Gate 3/3  Tests"
	"${TEST_CMD[@]}"
	fEcho "OK: tests passed"
	fSection "${APP_NAME} gate: PASSED."
	fEcho_Clean
	exit 0
fi

## Release identity, resolved up front rather than inside the release stage: the
## packaging and checksum steps need it even when the binaries themselves come
## from a per-platform lane outside this engine (see config.bash).
ver="$(read_version)"
art_dir=""; sums=""
if [[ -n "${RELEASE_ARTIFACT_DIR:-}" ]]; then
	[[ -n "$ver" ]] || fDie "no version found in ${VERSION_MANIFEST}"
	art_dir="${root}/${RELEASE_ARTIFACT_DIR}"
	sums="${EXE_NAME}-${ver}-sha256sums.txt"
fi

## Preflight: show the plan with resolved paths, then confirm.
profile_dir="$(cd "${root}" && mkdir -p "${PROFILE_OUT_DIR}" 2>/dev/null; cd "${PROFILE_OUT_DIR}" 2>/dev/null && pwd || echo "${root}/${PROFILE_OUT_DIR}")"
fixed_dest=""; for d in "${DOGFOOD_FIXED_DESTS[@]:-}"; do [[ -d "$d" && -w "$d" ]] && { fixed_dest="$d"; break; }; done
rot_dest="";   for d in "${DOGFOOD_ROTATING_DESTS[@]:-}"; do [[ -d "$d" && -w "$d" ]] && { rot_dest="$d"; break; }; done
rot_target="${rot_dest:-${DOGFOOD_ROTATING_DESTS[0]:-}}"  # created in stage 6 if it doesn't exist yet
df_pattern="${DOGFOOD_PREFIX:-}_<build date>${DOGFOOD_TAG:+_${DOGFOOD_TAG}}"

fEcho_Clean
fEcho_Clean "${APP_NAME} local CI/CD"
fEcho_Clean
fEcho_Clean "Repo root ...........: ${root}"
fEcho_Clean "Remote sync .........: $( ((no_sync)) && echo '(skipped --no-sync)' || echo 'fetch + fast-forward before building' )"
fEcho_Clean "Build jobs ..........: ${CICD_MAX_JOBS} (half of ${cores} cores)"
fEcho_Clean "CPU cap .............: $( [[ -n "${CICD_CPU_CAPPED:-}" ]] && echo "$(( cores * 50 ))% (user scope)" || echo 'job counts only (no user systemd)' )"
fEcho_Clean "Format ..............: ${FMT_CMD[*]:-(skipped)}"
fEcho_Clean "Debug build .........: ${DEBUG_BUILD_CMD[*]}"
fEcho_Clean "Tests ...............: ${TEST_CMD[*]}"
if ((PROFILE_ENABLE)); then
	fEcho_Clean "Profiler ............: ${PROFILE_SECS}s run -> flamegraph SVG (headless)"
	fEcho_Clean "  output dir ........: ${profile_dir}"
	fEcho_Clean "  sampler ...........: ${PROFILE_CMD[*]:-(none)}"
else
	fEcho_Clean "Profiler ............: (disabled)"
fi
fEcho_Clean "Release (native) ....: ${RELEASE_NATIVE_CMD[*]} -> ${RELEASE_NATIVE_BIN}"
if ((BUILD_CROSS)) && ((${#CROSS_TARGETS[@]})); then
	fEcho_Clean "Release (cross) .....:$( ((no_arm)) && echo ' (x86_64 only, --no-arm)')"
	for t in "${CROSS_TARGETS[@]}"; do fEcho_Clean "    - ${t%%|*}"; done
else
	fEcho_Clean "Release (cross) .....: (skipped)"
fi
if ((PACKAGE_ENABLE)) && ((! quick)); then
	fEcho_Clean "Packages ............:"
	for entry in "${PACKAGE_CMDS[@]:-}"; do [[ -n "$entry" ]] && fEcho_Clean "    - ${entry%%|*}"; done
	for entry in "${PACKAGE_CHECKS[@]:-}"; do [[ -n "$entry" ]] && fEcho_Clean "    - ${entry%%|*} (stops the run on failure)"; done
	fEcho_Clean "  deferred ..........: BSD, macOS, AppImage, Flatpak - no toolchain on this box"
else
	fEcho_Clean "Packages ............: $( ((quick)) && echo '(skipped --quick)' || echo '(disabled)')"
fi
if ((quick)); then
	fEcho_Clean "Private runner ......: (skipped --quick)"
elif [[ -n "${PRIVATE_RUNNER:-}" && -x "${PRIVATE_RUNNER}" ]]; then
	fEcho_Clean "Private runner ......: ${PRIVATE_RUNNER}"
else
	fEcho_Clean "Private runner ......: (not checked out)"
fi
if ((${#DOGFOOD_FIXED_DESTS[@]})); then
	if [[ -n "$fixed_dest" ]]; then
		if [[ -n "${DOGFOOD_PREFIX_SRC:-}" ]]; then
			fEcho_Clean "Dogfood, fixed name .: ${fixed_dest}/${EXE_NAME}/  (the whole prefix)"
		else
			fEcho_Clean "Dogfood, fixed name .: overwrite ${fixed_dest}/${EXE_NAME}"
		fi
	else fEcho_Clean "Dogfood, fixed name .: <none of: ${DOGFOOD_FIXED_DESTS[*]} exists - will skip>"; fi
else
	fEcho_Clean "Dogfood, fixed name .: (disabled)"
fi
if ((${#DOGFOOD_ROTATING_DESTS[@]})) && [[ -n "${DOGFOOD_PREFIX:-}" ]]; then
	fEcho_Clean "Dogfood, rotating ...: ${rot_target}/${df_pattern}  (dated copy; prunes idle ones)"
else
	fEcho_Clean "Dogfood, rotating ...: (disabled)"
fi
if ((${#DOGFOOD_CROSS_DESTS[@]})); then
	fEcho_Clean "Dogfood, cross ......:"
	for xd in "${DOGFOOD_CROSS_DESTS[@]}"; do
		xrest="${xd#*|}"; xname="${xrest%%|*}"; xdest="${xrest#*|}"
		fEcho_Clean "    - ${xd%%|*} -> ${xdest}/${xname}$( [[ -d "$xdest" ]] || echo '  <dest missing - will skip>' )"
	done
else
	fEcho_Clean "Dogfood, cross ......: (disabled)"
fi
if ((${#GIT_PUBLISH[@]} == 0)); then
	fEcho_Clean "Publish (last) ......: (disabled)"
elif [[ -n "$publish_msg" ]]; then
	fEcho_Clean "Publish (last) ......: ${GIT_PUBLISH[*]} (hands-off: \"${publish_msg}\")"
else
	fEcho_Clean "Publish (last) ......: ${GIT_PUBLISH[*]} (will prompt for message; blank = editor)"
fi
((${#GIT_PUBLISH[@]})) && fEcho_Clean "  working tree ......: $( ((allow_dirty)) && echo 'committed as-is (--allow-dirty)' || echo 'must be clean' )"
fEcho_Clean
fEcho_Clean "Fail-fast: any error aborts before the next stage."
fEcho_Clean

## Before the build, not after it: a dirty tree is a ten-second answer, and finding
## out at stage 8 wastes the whole run.
require_clean_tree_for_publish

if ((! assume_yes)); then
	## Capture the commit message up front so the run can finish unattended. This
	## is the natural place to bail on the common (publish) path - Ctrl+C here
	## aborts; there is no separate "Proceed? [y/N]" (removed to cut friction).
	if ((${#GIT_PUBLISH[@]})) && [[ -z "$publish_msg" ]]; then
		## No terminal - ssh with a command, cron, a piped run. Nobody can answer,
		## and a bare `read` under set -e aborts the whole run with no message at all.
		if [[ -t 0 ]]; then
			read -r -p "Publish commit message (blank = editor; Ctrl+C aborts): " m
			fEcho_ResetBlankCounter
			[[ -n "$m" ]] && publish_msg="$m"
		else
			fEcho_Clean "no terminal: the publish stage will fill in its own commit message"
		fi
	fi
fi

## Tee the rest of the run (all stages) to a gitignored log so warnings from any
## stage can be reviewed after the fact. Rotate the prior (closed) logs first.
if [[ -n "${LINT_LOG_DIR:-}" ]] && mkdir -p "${root}/${LINT_LOG_DIR}" 2>/dev/null; then
	gfs_rotate "${root}/${LINT_LOG_DIR}" run log >/dev/null 2>&1 || true
	exec > >(tee "${root}/${LINT_LOG_DIR}/run_${stamp}.log") 2>&1
fi

## Stage 0: remote sync.
fSection "0/8  Remote sync"
if ((no_sync)); then
	fEcho_Clean "remote sync skipped (--no-sync)"
else
	remote_sync
fi

## Stage 1: format.
fSection "1/8  Format"
if ((${#FMT_CMD[@]} == 0)); then
	fEcho_Clean "format skipped"
else
	"${FMT_CMD[@]}"
	fEcho "OK: formatted (${FMT_CMD[*]})"
fi

## Stage 2: debug build.
fSection "2/8  Debug build"
"${DEBUG_BUILD_CMD[@]}"
fEcho "OK: debug build"

## Stage 3: regression tests.
fSection "3/8  Regression tests"
"${TEST_CMD[@]}"
if [[ -n "${LINT_CMD+x}" ]] && ((${#LINT_CMD[@]})); then
	if "${LINT_PROBE[@]}" >/dev/null 2>&1; then
		"${LINT_CMD[@]}"
		fEcho "OK: lints clean"
	else
		fEcho "WARNING: lints skipped: ${LINT_PROBE[*]} failed (component not installed?)"
	fi
fi
## Bounded fuzzing of the parsers that read outside input. Slow by design, so it
## is left out of --quick; the gate never runs it at all.
if ((! quick)) && [[ -n "${FUZZ_CMD+x}" ]] && ((${#FUZZ_CMD[@]})); then
	if "${FUZZ_PROBE[@]}" >/dev/null 2>&1; then
		"${FUZZ_CMD[@]}"
		fEcho "OK: fuzz targets clean"
	else
		fEcho "WARNING: fuzz skipped: no clang or no libFuzzer runtime in the build container"
	fi
elif ((quick)); then
	fEcho_Clean "fuzz skipped (--quick)"
fi
if [[ -n "${DENY_CMD+x}" ]] && ((${#DENY_CMD[@]})); then
	if "${DENY_PROBE[@]}" >/dev/null 2>&1; then
		## Advisory-only for now: report license/advisory/duplicate findings
		## without failing the pipeline (tighten to gating once tuned).
		"${DENY_CMD[@]}" || fEcho "WARNING: cargo-deny reported findings (non-gating)"
	else
		fEcho "WARNING: deps check skipped: ${DENY_PROBE[*]} failed (cargo install cargo-deny)"
	fi
fi
## Headless scroll regression harness (slow; skipped under --quick). It skips itself
## on an environment miss (no Xvfb/binary) and exits non-zero only on a measured
## regression - which aborts here.
if ((! quick)) && [[ -n "${SCROLL_HARNESS+x}" ]] && ((${#SCROLL_HARNESS[@]})); then
	fEcho_Clean "scroll regression harness (headless, X11) ..."
	if "${root}/${SCROLL_HARNESS[0]}" "${SCROLL_HARNESS[@]:1}"; then
		fEcho "OK: scroll harness (X11)"
	else
		fDie "scroll regression harness reported a regression (X11)"
	fi
	if [[ "${SCROLL_HARNESS_WAYLAND:-0}" == 1 ]]; then
		fEcho_Clean "scroll regression harness (headless, Wayland) ..."
		if "${root}/${SCROLL_HARNESS[0]}" "${SCROLL_HARNESS[@]:1}" --wayland; then
			fEcho "OK: scroll harness (Wayland)"
		else
			fDie "scroll regression harness reported a regression (Wayland)"
		fi
	fi
elif ((quick)); then
	fEcho_Clean "scroll harness skipped (--quick)"
fi
fEcho "OK: tests passed"

## Stage 4: profiler (non-gating artifact; failures classified below).
run_profiler(){
	((PROFILE_ENABLE)) || { fEcho_Clean "profiler disabled"; return 0; }
	if ! declare -p PROFILE_CMD &>/dev/null || ((${#PROFILE_CMD[@]} == 0)); then
		fEcho "WARNING: profiler enabled but no PROFILE_CMD configured"; return 0
	fi

	## An environmental miss is the box's fault, not the app's, so warn and carry
	## on (unless PROFILE_STRICT). A failure of the run itself still aborts.
	local skip="" why=""
	command -v python3 >/dev/null 2>&1 || skip="python3 not found"
	if [[ -z "$skip" ]] && declare -p PROFILE_PROBE &>/dev/null && ((${#PROFILE_PROBE[@]})); then
		## Keep what the probe said - naming the missing tool saves a dig.
		if ! why="$("${PROFILE_PROBE[@]}" 2>&1)"; then
			why="$(printf '%s' "$why" | tail -1 | sed 's/^\[ *//; s/ *\]$//; s/^FAILED: //')"
			skip="${why:-profiling tools missing}"
		fi
	fi
	if [[ -n "$skip" ]]; then
		((PROFILE_STRICT)) && fDie "profiler: ${skip}"
		fEcho "WARNING: profiler skipped: ${skip}"; return 0
	fi

	mkdir -p "${profile_dir}"

	## Born canonical (role "frequent"); the rotation retags the newest as "latest".
	local out="${profile_dir}/flame_${stamp}_frequent.svg"
	local prc=0
	fEcho_Clean "profiling ${PROFILE_SECS}s ..."
	"${PROFILE_CMD[@]}" "${out}" --secs "${PROFILE_SECS}" || prc=$?
	((prc == 0)) || fDie "profiler run failed (exit ${prc})"
	[[ -s "$out" ]] || fDie "profiler produced no SVG: ${out}"
	gfs_rotate "${profile_dir}" flame svg
	## Rotation retags this run's file with whatever role it earned, so find it by
	## its timestamp rather than assuming which one that was.
	local latest retagged
	shopt -s nullglob
	retagged=("${profile_dir}/flame_${stamp}_"*.svg)
	shopt -u nullglob
	latest="${retagged[0]:-$out}"
	fEcho "OK: flamegraph: ${latest}"
	fEcho_Clean "open: ${latest}  (in a browser)"

	## Hot-spot summary into the log (non-fatal, no marker - the marker is for the
	## per-session --check gate, not the pipeline).
	local report="${here}/utility/flame-report.py"
	if [[ -f "$report" ]]; then
		fEcho_Clean ""
		python3 "$report" --dir "${profile_dir}" 2>/dev/null || fEcho_Clean "hot spots: (report unavailable)"
	fi
}
fSection "4/8  Profiler"
run_profiler

## Stage 5: release builds. Guarded like stages 4/6 so a project without a release
## path yet (RELEASE_ENABLE=0) still completes a clean run.
built_arts=()
fSection "5/8  Release build (native)"
if ((${RELEASE_ENABLE:-1})); then
"${RELEASE_NATIVE_CMD[@]}"
[[ -f "${RELEASE_NATIVE_BIN}" ]] || fDie "native release binary missing: ${RELEASE_NATIVE_BIN}"
fEcho "OK: native release: ${RELEASE_NATIVE_BIN} ($(du -h "${RELEASE_NATIVE_BIN}" | cut -f1))"
built_arts=("${RELEASE_NATIVE_OSARCH:-native}|${RELEASE_NATIVE_BIN}")
if ((BUILD_CROSS)) && ((${#CROSS_TARGETS[@]})); then
	for t in "${CROSS_TARGETS[@]}"; do
		local_label="${t%%|*}"; rest="${t#*|}"; osarch="${rest%%|*}"; rest="${rest#*|}"; art="${rest%%|*}"; cmd="${rest#*|}"
		fSection "5/8  Release build: ${local_label}"
		eval "${cmd}"
		[[ -f "${art}" ]] || fDie "missing artifact for ${local_label}: ${art}"
		fEcho "OK: ${local_label}: ${art} ($(du -h "${art}" | cut -f1))"
		built_arts+=("${osarch}|${art}")
	done
fi

## Collect the built binaries under versioned names + a sha256 checksums file,
## ready to attach to a release as plain uploads. A project whose release lane
## already writes its own artifacts sets RELEASE_COLLECT=0 and keeps the dir.
if [[ -n "${RELEASE_ARTIFACT_DIR:-}" ]] && ((${RELEASE_COLLECT:-1})); then
	rm -rf "${art_dir}"; mkdir -p "${art_dir}"
	for pair in "${built_arts[@]}"; do
		osarch="${pair%%|*}"; src="${pair#*|}"
		ext=""; [[ "$src" == *.exe ]] && ext=".exe"
		cp -f "${src}" "${art_dir}/${EXE_NAME}-${ver}-${osarch}${ext}"
	done
	write_sums
	fEcho "OK: ${#built_arts[@]} release artifact(s) + ${sums} -> ${RELEASE_ARTIFACT_DIR}/"
	((BUILD_CROSS)) || fEcho_Clean "note: cross targets skipped - artifact set is partial (native only)"
fi
else
	fEcho_Clean "release build disabled (RELEASE_ENABLE=0)"
fi

## Stage 6: packages. Distributables built from what stage 5 produced, never
## rebuilt from source. Which formats, and how, is entirely the project's business
## (PACKAGE_CMDS in config.bash) - the engine only decides when it happens and that
## a failing packager warns rather than aborting the run. Skipped under --quick.
## PACKAGE_CHECKS run after, and those do abort: a check that fails means what
## the packagers made is broken, and dogfood and publish would pass it on.
build_packages(){
	((PACKAGE_ENABLE)) || { fEcho_Clean "packages disabled"; return 0; }
	if ! declare -p PACKAGE_CMDS &>/dev/null || ((${#PACKAGE_CMDS[@]} == 0)); then
		fEcho "WARNING: packages enabled but no PACKAGE_CMDS configured"
		return 0
	fi

	local entry label cmd made=0
	for entry in "${PACKAGE_CMDS[@]}"; do
		label="${entry%%|*}"; cmd="${entry#*|}"
		fEcho_Clean "packaging: ${label} ..."
		if eval "${cmd}"; then
			made=$((made + 1))
		else
			## One format failing is not worth losing the others, or the run.
			fEcho "WARNING: ${label} failed"
		fi
	done
	write_sums
	fEcho "OK: ${made}/${#PACKAGE_CMDS[@]} packaging step(s) -> ${RELEASE_ARTIFACT_DIR}/"
}
run_package_checks(){
	((PACKAGE_ENABLE)) || return 0
	declare -p PACKAGE_CHECKS &>/dev/null || return 0

	local entry label cmd rc
	for entry in "${PACKAGE_CHECKS[@]}"; do
		label="${entry%%|*}"; cmd="${entry#*|}"
		fEcho_Clean "checking: ${label} ..."
		rc=0; eval "${cmd}" || rc=$?
		case "$rc" in
			0)  ;;
			77) fEcho "WARNING: ${label} skipped" ;;
			*)  fDie "${label} (exit ${rc})" ;;
		esac
	done
}
fSection "6/8  Packages"
if ((quick)); then
	fEcho_Clean "packages skipped (--quick)"
else
	build_packages
	run_package_checks
fi
## The private runner builds on boxes that are often off or busy, so it skips
## those itself and says so. Only a job that ran and failed stops the run.
priv_dogfood=()
if ((quick)); then
	fEcho_Clean "private runner skipped (--quick)"
elif [[ -n "${PRIVATE_RUNNER:-}" && -x "${PRIVATE_RUNNER}" ]]; then
	fEcho_Clean "private runner ..."
	priv_start="$(mktemp)"
	## The Store bundle is made from this run's Windows zip. With no packages
	## stage, the zip there is an older run's.
	priv_args=()
	((PACKAGE_ENABLE)) || priv_args+=(--no-store)
	"${PRIVATE_RUNNER}" --public "${root}" "${priv_args[@]}" || fDie "a private runner job failed"
	fEcho "OK: private runner"
	## A build joins this run's dogfood only if the runner made it just now. One
	## left from an earlier run of the same version must not be published again.
	for entry in "${PRIVATE_DOGFOOD[@]:-}"; do
		[[ -n "$entry" ]] || continue
		pd_src="${PRIVATE_RUNNER%/cicd/*}/${entry%%|*}"
		pd_src="${pd_src//@VER@/${ver}}"
		if [[ -d "$pd_src" && "$pd_src" -nt "$priv_start" ]]; then
			priv_dogfood+=("${pd_src}|${entry#*|}")
		fi
	done
	rm -f "$priv_start"
else
	fEcho_Clean "private runner not checked out; skipped"
fi

## Stage 7: dogfood. Three independent installs: fixed name, rotating dated copy,
## and cross-built binaries for a box that cannot build its own.
fSection "7/8  Dogfood (install native release locally)"
df_did=0

## What gets installed. DOGFOOD_PREFIX_SRC means the app is a relocatable tree
## rather than one binary, so it is copied whole and the name on PATH points into
## it. The stamp is the build's own mtime, never this run's clock: the launcher
## reads it as the build's identity and compares it against the source it copied
## from, so a run-time stamp reads as a newer build and gets the same one copied
## in again on the next launch.
if [[ -n "${DOGFOOD_PREFIX_SRC:-}" ]]; then
	df_src="${root}/${DOGFOOD_PREFIX_SRC}"
	df_stamp_from="${df_src}/bin/${EXE_NAME}"
else
	df_src="${RELEASE_NATIVE_BIN}"
	df_stamp_from="${df_src}"
fi

## 7a. Fixed name: the stable path you launch by hand.
if ((${#DOGFOOD_FIXED_DESTS[@]})); then
	if [[ -n "$fixed_dest" ]]; then
		if [[ -n "${DOGFOOD_PREFIX_SRC:-}" ]]; then
			## Copied alongside and swapped in, not rebuilt in place: a launcher on
			## another box reads this dir whenever it feels like it, and a half-copied
			## tree there reads as a perfectly good build.
			df_app="${fixed_dest}/${EXE_NAME}"
			[[ "$df_app" == /*/"${EXE_NAME}" ]] || fDie "refusing to replace ${df_app}"
			## The launcher runs its own local copy, but someone may have started
			## this one directly.
			if in_use "$df_app"; then
				fEcho "WARNING: ${df_app} is running; left as is"
			else
				rm -rf "${df_app}.new"
				cp -a "${df_src}" "${df_app}.new"
				rm -rf "${df_app}"
				mv "${df_app}.new" "${df_app}"
				fEcho "OK: published (fixed) -> ${df_app}"
			fi
		else
			cp -pf "${df_src}" "${fixed_dest}/${EXE_NAME}"
			fEcho "OK: installed (fixed) -> ${fixed_dest}/${EXE_NAME}"
		fi
		df_did=1
	else
		fEcho "WARNING: no fixed dogfood dest exists (${DOGFOOD_FIXED_DESTS[*]}); skipping"
	fi
fi

## 7b. Rotating name: dated copy so builds coexist; prune older ones not running.
if ((${#DOGFOOD_ROTATING_DESTS[@]})) && [[ -n "${DOGFOOD_PREFIX:-}" ]]; then
	[[ -z "$rot_dest" && -n "$rot_target" ]] && mkdir -p "$rot_target" 2>/dev/null && rot_dest="$rot_target"
	if [[ -n "$rot_dest" && -w "$rot_dest" ]]; then
		df_name="${DOGFOOD_PREFIX}_$(date -r "${df_stamp_from}" +%Y%m%d-%H%M%S)${DOGFOOD_TAG:+_${DOGFOOD_TAG}}"
		rm -rf "${rot_dest:?}/${df_name}"
		if [[ -n "${DOGFOOD_PREFIX_SRC:-}" ]]; then
			cp -a "${df_src}" "${rot_dest}/${df_name}"
		else
			cp -pf "${df_src}" "${rot_dest}/${df_name}"
			chmod +x "${rot_dest}/${df_name}"
		fi
		fEcho "OK: installed (rotating) -> ${rot_dest}/${df_name}"
		pruned=0
		for old in "${rot_dest}/${DOGFOOD_PREFIX}_"*; do
			[[ -e "$old" ]] || continue                  # no-match glob (nullglob is off)
			[[ "$(basename "$old")" == "$df_name" ]] && continue
			if in_use "$old"; then
				fEcho_Clean "kept (running): $(basename "$old")"
			else
				rm -rf "$old" && pruned=$((pruned + 1))
			fi
		done
		if ((pruned)); then fEcho_Clean "pruned ${pruned} old copy(ies) not in use"; fi
		df_did=1
	else
		fEcho "WARNING: no rotating dogfood dest writable (${DOGFOOD_ROTATING_DESTS[*]}); skipping"
	fi
fi

## 7c. Cross-built binaries under a fixed name, for the box that cannot build them
## itself to pick up over the sync layer. Only targets built this run are copied.
if ((${#DOGFOOD_CROSS_DESTS[@]})); then
	for xd in "${DOGFOOD_CROSS_DESTS[@]}"; do
		xosarch="${xd%%|*}"; xrest="${xd#*|}"; xname="${xrest%%|*}"; xdest="${xrest#*|}"
		xsrc=""
		for pair in "${built_arts[@]:-}"; do
			[[ "${pair%%|*}" == "$xosarch" ]] && { xsrc="${pair#*|}"; break; }
		done
		if [[ -z "$xsrc" ]]; then
			fEcho_Clean "no ${xosarch} build this run; cross dogfood skipped"
		elif [[ -d "$xdest" && -w "$xdest" ]]; then
			cp -pf "$xsrc" "${xdest}/${xname}"
			fEcho "OK: installed (cross ${xosarch}) -> ${xdest}/${xname}"
			df_did=1
		else
			fEcho "WARNING: cross dogfood dest not writable (${xdest}); skipping ${xosarch}"
		fi
	done
fi

## Private builds: what the private runner made this run for a platform this box
## cannot build, swapped in whole the same way as 7a. Not under --no-dogfood.
if ((${#DOGFOOD_FIXED_DESTS[@]})); then
	for entry in "${priv_dogfood[@]:-}"; do
		[[ -n "$entry" ]] || continue
		pd_src="${entry%%|*}"
		declare -n pd_dests="${entry#*|}"
		pd_dest=""
		for d in "${pd_dests[@]}"; do [[ -d "$d" && -w "$d" ]] && { pd_dest="$d"; break; }; done
		unset -n pd_dests
		if [[ -z "$pd_dest" ]]; then
			fEcho "WARNING: no dogfood dest for ${pd_src##*/dist/}; skipping"
			continue
		fi
		pd_app="${pd_dest}/${EXE_NAME}"
		[[ "$pd_app" == /*/"${EXE_NAME}" ]] || fDie "refusing to replace ${pd_app}"
		rm -rf "${pd_app}.new"
		cp -a "${pd_src}" "${pd_app}.new"
		rm -rf "${pd_app}"
		mv "${pd_app}.new" "${pd_app}"
		fEcho "OK: published (private) -> ${pd_app}"
		df_did=1
	done
fi

if ((! df_did)); then fEcho_Clean "dogfood disabled"; fi

## 7d. Whatever else this project wants kept in step with a dogfood drop. Never
## fatal - a launcher that didn't get updated is not a reason to fail a build.
if ((${#DOGFOOD_HOOK[@]})); then
	if ! (cd "${root}" && "${DOGFOOD_HOOK[@]}"); then
		fEcho "WARNING: dogfood hook failed (${DOGFOOD_HOOK[0]})"
	fi
fi

## 7e. A dogfood drop only another box can build, such as a packed Windows exe.
## Slow, so skipped under --quick, and never fatal for the same reason as 7d.
if ((! ${#DOGFOOD_REMOTE[@]})); then
	:
elif ((quick)); then
	fEcho_Clean "remote dogfood skipped (--quick)"
elif ! (cd "${root}" && "${DOGFOOD_REMOTE[@]}"); then
	fEcho "WARNING: remote dogfood failed (${DOGFOOD_REMOTE[*]})"
fi

## Refresh README screenshots (skipped under --quick; non-fatal - a miss never
## aborts). Runs before publish so changed images get committed; rendering needs
## a headless X + magick, so a failure just warns.
shots_hook="${root}/cicd/utility/screenshots.bash"
if ((! SHOTS_ENABLE)); then
	fEcho_Clean "screenshots disabled"
elif ((quick)); then
	fEcho_Clean "screenshots skipped (--quick)"
elif [[ -x "$shots_hook" ]]; then
	fEcho_Clean "refreshing README screenshots ..."
	if NEMO_BIN="${root}/${DOGFOOD_PREFIX_SRC}/bin/${EXE_NAME}" "$shots_hook" "${root}"; then
		fEcho "OK: screenshots"
	else
		fEcho "WARNING: screenshot hook failed (non-fatal)"
	fi
fi

## Re-record the demo video (same gating shape as screenshots: off by default,
## skipped under --quick, never aborts). Both recordings GFS-rotate into the
## demo-video/ directory beside the repo; the README gif is also written to
## assets/demo.gif. It runs on a private Xvfb, so nothing shows up on whatever
## display this was started from. It needs the release build, so it goes after
## stage 5.
demo_hook="${root}/cicd/utility/demo-video/demo-video.py"
if ((! ${DEMO_ENABLE:-0})); then
	fEcho_Clean "demo video disabled"
elif ((quick)); then
	fEcho_Clean "demo video skipped (--quick)"
elif [[ -f "$demo_hook" ]]; then
	fEcho_Clean "recording demo video ..."
	if NEMO_BIN="${root}/${RELEASE_NATIVE_BIN}" python3 "$demo_hook"; then
		fEcho "OK: demo video"
	else
		fEcho "WARNING: demo video hook failed (non-fatal)"
	fi
fi

## Stage 8: backup + publish.
fSection "8/8  Backup + publish"
## Again: preflight cleared the tree, but a stage since then may have written to it
## (an in-place formatter is the obvious one).
require_clean_tree_for_publish
## Always run the publisher quiet: cicd already gave the initial prompt, so skip
## its redundant continue-prompt. With no message it still lets git open the editor.
pub_flags=(--quiet)
if ((${#GIT_PUBLISH[@]} == 0)); then
	fEcho_Clean "publish disabled"
elif [[ -n "$publish_msg" ]]; then
	## Hands-off: quiet env skips the script's continue-prompt; the GIT_EDITOR
	## helper fills the empty commit message so `git commit` won't open an editor.
	fEcho_Clean "hands-off publish (commit message: \"${publish_msg}\")"
	GIT_BACKUP_AND_PUBLISH_QUIET=1 GIT_AUTO_MESSAGE="${publish_msg}" \
		GIT_EDITOR="${here}/utility/git-auto-msg.bash" "${GIT_PUBLISH[@]}" "${pub_flags[@]}"
	fEcho "OK: published"
elif [[ -t 0 ]]; then
	"${GIT_PUBLISH[@]}" "${pub_flags[@]}"
	fEcho "OK: published"
else
	## An empty message means git opens an editor, and there is no terminal to open
	## it into. Hand git the auto-message helper so the commit still gets written.
	GIT_BACKUP_AND_PUBLISH_QUIET=1 GIT_EDITOR="${here}/utility/git-auto-msg.bash" \
		"${GIT_PUBLISH[@]}" "${pub_flags[@]}"
	fEcho "OK: published"
fi

fSection "${APP_NAME} CI/CD: done."
fEcho_Clean


##	History:
##		- 2026-06-05 JC: Created.
