#!/bin/bash

## Every setting below is read by cicd.bash, which sources this file, so
## shellcheck sees the whole file as write-only. That is the one rule this file
## can't have on; the rest of the template's header block is gone, since none of
## it fired and a file-wide block hides real faults.
# shellcheck disable=2034  ## 'variable appears unused' - see above.

##	Purpose:
##		- Project-specific CI/CD settings for nemo-anywhere. cicd.bash stays generic;
##		  this file wires each stage to a concrete command.
##		- The stage sequence (format -> build -> test -> profile -> release -> package
##		  -> dogfood -> publish) is the enduring shape carried over from the source
##		  pipeline. What differs here is the toolchain: nemo-anywhere is C/GTK built
##		  with meson/ninja inside the `nemo-build` container, not a Rust/cargo tree.
##		- Stages nemo-anywhere can do TODAY are wired live: debug build, the test
##		  suite and launch smoke, lints, release, packaging, dogfood and git
##		  backup+publish. What is still DISABLED keeps the original cargo-era line
##		  commented out verbatim plus a "NEEDS:" note on what a meson/C equivalent
##		  would take; those stages self-skip.
##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


## Check if sourced
declare -i isSourced_t6wqf=0; [[ "${BASH_SOURCE[0]}" == "${0}" ]] || isSourced_t6wqf=1
((isSourced_t6wqf)) || { echo -e "\nError in $(basename "${BASH_SOURCE[0]}"): This script is meant to be 'sourced' from within another script.\n"; exit "${ERRNUM_MSG_ALREADY_SHOWN:-3}"; }


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Identity
APP_NAME="Nemo Anywhere"
EXE_NAME="nemo-anywhere"

## The reference Linux build runs inside this container (image nemo-build-deps:latest,
## created with --shm-size=2g). It mounts the repo root (github/) at /src and builds
## into /build. See design.md "Building". The build/test commands below auto-start it.
NEMO_CONTAINER="nemo-build"

## Directory of this config, for locating sibling helper scripts (self-contained,
## no reliance on cicd.bash internals).
_cfgdir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

## Wrapper that runs a shell command in NEMO_CONTAINER but stays resilient to a
## down/absent docker daemon: it skips-with-warning (exit 0) on an environmental
## miss instead of aborting the push with a raw socket error, and propagates a
## genuine build/smoke failure. See utility/docker-run.bash.
DOCKER_RUN="${_cfgdir}/utility/docker-run.bash"


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 1: format the source in place before anything is compiled or tested.
## Decided: NO in-place formatter for the inherited tree (a full reformat would
## bury real history in churn). The gate is the check-only lint below instead.
FMT_CMD=()
FMT_CHECK_CMD=()

