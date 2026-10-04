#!/usr/bin/env bash

##	- Purpose: Check the Downloads table in release notes: OS in rows, CPU in
##	  columns, an empty cell where nothing was built, links as GitHub has the
##	  files named, and checksums kept out of the table. Also that the local cut
##	  waits for the hosted Windows build and makes the release whole, in one
##	  step, and makes none when that build fails.
##	- Runs release-notes.bash over made-up asset lists, then a copy of
##	  release.bash --publish in a scratch repo against a stand-in gh. Nothing
##	  reaches GitHub.
##	- Needs git and python3; exit 77 without them. pandoc, when there, checks
##	  that the table renders as one.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-release-notes.bash
##	- Test ID: rjf2v5d5

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

## Releases are cut on Linux, and a Windows box may only have the python3 stub.
if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "release notes check skipped: Linux only"; exit 77; fi
for tool in git python3; do
	if ! command -v "$tool" >/dev/null 2>&1; then fEcho "release notes check skipped: no ${tool}"; exit 77; fi
done

scratch="$(mktemp -d "${TMPDIR:-/tmp}/release-notes-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_CONFIG_COUNT GIT_DIR GIT_WORK_TREE GH_TOKEN GH_REPO GITHUB_TOKEN
export SOURCE_DATE_EPOCH=1790086400

ver="9.9.9-beta1"
tag="v${ver}"
base="https://github.com/example/nemo-anywhere/releases/download/${tag}"
app="nemo-anywhere-${ver}"
changelog="${scratch}/changelog.md"
printf '# Changelog\n\n## v%s - 2026-10-04\n\n### Added\n\n- Something new.\n\n## v9.9.8 - 2026-01-01\n\n- Old.\n' "$ver" > "$changelog"

## $1 output, then names as GitHub has them. name@state for one not uploaded yet.
fAssets(){
	local out="$1" sep="" n st
	shift
	{
		printf '{"assets":['
		for n in "$@"; do
			st=uploaded
			if [[ "$n" == *@* ]]; then st="${n#*@}"; n="${n%@*}"; fi
			printf '%s{"name":"%s","state":"%s","url":"%s/%s"}' "$sep" "$n" "$st" "$base" "$n"
			sep=","
		done
		printf ']}\n'
	} > "$out"
}

## A cell of the table, by row label and column header, with GitHub's rule
## that a row short of cells has empty ones at the end.
fCell(){
	ROW="$2" COL="$3" awk -F'|' '
		/^\| / && !head { for (i = 2; i <= NF; i++) { h = $i; gsub(/^ +| +$/, "", h); col[h] = i }; head = 1; next }
		/^\| / { r = $2; gsub(/^ +| +$/, "", r); if (r == ENVIRON["ROW"]) { v = $(col[ENVIRON["COL"]]); gsub(/^ +| +$/, "", v); print v; found = 1 } }
		END { if (!found || !(ENVIRON["COL"] in col)) exit 1 }' <<< "$1"
}

## $1 notes, $2 what, $3 row, $4 column, $5 wanted cell.
fWantCell(){
	local got
	if ! got="$(fCell "$1" "$3" "$4")"; then fFail "${2}: no ${3} / ${4} cell in: ${1}"; return 0; fi
	if [[ "$got" == "$5" ]]; then fEcho "OK: ${2}: ${3} / ${4}"
	else fFail "${2}: ${3} / ${4} is '${got}', wanted '${5}'"
	fi
}

## $1 notes, $2 what, $3 text that must be there, or !text that must not.
fWant(){
	if [[ "$3" == '!'* ]]; then
		if [[ "$1" == *"${3#!}"* ]]; then fFail "${2}: has '${3#!}'"; else fEcho "OK: ${2}"; fi
	elif [[ "$1" == *"$3"* ]]; then fEcho "OK: ${2}"
	else fFail "${2}: no '${3}' in: ${1}"
	fi
}

fNotes(){ bash "${here}/release-notes.bash" --changelog "$changelog" "$@"; }
fLink(){ printf '[%s](%s/%s)' "$1" "$base" "$2"; }

