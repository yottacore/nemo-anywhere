#!/usr/bin/env bash

##	- Purpose: Check the Downloads table in release notes: OS in rows, CPU in
##	  columns, an empty cell where nothing was built, links as GitHub has the
##	  files named, and checksums kept out of the table. Also that it comes out
##	  whole whichever release lane adds its files last.
##	- Runs release-notes.bash over made-up asset lists, then a copy of
##	  release.bash --publish in a scratch repo against a stand-in gh, followed by
##	  the Windows workflow's last step, and again with the release already
##	  made by the Windows build. Nothing reaches GitHub.
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

## Every name the two lanes upload for a release.
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
## file_<name>, its notes in body.md, and its title and prerelease flag in
## title and prerelease. No assets.json means no release yet.
## after-view-N.json stands for another lane's upload finishing just after the
## Nth read, and made-meanwhile.json for the Windows build making the release
## just before this lane's create.
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
		n=$(( $(cat "${d}/views" 2>/dev/null || echo 0) + 1 ))
		echo "$n" > "${d}/views"
		cat "${d}/assets.json"
		if [[ -f "${d}/after-view-${n}.json" ]]; then cp "${d}/after-view-${n}.json" "${d}/assets.json"; fi ;;
	"release edit")
		[[ -f "${d}/assets.json" ]] || { echo "release not found" >&2; exit 1; }
		fFlags "$@" ;;
	"release create")
		if [[ -f "${d}/made-meanwhile.json" ]]; then mv "${d}/made-meanwhile.json" "${d}/assets.json"; fi
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
		fFiles add "${files[@]}" ;;
	"release upload")
		[[ -f "${d}/assets.json" ]] || { echo "release not found" >&2; exit 1; }
		shift 3
		files=()
		for f in "$@"; do [[ "$f" == --* ]] || files+=("$f"); done
		for f in "${files[@]}"; do
			n="${f##*/}"
			if [[ -f "${d}/file_${n}" && " $* " != *" --clobber "* ]]; then echo "asset under the same name already exists: ${n}" >&2; exit 1; fi
		done
		fFiles add "${files[@]}" ;;
	"release download")
		shift 3
		pattern="" dir="."
		while (($#)); do case "$1" in
			-p|--pattern) pattern="$2"; shift 2 ;;
			-D|--dir) dir="$2"; shift 2 ;;
			*) shift ;;
		esac; done
		[[ -f "${d}/file_${pattern}" ]] || { echo "no assets to download" >&2; exit 1; }
		cp "${d}/file_${pattern}" "${dir}/${pattern}" ;;
	"release delete-asset")
		[[ -f "${d}/file_${4:-}" ]] || { echo "asset not found: ${4:-}" >&2; exit 1; }
		fFiles drop "$4" ;;
	*) echo "stand-in gh: not handled: $*" >&2; exit 1 ;;
esac
EOF
chmod +x "${scratch}/bin/gh"
export FAKE_GH="$fakeDir" FAKE_BASE="$base" PATH="${scratch}/bin:${PATH}"

