<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
# filesystem

A mirror of where things go on disk, so a folder in here can be copied straight to the matching place on a machine. Nothing in here is compiled, installed or packaged - it is drop-in material, and the application picks it up at startup with no build step and no restart beyond the next launch.

The drop-in folders so far are for icon and widget themes:

~~~text
filesystem/.config/nemo-anywhere/icons/
filesystem/.config/nemo-anywhere/themes/
~~~

## Where that folder really lives

The config directory follows the platform, so `.config` above is the Linux spelling of it. The icons folder sits beside `settings.shcl` wherever that is:

| Platform   | Icon themes go in
| :---       | :---
| Linux, BSD | `~/.config/nemo-anywhere/icons/`
| Windows    | `%APPDATA%\nemo-anywhere\icons\`
| macOS      | `~/Library/Application Support/nemo-anywhere/icons/`

Widget themes work the same way, in `themes/` beside it. The application creates both folders empty on first run, so the place to put something is discoverable without reading this.

One icon theme is one folder with an `index.theme` in it - the ordinary freedesktop layout, exactly as any GTK icon theme comes. Drop it in whole and it appears in **Settings -> Appearance -> Icons** next time the application starts. A drop-in of the same name as a bundled set wins, which is how one can be replaced without rebuilding anything.

## Two optional lines in index.theme

The picker only offers themes drawn for the light or dark mode in force, and it can swap between the two halves of a pair when the mode changes. It reads two keys of ours from `index.theme` to do that, and guesses from the folder name when they are absent:

~~~ini
X-Nemo-Style=Circles Blue
X-Nemo-Modes=light;dark
X-Nemo-Counterpart=My-Theme-dark
~~~

- `X-Nemo-Style` is the label shown in the picker. Without it the folder name is used.

- `X-Nemo-Modes` is `light`, `dark`, or `light;dark`. Without it, a name ending `-dark` is taken as dark-only and anything with a `-dark` sibling as light-only.

- `X-Nemo-Counterpart` names the other half of a light/dark pair, so picking one follows the mode.

## Buuf

Buuf was on the list of sets to bundle and is not here, because it is CC BY-NC-SA - the NonCommercial term means it cannot ship inside anything, and it cannot sit in this repository either. It is worth having, though, so fetch it yourself from [gnome-look](https://www.gnome-look.org/p/1012233) or from [alfathmuqoddas/buuf-nestort](https://github.com/alfathmuqoddas/buuf-nestort), and unpack the theme folder into the icons directory above.

Bash (Linux and BSD):

~~~bash
dir="${XDG_CONFIG_HOME:-$HOME/.config}/nemo-anywhere/icons" && mkdir -p "$dir" && curl -fsSL https://github.com/alfathmuqoddas/buuf-nestort/archive/refs/heads/main.tar.gz | tar -xz -C "$dir" --strip-components=1
~~~

PowerShell (Windows):

~~~powershell
$dir = "$env:APPDATA\nemo-anywhere\icons"; $tgz = "$env:TEMP\buuf.tar.gz"; New-Item -ItemType Directory -Force $dir | Out-Null; curl.exe -fsSL https://github.com/alfathmuqoddas/buuf-nestort/archive/refs/heads/main.tar.gz -o $tgz; tar -xzf $tgz -C $dir --strip-components=1; Remove-Item $tgz
~~~

Buuf is bitmap art, so it is large and it does not scale the way the bundled sets do. That is the trade for the only hand-drawn set of its kind.