## Every name a release has.
linuxFiles=("${app}-linux-x86_64.deb" "${app}-linux-x86_64.rpm" "${app}-linux-x86_64.tar.gz")
winZip="${app}-windows-x86_64.zip"
winExe="${app}-windows-x86_64-portable.exe"
sums="${app}-sha256sums.txt"

fAssets "${scratch}/full.json" "${linuxFiles[@]}" "$sums" "$winZip" "$winExe"
notes="$(fNotes "$ver" "${scratch}/full.json")"
fWantCell "$notes" "full set" Linux x86_64 "$(fLink tar.gz "${app}-linux-x86_64.tar.gz"), $(fLink deb "${app}-linux-x86_64.deb"), $(fLink rpm "${app}-linux-x86_64.rpm")"
fWantCell "$notes" "full set" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
fWant "$notes" "checksums under the table" "Checksums: $(fLink "$sums" "$sums")"
[[ "$notes" == *"| Linux"*"| Windows"* ]] || fFail "full set: Windows row comes before Linux"
## Changelog, then the table, then the build number, last.
order="$(grep -n -E '^(- Something new\.|### Downloads|---|Build [0-9a-z]+)$' <<< "$notes" | cut -d: -f2 | tr '\n' '|')"
expectBuild="$(python3 "${root}/source/build-number.py")"
if [[ "$order" == "- Something new.|### Downloads|---|Build ${expectBuild}|" ]]; then fEcho "OK: notes in order, build ${expectBuild} last"
else fFail "notes order: ${order}"
fi
fWant "$notes" "older section left out" '!- Old.'

if command -v pandoc >/dev/null 2>&1; then
	html="$(pandoc -f gfm -t html <<< "$notes")"
	tds="$(grep -o '<td' <<< "$html" | wc -l)"
	if [[ "$(grep -c '<table>' <<< "$html")" == 1 && "$tds" == 4 && "$html" != *"| Linux"* ]]; then fEcho "OK: renders as a 2 by 2 table"
	else fFail "pandoc does not read one 2 by 2 table: ${html}"
	fi
else
	fEcho "render check skipped: no pandoc"
fi

## A CPU built for one OS only leaves the other's cell empty.
fAssets "${scratch}/arm.json" "${linuxFiles[@]}" "${app}-linux-arm64.tar.gz" "$sums" "$winZip"
notes="$(fNotes "$ver" "${scratch}/arm.json")"
fWantCell "$notes" "arm64 for Linux only" Linux arm64 "$(fLink tar.gz "${app}-linux-arm64.tar.gz")"
fWantCell "$notes" "arm64 for Linux only" Windows arm64 ""
fWantCell "$notes" "arm64 for Linux only" Windows x86_64 "$(fLink zip "$winZip")"
[[ "$(grep -m1 '^| ' <<< "$notes")" == *x86_64*arm64* ]] || fFail "arm64 column comes before x86_64"
if command -v pandoc >/dev/null 2>&1; then
	html="$(pandoc -f gfm -t html <<< "$notes")"
	if [[ "$(grep -o '<td' <<< "$html" | wc -l)" == 6 && "$(grep -c -E '^<td[^>]*></td>$' <<< "$html" || true)" == 1 ]]; then fEcho "OK: renders with the empty cell"
	else fFail "pandoc does not read a 2 by 3 table with an empty cell: ${html}"
	fi
fi

## The empty cell first in its row, where a shifted row would put arm64 under x86_64.
fAssets "${scratch}/mid.json" "${app}-linux-x86_64.tar.gz" "${app}-windows-arm64.zip"
notes="$(fNotes "$ver" "${scratch}/mid.json")"
fWantCell "$notes" "Windows on arm64 only" Windows x86_64 ""
fWantCell "$notes" "Windows on arm64 only" Windows arm64 "$(fLink zip "${app}-windows-arm64.zip")"
fWantCell "$notes" "Windows on arm64 only" Linux arm64 ""

## GitHub turns a "~" into ".", so the link has to be the name it kept.
renamed="nemo-anywhere-1.0.0.alpha.1-linux-x86_64.tar.gz"
fAssets "${scratch}/renamed.json" "$renamed"
notes="$(fNotes "$ver" "${scratch}/renamed.json")"
fWantCell "$notes" "name GitHub renamed" Linux x86_64 "$(fLink tar.gz "$renamed")"

