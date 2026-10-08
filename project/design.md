<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere design

What the project is for, and the decisions behind it. Companion to [backlog.md](backlog.md), which tracks the work itself. Larger features have a design doc of their own under [design_docs/](design_docs/), and the sections here link to them.

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
	- [Handlers on settings groups](#handlers-on-settings-groups)
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
	- [Building on FreeBSD](#building-on-freebsd)
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

### Handlers on settings groups

The table below is the rule for a handler connected to one of the settings groups declared in `nemo-global-preferences.h`. The groups last as long as the process. A view, a sidebar or a window can be torn down long before that, and a view can then stay in memory a while longer, held by an unmount, an eject or a rename that is still running. A handler left on a group past teardown is called with widgets that are already gone the next time the setting changes, and live reload makes a change an ordinary event. The same mistake was fixed in the places sidebar, in a test and in the list view, each a different way, so the answer is kept here once.

| Case                                                                                                                                           | Answer
| :---                                                                                                                                           | :---
| The data is an object that can go before the process ends: a view, a widget, a window, a dialog page, or a helper object such as the job queue | `g_signal_connect_object` with that object, with `G_CONNECT_SWAPPED` where the callback takes it first. No disconnect anywhere. The handler goes when the object is disposed.
| The callback works on a widget the owner holds, rather than on the owner                                                                       | `g_signal_connect_object` with the widget it works on, as the row hover tint does with the tree view.
| The data is `NULL`, or the address of a file-level static                                                                                      | Plain `g_signal_connect`, once per process, never disconnected.
| The data is the address of a local variable, as in a test                                                                                      | Plain connect with the id kept, and `g_signal_handler_disconnect` on the same group before the function returns.
| The data is a plain struct, not an object, freed when a widget goes                                                                            | Plain connect, and `g_signal_handlers_disconnect_by_data` on the same group in the function that frees the struct, run from that widget's `destroy`.
| `nemo_config_bind`                                                                                                                             | Used as it is. The binding goes when the object is finalized, and it only sets a stock property on the object itself, which is safe on a widget already torn down.
| A handler on some other object that outlives the receiver, such as a `NemoFile` or a monitor the whole process shares                          | As the first row.

- `g_signal_connect_object` was chosen over a disconnect in dispose, and over one in finalize.
	- Finalize is too late. The widgets inside are freed at dispose, and a held view is not finalized until the hold is let go. That is how the list view's row shading handler reached a freed tree view.
	- A disconnect in dispose works, but it names the group a second time, and the two names drifted apart in the sidebar and in a test. With `g_signal_connect_object` there is no second name to get wrong and no teardown line to forget.
	- GLib removes such a handler from the group once the object's dispose finishes, rather than only no longer calling it. That holds from GLib 2.72, the oldest the release build is made against, through the one in the Windows build.

- `cicd/utility/lint-pref-handlers.py` checks every row for the declared groups except `nemo_config_bind`, which needs no check. The last row has no check, since nothing in the source says how long some other object lives.
	- It also checks each key against the group the settings table puts it in. A handler on a group that does not have its key is never called, and nothing says so.

### One process per window

Each window is its own process by default, and every launch is a fresh one. A crash then takes one window rather than all of them, and two versions can be open side by side, which is what trying a build next to the one in daily use needs.

- The copies still find each other. Each queues on the one bus name, so a caller from outside always reaches the oldest. That is how `--quit` and Close All Windows reach every copy, how `--reset` knows one is running, and how a tab's menu lists the windows of other copies.
	- The list of copies comes from numbered slot names. Each copy takes the first free slot and queues on every slot below it, so the slot of a copy that ends passes up to a live one and the taken slots never have a gap. Asking who owns each slot in turn, up to the first free one, finds every copy.
	- The queue itself is not read, since GLib's bus on Windows answers both ways of reading it with an empty list.

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
	- A save reads the file first. A hand edit the app has not picked up yet is taken in, and a setting changed both ways keeps the change made in the app, the same as when the edit is picked up first. A file removed by hand goes back to defaults, apart from changes made in the app and not yet saved.

- The file ends with SHCL's info block, which names the format it was written in. A file in an older format is copied beside it as `settings_backup_<YYYYmmDD-HHMMSS>_format-v<N>.shcl`, and a new `settings.shcl` is written with the settings this release knows, converted by SHCL. A file with no format line is read as 2.x at startup, since every file a 2.x release wrote has none, and it is rewritten when today's rules would read it differently. The cost is a hand-written file the app never saved, which has no info block either: an unquoted backslash in it, as in `C:\temp`, is read the 2.x way, and the file as written is in the backup. A file with no format line that turns up while the app is running is a hand edit and is read by today's rules, unless the 2.x file from startup could not be saved over yet. A file in a newer format is used but never saved over, so running an older build does not undo a newer one's settings.

- Types, defaults and allowed values live in one table in the code, and a matching schema sits beside the app so `shcl check --schema` can catch a typo in a hand-edited file. Keeping defaults central is deliberately against the config library's own per-call-site advice: with nearly two hundred settings, many read from several places, two call sites disagreeing about what a setting means when absent is a silent bug.

- A handful of settings are the desktop's to decide rather than ours: which terminal to open, whether the session remembers recent files, 12h or 24h clocks. Where a desktop publishes them we read its answer, and everywhere else our own value stands in. That is the only remaining use of the desktop settings database, it is read-only, and it never touches a schema of ours.

- A few settings are file-only, with nothing in Preferences.

- Keyboard shortcuts are kept beside the settings in `accels`, in GTK's own format. Upstream Nemo keeps its shortcuts in `~/.gnome2/accels/nemo`. The first start with no `accels` reads that file, so custom shortcuts carry over, and writes `accels` straight away. That `accels` exists is what marks the old file as read, so it is read once and never written. `--reset` empties `accels` rather than removing it, for the same reason.

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

The file cache is the fourth store. It is a private SQLite database under the user's cache folder, with what has been worked out about files on disk, so it does not have to be worked out again. Thumbnails are the first thing in it and the reason it exists, but its tables are about files rather than pictures.

- The tables, checksums written onto files, and pruning are in [20260930-145641_file_cache.md](design_docs/20260930-145641_file_cache.md).

- How thumbnails are made, stored, ordered and shown is in [20260925-063617_thumbnails.md](design_docs/20260925-063617_thumbnails.md).

### File operations

#### Trash and delete

Trashing and deleting are the two things a file manager cannot take back, so they are held to a higher bar than the confirmation preferences alone. Home and mounted drives are never removed, a link is always removed as a link, nothing outside a window can remove anything, every question starts on Cancel, and every trash and delete is logged. A stricter test guard asks about every delete in pre-releases.

- The rules, the test guard and their history are in [20260930-150859_delete_guard.md](design_docs/20260930-150859_delete_guard.md).

#### Links

Copying a link asks what should be at the far end, once per operation, on every platform. A move always takes a link as the link. Make link and Edit link cover every kind of link each platform has, shortcuts included. Copies are clones where the filesystem allows.

- Copying, moving, clone copies, and making and editing links are in [20260930-145641_moving_and_copying.md](design_docs/20260930-145641_moving_and_copying.md).

#### Archives

Archives are written by libarchive, with the `7z` and `rar` commands as optional extras rather than the primary route. 7z is the exception: it goes to 7-Zip where that's installed, since the library writes 7z on one thread. Encryption and splitting are requirements, and a job that nothing installed can do is refused rather than written weaker. Unpacking reads far more formats than writing does, and never lets an archive write outside the folder picked.

- How it works today, and the planned Compress dialog reset with its link choices, size totals and the split of the archive code from the rest of the app, are in [20260929-101432_compression.md](design_docs/20260929-101432_compression.md).

### Search

- Content search converts documents itself, in C, on libraries the app already links. The old helpers were a Python script, a shell script and a LibreOffice call, none of which exists on a stock Windows machine and each a dependency the install could not promise. Word, Excel and PowerPoint in both their old binary and newer zip-of-xml forms, OpenDocument and EPUB are covered. The definition-file mechanism stays, so a helper for anything else can still be dropped in.

- Results can be grouped under the folder holding them. It is a heading row per folder that actually has a match, labeled with the path under the folder searched, rather than a full tree of every folder in between - a tree puts rows on screen for folders with nothing in them, and reading that path off one row is what a person actually wants. The heading rows are built by the view rather than the model, so a folder nobody asked to open is never read, monitored or walked. Flat is still the default and switching redraws from the results in hand rather than searching again.

- Find results keep their own list columns, the order they were dragged into, and their sort column and direction, whatever "Remember per-folder settings" says. They are not a folder, so there is no folder to keep them with. They also never touch the sort a folder uses, so leaving find mode gives the folder its own sort back. With nothing picked they show Name, Ext, Size, Date modified and Location, in that order. Type and the other two dates are hidden. With no sort picked they take the default sort order, as a folder does. Reset view and the column menu's Use default put back both the columns and the sort.

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

This rule has been rewritten several times and will probably move again, so the whole of it is here rather than spread between the code and a summary. This should be treated (and updated) as THE canonical, precise, complete, conflict-free definition. It describes where the behavior is going, so where the code differs it is the code that moves. The code has matched it since 2026-09-17. The arithmetic, and how each column counts what it has seen, is in `nemo-column-layout.c`, which knows nothing about widgets and can be tested without a screen; the measuring that feeds it is in `nemo-list-view.c`.

- There are three "classes" of columns, for width sizing:
	- The minimum column width that overrides all minimum-width definitions below: Column header text.
	- Primary variable-width class:
		- Members: "Name", "Location".
		- Min width:
			- What will display all of the shortest N% values, as set by `list-view.column-fit-percent`, default 90.
			- Name counts every file in the folder, since every name matters.
			- In find results one name can turn up in many folders, so there Name counts each distinct name once, like Location.
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

- Properties is the platform's own on Windows, with ours under Ctrl+Enter for what the shell sheet has no room for. The other platforms use our window throughout. See [20260930-150859_properties_dialog.md](design_docs/20260930-150859_properties_dialog.md).

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

- A shortcut to a folder sorts with the folders when folders come first, on every platform. Whether its target is a folder is read from the shortcut, the same as its folder icon, so the two always agree. A shortcut whose folder has since gone keeps both until it is opened, since checking the target could stall on a share that is not answering. For the same reason a shortcut that sits on a Windows share is not read for either, and sorts with the files. Only a regular file is read, so a pipe or device that happens to end in `.lnk` cannot hold up the listing.

#### Scaling and startup

- Scaling is the app's own job, not something done to it. The window declares itself per-monitor DPI aware, so a scaled display gets it drawn at that scale rather than drawn small and stretched, and moving it to a monitor at another scale redraws rather than restretches. The toolkit scales in whole steps, which leaves 125% or 150% short, so text is sized against the monitor's true DPI on top of that. On Windows the icons named at one of the toolkit's fixed sizes are asked for at that fraction too, so at 150% a 16 pixel icon is drawn at 24 and stays sharp. Padding, borders and the height of a text field or button come from the theme, and also stay on the whole step. GTK 3 cannot scale a theme by a fraction, and rewriting a theme's sizes would fight the look it was drawn for. At 150% that leaves rows that follow the text about a tenth short, and fixed heights a third short. File icons in the views keep the size the zoom gives them. On Linux and BSD the desktop publishes its own scaling and the toolkit follows it. A font DPI there is a text setting, so icons stay as every GTK 3 app draws them.

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
	- The shell is asked off the window's thread. A folder of shortcuts lists at once with the plain shortcut icon, and each one changes as its own icon is found. What was found is kept only while the app runs.
	- A shortcut whose target, or the icon it names, is on a share gets the icon for the target's name instead, so the share is never visited: a document the icon for its kind, a program the plain program icon. Whether it is on a share is read from the shortcut file and from the drive letter's mapping, never from the share.
		- Windows records the share as well as the drive path when the target's drive is shared. That shortcut counts as local when the drive's volume serial matches the one it records. A shortcut made on another machine's drive goes by the target's name.

### Platform integration

#### Paths and desktop settings

- Both `/` and `\` work in typed locations on every platform, without reserving `\`. On Windows both are already native. On POSIX `\` is a legal filename character - files created over SMB shares really do contain it - so it is not reserved and no escape syntax is introduced. Typed input is normalized by fallback instead: the literal path is tried first, and only if it does not resolve is a `\` to `/` retry attempted. Pasted Windows paths work and real backslash filenames keep working.

- On Windows, text a user writes takes a Windows path as written. That covers action, search helper, thumbnailer and link files, and the command lines and paths in the settings. A backslash there is a path character, never an escape: nobody means `\n` or `\t` in a path, and a path read as escapes breaks without a word. A file already written with every backslash doubled still reads right. A command line splits on blanks, and double quotes group words. On Linux those files keep the freedesktop spec's escapes. Text only the app writes and reads may keep escapes.

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

- The app never starts another program itself. The single-exe build carries its whole runtime inside it, and anything it starts would inherit that. So the desktop is asked to do the starting, for every launch. See [Programs the app starts](design_docs/20260930-145641_windows_exe_packing.md#programs-the-app-starts).

- The session bus GLib starts when none is up is still GLib's, but it runs from a small `gdbus.exe` of ours, which sits where GLib looks for its own. GLib's bus never removes the file it writes in TEMP, so one was left for every bus started. Ours gives the bus a TEMP folder of its own and removes it when the bus ends. A bus that was killed leaves its folder, and the next bus removes it.

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

- The program never goes to a network share on its own. Only something a person does reaches one, such as going to a share or opening a link or shortcut that points at one. A share that is not answering can hold each question for about twenty seconds, so an icon, sort place or emblem for something on a share comes from what is on local disk, or stays plain.
	- On Windows a share is a UNC path or a drive letter mapped to one. On Linux and the BSDs it is a mount of a network file system, read from the mount table rather than asked. A link counts when it leads onto a share other than the one it sits on. The share holding the home folder counts as local, since the program works there from its first window on.
	- A folder on a share counts as one too, wherever it was reached from, so item counts and thumbnails there are off by default, as "Local files only" says.
	- On Linux and the BSDs a folder is listed by following each link in it, to show what it points at. A link onto a share is the exception. It is listed as the link itself, a plain link that sorts with the files, until someone opens it. Windows lists every link that way already and still gets its type.
	- On Windows the side pane names and draws a mapped drive from its letter and what Windows keeps for it locally, and the trash state asks only the local drives' bins.

- On Windows the packed exe took 3.4 s to start on 2026-08-19, down from 14.2 s. See [Startup time](design_docs/20260930-145641_windows_exe_packing.md#startup-time).

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

- Full pipeline runs build the whole suite again with AddressSanitizer and UndefinedBehaviorSanitizer, and run it with leak checks on. Any report fails the test that made it. Leaks inside the libraries under GTK are listed by library in `cicd/linux/sanitizers.supp`. A leak in our own code is fixed. One that has to wait is filed on the backlog, and the one test that shows it runs without leak checks, with the ID beside it. GTK animations are off in that run, since a CSS transition leaks inside GTK itself. The leak tests read the heap through glibc, which the sanitizer replaces, so they report a skip there. It is not part of `--quick` or the gate.

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

- Enigma Virtual Box is needed only for the single-exe artifact. Without it everything still builds, tests and stages, and only the packing step skips. See [20260930-145641_windows_exe_packing.md](design_docs/20260930-145641_windows_exe_packing.md).

A cross-compile lane also exists, for checking a Windows build from the Linux box without Windows hardware. It is a developer convenience rather than part of the pipeline, since only Windows can pack the single exe.

- `cicd/win/fetch-sysroot.bash` resolves the dependency closure of a few root packages from the MSYS2 pacman database and unpacks each one into a sysroot. No pacman is needed, since the package database is a tarball of description files.

- `cicd/win/Dockerfile` builds the `nemo-winbuild` container: the mingw toolchain, the native GLib code generators that have to run on the build host, wine, and the baked sysroot. `cicd/win/win64.cross.txt` is the meson cross file, with wine as the exe wrapper.

Deliberately off for Windows either way: XMP and exempi, which are not packaged for mingw, and the Unix-only pieces (`gio-unix`, `x11`, SELinux, Tracker), which are guarded in meson by `host_machine.system()` and in the affected C files by `#ifdef`.

### Building on FreeBSD

FreeBSD 15.1 on amd64 is the known-good baseline. The build is native, with the base system's clang, and there is no container, so the versions are whatever `pkg` has at the time.

- Toolchain and libraries: `pkg install meson ninja pkgconf python3 gettext-tools intltool itstool gobject-introspection gtk3 json-glib libgsf exempi libexif libarchive sqlite3`. Base has its own libarchive, but only the package has the pkg-config file meson looks for.

- Configure and build as an ordinary user:
	- `meson setup build source`
	- `ninja -C build`

- The binary is at `build/src/nemo-anywhere`, as on Linux. It builds with no warnings under `-Dwerror=true`.

- The action layout editor wants `py312-pygobject` at run time.

- BMP, ICO, XPM, XBM and PNM thumbnails want `gdk-pixbuf-extra` at run time. gdk-pixbuf has left those loaders out of a default build since 2.42.11, and FreeBSD's package follows that. Without it the app hands those formats to ImageMagick when that is installed, and otherwise they keep their plain icon.

- The test suite also wants `xorg-vfbserver xdpyinfo openbox ImageMagick7-nox11 7-zip`, plus `rar` for the rar cases. There is no `xvfb-run`, so `meson test -C build` runs with `DISPLAY` set to an X server of its own and `DBUS_SESSION_BUS_ADDRESS=disabled:`, as `cicd/linux/run-tests.bash` does on Linux. A test that needs a display to itself starts its own Xvfb.

- What differs from Linux:
	- The leak tests and the allocation count read glibc's heap, so they skip.

- The pipeline builds and tests on the FreeBSD box too, behind `--include-bsd`. `cicd/bsd/lane.bash` sends the working tree there over ssh under the host lock, and runs the same `cicd/linux/run-tests.bash` the Linux gate runs. Where there is no `xvfb-run` that script starts an X server of its own.

- The release is two files made from one build: a `-bsd-` tarball of the relocatable prefix, which `install.bash` fetches on BSD, and a `pkg` file of the same prefix.
	- The pkg puts the prefix at `/usr/local/nemo-anywhere`, where a system install from `install.bash` goes on BSD, plus the command in `/usr/local/bin`, the menu entry and the app icon. The icon goes only in the shared theme, since pkg's icon cache trigger leaves empty folders behind in the app folder on a delete.
	- Its dependencies are the packages that own the libraries the binaries link, read off the build box, plus `gdk-pixbuf-extra`. No base system package is named.
	- `pkg install ./nemo-anywhere-<version>-bsd-x86_64.pkg` fetches any dependency that is missing. `pkg add` installs it only when they are all there already.
	- pkg takes no dash in a version, so `1.0.0-beta2` is `1.0.0.beta2` there, which pkg sorts below `1.0.0`.
	- The build runs on the FreeBSD box, since only `pkg create` makes a pkg file. The tarball is packed back on the Linux box, where GNU tar gives it the same fixed stamp and order as the Linux one. The pkg's files have the same stamp.

## Delivery

The guiding constraint is that the git host is dumb hosting plus release storage, with as few third-party tools as possible. The whole pipeline runs locally, from `cicd/cicd.bash` on Linux and `cicd/cicd-win.ps1` on Windows.

The one deliberate exception is a release-only workflow, `.github/workflows/release-win.yml`, which builds and packs the Windows exe on a release tag and hands it back as a build artifact. It never makes or changes a release. It exists because the code signing service chosen at the time would only sign artifacts from a verifiable public build. That application was refused and signing is deferred, so what the workflow earns its keep for now is being that public build, with the signing step left dormant behind a token gate.

### Branches and the merge gate

- Feature branches merge `--no-ff` into `dev`, the integration target. `main` is release-only, and merging dev into main is what cuts a release. Nothing is committed directly on either.

- The merge gate is `cicd.bash --gate` running as the `pre-push` hook, for pushes to main only. A push to dev is not gated, since each chunk is built and tested before it is merged there. It is the local stand-in for a hosted CI workflow: lints, then a container build, then the test suite, then a headless launch smoke test. Install it per clone with `cicd/hooks/install.bash`; override a run with `git push --no-verify` or `SKIP_GATE=1`.

- The Windows gate runs the same lints, build, test suite and smoke test.

- `--quick` skips the slow stages: the cross build, packages, the profiler, fuzzing, the sanitizer suite, screenshots and the demo. The native build, the full suite and dogfood still run. On Windows `-Quick` changes nothing yet, since none of those run there.

- `--include-bsd` adds the FreeBSD lane: the build and suite on the FreeBSD box in the test stage, and its tarball and pkg in the release stage. It is off by default, since it needs that box and its lock. Asked for, it runs under `--quick` too.

- The same hook blocks a push to main unless `source/meson.build` is a strict version increase over what is already there.

- Both the version and the README badge are read from the commit being pushed. The hook also refuses when the tracked files differ from that commit, since the gate builds and tests the working tree. So a release is pushed from a clean checkout of main.

### Versions and build numbers

- `source/meson.build` is the only place the version is written. Everything else reads it.

- The fork numbers its own releases from 1.0.0, independent of the 6.6.4 code baseline. Since 6.6.4 was never tagged or released here, that reset was a clean one-time step.

- Every build also carries a build number: minutes elapsed since the start of 2000, in lower-cased Crockford base32, which drops i, l, o and u so nothing reads as a digit by mistake. Five characters until 2063, six after. It sits beside the version in `--version`, `--about`, Help > About, the Windows splash screen and the release notes, so a bug report names not just which release but which build of it.
	- It is worked out at configure time from `SOURCE_DATE_EPOCH`, so two builds of one commit carry the same number. Without it the number comes from the commit date of HEAD, and only failing that from the clock.
	- It lives in a generated header of its own rather than in `config.h`, because the number moves on every reconfigure and a change in `config.h` rebuilds the whole tree.

### The pipeline

Stages, in order, each self-skipping when unconfigured: remote sync, format, debug build, tests and lints with fuzzing and the sanitizer suite, profiler, release build, packages, dogfood, backup and publish. Disabled on purpose today are the format stage, since there is no in-place C formatter worth running, and the engine's own release collector, because the per-platform release lanes write those artifacts themselves.

- Remote sync runs first for a reason. The publish stage pulls at the end, so without it a change merged remotely mid-run would be pushed having never been built or tested. It fast-forwards when the branch is only behind and stops the run outright when it has diverged. It is skipped in gate mode, since a pre-push hook must not rewrite the tree underneath the push that called it.

- No stage is allowed all the cores. Build parallelism is capped at half of them, so a full run leaves the machine usable.

- The publish stage refuses a dirty tree, checked once at preflight and again before it runs. It commits everything it finds, and nothing there can tell work in progress from a finished change.

- Profiling browses a generated folder tree on a private headless display while sampling every thread, then renders a flamegraph and prints the hot spots into the run log. It samples by attaching a debugger rather than using perf, because perf needs a privileged sysctl here and a profiler that cannot run without root is a profiler nobody runs. The cost is wall-clock samples, so a blocked thread reads as work; the report keeps waiting in its own bucket and gives every figure as a share of busy time as well as of total. It profiles the debug build, since the release binaries are stripped and a flamegraph with no function names says nothing.

- The last stage archives the project tree into a rotated set of backups and then commits and pushes the current branch. The archive keeps what would be painful to lose - source, docs, the pipeline, assets, release builds and their packages - and drops what a command regenerates, chiefly the staged Windows runtime snapshot, which by itself took each archive from about 1.6 MB to 36 MB.

### Reproducible builds

Nothing a build produces takes its timestamp from the clock. Every lane sets `SOURCE_DATE_EPOCH` to the commit date of what is being built, so the same commit builds to the same bytes on any box on any day and a released Linux artifact can be checked against a rebuild of its tag. That holds from the release after 1.0.0-beta2, since beta1 and beta2 were built with link-time optimization off. The Windows exe cannot be yet: its hosted build installs whatever MSYS2 packages are current that day.

- The Windows exe was the one that actually differed run to run. The linker writes a timestamp into the PE header, and left alone it writes the clock: two clean builds of one commit used to differ in exactly those four bytes.

- The strip rewrites the field too, so whatever strips a released exe has to carry the stamp as well as whatever linked it. Both Windows lanes were missed here, one at a time: the linker was given the stamp and the strip that ran after it put the clock back. Whichever step writes the file last is the one that decides.

- Each lane checks the stamp on the file it actually goes on to pack, not on an earlier copy of it. The cross lane checks twice, once after the link and once after the strip, so a failure says which step caused it.

- The linker, `dpkg-deb` and `rpmbuild` read the stamp themselves. `zip` has no such notion, so its input is stamped on disk and fed in sorted order, and `tar` is given the stamp and a sorted order explicitly.

- One script answers what the stamp is, and every local lane calls it rather than working it out again. The hosted Windows release reads the same commit date inline. `docker exec` does not carry the host environment into a container, so each lane hands it over explicitly.

- A tree with uncommitted changes still gets its `HEAD` commit's date, since the alternative is the clock, but the release lanes warn, because nothing built from it can be reproduced.

- The Linux release lane sets up an empty build dir every run and reads back the options meson recorded. The release image's meson keeps a reused dir's own link-time optimization setting on a reconfigure, so a dir first set up without it built every release without it, and none of them matched a clean rebuild of the commit.

- Left out on purpose: the wall clock still names log files and dated dogfood copies, which is what it is for. A signed exe can never be byte-identical anyway, since the countersignature carries the real time of signing.

### Release artifacts and packaging

The two platforms get deliberately different artifacts, because what a user already has installed is different.

Linux is a thin relocatable prefix of a couple of MB that uses the distro's own GTK3. A bundled GTK on Linux is the thing that goes stale and mismatches the desktop's theme, portals and input methods, and it would multiply the download for no gain.

- It is built in an Ubuntu 22.04 container, never the day-to-day Debian 13 one. A binary's glibc floor is whatever it was built against, so a release built on Debian 13 would refuse to start on anything older than 2025. The floor is therefore glibc 2.35 and GTK 3.24.33, which reaches Ubuntu 22.04, Debian 12, Mint 21 and Fedora 36 onward.

- The arm64 tarball is built the same way, on an arm64 Linux box with docker, from the same image file, so it has the same floor and the same library versions. Nothing here cross-builds GTK. `cicd/linux/release-arm64.bash` sends the working tree to that box, runs the same release script there, and brings the tarball back. The release script names it `arm64`, the name the installers ask for, where the kernel says `aarch64`. On an emulated box a build takes about an hour, so a pipeline run builds it only when asked to, with `cicd.bash --include-arm`. Its `.deb` and `.rpm` come with it, and only then.

- What makes it relocatable: the program works out where it is and points `XDG_DATA_DIRS` and `PATH` at the folder it sits in, at startup, before anything reads them. The extension API is inside the program, so there is no `lib/` folder to find. Everything looked up through the XDG data dirs - actions, search helpers, icons, mime info - then resolves wherever the folder was installed. There used to be a shell wrapper in `bin/` doing that with the real binary hidden in `libexec/`; two files where one would do, so it went.

- The D-Bus activation file is written at startup rather than installed, into the user's own service directory. It has to name an absolute path, and a portable copy does not have one until it runs.

- Staging leaves out what only a system install would read: mime data, polkit, man pages and the editor syntax files. Both packages install the prefix under `/opt`, where none of it is read, and the install rules still produce all of it, so a distro building `--prefix=/usr` is unaffected. Icons are compiled in except the app icon at its eight sizes, which packaging and the menu entry need as real files. Actions, search helpers and the settings schema stay as files, since those are the drop-in folders a user edits and Preferences has buttons that open them.

Windows is one self-contained `nemo-anywhere.exe` with the whole runtime packed inside it by Enigma Virtual Box, as an in-memory virtual filesystem with nothing extracted at run time. No library folder, no launcher, nothing installed or registered: an exe to copy anywhere. A plain zip of the same files is published beside it, and a setup exe that installs the zip's files for one user. See [20260930-145641_windows_exe_packing.md](design_docs/20260930-145641_windows_exe_packing.md).

Packaging builds from what the release lanes already produced and never rebuilds. The Linux tarball becomes a `.deb` and an `.rpm`, both installing the same relocatable prefix under `/opt` plus a launcher, a menu entry and icons in the shared theme. The `.deb`'s dependency versions are read off the built binaries inside the release container rather than on a development box, so the package claims the floor the binary was actually built against; `rpmbuild` derives its own from the ELF. The arm64 binaries can't be read in the x86_64 container, so the arm64 lane reads them in the release container on the arm64 box, right after its build, and brings the line back with the tarball. Both arches are then packaged on the same box with the same tools. A line read off any other build of the tarball is not used; the `.deb` gets a short list with no versions and a warning instead, as it does when the x86_64 container is down. The FreeBSD pkg is made on the FreeBSD box with the build, since only `pkg create` makes one, as [Building on FreeBSD](#building-on-freebsd) says. The Windows zip becomes the setup exe, built with NSIS in the cross-build container. macOS, AppImage and Flatpak wait on a toolchain.

A packager that fails only warns, so one broken format does not cost the others. The installer and prefix checks run after the packagers and stop the run on a failure, before dogfood and publish.

Cutting a release tags `v<version>` from a clean main and uploads the artifacts. Release notes are the hand-written changelog section for that version, never a generated commit list. A version with no section gets one line pointing at the changelog, so a release is never published blank. Under the notes, a Downloads table links each build, with the target OS in rows and the CPU in columns. Only the local cut makes a release. It pushes the tag, waits for the hosted builds the tag starts, downloads what they made, and puts up the release with every file, one sums file and the full notes in one step. If a hosted build fails, no release is made; once it passes, the same command picks up from the pushed tag. Any other platform built elsewhere, such as BSD, macOS or ARM, hands its files over the same way. A version carrying a pre-release part is published as a prerelease, which matters to the installers: their stable channel takes the newest release with no prerelease part, and the newest prerelease only while no stable release exists.

### Installing

`install.bash` and `install.ps1` sit at the repo root and run as one-liners straight from a shell. They are two standalone installers rather than one script with a helper: the bash one targets bash 3.2 so stock macOS runs it, the PowerShell one covers unix itself instead of handing off. The duplication is deliberate, and buys a one-liner that works from whichever shell someone already has open.

- The app installs as a whole folder plus the two things that make it reachable: a menu entry and a name on PATH. A file manager gets launched both ways.

- A user install is the default and needs no privileges. A system-wide install is opt-in and is the only path that escalates, which it states in the plan first.

- The OS and architecture are detected, not asked for. Every run prints what it is about to do and waits for a yes. Downloads are checksum-verified before anything is unpacked, so a bad download can never replace a working install. A release with no checksums file stops the install before the plan, `--yes` or not, and only `--no-verify` gets past it. An archive given with `--from` is not checked. Reinstalling replaces in place, and `--uninstall` removes exactly what was added.

- On Windows the setup exe installs into the same user install, the same way: the folder, the Start menu shortcut and the user PATH entry. It adds an uninstaller in the folder and an entry in Settings, Apps. So either one updates or removes what the other installed. `install.ps1` keeps the setup's uninstaller when it reinstalls, and its `-Uninstall` takes the Apps entry out too. The setup exe has no system-wide install.

- Because they read the releases page, the packaging stage has to produce exactly these names: `nemo-anywhere-<version>-<os>-<arch>.tar.gz` for unix and `.zip` for Windows, with `<os>` one of `linux`, `bsd` or `windows` and `<arch>` one of `x86_64` or `arm64`, plus `nemo-anywhere-<version>-sha256sums.txt` beside them in `sha256sum` format. The installers ask for `bsd` on every BSD, and the one built is FreeBSD. Its pkg file is `nemo-anywhere-<version>-bsd-<arch>.pkg`, beside the tarball. The portable Windows exe is `nemo-anywhere-<version>-windows-x86_64-portable.exe`, built by the hosted workflow and given its line in the same sums file by the local cut. The setup exe is `nemo-anywhere-<version>-windows-x86_64-setup.exe`, made from the zip in the packaging stage. Each archive holds one top-level folder, whose entry point is `bin/nemo-anywhere` on unix and `nemo-anywhere.exe` at the root on Windows.

### Dogfooding

Each platform's pipeline publishes one build to a shared drop folder for that platform and writes nothing else there: the whole relocatable prefix on Linux, the packed exe on Windows.

- The launcher owns the local side. It copies the drop into a pool of date-stamped versions and points a symlink at the newest, so the fixed name is the only thing anything else has to know. One script serves all three platforms, with a small wrapper per platform in the place that platform looks for commands, so it can be typed at a shell or named in a `.desktop` file.

- The pool is rotated on every launch: the newest of each finished hour, day, week, month and year, the most recent few, and the first build ever held. On top of that a budget of at most ten versions, at least five, and only as many between the two as fit in 1 GB. Nothing a running process lives inside is removed, so going back to an older build is a matter of running it rather than rebuilding it.

- A build already held is settled on its bytes, not its date. The sync layer restamps what it carries, so a date test on its own re-fetched the same build every run.

## Open questions

- How far to push a clean internal platform-abstraction boundary, against per-target `#ifdef`s in the shared files. Both conventions are in the tree today.
