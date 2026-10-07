#!/usr/bin/env bash

##	- Purpose: Check the one-line installers' download path, which --from skips:
##	  which release each channel picks, the checksum check, a release with no
##	  sums file, and the question before anything is installed. The GitHub API and the downloads are
##	  answered from a made-up release list, so nothing touches the network.
##	  install.bash gets a curl on PATH that serves those files, and install.ps1
##	  gets Invoke-RestMethod and Invoke-WebRequest functions, which PowerShell
##	  finds before the cmdlets, that go through the same curl.
##	- The made-up builds are named the way the release lanes name them, and a
##	  uname on PATH has the installers ask for the other arch once as well, so
##	  both arches are checked on either box. The same uname has them ask for
##	  the FreeBSD build once, under the name the FreeBSD lane gives it.
##	- Every install goes to a scratch home and a user prefix; neither installer
##	  reaches for sudo without --target system, which is never passed here.
##	- Linux only: install.bash does not run on Windows, and install.ps1 there
##	  would write the real PATH. A missing pwsh or python3 skips that part
##	  with a note; exit 77 when this is not Linux.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-install-download.bash
##	- Test ID: rhtrxr81

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly exeName="nemo-anywhere"
readonly dl="https://github.com/yottacore/${exeName}/releases/download"

fEcho(){ echo "[ $* ]"; }

if [[ "$(uname -s)" != "Linux" ]]; then
	fEcho "installer download check skipped: Linux only"
	exit 77
fi
# shellcheck source=../utility/include/release-files.bash
source "${root}/cicd/utility/include/release-files.bash"
arch="$(fReleaseArch "$(uname -m)")" || { fEcho "installer download check skipped: no build for $(uname -m)"; exit 77; }
os="$(fReleaseOs "$(uname -s)")"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/${exeName}-download-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

## Answers a URL from $CURL_SHIM_ROOT the way GitHub would, 404 for anything
## else, and logs every request. Takes the two shapes the installers use.
shimDir="${scratch}/shim"
mkdir -p "$shimDir"
cat > "${shimDir}/curl" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
url=""; out=""
while (($#)); do case "$1" in
	-o) out="$2"; shift 2 ;;
	-*) shift ;;
	*)  url="$1"; shift ;;
esac; done
printf '%s\n' "$url" >> "${CURL_SHIM_ROOT}/requests.log"
api="https://api.github.com/repos/yottacore/nemo-anywhere"
dl="https://github.com/yottacore/nemo-anywhere/releases/download"
case "$url" in
	"${api}/releases?per_page=100") file="${CURL_SHIM_ROOT}/releases.json" ;;
	"${api}/releases/tags/"*)       file="${CURL_SHIM_ROOT}/tag-${url##*/}.json" ;;
	"${dl}/"*)                      file="${CURL_SHIM_ROOT}/assets/${url#"${dl}/"}" ;;
	*)                              file="" ;;
esac
if [[ -z "$file" || ! -f "$file" ]]; then
	echo "curl: (22) The requested URL returned error: 404" >&2
	exit 22
fi
if [[ -n "$out" ]]; then cp "$file" "$out"; else cat "$file"; fi
EOF
chmod +x "${shimDir}/curl"

## Answers -m with $UNAME_SHIM_M and -s with $UNAME_SHIM_S when set; the real
## uname does the rest.
cat > "${shimDir}/uname" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == "-m" && -n "${UNAME_SHIM_M:-}" ]]; then echo "$UNAME_SHIM_M"; exit 0; fi
if [[ "${1:-}" == "-s" && -n "${UNAME_SHIM_S:-}" ]]; then echo "$UNAME_SHIM_S"; exit 0; fi
for u in $(type -ap uname); do [[ "$u" -ef "$0" ]] || exec "$u" "$@"; done
exit 127
EOF
chmod +x "${shimDir}/uname"

## PowerShell looks a function up before a cmdlet of the same name, so these
## stand in for the two web cmdlets for the one run of the installer.
cat > "${shimDir}/run-install.ps1" <<'EOF'
param([string]$Installer, [string]$Curl, [string]$Release = "stable", [switch]$Yes, [switch]$NoVerify)
function Invoke-RestMethod {
	param([string]$Uri, [switch]$UseBasicParsing)
	$null = $UseBasicParsing
	$text = & $Curl -fsSL $Uri
	if ($LASTEXITCODE -ne 0) { throw "Response status code does not indicate success: 404 (Not Found)." }
	return ($text -join "`n") | ConvertFrom-Json
}
function Invoke-WebRequest {
	param([string]$Uri, [string]$OutFile, [switch]$UseBasicParsing)
	$null = $UseBasicParsing
	& $Curl -fsSL $Uri -o $OutFile
	if ($LASTEXITCODE -ne 0) { throw "Response status code does not indicate success: 404 (Not Found)." }
}
## $? rather than $LASTEXITCODE: a run that ends well leaves the exit code of
## whatever native tool it called last.
## Only passed when set, so a run without it works on an installer that
## predates the option.
$extra = @{}
if ($NoVerify) { $extra.NoVerify = $true }
& $Installer -Release $Release -Yes:$Yes @extra
if (-not $?) { exit 1 }
exit 0
EOF

