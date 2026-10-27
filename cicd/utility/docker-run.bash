#!/usr/bin/env bash

## Run a command in a build container of its own. Flow: ensure docker is
## reachable (best-effort NON-sudo nudge), find the image, then run the given
## shell command in a fresh container named <project>-<run folder>-<step>, which
## goes when the command ends, unless --keep-temp (CICD_KEEP_TEMP=1). The build
## dirs that carry over between steps are cache volumes, held under 2 GB by
## linux/cache-cap.bash. With docker absent, the daemon down, or no image to be
## had, it says so in one line and runs the command on the host, if the host has
## the build stack. If not, as on the main dev box, which never gets the GTK dev
## stack, it stops with one line and exit 1: a skip would read as a pass.
## CICD_NO_CONTAINER=1 (cicd.bash --no-container) stops at once. A genuine
## command failure propagates its exit code. Backlog 2026102617145688 and
## 2026102617253209.
##
## Usage: docker-run.bash <label> <sh-command>
##   <label> names the step, and the container after it.
##   NEMO_IMAGE          the image (default nemo-build-deps:init, or a fresh
##                       clone's nemo-build-deps:latest from linux/Dockerfile.dev).
##   The command finds the tree at ${CICD_SRC} and builds under ${CICD_BUILD_ROOT}:
##   /src and empty in the container, the clone and its build/host on the host.
##   stdin reaches the command when it isn't a terminal.
##
## Why not auto-start the daemon: here it's a rootful system service, so starting
## it needs `sudo systemctl start docker` - which an unattended hook must not run.
## We nudge only the rootless (per-user) service, then fall back with that hint.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

label="${1:-container task}"
cmd="${2:-}"
image="${NEMO_IMAGE:-nemo-build-deps:init}"
project="${CICD_PROJECT:-nemo-anywhere}"
run="${CICD_RUN:-none}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

## Reproducible-build stamp. cicd.bash normally exports it before we are called;
## computing it here too keeps a standalone run honest.
# shellcheck source=include/source-date.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/include/source-date.bash"
fSetSourceDate "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
## The job count the command reads, as cicd.bash set it for this step, and the
## step container's name and cache volumes.
# shellcheck source=include/step-box.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/include/step-box.bash"
fJobs_Default

if [[ "${CICD_NO_CONTAINER:-}" == "1" ]]; then
	fEcho "STOPPED: ${label} needs a container, and --no-container runs on the host" >&2
	exit 1
fi

## Environmental miss: the host, when it can build the app.
fOnHost(){
	local missing=() tool
	for tool in meson ninja pkg-config; do command -v "$tool" >/dev/null 2>&1 || missing+=("$tool"); done
	((${#missing[@]})) || pkg-config --exists gtk+-3.0 2>/dev/null || missing+=("gtk+-3.0")
	if ((${#missing[@]})); then
		fEcho "STOPPED: ${label}: $1, and the host has no ${missing[*]} to run it on" >&2
		exit 1
	fi
	fEcho "${label}: $1, so it runs on the host" >&2
	mkdir -p "${repo}/build/host"
	export CICD_SRC="${repo}" CICD_BUILD_ROOT="${repo}/build/host"
	exec nice -n 19 sh -c "ulimit -c 0; ionice -c 3 -p \$\$ 2>/dev/null; $cmd"
}

command -v docker >/dev/null 2>&1 || fOnHost "docker not installed"

docker_up(){ timeout 10 docker info >/dev/null 2>&1; }

if ! docker_up; then
	## Nudge only what's startable without sudo (rootless service); harmless no-op
	## if it isn't that kind of setup. Starting a rootful daemon stays a manual step.
	if command -v systemctl >/dev/null 2>&1; then
		systemctl --user start docker.socket >/dev/null 2>&1 || true
		systemctl --user start docker        >/dev/null 2>&1 || true
	fi
	docker_up || fOnHost "docker daemon not reachable (try: sudo systemctl start docker)"
fi

## A fresh clone has no :init snapshot, so it builds the image from the same
## Dockerfile as by hand, on first use. A NEMO_IMAGE someone named and doesn't
## have is still an env miss.
if ! docker image inspect "$image" >/dev/null 2>&1; then
	[[ -z "${NEMO_IMAGE:-}" ]] || fOnHost "image '${image}' not found"
	image="nemo-build-deps:latest"
	if ! docker image inspect "$image" >/dev/null 2>&1; then
		fEcho "building image nemo-build-deps (first run only, takes a few minutes)" >&2
		docker build --label "dev.project=${project}" --label "dev.run=${run}" -t "$image" -f "${repo}/cicd/linux/Dockerfile.dev" "${repo}/cicd/linux/" >/dev/null \
			|| fOnHost "could not build image nemo-build-deps"
	fi
fi

for vol in build build-fuzz; do
	rc=0; fStepBox_Volume "${project}-${vol}" "${label}" || rc=$?
	case "${rc}" in
		0) ;;
		1) exit 1 ;;
		*) fOnHost "could not make volume ${project}-${vol}" ;;
	esac
done
name="$(fStepBox_Name "${label}")"

keep=(--rm); [[ "${CICD_KEEP_TEMP:-0}" != 1 ]] || keep=()
stdin=(); [[ -t 0 ]] || stdin=(-i)

## Real work: its exit code is the genuine result and still gates the push.
## 'ulimit -c 0' first: the container's workdir IS the mounted repo, and the kernel's
## core_pattern is a bare relative name, so a crash here drops a root-owned core.<pid>
## into the tree - unreadable to the host user, and enough to abort the next backup.
## SOURCE_DATE_EPOCH has to be handed across explicitly - the container starts with
## the image's environment, not ours - or the linker stamps the clock. Same for the
## job count, and for nice: the container doesn't inherit ours.
## --init reaps orphans; --ulimit core=0 keeps crash dumps out of the mounted tree.
## The two --security-opt let bubblewrap make the namespaces each test's jail
## needs (linux/jail.bash); docker's default profiles refuse them even to root.
exec docker run "${keep[@]}" "${stdin[@]}" --init --ulimit core=0 --shm-size=2g --cpus "${CICD_MAX_JOBS}" --cpu-shares 256 --name "$name" \
	--label "dev.project=${project}" --label "dev.run=${run}" \
	--security-opt seccomp=unconfined --security-opt apparmor=unconfined \
	-v "${repo}:/src" -v "${project}-build:/build" -v "${project}-build-fuzz:/build-fuzz" -w /src \
	-e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}" -e "CICD_MAX_JOBS=${CICD_MAX_JOBS}" -e CICD_SRC=/src -e CICD_BUILD_ROOT= -e CICD_CACHE_CAP_KB \
	"$image" nice -n 19 sh -c "ulimit -c 0; ionice -c 3 -p \$\$ 2>/dev/null; bash /src/cicd/linux/cache-cap.bash /build /build-fuzz || true; $cmd"
