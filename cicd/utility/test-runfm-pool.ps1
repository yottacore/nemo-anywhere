#!/usr/bin/env pwsh

##	Purpose:
##		- Check how the dogfood launcher (utility/n8runfm.ps1) keeps its pool of
##		  builds: which version each hour, day, week, month and year keeps, the
##		  count and size budget on top of that, the very first build and the
##		  newest always kept, and a version something is running from never
##		  removed.
##		- The pool functions and their settings are lifted out of the launcher
##		  through the parser, so it never runs. The clock, the process list and
##		  the version sizes are stand-ins; the pool itself is a scratch folder.
##		- Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-runfm-pool.ps1

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets', '',
	Justification = 'The pool reads the clock through Get-Date, and the test has to fix it.')]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$launcher = Join-Path $PSScriptRoot "../../utility/n8runfm.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: n8runfm.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

foreach ($name in @("fRotate", "fGfsRoles", "fPeriodKeys", "fBudget", "fHeldCopies", "fParseStamp", "fIdFile", "fRemoveIfIdle")) {
	$def = $ast.Find({
		param($node)
		$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
	}, $true)
	if (-not $def) { Write-Host "[ FAILED: n8runfm.ps1 has no function ${name} ]"; exit 1 }
	. ([scriptblock]::Create($def.Extent.Text))
}

## The launcher's own retention settings, read from its top level.
$settings = @("ProgramName", "MaxVersions", "MinVersions", "MaxPoolBytes", "StampFormat",
	"KeepFrequent", "KeepHourly", "KeepDaily", "KeepWeekly", "KeepMonthly", "KeepYearly")
foreach ($statement in $ast.EndBlock.Statements) {
	if ($statement -isnot [System.Management.Automation.Language.AssignmentStatementAst]) { continue }
	$left = $statement.Left
	if ($left -is [System.Management.Automation.Language.VariableExpressionAst] -and $settings -contains $left.VariablePath.UserPath) {
		. ([scriptblock]::Create($statement.Extent.Text))
	}
}

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

## The expectations below are worked out for these numbers. The header of the
## launcher promises the budget, so a change there should be a decision.
$want = "nemo-anywhere 10 5 1073741824 3 2 2 1 1 1"
$got  = "$ProgramName $MaxVersions $MinVersions $MaxPoolBytes $KeepFrequent $KeepHourly $KeepDaily $KeepWeekly $KeepMonthly $KeepYearly"
if ($got -ne $want) {
	Write-Host "[ FAILED: n8runfm.ps1 pool settings are now '${got}', this test expects '${want}' ]"
	exit 1
}

## A prefix pool, as on Linux and macOS, on every platform: that is the shape
## whose in-use test looks inside the version.
$script:ExeExt         = ""
$script:PayloadIsFile  = $false
$script:PayloadMainBin = "bin/${ProgramName}"
$scratch   = Join-Path ([System.IO.Path]::GetTempPath()) "runfm-pool-test-$([System.IO.Path]::GetRandomFileName())"
$TargetDir = Join-Path $scratch "${ProgramName}_versions"

$script:now     = [datetime]::new(2026, 6, 17, 12, 30, 0)
$script:running = @()
$script:sizes   = @{}
function Get-Date { return $script:now }
function fRunningExePaths { return $script:running }
function fItem { param([string]$Status, [string]$Label, [string]$Detail) $null = $Status, $Label, $Detail }
function fPayloadSize {
	param([string]$Path)
	$leaf = Split-Path -Leaf $Path
	if ($script:sizes.ContainsKey($leaf)) { return [int64]$script:sizes[$leaf] }
	return [int64]1MB
}

function fStampName { param([datetime]$When) return "${ProgramName}_$($When.ToString($StampFormat))" }

function fCopies {
	param([datetime[]]$Stamps)
	return @($Stamps | Sort-Object | ForEach-Object {
		$name = fStampName $_
		[pscustomobject]@{ Name = $name; Stamp = $_; Payload = [pscustomobject]@{ FullName = Join-Path $TargetDir $name } }
	})
}

## Now is Wednesday 2026-06-17 12:30, ISO week 25. Each stamp is the last of a
## completed period, except the two marked as held by nothing.
$stamps = [ordered]@{
	first    = [datetime]::new(2024, 3, 1, 10, 0, 0)
	nothing1 = [datetime]::new(2025, 2, 10, 10, 0, 0)
	year     = [datetime]::new(2025, 11, 20, 10, 0, 0)
	nothing2 = [datetime]::new(2026, 5, 2, 10, 0, 0)
	month    = [datetime]::new(2026, 5, 20, 10, 0, 0)
	week     = [datetime]::new(2026, 6, 10, 10, 0, 0)
	day1     = [datetime]::new(2026, 6, 15, 9, 0, 0)
	day2     = [datetime]::new(2026, 6, 16, 23, 0, 0)
	hour1    = [datetime]::new(2026, 6, 17, 10, 15, 0)
	hour2    = [datetime]::new(2026, 6, 17, 11, 5, 0)
	frequent = [datetime]::new(2026, 6, 17, 12, 5, 0)
	latest   = [datetime]::new(2026, 6, 17, 12, 20, 0)
}
$expectRole = @{}
foreach ($key in $stamps.Keys) {
	if ($key -notlike "nothing*") { $expectRole[(fStampName $stamps[$key])] = $key -replace '\d$', '' }
}

$copies = fCopies @($stamps.Values)
$roles  = fGfsRoles -Copies $copies
foreach ($copy in $copies) {
	$wantRole = if ($expectRole.ContainsKey($copy.Name)) { $expectRole[$copy.Name] } else { "(none)" }
	$gotRole  = if ($roles.ContainsKey($copy.Name)) { $roles[$copy.Name] } else { "(none)" }
	if ($gotRole -ne $wantRole) { fFail "role of $($copy.Name): ${gotRole}, expected ${wantRole}" }
}

