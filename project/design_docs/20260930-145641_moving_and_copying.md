<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere moving, copying and links

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [Copying links](#copying-links)
	- [Moving](#moving)
	- [Clone copies](#clone-copies)
	- [Making links](#making-links)
	- [Shortcuts](#shortcuts)
	- [Editing links](#editing-links)
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

How files are copied and moved, and how links are handled along the way. It also covers making and editing links, since those share the same kinds and the same rules.

- Built today: a copy that meets a link asks what to do with it, a move always takes a link as a link, copies are clones where the filesystem allows, and Make link and Edit link cover every kind of link on every platform.

- Deleting and trashing are in [design.md](../design.md#trash-and-delete).

## Specification

- Copying anything that holds a link asks what should be at the far end, once per operation. It's asked on every platform, even where the destination can hold no links.

- A move always takes a link as the link. A move on one drive is a rename and asks nothing.

- Deletes and moves never follow links of any kind.

- A link counts as one item, not a folder to walk into.

- A drop that moves files says what it is about to do, and waits. Copies and links don't ask by default. Both are settings under Behavior.

- A copy is a Copy-on-Write clone where the filesystem allows, except for a small file.

- Make link offers every kind the platform has. A hardlink is never picked for anyone.

- Nothing in the link dialogs is remembered between uses.

## Goals

- No surprises about links. A copy never quietly turns links into files, or files into links.

- Make it hard to move or lose files by accident, most of all by a stray drag.

- Copies that cost almost no space where the filesystem can share it.

- Links that work the same on Windows, Linux and the rest, and read the same way on each.

### Non-goals

- Snapshots of a folder tree, by clone or by hardlink. See [Rejected](#rejected).

- Rewriting links inside a copied tree so they point inside the copy.

- Picking a hardlink for anyone, even where nothing else can be made.

## Design

### Copying links

Copying a link asks what should be at the far end. A link can stay a link or be replaced by what it points at, and neither answer is right every time, so the question is put once per operation rather than guessed. It is asked whenever the source has a link, on every platform, including where the destination can take none. There every option but the copy is grayed out and the dialog says why. A copy that quietly turns links into files, or files into links, is the thing being avoided.

- The dialog names only the kinds the source actually has, with a row per kind:
	- File symlinks: "Copy link as-is" or "Copy contents".
	- Folder symlinks: "Copy link as-is", "Copy as a junction" on Windows, or "Copy contents".
	- Folder junctions, on Windows: "Copy link as-is", "Copy as a symlink", or "Copy contents".
	- On a move, "Move" takes the place of "Copy".
	- Labels are singular or plural to match the count.

- Shortcuts (.lnk) are always copied as just the shortcut file. A shortcut is a plain file to a copy, and alone it asks nothing.

- Links inside a folder whose contents are copied are kept as links, or junctions, where the destination allows. A tooltip on "Copy contents" says so.

- Windows is where this mattered most. A copy there always followed the link and left the contents behind, so a link could not be copied as a link at all. POSIX already kept symlinks by default; what is new there is being able to ask for the contents instead.

- Windows has two kinds of link where POSIX has one, and the dialog says so. A folder symlink and a junction both point at a folder, but only the symlink needs a privilege Windows normally withholds. Each row starts on the kind it found and falls back to the nearest kind that still reaches the same target, then to a plain copy. Anything the destination cannot take is grayed out rather than hidden, so the dialog does not change shape between machines.

- The kind of a Windows link comes from its reparse tag. Nothing else tells a junction from a folder symlink, and it also keeps cloud placeholders and store app aliases, which are reparse points too, from being read as links.

- A link keeps its own spelling, so a relative one still points where it pointed. Asking for a junction is the exception: those can only name a full path, so a relative target is resolved first.

- A link counts as one item rather than a folder to walk into. That is what POSIX always did and Windows never did, and it is what stops a copy following a link to somewhere large or unreachable.

### Moving

- A move always takes a link as the link. Taking the contents would empty the folder the link points at, which is not what was asked to go. A move to another drive still asks, and can make a different kind of link, but the copy option is grayed out. A move on one drive is a rename and asks nothing, so every link arrives as it was.

- A drop that moves files names what it is about to do and where, and waits for an answer. Accidental drags, most of all big ones across filesystems, are one of the worst things about file managers. Moves ask by default; copies and links don't. Both are settings under Behavior. It covers every place a drop can move files: the file list, the icon view, both sidebars, the path bar and the tabs. A drop on the Trash asks under its own setting, not twice.

- A move is held to the delete guard like a delete, since the original leaves. See [design.md](../design.md#trash-and-delete).

### Clone copies

- On Linux, Nemo Anywhere already does all copy operations on supported filesystems, with essentially the same thing as `cp --reflink=auto` - so that if it's possible and beneficial (e.g. not for tiny files), copies will be clones. There is no risk and almost never downsides to this being unchangeable hard-coded behavior, only potentially massive benefits. (And the downsides are trivial.)
	- A file under 64 KiB is read whole and written out plainly, so it is never cloned. 64 KiB is 16 blocks of 4 KiB. A clone saves less than that below it, while the filesystem keeps track of the shared part for as long as both copies exist.
	- The limit is `performance.clone-min-kib` in the settings file. 0 always clones, and 1024 is the most.
	- An overwrite, a link copied as a link, or anything the plain copy can't start goes the usual way, so conflicts and errors read the same.
	- Other platforms have nothing to skip, since only Linux clones on copy there.

- Which filesystems can clone is under [Research findings](#research-findings).

### Making links

Make link asks what to make, one row for the folders and one for the files. Folders get a junction or a symlink on Windows and a symlink elsewhere. Files get a symlink or a hardlink. Either can be a `.lnk` shortcut, labeled Shortcut. The title is in the window's title bar.

- From the menu the dialog does not say where the links go, since it is always the folder in view. A link drop opens the same dialog, and there it names the folder, since a drop can be onto another folder or another window.

- A symlink path row picks absolute or relative for whatever comes out a symlink. It is hidden when nothing does, since a junction is always absolute and a hardlink has no path, but keeps its space so the dialog does not jump. It sits a little lower than the rows above it, since it's a different kind of choice.
	- Absolute is the default. Relative is there for links inside a tree: it keeps working when the whole tree is moved or mounted somewhere else, which is the usual reason to link inside one. It always has to work from the folder the link really sits in, since `..` climbs from there. So the paths as seen and the real paths, with symlinked folders resolved, are both tried, and the shortest that still reaches the target wins. Resolving alone would send a link up to the root whenever a symlinked folder on the way leads to another tree. Between two Windows drives there is no relative path, and the link keeps the absolute one.

- A link made beside its original says what it is: "photo - symlink.jpg", "photo - hardlink.jpg", "folder - junction". The extension stays last so the link still opens as its type. A shortcut follows Explorer's pattern instead, "photo.jpg - shortcut.lnk". A link made in another folder keeps the original's name. A clash adds " 2", " 3" and so on.

- A drop never asks the drop question for links, since the dialog is the question. Every link drop opens it, from the Alt menu or with the link keys held, in any view or sidebar.

- A hardlink is never picked for anyone, even where a symlink cannot be made. It is the one choice that can cost something: an edit through one name changes a file that looks unrelated, and a program that saves by replacing the file splits the two names apart without a word. Its tooltip says so, and choosing it asks once more, every time, with Cancel as the default. Backing out goes back to the dialog with the choices as they were. See [Hardlinks](20260925-063617_dedupe_and_thumbnails.md#hardlinks) for why.

- Nothing is remembered. Every open starts from the defaults, so what it offers never depends on what was done last time. A way to change the defaults may come later.

- Windows with no symlink privilege still gets the menu item, since a junction, a hardlink and a shortcut need none. The symlink choices are gray, with a line saying why, and files start on Shortcut.

### Shortcuts

- A shortcut can carry several paths at once, and is followed by the first that still leads somewhere. So every shortcut gets all three: Absolute, Relative and Portable. There is no choice to make.
	- Portable is the path with a Windows environment variable in it, such as `%USERPROFILE%\Documents\notes.txt`, where one covers the target. It keeps working for another user or on another machine, so it is tried first. Off Windows the home folder is `%USERPROFILE%`, and a target on a mounted Windows share keeps its `\\server\share` path there instead.
	- Windows 11 follows a shortcut through its item ID list, which names the target in Windows' own terms, or else through the portable path. It never uses the absolute or relative path alone. No Linux path can be written as an item ID list, so Explorer follows a shortcut made off Windows only through its portable path. Nemo Anywhere on Windows reads the rest itself.
	- On Windows the shell writes the item ID list and the relative path.
	- The paths inside are spelled the Windows way on every platform, with backslashes, and variables are always `%NAME%`, never `$NAME`. Each is read back for the platform in use.

- A shortcut's target is read from the shortcut file, never by visiting the target. A shortcut to a share that isn't answering would otherwise stall the window.

### Editing links

Edit link changes an existing link's name and where it points. It is offered only when one symlink, junction or shortcut is selected. A hardlink has no target to change.

- A symlink or junction gets one target field, taken as typed, so a relative one stays relative. The new link is made under a spare name first. On Linux it is then renamed over the old one, so the old link is never missing. Windows cannot rename over a folder, so there the old link goes first, and the new one is taken back if it will not.

- A shortcut shows its three paths, and any can be emptied to drop it. Only the paths are rewritten; its arguments, Start in folder and icon stay. What else it records about the old target goes with them, the item ID list included, since Windows would follow that first.

- The old link is removed through the delete guard, which removes a link and nothing else.

### Open questions

- A relative symlink between two shares of one server doesn't resolve (2026092813381418).

## Alternative ideas

### Unconsidered

- A default for Make link that can be changed, from a "Defaults..." button.

### Rejected

- A "Snapshot ..." menu item, canceled on 2026-09-25. It would have made a Copy-on-Write clone or a hardlinked copy of a folder tree beside itself, then pointed links inside the copy at the copy.
	- The clone already comes from copy and paste on a filesystem that clones.
	- The hardlink copy is too fraught with future data loss for the user.
	- Rewriting links inside the copy could still surprise people, and is inconsistent, since only links into the tree would change.
	- It belongs in tools someone goes looking for, not a feature stumbled on in a file manager.

- Following links on a move. It would empty the folder the link points at.

- Remembering the choices in Make link.

- A hardlink as the fallback where a symlink can't be made.

- Cloning files under 64 KiB. A clone of a tiny file can cost more than it saves.

### Superseded

- "Symlink" and "Copy" as the link copy choices. "Symlink" read as making a new link. They became "Copy link as-is" and "Copy contents".

- A "Shortcut paths" choice in Make link. Every shortcut now gets all three paths.

- "Link to ..." names for links made in another folder. They keep the original's name now.

- A folder link on Windows was always made as a junction (2026-08-28). Make link now offers junction or symlink.

- Copies on Windows always following links. A link can now be copied as a link.

## Research findings

- Windows reports a junction as an ordinary folder. Only the reparse tag tells a junction from a folder symlink, a cloud placeholder or a store app alias.

- Windows 11 follows a shortcut through its item ID list, or else its portable path, never the absolute or relative path alone.

- Windows can't rename one folder over another, so an edited folder link can't be swapped in with no gap there.

- A clone below 64 KiB saves less than the filesystem spends keeping track of the shared part.

- Copy-on-Write clones, by platform: FICLONE on Btrfs, ZFS, XFS, OCFS2 and bcachefs on Linux, `clonefile()` on APFS, and FSCTL_DUPLICATE_EXTENTS_TO_FILE on ReFS. ZFS needs OpenZFS 2.2 or later with block cloning on.

## Roadmap

None.

## Related backlog issues

- Copying and pasting objects that includes symlinks or junctions, should open up an option dialog. Done.

- Link copy dialog. Done.

- When copying symlinks, make it clear that "Symlink" is not creating a new one. Done.

- Confirm mouse-movement-based actions. Done.

- Copying a tiny file makes a CoW clone of it. Done.

- Make link, Link names, Update to "Make a link" dialog, and "Make link" dialog. Done.

- New menu item: "Edit link". Done.

- Windows `.lnk` file support on macOS and Linux. Done.

- Menu: "Snapshot ...". Canceled.

- 2026092813381403: Edit link on a symlink named .lnk turns it into a plain file.

- 2026092813381412: redo after undoing Make link makes a different kind of link.

- 2026092813381414: Edit link can remove the link when only the case of its name changes.

- 2026092813381417: hardlinking a selected symlink links the symlink.

- 2026092813381418: a relative symlink between two shares of one server does not resolve.

- 2026092813381440: the shortcut path choice code is only reached by tests.
