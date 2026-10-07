##	Purpose:
##		- Pack the staged native Windows runtime bundle (cicd/artifacts/win-run)
##		  into ONE self-contained exe via Enigma Virtual Box: every dll, schema,
##		  icon, and theme rides inside the exe in an in-memory virtual file
##		  system - no library folder, no launcher, nothing extracted at run time.
##		- Two steps:
##		   1. Flatten the app/ + mingw64/ bundle into a GTK prefix layout under
##		      win-flat/: exe + dlls at the root, lib/ share/ etc/ beside them.
##		      GLib/GTK/gdk-pixbuf/fontconfig on Windows resolve their data dirs
##		      relative to their own dll, so this layout needs no env wiring at
##		      all - the flat tree double-click-runs as-is, and is also the layout
##		      the release .zip contract in project/design.md expects.
##		   2. Generate an .evb project over that tree and run enigmavbconsole.
##		      Virtual exes stay runnable (nemo spawns its helper exes) and the
##		      virtual system is shared with child processes.
##		- Output: <out-dir>\nemo-anywhere.exe, then a --version smoke with a
##		  bare System32-only PATH proves it truly self-contained.
##		- -FlattenOnly stops after step 1, which is what the sandbox wants: it
##		  runs the flat tree directly, so the pack is a few minutes wasted.
##		- Syntax:
##		  pwsh cicd/win/pack-portable.ps1 [-StageDir <dir>] [-OutDir <dir>] [-FlattenOnly]
##	History:
##		- 2026-09-03: -FlattenOnly.
##		- 2026-08-02: Created (backlog: ultra-portable single-exe Windows).

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[string]$StageDir = "",
	[string]$OutDir = "",
	[switch]$FlattenOnly,
	[switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ExeName = "nemo-anywhere"
if (-not $StageDir) { $StageDir = Join-Path $Root "cicd\artifacts\win-run" }
if (-not $OutDir)   { $OutDir   = Join-Path $Root "cicd\artifacts\win-portable" }
$FlatDir = Join-Path $Root "cicd\artifacts\win-flat"

$script:WasLastEchoBlank = $false
function fEcho_Clean {
	param([string]$Msg = "")
	if ($Msg) { Write-Host $Msg; $script:WasLastEchoBlank = $false }
	elseif (-not $script:WasLastEchoBlank) { Write-Host ""; $script:WasLastEchoBlank = $true }
}
function fEcho { param([string]$Msg = ""); if ($Msg) { fEcho_Clean "[ $Msg ]" } else { fEcho_Clean } }
function fDie  { param([string]$Msg); fEcho "FAILED: $Msg"; exit 1 }

if ($Help) {
	$inBlock = $false
	foreach ($line in (Get-Content -LiteralPath $PSCommandPath)) {
		if ($line -match '^##\tPurpose:') { $inBlock = $true }
		if ($inBlock) {
			if ($line -match '^##\tHistory:') { break }
			$line -replace '^##\t?', ''
		}
	}
	exit 0
}

## Locate the EVB console packer (standard install dirs, then PATH).
function fFindEvb {
	$cands = @(
		"C:\Program Files (x86)\Enigma Virtual Box\enigmavbconsole.exe",
		"C:\Program Files\Enigma Virtual Box\enigmavbconsole.exe"
	)
	foreach ($c in $cands) { if (Test-Path -LiteralPath $c) { return $c } }
	$cmd = Get-Command enigmavbconsole -ErrorAction SilentlyContinue
	if ($cmd) { return $cmd.Source }
	return $null
}

## Flatten app/ + mingw64/ into a prefix-layout tree: binaries at the root,
## lib/ share/ etc/ beside them. robocopy /MIR keeps reruns clean.
function fFlatten {
	if (-not (Test-Path -LiteralPath (Join-Path $StageDir "app\$ExeName.exe"))) {
		fDie "no staged bundle at $StageDir (run the stage step first)"
	}
	fEcho "Flattening bundle -> $FlatDir"
	if (Test-Path -LiteralPath $FlatDir) { Remove-Item -LiteralPath $FlatDir -Recurse -Force }
	New-Item -ItemType Directory -Path $FlatDir | Out-Null
	## -Recurse: without it any subfolder is created empty and its contents are
	## silently dropped out of the packed exe.
	Copy-Item -Path (Join-Path $StageDir "app\*") -Destination $FlatDir -Recurse -Force
	Copy-Item -Path (Join-Path $StageDir "mingw64\bin\*") -Destination $FlatDir -Recurse -Force
	foreach ($d in @("lib", "share", "etc")) {
		$src = Join-Path $StageDir "mingw64\$d"
		if (-not (Test-Path -LiteralPath $src)) { continue }
		& robocopy $src (Join-Path $FlatDir $d) /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
		if ($LASTEXITCODE -ge 8) { fDie "flatten copy failed for $d (robocopy exit $LASTEXITCODE)" }
	}
}

## Emit the .evb project: the flat tree as the virtual %DEFAULT FOLDER%, the
## main exe as input. EVB's project "xml" has a literal empty root tag, so it
## is string-built, not from an XML writer.
function fXmlEsc { param([string]$s); $s -replace '&', '&amp;' -replace '<', '&lt;' -replace '>', '&gt;' }

function fEmitTree {
	param([Text.StringBuilder]$Sb, [string]$Dir, [string]$SkipFile = "")
	foreach ($f in (Get-ChildItem -LiteralPath $Dir -File | Sort-Object Name)) {
		if ($SkipFile -and $f.FullName -eq $SkipFile) { continue }
		[void]$Sb.AppendLine("<File><Type>2</Type><Name>$(fXmlEsc $f.Name)</Name><File>$(fXmlEsc $f.FullName)</File><ActiveX>false</ActiveX><ActiveXInstall>false</ActiveXInstall><Action>0</Action><OverwriteDateTime>false</OverwriteDateTime><OverwriteAttributes>false</OverwriteAttributes><PassCommandLine>false</PassCommandLine><HideFromDialogs>0</HideFromDialogs></File>")
	}
	foreach ($d in (Get-ChildItem -LiteralPath $Dir -Directory | Sort-Object Name)) {
		[void]$Sb.AppendLine("<File><Type>3</Type><Name>$(fXmlEsc $d.Name)</Name><Files>")
		fEmitTree -Sb $Sb -Dir $d.FullName
		[void]$Sb.AppendLine("</Files></File>")
	}
}

function fWriteProject {
	param([string]$ProjPath, [string]$InExe, [string]$OutExe)
	$sb = [Text.StringBuilder]::new()
	[void]$sb.AppendLine('<?xml version="1.0" encoding="windows-1252"?>')
	[void]$sb.AppendLine('<>')
	[void]$sb.AppendLine("<InputFile>$(fXmlEsc $InExe)</InputFile>")
	[void]$sb.AppendLine("<OutputFile>$(fXmlEsc $OutExe)</OutputFile>")
	[void]$sb.AppendLine('<Files>')
	[void]$sb.AppendLine('<Enabled>true</Enabled>')
	[void]$sb.AppendLine('<DeleteExtractedOnExit>false</DeleteExtractedOnExit>')
	[void]$sb.AppendLine('<CompressFiles>true</CompressFiles>')
	[void]$sb.AppendLine('<Files>')
	[void]$sb.AppendLine('<File><Type>3</Type><Name>%DEFAULT FOLDER%</Name><Files>')
	fEmitTree -Sb $sb -Dir $FlatDir -SkipFile $InExe
	[void]$sb.AppendLine('</Files></File>')
	[void]$sb.AppendLine('</Files>')
	[void]$sb.AppendLine('</Files>')
	[void]$sb.AppendLine('<Registries>')
	[void]$sb.AppendLine('<Enabled>false</Enabled>')
	[void]$sb.AppendLine('<Registries>')
	foreach ($hive in @("Classes", "User", "Machine", "Users", "Config")) {
		[void]$sb.AppendLine("<Registry><Type>1</Type><Virtual>true</Virtual><Name>$hive</Name><ValueType>0</ValueType><Value/><Registries/></Registry>")
	}
	[void]$sb.AppendLine('</Registries>')
	[void]$sb.AppendLine('</Registries>')
	[void]$sb.AppendLine('<Packaging>')
	[void]$sb.AppendLine('<Enabled>false</Enabled>')
	[void]$sb.AppendLine('</Packaging>')
	[void]$sb.AppendLine('<Options>')
	[void]$sb.AppendLine('<ShareVirtualSystem>true</ShareVirtualSystem>')
	[void]$sb.AppendLine('<MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>')
	[void]$sb.AppendLine('<TemporaryFileMask/>')
	[void]$sb.AppendLine('<AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>')
	[void]$sb.AppendLine('<ProcessesOfAnyPlatforms>false</ProcessesOfAnyPlatforms>')
	[void]$sb.AppendLine('</Options>')
	[void]$sb.AppendLine('<Storage>')
	[void]$sb.AppendLine('<Files>')
	[void]$sb.AppendLine('<Enabled>false</Enabled>')
	[void]$sb.AppendLine('<Folder>%DEFAULT TEMP FOLDER%\</Folder>')
	[void]$sb.AppendLine('<RandomFileNames>false</RandomFileNames>')
	[void]$sb.AppendLine('<EncryptContent>false</EncryptContent>')
	[void]$sb.AppendLine('</Files>')
	[void]$sb.AppendLine('</Storage>')
	[void]$sb.AppendLine('</>')
	[IO.File]::WriteAllText($ProjPath, $sb.ToString(), [Text.Encoding]::GetEncoding(1252))
}

function fMain {
	if ($FlattenOnly) {
		fFlatten
		fEcho "OK: flat tree: $FlatDir"
		return
	}

	$evb = fFindEvb
	if (-not $evb) { fDie "Enigma Virtual Box not found (enigmavbconsole.exe) - install it first" }

	fFlatten

	New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
	$inExe   = Join-Path $FlatDir "$ExeName.exe"
	$outExe  = Join-Path $OutDir "$ExeName.exe"
	$projEvb = Join-Path $OutDir "$ExeName.evb"
	if (Test-Path -LiteralPath $outExe) { Remove-Item -LiteralPath $outExe -Force }

	$nFiles = (Get-ChildItem -LiteralPath $FlatDir -Recurse -File).Count
	fEcho "Writing EVB project ($nFiles files) -> $projEvb"
	fWriteProject -ProjPath $projEvb -InExe $inExe -OutExe $outExe

	fEcho "Packing (this can take a few minutes)..."
	## enigmavbconsole writes the output exe at the very end, but on some headless
	## runners it then never returns - a plain `& $evb` blocks forever after the
	## pack already succeeded. So wait on the OUTPUT, not the process: once the exe
	## appears, give it a short grace to finish flushing, then reap a console that
	## didn't self-exit. A hard cap still bounds a genuinely stuck pack.
	## Quoted: Start-Process joins ArgumentList on spaces, so a project path under
	## a spaced folder would arrive as two arguments.
	$proc     = Start-Process -FilePath $evb -ArgumentList @("`"$projEvb`"") -PassThru -NoNewWindow
	$capSec   = 900
	$waited   = 0
	$lastSize = -1
	$stableFor = 0
	while (-not $proc.HasExited) {
		Start-Sleep -Seconds 3; $waited += 3

		## Reap on a size that has stopped moving, not on a fixed grace: the exe
		## appears as soon as EVB opens it and a fixed wait can cut a large pack
		## off mid-write.
		## Not there yet is the normal case for the first few passes.
		$item = Get-Item -LiteralPath $outExe -ErrorAction SilentlyContinue
		$size = if ($item) { $item.Length } else { -1 }
		if ($size -ge 0 -and $size -eq $lastSize) { $stableFor += 3 } else { $stableFor = 0 }
		$lastSize = $size

		if ($size -gt 0 -and $stableFor -ge 15) {
			fEcho "output settled at $size bytes; reaping enigmavbconsole (did not self-exit)"
			try { $proc.Kill() } catch { fEcho "enigmavbconsole had already exited" }
			break
		}
		if ($waited -ge $capSec) {
			try { $proc.Kill() } catch { fEcho "enigmavbconsole had already exited" }
			fDie "enigmavbconsole timed out after ${capSec}s"
		}
	}
	if ($proc.HasExited -and $proc.ExitCode -ne 0 -and -not (Test-Path -LiteralPath $outExe)) {
		fDie "enigmavbconsole failed (exit $($proc.ExitCode))"
	}
	if (-not (Test-Path -LiteralPath $outExe)) { fDie "packer reported success but no output at $outExe" }

	## Smoke with a bare PATH: if anything leaks outside the virtual FS, it fails here.
	$saved = $env:PATH
	$savedDialog = $env:NEMO_NO_CRASH_DIALOG
	try {
		$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
		## A crash here would otherwise stop the run behind a modal box.
		$env:NEMO_NO_CRASH_DIALOG = "1"
		$out = (& $outExe --version 2>&1 | Out-String).Trim()
	} finally { $env:PATH = $saved; $env:NEMO_NO_CRASH_DIALOG = $savedDialog }
	if ($LASTEXITCODE -ne 0) { fDie "packed exe smoke failed (exit $LASTEXITCODE): $out" }

	$size = "{0:N1} MB" -f ((Get-Item -LiteralPath $outExe).Length / 1MB)
	fEcho "OK: packed single exe: $outExe ($size); smoke: $out"
}

fMain
