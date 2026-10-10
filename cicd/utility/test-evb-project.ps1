#!/usr/bin/env pwsh

##	Purpose:
##		- Check the options in an Enigma Virtual Box project that
##		  pack-portable.ps1 wrote. Sharing the virtual files with started
##		  programs must be off, since with it on every program the app starts
##		  gets the packer's hooks (2026100917220603).
##		- pack-portable.ps1 runs it on each project before packing.
##		- -SelfTest has pack-portable.ps1 write a project for a small flat tree
##		  and checks it, then checks that the same project with sharing on, or
##		  with the option missing, is refused. Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-evb-project.ps1 [-SelfTest] [-Path <evb>...]
##	Test ID: rjxn0ekh

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[string[]]$Path = @(),
	[switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

## The problems with one project's text, one line each; none when it is good.
function Test-ProjectText {
	param([Parameter(Mandatory)][string]$Label, [Parameter(Mandatory)][AllowEmptyString()][string]$Text)
	$problems = [System.Collections.Generic.List[string]]::new()
	$options = [regex]::Matches($Text, '(?s)<Options>(.*?)</Options>')
	if ($options.Count -ne 1) {
		$problems.Add("${Label}: $($options.Count) Options blocks, wanted 1")
		return $problems
	}
	$share = @([regex]::Matches($options[0].Groups[1].Value, '<ShareVirtualSystem>(.*?)</ShareVirtualSystem>'))
	if ($share.Count -ne 1) {
		$problems.Add("${Label}: ShareVirtualSystem set $($share.Count) times, wanted once")
	} elseif ($share[0].Groups[1].Value -cne 'false') {
		$problems.Add("${Label}: ShareVirtualSystem is '$($share[0].Groups[1].Value)', wanted 'false'")
	}
	return $problems
}

function Test-Project {
	param([Parameter(Mandatory)][string]$Project)
	$label = Split-Path -Leaf $Project
	if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) { return @("${label}: not there") }
	return Test-ProjectText -Label $label -Text ([IO.File]::ReadAllText($Project))
}

function Invoke-SelfTest {
	$failures = 0
	$stamp = Get-Date -Format 'yyyyMMdd-HHmmssff'
	$scratch = Join-Path ([IO.Path]::GetTempPath()) "test_evb-project_${stamp}_$PID"
	New-Item -ItemType Directory -Path $scratch | Out-Null
	try {
		$flat = Join-Path $scratch "flat"
		$out = Join-Path $scratch "out"
		New-Item -ItemType Directory -Path (Join-Path $flat "share") -Force | Out-Null
		foreach ($name in @("nemo-anywhere.exe", "libglib-2.0-0.dll", "share/a.txt")) {
			[IO.File]::WriteAllText((Join-Path $flat $name), "x")
		}

		$packer = Join-Path $PSScriptRoot "../win/pack-portable.ps1"
		$said = & pwsh -NoProfile -File $packer -ProjectFrom $flat -OutDir $out 2>&1 | Out-String
		$project = Join-Path $out "nemo-anywhere.evb"
		if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $project)) {
			Write-Host "FAIL: pack-portable.ps1 -ProjectFrom (exit $LASTEXITCODE)"
			Write-Host $said.TrimEnd()
			return 1
		}
		$text = [IO.File]::ReadAllText($project)

		$cases = @(
			@{ Name = "as written"; Text = $text; Good = $true },
			@{ Name = "sharing on"; Text = $text.Replace('<ShareVirtualSystem>false<', '<ShareVirtualSystem>true<'); Good = $false },
			@{ Name = "no sharing option"; Text = ($text -replace '<ShareVirtualSystem>[^<]*</ShareVirtualSystem>', ''); Good = $false },
			@{ Name = "set twice"; Text = $text.Replace('<Options>', '<Options><ShareVirtualSystem>false</ShareVirtualSystem>'); Good = $false }
		)
		foreach ($case in $cases) {
			if ($case.Name -ne "as written" -and $case.Text -ceq $text) {
				Write-Host "FAIL: $($case.Name): the edit changed nothing"
				$failures++
				continue
			}
			$problems = @(Test-ProjectText -Label $case.Name -Text $case.Text)
			if (($problems.Count -eq 0) -ne $case.Good) {
				$want = if ($case.Good) { "passed" } else { "refused" }
				Write-Host "FAIL: $($case.Name): wanted it $want, got: $($problems -join '; ')"
				$failures++
			}
		}
		## The written tree has to be in it, or the check passed a project for nothing.
		if (-not $text.Contains('<Name>libglib-2.0-0.dll</Name>') -or $text.Contains('<Name>nemo-anywhere.exe</Name>')) {
			Write-Host "FAIL: the project does not list the flat tree as expected"
			$failures++
		}
	} finally {
		Remove-Item -LiteralPath $scratch -Recurse -Force
	}
	return $failures
}

if ($SelfTest) {
	$failures = Invoke-SelfTest
	if ($failures -ne 0) { Write-Host "evb project self-test: $failures failure(s)"; exit 1 }
	Write-Host "evb project self-test: OK"
	exit 0
}

if ($Path.Count -eq 0) { Write-Host "Syntax: test-evb-project.ps1 [-SelfTest] [-Path <evb>...]"; exit 2 }
$all = [System.Collections.Generic.List[string]]::new()
foreach ($p in $Path) { foreach ($problem in @(Test-Project $p)) { $all.Add($problem) } }
if ($all.Count -ne 0) {
	foreach ($problem in $all) { Write-Host "  FAIL: $problem" }
	exit 1
}
Write-Host "  evb project options: OK"
exit 0
