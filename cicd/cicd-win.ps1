##	Purpose:
##		- Windows-native CI/CD pipeline for Nemo Anywhere. A PowerShell companion to
##		  the Linux cicd.bash - it does the same shape of work, but builds the app
##		  NATIVELY on Windows with MSYS2/MinGW-w64 (meson + ninja) rather than in a
##		  Linux container. The container (nemo-build / nemo-winbuild) is the Linux
##		  host's cross story; on real Windows there's a native GTK3 toolchain, so no
##		  Docker or wine is involved here. Does NOT touch cicd.bash or config.bash.
##		- Stages (fail-fast; any error aborts before the next stage):
##		   0. remote sync   (fetch; fast-forward if safely behind; abort if diverged)
##		   1. lint          (check-only: every checker in cicd/utility/lint.bash)
##		   2. debug build   (meson setup -Dxmp=false + ninja, MSYS2 mingw64)
##		   3. tests         (meson test suite, the installer's in-use check, then a
##		                     --version smoke and a GUI launch smoke of the built exe)
##		   4. stage         (self-contained runtime bundle - the packer's input)
##		   5. packages      (portable single-exe via Enigma Virtual Box; NSIS later)
##		   6. dogfood       (drop the single exe into the synced app dir)
##		   7. publish       (stash -> pull -> add -> commit -> push, current branch)
##		- The build needs MSYS2 with the mingw64 GTK toolchain (gtk3, meson, ninja,
##		  json-glib, libexif, libgsf). A missing toolchain warn-skips the build/stage
##		  (so sync + publish still run) unless -BuildStrict makes it a hard failure.
##		- The staged bundle is a whole folder (nemo is a GTK prefix): the single exe
##		  (extension lib folded in) in app\, and the mingw64 DLL dependency CLOSURE +
##		  pixbuf loaders + schemas + icons under mingw64\. It is the INPUT to the
##		  packer - the shipped/dogfood artifact is the single self-contained exe the
##		  packer produces, dropped as one file (nemo-anywhere.exe) beside the other
##		  by-self win64 apps. n8runfm.ps1 keeps its own stamped pool of that exe.
##		- Code signing is optional and OFF unless configured via env: set
##		  NEMO_SIGN_THUMBPRINT (an installed cert, incl. a hardware token) or
##		  NEMO_SIGN_PFX (+ NEMO_SIGN_PFX_PASSWORD) to Authenticode-sign the packed
##		  exe; unconfigured, the sign step is a no-op so the unsigned dev flow is
##		  never blocked. Timestamp URL via NEMO_SIGN_TS_URL, signtool via NEMO_SIGNTOOL.
##		- What Windows can't do (dropped vs cicd.bash): the profiler (Unix sampler),
##		  the headless X harness / screenshots / demo (Xvfb), .deb/.rpm packages, and
##		  the rar version-archive step of publish (Linux publisher only).
##		- Syntax:
##		  pwsh cicd/cicd-win.ps1 [options]
##		  Options:
##		   -Yes            run unattended (no confirm / message prompt)
##		   -Quiet          quiet + unattended (implies -Yes); publish runs quiet too
##		   -Quick          skip the slow stages (reserved: none run here yet, so the
##		                   build, tests, pack and dogfood all still run)
##		   -Gate           merge gate only: lint + build + tests + smokes, then exit (no stage/publish)
##		   -NoSync         skip the remote sync check (stage 0)
##		   -NoFmt          skip the lint stage
##		   -NoBuild        skip the build + tests + stage stages
##		   -NoDogfood      skip installing the staged bundle into the dogfood folder
##		   -NoPack         skip the portable single-exe pack stage
##		   -NoSign         skip Authenticode signing (also auto-skips if unconfigured)
##		   -NoPublish      skip the git publish stage
##		   -BuildStrict    a missing MSYS2 toolchain aborts instead of warn-skip
##		   -Message MSG    publish hands-off with this commit message (no editor)
##		   -Help           show this help
##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[switch]$Yes,
	[switch]$Quiet,
	[switch]$Quick,
	[switch]$Gate,
	[switch]$NoSync,
	[switch]$NoFmt,
	[switch]$NoBuild,
	[switch]$NoDogfood,
	[switch]$NoPack,
	[switch]$NoSign,
	[switch]$NoPublish,
	[switch]$BuildStrict,
	[Alias('Msg', 'm')][string]$Message = "",
	[switch]$Help
)

