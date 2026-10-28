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
##	   3. regression tests + lints (+ fuzzing, the sanitizer suite, any headless harness)
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
##	   --no-cross          skip cross-target release builds, arm64 included
##	   --include-arm       run the arm64 release build without --long, even
##	                       with --quick. It runs on another box and takes about
##	                       an hour and a half. --no-cross still leaves it out,
##	                       and its packages come only when the packages stage runs
##	   --no-arm            leave the arm64 build and its packages out, even
##	                       with --include-arm
##	   --no-package        skip the packages stage (.deb/.rpm/installer)
##	   --no-private        skip the private runner (PRIVATE_RUNNER in config.bash)
##	   --no-remote         skip every step on another box: arm64, the FreeBSD
##	                       lane, the private runner and remote dogfood, even
##	                       with --include-arm or --include-bsd
##	   --no-container      run on the host. The GTK dev stack never goes on
##	                       the host, so that's the lints, with the host's
##	                       tools, and the run stops with one line at the
##	                       first step that needs a container
##	   --no-profile        skip the profiler stage
##	   --no-sanitize       skip the test suite built with the address and
##	                       undefined behavior sanitizers
##	   --no-dogfood        skip installing the native release locally
##	   --no-publish        skip the git backup + publish stage
##	   --allow-dirty       let publish commit an uncommitted working tree (it
##	                       refuses one by default)
##	   --keep-temp         keep the run's temp folder and containers, and
##	                       print where the folder is. Each run works in its
##	                       own folder under HOST_TEMP (or TMPDIR), removed at
##	                       the end, failure and Ctrl+C included
##	   --shots             refresh README screenshots (off by default)
##	   --demo              re-record the demo video (off by default)
##	   --include-bsd       run the FreeBSD lane even with --quick (on by
##	                       default): the build and suite on the FreeBSD box in
##	                       stage 3, the tarball and pkg in stage 5
##	   --no-bsd            leave the FreeBSD lane out, even with --include-bsd
##	   --quick             run only the quick steps on this box: the build, the
##	                       full test suite, lints, the sanitizer suite, the
##	                       profiler, the native and Windows release builds and
##	                       local dogfood. Skips what is slow or needs another
##	                       box: fuzzing, the static analyzers, packages, arm64,
##	                       the FreeBSD lane, the private runner, remote dogfood,
##	                       scroll harness, screenshots, demo video. arm64 and
##	                       FreeBSD come back with --include-arm and
##	                       --include-bsd
##	   --long              everything, the arm64 release build included, which
##	                       a plain run leaves out to stay near an hour
##	   --slow              same as --long
##	   --backlog-run-quick [<area>...]
##	                       what a backlog worker runs before a commit, about 4
##	                       minutes: format check, lints and the test suite,
##	                       then exit. Name areas to run only those. Areas:
##	                       format, lint, tests
##	   --backlog-run-full  what a backlog round runs once at its end: every
##	                       check of a plain run, other boxes included
##	   --merge-gate        the same checks, before a merge to main
##	   --gate              same as --backlog-run-quick
##	   --ci                same as --merge-gate
##	                       None of these 3 syncs, formats in place, dogfoods,
##	                       installs, publishes, commits, pushes or prompts.
##	                       They leave out screenshots and the demo video too
## - Reuse: copy the cicd/ directory into another project and edit config.bash.

##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

## Help before anything else runs: the CPU scope and the container caps below
## would otherwise go on just to print usage.
## --no-container too, since the container caps go on before the options are read.
## Only the flag sets it here, so a test's copy run under such a run decides alone.
unset CICD_NO_CONTAINER
for arg in "$@"; do case "$arg" in
	-h|--help) sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	--no-container) export CICD_NO_CONTAINER=1 NEMO_LINT_HOST=1 ;;
esac; done

## Find the repo root and load project config.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/.." && pwd)"   # the git repo root (cicd/..)
## Per-user bin dirs, so a run over ssh or from cron finds the same helpers an
## interactive shell does - a non-interactive ssh gets a bare PATH and would
## otherwise silently skip the lints (cppcheck) and the profiler (the cargo-installed
## inferno-flamegraph). Cargo's goes last so its toolchain shadows nothing.
export PATH="${HOME}/.local/bin:${PATH}:${HOME}/.cargo/bin"
## Low priority for everything the run starts, CPU and disk both.
renice -n 19 -p $$ >/dev/null 2>&1 || true
command -v ionice >/dev/null 2>&1 && { ionice -c 3 -p $$ 2>/dev/null || true; }
cores="$(nproc 2>/dev/null || echo 2)"

## Job counts alone do not hold a run to half the machine: link-time optimization
## runs threads of its own under each ninja job, and rar uses every core. So the
## whole run goes inside one user scope with a CPU quota, which everything it
## starts inherits. With no user systemd manager only the job counts apply.
if [[ -z "${CICD_CPU_CAPPED:-}" ]] && command -v systemd-run >/dev/null 2>&1 \
	&& systemd-run --user --scope -q true >/dev/null 2>&1; then
	export CICD_CPU_CAPPED=1
	exec systemd-run --user --scope -q -p "CPUQuota=$(( cores * 50 ))%" bash "${BASH_SOURCE[0]}" "${@}"
fi

