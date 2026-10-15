#!/usr/bin/env bash

##	- Purpose: Check where package.bash gets the .deb's dependencies for each
##	  arch. A copy runs in a scratch repo against stand-in tarballs, with a
##	  stand-in docker for the release container and a stand-in uname for the
##	  box's arch.
##	- Checks: this box's arch reads the container and is the default even with
##	  a newer arm64 tarball beside it; --arch arm64 takes the line the arm64
##	  box read, only when it was read off the same bytes, and the conservative
##	  list with a warning otherwise; the arm64 .deb and .rpm say arm64 and
##	  aarch64 and come out the same twice; --depends-only prints the line alone
##	  on the arm64 box and fails rather than guess.
##	- Needs dpkg-deb; exit 77 without it. The .rpm checks need rpmbuild and rpm.
##	- Runs in the lint stage.
##	- Syntax: cicd/linux/test-package-arm64.bash
##	- Test ID: rjpxzs6x

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

fEcho(){ echo "[ $* ]"; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"

if ! command -v dpkg-deb >/dev/null 2>&1; then
	fEcho "arm64 package check skipped: no dpkg-deb"
	exit 77
fi
doRpm=0
if command -v rpmbuild >/dev/null 2>&1 && command -v rpm >/dev/null 2>&1; then doRpm=1; fi

failures=0
fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/package-arm64-check.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

ver="9.9.9-check"
lane="${scratch}/repo"
rel="${lane}/cicd/artifacts/release"
log="${scratch}/docker.log"
mkdir -p "${lane}/cicd/linux" "${lane}/cicd/utility/include" "$rel" "${scratch}/bin"
cp "${root}/cicd/linux/package.bash" "${lane}/cicd/linux/"
for inc in echo source-date release-files; do
	cp "${root}/cicd/utility/include/${inc}.bash" "${lane}/cicd/utility/include/"
done
# shellcheck source=../utility/include/release-files.bash
source "${root}/cicd/utility/include/release-files.bash"

fTarball(){
	local name="nemo-anywhere-${ver}-linux-$1" pfx
	pfx="${scratch}/src/${name}"
	mkdir -p "${pfx}/bin" "${pfx}/share/applications" "${pfx}/share/icons/hicolor/48x48/apps"
	echo "$1" > "${pfx}/share/icons/hicolor/48x48/apps/nemo-anywhere.png"
	printf '#!/bin/sh\necho %s\n' "$1" > "${pfx}/bin/nemo-anywhere"; chmod 755 "${pfx}/bin/nemo-anywhere"
	printf '[Desktop Entry]\nName=Nemo Anywhere\nExec=nemo-anywhere %%U\nIcon=nemo-anywhere\n' > "${pfx}/share/applications/nemo-anywhere.desktop"
	tar -czf "${rel}/${name}.tar.gz" -C "${scratch}/src" --owner=0 --group=0 --numeric-owner --sort=name --mtime=@1700000000 "$name"
}
fTarball x86_64
fTarball arm64
touch -d '+1 hour' "${rel}/nemo-anywhere-${ver}-linux-arm64.tar.gz"
armTar="${rel}/nemo-anywhere-${ver}-linux-arm64.tar.gz"
armDeb="${rel}/nemo-anywhere-${ver}-linux-arm64.deb"
armRpm="${rel}/nemo-anywhere-${ver}-linux-arm64.rpm"
x86Deb="${rel}/nemo-anywhere-${ver}-linux-x86_64.deb"
depsFile="$(fDebDependsFile "$lane" "${armTar##*/}")"

realUname="$(command -v uname)"
cat > "${scratch}/bin/uname" <<-EOF
	#!/usr/bin/env bash
	if [[ "\${1:-}" == -m ]]; then echo "\${FAKE_ARCH:-x86_64}"; else exec "${realUname}" "\$@"; fi
EOF
## The container answers with its own arch in the line, so a reading can be
## told from the arm64 box's.
cat > "${scratch}/bin/docker" <<-EOF
	#!/usr/bin/env bash
	echo "docker \$*" >> "${log}"
	[[ -z "\${FAKE_DOCKER_DOWN:-}" ]] || exit 1
	if [[ "\$1 \$2" == "exec -i" ]]; then
		cat > /dev/null
		echo "shlibs:Depends=libc6 (>= 2.35), from-container-\${FAKE_ARCH:-x86_64}"
	fi
EOF
chmod +x "${scratch}/bin/"*

## $1 where the output goes, rest the options.
fPack(){
	local outFile="$1"; shift
	local rc=0
	PATH="${scratch}/bin:${PATH}" TMPDIR="$scratch" SOURCE_DATE_EPOCH=1700000000 \
		bash "${lane}/cicd/linux/package.bash" "$@" > "$outFile" 2>&1 || rc=$?
	return "$rc"
}
fDepends(){ dpkg-deb -f "$1" Depends 2>/dev/null || true; }
fResetOut(){ rm -f "${rel}"/*.deb "${rel}"/*.rpm "$log"; }

## This box's arch, by default, from the container.
fResetOut
if ! fPack "${scratch}/out" --no-rpm; then
	fFail "the default run failed: $(cat "${scratch}/out")"
elif [[ -e "$armDeb" || ! -f "$x86Deb" ]]; then
	fFail "the default run did not package the x86_64 tarball alone"
else
	[[ "$(fDepends "$x86Deb")" == "libc6 (>= 2.35), from-container-x86_64" ]] || fFail "the x86_64 .deb depends on '$(fDepends "$x86Deb")', not the container's reading"
fi

## arm64 here, with the arm64 box's line read off these bytes.
mkdir -p "$(dirname "$depsFile")"
{ (cd "$rel" && sha256sum "${armTar##*/}"); echo "libc6 (>= 2.34), libgtk-3-0 (>= 3.24.33), from-arm64-box"; } > "$depsFile"
fResetOut
if ! fPack "${scratch}/out" --arch arm64; then
	fFail "--arch arm64 failed: $(cat "${scratch}/out")"
elif [[ ! -f "$armDeb" || -e "$x86Deb" ]]; then
	fFail "--arch arm64 did not package the arm64 tarball alone"
else
	[[ "$(fDepends "$armDeb")" == "libc6 (>= 2.34), libgtk-3-0 (>= 3.24.33), from-arm64-box" ]] || fFail "the arm64 .deb depends on '$(fDepends "$armDeb")', not the arm64 box's line"
	[[ "$(dpkg-deb -f "$armDeb" Architecture)" == arm64 ]] || fFail "the arm64 .deb is for $(dpkg-deb -f "$armDeb" Architecture)"
	! grep -q 'exec -i' "$log" 2>/dev/null || fFail "the arm64 .deb's dependencies were read in this box's container"
	if ((doRpm)); then
		if [[ ! -f "$armRpm" ]]; then
			fFail "no arm64 .rpm: $(cat "${scratch}/out")"
		else
			[[ "$(rpm -qp --qf '%{ARCH}' "$armRpm" 2>/dev/null)" == aarch64 ]] || fFail "the arm64 .rpm is for $(rpm -qp --qf '%{ARCH}' "$armRpm" 2>/dev/null)"
		fi
	fi
	cp "$armDeb" "${scratch}/first.deb"
	[[ ! -f "$armRpm" ]] || cp "$armRpm" "${scratch}/first.rpm"
	fResetOut
	fPack "${scratch}/out" --arch arm64 || true
	cmp -s "$armDeb" "${scratch}/first.deb" || fFail "the arm64 .deb came out different the second time"
	[[ ! -f "${scratch}/first.rpm" ]] || cmp -s "$armRpm" "${scratch}/first.rpm" || fFail "the arm64 .rpm came out different the second time"
fi

## A line read off another build, then none at all.
for kind in other none; do
	if [[ "$kind" == other ]]; then
		{ echo "0000000000000000000000000000000000000000000000000000000000000000  ${armTar##*/}"; echo "from-another-build"; } > "$depsFile"
	else
		rm -f "$depsFile"
	fi
	fResetOut
	if ! fPack "${scratch}/out" --arch arm64 --no-rpm; then
		fFail "--arch arm64 with ${kind} list failed: $(cat "${scratch}/out")"
		continue
	fi
	got="$(fDepends "$armDeb")"
	[[ "$got" == libc6,* && "$got" != *from-* ]] || fFail "with ${kind} list the arm64 .deb depends on '${got}', not the conservative list"
	grep -q 'WARNING: could not read the arm64 dependencies' "${scratch}/out" || fFail "with ${kind} list there was no warning"
done

## --depends-only, as the arm64 lane runs it on the box.
fResetOut
rc=0; FAKE_ARCH=aarch64 fPack "${scratch}/out" --depends-only --from "$armTar" || rc=$?
if [[ "$rc" != 0 ]]; then
	fFail "--depends-only on an arm64 box failed: $(cat "${scratch}/out")"
else
	[[ "$(cat "${scratch}/out")" == "libc6 (>= 2.35), from-container-aarch64" ]] || fFail "--depends-only printed '$(cat "${scratch}/out")', not the line alone"
	ls "$rel"/*.deb "$rel"/*.rpm >/dev/null 2>&1 && fFail "--depends-only made a package"
fi
rc=0; FAKE_ARCH=aarch64 FAKE_DOCKER_DOWN=1 fPack "${scratch}/out" --depends-only --from "$armTar" || rc=$?
[[ "$rc" != 0 ]] || fFail "--depends-only with no container passed: $(cat "${scratch}/out")"
grep -q from- "${scratch}/out" && fFail "--depends-only with no container printed a list"
rc=0; fPack "${scratch}/out" --depends-only --from "$armTar" || rc=$?
[[ "$rc" != 0 ]] || fFail "--depends-only read an arm64 tarball's dependencies on an x86_64 box"

if ((failures)); then
	fEcho "FAILED: arm64 packages, ${failures} problem(s)"
	exit 1
fi
fEcho "OK: arm64 packages take the arm64 box's dependency line"
