#!/usr/bin/env python3
"""Render one sheet showing every bundled icon set, for the README.

theme-sheet.py checks "is this set drawn correctly", with one sheet per set and
as many icons as you ask for. This sheet is for a different question: "which of
these do I want", which needs them all on one page and only enough icons each
to tell them apart. That takes 4: a folder, an open folder, a document, and the
trash. The folder is what a set is recognized by, and the other 3 show how it
handles a mimetype, a state, and a device-ish object.

Each set is drawn against both backgrounds, side by side, since half of them are
drawn for a dark desktop and wash out on white.

Every icon is rasterized on its own before being placed, rather than having its
markup pasted into one big document. Several sets color themselves through a
stylesheet keyed on the same class name, .ColorScheme-Text, applied through
currentColor, so pasted together they either all take the last
set's color or, once the classes are renamed apart, match no rule at all and
come out black. Rendering each file as its own document is how the application
draws them anyway, so what the sheet shows is what the picker will show.

Output is assets/icon-gallery.png, which is committed, since the README points at it.

Syntax: icon-gallery.py [<repo-root>]
"""

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

import base64
import os
import subprocess
import sys
import tempfile

# The 4 that tell 2 sets apart, in the order they read best.
NAMES = ("folder", "folder-open", "text-x-generic", "user-trash")

ICON = 48                  # drawn size of one icon
GAP = 7                    # between icons within a half
HALF = 4 * ICON + 5 * GAP  # one background's worth
CELL_W = 2 * HALF          # light half + dark half
LABEL = 22                 # room under a row for its name
ROW_H = ICON + 2 * GAP + LABEL
COLS = 2  # sets per row of the sheet

LIGHT = "#f4f4f4"
DARK = "#242424"

# The shim with the couple of dozen names Adwaita dropped. Nobody picks it.
SKIP = {"AdwaitaLegacy"}


def find_icon(theme_dir, name):
    """The file drawing @name in @theme_dir, or None."""
    for base, _dirs, files in os.walk(theme_dir):
        if name + ".svg" in files:
            return os.path.join(base, name + ".svg")
    return None


def style_of(theme_dir):
    """The set's own label for its look, falling back to its directory name."""
    index = os.path.join(theme_dir, "index.theme")
    if os.path.isfile(index):
        for line in open(index, encoding="utf-8", errors="replace"):
            if line.startswith("X-Nemo-Style="):
                return line.split("=", 1)[1].strip()
    return os.path.basename(theme_dir)


def rasterise(src, size, scratch):
    """@src as a data: png of @size square, or None if it won't render."""
    out = os.path.join(scratch, "cell.png")
    try:
        subprocess.check_call(["rsvg-convert", "-w", str(size), "-h", str(size),
                       "-o", out, src],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return None
    with open(out, "rb") as handle:
        return "data:image/png;base64," + base64.b64encode(handle.read()).decode("ascii")


def collect(root):
    """Every set worth offering, ours first, one entry per light/dark pair."""
    found = []
    for parent in (os.path.join(root, "assets", "icons"), os.path.join(root, "vendor", "icons")):
        if not os.path.isdir(parent):
            continue
        for name in sorted(os.listdir(parent)):
            path = os.path.join(parent, name)
            if os.path.isdir(path) and name not in SKIP:
                found.append(path)

    # The 2 halves of a light/dark pair are one entry: the picker offers them
    # as a single choice and swaps between them with the mode.
    paired = {}
    for path in found:
        name = os.path.basename(path)
        paired.setdefault(name[:-5] if name.endswith("-dark") else name, path)
    return [paired[key] for key in sorted(paired, key=str.lower)]


def build(root, out_svg, scratch):
    sets = collect(root)
    rows = (len(sets) + COLS - 1) // COLS
    width = COLS * CELL_W
    height = rows * ROW_H

    parts = ['<svg xmlns="http://www.w3.org/2000/svg" '
         'xmlns:xlink="http://www.w3.org/1999/xlink" '
         'width="%d" height="%d" viewBox="0 0 %d %d">' % (width, height, width, height)]
    parts.append('<rect width="%d" height="%d" fill="%s"/>' % (width, height, LIGHT))

    for slot, theme_dir in enumerate(sets):
        x0 = (slot % COLS) * CELL_W
        y0 = (slot // COLS) * ROW_H

        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s"/>'
                 % (x0, y0, HALF, ROW_H, LIGHT))
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s"/>'
                 % (x0 + HALF, y0, HALF, ROW_H, DARK))

        for n, name in enumerate(NAMES):
            path = find_icon(theme_dir, name)
            if path is None:
                continue
            data = rasterise(path, ICON * 2, scratch)
            if data is None:
                continue
            for slice_x in (x0, x0 + HALF):
                parts.append('<image x="%d" y="%d" width="%d" height="%d" '
                         'xlink:href="%s"/>'
                         % (slice_x + GAP + n * (ICON + GAP), y0 + GAP,
                        ICON, ICON, data))

        # One label per set, centered across the seam, so the 2 halves read as
        # the same set seen twice rather than as 2 different sets.
        parts.append('<rect x="%d" y="%d" width="1" height="%d" fill="#0000002c"/>'
                 % (x0 + HALF, y0, ROW_H))
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#7f7f7f28"/>'
                 % (x0, y0 + ROW_H - LABEL, CELL_W, LABEL))
        parts.append('<text x="%d" y="%d" font-family="Segoe UI,DejaVu Sans,sans-serif" '
                 'font-size="12" fill="#909090" text-anchor="middle">%s</text>'
                 % (x0 + CELL_W // 2, y0 + ROW_H - 7, style_of(theme_dir)))

    parts.append("</svg>")
    with open(out_svg, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(parts))
    return len(sets), width


def main(argv):
    root = os.path.abspath(argv[1] if len(argv) > 1
                   else os.path.join(os.path.dirname(__file__), "..", ".."))
    out_dir = os.path.join(root, "assets")
    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, "icon-gallery.png")

    scratch = tempfile.mkdtemp(prefix="nemo-gallery.")
    out_svg = os.path.join(scratch, "gallery.svg")
    try:
        count, width = build(root, out_svg, scratch)
        subprocess.call(["rsvg-convert", "-w", str(width), out_svg, "-o", out_png])
    finally:
        for name in os.listdir(scratch):
            os.remove(os.path.join(scratch, name))
        os.rmdir(scratch)

    print("[ %d sets -> %s ]" % (count, out_png))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
