#!/usr/bin/env bash

##	- Purpose: run the tool checkers (cppcheck, shellcheck, ruff,
##	  PSScriptAnalyzer) inside nemo-lint, so every Linux box gets the same
##	  versions and the same findings. See cicd/linux/Dockerfile.lint.
##	- fLintImage <repo-root> -> prints the image tag to use, or nothing for the
##	  host's own tools. Builds the image the first time a Dockerfile.lint hash
##	  is seen, and drops older nemo-lint tags after.
##	- fLintRun <repo-root> <image|""> <command...> -> runs the command in the
##	  image as the calling user, or on the host when the image is "".
##	- Host tools are used under MSYS2, with no docker or no daemon, when the
##	  build fails, or with NEMO_LINT_HOST=1.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


fLintImage(){
	local root="$1" file hash tag old
	file="${root}/cicd/linux/Dockerfile.lint"
	[[ "${NEMO_LINT_HOST:-0}" != "1" ]] || { echo "[ lint tools: host (NEMO_LINT_HOST=1) ]" >&2; return 0 ;}
	[[ "$(uname -o 2>/dev/null)" != "Msys" ]] || return 0
	[[ -f "$file" ]] || return 0
	if ! command -v docker >/dev/null 2>&1 || ! timeout 10 docker info >/dev/null 2>&1; then
		echo "[ WARNING: lint tools: host, docker not reachable ]" >&2
		return 0
	fi
	hash="$(sha256sum "$file")"
	tag="nemo-lint:${hash:0:12}"
	if ! docker image inspect "$tag" >/dev/null 2>&1; then
		echo "[ Building ${tag}, one time per Dockerfile.lint change ]" >&2
		if ! docker build -q -t "$tag" - <"$file" >&2; then
			echo "[ WARNING: lint tools: host, ${tag} did not build ]" >&2
			return 0
		fi
		## By tag, never -f, so an image something still runs stays put.
		while read -r old; do
			[[ "$old" == "$tag" ]] || docker rmi "$old" >/dev/null 2>&1 || true
		done < <(docker images nemo-lint --format '{{.Repository}}:{{.Tag}}')
	fi
	echo "[ lint tools: ${tag} ]" >&2
	echo "$tag"
}

## The repo goes in at its own path, and so does the git dir when it lives
## elsewhere, which it does in a worktree.
fLintRun(){
	local root="$1" img="$2"; shift 2
	[[ -n "$img" ]] || { "$@"; return ;}
	local common; local -a mounts=(-v "${root}:${root}")
	common="$(git -C "$root" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
	[[ -z "$common" || "$common" == "${root}/"* ]] || mounts+=(-v "${common}:${common}")
	docker run --rm --network none --user "$(id -u):$(id -g)" "${mounts[@]}" -w "$root" \
		-e CPPCHECK_STRICT -e SHELLCHECK_STRICT -e RUFF_STRICT -e PSA_STRICT \
		"$img" "$@"
}
