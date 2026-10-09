#!/usr/bin/env bash

##	- Purpose: Check that no bundle has libgsf, the gsf-office thumbnailer,
##	  the old search converters or their helper definitions, and that no
##	  package depends on libgsf. The app reads office files itself
##	  (2026100909260549).
##	  - Kinds, any number: linux-x86_64 and linux-arm64 (tarball, .deb and .rpm,
##	    with their Depends and Requires), windows (the zip, which the setup exe
##	    is made from), bsd (the pkg with its deps, and the tarball beside it).
##	  - Only this version's files in cicd/artifacts/release are read. A kind
##	    with none is skipped; with none at all it exits 77.
##	  - The single exe is packed on Windows from the same staged folder as
##	    the zip's, so it is not read here.
##	- --self-test makes small bundles with and without those files and checks
##	  that it finds each. Runs in the lint stage; exit 77 with no python3.
##	- Runs in the packages stage.
##	- Syntax: cicd/utility/test-bundle-left-out.bash [--self-test] [--dir DIR] KIND...
##	- Test ID: rjvwpz9d

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly slug="nemo-anywhere"

fEcho(){ echo "[ $* ]"; }

## The file names, by base name, and the dependency names no bundle may have.
fChecker(){
	python3 -I - "$@" <<'EOF'
import fnmatch, io, json, subprocess, sys, tarfile, zipfile

BANNED = [
    "libgsf-*", "libgsf*.so*", "libgsf*.dll", "gsf-office-thumbnailer*", "gsf-office.thumbnailer",
    "nemo-anywhere-*-to-txt", "nemo-anywhere-*-to-txt.exe",
    "epub2text.nemo_search_helper", "mso.nemo_search_helper", "mso-word.nemo_search_helper",
    "mso-ppt.nemo_search_helper", "mso-xls.nemo_search_helper", "odf.nemo_search_helper",
    "mso-doc.nemo_search_helper",
]

failures = []

def unzstd(raw):
    if raw[:4] == b"\x28\xb5\x2f\xfd":
        return subprocess.run(["zstd", "-dc"], input=raw, capture_output=True, check=True).stdout
    return raw

def tar_names(raw):
    with tarfile.open(fileobj=io.BytesIO(unzstd(raw)), mode="r:*") as t:
        return [m.name for m in t.getmembers() if not m.isdir()], t

def check_names(label, names):
    count = 0
    for name in names:
        count += 1
        base = name.rstrip("/").rsplit("/", 1)[-1]
        for pattern in BANNED:
            if fnmatch.fnmatchcase(base.lower(), pattern):
                failures.append(f"{label}: has {name}")
                break
    if count == 0:
        failures.append(f"{label}: no files listed")
    print(f"{label}: {count} file(s) looked at")

def check_deps(label, deps):
    for dep in deps:
        if "gsf" in dep.lower():
            failures.append(f"{label}: depends on {dep.strip()}")

def ar_members(raw):
    if raw[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive")
    pos, out = 8, {}
    while pos + 60 <= len(raw):
        head = raw[pos:pos + 60]
        name = head[:16].decode().strip().rstrip("/")
        size = int(head[48:58].decode().strip())
        out[name] = raw[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
    return out

def tarball(path):
    with open(path, "rb") as f:
        with tarfile.open(fileobj=io.BytesIO(unzstd(f.read())), mode="r:*") as t:
            check_names(path.rsplit("/", 1)[-1], [m.name for m in t.getmembers() if not m.isdir()])

def deb(path):
    label = path.rsplit("/", 1)[-1]
    with open(path, "rb") as f:
        members = ar_members(f.read())
    for name, body in members.items():
        if name.startswith("data.tar"):
            with tarfile.open(fileobj=io.BytesIO(unzstd(body)), mode="r:*") as t:
                check_names(label, [m.name for m in t.getmembers() if not m.isdir()])
        elif name.startswith("control.tar"):
            with tarfile.open(fileobj=io.BytesIO(unzstd(body)), mode="r:*") as t:
                control = t.extractfile("./control").read().decode()
            for line in control.splitlines():
                if line.startswith(("Depends:", "Pre-Depends:", "Recommends:")):
                    check_deps(label, line.split(":", 1)[1].split(","))

def rpm(path):
    label = path.rsplit("/", 1)[-1]
    try:
        names = subprocess.run(["rpm", "-qp", "--qf", "[%{FILEMODES:perms} %{FILENAMES}\\n]", path],
                               capture_output=True, text=True, check=True).stdout
        reqs = subprocess.run(["rpm", "-qp", "--requires", path], capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        print(f"{label}: no rpm here, not read")
        return
    check_names(label, [l.split(" ", 1)[1] for l in names.splitlines() if l and not l.startswith("d")])
    check_deps(label, reqs.splitlines())

def zip_file(path):
    with zipfile.ZipFile(path) as z:
        check_names(path.rsplit("/", 1)[-1], [n for n in z.namelist() if not n.endswith("/")])

def pkg(path):
    label = path.rsplit("/", 1)[-1]
    with open(path, "rb") as f:
        raw = unzstd(f.read())
    with tarfile.open(fileobj=io.BytesIO(raw), mode="r:*") as t:
        names = [m.name for m in t.getmembers() if not m.isdir()]
        manifest = json.loads(t.extractfile("+COMPACT_MANIFEST").read())
    check_names(label, [n for n in names if not n.lstrip("/").startswith("+")])
    check_deps(label, list(manifest.get("deps", {}).keys()))

for path in sys.argv[1:]:
    if path.endswith(".tar.gz"):
        tarball(path)
    elif path.endswith(".deb"):
        deb(path)
    elif path.endswith(".rpm"):
        rpm(path)
    elif path.endswith(".zip"):
        zip_file(path)
    elif path.endswith(".pkg"):
        pkg(path)
    else:
        failures.append(f"{path}: not a kind this reads")

for f in failures:
    print(f"FAILED: {f}")
sys.exit(1 if failures else 0)
EOF
}

## Every file for these kinds of this version under $1.
fBundles(){
	local dir="$1" ver="$2" kind f; shift 2
	for kind in "$@"; do
		case "$kind" in
			linux-x86_64|linux-arm64)
				for f in "${dir}/${slug}-${ver}-${kind}".{tar.gz,deb,rpm}; do [[ -f "$f" ]] && echo "$f"; done ;;
			windows)
				f="${dir}/${slug}-${ver}-windows-x86_64.zip"; [[ -f "$f" ]] && echo "$f" ;;
			bsd)
				for f in "${dir}/${slug}-${ver}-bsd-"*.{pkg,tar.gz}; do [[ -f "$f" ]] && echo "$f"; done ;;
			*) echo "unknown kind: ${kind}" >&2; return 2 ;;
		esac
	done
	return 0
}

