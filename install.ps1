#!/usr/bin/env pwsh

##	Purpose:
##		- One-liner installer for Nemo Anywhere, standalone on every platform it
##		  covers: Windows, Linux, BSD, WSL, and macOS once there is a build.
##		  Fetches a release build, verifies its checksum, and installs it as a
##		  self-contained folder plus a menu entry and a name on PATH.
##		- Full parity with install.bash - same options, same plan, same result.
##		  Either script alone does the whole job. This one runs on Windows
##		  PowerShell 5.1 or PowerShell 7; install.bash needs nothing but a shell.
##		- Idempotent: reinstalling replaces the folder in place, and -Uninstall
##		  removes exactly what was installed. Nothing is touched before the plan
##		  is printed and confirmed.
##	Syntax:
##		& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/yottacore/nemo-anywhere/main/install.ps1'))) [options]
##			-Release dev|stable     which release to take (default: stable)
##			-Target  user|system    where to install (default: user)
##			-From    PATH|URL       install this archive instead of a release
##			-AllowUnverified        install a release that has no checksums file
##			-Uninstall              remove an existing install
##			-Yes                    don't ask before making changes
##			-Version                print the installer's version
##			-Help                   list these options
##		Windows installs to %LOCALAPPDATA%\Programs\Nemo Anywhere (user) or
##		C:\Program Files\Nemo Anywhere (system, needs an elevated shell). Unix
##		installs to ~/.local/share/nemo-anywhere (user) or /opt/nemo-anywhere
##		(system, via sudo; /usr/local/nemo-anywhere on BSD).
##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

<#
.SYNOPSIS
	Installs Nemo Anywhere from a published release.

.DESCRIPTION
	Downloads the right build for this machine, verifies its checksum, and
	installs it as a self-contained folder plus a launcher and a name on PATH.
	Reinstalling replaces an existing copy in place. Nothing is changed until the
	plan has been printed and accepted.

	Covers Windows, Linux, BSD, WSL and macOS. install.bash is the same installer
	for anyone who would rather not need PowerShell.

.PARAMETER Release
	Which release to take: stable (the latest full release) or dev (the newest
	release including prereleases). Defaults to stable. With no stable release
	yet, stable takes the newest prerelease and says so in the plan.

.PARAMETER Target
	user installs for the current account only and needs no elevation. system
	installs for everyone and does need it. Defaults to user.

.PARAMETER From
	Install this archive - a path or a URL - instead of fetching a release.
	It is not checked against a checksums file.

.PARAMETER AllowUnverified
	Install a release that publishes no checksums file. Without it the installer
	stops there, even with -Yes. A checksums file that is there is still checked.

.PARAMETER Uninstall
	Remove an existing install, including its launcher and PATH entry. Settings
	are left alone.

.PARAMETER Yes
	Proceed without asking. Required when nothing is there to answer the prompt.

.PARAMETER Version
	Print the installer's version and exit.

.PARAMETER Help
	List the options and exit. Get-Help cannot reach a script run as a one-liner,
	so this is the way to see them there.

.EXAMPLE
	& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/yottacore/nemo-anywhere/main/install.ps1')))

	The one-liner: installs the latest stable release for the current user.

.EXAMPLE
	./install.ps1 -Release dev -Target system -Yes

	Installs the newest prerelease for every user, without prompting.

.EXAMPLE
	./install.ps1 -Uninstall

	Removes the user install this script made.
#>
[CmdletBinding()]
param(
	[ValidateSet("dev", "stable")][string]$Release = "stable",
	[ValidateSet("user", "system")][string]$Target = "user",
	[string]$From = "",
	[switch]$AllowUnverified,
	[switch]$Uninstall,
	[switch]$Yes,
	[switch]$Version,
	[switch]$Help
)


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Configuration

$Repo    = "yottacore/nemo-anywhere"
$InstallerVersion = "1.3.0"
$AppName = "Nemo Anywhere"
$ExeName = "nemo-anywhere"

## `exit` is only safe when this really is its own process. The one-liner runs
## the downloaded text inside the caller's shell, where an exit closes their
## window along with the error it just printed. So failures travel as an
## exception, and only a real script file turns that into an exit code.
$runningAsScriptFile = -not [string]::IsNullOrEmpty($MyInvocation.MyCommand.Path)
$state = @{ failed = $false; work = $null }


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Output helpers

## Same shape as install.bash: fEcho prints a bracketed status line,
## fEcho_Clean a plain one.
function fEcho       { param([string]$Msg) Write-Host "[ $Msg ]" }
function fEcho_Clean { param([string]$Msg = "") Write-Host $Msg }
function fWarn       { param([string]$Msg) Write-Host "WARNING: $Msg" -ForegroundColor Yellow }
function fFail {
	param([string]$Msg, [string[]]$Hints = @())
	Write-Host ""
	Write-Host "FAILED: $Msg" -ForegroundColor Red
	foreach ($hint in $Hints) { Write-Host "  $hint" }
	Write-Host ""
	## Carries nothing to print; fFail has said it all.
	throw (New-Object System.OperationCanceledException "installer-abort")
}

function fInnerMessage {
	param($ErrorRecord)
	$ex = $ErrorRecord.Exception
	while ($ex.InnerException) { $ex = $ex.InnerException }
	return $ex.Message
}

