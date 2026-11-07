##	Purpose:
##		- The run folder cicd-win.ps1 works in, and the guard every recursive
##		  removal in the PowerShell scripts goes through. The same rules as
##		  run-temp.bash. Dot-source it; it defines functions only.
##		- New-RunTemp -Project <name> -Stamp <YYYYmmDD-HHMMSS> makes
##		  <HOST_TEMP>\<project>\<stamp>_<NNN>, writes run.pid there, points TEMP
##		  and TMP at it, and returns its path. HOST_TEMP falls back to TEMP on
##		  Windows, else TMPDIR, then /tmp. Off Windows TMPDIR points there too.
##		  On Windows TMPDIR is cleared, since MSYS2 mounts /tmp on TEMP, and a
##		  TMPDIR there comes out as /c/.../Temp/..., which run-temp.bash's
##		  guard refuses.
##		- Remove-StaleRunTemp -Project <name> removes the project's run folders
##		  older than 2 days whose run.pid names no live process. A folder with
##		  no run.pid wasn't made by a run, so it stays.
##		- Remove-SafeTree -LiteralPath <path>... removes each path and what's
##		  in it, when it's under a folder named tmp (or Temp, on Windows), never
##		  that folder itself, or under <HOST_TEMP>\<CICD_PROJECT>. With -Under
##		  <parent>, each must be strictly under that one parent instead.
##		  Refuses a relative or UNC path, .., a doubled separator, a newline, a
##		  link, a path through a link, the temp root, or a project's whole
##		  folder right under it, short (8.3) names or not. A link inside is
##		  removed, never followed, and so is a mounted volume on Windows. One
##		  that's gone is skipped. Warns for each refusal, and returns $false if
##		  any was refused or couldn't go.

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

$script:SafeRmCase = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
## HOST_TEMP's forms, real and long, worked out once for each value it takes.
$script:SafeRmRootFor = $null
$script:SafeRmRoots = $null

