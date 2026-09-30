#!/usr/bin/env bash

##	- Purpose: Check how many processes vendor-themes.bash starts per icon it
##	  stages. Every one is a fork, and the lookup runs for about 3,600 icons a
##	  run, so this is where its speed goes. The count comes from a PID
##	  namespace of its own, where nothing else takes a pid, so it is exact and
##	  a busy box cannot move it.
##	- The bar is 4.5 per icon. The lookup as it stands starts 4: a copy and a
##	  mkdir for each icon staged, and one per name it tries. As it used to be
##	  it started 11. Any one fork more per icon fails it, such as asking the
##	  first lookup, which every icon takes, through a substitution.
##	- Exits 77 where no PID namespace can be made without root.
##	- Runs in the lint stage.
##	- Syntax: cicd/utility/test-vendor-forks.bash
##	- Test ID: rj4j8jk8

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
## In halves, so one fork more per icon is over it.
halvesPerIcon=9

fEcho(){ echo "[ $* ]"; }

if ! unshare -Urpf true >/dev/null 2>&1; then
	fEcho "SKIPPED: vendor-themes fork count: this box will not make a PID namespace"
	exit 77
fi

report="$(unshare -Urpf bash "${here}/vendor-themes.bash" --count-forks)"
icons="" forks="" found=""
for field in ${report}; do
	case "${field}" in
		icons=*) icons="${field#icons=}" ;;
		forks=*) forks="${field#forks=}" ;;
		found=*) found="${field#found=}" ;;
	esac
done

## A count of nothing staged would pass for the wrong reason.
if [[ -z "${icons}" || -z "${forks}" || "${found:-0}" == "0" ]]; then
	fEcho "FAILED: vendor-themes fork count: no usable report: ${report}"
	exit 1
fi

if (( forks * 2 > icons * halvesPerIcon )); then
	fEcho "FAILED: vendor-themes fork count: ${forks} processes for ${icons} icons, over $(( halvesPerIcon / 2 )).5 each"
	exit 1
fi
fEcho "OK: vendor-themes fork count: ${forks} processes for ${icons} icons"
