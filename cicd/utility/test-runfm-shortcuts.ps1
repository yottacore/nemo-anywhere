#!/usr/bin/env pwsh

##	Purpose:
##		- Check the dogfood launcher's Windows shortcut step (fRefreshShortcuts in
##		  utility/n8runfm.ps1) when some or none of the shortcut folders exist.
##		  A new shortcut goes in the user's own Start Menu, made if missing, and
##		  one of ours found anywhere else is repointed instead.
##		- The function is lifted out of the launcher through the parser. The
##		  profile folders are a scratch folder and the shell's shortcut object is
##		  a stand-in, so it runs anywhere pwsh does.
##		- Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-runfm-shortcuts.ps1
##	Test ID: rjm8ks2e

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets', '',
	Justification = 'The step asks New-Object for the shell COM object, which only Windows has.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions', '',
	Justification = 'The stand-in New-Object only builds an object.')]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

$launcher = Join-Path $PSScriptRoot "../../utility/n8runfm.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: n8runfm.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

$def = $ast.Find({
	param($node)
	$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq "fRefreshShortcuts"
}, $true)
if (-not $def) { Write-Host "[ FAILED: n8runfm.ps1 has no function fRefreshShortcuts ]"; exit 1 }
. ([scriptblock]::Create($def.Extent.Text))

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) "runfm-shortcut-test-$([System.IO.Path]::GetRandomFileName())"
$script:rows = [System.Collections.Generic.List[string]]::new()

$script:want = @{ Target = 'C:\Windows\System32\cmd.exe'; Arguments = '/c ""C:\util\runfm.cmd""' }
function fShortcutCommand { return $script:want }
function fShortcutIcon { return 'C:\pool\nemo-anywhere.exe,0' }
function fItem { param([string]$Status, [string]$Label, [string]$Detail) $script:rows.Add("$Status|$Label|$Detail") }

## The shell's shortcut object, kept as JSON in the .lnk itself. Save refuses
## anywhere outside the scratch folder, since a wrong path is what is under test.
function fFakeLink {
	param([string]$Path)
	$link = [pscustomobject]@{ Path = $Path; TargetPath = ""; Arguments = ""; IconLocation = ""; WorkingDirectory = ""; WindowStyle = 1; Description = "" }
	if (Test-Path -LiteralPath $Path) {
		$saved = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
		foreach ($prop in $saved.PSObject.Properties) { $link.($prop.Name) = $prop.Value }
		$link.Path = $Path
	}
	$link | Add-Member -MemberType ScriptMethod -Name Save -Value {
		$full = [System.IO.Path]::GetFullPath($this.Path)
		if (-not $full.StartsWith($scratch)) { throw "saved outside the profile: $full" }
		$this | Select-Object -Property * | ConvertTo-Json | Set-Content -LiteralPath $full
	}
	return $link
}
function New-Object {
	param([string]$ComObject)
	if ($ComObject -ne "WScript.Shell") { throw "unexpected New-Object $ComObject" }
	$shell = [pscustomobject]@{}
	$shell | Add-Member -MemberType ScriptMethod -Name CreateShortcut -Value { param($Path) fFakeLink $Path }
	return $shell
}

## Each case gets its own profile with only the named folders in it, and the
## .lnk files given as folder key -> target.
function fCase {
	param([string]$Name, [string[]]$Have, [hashtable]$Links = @{})

	$root = Join-Path $scratch $Name
	$env:APPDATA     = Join-Path $root "AppData/Roaming"
	$env:ProgramData = Join-Path $root "ProgramData"
	$env:PUBLIC      = Join-Path $root "Public"
	Set-Variable -Name HOME -Value (Join-Path $root "home") -Scope Global -Force
	$folders = @{
		start     = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
		quick     = Join-Path $env:APPDATA "Microsoft\Internet Explorer\Quick Launch"
		allstart  = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
		desktop   = Join-Path $HOME "Desktop"
		pubdesk   = Join-Path $env:PUBLIC "Desktop"
	}
	foreach ($key in $Have) { $null = New-Item -ItemType Directory -Path $folders[$key] -Force }
	foreach ($key in $Links.Keys) {
		$link = fFakeLink (Join-Path $folders[$key] "Nemo.lnk")
		$link.TargetPath = $Links[$key]
		$link.Save()
	}

	$script:rows.Clear()
	try {
		fRefreshShortcuts
	} catch {
		fFail "${Name}: the step stopped: $($_.Exception.Message)"
	}
	return $folders
}

function fCheckLink {
	param([string]$Case, [string]$Path)
	if (-not (Test-Path -LiteralPath $Path)) { fFail "${Case}: no shortcut at $Path (rows: $($script:rows -join '; '))"; return }
	$link = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
	if ($link.TargetPath -ne $script:want.Target -or $link.Arguments -ne $script:want.Arguments) {
		fFail "${Case}: $Path points at '$($link.TargetPath) $($link.Arguments)'"
	}
}

function fLinkCount {
	return @(Get-ChildItem -LiteralPath $scratch -Recurse -Filter "*.lnk").Count
}

try {
	$newName = "Nemo Anywhere (dogfood).lnk"

	## The reported case: a bare profile with none of the folders.
	$f = fCase -Name "none" -Have @()
	fCheckLink "none" (Join-Path $f.start $newName)

	## One folder only, so the filtered list is a single string, and not the
	## user's Start Menu.
	$f = fCase -Name "desktop-only" -Have @("desktop")
	fCheckLink "desktop-only" (Join-Path $f.start $newName)
	$f = fCase -Name "allusers-only" -Have @("allstart")
	fCheckLink "allusers-only" (Join-Path $f.start $newName)
	if (Test-Path -LiteralPath (Join-Path $f.allstart $newName)) { fFail "allusers-only: made one in the all-users Start Menu" }

	$f = fCase -Name "usual" -Have @("start", "quick", "allstart", "desktop", "pubdesk")
	fCheckLink "usual" (Join-Path $f.start $newName)

	## An old one of ours is repointed, and no second one is made.
	$before = fLinkCount
	$f = fCase -Name "stale" -Have @("desktop") -Links @{ desktop = 'C:\old\nemo-anywhere.exe' }
	fCheckLink "stale" (Join-Path $f.desktop "Nemo.lnk")
	if ((fLinkCount) -ne $before + 1) { fFail "stale: a new shortcut was made beside the repointed one" }

	## Someone else's is left alone, and ours is made.
	$f = fCase -Name "foreign" -Have @("start") -Links @{ start = 'C:\Tools\other.exe' }
	fCheckLink "foreign" (Join-Path $f.start $newName)
	$other = Get-Content -LiteralPath (Join-Path $f.start "Nemo.lnk") -Raw | ConvertFrom-Json
	if ($other.TargetPath -ne 'C:\Tools\other.exe') { fFail "foreign: another program's shortcut was changed" }
} finally {
	if ((Test-Path -LiteralPath $scratch) -and $scratch.StartsWith([System.IO.Path]::GetTempPath())) {
		Remove-Item -LiteralPath $scratch -Recurse -Force
	}
}

if ($failures) {
	Write-Host "[ FAILED: ${failures} shortcut checks ]"
	exit 1
}
Write-Host "[ OK: shortcut step with missing folders ]"
exit 0
