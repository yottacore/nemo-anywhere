#!/usr/bin/env bash

##	- Purpose: Checks on the FreeBSD pkg file and the -bsd- tarball beside it,
##	  read on this box with tar and python, so no FreeBSD is needed here.
##	  - The manifest names the app and this version as pkg writes a version,
##	    says FreeBSD, and depends on gtk3, glib and gdk-pixbuf-extra, and on no
##	    base system package.
##	  - Every file goes under /usr/local/nemo-anywhere, apart from the command
##	    link, the menu entry and the app icons, and the link and the menu entry
##	    point into that folder.
##	  - The tarball has the same files as that folder, so pkg add and
##	    install.bash install the same build. The pkg leaves out the folder's
##	    own copy of the app icon, which it has in the shared theme instead.
##	- No pkg for this version in cicd/artifacts/release skips with exit 77.
##	- Runs in the packages stage, after the FreeBSD lane.
##	- Syntax: cicd/bsd/test-pkg.bash [pkg file]
##	- Test ID: rjphng6y

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail
echo "[ Test $(sed -n 's/^##[[:space:]]*\(- \)\{0,1\}Test ID: //p' "${BASH_SOURCE[0]}") ${BASH_SOURCE[0]##*/} ]"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly slug="nemo-anywhere"

fEcho(){ echo "[ $* ]"; }

ver="$(grep -oP "(?<![_[:alnum:]])version\s*:\s*'\K[^']+" "${root}/source/meson.build" | head -1 || true)"
pkgFile="${1:-}"
if [[ -z "$pkgFile" ]]; then
	for pkgFile in "${root}/cicd/artifacts/release/${slug}-${ver}-bsd-"*.pkg; do break; done
fi
if [[ ! -f "$pkgFile" ]]; then
	fEcho "FreeBSD package check skipped: no ${slug}-${ver}-bsd-*.pkg (run cicd/bsd/lane.bash --release first)"
	exit 77
fi
if ! command -v python3 >/dev/null 2>&1 || ! command -v zstd >/dev/null 2>&1; then
	fEcho "FreeBSD package check skipped: needs python3 and zstd"
	exit 77
fi
tarball="${pkgFile%.pkg}.tar.gz"
[[ -f "$tarball" ]] || { fEcho "FAILED: no ${tarball##*/} beside ${pkgFile##*/}"; exit 1; }

python3 -I - "$pkgFile" "$tarball" "$slug" "$ver" <<'EOF'
import io, json, subprocess, sys, tarfile

pkg_path, tar_path, slug, ver = sys.argv[1:]
failures = []
fail = failures.append

raw = open(pkg_path, "rb").read()
if raw[:4] == b"\x28\xb5\x2f\xfd":
    raw = subprocess.run(["zstd", "-dc"], input=raw, capture_output=True, check=True).stdout
pkg = tarfile.open(fileobj=io.BytesIO(raw), mode="r:*")
members = {m.name.lstrip("/"): m for m in pkg.getmembers()}

manifest = json.loads(pkg.extractfile(members["+MANIFEST"]).read())
want_ver = ver.replace("-", ".")
if manifest.get("name") != slug:
    fail("manifest name is %r" % manifest.get("name"))
if manifest.get("version") != want_ver:
    fail("manifest version is %r, wanted %r" % (manifest.get("version"), want_ver))
if not str(manifest.get("abi", "")).startswith("FreeBSD:"):
    fail("manifest abi is %r" % manifest.get("abi"))
if manifest.get("prefix") != "/usr/local":
    fail("manifest prefix is %r" % manifest.get("prefix"))
deps = manifest.get("deps") or {}
for need in ("gtk3", "glib", "gdk-pixbuf-extra"):
    if need not in deps:
        fail("no dependency on " + need)
for name, dep in deps.items():
    if name.startswith("FreeBSD-"):
        fail("depends on the base system package " + name)
    if not dep.get("origin") or not dep.get("version"):
        fail("dependency %s has no origin or version" % name)

app = "usr/local/%s/" % slug
command = "usr/local/bin/" + slug
menu = "usr/local/share/applications/%s.desktop" % slug
exe = "/usr/local/%s/bin/%s" % (slug, slug)
files = [n for n, m in members.items() if not n.startswith("+") and not m.isdir()]
icons = 0
for n in files:
    if n.startswith(app) or n in (command, menu):
        continue
    if n.startswith("usr/local/share/icons/hicolor/") and "/apps/%s." % slug in n:
        icons += 1
        continue
    fail("file outside the app folder: /" + n)
if not icons:
    fail("no app icon in the shared theme")
if exe.lstrip("/") not in members:
    fail("no " + exe)
link = members.get(command)
if link is None or not link.issym() or link.linkname != exe:
    fail("/%s is not a link to %s" % (command, exe))
if menu not in members:
    fail("no menu entry")
else:
    text = pkg.extractfile(members[menu]).read().decode()
    if "\nExec=%s " % exe not in "\n" + text:
        fail("the menu entry does not start " + exe)

tar = tarfile.open(tar_path, "r:gz")
top = tar_path.rsplit("/", 1)[-1][: -len(".tar.gz")] + "/"
in_tar = sorted(m.name[len(top):] for m in tar.getmembers() if not m.isdir() and m.name.startswith(top))
in_pkg = sorted(n[len(app):] for n in files if n.startswith(app))
if any(n.startswith("share/icons/") for n in in_pkg):
    fail("icons under /%sshare/icons, which pkg delete leaves dirs behind for" % app)
in_tar = [n for n in in_tar if not n.startswith("share/icons/")]
if not in_tar:
    fail("the tarball has nothing under " + top)
elif in_tar != in_pkg:
    only_tar = sorted(set(in_tar) - set(in_pkg))[:5]
    only_pkg = sorted(set(in_pkg) - set(in_tar))[:5]
    fail("tarball and pkg differ: only in the tarball %s, only in the pkg %s" % (only_tar, only_pkg))

for f in failures:
    print("[ FAILED: %s ]" % f)
if failures:
    sys.exit(1)
print("[ OK: %s, %d dependencies, %d files ]" % (pkg_path.rsplit("/", 1)[-1], len(deps), len(files)))
EOF