## A checksum beside the exe, the old unversioned exe, and a file still
## uploading.
fAssets "${scratch}/odd.json" "$winExe" "${winExe}.sha256" "nemo-anywhere.exe" "${winZip}@starter"
notes="$(fNotes "$ver" "${scratch}/odd.json")"
fWantCell "$notes" "odd files" Windows x86_64 "$(fLink "portable exe" "$winExe")"
fWant "$notes" "exe checksum under the table" "Checksums: $(fLink "${winExe}.sha256" "${winExe}.sha256")"
fWant "$notes" "unversioned exe under the table" "Other files: $(fLink nemo-anywhere.exe nemo-anywhere.exe)"
fWant "$notes" "file still uploading left out" "!${winZip}"

## No files yet: no table, the rest as before.
fAssets "${scratch}/none.json"
notes="$(fNotes "$ver" "${scratch}/none.json")"
fWant "$notes" "no files, no table" '!### Downloads'
fWant "$notes" "no files, notes still there" "- Something new."
notes="$(fNotes "$ver")"
fWant "$notes" "no assets file, no table" '!### Downloads'
notes="$(fNotes 9.9.7 2>/dev/null)"
fWant "$notes" "no changelog section, placeholder" "See the changelog for details."

## A stand-in gh. It keeps the release's files in assets.json, their bytes in
## file_<name>, its notes in body.md (and as made in body-at-create.md), and
## its title and prerelease flag in title and prerelease. No assets.json means
## no release yet. after-view-N.json stands for a file showing up just after
## the Nth read. The hosted build is run_id, which run list hides for
## run_hidden looks, run_states, one state per look with the last one kept,
## and artifact/, what it handed over.
fakeDir="${scratch}/gh"
mkdir -p "${scratch}/bin" "$fakeDir"
cat > "${scratch}/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
d="${FAKE_GH}"
printf '%s\n' "$*" >> "${d}/log"
## $1 add or drop, then files to add or names to drop.
fFiles(){
	local how="$1" f n names=()
	shift
	for f in "$@"; do
		n="${f##*/}"; n="${n//[^A-Za-z0-9._-]/.}"; names+=("$n")
		if [[ "$how" == add ]]; then cp "$f" "${d}/file_${n}"; else rm -f "${d}/file_${n}"; fi
	done
	python3 - "${d}/assets.json" "${FAKE_BASE}" "$how" "${names[@]}" <<'PY'
import json, os, sys
out, base, how, names = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
have = json.load(open(out))["assets"] if os.path.exists(out) else []
have = [a for a in have if a["name"] not in names]
if how == "add":
    have += [{"name": n, "state": "uploaded", "url": base + "/" + n} for n in names]
text = json.dumps({"assets": have})
open(out, "w").write(text)
PY
}
fFlags(){
	while (($#)); do case "$1" in
		--notes-file) cp "$2" "${d}/body.md"; shift 2 ;;
		--title) printf '%s\n' "$2" > "${d}/title"; shift 2 ;;
		--prerelease|--prerelease=true) echo true > "${d}/prerelease"; shift ;;
		--prerelease=false) echo false > "${d}/prerelease"; shift ;;
		*) shift ;;
	esac; done
}
case "${1:-} ${2:-}" in
	"release view")
		if [[ ! -f "${d}/assets.json" ]]; then echo "release not found" >&2; exit 1; fi
		if [[ " $* " == *" isDraft "* ]]; then cat "${d}/draft" 2>/dev/null || echo false; exit 0; fi
		n=$(( $(cat "${d}/views" 2>/dev/null || echo 0) + 1 ))
		echo "$n" > "${d}/views"
		cat "${d}/assets.json"
		if [[ -f "${d}/after-view-${n}.json" ]]; then cp "${d}/after-view-${n}.json" "${d}/assets.json"; fi ;;
	"release edit")
		[[ -f "${d}/assets.json" ]] || { echo "release not found" >&2; exit 1; }
		fFlags "$@" ;;
	"release create")
		if [[ -f "${d}/assets.json" ]]; then echo "HTTP 422: Validation Failed (already_exists)" >&2; exit 1; fi
		shift 3
		files=() flags=()
		while (($#)); do case "$1" in
			--notes-file|--title) flags+=("$1" "$2"); shift 2 ;;
			--*) flags+=("$1"); shift ;;
			*) files+=("$1"); shift ;;
		esac; done
		echo false > "${d}/prerelease"
		fFlags "${flags[@]}"
		cp "${d}/body.md" "${d}/body-at-create.md"
		fFiles add "${files[@]}" ;;
	"run list")
		[[ -f "${d}/run_id" ]] || exit 0
		hidden="$(cat "${d}/run_hidden" 2>/dev/null || echo 0)"
		if ((hidden > 0)); then echo $((hidden - 1)) > "${d}/run_hidden"; exit 0; fi
		cat "${d}/run_id" ;;
	"run view")
		[[ "${3:-}" == "$(cat "${d}/run_id")" ]] || { echo "no such run: ${3:-}" >&2; exit 1; }
		head -1 "${d}/run_states"
		if [[ "$(wc -l < "${d}/run_states")" -gt 1 ]]; then sed -i 1d "${d}/run_states"; fi ;;
	"run download")
		dir="."
		while (($#)); do case "$1" in -D|--dir) dir="$2"; shift 2 ;; *) shift ;; esac; done
		mkdir -p "$dir"
		cp "${d}/artifact/"* "$dir/" ;;
	"repo view")
		echo "${FAKE_BASE%/releases/download/*}" ;;
	*) echo "stand-in gh: not handled: $*" >&2; exit 1 ;;
