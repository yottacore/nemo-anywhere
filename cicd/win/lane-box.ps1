##	Purpose:
##		- The Windows box's side of the native Windows lane (win/lane.bash). It
##		  is sent into the run's own folder there, with run-temp.ps1 for its
##		  removals, and called over ssh.
##		- -Start clones the bundle beside it and starts -Run outside the ssh
##		  session, through Win32_Process Create, so a dropped connection doesn't
##		  stop the gate. Prints "pid=<n>" and writes it to run.pid.
##		- -Run runs cicd-win.ps1 in the clone, with TEMP, TMP and HOST_TEMP in
##		  the run folder, to gate.log, then writes "rc=<n> secs=<n>" to run.rc.
##		  -GateArgs names its switches with commas and no dashes, such as
##		  Gate,Yes,NoSync, since a quoted value that starts with a dash reads
##		  as a parameter name on the way in.
##		- -Desktop, run as the ssh login after the gate, starts -DesktopRun in
##		  the test user's desktop session, through a schtasks /it task named
##		  for the run folder, on the meson tests named in desktop.txt. The
##		  user is given Modify on the run folder first. Prints "task=<name>",
##		  "nosession" when the user has no session on the box, or "locked"
##		  when it's locked.
##		- -DesktopRun runs cicd-win.ps1 -TestList desktop.txt in the clone, to
##		  desktop.log, then writes "rc=<n> secs=<n>" to desktop.rc. A session
##		  with no monitor is a failure, since that's what it's there for.
##		- -Stop ends the gate's whole process tree, from run.pid, and the
##		  desktop task's, from desktop.pid.
##		- -Remove removes the run folder, and the project's run folders beside
##		  it that are older than 2 days with no live process from their run.pid.
##		  Only a folder named <YYYYmmDD-HHMMSS>_<NNN> right under
##		  %ProgramData%\nemo-anywhere, and never one that is a link. The run's
##		  desktop task goes too.
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
	[switch]$Desktop,
	[switch]$DesktopRun,
	[string]$GateArgs = 'Gate,Yes,NoSync',
	[string]$TestUser = 'wintest'
)

Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'run-temp.ps1')
$ErrorActionPreference = 'Stop'

## Both the ssh login and the test user work here. PowerShell reads each
## folder above a path it works in, so not under either one's profile.
$projectRoot = Join-Path $env:ProgramData 'nemo-anywhere'
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