fSelfTest(){
	echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} --self-test ]"
	local scratch failures=0
	if ! command -v python3 >/dev/null 2>&1; then
		fEcho "bundle check self-test skipped: needs python3"
		return 77
	fi
	scratch="$(mktemp -d)"
	# shellcheck disable=SC2064  ## the path is fixed now
	trap "rm -rf '${scratch}'" RETURN
	fFail(){ fEcho "FAILED: $*"; failures=$((failures + 1)); }

	## $1 good|bad, $2 bundle
	fExpect(){
		local want="$1" file="$2" rc=0 out
		out="$(fChecker "$file" 2>&1)" || rc=$?
		case "$want" in
			good) [[ "$rc" == 0 ]] || fFail "${file##*/} was refused: ${out}" ;;
			bad)  [[ "$rc" == 1 && "$out" == *"FAILED: "* ]] || fFail "${file##*/} was let through (exit ${rc}): ${out}" ;;
		esac
		return 0
	}

	local tree="${scratch}/tree/${slug}"
	mkdir -p "${tree}/bin" "${tree}/share/${slug}/search-helpers" "${tree}/lib"
	touch "${tree}/bin/${slug}" "${tree}/share/${slug}/search-helpers/pdftotext.nemo_search_helper"
	tar -C "${scratch}/tree" -czf "${scratch}/good.tar.gz" "${slug}"
	for bad in bin/nemo-anywhere-xls-to-txt share/${slug}/search-helpers/odf.nemo_search_helper lib/libgsf-1.so.114; do
		touch "${tree}/${bad}"
		tar -C "${scratch}/tree" -czf "${scratch}/bad.tar.gz" "${slug}"
		fExpect bad "${scratch}/bad.tar.gz"
		rm -f "${tree:?}/${bad}"
	done
	fExpect good "${scratch}/good.tar.gz"

	## A zip like the Windows one, flat, with the dll at the root.
	local flat="${scratch}/flat/${slug}-x"
	mkdir -p "${flat}/share/thumbnailers"
	touch "${flat}/${slug}.exe" "${flat}/libglib-2.0-0.dll" "${flat}/share/thumbnailers/librsvg.thumbnailer"
	(cd "${scratch}/flat" && python3 -I -m zipfile -c "${scratch}/good.zip" "${slug}-x")
	fExpect good "${scratch}/good.zip"
	for bad in libgsf-1-114.dll gsf-office-thumbnailer.exe share/thumbnailers/gsf-office.thumbnailer; do
		touch "${flat}/${bad}"
		rm -f "${scratch}/bad.zip"
		(cd "${scratch}/flat" && python3 -I -m zipfile -c "${scratch}/bad.zip" "${slug}-x")
		fExpect bad "${scratch}/bad.zip"
		rm -f "${flat:?}/${bad}"
	done

	## A .deb, clean, then depending on libgsf.
	if command -v dpkg-deb >/dev/null 2>&1; then
		local pkgroot="${scratch}/deb"
		mkdir -p "${pkgroot}/DEBIAN" "${pkgroot}/opt/${slug}/bin"
		touch "${pkgroot}/opt/${slug}/bin/${slug}"
		printf 'Package: %s\nVersion: 1\nArchitecture: all\nMaintainer: x <x@example.com>\nDepends: libc6, libgtk-3-0\nDescription: x\n' "$slug" >"${pkgroot}/DEBIAN/control"
		dpkg-deb --root-owner-group -b "$pkgroot" "${scratch}/good.deb" >/dev/null
		fExpect good "${scratch}/good.deb"
		sed -i 's/^Depends: .*/Depends: libc6, libgsf-1-114 (>= 1.14)/' "${pkgroot}/DEBIAN/control"
		dpkg-deb --root-owner-group -b "$pkgroot" "${scratch}/bad.deb" >/dev/null
		fExpect bad "${scratch}/bad.deb"
	else
		fEcho "no dpkg-deb; the .deb cases not run"
	fi

	## A FreeBSD pkg: a tar with a compact manifest, depending on libgsf.
	local bsd="${scratch}/bsd"
	mkdir -p "${bsd}/usr/local/${slug}/bin"
	touch "${bsd}/usr/local/${slug}/bin/${slug}"
	printf '{"name":"%s","deps":{"gtk3":{"origin":"x11-toolkits/gtk30","version":"3"}}}' "$slug" >"${bsd}/+COMPACT_MANIFEST"
	tar -C "$bsd" -cf "${scratch}/good.pkg" +COMPACT_MANIFEST usr
	fExpect good "${scratch}/good.pkg"
	printf '{"name":"%s","deps":{"libgsf":{"origin":"devel/libgsf","version":"1"}}}' "$slug" >"${bsd}/+COMPACT_MANIFEST"
	tar -C "$bsd" -cf "${scratch}/bad.pkg" +COMPACT_MANIFEST usr
	fExpect bad "${scratch}/bad.pkg"

	if ((failures)); then
		fEcho "FAILED: bundle check self-test, ${failures} problem(s)"
		return 1
	fi
	fEcho "OK: bundle check self-test"
	return 0
}

if [[ "${1:-}" == --self-test ]]; then
	fSelfTest
	exit $?
fi

echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} $* ]"

dir="${root}/cicd/artifacts/release"
if [[ "${1:-}" == --dir ]]; then
	dir="${2:?--dir needs a folder}"
	shift 2
fi
(($#)) || { echo "usage: ${BASH_SOURCE[0]##*/} [--self-test] [--dir DIR] KIND..." >&2; exit 2; }

ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${root}/source/meson.build" | head -1 || true)"
bundles=()
while IFS= read -r f; do bundles+=("$f"); done < <(fBundles "$dir" "$ver" "$@")
if ((! ${#bundles[@]})); then
	fEcho "bundle check skipped: no ${slug}-${ver} bundle for $* in ${dir}"
	exit 77
fi
if ! command -v python3 >/dev/null 2>&1; then
	fEcho "bundle check skipped: needs python3"
	exit 77
fi

if ! fChecker "${bundles[@]}"; then
	fEcho "FAILED: bundle check, a bundle has a file or a dependency it may not"
	exit 1
fi
fEcho "OK: bundle check, ${#bundles[@]} file(s)"
