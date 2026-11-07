# Config for PSScriptAnalyzer, which cicd/utility/lint-powershell.bash runs
# over the project's own PowerShell. Same idea as pyproject.toml and
# .shellcheckrc: one set of rules for the lint stage and for an editor.

@{
	ExcludeRules = @(
		# These scripts are console tools. Write-Host is how they talk to the
		# person running them, and the transcript is the whole point.
		'PSAvoidUsingWriteHost'

		# Fires on every `& git fetch --quiet` style call. Naming the parameter
		# would make the command lines longer and no clearer.
		'PSAvoidUsingPositionalParameters'

		# install.ps1 must have no byte-order mark: it is fetched and piped
		# straight into the shell, and the mark breaks that parse. n8runfm.ps1
		# is the same file class. The check-docs gate already refuses a
		# non-ASCII byte in install.ps1, which is the rule that matters.
		'PSUseBOMForUnicodeEncodedFile'

		# Does not see a parameter used from the script body or from inside a
		# script block, so it names switches in cicd-win.ps1 and one in
		# pack-portable.ps1 that are all read a few lines later. Checked by
		# hand. (-Quick really is a no-op there, which is its own open item.
		# So are -Long, -Leakcheck, -NoLeakcheck, -NoContainer and -NoRemote,
		# taken so a command line for cicd.bash works there.)
		'PSReviewUnusedParameter'
	)
}
