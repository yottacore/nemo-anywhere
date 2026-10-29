#!/usr/bin/env bash

##	- Purpose: The pipeline's output helpers, in one place. Several scripts used
##	  to carry their own one-line copy of fEcho_Clean, which lost the repeated-
##	  blank collapsing and made their section spacing differ from everything else.
##	- fEcho "msg"       -> a bracketed "[ msg ]" status line
##	- fEcho_Clean "msg" -> a plain line; a bare call emits one blank at most,
##	                       however many times it is called in a row
##	- fSection "title"  -> blank line, full-width rule, then the title
##	- fDie "msg"        -> a fatal line on stderr, then exit 1
##	- fEcho_Stamp       -> local time to the hundredth, as YYYYmmDD-HHMMSS.NN
##	- A lane's own steps, in the step lines of cicd.bash. The lane is one step
##	  of that run, so its steps say "<label> N of M", not "Step N of M":
##	  fLaneStep_Plan <label> <step>... -> the plan, so M is known at step 1
##	  fLaneStep <step>        -> closes the step before as PASS, opens this one
##	  fLaneStep_Close <word>  -> closes the open step, as PASS or FAIL
##	  fLaneStep_OnExit <rc>   -> for the EXIT trap: an open step closes as FAIL,
##	                             or as PASS when <rc> is 0
##	- Syntax: source this file; it defines functions only.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


declare -i _wasLastEchoBlank=0

fEcho_ResetBlankCounter(){ _wasLastEchoBlank=0; }
fEcho_Clean(){ if [[ -n "${1:-}" ]]; then echo -e "$*"; _wasLastEchoBlank=0; elif [[ $_wasLastEchoBlank -eq 0 ]] && echo; then _wasLastEchoBlank=1; fi; }
fEcho(){       if [[ -n "$*"     ]]; then fEcho_Clean "[ $* ]"; else fEcho_Clean ""; fi; }
fEcho_Force(){ fEcho_ResetBlankCounter; fEcho "$*"; }

_letterbox="•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••"
fSection(){ fEcho_Clean; fEcho_Clean "${_letterbox}"; fEcho "$*"; }
fDie(){ { fEcho_Force "FAILED: $*"; } >&2; exit 1; }

fEcho_Stamp(){
	local t="${EPOCHREALTIME:-}" frac
	if [[ -n "$t" ]]; then
		t="${t/,/.}"; frac="${t#*.}"
		printf '%(%Y%m%d-%H%M%S)T.%s' "${t%.*}" "${frac:0:2}"
	else
		printf '%s.00' "$(date +%Y%m%d-%H%M%S)"
	fi
}

_laneLabel=""; _laneSteps=(); _laneNow=0; _laneOpen=""
fLaneStep_Plan(){ _laneLabel="$1"; shift; _laneSteps=("$@"); _laneNow=0; _laneOpen=""; }
fLaneStep_Close(){
	[[ -n "$_laneOpen" ]] || return 0
	fEcho_Force "$(fEcho_Stamp): ${_laneLabel} ${_laneNow} of ${#_laneSteps[@]}; $1 ${_laneOpen}"
	_laneOpen=""
}
fLaneStep(){
	fLaneStep_Close PASS
	_laneNow=$((_laneNow + 1)); _laneOpen="$1"
	fEcho_Clean; fEcho_Clean "${_letterbox}"
	fEcho "$(fEcho_Stamp): ${_laneLabel} ${_laneNow} of ${#_laneSteps[@]}; Starting ${_laneOpen}"
	[[ "${_laneSteps[_laneNow - 1]:-}" == "$1" ]] || fEcho "WARNING: the plan has ${_laneLabel} step ${_laneNow} as '${_laneSteps[_laneNow - 1]:-none}'"
}
fLaneStep_OnExit(){
	[[ -n "$_laneOpen" ]] || return 0
	if [[ "${1:-1}" == 0 ]]; then fLaneStep_Close PASS; else fLaneStep_Close FAIL; fi
	fEcho_Clean
}


##	History:
##		- 2026-08-04: Split out of cicd.bash so the helper scripts stop
##		  redefining a lesser version of it.
##		- 2026-10-28: The rule is 79 dots, as in cicd.bash. fEcho_Stamp and the
##		  fLaneStep family, for the lanes on other boxes.