## Requires PowerShell 7+ (pwsh): this uses $IsWindows and PS7 semantics. Windows
## PowerShell 5.1 has no $IsWindows, so the StrictMode guard below would throw a
## cryptic error instead. Bail early with a clear pointer.
if ($PSVersionTable.PSVersion.Major -lt 6) {
	Write-Error "cicd-win.ps1 needs PowerShell 7+ (pwsh); you're on Windows PowerShell $($PSVersionTable.PSVersion). Run: pwsh -File cicd/cicd-win.ps1"
	exit 1
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
## We drive native tools (git, bash, the exe) by hand and read $LASTEXITCODE -
## several probes (git diff --quiet) return non-zero ON PURPOSE. Keep a non-zero
## native exit from throwing so those reads work regardless of the shell.
$PSNativeCommandUseErrorActionPreference = $false

if ($Help) {
	## Print only the leading Purpose..History header block (mirrors cicd.bash's
	## `sed -n '/Purpose:/,/History:/p'`), not every top-level ## comment.
	$inBlock = $false
	foreach ($line in (Get-Content -LiteralPath $PSCommandPath)) {
		if ($line -match '^##\tPurpose:') { $inBlock = $true }
		if ($inBlock) {
			if ($line -match '^##\tHistory:') { break }
			$line -replace '^##\t?', ''
		}
	}
	exit 0
}

## Windows-only: this pipeline drives MSYS2 and writes a host-side dogfood folder.
if (-not $IsWindows) {
	Write-Error "cicd-win.ps1: this pipeline only runs on Windows (use cicd/cicd.bash on Linux)."
	exit 1
}

## -Quiet implies unattended; both suppress the preflight prompt.
$Unattended = ($Yes -or $Quiet)


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Configuration (Windows-native; config.bash stays the Linux source of truth).

## Repo root = the parent of this script's cicd/ dir. Git and bash run here.
$Root    = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$AppName = "Nemo Anywhere"
$ExeName = "nemo-anywhere"

## MSYS2 / mingw64 toolchain. The build runs through the mingw64 login shell so its
## PATH, pkg-config, and codegen tools resolve exactly as a hand build would.
$MsysRoot = "C:\msys64"
$MsysBash = Join-Path $MsysRoot "usr\bin\bash.exe"
$MingwBin = Join-Path $MsysRoot "mingw64\bin"

## Build + staged-bundle dirs (both under the gitignored cicd/artifacts/). Relative
## forms are what the mingw64 bash gets, so paths stay POSIX inside the shell.
$BuildRel = "cicd/artifacts/build-win"
$StageRel = "cicd/artifacts/win-run"
$StagerRel = "cicd/win/stage-native.bash"
$BuildDir = Join-Path $Root "cicd\artifacts\build-win"
$StageDir = Join-Path $Root "cicd\artifacts\win-run"

## Dogfood: the single self-contained exe dropped straight into the SYNCED app dir
## for Windows (rides Dropbox, any box can grab it), one file per app alongside the
## others - no app subfolder, no dll tree. n8runfm.ps1 reads this dir and keeps its
## own GFS-rotated pool locally, so nothing dated is written here.
## First one whose parent exists wins. Both spell the same directory where the
## junction is present, but a box may have only the Dropbox name.
$DogfoodRoots = @(
	(Join-Path $HOME "synced\0-0\common\exec\app\mswin")
	(Join-Path $HOME "Dropbox\0-0\common\exec\app\mswin")
)
$DogfoodRoot = $DogfoodRoots | Where-Object { Test-Path -LiteralPath (Split-Path $_ -Parent) } | Select-Object -First 1
if (-not $DogfoodRoot) { $DogfoodRoot = $DogfoodRoots[0] }
$DogfoodExe  = Join-Path $DogfoodRoot "$ExeName.exe"

## The packer's output - the single self-contained exe (cicd/win/pack-portable.ps1).
$PortableExe = Join-Path $Root "cicd\artifacts\win-portable\$ExeName.exe"

## The single version source. meson.build carries `version : '6.6.4'` (colon form).
$VersionManifest = Join-Path $Root "source\meson.build"

## Code signing (optional; all from env so nothing secret is written into the repo). Signing
## is a no-op unless a cert is configured, so the unsigned dev flow is never blocked.
##   NEMO_SIGN_THUMBPRINT  SHA1 thumbprint of an installed cert (store or token) - preferred
##   NEMO_SIGN_PFX (+ _PASSWORD)  a .pfx on disk (testing / self-signed)
##   NEMO_SIGN_TS_URL      RFC-3161 timestamp URL (default below)
##   NEMO_SIGNTOOL         explicit signtool.exe path (else PATH, then the Windows SDK)
$SignThumbprint = $env:NEMO_SIGN_THUMBPRINT
$SignPfx        = $env:NEMO_SIGN_PFX
$SignPfxPass    = $env:NEMO_SIGN_PFX_PASSWORD
$SignTsUrl      = if ($env:NEMO_SIGN_TS_URL) { $env:NEMO_SIGN_TS_URL } else { "http://timestamp.digicert.com" }

## Full-run transcript (gitignored; a Windows-side sibling of the Linux lint logs).
$LogDir = Join-Path $Root "cicd\artifacts\lint-win"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Output helpers (mirror cicd.bash: fEcho / fEcho_Clean / fSection)

$script:WasLastEchoBlank = $false
$script:Letterbox = "•" * 73
$script:Transcribing = $false

function fEcho_Clean {
	param([string]$Msg = "")
	if ($Msg) { Write-Host $Msg; $script:WasLastEchoBlank = $false }
	elseif (-not $script:WasLastEchoBlank) { Write-Host ""; $script:WasLastEchoBlank = $true }
}
function fEcho     { param([string]$Msg = ""); if ($Msg) { fEcho_Clean "[ $Msg ]" } else { fEcho_Clean } }
function fSection  { param([string]$Msg);      fEcho_Clean; fEcho_Clean $script:Letterbox; fEcho $Msg }
function fNote     { param([string]$Msg); fEcho_Clean $Msg }
function fWarn     { param([string]$Msg); fEcho "WARNING: $Msg" }
function fDie      { param([string]$Msg); fEcho "FAILED: $Msg"; exit 1 }


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Functions

## Convert a Windows path to the MSYS2 form the mingw64 bash expects (C:\x -> /c/x).
function fToMsysPath {
	param([Parameter(Mandatory)][string]$Path)
	$p = $Path -replace '\\', '/'
	if ($p -match '^([A-Za-z]):(.*)$') { return "/$($Matches[1].ToLower())$($Matches[2])" }
	return $p
}

## Run a command in the MSYS2 mingw64 login shell, from the repo root. The command's
## own stdout streams to the console; its exit code goes to $script:MingwRc (NOT the
## return value - a function's return is its whole pipeline, so returning the code
## would fold the native stdout into it). MSYSTEM=MINGW64 + a login shell give the
## mingw64 PATH and codegen tools; CHERE_INVOKING keeps our cd instead of $HOME.
## -Quiet swallows output (for probes).
$script:MingwRc = 0
function fMingw {
	param([Parameter(Mandatory)][string]$ShCommand, [switch]$Quiet)
	$rootU = fToMsysPath $Root
	$env:MSYSTEM = "MINGW64"
	$env:CHERE_INVOKING = "1"
	if ($Quiet) { & $MsysBash -lc "cd '$rootU' || exit 2; $ShCommand" *> $null }
	else        { & $MsysBash -lc "cd '$rootU' || exit 2; $ShCommand" }
	$script:MingwRc = $LASTEXITCODE
}

## True when the mingw64 GTK build toolchain is present. Returns a reason string
## when something's missing (for a clear warn-skip), or $null when it's good to go.
function fToolchainMissing {
	if (-not (Test-Path -LiteralPath $MsysBash)) { return "MSYS2 not found at $MsysRoot" }
	fMingw "command -v gcc meson ninja pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 json-glib-1.0 libexif" -Quiet
	if ($script:MingwRc -ne 0) { return "mingw64 toolchain incomplete (need gtk3, meson, ninja, json-glib, libexif, libgsf via pacman)" }
	return $null
}

## First `version : '...'` from meson.build (the project() line). Display only.
function fVersion {
	$m = Select-String -LiteralPath $VersionManifest -Pattern "version\s*:\s*'([^']+)'" | Select-Object -First 1
	if (-not $m) { return "?" }
	return $m.Matches[0].Groups[1].Value
}

## Native build: meson setup (first time with -Dxmp=false, else --reconfigure) then
## ninja, in the mingw64 shell. Aborts on a real build error.
function fBuild {
	## -j caps at half the cores, same as the Linux engine; ninja alone takes cores+2.
	$jobs = [Math]::Max(1, [Environment]::ProcessorCount / 2 -as [int])
	## The linker stamps the PE header with the clock unless told otherwise, which
	## makes two builds of the same commit differ. Set inside the snippet rather
	## than as $env: - the mingw shell does not reliably carry ours across.
	## meson removes a static archive before rebuilding it everywhere but here, so
	## a thin archive still naming the object of a deleted source fails to update,
	## and ar blames the archive itself ("No such file or directory"). One ar
	## cannot read is dropped; thin archives cost nothing to write again.
	## gcc runs the link-time optimization jobs through make, and with none on
	## PATH it does them one at a time with only a warning.
	$sh = @"
command -v make >/dev/null 2>&1 || { echo "no make in MSYS2, so LTO would run serially: pacman -S make"; exit 3; }
export SOURCE_DATE_EPOCH="`$(git log -1 --format=%ct 2>/dev/null || echo 0)"
if [ -f $BuildRel/build.ninja ]; then meson setup --reconfigure $BuildRel source; else meson setup -Dxmp=false $BuildRel source; fi
find $BuildRel -name '*.a' -type f | while read -r lib; do ar t "`$lib" >/dev/null 2>&1 || { echo "dropped stale `$lib"; rm -f "`$lib"; }; done
ninja -C $BuildRel -j $jobs
"@
	fMingw $sh
	if ($script:MingwRc -ne 0) { fDie "native build failed (exit $($script:MingwRc))" }
	$exe = Join-Path $BuildDir "src\$ExeName.exe"
	if (-not (Test-Path -LiteralPath $exe)) { fDie "build produced no exe: $exe" }
	$size = "{0:N1} MB" -f ((Get-Item -LiteralPath $exe).Length / 1MB)
	fEcho "OK: native build: $exe ($size)"
}

## Every check-only lint, through the shared bash helper (cicd/utility/lint.bash).
## Each checker warn-skips when its tool isn't installed; findings abort. Runs
## in the mingw64 shell where they live.
function fLint {
	fMingw "bash cicd/utility/lint.bash"
	if ($script:MingwRc -ne 0) { fDie "lint failed (exit $($script:MingwRc))" }
}

## Native smoke: run the built exe's --version on real Windows. The extension lib is
## folded into the exe; the GTK DLLs come from the host mingw64\bin on PATH. Proves
## the build links and loads.
function fSmoke {
	param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$RuntimeBin)
	$out = ""
	$saved = $env:PATH
	$savedDialog = $env:NEMO_NO_CRASH_DIALOG
	try {
		$env:PATH = "$RuntimeBin;$env:SystemRoot\System32;$env:SystemRoot"
		## A crash here would otherwise stop the run behind a modal box.
		$env:NEMO_NO_CRASH_DIALOG = "1"
		$out = (& $Exe --version 2>&1 | Out-String).Trim()
	} finally { $env:PATH = $saved; $env:NEMO_NO_CRASH_DIALOG = $savedDialog }
	if ($LASTEXITCODE -ne 0) { fDie "smoke failed (exit $LASTEXITCODE): $Exe --version`n$out" }
	fEcho "OK: smoke: $out"
}

