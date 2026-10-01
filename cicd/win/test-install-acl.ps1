#!/usr/bin/env pwsh

##	Purpose:
##		- Check that a Windows install takes its permissions from the folder it
##		  is installed into, not from the temp folder it was unpacked in. The
##		  install folder's parent grants Users read and run to everything below
##		  it, the temp folder does not, and every file and folder installed has
##		  to come out with that grant.
##		- This is what lets other accounts start a system install. The system
##		  target is never run here: the user target goes through the same
##		  staging, with LOCALAPPDATA, APPDATA and TEMP pointed into a scratch
##		  folder. install.ps1's own functions are lifted out through the parser
##		  and its main body run from an archive made here. Only the registry
##		  PATH read and write are swapped for a variable.
##		- Reads ACLs rather than opening files as another account, since an
##		  elevated shell reads through a deny.
##		- Windows only; anywhere else it exits 77. Runs in cicd-win.ps1's test
##		  stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/win/test-install-acl.ps1
##	Test ID: rj72n4xb

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

if (-not $IsWindows) { Write-Host "[ install.ps1 ACL check skipped: Windows only ]"; exit 77 }

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

## The user PATH stays as it is.
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

## The installer's parameters, as a user install of a local archive.
$script:Release   = "stable"
$script:Target    = "user"
$script:Uninstall = $false
$script:Yes       = $true

$failures = 0
function fProblem {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

$usersSid = New-Object System.Security.Principal.SecurityIdentifier "S-1-5-32-545"
$readRun  = [System.Security.AccessControl.FileSystemRights]::ReadAndExecute

function fUsersCanRun {
	param([string]$Path)
	$rules = (Get-Acl -LiteralPath $Path).GetAccessRules($true, $true, [System.Security.Principal.SecurityIdentifier])
	foreach ($rule in $rules) {
		if ($rule.IdentityReference -eq $usersSid -and
			$rule.AccessControlType -eq [System.Security.AccessControl.AccessControlType]::Allow -and
			($rule.FileSystemRights -band $readRun) -eq $readRun) { return $true }
	}
	return $false
}

## Rights for this account, SYSTEM and Administrators only, passed to
## everything below. Protected, so nothing comes down from above.
function fSetOwnAcl {
	param([string]$Path, [string[]]$ExtraSids = @())
	$inherit = [System.Security.AccessControl.InheritanceFlags]"ContainerInherit, ObjectInherit"
	$none    = [System.Security.AccessControl.PropagationFlags]::None
	$allow   = [System.Security.AccessControl.AccessControlType]::Allow
	$acl = Get-Acl -LiteralPath $Path
	$acl.SetAccessRuleProtection($true, $false)
	$sids = @([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value, "S-1-5-18", "S-1-5-32-544")
	foreach ($sid in $sids) {
		$who = New-Object System.Security.Principal.SecurityIdentifier $sid
		$acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule $who, "FullControl", $inherit, $none, $allow))
	}
	foreach ($sid in $ExtraSids) {
		$who = New-Object System.Security.Principal.SecurityIdentifier $sid
		$acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule $who, $readRun, $inherit, $none, $allow))
	}
	## Not Set-Acl, which writes the owner back too and can be refused that.
	[System.IO.FileSystemAclExtensions]::SetAccessControl((Get-Item -LiteralPath $Path), $acl)
}

## One volume for the temp folder and the install, as with a default profile,
## so the installer's same-volume path is the one taken.
$root     = Join-Path ([System.IO.Path]::GetTempPath()) "test_install-acl_$(Get-Date -Format 'yyyyMMdd-HHmmssff')"
$temp     = Join-Path $root "temp"
$local    = Join-Path $root "local"
$programs = Join-Path $local "Programs"
$roaming  = Join-Path $root "roaming"
$pack     = Join-Path $root "pack\nemo-anywhere-0.0.0-windows-x86_64"
New-Item -ItemType Directory -Path $temp, $programs, $roaming, (Join-Path $pack "share\icons") -Force | Out-Null
fSetOwnAcl $temp
fSetOwnAcl $programs -ExtraSids @($usersSid.Value)

Set-Content -LiteralPath (Join-Path $pack "nemo-anywhere.exe") -Value "not a real exe"
Set-Content -LiteralPath (Join-Path $pack "share\icons\icon.txt") -Value "icon"
$archive = Join-Path $root "nemo-anywhere-0.0.0-windows-x86_64.zip"
Compress-Archive -LiteralPath $pack -DestinationPath $archive
$script:From = $archive

$saved = @{ LOCALAPPDATA = $env:LOCALAPPDATA; APPDATA = $env:APPDATA; TEMP = $env:TEMP; TMP = $env:TMP }
try {
	$env:LOCALAPPDATA = $local
	$env:APPDATA      = $roaming
	$env:TEMP         = $temp
	$env:TMP          = $temp

	## The installer unpacks under GetTempPath. Without the grant missing there,
	## a moved tree would look right, and the check would prove nothing.
	$tempNow = [System.IO.Path]::GetTempPath()
	if (-not $tempNow.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase)) {
		Write-Host "[ install.ps1 ACL check skipped: the temp folder could not be redirected (${tempNow}) ]"; exit 77
	}
	if (fUsersCanRun $temp) { Write-Host "[ FAILED: the scratch temp folder already lets Users in ]"; exit 1 }
	if (-not (fUsersCanRun $programs)) { Write-Host "[ FAILED: the scratch Programs folder does not let Users in ]"; exit 1 }

	try {
		fMain
	} catch {
		fProblem "the install failed: $(fInnerMessage $_)"
	} finally {
		if ($state.work) { Remove-Item -LiteralPath $state.work -Recurse -Force -ErrorAction SilentlyContinue }
	}

	$prefix = Join-Path $programs "Nemo Anywhere"
	if (-not (Test-Path -LiteralPath (Join-Path $prefix "nemo-anywhere.exe"))) {
		fProblem "nothing installed at ${prefix}"
	} else {
		$items = @(Get-Item -LiteralPath $prefix -Force) + @(Get-ChildItem -LiteralPath $prefix -Recurse -Force)
		$missing = @($items | Where-Object { -not (fUsersCanRun $_.FullName) })
		foreach ($item in $missing) { fProblem "Users cannot read and run $($item.FullName.Substring($prefix.Length).TrimStart('\'))" }
		Write-Host "  $($items.Count - $missing.Count) of $($items.Count) installed items let Users read and run"
	}
	if (-not $script:fakePath.Contains($prefix)) { fProblem "the install folder did not go on PATH" }
} finally {
	foreach ($name in $saved.Keys) { Set-Item -LiteralPath "env:${name}" -Value $saved[$name] }
	Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) {
	Write-Host "[ FAILED: install.ps1 ACL check, ${failures} problem(s) ]"
	exit 1
}
Write-Host "[ OK: install.ps1 ACL check ]"
exit 0