## Pinned versions of the tools that run on the host, which the engine warns on
## when they drift. The lints run there. meson, ninja, gcc and clang run in the
## build containers, and the image pins those. The command runs under bash -c.
TOOL_PINS=(
	"cppcheck|2.17.1|cppcheck --version"
	"shellcheck|0.11.0|shellcheck --version | sed -n 2p"
	"ruff|0.15.22|ruff --version"
	"PSScriptAnalyzer|1.25.0|pwsh -NoProfile -Command '(Get-Module -ListAvailable PSScriptAnalyzer | Select-Object -First 1).Version.ToString()'"
	"Pillow|11.1.0|python3 -c 'import PIL; print(PIL.__version__)'"
)


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 2: debug build - READY. Configure (or reconfigure) and build in-container.
## The wrapper starts the container (and skips gracefully if docker is down) so a
## stopped box or dead daemon doesn't fail the stage. This is what the smoke runs
## against.
## -j is not optional: left alone ninja takes cores+2, and the engine's whole
## point in computing CICD_MAX_JOBS is that a pipeline run leaves the box usable.
DEBUG_BUILD_CMD=(bash "${DOCKER_RUN}" "debug build" "
	if [ -f /build/build.ninja ]; then meson setup --reconfigure /build /src/source
	else meson setup /build /src/source; fi && ninja -C /build -j ${CICD_MAX_JOBS:-2}
")

## Stage 3: regression tests - READY. The meson suite, then a headless launch and
## --version. Both run inside the container under its own Xvfb; see
## linux/run-tests.bash, which also does the build, since gate mode has no build
## stage of its own and would otherwise test whatever was last left in /build.
## -j is passed through for the same reason it is above. Same wrapper, so a
## down/absent daemon skips-with-warning instead of aborting the gate.
TEST_CMD=(bash "${DOCKER_RUN}" "tests" "NEMO_TEST_JOBS=${CICD_MAX_JOBS:-2} bash /src/cicd/linux/run-tests.bash")
## Held to CICD_MAX_JOBS cores each, since the run's own CPU cap cannot reach
## inside them.
CICD_CONTAINERS=(nemo-build nemo-build-jammy nemo-winbuild)

## Stage 3 (after tests): lints - READY. cicd/utility/lint.bash runs the C check
## (the changed C files on a feature branch, the whole tree on dev and main) and
## shellcheck over the project's own scripts. Nothing is rewritten. Each
## checker warn-skips on a box that lacks its tool, so the probe only has to say
## that a shell exists - the cppcheck probe that used to sit here took the whole
## stage down with it, Bash check included.
LINT_PROBE=(bash --version)
LINT_CMD=(bash cicd/utility/lint.bash)

## Stage 3 (after lints): fuzz the parsers that read outside input - READY.
## Bounded on purpose: each target gets FUZZ_SECS of search, and the budget
## running out is a clean result rather than a failure. A real find exits 86 and
## leaves the input that caused it under the build dir's findings/.
## Needs clang and the libFuzzer runtime inside the container; without either the
## probe fails and the stage skips with a warning. The ordinary gcc build still
## compiles the same targets and replays their seed corpus as tests, so nothing
## rots when this stage is skipped. Left out of --quick and out of the gate,
## where three minutes of searching does not belong.
FUZZ_SECS=60
FUZZ_PROBE=(bash "${DOCKER_RUN}" "fuzz probe" "bash /src/cicd/linux/fuzz.bash --probe")
FUZZ_CMD=(bash "${DOCKER_RUN}" "fuzz" "FUZZ_SECS=${FUZZ_SECS} NEMO_TEST_JOBS=${CICD_MAX_JOBS:-2} bash /src/cicd/linux/fuzz.bash")

## Stage 3 (after lints): dependency policy (licenses/advisories). NOT READY - unset.
## NEEDS: a C-world equivalent if wanted (there is no Cargo.lock to police); likely
## not applicable until there are vendored deps.
#	Rust-era original (reference only):
#	DENY_PROBE=(cargo deny --version)
#	DENY_CMD=(cargo deny check)

## Stage 3 (last): a headless behavioral harness. NOT READY - unset.
## NEEDS: an app-specific harness (the source project drove a terminal-scroll
## regression); nemo-anywhere has none yet.
#	Rust-era original (reference only):
#	SCROLL_HARNESS=(cicd/tests/scroll/run.bash)
#	SCROLL_HARNESS_WAYLAND=1


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 5: native release build - READY.
## release.bash owns the whole lane: it builds in nemo-build-jammy so the glibc floor
## stays at 2.35, stages the relocatable prefix, writes the versioned tarball and the
## sums file, and leaves the staged tree at cicd/artifacts/dogfood/ for stage 7 and
## the launcher. Incremental, so only the first run after a clone is slow.
## RELEASE_NATIVE_BIN is the wrapper inside that tree - the engine only checks it
## exists, and stage 7 installs the tree around it rather than the file itself.
RELEASE_ENABLE=1
RELEASE_NATIVE_CMD=(bash cicd/linux/release.bash)
RELEASE_NATIVE_BIN="cicd/artifacts/dogfood/${EXE_NAME}/bin/${EXE_NAME}"
RELEASE_NATIVE_OSARCH="linux-x86_64"

## Stage 5: cross-release targets. On, so the Windows .zip in stage 6 packs an exe
## built from the commit being run rather than whatever was cross-built by hand last.
## --quick and --no-cross leave it out. The Windows box still builds and dogfoods its
## own packed exe through cicd-win.ps1; this only keeps the zip current.
## The exe is copied out because the engine checks for the artifact on the host, and
## the cross build dir lives only in the container.
## Then the exe is opened under wine and its main window looked for, about twenty
## seconds, since nothing else here ever starts the Windows build's GUI.
BUILD_CROSS=1
CROSS_TARGETS=(
	"Windows x86_64 (mingw)|windows-x86_64|cicd/artifacts/cross/nemo-anywhere.exe|rm -f cicd/artifacts/cross/nemo-anywhere.exe && bash cicd/win/build-cross.bash && mkdir -p cicd/artifacts/cross && docker cp nemo-winbuild:/build-win/src/nemo-anywhere.exe cicd/artifacts/cross/nemo-anywhere.exe && docker exec nemo-winbuild bash /src/cicd/win/gui-smoke.bash"
)
#	Rust-era original (reference only - cargo/zig cross, not applicable to meson):
#	CROSS_TARGETS=(
#		"Windows x86_64 (mingw)|windows-x86_64|target/x86_64-pc-windows-gnu/release/${EXE_NAME}.exe|cargo build --release --target x86_64-pc-windows-gnu"
#		"Linux ARM64 (zig)|linux-arm64|target/aarch64-unknown-linux-gnu/release/${EXE_NAME}|cargo zigbuild --release --target aarch64-unknown-linux-gnu"
#		"Windows ARM64 (zig)|windows-arm64|target/aarch64-pc-windows-gnullvm/release/${EXE_NAME}.exe|cargo zigbuild --release --target aarch64-pc-windows-gnullvm"
#	)

## Stage 5 (after builds): collect versioned artifacts + sha256sums.
## The artifacts themselves come from the per-platform release lanes, not from this
## engine stage: cicd/linux/release.bash writes the Linux tarball + the sums file
## here, and the Windows exe is built and signed by the release-win workflow. Setting
## the dir is what lets utility/release.bash verify and attach them.
## The dir is still where the sums file is written and where utility/release.bash
## reads from, but the engine must not COLLECT into it: its collector wipes the dir
## and re-copies bare RELEASE_NATIVE_BIN binaries under a Cargo-shaped name, which
## would throw away the tarball release.bash just wrote.
RELEASE_ARTIFACT_DIR="cicd/artifacts/release"
RELEASE_COLLECT=0
VERSION_MANIFEST="source/meson.build"
#	Rust-era original (reference only):
#	RELEASE_ARTIFACT_DIR="cicd/artifacts/release"
#	VERSION_MANIFEST="source/Cargo.toml"


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 6: distributable packages - READY.
## Each PACKAGE_CMDS entry is "label|shell command"; a failure warns and the rest
## still run. PACKAGE_CHECKS entries run after them, the same way, but a failure
## stops the run before dogfood and publish; exit 77 only warns.
## Both steps work from what the per-platform release lanes already produced, so
## nothing is rebuilt here:
##   - .deb and .rpm are made from the Linux release tarball, and install the same
##     relocatable prefix under /opt plus a launcher, menu entry and icons. The
##     .deb's dependency versions are read in the Ubuntu release container so the
##     package claims the same floor the binary was built against.
##   - The Windows .zip is flattened out of the cross-build - exe at the folder
##     root beside its DLLs, which is the layout install.ps1 expects.
##   - Both installers then install, reinstall and uninstall the Linux tarball in
##     a scratch home, offline.
##   - Every file the tarball, .deb and .rpm install has to carry the app's name,
##     and the action layout editor's launcher has to work from a moved prefix.
## Deferred: BSD .pkg, macOS .pkg, AppImage, Flatpak - no toolchain here yet.
PACKAGE_ENABLE=1
PACKAGE_CMDS=(
	"Linux .deb + .rpm|bash cicd/linux/package.bash"
	"Windows .zip|bash cicd/win/pack-zip.bash"
)
PACKAGE_CHECKS=(
	"Installer check|bash cicd/linux/test-installers.bash"
	"Prefix check|bash cicd/linux/test-prefix.bash"
)

## Stage 6, after the packages: builds that need another box's OS or signing
## material, kept in a private repo. They run only when that repo is checked out at
## this path. Its runner takes --public <this repo>, waits for its own boxes, skips
## one that is off or busy, and exits 1 only when a job ran and failed. Its output
## stays in the private repo, apart from a dogfood drop for a platform this box
## cannot build (PRIVATE_DOGFOOD). Empty, or --no-private, to skip.
PRIVATE_RUNNER="${NEMO_PRIVATE_RUNNER-$(cd "${root}/../.." && pwd)/nemo-anywhere-private/repo/cicd/run.bash}"
## "<dir under the private repo>|<dogfood dests array>". The dir is the build to
## publish, with @VER@ for the version. It is used only when the runner made it
## during this run, so a build left from an earlier run is never published again.
PRIVATE_DOGFOOD=(
	"dist/@VER@/macos/dogfood/${EXE_NAME}|DOGFOOD_DESTS_MACOS"
)


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 4: profiler (non-gating flamegraph artifact) - READY.
## profile-run.bash browses a generated folder tree on a private headless display
## while sampling every thread, and renders a flamegraph. It samples by attaching
## a debugger rather than with perf, because perf needs a privileged sysctl on this
## box - the trade is wall-clock samples, so a blocked thread reads as waiting.
## flame-report.py knows that and reports busy time separately.
## It profiles the DEBUG build (stage 2): the release binaries are stripped, and a
## flamegraph with no function names is worthless.
PROFILE_ENABLE=1
PROFILE_SECS=15
PROFILE_CMD=(bash cicd/utility/profile-run.bash)
## Ask the sampler itself, so its tool list stays in one place. The probe used to be
## a bare `gdb --version`, which let a missing flamegraph renderer through and turned
## an unprofilable box into an aborted run.
PROFILE_PROBE=(bash cicd/utility/profile-run.bash --probe)
PROFILE_OUT_DIR="cicd/artifacts/profiling"
PROFILE_STRICT=0
## Unused by the current profiler; the engine's preflight still prints them.
PROFILE_WORKLOAD_SCRIPT=""
PROFILE_WORKLOAD_ARGS=""

## Pre-publish README screenshot refresh: NOT READY - off.
## NEEDS: a headless screenshot hook for the file-manager UI if wanted later.
SHOTS_ENABLE=0

## Demo video + README gif: READY, but off unless --demo is passed. Two recordings
## take about six minutes and the result only changes when the interface or the
## demo script does, so it is not worth a full run every time.
DEMO_ENABLE=0

## Full-run output is tee'd here (gitignored) so warnings from any stage can be
## reviewed after the fact. READY - kept.
LINT_LOG_DIR="cicd/artifacts/lint"


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 7: dogfood (publish the native release to the synced app dir) - READY.
## The app is a relocatable prefix, not one binary, so DOGFOOD_PREFIX_SRC names the
## staged tree stage 5 left behind and the stage publishes that whole tree under the
## program's own name.
##
## One drop, nothing else. utility/n8runfm.ps1 reads this dir on every launch, keeps
## its own GFS-rotated pool of dated versions locally, and points a symlink at the
## newest - so the pipeline has no business writing dated copies or names on PATH.
DOGFOOD_PREFIX_SRC="cicd/artifacts/dogfood/${EXE_NAME}"
## One list per platform, first existing and writable wins. Both spellings resolve to
## the same directory here, but a box usually has only one of them. A run only ever
## writes its own platform's list - the other two are here so the layout is in one
## place, and because this engine runs on macOS too.
DOGFOOD_DESTS_LINUX=(
	"${HOME}/synced/0-0/common/exec/app/linux"
	"${HOME}/.synced/Dropbox/0-0/common/exec/app/linux"
)
DOGFOOD_DESTS_MSWIN=(
	"${HOME}/synced/0-0/common/exec/app/mswin"
	"${HOME}/.synced/Dropbox/0-0/common/exec/app/mswin"
)
DOGFOOD_DESTS_MACOS=(
	"${HOME}/synced/0-0/common/exec/app/macos"
	"${HOME}/.synced/Dropbox/0-0/common/exec/app/macos"
)
case "$(uname -s)" in
	Darwin) DOGFOOD_FIXED_DESTS=("${DOGFOOD_DESTS_MACOS[@]}") ;;
	*)      DOGFOOD_FIXED_DESTS=("${DOGFOOD_DESTS_LINUX[@]}") ;;