## The built exe opened on a scratch folder, its window found and left up a few
## seconds, then ended. Over ssh the window is made but never shown, and the
## script allows for that.
function fGuiSmoke {
	param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$RuntimeBin)
	& pwsh -NoProfile -File (Join-Path $Root "cicd\win\gui-launch-smoke.ps1") -Exe $Exe -RuntimeBin $RuntimeBin
	if ($LASTEXITCODE -ne 0) { fDie "GUI launch smoke failed (exit $LASTEXITCODE)" }
}

## The meson suite against the debug build. The mingw64 shell has the GTK DLLs on
## PATH. A crash would otherwise stop the run behind a modal box.
function fTests {
	$jobs = [Math]::Max(1, [Environment]::ProcessorCount / 2 -as [int])
	fMingw "NEMO_NO_CRASH_DIALOG=1 meson test -C $BuildRel --no-rebuild --num-processes $jobs --print-errorlogs"
	if ($script:MingwRc -ne 0) { fDie "test suite failed (exit $($script:MingwRc))" }
	fEcho "OK: test suite"
	## The installer's in-use check needs a real process to find, so it runs
	## here rather than beside the other script tests in lint.
	& pwsh -NoProfile -File (Join-Path $Root "cicd\win\test-install-holders.ps1")
	if ($LASTEXITCODE -ne 0) { fDie "install.ps1 in-use check failed (exit $LASTEXITCODE)" }
	## Needs Windows ACLs to read, so it sits here for the same reason.
	& pwsh -NoProfile -File (Join-Path $Root "cicd\win\test-install-acl.ps1")
	if ($LASTEXITCODE -ne 0) { fDie "install.ps1 ACL check failed (exit $LASTEXITCODE)" }
}

