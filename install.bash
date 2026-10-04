#!/usr/bin/env bash

##	- Purpose: One-liner installer for Nemo Anywhere. Fetches a release build,
##	  verifies its checksum, and installs it as a self-contained prefix plus a
##	  desktop launcher and a command-line symlink.
##	- Covers Linux, BSD and WSL, and macOS once there is a build for it.
##	- install.ps1 is the same installer in PowerShell, and covers every platform
##	  this one does plus Windows. Either script alone does the whole job; this
##	  one is the no-dependencies option, since it needs nothing but a shell.
##	- Idempotent: reinstalling replaces the prefix in place, and --uninstall
##	  removes exactly what was installed. Nothing is touched before the plan is
##	  printed and confirmed.
##	- Bash 3.2 compatible on purpose (that is what macOS still ships).
##	- Syntax:
##	  bash <(curl -fsSL https://raw.githubusercontent.com/yottacore/nemo-anywhere/main/install.bash) [options]
##	    --release dev|stable    which release to take (default: stable)
##	    --target  user|system   where to install (default: user)
##	    --from    PATH|URL      install this archive instead of a release
##	    --allow-unverified      install a release that has no checksums file
##	    --uninstall             remove an existing install
##	    -y, --yes               don't ask before making changes
##	    --version               print the installer's version
##	  Options take their value as the next word or after an equals sign.
##	  Installs to ~/.local/share/nemo-anywhere (user) or /opt/nemo-anywhere
##	  (system, via sudo). Run with --uninstall to reverse it.
##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

REPO="yottacore/nemo-anywhere"
INSTALLER_VERSION="1.3.0"
APP_NAME="Nemo Anywhere"
EXE_NAME="nemo-anywhere"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Output helpers

## Same shape as the project's cicd output: fEcho prints a bracketed status
## line, fEcho_Clean a plain one and collapses repeated blanks.
_wasLastEchoBlank=0
fEcho_Clean(){ if [[ -n "${1:-}" ]]; then echo "$*"; _wasLastEchoBlank=0; elif [[ $_wasLastEchoBlank -eq 0 ]] && echo; then _wasLastEchoBlank=1; fi; }
fEcho(){       if [[ -n "$*" ]]; then fEcho_Clean "[ $* ]"; else fEcho_Clean ""; fi; }
fWarn(){ fEcho_Clean "WARNING: $*" >&2; }
fDie(){  { fEcho_Clean ""; fEcho_Clean "FAILED: $*"; fEcho_Clean ""; } >&2; exit 1; }

fAbort(){ printf "\nINSTALL ABORTED (exit %s) at line %s: %s\n\n" "$1" "$2" "$3" >&2; exit "$1"; }
trap 'fAbort $? $LINENO "$BASH_COMMAND"' ERR