## Job counts: half the cores, a quarter on a busy box or for a hard step,
## checked again before each step (fStep). Worked out before config.bash is
## read, since a few of its lines use CICD_MAX_JOBS; the commands it hands to
## the containers read it there at run time. A CICD_MAX_JOBS exported beforehand
## pins the count.
source "${here}/utility/include/jobs.bash"
: "${CICD_JOBS_PIN:=${CICD_MAX_JOBS:-}}"
fJobs

source "${here}/config.bash"

## A container's processes sit in docker's cgroup, not in this run's scope, so
## each gets the step's cap directly, and a low share of the CPU when others
## want it. It sticks to the container, so a build run by hand in there is held
## to it too.
declare -p CICD_CONTAINERS &>/dev/null || CICD_CONTAINERS=()
no_container=0
if [[ "${CICD_NO_CONTAINER:-}" == "1" ]]; then no_container=1; CICD_CONTAINERS=(); DOCKER_SWEEP=0; fi
ctrJobs=""
fCapContainers(){
	[[ "${ctrJobs}" != "${CICD_MAX_JOBS}" ]] || return 0
	ctrJobs="${CICD_MAX_JOBS}"
	local ctr
	for ctr in "${CICD_CONTAINERS[@]}"; do
		docker update --cpus "${CICD_MAX_JOBS}" --cpu-shares 256 "$ctr" >/dev/null 2>&1 || true
	done
}
fCapContainers
source "${here}/utility/include/gfs-rotate.bash"                  ## gfs_rotate() for the profiler artifacts
source "${here}/utility/include/source-date.bash"                 ## fSetSourceDate() for reproducible builds
source "${here}/utility/include/run-temp.bash"                    ## the run folder, and fSafeRm()
source "${here}/utility/include/docker-sweep.bash"                ## fDockerSweep() at the end of a run
declare -p FMT_CMD &>/dev/null || FMT_CMD=()                      ## tolerate a config without the fmt stage
declare -p FMT_CHECK_CMD &>/dev/null || FMT_CHECK_CMD=()
for v in LINT_CMD FUZZ_CMD SANITIZE_CMD ANALYZE_CMD DENY_CMD SCROLL_HARNESS; do
	declare -p "$v" &>/dev/null || declare -a "$v=()"
