<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
# filesystem

A mirror of where things go on disk, so a folder in here can be copied straight to the matching place on a machine. Nothing in here is compiled, installed, or packaged. The application reads it at startup, so it doesn't need a build step or any restart beyond the next launch.

The drop-in folders so far are for icon and widget themes:

~~~text
filesystem/.config/nemo-anywhere/icons/
filesystem/.config/nemo-anywhere/themes/
~~~

## Where that folder is

The config directory follows the platform, so `.config` above is the Linux name for it. The icons folder is beside `settings.shcl`, wherever that is:

| Platform   | Icon themes go in
| :---       | :---
| Linux, BSD | `~/.config/nemo-anywhere/icons/`
| Windows    | `%APPDATA%\nemo-anywhere\icons\`
| macOS      | `~/Library/Application Support/nemo-anywhere/icons/`

Widget themes work the same way, in `themes/` beside it. The application creates both folders empty on first run, so the place to put something is discoverable without reading this.

One icon theme is one folder with an `index.theme` in it. That's the ordinary freedesktop layout that any GTK icon theme comes in. Drop it in whole and it appears in **Settings -> Appearance -> Icons** next time the application starts. A drop-in of the same name as a bundled set wins, which is how one can be replaced without rebuilding anything.

## Optional lines in index.theme

The picker only offers themes drawn for the light or dark mode in use, and it can swap between both halves of a pair when the mode changes. It reads 2 keys of ours from `index.theme` for that, and guesses from the folder name when they're missing:

~~~ini
X-Nemo-Style=Circles Blue
X-Nemo-Modes=light;dark
X-Nemo-Counterpart=My-Theme-dark
~~~

- `X-Nemo-Style` is the label shown in the picker. Without it the folder name is used.

- `X-Nemo-Modes` is `light`, `dark`, or `light;dark`. Without it, a name ending `-dark` is taken as dark-only and anything with a `-dark` sibling as light-only.

- `X-Nemo-Counterpart` names the other half of a light/dark pair, so picking one follows the mode.

## Buuf

Buuf was on the list of sets to bundle and isn't here, because it's CC BY-NC-SA. The NonCommercial term means it can't be bundled with anything, or kept in this repository either. It's worth having, so fetch it yourself from [gnome-look](https://www.gnome-look.org/p/1012233) or from [alfathmuqoddas/buuf-nestort](https://github.com/alfathmuqoddas/buuf-nestort), and extract the theme folder into the icons directory above.

Bash (Linux and BSD):

~~~bash
dir="${XDG_CONFIG_HOME:-$HOME/.config}/nemo-anywhere/icons" && mkdir -p "$dir" && curl -fsSL https://github.com/alfathmuqoddas/buuf-nestort/archive/refs/heads/main.tar.gz | tar -xz -C "$dir" --strip-components=1
~~~

PowerShell (Windows):

~~~powershell
$dir = "$env:APPDATA\nemo-anywhere\icons"; $tgz = "$env:TEMP\buuf.tar.gz"; New-Item -ItemType Directory -Force $dir | Out-Null; curl.exe -fsSL https://github.com/alfathmuqoddas/buuf-nestort/archive/refs/heads/main.tar.gz -o $tgz; tar -xzf $tgz -C $dir --strip-components=1; Remove-Item $tgz
~~~

Buuf is bitmap art, so it's large and doesn't scale the way the bundled sets do.
