#!/usr/bin/env bash

##	- Purpose: the lint stage. Runs every checker in turn and fails on the first
##	  one that reports something.
##	- Each checker decides for itself what to do about a tool that is not
##	  installed, so there is nothing to probe for out here. That matters: while
##	  the stage was gated on cppcheck, a box without it ran no checks at all,
##	  which is how the Bash checker came to run nowhere.
##	- vendor-themes.bash's self-test rides along here rather than in the test
##	  stage: it needs git, which the build container does not have, and it runs
##	  in well under a second. The other script tests are here for the same
##	  reason - git, pwsh or python3, none of them in the container - and so the
##	  merge gate runs them. The packages stage never runs in the gate.
##	- A script test that cannot run on this box exits 77 and says why, which is
##	  not a failure here.
##	- Syntax: lint.bash [base-branch]   (passed through to the C check)

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fTest(){ local rc=0; "$@" || rc=$?; [[ "$rc" == "0" || "$rc" == "77" ]] || exit "$rc"; }

bash "${here}/lint-c.bash" "$@"
bash "${here}/lint-bash.bash"
bash "${here}/lint-python.bash"
bash "${here}/lint-powershell.bash"
bash "${here}/lint-identity.bash"
bash "${here}/lint-prose.bash"
## Its fixture is full of symlinks, which MSYS2 cannot make without the symlink
## privilege. Themes are only ever vendored on Linux.
if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then
	echo "[ vendor-themes self-test skipped: needs symlinks, not there under MSYS2 ]"
else
	bash "${here}/vendor-themes.bash" --self-test
	fTest bash "${here}/test-vendor-forks.bash"
fi

bash "${here}/../hooks/test-pre-push.bash"
bash "${here}/test-package-checks.bash"
bash "${here}/test-lint-scope.bash"
bash "${here}/test-cicd-help.bash"
fTest bash "${here}/test-docker-run.bash"
py=""
for cand in python3 python; do
	if command -v "$cand" >/dev/null 2>&1; then py="$cand"; break; fi
done
if [[ -n "$py" ]]; then
	"$py" "${here}/svg-min.py" --self-test
	"$py" "${here}/test-id.py" --check
else
	echo "[ svg-min self-test and test ID check skipped: no python ]"
fi
if command -v pwsh >/dev/null 2>&1; then
	pwsh -NoProfile -File "${here}/test-install-path.ps1"
	pwsh -NoProfile -File "${here}/test-runfm-pool.ps1"
else
	echo "[ install.ps1 PATH and n8runfm pool tests skipped: no pwsh ]"
fi
## Under MSYS2 install.ps1 would be installing into the real Windows profile.
if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then
	echo "[ installer download check skipped: Linux only ]"
else
	fTest bash "${here}/../linux/test-install-download.bash"
fi

## The app icons are cut from assets/logo.png by hand, so a new logo can sit there
## with the old icons still shipping. Needs Pillow, which not every box has.
if python3 -c 'import PIL' 2>/dev/null; then
	python3 "${here}/gen-app-icon.py" --check
else
	echo "[ app icon check skipped: no Pillow ]"
fi

## The content scrub is kept outside the repo, so a fresh clone without the
## private tree still lints.
scrub="$(cd "${here}/../.." && pwd)/../private/hooks/scrub.bash"
[[ -x "$scrub" ]] && bash "$scrub"

exit 0