esac
EOF
chmod +x "${scratch}/bin/gh"
export FAKE_GH="$fakeDir" FAKE_BASE="$base" PATH="${scratch}/bin:${PATH}"

fUpdate(){ bash "${here}/release-notes.bash" --changelog "$changelog" --update "$tag" > "${scratch}/update.out" 2>&1; }
fReset(){ rm -rf "${fakeDir:?}"/*; }

## A file uploaded between the read and the edit gets a second edit.
fReset
fAssets "${fakeDir}/assets.json" "${linuxFiles[@]}" "$sums" "$winZip"
fAssets "${fakeDir}/after-view-1.json" "${linuxFiles[@]}" "$sums" "$winZip" "$winExe"
fUpdate || fFail "upload during the edit: update failed: $(cat "${scratch}/update.out")"
fWantCell "$(cat "${fakeDir}/body.md")" "upload during the edit" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
edits="$(grep -c '^release edit' "${fakeDir}/log" || true)"
if [[ "$edits" == 2 ]]; then fEcho "OK: upload during the edit: written again"; else fFail "upload during the edit: ${edits} edit(s), wanted 2"; fi

## Files that never settle give up rather than loop.
fReset
fAssets "${fakeDir}/assets.json" "$winZip"
for n in 1 2 3 4 5 6 7 8 9 10 11; do
	if ((n % 2)); then fAssets "${fakeDir}/after-view-${n}.json" "$winZip" "$winExe"; else fAssets "${fakeDir}/after-view-${n}.json" "$winZip"; fi
done
if fUpdate; then fFail "files never settle: update said OK"
elif grep -q 'kept changing' "${scratch}/update.out"; then fEcho "OK: files never settle: gave up"
else fFail "files never settle: failed for another reason: $(cat "${scratch}/update.out")"
fi

## The real local cut: release.bash --publish in a scratch repo.
repo="${scratch}/repo"
mkdir -p "${repo}/cicd/utility/include" "${repo}/source"
cp "${root}/cicd/config.bash" "${repo}/cicd/"
cp "${root}/cicd/utility/release.bash" "${root}/cicd/utility/release-stamps.py" "${root}/cicd/utility/changelog-notes.bash" \
	"${root}/cicd/utility/release-notes.bash" "${root}/cicd/utility/release-table.py" "${repo}/cicd/utility/"
cp "${root}/cicd/utility/include/source-date.bash" "${repo}/cicd/utility/include/"
cp "${root}/source/build-number.py" "${repo}/source/"
cp "$changelog" "${repo}/changelog.md"
printf "project('nemo-anywhere', 'c', version : '%s')\n" "$ver" > "${repo}/source/meson.build"
printf 'stand-in\n' > "${repo}/README.md"
printf 'cicd/artifacts/\n' > "${repo}/.gitignore"
fGit(){ git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false -c core.hooksPath=/dev/null "$@"; }
git init -q --bare "${scratch}/origin.git"
fGit init -q -b main
fGit remote add origin "${scratch}/origin.git"
fGit add -A
GIT_AUTHOR_DATE="@${SOURCE_DATE_EPOCH}" GIT_COMMITTER_DATE="@${SOURCE_DATE_EPOCH}" fGit commit -q -m base

## Stand-in artifacts with HEAD's stamp, which is all release.bash checks.
art="${repo}/cicd/artifacts/release"
mkdir -p "$art" "${scratch}/pfx/${app}-linux-x86_64/bin"
printf 'x\n' > "${scratch}/pfx/${app}-linux-x86_64/bin/nemo-anywhere"
tar -czf "${art}/${app}-linux-x86_64.tar.gz" -C "${scratch}/pfx" \
	--owner=0 --group=0 --numeric-owner --sort=name --mtime="@${SOURCE_DATE_EPOCH}" "${app}-linux-x86_64"
python3 - "${art}/${winZip}" "$SOURCE_DATE_EPOCH" <<'EOF'
import struct, sys, zipfile
pe = bytearray(0x80)
pe[0:2] = b"MZ"
struct.pack_into("<I", pe, 0x3c, 0x40)
pe[0x40:0x44] = b"PE\0\0"
struct.pack_into("<I", pe, 0x48, int(sys.argv[2]))
with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("nemo-anywhere-9.9.9-beta1-windows-x86_64/nemo-anywhere.exe", bytes(pe))
EOF
( cd "$art" && sha256sum "${app}"-* > "$sums" )

export RELEASE_POLL_SECS=0 RELEASE_RUN_LOOKS=3
## $1 states the hosted build goes through, one per look. The run is not
## listed the first time it is looked for, as just after a tag push.
fHosted(){
	fReset
	mkdir -p "${fakeDir}/artifact"
	printf 'exe\n' > "${fakeDir}/artifact/${winExe}"
	echo 4242 > "${fakeDir}/run_id"
	echo 1 > "${fakeDir}/run_hidden"
	printf '%s\n' "$@" > "${fakeDir}/run_states"
}
## release.bash makes the tag, so a fresh cut starts without it.
fCut(){
	fGit tag -d "$tag" >/dev/null 2>&1 || true
	git -C "${scratch}/origin.git" tag -d "$tag" >/dev/null 2>&1 || true
	fCutAgain
}
fCutAgain(){ out="$(cd "$repo" && bash cicd/utility/release.bash --publish -y 2>&1)"; }
fNames(){ python3 -c 'import json, sys; print(" ".join(sorted(a["name"] for a in json.load(open(sys.argv[1]))["assets"])))' "${fakeDir}/assets.json"; }
exeLine="$(printf 'exe\n' | sha256sum | cut -d' ' -f1)  ${winExe}"
allNames="${app}-linux-x86_64.tar.gz ${sums} ${winExe} ${winZip}"
## $1 what. The release as one create made it, whole.
fWantRelease(){
	local got
	got="$(fNames)"
	if [[ "$got" == "$allNames" ]]; then fEcho "OK: ${1}: files"; else fFail "${1}: files are '${got}', wanted '${allNames}'"; fi
	[[ "$(grep -m1 -E '^release (create|upload|edit|delete)' "${fakeDir}/log" || true)" == "release create"* ]] \
		|| fFail "${1}: the release was touched before it was made"
	if grep -q -E '^release (upload|delete)' "${fakeDir}/log"; then fFail "${1}: files went up after the release was made"; else fEcho "OK: ${1}: one create"; fi
	got="$(cat "${fakeDir}/title" 2>/dev/null || true)"
	if [[ "$got" == "Nemo Anywhere ${ver}" ]]; then fEcho "OK: ${1}: title"; else fFail "${1}: title is '${got}'"; fi
	got="$(cat "${fakeDir}/prerelease" 2>/dev/null || true)"
	if [[ "$got" == true ]]; then fEcho "OK: ${1}: prerelease"; else fFail "${1}: prerelease is '${got}'"; fi
	body="$(cat "${fakeDir}/body-at-create.md" 2>/dev/null || true)"
	fWant "$body" "${1}, notes" "- Something new."
	fWant "$body" "${1}, checksums" "Checksums: $(fLink "$sums" "$sums")"
	fWantCell "$body" "${1}, table as made" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
	fWantCell "$body" "${1}, table as made" Linux x86_64 "$(fLink tar.gz "${app}-linux-x86_64.tar.gz")"
	fWant "$body" "${1}, build line" "Build ${expectBuild}"
	got="$(cat "${fakeDir}/file_${sums}" 2>/dev/null || true)"
	fWant "$got" "${1}, exe in the sums file" "$exeLine"
	fWant "$got" "${1}, Linux in the sums file" "  ${app}-linux-x86_64.tar.gz"
	fWant "$got" "${1}, zip in the sums file" "  ${winZip}"
}

## The hosted build is still running when the cut looks, then passes.
fHosted "in_progress " "in_progress " "completed success"
if fCut; then
	fWantRelease "hosted build waited for"
	waited="$(grep -c '^run view' "${fakeDir}/log" || true)"
	if [[ "$waited" == 3 ]]; then fEcho "OK: hosted build waited for: until it ended"; else fFail "hosted build waited for: ${waited} look(s), wanted 3"; fi
else
	fFail "release.bash --publish failed: ${out}"
fi

## A failed hosted build makes no release, and a rerun once it passes picks
## up from the pushed tag.
fHosted "completed failure"
if fCut; then fFail "hosted build failed: release.bash said OK"
else
	fWant "$out" "hosted build failed: says how to go on" "gh run rerun 4242"
	if [[ -f "${fakeDir}/assets.json" ]]; then fFail "hosted build failed: a release was made"; else fEcho "OK: hosted build failed: no release"; fi
	echo "completed success" > "${fakeDir}/run_states"
	if fCutAgain; then
		fWant "$out" "rerun after the hosted build passed: tag kept" "is there already on HEAD"
		fWantRelease "rerun after the hosted build passed"
	else
		fFail "rerun after the hosted build passed: ${out}"
	fi
fi

## A rerun on a different commit is still refused.
fGit commit -q --allow-empty -m later
if fCutAgain; then fFail "tag on another commit: release.bash said OK"
else fWant "$out" "tag on another commit: refused" "already exists"
fi
fGit reset -q --hard HEAD~1

## A release that is there already is never added to.
fHosted "completed success"
fAssets "${fakeDir}/assets.json" "$winExe"
if fCut; then fFail "release there already: release.bash said OK"
else
	fWant "$out" "release there already: refused" "published already"
	if grep -q -E '^release (create|upload|edit)' "${fakeDir}/log"; then fFail "release there already: it was changed"; else fEcho "OK: release there already: left alone"; fi
fi

## A hosted build that hands over something not named for this version.
fHosted "completed success"
printf 'x\n' > "${fakeDir}/artifact/nemo-anywhere.exe"
if fCut; then fFail "misnamed hosted file: release.bash said OK"
else
	fWant "$out" "misnamed hosted file: refused" "handed over 'nemo-anywhere.exe'"
	if [[ -f "${fakeDir}/assets.json" ]]; then fFail "misnamed hosted file: a release was made"; else fEcho "OK: misnamed hosted file: no release"; fi
fi

## A hosted build hands over files and never touches the release.
wfFile="${root}/.github/workflows/release-win.yml"
if grep -q -E 'gh release|contents: write' "$wfFile"; then fFail "release-win.yml still writes to the release"
else fEcho "OK: workflow leaves the release alone"
fi
upload="$(grep -A3 -F 'uses: actions/upload-artifact' "$wfFile" || true)"
if grep -q -F 'name: release-files' <<< "$upload"; then fEcho "OK: workflow hands over release-files"
else fFail "release-win.yml uploads no release-files artifact"
fi
if [[ " $(sed -n 's/^RELEASE_WORKFLOWS=(\(.*\))$/\1/p' "${root}/cicd/config.bash") " == *" release-win.yml "* ]]; then fEcho "OK: the cut waits for release-win.yml"
else fFail "release-win.yml is not in RELEASE_WORKFLOWS"
fi

if ((failures)); then fEcho "${failures} release notes check(s) failed"; exit 1; fi
fEcho "release notes checks passed"
