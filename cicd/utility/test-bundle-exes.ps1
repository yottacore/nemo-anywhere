#!/usr/bin/env pwsh

##	Purpose:
##		- Check that a Windows bundle has no program in it but the app's own
##		  nemo-anywhere.exe, at the bundle's root. Anything else the app starts
##		  is on disk outside the bundle and goes through nemo-launch-win32.c
##		  (2026100917220603). A program packed in the single exe can't load its
##		  libraries under hooking tools like MacType.
##		- The zip, which install.ps1 and the setup exe are made from, is read in
##		  the packages stage. pack-portable.ps1 runs it on the flat tree it is
##		  about to pack into the single exe, and test-install-acl.ps1 on what
##		  install.ps1 put in place.
##		- With no -Path it reads this version's zip in cicd/artifacts/release,
##		  and exits 77 when there is none.
##		- -SelfTest makes small bundles with and without another exe and checks
##		  each answer. Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-bundle-exes.ps1 [-SelfTest] [-Path <zip or folder>...]
##	Test ID: rjwc3jkm

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
Add-Type -AssemblyName System.IO.Compression.FileSystem

$AppExe = "nemo-anywhere.exe"

## Every file in the bundle, as a path from its root with forward slashes. A zip
## with one folder at the top has its root inside that folder.
function Get-BundleFile {
	param([Parameter(Mandatory)][string]$Bundle)
	if (Test-Path -LiteralPath $Bundle -PathType Container) {
		$base = (Resolve-Path -LiteralPath $Bundle).Path.TrimEnd('\', '/')
		return @(Get-ChildItem -LiteralPath $base -Recurse -File -Force |
			ForEach-Object { $_.FullName.Substring($base.Length + 1).Replace('\', '/') })
	}
	$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Bundle).Path)
	try {
		$names = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith('/') } |
			ForEach-Object { $_.FullName.Replace('\', '/') })
	} finally {
		$zip.Dispose()
	}
	$tops = @($names | ForEach-Object { $_.Split('/')[0] } | Sort-Object -Unique)
	if ($tops.Count -eq 1 -and @($names | Where-Object { $_.Contains('/') }).Count -eq $names.Count) {
		$cut = $tops[0].Length + 1
		$names = @($names | ForEach-Object { $_.Substring($cut) })
	}
	return $names
}

## The problems with one bundle, one line each; none when it is good.
function Test-Bundle {
	param([Parameter(Mandatory)][string]$Bundle)
	$label = Split-Path -Leaf $Bundle
	if (-not (Test-Path -LiteralPath $Bundle)) { return @("${label}: not there") }
	$files = @(Get-BundleFile $Bundle)
	$problems = [System.Collections.Generic.List[string]]::new()
	if ($files.Count -eq 0) { $problems.Add("${label}: no files listed") }
	if (-not ($files -ccontains $AppExe)) { $problems.Add("${label}: no ${AppExe} at its root") }
	foreach ($file in $files) {
		if ($file.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase) -and $file -cne $AppExe) {
			$problems.Add("${label}: has ${file}")
		}
	}
	Write-Host "  ${label}: $($files.Count) file(s) looked at"
	return $problems
}

function Invoke-SelfTest {
	$scratch = Join-Path ([System.IO.Path]::GetTempPath()) "test_bundle-exes_$(Get-Date -Format 'yyyyMMdd-HHmmssff')"
	$failures = 0
	try {
		$tree = Join-Path $scratch "nemo-anywhere-0.0.0-windows-x86_64"
		New-Item -ItemType Directory -Path (Join-Path $tree "lib/gdk-pixbuf-2.0") -Force | Out-Null
		Set-Content -LiteralPath (Join-Path $tree $AppExe) -Value "app"
		Set-Content -LiteralPath (Join-Path $tree "libglib-2.0-0.dll") -Value "dll"
		Set-Content -LiteralPath (Join-Path $tree "lib/gdk-pixbuf-2.0/loader.dll") -Value "dll"

		$cases = @(
			@{ Name = "good"; Add = $null; Good = $true },
			@{ Name = "spawn helper"; Add = "gspawn-win64-helper.exe"; Good = $false },
			@{ Name = "upper case"; Add = "GDBUS.EXE"; Good = $false },
			@{ Name = "deep"; Add = "lib/gdk-pixbuf-2.0/thumb.exe"; Good = $false },
			@{ Name = "app below the root"; Add = "bin/nemo-anywhere.exe"; Good = $false }
		)
		foreach ($case in $cases) {
			$extra = if ($case.Add) { Join-Path $tree $case.Add } else { $null }
			if ($extra) {
				New-Item -ItemType Directory -Path (Split-Path -Parent $extra) -Force | Out-Null
				Set-Content -LiteralPath $extra -Value "x"
			}
			$zip = Join-Path $scratch "case.zip"
			Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
			[System.IO.Compression.ZipFile]::CreateFromDirectory($tree, $zip, [System.IO.Compression.CompressionLevel]::Fastest, $true)
			foreach ($bundle in @($tree, $zip)) {
				$got = @(Test-Bundle $bundle)
				$kind = if ($bundle -eq $zip) { "zip" } else { "folder" }
				if ($case.Good -and $got.Count -ne 0) {
					Write-Host "[ FAILED: ${kind}, $($case.Name): refused: $($got -join '; ') ]"; $failures++
				} elseif (-not $case.Good -and $got.Count -eq 0) {
					Write-Host "[ FAILED: ${kind}, $($case.Name): let through ]"; $failures++
				}
			}
			if ($extra) { Remove-Item -LiteralPath $extra -Force }
		}

		## A flat zip, and one with no app at all.
		$flat = Join-Path $scratch "flat.zip"
		[System.IO.Compression.ZipFile]::CreateFromDirectory($tree, $flat)
		if (@(Test-Bundle $flat).Count -ne 0) { Write-Host "[ FAILED: a flat zip was refused ]"; $failures++ }
		Remove-Item -LiteralPath (Join-Path $tree $AppExe)
		if (@(Test-Bundle $tree).Count -eq 0) { Write-Host "[ FAILED: a bundle with no app was let through ]"; $failures++ }
	} finally {
		Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
	}
	if ($failures) {
		Write-Host "[ FAILED: bundle exe check self-test, ${failures} problem(s) ]"
		return 1
	}
	Write-Host "[ OK: bundle exe check self-test ]"
	return 0
}

Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath)$(if ($SelfTest) { ' -SelfTest' }) ]"

