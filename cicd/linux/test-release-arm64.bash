#!/usr/bin/env bash

##	- Purpose: Check what release-arm64.bash sends to the arm64 box and what it
##	  brings back, without the box or an hour of building. A copy of the script
##	  runs in a scratch repo with stand-in ssh, scp and host lock on PATH. The
##	  "box" is a scratch dir, and its release.bash only writes a tarball naming
##	  the stamp, job count and arch it was handed.
##	- Checks: the lock is taken by the box's name, an old tree on the box is
##	  cleared, ignored files stay here, the stamp and job count reach the
##	  build, the tarball comes back beside the x86_64 one with both in the sums
##	  file, the .deb's dependency line is read on the box by package.bash and
##	  kept here against the tarball's sha256 before the build container is
##	  stopped, a failed read only warns and leaves no list, and a box that is
##	  not arm64 is refused before anything is sent.
##	- Runs in the lint stage. Skipped under MSYS2, where the lane never runs and
##	  sha256sum marks every line binary.
##	- Syntax: cicd/linux/test-release-arm64.bash
##	- Test ID: rjph1pxd

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then fEcho "arm64 lane check skipped: Linux only"; exit 77; fi
if ! command -v git >/dev/null 2>&1 || ! command -v sha256sum >/dev/null 2>&1; then
	fEcho "arm64 lane check skipped: needs git and sha256sum"
	exit 77
fi

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/release-arm64-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

ver="9.8.7-check"
repo="${scratch}/repo"
box="${scratch}/box"
log="${scratch}/calls.log"
armName="nemo-anywhere-${ver}-linux-arm64.tar.gz"
x86Name="nemo-anywhere-${ver}-linux-x86_64.tar.gz"
sumsName="nemo-anywhere-${ver}-sha256sums.txt"

mkdir -p "${repo}/cicd/linux" "${repo}/cicd/utility/include" "${repo}/source" "${repo}/cicd/artifacts/release"
cp "${root}/cicd/linux/release-arm64.bash" "${root}/cicd/linux/package.bash" "${repo}/cicd/linux/"
for inc in echo source-date release-files; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${repo}/cicd/utility/include/"
done
printf "project('nemo-anywhere', 'c', version: '%s')\n" "$ver" > "${repo}/source/meson.build"
printf 'ignored.txt\ncicd/artifacts/\n' > "${repo}/.gitignore"
echo "kept here" > "${repo}/ignored.txt"
echo "x86_64 build" > "${repo}/cicd/artifacts/release/${x86Name}"
## The box runs the copy it was sent, so this stands in for the hour.
cat > "${repo}/cicd/linux/release.bash" <<-EOF
	set -eu
	mkdir -p cicd/artifacts/release
	echo "\${SOURCE_DATE_EPOCH} \${CICD_MAX_JOBS} \$(uname -m)" > cicd/artifacts/release/${armName}
EOF
git -C "$repo" init -q
git -C "$repo" add source/meson.build

## A tree left on the box from the run before.
mkdir -p "${box}/nemo-anywhere-arm64/src"
echo "old" > "${box}/nemo-anywhere-arm64/src/stale.txt"

mkdir -p "${scratch}/bin" "${scratch}/boxbin"
realUname="$(command -v uname)"
cat > "${scratch}/boxbin/uname" <<-EOF
	#!/usr/bin/env bash
	if [[ "\${1:-}" == -m ]]; then echo "\${FAKE_ARCH:-aarch64}"; else exec "${realUname}" "\$@"; fi
EOF
## The box's release container: answers the dependency read with its own
## arch, unless told to fail.
cat > "${scratch}/boxbin/docker" <<-EOF
	#!/usr/bin/env bash
	echo "docker \$*" >> "${log}"
	if [[ "\$1 \$2" == "exec -i" ]]; then
		cat > /dev/null
		[[ -z "\${FAKE_DOCKER_FAIL:-}" ]] || exit 1
		echo "shlibs:Depends=libc6 (>= 2.35), libgtk-3-0 (>= 3.24.33), from-\$(uname -m)"
	fi
EOF
## ssh [-o opt]... host command: runs the command in the box dir, with none of
## this side's environment, as over a real ssh.
cat > "${scratch}/bin/ssh" <<-EOF
	#!/usr/bin/env bash
	while [[ "\${1:-}" == -o ]]; do shift 2; done
	echo "ssh \$1" >> "${log}"
	shift
	cd "${box}"
	exec env -i HOME="${box}" PATH="${scratch}/boxbin:\${PATH}" FAKE_ARCH="\${FAKE_ARCH:-}" FAKE_DOCKER_FAIL="\${FAKE_DOCKER_FAIL:-}" bash -c "\$*"
