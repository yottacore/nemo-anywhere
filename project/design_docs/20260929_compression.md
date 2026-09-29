<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere compression and extraction

Design for the Compress dialog reset, and for keeping the archive code apart from the rest of the app. Companion to [design.md](../design.md), whose [Archives](../design.md#archives) section has what is built today.

Status: design pass, 2026-09-29. None of it is built yet. The backlog item is 2026092910143202. Two fixes waiting for testing touch the same code: 2026092813381404, links that lead nowhere, and 2026092813381416, wildcards in names.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Modularity](#modularity)
- [Compress dialog](#compress-dialog)
	- [Options the writer can't do](#options-the-writer-cant-do)
	- [Link handling](#link-handling)
		- [Link options](#link-options)
		- [Junction defaults](#junction-defaults)
		- [Mounted filesystems](#mounted-filesystems)
		- [Total size](#total-size)
	- [Counting sizes](#counting-sizes)
		- [Path list](#path-list)
		- [Size totals](#size-totals)
		- [Background scan](#background-scan)
	- [After OK](#after-ok)
	- [Delete originals after verification](#delete-originals-after-verification)
	- [Volume sizes](#volume-sizes)
- [Why the reset](#why-the-reset)
- [Open questions](#open-questions)

<!-- /TOC -->

## Modularity

- Keep as much of the compression dialog as modular as possible, for potentially splitting out to its own project. Also decompression.

- Three layers:
	- A core with no GTK and no nemo types. It picks the writer, builds the command lines, runs the link and size scan, writes and reads archives, checks an archive before a delete, and cleans stored paths on extract.
	- A GTK layer. The Compress dialog, the Extract conflict dialog, and the progress bars.
	- Thin glue in nemo. Menus, the job queue, settings, trash and the delete guard.

- The core takes its settings as values handed in. It never reads nemo's settings itself.

- The core reports progress, questions and file changes through callbacks. The glue passes them on to nemo's job queue, progress info and file-changes queue.

- Today the archive and extract code call these nemo parts directly. Each one goes behind an interface:
	- The job queue, progress info and the file-changes queue.
	- Command templates and settings.
	- The directory walk. On Windows it is what gets past MAX_PATH, so a split-out needs its own copy of it, not GIO's.
	- Trash, through file operations, and the delete guard. Both stay in nemo. The core asks its caller to delete and never deletes on its own.
	- eel's stock dialogs.

- The size scan below is new code, so it goes in the core from the start.

- Core tests run with no display.

## Compress dialog

### Options the writer can't do

- For options that aren't supported by the archiver (e.g. add recovery record), don't just disable it, but deselect AND disable.

- A forced off, from the format or the selection, doesn't overwrite the remembered choice. Switching back puts it back.

### Link handling

The options and workarounds got complex and confusing. This is a reset.

#### Link options

- If the selection contains at least one symlink and/or folder:
	- Symlinks (radio options):
		- [ ] Ignore              # Default
		- [ ] Follow (size Δ: N)
		- [ ] Store as Symlinks   # Disabled if selected archiver doesn't support
	- Junctions (radio options):  # Windows-only section
		- [ ] Ignore
		- [ ] Follow (size Δ: N)
		- [ ] Store as junctions
		- [ ] Store as symlinks

- `.lnk` files are always stored, never followed. Flyover text for "Follow" should say that.

- A store choice the archiver can't do falls back to "Ignore".

- 7z on Windows leaves links out today, so it counts as not supporting "Store as Symlinks" there, and that choice is disabled.

- On Windows a folder mount point and a junction are the same kind of reparse point. One that points at a whole volume counts as a mount point only.

- For the size totals, "Store" counts the same as "Ignore". The link goes in, but what it leads to doesn't.

#### Junction defaults

- Symlinks=Ignore -> Junctions defaults to Ignore

- Symlinks=Follow -> Junctions defaults to Follow

- Symlinks=Store as Symlinks:
	- Meaning of table headers below:
		- j2j supp: Are "Junctions as Junctions" supported?
		- j2s supp: Are "Junctions as Symlinks" supported?
		- j2j en: Then "Junctions as Junctions" UI option enabled?
		- j2s en: Then "Junctions as Symlinks" UI option enabled?
		- j2j st: Then "Junctions as Junctions" UI option selected by default?
		- j2s st: Then "Junctions as Symlinks" UI option selected by default?
	- Possible combinations:

		| j2j supp | j2s supp | j2j en | j2s en | j2j st | j2s st | Notes
		| :------: | :------: | :----: | :----: | :----: | :----: | :---
		|    Y     |    n     |   Y    |   n    |   ON   |  off   |
		|    Y     |    Y     |   Y    |   Y    |   ON   |  off   |
		|    n     |    Y     |   n    |   Y    |  off   |   ON   |
		|    n     |    n     |   n    |   n    |  off   |  off   | Default to 'Ignore' for Junctions.

- A hand change to Junctions sticks. A later Symlinks change doesn't reset it.

- Junctions can use any store choice the archiver supports, whatever Symlinks is on. The table sets only defaults.

#### Mounted filesystems

- An option below all that:
	- [ ] Follow across mounted filesystems (size Δ: N)
		- Unchecked by default.
		- Disabled if no folder is selected (of any type).

#### Total size

- On the left side of the dialog, same row as Cancel/OK:
	- Total size to include: N

### Counting sizes

How to calculate the four different sizes shown.

#### Path list

- Build a structured list, e.g. `canonicalFullFilePath` with the following fields. Note: none of the 'is*' booleans are mutually exclusive - it's possible for all to be true.
	- path
		- This field must be ultra fast to add up to millions of paths to, and to match one value in a large set.
	- bytes
	- isDifferentFs
		- Means the canonical file path is on a different FS.
		- Different from the filesystem of the folder the selection is in. "CWD" below means that folder too, not the program's working folder.
		- Just because a file may be under a cross-FS mount, doesn't mean it's not a symlink that points back to the same FS as CWD.
			- But even so, it won't be scanned if "Cross filesystem" option is off.
	- isSymlinked
		- A symlinked file or a descendant of a symlinked folder
	- isJunctioned
		- A descendant of a junction folder

#### Size totals

- Build a structure for total sizes, e.g. `totalSizes`
	- Stored. Each new path adds its bytes to exactly one of these eight, picked by its three flags:
		- nSymlinked_nJunctioned_nCross
		- nSymlinked_nJunctioned_yCross
		- nSymlinked_yJunctioned_nCross
		- nSymlinked_yJunctioned_yCross
		- ySymlinked_nJunctioned_nCross
		- ySymlinked_nJunctioned_yCross
		- ySymlinked_yJunctioned_nCross
		- ySymlinked_yJunctioned_yCross
	- Summed from those eight at each UI refresh. A name that leaves out a flag counts both values of it. E.g. `nSymlinked_nCross` = `nSymlinked_nJunctioned_nCross` + `nSymlinked_yJunctioned_nCross`.
		- nSymlinked_nJunctioned
		- nSymlinked_yJunctioned
		- ySymlinked_nJunctioned
		- nSymlinked_nCross
		- nSymlinked_yCross
		- ySymlinked_nCross
		- nJunctioned_nCross
		- nJunctioned_yCross
		- yJunctioned_nCross
		- nSymlinked
		- ySymlinked
		- nJunctioned
		- yJunctioned
		- nCross
		- yCross
		- total (all eight)
	- Those are the only sums the table below uses. The other three pairs aren't needed.

#### Background scan

- In a background task, gather a list of all canonical file paths included in the current selection and below, and populate `canonicalFullFilePath`, with the following exclusions:
	- If "follow symlinks" selection is off, don't count any files that are symlinks at any level, and don't follow any folder symlinks, at any level.
	- If "follow junctions" is off, don't follow any folder junctions, at any level.
	- If "Follow across mounted filesystems" is off, don't follow any folders that are mounted from a different filesystem, or links of any kind that made it past the previous exclusions, but point to a different filesystem.
	- As candidates that made it this far are scanned read:
		- Obtain the full canonical file path.
		- See if that path already exists in the existing `canonicalFullFilePath` list.
		- If it doesn't:
			- Add the full canonical file path to the list, and populate the other fields with it.
			- Update the `totalSizes` structure with the appropriate additive values.
		- If it does, and the stored entry has a flag this path doesn't, clear that flag and move the bytes to the matching total.
			- A file reached both directly and through a symlink counts as direct. Otherwise the order of the walk decides whether it counts with "Follow" off. A `current` link beside the `v2` folder it leads to is the common case.
	- If one of the UI options is changed at any time during this (i.e. "follow symlinks", "follow junctions", "follow across filesystems"):
		- If an option is *less* inclusive, don't interrupt scan but immediately back out of now-excluded paths, and resume with the next legal path. (But don't remove or change any values of either structure.)
		- If an option is *more* inclusive, restart the scan with the more inclusive option. (Only adding to the file list and doing math as previously unseen paths are discovered.)
	- Every 0.25 second, check to see if any of the values in `totalSizes` have changed, or if any of the three relevant options have changed. If so, update the appropriate GUI elements.
		- Calculations for UI:
			- A Δ is what turning that option off would take away from the total, with the other two left as set. A file that is both symlinked and junctioned counts in both Δs, so the three don't add up to anything.

			| f-sym | f-jnc | f-cfs | Δ-sym                         | Δ-jnc                         | Δ-cfs                         | total size
			| :---: | :---: | :---: | :---------------------------- | :---------------------------- | :---------------------------- | :----------------------------
			|   n   |   n   |   n   | 0                             | 0                             | 0                             | nSymlinked_nJunctioned_nCross
			|   n   |   n   |   y   | 0                             | 0                             | nSymlinked_nJunctioned_yCross | nSymlinked_nJunctioned
			|   n   |   y   |   n   | 0                             | nSymlinked_yJunctioned_nCross | 0                             | nSymlinked_nCross
			|   n   |   y   |   y   | 0                             | nSymlinked_yJunctioned        | nSymlinked_yCross             | nSymlinked
			|   y   |   n   |   n   | ySymlinked_nJunctioned_nCross | 0                             | 0                             | nJunctioned_nCross
			|   y   |   n   |   y   | ySymlinked_nJunctioned        | 0                             | nJunctioned_yCross            | nJunctioned
			|   y   |   y   |   n   | ySymlinked_nCross             | yJunctioned_nCross            | 0                             | nCross
			|   y   |   y   |   y   | ySymlinked                    | yJunctioned                   | yCross                        | total

- In the future, the file scanning will use the "TukzedoFS" cache - PostgreSQL on Tukzedo Linux, SQLite3 otherwise.

- Pressing Cancel or Enter aborts the filescan and totalling background tasks and releases memory.
	- So the job doesn't reuse the dialog's scan. It runs its own pre-scan after OK.

### After OK

- Once OK is hit, if pre-scanning is required, the progress dialog should have a second progress bar above the regular one, showing pre-scan progress.

- The job works from our own list of what goes in, built by its pre-scan. That is what makes mixed choices work in rar and 7z, such as following symlinks while storing junctions.
	- The 7z path already scans and passes a leave-out list, so part of the pre-scan exists.

### Delete originals after verification

- "Delete the originals once the archive checks out"
	- Rename to "Delete originals after verification".
	- After archive completion, if they don't match, be more specific about why the originals couldn't be deleted.
		- E.g. `"because [[[A folders][ and ][B files] selected, aren't in the archive][, [and]][[X folders][ and ][Y files] are in the archive that weren't selected]]|[total selected size is N but archive shows M]."`
			- The part about size mismatch should be left out as irrelevant if the file counts don't match.

- With following on, the delete check compares against what was meant to go in, followed content included.

### Volume sizes

- "Volume size" options: include what each is good for in parentheses. E.g. "4 GiB (max FAT32 size)".
	- FAT32 caps a file at 4 GiB less one byte, so a 4 GiB volume doesn't fit. Add a 4095 MiB entry for it.

## Why the reset

- Today's two boxes, "store links" and "follow links", make four cases. Store plus follow means nothing. Neither ticked quietly follows linked files but leaves linked folders out. Separate choices per kind of link remove both.

## Open questions

- On ZFS and btrfs each dataset or subvolume is its own filesystem. With the mount option off, nested datasets under a selected folder are left out, as with `find -xdev`. OK?

- The mount option is disabled with no folder selected. But a selected file symlink can lead to another filesystem, and with "Follow" on and the mount option stuck off, that file is left out. Suggest enabling it when any folder or link is selected.

- After the repeat-path rule, a file reached only through a symlink by one path, and only through a junction by another, keeps neither flag. It then counts even with both set to Ignore. Windows only, and rare. Suggest accepting that. The exact fix keeps, per file, which of the eight option sets let it in.

- Links that lead nowhere: 2026092813381404 stores them as links in every format that can, even with "store links" off. Under the new choices, suggest "Ignore" leaves them out like any other link, and "Follow" and "Store" store them, since there is nothing to follow.

- `-spd` is only in the built-in 7z command lines, so a line edited in the settings keeps the wildcard bug from 2026092813381416. Add it when missing, warn, or leave it?

- Should the library's 7z writer offer "Store as Symlinks"? It already keeps links that lead nowhere as links. That would move 7z jobs that store links off the 7-Zip program, and give 7z link storing back on Windows.

- Moving today's archive and extract code into the layers: as its own change now, or a part at a time as each is touched? Suggest the new scan first, then the rest as touched.
