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
##	   5. --publish: also attach cicd/artifacts/release/* to a GitHub Release
##	      as plain uploads (gh CLI; no Actions)
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
git rev-parse -q --verify "refs/tags/${tag}" >/dev/null && die "tag ${tag} already exists - bump the version on dev first"

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
## the tag and adds it, and its line in the sums file, once it is done.
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
git tag -a "${tag}" -m "${tag}"
echo "tagged ${tag}"

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
	## Notes are the hand-written changelog section when there is one.
	notes_file="$(mktemp)"
	trap 'rm -f "${notes_file}"' EXIT
	if ! "${here}/changelog-notes.bash" "${ver}" >"${notes_file}"; then
		echo "no changelog section for ${ver} - publishing with a placeholder body" >&2
		printf 'See the changelog for details.\n' >"${notes_file}"
	fi
	## Same number the binaries carry: both read it off HEAD's commit date.
	( source "${here}/include/source-date.bash"; fSetSourceDate "${here}/../.."
	  printf '\n---\n\nBuild %s\n' "$(python3 "${here}/../../source/build-number.py")" ) >>"${notes_file}"
	notes_arg=(--notes-file "${notes_file}")
	## A prerelease is anything with a pre-release part, matching the tag rule the
	## release workflow applies from its side.
	pre_arg=()
	if [[ "$ver" == *-* ]]; then pre_arg=(--prerelease); fi
	if ((have_artifacts)); then
		gh release create "${tag}" --title "${APP_NAME} ${ver}" "${notes_arg[@]}" "${pre_arg[@]}" \
			"${art_dir}/${EXE_NAME}-${ver}-"*
		echo "GitHub Release ${tag} created with artifacts"
	else
		gh release create "${tag}" --title "${APP_NAME} ${ver}" "${notes_arg[@]}" "${pre_arg[@]}"
		echo "GitHub Release ${tag} created (no artifacts attached - stage not wired yet)"
	fi
elif ((do_push)) && ((have_artifacts)); then
	echo "next (optional): gh release create ${tag} ${art_dir}/${EXE_NAME}-${ver}-*"
fi
