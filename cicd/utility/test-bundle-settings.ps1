#!/usr/bin/env pwsh

##	Purpose:
##		- Check that a Windows bundle has the GTK settings and font config from
##		  cicd/win/bundle-etc under its etc/, byte for byte. Without them GTK
##		  draws with Adwaita and a thin substitute font, and the zip and the
##		  copy install.ps1 put in place looked like that while the single exe
##		  had Fluent and Segoe UI (2026101007562511).
##		- Run on the same 3 forms as test-bundle-exes.ps1: the zip in the
##		  packages stage, the flat tree in pack-portable.ps1, and what
##		  install.ps1 put in place in test-install-acl.ps1.
##		- With no -Path it reads this version's zip in cicd/artifacts/release,
##		  and exits 77 when there is none.
##		- -SelfTest makes small bundles with the files, without them, changed and
##		  in the wrong place, and checks each answer. Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-bundle-settings.ps1 [-SelfTest] [-Path <zip or folder>...]
##	Test ID: rk50ewvs

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

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$EtcSource = Join-Path $Root "cicd/win/bundle-etc"

## Path under the bundle root -> bytes, for every file the bundles must have.
function Get-WantedFile {
	param([Parameter(Mandatory)][string]$Source)
	$wanted = @{}
	$base = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\', '/')
	foreach ($file in @(Get-ChildItem -LiteralPath $base -Recurse -File -Force)) {
		$rel = "etc/" + $file.FullName.Substring($base.Length + 1).Replace('\', '/')
		$wanted[$rel] = [System.IO.File]::ReadAllBytes($file.FullName)
	}
	return $wanted
}

## The bytes at a path under the bundle root, or $null. A zip with one folder at
## the top has its root inside that folder.
function Get-BundleContent {
	param([Parameter(Mandatory)][string]$Bundle, [Parameter(Mandatory)][string[]]$Names)
	$found = @{}
	if (Test-Path -LiteralPath $Bundle -PathType Container) {
		foreach ($name in $Names) {
			$file = Join-Path $Bundle $name
			if (Test-Path -LiteralPath $file -PathType Leaf) { $found[$name] = [System.IO.File]::ReadAllBytes($file) }
		}
		return $found
	}
	$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Bundle).Path)
	try {
		$entries = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith('/') })
		$tops = @($entries | ForEach-Object { $_.FullName.Replace('\', '/').Split('/')[0] } | Sort-Object -Unique)
		$cut = 0
		if ($tops.Count -eq 1 -and @($entries | Where-Object { $_.FullName.Replace('\', '/').Contains('/') }).Count -eq $entries.Count) {
			$cut = $tops[0].Length + 1
		}
		foreach ($entry in $entries) {
			$rel = $entry.FullName.Replace('\', '/').Substring($cut)
			if ($Names -cnotcontains $rel) { continue }
			$stream = $entry.Open()
			try {
				$copy = [System.IO.MemoryStream]::new()
				$stream.CopyTo($copy)
				$found[$rel] = $copy.ToArray()
			} finally {
				$stream.Dispose()
			}
		}
	} finally {
		$zip.Dispose()
	}
	return $found
}

## The problems with one bundle, one line each; none when it is good.
function Test-Bundle {
	param([Parameter(Mandatory)][string]$Bundle, [Parameter(Mandatory)][hashtable]$Wanted)
	$label = Split-Path -Leaf $Bundle
	if (-not (Test-Path -LiteralPath $Bundle)) { return @("${label}: not there") }
	$problems = [System.Collections.Generic.List[string]]::new()
	## Nothing to compare would pass every bundle.
	if ($Wanted.Count -eq 0) { $problems.Add("nothing in cicd/win/bundle-etc to look for"); return $problems }
	$names = [string[]]@($Wanted.Keys | Sort-Object)
	$have = Get-BundleContent -Bundle $Bundle -Names $names
	foreach ($name in $names) {
		if (-not $have.ContainsKey($name)) {
			$problems.Add("${label}: no ${name}")
		} elseif (-not [System.Linq.Enumerable]::SequenceEqual([byte[]]$have[$name], [byte[]]$Wanted[$name])) {
			$problems.Add("${label}: ${name} differs from cicd/win/bundle-etc")
		}
	}
	Write-Host "  ${label}: $($names.Count) file(s) looked for"
	return $problems
}

function Invoke-SelfTest {
	$scratch = Join-Path ([System.IO.Path]::GetTempPath()) "test_bundle-settings_$(Get-Date -Format 'yyyyMMdd-HHmmssff')"
	$failures = 0
	try {
		$source = Join-Path $scratch "bundle-etc"
		New-Item -ItemType Directory -Path (Join-Path $source "gtk-3.0") -Force | Out-Null
		New-Item -ItemType Directory -Path (Join-Path $source "fonts/conf.d") -Force | Out-Null
		[System.IO.File]::WriteAllText((Join-Path $source "gtk-3.0/settings.ini"), "[Settings]`ngtk-theme-name = Fluent`n")
		[System.IO.File]::WriteAllText((Join-Path $source "fonts/conf.d/99-x.conf"), "<fontconfig/>`n")
		$wanted = Get-WantedFile $source

		$tree = Join-Path $scratch "nemo-anywhere-0.0.0-windows-x86_64"
		New-Item -ItemType Directory -Path (Join-Path $tree "etc") -Force | Out-Null
		[System.IO.File]::WriteAllText((Join-Path $tree "nemo-anywhere.exe"), "app")
		Copy-Item -Path (Join-Path $source "*") -Destination (Join-Path $tree "etc") -Recurse -Force
		$ini = Join-Path $tree "etc/gtk-3.0/settings.ini"
		$good = [System.IO.File]::ReadAllBytes($ini)

		$cases = @(
			@{ Name = "good"; Good = $true; Break = { } },
			@{ Name = "no settings.ini"; Good = $false; Break = { Remove-Item -LiteralPath $ini } },
			@{ Name = "other theme"; Good = $false; Break = { [System.IO.File]::WriteAllText($ini, "[Settings]`ngtk-theme-name = Adwaita`n") } },
			@{ Name = "CRLF"; Good = $false; Break = { [System.IO.File]::WriteAllText($ini, "[Settings]`r`ngtk-theme-name = Fluent`r`n") } },
			@{ Name = "at the root"; Good = $false; Break = {
				Move-Item -LiteralPath (Join-Path $tree "etc/gtk-3.0") -Destination (Join-Path $tree "gtk-3.0") } }
		)
		foreach ($case in $cases) {
			& $case.Break
			$zip = Join-Path $scratch "case.zip"
			Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
			[System.IO.Compression.ZipFile]::CreateFromDirectory($tree, $zip, [System.IO.Compression.CompressionLevel]::Fastest, $true)
			foreach ($bundle in @($tree, $zip)) {
				$got = @(Test-Bundle -Bundle $bundle -Wanted $wanted)
				$kind = if ($bundle -eq $zip) { "zip" } else { "folder" }
				if ($case.Good -and $got.Count -ne 0) {
					Write-Host "[ FAILED: ${kind}, $($case.Name): refused: $($got -join '; ') ]"; $failures++
				} elseif (-not $case.Good -and $got.Count -eq 0) {
					Write-Host "[ FAILED: ${kind}, $($case.Name): let through ]"; $failures++
				}
			}
			$moved = Join-Path $tree "gtk-3.0"
			if (Test-Path -LiteralPath $moved) { Move-Item -LiteralPath $moved -Destination (Join-Path $tree "etc/gtk-3.0") }
			[System.IO.File]::WriteAllBytes($ini, $good)
		}

		## A flat zip, and an empty source folder.
		$flat = Join-Path $scratch "flat.zip"
		[System.IO.Compression.ZipFile]::CreateFromDirectory($tree, $flat)
		if (@(Test-Bundle -Bundle $flat -Wanted $wanted).Count -ne 0) { Write-Host "[ FAILED: a flat zip was refused ]"; $failures++ }
		if (@(Test-Bundle -Bundle $tree -Wanted @{}).Count -eq 0) { Write-Host "[ FAILED: nothing to look for was let through ]"; $failures++ }
	} finally {
		Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
	}
	if ($failures) {
		Write-Host "[ FAILED: bundle settings check self-test, ${failures} problem(s) ]"
		return 1
	}
	Write-Host "[ OK: bundle settings check self-test ]"
	return 0
}

Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath)$(if ($SelfTest) { ' -SelfTest' }) ]"

if ($SelfTest) { exit (Invoke-SelfTest) }

if ($Path.Count -eq 0) {
	$meson = Get-Content -LiteralPath (Join-Path $Root "source/meson.build") -Raw
	$version = if ($meson -match "(?<![_\w])version\s*:\s*'([^']+)'") { $Matches[1] } else { "" }
	$zip = Join-Path $Root "cicd/artifacts/release/nemo-anywhere-${version}-windows-x86_64.zip"
	if (-not $version -or -not (Test-Path -LiteralPath $zip)) {
		Write-Host "[ bundle settings check skipped: no nemo-anywhere-${version}-windows-x86_64.zip in cicd/artifacts/release ]"
		exit 77
	}
	$Path = @($zip)
}

$wanted = Get-WantedFile $EtcSource
$problems = [System.Collections.Generic.List[string]]::new()
foreach ($bundle in $Path) {
	foreach ($line in @(Test-Bundle -Bundle $bundle -Wanted $wanted)) { $problems.Add($line) }
}
foreach ($line in $problems) { Write-Host "FAILED: $line" }
if ($problems.Count) {
	Write-Host "[ FAILED: bundle settings check, a bundle is missing the GTK settings or font config ]"
	exit 1
}
Write-Host "[ OK: bundle settings check, $($Path.Count) bundle(s) ]"
exit 0
