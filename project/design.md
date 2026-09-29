<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere design

What the project is for, and the decisions behind it. Companion to [backlog.md](backlog.md), which tracks the work itself.

Status: kept current as decisions change, rather than written once. Last read through on 2026-09-19, at 1.0.0-beta2. The git log of this file is its revision history. Where a decision was reversed, the text says so at the point it changed.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Overview](#overview)
	- [What and why](#what-and-why)
	- [Goals](#goals)
	- [Fork decisions](#fork-decisions)
- [Architecture](#architecture)
	- [Software stack](#software-stack)
	- [Code layout](#code-layout)
	- [Data flow](#data-flow)
	- [Execution flow](#execution-flow)
	- [One process per window](#one-process-per-window)
- [Features](#features)
	- [Configuration and persistence](#configuration-and-persistence)
		- [Application settings](#application-settings)
		- [Bookmarks](#bookmarks)
		- [Per-folder view state](#per-folder-view-state)
		- [File cache](#file-cache)
	- [File operations](#file-operations)
		- [Trash and delete](#trash-and-delete)
		- [Links](#links)
		- [Archives](#archives)
	- [Search](#search)
	- [User interface](#user-interface)
		- [Window, tabs and side panes](#window-tabs-and-side-panes)
		- [Views and list columns](#views-and-list-columns)
		- [Icon sizes](#icon-sizes)
		- [List view column widths](#list-view-column-widths)
		- [Labels and dialogs](#labels-and-dialogs)
		- [Hidden files and shortcuts](#hidden-files-and-shortcuts)
		- [Scaling and startup](#scaling-and-startup)
	- [Appearance and themes](#appearance-and-themes)
	- [Platform integration](#platform-integration)
		- [Paths and desktop settings](#paths-and-desktop-settings)
		- [On Linux](#on-linux)
		- [On Windows](#on-windows)
- [Quality](#quality)
	- [Speed, memory and size](#speed-memory-and-size)
	- [Security](#security)
	- [When it crashes](#when-it-crashes)
	- [What it logs](#what-it-logs)
	- [Testing](#testing)
- [Building](#building)
	- [Building on Linux](#building-on-linux)
	- [Building on Windows](#building-on-windows)
- [Delivery](#delivery)
	- [Branches and the merge gate](#branches-and-the-merge-gate)
	- [Versions and build numbers](#versions-and-build-numbers)
	- [The pipeline](#the-pipeline)
	- [Reproducible builds](#reproducible-builds)
	- [Release artifacts and packaging](#release-artifacts-and-packaging)
	- [Installing](#installing)
	- [Dogfooding](#dogfooding)
- [Open questions](#open-questions)

<!-- /TOC -->

## Overview

### What and why

This is a hard fork of linuxmint/nemo at its 6.6.4 release, decoupled from Cinnamon and from Linux-desktop assumptions so it runs standalone anywhere. (And it is already far ahead of Nemo 6.6.4 in terms of bug fixes and feature improvements, and new features.) Independent and divergent: no upstream contribution, no downstream sync. GPL-2.0-only.

Targets in order: Linux on any desktop or none, then Windows, then BSD, then macOS. One codebase; per-platform builds are labels, not separate projects.

Windows is the first not-Linux target because it forces the cleanest separation. Nothing Linux-specific can be assumed there, so the couplings show up as build errors rather than as things that quietly still work. A de-Cinnamon Linux build falls out of the same work.

### Goals

What the project is trying to be, roughly in priority order:

- Belong to no desktop. Nothing in the program assumes Cinnamon, GNOME, or even Linux, and it never draws or owns the desktop. It can sit beside whatever already does, original Nemo included.

- Run on any desktop OS, from one codebase. Linux on any desktop or none, then Windows, then BSD, then macOS.
	- "For Windows" are labels on builds, not separate projects.

- Keep what makes Nemo worth porting: Fast navigation, tree folder view in list mode, sane folder merging, proper bookmarks, useful and simple settings, and an extension API that still works.

- Be portable in the copy-it-and-run sense. On Windows that is one executable with the runtime inside it. On Linux it is a small folder using the GTK the distro already has.
	- If not obtained via provided installers or packages: Nothing required to be installed, nothing registered, no repository to add. Installers and distro packages exist for people who want them, but nothing depends on them.

- Make it hard to lose a file by accident. This is where the fork is willing to be less convenient than its ancestors, e.g.:
	- A drag that moves files says what it is about to do, and waits. (Because accidental mouse drag-and-drops - especially large ones across filesystems - are the bane of GUI file managers.)
	- Trash and delete jobs each write a line saying what was taken and what asked for it.
	- Home, the folders above it and mounted drives are never deleted, and no one job may take most of a home folder.
	- A trash or delete with no user input driving it, and/or one over a size threshold, asks first no matter what the preferences say.

- Keep configuration in plain sight. One text file, readable and editable by hand, with no registry keys, no dconf, and no compiled schema to install. Hand-editing it behaves the same as changing the setting in the dialog.

- Fit each platform natively instead of pretending to be its file manager. Drive letters, the Recycle Bin, shortcuts, UNC paths and file associations are all done the way that platform does them.
	- Read the system's settings, don't rewrite them. File associations come out of the registry; the app's own overrides stay in the app's own config.

- Minimize dependencies. On Windows, for example, minimize dependence on Explorer.

- Handle natively in own code (and or reliance on optionally-installed CLI tools), far more robustly than Nemo's reliance on external tools - and lack of really good tools:
	- Archive/extract.
	- Find.
	- Robust rename that surpasses Thunar Renamer and Directory Opus in functionality, simplicity, and repeatability (e.g. saveable templates). For media types, be at least as robust as "CamHauler" (formerly "Rapid Photo Downloader Pro" and may get yet another rename).
	- Smart duplicate file and directory finder and handler.

- Never follow links of any kind for deletes or moves. The copy portion presents user with options for how to handle links.

- Be tolerant of crashes. Each instance of Nemo Anywhere gets its own process (not just thread).

- Interoperate with OS-native file managers. (E.g. bidirectional copy/paste and drag-n-drop.)

- Hide or gray out what a platform cannot do, rather than failing at it. A missing runtime service should cost a menu entry, not a crash.

- Look presentable on a bare system. Icon sets and window styles are inside the program (for non-Linux OSes), so a fresh copy has no missing art and nothing to download.

- Start fast and stay small. A file manager gets launched dozens of times a day, and a slow one is noticed every time.

- Ship builds that can be checked. Reproducible from the commit they were built at, published with checksums, and cut by the same pipeline that runs on a developer's own machine.

- Stay "Nemo". Same lineage, same licenses (GPL-2.0-only, and LGPL for the extension library), per-file attribution intact. Independent and divergent: nothing goes upstream and nothing is pulled back down.

- Deliberately out of scope: drawing the desktop, autorun of any kind on any platform, and migrating settings from a pre-1.0 install.

### Fork decisions

- Baseline is the 6.6.4 release tag, not master, so the starting point is known-good rather than a moving target. It was imported as a clean detached commit with no upstream history: lineage and attribution ride in [fork.md](../fork.md) and in the per-file copyright headers instead of in git ancestry.

- The name keeps "nemo" for discoverability and honest lineage, and adds "anywhere" for the portability and the belongs-to-no-desktop identity. Nemo Anywhere and the original Nemo can be installed and run on the same machine at once without conflicting, which is deliberate: separate config directory, separate settings file, app-private per-file keys.

- Version numbers start at 1.0.0 and are the fork's own, unrelated to the 6.6.4 code baseline.

- Desktop management is removed, not made optional. A file manager is not a desktop shell, and drawing the root desktop is where the deepest Cinnamon coupling lived: the `nemo-desktop` binary, the `org.Cinnamon` proxy, the per-monitor `x-nemo-desktop://` directory model. Cutting it outright was the cleanest first step and it benefits every target. Kept, despite the names: the `.desktop` launcher-file properties editor and the multi-monitor geometry helper, both ordinary file-manager features.

- xapp and cinnamon-desktop are reimplemented rather than compiled out, so the standalone build keeps favorites, thumbnails, tray feedback and the icon chooser instead of quietly losing them.
	- Favorites and the thumbnailer were adapted from their upstream implementations into `libnemo-private`, with provenance and licenses noted per file.
	- The tray icon uses GTK's own status icon. It is deprecated upstream but is still the only portable tray mechanism. Window taskbar progress was dropped: it is a Mint-only window-manager protocol with no portable equivalent.
	- The icon chooser is a file picker with an image preview. Browsing theme icons by name went with the old widget, which is an accepted simplification.

- The first runnable milestone was scoped to browse, copy, move, trash and delete. Everything else came after that worked.

## Architecture

### Software stack

- Language: C, built with meson and ninja. No C++ and no second language runtime.

- Toolkit: GTK 3, with GLib, GObject and GIO. GTK 3 rather than 4 because the fork inherits a large GTK 3 codebase and GTK 3 still has the better Windows story. The deprecated pieces still in use (the status icon, a few stock dialogs) are isolated and marked.

- Filesystem access: GIO everywhere, with native backends filling the gaps that have no portable answer - the Windows Recycle Bin, Windows network browsing, and Windows shell shortcuts.

- Other libraries: libarchive for reading and writing archives, libexif, libgsf and exempi for file property extraction, json-glib for the metadata store, SQLite for the file cache, and two vendored pieces: one header for the settings format and the blake3 hash. Deliberately absent: xapp, cinnamon-desktop, and GSettings for the app's own settings.

- Optional at runtime: gvfs on Linux, for network shares, trash and remote mounts. Where it is absent the affected entries hide themselves rather than fail.

Longer term the toolkit itself is the constraint. GTK 3 is no longer developed, keeps the project in C, and is weaker on Windows and macOS than the alternatives. Moving off it is a separate project ([Captain Nemo](https://github.com/t00mietum/captain-nemo)), not something this one attempts.

### Code layout

The repo root holds docs and licenses and little else. The buildable project is wrapped under `source/` with its internal GTK/meson layout intact, so meson is pointed there.

- `source/` - the meson entry point and all C sources.

- `project/` - this document, the backlog and the style guides.

- `assets/` - fork-authored artwork.

- `vendor/` - third-party sources kept in tree, each with its own license and pinned origin.

- `utility/` - standalone helper scripts, actions and the cross-platform launcher.

- `filesystem/` - a tree mirroring where files go on disk, so a drop-in theme folder can be copied straight across.

- `cicd/` - the local build, release and publish automation. See [Delivery](#delivery).

- `.github/` - repo metadata and the one release workflow.

Upstream kept everything at the root with decades of accumulated meta-files. The fork consolidated the build under `source/` and dropped what no longer serves a standalone cross-platform project: old changelogs, distro packaging, upstream CI.

Inside `source/` there are four layers, bottom to top, each depending only on what is below it.

- `eel/` - a small widget and utility library inherited from the fork's ancestry: string and GTK helpers, stock dialogs, the editable label and the canvas the icon view draws on. It knows nothing about files or settings, which is why the couple of desktop-integration helpers living here read the desktop's own settings directly instead of asking the config store.

- `libnemo-extension/` - the public plugin interface, and nothing else. The interfaces a third-party extension implements (menu provider, column provider, property page, info provider) and the small value types they exchange. It has its own headers and depends on GTK and on none of our other code. A default build makes it a shared library for extensions to link against. The release build folds it into the program, which then exports the same API itself, so an extension loads against either one.

- `libnemo-private/` - the model. Files and directories with their asynchronous attribute loading, the file operations engine, search, thumbnails, favorites, the settings store, the per-file metadata store, and the platform backends for trash, network and shell integration. No window or view lives here.

- `src/` - the application and its views. The GtkApplication, windows, tabs and slots, the icon, compact and list views, the sidebar and path bar, and the properties and preferences dialogs.

~~~text
    +---------------------------------------------------------------+
    |  src/               app, windows, tabs, views, dialogs        |
    +---------------------------------------------------------------+
    |  libnemo-private/   files and folders, file operations,       |
    |                     search, settings, metadata, platform code |
    +--------------------------------+------------------------------+
    |  libnemo-extension/            |  eel/                        |
    |  the public plugin interface   |  widget and string helpers   |
    +--------------------------------+------------------------------+
         GTK 3, GLib, GIO, libarchive and the other libraries
~~~

Platform-specific code is kept out of the shared files where it can be: `*-win32.c` modules for trash, network, shortcuts, clipboard, drag-and-drop and shell actions, plus a POSIX compatibility header that lets ordinary callers compile unchanged where the platform has no equivalent. Some large shared files still carry inline platform blocks. Settling on one convention is an open item.

### Data flow

A location is a URI throughout, and everything hangs off two model objects.

- `NemoDirectory` owns the list of files at one location and the machinery that loads them. Views ask for a set of attributes - names and sizes, mime types, deep counts, thumbnails - and the directory works out what is missing, issues the asynchronous requests, and reports each answer as it arrives.

- `NemoFile` is one file. Attributes arrive in stages, so a file starts with a name and fills in over time. It emits `changed` whenever anything about it moves, and every view redraws from that one signal, which is also why the caches added for redraw speed all invalidate there.

- Anything the filesystem does not store is layered on top. Per-folder view state, custom icons, emblems and favorite markers come from the app's own metadata store and are merged into the file's attributes as they load. On Linux a gvfs metadata daemon may supply the same keys; ours wins.

- Settings flow the other way. A read goes through one store to one file, a change emits a per-key signal, and the widgets bound to that key follow. An external edit to the file produces exactly the same signals as a change made in the UI.

~~~text
    disk, share, trash     --GIO, async-->  NemoDirectory  --files added-->  views
                                                 |                           |
    our metadata store     --merged in-->    NemoFile      --changed----->   |
                                                 ^                           |
                                                 +---attributes wanted-------+

    settings.shcl  <-->  config store  --changed::key-->  bound widgets
~~~

### Execution flow

One process per window by default, one main loop each, and a firm rule that nothing slow runs on it.

- Startup registers the application, opens the settings store, and creates a window. It never hands the location to a copy already running; see [One process per window](#one-process-per-window) for why.

- Directory loading, file operations, search and thumbnailing all run off the main loop: GIO asynchronous calls for anything touching a filesystem, worker threads for thumbnail generation and for the file operations engine.

- Work started off the main loop reports back on it. File operations own a progress object the UI observes, thumbnails hand back a finished image, a completed directory load emits `done_loading`. Callbacks outliving their object are the recurring hazard, so long-running work holds a reference and cancels on dispose.

- Debounce and coalesce rather than write or redraw on every event. Settings saves, metadata saves, window geometry and sidebar rebuilds all batch.

### One process per window

Each window is its own process by default, and every launch is a fresh one. A crash then takes one window rather than all of them, and two versions can be open side by side, which is what trying a build next to the one in daily use needs.

- The copies still find each other. Each queues on the one bus name, so a caller from outside always reaches the oldest and the rest are read off the queue. That is how `--quit` and Close All Windows reach every copy, and how `--reset` knows one is running.

- What it costs: a tab cannot really move to a window in another process, only be handed over, and its back and forward history stays behind. On Windows a new window carries the packed program's startup time rather than appearing at once. Those two are why it is a setting - turning it off puts new windows back inside one process. Launches from outside stay separate either way.

- A tab goes to another window by its right-click menu, which lists every other window and a new one, or by being dropped on another window. What moves is its folder, its view and its selection. The window taking it opens a tab with those, and the tab it came from closes once that has worked. A drop finds the window under the pointer from the window manager's stacking list on X11 and the window order on Windows. Wayland does not say what is under the pointer, so there a dropped tab always gets a new window. A search tab stays where it is, since the search lives in its own process.

- A selection has to be sayable on a command line for another process to show it, so `--select` takes the folder around an item with the item selected. "Show in folder" from other programs goes through it.

- D-Bus needed no per-platform gating. GLib autolaunches a per-user session bus on Windows as well, shared across processes, so the freedesktop file-manager interface gets a real connection everywhere.

## Features

### Configuration and persistence

Settings are ours, in a file we own, in a format a person can read. No settings daemon, no compiled schema, no per-platform store to keep in step.

GSettings was replaced outright rather than kept as an API over a new backend. A backend would have been a fraction of the work and left every call site untouched, but it keeps a compiled schema to build, install and ship on every platform, which is the thing being got rid of. The full replacement moved about three hundred call sites, and change notification, property binding and enum mapping are ours to maintain now. Both kept the shape they had - a detailed `changed::key` signal, a `bind` with optional mappings - so the call sites read as they did before. The other accepted cost is that settings do not migrate from a pre-1.0 install, because nothing is left that can read the old store.

Settings are isolated from an upstream Nemo installed alongside: our own file, our own config directory, app-private per-file keys. A few genuinely shared per-file keys - custom icons, emblems, annotations - stay interoperable on purpose.

Four stores, each with its own lifetime.

#### Application settings

Application settings live in `settings.shcl`, in whichever directory the platform keeps per-user configuration in: `~/.config` on Linux and BSD, `%APPDATA%` on Windows, `~/Library/Application Support` on macOS. A folder left by an older build is moved on first run.

- The file holds only what was actually chosen. A value equal to its default is dropped, so the file stays short and a later change to a default still reaches the user.

- Because of that the file alone would say nothing about what else there is, so everything unset is listed at the end, commented out, with the value in use and a one-line note wherever the name does not already explain itself. Uncommenting a line is the same as changing the setting in the dialog. Keys the app writes back itself - window size, sidebar width, the last state of a search toggle - are left off that list, since setting one by hand only gets it overwritten.

- Edits made while the app is running are picked up straight away, so hand-editing behaves like using the dialog.

- Types, defaults and allowed values live in one table in the code, and a matching schema sits beside the app so `shcl check --schema` can catch a typo in a hand-edited file. Keeping defaults central is deliberately against the config library's own per-call-site advice: with nearly two hundred settings, many read from several places, two call sites disagreeing about what a setting means when absent is a silent bug.

- A handful of settings are the desktop's to decide rather than ours: which terminal to open, whether the session remembers recent files, 12h or 24h clocks. Where a desktop publishes them we read its answer, and everywhere else our own value stands in. That is the only remaining use of the desktop settings database, it is read-only, and it never touches a schema of ours.

- A few settings are file-only, with nothing in Preferences.

- What a rename starts out with selected is a checkbox under Behavior. The default selects the whole name, extension included, since a person pressing F2 usually means to replace the name outright and a re-typed extension is cheaper than one silently kept. Turned off, only the part before the extension is selected.

- Where a setting is a command line for another program, the parts we fill in are written `{{LIKE_THIS}}` - capitals between double braces. Braces because nothing expands them: the same line pasted into a shell or a command prompt to try it out comes back unchanged, where `%NAME%` would vanish on Windows and `${NAME}` would on Linux. Only the markers a setting declares are replaced, so anything else in braces passes through as itself and there is nothing to escape.

#### Bookmarks

Bookmarks are the toolkit's own file on Linux and BSD, shared with every other GTK program there. On Windows nothing else reads that file and it sits in the local profile, so the list is kept beside the settings in the roaming one instead.

#### Per-folder view state

Per-folder view state - view mode, zoom, sort column, column layout - is app-owned and portable, in one file under the config directory. This replaced the Linux-only metadata service, so the behavior is now identical everywhere.

- Only a real per-folder choice is stored. A value that merely matches the current default is left out, so the folder keeps following the default if it later changes. Upstream stored it either way, which quietly pinned every folder ever opened.

- Changing a default in Settings also applies to folders already on screen. Folders not being looked at keep their own view and zoom until visited.

- Remembering per folder is off by default. While it is off nothing saved is read and nothing new is saved, and a window only keeps what was picked in it until it closes.

- A folder's settings are kept as one set: view type, sort and reverse, folders and favorites first, the zoom of each view, text beside icons, same-width columns, folder expanders and the list columns. With inheriting on, a folder with no set of its own uses the nearest parent's, else the defaults. A change in such a folder copies the set it was using first, so the rest does not jump back to the defaults. A small marker is saved with the set, so a set whose values all match the defaults still counts.

- Opening a folder saves nothing. The views write their settings back as a folder loads, and a write that matches what the folder already uses is dropped.

- The Current tab on the Views page shows the set for the folder in the last focused window, applies a change to that window at once, and can forget the set. The Default tab edits the defaults. Each has a button that copies to the other.

- Window size, position and maximized state are shared by every window and live with the application settings. They are written shortly after a move or resize settles rather than at close, so an abnormal exit does not discard them. With nothing saved yet a window opens at 1280x720 including its frame, with the side pane at about a fifth of the width.

#### File cache

The file cache is the fourth store. It is a private SQLite database under the user's cache directory, holding what has been worked out about files on disk so it does not have to be worked out again. Thumbnails are the first thing in it and the reason it exists, but the tables are about files rather than about pictures, so a checksum for a text file is as much at home there as an image is.

- Thumbnails were kept in the shared freedesktop cache until 2026-09-21, and that folder is still read. A thumbnail another program already made is used rather than rendered again; nothing is written back to it. On Windows and macOS there was never anything to share with.

- The change was asked for, and the earlier decision to stay with the shared cache is reversed. What settled it is that the new requirements cannot be said in a PNG-per-file store keyed on a hash of the path: a thumbnail stored at the largest size a file has actually been shown at, files recognized as the same after they move, and pruning by how often something has been drawn. The dependency that argued against it in 2026-09-05 turned out to be one apt line per Linux container and nothing at all for Windows, where the sysroot already had it.

- SQLite is linked static. It has to come through pkg-config rather than meson's `find_library`, because the cross sysroot is not on the compiler's own search path.

- Three tables of data, plus one row of bookkeeping for pruning. `files` is one row per distinct set of file contents: the size, and the checksum once anything has bothered to compute one. It holds no image, picture or not. `paths` is one row per uri, pointing at the file it holds and carrying its own timestamp. `thumbnails` is only for images; it hangs off a `files` row and holds the encoded image.

- Splitting paths from files is what lets a file that moved, or a second copy of one, find a thumbnail that is already there. It is also what a duplicate finder would need, which is why the split is drawn this way rather than around thumbnails: every file seen is a `files` row, and the copies of one are the paths hanging off it. A file nothing can draw has no `thumbnails` row and is otherwise an ordinary record.

- A checksum settles what two records that looked separate really were, so learning one folds them together. The image and every other name move onto the record that stays. Without a checksum, size and timestamp together are the only guess available, and two unrelated files that happen to match both would share a thumbnail.

- Every launch is its own process and several can be open at once, so the file is in WAL mode with a busy timeout. Writes are small and the whole store is rebuildable, so `synchronous` is NORMAL rather than FULL - a power cut can cost the last few rows, which is not worth an fsync per row.

- A damaged file, or one written by another version of the tables, is thrown away and rebuilt at open rather than migrated or repaired. Nothing in it cannot be worked out again from the disk. Damage noticed while running only stops the store being used, because deleting a file other processes still have open is worse than going without until the next launch. It also leaves a marker beside the file, and the next launch starts over when it sees one. A damaged page deep in the file does not stop it opening, so without the marker nothing would ever act on it.

- Draw counts are held in memory and written in one transaction. Scrolling a big folder draws the same file repeatedly, and the age rule works in days, so a write per draw would buy nothing.

- A checksum is also left on the file itself, in three attributes: the checksum, and the size and time it was taken at. That way it travels with the file - a copy onto another machine, or onto a drive this program has never seen, arrives already knowing what it is. On Linux and the BSDs they are extended attributes in the `user` namespace; on Windows they are alternate data streams, which only NTFS and ReFS have, and the setting says so where it is switched on.

- The time is written last of the three, and reading requires both the size and the time to match. A write that stops part way then leaves the old time next to the new checksum, and the next reader throws the lot away. Written the other way round, a half-finished write leaves the old checksum under a size and time that both match, which no reader can catch.

- Writing an attribute is slow enough that it happens after the database is already up to date, never in front of a draw. It is also optional: a file system with nowhere to put one simply goes without, and the database still knows.

- It is off by default, and switched on by a setting. It changes the file's status-change time, though not its modified time, and that can wake a backup tool, though most ignore it. With it on, a checksum is written when a thumbnail is made, so files already in the cache get one the next time they are drawn again. A file that already carries the same checksum is left alone.

- On NTFS a write to any stream counts as a change to the whole file and moves its modified time. Left alone, that would make each checksum stale as soon as it was written, and the file would read as edited to everything else too. The write tells its handle to leave the file times as they are.

- A thumbnail is made at the size it is being drawn at, rounded up to a step of 128 pixels, and made again bigger when a draw wants more than is stored. So each picture is kept at the largest size it has actually been shown at, and a folder only ever seen small stays small on disk.

- JPEG at quality 90, and PNG only when the picture has see-through parts. An alpha channel that is opaque everywhere still counts as a photo. WebP is out, since gdk-pixbuf cannot write it and the Windows build cannot read it.

- A thumbnail is read on a worker thread and decoded no bigger than the draw needs, so a picture stored at 640 and shown in a list takes a 128 pixel copy in memory. JPEG decodes straight to a smaller size, which is far cheaper than decoding in full and scaling.

- A render that failed is stored too, with no image, so a broken file is not tried again on every launch. Editing the file clears it.

- A file that has to be read in full to make its thumbnail is checksummed at the same time, since its bytes were just read. A copy of it under another name then finds the thumbnail already there.

- Reload makes the folder's thumbnails again, as it always has. It forgets the stored copy and stops using the freedesktop one for those files. The freedesktop cache itself is left alone.

- The file's own type icon stays up until its thumbnail is ready. There is no "loading" icon in between, since few themes have one and the stand-in flashed. An edited file keeps its old thumbnail until the new one is made.

- A folder of pictures on a local disk has all its thumbnails made once it has loaded, strictly top down in view order. Nothing is asked for while it loads, since where a file ends up is only known once the whole folder is in. Scrolling does not change the order: a file that comes into view waits its turn, and so does a picture stored on an earlier visit. A new sort or zoom queues the folder again in its new order.

- Each picture is also held in memory as it is made or found in the store, still in that order, so scrolling anywhere finds it drawn already. That stops at a limit, 1 GiB by default. Past it only the pictures within two screens of the view are held. Those are read back from the store several at a time, and the picture drawn longest ago makes room, one from a folder no view shows first.

- A folder that is left keeps its pictures for a minute, in case it is gone back to. Opening another folder of pictures lets them go at once.

- Thumbnails are made on half the processors by default, so neighbors can finish a little out of order. None can go ahead of its place in the queue. A folder that is not mostly pictures, or is on a share, is only made as it comes into view, top down on each screen.

- Photoshop files are read by a small reader of our own, since gdk-pixbuf has none. A .psd or .psb carries a flattened copy of the picture after its layers, and that is all a thumbnail needs, so the layers are skipped. It is shrunk while it is decoded, so a large file never sits in memory at full size. Grayscale, indexed, RGB and CMYK are read; Lab, multichannel and 32 bit files are not.

- Camera raw files are read by another small reader of our own. Nothing here can develop sensor data, and a library that can is large and slow. Every camera also stores a finished JPEG preview in the file for its own screen, so that is what gets drawn. The reader walks the file's directories, takes the smallest preview that still covers the draw, and decodes it already shrunk. Previews under 320 pixels are passed over while a bigger one exists, since the small ones are often letterboxed. The file's own orientation is applied, since the preview is stored the way the sensor saw it.
	- The TIFF based files are covered this way: DNG, CR2, NEF, ARW, PEF, RW2, SRW and kin. ORF keeps its preview in the maker note, which is read too. RAF and CR3 are other containers and have a path each. Canon CRW, Minolta MRW and Sigma X3F are not read.
	- A thumbnail takes a few milliseconds and reads a few directories and one JPEG, never the sensor data. So a raw file is not checksummed as a side effect of its thumbnail, the way a file read in full is.

- Formats nothing in the process reads go to ImageMagick, when it is installed. It is the last thing tried, after gdk-pixbuf and the two readers above, and covers JPEG 2000, HEIC, AVIF, EXR, DDS, TGA, FITS and the older raw containers. Linking a decoder for each would add a library per format to every build, most of them to the portable Windows exe as well, for files few people have. ImageMagick is already on most Linux desktops, has a Windows installer, and learns new formats on its own schedule.
	- It runs as a separate program, one file per run, on the same worker threads as every other thumbnail. Batching several files into one run was measured and saved about 15%, since the time goes into decoding rather than starting the program. A batch would also lose the top-down order and per-file cancel.
	- Only formats on a fixed list are handed over, chosen by extension, and the format is named in the command rather than guessed. ImageMagick also reads scripts, vector files and pseudo-files, and a file named for one could otherwise be read as one.
	- It never sees a file name. The file goes in on stdin and the PNG comes back on stdout. ImageMagick reads meaning into names, such as `%d` as a frame number, and versions 6 and 7 escape that differently, so no one spelling works for both.
	- Memory, disk and time are capped for each run, and a run is killed after 30 seconds like any other external thumbnailer. A file over 256 MiB is left alone, since ImageMagick holds all of its input in memory first. Both "disable all" and the per-type list in the thumbnailer settings apply.
	- `magick` is looked for first. On Linux, ImageMagick 6's `convert` is used when there is no `magick`. On Windows `convert.exe` is the system tool that converts a FAT drive to NTFS, so it is never run there.

- Pruning runs on a worker thread over a connection of its own, so it never holds up a draw. Each pass checks the file for damage, forgets local files that are gone, drops thumbnails not drawn for too long, then drops the least recently drawn until the file is under its size limit, and last hands the freed space back to the disk.

- A file is only forgotten when its folder is still there. A whole folder missing is more often a drive that is not plugged in. Shares are skipped, and so is any folder that is slow to answer, since one that is not answering costs about twenty seconds per question.

- A pass is due at random between 4 and 24 hours after the last, and waits until nothing has been drawn for 5 minutes. All three are in the config file. Whoever finishes a pass picks the next time and writes it in the file, so every copy running agrees on it.

- Only one process prunes at a time. The claim is a row in the database, taken in a write transaction, so SQLite's own locking decides who wins. That works the same on every platform, where a lock file would need a separate answer for Windows. The claim carries a heartbeat, and one nobody has touched for ten minutes belongs to a process that died and is taken over.

- The space goes back a few pages at a time with incremental vacuum rather than a full VACUUM. A full one holds the write lock for as long as it takes to copy the whole file, and every other copy would wait on it.

- Quitting stops a pass part way through, and it lets go of its claim.

- The settings are on the Preview page: the size limit, the age limit, forgetting missing files, and saving checksums onto files. The schedule stays in the config file only. The page also shows how many thumbnails there are and the space on disk, with a button to clean up now and one to empty the cache.

- Emptying asks first, then drops every row and runs a full VACUUM. In WAL mode the VACUUM writes the new file into the journal, which then holds more than the old file did, so the journal is folded back in and cut short after it.

- The older sweep of the shared freedesktop cache is gone, along with its two settings. Nothing here writes to that cache any more, and other programs that do can look after it.

### File operations

#### Trash and delete

Trashing and deleting are the two things a file manager cannot take back, so they are held to a higher bar than the confirmation preferences alone. This was settled after a copy of the app emptied a home folder with nothing anywhere to say why, and tightened when it happened a second time.

- Every trash and delete job writes one log line: how many items, which folder, the first item, the window, whether a trash or delete command asked for it, and the key or button behind it. On Linux the line also goes to the system journal. The usual place for it is a log file under the home folder, which is the first thing lost.

- Home, any folder above it, and the top of a mounted drive or share are never trashed or removed, whatever asked. A delete that reaches a mount inside a folder stops there instead of emptying the drive. This is checked where each file is actually removed rather than at the dialog, so a path that never shows a dialog is held to it too.

- A job that would take most of what sits directly in home is refused outright, not asked about. Nobody clears a home folder from a file manager on purpose, and a question is one Enter away from yes.

- Nothing outside a window can trash, delete, move or empty the trash. Each of those starts from something done in a window: a command, a drop, undo, or a button in the preferences. The bus interface that let other programs copy, move and empty the trash came from the Nemo desktop, and was removed. `cicd/utility/lint-c.bash` keeps three lists: the files allowed to start one of these jobs, the bus methods, and the files allowed a raw delete. That last list only touches the app's own files, such as the settings on `--reset`, the thumbnail cache and old crash reports.

- Only the trash and delete commands in a window count as a person asking. Anything else, such as undo or a drop, always asks first whatever the preference says, and the question says so. This used to be inferred from whether an input event was in flight, which any unrelated key or click could satisfy.

- A job of `confirm-many-items` or more asks even with confirmation switched off. Twenty by default, and zero turns it off. A slip that takes one file is a nuisance; one that takes a folder is a day.

- Every question that can remove files starts on Cancel. This reverses the earlier call to keep the affirmative as the default. The dialog was meant to be the pause, but a stray Enter goes straight through a pause.

- A trash or delete key that arrives within a second of a window appearing or taking focus is ignored. A window that opens while someone is typing somewhere else gets the rest of that typing.

- Removing a folder tree never follows a link to another folder. The link is removed as a link. That holds for a symlink, a junction, a `.desktop` file and a `.lnk` shortcut, wherever one sits in the tree, and it is not a setting. Windows reports a junction as an ordinary folder, so every walk that removes things asks for itself, and the lint gate fails a new one that does not.

#### Links

Copying a link asks what should be at the far end. A link can stay a link or be replaced by what it points at, and neither answer is right every time, so the question is put once per operation rather than guessed. It is asked whenever the source holds a link, on every platform, including where the destination can hold none - there every option but the copy is grayed out and the dialog says why. A copy that quietly turns links into files, or files into links, is the thing being avoided.

- Windows is where this mattered most. A copy there always followed the link and left the contents behind, so a link could not be copied as a link at all. POSIX already kept symlinks by default; what is new there is being able to ask for the contents instead.

- Windows has two kinds of link where POSIX has one, and the dialog says so. A folder symlink and a junction both point at a folder, but only the symlink needs a privilege Windows normally withholds. Each row starts on the kind it found and falls back to the nearest kind that still reaches the same target, then to a plain copy. Anything the destination cannot take is grayed out rather than hidden, so the dialog does not change shape between machines.

- A link counts as one item rather than a folder to walk into. That is what POSIX always did and Windows never did, and it is what stops a copy following a link to somewhere large or unreachable.

- A move always takes a link as the link. Taking the contents would empty the folder the link points at, which is not what was asked to go. A move to another drive still asks, and can make a different kind of link, but the copy option is grayed out. A move on one drive is a rename and asks nothing, so every link arrives as it was.

- Make link asks what to make, one row for the folders and one for the files. Folders get a junction or a symlink on Windows and a symlink elsewhere. Files get a symlink or a hardlink. Either can be a `.lnk` shortcut, labeled Shortcut. The title is in the window's title bar. From the menu the dialog does not say where the links go, since it is always the folder in view. A link drop opens the same dialog, and there it names the folder, since a drop can be onto another folder or another window. A symlink path row picks absolute or relative for whatever comes out a symlink. It is hidden when nothing does, since a junction is always absolute and a hardlink has no path, but keeps its space so the dialog does not jump.
	- Absolute is the default. Relative is there for links inside a tree: it keeps working when the whole tree is moved or mounted somewhere else, which is the usual reason to link inside one. It always has to work from the folder the link really sits in, since `..` climbs from there. So the paths as seen and the real paths, with symlinked folders resolved, are both tried, and the shortest that still reaches the target wins. Resolving alone would send a link up to the root whenever a symlinked folder on the way leads to another tree. Between two Windows drives there is no relative path, and the link keeps the absolute one.
	- A link made beside its original says what it is: "photo - symlink.jpg", "photo - hardlink.jpg", "folder - junction". The extension stays last so the link still opens as its type. A shortcut follows Explorer's pattern instead, "photo.jpg - shortcut.lnk". A link made in another folder keeps the original's name. A clash adds " 2", " 3" and so on.
	- A drop never asks the drop question for links, since the dialog is the question.
	- A hardlink is never picked for anyone, even where a symlink cannot be made. It is the one choice that can cost something: an edit through one name changes a file that looks unrelated, and a program that saves by replacing the file splits the two names apart without a word. Its tooltip says so, and choosing it asks once more, every time, with Cancel as the default. Backing out goes back to the dialog with the choices as they were.
	- Nothing is remembered. Every open starts from the defaults, so what it offers never depends on what was done last time. A way to change the defaults may come later.
	- Windows with no symlink privilege still gets the menu item, since a junction, a hardlink and a shortcut need none. The symlink choices are gray, with a line saying why, and files start on Shortcut.

- A shortcut can hold several paths at once, and is followed by the first that still leads somewhere. So every shortcut gets all three: Absolute, Relative and Portable. There is no choice to make.
	- Portable is the path with a Windows environment variable in it, such as `%USERPROFILE%\Documents\notes.txt`, where one covers the target. It keeps working for another user or on another machine, so it is tried first. Off Windows the home folder is `%USERPROFILE%`, and a target on a mounted Windows share keeps its `\\server\share` path there instead.
	- Windows 11 follows a shortcut through its item ID list, which names the target in Windows' own terms, or else through the portable path. It never uses the absolute or relative path alone. No Linux path can be written as an item ID list, so Explorer follows a shortcut made off Windows only through its portable path. Nemo Anywhere on Windows reads the rest itself.
	- On Windows the shell writes the item ID list and the relative path.
	- The paths inside are spelled the Windows way on every platform, with backslashes, and variables are always `%NAME%`, never `$NAME`. Each is read back for the platform in use.

- Edit link changes an existing link's name and where it points. It is offered only when one symlink, junction or shortcut is selected. A hardlink has no target to change.
	- A symlink or junction gets one target field, taken as typed, so a relative one stays relative. The new link is made under a spare name first. On Linux it is then renamed over the old one, so the old link is never missing. Windows cannot rename over a folder, so there the old link goes first, and the new one is taken back if it will not.
	- A shortcut shows its three paths, and any can be emptied to drop it. Only the paths are rewritten; its arguments, Start in folder and icon stay. What else it holds about the old target goes with them, the item ID list included, since Windows would follow that first.
	- The old link is removed through the delete guard, which removes a link and nothing else.

#### Archives

Archives are written by libarchive, with the `7z` and `rar` commands as optional extras rather than the primary route. Linking a library needs nothing installed on the user's machine, writes the tar, zip and 7z families natively, and reports real per-file progress through the ordinary job queue. What it cannot do on the write side is why the commands are still reached for: no rar at all, and no split volumes, solid blocks, duplicate references or 7z encryption. Where an installed command can honor one of those it is used, and where nothing can the option is grayed out rather than hidden.

The planned Compress dialog reset, with its link choices, size totals and the split of the archive code from the rest of the app, is in [20260929_compression.md](design_docs/20260929_compression.md).

- Which writer gets a job follows from what was asked for, not from the format. Encryption and splitting are requirements - a backend that cannot do them is not a candidate, because quietly writing a readable archive when one was asked to be locked is the worst possible outcome. Everything else is a preference, honored where a writer can and dropped where none can, rather than failing the job.

- Where the archive goes follows the long-standing convention rather than anything invented here. One item is archived as itself and offered beside itself, so opening the archive shows the folder and the contents are one level in. Selecting a folder's whole contents instead still takes the folder's name, but is offered inside the folder with the contents at its root. A partial selection gets no suggested name at all - it is not the folder, and there is no other name a person would agree with - so the field starts empty and Compress waits until it is filled in.

- Compressing a selection separately is that convention applied per item, and deliberately one job rather than one per item: several progress bars racing for the same folder would be unreadable, and canceling would mean canceling each of them.

- Unpacking reaches much further than writing, so the two sides are not symmetrical. libarchive reads the tar, zip, 7z, rar, cab, lha, cpio, xar and iso families and the bare compressors, which is most of what anyone double-clicks, and it reads them entry by entry - which is what makes per-file progress, canceling and a collision prompt possible at all. A command is reached for only when libarchive will not open the file, and only while nothing has been written yet, so handing the archive on costs nothing.

- Where an archive says an entry goes is not taken at its word. A stored path that is absolute, names a drive, or climbs out with `..` is reduced to something inside the folder the person picked. An archive must not be able to write wherever it likes on the strength of being opened.

- Following symlinked and junctioned folders is off by default, and is ours rather than the archiver's, because the tree is walked through GIO before anything reaches a writer. A link loop would otherwise pull in the whole disk, so the walk remembers directories by file id and terminates even with following switched on.

- A link that leads nowhere goes in as a link, even when links are otherwise followed, since there is nothing to follow. The library does that in every format it writes. 7z and rar keep links all or none, so there those links go in first, by a run of their own that keeps links, and the real run adds the rest. The delete check counts such a link as in.
	- It cannot be done for a split set, which neither tool can add to, or for 7z on Windows, where 7-Zip is never asked to keep links. It is also not done where 7-Zip would reach the link through a linked folder, which it refuses, or where rar would read a `*` or `?` in the name as a pattern. There the link is left out, and a warning names it.
	- The 7z and rar commands end a run that passed over such a link on a warning status. That is taken as success only when every warning they printed is about one of those links, so any other warning still fails the archive.

- A name with `*` or `?` in it is a name, not a pattern. 7-Zip is told so. rar has no way to be told, so a rar archive is refused when one of the names it would be handed has either, rather than written with the wrong files in it, and unpacking passes such an archive on to 7-Zip.

- Deleting what went in is off by default, and never happens on the writer's word. The archive is read back first, and every file that should be in there has to be there under the same relative path at the same size, with nothing passed over on the way in - a dangling link, or a linked folder the options said not to follow, is a miss like any other. The check reads the archive with the library rather than asking the program that wrote it, because checking work with the code that did it proves very little. What passes goes to the trash through the ordinary delete, which asks in its own right, so ticking the box is never the last word. A split archive is not offered the option at all: one volume will not open on its own, so there is nothing to check.

- The dialog starts from what it was last used with, so a second archive does not mean setting the same five things again. Two are left out on purpose. The password is never written anywhere, and deleting the originals is a decision about one archive rather than a preference, so both start clear every time. What is remembered is per user, not per folder.

- Deleting the originals with a password set asks for the password a second time before anything starts. A typo writes a perfectly good archive, the read-back passes, and the files go, leaving an archive nobody can open. Either half on its own leaves a way back, so it is only the two together that ask.

- The command lines the two programs run with are settings, not code, so a person can point one at a different build or add a switch nobody thought to offer. Every control the Compress dialog offers has a `{{MARKER}}` of its own, so an edited line keeps the dialog working; leave one out and the app says which control has gone quiet. Clearing a line puts the original back. A password is handed over as a value and never written into the line or the settings file, though it is still visible in the process list while the program runs, which is true of every archiver.

- How much of the machine a compression may use is one setting, a percentage of the cores found rather than a thread count written into each line. A percentage still means something on a machine with a different core count, and one answer covers both programs and the built-in writer. The default is 50%, because a hyperthreaded core is not a whole core and taking every logical processor slows the rest of the machine for nothing.

### Search

- Content search converts documents itself, in C, on libraries the app already links. The old helpers were a Python script, a shell script and a LibreOffice call, none of which exists on a stock Windows machine and each a dependency the install could not promise. Word, Excel and PowerPoint in both their old binary and newer zip-of-xml forms, OpenDocument and EPUB are covered. The definition-file mechanism stays, so a helper for anything else can still be dropped in.

- Results can be grouped under the folder holding them. It is a heading row per folder that actually has a match, labeled with the path under the folder searched, rather than a full tree of every folder in between - a tree puts rows on screen for folders with nothing in them, and reading that path off one row is what a person actually wants. The heading rows are built by the view rather than the model, so a folder nobody asked to open is never read, monitored or walked. Flat is still the default and switching redraws from the results in hand rather than searching again.

- On Windows the search index is used when asked, through a switch that is off by default. It answers for any folder the index covers; a folder outside it, a network location, a regular expression or a case-sensitive content match goes to the ordinary walk unchanged. Off by default because the index only knows what it has been told to watch, and a search that quietly misses a folder is worse than a slow one.

### User interface

The window is a menu and toolbar, the side panes, a path bar and a view, and the view is interchangeable.

#### Window, tabs and side panes

- A window holds tabs. Each tab is a slot with its own location, history and view, and navigation, loading state and the busy cursor all belong to the slot, which is why a slow location can only block its own tab.

- A tab is as wide as its title, between two percentages of the tab row. With the full path shown, a path too wide for that gets shorter a step at a time: an ellipsis eats the middle a folder at a time, and only when that has run out do the folders above the last one drop to their initials. The root and the folder's own name are always kept, and a path under home reads as ~ on Linux.

- The tab in front is the exception. It is not capped, so it spells its path out whenever the row can spare the width, and the tabs behind it shorten together until it can. They take the same step as each other, so the row reads as one set. Only when they have nothing left to give does the tab in front start shortening too, and past its shortest form the row scrolls, as it always did.

- The window title is the folder and then the program name, or the whole path and then the name when the full path is shown. The folder goes first so a narrow taskbar button still shows it. It is in double quotes only when it has a space, which is where the end of the name gets hard to see. A title bar belongs to the window manager and its width cannot be read, so the path is measured against the window's own width less room for the icon and buttons - close enough to tell a path that obviously fits from one that does not. It shortens by the same ladder as a tab, and is worked out again whenever the window is resized.

- Places and the tree view are separate panes, and both can be up at once. Each remembers its own width. A window resize leaves Places at the width it was given and shares the change among the tree view and the content panes, each in proportion to what it already had, so a pane at a third of the window stays at a third. Either pane can be turned off by itself, and one button collapses both and puts them back.

- The tree view lists folders only, and a folder with nothing to list has no expander. Whether a folder has any sub-folders is checked in the background before anyone opens it, one folder at a time so that expanding a big folder does not flood the disk. Shares are not checked, since one that is not answering would hold up every folder behind it; they keep an expander until opened. Hidden folders count for the check, so a wrong answer can only leave an expander that goes away once the folder is opened.

- Places is one tree store rebuilt from bookmarks, mounts, drives and network locations. Anything that could be slow to answer, such as free space or mount state, is fetched off the main loop and folded in when it arrives.

#### Views and list columns

- Three views share one interface: icon, compact and list, with an optional tree column in list view. Each reads its layout from per-folder state where the folder has any, and from the defaults where it does not.

- Every other row can be shaded, off by default. GTK 3 no longer draws the rules hint, so each cell tints its own background on odd rows, counted by place on screen so an open subfolder's rows take their turn. A cell leaves its background off a selected row, and the tint is see-through, so selection and hover still read. The color is the setting's, then the theme's `nemo_row_shading`, then a faint wash of the text color.

- The row under the pointer is tinted with the theme's selection hue, the way Explorer does it, and never gray, so it cannot pass for a shaded row. It is the faintest of the row colors. The tint is sized in OKLab, a color space where equal steps look equal on light and dark rows. It is fainter than a shaded row, no more than 40% of the way to a selected one, and as much of it as possible is hue rather than lightness. Near white there is little room for color, so on a white row it is also a little darker. A theme that selects in gray gets Windows' default blue. It covers the list and both sidebars. The setting wins, then the theme's `nemo_row_hover`, then the worked-out tint.

- The list view scrolls sideways before it crushes a column, and remembers a width dragged by hand. The whole rule is under [List view column widths](#list-view-column-widths).

- The column roster earns its defaults. Ext shows by default just right of Name, without the dot, and stays blank when the tail after a dot is not really an extension. Owner shows the user name alone, by default on Windows too, where the platform reports a file's real owner. Owner name and Owner - name are offered everywhere. Windows reports no display name, so there it is looked up for local accounts only; a domain account shows none. Permissions source - whether a file's permissions come from its folder, from the file itself, or both - is offered on Windows and off by default.

- Extensions can add context-menu items, list columns, property pages and file attributes. Nothing in the interface depends on one being present.

#### Icon sizes

- An icon size is a number of pixels, at a scale factor of one. It used to be one of seven named steps, which meant eight separate tables keyed by the step and no way to ask for a size the list did not already hold.

- The named steps survive as stops: 24, 32, 48, 64, 96, 128, 256, 320, 448 and 640 pixels. They are what the slider marks and what Zoom In and Zoom Out move between. The slider reaches everything in between, so the keyboard stays coarse while dragging is fine. Adding a size is a row in one table.

- Both the settings and the preferences window say it as a percent of the standard 64 pixels, so 100% is 64 and 1000% is 640. The preferences window shows each one as a spin box, since the range no longer fits a short list.

- There are two defaults for the icon view. An ordinary folder opens at 100%. A folder that is mostly images opens at 500%, which is 320 pixels, so pictures are shown at a size worth looking at without anyone reaching for the slider.

- "Mostly images" means at least two images, and images making up at least half of the files. Both numbers are in the settings file. Folders are not counted either way, so filing pictures into sub-folders does not change the answer. The count is taken once the folder has finished loading, which is the first moment anything is known about what is in it.

- Such a folder also opens in icon view when the default view is list or compact, unless it has a view of its own. Since the count waits for the load, the switch happens after it. To avoid that, the answer is kept in memory for the last few hundred folders, and the local sub-folders of a folder just loaded are counted ahead in the background. Either way the view is right from the first draw. Only a folder nothing is known about still switches late. Picking another view there by hand is saved on the folder, so it is not switched again.

- The image size is a default, not a rule. The slider still moves the folder, and where "Remember per-folder settings" is on that size is what sticks. The default itself is never written back: a folder with nothing of its own keeps following whatever the setting says, and in a window that is not remembering per folder the bigger size does not follow you into the next folder.

- A folder therefore remembers two icon sizes, not one: what it should look like full of pictures, and what it should look like otherwise. Zooming decides which of the two is being set by what is in the folder at the time, so zooming a gallery never moves the size its plain sibling opens at. With per-folder settings off, the window holds the same pair for as long as it is open, so a zoom in a gallery does not follow into the next list either.

- Both sizes inherit. A child with nothing of its own takes the pair from the nearest parent that has one and then picks between them by what is in the child. So one setting on a photo library gives every album under it the big size, while a folder of notes filed in the same tree still opens small. Where a parent only ever had one of the two set, the other falls back to its default.

- The Current tab in preferences shows both, since the pair is what children inherit. A size there that the folder itself will never use is still worth setting.

- The slider runs along the stops rather than over pixels. The range is nearly thirty times as wide at one end as the other, so spacing the marks evenly is the only way the low end stays usable.

- A name under an icon is one size whatever the icon is. It runs as wide as the icon above it, and never narrower than 110 pixels, or an ordinary file name wraps at the usual size. Below 32 pixels there is no name at all.

- The list view is held to the stops rather than taking any size, because a row's icon comes out of one of the model's size columns and a tree model's column count is fixed. Its row text does grow with its size, unlike a name under an icon: in a list the row height is most of what a size means.

- A saved size is a number of pixels. One saved before this change is a step number, 0 to 6, which no real size can be, so the two are told apart on read.

#### List view column widths

This rule has been rewritten several times and will probably move again, so the whole of it is here rather than spread between the code and a summary. This should be treated (and updated) as THE canonical, precise, complete, conflict-free definition. It describes where the behavior is going, so where the code differs it is the code that moves. The code has matched it since 2026-09-17. The arithmetic is in `nemo-column-layout.c`, which knows nothing about widgets and can be tested without a screen; the measuring that feeds it is in `nemo-list-view.c`.

- There are three "classes" of columns, for width sizing:
	- The minimum column width that overrides all minimum-width definitions below: Column header text.
	- Primary variable-width class:
		- Members: "Name", "Location".
		- Min width:
			- What will display all of the shortest N% values, as set by `list-view.column-fit-percent`, default 90.
			- Name counts every file in the folder, since every name matters.
			- Location counts each distinct value once, so a location repeated down a folder counts once rather than fifty times.
			- The share calculation: max(1, floor(count*FITPERCENT))
			- Plus ellipses for values that are too short.
			- Plus one character of air on the right.
		- Default width if room:
			- The width that shows all of the values for the column, plus one character of air on the right.
		- Max width (the width if the available horizontal space is more than all default widths combined):
			- The columns in this class expand proportionally (or either one alone if the other is not visible), so that the rightmost visible column's right edge is adjacent to the window edge.
		- Manual resize: Only persists for the current folder view. If navigated away and then back, it resets to this described default.
	- Fixed-width class:
		- Columns that display values that vary in narrow bounds based on the nature of their data.
		- These columns don't resize, and can't even be manually resized.
		- Sized to fit the longest value displayed, plus one character of air on the right.
		- Examples: All date/time-related columns, octal and *nix-style permissions
		- Manual resize: Not offered. There is no grip on the column edge to drag.
	- Minor variable-width class:
		- All other columns
		- Min width:
			- Uses the same formula as "Min width" for [Primary variable-width class], but for the shortest 50%, plus one character of air on the right.
			- Plus ellipses for values that are too short - except for columns that are already very narrow (e.g. Ext with only 1 to 4 character extensions.)
			- Plus one character of air on the right.
		- Default width if room:
			- Default size: Same formula and % as "Min width" for [Primary variable-width class], plus one character of air on the right.
		- Max width:
			- The width that shows all of the values for the column, plus one character of air on the right.
		- Manual resize: Persists for that folder view if "remember per-folder settings" is enabled (off by default), unless manually reset to default view by user.

- What gets measured, and when.
	- Every row is measured as it arrives and as its details fill in, which is a handful of cells at a time rather than a walk of the folder.
	- Rows a subfolder adds count while it is open, and are forgotten when it collapses.
	- Everything is measured again when the size changes the font or the icon, when the theme brings a new font, and when a column is switched on that was not there to be measured while it was hidden.
	- Samples are thrown away on a folder change. The names in the last folder say nothing about this one.
	- A column remembers the width it worked out for a piece of text, so a type, an owner or a set of permissions that repeats down the folder is laid out once instead of once a row. Name is left out, since no two files in a folder share a name. A column stops remembering past a couple of thousand distinct values, which is where a date would otherwise keep one entry per row for nothing. The remembered widths go when the samples do.

- Every column is a fixed-width column as far as the toolkit is concerned, whatever class it is in here. The widths above are worked out for the whole row at once and handed over, so there is nothing left for the toolkit to decide. That also lets the list run in fixed-height mode, where the row height is measured once instead of once per row - which is about half of what loading a big folder used to cost. The two go together: leave one column to size itself and the toolkit measures every row again, quietly, and the saving goes away.

- A horizontal scrollbar appears, if there is not enough room because the combined min column widths exceed the displayable area.

- The columns are laid out in the view's own size allocation, for the width the tree view is about to be given, not in the tree view's own. Laying them out from the tree view's allocation draws one frame at the old widths on every step of a resize, which reads as flicker. The difference between the two allocations is learned from the previous one, so the frame where a scrollbar appears or goes is the one case still caught late.

#### Labels and dialogs

- Every label reads as a sentence rather than a headline. Only the first word is capitalized, and a name keeps its capital wherever it stands: the platforms, the toolkit, Trash and the other sidebar places, formats, acronyms. Mnemonics do not move and shortcut text is untouched. It is checked at lint time over every translatable string in the tree, so a label copied from upstream in Title Case is caught where it is added.

- Properties is the platform's own on Windows. Alt+Enter and Ctrl+I hand the selection to the shell property sheet, the same one Explorer shows, so a file's details read the way they do everywhere else on the machine and any tab a third-party program adds is there too. Our own window is still there under Ctrl+Enter, covering what the shell sheet has no room for - a custom icon, an emblem, an annotation, an extension page - and anything the shell cannot name falls back to it rather than doing nothing. The other platforms use our window throughout.

- A settings window opens the size of its longest page. The preferences dialog measures every page it holds and opens tall and wide enough for the largest, up to nine tenths of the screen, so no page starts out behind a scrollbar. Its floor is written for a 96dpi screen and scaled by the display's font scaling, so it means the same thing at 150% as at 100%.

- Settings that only exist on Windows sit on a page of their own, and in a `windows` group in the file rather than scattered through the others. The page carries the separator choice, the hidden-file switches, the search index and the theme controls, and it is not built into the other platforms' dialogs at all. The keys still take effect if hand-edited anywhere - it is the page that is Windows-only, not the settings.

#### Hidden files and shortcuts

- Hidden means two things on Windows and one thing everywhere else. Windows marks a file hidden with an attribute and treats a leading dot as an ordinary character; the rest of the world reads the dot and nothing else. So Windows gets a switch and a View menu item for each, and turning hidden files on from the menu moves the pair together, so one keystroke shows everything that was out of sight. The two can still be set apart in preferences.

- A shortcut's extension is off the listing and on in the rename box. `.lnk` and `.desktop` are both noise in a file list and both have to survive a rename, so the name on screen leaves them off while the rename box shows the whole name, and a rename that arrives without one gets it back. Without that, renaming a shortcut would turn it into an ordinary file. The Ext column still says what it is. One preference covers both, offered on every platform since `.desktop` launchers are a Linux thing.

- A Windows shortcut opens off Windows too. With no shell to ask, the app reads the file itself. Windows keeps using the shell.
	- The path inside is used only when it can be placed for certain. A path with variables comes first, when every variable is set here; `%USERPROFILE%` is the home folder. Then a drive is found by the volume serial the shortcut records, and a share by server and share name among what is mounted. After that comes the path relative to the shortcut. That is the order Windows tries.
	- Names match ignoring case, as on Windows. Two names that differ only in case are a miss, because a wrong target is worse than none.
	- A folder shortcut changes the current folder rather than showing the folder inside it, the way Windows does. A file opens with its program started in the shortcut's Start in folder, or else the file's own.
	- Arguments are left out, since they were written for a Windows program.
	- The icon comes from what the shortcut records, never from the target, for the same reason as on Windows.

- A shortcut to a folder sorts with the folders when folders come first, on every platform. Whether its target is a folder is read from the shortcut, the same as its folder icon, so the two always agree. A shortcut whose folder has since gone keeps both until it is opened, since checking the target could stall on a share that is not answering.

#### Scaling and startup

- Scaling is the app's own job, not something done to it. The window declares itself per-monitor DPI aware, so a scaled display gets it drawn at that scale rather than drawn small and stretched, and moving it to a monitor at another scale redraws rather than restretches. The toolkit scales in whole steps, which leaves 125% or 150% short, so text is sized against the monitor's true DPI on top of that. Type comes out right at any scale; the widgets around it are still on the whole step below, which is the open item. On Linux and BSD the desktop publishes its own scaling and the toolkit follows it.

- A launch shows something at every stage. The window is put on screen at its remembered size and place as soon as it exists, before the first folder resolves, with its panes still empty. On Windows, where getting that far takes measurably longer, a small panel appears first - drawn with the platform's own toolkit, since it has to be up before GTK is - and leaves as soon as the real window has drawn.

### Appearance and themes

Two settings decide how the app looks: a light or dark mode, and the widget and icon themes to draw with. Both live under `appearance` in the settings file, both apply while the app is running, and both are offered on the Windows page of the preferences dialog. Elsewhere the desktop decides and there is nothing to ask.

- Mode is Light, Dark, or follow the system. Following means asking the platform - on Windows the `AppsUseLightTheme` personalization value, watched so the app turns with the rest of the desktop; anywhere the desktop has already told GTK, it means leaving that answer alone. An explicit Light or Dark overrides the platform everywhere.

- Themes are offered by the mode they suit. A theme states which backgrounds it was drawn for, and one that says nothing is judged by its name, which is how the convention already works: a trailing `-dark` marks the dark half of a pair, and a theme with a `-dark` sibling is the light half. Most colorful icon sets serve both, because GTK recolors the monochrome half to the foreground anyway. Choosing a theme and then changing mode swaps to its counterpart rather than leaving a dark theme on a light window.

- Targets unlikely to have GTK themes installed carry their own set: Windows and macOS. Linux and the BSDs use what the desktop provides. Each bundled icon theme is trimmed to the roughly 180 icon names a file manager actually asks for, which is what keeps one to a few hundred KB instead of tens of MB, and anything missing falls through the standard `Inherits` chain to Adwaita and then hicolor. A gap is a mismatched glyph, never a missing one.

- The four Windows icon sets are the project's own artwork. No cleanly-licensed set of any Windows generation exists, and what circulates is Microsoft's shell art extracted and repackaged, which this project will not ship - and draws blue folders besides, which Windows has never had. Every other bundled theme is an upstream open-source theme, unmodified apart from the trim, keeping its own license file and a pinned source commit.

- Themes can be dropped in on any platform by putting an ordinary GTK theme folder in `themes` or an icon theme in `icons` beside the settings file. Drop-ins are searched before the bundled set, so a same-named theme shadows it.

- The bundled set lives inside the binary rather than as files beside it. It was a couple of thousand small files, and the Windows single-file build was spending nearly all of its startup unpacking them, since the cost there is per file rather than per byte. As one compiled-in resource it costs a few MB of binary and nothing at launch. The trade is that a bundled theme cannot be edited in place, which is what the drop-in folders are for.

- The two link overlays are the app's own art rather than the theme's. A shortcut and a symlink have to read differently at a glance, and most icon themes draw the same arrow-in-a-box for a symlink that Windows draws for a shortcut. An icon added by resource path is only searched after every installed theme, so overriding one by name is not possible; both carry their own names and ship with the app. A shortcut gets the arrow, a symlink or junction a chain link.

- A shortcut to a folder wears the theme's folder icon. Everything else about a shortcut's icon comes from the shell, since only it can find a program's own artwork, but for a folder that answer is Microsoft's folder drawn among the theme's, which reads as a mistake. Whether the target is a folder comes from what the shortcut file records rather than from looking at the target, since a shortcut to a share that is not answering would otherwise stall the listing.

### Platform integration

#### Paths and desktop settings

- Both `/` and `\` work in typed locations on every platform, without reserving `\`. On Windows both are already native. On POSIX `\` is a legal filename character - files created over SMB shares really do contain it - so it is not reserved and no escape syntax is introduced. Typed input is normalized by fallback instead: the literal path is tried first, and only if it does not resolve is a `\` to `/` retry attempted. Pasted Windows paths work and real backslash filenames keep working.

- Desktop settings schemas are optional at runtime. Upstream read several Cinnamon and GNOME schemas that only exist on those desktops, and a missing schema is a hard abort in GLib. The app now looks a schema up before opening it, prefers the real one wherever the session provides it, and uses its own value everywhere else. Cinnamon integration is preserved and every other environment starts clean.

- Virtual locations - network, computer, trash - are shown only where the running platform actually supports them, extending the runtime scheme check the codebase already had.

#### On Linux

On Linux, gvfs stays an optional runtime dependency. It turned out to be desktop-agnostic rather than a Cinnamon thing, a freedesktop and GIO service present on virtually every desktop, so where it is there it provides network shares, trash, mtp and sftp, and where it is not the affected entries hide themselves. What it used to provide that is now ours everywhere is per-file metadata, which moved to the app's own store; see [Configuration and persistence](#configuration-and-persistence).

#### On Windows

On Windows the gaps are filled natively rather than by porting gvfs:

- Deleting to the Recycle Bin, and browsing it in-app to view, restore and empty.

- Network browsing enumerates the Windows network neighborhood. UNC paths are ordinary paths and need nothing special.

- Fixed drives are first-class sidebar roots with a disk-usage bar each, replacing the single Unix filesystem root, which means nothing there. Removable, optical and network drives stay on the normal devices path, since that path carries eject and unmount.

- Per-type file icons are derived from the file's content type, because the platform's file layer reports one generic icon for nearly every file.

- "Local only" means local there too. The preferences that trade speed for detail - item counts, thumbnails - default to doing the work only for local files, and a share is native as far as the toolkit is concerned, so those defaults used to sail straight past one. A folder holding a link to a host that was not answering paid twenty to fifty seconds per link with the whole folder waiting. A share, and a link pointing at one, now count as remote.

- File associations are read from the registry and never written to it. Windows keeps the per-user default under a hash a program is not meant to set, so "Set as default" used to fail outright. The choice is kept in the settings file instead: one line per type, a command line with `%1` for the file, in the shape the registry itself uses. The map is consulted first and the registry answers for everything else, through the same query Explorer makes, so the open verb comes back rather than a print one.

- The app never starts another program itself. The single-exe build carries its whole runtime inside it, and anything it starts inherits that view of the disk along with the rest of the environment, which is not the machine the other program expects. So the desktop is asked to do the starting, for every launch rather than only the ones worked out here. The programs offered under "Open with" come from the toolkit but carry the same registry command line, so they go the same way. A store app has no command line and is left to the toolkit.

- The clipboard and outbound drags are the app's own rather than the toolkit's. The toolkit only puts its own target names into a drag, and nothing outside it reads those; the one format every Windows program does read has no name to register it under, so it cannot be added from outside. A drag now carries what Explorer's own drags carry, with the app's own formats riding alongside, so drops back into our own window behave exactly as before. One switch turns the whole thing off and puts every drag back on the toolkit's. Control copies and shift moves, following Windows, read from the keyboard directly because the toolkit reports the same suggested action either way.

- "Open in terminal" and "open elevated" map to native equivalents. On Windows that is the native console - Windows Terminal, then PowerShell, then cmd - opened at the folder, and an elevated relaunch through the ordinary UAC prompt, labeled "Open as Administrator". On Linux it is the configured terminal and a pkexec relaunch, labeled "Open as Root".

- A copy running elevated cannot be dropped on at all. Windows refuses to let an ordinary program hand anything to an elevated one and there is no way to accept it from this side. Dragging out is unaffected.

## Quality

### Speed, memory and size

No hard budget is set yet. The figures below are where things stand, and a change that makes one of them noticeably worse needs a reason. The profiler stage of the pipeline is where to look first. See [The pipeline](#the-pipeline).

Measured on 2026-09-20 with the Linux release build on a desktop machine. Each is the median of several launches into a folder of empty files, in list view, with no saved settings. `NEMO_BENCHMARK_LOADING` prints the two times.

| Folder          | Window up and listed | Settled | Peak memory
| :---            | :---                 | :---    | :---
| empty           | 0.4 s                | 0.2 s   | 89 MiB
| 1,000 files     | 0.5 s                | 0.3 s   | 92 MiB
| 10,000 files    | 1.4 s                | 1.2 s   | 104 MiB
| 50,000 files    | 5.2 s                | 5.0 s   | 158 MiB

- "Settled" is when the view has gone idle, with icons and column widths done.

- Listing time grows about in step with the file count, at roughly a tenth of a millisecond per file. That is the one to watch. A big download or photo folder is where it is felt.

- The 10,000 and 50,000 rows were about four times these numbers until two changes to the measuring, both covered under [List view column widths](#list-view-column-widths): the list was put into fixed-height mode, and each column now remembers the width of a value it has already laid out. Peak memory came down with the first and did not move with the second.

- What is left at this size is still measuring, but now it is the Name column, where no two values repeat. A folder with varied names, sizes and dates rather than empty files runs about half again as long as the table above.

- On Windows the packed exe took 3.4 s to start on 2026-08-19. It had been 14.2 s, nearly all of it the packer handling a couple of thousand small theme files before any of our code ran, until the themes were compiled in.

- Size: the Linux drop is 43 files and 3.4 MB, 3.1 MB of it the program. The packed Windows exe is about 38 MB, most of it the GTK runtime.

### Security

The line that matters is the user account. The program runs as the person using it and can do what they can do, no more. It does not try to protect them from their own other programs: anything that can write the settings file, drop an action in the actions folder or talk on their session bus can already do what it likes as that user.

What comes from outside that account is treated as hostile:

- File names can hold any bytes a filesystem allows, and are shown and passed on without being interpreted.

- An archive does not get to say where its contents go. A stored path that is absolute, names a drive or climbs out with `..` is brought back inside the folder picked. See [File operations](#file-operations).

- The parsers that read text from outside - the settings file, drag data from other programs, the command lines in the config - are fuzzed. See [Testing](#testing).

- A share is not trusted to answer quickly. The per-file questions that would wait on one are skipped for files on a share, so a host that has stopped answering does not hold up a listing.

What it does not do:

- It opens no network connection of its own. No update check, no usage reporting, no crash upload. The only web addresses it shows are the project links in About and `--about`.

- It never elevates itself. Open as Administrator on Windows and Open as Root on Linux start a new copy through UAC or pkexec, which ask in their own right. The copy that asked stays as it was.

Other programs on the session bus can reach one interface, the freedesktop one, which only shows folders and properties. Nothing on the bus can copy, move, trash or delete. Nemo's own interface for that served its desktop, and it was removed.

Extensions and actions run with the user's rights. An extension is loaded into the process and can do anything the program can, so one is only worth installing from someone trusted with that much. A command line kept in the settings file is run as written.

An archive password is handed to the archiver as a value and never written to disk, but it shows in the process list while the archiver runs. That is true of every archiver that takes one on the command line.

A crash report holds the version, what killed the program and a list of addresses with the modules they fall in. It holds no file contents and no names from the folder being viewed. A module path can include the user name, when the program was installed under home.

Report a security problem privately, as [contributing.md](../contributing.md) describes.

### When it crashes

A crash leaves a report. Without one there is nothing to work from: a windowed program on Windows has no stderr, so it used to disappear off the screen and that was the whole story.

- The report goes to `crash/`, beside the settings file. It gives the version and build, what killed the program and where, and the stack. The name carries when the run started, since working out the current time is not something the code can safely do at that point; the file's own timestamp is when it died. When a process id comes round again within the same second, the second report gets a number on the end rather than being lost.

- The same text goes to stderr, which is where a launcher log keeps it. Windows gets a message box as well, because a windowed build has no stderr for anyone to read.

- Frames are addresses, not names. The Windows build carries no debug database and the released Linux build is stripped, so a frame is only worth anything alongside the build it came from. Windows reports each frame at the address it was linked at, which is what `addr2line` takes directly. Linux writes the module, an offset in parentheses, and the address it happened to run at in brackets; the bare `+0x...` in parentheses is the one to hand over, and where a symbol name stands beside it instead the frame is already named.

- On the way out a fault is left to happen again with no handler in place, so a core file and an attached debugger both stop on the fault itself. A signal sent by something else has no fault to repeat, so that one is raised again from the reporter.

- The handler stays off the allocator, since a crash inside it is one of the cases to survive, but it cannot avoid locks altogether: reading a symbol takes the loader's on Linux, and naming a module takes it on Windows. What it does avoid is the worst of them - the Windows side walks the stack with the operating system's own unwinder rather than the symbol library, whose first act is to enumerate every loaded module.

- What is known for certain is written before the stack is collected, since walking a broken stack can fault again. A crash on a worker thread that runs out of stack is the one case that still reports nothing: the reserve that lets the handler run at all belongs to the thread that installed it.

- Reports do not pile up. The oldest are dropped at startup, and the first run after a crash notes in the log that one was left behind. On Windows that log line goes nowhere in a windowed build, which is what the message box at the time of the crash is for.

- `NEMO_NO_CRASH_HANDLER` installs nothing, and `NEMO_NO_CRASH_DIALOG` keeps the report while dropping the message box, for anything running unattended.

### What it logs

Not much, by default. Warnings and criticals go to stderr. Started from a desktop menu on Linux, that usually ends up in the session log or the journal. A windowed build on Windows has no stderr at all, which is why a crash there also puts up a message box.

- Every trash, delete and empty trash writes one line saying what was taken and what asked for it, and so does every refusal. On Linux the same line goes to the system journal, since a log file under home is the first thing lost when home is. `journalctl -t nemo-anywhere` shows them.

- `G_MESSAGES_DEBUG="Nemo Anywhere"` turns on the program's own debug messages, and so does `--debug`. That name is the log domain. `all` turns on everything, the toolkit's included, which is a lot.

- `NEMO_DEBUG` picks areas of the older debug output inherited from Nemo, by name and comma separated: Actions, Bookmarks, DBus, DirectoryView, File, IconContainer, IconView, ListView, Mime, Places, Preferences, Previewer, Search, Thumbnails, Undo, Window, or `all`. It prints through the same log domain, so it needs `G_MESSAGES_DEBUG` too. It also makes a warning or critical stop in an attached debugger.

- `NEMO_DEBUG_IO` prints which fetch a folder is waiting on and for how long. It was written to find the one fetch costing twenty seconds on a share that was not answering.

- `NEMO_BENCHMARK_LOADING` prints the startup time and how long the first folder took to list and then to settle.

- Crash reports are their own thing, under [When it crashes](#when-it-crashes).

### Testing

Tests are ordinary executables run by meson, and the bar for adding one is a defect that could come back.

- Each regression test is written against a specific defect and is checked by backing the fix out and watching the test fail. A test that passes either way is not evidence.

- Coverage is concentrated where the risk is: the settings parser and its bindings, the metadata store, favorites, search patterns, drag-and-drop parsing, extension objects, symlink handling, and the Windows trash and shortcut backends.

- The parsers that read text from outside the program are fuzzed: the settings file, the drag payload, and the command lines kept in the config. Each target builds two ways. Ordinarily it replays a checked-in seed corpus as part of the suite, which keeps the target compiling and the seeds meaning something. With `-Dfuzzing=true` it builds against libFuzzer and the pipeline runs a real search for a bounded time per target, where the budget running out is a pass and a find leaves behind the input that caused it. The settings parser is vendored rather than ours, so a find there is a report upstream instead of a local patch.

- The suite runs headless, on a virtual display where GTK needs one, and forms part of the Linux pre-push gate along with the build, the lints and a launch smoke test. A test that cannot run on the current platform reports a skip, never a pass.

- Anything needing a real desktop - clicking a menu, driving a drag - runs on a private virtual display with a window manager in the Linux build container, and on Windows in a throwaway Windows Sandbox built from the host's own image, which has its own desktop and keeps no state. A window can also be photographed without disturbing anything, since it renders off-screen even when covered.

- Interactive behavior that no assertion reaches is verified by hand against a build kept on the desktop for daily use.

## Building

The reference Linux builds happen in containers rather than on a development machine, so the dependency versions are pinned and host library drift cannot quietly change the baseline. The Windows build is native.

### Building on Linux

Stock Debian 13 is the known-good baseline, in `cicd/linux/Dockerfile.dev` (image `nemo-build-deps`, container `nemo-build`). That file is the authoritative dependency list; the packages below are the same set spelled out for anyone building on their own machine.

- The pipeline makes that container on first use if there is none: it builds the image, then runs it with the repo mounted at `/src`, `--shm-size=2g` so parallel gcc has room, `--init` to reap stray processes, and `--ulimit core=0` so a crash leaves no core file in the tree. The release and cross-build containers are made the same way by their own scripts.

- Toolchain and development libraries: `meson ninja-build gcc pkg-config gobject-introspection intltool itstool python3-gi`, `libgtk-3-dev libglib2.0-dev libpango1.0-dev libatk1.0-dev libgail-3-dev`, `libjson-glib-dev libgirepository1.0-dev libgsf-1-dev libexempi-dev libexif-dev`, `libarchive-dev libsqlite3-dev`, `libx11-dev libxext-dev libxrender-dev`.

- `clang` and `llvm` are only needed to build the fuzz targets against libFuzzer with `-Dfuzzing=true`. Everything else builds with gcc, and without the option the fuzz targets still build and replay their seed corpus as ordinary tests. See [Testing](#testing).

- Configure and build:
	- `export SOURCE_DATE_EPOCH="$(git log -1 --format=%ct)"`, so the build is reproducible. See [Reproducible builds](#reproducible-builds).
	- `meson setup build source`
	- `ninja -C build`

- The binary is at `build/src/nemo-anywhere`. There is no second desktop-drawing binary.

- `-Dextension_library=static` builds the extension API into the program, the way the release does. The default, `shared`, installs it as a library with headers and a pkg-config file for extensions to build against.

- The action layout editor is a separate PyGObject script rather than part of the program, so at run time it wants `python3-gi`, `python3-gi-cairo` and `gir1.2-gtk-3.0`. Nothing else needs them, and without them only that one window is missing.

Release builds do not use this container. They are built against an older glibc, for reasons under [Release artifacts and packaging](#release-artifacts-and-packaging).

### Building on Windows

The Windows build is native, not cross-compiled: MSYS2 with the mingw64 GTK3 toolchain, which is what both the Windows development box and the hosted release workflow use.

- `pacman -S --needed mingw-w64-x86_64-{gcc,meson,ninja,pkgconf,gtk3,json-glib,libarchive,libexif,libgsf,cppcheck,gettext} intltool git`, then `meson setup -Dxmp=false build source` and `ninja -C build`.

- Enigma Virtual Box is needed only for the single-exe artifact. Without it everything still builds, tests and stages, and only the packing step skips.

A cross-compile lane also exists, for checking a Windows build from the Linux box without Windows hardware. It is a developer convenience rather than part of the pipeline, since only Windows can pack the single exe.

- `cicd/win/fetch-sysroot.bash` resolves the dependency closure of a few root packages from the MSYS2 pacman database and unpacks each one into a sysroot. No pacman is needed, since the package database is a tarball of description files.

- `cicd/win/Dockerfile` builds the `nemo-winbuild` container: the mingw toolchain, the native GLib code generators that have to run on the build host, wine, and the baked sysroot. `cicd/win/win64.cross.txt` is the meson cross file, with wine as the exe wrapper.

Deliberately off for Windows either way: XMP and exempi, which are not packaged for mingw, and the Unix-only pieces (`gio-unix`, `x11`, SELinux, Tracker), which are guarded in meson by `host_machine.system()` and in the affected C files by `#ifdef`.

## Delivery

The guiding constraint is that the git host is dumb hosting plus release storage, with as few third-party tools as possible. The whole pipeline runs locally, from `cicd/cicd.bash` on Linux and `cicd/cicd-win.ps1` on Windows.

The one deliberate exception is a release-only workflow, `.github/workflows/release-win.yml`, which builds, packs and publishes the Windows exe on a release tag. It exists because the code signing service chosen at the time would only sign artifacts from a verifiable public build. That application was refused and signing is deferred, so what the workflow earns its keep for now is being that public build, with the signing step left dormant behind a token gate.

### Branches and the merge gate

- Feature branches merge `--no-ff` into `dev`, the integration target. `main` is release-only, and merging dev into main is what cuts a release. Nothing is committed directly on either.

- The merge gate is `cicd.bash --gate` running as the `pre-push` hook, for pushes to main only. A push to dev is not gated, since each chunk is built and tested before it is merged there. It is the local stand-in for a hosted CI workflow: lints, then a container build, then the test suite, then a headless launch smoke test. Install it per clone with `cicd/hooks/install.bash`; override a run with `git push --no-verify` or `SKIP_GATE=1`.

- The Windows gate runs the same lints, build, test suite and smoke test.

- `--quick` skips the slow stages: the cross build, packages, the profiler, screenshots and the demo. The native build, the full suite and dogfood still run. On Windows `-Quick` changes nothing yet, since none of those run there.

- The same hook blocks a push to main unless `source/meson.build` is a strict version increase over what is already there.

### Versions and build numbers

- `source/meson.build` is the only place the version is written. Everything else reads it.

- The fork numbers its own releases from 1.0.0, independent of the 6.6.4 code baseline. Since 6.6.4 was never tagged or released here, that reset was a clean one-time step.

- Every build also carries a build number: minutes elapsed since the start of 2000, in lower-cased Crockford base32, which drops i, l, o and u so nothing reads as a digit by mistake. Five characters until 2063, six after. It sits beside the version in `--version`, `--about`, Help > About, the Windows splash screen and the release notes, so a bug report names not just which release but which build of it.
	- It is worked out at configure time from `SOURCE_DATE_EPOCH`, so two builds of one commit carry the same number. Without it the number comes from the commit date of HEAD, and only failing that from the clock.
	- It lives in a generated header of its own rather than in `config.h`, because the number moves on every reconfigure and a change in `config.h` rebuilds the whole tree.

### The pipeline

Stages, in order, each self-skipping when unconfigured: remote sync, format, debug build, tests and lints, profiler, release build, packages, dogfood, backup and publish. Disabled on purpose today are the format stage, since there is no in-place C formatter worth running, and the engine's own release collector, because the per-platform release lanes write those artifacts themselves.

- Remote sync runs first for a reason. The publish stage pulls at the end, so without it a change merged remotely mid-run would be pushed having never been built or tested. It fast-forwards when the branch is only behind and stops the run outright when it has diverged. It is skipped in gate mode, since a pre-push hook must not rewrite the tree underneath the push that called it.

- No stage is allowed all the cores. Build parallelism is capped at half of them, so a full run leaves the machine usable.

- The publish stage refuses a dirty tree, checked once at preflight and again before it runs. It commits everything it finds, and nothing there can tell work in progress from a finished change.

- Profiling browses a generated folder tree on a private headless display while sampling every thread, then renders a flamegraph and prints the hot spots into the run log. It samples by attaching a debugger rather than using perf, because perf needs a privileged sysctl here and a profiler that cannot run without root is a profiler nobody runs. The cost is wall-clock samples, so a blocked thread reads as work; the report keeps waiting in its own bucket and gives every figure as a share of busy time as well as of total. It profiles the debug build, since the release binaries are stripped and a flamegraph with no function names says nothing.

- The last stage archives the project tree into a rotated set of backups and then commits and pushes the current branch. The archive keeps what would be painful to lose - source, docs, the pipeline, assets, release builds and their packages - and drops what a command regenerates, chiefly the staged Windows runtime snapshot, which by itself took each archive from about 1.6 MB to 36 MB.

### Reproducible builds

Nothing a build produces takes its timestamp from the clock. Every lane sets `SOURCE_DATE_EPOCH` to the commit date of what is being built, so the same commit builds to the same bytes on any box on any day and a released Linux artifact can be checked against a rebuild of its tag. The Windows exe cannot be yet: its hosted build installs whatever MSYS2 packages are current that day.

- The Windows exe was the one that actually differed run to run. The linker writes a timestamp into the PE header, and left alone it writes the clock: two clean builds of one commit used to differ in exactly those four bytes.

- The strip rewrites the field too, so whatever strips a shipped exe has to carry the stamp as well as whatever linked it. Both Windows lanes were missed here, one at a time: the linker was given the stamp and the strip that ran after it put the clock back. Whichever step writes the file last is the one that decides.

- Each lane checks the stamp on the file it actually goes on to pack, not on an earlier copy of it. The cross lane checks twice, once after the link and once after the strip, so a failure says which step caused it.

- The linker, `dpkg-deb` and `rpmbuild` read the stamp themselves. `zip` has no such notion, so its input is stamped on disk and fed in sorted order, and `tar` is given the stamp and a sorted order explicitly.

- One script answers what the stamp is, and every local lane calls it rather than working it out again. The hosted Windows release reads the same commit date inline. `docker exec` does not carry the host environment into a container, so each lane hands it over explicitly.

- A tree with uncommitted changes still gets its `HEAD` commit's date, since the alternative is the clock, but the release lanes warn, because nothing built from it can be reproduced.

- Left out on purpose: the wall clock still names log files and dated dogfood copies, which is what it is for. A signed exe can never be byte-identical anyway, since the countersignature carries the real time of signing.

### Release artifacts and packaging

The two platforms get deliberately different artifacts, because what a user already has installed is different.

Linux is a thin relocatable prefix of a couple of MB that uses the distro's own GTK3. A bundled GTK on Linux is the thing that goes stale and mismatches the desktop's theme, portals and input methods, and it would multiply the download for no gain.

- It is built in an Ubuntu 22.04 container, never the day-to-day Debian 13 one. A binary's glibc floor is whatever it was built against, so a release built on Debian 13 would refuse to start on anything older than 2025. The floor is therefore glibc 2.35 and GTK 3.24.33, which reaches Ubuntu 22.04, Debian 12, Mint 21 and Fedora 36 onward.

- What makes it relocatable: the program works out where it is and points `XDG_DATA_DIRS` and `PATH` at the folder it sits in, at startup, before anything reads them. The extension API is inside the program, so there is no `lib/` folder to find. Everything looked up through the XDG data dirs - actions, search helpers, icons, mime info - then resolves wherever the folder was installed. There used to be a shell wrapper in `bin/` doing that with the real binary hidden in `libexec/`; two files where one would do, so it went.

- The D-Bus activation file is written at startup rather than installed, into the user's own service directory. It has to name an absolute path, and a portable copy does not have one until it runs.

- Staging leaves out what only a system install would read: mime data, polkit, man pages and the editor syntax files. Both packages install the prefix under `/opt`, where none of it is read, and the install rules still produce all of it, so a distro building `--prefix=/usr` is unaffected. Icons are compiled in except the app icon at its eight sizes, which packaging and the menu entry need as real files. Actions, search helpers and the settings schema stay as files, since those are the drop-in folders a user edits and Preferences has buttons that open them.

Windows is one self-contained `nemo-anywhere.exe` with the whole runtime packed inside it by Enigma Virtual Box, as an in-memory virtual filesystem with nothing extracted at run time. No library folder, no launcher, nothing installed or registered: an exe to copy anywhere.

- The pack source is the same flat layout the zip uses - exe and dlls at the root, `lib/`, `share/` and `etc/` beside them - and GLib-stack libraries resolve their data relative to their own dll, so that tree also runs unpacked with a bare double-click.

- Packed exes are occasionally false-flagged by antivirus, so the plain zip stays available as the fallback artifact. It is also the fallback for the release being unsigned.

Packaging builds from what the release lanes already produced and never rebuilds. The Linux tarball becomes a `.deb` and an `.rpm`, both installing the same relocatable prefix under `/opt` plus a launcher, a menu entry and icons in the shared theme. The `.deb`'s dependency versions are read off the built binaries inside the release container rather than on a development box, so the package claims the floor the binary was actually built against; `rpmbuild` derives its own from the ELF. BSD, macOS, AppImage and Flatpak wait on a toolchain.

Cutting a release tags `v<version>` from a clean main and uploads the artifacts. Release notes are the hand-written changelog section for that version, never a generated commit list, falling back to generated notes only so a release is never published blank. A version carrying a pre-release part is published as a prerelease, which matters to the installers: their stable channel takes the newest release with no prerelease part, and the newest prerelease only while no stable release exists.

### Installing

`install.bash` and `install.ps1` sit at the repo root and run as one-liners straight from a shell. They are two standalone installers rather than one script with a helper: the bash one targets bash 3.2 so stock macOS runs it, the PowerShell one covers unix itself instead of handing off. The duplication is deliberate, and buys a one-liner that works from whichever shell someone already has open.

- The app installs as a whole folder plus the two things that make it reachable: a menu entry and a name on PATH. A file manager gets launched both ways.

- A user install is the default and needs no privileges. A system-wide install is opt-in and is the only path that escalates, which it states in the plan first.

- The OS and architecture are detected, not asked for. Every run prints what it is about to do and waits for a yes. Downloads are checksum-verified before anything is unpacked, so a bad download can never replace a working install. Reinstalling replaces in place, and `--uninstall` removes exactly what was added.

- Because they read the releases page, the packaging stage has to produce exactly these names: `nemo-anywhere-<version>-<os>-<arch>.tar.gz` for unix and `.zip` for Windows, with `<os>` one of `linux` or `windows` and `<arch>` one of `x86_64` or `arm64`, plus `nemo-anywhere-<version>-sha256sums.txt` beside them in `sha256sum` format. The portable Windows exe is `nemo-anywhere-<version>-windows-x86_64-portable.exe`, built by the hosted workflow, which adds its line to the same sums file. Each archive holds one top-level folder, whose entry point is `bin/nemo-anywhere` on unix and `nemo-anywhere.exe` at the root on Windows.

### Dogfooding

Each platform's pipeline publishes one build to a shared drop folder for that platform and writes nothing else there: the whole relocatable prefix on Linux, the packed exe on Windows.

- The launcher owns the local side. It copies the drop into a pool of date-stamped versions and points a symlink at the newest, so the fixed name is the only thing anything else has to know. One script serves all three platforms, with a small wrapper per platform in the place that platform looks for commands, so it can be typed at a shell or named in a `.desktop` file.

- The pool is rotated on every launch: the newest of each finished hour, day, week, month and year, the most recent few, and the first build ever held. On top of that a budget of at most ten versions, at least five, and only as many between the two as fit in 1 GB. Nothing a running process lives inside is removed, so going back to an older build is a matter of running it rather than rebuilding it.

- A build already held is settled on its bytes, not its date. The sync layer restamps what it carries, so a date test on its own re-fetched the same build every run.

## Open questions

- How far to push a clean internal platform-abstraction boundary, against per-target `#ifdef`s in the shared files. Both conventions are in the tree today.

- Whether a fractional display scale should drive widget sizing and spacing through a stylesheet of the app's own, since the toolkit will only scale in whole steps.
