##	Purpose:
##		- Drive a throwaway Windows Sandbox for GUI testing and demo work,
##		  without touching the live desktop session. The sandbox is built from
##		  the host's own Windows image (no second license), keeps no state,
##		  and every -Start is a fresh machine.
##		- Stages a shared folder (cicd/artifacts/sandbox/share) with the app,
##		  the in-sandbox agent and the gui.ps1 driver, generates the .wsb
##		  beside it, launches WindowsSandbox.exe, and hands job scripts across
##		  through jobs\ -> done\ with logs in out\.
##		- The app is either the packed single exe (-Exe) or a whole flattened
##		  build directory (-Dir, e.g. cicd/artifacts/win-flat). The directory
##		  form is the one to use while working - it skips the pack, so a
##		  rebuild can be looked at straight away.
##		- Verbs:
##		   -Start        stage + launch, wait for the agent heartbeat
##		   -Run <ps1>    queue a job script inside, wait for it, print its log
##		   -Smoke        built-in job: launch the app in there, screenshot it
##		   -Status       heartbeat age
##		   -Stop         end the sandbox
##		- -NoNetwork cuts the machine off the network, for testing what the app
##		  says when there is none. The mapped folder still works.
##		- Syntax:
##		  pwsh cicd/win/sandbox.ps1 -Start [-Exe <packed exe> | -Dir <build dir>] [-NoNetwork]
##		  pwsh cicd/win/sandbox.ps1 -Run <script.ps1> [-TimeoutSec <n>]
##	History:
##		- 2026-09-02: -Dir, to run an unpacked build.
##		- 2026-08-31: -NoNetwork.
##		- 2026-08-30: Created (backlog: GUI testing without touching the live session).

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[switch]$Start,
	[string]$Run = "",
	[switch]$Smoke,
	[switch]$Status,
	[switch]$Stop,
	[string]$Exe = "",
	[string]$Dir = "",
	[int]$TimeoutSec = 300,
	[switch]$NoNetwork,
	[switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$Root    = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$SbxDir  = Join-Path $Root "cicd\artifacts\sandbox"
$Share   = Join-Path $SbxDir "share"
$WsbPath = Join-Path $SbxDir "nemo.wsb"
$PidPath = Join-Path $SbxDir "sandbox.pid"
$Beat    = Join-Path $Share "out\agent.txt"

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

function fBeatAge {
	if (-not (Test-Path -LiteralPath $Beat)) { return $null }
	return ((Get-Date) - (Get-Item -LiteralPath $Beat).LastWriteTime).TotalSeconds
}

function fQueueJob {
	param([string]$SourcePath, [string]$BaseName)
	$jobName = "{0:yyyyMMdd-HHmmss}-{1}.ps1" -f (Get-Date), $BaseName
	Copy-Item -LiteralPath $SourcePath -Destination (Join-Path $Share "jobs\$jobName") -Force
	$doneMark = Join-Path $Share "done\$jobName"
	$log = Join-Path $Share ("out\" + [IO.Path]::GetFileNameWithoutExtension($jobName) + ".log")
	fEcho "Job queued: $jobName"
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while (-not (Test-Path -LiteralPath $doneMark)) {
		if ((Get-Date) -gt $deadline) { fDie "job still running after ${TimeoutSec}s: $jobName" }
		Start-Sleep -Seconds 1
	}
	fEcho "Job done, log:"
	if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log | ForEach-Object { fEcho_Clean "  $_" } }
	return $log
}

if ($Start) {
	if (-not (Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue)) { fDie "Windows Sandbox is not available (feature off, or reboot pending)" }
	if ($Dir -and $Exe) { fDie "-Exe and -Dir are alternatives; pass one" }
	if ($Dir) {
		if (-not (Test-Path -LiteralPath (Join-Path $Dir "nemo-anywhere.exe"))) { fDie "no nemo-anywhere.exe in $Dir (stage the build first)" }
	} else {
		if (-not $Exe) { $Exe = Join-Path $Root "cicd\artifacts\win-portable\nemo-anywhere.exe" }
		if (-not (Test-Path -LiteralPath $Exe)) { fDie "packed exe not found: $Exe (run the pack first)" }
	}

	fEcho_Clean
	fEcho "Staging share"
	foreach ($d in @($SbxDir, $Share, (Join-Path $Share "jobs"), (Join-Path $Share "out"), (Join-Path $Share "done"))) {
		if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
	}
	Remove-Item -Path (Join-Path $Share "jobs\*"), (Join-Path $Share "done\*") -Force -ErrorAction SilentlyContinue
	if (Test-Path -LiteralPath $Beat) { Remove-Item -LiteralPath $Beat -Force }
	## Only one of the two forms is left staged, so the agent cannot pick up a
	## leftover from the previous run.
	$AppTree = Join-Path $Share "app"
	$AppOne  = Join-Path $Share "nemo-anywhere.exe"
	Remove-Item -LiteralPath $AppTree -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath $AppOne -Force -ErrorAction SilentlyContinue
	if ($Dir) {
		$mb = [int]((Get-ChildItem -LiteralPath $Dir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
		fEcho "Copying build dir ($mb MB)"
		Copy-Item -LiteralPath $Dir -Destination $AppTree -Recurse -Force
	} else {
		Copy-Item -LiteralPath $Exe -Destination $AppOne -Force
	}
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot "sandbox-agent.ps1") -Destination (Join-Path $Share "agent.ps1") -Force
	Copy-Item -LiteralPath (Join-Path $PSScriptRoot "gui.ps1") -Destination (Join-Path $Share "gui.ps1") -Force

	## Cut the machine off the network, for testing what nemo says when there is
	## none. The mapped folder still works - it is not a network share.
	$net = if ($NoNetwork) { "`n`t<Networking>Disable</Networking>" } else { "" }

	## The logon command waits for the mapped folder - it has raced the agent before.
	$wsb = @"
<Configuration>
	<MemoryInMB>4096</MemoryInMB>$net
	<MappedFolders>
		<MappedFolder>
			<HostFolder>$Share</HostFolder>
			<SandboxFolder>C:\share</SandboxFolder>
			<ReadOnly>false</ReadOnly>
		</MappedFolder>
	</MappedFolders>
	<LogonCommand>
		<Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "for(`$i=0;`$i -lt 120 -and -not (Test-Path C:\share\agent.ps1);`$i++){Start-Sleep 1}; &amp; C:\share\agent.ps1"</Command>
	</LogonCommand>
</Configuration>
"@
	Set-Content -LiteralPath $WsbPath -Value $wsb -Encoding utf8

	fEcho "Launching sandbox"
	$p = Start-Process WindowsSandbox.exe -ArgumentList "`"$WsbPath`"" -PassThru
	Set-Content -LiteralPath $PidPath -Value $p.Id

	fEcho "Waiting for agent heartbeat (up to ${TimeoutSec}s)"
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while (-not (Test-Path -LiteralPath $Beat)) {
		if ((Get-Date) -gt $deadline) { fDie "no heartbeat after ${TimeoutSec}s" }
		Start-Sleep -Seconds 2
	}
	fEcho "Sandbox up"
	exit 0
}

if ($Run) {
	if (-not (Test-Path -LiteralPath $Run)) { fDie "job script not found: $Run" }
	$age = fBeatAge
	if ($null -eq $age -or $age -gt 30) { fDie "agent heartbeat missing or stale - is the sandbox running? (-Start)" }
	fQueueJob $Run ([IO.Path]::GetFileNameWithoutExtension($Run)) | Out-Null
	exit 0
}

if ($Smoke) {
	$age = fBeatAge
	if ($null -eq $age -or $age -gt 30) { fDie "agent heartbeat missing or stale - is the sandbox running? (-Start)" }
	$jobSrc = Join-Path $SbxDir "smoke-job.ps1"
	## First launch in there is slow: the packer maps its image and Defender scans a fresh 38 MB exe.
	Set-Content -LiteralPath $jobSrc -Encoding utf8 -Value @'
Start-Process $env:SBX_EXE
$p = $null
$deadline = (Get-Date).AddSeconds(30)
while (-not $p -and (Get-Date) -lt $deadline) { Start-Sleep 1; $p = Get-Process nemo-anywhere -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $p) { "no process"; exit 1 }
"pid=$($p.Id)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $env:SBX_GUI wait $p.Id 120
if ($LASTEXITCODE -ne 0) { "no window"; exit 1 }
Start-Sleep 3
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $env:SBX_GUI rect $p.Id
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $env:SBX_GUI raise $p.Id
Start-Sleep 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $env:SBX_GUI shot $p.Id (Join-Path $env:SBX_OUT "smoke.png")
Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
'@
	$log = fQueueJob $jobSrc "smoke"
	$png = Join-Path $Share "out\smoke.png"
	if (Test-Path -LiteralPath $png) { fEcho "Screenshot: $png" } else { fDie "no screenshot came back" }
	exit 0
}

if ($Status) {
	$age = fBeatAge
	if ($null -eq $age) { fEcho "no heartbeat - sandbox not running or agent never started" }
	elseif ($age -gt 30) { fEcho ("heartbeat stale ({0:n0}s old)" -f $age) }
	else { fEcho ("sandbox up (heartbeat {0:n1}s old)" -f $age) }
	exit 0
}

if ($Stop) {
	## Only one sandbox can exist at a time, so killing by name cannot hit anyone else's.
	if (Test-Path -LiteralPath $PidPath) {
		$sbPid = [int](Get-Content -LiteralPath $PidPath)
		Stop-Process -Id $sbPid -Force -ErrorAction SilentlyContinue
		Remove-Item -LiteralPath $PidPath -Force
	}
	Get-Process WindowsSandbox, WindowsSandboxClient, WindowsSandboxRemoteSession -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
	fEcho "sandbox stopped"
	exit 0
}

fDie "nothing to do - use -Start, -Run <ps1>, -Smoke, -Status or -Stop (see -Help)"