## Path to the freshly built exe for the in-place smoke. The extension lib is folded
## in, so nothing else needs to sit beside it. Returns the exe path.
function fPrepInPlaceSmoke {
	return (Join-Path $BuildDir "src\$ExeName.exe")
}

## Stage the self-contained runtime bundle via the shared bash helper (it needs ldd
## and the mingw64 tree), then re-smoke it using ONLY the bundle's own runtime, so a
## missing dll shows up here rather than on another box.
function fStage {
	fMingw "bash $StagerRel $BuildRel $StageRel"
	if ($script:MingwRc -ne 0) { fDie "staging failed (exit $($script:MingwRc))" }
	$exe = Join-Path $StageDir "app\$ExeName.exe"
	if (-not (Test-Path -LiteralPath $exe)) { fDie "staged bundle missing its exe: $exe" }
	fEcho "OK: staged runtime bundle -> $StageRel"
	fSmoke -Exe $exe -RuntimeBin (Join-Path $StageDir "mingw64\bin")
}

## Dogfood: drop the single packed exe into the synced app dir as one file. Needs
## the packer's output, so it runs after stage 5. Also retires what earlier layouts
## left in the by-self folder this drop used to live in.
function fDogfood {
	if (-not (Test-Path -LiteralPath $PortableExe)) { fWarn "no portable exe to dogfood (pack skipped or failed); skipping"; return }
	New-Item -ItemType Directory -Path $DogfoodRoot -Force | Out-Null
	## The launcher runs its own local copy, but someone may have started this one
	## directly, and Windows will not overwrite a running exe anyway.
	$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -ieq $DogfoodExe }
	if ($running) { fWarn "$DogfoodExe is running; left as is"; return }
	Copy-Item -LiteralPath $PortableExe -Destination $DogfoodExe -Force
	fEcho "OK: dogfood -> $DogfoodExe"
	fRetireOldDogfood
}


## The drop used to go to the by-self win64 folder, and before that as a whole
## nemo-anywhere\ bundle inside it. Both are dead weight once the app dir above is
## the source the launcher reads, and neither goes away on its own.
function fRetireOldDogfood {
	$oldRoot = "C:\opt\0-0\common\exec\synced\util\mswin\gui\by-self\win64"
	foreach ($stale in @((Join-Path $oldRoot "$ExeName.exe"), (Join-Path $oldRoot $ExeName))) {
		if (-not (Test-Path -LiteralPath $stale)) { continue }
		try {
			Remove-Item -LiteralPath $stale -Recurse -Force -ErrorAction Stop
			fNote "retired the old dogfood drop: $stale"
		} catch {
			fWarn "couldn't remove $stale ($($_.Exception.Message))"
		}
	}
}

## True when a signing identity is configured (thumbprint or pfx).
function fSignConfigured { return [bool]($SignThumbprint -or $SignPfx) }

