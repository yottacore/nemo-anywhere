#!/usr/bin/env pwsh

##	Purpose:
##		- Check the PowerShell copy of the guard, include/run-temp.ps1: what
##		  Remove-SafeTree refuses and what it removes, with -Under too, and the
##		  run folder New-RunTemp makes and Remove-StaleRunTemp sweeps. The
##		  bash guard's cases are in test-safe-rm.bash.
##		- Every recursive removal in the tracked PowerShell goes through it,
##		  but for the scripts still to move, each with its count. A new one
##		  anywhere else fails, and so does a count that drops without the
##		  list.
##		- The copies in install.ps1 and n8runfm.ps1, which run on their own,
##		  take the same -Under cases.
##		- Runs in the lint stage.
##	Syntax:
##		pwsh -NoProfile -File cicd/utility/test-safe-rm.ps1
##	Test ID: detts

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Write-Host "[ Test $((Select-String -LiteralPath $PSCommandPath -Pattern '^##\s+Test ID: (\S+)$').Matches[0].Groups[1].Value) $(Split-Path -Leaf $PSCommandPath) ]"

. (Join-Path $PSScriptRoot 'include/run-temp.ps1')

$failures = 0
function fFail {
	param([string]$Msg)
	Write-Host "[ FAILED: $Msg ]"
	$script:failures++
}

## Junctions on Windows, which need no admin.
function fLink {
	param([string]$Path, [string]$Target)
	$type = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
	New-Item -ItemType $type -Path $Path -Target $Target | Out-Null
}
function fMkdir { param([string]$Path); [IO.Directory]::CreateDirectory($Path) | Out-Null }
function fTouch { param([string]$Path); fMkdir (Split-Path -Parent $Path); [IO.File]::WriteAllText($Path, 'x') }
function fThere { param([string]$Path); return [IO.Directory]::Exists($Path) -or [IO.File]::Exists($Path) -or $null -ne [IO.FileInfo]::new($Path).LinkTarget }

## Expects a refusal, and also a warning saying so, and the path left as it was.
function fRefused {
	param([string]$Label, [string[]]$Paths, [string]$Under = '', [string]$Says = 'safe-rm: refused')
	$before = @($Paths | Where-Object { $_ -and -not $_.Contains("`n") } | Where-Object { fThere $_ })
	$warned = $null
	$got = Remove-SafeTree -LiteralPath $Paths -Under $Under -WarningVariable warned -WarningAction SilentlyContinue
	if ($got -ne $false) { fFail "${Label}: returned $got, not False" }
	if (-not ($warned | Where-Object { "$_" -like "*$Says*" })) { fFail "${Label}: no '$Says' warning: $($warned -join '; ')" }
	foreach ($p in $before) { if (-not (fThere $p)) { fFail "${Label}: $p went anyway" } }
}

