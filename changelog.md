# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!--
## vNEXT - DATE

### Notes

### Added

### Changed

### Removed

### Other work
-->

## vNEXT - unreleased

### Added

- Every move to the trash and every delete is written to the log with how many items, which folder, and the key, click or drop that asked for it. One that nothing asked for - another program, another copy of the app, a timer - always asks first, and says so. So does one of twenty items or more (`confirm-many-items` in the settings file), even with confirmation turned off.

- Each new window is its own process, so a crash in one leaves the others running, and two versions can be open side by side. A launch from the command line is always a fresh process too, never handed to a copy already running. `--quit` still takes every copy down. Off by a setting under Behavior, which puts new windows back inside one process.

- `--select` opens the folder around each item given, with the item selected. Programs that ask for "show in folder" go through it.

- Search results can be grouped under the folder that holds them instead of shown as one flat list, with a toggle next to the one for recursing into subfolders. Grouped results drop the Location column and switching either way happens without running the search again.

- A drop that moves files asks first, naming the files and the folder they are headed for. Two settings under Behavior: moves ask by default, copies do not. The Trash keeps its own separate question rather than asking twice.

- Content search ("Containing:") reads Word, Excel and PowerPoint documents in both their old and new formats, OpenDocument files and EPUB books, on every platform. The converters are built in, so nothing else needs installing.

- Windows `.lnk` shortcuts work on Linux. One to a folder changes to that folder, and one to a file opens the file, started in the shortcut's "Start in" folder or else the file's own. Each shows its target's icon. The Windows path is matched to a drive by its volume serial, or to a share by server and name, whether the kernel or gvfs mounted it. A share that is not mounted opens as `smb://` where gvfs can. When nothing matches, a message names the path instead of a guess.

- On Windows, a shortcut is drawn with the icon the shell would give it, its target's, at every view size, and without Explorer having to be running.

- On Windows, Properties on a shortcut shows its target, arguments, start-in folder and comment, and each can be edited in place, the way a `.desktop` launcher can on Linux. A file dropped on the target or start-in field fills it in.

- On Windows, "Set as default" in Open With works. The choice is kept in the settings file, as a command line with `%1` for the file, and is used ahead of the registry; the registry itself is only ever read, the way Explorer reads it. Reset takes the choice away again.

- On Windows, a switch in Preferences answers searches from the Windows Search index for folders it covers. Off by default. Folders outside the index, and content searches by pattern or by case, are still searched directly.

- A `--reset` flag that clears the settings and the bookmarks and puts everything back to defaults. It will not run while the app is open.

- On Windows, a first start fills the bookmark list with the drive, Desktop, Documents, Downloads, Pictures, Videos and AppData. Anything left behind by a config copied from a Linux machine is dropped at the same time.

- Typed locations understand `~` for the home folder and environment variables, written either `%NAME%` or `$NAME`, on any platform. A folder whose name really contains one of those characters still opens as itself.

- An Appearance page in Preferences: Light, Dark, or follow the system, plus a style and an icon theme to draw with. Both lists only offer themes drawn for the mode you are in, and picking one half of a light/dark pair follows the pair when the mode changes.

- The Windows build carries a set of themes so the app looks like something out of the box: Windows 11, Windows 10, Windows 7, Windows XP and macOS window styles, and twenty-four icon sets, including Windows XP, 7, 10 and 11 sets drawn for this project. Linux keeps using whatever the desktop provides.

- Themes can be dropped in on any platform. Put a GTK theme folder in `themes`, or an icon theme in `icons`, beside the settings file and it shows up in the lists.

- A small panel while Windows starts the app, saying what it is doing, so a launch that takes a moment does not look like nothing happened. It goes away the instant the real window has drawn.

- Two more owner columns for the list view: Owner name, the owner's display name, and Owner - name, which shows both. On Windows the name is the local account's full name; a domain account has none.

- A checkbox under Behavior for whether a rename starts with the extension selected too. On by default, as before.

