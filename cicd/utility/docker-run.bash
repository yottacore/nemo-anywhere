#!/usr/bin/env bash

## Run a command inside the reference build container. Flow: ensure docker is
## reachable (best-effort NON-sudo nudge), start the container, then exec the given
## shell command in it. With docker absent, the daemon down, or no image or
## container to be had, it says so in one line and runs the command on the host,
## if the host has the build stack. If not, as on the main dev box, which never
## gets the GTK dev stack, it stops with one line and exit 1: a skip would read as
## a pass. CICD_NO_CONTAINER=1 (cicd.bash --no-container) stops at once. A genuine
## command failure propagates its exit code. Backlog 2026102617145688.
##
## Usage: docker-run.bash <label> <sh-command>
##   NEMO_CONTAINER      selects the container (default nemo-build).
##   The command finds the tree at ${CICD_SRC} and builds under ${CICD_BUILD_ROOT}:
##   /src and empty in the container, the clone and its build/host on the host.
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
container="${NEMO_CONTAINER:-nemo-build}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

## Reproducible-build stamp. cicd.bash normally exports it before we are called;
## computing it here too keeps a standalone run honest.
# shellcheck source=include/source-date.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/include/source-date.bash"
fSetSourceDate "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
## The job count the command reads, as cicd.bash set it for this step.
# shellcheck source=include/jobs.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/include/jobs.bash"
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

## A fresh clone has no build box yet, so the day-to-day one is made on first use,
## from the same Dockerfile and with the same flags as by hand. Any other name is
## someone's own box, and a missing one is still an env miss.
## --init reaps orphans; --ulimit core=0 keeps crash dumps out of the mounted tree.
## The two --security-opt let bubblewrap make the namespaces each test's jail
## needs (linux/jail.bash); docker's default profiles refuse them even to root.
names="$(docker ps -a --format '{{.Names}}' 2>/dev/null || true)"
if ! grep -qx "$container" <<<"$names"; then
	[[ "$container" == "nemo-build" ]] || fOnHost "build container '${container}' not found"
	if ! docker image inspect nemo-build-deps:latest >/dev/null 2>&1; then
		fEcho "building image nemo-build-deps (first run only, takes a few minutes)"
		docker build --label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" -t nemo-build-deps:latest -f "${repo}/cicd/linux/Dockerfile.dev" "${repo}/cicd/linux/" >/dev/null \
			|| fOnHost "could not build image nemo-build-deps"
	fi
	fEcho "creating container ${container}"
	docker run -d --init --ulimit core=0 --shm-size=2g --cpus "${CICD_MAX_JOBS}" --cpu-shares 256 --name "$container" \
		--label "dev.project=${CICD_PROJECT:-nemo-anywhere}" --label "dev.run=${CICD_RUN:-none}" \
		--security-opt seccomp=unconfined --security-opt apparmor=unconfined \
		-v "${repo}:/src" nemo-build-deps:latest sleep infinity >/dev/null \
		|| fOnHost "could not create container '${container}'"
fi
docker start "$container" >/dev/null 2>&1 || true

## The container builds the clone it was made on. Called from any other, such as
## a second worktree, it would pass or fail that clone instead.
here_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
mounted="$(docker inspect "$container" --format '{{range .Mounts}}{{if eq .Destination "/src"}}{{.Source}}{{end}}{{end}}' 2>/dev/null || true)"
if [[ -n "$mounted" && "$(realpath -m "$mounted")" != "$here_repo" ]]; then
	fEcho "FAILED: ${label}: ${container} builds ${mounted}, not ${here_repo}" >&2
	exit 1
fi

## Real work: its exit code is the genuine result and still gates the push.
## 'ulimit -c 0' first: the container's workdir IS the mounted repo, and the kernel's
## core_pattern is a bare relative name, so a crash here drops a root-owned core.<pid>
## into the tree - unreadable to the host user, and enough to abort the next backup.
## SOURCE_DATE_EPOCH has to be handed across explicitly - docker exec starts with
## the container's environment, not ours - or the linker stamps the clock. Same
## for the job count, and for nice: docker exec doesn't inherit ours.
exec docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}" -e "CICD_MAX_JOBS=${CICD_MAX_JOBS}" -e CICD_SRC=/src -e CICD_BUILD_ROOT= "$container" \
	nice -n 19 sh -c "ulimit -c 0; ionice -c 3 -p \$\$ 2>/dev/null; $cmd"