## A scratch folder under the project's own, removed at the end by its path.
$hostTemp = if ($env:HOST_TEMP) { $env:HOST_TEMP } elseif ($IsWindows) { $env:TEMP } elseif ($env:TMPDIR) { $env:TMPDIR } else { '/tmp' }
$base = Join-Path $hostTemp 'nemo-anywhere'
fMkdir $base
$scratch = $null
$runStamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
for ($n = 1; $n -le 999 -and -not $scratch; $n++) {
	$try = Join-Path $base ('{0}_{1:000}' -f $runStamp, $n)
	try { New-Item -ItemType Directory -Path $try -ErrorAction Stop | Out-Null; $scratch = $try }
	catch { $scratch = $null }
}
if (-not $scratch) { Write-Host "[ FAILED: no scratch folder under $base ]"; exit 1 }
$scratch = Resolve-SafeRmPath -Path $scratch
$saved = @{}
foreach ($name in 'HOST_TEMP', 'CICD_PROJECT', 'CICD_RUN', 'TEMP', 'TMP', 'TMPDIR') { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$sep = [IO.Path]::DirectorySeparatorChar
$root = [IO.Path]::GetPathRoot($scratch)

try {
	$env:HOST_TEMP = $null; $env:CICD_PROJECT = $null
	$t = Join-Path $scratch 'tmp'

	## A tree under a tmp folder goes, hidden and read-only files included,
	## and a link in it goes without what it points at.
	$keep = Join-Path $scratch 'keep'
	fTouch (Join-Path $keep 'kept.txt')
	$a = Join-Path $t 'a'
	fTouch (Join-Path $a 'sub/deep/file.txt')
	fTouch (Join-Path $a '.hidden')
	fTouch (Join-Path $a 'ro.txt')
	$ro = [IO.FileInfo]::new((Join-Path $a 'ro.txt'))
	$ro.Attributes = $ro.Attributes -bor [IO.FileAttributes]::ReadOnly
	if ($IsWindows) {
		$hid = [IO.FileInfo]::new((Join-Path $a '.hidden'))
		$hid.Attributes = $hid.Attributes -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System
	}
	fLink (Join-Path $a 'out') $keep
	$got = Remove-SafeTree -LiteralPath $a
	if ($got -ne $true) { fFail "a tree under tmp: returned $got" }
	if (fThere $a) { fFail "a tree under tmp: still there" }
	if (-not [IO.File]::Exists((Join-Path $keep 'kept.txt'))) { fFail "a link in the tree: what it pointed at went too" }

	## One that's gone is fine, and a trailing separator too.
	if ((Remove-SafeTree -LiteralPath (Join-Path $t 'gone')) -ne $true) { fFail "a path that's gone: not True" }
	fTouch (Join-Path $t 'slash/f')
	if ((Remove-SafeTree -LiteralPath ((Join-Path $t 'slash') + $sep)) -ne $true -or (fThere (Join-Path $t 'slash'))) { fFail "a trailing separator: not removed" }

	## One that can't go: a warning and False, not a throw. Windows tries
	## again for 5 s first.
	$locked = Join-Path $t 'locked'
	fTouch (Join-Path $locked 'in/f')
	$hold = $null
	if ($IsWindows) { $hold = [IO.File]::Open((Join-Path $locked 'in/f'), 'Open', 'Read', 'None') }
	else { [IO.File]::SetUnixFileMode((Join-Path $locked 'in'), [IO.UnixFileMode]'UserRead, UserExecute') }
	try {
		$out = @(Remove-SafeTree -LiteralPath $locked 3>&1)
		if ($out[-1] -ne $false) { fFail "one that can't go: returned $($out[-1])" }
		if (-not ($out | Where-Object { "$_" -like "*couldn't remove*" })) { fFail "one that can't go: no warning" }
		if (-not [IO.File]::Exists((Join-Path $locked 'in/f'))) { fFail "one that can't go: went" }
	} finally {
		if ($hold) { $hold.Dispose() } else { [IO.File]::SetUnixFileMode((Join-Path $locked 'in'), [IO.UnixFileMode]'UserRead, UserWrite, UserExecute') }
	}
	if ((Remove-SafeTree -LiteralPath $locked) -ne $true) { fFail "one that can't go: not removed once it could" }

	## Refused by what the path says.
	fRefused 'empty' @('')
	fRefused 'relative' @('tmp/x') -Says 'bad path'
	fTouch (Join-Path $t 'b/f')
	fRefused 'a ..' @("$t$($sep)b$($sep)..$($sep)b") -Says 'bad path'
	fRefused 'a newline' @("$t$($sep)b`n") -Says 'bad path'
	fRefused 'a doubled separator' @("$t$($sep)$($sep)b") -Says 'bad path'
	fRefused 'UNC' @('\\server\share\tmp\x') -Says 'bad path'
	fRefused 'the tmp folder itself' @("$($root)srv$($sep)tmp") -Says 'not under a tmp folder'
	fRefused 'no tmp folder' @("$($root)srv$($sep)work$($sep)x") -Says 'not under a tmp folder'
	fRefused 'tmp only as part of a name' @("$($root)srv$($sep)tmpx$($sep)x") -Says 'not under a tmp folder'
	$temp = "$($root)srv$($sep)Temp$($sep)x"
	if ($IsWindows) {
		if (Get-SafeRmRefusal -Path $temp) { fFail "Windows: Temp isn't taken as a tmp folder" }
	} elseif (-not (Get-SafeRmRefusal -Path $temp)) { fFail "off Windows: Temp is taken as a tmp folder" }

	## The project folder rule, and never the temp root or a project's whole folder.
	$env:HOST_TEMP = "$($root)srv$($sep)ht"; $env:CICD_PROJECT = 'proj'
	if (Get-SafeRmRefusal -Path "$($root)srv$($sep)ht$($sep)proj$($sep)run") { fFail "HOST_TEMP\proj\run refused" }
	if (-not (Get-SafeRmRefusal -Path "$($root)srv$($sep)ht$($sep)other$($sep)run")) { fFail "another project's folder taken" }
	$env:HOST_TEMP = $t
	fTouch (Join-Path $t 'proj/run/f')
	fRefused 'a project whole folder' @((Join-Path $t 'proj')) -Says "project's whole folder"
	if ((Remove-SafeTree -LiteralPath (Join-Path $t 'proj/run')) -ne $true -or (fThere (Join-Path $t 'proj/run'))) { fFail "a run folder under HOST_TEMP: not removed" }
	## HOST_TEMP as a box may set it, through a link to the real folder.
	fLink (Join-Path $scratch 'ht-link') $t
	$env:HOST_TEMP = Join-Path $scratch 'ht-link'
	fRefused 'a project whole folder, HOST_TEMP through a link' @((Join-Path $t 'proj')) -Says "project's whole folder"
	fRefused 'the temp root, HOST_TEMP through a link' @($t) -Says "project's whole folder"
	$env:HOST_TEMP = $null; $env:CICD_PROJECT = $null

	## Links.
	$target = Join-Path $t 'target'
	fTouch (Join-Path $target 'inner/f')
	fLink (Join-Path $t 'lnk') $target
	fRefused 'a link' @((Join-Path $t 'lnk')) -Says 'refused, a link:'
	fRefused 'through a link' @((Join-Path $t 'lnk/inner')) -Says 'goes through a link'
	if (-not [IO.File]::Exists((Join-Path $target 'inner/f'))) { fFail "through a link: the target lost its file" }

	## Several at once: the refused one stays, the rest go.
	fTouch (Join-Path $t 'c/f')
	$got = Remove-SafeTree -LiteralPath (Join-Path $t 'lnk'), (Join-Path $t 'c') -WarningAction SilentlyContinue
	if ($got -ne $false) { fFail "one of several refused: returned $got" }
	if (-not (fThere (Join-Path $t 'lnk')) -or (fThere (Join-Path $t 'c'))) { fFail "one of several refused: the link went, or the good one stayed" }

	## -Under: strictly under the one parent, with no link below it, while
	## the parent itself may sit behind one.
	$u = Join-Path $scratch 'u'
	fTouch (Join-Path $u 'a/f')
	if ((Remove-SafeTree -LiteralPath (Join-Path $u 'a') -Under $u) -ne $true -or (fThere (Join-Path $u 'a'))) { fFail "-Under: not removed" }
	fTouch (Join-Path $scratch 'other/f')
	fRefused '-Under, elsewhere' @((Join-Path $scratch 'other')) -Under $u -Says 'not under'
	fRefused '-Under, a sibling sharing the name' @("$u-x") -Under $u -Says 'not under'
	fRefused '-Under, the parent itself' @($u) -Under $u -Says 'not under'
	fRefused '-Under, a relative parent' @((Join-Path $u 'a')) -Under 'u' -Says 'bad parent'
	fMkdir (Join-Path $scratch 'elsewhere/x')
	fLink (Join-Path $u 'l') (Join-Path $scratch 'elsewhere')
	fRefused '-Under, a link between' @((Join-Path $u 'l/x')) -Under $u -Says 'goes through a link'
	fLink (Join-Path $scratch 'ulink') $u
	fTouch (Join-Path $u 'b/f')
	if ((Remove-SafeTree -LiteralPath (Join-Path $scratch 'ulink/b') -Under (Join-Path $scratch 'ulink')) -ne $true -or (fThere (Join-Path $u 'b'))) { fFail "-Under, a parent behind a link: not removed" }

	## The run folder: a counter for each one in the same second, run.pid,
	## and TEMP and TMP pointed at it.
	$env:HOST_TEMP = Join-Path $t 'ht'
	fMkdir $env:HOST_TEMP
	$one = New-RunTemp -Project 'proj' -Stamp '20261103-120000'
	$two = New-RunTemp -Project 'proj' -Stamp '20261103-120000'
	if ((Split-Path -Leaf $one) -ne '20261103-120000_001' -or (Split-Path -Leaf $two) -ne '20261103-120000_002') { fFail "New-RunTemp: $one then $two" }
	if (-not [IO.File]::Exists((Join-Path $two 'run.pid')) -or [IO.File]::ReadAllText((Join-Path $two 'run.pid')).Trim() -ne "$PID") { fFail "New-RunTemp: no run.pid with this pid" }
	if ($env:TEMP -ne $two -or $env:TMP -ne $two -or $env:CICD_RUN -ne '20261103-120000_002' -or $env:CICD_PROJECT -ne 'proj') { fFail "New-RunTemp: TEMP $($env:TEMP), TMP $($env:TMP), CICD_RUN $($env:CICD_RUN)" }
	if ($IsWindows -and $null -ne $env:TMPDIR) { fFail "New-RunTemp: TMPDIR still set on Windows" }
	if (-not $IsWindows -and $env:TMPDIR -ne $two) { fFail "New-RunTemp: TMPDIR $($env:TMPDIR)" }

	## The sweep: only an old folder whose run.pid names no live process.
	$projDir = Join-Path $env:HOST_TEMP 'proj'
	$dead = 999999
	while ($null -ne (Get-Process -Id $dead -ErrorAction SilentlyContinue)) { $dead++ }
	$recent = [DateTime]::Now.AddHours(-1).ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
	$cases = [ordered]@{ '20200101-000000_001' = "$dead"; '20200101-000000_002' = "$PID"; '20200101-000000_003' = $null; "$($recent)_001" = "$dead"; '20200101-000000_x' = "$dead"; '20200101-000000_004' = 'junk' }
	foreach ($name in $cases.Keys) {
		fMkdir (Join-Path $projDir $name)
		if ($null -ne $cases[$name]) { [IO.File]::WriteAllText((Join-Path $projDir "$name/run.pid"), $cases[$name]) }
	}
	fMkdir (Join-Path $t 'linked-run')
	[IO.File]::WriteAllText((Join-Path $t 'linked-run/run.pid'), "$dead")
	fLink (Join-Path $projDir '20200101-000000_005') (Join-Path $t 'linked-run')
	$sweepWarn = @(Remove-StaleRunTemp -Project 'proj' -Keep $two 3>&1)
	if ($sweepWarn) { fFail "sweep: $($sweepWarn -join '; ')" }
	foreach ($name in @($cases.Keys) + '20200101-000000_005') {
		$want = $name -in '20200101-000000_001', '20200101-000000_004'
		if ((fThere (Join-Path $projDir $name)) -eq $want) { fFail "sweep: $name $(if ($want) { 'stayed' } else { 'went' })" }
	}
	if (-not (fThere (Join-Path $t 'linked-run/run.pid'))) { fFail "sweep: went into a linked run folder" }
	if (-not (fThere $one) -or -not (fThere $two)) { fFail "sweep: took a new run folder" }

	## The copies in install.ps1 and n8runfm.ps1, which run on their own and
	## can't dot-source the include. Lifted out through the parser and run in a
	## scope of their own, with the -Under cases.
	$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
	foreach ($rel in 'install.ps1', 'utility/n8runfm.ps1') {
		$copyAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repoRoot $rel), [ref]$null, [ref]$null)
		$lift = @('fSafeRm_Under', 'fSafeRm_BadPath', 'fSafeRm_Tree') + @(if ($rel -eq 'install.ps1') { 'fSh' })
		$defs = @($copyAst.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in $lift }, $true))
		if ($defs.Count -ne $lift.Count) { fFail "${rel}: has $(@($defs | ForEach-Object Name) -join ', ') of $($lift -join ', ')"; continue }
		$c = Join-Path $scratch ('copy-' + (Split-Path -LeafBase $rel))
		& {
			foreach ($def in $defs) { . ([scriptblock]::Create($def.Extent.Text)) }
			$warns = [Collections.Generic.List[string]]::new()
			$sudoLog = [Collections.Generic.List[string]]::new()
			## Read by the lifted functions; install.ps1's rm goes through sudo below.
			$state = @{ windows = $IsWindows; rmOneFs = $null }
			$priv = $true
			$null = $state, $priv
			function fWarn { param([string]$Msg) $warns.Add($Msg) }
			function fInnerMessage { param($ErrorRecord) return $ErrorRecord.Exception.Message }
			function sudo { $sudoLog.Add($args -join ' '); $cmd = $args[0]; $rest = @($args | Select-Object -Skip 1); & $cmd @rest }
			function fCopyRefused {
				param([string]$Label, [string]$Parent, [string[]]$Paths, [string]$Says)
				$before = @($Paths | Where-Object { $_ -and -not $_.Contains("`n") } | Where-Object { fThere $_ })
				$warns.Clear()
				$got = fSafeRm_Under $Parent $Paths
				if ($got -ne $false) { fFail "${rel} ${Label}: returned $got, not False" }
				if (-not ($warns | Where-Object { $_ -like "*$Says*" })) { fFail "${rel} ${Label}: no '$Says' warning: $($warns -join '; ')" }
				foreach ($p in $before) { if (-not (fThere $p)) { fFail "${rel} ${Label}: $p went anyway" } }
			}

			$u = Join-Path $c 'u'
			fTouch (Join-Path $u 'a/sub/deep/f')
			fTouch (Join-Path $u 'a/.hidden')
			fTouch (Join-Path $u 'a/ro.txt')
			$ro = [IO.FileInfo]::new((Join-Path $u 'a/ro.txt'))
			$ro.Attributes = $ro.Attributes -bor [IO.FileAttributes]::ReadOnly
			fTouch (Join-Path $c 'keep/kept.txt')
			fLink (Join-Path $u 'a/out') (Join-Path $c 'keep')
			fTouch (Join-Path $u 'file')
			$got = fSafeRm_Under $u @((Join-Path $u 'a'), (Join-Path $u 'file'), (Join-Path $u 'nosuch'))
			if ($got -ne $true -or (fThere (Join-Path $u 'a')) -or (fThere (Join-Path $u 'file'))) { fFail "${rel}: a tree, a file and a missing path under the parent: returned $got, $($warns -join '; ')" }
			if (-not [IO.File]::Exists((Join-Path $c 'keep/kept.txt'))) { fFail "${rel}: a link in the tree: what it pointed at went too" }
			if (-not $IsWindows -and $rel -eq 'install.ps1') {
				& rm --version *> $null
				$want = if ($LASTEXITCODE -eq 0) { '--one-file-system' } else { '-x' }
				if (-not ($sudoLog | Where-Object { $_ -eq "rm -rf $want -- $(Join-Path $u 'a')" })) { fFail "${rel}: the rm didn't go through priv, on one file system: $($sudoLog -join '; ')" }
			}
			fTouch (Join-Path $c 'other/f')
			fCopyRefused 'elsewhere' $u @((Join-Path $c 'other')) 'not under'
			fTouch (Join-Path $c 'v/x/f')
			fCopyRefused 'elsewhere, as long a name' $u @((Join-Path $c 'v/x')) 'not under'
			fCopyRefused 'a sibling sharing the name' $u @("$u-x") 'not under'
			fMkdir "$u-x"
			fCopyRefused 'a sibling sharing the name, there' $u @("$u-x") 'not under'
			fCopyRefused 'the parent itself' $u @($u) 'not under'
			fTouch (Join-Path $u 'b/f')
			fCopyRefused 'a ..' $u @("$u$($sep)b$($sep)..$($sep)b") 'not under'
			fCopyRefused 'a newline' $u @("$u$($sep)b`n") 'not under'
			fCopyRefused 'a relative parent' 'u' @('u/b') 'anything under'
			fCopyRefused 'the root as parent' $root @("$($root)nosuch-safe-rm-check") 'anything under'
			fMkdir (Join-Path $c 'elsewhere/x')
			fLink (Join-Path $u 'l') (Join-Path $c 'elsewhere')
			fCopyRefused 'a link' $u @((Join-Path $u 'l')) "it's a link"
			fCopyRefused 'through a link' $u @((Join-Path $u 'l/x')) 'goes through a link'
			if (-not (fThere (Join-Path $c 'elsewhere/x'))) { fFail "${rel}: through a link: the target lost its folder" }
			fLink (Join-Path $c 'ulink') $u
			$got = fSafeRm_Under (Join-Path $c 'ulink') @((Join-Path $c 'ulink/b'))
			if ($got -ne $true -or (fThere (Join-Path $u 'b'))) { fFail "${rel}: a parent behind a link: returned $got, $($warns -join '; ')" }

			## One that can't go: False, not a throw.
			$locked = Join-Path $u 'locked'
			fTouch (Join-Path $locked 'in/f')
			$hold = $null
			if ($IsWindows) { $hold = [IO.File]::Open((Join-Path $locked 'in/f'), 'Open', 'Read', 'None') }
			else { [IO.File]::SetUnixFileMode((Join-Path $locked 'in'), [IO.UnixFileMode]'UserRead, UserExecute') }
			try {
				$got = fSafeRm_Under $u @($locked) 2>$null
				if ($got -ne $false) { fFail "${rel}: one that can't go: returned $got" }
				if (-not [IO.File]::Exists((Join-Path $locked 'in/f'))) { fFail "${rel}: one that can't go: went" }
			} finally {
				if ($hold) { $hold.Dispose() } else { [IO.File]::SetUnixFileMode((Join-Path $locked 'in'), [IO.UnixFileMode]'UserRead, UserWrite, UserExecute') }
			}
			if ((fSafeRm_Under $u @($locked)) -ne $true -or (fThere $locked)) { fFail "${rel}: one that can't go: not removed once it could" }
		}
	}
} finally {
	foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
	## Only the folder this test made, right under the one parent it may use.
	$item = Get-Item -LiteralPath $scratch -Force -ErrorAction SilentlyContinue
	if ($item -and -not $item.LinkType -and (Split-Path -Parent $item.FullName) -eq (Resolve-SafeRmPath -Path $base)) {
		if (-not (Remove-SafeTree -LiteralPath $scratch -Under (Split-Path -Parent $item.FullName))) { fFail "couldn't remove $scratch" }
	}
}