## Spelled out rather than read back out of this file: run the documented way,
## as `bash <(curl ...)`, the script is a pipe that has already been consumed,
## so anything that re-reads its own source prints nothing.
fHelp(){
	cat <<-EOF

		${APP_NAME} installer.

		  bash <(curl -fsSL https://raw.githubusercontent.com/${REPO}/main/install.bash) [options]

		    --release dev|stable    which release to take (default: stable)
		    --target  user|system   where to install (default: user)
		    --from    PATH|URL      install this archive instead of a release
		    --allow-unverified      install a release that has no checksums file
		    --uninstall             remove an existing install
		    -y, --yes               don't ask before making changes
		    --version               the installer's version
		    -h, --help              this text

		  The OS and architecture are detected. With no stable release yet,
		  stable takes the newest prerelease and says so in the plan.

		  A release download is checked against the release's checksums file.
		  With no checksums file it stops, even with -y, unless
		  --allow-unverified is given. A --from archive is not checked.

		  User install goes to \${XDG_DATA_HOME:-~/.local/share}/${EXE_NAME}, with a
		  launcher in ~/.local/share/applications and ${EXE_NAME} on PATH via
		  ~/.local/bin. A system install goes to /opt/${EXE_NAME} (/usr/local on BSD)
		  and needs sudo; the plan says so before anything happens.

		  Covers Linux, BSD, WSL and macOS. install.ps1 is the same installer for
		  PowerShell, and adds Windows.

	EOF
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Arguments

release="stable"; target="user"; from=""; do_uninstall=0; assume_yes=0; allow_unverified=0
## A "shift 2" with no value after the option shifts nothing, and the loop
## would never end, so a missing value is caught here instead.
fNeedValue(){ [[ -n "${2:-}" ]] || fDie "$1 needs a value (try --help)"; }
while (($#)); do case "$1" in
	--release=*) release="${1#*=}"; shift ;;
	--target=*)  target="${1#*=}";  shift ;;
	--from=*)    from="${1#*=}";    shift ;;
	--release) fNeedValue "$@"; release="$2"; shift 2 ;;
	--target)  fNeedValue "$@"; target="$2";  shift 2 ;;
	--from)    fNeedValue "$@"; from="$2";    shift 2 ;;
	--uninstall) do_uninstall=1; shift ;;
	--allow-unverified) allow_unverified=1; shift ;;
	-y|--yes)  assume_yes=1; shift ;;
	--version) echo "${APP_NAME} installer ${INSTALLER_VERSION}"; exit 0 ;;
	-h|--help) fHelp; exit 0 ;;
	*) fDie "unknown option: $1 (try --help)" ;;
esac; done

[[ "$release" == "stable" || "$release" == "dev" ]] || fDie "--release must be dev or stable"
[[ "$target"  == "user"   || "$target"  == "system" ]] || fDie "--target must be user or system"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Platform and paths

case "$(uname -s | tr '[:upper:]' '[:lower:]')" in
	linux*)                               os="linux" ;;
	freebsd*|openbsd*|netbsd*|dragonfly*) os="bsd" ;;
	darwin*) fDie "there is no macOS build yet - the .app bundle gets installed from here once there is one" ;;
	*) fDie "unsupported OS: $(uname -s)" ;;
esac

case "$(uname -m)" in
	x86_64|amd64)  arch="x86_64" ;;
	aarch64|arm64) arch="arm64" ;;
	*) fDie "unsupported architecture: $(uname -m)" ;;
esac

## GUI package layout: the whole prefix in one dir, reached by a desktop
## launcher and a symlink on PATH (a file manager gets started both ways).
if [[ "$target" == "system" ]]; then
	[[ "$os" == "bsd" ]] && prefix="/usr/local/${EXE_NAME}" || prefix="/opt/${EXE_NAME}"
	appdir="/usr/local/share/applications"
	bindir="/usr/local/bin"
else
	prefix="${XDG_DATA_HOME:-${HOME}/.local/share}/${EXE_NAME}"
	appdir="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
	bindir="${HOME}/.local/bin"
fi
launcher="${appdir}/${EXE_NAME}.desktop"
symlink="${bindir}/${EXE_NAME}"

## Privileged steps run through this; empty when we are already root.
priv=""
if [[ "$target" == "system" && "$(id -u)" != "0" ]]; then
	command -v sudo >/dev/null 2>&1 || fDie "--target system needs root, and sudo was not found - re-run as root"
	priv="sudo"
fi

## A user install that cannot write where it is going would otherwise fail
## halfway, on a bare command. Say so before asking. Renames need the parent.
fCheckWritable(){
	[[ -n "$priv" ]] && return 0
	local dir
	for dir in "$(dirname "$prefix")" "$appdir" "$bindir"; do
		while [[ ! -e "$dir" ]]; do dir="$(dirname "$dir")"; done
		[[ -w "$dir" ]] || fDie "no write access to ${dir} - check who owns it with: ls -ld ${dir}"
	done
	return 0
}

