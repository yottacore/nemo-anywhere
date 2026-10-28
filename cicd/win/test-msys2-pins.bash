#!/usr/bin/env bash

##	- Purpose: Check that the hosted Windows build installs only pinned MSYS2
##	  packages. The workflow pins setup-msys2 by commit, which fixes the base
##	  install, turns its update off and names no packages of its own. The list
##	  is well formed, has every root, and was written for that same commit. The
##	  closure follows provides and leaves out the base, and a hole fails. The
##	  install refuses a bad checksum or line before pacman runs.
##	- Runs in the lint stage.
##	- Syntax: cicd/win/test-msys2-pins.bash
##	- Test ID: dehzp

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
workflow="${root}/.github/workflows/release-win.yml"
pins="${here}/msys2-pins.txt"
# shellcheck source=msys2-pins.bash
source "${here}/msys2-pins.bash"

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d)"
trap 'fSafeRm_IfThere "${scratch}"' EXIT

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	The workflow and the list
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

wf="$(cat "${workflow}")"
uses="$(grep -E '^[[:space:]]*uses:[[:space:]]*msys2/setup-msys2@' <<<"${wf}" || true)"
[[ "$(grep -c . <<<"${uses}")" == 1 ]] || fFail "want one setup-msys2 step, got: ${uses:-none}"
[[ "${uses}" =~ @([0-9a-f]{40})([[:space:]]|$) ]] || fFail "setup-msys2 not pinned by commit: ${uses}"
[[ "${BASH_REMATCH[1]:-}" == "${SETUP_MSYS2}" ]] || fFail "workflow has setup-msys2 ${BASH_REMATCH[1]:-?}, msys2-pins.bash has ${SETUP_MSYS2}"
## The with: block under that step, up to the next step.
block="$(sed -n '/uses:[[:space:]]*msys2\/setup-msys2@/,/^[[:space:]]*- name:/p' "${workflow}")"
grep -qE '^[[:space:]]*update:[[:space:]]*false[[:space:]]*$' <<<"${block}" || fFail "setup-msys2 must have update: false"
if grep -qE '^[[:space:]]*(install|pacboy):' <<<"${block}"; then fFail "setup-msys2 installs packages of its own"; fi
grep -qE '^[[:space:]]*run:[[:space:]]*bash cicd/win/msys2-pins\.bash install[[:space:]]*$' <<<"${wf}" || fFail "no step runs msys2-pins.bash install"
if grep -qE 'pacman[[:space:]]+-S' <<<"${wf}"; then fFail "workflow runs pacman -S, which installs whatever is current"; fi
if grep -E '^[[:space:]]*choco[[:space:]]+install' <<<"${wf}" | grep -vqE '[[:space:]]--version[[:space:]=][0-9]'; then fFail "a choco install with no --version"; fi

header="$(sed -n '2p' "${pins}")"
[[ "${header}" == "# setup-msys2 ${SETUP_MSYS2}, base ${BASE_DATE}" ]] || fFail "pins were written for another base: ${header}"

