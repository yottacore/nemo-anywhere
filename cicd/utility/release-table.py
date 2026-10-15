#!/usr/bin/env python3
"""Print the Downloads section of a release's notes: a table with the target OS
in rows and the CPU in columns, each cell linking that build's files.

Reads what GitHub says the release holds (gh release view <tag> --json assets),
not what was built here. GitHub renames any character outside [A-Za-z0-9._-] in
an asset name, so the names and links come from its side. A row or column only
shows up when something was built for it; a combination not built is an empty
cell. Checksums and files that name no OS and CPU go in a line under the table.

Prints nothing when the release has no files yet.

Syntax: release-table.py <assets.json>
        release-table.py --names <assets.json>   (one name per line, sorted)
"""

import json
import re
import sys

# Rows in the order the targets are worked on. The installers ask for "bsd" on
# every BSD, and the one built is FreeBSD.
OS_NAMES = {"linux": "Linux", "windows": "Windows", "bsd": "FreeBSD", "macos": "macOS"}
ARCH_ORDER = ["x86_64", "arm64"]
# Within a cell: the plain archive, then the packages, then the rest by name.
EXT_ORDER = ["tar.gz", "deb", "rpm", "pkg", "zip", "exe", "dmg"]

# <anything>-<os>-<cpu>[-<variant>...].<ext>, the version left out on purpose:
# a prerelease has hyphens in it and a renamed one has dots where it had others.
BUILD = re.compile(r"^.+-(?P<os>" + "|".join(OS_NAMES) + r")-(?P<arch>[a-z0-9]+(?:_[a-z0-9]+)?)"
                   r"(?P<variant>(?:-[a-z0-9]+)*)\.(?P<ext>[a-z0-9]+(?:\.[a-z0-9]+)?)$")
CHECKSUM = re.compile(r"sha256|sha512|\.(?:sig|asc)$")


def link(text, url):
    return "[%s](%s)" % (text, url)


def order(items, preferred):
    return sorted(items, key=lambda x: (preferred.index(x) if x in preferred else len(preferred), x))


def section(assets):
    cells = {}
    sums = []
    other = []
    for asset in sorted(assets, key=lambda a: a["name"]):
        # A file still uploading has no download yet.
        if asset.get("state", "uploaded") != "uploaded":
            continue
        name, url = asset["name"], asset["url"]
        m = None if CHECKSUM.search(name) else BUILD.match(name)
        if m:
            label = (m.group("variant").replace("-", " ").strip() + " " + m.group("ext")).strip()
            cells.setdefault((m.group("os"), m.group("arch")), []).append((m.group("ext"), label, url))
        elif CHECKSUM.search(name):
            sums.append(link(name, url))
        else:
            other.append(link(name, url))

    if not (cells or sums or other):
        return ""

    out = ["### Downloads", ""]
    if cells:
        oses = order({o for o, _ in cells}, list(OS_NAMES))
        arches = order({a for _, a in cells}, ARCH_ORDER)
        rows = [[""] + arches]
        for os_id in oses:
            row = [OS_NAMES[os_id]]
            for arch in arches:
                files = cells.get((os_id, arch), [])
                files.sort(key=lambda f: (EXT_ORDER.index(f[0]) if f[0] in EXT_ORDER else len(EXT_ORDER), f[1]))
                row.append(", ".join(link(label, url) for _, label, url in files))
            rows.append(row)
        widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
        widths = [max(w, 4) for w in widths]

        def line(row):
            return ("| " + " | ".join(c.ljust(w) for c, w in zip(row, widths))).rstrip()

        out.append(line(rows[0]))
        out.append(line([":---"] * len(widths)))
        out.extend(line(r) for r in rows[1:])
        out.append("")
    if sums:
        out.append("Checksums: " + ", ".join(sums))
        out.append("")
    if other:
        out.append("Other files: " + ", ".join(other))
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def main(argv):
    names_only = argv[1:2] == ["--names"]
    args = argv[2:] if names_only else argv[1:]
    if len(args) != 1:
        sys.exit("Syntax: release-table.py [--names] <assets.json>")
    with open(args[0], encoding="utf-8") as f:
        assets = json.load(f).get("assets") or []
    # A Windows runner would otherwise write CRLF.
    sys.stdout.reconfigure(newline="\n")
    if names_only:
        for name in sorted(a["name"] for a in assets if a.get("state", "uploaded") == "uploaded"):
            print(name)
    else:
        sys.stdout.write(section(assets))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
