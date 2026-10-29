#!/usr/bin/env bash

##	- Purpose: Check that the day-to-day build image is pinned: its base by
##	  digest, and every apt source, debug symbols included, on one dated
##	  snapshot before apt first runs. Without that, a box that builds the image
##	  gets whatever trixie has that day, and the suppressions' dbgsym versions
##	  can stop matching the libraries.
##	- Runs the Dockerfile's own sources rewrite over the stock trixie sources.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-dev-image-pin.bash
##	- Test ID: dejma

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dockerfile="${DEV_DOCKERFILE:-${here}/Dockerfile.dev}"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

text="$(cat "$dockerfile")"

froms="$(grep -E '^FROM[[:space:]]' <<<"$text" || true)"
[[ -n "$froms" ]] || fFail "no FROM line in ${dockerfile}"
while IFS= read -r line; do
	[[ -z "$line" ]] && continue
	[[ "$line" =~ @sha256:[0-9a-f]{64}([[:space:]]|$) ]] || fFail "base not pinned by digest: ${line}"
done <<<"$froms"

## debian.sources as it comes in the trixie base image.
stock='Types: deb
# http://snapshot.debian.org/archive/debian/20260713T000000Z
URIs: http://deb.debian.org/debian
Suites: trixie trixie-updates
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp

Types: deb
# http://snapshot.debian.org/archive/debian-security/20260713T000000Z
URIs: http://deb.debian.org/debian-security
Suites: trixie-security
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.pgp'

## Comments out, continuation lines joined, so each RUN is one line.
joined="$(grep -v '^[[:space:]]*#' <<<"$text" | sed -e ':a' -e '/\\$/{N;s/\\\n//;ba' -e '}')"
expr="$(grep -oE "sed -i -E '[^']+' /etc/apt/sources.list.d/debian.sources" <<<"$joined" | head -1 || true)"
if [[ -z "$expr" ]]; then
	fFail "nothing rewrites /etc/apt/sources.list.d/debian.sources"
else
	expr="${expr#sed -i -E \'}"; expr="${expr%\' /etc/apt/sources.list.d/debian.sources}"
	uris="$(sed -E "$expr" <<<"$stock" | grep '^URIs:')"
	## Any other source the file adds, such as the debug archive.
	uris+=$'\n'"$(grep -oE "URIs: [^' ]+" <<<"$joined" || true)"
	dates=""
	while IFS= read -r line; do
		[[ -z "$line" ]] && continue
		if [[ "$line" =~ ^URIs:\ http://snapshot\.debian\.org/archive/debian(-security|-debug)?/([0-9]{8}T[0-9]{6}Z)$ ]]; then
			dates+="${BASH_REMATCH[2]}"$'\n'
		else
			fFail "source not on a dated snapshot: ${line}"
		fi
	done <<<"$uris"
	[[ "$(sort -u <<<"$dates" | grep -c .)" == 1 ]] || fFail "sources do not share one snapshot: $(sort -u <<<"$dates" | grep . | tr '\n' ' ')"
	before="${joined%%debian.sources*}"
	[[ "$before" != *apt-get* && "$before" != *"apt "* ]] || fFail "apt runs before the sources are moved to the snapshot"
	grep -q 'Check-Valid-Until "false"' <<<"$joined" || fFail "apt would refuse the snapshot's expired Release files"
fi
! grep -q 'deb\.debian\.org' <<<"$joined" || fFail "a source still reads the live archive: $(grep -o '[^ ]*deb\.debian\.org[^ ]*' <<<"$joined" | head -1)"

if ((failures)); then fEcho "${failures} dev image pin check(s) failed"; exit 1; fi
fEcho "dev image pin checks passed"


##	History:
##		- 2026-10-28: Created.