esac
## The launcher owns the local pool now.
DOGFOOD_ROTATING_DESTS=()
DOGFOOD_PREFIX=""
DOGFOOD_TAG=""
## Cross-built binaries for another box to pick up over the sync layer. Empty: what a
## Windows box dogfoods is the packed single exe, and only Windows can pack it, so
## DOGFOOD_DESTS_MSWIN is written by that box's own pipeline rather than from here.
## A macOS build comes from the private runner, through PRIVATE_DOGFOOD.
DOGFOOD_CROSS_DESTS=()
## Run after the installs. Keeps the launcher and its wrappers in step with the
## synced copies people actually run.
DOGFOOD_HOOK=(bash cicd/utility/deploy-launcher.bash)
## The packed Windows exe, built on whichever Windows box answers, under the shared
## host lock. Off under --quick. Without it the mswin drop only moved when someone
## ran cicd-win.ps1 by hand, and it sat 18 days behind before anyone noticed.
DOGFOOD_REMOTE=(bash cicd/utility/win-dogfood.bash)


#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
## Stage 8: backup + publish to git (runs from repo root). READY.
GIT_PUBLISH=(cicd/utility/n8git_backup-and-publish)

## Set non-empty to publish hands-off (suppresses the prompt and supplies the commit
## message so `git commit` won't open an editor). Empty = interactive unless -m/-y.
PUBLISH_AUTO_MESSAGE=""

## Extra rar excludes for the version backup live in cicd/rar-excludes.conf, not here:
## the publisher reads that file itself, so the excludes hold when it is run standalone
## too. GIT_BACKUP_AND_PUBLISH_RAR_EXCLUDES stays available for one-off additions.


##	History:
##		- 2026-07-18: Adapted from the source pipeline's config.bash for nemo-anywhere
##		  (meson/container build). Ready: build, smoke test, publish. Rest disabled
##		  with NEEDS notes; nothing ported.