## Locate signtool.exe: explicit override, then PATH, then the newest x64 build under
## the Windows 10/11 SDK. $null when none found.
function fFindSigntool {
	if ($env:NEMO_SIGNTOOL -and (Test-Path -LiteralPath $env:NEMO_SIGNTOOL)) { return $env:NEMO_SIGNTOOL }
	$onPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
	if ($onPath) { return $onPath.Source }
	$kits = "C:\Program Files (x86)\Windows Kits\10\bin"
	if (Test-Path -LiteralPath $kits) {
		$cand = Get-ChildItem -LiteralPath $kits -Directory -ErrorAction SilentlyContinue |
			Sort-Object Name -Descending |
			ForEach-Object { Join-Path $_.FullName "x64\signtool.exe" } |
			Where-Object { Test-Path -LiteralPath $_ } |
			Select-Object -First 1
		if ($cand) { return $cand }
	}
	return $null
}

## Authenticode-sign one file with a SHA-256 digest and an RFC-3161 timestamp, then
## verify the chain. No-op (a note) when signing isn't configured, so an unsigned
## build still ships. A missing signtool warns and leaves the exe unsigned; a real
## signing error aborts (a half-signed release should not go out).
function fSignFile {
	## The password arrives as plaintext from the environment either way. This
	## turns it into a SecureString to import the cert, which is the safer of
	## the two ways to hand it to signtool - see the note further down.
	[Diagnostics.CodeAnalysis.SuppressMessageAttribute(
		'PSAvoidUsingConvertToSecureStringWithPlainText', '')]
	param([Parameter(Mandatory)][string]$Path)
	if (-not (fSignConfigured)) { fNote "signing not configured (set NEMO_SIGN_THUMBPRINT or NEMO_SIGN_PFX); leaving exe unsigned"; return }
	$signtool = fFindSigntool
	if (-not $signtool) { fWarn "signtool.exe not found (install the Windows SDK, or set NEMO_SIGNTOOL); leaving exe unsigned"; return }

	$signArgs = @("sign", "/fd", "sha256", "/tr", $SignTsUrl, "/td", "sha256", "/v")
	$imported = $null

	if ($SignThumbprint) {
		$signArgs += @("/sha1", $SignThumbprint)
	} elseif ($SignPfxPass) {
		## Import into this user's store and sign by thumbprint rather than
		## handing signtool /p: a password on a command line is readable by every
		## other process on the box for as long as the process lives. The cert is
		## removed again below.
		$secure = ConvertTo-SecureString -String $SignPfxPass -AsPlainText -Force
		$imported = Import-PfxCertificate -FilePath $SignPfx `
			-CertStoreLocation Cert:\CurrentUser\My -Password $secure
		$signArgs += @("/sha1", $imported.Thumbprint)
	} else {
		$signArgs += @("/f", $SignPfx)
	}
	$signArgs += $Path

	try {
		& $signtool @signArgs
	} finally {
		if ($imported) {
			Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $imported.Thumbprint) -Force -ErrorAction SilentlyContinue
		}
	}
	if ($LASTEXITCODE -ne 0) { fDie "signing failed (exit $LASTEXITCODE): $Path" }
	## /pa = default authenticode policy; a self-signed/test cert verifies only if it
	## is trusted on this box, so treat a verify miss as a warning, not a hard fail.
	& $signtool @("verify", "/pa", "/v", $Path) | Out-Null
	if ($LASTEXITCODE -ne 0) { fWarn "signed, but chain verify failed (untrusted/self-signed cert?): $Path" }
	fEcho "OK: signed $Path"
}

## Pack: the staged bundle -> one self-contained exe (cicd/win/pack-portable.ps1,
## Enigma Virtual Box), then sign it (no-op unless a cert is configured). The script
## warn-skips when EVB isn't installed - the dogfood/single exe is the release artifact.
function fPack {
	$evbFound = (Test-Path -LiteralPath "C:\Program Files (x86)\Enigma Virtual Box\enigmavbconsole.exe") -or
		(Test-Path -LiteralPath "C:\Program Files\Enigma Virtual Box\enigmavbconsole.exe") -or
		[bool](Get-Command enigmavbconsole -ErrorAction SilentlyContinue)
	if (-not $evbFound) { fWarn "Enigma Virtual Box not installed; pack skipped"; return }
	& pwsh -File (Join-Path $Root "cicd\win\pack-portable.ps1")
	if ($LASTEXITCODE -ne 0) { fDie "portable pack failed (exit $LASTEXITCODE)" }
	if ($NoSign) { fNote "signing skipped (-NoSign)" }
	else { fSignFile -Path $PortableExe }
}

## Stage 0: remote sync (see cicd-win history / fRemoteSync in the Linux gate). Make
## sure the local branch can be safely refreshed BEFORE spending the build - what
## publish pushes should be what got built and tested. Behind-only fast-forwards
## (stash-wrapped); diverged aborts now; offline warns.
function fRemoteSync {
	& git rev-parse --abbrev-ref '@{u}' 2>$null | Out-Null
	if ($LASTEXITCODE -ne 0) {
		$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
		fNote "no upstream for ${branch}; nothing to sync"
		return
	}
	& $script:GitExe @script:GitPre fetch --quiet 2>$null
	if ($LASTEXITCODE -ne 0) { fWarn "git fetch failed (offline?); continuing with the local tree"; return }
	$ahead  = [int](& git rev-list --count '@{u}..HEAD')
	$behind = [int](& git rev-list --count 'HEAD..@{u}')
	if ($behind -eq 0) {
		if ($ahead) { fEcho "OK: up to date with upstream ($ahead ahead)" }
		else        { fEcho "OK: up to date with upstream" }
		return
	}
	if ($ahead -gt 0) { fDie "diverged from upstream ($ahead ahead, $behind behind) - reconcile first, or rerun with -NoSync" }
	& git diff --quiet;          $dirtyTracked = ($LASTEXITCODE -ne 0)
	& git diff --cached --quiet; $dirtyStaged  = ($LASTEXITCODE -ne 0)
	$untracked = (& git ls-files --others --exclude-standard)
	$didStash = $false
	if ($dirtyTracked -or $dirtyStaged -or $untracked) {
		$before = @(& git stash list).Count
		fEcho_Clean "git stash push --include-untracked ..."
		fRun "git stash" "git" @("stash", "push", "--include-untracked", "-m", "auto-stash")
		$after = @(& git stash list).Count
		$didStash = ($after -gt $before)
	}
	fEcho_Clean "git pull --ff-only ..."
	fRun "git pull" $script:GitExe ($script:GitPre + @("pull", "--ff-only"))
	if ($didStash) {
		fEcho_Clean "git stash pop ..."
		## A conflicting pop leaves the stash held and the tree half-merged.
		## Abort saying where the work is: a rerun with sync off would otherwise
		## build and commit a tree missing it, conflict markers and all.
		& git stash pop
		if ($LASTEXITCODE -ne 0) {
			fEcho_Clean
			fEcho "Your changes are still in the stash (git stash list)."
			fEcho "Resolve the conflicts, then: git stash drop"
			fEcho "Or start over:               git checkout -- . ; git stash pop"
			fDie "stash pop conflicted after the pull"
		}
	}
	fEcho "OK: fast-forwarded $behind commit(s) from upstream"
}

## The unattended commit message comes from the one helper the Linux side uses,
## through MSYS2's bash, so both platforms word it alike. "Updated" without it.
function fAutoMessage {
	if (-not (Test-Path -LiteralPath $MsysBash)) { return "Updated" }
	$suggest = & $MsysBash -lc "cd '$(fToMsysPath $Root)' && bash cicd/utility/git-auto-msg.bash --suggest" 2>$null
	if ($LASTEXITCODE -eq 0 -and $suggest) { return ([string]($suggest | Select-Object -Last 1)).Trim() }
	return "Updated"
}

## Git that talks to the remote goes through gitsby where it is installed, so the
## fetch, pull and push use the key and account this repo belongs to.
$script:GitExe = "git"
$script:GitPre = @()
if (Get-Command gitsby -ErrorAction SilentlyContinue) { $script:GitExe = "gitsby"; $script:GitPre = @("raw", "git") }

## Run a native command from the repo root; abort (fail-fast) on a non-zero exit.
function fRun {
	param([Parameter(Mandatory)][string]$What, [Parameter(Mandatory)][string]$File, [string[]]$CmdArgs = @())
	& $File @CmdArgs
	if ($LASTEXITCODE -ne 0) { fDie "$What failed (exit $LASTEXITCODE): $File $($CmdArgs -join ' ')" }
}

## Publish: the host-side half of the Linux backup+publish, MINUS the rar version
## archive (that lives in the Linux-only n8git_backup-and-publish). stash (if dirty)
## -> pull --ff-only (if upstream) -> pop -> add -> commit -> push. $Msg empty means
## "let git open its editor".
function fPublish {
	param([Parameter(Mandatory)][AllowEmptyString()][string]$Msg)
	$branch = (& git rev-parse --abbrev-ref HEAD).Trim()
	fNote "branch: $branch"

	& git diff --quiet;          $dirtyTracked = ($LASTEXITCODE -ne 0)
	& git diff --cached --quiet; $dirtyStaged  = ($LASTEXITCODE -ne 0)
	$untracked = (& git ls-files --others --exclude-standard)
	$didStash = $false
	if ($dirtyTracked -or $dirtyStaged -or $untracked) {
		$before = @(& git stash list).Count
		fEcho_Clean "git stash push --include-untracked ..."
		fRun "git stash" "git" @("stash", "push", "--include-untracked", "-m", "auto-stash")
		$after = @(& git stash list).Count
		$didStash = ($after -gt $before)
	}

	& git rev-parse --abbrev-ref '@{u}' 2>$null | Out-Null
	$hasUpstream = ($LASTEXITCODE -eq 0)
	if ($hasUpstream) {
		## ff-only, like fRemoteSync and the Linux publisher: a diverged branch
		## should stop here, not get a fabricated merge pushed to it.
		fEcho_Clean "git pull --ff-only ..."
		fRun "git pull" $script:GitExe ($script:GitPre + @("pull", "--ff-only"))
	}
	if ($didStash) {
		fEcho_Clean "git stash pop ..."
		## A conflicting pop leaves the stash held and the tree half-merged.
		## Abort saying where the work is: a rerun with sync off would otherwise
		## build and commit a tree missing it, conflict markers and all.
		& git stash pop
		if ($LASTEXITCODE -ne 0) {
			fEcho_Clean
			fEcho "Your changes are still in the stash (git stash list)."
			fEcho "Resolve the conflicts, then: git stash drop"
			fEcho "Or start over:               git checkout -- . ; git stash pop"
			fDie "stash pop conflicted after the pull"
		}
	}

	fEcho_Clean "git add --all ..."
	fRun "git add" "git" @("add", "--all")

	& git diff --cached --quiet; $hasStaged = ($LASTEXITCODE -ne 0)
	if ($hasStaged) {
		if ($Msg) {
			fRun "git commit" "git" @("commit", "-m", $Msg)
			fEcho "OK: committed (`"$Msg`")"
		} else {
			& git commit
			if ($LASTEXITCODE -ne 0) { fDie "git commit failed or was aborted (empty message?)" }
			fEcho "OK: committed (via editor)"
		}
	} else {
		fNote "nothing to commit"
	}

	if (-not $hasUpstream) {
		fEcho_Clean "git push -u origin HEAD ..."
		fRun "git push" $script:GitExe ($script:GitPre + @("push", "-u", "origin", "HEAD"))
		fEcho "OK: pushed $branch (upstream set)"
	} else {
		$ahead = (& git log '@{u}..' --oneline)
		if ($ahead) {
			fEcho_Clean "git push origin ..."
			fRun "git push" $script:GitExe ($script:GitPre + @("push", "origin"))
			fEcho "OK: pushed $branch"
		} else {
			fNote "up to date with upstream; nothing to push"
		}
	}
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Entry point