EOF
## scp [-q] [-o opt]... host:path dest
cat > "${scratch}/bin/scp" <<-EOF
	#!/usr/bin/env bash
	args=()
	while ((\$#)); do case "\$1" in -q) shift ;; -o) shift 2 ;; *) args+=("\$1"); shift ;; esac; done
	cp "${box}/\${args[0]#*:}" "\${args[1]}"
EOF
cat > "${scratch}/lock.bash" <<-EOF
	echo "lock \$*" >> "${log}"
	while ((\$#)) && [[ "\$1" != -- ]]; do shift; done
	shift
	exec "\$@"
EOF
chmod +x "${scratch}/bin/"* "${scratch}/boxbin/"*

fRunLane(){
	PATH="${scratch}/bin:${PATH}" WINDOWS_HOST_LOCK="${scratch}/lock.bash" \
		NEMO_ARM_HOST="tester@armbox" NEMO_ARM_LOCK="armbox" NEMO_ARM_JOBS=3 \
		SOURCE_DATE_EPOCH=1791244781 bash "${repo}/cicd/linux/release-arm64.bash" 2>&1
}

rc=0; out="$(fRunLane)" || rc=$?
if [[ "$rc" != 0 ]]; then
	fFail "the lane failed (exit ${rc}): ${out}"
else
	got="${repo}/cicd/artifacts/release/${armName}"
	if [[ ! -f "$got" ]]; then
		fFail "no ${armName} brought back"
	elif [[ "$(cat "$got")" != "1791244781 3 aarch64" ]]; then
		fFail "the build got '$(cat "$got")', not the stamp, job count and arch it was meant to"
	fi
	sums="${repo}/cicd/artifacts/release/${sumsName}"
	for name in "$armName" "$x86Name"; do
		grep -q "  ${name}\$" "$sums" 2>/dev/null || fFail "${name} is not in the sums file"
	done
	( cd "${repo}/cicd/artifacts/release" && sha256sum -c --quiet "$sumsName" >/dev/null 2>&1 ) || fFail "the sums file does not verify"
	[[ ! -e "${box}/nemo-anywhere-arm64/src/stale.txt" ]] || fFail "the old tree on the box was not cleared"
	[[ -f "${box}/nemo-anywhere-arm64/src/source/meson.build" ]] || fFail "a tracked file was not sent"
	[[ -f "${box}/nemo-anywhere-arm64/src/cicd/linux/release-arm64.bash" ]] || fFail "an untracked file was not sent"
	[[ ! -e "${box}/nemo-anywhere-arm64/src/ignored.txt" ]] || fFail "an ignored file was sent"
	[[ ! -e "${box}/nemo-anywhere-arm64/src/cicd/artifacts/release/${x86Name}" ]] || fFail "this side's artifacts were sent"
	grep -q '^lock wrap armbox ' "$log" || fFail "the box was not locked by its name"
	grep -q '^ssh tester@armbox$' "$log" || fFail "ssh did not go to NEMO_ARM_HOST"
	grep -q '^docker stop nemo-build-jammy' "$log" || fFail "the build container was not stopped"
	deps="${repo}/cicd/artifacts/deb-depends/${armName%.tar.gz}.txt"
	if [[ ! -f "$deps" ]]; then
		fFail "no dependency list for the arm64 tarball"
	else
		[[ "$(head -1 "$deps")" == "$(cd "${repo}/cicd/artifacts/release" && sha256sum "$armName")" ]] || fFail "the dependency list names '$(head -1 "$deps")', not the tarball's sha256"
		[[ "$(sed -n 2p "$deps")" == "libc6 (>= 2.35), libgtk-3-0 (>= 3.24.33), from-aarch64" ]] || fFail "the dependency list has '$(sed -n 2p "$deps")', not the box container's reading"
	fi
	readAt="$(grep -n 'exec -i nemo-build-jammy' "$log" | head -1 | cut -d: -f1)"
	stopAt="$(grep -n '^docker stop nemo-build-jammy' "$log" | head -1 | cut -d: -f1)"
	[[ -n "$readAt" && -n "$stopAt" && "$readAt" -lt "$stopAt" ]] || fFail "the dependencies were not read in the box's container before it was stopped"
fi

## The box can't read the dependencies: the tarball still comes back, with
## a warning and no list, and an old list goes.
rm -f "${repo}/cicd/artifacts/release/${armName}" "$log"
rc=0; out="$(FAKE_DOCKER_FAIL=1 fRunLane)" || rc=$?
if [[ "$rc" != 0 ]]; then
	fFail "the lane failed when the dependency read did (exit ${rc}): ${out}"
else
	[[ -f "${repo}/cicd/artifacts/release/${armName}" ]] || fFail "no tarball when the dependency read failed"
	[[ ! -e "${repo}/cicd/artifacts/deb-depends/${armName%.tar.gz}.txt" ]] || fFail "a dependency list was left when the read failed"
	[[ "$out" == *"WARNING: could not read the arm64 dependencies"* ]] || fFail "no warning when the dependency read failed"
	grep -q '^docker stop nemo-build-jammy' "$log" || fFail "the build container was not stopped after a failed read"
fi

## A box that answers but is not arm64.
rm -f "${repo}/cicd/artifacts/release/${armName}" "$log"
echo "old" > "${box}/nemo-anywhere-arm64/src/stale.txt"
rc=0; out="$(FAKE_ARCH=x86_64 fRunLane)" || rc=$?
[[ "$rc" != 0 ]] || fFail "an x86_64 box was taken as arm64"
[[ "$out" == *"is not an arm64 box"* ]] || fFail "no reason given for the x86_64 box: ${out}"
[[ -e "${box}/nemo-anywhere-arm64/src/stale.txt" ]] || fFail "the tree on an x86_64 box was touched"
[[ ! -e "${repo}/cicd/artifacts/release/${armName}" ]] || fFail "an arm64 tarball came from an x86_64 box"

if ((failures)); then
	fEcho "FAILED: arm64 lane, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: arm64 lane sends the tree, builds with this side's stamp and brings the tarball back"
