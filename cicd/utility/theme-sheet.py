#!/usr/bin/env python3
"""Render a contact sheet per bundled icon theme, so the set can be judged by eye.

Nothing about an icon theme is assertable. Whether a folder reads as a folder,
whether 2 themes can be told apart, whether a glyph came out as a barcode. All
of it needs a person looking at a picture. This puts that picture one command
away, and it has already caught 3 defects that read as fine in
the SVG source: a film glyph drawn as a barcode, a photo frame filled solid, and
a resolver that quietly preferred 24px line icons over the real artwork.

Output goes to cicd/artifacts/themes, which isn't version controlled.

Syntax: theme-sheet.py [<repo-root>] [--names folder,user-trash,...]
"""

import os
import re
import subprocess
import sys

CELL = 76
PAD = 7
COLS = 10


def read_icon(path, index):
    """Body and viewBox of one SVG, with its ids made unique to this cell."""
    raw = open(path, encoding="utf-8", errors="replace").read()

    head = re.match(r"^\s*(?:<\?xml[^>]*\?>\s*)?<svg([^>]*)>", raw)
    attrs = head.group(1) if head else ""

    box = re.search(r'viewBox="([^"]+)"', attrs)
    if box:
        view = box.group(1)
    else:
        # No viewBox: the drawing is in the user space width/height describe.
        width = re.search(r'\bwidth="([\d.]+)', attrs)
        height = re.search(r'\bheight="([\d.]+)', attrs)
        view = "0 0 %s %s" % (width.group(1) if width else "48",
                      height.group(1) if height else "48")

    body = re.sub(r"^\s*(?:<\?xml[^>]*\?>\s*)?<svg[^>]*>", "", raw)
    body = re.sub(r"</svg>\s*$", "", body)

    # Gradients, clip paths and filters are referenced by id, and every theme
    # reuses the same short ones - without this, cell 20 paints with cell 3's
    # gradient.
    for gid in set(re.findall(r'id="([^"]+)"', body)):
        unique = "%s_%d" % (gid, index)
        body = body.replace('id="%s"' % gid, 'id="%s"' % unique)
        body = body.replace("url(#%s)" % gid, "url(#%s)" % unique)
        body = body.replace('href="#%s"' % gid, 'href="#%s"' % unique)

    return view, body


def build_sheet(theme_dir, names, out_svg):
    found = []
    for base, _dirs, files in os.walk(theme_dir):
        for name in sorted(files):
            if not name.endswith(".svg"):
                continue
            if names and name[:-4] not in names:
                continue
            found.append(os.path.join(base, name))
    found.sort(key=lambda p: os.path.basename(p))

    if not found:
        return 0

    rows = (len(found) + COLS - 1) // COLS
    width = COLS * CELL
    height = rows * (CELL + 15)

    parts = ['<svg xmlns="http://www.w3.org/2000/svg" '
         'xmlns:xlink="http://www.w3.org/1999/xlink" '
         'width="%d" height="%d" viewBox="0 0 %d %d">' % (width, height, width, height)]
    parts.append('<rect width="%d" height="%d" fill="#f2f2f2"/>' % (width, height))

    for index, path in enumerate(found):
        view, body = read_icon(path, index)
        x = (index % COLS) * CELL
        y = (index // COLS) * (CELL + 15)
        parts.append('<svg x="%d" y="%d" width="%d" height="%d" viewBox="%s">%s</svg>'
                 % (x + PAD, y + PAD, CELL - 2 * PAD, CELL - 2 * PAD, view, body))
        parts.append('<text x="%d" y="%d" font-family="Segoe UI,DejaVu Sans,sans-serif" '
                 'font-size="7" fill="#333" text-anchor="middle">%s</text>'
                 % (x + CELL // 2, y + CELL + 5, os.path.basename(path)[:-4][:24]))

    parts.append("</svg>")

    with open(out_svg, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(parts))

    return len(found)


def main(argv):
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    names = None

    rest = argv[1:]
    if rest and rest[0] == "--names":
        names = set(rest[1].split(","))
        rest = rest[2:]
    elif len(rest) >= 2 and rest[1] == "--names":
        root, names, rest = rest[0], set(rest[2].split(",")), rest[3:]
    elif rest:
        root = rest[0]

    out_dir = os.path.join(root, "cicd", "artifacts", "themes")
    os.makedirs(out_dir, exist_ok=True)

    sources = []
    for parent in (os.path.join(root, "vendor", "icons"), os.path.join(root, "assets", "icons")):
        if os.path.isdir(parent):
            sources += [os.path.join(parent, n) for n in sorted(os.listdir(parent))]

    rendered = 0
    for theme_dir in sources:
        if not os.path.isdir(theme_dir):
            continue
        name = os.path.basename(theme_dir)
        out_svg = os.path.join(out_dir, name + ".svg")
        count = build_sheet(theme_dir, names, out_svg)
        if not count:
            continue

        out_png = os.path.join(out_dir, name + ".png")
        try:
            subprocess.call(["rsvg-convert", "-w", "1000", out_svg, "-o", out_png])
        except OSError:
            out_png = "(no rsvg-convert)"
        print("[ %-16s %3d icons -> %s ]" % (name, count, out_png))
        rendered += 1

    print("[ %d sheets in %s ]" % (rendered, out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