## Access denied and file in use are the two that happen, and they need
## opposite advice.
function fFileError {
	param($ErrorRecord, [string]$What, [string]$Path)
	$ex = $ErrorRecord.Exception
	while ($ex.InnerException) { $ex = $ex.InnerException }
	if ($ex -is [System.UnauthorizedAccessException]) {
		if ($os -eq "windows" -and $Target -eq "system") { $hint = "Re-run from an elevated PowerShell (Run as administrator), or use -Target user." }
		elseif ($os -eq "windows") { $hint = "Check the folder is not read-only, and that antivirus is not blocking it." }
		else { $hint = "Check who owns it: ls -ld $Path" }
		fFail "${What} - permission denied: ${Path}" @($hint)
	}
	if ($ex.Message -match "used by another process") {
		fFail "${What} - ${Path} is in use" @("Close every ${AppName} window, and any window sitting in that folder, then run this again.")
	}
	fFail "${What} - $($ex.Message)" @("Path: ${Path}")
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Functions - both platforms

function fConfirm {
	if ($Yes) { return }
	## Nothing to ask at, so never assume yes: at EOF Read-Host hands back $null,
	## and a $null -notmatch test is false, which would read as a yes.
	if ([Console]::IsInputRedirected) {
		fFail "nothing to read a confirmation from - re-run with -Yes to accept the plan above"
	}
	$answer = "$(Read-Host 'Proceed? [y/N]')"
	if ($answer -notmatch '^[yY]') {
		fEcho_Clean ""
		fEcho "Nothing changed."
		fEcho_Clean ""
		throw (New-Object System.OperationCanceledException "installer-declined")
	}
}

function fHelp {
	fEcho_Clean ""
	fEcho_Clean "${AppName} installer."
	fEcho_Clean ""
	fEcho_Clean "  & ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/${Repo}/main/install.ps1'))) [options]"
	fEcho_Clean ""
	fEcho_Clean "    -Release dev|stable     which release to take (default: stable)"
	fEcho_Clean "    -Target  user|system    where to install (default: user)"
	fEcho_Clean "    -From    PATH|URL       install this archive instead of a release"
	fEcho_Clean "    -AllowUnverified        install a release that has no checksums file"
	fEcho_Clean "    -Uninstall              remove an existing install"
	fEcho_Clean "    -Yes                    don't ask before making changes"
	fEcho_Clean "    -Version                the installer's version"
	fEcho_Clean "    -Help                   this text"
	fEcho_Clean ""
	fEcho_Clean "  The OS and architecture are detected. With no stable release yet,"
	fEcho_Clean "  stable takes the newest prerelease and says so in the plan."
	fEcho_Clean ""
	fEcho_Clean "  A release download is checked against the release's checksums file."
	fEcho_Clean "  With no checksums file it stops, even with -Yes, unless"
	fEcho_Clean "  -AllowUnverified is given. A -From archive is not checked."
	fEcho_Clean ""
}

## Same key as install.bash: numbers zero-padded so 1.10 sorts above 1.9, and a
## release above its own prereleases. [version] can't parse a prerelease part.
function fVersionKey {
	param([string]$Tag)
	$ver = $Tag -replace '^v', ''
	$pre = ""
	$dash = $ver.IndexOf('-')
	if ($dash -ge 0) { $pre = $ver.Substring($dash + 1); $ver = $ver.Substring(0, $dash) }
	$parts = @($ver.Split('.')) + @('0', '0', '0')
	$key = ""
	for ($i = 0; $i -lt 3; $i++) {
		$num = 0
		[void][int]::TryParse($parts[$i], [ref]$num)
		$key += "{0:D6}." -f $num
	}
	if (-not $pre) { return "${key}1" }
	$padded = [regex]::Replace($pre, '[0-9]+', { param($m) "{0:D6}" -f [int64]$m.Value })
	return "${key}0${padded}"
}

## stable = the newest release with no prerelease part, or the newest prerelease
## when nothing stable exists yet; dev = the newest of any kind. Ranked by
## version rather than by the order the API lists them in, and never through
## releases/latest, which 404s on a repo that has only prereleases.
function fResolveTag {
	try {
		## Assigned first: irm passes a JSON array down the pipe as one object.
		$releases = Invoke-RestMethod -Uri "https://api.github.com/repos/${Repo}/releases?per_page=100" -UseBasicParsing
	} catch {
		fFail "could not list the releases of ${Repo} ($(fInnerMessage $_))" @("Check the connection. GitHub also limits anonymous requests to 60 an hour.")
	}
	$tags = @(foreach ($rel in @($releases)) { if ($rel.tag_name) { [string]$rel.tag_name } })
	if ($tags.Count -eq 0) { return $null }
	## Ordinal, as install.bash sorts under LC_ALL=C. Sort-Object compares by
	## culture and ignores case, so the two could pick different tags.
	[string[]]$keys   = @($tags | ForEach-Object { fVersionKey $_ })
	[string[]]$ranked = $tags
	[Array]::Sort($keys, $ranked, [StringComparer]::Ordinal)
	[Array]::Reverse($ranked)
	$stable = @($ranked | Where-Object { -not $_.Contains('-') })
	if ($Release -eq "stable" -and $stable.Count -gt 0) { return $stable[0] }
	return $ranked[0]
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Functions - Windows

## Names of the running processes executing something inside the given folder -
## Windows can't replace files that are still open.
function fHoldersOf {
	param([string]$Folder)
	## Win32_Process exposes ExecutablePath for processes whose handle Get-Process
	## cannot open (protected or cross-session), so a running instance is not read
	## as absent. Match on the folder plus a separator so C:\foo doesn't hit
	## C:\foobar.
	$full  = [System.IO.Path]::GetFullPath($Folder).TrimEnd('\') + '\'
	$names = @()
	foreach ($proc in (Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)) {
		$path = $proc.ExecutablePath
		if (-not $path) { continue }
		## GetFullPath has already long-formed the folder, but a process started
		## from an 8.3 short path still reports the short one, and then a real
		## holder reads as absent. A '~' is what marks a short name.
		if ($path.Contains('~')) { $path = [System.IO.Path]::GetFullPath($path) }
		if ($path.StartsWith($full, [StringComparison]::OrdinalIgnoreCase)) {
			$names += [System.IO.Path]::GetFileName($path)
		}
	}
	return @($names | Sort-Object -Unique)
}

## The session bus the app starts lives in the install folder and outlives the
## window by a few seconds, so closing the app is not enough on its own. Wait it
## out, then say what is actually holding the folder rather than blaming the app.
function fWaitUntilFree {
	param([string]$Folder, [int]$Seconds = 10)
	for ($i = 0; $i -lt $Seconds; $i++) {
		## @() on the way out of every call: an empty array unrolls to $null on
		## return, and reading .Count off that throws under strict mode.
		$holders = @(fHoldersOf $Folder)
		if ($holders.Count -eq 0) { return @() }
		if ($i -eq 0) { fEcho_Clean "waiting for $($holders -join ', ') to finish with ${Folder}" }
		Start-Sleep -Seconds 1
	}
	return (fHoldersOf $Folder)
}

function fMakeShortcut {
	param([string]$LinkPath, [string]$TargetPath, [string]$WorkDir)
	$shell = New-Object -ComObject WScript.Shell
	$link  = $shell.CreateShortcut($LinkPath)
	$link.TargetPath       = $TargetPath
	$link.WorkingDirectory = $WorkDir
	$link.IconLocation     = $TargetPath
	$link.Description      = $AppName
	$link.Save()
}

## PATH edits go through the registry, NOT [Environment]::SetEnvironmentVariable:
## that reads the value expanded and writes it back literally, which would bake
## %SystemRoot% and friends into the machine PATH permanently. Read raw, write
## back as an expandable string, and leave every other entry untouched.
function fPathKey {
	param([string]$Scope)
	if ($Scope -eq "Machine") { return "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" }
	return "HKCU:\Environment"
}

function fPathRead {
	param([string]$Scope)
	$key = Get-Item -LiteralPath (fPathKey $Scope)
	return [string]$key.GetValue("Path", "", [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
}

function fPathWrite {
	param([string]$Scope, [string]$Value)
	Set-ItemProperty -LiteralPath (fPathKey $Scope) -Name "Path" -Value $Value -Type ExpandString
	## Without this, a newly opened console keeps inheriting the old PATH until
	## the next sign-in. Best effort - the install is fine either way.
	try {
		if (-not ("NemoEnvBroadcast" -as [type])) {
			Add-Type -Namespace "" -Name "NemoEnvBroadcast" -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Auto)]
public static extern System.IntPtr SendMessageTimeout(System.IntPtr hWnd, uint Msg, System.IntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out System.UIntPtr lpdwResult);
"@
		}
		$ignored = [UIntPtr]::Zero
		[NemoEnvBroadcast]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [IntPtr]::Zero, "Environment", 2, 5000, [ref]$ignored) | Out-Null
	} catch {
		fWarn "already-running programs will not see the new PATH until you sign in again"
	}
}

function fPathContains {
	param([string]$Scope, [string]$Dir)
	$current = fPathRead $Scope
	if (-not $current) { return $false }
	return @($current -split ';' | Where-Object { $_.TrimEnd('\') -ieq $Dir.TrimEnd('\') }).Count -gt 0
}

## A trailing separator means nothing to Windows, but it was there before the
## install touched the value, so an uninstall has to hand back what it found.
function fPathAdd {
	param([string]$Scope, [string]$Dir)
	if (fPathContains $Scope $Dir) { return $false }
	$current = fPathRead $Scope
	$tail = if ($current -and $current.EndsWith(';')) { ';' } else { '' }
	$head = $current.Substring(0, $current.Length - $tail.Length)
	fPathWrite $Scope $(if ($current) { "${head};${Dir}${tail}" } else { $Dir })
	return $true
}

function fPathRemove {
	param([string]$Scope, [string]$Dir)
	if (-not (fPathContains $Scope $Dir)) { return $false }
	## Empty entries are kept, a trailing one included, so the value comes back
	## as it was found.
	$kept = @((fPathRead $Scope) -split ';' | Where-Object { -not $_ -or ($_.TrimEnd('\') -ine $Dir.TrimEnd('\')) })
	fPathWrite $Scope ($kept -join ';')
	return $true
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Functions - unix

## Every unix file step runs through here, so the privileged and plain paths
## stay one code path - sudo only prefixes the same argv. Arguments arrive as
## one array because a bare -R would otherwise bind as a parameter name.
function fSh {
	param([Parameter(Mandatory)][string[]]$Argv, [switch]$Soft)
	## Typed and assigned directly: taking the tail as an if-statement's output
	## would unroll a lone element back to a string, and splatting a string
	## hands the command one argument per character.
	[string[]]$rest = @()
	$exe = $Argv[0]
	if ($Argv.Count -gt 1) { $rest = $Argv[1..($Argv.Count - 1)] }
	if ($priv) { $rest = @($exe) + $rest; $exe = "sudo" }
	if ($Soft) { & $exe @rest 2>$null } else { & $exe @rest }
	if ($LASTEXITCODE -ne 0 -and -not $Soft) { fFail "$($Argv -join ' ') failed (exit ${LASTEXITCODE})" }
}

## Where a symlink points, or nothing when the path is not one.
function fLinkTarget {
	param([string]$Path)
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
	if (-not $item -or $item.LinkType -ne "SymbolicLink") { return $null }
	return @($item.Target)[0]
}

## Write the desktop launcher, preferring the one shipped in the prefix and
## rewriting Exec/Icon to absolute paths (nothing else knows where we landed).
function fInstallLauncher {
	$shipped = Join-Path $prefix "share/applications/${ExeName}.desktop"
	$icon    = ""
	foreach ($candidate in @(
		(Join-Path $prefix "share/icons/hicolor/scalable/apps/${ExeName}.svg"),
		(Join-Path $prefix "share/icons/hicolor/48x48/apps/${ExeName}.png"),
		(Join-Path $prefix "share/pixmaps/${ExeName}.png")
	)) { if (Test-Path -LiteralPath $candidate) { $icon = $candidate; break } }
	if (-not $icon) { $icon = $ExeName }

	$exec = "${prefix}/bin/${ExeName}"
	$tmp  = Join-Path $work "${ExeName}.desktop"
	if (Test-Path -LiteralPath $shipped) {
		## Line by line rather than -replace: a prefix containing a $ would be
		## read as a capture reference in the replacement string.
		$entry = foreach ($line in (Get-Content -LiteralPath $shipped)) {
			if     ($line -like "Exec=*")    { "Exec=${exec} %U" }
			elseif ($line -like "TryExec=*") { "TryExec=${exec}" }
			elseif ($line -like "Icon=*")    { "Icon=${icon}" }
			else                             { $line }
		}
	} else {
		$entry = @(
			"[Desktop Entry]"
			"Type=Application"
			"Name=${AppName}"
			"Comment=Browse the file system"
			"Exec=${exec} %U"
			"Icon=${icon}"
			"Terminal=false"
			"Categories=System;FileTools;FileManager;"
			"MimeType=inode/directory;"
		)
	}
	Set-Content -LiteralPath $tmp -Value $entry

	fSh @("mkdir", "-p", $appDir)
	fSh @("cp", $tmp, $launcher)
	fSh @("chmod", "644", $launcher)
}

## Desktop caches only matter to the menu showing up promptly - never fatal.
function fRefreshCaches {
	if (Get-Command update-desktop-database -ErrorAction SilentlyContinue) {
		fSh @("update-desktop-database", $appDir) -Soft
	}
	$icons = Join-Path $prefix "share/icons/hicolor"
	if ((Test-Path -LiteralPath $icons) -and (Get-Command gtk-update-icon-cache -ErrorAction SilentlyContinue)) {
		fSh @("gtk-update-icon-cache", "-qtf", $icons) -Soft
	}
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Script entry point

## Everything runs from here, so preferences and strict mode last only for the
## run, the temp folder goes whatever happens, and a failure never reaches the
## caller's shell as an exit.
function fMain {
	fEcho_Clean ""
	fEcho_Clean "${AppName} installer"

	## $IsWindows and friends arrived with PowerShell 6; Windows PowerShell 5.1
	## predates them and is Windows by definition. Unix flavour comes from uname,
	## since .NET reports BSD as neither Linux nor macOS.
	if ($PSVersionTable.PSEdition -eq "Desktop" -or $IsWindows) {
		$os = "windows"
	} else {
		$uname = "$(& uname -s)".ToLowerInvariant()
		if     ($uname -like "linux*")  { $os = "linux" }
		elseif ($uname -like "*bsd*" -or $uname -like "dragonfly*") { $os = "bsd" }
		elseif ($uname -like "darwin*") { fFail "there is no macOS build yet - the .app bundle gets installed from here once there is one" }
		else                            { fFail "unsupported OS: ${uname}" }
	}

	## 5.1 inherits .NET Framework's TLS default, which on older Windows is still
	## TLS 1.0, and github.com refuses that. The symptom would be an unhelpful
	## "underlying connection was closed".
	if ($PSVersionTable.PSEdition -eq "Desktop") {
		try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072 } catch { $null = $_ }
	}

	## Architecture: the OS's, not the process's, so a 32-bit shell still gets the
	## right build. Read from the environment and uname rather than
	## [RuntimeInformation], which needs a newer .NET than 5.1 may have.
	if ($os -eq "windows") {
		$rawArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
	} else {
		$rawArch = "$(& uname -m)"
	}
	$arch = switch -Regex ($rawArch) {
		"^(AMD64|x86_64|amd64)$"  { "x86_64" }
		"^(ARM64|aarch64|arm64)$" { "arm64" }
		default                   { fFail "unsupported architecture: ${rawArch}" }
	}

	## GUI package layout, both platforms: the whole folder in one place, reached by
	## a menu entry and a name on PATH (a file manager gets started both ways).
	$priv = $false
	if ($os -eq "windows") {
		if ($Target -eq "system") {
			## ProgramW6432 is the 64-bit folder even from a 32-bit shell.
			$programFiles = if ($env:ProgramW6432) { $env:ProgramW6432 } else { $env:ProgramFiles }
			$prefix    = Join-Path $programFiles $AppName
			$menuDir   = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
			$pathScope = "Machine"
			$identity  = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
			if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
				fFail "-Target system needs an elevated shell - re-run this from 'Run as administrator', or use -Target user"
			}
		} else {
			$prefix    = Join-Path $env:LOCALAPPDATA "Programs\$AppName"
			$menuDir   = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
			$pathScope = "User"
		}
		$shortcut = Join-Path $menuDir "${AppName}.lnk"
		$exePath  = Join-Path $prefix "${ExeName}.exe"
		$leafName = $AppName
	} else {
		$dataHome = if ($env:XDG_DATA_HOME) { $env:XDG_DATA_HOME } else { Join-Path $HOME ".local/share" }
		if ($Target -eq "system") {
			$prefix = if ($os -eq "bsd") { "/usr/local/${ExeName}" } else { "/opt/${ExeName}" }
			$appDir = "/usr/local/share/applications"
			$binDir = "/usr/local/bin"
			## Privileged steps go through sudo; not needed when already root.
			if ("$(& id -u)" -ne "0") {
				if (-not (Get-Command sudo -ErrorAction SilentlyContinue)) {
					fFail "-Target system needs root, and sudo was not found - re-run as root"
				}
				$priv = $true
			}
		} else {
			$prefix = Join-Path $dataHome $ExeName
			$appDir = Join-Path $dataHome "applications"
			$binDir = Join-Path $HOME ".local/bin"
		}
		$launcher = Join-Path $appDir "${ExeName}.desktop"
		$symlink  = Join-Path $binDir $ExeName
		$exePath  = "${prefix}/bin/${ExeName}"
		$leafName = $ExeName
		if (-not $prefix.StartsWith("/")) { fFail "refusing to touch a non-absolute prefix: ${prefix}" }
	}

	## Guard every destructive path: an unexpected prefix must never reach a delete.
	if ((Split-Path -Leaf $prefix) -ne $leafName) { fFail "refusing to touch a folder not named '${leafName}': ${prefix}" }


	#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
	# Uninstall

	if ($Uninstall) {
		$havePrefix = Test-Path -LiteralPath $prefix

		fEcho_Clean ""
		fEcho "Uninstall plan"
		if ($os -eq "windows") {
			$haveShortcut = Test-Path -LiteralPath $shortcut
			$havePath     = fPathContains $pathScope $prefix
			$haveAnything = $havePrefix -or $haveShortcut -or $havePath
			fEcho_Clean ("Folder ....: {0}{1}" -f $prefix,   $(if ($havePrefix)   { "" } else { "   (not present)" }))
			fEcho_Clean ("Shortcut ..: {0}{1}" -f $shortcut, $(if ($haveShortcut) { "" } else { "   (not present)" }))
			fEcho_Clean ("PATH ......: {0} ({1}){2}" -f $prefix, $pathScope, $(if ($havePath) { "" } else { "   (not present)" }))
		} else {
			$haveLauncher = Test-Path -LiteralPath $launcher
			$linkTarget   = fLinkTarget $symlink
			$haveAnything = $havePrefix -or $haveLauncher -or $linkTarget
			fEcho_Clean ("Prefix ....: {0}{1}" -f $prefix,   $(if ($havePrefix)   { "" } else { "   (not present)" }))
			fEcho_Clean ("Launcher ..: {0}{1}" -f $launcher, $(if ($haveLauncher) { "" } else { "   (not present)" }))
			fEcho_Clean ("Symlink ...: {0}{1}" -f $symlink,  $(if ($linkTarget)   { "" } else { "   (not present)" }))
			if ($priv) { fEcho_Clean "Privileges : sudo (system target)" }
		}

		if (-not $haveAnything) {
			fEcho_Clean ""
			fEcho "Nothing installed here. Nothing to do."
			fEcho_Clean ""
			return
		}

		fEcho_Clean ""
		fConfirm

		fEcho_Clean ""
		fEcho "Removing"
		if ($os -eq "windows") {
			if ($havePrefix) {
				$holders = @(fWaitUntilFree $prefix)
				if ($holders.Count -gt 0) { fFail "$($holders -join ', ') still running from ${prefix} - close it and try again" }
			}
			if ($haveShortcut) { Remove-Item -LiteralPath $shortcut -Force; fEcho_Clean "removed ${shortcut}" }
			if ($havePrefix)   { Remove-Item -LiteralPath $prefix -Recurse -Force; fEcho_Clean "removed ${prefix}" }
			if (fPathRemove $pathScope $prefix) { fEcho_Clean "removed ${prefix} from the ${pathScope} PATH" }
			$settings = "%APPDATA%\${ExeName}"
		} else {
			## Only unlink a symlink that actually points into our prefix.
			if ($linkTarget) {
				if ($linkTarget.StartsWith("${prefix}/")) {
					fSh @("rm", "-f", $symlink); fEcho_Clean "removed ${symlink}"
				} else {
					fWarn "left ${symlink} alone - it points at ${linkTarget}, not our prefix"
				}
			}
			if ($haveLauncher) { fSh @("rm", "-f",  $launcher); fEcho_Clean "removed ${launcher}" }
			if ($havePrefix)   { fSh @("rm", "-rf", $prefix);   fEcho_Clean "removed ${prefix}" }
			if (Get-Command update-desktop-database -ErrorAction SilentlyContinue) {
				fSh @("update-desktop-database", $appDir) -Soft
			}
			$settings = "~/.config/${ExeName}"
		}

		fEcho_Clean ""
		fEcho "Uninstalled. Settings in ${settings} were left in place."
		fEcho_Clean ""
		return
	}


	#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
	# Resolve what to install

	$archiveExt = if ($os -eq "windows") { "zip" } else { "tar.gz" }

	$work = Join-Path ([System.IO.Path]::GetTempPath()) "${ExeName}-install-$([System.IO.Path]::GetRandomFileName())"
	New-Item -ItemType Directory -Path $work -Force | Out-Null
	$state.work = $work

	fEcho_Clean ""
	fEcho "Resolving"

	$sumsUrl = ""
	if ($From) {
		$downloadUrl  = $From
		$sourceDesc   = $From
		## Display only: a conventionally named archive still tells us its version.
		## Stop at the platform suffix, not at the first dash - a prerelease version
		## has one of its own (1.0.0-beta2), and cutting there reported a beta as the
		## release it precedes.
		$relVersion   = if ((Split-Path -Leaf $From) -match "^${ExeName}-(.+)-[^-]+-[^-.]+\.(?:zip|tar\.gz|tgz)$") { $Matches[1] } else { "" }
		$releaseDesc  = "local archive"
		$verifyDesc   = "no checksum (-From)"
	} else {
		$tag = fResolveTag
		if (-not $tag) { fFail "no release published yet for ${Repo}" }
		$relVersion = $tag -replace '^v', ''
		$asset      = "${ExeName}-${relVersion}-${os}-${arch}.${archiveExt}"
		$sumsAsset  = "${ExeName}-${relVersion}-sha256sums.txt"

		try {
			$tagInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/${Repo}/releases/tags/${tag}" -UseBasicParsing
		} catch {
			fFail "couldn't read release ${tag} ($($_.Exception.Message))"
		}
		## Read the property off the matched asset, not off the match expression -
		## strict mode throws on a property of nothing.
		$assetInfo   = $tagInfo.assets | Where-Object { $_.name -eq $asset     } | Select-Object -First 1
		$sumsInfo    = $tagInfo.assets | Where-Object { $_.name -eq $sumsAsset } | Select-Object -First 1
		$downloadUrl = if ($assetInfo) { $assetInfo.browser_download_url } else { "" }
		$sumsUrl     = if ($sumsInfo)  { $sumsInfo.browser_download_url  } else { "" }

		if (-not $downloadUrl) {
			fEcho_Clean ""
			fEcho_Clean "Release ${tag} has no build for ${os}-${arch}. It publishes:"
			$tagInfo.assets | ForEach-Object { fEcho_Clean "  $($_.name)" }
			fFail "no ${asset} in release ${tag}"
		}
		$sourceDesc  = $downloadUrl
		$releaseDesc = "${Release} ${relVersion}"
		if ($Release -eq "stable" -and $relVersion.Contains('-')) { $releaseDesc += "   (no stable release yet, so the newest prerelease)" }
		if ($sumsUrl) {
			$verifyDesc = "sha256, against ${sumsAsset}"
		} elseif ($AllowUnverified) {
			$verifyDesc = "UNVERIFIED - release publishes no checksums (-AllowUnverified)"
		} else {
			## Before the plan and the question, so -Yes alone never gets past it.
			fFail "release ${tag} publishes no ${sumsAsset}, so the download can't be checked - re-run with -AllowUnverified to install it anyway"
		}
	}

	fEcho_Clean ""
	fEcho "Plan"
	fEcho_Clean "Release ...: ${releaseDesc}"
	fEcho_Clean "Platform ..: ${os}-${arch}"
	fEcho_Clean "Download ..: ${sourceDesc}"
	fEcho_Clean "Verify ....: ${verifyDesc}"
	$replaces = if (Test-Path -LiteralPath $prefix) { "   (replaces the install already there)" } else { "" }
	if ($os -eq "windows") {
		fEcho_Clean ("Folder ....: {0}{1}" -f $prefix, $replaces)
		fEcho_Clean "Shortcut ..: ${shortcut}"
		fEcho_Clean "PATH ......: adds ${prefix} to the ${pathScope} PATH"
	} else {
		fEcho_Clean ("Prefix ....: {0}{1}" -f $prefix, $replaces)
		fEcho_Clean "Launcher ..: ${launcher}"
		fEcho_Clean "Symlink ...: ${symlink} -> ${exePath}"
		if ($priv) { fEcho_Clean "Privileges : sudo (system target)" }
	}

	fEcho_Clean ""
	fConfirm


	#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
	# Download and verify

	fEcho_Clean ""
	fEcho "Downloading"

	$archive = Join-Path $work "${ExeName}.${archiveExt}"
	if ($From -and (Test-Path -LiteralPath $From)) {
		Copy-Item -LiteralPath $From -Destination $archive
		fEcho_Clean "using local archive ${From}"
	} else {
		try {
			Invoke-WebRequest -Uri $downloadUrl -OutFile $archive -UseBasicParsing
		} catch {
			fFail "download failed ($($_.Exception.Message))"
		}
		fEcho_Clean "got $((Get-Item -LiteralPath $archive).Length) bytes"
	}

	## A downloaded archive carries a mark-of-the-web that would follow every file
	## out of it and have SmartScreen block the exe. pwsh on Linux has the cmdlet
	## too, but it throws there whatever -ErrorAction says, so ask the OS instead.
	if ($os -eq "windows") {
		Unblock-File -LiteralPath $archive -ErrorAction SilentlyContinue
	}

	if (-not $From -and $sumsUrl) {
		$sumsFile = Join-Path $work "sums.txt"
		try {
			Invoke-WebRequest -Uri $sumsUrl -OutFile $sumsFile -UseBasicParsing
		} catch {
			fFail "could not download ${sumsAsset} ($(fInnerMessage $_))"
		}
		$line = Get-Content -LiteralPath $sumsFile | Where-Object { $_ -match "[ *]$([regex]::Escape($asset))$" } | Select-Object -First 1
		if (-not $line) { fFail "${sumsAsset} has no line for ${asset}" }
		$expected = ($line -split '\s+')[0]
		$actual   = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
		if ($actual -ine $expected) { fFail "checksum mismatch - expected ${expected}, got ${actual}" }
		fEcho_Clean "sha256 verified"
	} else {
		fWarn "skipping checksum verification"
	}


	#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
	# Install

	fEcho_Clean ""
	fEcho "Installing"

	$unpacked = Join-Path $work "unpacked"
	New-Item -ItemType Directory -Path $unpacked -Force | Out-Null
	if ($os -eq "windows") {
		try {
			Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
		} catch {
			fFail "could not unpack the archive ($($_.Exception.Message))"
		}
	} else {
		## tar, not Expand-Archive: the unix packages are tarballs, and tar also
		## keeps the executable bits the launcher and symlink depend on.
		& tar -xzf $archive -C $unpacked
		if ($LASTEXITCODE -ne 0) { fFail "could not unpack the archive" }
	}

	## Archives carry one top-level folder; tolerate a flat one too.
	$tree    = $unpacked
	$entries = @(Get-ChildItem -LiteralPath $unpacked -Force)
	if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $tree = $entries[0].FullName }
	$stagedName = if ($os -eq "windows") { "${ExeName}.exe" } else { "bin/${ExeName}" }
	if (-not (Test-Path -LiteralPath (Join-Path $tree $stagedName))) {
		fFail "archive has no ${stagedName} - wrong or damaged package"
	}

	if ($os -eq "windows") {
		if (Test-Path -LiteralPath $prefix) {
			$holders = @(fWaitUntilFree $prefix)
			if ($holders.Count -gt 0) { fFail "$($holders -join ', ') still running from ${prefix} - close it and try again" }
		}
		try {
			New-Item -ItemType Directory -Path (Split-Path -Parent $prefix) -Force | Out-Null
		} catch {
			fFileError $_ "could not create the install folder" (Split-Path -Parent $prefix)
		}
		## Stage beside the prefix, then swap with same-volume renames. Copying the
		## new tree in before removing the old one means a cross-volume or disk-full
		## failure never leaves nothing installed; the old copy is dropped only once
		## the new one is in place.
		## Copied, never moved, out of the temp folder: a move on one volume keeps
		## the temp folder's permissions, so other accounts could not run a system
		## install. New files take the install folder's own.
		$staging = "${prefix}.new.${PID}"
		$backup  = "${prefix}.old.${PID}"
		if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
		if (Test-Path -LiteralPath $backup)  { Remove-Item -LiteralPath $backup  -Recurse -Force }
		try {
			Copy-Item -LiteralPath $tree -Destination $staging -Recurse -Force -ErrorAction Stop
		} catch {
			fFileError $_ "could not stage the new install" $staging
		}
		if (Test-Path -LiteralPath $prefix) {
			try {
				Move-Item -LiteralPath $prefix -Destination $backup -ErrorAction Stop
			} catch {
				## Something has the folder open that the process scan cannot see - a
				## scanner, a shell window sitting in it, a handle from another session.
				fFail "could not replace ${prefix} - something still has it open, close it and try again"
			}
		}
		try {
			Move-Item -LiteralPath $staging -Destination $prefix -ErrorAction Stop
		} catch {
			if (Test-Path -LiteralPath $backup) { Move-Item -LiteralPath $backup -Destination $prefix -ErrorAction Stop }
			throw
		}
		if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
		fEcho_Clean "folder installed at ${prefix}"

		try {
			New-Item -ItemType Directory -Path $menuDir -Force | Out-Null
			fMakeShortcut -LinkPath $shortcut -TargetPath $exePath -WorkDir $prefix
		} catch {
			fFileError $_ "could not write the Start Menu shortcut" $shortcut
		}
		fEcho_Clean "shortcut installed at ${shortcut}"

		if (fPathAdd $pathScope $prefix) {
			fEcho_Clean "added ${prefix} to the ${pathScope} PATH"
		} else {
			fEcho_Clean "${prefix} was already on the ${pathScope} PATH"
		}
	} else {
		## Same as install.bash: stage beside the prefix, on its own filesystem, so
		## the copy out of the temp folder is done before the old install is
		## touched. Then swap with same-filesystem renames, which cannot fail
		## partway and leave neither install.
		$parent  = Split-Path -Parent $prefix
		$staging = Join-Path $parent ".${ExeName}-install.${PID}"
		$backup  = "${prefix}.old.${PID}"
		fSh @("mkdir", "-p", $parent)
		fSh @("rm", "-rf", $staging, $backup)
		fSh @("cp", "-a", $tree, $staging)
		if ($Target -eq "system") {
			## Staged as the invoking user; a system prefix must not stay user-writable.
			fSh @("chown", "-R", "0:0", $staging) -Soft
			fSh @("chmod", "-R", "a+rX", $staging)
		}
		if (Test-Path -LiteralPath $prefix) { fSh @("mv", $prefix, $backup) }
		try {
			fSh @("mv", $staging, $prefix)
		} catch {
			if (Test-Path -LiteralPath $backup) { fSh @("mv", $backup, $prefix) -Soft }
			fSh @("rm", "-rf", $staging) -Soft
			throw
		}
		fSh @("rm", "-rf", $backup)
		fEcho_Clean "prefix installed at ${prefix}"

		fInstallLauncher
		fEcho_Clean "launcher installed at ${launcher}"

		fSh @("mkdir", "-p", $binDir)
		fSh @("ln", "-sfn", $exePath, $symlink)
		fEcho_Clean "symlink installed at ${symlink}"

		fRefreshCaches
	}



	#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
	# Done

	fEcho_Clean ""
	fEcho ("Installed {0} {1}" -f $AppName, $relVersion).TrimEnd()
	if ($os -eq "windows") {
		fEcho_Clean "Start it from the Start Menu, or type: ${ExeName}"
		fEcho_Clean "A new shell is needed before the PATH entry takes effect."
	} else {
		fEcho_Clean "Run it from the menu, or type: ${ExeName}"
		if (-not (@($env:PATH -split ':') -contains $binDir)) {
			fEcho_Clean "Note: ${binDir} is not on your PATH yet."
		}
	}
	fEcho_Clean "Uninstall with the same command plus -Uninstall."
	fEcho_Clean ""
}

& {
	Set-StrictMode -Version Latest
	$ErrorActionPreference = "Stop"
	$ProgressPreference    = "SilentlyContinue"   ## Invoke-WebRequest is far faster without the bar
	## Native tools report through their exit code, which fSh checks itself. Left on
	## (the 7.4 default) a non-zero exit would throw before that check is reached.
	$PSNativeCommandUseErrorActionPreference = $false
	try {
		if     ($Help)    { fHelp }
		elseif ($Version) { fEcho_Clean "${AppName} installer ${InstallerVersion}" }
		else              { fMain }
	} catch [System.OperationCanceledException] {
		## Already reported by fFail, or the plan was declined.
		$state.failed = $true
	} catch {
		Write-Host ""
		Write-Host "FAILED: $(fInnerMessage $_)" -ForegroundColor Red
		Write-Host ""
		$state.failed = $true
	} finally {
		if ($state.work) { Remove-Item -LiteralPath $state.work -Recurse -Force -ErrorAction SilentlyContinue }
	}
}
if ($state.failed -and $runningAsScriptFile) { exit 1 }


##	History:
##		- 2026-07-23 JC: Created (Windows half of the one-liner install; hands
##		  off to install.bash on the unix side).
##		- 2026-07-23 JC: Now installs on unix itself instead of handing off, so
##		  either installer alone covers every platform.
##		- 2026-09-19 JC: Dropped -Arch (always detected), added -Version and
##		  -Help, and stable now falls back to the newest prerelease.
##		- 2026-09-25 JC: Runs inside one function, so a failure or -Help no longer
##		  closes the window of a shell running the one-liner, and the temp folder
##		  goes on every exit. Unix installs swap in place like install.bash.
##		  Clearer errors for a failed GitHub request and for access denied or a
##		  file in use. TLS 1.2 and the architecture read so Windows PowerShell
##		  5.1 works, and tags sort the same way install.bash sorts them.
##		- 2026-10-01 JC: The Windows install folder is copied out of the temp
##		  folder rather than moved, so it takes the install folder's permissions
##		  and other accounts can run a system install.
##		- 2026-10-03 JC: A release with no checksums file stops the install,
##		  -Yes or not, unless -AllowUnverified is given.
