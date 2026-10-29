#!/usr/bin/env pwsh

##	Purpose:
##		- Check cicd-win.ps1's step lines, job counts and priority, the same
##		  things test-cicd-quick.bash and test-cicd-jobs.bash check for cicd.bash.
##		- The job count is lifted out through the parser. The rest runs a copy
##		  of the script with every step's work stubbed out, through the gate and
##		  the full run: each step opens under a 79-dot rule and closes PASS, or
##		  FAIL when it dies, numbered out of the steps the plan has on, and the
##		  plan lists the ones turned off with why.
##		- Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-cicd-win.ps1
##	Test ID: dejjt

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

$script = Join-Path $PSScriptRoot "../cicd-win.ps1"
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors) { Write-Host "[ FAILED: cicd-win.ps1 does not parse: $($parseErrors[0].Message) ]"; exit 1 }

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

## The count itself.
$def = $ast.Find({
	param($node)
	$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'fJobs_Count'
}, $true)
if (-not $def) { Write-Host "[ FAILED: cicd-win.ps1 has no fJobs_Count ]"; exit 1 }
. ([scriptblock]::Create($def.Extent.Text))
foreach ($case in @(
		@(16, 10, $false, 8), @(16, 50, $false, 8), @(16, 51, $false, 4), @(16, 10, $true, 4),
		@(3, 0, $false, 1), @(2, 90, $false, 1), @(1, 0, $true, 1), @(12, -1, $false, 6))) {
	$got = fJobs_Count -Cpus $case[0] -CpuUse $case[1] -Hard:$case[2]
	if ($got -ne $case[3]) { fFail "fJobs_Count $($case[0]) cpus, $($case[1])% use, hard $($case[2]): $got, not $($case[3])" }
}

## One place works the count out, and every tool that runs in parallel gets it.
$text = Get-Content -LiteralPath $script -Raw
$where = @($ast.FindAll({
			param($node)
			$node -is [System.Management.Automation.Language.MemberExpressionAst] -and "$($node.Member)" -eq 'ProcessorCount'
		}, $true) | ForEach-Object {
		$fn = $_.Parent
		while ($fn -and $fn -isnot [System.Management.Automation.Language.FunctionDefinitionAst]) { $fn = $fn.Parent }
		if ($fn) { $fn.Name } else { 'top level' }
	})
if (($where -join ',') -ne 'fJobs') { fFail "the CPU count is read in: $($where -join ', '), not only fJobs" }
if ($text -notmatch 'ninja -C \$BuildRel -j \$\(\$script:Jobs\)') { fFail "ninja doesn't get the job count" }
if ($text -notmatch '--num-processes \$\(\$script:Jobs\)') { fFail "meson test doesn't get the job count" }
if ($text -notmatch 'export CICD_MAX_JOBS=\$\(\$script:Jobs\); ') { fFail "the MSYS2 shell doesn't get the job count" }

## A scratch run folder, removed at the end by its own path.
$hostTemp = if ($env:HOST_TEMP) { $env:HOST_TEMP } elseif ($IsWindows) { $env:TEMP } elseif ($env:TMPDIR) { $env:TMPDIR } else { '/tmp' }
$base = Join-Path $hostTemp 'nemo-anywhere'
New-Item -ItemType Directory -Path $base -Force | Out-Null
$scratch = $null
$runStamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
for ($n = 1; $n -le 999 -and -not $scratch; $n++) {
	$try = Join-Path $base ('{0}_{1:000}' -f $runStamp, $n)
	try { New-Item -ItemType Directory -Path $try -ErrorAction Stop | Out-Null; $scratch = $try }
	catch { $scratch = $null }
}
if (-not $scratch) { Write-Host "[ FAILED: no scratch folder under $base ]"; exit 1 }