## Guard every destructive path: an empty or unexpected prefix must never reach rm -rf.
fCheckPrefix(){
	[[ "${prefix:0:1}" == "/" ]] || fDie "refusing to touch a non-absolute prefix: ${prefix}"
	[[ "$(basename "$prefix")" == "$EXE_NAME" ]] || fDie "refusing to touch a prefix not named ${EXE_NAME}: ${prefix}"
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Functions

fConfirm(){
	((assume_yes)) && return 0
	[[ -t 0 ]] || fDie "nothing to read a confirmation from - re-run with -y to accept the plan above"
	local answer; read -r -p "Proceed? [y/N] " answer
	[[ "$answer" == [yY]* ]] || { fEcho_Clean ""; fEcho "Nothing changed."; fEcho_Clean ""; exit 1; }
}

fFetch(){ ## url dest
	if   command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
	elif command -v wget >/dev/null 2>&1; then wget -qO "$2" "$1"
	else fDie "need curl or wget to download"; fi
}

fFetchText(){ ## url -> stdout
	if   command -v curl >/dev/null 2>&1; then curl -fsSL "$1"
	elif command -v wget >/dev/null 2>&1; then wget -qO- "$1"
	else fDie "need curl or wget to download"; fi
}

fSha256(){ ## file -> hash
	if   command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
	elif command -v shasum    >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
	elif command -v openssl   >/dev/null 2>&1; then openssl dgst -sha256 "$1" | awk '{print $NF}'
	else return 1; fi
}

## Pull one string field out of a GitHub API response. No jq dependency: the
## API emits one value per match, and we only ever want the first or a fixed list.
fJsonValues(){ ## field < json
	{ grep -o "\"$1\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" || true; } | sed 's/.*"\([^"]*\)"$/\1/'
}

## Tags in, "sortkey<TAB>tag" out. Numbers are zero-padded so a plain sort gets
## 1.10 above 1.9, and a release sorts above its own prereleases, which sort -V
## gets backwards. Plain awk and sort, since macOS has no sort -V anyway.
fVersionKeys(){
	awk '{
		tag = $0; ver = tag; sub(/^v/, "", ver)
		pre = ""; dash = index(ver, "-")
		if (dash) { pre = substr(ver, dash + 1); ver = substr(ver, 1, dash - 1) }
		n = split(ver, part, ".")
		key = ""
		for (i = 1; i <= 3; i++) key = key sprintf("%06d.", (i <= n ? part[i] + 0 : 0))
		if (pre == "") { key = key "1" } else {
			padded = ""
			while (match(pre, /[0-9]+/)) {
				padded = padded substr(pre, 1, RSTART - 1) sprintf("%06d", substr(pre, RSTART, RLENGTH) + 0)
				pre = substr(pre, RSTART + RLENGTH)
			}
			key = key "0" padded pre
		}
		printf "%s\t%s\n", key, tag
	}'
}

## stable = the newest release with no prerelease part, or the newest prerelease
## when nothing stable exists yet; dev = the newest of any kind. Ranked by
## version rather than by the order the API lists them in, and never through
## releases/latest, which 404s on a repo that has only prereleases.
## Takes the release list the caller fetched, so a failed request can be told
## apart from an empty list. Prints nothing when there is no release at all - the
## caller reports that, since dying inside a command substitution only kills the
## subshell.
fResolveTag(){ ## releases json
	local tags newest stable
	tags="$(printf '%s' "$1" | fJsonValues tag_name || true)"
	[[ -n "$tags" ]] || return 0
	newest="$(printf '%s\n' "$tags" | fVersionKeys | LC_ALL=C sort -r | head -1 | cut -f2 || true)"
	stable="$(printf '%s\n' "$tags" | grep -v -- '-' | fVersionKeys | LC_ALL=C sort -r | head -1 | cut -f2 || true)"
	if [[ "$release" == "stable" && -n "$stable" ]]; then echo "$stable"; else echo "$newest"; fi
}

