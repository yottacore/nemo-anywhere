#!/usr/bin/env bash

##	- Purpose: Install the tracked git hooks into this clone's .git/hooks. Run once
##	  per clone (hooks live outside version control). Idempotent.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only


set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hooksDir="$(git -C "${here}" rev-parse --absolute-git-dir)/hooks"
mkdir -p "${hooksDir}"

hooks=(pre-push)
for hook in "${hooks[@]}"; do
	src="${here}/${hook}"
	[[ -f "$src" ]] || continue
	install -m 0755 "$src" "${hooksDir}/${hook}"
	echo "installed ${hook} -> ${hooksDir}/${hook}"
done