## A release list with every tag given its own build, sums file and release
## page. $1 the fixture dir, the rest the tags, in the order the API lists them.
## With withSums=0 no release publishes a sums file.
withSums=1
fMakeFixture(){
	local dir="$1"; shift
	local tag ver name tree sep="" asset sums sumsEntry
	mkdir -p "${dir}/assets"
	: > "${dir}/requests.log"
	{
		printf '['
		for tag in "$@"; do
			printf '%s\n  {"tag_name": "%s", "prerelease": %s}' "$sep" "$tag" "$([[ "$tag" == *-* ]] && echo true || echo false)"
			sep=","
		done
		printf '\n]\n'
	} > "${dir}/releases.json"

	for tag in "$@"; do
		ver="${tag#v}"
		name="${exeName}-${ver}-${os}-${arch}"
		asset="${name}.tar.gz"
		sums="${exeName}-${ver}-sha256sums.txt"
		tree="${dir}/build/${name}"
		mkdir -p "${tree}/bin" "${dir}/assets/${tag}"
		printf '#!/bin/sh\necho %s\n' "$ver" > "${tree}/bin/${exeName}"
		chmod +x "${tree}/bin/${exeName}"
		tar -czf "${dir}/assets/${tag}/${asset}" -C "${dir}/build" "$name"
		sumsEntry=""
		if ((withSums)); then
			( cd "${dir}/assets/${tag}" && sha256sum "$asset" > "$sums" )
			sumsEntry=",
			  {\"name\": \"${sums}\", \"browser_download_url\": \"${dl}/${tag}/${sums}\"}"
		fi
		## The Windows zip is listed too, so an installer has to pick by name.
		cat > "${dir}/tag-${tag}.json" <<-EOF
			{"tag_name": "${tag}", "assets": [
			  {"name": "${exeName}-${ver}-windows-x86_64.zip", "browser_download_url": "${dl}/${tag}/${exeName}-${ver}-windows-x86_64.zip"},
			  {"name": "${asset}", "browser_download_url": "${dl}/${tag}/${asset}"}${sumsEntry}
			]}
		EOF
	done
	return 0
}

## Runs one installer. $1 installer (bash|pwsh), $2 label, $3 fixture, $4
## channel, $5 answer to the question ("" = pass the yes option instead), $6
## "unverified" to pass the option that installs a release with no sums file.
## Leaves the exit code in $runRc and the transcript in ${home}/run.log.
home=""; runRc=0; unameM=""; unameS=""
fRun(){
	local installer="$1" label="$2" fixture="$3" channel="$4" answer="$5" unverified="${6:-}"
	local -a cmd
	home="${scratch}/home-${installer}-${label}"
	mkdir -p "${home}/tmp"
	: > "${fixture}/requests.log"
	if [[ "$installer" == "bash" ]]; then
		cmd=(bash "${root}/install.bash" --release "$channel")
		[[ -n "$answer" ]] || cmd+=(--yes)
		[[ -z "$unverified" ]] || cmd+=(--no-verify)
	else
		cmd=(pwsh -NoProfile -File "${shimDir}/run-install.ps1" -Installer "${root}/install.ps1" -Curl "${shimDir}/curl" -Release "$channel")
		[[ -n "$answer" ]] || cmd+=(-Yes)
		[[ -z "$unverified" ]] || cmd+=(-NoVerify)
	fi
	## A proxy that answers nothing, so a request that slips past the stand-ins
	## fails here instead of reaching GitHub.
	local runEnv=(env -u DISPLAY -u no_proxy -u NO_PROXY HOME="$home" XDG_DATA_HOME="${home}/.local/share"
		TMPDIR="${home}/tmp" PATH="${shimDir}:${PATH}" CURL_SHIM_ROOT="$fixture" UNAME_SHIM_M="$unameM" UNAME_SHIM_S="$unameS"
		https_proxy="http://127.0.0.1:9" HTTPS_PROXY="http://127.0.0.1:9")
	runRc=0
	if [[ -z "$answer" ]]; then
		"${runEnv[@]}" "${cmd[@]}" </dev/null >"${home}/run.log" 2>&1 || runRc=$?
	else
		## The installers only ask on a terminal, so give them one.
		"${runEnv[@]}" python3 "${root}/cicd/linux/answer-prompt.py" "Proceed?" "$answer" "${cmd[@]}" \
			</dev/null >"${home}/run.log" 2>&1 || runRc=$?
	fi
	return 0
}

## $1 what, then the installed version to expect, or "" for nothing installed,
## then "unverified" when there was no sums file to check against.
fExpectInstall(){
	local what="$1" ver="$2" unverified="${3:-}"
	local prefix="${home}/.local/share/${exeName}"
	local launcher="${home}/.local/share/applications/${exeName}.desktop"
	local symlink="${home}/.local/bin/${exeName}"
	if [[ -z "$ver" ]]; then
		[[ ! -e "$prefix" && ! -e "$launcher" && ! -L "$symlink" ]] || fFail "${what}: installed something anyway"
		[[ -z "$(find "${home}/.local/share" -maxdepth 1 -name ".${exeName}-install.*" 2>/dev/null || true)" ]] \
			|| fFail "${what}: left a staging folder behind"
		return 0
	fi
	if [[ "$runRc" != "0" ]]; then
		fFail "${what}: exited ${runRc}; last lines:"
		tail -5 "${home}/run.log"
		return 0
	fi
	grep -q -F "echo ${ver}" "${prefix}/bin/${exeName}" 2>/dev/null || fFail "${what}: ${ver} is not what got installed"
	[[ -f "$launcher" ]] || fFail "${what}: no launcher"
	[[ "$(readlink "$symlink" || true)" == "${prefix}/bin/${exeName}" ]] || fFail "${what}: symlink does not point into the prefix"
	if [[ -n "$unverified" ]]; then
		grep -q "UNVERIFIED" "${home}/run.log" || fFail "${what}: the plan never said the install is unverified"
	else
		grep -q "sha256 verified" "${home}/run.log" || fFail "${what}: installed without saying the checksum was verified"
	fi
	return 0
}

## $1 what, $2 text the transcript must hold.
fExpectSaid(){
	grep -q -F -- "$2" "${home}/run.log" || { fFail "${1}: never said '${2}'; last lines:"; tail -5 "${home}/run.log"; }
	return 0
}

## $1 what, $2 fixture, $3 tag whose build and sums had to be fetched.
fExpectFetched(){
	local log="${2}/requests.log"
	grep -q -F "/${3}/${exeName}-${3#v}-${os}-${arch}.tar.gz" "$log" || fFail "${1}: never downloaded the ${3} build"
	grep -q -F "/${3}/${exeName}-${3#v}-sha256sums.txt" "$log" || fFail "${1}: never downloaded the ${3} sums"
	! grep -q -F "releases/latest" "$log" || fFail "${1}: asked for releases/latest, which fails with no stable release"
	return 0
}

## 1.0.0-alpha.2 is listed on purpose: sort -V ranks it above 1.0.0.
mixed="${scratch}/mixed"
fMakeFixture "$mixed" v1.0.0-beta2 v1.0.0 v0.9.0 v1.1.0-rc.1 v1.0.0-alpha.2
preOnly="${scratch}/prerelease-only"
fMakeFixture "$preOnly" v1.0.0-beta2 v1.0.0-beta10 v1.0.0-alpha.2
tampered="${scratch}/tampered"
fMakeFixture "$tampered" v1.0.0
## Still a good archive, so only the checksum stands between it and the prefix.
printf 'echo tampered\n' >> "${tampered}/build/${exeName}-1.0.0-linux-${arch}/bin/${exeName}"
tar -czf "${tampered}/assets/v1.0.0/${exeName}-1.0.0-linux-${arch}.tar.gz" -C "${tampered}/build" "${exeName}-1.0.0-linux-${arch}"
noLine="${scratch}/no-sums-line"
fMakeFixture "$noLine" v1.0.0
printf '%064d  %s\n' 0 "${exeName}-1.0.0-windows-x86_64.zip" > "${noLine}/assets/v1.0.0/${exeName}-1.0.0-sha256sums.txt"
noSums="${scratch}/no-sums"
withSums=0
fMakeFixture "$noSums" v1.0.0 v1.1.0-rc.1
withSums=1

installers=(bash)
if command -v pwsh >/dev/null 2>&1; then
	installers+=(pwsh)
else
	fEcho "pwsh not installed; install.ps1 not checked"
fi
havePython=1
command -v python3 >/dev/null 2>&1 || { havePython=0; fEcho "python3 not installed; the question is not checked"; }

for inst in "${installers[@]}"; do
	fRun "$inst" stable "$mixed" stable ""
	fExpectInstall "${inst} stable" 1.0.0
	fExpectFetched "${inst} stable" "$mixed" v1.0.0

	fRun "$inst" dev "$mixed" dev ""
	fExpectInstall "${inst} dev" 1.1.0-rc.1
	fExpectFetched "${inst} dev" "$mixed" v1.1.0-rc.1

	fRun "$inst" prerelease-only "$preOnly" stable ""
	fExpectInstall "${inst} stable with only prereleases" 1.0.0-beta10
	fExpectSaid "${inst} stable with only prereleases" "no stable release yet"
	fExpectFetched "${inst} stable with only prereleases" "$preOnly" v1.0.0-beta10

	fRun "$inst" tampered "$tampered" stable ""
	[[ "$runRc" != "0" ]] || fFail "${inst} tampered build: exited 0"
	fExpectSaid "${inst} tampered build" "checksum mismatch"
	fExpectInstall "${inst} tampered build" ""

	fRun "$inst" no-sums-line "$noLine" stable ""
	[[ "$runRc" != "0" ]] || fFail "${inst} sums without the build: exited 0"
	fExpectSaid "${inst} sums without the build" "has no line for ${exeName}-1.0.0-linux-${arch}.tar.gz"
	fExpectInstall "${inst} sums without the build" ""

	if ((havePython)); then
		fRun "$inst" answered-no "$mixed" stable n
		[[ "$runRc" != "0" ]] || fFail "${inst} answered no: exited 0"
		fExpectSaid "${inst} answered no" "Nothing changed."
		fExpectInstall "${inst} answered no" ""
		## Nothing is fetched until the plan is accepted.
		! grep -q -F ".tar.gz" "${mixed}/requests.log" || fFail "${inst} answered no: downloaded the build first"

		fRun "$inst" answered-yes "$mixed" stable y
		fExpectInstall "${inst} answered yes" 1.0.0
	fi

	## No sums file: refused before anything is fetched, on either channel, and
	## the yes option alone does not get past it.
	for channel in stable dev; do
		fRun "$inst" "no-sums-${channel}" "$noSums" "$channel" ""
		[[ "$runRc" != "0" ]] || fFail "${inst} ${channel} with no sums file: exited 0"
		opt="--no-verify"
		[[ "$inst" == "bash" ]] || opt="-NoVerify"
		fExpectSaid "${inst} ${channel} with no sums file" "$opt"
		fExpectInstall "${inst} ${channel} with no sums file" ""
		! grep -q -F ".tar.gz" "${noSums}/requests.log" || fFail "${inst} ${channel} with no sums file: downloaded the build first"
	done

	fRun "$inst" no-sums-allowed "$noSums" dev "" unverified
	fExpectInstall "${inst} no sums file, allowed" 1.1.0-rc.1 unverified

	## The option is for a missing sums file only. One that is there still counts.
	fRun "$inst" allowed-with-sums "$mixed" stable "" unverified
	fExpectInstall "${inst} allowed, with a sums file" 1.0.0
	fRun "$inst" allowed-tampered "$tampered" stable "" unverified
	[[ "$runRc" != "0" ]] || fFail "${inst} allowed, tampered build: exited 0"
	fExpectSaid "${inst} allowed, tampered build" "checksum mismatch"
	fExpectInstall "${inst} allowed, tampered build" ""
done

## The other arch, under the name its release lane gives it.
hostArch="$arch"
case "$hostArch" in
	x86_64) unameM="aarch64" ;;
	*)      unameM="x86_64" ;;
esac
arch="$(fReleaseArch "$unameM")"
otherArch="${scratch}/other-arch"
fMakeFixture "$otherArch" v1.0.0
for inst in "${installers[@]}"; do
	fRun "$inst" other-arch "$otherArch" stable ""
	fExpectInstall "${inst} on ${unameM}" 1.0.0
	fExpectFetched "${inst} on ${unameM}" "$otherArch" v1.0.0
done
unameM=""; arch="$hostArch"

## FreeBSD on amd64, which uname calls it there.
unameS="FreeBSD"; unameM="amd64"
os="$(fReleaseOs "$unameS")"; arch="$(fReleaseArch "$unameM")"
freebsd="${scratch}/freebsd"
fMakeFixture "$freebsd" v1.0.0
for inst in "${installers[@]}"; do
	fRun "$inst" freebsd "$freebsd" stable ""
	fExpectInstall "${inst} on FreeBSD" 1.0.0
	fExpectFetched "${inst} on FreeBSD" "$freebsd" v1.0.0
done
unameS=""; unameM=""; arch="$hostArch"; os="$(fReleaseOs "$(uname -s)")"

if ((failures)); then
	fEcho "FAILED: installer download check, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: installer download check (${installers[*]})"