done
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
include_bsd=""; no_bsd=0                                          ## empty = whatever --quick says
assume_yes=0; quick=0; long=0; gate=0; no_arm=0; no_fmt=0; include_arm=""; no_sync=0; allow_dirty=0; cli_message=""
## checks_only: --backlog-run-full or --merge-gate. run_mode names the one given,
## for the plan and the last line.
checks_only=0; run_mode=""; alias_note=""; areas=(); no_remote=0
known_areas="format lint tests"
## Whether the config has a remote dogfood at all, before any flag empties it.
had_remote_df=0; declare -p DOGFOOD_REMOTE &>/dev/null && ((${#DOGFOOD_REMOTE[@]})) && had_remote_df=1
while (($#)); do case "$1" in
	-y|--yes)                 assume_yes=1; shift ;;
	-q|--quiet)               assume_yes=1; shift ;;            ## publish runs quiet whatever this says
	## Of these 3 modes the last one named wins.
	--backlog-run-quick|--gate)
		alias_note=""; [[ "$1" == "--gate" ]] && alias_note="--gate is another name for --backlog-run-quick"
		gate=1; checks_only=0; run_mode="--backlog-run-quick"; areas=(); shift
		while (($#)) && [[ "$1" != -* ]]; do
			[[ " ${known_areas} " == *" $1 "* ]] || { echo "unknown area: $1 (areas: ${known_areas})" >&2; exit 2; }
			areas+=("$1"); shift
		done ;;
	--backlog-run-full)       gate=0; checks_only=1; run_mode="$1"; alias_note=""; shift ;;
	--merge-gate|--ci)
		alias_note=""; [[ "$1" == "--ci" ]] && alias_note="--ci is another name for --merge-gate"
		gate=0; checks_only=1; run_mode="--merge-gate"; shift ;;
	--no-sync)                no_sync=1; shift ;;
	--no-fmt)                 FMT_CMD=(); no_fmt=1; shift ;;
	--no-cross)               BUILD_CROSS=0; shift ;;
	--include-arm)            include_arm=1; shift ;;
	--no-arm)                 no_arm=1; shift ;;                ## wins over --include-arm
	--no-package)             PACKAGE_ENABLE=0; shift ;;
	--no-private)             PRIVATE_RUNNER=""; shift ;;
	--no-remote)              no_remote=1; shift ;;             ## wins over --include-arm and --include-bsd
	--no-container)           shift ;;                          ## read before config.bash, above
	--no-profile)             PROFILE_ENABLE=0; shift ;;
	--no-sanitize)            SANITIZE_CMD=(); shift ;;
	--no-dogfood)             DOGFOOD_FIXED_DESTS=(); DOGFOOD_ROTATING_DESTS=(); DOGFOOD_CROSS_DESTS=(); DOGFOOD_HOOK=(); DOGFOOD_REMOTE=(); shift ;;
	--no-publish)             GIT_PUBLISH=(); shift ;;
	--allow-dirty)            allow_dirty=1; shift ;;
	--keep-temp)              runTempKeep=1; shift ;;
	--shots)                  SHOTS_ENABLE=1; shift ;;
	--demo)                   DEMO_ENABLE=1; shift ;;
	--include-bsd)            include_bsd=1; shift ;;
	--no-bsd)                 no_bsd=1; shift ;;                ## wins over --include-bsd
	--quick)                  quick=1; PACKAGE_ENABLE=0; shift ;;   ## the quick local steps only
	--long|--slow)            long=1; shift ;;                  ## adds the arm64 lane
	--message=*|--msg=*|-m=*) cli_message="${1#*=}"; shift ;;
	-m|--message|--msg)       cli_message="${2-}"; shift; (($#)) && shift ;;
	-h|--help)                sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
esac; done
[[ -z "$alias_note" ]] || echo "[ ${alias_note} ]"

## Checks only: a round or the pre-push hook runs these, so nothing may change
## the tree, the box or the remote, and nobody is there to answer a prompt.
## After the loop, so no later option can turn a step back on.
if ((checks_only)); then
	assume_yes=1; no_sync=1
	FMT_CMD=()                                                    ## stage 1 runs FMT_CHECK_CMD instead
	DOGFOOD_FIXED_DESTS=(); DOGFOOD_ROTATING_DESTS=(); DOGFOOD_CROSS_DESTS=(); DOGFOOD_HOOK=(); DOGFOOD_REMOTE=()
	PRIVATE_DOGFOOD=()
	GIT_PUBLISH=()
	SHOTS_ENABLE=0; DEMO_ENABLE=0                                 ## both rewrite tracked files
fi
## Nothing on another box. Each step's own off switch, so the rest below reads
## them as before.
if ((no_remote)); then
	no_arm=1; no_bsd=1; PRIVATE_RUNNER=""; DOGFOOD_REMOTE=()
fi

## FreeBSD runs unless --quick, and arm64 only with --long, since its emulated box
## takes about 98 of a plain run's 152 minutes. Naming either one beats both flags.
[[ -n "$include_arm" ]] || include_arm=$((long))
[[ -n "$include_bsd" ]] || include_bsd=$((! quick))
((! no_bsd)) || include_bsd=0
declare -p BSD_TEST_CMD &>/dev/null || BSD_TEST_CMD=()           ## a config with no FreeBSD box
declare -p BSD_RELEASE_CMD &>/dev/null || BSD_RELEASE_CMD=()
((${#BSD_TEST_CMD[@]} + ${#BSD_RELEASE_CMD[@]})) || include_bsd=0

## The arm64 box here is emulated and takes about an hour. Native x86_64 is
## untouched; the Windows/Linux x86_64 crosses stay.
arm_note=""; arm_label=""; arm_why=""
if ((! include_arm || no_arm)) && declare -p CROSS_TARGETS &>/dev/null; then
	kept=()
	for t in "${CROSS_TARGETS[@]}"; do case "$t" in *arm64*|*aarch64*) arm_label="${t%%|*}" ;; *) kept+=("$t") ;; esac; done
	CROSS_TARGETS=("${kept[@]}")
	if   [[ -z "$arm_label" ]]; then :
	elif ((no_remote));         then arm_why="--no-remote"
	elif ((no_arm));            then arm_why="--no-arm"
	elif ((quick));             then arm_why="--quick"
	else                             arm_why="--long adds it"; fi
	[[ -z "$arm_why" ]] || arm_note=" (no arm64, ${arm_why})"
fi
## arm64 packages and their checks are made from this run's arm64 build, so they
## go wherever it goes.
arm_built=0
if ((BUILD_CROSS)) && declare -p CROSS_TARGETS &>/dev/null; then
	for t in "${CROSS_TARGETS[@]}"; do case "$t" in *arm64*|*aarch64*) arm_built=1 ;; esac; done
fi
## Also when the arm64 box turns out not to be there.
fDropArmPackages(){
	local t kept=()
	if declare -p PACKAGE_CMDS &>/dev/null; then
		for t in "${PACKAGE_CMDS[@]}"; do case "$t" in *arm64*|*aarch64*) ;; *) kept+=("$t") ;; esac; done
		PACKAGE_CMDS=("${kept[@]}")
	fi
	kept=()
	if declare -p PACKAGE_CHECKS &>/dev/null; then
		for t in "${PACKAGE_CHECKS[@]}"; do case "$t" in *arm64*|*aarch64*) ;; *) kept+=("$t") ;; esac; done
		PACKAGE_CHECKS=("${kept[@]}")
	fi
}
((arm_built)) || fDropArmPackages
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
_letterbox="•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••"
fSection(){ fEcho_Clean; fEcho_Clean "${_letterbox}"; fEcho "$*"; }
fDie(){ { fEcho_Force "FAILED: $*"; } >&2; exit 1; }
fLints(){
	fStep "Lints"
	if "${LINT_PROBE[@]}" >/dev/null 2>&1; then
		"${LINT_CMD[@]}"
		fEcho "OK: lints clean"
	else
		fEcho "WARNING: lints skipped: ${LINT_PROBE[*]} failed (component not installed?)"
	fi
}
## Not a skip: what wasn't checked mustn't read as a pass. 2026102516295605.
fNoContainerStop(){
	((no_container)) || return 0
	fStep_Close PASS
	{ fEcho_Force "STOPPED at ${1}: it needs a container, and --no-container runs on the host"; } >&2
	exit 1
}
## Local time to the hundredth, as YYYYmmDD-HHMMSS.NN.
fStamp(){
	local t="${EPOCHREALTIME:-}" frac
	if [[ -n "$t" ]]; then
		t="${t/,/.}"; frac="${t#*.}"
		printf '%(%Y%m%d-%H%M%S)T.%s' "${t%.*}" "${frac:0:2}"
	else
		printf '%s.00' "$(date +%Y%m%d-%H%M%S)"
	fi
}
## Steps. The plan is made first, so each header can say "Step N of M": fPlan
## <on> <name> <why off>. A step that's off isn't counted, and the plan lists it
## with its flag or reason. fStep <name> [hard] closes the step before it as
## PASS, checks the load again for the job count, and opens this one. The EXIT
## trap closes an open step as FAIL. fStep_Skip closes it as SKIP, for a box
## that isn't there, and the end of the run lists those.
stepList=(); stepOff=(); stepSkipped=(); stepNow=0; stepOpen=""
fPlan(){ if (($1)); then stepList+=("$2"); else stepOff+=("$2 (${3})"); fi; }
fStep_Close(){
	[[ -n "$stepOpen" ]] || return 0
	fEcho_Force "$(fStamp): Step ${stepNow} of ${#stepList[@]}; $1 ${stepOpen}"
	stepOpen=""
}
fStep_Skip(){ stepSkipped+=("$stepOpen"); fStep_Close SKIP; }
fStep(){
	local why=""
	fStep_Close PASS
	stepNow=$((stepNow + 1)); stepOpen="$1"
	fJobs "${2:-}"; fCapContainers
	fEcho_Clean; fEcho_Clean "${_letterbox}"
	fEcho "$(fStamp): Step ${stepNow} of ${#stepList[@]}; Starting ${stepOpen}"
	[[ "${stepList[stepNow - 1]:-}" == "$1" ]] || fEcho "WARNING: the plan has step ${stepNow} as '${stepList[stepNow - 1]:-none}'"
	if   [[ -n "${CICD_JOBS_PIN:-}" ]]; then why="pinned"
	elif [[ -n "${2:-}" ]];             then why="hard step"
	elif ((jobsBusy));                  then why="busy box"; fi
	[[ -z "$why" ]] || why="${why}, "
	fEcho_Clean "jobs ${CICD_MAX_JOBS} (${why}load ${jobsLoad} on ${jobsCpus} cpus)"
}
fPlan_Show(){
	local x
	fEcho_Clean "Steps ...............: ${#stepList[@]}"
	((${#stepOff[@]})) || return 0
	fEcho_Clean "Turned off ..........:"
	for x in "${stepOff[@]}"; do fEcho_Clean "    - ${x}"; done
}
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

## Everything the run writes to a temp folder goes in its own run folder, which
## is removed at the end whatever the outcome, along with the run's containers
## when DOCKER_SWEEP is on. The signal traps turn Ctrl+C into an exit, so the
## EXIT trap runs.
fRunTemp_Make "${EXE_NAME}" "${stamp}" || fDie "could not make a run folder under ${HOST_TEMP:-${TMPDIR:-/tmp}}/${EXE_NAME}"
fRunEnd(){
	local rc=$?
	if ((rc)) && [[ -n "$stepOpen" ]]; then fStep_Close FAIL; fEcho_Clean; fi
	## || true: a docker hiccup here must not keep the folder from going.
	if ((${DOCKER_SWEEP:-0})); then fDockerSweep "${CICD_CONTAINERS[@]}" || true; fi
	fRunTemp_Done || true
}
trap fRunEnd EXIT
trap 'exit 130' INT; trap 'exit 143' TERM; trap 'exit 129' HUP
fRunTemp_Sweep

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

## --backlog-run-quick: format check + lints + tests, fail-fast, no
## artifacts/log-tee/publish. The test stage builds first, since there is no
## build stage here - so what is compiled is the working tree. Source tree
## untouched, build directory written to.
## True when the area was named, or when none were.
fArea(){ ((${#areas[@]} == 0)) || [[ " ${areas[*]} " == *" $1 "* ]]; }
if ((gate)); then
	gate_fmt=0; gate_fmt_why="area not named"
	if fArea format; then gate_fmt=$((${#FMT_CHECK_CMD[@]} > 0)); gate_fmt_why="no format check set up"; fi
	gate_lint=0; gate_lint_why="area not named"
	if fArea lint; then gate_lint=$((${#LINT_CMD[@]} > 0)); gate_lint_why="no lint stage set up"; fi
	gate_tests=0; fArea tests && gate_tests=1
	fPlan "$gate_fmt"   "Format check" "$gate_fmt_why"
	fPlan "$gate_lint"  "Lints"        "$gate_lint_why"
	if ((no_container && gate_tests)); then fPlan 0 "Tests" "--no-container, the run stops there"
	else fPlan "$gate_tests" "Tests" "area not named"; fi
	fSection "${APP_NAME} backlog-run-quick${areas[*]:+ ${areas[*]}}"
	fPlan_Show
	if ((gate_fmt)); then
		fStep "Format check"
		"${FMT_CHECK_CMD[@]}" || fDie "format check failed (run: ${FMT_CMD[*]:-cargo fmt})"
		fEcho "OK: formatting clean"
	fi
	((gate_lint)) && fLints
	if ((gate_tests)); then
		fNoContainerStop "Tests"
		fStep "Tests"
		"${TEST_CMD[@]}"
		fEcho "OK: tests passed"
	fi
	fStep_Close PASS
	fSection "${APP_NAME} backlog-run-quick${areas[*]:+ ${areas[*]}}: PASSED."
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

## The steps this run takes, in order, and the ones turned off with why.
sync_why="--no-sync"; ((checks_only)) && sync_why="${run_mode}"
mode_or(){ if ((checks_only)); then printf '%s' "${run_mode}"; else printf '%s' "$1"; fi; }
quick_or(){ if ((quick)); then printf '%s' --quick; else mode_or "$1"; fi; }
remote_why(){ if ((no_remote)); then echo --no-remote; elif ((quick)); then echo --quick; else echo "$1"; fi; }
fmt_step="Format"; fmt_why="no formatter set up"
if ((checks_only)); then fmt_step="Format check"; fmt_why="no format check set up"; fi
((no_fmt)) && ((! checks_only)) && fmt_why="--no-fmt"
run_fmt=0
if ((checks_only)); then run_fmt=$((${#FMT_CHECK_CMD[@]} > 0)); else run_fmt=$((${#FMT_CMD[@]} > 0)); fi
run_release=$((${RELEASE_ENABLE:-1}))
run_cross=$((run_release && BUILD_CROSS && ${#CROSS_TARGETS[@]} > 0))
run_bsd_tests=$((include_bsd && ${#BSD_TEST_CMD[@]} > 0))
run_bsd_release=$((run_release && include_bsd && ${#BSD_RELEASE_CMD[@]} > 0))
bsd_why="$(remote_why "not set up")"; ((no_bsd && ! no_remote)) && bsd_why="--no-bsd"
run_private=0; priv_why="not checked out"
if   ((quick));     then priv_why="--quick"
elif ((no_remote)); then priv_why="--no-remote"
elif [[ -n "${PRIVATE_RUNNER:-}" && -x "${PRIVATE_RUNNER}" ]]; then run_private=1
elif [[ -z "${PRIVATE_RUNNER:-}" ]]; then priv_why="--no-private"; fi
run_dogfood=$(( ${#DOGFOOD_FIXED_DESTS[@]} + ${#DOGFOOD_ROTATING_DESTS[@]} + ${#DOGFOOD_CROSS_DESTS[@]} + ${#DOGFOOD_HOOK[@]} > 0 ))
dogfood_why="$(mode_or --no-dogfood)"
run_remote_df=$((! quick && ${#DOGFOOD_REMOTE[@]} > 0))
shots_hook="${root}/cicd/utility/screenshots.bash"
demo_hook="${root}/cicd/utility/demo-video/demo-video.py"

fPlan "$((! no_sync))"                  "Remote sync"           "${sync_why}"
fPlan "$run_fmt"                        "$fmt_step"             "$fmt_why"
fPlan 1                                 "Build (debug)"         ""
fPlan 1                                 "Tests"                 ""
fPlan "$((${#LINT_CMD[@]} > 0))"        "Lints"                 "no lint stage set up"
((${#FUZZ_CMD[@]})) && fPlan "$((! quick))" "Fuzzing"           "--quick"
fPlan "$((${#SANITIZE_CMD[@]} > 0))"    "Sanitizer suite"       "--no-sanitize"
((${#BSD_TEST_CMD[@]})) && fPlan "$run_bsd_tests" "FreeBSD suite" "$bsd_why"
((${#DENY_CMD[@]})) && fPlan 1          "Dependency check"      ""
((${#SCROLL_HARNESS[@]})) && fPlan "$((! quick))" "Scroll harness" "--quick"
fPlan "$((PROFILE_ENABLE))"             "Profiler"              "--no-profile"
fPlan "$run_release"                    "Release build (native)" "RELEASE_ENABLE=0"
if ((run_cross)); then
	for t in "${CROSS_TARGETS[@]}"; do fPlan 1 "Release build: ${t%%|*}" ""; done
elif ((run_release)) && ((! BUILD_CROSS)); then
	fPlan 0 "Release builds (cross)" "--no-cross"
fi
[[ -z "$arm_why" ]] || ((! (run_release && BUILD_CROSS) )) || fPlan 0 "Release build: ${arm_label}" "$arm_why"
((${#BSD_RELEASE_CMD[@]})) && fPlan "$run_bsd_release" "Release build: FreeBSD" "$bsd_why"
((${#ANALYZE_CMD[@]})) && fPlan "$((! quick))" "Static analyzers" "--quick"
pkg_why="--no-package"; ((quick)) && pkg_why="--quick"
fPlan "$((PACKAGE_ENABLE && ! quick))"  "Packages"              "$pkg_why"
fPlan "$run_private"                    "Private runner"        "$priv_why"
fPlan "$run_dogfood"                    "Dogfood"               "$dogfood_why"
((had_remote_df)) && fPlan "$run_remote_df" "Dogfood (remote)" "$(remote_why "$(mode_or --no-dogfood)")"
[[ ! -x "$shots_hook" ]] || fPlan "$((SHOTS_ENABLE && ! quick))" "Screenshots" "$(quick_or "--shots turns it on")"
[[ ! -f "$demo_hook" ]] || fPlan "$((${DEMO_ENABLE:-0} && ! quick))" "Demo video" "$(quick_or "--demo turns it on")"
fPlan "$((${#GIT_PUBLISH[@]} > 0))"     "Publish"               "$(mode_or --no-publish)"
## On the host nothing runs past the lints, which go before the build.
if ((no_container)); then
	kept=(); stopAt=""
	for x in "${stepList[@]}"; do case "$x" in
		"Remote sync"|"$fmt_step"|Lints) kept+=("$x") ;;
		*) [[ -n "$stopAt" ]] || stopAt="$x"; stepOff+=("${x} (--no-container)") ;;
	esac; done
	stepList=("${kept[@]}")
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
fEcho_Clean "Run folder ..........: ${runTempDir}$( ((runTempKeep)) && echo '  (kept, --keep-temp)')"
((checks_only)) && fEcho_Clean "Mode ................: ${run_mode}, checks only: no sync, in-place format, dogfood, publish, screenshots, demo or prompt"
fEcho_Clean "Remote sync .........: $( ((no_sync)) && echo "(skipped ${sync_why})" || echo 'fetch + fast-forward before building' )"
fEcho_Clean "Build jobs ..........: $( [[ -n "${CICD_JOBS_PIN:-}" ]] && echo "${CICD_JOBS_PIN} (pinned)" || echo "half of ${cores} cores, a quarter on a busy box or for a hard step; ${CICD_MAX_JOBS} now, load ${jobsLoad}" )"
fEcho_Clean "Priority ............: nice 19$(command -v ionice >/dev/null 2>&1 && echo ', ionice idle'); containers get --cpu-shares 256 and nice 19 inside"
((no_container)) && fEcho_Clean "Containers ..........: none (--no-container): lints with the host's tools, then stop at ${stopAt:-the build}"
fEcho_Clean "CPU cap .............: $( [[ -n "${CICD_CPU_CAPPED:-}" ]] && echo "$(( cores * 50 ))% (user scope)" || echo 'job counts only (no user systemd)' )"
if ((checks_only)); then
	fEcho_Clean "Format check ........: ${FMT_CHECK_CMD[*]:-(skipped)}"
else
	fEcho_Clean "Format ..............: ${FMT_CMD[*]:-(skipped)}"
fi
fEcho_Clean "Debug build .........: ${DEBUG_BUILD_CMD[*]}"
fEcho_Clean "Tests ...............: ${TEST_CMD[*]}"
if [[ -n "${SANITIZE_CMD+x}" ]] && ((${#SANITIZE_CMD[@]})); then
	fEcho_Clean "Sanitizers ..........: ${SANITIZE_CMD[*]}"
else
	fEcho_Clean "Sanitizers ..........: (skipped)"
fi
if ((quick)); then
	fEcho_Clean "Static analyzers ....: (skipped --quick)"
elif [[ -n "${ANALYZE_CMD+x}" ]] && ((${#ANALYZE_CMD[@]})); then
	fEcho_Clean "Static analyzers ....: ${ANALYZE_CMD[*]}"
fi
if ((include_bsd)); then
	fEcho_Clean "FreeBSD .............: ${BSD_TEST_CMD[*]:-(no tests)}, then ${BSD_RELEASE_CMD[*]:-(no release)}"
elif ((no_bsd)); then
	fEcho_Clean "FreeBSD .............: (skipped $( ((no_remote)) && echo --no-remote || echo --no-bsd))"
elif ((quick)); then
	fEcho_Clean "FreeBSD .............: (skipped --quick)"
else
	fEcho_Clean "FreeBSD .............: (not set up)"
fi
if ((PROFILE_ENABLE)); then
	fEcho_Clean "Profiler ............: ${PROFILE_SECS}s run -> flamegraph SVG (headless)"
	fEcho_Clean "  output dir ........: ${profile_dir}"
	fEcho_Clean "  sampler ...........: ${PROFILE_CMD[*]:-(none)}"
else
	fEcho_Clean "Profiler ............: (disabled)"
fi
fEcho_Clean "Release (native) ....: ${RELEASE_NATIVE_CMD[*]} -> ${RELEASE_NATIVE_BIN}"
if ((BUILD_CROSS)) && ((${#CROSS_TARGETS[@]})); then
	fEcho_Clean "Release (cross) .....:${arm_note}"
	for t in "${CROSS_TARGETS[@]}"; do fEcho_Clean "    - ${t%%|*}"; done
else
	fEcho_Clean "Release (cross) .....: (skipped$( ((include_arm && ! no_arm)) && echo ', arm64 too'))"
fi
if ((PACKAGE_ENABLE)) && ((! quick)); then
	fEcho_Clean "Packages ............:"
	for entry in "${PACKAGE_CMDS[@]:-}"; do [[ -n "$entry" ]] && fEcho_Clean "    - ${entry%%|*}"; done
	for entry in "${PACKAGE_CHECKS[@]:-}"; do [[ -n "$entry" ]] && fEcho_Clean "    - ${entry%%|*} (stops the run on failure)"; done
	fEcho_Clean "  deferred ..........: macOS, AppImage, Flatpak - no toolchain on this box"
else
	fEcho_Clean "Packages ............: $( ((quick)) && echo '(skipped --quick)' || echo '(disabled)')"
fi
if ((quick)); then
	fEcho_Clean "Private runner ......: (skipped --quick)"
elif ((no_remote)); then
	fEcho_Clean "Private runner ......: (skipped --no-remote)"
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
fPlan_Show
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
if ((! no_sync)); then
	fStep "Remote sync"
	remote_sync
fi

## Stage 1: format.
if ((run_fmt)) && ((checks_only)); then
	fStep "$fmt_step"
	"${FMT_CHECK_CMD[@]}" || fDie "format check failed"
	fEcho "OK: formatting clean"
elif ((run_fmt)); then
	fStep "$fmt_step"
	"${FMT_CMD[@]}"
	fEcho "OK: formatted (${FMT_CMD[*]})"
fi

if ((no_container)); then
	((${#LINT_CMD[@]})) && fLints
	fNoContainerStop "${stopAt:-Build (debug)}"
fi

## Stage 2: debug build.
fStep "Build (debug)"
"${DEBUG_BUILD_CMD[@]}"
fEcho "OK: debug build"

## Stage 3: regression tests, lints, fuzzing, the sanitizer suite and the
## FreeBSD suite.
fStep "Tests"
"${TEST_CMD[@]}"
fEcho "OK: tests passed"
((${#LINT_CMD[@]})) && fLints
## Bounded fuzzing of the parsers that read outside input. Slow by design, so it
## is left out of --quick; the gate never runs it at all.
if ((! quick)) && ((${#FUZZ_CMD[@]})); then
	fStep "Fuzzing" hard
	if "${FUZZ_PROBE[@]}" >/dev/null 2>&1; then
		"${FUZZ_CMD[@]}"
		fEcho "OK: fuzz targets clean"
	else
		fEcho "WARNING: fuzz skipped: no clang or no libFuzzer runtime in the build container"
	fi
fi
## The suite again on a build with the address and undefined behavior
## sanitizers. It has a build of its own, so the gate leaves it out. --quick
## keeps it, since a warm run takes about as long as the plain suite.
if ((${#SANITIZE_CMD[@]})); then
	fStep "Sanitizer suite" hard
	"${SANITIZE_CMD[@]}"
	fEcho "OK: sanitizer suite clean"
fi
## The same build and suite on the FreeBSD box.
if ((run_bsd_tests)); then
	fStep "FreeBSD suite"
	## 77: the box isn't there.
	lane_rc=0; "${BSD_TEST_CMD[@]}" || lane_rc=$?
	case "$lane_rc" in
		0)  fEcho "OK: FreeBSD suite" ;;
		77) fStep_Skip ;;
		*)  fDie "FreeBSD suite (exit ${lane_rc})" ;;
	esac
fi
if ((${#DENY_CMD[@]})); then
	fStep "Dependency check"
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
if ((! quick)) && ((${#SCROLL_HARNESS[@]})); then
	fStep "Scroll harness"
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
fi

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
if ((PROFILE_ENABLE)); then
	fStep "Profiler"
	run_profiler
fi

## Stage 5: release builds. Guarded like stages 4/6 so a project without a release
## path yet (RELEASE_ENABLE=0) still completes a clean run.
built_arts=()
if ((run_release)); then
fStep "Release build (native)"
"${RELEASE_NATIVE_CMD[@]}"
[[ -f "${RELEASE_NATIVE_BIN}" ]] || fDie "native release binary missing: ${RELEASE_NATIVE_BIN}"
fEcho "OK: native release: ${RELEASE_NATIVE_BIN} ($(du -h "${RELEASE_NATIVE_BIN}" | cut -f1))"
built_arts=("${RELEASE_NATIVE_OSARCH:-native}|${RELEASE_NATIVE_BIN}")
if ((run_cross)); then
	for t in "${CROSS_TARGETS[@]}"; do
		local_label="${t%%|*}"; rest="${t#*|}"; osarch="${rest%%|*}"; rest="${rest#*|}"; art="${rest%%|*}"; cmd="${rest#*|}"
		art="${art//@VER@/${ver}}"   ## for a lane that puts the version in the file name
		fStep "Release build: ${local_label}"
		lane_rc=0; eval "${cmd}" || lane_rc=$?
		if ((lane_rc == 77)); then
			fStep_Skip
			case "$osarch" in *arm64*|*aarch64*) fDropArmPackages ;; esac
			continue
		fi
		((lane_rc == 0)) || fDie "${local_label} (exit ${lane_rc})"
		[[ -f "${art}" ]] || fDie "missing artifact for ${local_label}: ${art}"
		fEcho "OK: ${local_label}: ${art} ($(du -h "${art}" | cut -f1))"
		built_arts+=("${osarch}|${art}")
	done
fi

if ((run_bsd_release)); then
	fStep "Release build: FreeBSD"
	lane_rc=0; "${BSD_RELEASE_CMD[@]}" || lane_rc=$?
	case "$lane_rc" in
		0)  fEcho "OK: FreeBSD tarball and pkg" ;;
		77) fStep_Skip ;;
		*)  fDie "FreeBSD release build (exit ${lane_rc})" ;;
	esac
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
fi

## After the cross build, so the Windows compile list is current. Slow, so not
## under --quick.
if ((! quick)) && ((${#ANALYZE_CMD[@]})); then
	fStep "Static analyzers" hard
	"${ANALYZE_CMD[@]}"
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

	## Each check gets one "PASS|FAIL|SKIP <id> <label>" line, with the ID read
	## from the test script its command names.
	local entry label cmd rc script id
	for entry in "${PACKAGE_CHECKS[@]}"; do
		label="${entry%%|*}"; cmd="${entry#*|}"
		fEcho_Clean "checking: ${label} ..."
		rc=0; eval "${cmd}" || rc=$?
		id=''; script="$(grep -oE "cicd/[^ '\"]*/test-[^ '\"]*\.(bash|ps1)" <<< "${cmd}" | tail -n 1 || true)"
		[[ -n "${script}" && -f "${script}" ]] && id="$(sed -n -E 's/^##[[:space:]]+(- )?Test ID: ([0-9a-z]+)$/\2/p' "${script}" | head -n 1)"
		case "$rc" in
			0)  echo "PASS ${id:-?} ${label}" ;;
			77) echo "SKIP ${id:-?} ${label}"; fEcho "WARNING: ${label} skipped" ;;
			*)  echo "FAIL ${id:-?} ${label}"; fDie "${label} (exit ${rc})" ;;
		esac
	done
}
if ((PACKAGE_ENABLE && ! quick)); then
	fStep "Packages"
	build_packages
	run_package_checks
fi
## The private runner builds on boxes that are often off or busy, so it skips
## those itself and says so. Only a job that ran and failed stops the run.
priv_dogfood=()
if ((run_private)); then
	fStep "Private runner"
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
fi

## Stage 7: dogfood. Three independent installs: fixed name, rotating dated copy,
## and cross-built binaries for a box that cannot build its own.
((run_dogfood)) && fStep "Dogfood"
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

if ((run_dogfood && ! df_did)); then fEcho_Clean "no dogfood dest set up"; fi

## 7d. Whatever else this project wants kept in step with a dogfood drop. Never
## fatal - a launcher that didn't get updated is not a reason to fail a build.
if ((${#DOGFOOD_HOOK[@]})); then
	if ! (cd "${root}" && "${DOGFOOD_HOOK[@]}"); then
		fEcho "WARNING: dogfood hook failed (${DOGFOOD_HOOK[0]})"
	fi
fi

## 7e. A dogfood drop only another box can build, such as a packed Windows exe.
## Slow, so skipped under --quick, and never fatal for the same reason as 7d.
if ((run_remote_df)); then
	fStep "Dogfood (remote)"
	(cd "${root}" && "${DOGFOOD_REMOTE[@]}") || fEcho "WARNING: remote dogfood failed (${DOGFOOD_REMOTE[*]})"
fi

## Refresh README screenshots (skipped under --quick; non-fatal - a miss never
## aborts). Runs before publish so changed images get committed; rendering needs
## a headless X + magick, so a failure just warns.
if ((SHOTS_ENABLE && ! quick)) && [[ -x "$shots_hook" ]]; then
	fStep "Screenshots"
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
if ((${DEMO_ENABLE:-0} && ! quick)) && [[ -f "$demo_hook" ]]; then
	fStep "Demo video"
	if NEMO_BIN="${root}/${RELEASE_NATIVE_BIN}" python3 "$demo_hook"; then
		fEcho "OK: demo video"
	else
		fEcho "WARNING: demo video hook failed (non-fatal)"
	fi
fi

## Stage 8: backup + publish.
((${#GIT_PUBLISH[@]})) && fStep "Publish"
## Again: preflight cleared the tree, but a stage since then may have written to it
## (an in-place formatter is the obvious one).
require_clean_tree_for_publish
## Always run the publisher quiet: cicd already gave the initial prompt, so skip
## its redundant continue-prompt. With no message it still lets git open the editor.
pub_flags=(--quiet)
if ((${#GIT_PUBLISH[@]} == 0)); then
	:
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

fStep_Close PASS
if ((${#stepSkipped[@]})); then
	fEcho_Clean
	fEcho_Clean "Skipped, no box .....:"
	for x in "${stepSkipped[@]}"; do fEcho_Clean "    - ${x}"; done
fi
fSection "${APP_NAME} CI/CD${run_mode:+ ${run_mode}}: done."
fEcho_Clean


##	History:
##		- 2026-06-05 JC: Created.
##		- 2026-10-15: arm64 and FreeBSD on by default, off under --quick; --no-bsd.
##		- 2026-10-19: --quick keeps the sanitizers, profiler and Windows cross
##		  build; --include-arm works under --quick.
##		- 2026-10-21: static analyzer stage after the cross build, report only.
##		- 2026-10-24: --long (or --slow) adds arm64, now off in a plain run.
##		- 2026-10-25: --backlog-run-quick, --backlog-run-full and --merge-gate;
##		  --gate and --ci are other names for the first and last. --no-remote.
##		- 2026-10-25: each run works in its own temp folder; --keep-temp. The
##		  run's labeled containers and the project's old images go at the end.
##		- 2026-10-25: job counts from include/jobs.bash, a quarter on a busy box
##		  or for a hard step, checked before each step; nice and ionice, and a
##		  low CPU share for the containers. Each step opens and closes with a
##		  timestamped "Step N of M" line; the plan lists the steps turned off.
##		- 2026-10-26: --no-container: the lints on the host, then a stop at the
##		  first step that needs a container.
##		- 2026-10-28: a lane that exits 77, for a box that isn't there, closes
##		  its step as SKIP and the run goes on; the end lists those steps.
