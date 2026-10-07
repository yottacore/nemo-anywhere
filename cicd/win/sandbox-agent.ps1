##	Purpose:
##		- Runs inside Windows Sandbox as the logon command, straight off the
##		  mapped share. Copies the packed exe to local disk (the mapped folder
##		  is slow to execute from), then loops: pick up share\jobs\*.ps1
##		  oldest-first, run each on the sandbox desktop with powershell.exe,
##		  log to share\out\<name>.log, move the finished job to share\done.
##		- Heartbeat: share\out\agent.txt, rewritten every poll, so the host
##		  side can tell the sandbox is up.
##		- Jobs get SBX_EXE (local exe copy), SBX_SHARE, SBX_OUT and SBX_GUI
##		  (the gui.ps1 driver) in their environment.
##	History:
##		- 2026-08-30: Created (backlog: GUI testing without touching the live session).

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

$Share = Split-Path -Parent $PSCommandPath
$Jobs  = Join-Path $Share "jobs"
$Out   = Join-Path $Share "out"
$Done  = Join-Path $Share "done"
foreach ($d in @($Jobs, $Out, $Done)) { if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null } }

$AppDir = Join-Path $env:LOCALAPPDATA "nemo-sbx"
if (-not (Test-Path -LiteralPath $AppDir)) { New-Item -ItemType Directory -Path $AppDir | Out-Null }
## Either a packed single exe or a whole flattened build dir. The copy is local
## so the app is not reading itself over the mapped folder.
$treeSrc = Join-Path $Share "app"
$exeSrc  = Join-Path $Share "nemo-anywhere.exe"
if (Test-Path -LiteralPath $treeSrc) {
	Copy-Item -Path (Join-Path $treeSrc "*") -Destination $AppDir -Recurse -Force
} elseif (Test-Path -LiteralPath $exeSrc) {
	Copy-Item -LiteralPath $exeSrc -Destination $AppDir -Force
}

$env:SBX_SHARE = $Share
$env:SBX_OUT   = $Out
$env:SBX_EXE   = Join-Path $AppDir "nemo-anywhere.exe"
$env:SBX_GUI   = Join-Path $Share "gui.ps1"

$beat = Join-Path $Out "agent.txt"
while ($true) {
	Set-Content -LiteralPath $beat -Value ("{0:yyyy-MM-dd HH:mm:ss}" -f (Get-Date))
	$job = Get-ChildItem -Path (Join-Path $Jobs "*.ps1") -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1
	if ($job) {
		$log = Join-Path $Out ($job.BaseName + ".log")
		powershell.exe -NoProfile -ExecutionPolicy Bypass -File $job.FullName > $log 2>&1
		Add-Content -LiteralPath $log -Value "exit=$LASTEXITCODE"
		Move-Item -LiteralPath $job.FullName -Destination $Done -Force
	}
	Start-Sleep -Seconds 1
}
