#!/usr/bin/env bash

##	- Purpose: Check that the release image is pinned: its base by digest, and
##	  every apt source moved to one dated snapshot before apt first runs. Without
##	  both, a box that has to build the image gets whatever 22.04 updates are
##	  current that day, and other compiler and library bytes with them.
##	- Runs the Dockerfile's own sources rewrite over the stock jammy sources list.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-release-image-pin.bash
##	- Test ID: rjcma0t3

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dockerfile="${here}/Dockerfile"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

text="$(cat "$dockerfile")"

froms="$(grep -E '^FROM[[:space:]]' <<<"$text" || true)"
[[ -n "$froms" ]] || fFail "no FROM line in ${dockerfile}"
while IFS= read -r line; do
	[[ -z "$line" ]] && continue
	if [[ ! "$line" =~ @sha256:[0-9a-f]{64}([[:space:]]|$) ]]; then fFail "base not pinned by digest: ${line}"; fi
done <<<"$froms"

## The sources list as it comes in the jammy base image.
stock='deb http://archive.ubuntu.com/ubuntu/ jammy main restricted
deb http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted
deb http://archive.ubuntu.com/ubuntu/ jammy universe
deb http://archive.ubuntu.com/ubuntu/ jammy-updates universe
deb http://archive.ubuntu.com/ubuntu/ jammy multiverse
deb http://archive.ubuntu.com/ubuntu/ jammy-updates multiverse
deb http://archive.ubuntu.com/ubuntu/ jammy-backports main restricted universe multiverse
deb http://security.ubuntu.com/ubuntu/ jammy-security main restricted
deb http://security.ubuntu.com/ubuntu/ jammy-security universe
deb http://security.ubuntu.com/ubuntu/ jammy-security multiverse'

## Drop comments and join continuation lines so each RUN is one line, then take
## the sed that rewrites the sources and run it here.
joined="$(grep -v '^[[:space:]]*#' <<<"$text" | sed -e ':a' -e '/\\$/{N;s/\\\n//;ba' -e '}')"
expr="$(grep -oE "sed -i -E '[^']+' /etc/apt/sources.list" <<<"$joined" | head -1 || true)"
if [[ -z "$expr" ]]; then
	fFail "nothing rewrites /etc/apt/sources.list"
else
	expr="${expr#sed -i -E \'}"; expr="${expr%\' /etc/apt/sources.list}"
	rewritten="$(sed -E "$expr" <<<"$stock")"
	snaps="$(grep -oE 'https?://snapshot\.ubuntu\.com/ubuntu/[0-9]{8}T[0-9]{6}Z/' <<<"$rewritten" | sort -u || true)"
	while IFS= read -r line; do
		[[ "$line" =~ snapshot\.ubuntu\.com/ubuntu/[0-9]{8}T[0-9]{6}Z/ ]] || fFail "source not on a dated snapshot: ${line}"
	done <<<"$rewritten"
	if [[ "$(grep -c . <<<"$snaps")" != 1 ]]; then fFail "sources do not share one snapshot: $(tr '\n' ' ' <<<"$snaps")"; fi

	## apt must not read the live archive first.
	before="${joined%%sources.list*}"
	if [[ "$before" == *apt-get* || "$before" == *"apt "* ]]; then fFail "apt runs before the sources are moved to the snapshot"; fi
fi

if ((failures)); then fEcho "${failures} release image pin check(s) failed"; exit 1; fi
fEcho "release image pin checks passed"