function Get-RunPid([string]$Path, [string]$Name = 'run.pid') {
	$pidFile = Join-Path $Path $Name
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

## WTS_SESSIONSTATE_LOCK, from WTSINFOEX_LEVEL1's SessionFlags. Unknown reads
## as unlocked.
function Test-SessionLocked([int]$SessionId) {
	Add-Type -Namespace LaneBox -Name Wts -MemberDefinition @'
[DllImport("wtsapi32.dll")] public static extern bool WTSQuerySessionInformationW(System.IntPtr server, int session, int infoClass, out System.IntPtr buffer, out int bytes);
[DllImport("wtsapi32.dll")] public static extern void WTSFreeMemory(System.IntPtr memory);
'@
	$buffer = [IntPtr]::Zero; $bytes = 0
	## WTSSessionInfoEx
	if (-not [LaneBox.Wts]::WTSQuerySessionInformationW([IntPtr]::Zero, $SessionId, 25, [ref]$buffer, [ref]$bytes)) { return $false }
	try {
		## Level, then the union at 8, whose SessionFlags follow SessionId and SessionState.
		return [Runtime.InteropServices.Marshal]::ReadInt32($buffer, 16) -eq 0
	} finally { [LaneBox.Wts]::WTSFreeMemory($buffer) }
}

## One per run folder, so a stale sweep can find it from the folder's name.
function Get-TaskName([string]$Path) { return "nemo-anywhere-$(Split-Path -Leaf $Path)" }

function Remove-RunTask([string]$Path) {
	$task = Get-TaskName $Path
	& schtasks.exe /query /tn $task *> $null
	if ($LASTEXITCODE -ne 0) { return }
	& schtasks.exe /end /tn $task *> $null
	& schtasks.exe /delete /f /tn $task *> $null
}

function Stop-Run([string]$Path) {
	Stop-RunTree (Get-RunPid $Path)
	Stop-RunTree (Get-RunPid $Path 'desktop.pid')
	Remove-RunTask $Path
}

function Remove-RunFolder([string]$Path) {
	if (-not (Test-RunFolder $Path)) { throw "not a run folder of this project: $Path" }
	if (-not (Remove-SafeTree -LiteralPath $Path -Under $projectRoot)) { throw "could not remove $Path" }
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

if ($Desktop) {
	if (-not (Test-RunFolder $Dir)) { throw "not a run folder of this project: $Dir" }
	## A disconnected session still draws, and takes a task's windows.
	$session = @(& query.exe session 2>$null | Where-Object { $_ -match "^.\S*\s+$([regex]::Escape($TestUser))\s+\d+\s+(Active|Disc)" })
	if ($session.Count -eq 0) { 'nosession'; exit 0 }
	## A locked session draws, but no window there gets the focus, so the
	## tests that need it fail for no fault of the app. A disconnected one
	## reads as locked too.
	if ((Test-SessionLocked ([int]($session[0] -replace "^.\S*\s+\S+\s+(\d+).*", '$1')))) { 'locked'; exit 0 }
	& icacls.exe $Dir /grant "${TestUser}:(OI)(CI)M" /Q *> $null
	if ($LASTEXITCODE -ne 0) { throw "could not give $TestUser Modify on $Dir" }
	$self = Join-Path $Dir 'lane-box.ps1'
	$cmdFile = Join-Path $Dir 'desktop.cmd'
	## schtasks /tr takes a whole command line badly, so it points at a file.
	"@`"$pwshExe`" -NoProfile -ExecutionPolicy Bypass -File `"$self`" -DesktopRun -Dir `"$Dir`"`r`n" | Out-File -LiteralPath $cmdFile -Encoding ascii -NoNewline
	$task = Get-TaskName $Dir
	& schtasks.exe /create /f /tn $task /tr $cmdFile /sc once /st 23:59 /it /ru $TestUser *> $null
	if ($LASTEXITCODE -ne 0) { throw "could not make the task $task for $TestUser" }
	& schtasks.exe /run /tn $task *> $null
	if ($LASTEXITCODE -ne 0) { throw "could not start the task $task" }
	"task=$task"
	exit 0
}

if ($DesktopRun) {
	$ErrorActionPreference = 'Continue'
	"$PID" | Out-File -LiteralPath (Join-Path $Dir 'desktop.pid') -Encoding ascii
	$log = Join-Path $Dir 'desktop.log'
	$sw = [Diagnostics.Stopwatch]::StartNew()
	Add-Type -Namespace LaneBox -Name User32 -MemberDefinition '[DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);'
	## SM_CMONITORS
	$monitors = [LaneBox.User32]::GetSystemMetrics(80)
	$sessionId = (Get-Process -Id $PID).SessionId
	"desktop session $sessionId as $env:USERNAME, $monitors monitor(s)" | Out-File -LiteralPath $log -Encoding utf8
	$rc = 1
	if ($sessionId -eq 0 -or $monitors -lt 1) {
		'no desktop to draw on' | Out-File -LiteralPath $log -Append -Encoding utf8
	} else {
		$tmp = Join-Path $Dir 'tmp'
		New-Item -ItemType Directory -Force -Path $tmp | Out-Null
		$env:HOST_TEMP = $tmp; $env:TEMP = $tmp; $env:TMP = $tmp
		## A task's pwsh doesn't start in the location set here, so every path is whole.
		$repo = Join-Path $Dir 'repo'
		& $pwshExe -NoProfile -ExecutionPolicy Bypass -WorkingDirectory $repo -File (Join-Path $repo 'cicd\cicd-win.ps1') -Yes -TestList (Join-Path $Dir 'desktop.txt') *>&1 | Out-File -LiteralPath $log -Append -Encoding utf8
		$rc = $LASTEXITCODE
	}
	"rc=$rc secs=$([int]$sw.Elapsed.TotalSeconds)" | Out-File -LiteralPath (Join-Path $Dir 'desktop.rc') -Encoding ascii
	exit 0
}

if ($Stop) {
	Stop-Run $Dir
	exit 0
}

if ($Remove) {
	if (Test-Path -LiteralPath $Dir) {
		Stop-Run $Dir
		Remove-RunFolder $Dir
		"removed $Dir"
	}
	$cutoff = (Get-Date).AddDays(-2)
	foreach ($old in @(Get-ChildItem -LiteralPath $projectRoot -Directory -Force -ErrorAction SilentlyContinue)) {
		if (-not (Test-RunFolder $old.FullName) -or $old.LastWriteTime -gt $cutoff) { continue }
		$oldPid = Get-RunPid $old.FullName
		if ($oldPid -gt 0 -and $null -ne (Get-Process -Id $oldPid -ErrorAction SilentlyContinue)) { continue }
		Remove-RunTask $old.FullName
		Remove-RunFolder $old.FullName
		"removed stale $($old.FullName)"
	}
	exit 0
}

throw 'need -Start, -Run, -Desktop, -DesktopRun, -Stop or -Remove'


##	History:
##		- 2026-11-03: Removals go through Remove-SafeTree, from run-temp.ps1 sent
##		  beside it.
##		- 2026-11-02: -Desktop says "locked" for a locked session.
##		- 2026-11-02: Run folders are under %ProgramData%, where the test user
##		  can reach them.
##		- 2026-10-31: -Desktop and -DesktopRun, for the tests the gate skipped
##		  with no desktop.
##		- 2026-10-31: Created.