try {
	## The copy: Windows check off, MSYS2 nowhere, and stubs for each step's
	## work after the real functions, so the real plan and step code run. The stubs say
	## the job count and priority they ran at, and die when told to.
	$copyDir = Join-Path $scratch 'cicd'
	New-Item -ItemType Directory -Path $copyDir | Out-Null
	$guard = 'if (-not $IsWindows) {'
	$msys = '$MsysRoot = "C:\msys64"'
	$entry = "#------------------------------------------------------------------------------`n# Entry point"
	$body = $text -replace "`r`n", "`n"
	if (-not $body.Contains($guard)) { throw "cicd-win.ps1 has no '$guard' line to turn off" }
	if (-not $body.Contains($msys)) { throw "cicd-win.ps1 has no '$msys' line" }
	if (-not $body.Contains($entry)) { throw "cicd-win.ps1 has no entry point marker" }
	$stubs = @'
function fToolchainMissing {
	fEcho_Clean "probe at priority $([string](Get-Process -Id $PID).PriorityClass)"
	if ($env:T_TOOLMISS) { return 'no MSYS2 here' }
	return $null
}
function fCpuUse { if ($env:T_CPU_USE) { return [int]$env:T_CPU_USE }; return 10 }
function fEvbFound { return -not $env:T_NO_EVB }
function fMingw { param([string]$ShCommand, [switch]$Quiet); $script:MingwRc = 0 }
function fStub {
	param([string]$Name)
	fEcho_Clean "ran $Name at jobs $($script:Jobs) env $($env:CICD_MAX_JOBS) priority $([string](Get-Process -Id $PID).PriorityClass)"
	if ($env:T_FAIL -eq $Name) { fDie "$Name broke" }
}
function fRemoteSync { fStub fRemoteSync }
function fLint { fStub fLint }
function fBuild { fStub fBuild }
function fTests { fStub fTests }
function fSmoke { param($Exe, $RuntimeBin); fStub fSmoke }
function fGuiSmoke { param($Exe, $RuntimeBin); fStub fGuiSmoke }
function fStage { fStub fStage }
function fPack { fStub fPack }
function fDogfood { fStub fDogfood }
function fPublish { param($Msg); fStub fPublish }

'@
	$body = $body.Replace($guard, 'if ($false) {').Replace($msys, '$MsysRoot = Join-Path $Root "no-msys"').Replace($entry, $stubs + $entry)
	$copy = Join-Path $copyDir 'cicd-win.ps1'
	Set-Content -LiteralPath $copy -Value $body -NoNewline
	New-Item -ItemType Directory -Path (Join-Path $scratch 'source') | Out-Null
	Set-Content -LiteralPath (Join-Path $scratch 'source/meson.build') -Value "project('x', 'c', version : '1.2.3')"

	$rule = ([string][char]0x2022) * 79
	$stepRx = '^\[ (\d{8}-\d{6}\.\d{2}): Step (\d+) of (\d+); (Starting|PASS|FAIL) (.+) \]$'
	$saved = @{}
	foreach ($name in 'T_TOOLMISS', 'T_CPU_USE', 'T_NO_EVB', 'T_FAIL', 'CICD_JOBS_PIN', 'CICD_MAX_JOBS') { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }

	## Runs the copy and checks its step lines against the steps and turned-off
	## entries expected, in order. Returns the output lines.
	function fRun {
		param([string]$Label, [string[]]$CopyArgs, [hashtable]$Vars = @{}, [string[]]$Steps, [string[]]$Off = @(), [int]$Rc = 0, [string]$Failed = '')
		foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $null) }
		foreach ($name in $Vars.Keys) { [Environment]::SetEnvironmentVariable($name, $Vars[$name]) }
		$lines = @(& pwsh -NoProfile -File $copy @CopyArgs 2>&1 | ForEach-Object { "$_" })
		$exit = $LASTEXITCODE
		if ($exit -ne $Rc) { fFail "${Label}: exit $exit, not $Rc"; $lines | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" } }
		$want = $Steps.Count
		$opened = [Collections.Generic.List[string]]::new()
		$closed = @{}
		for ($i = 0; $i -lt $lines.Count; $i++) {
			if ($lines[$i] -notmatch $stepRx) { continue }
			$n = [int]$Matches[2]; $of = [int]$Matches[3]; $state = $Matches[4]; $name = $Matches[5]
			if ($of -ne $want) { fFail "${Label}: step $n of $of, not of $want" }
			if ($state -eq 'Starting') {
				$opened.Add($name)
				if ($n -ne $opened.Count) { fFail "${Label}: '$name' is step $n, not $($opened.Count)" }
				if ($i -lt 1 -or $lines[$i - 1] -ne $rule) { fFail "${Label}: no 79-dot rule above step $n" }
				if ($i -lt 2 -or $lines[$i - 2] -ne '') { fFail "${Label}: no blank line before the rule of step $n" }
				if ($i + 1 -ge $lines.Count -or $lines[$i + 1] -notmatch '^jobs \d+ \(.*cpus\)$') { fFail "${Label}: no jobs line under step $n" }
			} else {
				if ($closed.ContainsKey($n)) { fFail "${Label}: step $n closed twice" }
				$closed[$n] = "$state $name"
				if ($opened.Count -ne $n -or $opened[$n - 1] -ne $name) { fFail "${Label}: '$state $name' closes step $n, which isn't the one open" }
			}
		}
		$wantOpened = if ($Failed) { $Steps[0..([Array]::IndexOf($Steps, $Failed))] } else { $Steps }
		if (($opened -join '|') -ne ($wantOpened -join '|')) { fFail "${Label}: opened '$($opened -join ', ')', not '$($wantOpened -join ', ')'" }
		for ($n = 1; $n -le $opened.Count; $n++) {
			$state = if ($opened[$n - 1] -eq $Failed) { 'FAIL' } else { 'PASS' }
			if (-not $closed.ContainsKey($n)) { fFail "${Label}: step $n ($($opened[$n - 1])) never closed" }
			elseif ($closed[$n] -ne "$state $($opened[$n - 1])") { fFail "${Label}: step $n closed '$($closed[$n])', not '$state $($opened[$n - 1])'" }
		}
		$listed = @($lines | Where-Object { $_ -match '^    - ' } | ForEach-Object { $_.Substring(6) })
		if (($listed -join '|') -ne ($Off -join '|')) { fFail "${Label}: turned off '$($listed -join ', ')', not '$($Off -join ', ')'" }
		$count = @($lines | Where-Object { $_ -match '^Steps \.+: (\d+)$' } | ForEach-Object { [int]($_ -replace '^Steps \.+: ', '') })
		if ($count.Count -ne 1 -or $count[0] -ne $want) { fFail "${Label}: the plan says '$($count -join ',')' steps, not $want" }
		if ($lines.Count -eq 0 -or $lines[-1] -ne '') { fFail "${Label}: doesn't end with a blank line" }
		return $lines
	}

	$gate = @('Lints', 'Build (debug)', 'Tests', 'Smoke checks')
	$out = fRun 'gate' @('-Gate') -Steps $gate
	$ran = @($out | Where-Object { $_ -match '^ran ' })
	if ($ran.Count -ne 5) { fFail "gate: $($ran.Count) step bodies ran, not 5" }
	$cpus = [Environment]::ProcessorCount
	$half = [Math]::Max(1, [Math]::Floor($cpus / 2))
	foreach ($line in $ran) {
		if ($line -notmatch "jobs $half env $half priority (\w+)$") { fFail "gate: '$line' isn't at $half jobs" }
		elseif ($Matches[1] -notin 'Idle', 'BelowNormal') { fFail "gate: '$line' isn't at low priority" }
	}
	if (-not ($out -match "^jobs $half \(cpu use 10% on $cpus cpus\)$")) { fFail "gate: no plain jobs line" }
	## The toolchain probe runs before any step, so only the top of the script lowers it.
	if (-not ($out -match '^probe at priority (Idle|BelowNormal)$')) { fFail "gate: the toolchain probe didn't run at low priority" }

	fRun 'gate -NoFmt' @('-Gate', '-NoFmt') -Steps $gate[1..3] -Off 'Lints (-NoFmt)' | Out-Null
	fRun 'gate, tests fail' @('-Gate') -Vars @{ T_FAIL = 'fTests' } -Steps $gate -Rc 1 -Failed 'Tests' | Out-Null
	$out = fRun 'gate, no toolchain' @('-Gate') -Vars @{ T_TOOLMISS = '1' } -Steps @() -Off ($gate | ForEach-Object { "$_ (toolchain missing)" })
	if (-not ($out -match 'gate: SKIPPED')) { fFail "gate, no toolchain: no SKIPPED line" }

	$quarter = [Math]::Max(1, [Math]::Floor($cpus / 4))
	$out = fRun 'gate, busy' @('-Gate') -Vars @{ T_CPU_USE = '80' } -Steps $gate
	if (-not ($out -match "^jobs $quarter \(busy box, cpu use 80% on $cpus cpus\)$")) { fFail "gate, busy: no busy jobs line at $quarter" }
	$out = fRun 'gate, pinned' @('-Gate') -Vars @{ CICD_JOBS_PIN = '3'; T_CPU_USE = '80' } -Steps $gate
	if (-not ($out -match '^ran fBuild at jobs 3 env 3 ')) { fFail "gate, pinned: the build didn't get the pinned 3" }
	if (-not ($out -match '^jobs 3 \(pinned, cpu use 80%')) { fFail "gate, pinned: no pinned jobs line" }
	foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $null) }
	$env:CICD_MAX_JOBS = 'all'
	$out = @(& pwsh -NoProfile -File $copy -Gate 2>&1 | ForEach-Object { "$_" })
	if ($LASTEXITCODE -ne 1 -or -not ($out -match 'must be a whole number')) { fFail "gate, bad pin: exit $LASTEXITCODE, and no complaint about the pin" }
	$env:CICD_MAX_JOBS = $null

	$full = @('Remote sync', 'Lints', 'Build (debug)', 'Tests', 'Smoke checks', 'Runtime bundle', 'Packages', 'Dogfood', 'Publish')
	fRun 'full' @('-Yes', '-Message', 'x') -Steps $full | Out-Null
	fRun 'full, some off' @('-Yes', '-NoSync', '-NoDogfood', '-NoPublish') -Steps $full[1..6] `
		-Off 'Remote sync (-NoSync)', 'Dogfood (-NoDogfood)', 'Publish (-NoPublish)' | Out-Null
	fRun 'full, no EVB' @('-Yes', '-Message', 'x') -Vars @{ T_NO_EVB = '1' } -Steps ($full[0..5] + 'Publish') `
		-Off 'Packages (Enigma Virtual Box not installed)', 'Dogfood (no pack: Enigma Virtual Box not installed)' | Out-Null
	fRun 'full, -NoBuild' @('-Yes', '-Message', 'x', '-NoBuild') -Steps 'Remote sync', 'Lints', 'Publish' `
		-Off 'Build (debug) (-NoBuild)', 'Tests (-NoBuild)', 'Smoke checks (-NoBuild)', 'Runtime bundle (-NoBuild)', 'Packages (-NoBuild)', 'Dogfood (no pack: -NoBuild)' | Out-Null
	fRun 'full, no toolchain' @('-Yes', '-Message', 'x') -Vars @{ T_TOOLMISS = '1' } -Steps 'Remote sync', 'Publish' `
		-Off 'Lints (toolchain missing)', 'Build (debug) (toolchain missing)', 'Tests (toolchain missing)', 'Smoke checks (toolchain missing)', 'Runtime bundle (toolchain missing)', 'Packages (toolchain missing)', 'Dogfood (no pack: toolchain missing)' | Out-Null
	fRun 'full, -BuildStrict, no toolchain' @('-Yes', '-Message', 'x', '-BuildStrict') -Vars @{ T_TOOLMISS = '1' } -Steps (@($full[0]) + $full[2..8]) `
		-Off 'Lints (toolchain missing)' -Rc 1 -Failed 'Build (debug)' | Out-Null
	fRun 'full, pack fails' @('-Yes', '-Message', 'x') -Vars @{ T_FAIL = 'fPack' } -Steps $full -Rc 1 -Failed 'Packages' | Out-Null
} finally {
	foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
	## Only the folder this test made, right under the one parent it may use.
	$item = Get-Item -LiteralPath $scratch -Force -ErrorAction SilentlyContinue
	if ($item -and -not $item.LinkType -and (Split-Path -Parent $item.FullName) -eq (Get-Item -LiteralPath $base).FullName) {
		Remove-Item -LiteralPath $scratch -Recurse -Force
	}
}

if ($failures) { Write-Host "[ FAILED: $failures problem(s) ]"; exit 1 }
Write-Host "[ OK: cicd-win.ps1 steps, job counts and priority ]"
exit 0
