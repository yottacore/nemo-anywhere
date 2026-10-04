#!/usr/bin/env bash

##	- Purpose: Write a release's notes: the version's changelog section, the
##	  Downloads table, and the build number. The one place both release lanes
##	  get them from, so the notes come out the same whichever lane is last.
##	- With a version, prints the notes. The table comes from an assets file in
##	  the form `gh release view <tag> --json assets` prints, and is left out
##	  without one.
##	- With --update <tag>, reads what the release holds now and edits its
##	  notes to match. Each lane runs this after its own uploads. The local cut
##	  and the Windows build both add files, minutes apart, so the list is read
##	  again after the edit, and the edit is redone if it moved meanwhile.
##	- A version with no changelog section gets a placeholder line, not a
##	  generated commit list.
##	- Syntax: release-notes.bash [--changelog <file>] <version> [assets.json]
##	          release-notes.bash [--changelog <file>] --update <tag>

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fEcho(){ echo "[ $* ]"; }
fHelp(){ sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; }

changelog=""
update=0
while (($#)); do case "$1" in
	--changelog) changelog="${2:-}"; shift 2 ;;
	--update)    update=1; shift ;;
	-h|--help)   fHelp; exit 0 ;;
	*) break ;;
esac; done
[[ -n "${1:-}" ]] || { fHelp; exit 2; }

## A Windows runner may answer python3 with the store stub, which runs nothing.
py=""
for cand in python3 python; do
	if "$cand" -c '' >/dev/null 2>&1; then py="$cand"; break; fi
done
[[ -n "$py" ]] || { fEcho "FAILED: release notes need python" >&2; exit 1; }

## $1 version, $2 assets file or empty.
fNotes(){
	local ver="${1#v}" body table build
	if ! body="$(bash "${here}/changelog-notes.bash" "$ver" ${changelog:+"$changelog"})"; then
		echo "no changelog section for ${ver} - notes get a placeholder" >&2
		body="See the changelog for details."
	fi
	table=""
	[[ -z "${2:-}" ]] || table="$("$py" "${here}/release-table.py" "$2")"
	## Same number the binaries carry: both read it off HEAD's commit date.
	build="$( source "${here}/include/source-date.bash"; fSetSourceDate "${here}/../.."
		"$py" "${here}/../../source/build-number.py" )"
	printf '%s\n' "$body"
	[[ -z "$table" ]] || printf '\n%s\n' "$table"
	printf '\n---\n\nBuild %s\n' "$build"
}

if ((! update)); then
	fNotes "$1" "${2:-}"
	exit 0
fi

tag="$1"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/release-notes.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT
gh release view "$tag" --json assets > "${scratch}/assets.json"
for try in 1 2 3 4 5; do
	"$py" "${here}/release-table.py" --names "${scratch}/assets.json" > "${scratch}/before.txt"
	fNotes "$tag" "${scratch}/assets.json" > "${scratch}/notes.md"
	gh release edit "$tag" --notes-file "${scratch}/notes.md" >/dev/null
	gh release view "$tag" --json assets > "${scratch}/assets.json"
	"$py" "${here}/release-table.py" --names "${scratch}/assets.json" > "${scratch}/after.txt"
	if cmp -s "${scratch}/before.txt" "${scratch}/after.txt"; then
		fEcho "OK: ${tag} notes list $(wc -l < "${scratch}/after.txt" | tr -d ' ') file(s)"
		exit 0
	fi
	fEcho "${tag} files changed while writing its notes, try ${try}"
done
fEcho "FAILED: ${tag} files kept changing; rerun: cicd/utility/release-notes.bash --update ${tag}" >&2
exit 1