## Write the desktop launcher, preferring the one shipped in the prefix and
## rewriting Exec/Icon to absolute paths (nothing else knows where we landed).
fInstallLauncher(){
	local shipped="${prefix}/share/applications/${EXE_NAME}.desktop"
	local icon tmp; icon=""
	for candidate in \
		"${prefix}/share/icons/hicolor/scalable/apps/${EXE_NAME}.svg" \
		"${prefix}/share/icons/hicolor/48x48/apps/${EXE_NAME}.png" \
		"${prefix}/share/pixmaps/${EXE_NAME}.png"
	do if [[ -f "$candidate" ]]; then icon="$candidate"; break; fi; done
	[[ -n "$icon" ]] || icon="${EXE_NAME}"

	tmp="${work}/${EXE_NAME}.desktop"
	if [[ -f "$shipped" ]]; then
		sed -e "s|^Exec=.*|Exec=${prefix}/bin/${EXE_NAME} %U|" \
		    -e "s|^Icon=.*|Icon=${icon}|" \
		    -e "s|^TryExec=.*|TryExec=${prefix}/bin/${EXE_NAME}|" "$shipped" > "$tmp"
	else
		cat > "$tmp" <<-EOF
			[Desktop Entry]
			Type=Application
			Name=${APP_NAME}
			Comment=Browse the file system
			Exec=${prefix}/bin/${EXE_NAME} %U
			Icon=${icon}
			Terminal=false
			Categories=System;FileTools;FileManager;
			MimeType=inode/directory;
		EOF
	fi

	$priv mkdir -p "$appdir"
	$priv cp "$tmp" "$launcher"
	$priv chmod 644 "$launcher"
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Uninstall

fEcho_Clean ""
fEcho_Clean "${APP_NAME} installer"

if ((do_uninstall)); then
	fCheckPrefix
	fEcho_Clean ""
	fEcho "Uninstall plan"
	fEcho_Clean "Prefix ....: ${prefix}$([[ -d "$prefix" ]] || echo '   (not present)')"
	fEcho_Clean "Launcher ..: ${launcher}$([[ -f "$launcher" ]] || echo '   (not present)')"
	fEcho_Clean "Symlink ...: ${symlink}$([[ -L "$symlink" ]] || echo '   (not present)')"
	[[ -n "$priv" ]] && fEcho_Clean "Privileges : sudo (system target)" || true

	if [[ ! -d "$prefix" && ! -f "$launcher" && ! -L "$symlink" ]]; then
		fEcho_Clean ""
		fEcho "Nothing installed here. Nothing to do."
		fEcho_Clean ""
		exit 0
	fi

	fEcho_Clean ""
	fConfirm

	fEcho_Clean ""
	fEcho "Removing"
	## Only unlink a symlink that actually points into our prefix.
	if [[ -L "$symlink" ]]; then
		linkdest="$(readlink "$symlink" || true)"
		case "$linkdest" in
			"${prefix}"/*) $priv rm -f "$symlink"; fEcho_Clean "removed ${symlink}" ;;
			*) fWarn "left ${symlink} alone - it points at ${linkdest}, not our prefix" ;;
		esac
	fi
	if [[ -f "$launcher" ]]; then $priv rm -f  "$launcher"; fEcho_Clean "removed ${launcher}"; fi
	if [[ -d "$prefix"   ]]; then $priv rm -rf "$prefix";   fEcho_Clean "removed ${prefix}";   fi
	command -v update-desktop-database >/dev/null 2>&1 && $priv update-desktop-database "$appdir" 2>/dev/null || true

	fEcho_Clean ""
	fEcho "Uninstalled. Settings in ~/.config/${EXE_NAME} were left in place."
	fEcho_Clean ""
	exit 0
fi


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Resolve what to install

work="$(mktemp -d "${TMPDIR:-/tmp}/${EXE_NAME}-install.XXXXXX")"
trap 'rm -rf "${work}"' EXIT

fEcho_Clean ""
fEcho "Resolving"

sums_url=""
if [[ -n "$from" ]]; then
	download_url="$from"
	source_desc="$from"
	## Display only: a conventionally named archive still tells us its version.
	## Stop at the platform, not at the first dash: a prerelease version has its
	## own dashes (1.0.0-beta1) and cutting there reported it as plain 1.0.0.
	version="$(printf '%s' "${from##*/}" | sed -nE "s/^${EXE_NAME}-([0-9].*)-(linux|bsd|windows|macos)-.*/\1/p")"
	release_desc="local archive"
	verify="no checksum (--from)"