declare -A names=()
count=0
while read -r repo file sha extra; do
	[[ -z "${repo}" || "${repo}" == \#* ]] && continue
	count=$((count + 1))
	[[ "${repo}" == mingw/mingw64 || "${repo}" == msys/x86_64 ]] || fFail "unknown repo: ${repo}"
	[[ "${sha}" =~ ^[0-9a-f]{64}$ && -z "${extra}" ]] || fFail "bad line: ${repo} ${file} ${sha} ${extra}"
	if [[ ! "${file}" =~ ^([A-Za-z0-9@._+-]+)-[^-/]+-[^-/]+-(any|x86_64)\.pkg\.tar\.(zst|xz)$ ]]; then fFail "bad file name: ${file}"; continue; fi
	n="${BASH_REMATCH[1]}"
	[[ -z "${names[$n]:-}" ]] || fFail "${n} pinned twice"
	names[$n]=1
done < "${pins}"
[[ ${count} -gt 50 ]] || fFail "only ${count} pins"
for r in "${MINGW_ROOTS[@]}"; do [[ -n "${names[mingw-w64-x86_64-${r}]:-}" ]] || fFail "root ${r} not pinned"; done
for r in "${MSYS_ROOTS[@]}"; do [[ -n "${names[${r}]:-}" ]] || fFail "root ${r} not pinned"; done

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Closure, on a made-up database
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

## fDesc <dir> <name> <file> <deps> [provides]
fDesc(){
	mkdir -p "$1/$2"
	{
		printf '%%FILENAME%%\n%s\n\n%%NAME%%\n%s\n\n%%SHA256SUM%%\n%064d\n\n' "$3" "$2" 0
		printf '%%DEPENDS%%\n'; for d in $4; do echo "${d}"; done; echo
		if [[ -n "${5:-}" ]]; then printf '%%PROVIDES%%\n%s\n\n' "$5"; fi
	} > "$1/$2/desc"
}
z="$(printf '%064d' 0)"
db="${scratch}/db"
fDesc "${db}" app app-1-1-any.pkg.tar.zst "lib>=2 cc-libs"
fDesc "${db}" lib lib-2-1-any.pkg.tar.zst "app"
fDesc "${db}" gcc-libs gcc-libs-1-1-any.pkg.tar.zst "" "cc-libs=1"
fDesc "${db}" unused unused-1-1-any.pkg.tar.zst ""
got="$( (fMsys2Pins_Closure r "${db}" "" app) 2>&1 || true)"
want="r app-1-1-any.pkg.tar.zst ${z}
r gcc-libs-1-1-any.pkg.tar.zst ${z}
r lib-2-1-any.pkg.tar.zst ${z}"
[[ "${got}" == "${want}" ]] || fFail "closure with a cycle and a provides: ${got}"

fDesc "${db}" holey holey-1-1-any.pkg.tar.zst "nothing-has-this"
if (fMsys2Pins_Closure r "${db}" "" holey) > /dev/null 2>&1; then fFail "a dep nothing provides passed"; fi

sdb="${scratch}/sdb"
fDesc "${sdb}" intltool intltool-1-1-x86_64.pkg.tar.zst "perl-xml-parser"
fDesc "${sdb}" perl-xml-parser perl-xml-parser-1-1-x86_64.pkg.tar.zst "perl libexpat"
fDesc "${sdb}" perl perl-5-1-x86_64.pkg.tar.zst "libxcrypt sh"
fDesc "${sdb}" bash bash-5-1-x86_64.pkg.tar.zst "msys2-runtime" "sh"
printf 'bash 5-1\nlibxcrypt 4-1\nmsys2-runtime 3-1\nlibexpat 2-1\n' > "${scratch}/base.txt"
got="$( (fMsys2Pins_Closure m "${sdb}" "${scratch}/base.txt" intltool) 2>&1 || true)"
want="m intltool-1-1-x86_64.pkg.tar.zst ${z}
m perl-5-1-x86_64.pkg.tar.zst ${z}
m perl-xml-parser-1-1-x86_64.pkg.tar.zst ${z}"
[[ "${got}" == "${want}" ]] || fFail "closure leaving out the base: ${got}"

#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
#	Install, with stand-ins for uname, curl and pacman
#•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••

shims="${scratch}/bin"
mkdir "${shims}"
printf '#!/usr/bin/env bash\necho Msys\n' > "${shims}/uname"
## Each fetched file holds its own name, so its sha256 is known up front.
cat > "${shims}/curl" <<'END'
#!/usr/bin/env bash
cfg=""; while [[ $# -gt 0 ]]; do [[ "$1" == -K ]] && cfg="$2"; shift; done
while IFS= read -r line; do
	[[ "${line}" =~ ^output\ =\ \"(.*)\"$ ]] && printf '%s' "${BASH_REMATCH[1]##*/}" > "${BASH_REMATCH[1]}"
done < "${cfg}"
END
cat > "${shims}/pacman" <<'END'
#!/usr/bin/env bash
printf '%s\n' "$@" > "${PACMAN_LOG}"
END
chmod +x "${shims}"/*
export PACMAN_LOG="${scratch}/pacman.log"

fSum(){ printf '%s' "$1" | sha256sum | cut -d' ' -f1; }
printf '# x\n# y\nmingw/mingw64 a-1-1-any.pkg.tar.zst %s\nmsys/x86_64 b-1-1-x86_64.pkg.tar.zst %s\n' \
	"$(fSum a-1-1-any.pkg.tar.zst)" "$(fSum b-1-1-x86_64.pkg.tar.zst)" > "${scratch}/good.txt"
if (PATH="${shims}:${PATH}" fMsys2Pins_Install "${scratch}/good.txt") > "${scratch}/out" 2>&1; then
	got="$(sed 's#.*/##' "${PACMAN_LOG}" | tr '\n' ' ')"
	[[ "${got}" == "-U --noconfirm a-1-1-any.pkg.tar.zst b-1-1-x86_64.pkg.tar.zst " ]] || fFail "pacman got: ${got}"
else
	fFail "install of good pins failed: $(cat "${scratch}/out")"
fi

for bad in "sum:mingw/mingw64 a-1-1-any.pkg.tar.zst $(fSum other)" "line:mingw/mingw64 a-1-1-any.pkg.tar.zst nothex" "path:mingw/mingw64 ../a-1-1-any.pkg.tar.zst $(fSum ../a-1-1-any.pkg.tar.zst)" "empty:# only a comment"; do
	printf '%s\n' "${bad#*:}" > "${scratch}/bad.txt"
	: > "${PACMAN_LOG}"
	if (PATH="${shims}:${PATH}" fMsys2Pins_Install "${scratch}/bad.txt") > /dev/null 2>&1; then fFail "install took a bad ${bad%%:*}"; fi
	[[ ! -s "${PACMAN_LOG}" ]] || fFail "pacman ran on a bad ${bad%%:*}"
done

if [[ ${failures} -gt 0 ]]; then fEcho "${failures} failure(s)"; exit 1; fi
fEcho "OK: the hosted Windows build installs ${count} pinned MSYS2 packages and nothing else"

##	History:
##		- 20261028: Created.
