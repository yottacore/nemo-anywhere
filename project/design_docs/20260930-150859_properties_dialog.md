<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere Properties dialog

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [Which window opens](#which-window-opens)
	- [Counting today](#counting-today)
	- [Planned counting](#planned-counting)
	- [Hidden attribute](#hidden-attribute)
	- [Open questions](#open-questions)
- [Alternative ideas](#alternative-ideas)
	- [Unconsidered](#unconsidered)
	- [Rejected](#rejected)
	- [Superseded](#superseded)
- [Research findings](#research-findings)
- [Roadmap](#roadmap)
- [Related backlog issues](#related-backlog-issues)

<!-- /TOC -->

## Summary

The Properties window shows what a file or folder is, and lets a few things about it be changed.

- Built today: on Windows, Properties opens the shell's own sheet, and ours is a second item. A folder's files, folders and size count up live, with the hidden ones counted beside them. A shortcut's target can be edited there.

- Planned, not built: counting the way the [compression](20260929-101432_compression.md) dialog does, with a choice per kind of link and per filesystem and a size change beside each. Also a "Hidden" box that hides a file the way Windows does, on every platform.

## Specification

- On Windows, Alt+Enter, Ctrl+I and "Windows properties" open the shell's own sheet. Ours is plain "Properties", first in the menu, and on Ctrl+Enter.

- Anything the shell can't name opens ours rather than doing nothing.

- On a folder, or several items, Contents is two lines, Folders and Files, each with its hidden count beside it, as in "12 (and 3 hidden)". Both count up live while the folder is walked, and so does Size.

- A shortcut shows its Target, Arguments, Start in and Comment, each saved as it's edited.

- Planned:
	- Counting like the Compress dialog: link choices, nested and other filesystems, and exact totals for a file with more than one path.
	- A "Hidden" box, like the Hidden attribute in Windows, that works on every platform.

## Goals

- Answer "how big is this, really" without surprises from links or mounted drives.

- Show hidden things as hidden, not leave them out of the count.

- Hide a file the same way everywhere, so a folder looks the same on each platform.

### Non-goals

- Replacing the Windows sheet on Windows. It has tabs from other programs, and reads the way it does everywhere else on the machine.

- Changing a file's name to hide it. A leading dot is a name, not a setting.

## Design

### Which window opens

- Properties is the platform's own on Windows. Alt+Enter and Ctrl+I hand the selection to the shell property sheet, the same one Explorer shows, so a file's details read the way they do everywhere else on the machine and any tab a third-party program adds is there too.

- Our own window is still there under Ctrl+Enter. It covers what the shell sheet has no room for: a custom icon, an emblem, an annotation, an extension page. Anything the shell can't name falls back to it: a virtual location, a selection spanning folders such as a search result set, or an item that's gone since it was clicked.

- The shell sheet runs off the window's thread, so the window behind it stays live while it's open.

- The other platforms use our window throughout.

### Counting today

- Contents is two lines, Folders and Files, each with its hidden count beside it. Both count up live while the folder is walked, and so does Size.

- With several items selected, a selected folder counts as one of the folders. A lone folder shows only what is inside it.

- Largest first was left out. The size of each file comes with the listing, so the total is final the moment the listing is.

- There's no Help button. It opened GNOME help pages, which this project doesn't have.

### Planned counting

The same scan and counting as the Compress dialog. See [Counting sizes](20260929-101432_compression.md#counting-sizes).

- The choices, when the selection has a folder or a link:
	- Symlinks: Ignore, or Follow. Ignore is the default.
	- Junctions, on Windows only: Ignore, or Follow.
	- Follow nested filesystems, checked by default.
	- Follow other filesystems, never checked by default.

- Each choice shows how much it adds, the way the Compress dialog does. The Folders, Files and Size lines follow the choices as set.

- A file reached by two paths counts once, whichever path the scan found first.

- `.lnk` files are counted as files, never followed.

- The count comes from [TukzedoFS](20260930-145641_tukzedofs.md) once it has the rows.

### Hidden attribute

A "Hidden" box on the Basic page, like the Hidden attribute in Windows.

- On Windows it sets or clears the file's Hidden attribute, the same one Explorer sets.

- On a folder it asks, as Windows does, whether to apply it to the folder only, or to everything inside too.

- With several items selected it shows a mixed state when some are hidden and some aren't.

- Off Windows there's no Hidden attribute. See [Open questions](#open-questions) for where it's kept there.

- The listing already treats the two kinds apart on Windows: attribute-hidden files, and dot-files. On other platforms one switch covers both. See [design.md](../design.md#hidden-files-and-shortcuts).

### Open questions

- Where "Hidden" is kept off Windows. The choices:
	- A `.hidden` file in the folder, one name per line. GLib already reads it, and so do other file managers on Linux. It only covers files in that folder, and a rename or move loses it.
	- An extended attribute on the file itself. It travels with the file, but nothing else reads it.
	- A row in TukzedoFS. Nothing else reads it, and it's lost with the database.

- Whether a dot-file shows the box ticked and grayed, since its name is what hides it.

- Whether the counting choices are remembered, like the Compress dialog's options, or start from the defaults every time.

## Alternative ideas

### Unconsidered

- The other Windows attributes, such as Read-only and Archive.

### Rejected

- Counting the largest files first. The size of each file comes with the listing, so it would only make the total later.

### Superseded

- "Contents" and "Size" as one line each. Replaced by the Folders and Files lines with hidden counts.

- "Advanced properties" as the name of ours on Windows. It's plain "Properties" now, and sits first.

## Research findings

- GLib has read a `.hidden` file in each folder since 2.36, and marks the names in it hidden.

- Windows reports only its own Hidden attribute, so dot-files were shown there whatever the setting said, until the second switch.

## Roadmap

- Planned counting, after the Compress dialog reset, since it shares the scan.

- The Hidden box, once the open question on where it's kept is settled.

## Related backlog issues

- Folder properties dialog. Done.

- Properties on Windows opens the one Windows itself shows, instead of ours. Done.

- Right-click properties wording. Done.

- Windows: edit a `.lnk`'s target from a properties view. Done.

- Windows: two kinds of hidden file, two options. Done.

- Ctrl+H toggles dot-files and Windows hidden files together. Done.

- 2026092910143202: Compression dialog reset.
