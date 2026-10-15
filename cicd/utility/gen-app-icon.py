#!/usr/bin/env python3
"""Cut the program icon down to the sizes each platform wants.

One source, assets/logo.png, becomes the Windows exe icon (source/src, embedded
by the resource compiler) and the hicolor app icons the Linux install and the
in-binary icon theme read. Output is committed, so no build step depends on this
script. Re-run it after changing the logo.

With --check nothing is written. It fails if a committed icon no longer looks
like the logo, which is how a new logo went unnoticed for two days.
Test ID: rhdsqqx1

Syntax: gen-app-icon.py [--check] [<repo-root>]
"""

import os
import sys

from PIL import Image, ImageChops

# The exe carries every size Windows asks for, from the file list up to the
# 256 the large-icon views and the alt-tab switcher use.
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

# Keep in step with publicIcons in source/data/icons/meson.build and the
# appicons block in source/gresources/nemo.gresource.xml.
THEME_SIZES = (16, 22, 24, 32, 48, 64, 128, 256)


# Per channel, out of 255. Enough to let a different Pillow resample a little
# differently, far short of a different picture.
CHECK_TOLERANCE = 8


def check(root, logo):
    apps = os.path.join(root, "source", "data", "icons", "hicolor", "apps")
    stale = []
    for size in THEME_SIZES:
        path = os.path.join(apps, "%dx%d" % (size, size), "nemo-anywhere.png")
        want = logo.resize((size, size), Image.LANCZOS)
        try:
            have = Image.open(path).convert("RGBA")
        except OSError:
            stale.append(path)
            continue
        if have.size != want.size:
            stale.append(path)
            continue
        worst = max(hi for _, hi in ImageChops.difference(have, want).getextrema())
        if worst > CHECK_TOLERANCE:
            stale.append(path)

    if stale:
        print("[ FAIL: app icons do not match assets/logo.png; run cicd/utility/gen-app-icon.py ]")
        for path in stale:
            print("  " + os.path.relpath(path, root))
        return 1
    print("[ OK: app icons match the logo ]")
    return 0


def main(argv):
    args = argv[1:]
    checking = "--check" in args
    args = [a for a in args if a != "--check"]
    root = args[0] if args else os.path.join(os.path.dirname(__file__), "..", "..")
    root = os.path.abspath(root)

    logo = Image.open(os.path.join(root, "assets", "logo.png")).convert("RGBA")

    if checking:
        tag = next(line[9:] for line in __doc__.splitlines() if line.startswith("Test ID: "))
        print("[ Test %s gen-app-icon.py --check ]" % tag)
        return check(root, logo)

    ico = os.path.join(root, "source", "src", "nemo-anywhere.ico")
    logo.save(ico, format="ICO", sizes=[(s, s) for s in ICO_SIZES])
    print("[ %s: %d KB ]" % (os.path.basename(ico), os.path.getsize(ico) / 1024))

    apps = os.path.join(root, "source", "data", "icons", "hicolor", "apps")
    total = 0
    for size in THEME_SIZES:
        out_dir = os.path.join(apps, "%dx%d" % (size, size))
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, "nemo-anywhere.png")
        logo.resize((size, size), Image.LANCZOS).save(out, optimize=True)
        total += os.path.getsize(out)
    print("[ hicolor apps: %d sizes, %d KB ]" % (len(THEME_SIZES), total / 1024))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