- A checkbox under Behavior to keep the tab bar showing with only one tab open. Off by default.

- Every other row in the list view can be shaded, from the Display page. Off by default. The shade comes from the theme's `nemo_row_shading` color when it has one, or a faint tint of the text color, and `row-shading-color` in the settings file overrides both.

- "Edit link..." changes where a symlink or junction points, and its name. On a Windows shortcut it shows the absolute, relative and portable paths, any of which can be changed or left empty to drop it. The shortcut's arguments, Start in folder and icon are kept. Only shown when one link is selected.

### Changed

- Keyboard shortcuts are saved in `accels` beside the settings file. They used to go to upstream Nemo's `~/.gnome2/accels/nemo`, and only where that folder was already there, so never on Windows. Custom shortcuts in that file are read once on the first start, and it is no longer written. `--reset` clears them too.

- The row under the pointer, in the list and both sidebars, is tinted with a faint wash of the theme's selection color instead of gray, so it no longer looks like a shaded row. It is kept fainter than a shaded row and well short of a selected one, light or dark. A theme whose selection is gray gets a soft blue. `row-hover-color` in the settings file, or a `nemo_row_hover` color in the theme, overrides it; `transparent` turns it off.

- A Windows shortcut to a folder sorts with the folders when folders come first.

- Ctrl+Shift+T opens a new tab when no folder is selected. With folders selected it opens them in new tabs, as before, and a selected file no longer opens in its program.

- "Make symlink" is now "Make link...", and asks what to make. Folders get a junction or a symlink on Windows, files a symlink or a hardlink, and either can be a Windows `.lnk` shortcut, called a Shortcut there. A symlink's path can be relative or absolute. A hardlink asks once more first, saying what can go wrong with one. Every open starts from the defaults: a junction for folders on Windows, else a symlink, and an absolute path. On Windows without the symlink privilege the item stays on, since a junction, a hardlink and a shortcut need none. A shortcut can hold an absolute path, a relative one and a portable one with a Windows variable such as `%USERPROFILE%` in it, all three to start with, and is followed by the first that works. Windows follows a shortcut made on another platform through its portable path, and a target on a Windows share keeps its `\\server\share` path there. Nemo Anywhere follows any of them, on any platform. A drop that makes links opens the same dialog, in place of the drop question. A link made beside its original is named for its kind, such as "photo - symlink.jpg" or "photo.jpg - shortcut.lnk", and one made in another folder keeps the original's name.

- When a copy finds links, the choices now say what happens to each: "Copy link as-is" or "Copy contents", or "Copy as a junction" and "Copy as a symlink" where one kind can become the other. A move says "Move" in place of "Copy".

- The settings file is read with SHCL 3. A backslash in an unquoted value is now just a backslash, so a Windows path can be typed as it is, and escapes only work inside double quotes. A settings file from an earlier beta that holds a network path may read it wrong; delete it and start again. The file now ends with a short block naming its format, so a later version knows what it is reading.

- The settings file now ends with a commented list of everything you have not set, each line carrying the value used instead and, where the name is not obvious, a short note. Uncomment a line to change it. Sizes, positions and other things the app remembers for itself are left off the list.

- The thumbnail cache is cleaned up every few hours, at a quiet moment, rather than growing forever: a thumbnail whose file is gone goes first, then anything not drawn for longer than the age allowed, then the least recently drawn until the rest fit in the size allowed. Both limits are on the Preview page and either can be turned off. The shared freedesktop thumbnail folder is no longer touched.

- Camera raw files get thumbnails, from the preview the camera stores in each one: DNG, CR2, CR3, NEF, ARW, RAF, RW2, ORF, PEF, SRW and most others built on TIFF. Nothing else needs installing, and the sensor data itself is never read.

- With ImageMagick installed, JPEG 2000, HEIC, AVIF, EXR and other picture formats get thumbnails too. It is looked for, never required, and run only on a fixed list of picture formats.

