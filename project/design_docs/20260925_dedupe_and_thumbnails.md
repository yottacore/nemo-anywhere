<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere dedupe and thumbnails

File uniqueness design, for thumbnails now and deduping later. Companion to [design.md](../design.md), whose [File cache](../design.md#file-cache) section has what is built today.

Status: first idea pass. None of the tables below exist yet. When built, they replace the file cache's current tables. Before release there is no migration: an old database is thrown away and rebuilt, as it already is whenever the tables change.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [General principles and gotchas](#general-principles-and-gotchas)
	- [Generating thumbnails](#generating-thumbnails)
	- [Deduping](#deduping)
		- [Hardlinks](#hardlinks)
		- [Copy-on-Write clones](#copy-on-write-clones)
- [Other facts](#other-facts)
	- [Native attributes to consider](#native-attributes-to-consider)
- [Our method for duplicate detection](#our-method-for-duplicate-detection)
	- [Tables](#tables)
		- [Table: devices](#table-devices)
		- [Table: dirs](#table-dirs)
		- [Table: files](#table-files)
		- [Table: content](#table-content)
		- [Table: thumbnails](#table-thumbnails)
		- [Table: jobs](#table-jobs)
		- [Table: journal](#table-journal)
	- [Method for thumbnail lookup and generation](#method-for-thumbnail-lookup-and-generation)
	- [Method for general duplicate file detection](#method-for-general-duplicate-file-detection)
		- [Deduping in place](#deduping-in-place)

<!-- /TOC -->

## General principles and gotchas

- Must be generic not just for images (most of which get read anyway for thumbnailing), but also for future functionality (e.g. dedupe).

- Scanning large files for checksum (blake3) should be used only as a last resort when all else fails.
	- And even then, first check the database, then xattrs, to see if it's been scanned before and the hash is still fresh.

- Must be critically aware of path duplicates, i.e. one file reached by more than one path. (E.g. to avoid accidentally deleting the only copy, in future dedupe work.) Such files are not actually duplicates, they're the same one file.
	- Path duplicate detection:
		- Same device and inode.
		- Same filename.
		- Parent directories also have the same device and inode.
	- Device and inode are good enough to compare within one scan. They are not kept as a file's identity from one scan to the next. See [Native attributes to consider](#native-attributes-to-consider).
	- Windows has no inode. The equivalent is the volume serial number plus the file ID.

- Hardlinks are detected and logged as such in the database: same device and inode, but a different filename or parent directory.
	- Like path duplicates, they are one file, not duplicates.
	- For deduping, they are left out unless that is turned on. See [Deduping](#deduping).
	- For thumbnails, hardlinks of one file are only read and checksummed once.

- Must detect infinitely looped symlinked directories.

### Generating thumbnails

- If the whole file has to be read by our own code to generate a thumbnail anyway, then:
	- Also compute the blake3 checksum even if not needed; it's essentially free.
	- Create or update the checksum and related attributes in xattrs or Windows alternate data stream.
		- Writing an attribute changes the file's ctime, which can trick poorly-written backup programs into backing up unchanged content again.
			- On Linux, the BSDs and macOS, ctime can't be reset. The kernel sets it on any attribute write, and there is no call to set it back. That's why writing checksums onto files is a setting, off by default.
			- On Windows, the write tells its handle to leave the file times as they are.

- A file that is not read in full, such as a camera raw file whose preview is all that's read, is still read in full for a checksum if it has none. A file handed to an external tool, such as ImageMagick, should be fresh in the disk cache by then. A camera raw file mostly is not, since only its preview was read. Either way, the cost is paid back later by not making its thumbnail again.

### Deduping

- Our future deduping should never:
	- Delete or trash files.
	- Dedupe small files - too small can cost more than savings, and/or not worth the effort (and risk if moving).
	- Offer hardlinking detected duplicates together. See [Hardlinks](#hardlinks).

- Existing hardlinks are worth deduping too, optionally. In user data they are rarely a good thing. Replacing one with a CoW clone saves almost as much space, and the two can then safely diverge.
	- Off by default.
	- Trees kept by backup tools that depend on their hardlinks, such as rsnapshot, are always skipped. How to spot one is still open.

#### Hardlinks

- This is a common (and very alluring) method used by dedicated file deduplicators - multiple programs covering all OSes.

- However, it is incredibly risky in ways most users (if not even developers) haven't thought of. For example:
	- Many OSes create template metadata files in many directories. (E.g. Windows, macOS.) By default, they are all the same. If you hardlink them all together:
		- The OS might be constantly churning on them because it notices that most of them are wrong,
		- Or at minimum, the information in all but one of them will be guaranteed to be wrong at all times. This alone could be disastrous if not even monetarily expensive for the user, depending on the circumstances.
	- Some users copy large custom documents as default templates for project work. (And if using a shared drive, you may never even know these exist, nor that they're identical content in spite of having different names and paths.)
		- If they get "deduped" by hardlinking together, you might spend hours or days updating a document - only for its hardlinked sibling (that might have been hardlinked years earlier) to be edited - possibly by yourself - without realizing it's also catastrophically changing a "different" document you spent hours or days on, that you haven't looked at in years, and may not be aware was ruined until several more years, after backups of the original content have rotated out.
	- And...I know this because hardlinked "dedupes" have bitten me in all of these ways in the past.

- The *only* safe, legitimate, and broadly applicable use of hardlinks (for our purposes but also idealistically) is:
	- For use by automated tools that keep versions of entire directory trees. Often with pruning algorithms. (E.g. older versions of Apple's "Time Machine", and some other backup utilities such as rsnapshot.)
	- Or other similar uses that are unambiguously, with no room for confusion, meant to represent the *same* actual file - and *never* just "has the same content".

#### Copy-on-Write clones

The only safe option for deduping Nemo Anywhere will ever consider: Copy-on-Write clones, for directory structures on the same filesystem.

- This reduces the disk usage of large files to close to just the one original file. But if any clone is later edited, only that one is changed. (And only the new content needs additional storage.)

- Supported so far by:
	- Linux: FICLONE ioctl on Btrfs, ZFS, XFS, OCFS2, bcachefs.
		- ZFS needs OpenZFS 2.2 or later with block cloning turned on. Try the clone anyway; if it fails, fall back to a plain copy.
	- macOS: clonefile() on APFS
	- Windows: FSCTL_DUPLICATE_EXTENTS_TO_FILE on ReFS. CopyFileEx does it on its own since Windows 11 KB5034848.

- Deduping makes the clone in place where the platform allows it. See [Deduping in place](#deduping-in-place).

- On Linux, Nemo Anywhere already does all copy operations on supported filesystems, with essentially the same thing as `cp --reflink=auto` - so that if it's possible and beneficial (e.g. not for tiny files), copies will be clones. There is no risk and almost never downsides to this being unchangeable hard-coded behavior, only potentially massive benefits. (And the downsides are trivial.)
	- A file under 64 KiB is copied in full rather than cloned. The limit is `performance.clone-min-kib` in the settings file, and 0 always clones.

## Other facts

- Symlinked files should never be considered for deduplication; for now, only for thumbnail generation.

- Symlinked folders can be followed, but the canonical path must be read and considered for the case of path duplicates.

### Native attributes to consider

- Good attributes:
	- Canonical file path is not reliable (e.g. files and folders can be renamed and/or moved), but still an important part of early identification pass.
	- mtime and size are good, important, and reliable attributes for a first pass.

- Attributes to use with care:
	- crtime is not always available, or reliable - but can still be useful for the algorithm if that's kept in mind.

- Unreliable attributes, don't use:
	- inodes can be reused on most filesystems, and aren't even always unique between remounts (e.g. FUSE filesystems).
	- Unique device identification (a necessary part of unique inodes) is also not universally reliable across platforms and filesystems.
	- Both are still fine for comparing files within one scan, such as to find path duplicates and hardlinks. They are just not stored as identity.

## Our method for duplicate detection

Store in a local fast cache (e.g. SQLite database on SSD); populate only as-needed as target files are scanned.

### Tables

First idea pass.

- Tables like `dirs` may need further optimization for size.
	- E.g. Millions of entries that contain most of the same path information, would get slow.
	- Maybe a node-based rather than SQL database might be a better choice?
	- For `dirs`, this is answered by storing it as a tree. Each row holds its parent and its own name, and full paths are built by walking up and cached in memory. Renaming a folder then changes one row, however much is under it.

- Rows are deleted outright when they are no longer needed, in one transaction, with cascades to the rows that hang off them.

#### Table: devices

| Column                          | Notes
| :---                            | :---
| `id`                            |
| `is_removable`                  | Not always knowable or reliable.
| `is_spinning`                   | Not always knowable or reliable.
| ~~`seen_dt_first`~~             | Dropped until something reads it.
| `seen_dt_last`                  |
| ~~`seen_count`~~                | Dropped until something reads it.
| ~~`not_seen_dt_first`~~         | Dropped until something reads it.
| ~~`not_seen_dt_last`~~          | Dropped until something reads it.
| ~~`not_seen_count`~~            | Dropped until something reads it.
| `not_seen_count_current_streak` | How many attempts to see but didn't exist, reset to 0 every time seen.
| `last_device_ident`             | The device's identity, where it can be had: the filesystem UUID on Linux, the volume GUID on Windows, the volume UUID on macOS.
| `mount_path`                    | Where it was last mounted. Used as identity only when there is no `last_device_ident`.
| `is_canonical`                  | Not always reliable (e.g. bind mounts). Guess if necessary by shortest path.
| `canonical_device_id`           | Another id in same table, if is_canonical == 0

- Unique indexes: `last_device_ident` where known. Otherwise `mount_path`.

- A drive mounted in a different place from last time is still the same device.

#### Table: dirs

| Column                          | Notes
| :---                            | :---
| `id`                            |
| `device_id`                     | Id in `devices`.
| ~~`seen_dt_first`~~             | Dropped until something reads it.
| `seen_dt_last`                  |
| ~~`seen_count`~~                | Dropped until something reads it.
| ~~`not_seen_dt_first`~~         | Dropped until something reads it.
| ~~`not_seen_dt_last`~~          | Dropped until something reads it.
| ~~`not_seen_count`~~            | Dropped until something reads it.
| `not_seen_count_current_streak` | How many attempts to see but didn't exist, reset to 0 every time seen.
| `parent_id`                     | Another id in same table. Empty for the root of a device, which is at `devices.mount_path`.
| `name`                          | This folder's own name, not the full path.
| `is_canonical`                  | Not always reliable (e.g. bind mounts). If not sure, =1.
| `canonical_dir_id`              | Another id in same table, if is_canonical == 0

- Unique indexes: {`parent_id` + `name`} (together)

- A path is unique, but not necessarily the only canonical path to the same thing.

#### Table: files

| Column                          | Notes
| :---                            | :---
| `id`                            |
| ~~`seen_dt_first`~~             | Dropped until something reads it.
| `seen_dt_last`                  |
| ~~`seen_count`~~                | Dropped until something reads it.
| ~~`not_seen_dt_first`~~         | Dropped until something reads it.
| ~~`not_seen_dt_last`~~          | Dropped until something reads it.
| ~~`not_seen_count`~~            | Dropped until something reads it.
| `not_seen_count_current_streak` | How many attempts to see but didn't exist, reset to 0 every time seen.
| `deduped_dt_first`              |
| `deduped_dt_last`               |
| `deduped_count`                 |
| `dir_id`                        |
| `filename`                      |
| `bytes`                         | Compared with mtime to know the record is still fresh. Also what dedupe groups on first, before anything is hashed.
| `mtime`                         | Reliable but not useful for finding uniques
| `crtime`                        | Can be empty. Not always available, or reliable. Used when picking the canonical copy.
| `link_count`                    | Number of hardlinks. Over 1 means the file is hardlinked.
| `hardlink_of_id`                | Another id in same table, if seen in the same scan as a hardlink of this one (same device and inode).
| `content_id`                    | Empty until the file has a full hash.

- Unique indexes: {`dir_id` + `filename`} (together)

- Files with the same `content_id` are confirmed identical.

- A file whose `deduped_dt_last` is newer than its `mtime` already shares storage with the others. An edit since then moves the mtime, and the file is no longer counted as deduped.

#### Table: content

| Column                          | Notes
| :---                            | :---
| `id`                            |
| ~~`seen_dt_first`~~             | Dropped until something reads it.
| `seen_dt_last`                  |
| ~~`seen_count`~~                | Dropped until something reads it.
| ~~`not_seen_dt_first`~~         | Dropped until something reads it.
| ~~`not_seen_dt_last`~~          | Dropped until something reads it.
| ~~`not_seen_count`~~            | Dropped until something reads it.
| `not_seen_count_current_streak` | How many attempts to see but didn't exist, reset to 0 every time seen.
| `sample_hash_start`             | Can be empty. Hash of first min(N, floor(25% of file size)) bytes
| `sample_hash_middle`            | Can be empty. Hash of middle min(N, floor(25% of file size)) bytes
| `sample_hash_end`               | Can be empty. Hash of last min(N, floor(25% of file size)) bytes
| `hash_full`                     | Required, and unique in this table. (Many kinds of files legitimately have the same content.)

- Unique index: `hash_full`

- A content row can't exist without a full hash. Until a file is fully hashed, its `content_id` is empty, and nothing is shared with it.
	- So two unrelated files that only happen to be the same size never share a record, or a thumbnail.

- mtime and size are not part of content. They belong to a path, in `files`.

#### Table: thumbnails

| Column                          | Notes
| :---                            | :---
| `id`                            |
| ~~`seen_dt_first`~~             | Dropped until something reads it.
| `seen_dt_last`                  |
| ~~`seen_count`~~                | Dropped until something reads it.
| ~~`not_seen_dt_first`~~         | Dropped until something reads it.
| ~~`not_seen_dt_last`~~          | Dropped until something reads it.
| ~~`not_seen_count`~~            | Dropped until something reads it.
| `not_seen_count_current_streak` | How many attempts to see but didn't exist, reset to 0 every time seen.
| `content_id`                    |
| `type`                          | E.g. jpg, or png for transparency
| `size`                          | Icon size it was rendered for, in pixels. A draw that wants more renders it again, bigger.
| `width`                         | What the stored image actually is. 0 for a render that failed, with no image behind it.
| `height`                        |
| `stored`                        | When it was rendered.
| `rendered`                      | When it was last drawn. For pruning.
| `renders`                       | How many times it has been drawn. For pruning.
| `thumbnail`                     | Binary. Empty for a render that failed, so a broken file is not tried again on every launch.

- This table is sparse 1:1 with content. All thumbnails will have content, but not all content will have thumbnails.

- A thumbnail can only be stored once its file is fully hashed. A file hashed after its thumbnail is drawn, such as a camera raw file, keeps the thumbnail in memory until then.

#### Table: jobs

If a dedupe job would take too much time and/or memory, use a journal method that cuts way down on memory, and is more crash-recoverable.

| Column                  | Notes
| :---                    | :---
| `id`                    |
| `owner`                 | The process that holds the job.
| `heartbeat`             | Updated as the job works. One nobody has touched for ten minutes belongs to a process that died, and is taken over.
| `dt_started`            |
| `dt_last_paused`        |
| `dt_last_resumed`       | Cleared on start or pause; set on resume
| `dt_finished`           |
| `count_files_total`     |
| `count_files_processed` |
| `base_dir_id`           | The directory the job is rooted in - to 1) help avoid simultaneous redundant or overlapping work, and 2) as part of knowing whether to resume a previous paused or crashed job, or start a new one.

- Each window is its own process, so two could start the same job. A job is claimed in a write transaction, the same way the file cache's prune is, so only one process can hold it.

- Thumbnails don't use jobs. Each thumbnail is stored as soon as it's made, so a crash loses at most the one in progress.

#### Table: journal

| Column               | Notes
| :---                 | :---
| `id`                 |
| `job_id`             |
| `file_id`            | Id in `files`.
| `stage`              | How far this file got: grouped by size, then first, middle and last samples, then the full hash.
| `sample_hash_start`  | Kept here until the full hash is known, then copied to `content`.
| `sample_hash_middle` |
| `sample_hash_end`    |
| `is_canonical`       |
| `dupe_journal_id`    | If is_canonical == false, this must eventually be populated.
| `is_finished`        | If ruled out as a dupe, or verified as a dupe.

- `stage` and the sample hashes let a resumed job carry on from where each file got to, rather than start the grouping over.

### Method for thumbnail lookup and generation

- Build a list of image files in the directory. Leave out files too small to be a valid image, and files larger than the thumbnail size limit.

- Note which files are hardlinks of each other. Only one of each set is read and checksummed, and the rest share its results.

- Work the thumbnails in order that they are sorted in the view, top-down. For each:
	- Look up the file's path in the database first. If its size and mtime still match, use what's there and go straight to the thumbnail checks below. This is the usual case, and it reads nothing from the file.
	- Otherwise, check for a valid fresh checksum in xattrs (comparing mtime and size in xattrs with actual).
	- If a current checksum is not found:
		- If making the thumbnail reads the whole file anyway, calculate checksums for first bytes, middle bytes, last bytes, and full while it's read.
			- If writing checksums onto files is turned on, write or update the checksum and freshness attributes to the file's xattrs.
		- Otherwise, read the whole file for the checksum after its thumbnail is drawn, so the draw is not held up.
	- Look up the checksum in the database.
		- If it exists, point the file at that content row, and use its thumbnail. No canonical copy is chosen; that is only for deduping.
		- If not, create the content row.
	- If a thumbnail is found but it's too small, generate a new one at the current view size.
	- If no thumbnail is found, generate one at the current view size.

### Method for general duplicate file detection

- Read the list of candidate files. Only files inside the size range for the job are candidates; anything smaller than its minimum or larger than its maximum is left out.
	- For deduping, leave out files smaller than the minimum dedupe size, and files larger than the maximum dedupe size.
		- Follow folders [and optionally folder symlinks]
	- For thumbnails, leave out files too small to be a valid image, and files larger than the thumbnail size limit.

- Add or update tables with knowable information so far:
	- Including: devices, dirs, files (not content or thumbnails yet)
	- Including hardlinks, found by device and inode within this scan.

- Hardlinks of one file are hashed only once, and share the result.
	- For deduping, they are left out unless that is turned on. Then they are candidates like any other file, and all but one can be deduped as below.

- Decide if the set of files is small enough by count and total size, to work in memory, or to use `jobs` & `journal` tables to save on memory and be resumable.

- A file that already has a fresh `content_id` skips the hashing below, and goes straight into its full hash group.

- Group files by identical size (either in-memory if small & fast enough, or in `jobs` & `journal`). Within each group of count >1:
	- Read and hash partial first bytes
	- Group files by partial first bytes matches. Within each group of count >1:
		- Read and hash partial middle bytes
		- Group files by partial middle bytes matches. Within each group of count >1:
			- Read and hash partial last bytes
			- Group files by partial last bytes matches. Within each group of count >1:
				- Perform a full hash
				- Group files by full hash matches. Within each group of count >1:
					- Determine the "canonical", "one-true original":
						- Sort and group by crtime, if available. If [not all available] or [earliest time has count >1]:
							- Sort and group by mtime. If [not all available] or [earliest time has count >1]:
								- Sort and group by *canonical* directory path length. If the shortest path has count >1:
									- Sort and group by file name length. If the shortest filename has count >1:
										- Sort alphabetically.
					- Then pick the first one from that result, and mark it as canonical (in-memory or journal).
					- Point the other dupes at the canonical ID (in-memory or journal).
					- Dedupe each of the others against the canonical one, in place. See [Deduping in place](#deduping-in-place).

- Add or update the database with all the new generated metadata (hashes etc.).
	- Including, deleting any now-proven-obsolete matching rows.

#### Deduping in place

A duplicate is never replaced by a new file where that can be avoided. Making a fresh clone and renaming it over the duplicate loses its inode, hardlinks, xattrs, ACLs and owner. It also races with any program that writes the file between the hash and the swap.

- Linux: `FIDEDUPERANGE` ioctl. The kernel checks that the bytes are identical and shares the storage in one step. The file itself is untouched.
	- Works on Btrfs and XFS. ZFS needs checking.

- Windows: `FSCTL_DUPLICATE_EXTENTS_TO_FILE`, on ReFS, also works on the existing file. It doesn't check the bytes, so compare them first.

- macOS: only `clonefile()`, which makes a new file. This is the one platform that needs the replace.
	- Carry the xattrs, ACLs, owner and times over to the new file.
	- Check the duplicate hasn't changed just before the swap.
	- A hardlinked duplicate is skipped, since the swap would break the link.

- Set `deduped_dt_last` on each file deduped.
