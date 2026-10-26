#!/usr/bin/env bash

##	- Purpose: Docker cleanup at the end of a run. Everything the pipeline makes
##	  carries dev.project and dev.run labels; this removes only what carries
##	  this project's.
##	- fDockerSweep [name...] -> removes this run's containers, except the named
##	  ones (the long-lived build boxes), then the project's dangling images and
##	  its tags past the newest 2 of each repository, then build cache older
##	  than a week. Prints one line with what went and about what it freed.
##	  Needs CICD_PROJECT and CICD_RUN, which fRunTemp_Make sets. With
##	  runTempKeep=1 the run's containers stay.
##	- Never docker rmi -f or docker system prune, so an image in use stays put.
##	- Syntax: source this file; it defines functions only.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


## Sum of what docker system df lists, in bytes, or nothing. The daemon runs one
## df at a time and refuses a second, so it gets 3 tries.
fDockerSweep_Bytes(){
	local df try
	for try in 1 2 3; do
		df="$(docker system df --format '{{.Size}}' 2> /dev/null)" && break
		df=""; ((try == 3)) || sleep 2
	done
	[[ -n "${df}" ]] || return 0
	awk '
		{ n = $1 + 0; u = $1; sub(/^[0-9.]+/, "", u)
		  m["B"] = 1; m["kB"] = 1e3; m["KB"] = 1e3; m["MB"] = 1e6; m["GB"] = 1e9; m["TB"] = 1e12
		  t += n * (u in m ? m[u] : 1) }
		END { printf "%.0f\n", t }' <<< "${df}"
}

fDockerSweep(){
	local project="${CICD_PROJECT:-}" run="${CICD_RUN:-}" before after freed id name ref
	local containers=0 images=0
	[[ -n "${project}" && -n "${run}" ]] || return 0
	if ! command -v docker > /dev/null 2>&1 || ! timeout 10 docker info > /dev/null 2>&1; then return 0; fi
	before="$(fDockerSweep_Bytes)"
	if ((! ${runTempKeep:-0})); then
		while read -r id name; do
			if [[ -n "${id}" && " ${*} " != *" ${name} "* ]] && docker rm -f "${id}" > /dev/null 2>&1; then
				containers=$((containers + 1))
			fi
		done < <(docker ps -a --filter "label=dev.project=${project}" --filter "label=dev.run=${run}" --format '{{.ID}} {{.Names}}' 2> /dev/null)
	fi
	while read -r id; do
		if [[ -n "${id}" ]] && docker rmi "${id}" > /dev/null 2>&1; then images=$((images + 1)); fi
	done < <(docker images -q --filter dangling=true --filter "label=dev.project=${project}" 2> /dev/null)
	## docker images lists newest first.
	while read -r ref; do
		if [[ -n "${ref}" ]] && docker rmi "${ref}" > /dev/null 2>&1; then images=$((images + 1)); fi
	done < <(docker images --filter "label=dev.project=${project}" --format '{{.Repository}} {{.Tag}}' 2> /dev/null \
		| awk '$2 != "<none>" && seen[$1]++ >= 2 { print $1 ":" $2 }')
	docker builder prune -f --filter until=168h > /dev/null 2>&1 || true
	after="$(fDockerSweep_Bytes)"
	freed="size not known"
	if [[ "${before}" =~ ^[0-9]+$ && "${after}" =~ ^[0-9]+$ ]]; then
		freed="$(awk -v b="$((before > after ? before - after : 0))" 'BEGIN { if (b >= 1e9) printf "%.1f GB", b / 1e9; else printf "%.0f MB", b / 1e6 }')"
		freed="about ${freed} freed"
	fi
	printf '[ docker: removed %d container(s) and %d image(s), and build cache over a week old; %s ]\n' "${containers}" "${images}" "${freed}"
}


##	History:
##		- 2026-10-25: Created.
