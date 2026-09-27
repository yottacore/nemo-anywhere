##	Purpose:
##		- GUI launch smoke for the native Windows build: start the exe on a
##		  scratch folder, wait for its main window, see that it is still alive a
##		  few seconds later, then end it by its pid. The --version smoke never
##		  opens a window, so it proves nothing about GTK, the theme or the first
##		  listing.
##		- Every setting and cache goes to a scratch folder, so the box's own
##		  config is never read or written. A crash report there fails the run.
##		- Any critical on stderr fails the run, apart from the few GDK logs on
##		  every start in a session with no monitor. That is both an ssh login
##		  (session 0) and the disconnected desktop session a scheduled task
##		  reaches, so G_DEBUG=fatal-criticals would stop every run there.
##		- Session 0 has no desktop to show on: the main window is made but never
##		  visible, so there it only has to exist. Anywhere else it has to show.
##		- Exits 77 where it cannot run: not Windows, or no exe.
##	Syntax:
##		pwsh -NoProfile -File cicd/win/gui-launch-smoke.ps1 -Exe <exe> -RuntimeBin <dir> [-Seconds <n>]
##	Test ID: rhtwm2cc

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$Exe,
	[Parameter(Mandatory)][string]$RuntimeBin,
	[int]$Seconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

if (-not $IsWindows) { Write-Host "[ GUI launch smoke skipped: Windows only ]"; exit 77 }
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "[ GUI launch smoke skipped: no exe at ${Exe} ]"; exit 77 }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NaSmoke {
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
	/* The main window, not the splash or one of GTK's hidden 1x1 toplevels. */
	public static string MainWindow(uint pid, bool visible) {
		string found = null;
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p != pid || (visible && !IsWindowVisible(h))) return true;
			var cls = new StringBuilder(256); GetClassNameW(h, cls, 256);
			if (cls.ToString() != "gdkWindowToplevel") return true;
			var title = new StringBuilder(512); GetWindowTextW(h, title, 512);
			found = title.ToString();
			return false;
		}, IntPtr.Zero);
		return found;
	}
}
'@

$session = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
$allowed = 'gdk_monitor_get_geometry|_gdk_win32_monitor_get_pixel_structure|gtk_widget_get_preferred_height_for_width'

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) "nemo-gui-smoke-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$browse = Join-Path $scratch "browse here"
$config = Join-Path $scratch "config"
New-Item -ItemType Directory -Path $browse, $config, (Join-Path $browse "a folder") -Force | Out-Null
Set-Content -LiteralPath (Join-Path $browse "notes.txt") -Value "smoke"
$errLog = Join-Path $scratch "stderr.txt"
$outLog = Join-Path $scratch "stdout.txt"

$names = @("PATH", "APPDATA", "LOCALAPPDATA", "HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME",
	"DBUS_SESSION_BUS_ADDRESS", "NEMO_NO_CRASH_DIALOG")
$saved = @{}
foreach ($n in $names) { $saved[$n] = [Environment]::GetEnvironmentVariable($n) }

$failures = @()
$proc = $null
try {
	$env:PATH = "$RuntimeBin;$env:SystemRoot\System32;$env:SystemRoot"
	foreach ($n in "APPDATA", "LOCALAPPDATA", "HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME") {
		[Environment]::SetEnvironmentVariable($n, $config)
	}
	## Off the session bus, so no bus daemon outlives the run.
	$env:DBUS_SESSION_BUS_ADDRESS = "disabled:"
	$env:NEMO_NO_CRASH_DIALOG = "1"

	Write-Host "  session ${session}"
	$proc = Start-Process -FilePath $Exe -ArgumentList "`"$browse`"" -PassThru `
		-RedirectStandardError $errLog -RedirectStandardOutput $outLog

	$title = $null
	$deadline = (Get-Date).AddSeconds(60)
	while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
		$title = [NaSmoke]::MainWindow([uint32]$proc.Id, $session -ne 0)
		if ($null -ne $title) { break }
		Start-Sleep -Milliseconds 250
	}

	if ($proc.HasExited) {
		$failures += "exited with $($proc.ExitCode) before a window came up"
	} elseif ($null -eq $title) {
		$failures += "no main window within 60 seconds"
	} else {
		Write-Host "  window up: '${title}'"
		Start-Sleep -Seconds $Seconds
		if ($proc.HasExited) {
			$failures += "exited with $($proc.ExitCode) ${Seconds}s after its window came up"
		} else {
			## It reads "Loading..." until the folder is listed.
			$title = [NaSmoke]::MainWindow([uint32]$proc.Id, $session -ne 0)
			Write-Host "  ${Seconds}s later: '${title}'"
			if ($null -eq $title -or -not $title.Contains("browse here")) { $failures += "the window is not on the folder asked for: '${title}'" }
		}
	}
} finally {
	if ($null -ne $proc -and -not $proc.HasExited) {
		Stop-Process -Id $proc.Id -Force
		$proc.WaitForExit(10000) | Out-Null
	}
	foreach ($n in $names) { [Environment]::SetEnvironmentVariable($n, $saved[$n]) }
}

$criticals = @()
if (Test-Path -LiteralPath $errLog) {
	$criticals = @(Get-Content -LiteralPath $errLog | Where-Object { $_ -match '-CRITICAL \*\*' -and $_ -notmatch $allowed })
}
if ($criticals.Count -gt 0) { $failures += "$($criticals.Count) critical(s), first: $($criticals[0])" }

$crashDir = Join-Path $config "nemo-anywhere\crash"
if (Test-Path -LiteralPath $crashDir) {
	$reports = @(Get-ChildItem -LiteralPath $crashDir -File)
	if ($reports.Count -gt 0) { $failures += "crash report written: $($reports[0].FullName)"; Get-Content -LiteralPath $reports[0].FullName | Select-Object -First 30 }
}

if ($failures.Count -gt 0) {
	foreach ($f in $failures) { Write-Host "[ FAILED: GUI launch smoke: $f ]" }
	if (Test-Path -LiteralPath $errLog) { Get-Content -LiteralPath $errLog | Select-Object -Last 20 }
	Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
	exit 1
}
Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[ OK: GUI launch smoke ]"
exit 0
