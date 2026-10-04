#!/usr/bin/env bash

##	- Purpose: Cut a release locally from main. No hosted CI: the tag, the
##	  artifacts, and the optional GitHub Release upload all happen on this box.
##	- Flow (run AFTER merging dev into main --no-ff):
##	   1. verify: on main, clean tree, version bumped, README badge matches
##	   2. artifacts exist, match their sums file, and were built from HEAD itself,
##	      not from dev before the merge
##	   3. tag the merge: v<version>, where <version> comes from source/meson.build
##	      alone (the build stamps from it too, so they can never disagree)
##	   4. --push: push main + the tag
##	   5. --publish: wait for the hosted builds the tag started
##	      (RELEASE_WORKFLOWS), download their files, and make the GitHub Release
##	      with those and cicd/artifacts/release/* in one step, one sums file
##	      and the Downloads table included. Only this lane makes a release.
##	      Run again after a failed hosted build, it picks up from the pushed tag.
##	- Status: tag + push work today. Artifact verification/attach is gated off until
##	  the release-build stage produces host-side artifacts (RELEASE_ARTIFACT_DIR in
##	  config.bash). First release also needs a "Release-<ver>" README badge to exist.
##	- Syntax:
##	  cicd/utility/release.bash [--push] [--publish] [-y]
##	  With no flags it tags only and prints the remaining steps.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
cd "${root}"
source "${here}/../config.bash"

do_push=0; do_publish=0; assume_yes=0
while (($#)); do case "$1" in
	--push)    do_push=1; shift ;;
	--publish) do_push=1; do_publish=1; shift ;;
	-y|--yes)  assume_yes=1; shift ;;
	-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	*) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
esac; done

die(){ echo "FAILED: $*" >&2; exit 1; }

## 1. Preconditions: releases only cut from a clean main, with the version
## already bumped on dev (so nothing is ever committed directly on main here).
branch="$(git rev-parse --abbrev-ref HEAD)"
[[ "$branch" == "main" ]] || die "not on main (on ${branch}); merge dev --no-ff into main first"
if ! git diff --quiet || ! git diff --cached --quiet; then die "working tree not clean"; fi

## meson.build form: project('nemo-anywhere', 'c', version : '6.6.4', ...). Grab the
## first `version : '...'` (the project version; dep version checks use `>=`, not this).
ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${VERSION_MANIFEST}" | head -1)"
[[ -n "$ver" ]] || die "no version in ${VERSION_MANIFEST}"
tag="v${ver}"
resume=0
if git rev-parse -q --verify "refs/tags/${tag}" >/dev/null; then
	## A publish stopped by a hosted build that failed starts again from here.
	if ((do_publish)) && [[ "$(git rev-parse "${tag}^{commit}")" == "$(git rev-parse HEAD)" ]]; then
		resume=1
	else
		die "tag ${tag} already exists - bump the version on dev first"
	fi
fi

## Only a hand-written release badge needs checking, and it must be bumped on dev
## with the version (shields.io escapes '-' as '--'), never patched here on main.
## A badge that reads the release off GitHub keeps itself in step, so it is skipped.
if grep -q 'shields.io/badge/Release-' README.md; then
	badge_ver="${ver//-/--}"
	grep -q "Release-${badge_ver}-" README.md || die "README release badge does not say ${ver} - update it on dev before the release merge"
fi

## 2. Release artifacts must exist and carry this version (full cicd run makes them).
## Gated on a configured RELEASE_ARTIFACT_DIR; without one a release is tag and
## push only. The Windows exe is not among them: the hosted workflow builds it on
## the tag, and --publish fetches it from there.
have_artifacts=0
art_dir="${RELEASE_ARTIFACT_DIR:-}"
if [[ -n "$art_dir" ]]; then
	have_artifacts=1
	sums="${art_dir}/${EXE_NAME}-${ver}-sha256sums.txt"
	[[ -s "$sums" ]] || die "no ${sums} - run cicd/cicd.bash (full, not --quick) first"
	( cd "${art_dir}" && sha256sum -c "${EXE_NAME}-${ver}-sha256sums.txt" >/dev/null ) || die "artifact checksums do not verify"
	## The tag goes on HEAD, so the artifacts have to be HEAD's build. Built on dev
	## before the merge, they have dev's commit date: a rebuild of the tag would
	## not match them, and the build number in the notes would be one no binary has.
	shopt -s nullglob
	arts=("${art_dir}/${EXE_NAME}-${ver}-"*)
	shopt -u nullglob
	head_ct="$(git log -1 --format=%ct)"
	if ! stamps="$(python3 "${here}/release-stamps.py" --expect "${head_ct}" --exe "${EXE_NAME}.exe" "${arts[@]}")"; then
		printf '%s\n' "$stamps" >&2
		die "artifacts are not stamped ${head_ct}, HEAD's commit date - rebuild them here with cicd/cicd.bash --no-publish (not --quick) first"
	fi
fi

echo ""
echo "Release ${tag} from $(git rev-parse --short HEAD) on main"
if ((have_artifacts)); then
	echo "Artifacts:"; printf '  %s\n' "${arts[@]}"
else
	echo "Artifacts: (none - tag-only release; artifact stage not wired yet)"
fi
echo "Push: ${do_push}  Publish (gh): ${do_publish}"
if ((! assume_yes)); then read -r -p "Proceed? [y/N] " a; [[ "$a" == [yY]* ]] || exit 1; fi

