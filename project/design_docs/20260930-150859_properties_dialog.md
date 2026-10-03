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

## Specification

- On Windows, Alt+Enter, Ctrl+I and "Windows properties" open the shell's own sheet. Ours is plain "Properties", first in the menu, and on Ctrl+Enter.

- Anything the shell can't name opens ours rather than doing nothing.

- On a folder, or several items, Contents is two lines, Folders and Files, each with its hidden count beside it, as in "12 (and 3 hidden)". Both count up live while the folder is walked, and so does Size.

- A shortcut shows its Target, Arguments, Start in and Comment, each saved as it's edited.

## Goals

- Show hidden things as hidden, not leave them out of the count.

### Non-goals

- Replacing the Windows sheet on Windows. It has tabs from other programs, and reads the way it does everywhere else on the machine.

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

### Open questions

None.

## Alternative ideas

### Unconsidered

None.

### Rejected

- Counting the largest files first. The size of each file comes with the listing, so it would only make the total later.

### Superseded

- "Contents" and "Size" as one line each. Replaced by the Folders and Files lines with hidden counts.

- "Advanced properties" as the name of ours on Windows. It's plain "Properties" now, and sits first.

## Research findings

- Windows reports only its own Hidden attribute, so dot-files were shown there whatever the setting said, until the second switch.

## Roadmap

None.

## Related backlog issues

- Folder properties dialog. Done.

- Properties on Windows opens the one Windows itself shows, instead of ours. Done.

- Right-click properties wording. Done.

- Windows: edit a `.lnk`'s target from a properties view. Done.

- Windows: two kinds of hidden file, two options. Done.

- Ctrl+H toggles dot-files and Windows hidden files together. Done.
