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
	- [Options expander](#options-expander)
	- [Options the writer can't do](#options-the-writer-cant-do)
	- [Link handling](#link-handling)
		- [Link options](#link-options)
		- [Junction defaults](#junction-defaults)
		- [Nested and other filesystems](#nested-and-other-filesystems)
		- [Total size](#total-size)
	- [Counting sizes](#counting-sizes)
		- [Path list](#path-list)
		- [Size totals](#size-totals)
		- [Background scan](#background-scan)
	- [After OK](#after-ok)
	- [Delete originals after verification](#delete-originals-after-verification)
	- [Volume sizes](#volume-sizes)
- [Why the reset](#why-the-reset)
- [Archiver programs](#archiver-programs)
	- [7-Zip first for 7z](#7-zip-first-for-7z)
	- [Wildcards in an edited 7-Zip line](#wildcards-in-an-edited-7-zip-line)

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

- Order of the work, so nothing is moved twice:
	- First the interfaces, and a place in the tree for the core. Nothing moves yet.
	- Then the reset, written in the core from the start. It rewrites most of the link and walk code anyway.
	- Then the rest of the archive and extract code moves over in one pass, with no change in what it does. The archive and extract tests stay as they are, and pass before and after.

- Core tests run with no display.

## Compress dialog

### Options expander

- If any stored options (from last session) are non-default, auto-expand "Options".
	- It compares the remembered choices, so one the format has forced off still counts.

- Add a button or icon next to the "Options" expander, to reset to default.
	- It puts every choice under Options back to its default, and drops the remembered ones from the settings file.
	- Then it collapses Options.
	- Disabled when everything is already at default.

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

- A link that leads nowhere: "Ignore" leaves it out like any other link. "Follow" and "Store" keep it as a link, since there is nothing to follow.
	- Until this is built, 2026092813381404 stays as it is. It keeps these links even with "store links" off.

- The library's 7z writer offers "Store as Symlinks". It already keeps links that lead nowhere as links.
	- 7-Zip is used first for 7z where it's installed (see [7-Zip first for 7z](#7-zip-first-for-7z)). So this matters on Windows, where 7-Zip leaves links out. A job that stores links goes to the library there, on one thread.
	- When that is the case, the flyover text for "Store as Symlinks" says it forces single-threaded compression.
	- With a password or volumes the job still needs 7-Zip, so on Windows the choice is disabled.

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

#### Nested and other filesystems

- Two options below all that:
	- [ ] Follow nested filesystems (size Δ: N)
		- For ZFS, Btrfs, etc.
		- Checked by default.
	- [ ] Follow other filesystems (size Δ: N)
		- Was "Follow across mounted filesystems".
		- Never checked by default.
	- "Follow nested filesystems" is disabled unless a folder or link is selected (of any type).
	- "Follow other filesystems" is disabled unless a folder or link is selected (of any type).

- A link that points onto a nested or other filesystem is followed only when both are on: its link option, and that filesystem option. Either one off, and it isn't followed. That's the same as a link to a local folder with Follow off.

- Nested means the same pool or volume on both sides of the mount: one ZFS pool, one Btrfs filesystem, one APFS container. Anything else is another filesystem.

- Windows has no nested kind. A volume mounted in a folder is another filesystem, so the nested option is hidden there.

#### Total size

- On the left side of the dialog, same row as Cancel/OK:
	- Total size to include: N

### Counting sizes

How to calculate the five different sizes shown.

#### Path list

- Build a structured list, e.g. `canonicalFullFilePath`, with one entry per file:
	- path
		- This field must be ultra fast to add up to millions of paths to, and to match one value in a large set.
	- bytes
	- countedIn
		- Which of the sixteen totals below already have this file's bytes. One bit each.

- Each path the scan takes to a file has four flags. None of them are mutually exclusive - it's possible for all to be true. They describe the path, not the file, so a file reached by two paths can have two sets.
	- isSymlinked
		- A symlinked file or a descendant of a symlinked folder
	- isJunctioned
		- A descendant of a junction folder
	- isNestedFs
		- The path crossed into a nested filesystem.
	- isDifferentFs
		- The path crossed onto another filesystem.
		- Just because a file may be under a cross-FS mount, doesn't mean it's not a symlink that points back to the same FS as CWD.
			- But even so, it won't be scanned if "Follow other filesystems" is off.
			- So it's the path that counts, not where the file ends up. Otherwise the totals and the archive would disagree.
		- CWD here is the folder the selection is in, not the program's working folder.

#### Size totals

- Build a structure for total sizes, e.g. `totalSizes`, with one total for each mix of the four follow options, sixteen in all. Each one is the size of what that mix would put in the archive.

- When the scan reaches a file by a path:
	- Find the mixes that let that path in. A mix lets it in when it follows every flag the path has.
	- Add the file's bytes to each of those totals that the file's `countedIn` doesn't have yet, then mark them in `countedIn`.

- So a file counts once in every mix that lets in any path to it. Which path the scan found first doesn't matter.
	- A file in `v2/` that is also reached through a `current -> v2` link counts in all sixteen, since the direct path needs nothing.
	- A file reached only through a symlink by one path, and only through a junction by another, counts wherever symlinks or junctions are followed, and nowhere else.

- This replaces the eight flag totals of the first draft. One set of flags per file can't be exact when a file has two paths. The flags the paths share count it where neither path goes. All the flags of both leave it out where one path alone would bring it in.

- The cost is two bytes per file, and at most sixteen additions per path.

- This is also the live count. Each path adds to the totals as it's found, so nothing is worked out again at the end.

#### Background scan

- In a background task, gather a list of all canonical file paths included in the current selection and below, and populate `canonicalFullFilePath`, with the following exclusions:
	- If "follow symlinks" selection is off, don't count any files that are symlinks at any level, and don't follow any folder symlinks, at any level.
	- If "follow junctions" is off, don't follow any folder junctions, at any level.
	- If "Follow nested filesystems" is off, don't follow any folders that are mounted from a nested filesystem, or links of any kind that made it past the previous exclusions, but point to a nested filesystem.
	- If "Follow other filesystems" is off, don't follow any folders that are mounted from a different filesystem, or links of any kind that made it past the previous exclusions, but point to a different filesystem.
	- As candidates that made it this far are scanned read:
		- Obtain the full canonical file path.
		- See if that path already exists in the existing `canonicalFullFilePath` list.
		- If it doesn't, add the full canonical file path to the list, with its bytes.
		- Either way, update `totalSizes` and `countedIn` as above.
	- If one of the UI options is changed at any time during this (i.e. "follow symlinks", "follow junctions", "follow nested filesystems", "follow other filesystems"):
		- If an option is *less* inclusive, don't interrupt scan but immediately back out of now-excluded paths, and resume with the next legal path. (But don't remove or change any values of either structure.)
		- If an option is *more* inclusive, restart the scan with the more inclusive option. (Only adding to the file list and doing math as previously unseen paths are discovered.)
	- Every 0.25 second, check to see if any of the values in `totalSizes` have changed, or if any of the four relevant options have changed. If so, update the appropriate GUI elements.
		- Calculations for UI:
			- The total is the `totalSizes` entry for the options as set.
			- A Δ is what turning that option off would take away from the total, with the other three left as set. It's the total less the entry for the same options with that one off, so it's 0 for an option that's off. A file that is both symlinked and junctioned counts in both Δs, so the Δs don't add up to anything.
			- Both are exact. The scan has walked every path the options as set allow, and so every path a mix with one option fewer allows. An entry for a mix that follows more is partial until the scan restarts with it.

- In the future, the file scanning will use the "TukzedoFS" cache - PostgreSQL on Tukzedo Linux, SQLite3 otherwise.

- Pressing Cancel or Enter aborts the filescan and totalling background tasks and releases memory.
	- So the job doesn't reuse the dialog's scan. It runs its own pre-scan after OK.

### After OK

- Once OK is hit, if pre-scanning is required, the progress dialog should have a second progress bar above the regular one, showing pre-scan progress.

- The job works from our own list of what goes in, built by its pre-scan. That is what makes mixed choices work in rar and 7z, such as following symlinks while storing junctions.
	- The 7z path already scans and passes a leave-out list, so part of the pre-scan exists.

- This design might need some extra work, if testing shows that lists of files to include and/or exclude exceeds the command-line length, on any OS.

### Delete originals after verification

- "Delete the originals once the archive checks out"
	- Rename to "Delete originals after verification".
	- After archive completion, if they don't match, be more specific about why the originals couldn't be deleted.
		- E.g. `"because [[[A folders][ and ][B files] selected, aren't in the archive][, [and]][[X folders][ and ][Y files] are in the archive that weren't selected]]|[total selected size is N but archive shows M]."`
			- The part about size mismatch should be left out as irrelevant if the file counts don't match.

- With following on, the delete check compares against what was meant to go in, followed content included.

### Volume sizes

- "Volume size" options: include what each is good for in parentheses. E.g. "4,095 MiB (max FAT32 size)".

## Why the reset

- Today's two boxes, "store links" and "follow links", make four cases. Store plus follow means nothing. Neither ticked quietly follows linked files but leaves linked folders out. Separate choices per kind of link remove both.

## Archiver programs

- The command lines in the settings are base flags. The dialog adds to them, or changes them, as a job needs. They're there to future-proof, or to adjust for a slightly different version of a program, and otherwise shouldn't need editing. The comment above each one in the settings file says so.

### 7-Zip first for 7z

- Where 7-Zip is installed, 7z archives go to it rather than the library. It compresses on as many threads as the settings allow.
	- The library's 7z writer has no thread option. Checked on libarchive 3.7.4, which turns down `7zip:threads`. It can't be given one without changing libarchive.
	- 7-Zip reports its own percent done, so the progress bar still moves.

- The library still writes 7z when 7-Zip isn't installed, or when the job needs what only the library can do. Today that is storing links on Windows.

### Wildcards in an edited 7-Zip line

- 7-Zip reads `*` and `?` in a name as wildcards unless the line has `-spd`. The built-in lines have it. A line edited in the settings keeps what was typed.
	- Where it matters: a selected item or a left-out folder when compressing, and the archive's own path when extracting. Windows allows neither character in a name, so this is Linux, BSD and macOS only.
	- What goes wrong without it: files whose names match go in or are left out along with it, and the job still reports success.

- Add `-spd` at run time when the line runs 7-Zip and doesn't have it. The saved line isn't changed.
	- A line that runs some other program is left alone.

- rar has no such switch, so it keeps refusing a name with `*` or `?`, as it does today.