fUpdate(){ bash "${here}/release-notes.bash" --changelog "$changelog" --update "$tag" > "${scratch}/update.out" 2>&1; }
fReset(){ rm -f "${fakeDir}"/*; }

## The Windows build first, the local cut after.
fReset
fAssets "${fakeDir}/assets.json" "$winExe"
fUpdate || fFail "Windows lane first: update failed: $(cat "${scratch}/update.out")"
fAssets "${fakeDir}/assets.json" "$winExe" "${linuxFiles[@]}" "$sums" "$winZip"
fUpdate || fFail "local lane second: update failed: $(cat "${scratch}/update.out")"
body="$(cat "${fakeDir}/body.md")"
fWantCell "$body" "Windows lane first" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
fWantCell "$body" "Windows lane first" Linux x86_64 "$(fLink tar.gz "${app}-linux-x86_64.tar.gz"), $(fLink deb "${app}-linux-x86_64.deb"), $(fLink rpm "${app}-linux-x86_64.rpm")"

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

## The real local cut: release.bash --publish in a scratch repo, then the
## workflow's last step once its exe and sums are up.
fReset
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

## release.bash refuses a tag that is there already, so each run starts without it.
fCut(){
	fGit tag -d "$tag" >/dev/null 2>&1 || true
	git -C "${scratch}/origin.git" tag -d "$tag" >/dev/null 2>&1 || true
	out="$(cd "$repo" && bash cicd/utility/release.bash --publish -y 2>&1)"
}
fNames(){ python3 -c 'import json, sys; print(" ".join(sorted(a["name"] for a in json.load(open(sys.argv[1]))["assets"])))' "${fakeDir}/assets.json"; }
## $1 what. The release as the local cut makes it, whoever made it first.
fWantLocalCut(){
	local got
	got="$(cat "${fakeDir}/title" 2>/dev/null || true)"
	if [[ "$got" == "Nemo Anywhere ${ver}" ]]; then fEcho "OK: ${1}: title"; else fFail "${1}: title is '${got}'"; fi
	got="$(cat "${fakeDir}/prerelease" 2>/dev/null || true)"
	if [[ "$got" == true ]]; then fEcho "OK: ${1}: prerelease"; else fFail "${1}: prerelease is '${got}'"; fi
	body="$(cat "${fakeDir}/body.md" 2>/dev/null || true)"
	fWant "$body" "${1}, notes" "- Something new."
	fWant "$body" "${1}, checksums" "Checksums: $(fLink "$sums" "$sums")"
	fWantCell "$body" "$1" Linux x86_64 "$(fLink tar.gz "${app}-linux-x86_64.tar.gz")"
}

fReset
if ! fCut; then
	fFail "release.bash --publish failed: ${out}"
elif [[ ! -f "${fakeDir}/body.md" ]]; then
	fFail "release.bash --publish wrote no notes: ${out}"
else
	fWantLocalCut "local cut"
	fWantCell "$body" "local cut" Windows x86_64 "$(fLink zip "$winZip")"
	## What the workflow uploads, then its last step, run as it runs it.
	fAssets "${fakeDir}/assets.json" "${app}-linux-x86_64.tar.gz" "$sums" "$winZip" "$winExe"
	if ! out="$(cd "$repo" && GITHUB_REF_NAME="$tag" bash cicd/utility/release-notes.bash --update "$tag" 2>&1)"; then
		fFail "workflow step failed: ${out}"
	else
		body="$(cat "${fakeDir}/body.md")"
		fWantCell "$body" "Windows lane last" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
		fWantCell "$body" "Windows lane last" Linux x86_64 "$(fLink tar.gz "${app}-linux-x86_64.tar.gz")"
		fWant "$body" "Windows lane last, build line" "Build ${expectBuild}"
	fi
fi

## The Windows build made the release, as it does when the tag gets there
## first: its own title, no prerelease flag here to see it put right, and the
## exe's checksum beside it, since it gave up waiting for the sums file.
exeLine="$(printf 'exe\n' | sha256sum | cut -d' ' -f1)  ${winExe}"
fWorkflowFirst(){
	printf 'exe\n' > "${fakeDir}/file_${winExe}"
	printf '%s\n' "$exeLine" > "${fakeDir}/file_${winExe}.sha256"
	fAssets "$1" "$winExe" "${winExe}.sha256"
	printf '%s\n' "$tag" > "${fakeDir}/title"
	echo false > "${fakeDir}/prerelease"
	printf 'Generated.\n' > "${fakeDir}/body.md"
}
fReset
fWorkflowFirst "${fakeDir}/assets.json"
if ! fCut; then
	fFail "Windows lane made the release: release.bash --publish failed: ${out}"
else
	fWantLocalCut "Windows lane made the release"
	fWantCell "$body" "Windows lane made the release" Windows x86_64 "$(fLink zip "$winZip"), $(fLink "portable exe" "$winExe")"
	got="$(fNames)"
	want="${app}-linux-x86_64.tar.gz ${sums} ${winExe} ${winZip}"
	if [[ "$got" == "$want" ]]; then fEcho "OK: Windows lane made the release: files, exe checksum folded in"
	else fFail "Windows lane made the release: files are '${got}', wanted '${want}'"
	fi
	sumsText="$(cat "${fakeDir}/file_${sums}" 2>/dev/null || true)"
	fWant "$sumsText" "Windows lane made the release, exe in the sums file" "$exeLine"
	fWant "$sumsText" "Windows lane made the release, Linux in the sums file" "  ${app}-linux-x86_64.tar.gz"
fi

## The release is made between this lane's look and its create.
fReset
fWorkflowFirst "${fakeDir}/made-meanwhile.json"
if ! fCut; then
	fFail "release made meanwhile: release.bash --publish failed: ${out}"
else
	got="$(fNames)"
	if [[ "$got" == *"$sums"* && "$got" == *"$winExe"* && "$got" == *"${app}-linux-x86_64.tar.gz"* ]]; then fEcho "OK: release made meanwhile: files added to it"
	else fFail "release made meanwhile: files are '${got}'"
	fi
	got="$(cat "${fakeDir}/title" 2>/dev/null || true)"
	if [[ "$got" == "Nemo Anywhere ${ver}" ]]; then fEcho "OK: release made meanwhile: title"; else fFail "release made meanwhile: title is '${got}'"; fi
fi
step="$(grep -A6 -F -- '- name: Downloads table' "${root}/.github/workflows/release-win.yml" | grep -F 'run:' || true)"
if [[ "$step" == *"bash cicd/utility/release-notes.bash --update \"\${GITHUB_REF_NAME}\""* ]]; then fEcho "OK: workflow's last step runs the same update"
else fFail "release-win.yml has no Downloads table step running release-notes.bash --update"
fi
lastStep="$(grep -E '^      - name: ' "${root}/.github/workflows/release-win.yml" | tail -1)"
[[ "$lastStep" == *"Downloads table"* ]] || fFail "the Downloads table step is not the workflow's last: ${lastStep}"

if ((failures)); then fEcho "${failures} release notes check(s) failed"; exit 1; fi
fEcho "release notes checks passed"