$one = fGfsRoles -Copies (fCopies @($stamps.latest))
if ($one[(fStampName $stamps.latest)] -ne "first") { fFail "a lone version is not kept as first" }
$two = fGfsRoles -Copies (fCopies @($stamps.hour1, $stamps.latest))
if ($two[(fStampName $stamps.hour1)] -ne "first" -or $two[(fStampName $stamps.latest)] -ne "latest") {
	fFail "two versions are not first and latest"
}

## Budget: fourteen versions a day apart, all with a role.
$many = fCopies @(0..13 | ForEach-Object { [datetime]::new(2026, 5, 1, 12, 0, 0).AddDays($_) })
$manyRoles = @{}
foreach ($copy in $many) { $manyRoles[$copy.Name] = "frequent" }
$oldest = $many[0].Name
$newest = $many[$many.Count - 1].Name

## label, sizes by index (the rest 1 MB), how many kept.
foreach ($case in @(
	@("small versions stop at the count ceiling", @{}, 10),
	@("300 MB versions stop at the floor", @{ all = 300MB }, 5),
	@("150 MB versions stop at 1 GB", @{ all = 150MB }, 6),
	@("a 2 GB first build still leaves the floor", @{ 0 = 2GB }, 5),
	@("a 2 GB newest build still leaves the floor", @{ 13 = 2GB }, 5)
)) {
	$label = $case[0]
	$script:sizes = @{}
	for ($i = 0; $i -lt $many.Count; $i++) {
		if ($case[1].ContainsKey("all")) { $script:sizes[$many[$i].Name] = $case[1].all }
		elseif ($case[1].ContainsKey($i)) { $script:sizes[$many[$i].Name] = $case[1][$i] }
	}
	$keep = fBudget -Copies $many -Roles $manyRoles
	if ($keep.Count -ne $case[2]) { fFail "${label}: kept $($keep.Count), expected $($case[2])" }
	if (-not $keep.Contains($oldest)) { fFail "${label}: the first build was dropped" }
	if (-not $keep.Contains($newest)) { fFail "${label}: the newest build was dropped" }
	## Filled newest first, so what goes is always the oldest of the rest.
	$dropped = @($many | Where-Object { -not $keep.Contains($_.Name) })
	$keptRest = @($many | Where-Object { $keep.Contains($_.Name) -and $_.Name -ne $oldest })
	if ($dropped -and $keptRest -and ($dropped[$dropped.Count - 1].Stamp -gt $keptRest[0].Stamp)) {
		fFail "${label}: dropped a newer version than one it kept"
	}
}
$script:sizes = @{}

## No role, no place in the budget.
$partRoles = @{}
foreach ($copy in $many[0..3]) { $partRoles[$copy.Name] = "day" }
$keep = fBudget -Copies $many -Roles $partRoles
if ($keep.Count -ne 4) { fFail "budget kept $($keep.Count) of 4 versions with a role" }

## A lone version comes back as a set: returned bare, it would be a string, and
## .Contains on a string is a substring test.
$keep = fBudget -Copies (fCopies @($stamps.latest)) -Roles @{ (fStampName $stamps.latest) = "first" }
if ($keep -isnot [System.Collections.Generic.HashSet[string]]) { fFail "a one-version budget is a $($keep.GetType().Name), not a set" }
elseif ($keep.Contains($ProgramName)) { fFail "a one-version budget matched part of a name" }

## The whole rotation on a real folder, with a process running out of one of
## the two versions nothing keeps.
try {
	foreach ($when in $stamps.Values) {
		$bin = Join-Path (Join-Path $TargetDir (fStampName $when)) "bin"
		New-Item -ItemType Directory -Path $bin -Force | Out-Null
		Set-Content -LiteralPath (Join-Path $bin $ProgramName) -Value "build"
	}
	$busy = Join-Path $TargetDir (fStampName $stamps.nothing2)
	$script:running = @(Join-Path (Join-Path $busy "bin") $ProgramName)

	fRotate

	$left = @(Get-ChildItem -LiteralPath $TargetDir -Directory | ForEach-Object { $_.Name } | Sort-Object)
	$wantLeft = @($expectRole.Keys | ForEach-Object { "${_}_$($expectRole[$_])" }) + @(fStampName $stamps.nothing2) | Sort-Object
	if (($left -join " ") -ne ($wantLeft -join " ")) {
		fFail "after rotation the pool holds [$($left -join ', ')], expected [$($wantLeft -join ', ')]"
	}
	if (-not (Test-Path -LiteralPath $busy)) { fFail "rotation removed a version something is running from" }
	if (Test-Path -LiteralPath (Join-Path $TargetDir (fStampName $stamps.nothing1))) { fFail "rotation kept a version nothing needs" }

	## Run again with nothing running: the idle one goes, the names stay.
	$script:running = @()
	fRotate
	$again = @(Get-ChildItem -LiteralPath $TargetDir -Directory | ForEach-Object { $_.Name } | Sort-Object)
	$wantAgain = @($wantLeft | Where-Object { $_ -ne (fStampName $stamps.nothing2) })
	if (($again -join " ") -ne ($wantAgain -join " ")) {
		fFail "second rotation left [$($again -join ', ')], expected [$($wantAgain -join ', ')]"
	}
} finally {
	Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) {
	Write-Host "[ FAILED: n8runfm.ps1 pool, ${failures} problem(s) ]"
	exit 1
}
Write-Host "[ OK: n8runfm.ps1 pool ]"
exit 0