## Still to move, each with its count. install.ps1 and n8runfm.ps1 have their
## own copies of the guard, checked above.
$toMove = @{}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$files = @(& git -C $repo ls-files '*.ps1' '*.psm1' 2>$null)
if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) { fFail "no PowerShell files from git ls-files" }
$isRecursive = {
	param($node)
	if ($node -is [Management.Automation.Language.CommandAst]) {
		if ($node.GetCommandName() -notin 'Remove-Item', 'rm', 'rmdir', 'rd', 'del', 'erase', 'ri') { return $false }
		return [bool]@($node.CommandElements | Where-Object { $_ -is [Management.Automation.Language.CommandParameterAst] -and $_.ParameterName -and 'recurse'.StartsWith($_.ParameterName.ToLower()) }).Count
	}
	## [IO.Directory]::Delete(path, $true) and DirectoryInfo.Delete($true).
	if ($node -is [Management.Automation.Language.InvokeMemberExpressionAst] -and "$($node.Member)" -eq 'Delete' -and $node.Arguments) {
		$last = "$($node.Arguments[-1])"
		return ($node.Arguments.Count -eq 2 -and $last -ne '$false') -or ($node.Arguments.Count -eq 1 -and $last -eq '$true')
	}
	return $false
}
$found = @{}
foreach ($rel in $files) {
	$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $rel), [ref]$null, [ref]$null)
	$hits = @($ast.FindAll($isRecursive, $true)).Count
	if ($hits) { $found[$rel] = $hits }
}
foreach ($rel in $found.Keys) {
	if (-not $toMove.ContainsKey($rel)) { fFail "${rel}: $($found[$rel]) recursive removal(s) not through Remove-SafeTree" }
	elseif ($found[$rel] -ne $toMove[$rel]) { fFail "${rel}: $($found[$rel]) recursive removal(s), and the list says $($toMove[$rel])" }
}
foreach ($rel in $toMove.Keys) { if (-not $found.ContainsKey($rel)) { fFail "${rel}: on the list, but has none left" } }

if ($failures) { Write-Host "[ FAILED: $failures problem(s) ]"; exit 1 }
Write-Host "[ OK: the PowerShell guard and run folder ]"
exit 0