if ($SelfTest) { exit (Invoke-SelfTest) }

if ($Path.Count -eq 0) {
	$root = Join-Path $PSScriptRoot "..\.."
	$meson = Get-Content -LiteralPath (Join-Path $root "source/meson.build") -Raw
	$version = if ($meson -match "(?<![_\w])version\s*:\s*'([^']+)'") { $Matches[1] } else { "" }
	$zip = Join-Path $root "cicd/artifacts/release/nemo-anywhere-${version}-windows-x86_64.zip"
	if (-not $version -or -not (Test-Path -LiteralPath $zip)) {
		Write-Host "[ bundle exe check skipped: no nemo-anywhere-${version}-windows-x86_64.zip in cicd/artifacts/release ]"
		exit 77
	}
	$Path = @($zip)
}

$problems = [System.Collections.Generic.List[string]]::new()
foreach ($bundle in $Path) {
	foreach ($line in @(Test-Bundle $bundle)) { $problems.Add($line) }
}
foreach ($line in $problems) { Write-Host "FAILED: $line" }
if ($problems.Count) {
	Write-Host "[ FAILED: bundle exe check, a bundle has a program besides the app ]"
	exit 1
}
Write-Host "[ OK: bundle exe check, $($Path.Count) bundle(s) ]"
exit 0
