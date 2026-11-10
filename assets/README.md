# Assets

First-party artwork. Everything here is original work by the nemo-anywhere project and is under the project's GPL-2.0-only license, in `license.txt` at the repo root. Third-party themes are under `vendor/` instead, with their own license files and pinned upstream commits.

## logo.png

This is the program icon. `cicd/utility/gen-app-icon.py` cuts the other icons from it: the Windows exe icon at `source/src/nemo-anywhere.ico`, and the `nemo-anywhere` app icon at each size the Linux install and the in-binary icon theme read. Those are committed too, so no build step depends on the script. Re-run it after changing the logo:

~~~sh
python3 cicd/utility/gen-app-icon.py
~~~

## icons

There are 2 icon themes here, `Luna` for Windows XP and `Aero` for Windows 7. `cicd/utility/gen-icon-theme.py` draws them, and they're committed so no build step depends on the script. Re-run it after changing a shape or a palette:

~~~sh
python3 cicd/utility/gen-icon-theme.py
~~~

They exist because there's no cleanly licensed SVG icon set in either style. What's out there is Microsoft's own shell art, extracted and repackaged, sometimes under a license the repackager had no right to grant. That can't go in this project, so both styles are drawn from scratch. They share one set of glyphs and base shapes, such as folder, page, disc, drive, monitor, and bin, recolored per theme.

The themes cover folders, file types, and devices, which is where the look shows. Toolbar and status glyphs are left out on purpose and fall through `Inherits` to Adwaita, which is what the freedesktop fallback chain is for.
