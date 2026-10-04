#!/usr/bin/env pwsh

##	Purpose:
##		- Check that an install.ps1 install that fails after its staging folder
##		  exists leaves nothing beside the install folder, and the old install
##		  where it was. Three ways out: the copy into staging fails partway, the
##		  old folder cannot be moved aside, and the final rename fails.
##		- On Windows the copy fails on a source file held open, and the old
##		  folder on a file held open inside it, which must not split the old
##		  install in two. The final rename, and on unix the move aside and the
##		  rename, are refused by a stand-in for the call. On unix the copy
##		  fails on a source file nobody can read.
##		- install.ps1's own functions are lifted out through the parser and its
##		  main body run on an archive made here, as a user install with the
##		  profile and temp folders pointed into a scratch folder. The system
##		  target is never run. Besides the stand-ins and the unix launcher, only
##		  the registry PATH read and write are swapped for a variable.
##		- Runs in cicd-win.ps1's test stage, and in the lint stage elsewhere.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-install-staging.ps1
##	Test ID: rjeqef3d

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets', '',
	Justification = 'The copy has to fail partway, on a source file held open while Copy-Item runs.')]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"
$PSNativeCommandUseErrorActionPreference = $false
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

$onWindows = $IsWindows
if (-not $onWindows -and "$(& id -u)" -eq "0") {
	## root reads the unreadable file, so the copy would not fail.
	Write-Host "[ install.ps1 staging check skipped: running as root ]"; exit 77
}

$installer = Join-Path $PSScriptRoot "../../install.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($installer, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: install.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

## Every function and the settings fMain reads, but not the entry block, which
## would run the installer with this script's arguments.
$settings = @("Repo", "InstallerVersion", "AppName", "ExeName", "state")
foreach ($stmt in $ast.EndBlock.Statements) {
	$isSetting = $stmt -is [System.Management.Automation.Language.AssignmentStatementAst] -and
		$stmt.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
		$settings -contains $stmt.Left.VariablePath.UserPath
	if ($isSetting -or $stmt -is [System.Management.Automation.Language.FunctionDefinitionAst]) {
		. ([scriptblock]::Create($stmt.Extent.Text))
	}
}
if (-not (Get-Command fMain -CommandType Function -ErrorAction SilentlyContinue)) {
	Write-Host "[ FAILED: install.ps1 has no function fMain ]"; exit 1
}

$script:fakePath = ""
function fPathRead {
	param([string]$Scope)
	$null = $Scope
	return $script:fakePath
}
function fPathWrite {
	param([string]$Scope, [string]$Value)
	$null = $Scope
	$script:fakePath = $Value
}

## $HOME cannot be redirected, and the unix symlink goes under it. Every case
## stops before then; this makes sure of it.
function fInstallLauncher { fFail "the install went on past the swap" }

$script:Release   = "stable"
$script:Target    = "user"
$script:NoVerify  = $false
$script:Uninstall = $false
$script:Yes       = $true
$script:case      = ""

## Stand-ins that fail one step on purpose and pass every other call through.
function Copy-Item {
	[CmdletBinding()]
	param([string]$LiteralPath, [string]$Destination, [switch]$Recurse, [switch]$Force)
	$held = $null
	if ($script:case -eq "copy" -and $Destination -like "*.new.*") {
		$held = [System.IO.File]::Open((Join-Path $LiteralPath "share\icons\icon.txt"), "Open", "Read", "None")
	}
	try {
		Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
	} finally {
		if ($held) { $held.Dispose() }
	}
}
$realRename = ${function:fRenameFolder}
function fRenameFolder {
	param([string]$From, [string]$To)
	if ($script:case -eq "swap" -and $From -like "*.new.*") { throw "rename refused" }
	& $realRename -From $From -To $To
}
$realSh = ${function:fSh}
function fSh {
	param([Parameter(Mandatory)][string[]]$Argv, [switch]$Soft)
	if ($Argv[0] -eq "cp" -and $Argv[-1] -like "*-install.*" -and $script:case -eq "copy") {
		& chmod 000 (Join-Path $Argv[-2] "share/icons/icon.txt")
	}
	if ($Argv[0] -eq "mv" -and $Argv[-1] -like "*.old.*" -and $script:case -eq "aside") { fFail "$($Argv -join ' ') refused" }
	if ($Argv[0] -eq "mv" -and $Argv[1] -like "*-install.*" -and $script:case -eq "swap") { fFail "$($Argv -join ' ') refused" }
	& $realSh -Argv $Argv -Soft:$Soft
}

