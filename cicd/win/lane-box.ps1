##	Purpose:
##		- The Windows box's side of the native Windows lane (win/lane.bash). It
##		  is sent into the run's own folder there, and called over ssh.
##		- -Start clones the bundle beside it and starts -Run outside the ssh
##		  session, through Win32_Process Create, so a dropped connection doesn't
##		  stop the gate. Prints "pid=<n>" and writes it to run.pid.
##		- -Run runs cicd-win.ps1 in the clone, with TEMP, TMP and HOST_TEMP in
##		  the run folder, to gate.log, then writes "rc=<n> secs=<n>" to run.rc.
##		  -GateArgs names its switches with commas and no dashes, such as
##		  Gate,Yes,NoSync, since a quoted value that starts with a dash reads
##		  as a parameter name on the way in.
##		- -Stop ends the gate's whole process tree, from run.pid.
##		- -Remove removes the run folder, and the project's run folders beside
##		  it that are older than 2 days with no live process from their run.pid.
##		  Only a folder named <YYYYmmDD-HHMMSS>_<NNN> right under
##		  %TEMP%\nemo-anywhere, and never one that is a link.
##	History: At bottom of script.

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions', '',
	Justification = 'Called over ssh with no one to confirm; the lane on the Linux side is what decides.')]
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$Dir,
	[switch]$Start,
	[switch]$Run,
	[switch]$Stop,
	[switch]$Remove,
	[string]$GateArgs = 'Gate,Yes,NoSync'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Join-Path ([IO.Path]::GetTempPath()) 'nemo-anywhere'
$pwshExe = Join-Path $PSHOME 'pwsh.exe'
## A task or WMI start may not have git on PATH.
$gitExe = 'C:\Program Files\Git\cmd\git.exe'
if (-not (Test-Path -LiteralPath $gitExe)) { $gitExe = 'git' }

function Test-RunFolder([string]$Path) {
	$parent = (Split-Path -Parent $Path).TrimEnd('\')
	if ($parent -ne $projectRoot.TrimEnd('\')) { return $false }
	if ((Split-Path -Leaf $Path) -notmatch '^\d{8}-\d{6}_\d{3}$') { return $false }
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
	if ($null -eq $item -or -not $item.PSIsContainer) { return $false }
	return -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)
}

function Get-RunPid([string]$Path) {
	$pidFile = Join-Path $Path 'run.pid'
	if (-not (Test-Path -LiteralPath $pidFile)) { return 0 }
	$text = "$(Get-Content -LiteralPath $pidFile -Raw)".Trim()
	if ($text -match '^\d+$') { return [int]$text }
	return 0
}

function Stop-RunTree([int]$RunPid) {
	if ($RunPid -le 0) { return }
	if ($null -eq (Get-Process -Id $RunPid -ErrorAction SilentlyContinue)) { return }
	& taskkill.exe /PID $RunPid /T /F 2>&1 | Out-Null
}

function Remove-RunFolder([string]$Path) {
	if (-not (Test-RunFolder $Path)) { throw "not a run folder of this project: $Path" }
	Remove-Item -LiteralPath $Path -Recurse -Force
}

$Dir = $Dir.TrimEnd('\')

if ($Start) {
	if (-not (Test-RunFolder $Dir)) { throw "not a run folder of this project: $Dir" }
	Set-Location -LiteralPath $Dir
	## The bundle holds only HEAD, so it's fetched rather than cloned.
	$cloneLog = Join-Path $Dir 'clone.log'
	& $gitExe init -q repo *>&1 | Out-File -LiteralPath $cloneLog -Encoding utf8
	if ($LASTEXITCODE -eq 0) { & $gitExe -C repo fetch -q ..\round.bundle HEAD *>&1 | Out-File -LiteralPath $cloneLog -Append -Encoding utf8 }
	if ($LASTEXITCODE -eq 0) { & $gitExe -C repo checkout -q -B gate FETCH_HEAD *>&1 | Out-File -LiteralPath $cloneLog -Append -Encoding utf8 }
	if ($LASTEXITCODE -ne 0) { throw "the bundle did not check out, see $cloneLog" }
	$self = Join-Path $Dir 'lane-box.ps1'
	$cl = "`"$pwshExe`" -NoProfile -ExecutionPolicy Bypass -File `"$self`" -Run -Dir `"$Dir`" -GateArgs `"$GateArgs`""
	$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl; CurrentDirectory = $Dir }
	if ($r.ReturnValue -ne 0) { throw "Win32_Process Create returned $($r.ReturnValue)" }
	"$($r.ProcessId)" | Out-File -LiteralPath (Join-Path $Dir 'run.pid') -Encoding ascii
	"pid=$($r.ProcessId)"
	exit 0
}

if ($Run) {
	$ErrorActionPreference = 'Continue'
	$tmp = Join-Path $Dir 'tmp'
	New-Item -ItemType Directory -Force -Path $tmp | Out-Null
	$env:HOST_TEMP = $tmp; $env:TEMP = $tmp; $env:TMP = $tmp
	Set-Location -LiteralPath (Join-Path $Dir 'repo')
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$gateArgList = @($GateArgs -split ',' | Where-Object { $_ } | ForEach-Object { "-$_" })
	& $pwshExe -NoProfile -ExecutionPolicy Bypass -File cicd\cicd-win.ps1 @gateArgList *>&1 | Out-File -LiteralPath (Join-Path $Dir 'gate.log') -Encoding utf8
	"rc=$LASTEXITCODE secs=$([int]$sw.Elapsed.TotalSeconds)" | Out-File -LiteralPath (Join-Path $Dir 'run.rc') -Encoding ascii
	exit 0
}

if ($Stop) {
	Stop-RunTree (Get-RunPid $Dir)
	exit 0
}

if ($Remove) {
	if (Test-Path -LiteralPath $Dir) {
		Stop-RunTree (Get-RunPid $Dir)
		Remove-RunFolder $Dir
		"removed $Dir"
	}
	$cutoff = (Get-Date).AddDays(-2)
	foreach ($old in @(Get-ChildItem -LiteralPath $projectRoot -Directory -Force -ErrorAction SilentlyContinue)) {
		if (-not (Test-RunFolder $old.FullName) -or $old.LastWriteTime -gt $cutoff) { continue }
		$oldPid = Get-RunPid $old.FullName
		if ($oldPid -gt 0 -and $null -ne (Get-Process -Id $oldPid -ErrorAction SilentlyContinue)) { continue }
		Remove-RunFolder $old.FullName
		"removed stale $($old.FullName)"
	}
	exit 0
}

throw 'need -Start, -Run, -Stop or -Remove'


##	History:
##		- 2026-10-31: Created.