function fMain {
	Set-Location -LiteralPath $Root
	$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

	## Gate mode: lint + build + tests + smoke, then exit. The local verification
	## for a push. Nothing is staged or published.
	if ($Gate) {
		$miss = fToolchainMissing
		if ($miss) {
			if ($BuildStrict) { fDie "gate: $miss" }
			fSection "$AppName gate: SKIPPED"
			fWarn "toolchain missing: $miss"
			fEcho_Clean
			return
		}
		fSection "Gate 1/4  Lint"
		if ($NoFmt) { fNote "lint skipped (-NoFmt)" }
		else { fLint }
		fSection "Gate 2/4  Build"
		fBuild
		fSection "Gate 3/4  Tests"
		fTests
		fSection "Gate 4/4  Smoke"
		fSmoke -Exe (fPrepInPlaceSmoke) -RuntimeBin $MingwBin
		fGuiSmoke -Exe (fPrepInPlaceSmoke) -RuntimeBin $MingwBin
		fSection "$AppName gate: PASSED."
		fEcho_Clean
		return
	}

	## Resolve the publish commit message: -Message wins, then the same helper the
	## Linux side asks when unattended; interactive runs capture it at the preflight prompt below.
	$publishMsg = ""
	if     ($Message)     { $publishMsg = $Message }
	elseif ($Unattended)  { $publishMsg = fAutoMessage }

	## Preflight summary.
	$toolMiss = fToolchainMissing
	fEcho_Clean
	fEcho_Clean "$AppName Windows CI/CD (native MSYS2/mingw64)"
	fEcho_Clean
	fEcho_Clean "Repo root ...: $Root"
	fEcho_Clean "Version .....: $(fVersion)  (source/meson.build)"
	fEcho_Clean "Toolchain ...: $(if ($toolMiss) { "MISSING - $toolMiss" } else { "mingw64 GTK toolchain OK" })"
	fEcho_Clean "Remote sync .: $(if ($NoSync) { '(skipped)' } else { 'fetch + fast-forward check' })"
	fEcho_Clean "Lint ........: $(if ($NoFmt) { '(skipped)' } else { 'every checker in lint.bash, check-only' })"
	fEcho_Clean "Build .......: $(if ($NoBuild) { '(skipped)' } else { 'meson + ninja, native mingw64' })"
	fEcho_Clean "Tests .......: $(if ($NoBuild) { '(skipped)' } else { 'meson test suite + --version and GUI smokes' })"
	fEcho_Clean "Packages ....: $(if ($NoPack -or $NoBuild) { '(skipped)' } else { 'portable single-exe (Enigma Virtual Box)' })"
	fEcho_Clean "Signing .....: $(if ($NoSign) { '(skipped)' } elseif (fSignConfigured) { 'Authenticode (configured)' } else { '(none configured)' })"
	fEcho_Clean "Dogfood .....: $(if ($NoDogfood -or $NoBuild -or $NoPack) { '(skipped)' } else { $DogfoodExe })"
	if ($NoPublish)          { fEcho_Clean "Publish .....: (skipped)" }
	elseif ($publishMsg)     { fEcho_Clean "Publish .....: commit + push current branch (hands-off: `"$publishMsg`")" }
	else                     { fEcho_Clean "Publish .....: commit + push current branch (will prompt; blank = editor)" }
	fEcho_Clean
	fEcho_Clean "Fail-fast: any error aborts before the next stage."
	fEcho_Clean

	## Capture the commit message up front so the run finishes unattended. Ctrl+C
	## here aborts on the common (publish) path.
	## No tty means Read-Host returns immediately at EOF and the run would sail
	## on to a git commit with no editor and nothing to type into it.
	if (-not $Unattended -and -not $NoPublish -and -not $publishMsg) {
		if ([Console]::IsInputRedirected) {
			throw "Publish needs a commit message: pass -Message, or -Yes, or run interactively."
		}
		$m = Read-Host "Publish commit message (blank = editor; Ctrl+C aborts)"
		$script:WasLastEchoBlank = $false
		if ($m) { $publishMsg = $m }
	}

	## Start the transcript once past the preflight. A run without one is worth
	## saying out loud, since the log is where a failure gets read afterwards.
	New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
	## Same GFS rotation as the Linux logs, done before this run's file is open,
	## since Windows cannot rename a file in use.
	if (Test-Path -LiteralPath $MsysBash) {
		fMingw -Quiet "source cicd/utility/include/gfs-rotate.bash && gfs_rotate cicd/artifacts/lint-win run log"
		if ($script:MingwRc -ne 0) { fWarn "log rotation failed (exit $($script:MingwRc)); old logs kept" }
	}
	try {
		Start-Transcript -LiteralPath (Join-Path $LogDir "run_$stamp.log") | Out-Null
		$script:Transcribing = $true
	} catch {
		fWarn "no transcript for this run: $($_.Exception.Message)"
	}

	## Stage 0: remote sync.
	fSection "0  Remote sync"
	if ($NoSync) { fNote "remote sync skipped" }
	else { fRemoteSync }

	## Stage 1: lint (check-only; the shared helper warn-skips without cppcheck).
	fSection "1  Lint"
	if ($NoFmt) { fNote "lint skipped (-NoFmt)" }
	elseif ($toolMiss) { fWarn "lint SKIPPED: $toolMiss" }
	else { fLint }

	## Stages 2-4: build, smoke, stage. Guarded as one block: a missing toolchain
	## warn-skips them all (so sync + publish still run) unless -BuildStrict.
	if ($NoBuild) {
		fSection "2  Build"; fNote "build skipped (-NoBuild)"
	} elseif ($toolMiss) {
		fSection "2  Build"
		if ($BuildStrict) { fDie "build: $toolMiss" }
		fWarn "build/smoke/stage SKIPPED: $toolMiss"
	} else {
		fSection "2  Debug build"
		fBuild
		fSection "3  Tests"
		fTests
		fSmoke -Exe (fPrepInPlaceSmoke) -RuntimeBin $MingwBin
		fGuiSmoke -Exe (fPrepInPlaceSmoke) -RuntimeBin $MingwBin
		fSection "4  Stage"
		fStage
	}

	## Stage 5: packages - the portable single-exe (Enigma Virtual Box), then sign it
	## (no-op unless a cert is configured). A missing packer or bundle warn-skips.
	fSection "5  Packages"
	if ($NoPack -or $NoBuild) { fNote "pack skipped" }
	elseif (-not (Test-Path -LiteralPath (Join-Path $StageDir "app\$ExeName.exe"))) { fWarn "no staged bundle; pack skipped" }
	else { fPack }

	## Stage 6: dogfood - the packed single exe into the synced by-self folder. Needs
	## the packer's output, so it follows stage 5; no build/pack means no exe to drop.
	fSection "6  Dogfood"
	if ($NoDogfood) { fNote "dogfood skipped (-NoDogfood)" }
	elseif ($NoPack -or $NoBuild) { fNote "dogfood skipped (no pack)" }
	else { fDogfood }

	## Stage 7: publish.
	fSection "7  Publish"
	if ($NoPublish) { fNote "publish skipped" }
	else { fPublish -Msg $publishMsg }

	fSection "$AppName Windows CI/CD: done."
	fEcho_Clean
}

try {
	fMain
} finally {
	if ($script:Transcribing) { Stop-Transcript | Out-Null }
}


##	History:
##		- 2026-09-26: Stage 3 and the gate also run the installer's in-use check and
##		  a GUI launch smoke of the built exe.
##		- 2026-09-15: The gate runs the meson suite too, between build and smoke.
##		- 2026-09-15: Stage 3 runs the meson suite ahead of the --version smoke.
##		- 2026-08-04: Optional Authenticode signing of the packed exe (stage 5, after
##		  pack), configured entirely by env (NEMO_SIGN_*); no-op/warn when unconfigured
##		  or signtool is absent, so unsigned dev builds still ship. -NoSign to skip.
##		- 2026-08-04: Dogfood is now the single packed exe, dropped as one file in the
##		  by-self win64 folder (its own stage 6, after pack); the old robocopy of the
##		  app\+mingw64\ bundle into a nemo-anywhere\ subfolder is gone.
##		- 2026-08-02: Stage 1 wired: check-only cppcheck lint over the changed C
##		  files (cicd/utility/lint-c.bash); the gate runs it too (now 3 steps).
##		- 2026-07-30: Created. Windows-NATIVE companion to cicd.bash: MSYS2/mingw64
##		  meson+ninja build, native --version smoke, self-contained runtime bundle
##		  staged (cicd/win/stage-native.bash) and dogfooded to the synced folder,
##		  native git sync + publish (rar step dropped). No container/wine. Profiler,
##		  Linux packages, and the rar archive don't apply here; NSIS packaging is a
##		  later stage.
