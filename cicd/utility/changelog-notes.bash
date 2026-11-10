#!/usr/bin/env bash

##	- Purpose: Print one version's section of changelog.md, so a release can be
##	  published with the notes that were already written by hand rather than a
##	  machine-generated commit list.
##	- Output is the section body only: the "## v<version>" heading is dropped and
##	  the surrounding blank lines are trimmed, so it can go straight into
##	  `gh release create --notes-file`.
##	- Exits 1 with no output when that version has no section yet, which lets a
##	  caller fall back to generated notes instead of publishing an empty body.
##	- Version may be given with or without the leading v.
##	- Syntax: changelog-notes.bash <version> [changelog-path]

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

case "${1:-}" in
	""|-h|--help) sed -n '/^##	- Purpose:/,/^##	Copyright/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 2 ;;
esac

version="${1#v}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
file="${2:-${here}/../../changelog.md}"
[[ -f "$file" ]] || { echo "no changelog at ${file}" >&2; exit 1; }

## Matched as a literal string, not a pattern, since versions have dots and hyphens.
## The template section at the top is commented out, so it is skipped rather than
## mistaken for a real release.
notes="$(awk -v want="## v${version}" '
	BEGIN { n = length(want) }
	/^<!--/ { commented = 1 }
	/-->/   { commented = 0; next }
	commented { next }
	/^## / {
		if (substr($0, 1, n) == want && (length($0) == n || substr($0, n + 1, 1) == " ")) { inside = 1; next }
		if (inside) { exit }
	}
	inside {
		## Blanks are held back until something follows them, which trims the
		## leading and trailing ones without a second pass.
		if (NF) { while (held-- > 0) print ""; held = 0; print; started = 1 }
		else if (started) held++
	}
' "$file")"

[[ -n "$notes" ]] || exit 1
printf '%s\n' "$notes"
