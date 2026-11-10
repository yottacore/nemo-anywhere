#!/usr/bin/env bash

##	Purpose:
##		- The commit message an unattended run commits under, in one place so the
##		  pipeline and the publisher can't disagree about it.
##		- As a GIT_EDITOR: git invokes it as `git-auto-msg.bash <msgfile>`. Fill an
##		  empty message; leave one git already pre-filled (e.g. a merge message).
##		  Either way, never block.
##		- As `--suggest`: print the message and exit, for a caller that wants it up
##		  front rather than at editor time.
##		- Nothing here can know what a change MEANS, so the wording stays at the
##		  shape a person types in a hurry - the area that moved, or nothing useful
##		  at all. No timestamps, no describing the run that made it.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -euo pipefail

## --no-optional-locks: as a GIT_EDITOR this runs while the commit holds index.lock,
## and a git that decides to refresh the index would fail there.
fSuggest(){
	local areas area=""
	areas="$(git --no-optional-locks status --porcelain 2>/dev/null | sed 's/^...//; s/.* -> //; s|/.*||' | sort -u)"
	if [[ "$(printf '%s' "${areas}" | grep -c .)" == 1 ]]; then
		case "${areas}" in
			source)  area="source"    ;;
			cicd)    area="cicd"      ;;
			project) area="docs"      ;;
			assets)  area="assets"    ;;
			vendor)  area="vendor"    ;;
			.github) area="workflows" ;;
		esac
	fi
	if [[ -n "${area}" ]]; then printf '%s\n' "${area} tweaks"
	else                        printf '%s\n' "Updated"
	fi
}

if [[ "${1:-}" == "--suggest" ]]; then fSuggest; exit 0; fi

file="${1:?usage: git-auto-msg.bash <msgfile> | --suggest}"

## Ask git for its comment string rather than assume `#`: core.commentChar can
## be anything, and git strips by whatever it is.
cc="$(printf 'x\n' | git stripspace --comment-lines)"; cc="${cc% x}"
scissors="${cc} ------------------------ >8 ------------------------"

## What git keeps: everything above the scissors (commit.verbose puts a diff
## below it), minus comment lines and blank runs.
fKept(){ SC="$scissors" awk '$0 == ENVIRON["SC"] {exit} {print}' | git stripspace --strip-comments; }

tmpl="$(git config --path commit.template 2>/dev/null || true)"
existing="$(fKept <"$file")"
## An untouched template isn't a message; git refuses it as unedited.
if [[ -n "$existing" && -n "$tmpl" && -r "$tmpl" && "$existing" == "$(fKept <"$tmpl")" ]]; then existing=""; fi
[[ -n "$existing" ]] && exit 0

msg="${GIT_AUTO_MESSAGE:-}"
[[ -n "$(printf '%s' "$msg" | tr -d '[:space:]')" ]] || msg="$(fSuggest)"
kept="$(printf '%s\n' "$msg" | git stripspace --strip-comments)"
if [[ "$kept" != "$(printf '%s\n' "$msg" | git stripspace)" ]]; then
	printf 'git-auto-msg: message has a line starting with "%s", which git drops as a comment\n' "$cc" >&2
	exit 1
fi

## Keep git's own comment lines and anything from the scissors down; drop
## template text above them.
rest="$(SC="$scissors" CC="$cc" awk 'f || $0 == ENVIRON["SC"] {f=1; print; next} index($0, ENVIRON["CC"]) == 1 {print}' "$file")"
printf '%s\n\n%s\n' "$msg" "$rest" >"$file"