else
	command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || fDie "need curl or wget to reach the releases page"
	releases_json="$(fFetchText "https://api.github.com/repos/${REPO}/releases?per_page=100" 2>/dev/null)" \
		|| fDie "could not list the releases of ${REPO} - check the connection. GitHub also limits anonymous requests to 60 an hour."
	tag="$(fResolveTag "$releases_json")"
	[[ -n "$tag" ]] || fDie "no release published yet for ${REPO}"
	version="${tag#v}"
	release_note=""
	[[ "$release" == "stable" && "$version" == *-* ]] && release_note="   (no stable release yet, so the newest prerelease)"
	asset="${EXE_NAME}-${version}-${os}-${arch}.tar.gz"
	sums_asset="${EXE_NAME}-${version}-sha256sums.txt"

	tag_json="$(fFetchText "https://api.github.com/repos/${REPO}/releases/tags/${tag}")" \
		|| fDie "could not read release ${tag} of ${REPO} from GitHub"
	asset_urls="$(printf '%s' "$tag_json" | fJsonValues browser_download_url || true)"
	asset_url="$(printf '%s\n' "$asset_urls" | grep -F "/${asset}" | head -1 || true)"
	sums_url="$(printf '%s\n' "$asset_urls" | grep -F "/${sums_asset}" | head -1 || true)"

	if [[ -z "$asset_url" ]]; then
		fEcho_Clean ""
		fEcho_Clean "Release ${tag} has no build for ${os}-${arch}. It publishes:"
		printf '%s\n' "$asset_urls" | sed 's|.*/|  |'
		fDie "no ${asset} in release ${tag}"
	fi
	download_url="$asset_url"
	source_desc="$asset_url"
	release_desc="${release} ${version}"
	if [[ -n "$sums_url" ]]; then
		verify="sha256, against ${sums_asset}"
	elif ((allow_unverified)); then
		verify="UNVERIFIED - release publishes no checksums (--allow-unverified)"
	else
		## Before the plan and the question, so -y alone never gets past it.
		fDie "release ${tag} publishes no ${sums_asset}, so the download can't be checked - re-run with --allow-unverified to install it anyway"
	fi
fi

fCheckPrefix
fCheckWritable

fEcho_Clean ""
fEcho "Plan"
fEcho_Clean "Release ...: ${release_desc}${release_note:-}"
fEcho_Clean "Platform ..: ${os}-${arch}"
fEcho_Clean "Download ..: ${source_desc}"
fEcho_Clean "Verify ....: ${verify}"
fEcho_Clean "Prefix ....: ${prefix}$([[ -d "$prefix" ]] && echo '   (replaces the install already there)')"
fEcho_Clean "Launcher ..: ${launcher}"
fEcho_Clean "Symlink ...: ${symlink} -> ${prefix}/bin/${EXE_NAME}"
[[ -n "$priv" ]] && fEcho_Clean "Privileges : sudo (system target)" || true

fEcho_Clean ""
fConfirm


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Download and verify

fEcho_Clean ""
fEcho "Downloading"

archive="${work}/${EXE_NAME}.tar.gz"
if [[ -n "$from" && -f "$from" ]]; then
	cp "$from" "$archive"
	fEcho_Clean "using local archive ${from}"
else
	fFetch "$download_url" "$archive" || fDie "download failed: ${download_url}"
	fEcho_Clean "got $(wc -c < "$archive" | tr -d ' ') bytes"
fi

if [[ -z "$from" && -n "${sums_url}" ]]; then
	fFetch "$sums_url" "${work}/sums.txt" || fDie "download failed: ${sums_url}"
	expected="$(grep -E "[ *]${asset}\$" "${work}/sums.txt" | awk '{print $1}' | head -1 || true)"
	[[ -n "$expected" ]] || fDie "${sums_asset} has no line for ${asset}"
	actual="$(fSha256 "$archive")" || fDie "no sha256 tool found (sha256sum, shasum or openssl)"
	[[ "$actual" == "$expected" ]] || fDie "checksum mismatch - expected ${expected}, got ${actual}"
	fEcho_Clean "sha256 verified"
