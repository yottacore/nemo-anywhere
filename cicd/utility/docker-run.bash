#!/usr/bin/env bash

## Run a command inside the reference build container, resilient to a down/absent
## docker daemon - so an unreachable daemon can't turn every gated push into a raw
## socket error. Flow: ensure docker is reachable (best-effort NON-sudo nudge),
## start the container, then exec the given shell command in it. An environmental
## miss (docker absent, daemon down, container gone) SKIPS with a warning and
## exits 0, so it never hard-blocks a push; a genuine command failure (build error,
## smoke crash) propagates its exit code and still blocks.
##
## Usage: docker-run.bash <label> <sh-command>
##   NEMO_CONTAINER      selects the container (default nemo-build).
##   DOCKER_GATE_STRICT=1 turns an environmental miss into a hard failure instead
##                       of a skip (for a run that must not silently no-op).
##
## Why not auto-start the daemon: here it's a rootful system service, so starting
## it needs `sudo systemctl start docker` - which an unattended hook must not run.
## We nudge only the rootless (per-user) service, then skip with that hint.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

label="${1:-container task}"
cmd="${2:-}"
container="${NEMO_CONTAINER:-nemo-build}"
strict="${DOCKER_GATE_STRICT:-0}"

fEcho(){ echo "[ $* ]"; }

## Reproducible-build stamp. cicd.bash normally exports it before we are called;
## computing it here too keeps a standalone run honest.
# shellcheck source=include/source-date.bash
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/include/source-date.bash"
fSetSourceDate "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

## Environmental miss: skip (exit 0) so a push isn't blocked, unless strict.
skip_or_die(){
	if [[ "$strict" == "1" ]]; then
		fEcho "FAILED: ${label}: $1" >&2
		exit 1
	fi
	fEcho "WARNING: ${label} SKIPPED: $1 - not verified against the container" >&2
	exit 0
}

command -v docker >/dev/null 2>&1 || skip_or_die "docker not installed"

docker_up(){ timeout 10 docker info >/dev/null 2>&1; }

if ! docker_up; then
	## Nudge only what's startable without sudo (rootless service); harmless no-op
	## if it isn't that kind of setup. Starting a rootful daemon stays a manual step.
	if command -v systemctl >/dev/null 2>&1; then
		systemctl --user start docker.socket >/dev/null 2>&1 || true
		systemctl --user start docker        >/dev/null 2>&1 || true
	fi
	docker_up || skip_or_die "docker daemon not reachable (try: sudo systemctl start docker)"
fi

## A fresh clone has no build box yet, so the day-to-day one is made on first use,
## from the same Dockerfile and with the same flags as by hand. Any other name is
## someone's own box, and a missing one is still an env miss.
## --init reaps orphans; --ulimit core=0 keeps crash dumps out of the mounted tree.
names="$(docker ps -a --format '{{.Names}}' 2>/dev/null || true)"
if ! grep -qx "$container" <<<"$names"; then
	[[ "$container" == "nemo-build" ]] || skip_or_die "build container '${container}' not found"
	repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
	if ! docker image inspect nemo-build-deps:latest >/dev/null 2>&1; then
		fEcho "building image nemo-build-deps (first run only, takes a few minutes)"
		docker build -t nemo-build-deps:latest -f "${repo}/cicd/linux/Dockerfile.dev" "${repo}/cicd/linux/" >/dev/null \
			|| skip_or_die "could not build image nemo-build-deps"
	fi
	fEcho "creating container ${container}"
	docker run -d --init --ulimit core=0 --shm-size=2g --cpus "$(( $(nproc) / 2 ))" --name "$container" \
		-v "${repo}:/src" nemo-build-deps:latest sleep infinity >/dev/null \
		|| skip_or_die "could not create container '${container}'"
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
## the container's environment, not ours - or the linker stamps the clock.
exec docker exec -e "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}" "$container" sh -c "ulimit -c 0; $cmd"
