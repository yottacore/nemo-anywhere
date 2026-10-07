#!/usr/bin/env pwsh

##	Purpose:
##		- Check install.ps1's in-use check against a real running program: the
##		  folder it runs from is named as held while it runs and free once it
##		  exits, a sibling folder whose name only starts the same is never
##		  named, a start through the folder's 8.3 name is still seen, and an
##		  empty answer reads as a count of zero rather than throwing.
##		- The functions are lifted out of install.ps1 through the parser, so the
##		  installer itself never runs. The program is a copy of Windows
##		  PowerShell, told to sleep.
##		- Windows only; anywhere else it exits 77. Runs in cicd-win.ps1's test
##		  stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/win/test-install-holders.ps1
##	Test ID: rhtwm2cd

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

if (-not $IsWindows) { Write-Host "[ install.ps1 in-use check skipped: Windows only ]"; exit 77 }

$installer = Join-Path $PSScriptRoot "../../install.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($installer, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: install.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

foreach ($name in @("fHoldersOf", "fWaitUntilFree")) {
	$def = $ast.Find({
		param($node)
		$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
	}, $true)
	if (-not $def) { Write-Host "[ FAILED: install.ps1 has no function ${name} ]"; exit 1 }
	. ([scriptblock]::Create($def.Extent.Text))
}

## fWaitUntilFree says what it is waiting on.
function fEcho_Clean {
	param([string]$Msg)
	Write-Host "  $Msg"
}

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) "nemo-install-holders-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$held = Join-Path $root "Nemo Anywhere"
$sibling = Join-Path $root "Nemo Anywhere Two"
New-Item -ItemType Directory -Path $held, $sibling -Force | Out-Null
$source = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
Copy-Item -LiteralPath $source -Destination (Join-Path $held "holder.exe")
Copy-Item -LiteralPath $source -Destination (Join-Path $sibling "other.exe")

## Started by the spelling given, and left sleeping until killed.
function fStart {
	param([string]$Exe)
	return Start-Process -FilePath $Exe -ArgumentList "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 120" -WindowStyle Hidden -PassThru
}

## A process can take a moment to show up in the WMI list.
function fHeldWithin {
	param([string]$Folder, [string]$Name)
	for ($i = 0; $i -lt 20; $i++) {
		if (@(fHoldersOf $Folder) -contains $Name) { return $true }
		Start-Sleep -Milliseconds 250
	}
	return $false
}

$procs = @()
try {
	$procs += fStart (Join-Path $sibling "other.exe")
	$procs += fStart (Join-Path $held "holder.exe")

	if (-not (fHeldWithin $held "holder.exe")) { fFail "a program running from the folder is not named" }
	$names = @(fHoldersOf $held)
	if ($names -contains "other.exe") { fFail "a program in '${sibling}' was named for '${held}'" }
	$waited = @(fWaitUntilFree $held 1)
	if ($waited.Count -ne 1) { fFail "waiting on a held folder gave '$($waited -join ', ')'" }

	$procs[1] | Stop-Process -Force
	$procs[1].WaitForExit()

	## The count is read the way the installer reads it, under strict mode.
	$free = @(fWaitUntilFree $held 5)
	if ($free.Count -ne 0) { fFail "the folder is still held after its program exited: '$($free -join ', ')'" }
	$now = @(fHoldersOf $held)
	if ($now.Count -ne 0) { fFail "fHoldersOf still names '$($now -join ', ')'" }

	## Through the 8.3 name. A volume with short names off has none to give.
	$short = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($held).ShortPath
	if ($short -eq $held -or -not $short.Contains('~')) {
		Write-Host "  note: no 8.3 name for ${held}, short path case not run"
	} else {
		$procs += fStart (Join-Path $short "holder.exe")
		$reported = $null
		for ($i = 0; $i -lt 20 -and -not $reported; $i++) {
			$reported = (Get-CimInstance Win32_Process -Filter "ProcessId = $($procs[-1].Id)").ExecutablePath
			if (-not $reported) { Start-Sleep -Milliseconds 250 }
		}
		Write-Host "  started as ${short}\holder.exe, reported as ${reported}"
		if (-not (fHeldWithin $held "holder.exe")) { fFail "a program started through the 8.3 name is not named" }
	}
} finally {
	foreach ($p in $procs) { if (-not $p.HasExited) { $p | Stop-Process -Force; $p.WaitForExit() } }
	Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) {
	Write-Host "[ FAILED: install.ps1 in-use check, ${failures} problem(s) ]"
	exit 1
}
Write-Host "[ OK: install.ps1 in-use check ]"
exit 0