- The list view no longer shrinks a column past what it shows. Name keeps the width that shows all but the widest tenth of the names in the folder (a setting under list-view), a type or owner column the width that shows most of its distinct values, and a date or a size is never cut at all. Type gives a little further, down to twice the Ext column, and past that the view scrolls sideways.

- Search results give the Name and Location columns only the width their contents need, leaving the rest of the row empty. Where the two do not both fit they shrink in proportion to what each asked for, and neither ends more than twice the width of the other. Dragging either column still pins a split of the whole row.

- `--existing-window` opens the URIs as tabs in one new window. There is no window of ours left to join, since every launch is its own process.

- Properties on a folder shows folders and files on lines of their own, each with how many more are hidden, in place of one Contents count. The Help button is gone from the dialog.

- On Windows, bookmarks are kept beside the settings in the roaming profile, so they follow the settings between machines. A list from an earlier version is picked up from the old place on first start.

- Search helpers: the `Priority` field is honored, and one helper runs per file rather than every helper that claims the type.

- The bundled icon themes are trimmed to the icons a file manager actually asks for. The Windows 11 set went from 1.8 MB to around 300 KB; anything not shipped falls back the way icon themes are meant to.

- Settings now live where each platform expects them: `%APPDATA%\nemo-anywhere` on Windows, `~/Library/Application Support/nemo-anywhere` on macOS, `~/.config/nemo-anywhere` on Linux and BSD as before. An existing settings folder is moved to the new place on first run, so nothing is lost. Drop-in themes are unaffected.

- The single-file Windows build starts far faster. Nearly all of its startup went on unpacking the couple of thousand loose theme and icon files it carried; those now live inside the executable itself. Nothing about how themes are chosen or dropped in changes.

- The window appears at the size and place you left it as soon as it exists, rather than waiting for the first folder to finish loading.

- The Owner column shows the user name alone. The name with the display name after it moved to the new Owner - name column.

- The window title puts the folder first and the program name after it, as `Documents - Nemo Anywhere`. A name or path with a space in it is put in double quotes.

### Fixed

- An owner whose account has an empty display name showed as the user name followed by a stray " - ".

- The bottom scrollbar flashed at every step of a resize, could stay after one when nothing needed it, and now and then strobed along with the vertical one.

- On Windows, a first start with a fresh roaming profile carried the local data folder (actions, scripts) off into the settings folder, mistaking it for settings left by an older version. Only a folder that holds a settings file is moved now.

- On Windows the window opened behind whatever you were already looking at, so a launch could look like nothing had happened until you noticed the taskbar button. It comes to the front now.

- With the tab bar set to show for a single tab, closing a tab down to one hid it anyway.

- When another program removed files one after another, the status bar could go on counting a few that were already gone.

## v1.0.0-beta2 - 2026-08-04

### Notes

- Settings do not carry over from beta1. Configuration moved out of GSettings, and nothing can read the old store once its schema is gone, so the first run after upgrading starts from defaults.

### Added

- Linux packages: a `.deb`, an `.rpm`, and a portable `.tar.gz`. They are built against glibc 2.35, so they run on Ubuntu 22.04+, Debian 12+, Mint 21+, Fedora 36+, and anything newer.

- A Windows `.zip` beside the single executable. This is the archive the PowerShell installer looks for, so the one-liner install works now.

### Changed

- Settings live in one readable file - `~/.config/nemo-anywhere/settings.shcl`, or `%LOCALAPPDATA%\nemo-anywhere\settings.shcl` on Windows. Only the values you changed are written, each with a short comment saying what it is, and editing the file while the app is running applies straight away.

### Removed

- GSettings and dconf, along with the compiled schema that used to be installed system-wide.

### Other work

- The local build pipeline gained packaging, profiling, and a remote-sync step.

## v1.0.0-beta1 - 2026-08-04

### Notes

- First public beta. Windows shipped as a single self-contained executable; Linux was source-only at this point.
