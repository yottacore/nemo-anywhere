<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->
<div align="center">

![Made with](https://img.shields.io/badge/Made%20with-C-1f425f.svg)
![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)
[![Release](https://img.shields.io/github/v/release/yottacore/nemo-anywhere?include_prereleases&label=release)](https://github.com/yottacore/nemo-anywhere/releases)

</div>
<!--
[![!#/bin/bash](https://img.shields.io/badge/-%23!%2Fbin%2Fbash-1f425f.svg?logo=gnu-bash)](https://www.gnu.org/software/bash/)
[![made-with-python](https://img.shields.io/badge/Made%20with-Python-1f425f.svg)](https://www.python.org/)
[![made-with-rust](https://img.shields.io/badge/Made%20with-Rust-1f425f.svg)](https://www.rust-lang.org/)
![Go](https://img.shields.io/badge/Go-00ADD8?logo=go&logoColor=white)
![Made with](https://img.shields.io/badge/Made%20with-C%2B%2B-brightgreen?style=plastic)
![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)
![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
![Lifecycle: Alpha](https://img.shields.io/badge/Lifecycle-Alpha-orange)
![Lifecycle: Beta](https://img.shields.io/badge/Lifecycle-Beta-yellow)
![Lifecycle: RC](https://img.shields.io/badge/Lifecycle-RC-blue)
![Lifecycle: Stable](https://img.shields.io/badge/Lifecycle-Stable-brightgreen)
![Coverage](https://img.shields.io/badge/Coverage-75%25-yellow)
![Status: Passing](https://img.shields.io/badge/Status-Passing-brightgreen)
-->

<!-- TOC ignore:true -->
# nemo-anywhere

<table style="border: none; border-collapse: collapse;">
	<tr style="border: none; border-collapse: collapse;">
		<td style="border: none; border-collapse: collapse;"><img src="assets/logo.png" alt="Logo" width="160"/></td>
		<td style="border: none;">This is the legendary Nemo file manager, ported to Windows, with BSD and macOS to follow - as well as to Linux without the Cinnamon dependency. It also has several major new convenience features.</td>
	</tr>
</table>

<div align="center">

![Demo](assets/demo.gif)

<!-- [![Video](https://img.youtube.com/vi/VIDEO_ID/maxresdefault.jpg)](https://www.youtube.com/watch?v=VIDEO_ID) -->

</div>

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Why](#why)
- [Existing features](#existing-features)
- [What this fork adds or improves](#what-this-fork-adds-or-improves)
- [Status](#status)
- [Icon themes](#icon-themes)
	- [Add your own theme](#add-your-own-theme)
- [Installation](#installation)
- [Longer-term roadmap](#longer-term-roadmap)
- [Copyright and license](#copyright-and-license)

<!-- /TOC -->

## Why

Nemo is one of the best file managers going. It's fast, sane and powerful.

Unfortunately, it's tightly integrated into the Linux Cinnamon desktop.

This project removes the Cinnamon (and even Linux) dependency:

- It takes Nemo as-is, from the source.

- It removes every assumption that says "you depend on Cinnamon", "you're managing the desktop", or even "you are running on Linux".

- It removes the heavy desktop integration. Your existing manager is untouched. (Which can even be the original Nemo, since they don't conflict.)

- It's shippable everywhere, or at least to every desktop OS.

That means, in order:

- **Linux**:

	- It runs standalone on any desktop.

	- It has no big stack of Cinnamon dependencies. (But run on the Cinnamon desktop just fine.)

	- It doesn't try to compete with existing desktop managers for control of desktop rendering.

- **Windows**: It's a real native build, not a compatibility shim.

- **BSD**: Coming soon.

- **macOS**: App store process underway.

There is one codebase. "For Windows" and friends are just labels on builds, not separate projects.

It is opinionated. It manages files and folders. It does more of the file management you need, built in, on every platform, so nothing depends on third-party programs, plugins or extensions that only exist on one of them.

This is an independent, unofficial hard fork of [linuxmint/nemo](https://github.com/linuxmint/nemo), taken at the 6.6.4 release. It is not affiliated with, endorsed by, or supported by Linux Mint, the Cinnamon team, or GNOME.

Report issues here, never upstream. Provenance details live in [fork.md](fork.md).

## Existing features

These are the features that make Nemo worth porting:

- Navigation is fast and no-nonsense. Back, forward, up and refresh are there, with breadcrumbs or a path box, whichever you pick.

- File operations show real progress. You can see what is happening, and how far along it is.

- Folder contents merging is intuitive. No more accidental clobbering.

- Open in terminal is built in.

- It has proper bookmarks.

- Copies use near-instant and near-zero-size CoW copies automatically, if the underlying filesystem allows it.

## What this fork adds or improves

- **Cinnamon Desktop isn't required**.

- **Linux isn't required**. Windows is a real native build, not a compatibility layer. (BSD coming soon.) You can install it from here, and Windows Store and Apple Store downloads are underway.

- On Windows it is one executable. The whole runtime is packed inside it, so there is nothing to install and nothing to keep in step. Copy it where you like and run it.

	- It's the same idea as an AppImage or a Flatpak, without the runtime or the sandbox.

	- On Linux it stays a small folder that uses the GTK3 your distro already has, because that is what a Linux user expects and it keeps the download tiny.

- Windows features:

	- A conscious decision was made to avoid Explorer integration at any cost, since we can't let an Explorer crash bring Nemo Anywhere down with it. Nemo Anywhere only links to shell32.dll in its own process space, which provides most native Windows functionality.

	- The native Windows Recycle Bin can be browsed, restored from, and emptied.

	- Alt+Enter opens the same Properties sheet Explorer shows, tabs from other programs included.

	- `.lnk` shortcuts can be made, edited and opened the same way Explorer does. Shortcuts, symlinks and junctions each get their own overlay, so they are easy to tell apart. (You can make hardlinks too.)

	- Each fixed drive sits in the sidebar with a usage bar.

	- Dot files and files with the hidden attribute each have a switch. Ctrl+H flips both. (They can also be independently hidden.)

	- Includes right-click options like "Open in Windows Terminal", "Open as Administrator", "Open with Explorer", and "Copy path" with either kind of slash.

	- Light or dark follows the Windows setting. Four icon sets are drawn to match XP, 7, 10 and 11.

	- Per-monitor DPI-aware.

	- File associations are read from the registry and never written to it. "Set as default" keeps its choice in the settings file.

- Every instance gets its own process. If one crashes for some reason, the rest keep going.

	- A crash writes a report beside the settings file.

- Archive handling is integrated, and more advanced. No more third-party GUI application dependencies that don't feel integrated, and don't support the archive format's best options.

	- Create and extract zip, tar and 7z natively. Features like split volumes, 7z passwords, and all rar features are automatically enabled if the native 7z and/or rar CLI programs are installed.

	- Right-click Extract reads most archive types, with real progress and a question when a file already exists. An archive can't write outside the folder it is unpacked into.

	- Compress can delete the original contents afterward (off by default). It reads the finished archive back first and checks every file and size; only then sends the originals to the trash.

- Comprehensive link creation options:

	- "Make link" asks what to make: a symlink, a hardlink, or a Windows shortcut.

	- Reading and creating Windows `.lnk` shortcuts work on Linux and macOS too. (But unfortunately, on those platforms, the Windows shortcut format only works in Nemo Anywhere.) The nuances of behavioral differences between symlinks and shortcuts work on Linux and macOS just as they do on Windows.

	- On Windows, directory junctions are also offered as a (generally superior) option.

	- For symlinks, you can choose relative or absolute path.

		- Note: Shortcuts, by Microsoft definition, handily contain both relative and absolute paths by definition, plus a portable path such as `%USERPROFILE%\Documents`. This helps the shortcut format overcome the limitations of relative vs absolute symbolic links.

- Windows environment variables and backslashes work on Linux and macOS. Linux and macOS variables and forward slashes work on Windows.

- The thumbnail-caching engine has been modernized:

	- A folder of pictures has all its thumbnails generated as soon as it opens - top down, to minimize I/O contention and maximize total speed.

	- It no longer gets confused by showing the wrong thumbnail for renamed files that have the same filename and size but different content.

	- Photoshop, HEIC, and most camera raw files get thumbnails too. If ImageMagick is installed, thumbnails also work for JPEG 2000, EXR, and a few dozen more.

	- The thumbnail cache is cleaned up once in a while in the background, rather than growing forever.

	- Image thumbnails get their own (larger) thumbnail size, and folders with mostly images auto-switch to large thumbnail view (which can be disabled), while regular folders maintain their normal view.

- List view columns sizing is no longer a mess. They intelligently size themselves by priority to what is in them, and the space given. A scrollbar appears before columns get squeezed too narrow to read.

- There's a handy new "extension" column, in addition to "file type".

- Moves and deletes are safer:

	- A drag that moves files, alerts you to what it's about to do first. (One of the easiest ways to lose track of a file in any graphical file manager is an accidental drag to some folder you didn't see it drop in. A copy goes through without a word unless you opt-in to that check also. A dropped link opens a "Make link" dialog, so you can pick what kind (or cancel).

	- A large delete, or one generated with no user input, prompts even with confirmation turned off.

	- Trash and delete operations record what they did: how many items, which folder, the first name in the batch, and what set it off.

	- Links are never followed on a delete or move. Only the link itself goes. Copying a link asks whether to keep it as a link, or copy what it points to.

	- Only a window can trash, delete or move files. Another program on the session bus can't (which Nemo's old desktop interface allowed).

- Alternate rows can be shaded, off by default. Selection and hover still show through it.

- Places and the folder tree can now both open at the same time. The tree lists folders only, and a folder with nothing under it gets no expander.

- Tabs are as wide as their title. With full paths on, a path too long to fit is shortened a step at a time, and the name of the folder itself is always kept. The active tab shows as much of the path as possible.

- Every folder follows one set of view defaults, unless per-folder settings are turned on.

- Going to a network address, such as `smb://`, `sftp://` or a `\\server\share` path, shows a spinner and "Connecting to server..." in the path bar until the server answers or gives up. Meanwhile that window takes no other input, so nothing done in it is lost when the share opens, and it slowly fades between grayed out and normal so it's clear it is busy and not frozen. With animations turned off it stays grayed instead. Escape stops the attempt, and other windows stay usable.

- Window size and zoom are remembered for each monitor, by its resolution and DPI. A window dragged to another monitor takes the size and zoom last used there. (Coming soon.)

- Search has several improvements:

	- Search results can be grouped under the folder they came from. A flat list of thirty files all called `notes.txt` tells you nothing; a row per folder with the matches under it tells you where to look. It's one toggle in the search bar, and the results are the same either way.

	- Search results keep the columns you pick, the order you drag them into, and how they are sorted. By default they show Name, Ext, Size, Date modified and Location. They open in the list view, whatever view the folder is in, and switching views there leaves the folder's view alone.

	- Content search reads Word, Excel, PowerPoint, OpenDocument, and EPUB files by itself. It needs no helper scripts and no office suite.

	- On Windows, the search index can be used for faster searches. It is off by default, since it only knows the folders it was told to watch.

	- Escape leaves search and goes back to the folder, from the search box or from the results. In the results a selection is cleared first, so it takes a second Escape.

- F2 on several files at once opens a bulk renamer, and the setting comes filled in with one that's installed: Thunar's or KRename on Linux, ReNamer or Ant Renamer on Windows. Any program that takes a list of files will do.

- Settings live in one plain text file you can read and edit. There's no registry, no dconf and no compiled schema. Editing it by hand does the same thing as changing the setting in the dialog.

- Copy, paste and drag work with the platform's own file manager, in both directions.

- Features that a platform can't do is either hidden or visually disabled.

- Releases can be checked. Every download is published with checksums, and the Linux builds can be rebuilt from their commit to the same bytes. That starts with the release after 1.0.0-beta2.

- There are dozens of "minor papercut" fixes and "quality-of-life" improvements.

## Status

Currently beta - for options tuning and visual polish. But the guts and stability are safe, solid, and ready for safe everyday use.

Every build goes through static analysis, the full regression suite, and the exhaustive checks that guard against losing files. A full pipeline run adds fuzzing and a performance profile.

Current limitations:

- For now, every trash and delete asks first with a detailed dialog noting exactly what is about to go, where, and why - which is wired into all possible trash/delete/move code paths. It is an extra safety net for all prereleases whether needed or not.

	- It can be safely turned off in Settings.

- The Windows exe isn't code-signed yet, so Windows may warn the first time it runs.

- Settings from a pre-1.0 install don't carry over.

- macOS and BSD aren't built yet.

Details are in these docs:

- The development backlog: [project/backlog.md](project/backlog.md).

- Design: [project/design.md](project/design.md).

- Code style: [project/style-guide_code.md](project/style-guide_code.md).

- UX style guide: [project/style-guide_ux.md](project/style-guide_ux.md).

## Icon themes

24 icon sets, light and dark, are built into the Windows build, so there's nothing to download. Linux and BSD builds use the icon themes the desktop already has. Pick one in **Settings -> Appearance**; the Style picker moves the Icons picker to match, so a Windows 11 window frame doesn't come with macOS icons unless you ask for it.

![Icon themes](assets/icon-gallery.png)

Each set is shown twice, on a light background and a dark one, because half of them are drawn for a dark desktop. The four Windows looks - XP, 7, 10 and 11 - are drawn in-house: no cleanly-licensed set of any of them exists, and every set that circulates draws blue folders, which Windows has never had. The rest are trimmed to the roughly 180 names a file manager actually asks for, which is what holds a set to a few hundred kilobytes; anything not drawn falls through to Adwaita.

Provenance and license for every vendored set is in [vendor/README.md](vendor/README.md).

### Add your own theme

On Linux and BSD, Nemo Anywhere inherits whatever widget and icon theme is set at the desktop level (as one would expect).

But on Windows, you can drop a theme folder into the icons directory beside your settings file, and it appears in the picker on the next launch - `~/.config/nemo-anywhere/icons/` on Linux and BSD, `%APPDATA%\nemo-anywhere\icons\` on Windows, `~/Library/Application Support/nemo-anywhere/icons/` on macOS. Widget themes work the same way in `themes/` beside it. Both folders are created empty on first run.

[filesystem/README.md](filesystem/README.md) covers the layout, the two optional `index.theme` keys that tell the picker which modes a theme suits, and one-line fetch commands for Buuf - a set worth having that can't be bundled, because its NonCommercial license rules it out of anything distributed.

## Installation

Packages, installers, the one-line install scripts, and the plain tarball are in [install.md](install.md). To build it and work on it, see [building.md](building.md).

## Longer-term roadmap

For maximum cross-platform portability, Nemo Anywhere needs to move off of not just GTK+ v3, but GTK+ period. While GTK+ v3 works and looks nice, it's no longer actively developed, is basically stuck with C, and is not as reliable on Windows and macOS as, say, Qt. That's what the sister project [Captain Nemo](https://github.com/t00mietum/captain-nemo) is for, once this project reaches a few stable rounds.

## Copyright and license

The [original Nemo](https://github.com/linuxmint/nemo) is the work of the Linux Mint project and [many contributors](https://github.com/linuxmint/nemo/graphs/contributors), and is itself a hard fork from 2012 of [GNOME Files aka Nautilus](https://github.com/GNOME/nautilus).

This repository, although also a hard fork, retains all original copyright and license notices; see `license.txt` (originally 'COPYING'), `license-lib.txt` (originally 'COPYING.LIB'), `license-docs.txt` (originally 'COPYING-DOCS'), and `license-for-extensions.txt` (originally 'COPYING.EXTENSIONS').

The sound files the demo recorder mixes into its video are third-party, under their own terms, and aren't part of the application. Sources and licenses are in `cicd/utility/demo-video/sounds/LICENSES.txt`.

The design docs in `project/design_docs/` are under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) instead, as each one says at the end.

> Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)<br>
> Upstream code Copyrights © [Nemo authors](https://github.com/linuxmint/nemo/graphs/contributors).<br />
> Licensed under [GNU GPL v2](https://opensource.org/license/GPL-2.0) license. No warranty.