## The path with every link in it swapped for its target, or $null when part
## of it isn't there. Short (8.3) names stay as they are, on both sides.
function Resolve-SafeRmPath {
	[CmdletBinding()]
	[OutputType([string])]
	param([Parameter(Mandatory)][string]$Path, [int]$Depth = 0)
	if ($Depth -gt 40) { return $null }
	$root = [IO.Path]::GetPathRoot($Path)
	$cur = $root
	foreach ($part in ($Path.Substring($root.Length) -split '[\\/]' | Where-Object { $_ })) {
		$next = Join-Path $cur $part
		$info = if ([IO.Directory]::Exists($next)) { [IO.DirectoryInfo]::new($next) } else { [IO.FileInfo]::new($next) }
		if (-not $info.Exists -and $null -eq $info.LinkTarget) { return $null }
		if ($null -ne $info.LinkTarget) {
			$target = $info.ResolveLinkTarget($true)
			if ($null -eq $target) { return $null }
			$next = Resolve-SafeRmPath -Path $target.FullName -Depth ($Depth + 1)
			if ($null -eq $next) { return $null }
		}
		$cur = $next
	}
	return $cur.TrimEnd('\', '/')
}

## Windows names a folder by its short (8.3) name too, so 2 strings can be one
## folder. A made-up short name always has a ~, so the rest skip the call.
## As it was when it isn't there.
function Get-SafeRmLongPath {
	[CmdletBinding()]
	[OutputType([string])]
	param([AllowEmptyString()][string]$Path)
	if (-not $IsWindows -or -not $Path -or -not $Path.Contains('~')) { return $Path }
	if (-not ('SafeRm.Native' -as [type])) {
		Add-Type -Namespace SafeRm -Name Native -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern uint GetLongPathNameW(string shortPath, System.Text.StringBuilder longPath, uint size);'
	}
	$buf = [Text.StringBuilder]::new(260)
	$n = [SafeRm.Native]::GetLongPathNameW($Path, $buf, [uint32]$buf.Capacity)
	if ($n -ge $buf.Capacity) {
		$buf = [Text.StringBuilder]::new([int]$n)
		$n = [SafeRm.Native]::GetLongPathNameW($Path, $buf, [uint32]$buf.Capacity)
	}
	if ($n -eq 0 -or $n -ge $buf.Capacity) { return $Path }
	return $buf.ToString().TrimEnd('\', '/')
}

## Why a path may not be removed, or '' when it may. With $Parent, the rule
## is strictly under it; else the tmp folder or project folder rule.
function Get-SafeRmRefusal {
	[CmdletBinding()]
	[OutputType([string])]
	param([AllowEmptyString()][string]$Path, [string]$Parent = '')
	$sep = [IO.Path]::DirectorySeparatorChar
	if (-not $Path -or $Path.Contains("`n") -or $Path.Contains("`r") -or $Path.Contains('..') -or
		-not [IO.Path]::IsPathFullyQualified($Path) -or $Path.StartsWith('\\') -or $Path.StartsWith('//')) {
		return "bad path: `"$Path`""
	}
	$p = $Path.TrimEnd('\', '/')
	if ($p.Substring(1) -match '[\\/]{2}') { return "bad path: `"$Path`"" }
	if ($Parent) {
		$under = $Parent.TrimEnd('\', '/')
		if (-not $p.StartsWith($under + $sep, $script:SafeRmCase) -and -not $p.StartsWith($under + '/', $script:SafeRmCase)) {
			return "not under ${Parent}: `"$Path`""
		}
	} else {
		$tmpRx = if ($IsWindows) { '(?i)[\\/](tmp|temp)[\\/][^\\/]' } else { '/tmp/[^/]' }
		$own = ''
		if ($env:HOST_TEMP -and $env:CICD_PROJECT) { $own = (Join-Path $env:HOST_TEMP $env:CICD_PROJECT).TrimEnd('\', '/') }
		$inOwn = $own -and ($p.StartsWith($own + $sep, $script:SafeRmCase) -or $p.StartsWith($own + '/', $script:SafeRmCase))
		if (-not ($p -match $tmpRx) -and -not $inOwn) { return "not under a tmp folder: $Path" }
	}
	if ($env:HOST_TEMP) {
		## Outside a run HOST_TEMP is as the box set it, and may sit behind a
		## link, while a path that gets this far has none in it. Either side
		## may have short names in it.
		$root = $env:HOST_TEMP.TrimEnd('\', '/')
		if ($script:SafeRmRootFor -ne $env:HOST_TEMP) {
			$script:SafeRmRootFor = $env:HOST_TEMP
			$real = if ([IO.Path]::IsPathFullyQualified($root)) { Resolve-SafeRmPath -Path $root } else { $null }
			$script:SafeRmRoots = @($root, (Get-SafeRmLongPath $root), $real, (Get-SafeRmLongPath $real)) | Where-Object { $_ }
		}
		$up = [IO.Path]::GetDirectoryName($p)
		foreach ($mine in @($p, $up, (Get-SafeRmLongPath $p), (Get-SafeRmLongPath $up))) {
			foreach ($r in $script:SafeRmRoots) {
				if ($mine -and [string]::Equals($mine, $r, $script:SafeRmCase)) { return "the temp root or a project's whole folder: $Path" }
			}
		}
	}
	return ''
}

## Depth first, deleting a link or mounted volume as the entry it is rather
## than going into it. Hidden and system files too, which enumeration skips by
## default.
function Remove-SafeTreeItem {
	[CmdletBinding(SupportsShouldProcess)]
	param([Parameter(Mandatory)][IO.FileSystemInfo]$Item)
	$isLink = ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $null -ne $Item.LinkTarget
	if ($Item -is [IO.DirectoryInfo] -and -not $isLink) {
		$options = [IO.EnumerationOptions]::new()
		$options.AttributesToSkip = [IO.FileAttributes]0
		foreach ($child in $Item.EnumerateFileSystemInfos('*', $options)) { Remove-SafeTreeItem -Item $child }
	}
	if (-not $PSCmdlet.ShouldProcess($Item.FullName, 'remove')) { return }
	if (-not $isLink -and ($Item.Attributes -band [IO.FileAttributes]::ReadOnly)) { $Item.Attributes = $Item.Attributes -band -bnot [IO.FileAttributes]::ReadOnly }
	## Windows takes a directory link off with RemoveDirectory, and elsewhere
	## any link is unlinked.
	if ($Item -is [IO.DirectoryInfo] -and -not ($isLink -and -not $IsWindows)) { [IO.Directory]::Delete($Item.FullName, $false) } else { [IO.File]::Delete($Item.FullName) }
}

function Remove-SafeTree {
	[CmdletBinding(SupportsShouldProcess)]
	[OutputType([bool])]
	param(
		[Parameter(Mandatory, Position = 0)][AllowEmptyString()][AllowEmptyCollection()][string[]]$LiteralPath,
		[string]$Under = ''
	)
	$ok = $true
	$realUnder = $null
	if ($Under -and ($Under.Contains('..') -or -not [IO.Path]::IsPathFullyQualified($Under) -or $Under.StartsWith('\\'))) {
		Write-Warning "safe-rm: refused, bad parent: `"$Under`""
		return $false
	}
	foreach ($path in $LiteralPath) {
		$why = Get-SafeRmRefusal -Path $path -Parent $Under
		if ($why) { Write-Warning "safe-rm: refused, $why"; $ok = $false; continue }
		$p = $path.TrimEnd('\', '/')
		$item = if ([IO.Directory]::Exists($p)) { [IO.DirectoryInfo]::new($p) } else { [IO.FileInfo]::new($p) }
		if (-not $item.Exists -and $null -eq $item.LinkTarget) { continue }
		if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $null -ne $item.LinkTarget) {
			Write-Warning "safe-rm: refused, a link: $p"; $ok = $false; continue
		}
		## The whole path, or with -Under the part below the parent, has no
		## link in it. The parent itself may sit behind one.
		$real = Resolve-SafeRmPath -Path $p
		$want = $p
		if ($Under) {
			if ($null -eq $realUnder) {
				$realUnder = Resolve-SafeRmPath -Path $Under
				if ($null -eq $realUnder) { Write-Warning "safe-rm: refused, no parent: $Under"; return $false }
			}
			$want = $realUnder + $p.Substring($Under.TrimEnd('\', '/').Length)
		}
		if ($null -eq $real -or -not [string]::Equals($real, $want, $script:SafeRmCase)) {
			Write-Warning "safe-rm: refused, goes through a link: $p"; $ok = $false; continue
		}
		## A virus scanner can hold a file just written for a moment, and
		## Windows then refuses the delete.
		for ($try = 1; ; $try++) {
			try { Remove-SafeTreeItem -Item $item; break }
			catch [IO.IOException], [UnauthorizedAccessException] {
				if (-not $IsWindows -or $try -ge 10) { Write-Warning "safe-rm: couldn't remove ${p}: $($_.Exception.Message)"; $ok = $false; break }
				Start-Sleep -Milliseconds 500
				$item.Refresh()
				if (-not $item.Exists) { break }
			}
		}
	}
	return $ok
}

function New-RunTemp {
	[CmdletBinding(SupportsShouldProcess)]
	[OutputType([string])]
	param([Parameter(Mandatory)][string]$Project, [Parameter(Mandatory)][string]$Stamp)
	$hostTemp = if ($env:HOST_TEMP) { $env:HOST_TEMP } elseif ($IsWindows) { $env:TEMP } elseif ($env:TMPDIR) { $env:TMPDIR } else { '/tmp' }
	## Resolved once, since the guard compares real paths.
	$hostTemp = Resolve-SafeRmPath -Path ([IO.Path]::GetFullPath($hostTemp))
	if (-not $hostTemp) { throw "no temp folder at $($env:HOST_TEMP)" }
	if (-not $PSCmdlet.ShouldProcess($hostTemp, 'make a run folder')) { return }
	$base = Join-Path $hostTemp $Project
	[IO.Directory]::CreateDirectory($base) | Out-Null
	## Made one at a time, so 2 runs started in the same second can't share one.
	$dir = $null
	for ($n = 1; $n -le 999 -and -not $dir; $n++) {
		$try = Join-Path $base ('{0}_{1:000}' -f $Stamp, $n)
		if ([IO.Directory]::Exists($try) -or [IO.File]::Exists($try)) { continue }
		try { New-Item -ItemType Directory -Path $try -ErrorAction Stop | Out-Null; $dir = $try }
		catch { if (-not [IO.Directory]::Exists($try)) { throw } }
	}
	if (-not $dir) { throw "no free run folder under $base" }
	[IO.File]::WriteAllText((Join-Path $dir 'run.pid'), "$PID`n")
	$env:HOST_TEMP = $hostTemp
	$env:TEMP = $dir
	$env:TMP = $dir
	if ($IsWindows) { $env:TMPDIR = $null } else { $env:TMPDIR = $dir }
	$env:CICD_PROJECT = $Project
	$env:CICD_RUN = Split-Path -Leaf $dir
	return $dir
}

function Remove-StaleRunTemp {
	[CmdletBinding(SupportsShouldProcess)]
	param([Parameter(Mandatory)][string]$Project, [string]$Keep = '')
	if (-not $env:HOST_TEMP) { return }
	$base = Join-Path $env:HOST_TEMP $Project
	if (-not [IO.Directory]::Exists($base)) { return }
	$cutoff = [DateTime]::Now.AddDays(-2).ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
	foreach ($dir in [IO.DirectoryInfo]::new($base).EnumerateDirectories()) {
		if ($dir.Name -notmatch '^\d{8}-\d{6}_\d{3}$' -or [string]::CompareOrdinal($dir.Name.Substring(0, 15), $cutoff) -ge 0) { continue }
		if ($Keep -and [string]::Equals($dir.FullName, $Keep, $script:SafeRmCase)) { continue }
		if ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
		$pidFile = Join-Path $dir.FullName 'run.pid'
		if (-not [IO.File]::Exists($pidFile)) { continue }
		$runPid = 0
		if ([int]::TryParse([IO.File]::ReadAllText($pidFile).Trim(), [ref]$runPid) -and $runPid -gt 0 -and
			$null -ne (Get-Process -Id $runPid -ErrorAction SilentlyContinue)) { continue }
		if ($PSCmdlet.ShouldProcess($dir.FullName, 'remove a stale run folder')) { Remove-SafeTree -LiteralPath $dir.FullName | Out-Null }
	}
}


##	History:
##		- 2026-11-03: Created.
##		- 2026-11-04: The temp root rule also holds when HOST_TEMP sits behind a
##		  link.
##		- 2026-11-06: And when either side has a short (8.3) name in it.
