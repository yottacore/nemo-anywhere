<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere dedupe and thumbnails

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [Making thumbnails](#making-thumbnails)
	- [Showing thumbnails](#showing-thumbnails)
	- [File formats](#file-formats)
	- [Looking up a thumbnail](#looking-up-a-thumbnail)
	- [Deduping](#deduping)
		- [Hardlinks](#hardlinks)
		- [Copy-on-Write clones](#copy-on-write-clones)
	- [Finding duplicates](#finding-duplicates)
		- [Deduping in place](#deduping-in-place)
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

Two things built on what the app knows about files: thumbnails, and finding and deduping copies. Both stand on [TukzedoFS](20260930-145641_tukzedofs.md), which has the database, the tables, and how one file is told apart from another.

- Thumbnails are built and in use. They're made at the size they're shown at, stored in the file cache, and found again by content, so a moved or copied picture doesn't get made twice.

- Dedupe is design only. It will find files with the same content and let them share storage, as Copy-on-Write clones. It never deletes, moves or hardlinks anything.

## Specification

- Thumbnails:
	- Made at the size they are drawn at, rounded up to a step of 128 pixels, and made again bigger when a draw wants more.
	- JPEG at quality 90, PNG only for pictures with see-through parts.
	- A folder of pictures on a local disk is made in full, strictly top down in view order.
	- A file that has to be read in full for its thumbnail is hashed at the same time.
	- A render that failed is stored too, so a broken file isn't tried again every launch.
	- Nothing on a network share is read unless it's being looked at.

- Dedupe:
	- Our future deduping should never:
		- Delete or trash files.
		- Dedupe small files - too small can cost more than savings, and/or not worth the effort (and risk if moving).
		- Offer hardlinking detected duplicates together. See [Hardlinks](#hardlinks).
	- Only Copy-on-Write clones, on the same filesystem.
	- Done in place where the platform allows. See [Deduping in place](#deduping-in-place).
	- Symlinked files are never deduped.
	- Existing hardlinks are left alone unless that is turned on. Off by default.

## Goals

- Pictures show as pictures quickly, in the order they sit in the view.

- A thumbnail is made once, however many copies or names the picture has.

- Free space taken by duplicate files, with no chance of losing one.

### Non-goals

- Deleting duplicates. That's a choice for a person with a list in front of them, not for the tool.

- Hardlinking duplicates. See [Hardlinks](#hardlinks).

- Writing to the shared freedesktop thumbnail folder. It is still read.

## Design

### Making thumbnails

- If the whole file has to be read by our own code to generate a thumbnail anyway, then:
	- Also compute the blake3 checksum even if not needed; it's essentially free.
	- If writing checksums onto files is turned on, create or update the checksum and related attributes in xattrs or Windows alternate data stream. See [Checksums on the files](20260930-145641_tukzedofs.md#checksums-on-the-files).

- A file that is not read in full, such as a camera raw file whose preview is all that's read, is still read in full for a checksum if it has none. A file handed to an external tool, such as ImageMagick, should be fresh in the disk cache by then. A camera raw file mostly is not, since only its preview was read. Either way, the cost is paid back later by not making its thumbnail again.
	- That read comes after the thumbnail is drawn, so the draw is not held up.

- The size and time a thumbnail job carries are from when the view last looked. They are read again once the checksum is done, and a file that changed in between gets no checksum that time. Otherwise the new contents' checksum would be stored with the old size.

- A thumbnail is made at the size it is being drawn at, rounded up to a step of 128 pixels, and made again bigger when a draw wants more than is stored. So each picture is kept at the largest size it has actually been shown at, and a folder only ever seen small stays small on disk.

- JPEG at quality 90, and PNG only when the picture has see-through parts. An alpha channel that is opaque everywhere still counts as a photo.

- A render that failed is stored too, with no image, so a broken file is not tried again on every launch. Editing the file clears it.

### Showing thumbnails

- A thumbnail is read on a worker thread and decoded no bigger than the draw needs, so a picture stored at 640 and shown in a list takes a 128 pixel copy in memory. JPEG decodes straight to a smaller size, which is far cheaper than decoding in full and scaling.

- The file's own type icon stays up until its thumbnail is ready. There is no "loading" icon in between, since few themes have one and the stand-in flashed. An edited file keeps its old thumbnail until the new one is made.

- A folder of pictures on a local disk has all its thumbnails made once it has loaded, strictly top down in view order. Nothing is asked for while it loads, since where a file ends up is only known once the whole folder is in. Scrolling does not change the order: a file that comes into view waits its turn, and so does a picture stored on an earlier visit. A new sort or zoom queues the folder again in its new order.

- Each picture is also kept in memory as it is made or found in the store, still in that order, so scrolling anywhere finds it drawn already. That stops at a limit, 1 GiB by default. Past it only the pictures within two screens of the view are kept. Those are read back from the store several at a time, and the picture drawn longest ago makes room, one from a folder no view shows first.

- A folder that is left keeps its pictures for a minute, in case it is gone back to. Opening another folder of pictures lets them go at once.

- Thumbnails are made on half the processors by default, so neighbors can finish a little out of order. None can go ahead of its place in the queue. A folder that is not mostly pictures, or is on a share, is only made as it comes into view, top down on each screen.

- Two stacked progress bars in the status bar, while a run lasts more than a moment. The top one is how much of the current run is made. The bottom one is how many pictures are drawn, counting every picture in the folder, and it's never further along than the top one. Hovering over either gives the counts. The space stays reserved, so the status text does not jump sideways.

- Reload makes the folder's thumbnails again, as it always has. It forgets the stored copy and stops using the freedesktop one for those files. The freedesktop folder itself is left alone.

- Thumbnails another program already made in the shared freedesktop folder are used rather than made again. Nothing is written back to it. On Windows and macOS there was never anything to share with.

### File formats

- Photoshop files are read by a small reader of our own, since gdk-pixbuf has none. A .psd or .psb carries a flattened copy of the picture after its layers, and that is all a thumbnail needs, so the layers are skipped. It is shrunk while it is decoded, so a large file never sits in memory at full size. Grayscale, indexed, RGB and CMYK are read; Lab, multichannel and 32 bit files are not.

- Camera raw files are read by another small reader of our own. Nothing here can develop sensor data, and a library that can is large and slow. Every camera also stores a finished JPEG preview in the file for its own screen, so that is what gets drawn. The reader walks the file's directories, takes the smallest preview that still covers the draw, and decodes it already shrunk. Previews under 320 pixels are passed over while a bigger one exists, since the small ones are often letterboxed. The file's own orientation is applied, since the preview is stored the way the sensor saw it.
	- The TIFF based files are covered this way: DNG, CR2, NEF, ARW, PEF, RW2, SRW and kin. ORF keeps its preview in the maker note, which is read too. RAF and CR3 are other containers and have a path each. Canon CRW, Minolta MRW and Sigma X3F are not read.
	- A thumbnail takes a few milliseconds and reads a few directories and one JPEG, never the sensor data. So a raw file is not checksummed as a side effect of its thumbnail, the way a file read in full is.

- Formats nothing in the process reads go to ImageMagick, when it is installed. It is the last thing tried, after gdk-pixbuf and the two readers above, and covers JPEG 2000, HEIC, AVIF, EXR, DDS, TGA, FITS and the older raw containers. Linking a decoder for each would add a library per format to every build, most of them to the portable Windows exe as well, for files few people have. ImageMagick is already on most Linux desktops, has a Windows installer, and learns new formats on its own schedule.
	- It runs as a separate program, one file per run, on the same worker threads as every other thumbnail.
	- Only formats on a fixed list are handed over, chosen by extension, and the format is named in the command rather than guessed. ImageMagick also reads scripts, vector files and pseudo-files, and a file named for one could otherwise be read as one.
	- It never sees a file name. The file goes in on stdin and the PNG comes back on stdout. ImageMagick reads meaning into names, such as `%d` as a frame number, and versions 6 and 7 escape that differently, so no one spelling works for both.
	- Memory, disk and time are capped for each run, and a run is killed after 30 seconds like any other external thumbnailer. A file over 256 MiB is left alone, since ImageMagick reads all of its input into memory first. Both "disable all" and the per-type list in the thumbnailer settings apply.
	- `magick` is looked for first. On Linux, ImageMagick 6's `convert` is used when there is no `magick`. On Windows `convert.exe` is the system tool that converts a FAT drive to NTFS, so it is never run there.

### Looking up a thumbnail

This is the planned lookup, on the [TukzedoFS tables](20260930-145641_tukzedofs.md#tables).

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

### Deduping

- Existing hardlinks are worth deduping too, optionally. In user data they are rarely a good thing. Replacing one with a CoW clone saves almost as much space, and the two can then safely diverge.
	- Off by default.
	- Trees kept by backup tools that depend on their hardlinks, such as rsnapshot, are always skipped. How to spot one is still open.

- Symlinked files should never be considered for deduplication; for now, only for thumbnail generation.

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

- Ordinary copies are already clones where the filesystem allows. See [Clone copies](20260930-145641_moving_and_copying.md#clone-copies).

### Finding duplicates

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

### Open questions

- How to spot a tree kept by a backup tool that depends on its hardlinks, such as rsnapshot, so it's always skipped.

- Whether `FIDEDUPERANGE` works on ZFS.

- The minimum and maximum dedupe sizes, and where they are set.

- What the duplicate finder shows, and what a person can do from it besides dedupe.

- A thumbnail a thread has already started is finished even after its folder is left. A 42 MB Photoshop file took about 11 s. The readers take no cancel yet (2026093013002529).

## Alternative ideas

### Unconsidered

- Thumbnails for video, from a frame partway in.

### Rejected

- Hardlinking duplicates together. See [Hardlinks](#hardlinks).

- Deleting or trashing duplicates from the deduper.

- Replacing a duplicate with a fresh clone renamed over it, where the platform can share storage in place. It loses the file's inode, hardlinks, attributes and owner.

- WebP thumbnails. gdk-pixbuf can't write them and the Windows build can't read them.

- Linking a decoder for every picture format. It adds a library per format to every build, and to the Windows exe.

- Batching several files into one ImageMagick run. It saved about 15%, but loses the top-down order and canceling one file.

- A "loading" icon while a thumbnail is made. Few themes have one, and the stand-in flashed.

### Superseded

- Keeping thumbnails in the shared freedesktop folder, a PNG per file named by a hash of its path. That was the plan until 2026-09-21, and the call on 2026-09-05 to stay with it was reversed. The new needs can't be met in that kind of store: a thumbnail kept at the largest size it was shown, files known again after they move, and pruning by how often something is drawn. The dependency that argued against a database turned out to be one apt line per Linux container, and nothing at all on Windows.

- The older sweep of the freedesktop folder, and its two settings. Nothing here writes there any more, and the programs that do can look after it.

- A 256 pixel largest thumbnail, which a big icon showed scaled up. Replaced by the 128 pixel steps.

## Research findings

- Copy-on-Write clones, by platform: FICLONE on Btrfs, ZFS, XFS, OCFS2 and bcachefs on Linux, `clonefile()` on APFS, and FSCTL_DUPLICATE_EXTENTS_TO_FILE on ReFS. ZFS needs OpenZFS 2.2 or later with block cloning on.

- ImageMagick's time goes into decoding, not starting up. One run per file costs about 15% more than a batch.

- `convert.exe` on Windows is the FAT to NTFS converter, not ImageMagick.

- ImageMagick 6 and 7 escape `%` in file names differently.

## Roadmap

- Cancel a started thumbnail once nothing wants it (2026093013002529).

- Move thumbnails onto the planned TukzedoFS tables.

- The duplicate finder, then dedupe in place.

## Related backlog issues

- File uniqueness design. Opened 20260925-063617.

- Feature: Find duplicate files and directories. Opened 20260908-111526.

- Thumbnails (PSD, flashing, whole folder top down). Done.

- Additional thumbnailer formats. Done.

- Thumbnail scan progress bars, and the two stacked bars. Done.

- Better thumbnail cache management. Database plus background pruning. Done.

- 2026092813381401: zooming while thumbnails render can store a small one as full size.

- 2026092813381410: a small PSD file can tie up a thumbnail thread for minutes.

- 2026092813381419: an Olympus raw file with a looping directory takes seconds to read.

- 2026093013002529: a thumbnail already being made runs to the end after its folder is left.