## 3. Tag the merge.
if ((resume)); then
	echo "tag ${tag} is there already on HEAD; publishing it"
else
	git tag -a "${tag}" -m "${tag}"
	echo "tagged ${tag}"
fi

## 4/5. Push and publish.
if ((do_push)); then
	git push origin main
	git push origin "${tag}"
	echo "pushed main + ${tag}"
else
	echo "next: git push origin main && git push origin ${tag}"
fi
if ((do_publish)); then
	command -v gh >/dev/null 2>&1 || die "gh CLI not found"
	pub_dir="$(mktemp -d)"
	trap 'rm -rf "${pub_dir}"' EXIT
	## gh leaves a draft when an upload fails part way.
	if is_draft="$(gh release view "${tag}" --json isDraft --jq .isDraft 2>/dev/null)"; then
		[[ "$is_draft" == true ]] || die "release ${tag} is published already"
		die "a draft release for ${tag} is left from a failed publish; delete it (gh release delete ${tag} -y) and rerun"
	fi
	sums_out="${pub_dir}/${EXE_NAME}-${ver}-sha256sums.txt"
	: >"${sums_out}"
	files=()
	if ((have_artifacts)); then
		cp "${sums}" "${sums_out}"
		for f in "${arts[@]}"; do [[ "$f" == "$sums" ]] || files+=("$f"); done
	fi
	## Each hosted build hands over its files as a release-files artifact,
	## already named for the release. All of them come down first, so the
	## release goes up whole or not at all.
	head_sha="$(git rev-parse HEAD)"
	for wf in "${RELEASE_WORKFLOWS[@]}"; do
		run=""
		for _ in $(seq 1 "${RELEASE_RUN_LOOKS:-20}"); do
			run="$(gh run list --workflow "${wf}" --commit "${head_sha}" --event push --json databaseId,headBranch \
				--jq "[.[] | select(.headBranch == \"${tag}\")][0].databaseId // empty" 2>/dev/null || true)"
			[[ -z "$run" ]] || break
			sleep "${RELEASE_POLL_SECS:-15}"
		done
		[[ -n "$run" ]] || die "no ${wf} run for ${tag} turned up; start one, then rerun: cicd/utility/release.bash --publish"
		echo "waiting for ${wf} run ${run}"
		state=""
		for _ in $(seq 1 120); do
			state="$(gh run view "${run}" --json status,conclusion --jq '.status + " " + .conclusion' 2>/dev/null || true)"
			[[ "$state" != completed* ]] || break
			sleep "${RELEASE_POLL_SECS:-30}"
		done
		[[ "$state" == "completed success" ]] || die "${wf} run ${run} ended '${state:-still running}'; once it passes (gh run rerun ${run}), rerun: cicd/utility/release.bash --publish"
		got_dir="${pub_dir}/from-${wf%.*}"
		gh run download "${run}" --name release-files --dir "${got_dir}" || die "could not download release-files from ${wf} run ${run}"
		shopt -s nullglob
		got=("${got_dir}"/*)
		shopt -u nullglob
		((${#got[@]})) || die "${wf} run ${run} handed over no files"
		for f in "${got[@]}"; do
			name="${f##*/}"
			[[ -f "$f" && "$name" == "${EXE_NAME}-${ver}-"* ]] || die "${wf} handed over '${name}', not a ${EXE_NAME}-${ver}-* file"
			for have in "${files[@]}"; do [[ "${have##*/}" != "$name" ]] || die "${wf} handed over ${name}, which is here already"; done
			( cd "${got_dir}" && sha256sum "${name}" ) >>"${sums_out}"
			files+=("$f")
		done
	done
	## Every file has its line, so the sums file is never one lane's half.
	sort -k2 -o "${sums_out}" "${sums_out}"
	if [[ -s "${sums_out}" ]]; then files+=("${sums_out}"); fi
	## The table is written before the upload, from the names GitHub will keep
	## (anything outside [A-Za-z0-9._-] becomes a dot), so the release is never
	## seen without it.
	base_url="$(gh repo view --json url --jq .url)/releases/download/${tag}"
	python3 -c 'import json, re, sys
base = sys.argv[1]
names = [re.sub(r"[^A-Za-z0-9._-]", ".", n) for n in sys.argv[2:]]
print(json.dumps({"assets": [{"name": n, "state": "uploaded", "url": base + "/" + n} for n in names]}))' \
		"${base_url}" "${files[@]##*/}" >"${pub_dir}/assets.json"
	notes_file="${pub_dir}/notes.md"
	bash "${here}/release-notes.bash" "${ver}" "${pub_dir}/assets.json" >"${notes_file}"
	## A prerelease is anything with a pre-release part.
	pre_arg=()
	if [[ "$ver" == *-* ]]; then pre_arg=(--prerelease); fi
	## With files given, gh makes a draft, uploads, then publishes.
	gh release create "${tag}" --verify-tag --title "${APP_NAME} ${ver}" --notes-file "${notes_file}" "${pre_arg[@]}" "${files[@]}" \
		|| die "gh release create ${tag} failed"
	echo "GitHub Release ${tag} made with ${#files[@]} file(s)"
	## Read back what GitHub has, in case it named a file some other way.
	bash "${here}/release-notes.bash" --update "${tag}" \
		|| echo "WARNING: Downloads table not checked; rerun: cicd/utility/release-notes.bash --update ${tag}" >&2
elif ((do_push)); then
	echo "next: cicd/utility/release.bash --publish (waits for the hosted builds, then makes the release)"
fi