$failures = 0
function fProblem {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

$root    = Join-Path ([System.IO.Path]::GetTempPath()) "test_install-staging_$(Get-Date -Format 'yyyyMMdd-HHmmssff')"
$temp    = Join-Path $root "temp"
$exeRel  = if ($onWindows) { "nemo-anywhere.exe" } else { "bin/nemo-anywhere" }
$pack    = Join-Path $root "pack/nemo-anywhere-0.0.0-$(if ($onWindows) { 'windows' } else { 'linux' })-x86_64"
New-Item -ItemType Directory -Path $temp, (Join-Path $pack "share/icons"), (Split-Path -Parent (Join-Path $pack $exeRel)) -Force | Out-Null
Set-Content -LiteralPath (Join-Path $pack $exeRel) -Value "new"
Set-Content -LiteralPath (Join-Path $pack "share/icons/icon.txt") -Value "icon"
if ($onWindows) {
	$archive = "${pack}.zip"
	Compress-Archive -LiteralPath $pack -DestinationPath $archive
	$parent = Join-Path $root "local/Programs"
	$prefix = Join-Path $parent "Nemo Anywhere"
} else {
	$archive = "${pack}.tar.gz"
	& tar -czf $archive -C (Split-Path -Parent $pack) (Split-Path -Leaf $pack)
	if ($LASTEXITCODE -ne 0) { Write-Host "[ FAILED: could not make the test archive ]"; exit 1 }
	$parent = Join-Path $root "data"
	$prefix = Join-Path $parent "nemo-anywhere"
}
$script:From = $archive

$saved = @{}
foreach ($name in @("LOCALAPPDATA", "APPDATA", "TEMP", "TMP", "TMPDIR", "XDG_DATA_HOME")) {
	$saved[$name] = [Environment]::GetEnvironmentVariable($name)
}
try {
	$env:LOCALAPPDATA  = Join-Path $root "local"
	$env:APPDATA       = Join-Path $root "roaming"
	$env:TEMP          = $temp
	$env:TMP           = $temp
	$env:TMPDIR        = $temp
	$env:XDG_DATA_HOME = $parent

	foreach ($way in @("copy", "aside", "swap")) {
		## A fresh old install each time, and nothing else beside it.
		if (Test-Path -LiteralPath $parent) { Remove-Item -LiteralPath $parent -Recurse -Force }
		$oldExe  = Join-Path $prefix $exeRel
		$oldIcon = Join-Path $prefix "share/icons/icon.txt"
		New-Item -ItemType Directory -Path (Split-Path -Parent $oldExe), (Split-Path -Parent $oldIcon) -Force | Out-Null
		Set-Content -LiteralPath $oldExe -Value "old"
		Set-Content -LiteralPath $oldIcon -Value "old"

		$script:case = $way
		$held = $null
		if ($way -eq "aside" -and $onWindows) {
			## A file below the exe, so a move file by file would take the exe first.
			$held = [System.IO.File]::Open($oldIcon, "Open", "Read", "ReadWrite")
		}
		$failedWith = ""
		try {
			fMain 6>$null
		} catch {
			$failedWith = fInnerMessage $_
			if (-not $failedWith -or $failedWith -eq "installer-abort") { $failedWith = "failed" }
		} finally {
			if ($held) { $held.Dispose() }
			$script:case = ""
			if ($state.work) {
				if (-not $onWindows) { & chmod -R u+rwX $state.work 2>$null }
				Remove-Item -LiteralPath $state.work -Recurse -Force -ErrorAction SilentlyContinue
				$state.work = $null
			}
		}

		if (-not $failedWith) { fProblem "${way}: the install did not fail"; continue }
		$left = @(Get-ChildItem -LiteralPath $parent -Force | Where-Object { $_.Name -ne (Split-Path -Leaf $prefix) })
		foreach ($item in $left) { fProblem "${way}: left behind beside the install: $($item.Name)" }
		$kept = $true
		foreach ($file in @($oldExe, $oldIcon)) {
			$now = if (Test-Path -LiteralPath $file) { (Get-Content -LiteralPath $file -Raw).Trim() } else { "missing" }
			if ($now -ne "old") { fProblem "${way}: the old install was not kept whole ($(Split-Path -Leaf $file) reads: ${now})"; $kept = $false }
		}
		if ($left.Count -eq 0 -and $kept) { Write-Host "  ${way}: failed as it should ($failedWith), nothing left behind" }
	}
} finally {
	foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
	if (-not $onWindows) { & chmod -R u+rwX $root 2>$null }
	Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) {
	Write-Host "[ FAILED: install.ps1 staging check, ${failures} problem(s) ]"
	exit 1
}
Write-Host "[ OK: install.ps1 staging check ]"
exit 0
