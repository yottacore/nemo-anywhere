#!/usr/bin/env pwsh

##	Purpose:
##		- Check that install.ps1 hands the Windows PATH back exactly as it found
##		  it: an install adds its folder, an uninstall takes it out again, and
##		  nothing else in the value changes, a trailing ';' included.
##		- The PATH functions are lifted out of install.ps1 through the parser, so
##		  the installer itself never runs, and the registry read and write are
##		  replaced by a variable. Runs anywhere pwsh does.
##		- Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-install-path.ps1

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$installer = Join-Path $PSScriptRoot "../../install.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($installer, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: install.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

## Only the three that do the work. fPathRead and fPathWrite are the registry.
foreach ($name in @("fPathContains", "fPathAdd", "fPathRemove")) {
	$def = $ast.Find({
		param($node)
		$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
	}, $true)
	if (-not $def) { Write-Host "[ FAILED: install.ps1 has no function ${name} ]"; exit 1 }
	. ([scriptblock]::Create($def.Extent.Text))
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

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

$dir = 'C:\Users\someone\AppData\Local\Programs\Nemo Anywhere'

## -ceq throughout: the round trip has to give back the same bytes, and -eq
## ignores case.
foreach ($before in @(
	""
	"a;b"
	"a;b;"
	"a;;b"
	";a"
	"a;b;;"
	'%SystemRoot%\system32;%SystemRoot%'
	'%SystemRoot%\system32;C:\Tools;'
)) {
	$script:fakePath = $before
	$added = fPathAdd "User" $dir
	$afterAdd = $script:fakePath
	$held = @($afterAdd -split ';' | Where-Object { $_ -ceq $dir }).Count
	if (-not $added) { fFail "add to '${before}' reported nothing added" }
	if ($held -ne 1) { fFail "add to '${before}' gave '${afterAdd}', holding the folder ${held} time(s)" }
	if ($before.EndsWith(';') -ne $afterAdd.EndsWith(';')) { fFail "add to '${before}' changed the trailing ';': '${afterAdd}'" }

	$removed = fPathRemove "User" $dir
	if (-not $removed) { fFail "remove from '${afterAdd}' reported nothing removed" }
	if (-not ($script:fakePath -ceq $before)) { fFail "round trip of '${before}' gave back '$($script:fakePath)'" }
}

## Already there, in any spelling Windows treats as the same folder: nothing is
## added, and nothing is written.
foreach ($before in @(
	"a;${dir};b"
	"a;${dir}\;b"
	"$($dir.ToLowerInvariant());b;"
)) {
	$script:fakePath = $before
	$added = fPathAdd "User" $dir
	if ($added) { fFail "add to '${before}' added it a second time" }
	if (-not ($script:fakePath -ceq $before)) { fFail "add to '${before}', which already held it, changed it to '$($script:fakePath)'" }
}

## Nothing of ours there: a remove leaves the value alone.
$script:fakePath = 'a;C:\Other\Nemo Anywhere Two;b;'
if (fPathRemove "User" $dir) { fFail "remove took something out of a PATH that never held the folder" }
if (-not ($script:fakePath -ceq 'a;C:\Other\Nemo Anywhere Two;b;')) { fFail "remove changed a PATH that never held the folder: '$($script:fakePath)'" }

if ($failures) {
	Write-Host "[ FAILED: install.ps1 PATH round trip, ${failures} problem(s) ]"
	exit 1
}
Write-Host "[ OK: install.ps1 PATH round trip ]"
exit 0