else
	fWarn "skipping checksum verification"
fi


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Install

fEcho_Clean ""
fEcho "Installing"

mkdir -p "${work}/unpacked"
tar -xzf "$archive" -C "${work}/unpacked" || fDie "could not unpack the archive"

## Archives carry one top-level dir; tolerate a flat one too.
tree="${work}/unpacked"
entries="$(ls -A "$tree")"
if [[ "$(printf '%s\n' "$entries" | wc -l | tr -d ' ')" == "1" && -d "${tree}/${entries}" ]]; then
	tree="${tree}/${entries}"
fi
[[ -x "${tree}/bin/${EXE_NAME}" ]] || fDie "archive has no bin/${EXE_NAME} - wrong or damaged package"

## Stage beside the prefix, on its own filesystem, so the cross-device copy
## out of TMPDIR happens before the existing install is touched. Then swap with
## same-filesystem renames, which are atomic and cannot fail partway - a plain
## cross-device "mv $tree $prefix" could die mid-copy and lose both installs.
$priv mkdir -p "$(dirname "$prefix")"
staging="$(dirname "$prefix")/.${EXE_NAME}-install.$$"
backup="${prefix}.old.$$"
$priv rm -rf "$staging" "$backup"
$priv cp -a "$tree" "$staging" || { $priv rm -rf "$staging"; fDie "could not stage the new install"; }
if [[ "$target" == "system" ]]; then
	## Staged as the invoking user; a system prefix must not stay user-writable.
	$priv chown -R 0:0 "$staging" 2>/dev/null || true
	$priv chmod -R a+rX "$staging"
fi
if [[ -e "$prefix" ]]; then
	$priv mv "$prefix" "$backup" || { $priv rm -rf "$staging"; fDie "could not move the existing install aside"; }
fi
if ! $priv mv "$staging" "$prefix"; then
	[[ -e "$backup" ]] && $priv mv "$backup" "$prefix"
	$priv rm -rf "$staging"
	fDie "could not move the new install into place"
fi
$priv rm -rf "$backup"
fEcho_Clean "prefix installed at ${prefix}"

fInstallLauncher
fEcho_Clean "launcher installed at ${launcher}"

$priv mkdir -p "$bindir"
$priv ln -sfn "${prefix}/bin/${EXE_NAME}" "$symlink"
fEcho_Clean "symlink installed at ${symlink}"

command -v update-desktop-database >/dev/null 2>&1 && $priv update-desktop-database "$appdir" 2>/dev/null || true
command -v gtk-update-icon-cache    >/dev/null 2>&1 && $priv gtk-update-icon-cache -qtf "${prefix}/share/icons/hicolor" 2>/dev/null || true


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Done

fEcho_Clean ""
fEcho "Installed ${APP_NAME}${version:+ }${version}"
fEcho_Clean "Run it from the menu, or type: ${EXE_NAME}"
case ":${PATH}:" in
	*":${bindir}:"*) ;;
	*) fEcho_Clean "Note: ${bindir} is not on your PATH yet." ;;
esac
fEcho_Clean "Uninstall with the same command plus --uninstall."
fEcho_Clean ""


##	History:
##		- 2026-07-23 JC: Created (unix half of the one-liner install; Windows is
##		  install.ps1).
##		- 2026-09-19 JC: Dropped --arch (always detected), added --version and
##		  --opt=value, and stable now falls back to the newest prerelease.
##		- 2026-09-25 JC: A failed request to GitHub says so instead of reporting
##		  no release, downloads fail with a sentence, and a user install checks
##		  it can write where it is going before asking.
##		- 2026-10-03 JC: A release with no checksums file stops the install,
##		  -y or not, unless --allow-unverified is given.
