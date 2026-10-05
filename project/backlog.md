<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# Project backlog

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Introduction](#introduction)
- [Issues](#issues)
- [Old format](#old-format)
	- [Bugs](#bugs)
	- [Features and enhancements](#features-and-enhancements)
	- [Done](#done)
		- [Done - Bugs](#done---bugs)
		- [Done - Features and enhancements](#done---features-and-enhancements)
	- [Deferred](#deferred)
	- [Canceled](#canceled)
- [Template](#template)

<!-- /TOC -->

## Introduction

Going forward, new issues in the new template at the bottom of this file, will go in the '## New format' section only. No more status emojis. Refer to '## Reference' for sort order. Issues in the old format (with status emojis) won't be refactored, but will continue to be worked until moved to closed, canceled, or deferred sections, and emojis updated. (Eventually this will all be moved to nano-git-db anyway. This new template is an intermediate effort to make issues going forward more structured and importable.)

This is a product backlog just for pre-v1.0.0 release. After that, bugs, features, and enhancements will be managed in Github Issues.

## Issues

- Release page: group the downloads in a table.
	- ID: 2026100413051728
	- Type: Feature
	- Status: Waiting on signoff
	- Needs external testing: the next real release tag, and the release page looked at then.
	- Opened: 20261004-130517
	- Opened by: t00mietum
	- Requirements:
		- When a release is made, its downloads are grouped in a table.
		- CPU architecture in columns, and target OS in rows.
	- Decisions:
		- A row or column shows only when something was built for it. A combination not built is an empty cell. A call made without asking.
		- A cell links each file of that build, such as tar.gz, deb and rpm. Checksums, and files that name no OS and CPU, go in a line under the table. A call made without asking.
		- The table goes after the changelog section and before the build number.
	- Against: design.md said a version with no changelog section falls back to generated notes. Both lanes now write one line pointing at the changelog, as the local cut already did. design.md says so now.
	- Done: both release lanes write the notes through one script, once their own uploads are done, from the files the release holds. So the table is whole whichever lane finishes last. The Windows build's notes keep the build number now too.
	- Note: since `2026100415281302`, only the local cut writes the notes. It writes the table before the upload, from the names the files will have.
	- Swept: every place release notes are written: `release.bash`, and the notes and publish steps in `release-win.yml`. `changelog-notes.bash` is still the one reader of the changelog.
	- Branch: relnotes
	- Commit: 4e5127f
	- Test case: rjf2v5d5 (`test-release-notes.bash`, lint stage).
	- Verified: rjf2v5d5 passes, and the lint stage is clean.

- If the Windows release workflow makes the release before the local cut does, the local cut fails.
	- ID: 2026100415281302
	- Type: Bug
	- Status: Waiting for testing
	- Needs external testing: the next real release tag. The cut should wait for the Windows build, then put up one release with every file.
	- Priority|Severity: Low
	- Opened: 20261004-152813
	- Opened by: work on 2026100413051728
	- Related IDs: 2026100413051728
	- Incorrect behavior: `release.bash` pushes the tag, then runs `gh release create`. The tag starts the Windows workflow, which makes the release itself when none is there yet. If the local step runs late, its create fails.
	- Expected behavior: only the local cut makes a release. Hosted builds only build, and hand their files back to it, so the release goes up whole in one step. The same holds for any later BSD, macOS or ARM build done elsewhere.
		- Answered 2026-10-04, replacing the first fix, where whichever side came second added to the other's release.
	- Reproduced: yes. The new cases in rjf2v5d5 fail on the old `release.bash`, where the create is refused because the release is there.
	- Decisions:
		- If a hosted build fails, no release is made. Run `release.bash --publish` again once it passes; it picks up from the pushed tag. A call made without asking.
		- A release already there for the tag is refused, never added to. A call made without asking.
	- Actual fix: `release-win.yml` only builds, and hands back the exe as a `release-files` artifact under its release name. `release.bash --publish` waits for each hosted build in `RELEASE_WORKFLOWS`, downloads its files, adds their lines to the one sums file, writes the notes with the Downloads table, and makes the release with every file in one `gh release create`.
	- Swept: every `gh release` call. None are left in the workflow. design.md and `cicd/win/signing.md` say the workflow no longer publishes.
	- Branch: relrace, then relone
	- Commit: 24de39d, 3cfa988
	- Test case: rjf2v5d5 (`test-release-notes.bash`, lint stage): a hosted build still running, one that fails and is rerun, a release already there, and a misnamed hosted file.
	- Verified: rjf2v5d5 fails on the old `release.bash` and workflow and passes now.

- A thumbnail already being made runs to the end after its folder is left.
	- ID: 2026093013002529
	- Type: Enhancement
	- Status: Waiting for testing
	- Needs local test suite run?: no. The full Linux suite passed 169 of 169 on 20261004, on thumbstop.
	- Needs external testing: rjffcm7d natively on Windows. Its ImageMagick case checks that a `magick.exe` nothing wants any more is ended and its thread freed. It passes under wine, which says little about how Windows ends a program.
	- Opened: 20260930-130025
	- Opened by: review of code review 20260928 item 10
	- Related IDs: 2026092813381410
	- Requirements:
		- Leaving a folder drops its queued thumbnails, but one a thread has started is finished. A 42 MB Photoshop file of 30000 by 30000 took about 11 s of a thread. The readers and the thumbnail factory take no cancel from the thread.
		- Stop a started thumbnail once nothing wants it, for every reader.
	- Decisions:
		- Nothing wants a thumbnail once its file is let go, the same moment a queued one is dropped. A bigger size asked for while a smaller one is made still lets the smaller one finish, as item 2026092813381401 settled. A call made without asking.
		- Quitting stops every started thumbnail too, since quit waits for the threads. A call made without asking.
		- A stopped thumbnail stores nothing, not even a failure, so the file is tried again the next time it is shown.
	- Done: each started thumbnail has its own cancel, set when its file is let go or the app quits. The thumbnail factory hands it to every reader. The Photoshop and camera raw readers check it between reads and between rows. The gdk-pixbuf path feeds its loader a piece at a time and checks between pieces. A thumbnailer program or ImageMagick is ended, the same way the 30 s timeout ends one. The checksum read before a thumbnail takes the same cancel.
	- Note: one reader can't be stopped all the way. A gdk-pixbuf loader that only decodes once it has the whole file, such as TIFF, still finishes that decode. gdk-pixbuf has no cancel for it. Reading the file stops, and JPEG and PNG decode as they read, so they stop.
	- Note: ending a thumbnailer program ends only that program. One that starts others of its own leaves them running, as the timeout already did.
	- Note: ImageMagick runs through GLib's process calls on every platform, as the archive tools do, and is ended through them. `nemo-launch-win32.c` starts what the user opens and keeps no hold on it, so the stop does not go through there.
	- Swept: every reader the thumbnail factory calls: thumbnailer programs, gdk-pixbuf, Photoshop, camera raw and ImageMagick. Reading back a stored thumbnail is small and left alone. Every caller of the changed calls: the thumbnail queue, and the Photoshop, camera raw and ImageMagick tests.
	- Branch: thumbstop
	- Commit: 2f6ba48
	- Test case: rjffcm7d (`test-nemo-thumbnail-stop`). A 42 MB Photoshop file of 30000 by 30000, dropped partway on the only thread, frees it within 3 s, and nothing is stored for it. A thumbnailer program, a stand-in ImageMagick and a slow gdk-pixbuf read are each stopped within 3 s, and the programs are gone. `test-nemo-psd` has a stopped read too. The gdk-pixbuf case is POSIX only.
	- Verified: rjffcm7d fails with started thumbnails left to run: the next picture came 14 s after the drop, and the dropped one was stored. It also fails with a stopped one stored as a failure, and with each reader's stop taken out. It passes now. The Windows cross build and lint are clean, and rjffcm7d passes under wine.

- Code review 20260928 item 35. Add a sanitizer build of the test suite to the pipeline.
	- ID: 2026092813381435
	- Type: Enhancement
	- Status: Waiting for testing
	- Needs local test suite run?: one full `cicd.bash` run, to see the stage in its place. Its command ran on its own, and the help test passes.
	- Needs external testing: one Windows launch with a folder and an option on the command line, since the option parse changed. The cross build is clean.
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Directive dated 20260919: ASan and UBSan on the test build.
		- Items 23, 32 and 33 came from one such run. A leak pass needs a suppressions file for GTK's own.
	- Decisions:
		- Stage 3, after fuzzing, on full runs only. Not in `--quick` or the gate. `--no-sanitize` skips it. The flag name is a call made without asking.
		- Short stacks. Full ones slowed the GUI tests past their own limits, two runs out of two. A call made without asking.
		- GTK animations are off in that run, since GTK 3.24.49 leaks a value on each CSS transition and only GTK's code is on that path. A call made without asking.
		- The leak tests and the allocations test skip there by themselves (exit 77), since they find the heap unreadable. No lane-level exclusion.
	- Done: `cicd/linux/test-sanitizers.bash` builds the suite with both sanitizers in its own build dir and runs it through `run-tests.bash`, leak checks on. Any report fails the test. `cicd/linux/sanitizers.supp` lists fontconfig and Mesa by library.
	- Fixed, found by the first runs:
		- Every option parsed from the command line leaked, since the parse left them out of the list the application frees.
		- A closed tab or window kept its location, and its scroll target when it closed before loading finished. The pending scroll target was also replaced without being freed.
		- The application never freed its undo manager, and the places pane never freed its menu manager.
		- Test side: scratch paths and a folder in five tests, and the crash test now leaves its deliberate faults to the reporter under the sanitizers.
	- Swept: every place `pending_scroll_to` is set (one other, after the old location change is ended) and the slot's dispose. Other option parses in `source/`: none.
	- Branch: asan
	- Commit: 81d03ad, 8c89061
	- Test case: rjfgk2mp (`cicd/linux/test-sanitizers.bash`, the lane itself).
	- Verified: the lane fails on a heap overflow, a leak and a signed overflow in a test, and passes on this branch with 157 OK and 12 skipped. Linux suite 169 of 169, lint clean, Windows cross build clean.

- Code review 20260928.
	- ID: 2026092813381400
	- Type: Task
	- Status: Started
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Requirements:
		- Everything changed from 20260917 to 20260927, reviewed or not, plus the ground the 20260919 round did not reach where it changed since.
		- Items 1 to 45 below carry this ID as their parent. Technical detail is in the private notes under the same numbers.
	- Progress log:
		- 20260928-133814: Filed 33 defects and 12 enhancements. Of the defects, 4 are regressions or missed twins of an earlier fix (items 4, 12, 21, 22), item 15 reopens three closures, and the rest are new ground. 19 were reproduced, some only in part. The others were only read, and each says so.
	- Decisions:
		- Not release-ready. Items 1, 3, 5, 6, 7 and 16 give a wrong result with no error, or change files the user did not ask to change.
		- Handlers that outlive their widget have come back a third time (20260919 items 3 and 10, now item 22). Per the fix rules, that class wants a table in design.md.
		- Decided against: a same-size, same-time twin showing another file's picture. Already recorded as designed.
		- Decided against: shortcut reads on the main thread when opening one, and an edited shortcut losing its item ID list. Both recorded as known gaps.
		- Decided against: the archive password showing in the process list. design.md says so.
		- Decided against: a small copy leaving a partial file on a failed write. GLib's own copy does the same.
		- Decided against: Escape not restoring the selection, Ctrl+Shift+T, and Control kept for F1, tab keys, Ctrl+H and Ctrl+M on macOS. All settled earlier.
		- Decided against: warn-only packagers, lint scoped by file, the launcher's names, and three flagged words in hand-written prose. All settled earlier.
	- Test case: none, review round.

- Compression dialog reset: link handling per kind of link, mounted filesystems, live size totals, clearer delete check.
	- ID: 2026092910143202
	- Type: Enhancement
	- Status: Queued
	- Opened: 20260929-101432
	- Opened by: t00mietum
	- Related IDs: 2026092813381404, 2026092813381416
	- Target OS: Linux, Windows
	- Design: [20260929-101432_compression.md](design_docs/20260929-101432_compression.md). The requirements, decisions and open questions are there.
	- Requirements:
		- Deselect and disable options the archiver can't do.
		- Symlinks and Junctions as radio groups, plus nested and other filesystems options, each with a live size change. A total size beside Cancel and OK.
		- Rename the delete box, and say why a delete check failed.
		- Volume sizes say what each is for.
		- A pre-scan progress bar after OK.
		- Keep compress and extract modular, for a possible split to their own project.
		- Options opens by itself when a remembered choice isn't the default, and a button beside it resets them.
		- 7z goes to 7-Zip first where it's installed, and an edited 7-Zip line gets `-spd` at run time.
		- An edited rar line gets `-r0` at run time, so an `-r` left in it no longer takes same-named files from the folders below.
	- Progress log:
		- 20260929-161500: design moved to its own doc, with the new size counting. Five of the eight old questions are answered there.
		- 20260929-173000: answers folded in. A nested filesystems option, exact totals for files with more than one path, dangling links under Ignore, the library's 7z storing links, and the order of the code split. One question left, on `-spd`.
		- 20260929-190000: `-spd` is added at run time, 7-Zip is used first for 7z, and Options opens by itself and gets a reset button. Other filesystems is for folders only. One question left, on a selected link to another filesystem.
		- 20260930-090000: a link onto another filesystem is followed only when both options are on. The reset button also collapses Options, and the store option's flyover says when it forces one thread. The settings comments on the command lines now say they are base flags. No questions left.
		- 20261004-150000: `-r0` is added at run time to an edited rar line, from 2026100410431108.
	- Test case: extend test-nemo-archive-combos to each link choice and the mounted filesystem option. IDs when written.

- Code review 20260928 item 34. Apply the directives' new C section.
	- ID: 2026092813381434
	- Type: Enhancement
	- Status: Queued
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Directive dated 20260919, new since the last round. Not a regression.
		- Name the C standard in the build. None is named today, so gcc's default applies.
		- Raise the warning level past `-Wall`. At the next level there are about 1700 warnings, almost all unused parameters and missing field initializers, 139 of them on lines changed in the last 10 days.
		- One line in each allocating function's header comment on who frees the result.
	- Decisions:
		- 20260928: full `-Wextra` over the whole tree, with every warning fixed. The fork will never track upstream, so churn in inherited files is fine.
	- Test case: none yet.

- The app visits network shares on its own.
	- ID: 2026093010493450
	- Type: Task
	- Status: Queued
	- Opened: 20260930-104934
	- Opened by: code review 20260928 follow-up
	- Related IDs: 2026093010493389, 2026092813381408
	- Requirements:
		- The app never visits a network share on its own. Only something a person does reaches one, such as going to a share or opening a link or shortcut that points at one.
		- Find each place that touches a share with no such action behind it, and gate it or work from what is on local disk. Icons, sort places, emblems, thumbnails, link targets, free space and the side pane are the first to check.
		- Asked 20260930, as design.md "Speed, memory and size".
	- Test case: none yet. One per path found, where it can run off Windows.

- Demo gif: show best features first.
	- ID: 2026100219523841
	- Type: Enhancement
	- Status: Queued
	- Opened: 20261002-195238
	- Opened by: t00mietum
	- Requirements:
		- Show best features first, e.g.
			- Native compression features
			- Full Windows .lnk support in Linux and macOS
			- Relative link creation
			- Copy allows link-handling options
			- Advanced automatic column sizing logic
			- Optional striped rows (turn on instantly, don't bother with menu)
	- Test case: none, demo content. `cicd/utility/lint-demo-script.py` checks the script.

- Code review 20260928 item 2. The tree sidebar crashes on Shift+F10 or the Menu key.
	- ID: 2026092813381402
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Priority|Severity: High
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Steps to reproduce [Bug]:
		- Show the tree sidebar, click a folder in it, press Shift+F10.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: the keyboard path passes no mouse event, and the menu code reads the pointer position from it.
	- Origin: upstream, never touched here. Not seen by an earlier round. Confirmed.
	- Actual fix: with no mouse event the menu is for the row the keyboard is on. With no row at all, no menu opens, from the keyboard or a right click.
	- Note: a right click on empty space in the tree used to open the menu with nothing behind it, so its items acted on no file. It now opens nothing. A row with no file behind it, such as one still loading, is treated the same.
	- Note: the keyboard menu opens at the top left of the tree, not beside the row. That comes from the shared placement code the other views use, and is left as is.
		- Filed as 2026100408414402, for every view.
	- Swept: the places sidebar reads the selected row and never the event. The list and icon views pass the event on to the shared view code, which checks for none before reading the position. The tab bar checks for none before reading the button and time. The path bar, location bar and toolbar back and forward menus only open from a click, so always have an event. The rename field's menu checks for none. No other code reads a position or button from a menu event.
	- Branch: treemenu
	- Commit: d879cfb
	- Test case: rj04ta3n, Tree menu key test. Linux only. Fails before the fix, passes after.
	- Verified: the new test fails before the fix, with the crash, and passes five runs in a row after it on Linux. A right click on a tree row still opens the menu, and one on empty space opens nothing. Lint is clean.
	- Verified: 20261003, Linux. Shift+F10 and the Menu key on a tree row both open the menu for that row, with no crash.
	- Acceptance signoff: Self-closed: rj04ta3n passes in the full Linux suite, and Shift+F10 on a tree row was seen on screen on Linux. The menu opens for that row, and the program keeps running. The menu still opens at the top left of the tree, as noted above.
	- Closed: 20261003-174609

- Code review 20260928 item 1. Zooming while thumbnails render can store a small thumbnail as full size, and it is never made again.
	- ID: 2026092813381401
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: High
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: the picture stays blurry at the larger size until the file is edited or the cache is cleared.
	- Expected behavior: design.md, a thumbnail is made again bigger when a draw wants more than is stored.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: a new request merges into the job a worker is already running, and the worker stores the new size with the old picture.
	- Origin: 0c1612a and 056d3e0, 20260921 (thumbdb, thumbs). New ground. Confirmed.
	- Actual fix: a job a worker has started is no longer changed. A bigger ask for the same file waits behind it and starts when it ends. A smaller or equal one is answered by the job already running. A job that ends only clears its own entry from the queue table, not a newer one for the same file.
	- Swept: besides the merge, a queued job is only changed by the remove path, which just marks it canceled, and by shutdown, which now drops a waiting follow-up. No other code writes to a job once it is queued.
	- Note: edits during a render no longer rewrite the running job's size and time either. Item 9's queued-edit path is unchanged.
	- Note: while a bigger ask waits behind a running job, the file reads as not being made, so the thumbnail progress bar can end a moment early. Left as is.
	- Branch: thumbzoom
	- Commit: 1a7470d
	- Test case: rj043mnp, Thumbnail zoom during render test. Linux only. Fails before the fix, passes after.
	- Verified: the new test, and the order, hold, jobs and memory thumbnail tests, pass three runs in a row on Linux. Lint is clean.
	- Acceptance signoff: Self-closed: a race between zoom and rendering, which can't be checked reliably by hand. rj043mnp covers it.
	- Closed: 20261003-112426

- Moving the only tab to another window can crash the window it left.
	- ID: 2026100413510329
	- Type: Bug
	- Status: Done
	- Priority|Severity: Avg
	- Opened: 20261004-135103
	- Opened by: work on 2026092813381442
	- Related IDs: 2026092813381442
	- Target OS: all
	- Steps to reproduce:
		- With "Always show tabs" on and two windows open, drag the only tab of one window onto the other, or send it there with "Move tab to".
	- Incorrect behavior: the tab moves. The window it left then logs criticals about a window that is gone as it closes, and sometimes crashes, which leaves a crash report.
	- Expected behavior: the window closes cleanly.
	- Reproduced: yes, 20261004, Linux. Once as a crash, with the only tab sent to a new copy before item 42's change. Once as criticals, with the only tab dropped on another window.
		- Again 20261004, Linux, by rhmr6qgs: the copy whose only tab was dropped on another window crashed on every run and left a crash report, or logged the criticals.
	- Possible cause: the tab is closed from an idle that holds its own reference. Closing the last tab closes the window, so the tab is freed after the window, and its view then takes its menus off a window that is gone (`real_unmerge_menus`).
	- Actual cause: a tab taken out of its window was only torn down when its last reference went. The idle that closes a moved tab holds one, so for the last tab the window went first. The tab menu and a drag over a tab hold references the same way.
	- Actual fix: a tab is torn down as it leaves its pane, while its window is still there. The tab's own teardown now runs once, since it is called again at the last reference.
	- Swept: closing a tab by its button, Ctrl+W and the tab menu; the last tab of a pane in split view; a window closed with tabs in it; a tab moved or torn off to a new window, in its own process or in one process. GTK's own move of a tab between windows in one process keeps the tab alive and is not affected. The other timeouts and idles on windows and tabs are removed by id when those go.
	- Branch: tabcrash
	- Commit: feb2ce0
	- Test case: rhmr6qgs, Tab move between processes test. Two new sub-cases: the only tab dropped on another copy's window, and the only tab closed from the tab menu, each checking that the copy it left ends cleanly. The first failed before the fix and passes after. The second passed both ways, and guards the menu's hold on the tab.
	- Verified: full Linux suite 167 of 167, lint clean, Windows cross build clean.
	- Acceptance signoff: Self-closed: reproduced, its test failed before the fix and passes after, and there is nothing to judge on screen.
	- Closed: 20261004-142024

- Under rar, a selected file also takes same-named files from the folders below it, and a link that leads nowhere beside the selection fails the job.
	- ID: 2026100410431108
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 164 of 164 on 20261004, on rarsel.
	- Needs external testing: none left. Ran on vm925w on 20261004.
	- Priority|Severity: Avg
	- Opened: 20261004-104311
	- Opened by: item 2026100312494905
	- Related IDs: 2026100312494905, 2026092813381404
	- Incorrect behavior: the rar line has `-r`, and rar then reads each selected name as a pattern for every folder below where the job runs. Picking `a.txt` in a folder that also has `sub/a.txt` puts both in the archive. rar also tries every other name it walks past, so a link that leads nowhere sitting beside the selection, not picked, makes rar warn, and the job fails and deletes the archive.
	- Expected behavior: the archive holds what was selected and nothing else, and names that were not selected play no part.
	- Reproduced: rar's side yes, 20261004, Linux, RAR 7.20: `rar a -r -- x.rar a.txt` took `sub/a.txt` too, and said it could not open an unpicked link beside it, with exit 6. The second half on the job's side too, on winlinks: a selected file and linked folder with a link that leads nowhere beside them failed. The first half on the job's side is read only. Plausible.
		- Both halves on the job's side, 20261004, Linux: picking `a.txt` and a folder put `sub/a.txt` in the archive and the job said it worked. With an unpicked link that leads nowhere beside them, the job failed and left no archive.
	- Actual cause: rar's `-r` makes every selected name a pattern for the working folder and each folder below it, so rar looks at every name it walks past. A folder named on the line goes in whole without `-r`, as rar's own docs say.
	- Origin: 801ed01, 20260821, which put the command lines in the settings with `-r`. Not seen by an earlier round. Confirmed.
	- Decisions:
		- 20261004: a rar line edited in the settings keeps what it has, `-r` included, as item 2026092813381416 settled for `-spd` on 7-Zip lines. When the compression reset adds run-time flags to edited lines, `-r0` after an edited line's `-r` would undo it, since rar takes the last one said. It recurses only for a name with `*` or `?`, which rar already refuses.
		- 20261004: answered yes. The compression reset adds `-r0` at run time to an edited rar line, as it adds `-spd` to an edited 7-Zip line. Recorded on 2026092910143202.
	- Actual fix: the built-in rar line no longer has `-r`. A selected file is taken from the job's folder only, and a selected folder still goes in whole, hidden files, empty folders and links included.
	- Swept: the built-in rar line and the settings schema's copy of it. The first run that keeps links that lead nowhere still says `-r-`, for an edited line. The 7-Zip lines never had `-r`, and the new rows pass for every format. Neither extract line has it. Names with `*` or `?`: rar reads them as patterns with or without `-r`, so item 2026092813381416's refusals stay as they are. Left-out names after `-x` are relative paths with no wildcards, which rar matches only where they are, with or without `-r`. A selected link named with a leading @ still goes in as `./@name`.
	- Branch: rarsel
	- Commit: 81e4f82
	- Test case: rhr6ggmt, Archive option combinations: a picked `a.txt` and folder beside `sub/a.txt` and `sub/held`, in every format, then again with an unpicked link that leads nowhere beside them. Under rar the first run took `sub/a.txt` and the second failed before the fix; both pass after. rev86z08, Archive options test: the built-in rar line has no `-r`. Fails before the fix, passes after.
	- Verified: 20261004, Linux: rhr6ggmt, rev86z08 and rewygsbg pass, rar ran 43 rows. Full Linux suite 164 of 164.
	- Verified: 20261004, Windows, at bab9a49: rhr6ggmt passes natively on vm925w, rar ran 43 rows. The native suite there had no failures, 142 OK.
	- Acceptance signoff: Self-closed: its tests pass on Linux and natively on Windows, and nothing is left to judge on screen.
	- Closed: 20261004-140500

- Clicking a place in "Places" leaves the keyboard focus there.
	- ID: 2026100408525989
	- Type: Bug
	- Status: Done
	- Priority|Severity: Avg
	- Opened: 20261004-085259
	- Opened by: t00mietum
	- Target OS: all
	- Steps to reproduce:
		- Click a place in "Places".
		- Press an arrow key or type a letter.
	- Incorrect behavior: the keys go to "Places", not to the folder that opened.
	- Expected behavior:
		- Focus moves to the main view after the click, with nothing selected there.
		- "Places" never takes keyboard focus, except while renaming an entry after right-clicking it and choosing "Rename".
	- Reproduced: yes, 20261004, Linux. After a click on a bookmark, Down moved the cursor in "Places" and nothing in the folder.
	- Actual cause: the earlier item "Focus can never remain on the "Places" pane" was built backward. Connecting a view skips the focus grab while either side pane has focus, so the keyboard stays on the clicked place. The test and the lint check written for it lock that in.
	- Note: the tree view may still hold focus, per the "Places" and tree view layout item. Only "Places" changes.
	- Note: 2026100408414402 lists the places sidebar for the keyboard menu. With no focus there, that part goes away.
	- Actual fix: the "Places" tree refuses the keyboard focus, and so does the scrolled window around it, which would otherwise take it from Tab or F6. Opening a place hands the focus to the folder. A click on the open folder's own place also clears its selection. A rename still gets its entry, and when it ends the focus goes back to the folder. F6 now goes from the folder straight to the tree pane. The handler that moved the "Places" cursor whenever Tab passed it is gone.
	- Swept: every way a place opens in the same window goes through one function: left click, middle click into a new tab, the menu's Open, and a volume mounted first. A new window or close-behind leaves the focus alone. Both ends of a rename, done and canceled. The tree pane is unchanged, and connecting a view still does not take the focus from it. Nothing else puts the focus in "Places".
	- Note: the clicked place stays highlighted in "Places". That is its selection, which follows the open folder, not the focus.
	- Branch: placesfocus2
	- Commit: 4968d39
	- Test case: `rjedw75s Places focus test`, new. It runs the program, clicks a bookmark, clicks it again, and renames it from its menu. 8 of its 11 checks fail before the fix, and all pass after. `rgxy149r Focus guard test` and `rhtg2yej` in the C lint now check the new behavior, and each fails with either refusal taken out. `rj04ta3n Tree menu key test` checks F6 still reaches the tree.
	- Verified: in the running program, a click on a place and then Down or typing moves in the folder. A rename from the menu still edits, and Enter or Escape gives the keys back to the folder. A place whose folder uses another view type gets the focus in the new view. A click in the tree keeps the keys in the tree. F6 goes from the folder to the tree and back. Full Linux suite 163 of 163, lint clean. rjedw75s passed 64 runs, 16 at a time on one display.
	- Acceptance signoff: Self-closed: the item spelled out the behavior, the change does that and no more, and rjedw75s pins it.
	- Closed: 20261004-092702

- Code review 20260928 item 4. A dangling symlink fails a 7z or rar archive, and the finished archive is deleted.
	- ID: 2026092813381404
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: no. Ran natively on b29w and on vm925w on 20261003, a link with a name past ASCII included.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Design: [20260929-101432_compression.md](design_docs/20260929-101432_compression.md). Under the reset, "Ignore" leaves these links out too.
	- Steps to reproduce [Bug]:
		- Untick "store links", then compress a folder holding a link to a missing file as 7z or rar.
	- Incorrect behavior: "could not be created", and the whole archive is gone. On Windows it happens to every 7z, whatever the checkbox says.
	- Expected behavior: the link goes in as a link, even with "store links" unticked, wherever the format and tool can keep it. Where they cannot, it is left out with a warning that names it, and the rest of the archive stands.
	- Reproduced: yes for the tools' exit codes, 20260928, Linux. The job side was read only. The job side too on 20260928, with the new test rows.
		- Windows, 20261003, b29w natively: every format failed, not only rar.
	- Actual cause: 7z exits 1 and rar exits 6 when they skip a link they cannot follow, and any non-zero exit fails the job.
		- Windows: GLib gives a link that leads nowhere the link's own type, a file or a folder, as if it had been followed. So the scan never saw one there, and every writer tried to read it.
		- Windows: GLib does not fold a program's error output into its normal output, so what 7z and rar said about the link never reached the reader.
		- Windows: 7-Zip puts in an empty entry for a link it cannot open, and rar's real run writes a plain folder over a folder link its first run kept.
		- Windows: 7z and rar print names in the console code page, and the library zip writer stores them in a local code page. A name past ASCII came out changed, and the delete check read archive names the same way.
	- Progress log:
		- 20260929-070928: Reworked for the decision below. Links that lead nowhere now go in as links. Leaving them out with a warning is kept only where the tool cannot keep them.
		- 20261002-194800: the archive combinations test fails on b29w in the native suite. The four rar rows with a link that leads nowhere fail: rar says it cannot open the dead link, and it also cannot open the good link beside it ("The filename, directory name, or volume label syntax is incorrect"), so no archive is made. The 7z, zip and tar rows pass.
		- 20261003-124500: the full log of that run shows the zip, tar and 7z dangling rows failing too. The good link's error was the test's own: it was spelled with /, which Windows does not follow at all. Fixed for Windows on arcwin.
		- 20261003-124949: left at signoff rather than closed. On Windows the fix also changes what goes into archives: zip names, and the two built-in command lines.
	- Decisions:
		- 20260929: every format stores a link that leads nowhere as a link, where the format and tool can, even when links are otherwise followed. Leaving it out with a warning is only the fallback.
		- 20260929: under the Compress dialog reset, "Ignore" leaves these links out too. "Follow" and "Store" keep them. Nothing changes here until the reset is built.
	- Origin: 6c2418f, 20260820. Widened on Windows by 09506ec, 20260926 (bugs), which took link storing away from 7z there. Regression of that fix on Windows. Confirmed.
	- Note: the zip writer did not warn either. For a link to nothing, GIO answers with the link itself rather than failing, so the scan's "dangling" branch never ran and every writer left the link out without a word.
	- Actual fix: the library writer keeps each link that leads nowhere as a link, in every format it writes, 7z included, and still follows the other links. 7z and rar keep links only all or none. So those links go in first, by a run of their own that keeps links, and the real run adds the rest to that archive, following links as before. If that first run fails, the links are left out and named instead. The delete check counts a link that went in as in.
		- Left out with the warning, as before: a split archive, since neither tool can add to one. 7z on Windows, which is never asked to keep links. A link 7-Zip would reach through a followed linked folder, since it refuses that path. Under rar, a name with * or ?, which it would read as a pattern.
		- The real run is told to leave out every such link by name, whether the first run kept it or not. rar is not told about a name with * or ?, or one starting with @, which it would read as a list file. It passes over those with a warning status. The output reader from 334b239 still fails the job on any warning that does not name one of them.
		- Windows: a link counts as leading nowhere when opening through it finds nothing. The tools' error output is read beside the normal output. 7z and rar are asked for UTF-8 names in their built-in lines, the library zip writer stores UTF-8 names, and the delete check reads archive names as UTF-8. The reader takes the count line both tools end on there.
	- Swept: every writer. The library writes zip, tar and its three compressed forms, and 7z; 7z writes 7z, and zip when split; rar writes rar. The delete check's own walk follows the same rule. Compress each goes through the same per-archive code. Unpacking has no link scan.
		- Windows: the dangling check is one function for the scan and the delete check. Both places that start 7z or rar, compress and extract, read error output the same way, so extracting now sees a wrong password on Windows too. Extracting already read names as UTF-8; its hard link names now do as well. The other GIO link type checks are filed as 2026100312494903. A linked folder named with a leading @ has the same list file trouble under rar, filed as 2026100312494905.
	- Branch: arclinks, then arcdangle, then arcwin
	- Commit: 334b239, then 991b6e1, then d0519a7, 3871931, 1507a21, 1ec66eb
	- Test case: rhr6ggmt, Archive option combinations. Its dangling-link rows check that the link reads back as a link, with a good link beside it still followed, for every format, with links stored and not, delete on and off, and one split. New rows: the library's 7z, a selection of only a dangling link, one inside a followed linked folder, and on Linux a name with ? for rar. 26 rows fail before the rework and all pass after, on Linux.
		- rewygsbg, Archive job test: the delete check now passes with such a link in a zip. Its older check that the delete check refused is commented out with the reason. Fails before, passes after.
		- rev86z08, Archive options test: the output reader rows from 334b239, and new rows for the line of the first run. That line is new, so it has no before run.
		- Windows, arcwin: rhr6ggmt's dangling rows add a folder link that leads nowhere and, on Windows, a name past ASCII. The good links use the native separator. 36 rows fail natively on b29w before the fix and all pass after. rev86z08 gained rows for the count line and the UTF-8 switches, which fail with the count handling taken out.
	- Verified: the 9 archive, extract, template and schema tests pass on Linux, the combinations and job tests three runs in a row. Lint and the Windows cross build are clean. The Archive options test passes under wine, its Windows-only rows included.
		- 20261003: rhr6ggmt, rewygsbg and rev86z08 pass natively on b29w and on Linux. Full Linux suite 157 of 157. Full native suite on b29w at 1507a21: 137 passed, 10 skipped, 1 failed, rfhr0zw0, which belongs to 2026093010493389. The archive and extract tests again at 1ec66eb.
	- Verified: 20261003, rhr6ggmt, rewygsbg and rev86z08 pass in the native suite on vm925w at c78aa9e.
	- Acceptance signoff: Self-closed: rhr6ggmt, rewygsbg and rev86z08 pass on Linux and natively on b29w and vm925w. The Windows changes to zip names and to the built-in 7z and rar lines are what the item asked for, and rhr6ggmt and rev86z08 check them.
	- Closed: 20261003-174609

- On Windows, a folder full of shortcuts shows nothing until every shortcut icon is found.
	- ID: 2026100112000535
	- Type: Bug
	- Status: Done
	- Needs external testing: done. The Start menu folder was seen on screen on vm925w, 20261003. The rest ran on b29w.
	- Priority|Severity: Avg
	- Opened: 20261001-120005
	- Opened by: t00mietum
	- Related IDs: 2026093010493389, 2026092813381436
	- Target OS: Windows
	- Steps to reproduce:
		- Go to the Start menu Programs folder, or any folder with many `.lnk` files.
	- Incorrect behavior: the content pane stays empty until the icons for all the shortcuts are loaded.
	- Expected behavior: the pane shows the files right away, with a plain icon or the last known one. Shortcut icons load in the background and replace them as each one is found.
		- Possibly the shortcut icons can use the thumbnail cache, with its own icon table, so a folder seen before draws its real icons at once.
	- Reproduced: no. Seen on Windows, not yet reproduced here.
	- Possible cause: each shortcut's icon comes from the Windows shell on the window's thread, one file after another, the first time the view asks for it. The only cache is in memory, so every new run pays it again.
	- Note: 2026093010493389 already wants these lookups off the window's thread. One fix may cover both.
	- Actual cause: as above, read from the code. The view asked the shell for each shortcut's icon the first time it drew it, and waited for the answer.
	- Actual fix: the view gets the cached icon or nothing, at once, and the plain shortcut icon stands in. One worker thread asks the shell, and the shortcut is redrawn when an icon is found. A shortcut the shell has no icon for is remembered too, so it is not asked again. Same branch and fix as 2026093010493389.
	- Note: the icon table in the file cache was not built. It needs a new cache table and version, its own pruning, and a choice of which sizes to keep, which is more than this fix. Until then the last known icon is kept only while the app runs, so a new run starts plain again.
	- Note: the folder check behind the sort place and the folder icon still reads each shortcut on the window's thread. It is a local file read, cached, and was not measured.
	- Branch: lnkasync
	- Commit: 61dcecc
	- Test case: rfhr0zw0, Shell icon test, new lookup cases: the first ask returns at once with nothing, a second one while it runs is not queued again, the window is told once when the icon is found, and the cache answers after. A file that is not a shortcut is finished with and not asked again. No case shows the wait itself, since a slow shell can't be made here.
	- Verified: rfhr0zw0 passes under wine, and the wine build lists a folder of shortcuts with each one's own icon. The rest as on 2026093010493389.
	- Verified: the lookup cases in rfhr0zw0 pass on b29w on 20261002, in the native suite. The test as a whole fails on the dead share item's cases.
	- Verified: 20261003 on b29w, rfhr0zw0 passes whole, with the fix for 2026093010493389 on lnkicon. Asking for the icons of all 335 shortcuts in both Start menu folders took under 1 ms in all. The icons came in over 28 seconds on a first run, and 3 seconds on a second.
	- Note: before the fix the view asked for each of these on the window's thread, from the code. Not timed.
	- Note: the folder was not opened on screen. b29w's only session is the one on its own screen.
	- Verified: 20261003 on vm925w, on screen. The Start menu Programs folder, 67 items, was listed within 300 ms of the window showing, the shortcuts with the plain shortcut icon. Their own icons were all in by 2 s. The window answered every check while they loaded.
	- Acceptance signoff: Self-closed: rfhr0zw0 passes natively on b29w and vm925w, and the Start menu folder was seen on screen.
	- Closed: 20261003-174609

- On Windows, a local shortcut to a share that is not answering can stall the window while its icon is looked up.
	- ID: 2026093010493389
	- Type: Bug
	- Status: Done
	- Needs external testing: done on b29w, 20261003. rfhr0zw0 passes there, dead share cases included.
	- Priority|Severity: Avg
	- Opened: 20260930-104934
	- Opened by: code review 20260928 follow-up
	- Related IDs: 2026092813381408, 2026093010493450
	- Target OS: Windows
	- Incorrect behavior: a shortcut with no icon of its own gets one from the Windows shell, on the window's thread. The shell may go to the target for it. On a share that is not answering that is about twenty seconds per shortcut.
	- Expected behavior: the share is never visited for an icon.
		- The target path is read from the shortcut file, as the folder check already does.
		- When the target is on a share, the icon comes from the name alone. A folder gets the folder icon, a document the icon for its extension, and a program the plain program icon.
		- Shortcut icon lookups run off the window's thread, local targets included.
	- Reproduced: no. Read only, from item 8 of code review 20260928.
	- Reproduced: yes, the second cause below, 20261003 on b29w. rfhr0zw0 failed the same two checks with the build from dev.
	- Decisions:
		- 20260930: assume the stall rather than time it first. Many shortcuts to shares would multiply it.
		- 20260930: a program on a share showing the plain program icon is fine.
	- Actual cause: the shell was handed the shortcut itself, and it reads the target, or the icon file the shortcut names, to find the icon. Nothing checked whether either was on a share.
	- Actual fix: the shortcut file is read for its target and for the icon file it names. When either is on a share, by its path or by a drive letter mapped to one, the shell is asked about the target's name alone, which it answers without opening anything. A folder still gets the theme's folder icon from the folder check. Every lookup now runs off the window's thread, with 2026100112000535.
	- Actual cause: a second one, on b29w after the first fix. Windows records the share as well as the drive path in a shortcut whose target's drive is shared, and b29w shares its C drive. Every local shortcut there was taken as one on a share, so it got the icon for its target's name rather than the one it names. Wine never writes the share part, so the test passed there.
	- Actual fix: a shortcut that records both counts as local when the drive's volume serial matches the one it records. One made on another machine's drive still goes by the name. Only the drive root is asked for its serial, so no link on the way to the target is followed.
	- Swept: the shell icon is asked for in one place. The folder check and the sort place already read only the shortcut file. Off Windows the icon comes from the shortcut file only. design.md says how a shortcut on a share gets its icon.
	- Note: a shortcut that records only an item ID list, with no path, is still handed to the shell. Nothing in the file says where such a target lives without asking the shell.
	- Branch: lnkasync, lnkicon
	- Commit: 61dcecc, a9aa321
	- Test case: rfhr0zw0, Shell icon test, new share cases: a document on a share wears the icon for its name and not the one the shortcut names, and a program on a share, or a shortcut whose icon is on a share, gets the plain program icon. The document cases fail with the share route taken out and pass with it, under wine. rhmxm5ah, Windows shortcut reader test, new cases for the icon file and the share check, on Linux.
	- Test case: rfhr0zw0, two more cases: a shortcut made by the shell and given a record of both its drive and a share, once with this drive's serial and once with another. The first wears the icon it names. It fails before the second fix and passes after, on b29w and under wine. rhmxm5ah, a shortcut that records both is not on a share. Fails before and passes after, on Linux.
	- Swept: the share check has one caller. Opening a shortcut tries the share only when the drive path is not there, and Edit link shows the drive path first. Both already right.
	- Note: the two checks that failed on b29w expected the right thing. The code was wrong there, and on any machine whose drive is shared.
	- Verified: 20261003, rfhr0zw0 passes on b29w with all 37 checks, and fails three before the fix. It passes under wine. Full Linux suite 157 of 157. The Windows cross build has no warnings, and lint is clean.
	- Verified: the Linux build and the shortcut reader, sort, link edit, link copy and make link tests pass. The Windows cross build compiles with no warnings. rfhr0zw0 passes under wine, and the Windows shortcut and share tests give the same results there as on dev. C lint and the test ID check are clean.
	- Progress log:
		- 20261002-194800: rfhr0zw0 fails two checks on b29w in the native suite: a local shortcut does not wear the icon it names, and a document on a share does. The other share cases and every lookup case pass.
		- 20261003-133500: the cause was the share record Windows writes for a shared drive. Fixed on lnkicon. The dead share check on screen was not run; the share cases in rfhr0zw0 use a share address that does not answer.
	- Verified: 20261003 on vm925w, on screen. A local folder holding a shortcut to a document on a share that does not answer, and one whose icon is on that share, opened as fast as any other folder, and the window answered every check over 8 s. The first wears the document icon and the second the plain program icon. The Windows shell itself took 45 s to make the first shortcut.
	- Acceptance signoff: Self-closed: rfhr0zw0 passes natively on b29w and vm925w, and a folder with shortcuts to a share that does not answer was seen on screen.
	- Closed: 20261003-174609

- Code review 20260928 item 3. Edit link on a symlink whose name ends in .lnk turns the symlink into a plain file.
	- ID: 2026092813381403
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Needs external testing: done on vm925w, 20261003.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Linux, BSD, macOS.
	- Incorrect behavior: the symlink is replaced by an edited copy of the shortcut it pointed at, and the real shortcut is left as it was.
	- Expected behavior: a symlink always gets the symlink editor, whatever its name.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: the dialog picks the shortcut editor by the name alone. A shortcut save also resets the file's permissions.
	- Origin: 1866e56, 20260925 (linkedit). New ground. Confirmed.
	- Note: the permissions reset happens with GLib 2.72, the Ubuntu 22.04 floor. GLib 2.84 keeps them on its own.
	- Actual fix: a symlink or junction gets the target editor whatever its name, and the shortcut save refuses one. A shortcut save puts back the permissions the file had.
	- Swept: the Windows shortcut page in Properties also chose by name alone, and now skips a symlink. The only other shortcut writer rewrites a file it has just made. Other code that checks for a .lnk name only reads.
	- Branch: linkfix
	- Commit: 342d30a
	- Test case: rhqxx81r, Link edit test, with a symlink named .lnk and a save of a shortcut with its own permissions. Fails before the fix and passes after, on Linux. The permissions check only fails on GLib 2.72, so it was run both ways on Ubuntu 22.04.
	- Verified: the link edit test passes on Linux with GLib 2.84 and 2.72. All 21 link, shortcut and undo tests pass on Linux. Lint and the Windows cross build are clean.
	- Verified: rhqxx81r passed on b29w on 20261002, in the native suite, the .lnk symlink case included.
	- Verified: 20261003 on vm925w, on screen. Edit link on a symlink named `s.lnk` shows the symlink editor, with its name and where it points, and its Properties has no shortcut fields. A real shortcut beside it still gets the shortcut editor and the shortcut fields. rhqxx81r passed natively there with symlinks allowed, so the `.lnk` symlink case ran.
	- Acceptance signoff: Self-closed: rhqxx81r passes on Linux and natively on Windows with its symlink cases run, and both dialogs were seen on screen on Windows.
	- Closed: 20261003-174609

- Code review 20260928 item 8. A FIFO named .lnk freezes the window.
	- ID: 2026092813381408
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Needs external testing: done on vm925w, 20261003.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Linux, BSD, macOS for the FIFO. Windows for the share.
	- Incorrect behavior: listing a folder that holds a FIFO named `x.lnk` hangs for good. Every `.lnk` on a slow share is also read on the main thread for its icon and sort place, with no share check.
	- Expected behavior: only regular files are read, and a per-file read on a share is gated, per the project rule.
	- Reproduced: yes for the hang, 20260928, Linux. The share case was read only.
	- Origin: 673bcbb, 20260924 (lnkread). New ground. Confirmed.
	- Actual cause: the shortcut reader opened and read any file named `.lnk`, and a FIFO with no writer blocks both. The icon and sort checks looked only at the name and at whether the folder is local. On Windows they never asked whether the file sits on a share.
	- Against: design.md says a folder shortcut sorts with the folders on every platform. On Windows one that sits on a share now sorts with the files, and design.md says so.
	- Signed off: 20260930, a shortcut on a share sorts with the files and wears the plain shortcut icon.
	- Actual fix: the reader opens without blocking and reads only a regular file. The icon and sort place of a shortcut are read only for a regular file that is local and not on a share.
	- Swept: every shortcut read goes through the one reader, so following one, opening one and the Edit link dialog refuse a FIFO too. The two paths-rewrite calls read the file whole, but only after the reader has read it. The Windows target check and shell icon for a shortcut sit behind the same new gate. The other reads made while a folder lists, `.desktop` link info and thumbnails with their checksums, go by content type, which is `inode/fifo` for a FIFO whatever its name, and both run off the main thread.
	- Note: opening a shortcut still reads it on the main thread, a recorded known gap. Only the FIFO hang is gone there.
	- Branch: lnkfifo
	- Commit: fd2b0d0
	- Test case: rhmxm5ah, Windows shortcut reader test, and rhnqqpm8, Folder shortcuts sort with folders test, each with a new FIFO case. Both fail before the fix, stopped after 10 seconds, and pass after, on Linux.
	- Verified: the Windows cross build compiles. C lint is clean. The link edit, link emblem, link copy, make link shortcut and thumbnail hold tests pass.
	- Verified: 20261003 on vm925w, on screen. A folder of shortcuts opened through a share listed with no stall, the window answering every check over 8 s. Its folder shortcut sorts with the files and wears the plain shortcut icon. The same folder opened locally sorts the folder shortcut with the folders, with the folder icon.
	- Acceptance signoff: Self-closed: rhmxm5ah and rhnqqpm8 pass in the full Linux suite, and the share listing was seen on screen on Windows.
	- Closed: 20261003-174609

- Settings an older release wrote with a backslash or tab in a value read wrong after the upgrade, with no warning.
	- ID: 2026100314515200
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: done. rjcev513 passed natively on b29w and vm925w, 20261003.
	- Priority|Severity: Avg
	- Opened: 20261003-145152
	- Opened by: item 2026100314290808
	- Related IDs: 2026100311512222, 2026100314290808
	- Steps to reproduce [Bug]:
		- With a release on SHCL 1.2.0 or 2.0.0, such as v1.0.0-beta2, set a value like `\\server\share\term.exe`, `tools\7z.exe`, a tab, or an association like `log=C:\Tools\view.exe "%1"`. Then start this release on the same settings.
	- Incorrect behavior [Bug]: the backslashes read doubled and the tab reads as `\t`. Nothing is backed up and no warning is given. The next save writes the wrong values in the current format, so they stay wrong and the old file is gone.
	- Expected behavior [Bug]: every value reads as the old release read it.
	- Reproduced [Bug]: yes, 20261003, Linux. Test rjcev513 writes the file with each old release's own code and reads it through the app.
	- Actual cause [Bug]: the old releases wrote a backslash as `\\` outside double quotes. Format 3 reads it there as it stands. A file with no format line is converted with only the spellings both rule sets read the same way, per a decision on 2026100311512222, and these are not among them. With nothing changed, the "read two ways" warning is skipped too.
	- Against: 2026100311512222, Decisions, "A file with no format line ... only spellings both rules agree on are changed." Replaced by the answer below.
	- Decisions:
		- 20261003: a file with no format line is converted as 2.x at startup.
	- Note: design.md, "Settings", said such a file is rewritten when the old rules read it differently. These values were read differently and it was not. It now says what the code does.
	- Progress log:
		- 20261003-145152: with the file converted as 2.x instead, every value both old releases wrote reads right in rjcev513, and the only other change is that such a file is then backed up and rewritten. The cost is a hand-written current file with no info block and a backslash escape outside double quotes, which would then be read as 2.x. Every file the app itself wrote before format 3 has no format line.
		- 20261003-145152: question. Should a file with no format line be converted as 2.x at startup? Suggested: yes, since app-written old files are the common case. A smaller step either way: back up the file and warn whenever a value could be read two ways, even when nothing is converted.
		- 20261003: answered yes. A file with no format line is converted as 2.x at startup.
	- Actual fix [Bug]: a file with no format line is converted as 2.x at startup. While the app can't save over that file yet, because its backup or the save failed, a hand edit to it is read as 2.x too. Before, the edit was read by today's rules, and with no backup the next save wrote the misread values over the file.
	- Note: the cost named above, as it behaves now. A hand-written file the app never saved, with an unquoted backslash, is read the 2.x way at startup: `\\server\share` reads as `\server\share`, and `C:\temp\new` gets a tab and a line break. The file as written goes to the backup first, with a message naming it, and no warning. Once the app has saved the file, or for an edit made while it runs, the backslash reads as written.
	- Verified: rjcev513 fails before the fix and passes after, both for the four values and for its new no-backup case. rjc4dd8z, rg6a49ar and rdjjz89r pass. Lint is clean.
	- Swept: the conversion has one call, in `load_locked`. Startup reaches it, and so does `reload_locked`, which the monitor and the save share. A reload reads a file with no format line by today's rules, except in the no-backup case above.
	- Branch: v2read
	- Commit: 660122f
	- Test case: rjcev513 Config old formats test. The four values are plain checks now. It also covers a file both rules read alike, which is left alone, and a 2.x file that can't be backed up and is edited while the app runs.
	- Acceptance signoff: Self-closed: rjcev513 fails before the fix and passes after, on Linux and natively on Windows. The question on the item was answered.
	- Closed: 20261003-174609

- A release build reuses an old build dir that keeps link-time optimization off.
	- ID: 2026100316321188
	- Type: Bug
	- Status: Done
	- Priority|Severity: Avg
	- Opened: 20261003-163211
	- Opened by: code review 20260928 items 25 to 28
	- Related IDs: 2026092813381427, 2026092813381428
	- Target OS: Linux.
	- Incorrect behavior: release.bash asks for link-time optimization, but the release container's build dir has it off and keeps it off. A clean build of the same commit has it on, so every binary differs from the published ones.
	- Expected behavior: README, the Linux builds can be rebuilt from their commit to the same bytes.
	- Reproduced: yes, 20261003, Linux. Dev `f4d2386` built in a new container from the current image, with its old build dir, gave a 2175143 byte tarball. A clean build gave 2195908 bytes. The live release container's build dir also reads link-time optimization off.
	- Possible cause: meson 0.61's `setup --reconfigure` does not apply the option to a build dir set up without it. The current image also has a build dir saved inside it, so even a new container starts from it.
	- Actual cause: on a reconfigure, the release image's meson takes plain options but drops new `b_` ones, so `b_lto` and `b_lto_threads` never reached a dir first set up without them. release.bash reconfigured whatever dir it found, and the image had one saved inside it from hand work in the container.
	- Actual fix: the build dir is set up from nothing on every release build, by the new `cicd/linux/release-setup.bash`, which then reads back what meson recorded and stops on anything but what was asked. `--clean` is still taken but does nothing now. The release image was re-committed without its build dirs, temp files and the beta1 package's leftover config, and the release container was made again from it.
	- Swept: the cross lane's meson keeps `b_` options on a reconfigure, and its build dir reads link-time optimization on. The fuzz lane already starts over when its flags are missing. The debug build and the suite are not release builds. The hosted Windows build runs on a fresh runner.
	- Note: the item 27 decision still holds. The image has the same packages as before; only build leftovers were removed.
	- Note: a clean build takes about a minute here, so each pipeline run is that much longer.
	- Note: beta1 and beta2 were built with link-time optimization off, so a rebuild of either tag does not match what was published.
		- 20261004: README and design.md now say the same-bytes claim starts with the release after beta2. The release image was flattened, which took about 390 MB of old build dirs out of its layers. Its files are unchanged.
	- Branch: cleanrel
	- Commit: a24072f
	- Test case: `cicd/linux/test-release-setup.bash` (rjcpvcyb), in the lint stage. Fails before the fix, passes after.
	- Verified: rjcpvcyb fails with the old reconfigure logic (`b_lto` off, no `-flto`) and passes with the fix, and fails when the setup script's guard on what it clears is taken out. A full release build in the old container, over its old dir, came out with link-time optimization on. A second one in a new container from the re-committed image gave the same tarball, byte for byte. The lint stage passes.
	- Acceptance signoff: Self-closed: a build lane fix with nothing on screen. rjcpvcyb covers it.
	- Closed: 20261003-172500

- Code review 20260928 item 5. The installer and prefix checks cannot fail a pipeline run.
	- ID: 2026092813381405
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full pipeline ran on 20261002. The packages stage ran both checks, both passed, and the run went on to dogfood.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a broken installer prints a warning, and the run still goes on to dogfood and publish.
	- Expected behavior: the check stops the run, as the done item for the installer fixes says it does.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: both checks sit in the packaging list, where a failure only warns.
	- Origin: 7284973, 20260925, and c5f4e7b, 20260926. New ground. Confirmed.
	- Against: warn-only packagers, in the review's Decisions. The packagers still only warn. Only the two checks moved.
	- Actual fix: the two checks have a list of their own in the pipeline config. They run after the packagers, and any failure but a skip stops the run before dogfood and publish.
	- Swept: the Windows pipeline has no packages stage, and its installer check already stops the run. Nothing else sits in the packaging list but the two packagers.
	- Branch: gatefix
	- Commit: 9000f48
	- Test case: rj3yttvt, Package checks test. Fails before the fix and passes after, on Linux.
	- Verified: the test also fails on a fix that warns instead of stopping. The installer and prefix checks both pass on the current release tarball, so the new stop does not block a run today.
	- Acceptance signoff: Self-closed: a pipeline check with nothing on screen. rj3yttvt covers it.
	- Closed: 20261003-112426

- Code review 20260928 item 9. A checksum taken after a file changed keeps that content out of the cache for good.
	- ID: 2026092813381409
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a picture edited while its thumbnail is queued is stored with its old size and new checksum. Every later store of that content, under any name, then fails, and the file is made again on every visit.
	- Reproduced: yes at the database level, 20260928, Linux. Through the thumbnail queue too, 20260930, Linux: a picture edited after the view read its size and time, and before its job ran.
	- Actual cause: a thumbnail job keeps the size and time from when the view last looked, and checksums the contents as they are when it runs. The checksum is unique on its own, but the lookup matches checksum and size.
	- Origin: 42adbdf and 0c1612a, 20260921 (thumbdb). New ground. Confirmed.
	- Actual fix: the job reads the size and time again once the checksum is done, and keeps no checksum if either moved. The store finds a record by checksum alone, and a record found at another size takes the newer size. Records written before this are thrown away with the cache, since item 11 changed the tables.
	- Swept: the thumbnail job is the only place a checksum is worked out. The other source is the store itself, read back by name, size and time. The checksum written onto the file uses the same checked size and time. Attaching a checksum to a name, which only the tests call today, puts the size right the same way. Lookups by name and by size and time still match the size, as they should.
	- Note: item 1's rule is unchanged. A job a worker has started still keeps its size and time. The recheck is after the checksum, inside the job.
	- Note: on Windows the view reads a symlink's own size and time, while the checksum reads what it points at. That was the same mismatch, and such a file now gets no checksum. Read only.
	- Branch: cachedb
	- Commit: 4e75a91
	- Test case: rj40hkdr, Thumbnail of an edited file test. rhd1cv38, File cache store test, with two stores of one checksum at different sizes, under two names. Both fail before the fix and pass after, on Linux.
	- Verified: those two, and the cache prune, thumbnail store, zoom, hold, order, memory, jobs and file checksum tests, pass three runs in a row on Linux. Lint and the Windows cross build are clean.
	- Verified: rj40hkdr passed on b29w on 20261002, in the native suite.
	- Acceptance signoff: Self-closed: what the cache keeps can't be seen on screen. rj40hkdr and rhd1cv38 cover it.
	- Closed: 20261003-112426

- Code review 20260928 item 11. Cache pruning sorts the whole thumbnail table while it holds the write lock.
	- ID: 2026092813381411
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: about 1 s per batch at 40k thumbnails, near the 3 s busy timeout at the 2 GiB default. Other windows' stores then fail.
	- Expected behavior: the file cache item, many processes share the cache without getting in each other's way.
	- Reproduced: yes for the timing, 20260928, Linux. The failed stores were read only.
	- Actual cause: both prune rules go by a thumbnail's age, the later of when it was made and when it was last drawn, and nothing indexed it. Each batch sorted or scanned the whole table while holding the write lock.
	- Origin: 5678432 and 6d52b4b, 20260921 (thumbdb). New ground. Confirmed.
	- Actual fix: the age is indexed, and both rules' queries walk the index. The index on draw time alone, which nothing used, is gone. The tables changed, so the cache starts over once, on the first run of this version.
	- Swept: every query the prune runs inside a write. Missing names are picked before the write, in row order. Orphaned records are found by a scan of the records with no sort, fast at 40 thousand. Draw counts, forget and store go by key. There is no other sort in the store.
	- Note: picking no longer grows with the table, but a batch still holds the write lock for as long as its deletes take. With 40 thousand thumbnails of 16 KB, another window's write waited at most about 1.4 s, against about 1.6 s before. Both are under the 3 s timeout. A cap on bytes per batch would cut it further. Not done here.
	- Branch: cachedb
	- Commit: 1a08e33
	- Test case: rhd69rjr, File cache prune test, with 1500 thumbnails. The prune's own pick queries walk at most one batch and sort nothing. Fails before the fix and passes after, on Linux.
	- Verified: same runs as item 9.
	- Acceptance signoff: Self-closed: how long the cache holds its lock can't be seen on screen. rhd69rjr covers it.
	- Closed: 20261003-112426

- Code review 20260928 item 6. The pre-push version guard reads the working tree, not the commit being pushed.
	- ID: 2026092813381406
	- Type: Bug
	- Status: Done
	- Needs external testing: Windows. One push to main through the hook from a Windows checkout, to see that a clean checkout reads as clean.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a push to `main` with no version bump passes when the bump is only uncommitted, or when another branch is checked out. The gate also runs on the tree rather than the pushed commit.
	- Expected behavior: the hook header, a push to `main` must raise the version.
	- Reproduced: yes, 20260928, Linux.
	- Origin: 2d475c4, 20260718. Not seen by an earlier round. Confirmed.
	- Actual cause: the hook read the version and the README badge from the working tree, and the gate builds whatever tree is checked out.
	- Actual fix: the version and badge are read from the commit being pushed. A push to main is refused when the tracked files differ from that commit, as with another branch checked out or an uncommitted edit. Untracked files are allowed. The container runner also refuses a clone other than the one its container has mounted, such as a second worktree, which it would otherwise have tested instead.
	- Note: a release is now pushed from a clean checkout of main in the main clone. A merge made while another branch is checked out still works, but main has to be checked out, with nothing uncommitted, before the push.
	- Signed off: 20260930, a clean main in the main clone for a release push.
	- Swept: both reads in the version guard, the Windows gate (the same check runs before it), and the container runner the gate's build and tests go through. The release and cross builds call the container directly, but a full run from another clone now stops at the debug build, before they run.
	- Branch: gatefix
	- Commit: 683eae0
	- Test case: rhtrxr80, Pre-push version guard, with five new hook runs: a bump only in the tree, another commit checked out, an uncommitted edit, an untracked file, and a badge right only in the tree. rj3ytv0b, Container clone test. Both fail before the fix and pass after, on Linux.
	- Acceptance signoff: Self-closed: a hook check with nothing on screen. rhtrxr80 and rj3ytv0b cover it. The Windows case is left to the next push from a Windows checkout.
	- Closed: 20261003-112426

- Code review 20260928 item 7. C static analysis on `main` and `dev` checks no files.
	- ID: 2026092813381407
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Needs external testing: Windows. The lint stage under MSYS2 runs the new scope test with the Windows git, and the whole-tree cppcheck pass.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: the file list is changes against `dev`, which is empty on `dev` and right after a merge to `main`. The stage prints OK.
	- Expected behavior: README, every build goes through static analysis, and the pre-push header, nothing reaches the release branch unverified.
	- Reproduced: yes, 20260928, Linux.
	- Origin: 0d92350, 20260802. Not seen by an earlier round. Confirmed.
	- Actual cause: dev and main only take merges, so the changes since the merge base with dev are always empty there.
	- Against: lint scoped by file, in the review's Decisions. File scoping stays on feature branches. dev and main lint the whole tree, chosen 20260930 over the latest merge.
	- Actual fix: on dev, main or a named base, cppcheck covers every first-party C file in the tree, plus untracked ones. Vendored code stays out, now `source/cut-n-paste-code/` as well as `vendor/`. Feature branches are unchanged.
		- The 15 findings a whole-tree pass had are gone. The string formatter uses the standard `va_copy` in place of the glib macro cppcheck could not follow. The conflict dialog leaked two names, and now frees them. The old bus test's one false positive is suppressed inline.
		- cppcheck runs on half the cores with a build dir, which keeps the cross-file checks. The whole tree takes about 13 seconds, against about 50 on one core.
	- Swept: the Windows pipeline runs the same C lint through the lint stage. No other check picks its files by a diff against dev. The scope is described in the lint's header, the code style guide, the cicd config and the suppressions file. The Windows script's own wording goes with item 31.
	- Branch: gatefix, lintall
	- Commit: 8116044, f63374e, fee3012
	- Test case: rj3ytty2, C lint scope test: a feature branch, dev after a merge and after a later commit, main after the release merge, untracked and deleted files, a branch with no C and one with an uncommitted edit, and a branch named as the base. Fails against the latest-merge scope and passes after, on Linux. rj46m24q, EEL string check test, and rj46pkw3, Conflict dialog test, for the C fixes. Both pin behavior the fixes kept, so neither fails before them.
	- Verified: the whole-tree C lint fails with the 15 findings before the C fixes and is clean after. Lint and the test ID check pass.
	- Acceptance signoff: Self-closed: a lint stage with nothing on screen. rj3ytty2, rj46m24q and rj46pkw3 cover it. The Windows case is left to the next native gate run.
	- Closed: 20261003-112426

- Code review 20260928 item 12. Redo after undoing Make link makes a different kind of link.
	- ID: 2026092813381412
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: on Windows a junction, symlink or hardlink comes back as a shortcut. Elsewhere a relative symlink, hardlink or shortcut comes back as an absolute symlink.
	- Expected behavior: redo makes what was made the first time.
	- Reproduced: yes, 20260928, Linux. A hardlink and a folder shortcut came back as absolute symlinks, and relative symlinks as absolute ones.
	- Actual cause: the undo record does not keep the dialog's choices.
	- Origin: the redo code is upstream. It broke when the choices came in with 73ec92e, 20260924 (makelink). Regression. Confirmed.
	- Actual fix: the undo record keeps the dialog's choices, and a redo makes the links with them.
	- Swept: Make link is the only job that takes the dialog's choices. A copy's redo does not keep the link copy choice either, but every copy job asks it again when links are in it, so nothing is picked quietly. That was read, not run.
	- Branch: linkfix
	- Commit: 342d30a
	- Test case: rj05egmb, Make link redo. Fails before the fix and passes after, on Linux.
	- Verified: the new test passes on Linux. Under wine the hardlink half passes on the first run and the redo alike, and the shortcut half fails both times the same way, as it always has there.
	- Verified: rj05egmb passed on b29w on 20261002, in the native suite.
	- Acceptance signoff: Self-closed: reproduced, test fails before and passes after, and passes on Windows.
	- Closed: 20261002-194800

- Code review 20260928 item 14. Edit link can remove the link when only the case of its name changes.
	- ID: 2026092813381414
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Linux and macOS, on a case-insensitive file system.
	- Incorrect behavior: renaming "Link" to "link" while changing its target leaves no link. The target is not touched.
	- Expected behavior: design.md, the old link is never missing.
	- Reproduced: yes, 20260928, Linux, on a ZFS dataset that normalizes names. "café" in its two Unicode spellings is one entry there, the same way "Link" and "link" are on a case-insensitive one.
	- Actual cause: the new link is renamed over the old name, and the old name, which is now the new link, is then removed.
	- Origin: 1866e56, 20260925 (linkedit). New ground. Confirmed.
	- Actual fix: before the rename, it checks whether the new name is the old link under another spelling. If it is, nothing is removed afterwards.
	- Note: on that ZFS dataset a lookup by the other spelling can still show the old link after the rename. Checking only after the rename missed the bug, so the check comes first, and the test reads the folder listing.
	- Swept: the Windows branch removes the old link before the rename, so it cannot hit this. A rename with no new target and a shortcut rename remove nothing.
	- Branch: linkfix
	- Commit: 342d30a
	- Test case: rhqxx81r, Link edit test, a new target under another spelling. It runs only where two spellings name one entry, and says so otherwise. Fails before the fix and passes after, on the normalizing dataset.
	- Verified: the link edit test passes on Linux, both on that dataset and in the suite's own temp folder, where this check is skipped.
	- Verified: rhqxx81r passed on b29w on 20261002, in the native suite. NTFS names one entry by both spellings, so the case check ran there.
	- Acceptance signoff: Self-closed: reproduced, test fails before and passes after.
	- Closed: 20261002-194800

- Code review 20260928 item 13. A Windows install for all users may not run for other users.
	- ID: 2026092813381413
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. Windows only. The lint stage and the installer check pass on Linux.
	- Needs external testing: Windows. Done 20261001 on b29w: the new test fails before the fix and passes after.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Related IDs: 2026100112505357
	- Target OS: Windows.
	- Incorrect behavior: the installed folder keeps the installing user's temp folder permissions, so another user sees the shortcut and PATH entry but cannot start the program.
	- Reproduced: yes, 20261001, b29w, by the new test. Before the fix none of the five installed items let Users read and run.
	- Actual cause: the unpacked tree is moved, not copied, and a move on one drive keeps the old permissions.
	- Origin: before 20260917, carried into 7284973, 20260925. Not seen by an earlier round. Confirmed 20261001.
	- Actual fix: the new tree is copied out of the temp folder into the staging folder beside the install, never moved, so every file takes the install folder's permissions. User and system installs take the same path.
	- Swept: no other move in the project's PowerShell takes a tree out of a temp folder. The installer's own swap renames the staging folder inside the folder it was copied to, so it keeps the right permissions. The unix side already sets owner and mode on a system install.
	- Note: the system target itself is not run by the test. A user install takes the same staging, under a parent that lets Users read and run where the temp folder does not.
	- Signoff: the fix changes the permissions of installed files, and the all-users target is checked by reading only.
	- Branch: installacl
	- Commit: a876603, test in 3c40b76
	- Test case: `cicd/win/test-install-acl.ps1` (rj72n4xb), in the Windows test stage. Every installed file and folder has to carry the read and run grant its parent passes down.

- Code review 20260928 item 15. Three items from code review 20260919 closed with no test and no reason.
	- ID: 2026092813381415
	- Type: Bug
	- Status: Done
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: items 11 and 12 were speed fixes with no speed test, and item 21's hoists and dead code have none. The review rules reopen a closed item with neither a test nor a reason.
	- Expected behavior: a speed fix has a check with a number to fail on.
	- Origin: code review 20260919. Confirmed.
	- Fixed: each of the three now has a check with a number to fail on, or a reason on its entry where none can be made.
	- Note: the fork still left in each name lookup of the theme vendoring script is filed as its own item.
	- Test case: `rj4jkr22 List view work per row test`, `cicd/utility/test-vendor-forks.bash` (rj4j8jk8), `rj4jbn1b Allocations per read test` and `rj4jewn6 Archive check cost test`.
	- Branch: speedpins

- Code review 20260928 item 10. A small PSD file can tie up a thumbnail thread for minutes.
	- ID: 2026092813381410
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 139 of 139 on 20260930.
	- Priority|Severity: Avg
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a 180 KB file took 15 s and a 720 KB one 64 s, and neither can be canceled.
	- Expected behavior: the PSD done item, a bad file is refused cleanly and a large one never costs full size.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: rows of zero length are accepted and padded out, so the work follows the declared size, not the file.
	- Origin: 056d3e0, 20260921 (thumbs). New ground. Confirmed.
	- Actual fix: a packed row shorter than two bytes for every 128 of the row cannot fill it, and a file with one is now refused before any row is decoded. A row with enough bytes that still ends early is padded as before. So the work a file can cause stays in step with its size.
	- Swept: the Photoshop reader is the only run-length row decoder among the thumbnail readers. The camera raw reader reads previews, and its own slow file is item 19.
	- Note: the reader still takes no cancel from the thumbnail thread. That would mean a cancel through the thumbnail factory for every reader, and a small file no longer runs long enough to need one. Left as is.
	- Note: waits on signoff because the fix picked one of two options the review offered, and a file with one short row now shows the type icon instead of a padded picture. The fuzz stage still owes a run with the changed seeds.
	- Test case: `test-nemo-psd`: rows at the least length are read, one byte less is refused, and a file of empty rows, 30000 by 30000 as psd and 60000 by 60000 as psb, is refused in under a second. Fuzz seed `zero-rows`, and `short-literal` reworked so it still reaches the literal-run bound.
	- Branch: psdrows
	- Commit: 24d99cd

- The keyboard menu opens at the pane's top left, not beside the selected item.
	- ID: 2026100408414402
	- Type: Enhancement
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 164 of 164 on 20261004.
	- Priority|Severity: Avg
	- Opened: 20261004-084144
	- Opened by: t00mietum
	- Related IDs: 2026092813381402, 2026100408525989
	- Target OS: all
	- Requirements:
		- Shift+F10 or the Menu key opens the menu beside the selected item, in the file list, icon view, tree and places sidebar.
		- With nothing selected, the menu opens near the pane's top left, as today.
		- A right click still opens the menu at the pointer.
	- Note: all views place the keyboard menu through one shared function in eel, so one change covers them.
	- Note: "Places" no longer takes the keyboard (2026100408525989), so it has no keyboard menu. That part of the requirements goes away.
	- Decisions:
		- The menu opens just below the item, from its left edge. With no room below, it opens above it. Confirmed 20261004. The item is the name cell in the list view, the icon and its label in the icon and compact views, and the row in the tree.
		- With several items selected, the menu goes by the one the keyboard is on when that one is selected. Otherwise it goes by the first selected item in sight.
		- A selected item scrolled out of sight gets the top left, as with nothing selected. The view is not scrolled to it.
		- Ctrl+F10 opens the folder's own menu, not the selection's, so it stays at the top left. No key press opens a menu at the pointer.
	- Note: the list, icon and compact views say where their selected item is, and the shared function puts the menu below it. The tree does the same for its keyboard row.
	- Swept: every caller of the shared function. The list, icon and compact views' selection menu and the tree's row menu now go below the item. The folder's own menu keeps the top left. The location menu only opens from a click. The tab bar never takes the keyboard. The rename field's menu has its own placement.
	- Branch: kbmenu
	- Commit: 8f7b345
	- Test case: `rjefm41d Keyboard menu beside the item test`, new, Linux only. It covers the list, icon and compact views, the tree, nothing selected and a right click. 7 of its 11 checks fail before the change, and all pass after. `rhtmbdma Keyboard context menu test` gained two cases, an item in sight and one scrolled out of it.
	- Verified: in the running program, Shift+F10 and the Menu key open the menu below the selected file in the list and icon views, and below the open folder's row in the tree. With nothing selected it opens at the folder's top left, and a right click opens it at the pointer. rjefm41d passed ten runs. rj04ta3n, rhtmbdma and rjedw75s passed five each. Full Linux suite 164 of 164, lint clean, Windows cross build clean.
	- Note: on Windows the keyboard menu takes the same placement code as on Linux. rhtmbdma is not Linux only, so its new cases run in the next native suite.
	- Acceptance signoff: Self-closed: the item said where the menu goes, the change does that, and rjefm41d pins it in every view. Below rather than over the item is in Decisions.
	- Closed: 20261004-095546

- Settings in an older SHCL format are kept as a backup and written again in the current one.
	- ID: 2026100311512222
	- Type: Enhancement
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: done. rjc4dd8z passed natively on b29w and vm925w, 20261003.
	- Priority|Severity: Avg
	- Opened: 20261003-115122
	- Opened by: t00mietum
	- Requirements:
		- When a shcl upgrade breaks compatibility with the application config file(s):
			- Check if the new shcl version has breaking changes. If so:
				- Rename the latest config file '[origname]_backup_YYYYmmDD-HHMMSS_format-v[shcl version].shcl'
				- Write a new config file with the same previous path and name, from scratch through shcl, using whatever settings and conversions shcl can handle.
	- Decisions:
		- The old file is copied to the backup name, then the new one replaces it in one step. Another copy of the app starting at that moment never finds the file missing.
		- A file with no format line came from a 2.x release, or a hand edit took the line out. At startup it is only rewritten when the old rules read it differently, and only spellings both rules agree on are changed. While running it is read as a hand edit and left alone. Its backup name says format 2.
			- Replaced 20261003 by the decision on 2026100314515200: such a file is converted as 2.x at startup.
		- A file in a newer format is read but never saved over. A change made meanwhile is kept and saved once the file is current again. Otherwise an older build and a newer one would keep rewriting each other's file, with a new backup each time.
		- If the backup can't be written, the old file is not saved over.
		- The new file has only the settings this release knows, each with its comment. Anything else stays in the backup.
	- Branch: shclfmt
	- Commit: dd08205
	- Test case: rjc4dd8z, Config format upgrade test. Fails before the change, passes after.
	- Acceptance signoff: Self-closed: rjc4dd8z fails before the change and passes after, on Linux and natively on Windows. Nothing on screen to judge.
	- Closed: 20261003-174609

- A CI test makes settings files in older SHCL formats and checks they are converted.
	- ID: 2026100314290808
	- Type: Task
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: done. The Windows cross build is clean, and rjcev513 passed natively on b29w and vm925w, 20261003.
	- Priority|Severity: Avg
	- Opened: 20261003-142908
	- Opened by: t00mietum
	- Related IDs: 2026100311512222, 2026100314515200
	- Requirements:
		- Before rc.1.
		- Part of CI/CD. The test makes settings files in each older SHCL format, then checks the app's own conversion, not one done with SHCL's help.
	- Note: rjc4dd8z only covers one hand-written 2.x file.
	- Done: settings were written with SHCL 1.2.0 (v1.0.0-beta2 and on) and 2.0.0 before format 3. Both headers are kept in `vendor/shcl-old`, and a small writer is built against each. It writes a settings file with the setters the app used then, with and without a few hand-edited lines, and records what that release read back from it. The test opens each file in the app, then opens what the app saved in a second run, and checks every value, the backup, and the format line.
	- Note: both old releases write these files the same way. Settings in a comma-decimal locale under 1.2.0 are not covered, since the build box has no such locale. Keys renamed since are left out; the test is about the format.
	- Note: four values do not read right. Filed as 2026100314515200, and marked in the writer so the test fails once they do.
	- Verified: rjcev513 passes. It fails when the app converts no file with no format line at startup, and when it converts every such file as 2.x.
	- Branch: shclold
	- Commit: d344708
	- Test case: rjcev513 Config old formats test.
	- Acceptance signoff: Self-closed: rjcev513 is in the suite and passes on Linux and natively on Windows.
	- Closed: 20261003-174609

- On Windows, the file cache test of many copies opening at once failed once in a parallel suite run with a disk I/O error.
	- ID: 2026100415281201
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 167 of 167 on 20261004, on cacheio.
	- Needs external testing: none left. Ran on vm925w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261004-152812
	- Opened by: owed native tests, 20261004
	- Related IDs: 2026100113372592
	- Target OS: Windows
	- Test environment: vm925w, session 0, native suite at 8 jobs.
	- Incorrect behavior: in rjch1a9a, one of 20 rounds had a copy with no store, after "could not set up the file cache: disk I/O error" at 302 ms.
	- Expected behavior: every copy gets its store, even with the rest of the suite running beside it.
	- Reproduced: once, 20261004, vm925w at 0ad01d1, in the full native suite. Passed 5 of 5 run alone right after, and passed in the full suite at bab9a49 earlier the same day.
		- Reproduced again 20261004 on vm925w, with rjch1a9a and the prune test run over and over, 12 at a time. rjch1a9a failed in each of three tries, 4 times in all. The message now has sqlite's own code and the system error, and said the -shm file could not be emptied because it was still mapped.
	- Possible cause: not known. The fix for 2026100113372592 retries a busy cache, and an I/O error may need the same, or it may be something Windows does to a new file under load.
	- Actual cause: a copy that quits without closing the store lets go of its locks on the -shm file before Windows unmaps the file from it. A copy opening at that moment finds no lock, takes itself for the first, and tries to empty the file. Windows refuses that while a view of the file is left. The sqlite in the Windows build gives up with a disk I/O error there, where older versions carried on. The app quits without closing the store too, so two windows could hit it, not only the test.
		- With the copies closing the store before they quit, 60 runs under the same load all passed, so the old copy's view is what is in the way.
	- Actual fix: setting up the store waits out that error as it does a busy file, for up to the same 3 s, on Windows only. The first read through the new journal, which is where a new file opens the -shm file, moved into the setup so it gets the same wait. Every setup failure now logs sqlite's own code and the system error.
	- Swept: every connection to the store is opened in one place, the prune's own and the test hook's included, and nothing else in the app uses sqlite. Once a connection is set up it has the -shm file locked and never empties it again.
	- Note: closing the store when the app quits would narrow the window for a normal quit, but not for a crash or a killed copy, and worker threads can still be using it then. Left alone.
	- Branch: cacheio
	- Commit: e9ac264, 18be0d2
	- Test case: rjch1a9a, File cache opened by many at once test. On Windows it now first leaves a view of a new -shm file with no lock behind it, as a quitting copy would, and lets it go after 300 ms; the store has to open. Fails before the fix at once with the disk I/O error, and passes after.
	- Verified: 20261004, Windows, vm925w: the new case fails before the fix and passes after. With the fix, rjch1a9a and the prune test run 60 times each, 12 at a time, three times over, all passed, against 4 failures in three tries before. The full native suite passed seven times at 8 jobs, 141 OK each, five before the fix and two after.
	- Verified: 20261004, Linux: the full suite passed 167 of 167, and lint and the Windows cross build are clean.
	- Acceptance signoff: Self-closed: reproduced, its test fails before the fix and passes after natively on Windows, and nothing is left to judge on screen.
	- Closed: 20261004-164709

- On a real Windows screen, the compress dialog test finds the options area capped at a different height than the dialog code works out.
	- ID: 2026100413554978
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261004-135549
	- Opened by: owed native tests, 20261004
	- Target OS: Windows
	- Test environment: vm925w, console session, 2512 px work area.
	- Incorrect behavior: rhtmbdmj fails at `test-nemo-archive-dialog.c` line 164. With Options opened, the scroll's max height is 2241, which is not what `nemo_archive_options_room` gives for that screen. The three made-up screen heights pass.
	- Expected behavior: the test passes, or the test is shown to be wrong and fixed.
	- Reproduced: yes, twice, 20261004, vm925w console session at bab9a49. Session 0 runs skip it for no monitor, so it may never have run on a real Windows screen before.
		- Again 20261004 on Linux, with a window manager on the test's display: off by the 25 px frame there.
	- Possible cause: not known. The dialog may size against a different monitor or work area than the test reads, or a scale factor is applied on one side only.
	- Actual cause: the test was wrong. The dialog counts its own frame in the closed height, as `nemo_archive_options_room` says it should. The test left the frame out. The Windows title bar and borders are the missing 39 px. Linux runs had no window manager, so no frame, and passed either way.
	- Actual fix: the test takes the closed height from the dialog's outer frame. On Linux it gets its own display with a window manager, so the frame is real there too. It also checks that the opened dialog sits inside the work area.
	- Swept: the other two dialogs sized from the work area, the delete check and Preferences, cap at a fraction of it and leave room for a frame.
	- Note: a title bar the toolkit draws itself, as on Wayland, is not counted by the dialog's frame measure. Not seen here; on X11 this dialog got no such title bar, even with `GTK_CSD=1`.
	- Branch: dlgheight
	- Commit: bb67758
	- Test case: rhtmbdmj, Compress dialog height test. It failed before on vm925w and on Linux with the frame, and passes after on both. It also fails on Linux when the dialog leaves its frame out of the room.
	- Verified: rhtmbdmj passes in vm925w's console session and on Linux. Windows cross build clean, lint clean.
	- Acceptance signoff: Self-closed: reproduced, the test was the defect, it failed before the fix and passes after, and there is nothing to judge on screen.
	- Closed: 20261004-144645

- The tab menu is made again on every right-click and never freed until its window closes.
	- ID: 2026100414202491
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 168 of 168 on 20261004, on tabmenu.
	- Needs external testing: none.
	- Priority|Severity: Low
	- Opened: 20261004-142024
	- Opened by: work on 2026100413510329
	- Related IDs: 2026100413510329
	- Target OS: all
	- Incorrect behavior: each right-click on a tab, or the Menu key on the tab bar, builds a new menu and attaches it to the tab bar. Nothing frees it when it closes, so menus pile up for the life of the window, and each one keeps a hold on its tab.
	- Expected behavior: a tab menu is freed once it closes.
	- Reproduced: yes, 20261004, Linux, by rjf00qfj. Five opens of the tab menu left ten menu windows behind, since its Move tab to submenu is one too.
	- Origin: inherited from upstream Nemo, in the baseline. Confirmed.
	- Actual cause: the tab menu is attached to the tab bar, and nothing destroys it before the tab bar goes. The history menu on Back and Forward and the list header's column menu are made the same way and never attached, so they stayed for the life of the program. Up and the other toolbar buttons made an empty menu on a right-click or long press, and never showed it.
	- Actual fix: a menu made for one open is destroyed once it closes, after the chosen item has run. The toolbar buttons make a menu only for Back and Forward.
	- Swept: the view's file, background and path bar menus, and the places and tree sidebar menus, are made once from the window's menu definitions and kept, so they do not pile up. The keyboard menu opens those same menus. The drop menu that asks Move, Copy or Link is freed after its answer. The rename field keeps one menu and replaces it on each open. A grep for `gtk_menu_new` finds no other menu made on demand.
	- Test case: rjf00qfj, Menus freed after closing test. It opens and closes the tab menu, the Back history menu, the column menu and a right-click on Up five times each, and counts what is left over. It failed before the fix on all four and passes after. rhtmbdma gained three sub-cases: a menu freed once it closes, picked from or not, with the picked item still run, and the drop menu freed after its answer. Each failed with its freeing taken out.
	- Verified: full Linux suite 168 of 168, lint clean, Windows cross build clean.
	- Branch: tabmenu
	- Commit: fdb03bb
	- Acceptance signoff: Self-closed: reproduced, its test failed before the fix and passes after, and nothing changes on screen.
	- Closed: 20261004-143637

- Two copies starting at once on a new file cache can find it locked, and one runs with the cache off.
	- ID: 2026100113372592
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 162 of 162 on 20261003, on cachelock.
	- Needs external testing: none left. Ran on vm925w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261001-133725
	- Opened by: code review 20260928 item 20
	- Related IDs: 2026092813381420, 2026100316054301
	- Incorrect behavior: while one connection sets up a cache file that did not exist, another opening it fails at once with "could not set up the file cache: database is locked" and that copy has no cache until it is started again. The store's own comment says a busy one waits up to 3 s.
	- Expected behavior: the second one waits its turn and uses the cache.
	- Reproduced: once, 20261001, Linux, with a second connection opening a new file as a window did. Not on demand. Again 20261003, Linux: in 2 of 200 rounds of four processes setting up one new file at once, and every time while another connection held the write lock on a new file.
	- Possible cause: the first switch of a new file to its journal mode takes a lock that sqlite does not wait for.
	- Actual cause: the switch to WAL reads the file and then takes the write lock. sqlite never waits on that second step, since two readers each waiting on the other would wait for good, so it answers busy at once and the busy timeout never applies.
		- The failure on vm925w came from the test, not the store. The test's holder committed with no wait of its own. While the store waits it keeps trying, and each try takes a read lock for a moment. One was there right as the holder committed, so the commit was refused, and the holder kept the write lock until the store gave up. The store's 3 s wait ran to 4.9 s on a busy box.
	- Origin: 9fd0939, 20260921 (thumbdb). New ground. Confirmed.
	- Actual fix: the setup is tried again on a busy answer, every 10 ms, for as long as the busy timeout.
		- The test's holder now waits on a busy file like any other copy, and its commit is checked. A reader is put in the way of that commit on every run.
	- Swept: the prune's own connection opens through the same setup. There is no other journal mode change. The store opens the file in one place, which sets the busy timeout, and a refused commit is always rolled back, so no store connection keeps a lock past one.
	- Note: once, under a parallel full build, two copies in one round still had no store. How long they had waited was not shown then. The test now prints it, and a copy that gave up only after the whole busy timeout is listed but not counted, since that is the designed limit on a disk that slow. Not seen again in 450 rounds under the same load.
	- Note: a busy answer to the version read at open is taken as a file from another version, and the file is wiped. Filed as 2026100316054301.
	- Branch: quitlock, then cachelock
	- Commit: aced497, f7c0854
	- Test case: `rjch1a9a File cache opened by many at once test`. One connection holds the write lock on a new file for 300 ms while the store opens it, then rounds of four copies open a new file at the same instant. The held lock fails before the fix and passes after, on Linux. The rounds hit the bug about once in a hundred before the fix, so they are a sweep rather than the pin. The held lock also has a reader in the way of its release. With the old holder that fails every run, the store giving up after 3.3 s with "database is locked". With the fix it passes.
	- Verified: rjch1a9a passes on Linux, 60 rounds. Lint is clean. On 20261003, the reader case fails before the test fix and passes after. 320 runs of rjch1a9a, 16 at a time, passed beside 240 runs of the store and prune tests. The full Linux suite, the Windows cross build and lint pass.
	- Progress log:
		- 20261003: rjch1a9a passed natively on b29w at 43a9126, but failed once in the native suite on vm925w at c78aa9e. With the write lock held for 300 ms, the store gave up after 4.9 s with "database is locked". It passed five runs in a row there on its own afterward. Back to Queued.
		- 20261003: the cause was the test's own holder. Fixed on cachelock. Waits on a native run on vm925w.
	- Verified: 20261004, Windows, at bab9a49: rjch1a9a passes natively on vm925w. The native suite there had no failures, 142 OK.
	- Acceptance signoff: Self-closed: its tests pass on Linux and natively on Windows, and nothing is left to judge on screen.
	- Closed: 20261004-140500

- A waiting store can miss every gap between the prune's writes.
	- ID: 2026100319191870
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 164 of 164 on 20261004, on prunegap.
	- Needs external testing: none left. Ran on vm925w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261003-191918
	- Opened by: work on 2026093010493420
	- Related IDs: 2026093010493420, 2026092813381436
	- Incorrect behavior: while the prune hands space back, each step holds the write lock for only about 40 ms, but the steps follow one another with no gap. A store from another window sleeps up to 100 ms between tries, so it can keep waking while the next step has the lock. It waited 1 to 1.4 s, against the 3 s timeout.
	- Expected behavior: a waiting store gets its turn within about one step of the prune.
	- Reproduced: yes, 20261003, Linux, with 40 thousand thumbnails of 16 KB and with 5 thousand of 128 KB, and a second connection writing every 20 ms during the pass.
	- Possible cause: nothing in the prune lets go of the lock long enough for a waiting connection to notice. The same can happen between delete batches.
	- Actual cause: sqlite gives a free lock to whoever asks first, not to whoever has waited longest. The prune went from each write straight into the next, while sqlite's own wait backs off to 100 ms between tries, so a waiter kept waking while the next write had the lock. The only gaps were the ones left now and then while the journal was copied back into the file.
		- Reproduced again 20261004, Linux, in the new test: with a window's read open through the pass, a writer sat out 24 to 29 of the prune's 34 writes in one wait, every run.
	- Decisions:
		- The prune rests as long as each write took, and at least 40 ms, so a pass takes at least twice as long. The Clean up button waits for it too. Confirmed 20261004.
	- Actual fix: after each write the prune rests as long as the write took, and at least 40 ms, so the gap allows two Windows clock ticks. A copy waiting on the file tries again every 5 ms instead of backing off to 100 ms, and its 3 s limit is timed on the clock rather than by adding up its sleeps.
	- Swept: every write the prune makes in a loop: dropping missing names, orphans, old thumbnails, the size rule and each compact step. The claim and the release are single writes. Every connection is opened in one place, so the prune's own waits the same way as a window's. Nothing else in the store writes in a loop.
	- Note: a store's commit can also copy the journal back into the file, which on a busy disk took about a second. That is time on the disk, not a wait for the prune's lock, and it holds the store's own lock, so it belongs with 2026092813381436.
	- Note: rhd69rjr's time limit went to 120 s, since eight copies at once took 31 to 35 s with the rests.
	- Branch: prunegap
	- Commit: 3474eb0
	- Test case: rhd69rjr, File cache prune test, the waiting writer case. Another connection writes every 20 ms while a pass takes out 16 MB and hands it back, with a read held open through the pass. It may sit out at most two of the prune's writes in one wait. Fails before the fix, at 24 to 29 of 34, and passes after, at one.
	- Verified: 20261004, Linux: rhd69rjr fails before the fix in 26 of 26 runs, 16 of them at once, and passes after in every run, 16 at once included. 48 runs of rjch1a9a beside 16 each of the store and prune tests, 16 at a time, all passed. The full Linux suite, lint and the Windows cross build pass.
	- Verified: 20261004, Windows, at bab9a49: rhd69rjr and rjch1a9a pass natively on vm925w. The native suite there had no failures, 142 OK.
	- Acceptance signoff: Self-closed: its tests pass on Linux and natively on Windows, and nothing is left to judge on screen.
	- Closed: 20261004-140500

- The rename field moves a window that does not exist yet when it is sized before it is shown.
	- ID: 2026100412363910
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261004-123639
	- Opened by: work on 2026100412230310
	- Related IDs: 2026100412230310
	- Target OS: all
	- Incorrect behavior: an EelEditableLabel given its size before it is realized logs a critical, since its size code moves its text window, which is only made on realize.
	- Expected behavior: no critical. The text window takes its size when it is made.
	- Reproduced: yes, 20261004, Linux, with the field put in a window before the window is shown. A rename in the program puts it in a view already on screen, and was not seen to log it.
	- Possible cause: `eel_editable_label_size_allocate` calls `gdk_window_move_resize` on `text_area` without checking that the widget is realized.
	- Actual cause: as above. GTK sizes a window's widgets before it realizes them, so a field in a window that is not yet shown is sized first, and the text window is not there to move.
	- Origin: upstream.
	- Actual fix: the size code moves the text window only once the field is realized. Realize already makes the text window at the field's place and size, and now says so at the site.
	- Swept: the only other `gdk_window_move_resize` in a size handler, the path bar's, already checks first. The icon container's one `gdk_window_move` realizes its dialog just before. The field's other uses of the text window, on map, unmap, pointer motion and the screen position, only run once it is realized.
	- Branch: editlbl
	- Commit: 9d15cf8
	- Test case: rjew8g59, Rename field sized before shown test. A field sized with no window, then one put in a window before it is shown, log nothing, and the text window matches the field when shown, after a move and when shown again. Fails before the fix with two criticals, passes after.
	- Verified: 20261004, Linux: rjew8g59 fails before the fix and passes after. It also fails when the text window is made at the wrong size, and when the size code never moves it. Full Linux suite 167 of 167, rjes67yy included. The Windows cross build and lint pass.
	- Acceptance signoff: Self-closed: reproduced, rjew8g59 fails before the fix and passes after, and the sweep is answered.
	- Closed: 20261004-132748

- The rename field in the icon and compact views gives a screen reader nothing to read.
	- ID: 2026100412230310
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261004-122303
	- Opened by: work on 2026100409554600
	- Related IDs: 2026100409554600, 2026100412363910
	- Target OS: all
	- Incorrect behavior: the rename field's accessible object is built on the do-nothing object, so its text calls fail a type check and give no text or position.
	- Expected behavior: a screen reader can read the rename field and find it on screen.
	- Reproduced: yes, 20261004, Linux. During a rename in the icon view, the field's accessible object gave no name, no text and no extents, and logged four criticals.
	- Possible cause: the same run-time derivation the canvas items had, in `eel_accessibility_create_derived_type`. GTK 3 registers no accessible factory for widgets, so the registry hands back the do-nothing object. The fix is likely to build it on the GTK label or widget accessible directly.
	- Actual cause: as above. The type was derived from what the registry gave for the field's widget type, the do-nothing object, which is not a widget accessible. Every text call, the name and the state set ask for the widget and got nothing.
	- Origin: upstream.
	- Actual fix: the field's accessible is now an ordinary type built on GTK's widget accessible, with the text and editable text parts, and GTK makes it for the field. It gives its name and text, takes an edit, and gives the field's place and each character's.
	- Swept: `eel_accessibility_create_derived_type` has one other user, the icon container. It derives from the canvas's own registered accessible, not the do-nothing object, and works: during a rename its last child is the field. No other code in the tree derives an accessible type at run time.
	- Branch: renameatk
	- Commit: 58e96c5
	- Test case: rjes67yy, Rename field accessible test. Fails before the fix, passes after.
	- Verified: 20261004, Linux: rjes67yy fails before the fix, 13 failures and 16 criticals, and passes after. In the program, a rename in the icon view gives the file name as its name and text, and extents that match the field, with no warning. Full Linux suite 166 of 166, rjerav6k, rjefm41d and rhtmbdma included. The Windows cross build and lint pass.
	- Acceptance signoff: Self-closed: reproduced, rjes67yy fails before the fix and passes after, and the sweep is answered.
	- Closed: 20261004-123639

- Icons in the icon and compact views give a screen reader no place on screen.
	- ID: 2026100409554600
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 165 of 165 on 20261004, on iconatk.
	- Priority|Severity: Low
	- Opened: 20261004-095546
	- Opened by: work on 2026100408414402
	- Related IDs: 2026100412230310
	- Target OS: all
	- Incorrect behavior: an icon's accessible object has no position or size, and asking where its picture is logs a critical warning.
	- Expected behavior: a screen reader can find each icon on screen.
	- Reproduced: yes, 20261004, Linux. Asking an icon's accessible object for its extents fails a type check.
	- Possible cause: the icon's accessible type is built on the plain one for any object, so it lacks the part that gives a position. Its picture position code still calls that part on itself.
	- Actual cause: the icon's accessible type was built on the plain GObject one, with no position part. The canvas item accessible that has one was never used for icons. It was also derived at run time from what the registry gives a plain object, which under GTK 3 is the do-nothing object, so it could not find its own item.
	- Origin: upstream.
	- Actual fix: the canvas item accessible is now an ordinary type built on the GObject one, with the position part, and the icon's accessible is built on it. Icons now give their extents on screen and in the window, and the picture's place. An icon scrolled out of sight gives no place, for the picture too.
	- Swept: the three position calls in the icon accessible, for the picture, a character and a point in the text, now all reach the canvas item's. The rubber band selection box gets the same canvas item accessible. The icon container's own accessible is built from the canvas's registered factory and already had a position. The rename field is the one other type derived at run time, and it gets the do-nothing object too; filed as 2026100412230310.
	- Branch: iconatk
	- Commit: 472da37
	- Test case: rjerav6k, Icon accessible extents test. Linux only. Fails before the fix, passes after. rjefm41d and rhtmbdma still pass.
	- Verified: 20261004, Linux: rjerav6k fails before the fix, with no extents and over a hundred criticals per view, and passes after in the icon view, the compact view and the icon view scrolled part way down. Full Linux suite 165 of 165. The Windows cross build and lint pass.
	- Acceptance signoff: Self-closed: rjerav6k passes, and the fix does what the item asked and no more.
	- Closed: 20261004-122303

- A failed Windows install leaves a half-copied folder beside the install folder.
	- ID: 2026100112505357
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261001-125053
	- Opened by: review of code review 20260928 item 13
	- Related IDs: 2026092813381413
	- Target OS: Windows.
	- Incorrect behavior: when the copy into the staging folder fails partway, such as on a full disk, the partial `<install folder>.new.<number>` folder stays next to the install. No later install removes it. The old install is still there and still runs.
	- Expected behavior: a failed install leaves nothing behind, as install.bash does.
	- Reproduced: yes, 20261004, b29w, by the new test. All three ways out left the staging folder. Worse, when the old folder could not be moved aside because a file in it was held open, its other files were moved one at a time into `<install folder>.old.<number>`, so the old install no longer ran.
	- Origin: the copy fallback from 7284973, 20260925, which item 13's fix made the path every install takes. The split old install is from the same commit: PowerShell 7's Move-Item falls back to moving file by file when a folder rename is refused. Confirmed 20261004.
	- Sweep: every way out of the Windows install after the staging folder exists: the failed copy, the old folder that cannot be renamed, and the failed final rename.
	- Actual fix: the staging folder is removed on every way out once it exists, as install.bash does. The install folder renames are plain folder renames, which fail whole instead of splitting the folder. install.ps1 is 1.3.2.
	- Swept: all three Windows ways out, through one cleanup around the whole stage and swap. install.ps1's unix half had the same gap for a failed copy and a failed move aside, and now cleans up the same way. install.bash already removed its staging folder on all three. No other PowerShell in the project moves a folder: n8runfm.ps1 moves single files on Windows, and the sandbox agent moves one job file.
	- Branch: stageclean
	- Commit: d8da36d
	- Test case: `cicd/utility/test-install-staging.ps1` (rjeqef3d), in the Windows test stage and in the lint stage on Linux. Each way out has to fail, leave nothing beside the install folder, and keep the old install whole.
	- Verified: 20261004, b29w. rjeqef3d fails on 1.3.1, with all three staging folders left and the old install split, and passes on 1.3.2. rj72n4xb still passes. On Linux rjeqef3d fails on 1.3.1 for the copy and the move aside and passes on 1.3.2, and rhqmz9n8 passes. The lint stage passes. install.ps1 parses clean in PowerShell 5.1 and 7.
	- Acceptance signoff: Self-closed: the item said what a failed install should leave, the change does that, and rjeqef3d fails before and passes after.
	- Closed: 20261004-120651

- On Windows, a link that leads nowhere is never shown as broken.
	- ID: 2026100312494903
	- Type: Bug
	- Status: Done
	- Needs external testing: no. Ran natively on b29w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261003-124949
	- Opened by: item 2026092813381404
	- Related IDs: 2026092813381404
	- Target OS: Windows
	- Incorrect behavior: the app tells a broken link by GIO answering with the link type after following it. On Windows GIO answers with the link's own type instead, a file or a folder, whether the link leads anywhere or not. So the broken link checks in `nemo-file.c`, used for emblems, favorites and opening, never fire there.
	- Expected behavior: a link that leads nowhere is treated as broken on Windows too.
	- Reproduced: GIO's answer yes, 20261003, b29w. The app's side is read only. Plausible.
		- The app's side too, 20261004, b29w: a file link, a folder link and a junction that lead nowhere all read as not broken.
	- Possible fix: the check the archive scan now uses on Windows, which opens through the link.
	- Actual cause: the app asked only for the link type GIO gives a link that leads nowhere, and on Windows GIO never gives it.
	- Origin: the inherited check, wrong on Windows since the port. Widened by ac5347f, 20260827, which lists folders without following links. Confirmed.
	- Decisions:
		- 20261004: a link on a share, or one whose way passes a share or a drive mapped to one, is not looked into and shows as not broken. Opening through the link, as the possible fix said, would go to the share with no user action. A call made without asking, from the share rule. The archive scan keeps opening through the link, since a compress is a user action.
	- Actual fix: on Windows the app works out where a link leads from the links themselves. Each name on the way is read from the folder above it, and each link from its own target, so nothing is opened through a link. A chain of links is followed the same way, and a loop counts as leading nowhere. The answer is kept until the file is read again, since the type column asks on every draw.
	- Swept: every caller of the broken link check: the type text and detailed type, which sort by type also reads, the favorites toggle, and opening, which offers to move a broken link to the Trash. The emblems never asked; a link wears the link emblem either way. No other check tells a broken link by GIO's type: link copy reads the reparse tag on Windows, and the archive scan has its own. The check of a drive mapped to a share now sits beside the link code and is shared with the shortcut icons.
	- Branch: winlinks
	- Commit: eea8dfb
	- Test case: rjehwjw7, Link end test, Windows only: file, folder and absolute links, a junction whose folder is gone, links Windows will not follow (/ and * in a relative target), chains good and gone, a loop, links to a share directly and through another link, and the kept answer until the file is read again. Fails before the fix and passes after, natively on b29w. The share and read-again rows each fail with their part of the fix taken out.
	- Verified: 20261004, rjehwjw7, rfmxrdpg and rfhr0zw0, which uses the moved drive check, pass natively on b29w. Full native suite there at eea8dfb: 141 passed, 10 skipped, none failed. Full Linux suite 164 of 164.
	- Acceptance signoff: Self-closed: reproduced, red before the fix and green after, and swept. The broken link dialog on opening is the existing one.
	- Closed: 20261004-104311

- On Windows, a symlink made with / in a relative target leads nowhere.
	- ID: 2026100312494904
	- Type: Bug
	- Status: Done
	- Needs external testing: no. Ran natively on b29w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261003-124949
	- Opened by: item 2026092813381404
	- Related IDs: 2026092813381404
	- Target OS: Windows
	- Incorrect behavior: a symlink keeps its target as it was typed. Windows does not follow a relative target spelled with /, so the new link opens nothing, though / works almost everywhere else on Windows.
	- Expected behavior: a symlink made by the app leads where it was pointed.
	- Reproduced: Windows' side yes, 20261003, b29w: such a link cannot be opened. That the app writes one is read only. Plausible.
		- The app's side too, 20261004, b29w: links it made to `sub/target.txt`, `sub/dir` and `../sub/target.txt` opened nothing.
	- Possible fix: write a relative symlink target with backslashes, as junctions already are.
	- Actual cause: Windows keeps a relative symlink target exactly as given and splits it only at `\`. An absolute one it rewrites itself, so only relative ones were hit.
	- Origin: 5791854, 20260902 (copying links). Confirmed.
	- Actual fix: every symlink the app makes has its target written with `\`.
	- Note: "a link keeps its own spelling" in the links copy item is about relative against absolute. A relative link stays relative.
	- Swept: Windows symlinks are made in one place, which Make link, Edit link, link copy and the default link all go through. Junctions already used `\`.
	- Branch: winlinks
	- Commit: eea8dfb
	- Test case: rjehwjw7, Link end test, Windows only: links made with / to a file, to a folder, and up a folder, each read through, and the stored target read back. Fails before the fix and passes after, natively on b29w.
	- Verified: 20261004, rjehwjw7 and the link tests (rfwwdyvg, rhqxx81r, rhmye3d0, rhmye3d3, rhr6ggms) pass natively on b29w. Full native suite there at eea8dfb: 141 passed, 10 skipped, none failed. The Windows cross build is clean.
	- Acceptance signoff: Self-closed: reproduced, red before the fix and green after, and swept.
	- Closed: 20261004-104311

- Under rar, a selected linked folder whose name starts with @ is not left out.
	- ID: 2026100312494905
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 164 of 164 on 20261004, on winlinks.
	- Needs external testing: no. The archive tests passed natively on b29w on 20261004.
	- Priority|Severity: Low
	- Opened: 20261003-124949
	- Opened by: item 2026092813381404
	- Related IDs: 2026092813381404
	- Incorrect behavior: a linked folder that is not followed is left out of a rar with `-x` and its name. For a selected folder named `@x` that is `-x@x`, which rar reads as a list file called `x`, so the folder is not left out and the run may fail.
	- Expected behavior: the linked folder is left out, whatever its name.
	- Reproduced: rar's side yes, 20261003, Linux: `-x@b` makes rar look for a list file `b`. The job's side is read only. Plausible.
		- The job's side too, 20261004, Linux: the archive could not be created, and rar said it could not open `x`.
	- Actual cause: rar reads `@name` as a list file to load after `-x`. It does the same with a selected name when nothing by that name can be opened, as with a link that leads nowhere, so such a link named with a leading @ failed its first run as well.
	- Origin: b8e1401, 20260925, which leaves linked folders out by name. Seen by item 2026092813381404's fix, which only passed such names over. Confirmed.
	- Actual fix: rar is handed a name that starts with @ as `./@name`, after `-x` and as a selected name. It stores it as `@name`. What rar says about `./@name` is read as being about `@name`. The job no longer passes those names over, so a link that leads nowhere named with a leading @ is left out of the real run by name, like any other.
	- Swept: every name rar is handed: the names left out, the selected names, and the names of the first run, which all go through the one command builder. The output reader. 7z takes `-x!@name` and a selected `@name` as names. Archive paths are always full paths, so they never start with @.
	- Note: rar's `-r` also tries every name beside a selection, picked or not, and takes same-named files from the folders below. Filed as 2026100410431108.
	- Branch: winlinks
	- Commit: a33ea89
	- Test case: rhr6ggmt, Archive option combinations: new rows for a selected linked folder `@x` alone, then beside a link `@gone` that leads nowhere, in every format. rev86z08, Archive options test: rows for `-x./@x`, `./@gone`, and the reader taking `./@gone` as `@gone`. Both fail before the fix and pass after, on Linux.
	- Verified: 20261004, rhr6ggmt, rev86z08 and rewygsbg pass on Linux, and natively on b29w, where rar ran 43 rows. Full Linux suite 164 of 164. Full native suite on b29w at eea8dfb: 141 passed, 10 skipped, none failed.
	- Acceptance signoff: Self-closed: reproduced, red before the fix and green after, and swept.
	- Closed: 20261004-104311

- Code review 20260928 item 26. The installers go ahead when a release has no sums file.
	- ID: 2026092813381426
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: the plan says "UNVERIFIED", and with the yes flag the install goes on.
	- Expected behavior: design.md and README, downloads are checked before anything is unpacked.
	- Decisions:
		- 20260928: refuse by default. An explicit override flag installs anyway. The yes flag alone does not.
		- 20261004: the override is `--no-verify` in install.bash and `-NoVerify` in install.ps1. No other name is taken.
	- Reproduced: yes, 20261003, Linux. Both installers installed a stable and a prerelease build that had no sums file, with the yes flag.
	- Origin: a2b0e10, 20260723. Not seen by an earlier round. Confirmed.
	- Actual fix: with no sums file in the release, both installers stop while resolving, before the plan and the question, and name the override. The override is `--no-verify` in install.bash and `-NoVerify` in install.ps1. With it the plan says "UNVERIFIED". A sums file that is there is still checked with the override on. Help text, README and design.md say so. Installer version 1.3.1.
	- Swept: both installers, stable and dev channels, each through one shared branch in its resolve step. A `--from` archive, a path or a URL, never had a sums file and is still not checked; design.md and the help now say so. A sums file that fails to download, has no line for the build, or does not match still stops the install, override or not. The rename covered both installers' options, help, plan and errors, README, design.md and rhtrxr81; a grep of the repo finds the old names nowhere else.
	- Branch: nosums, noverify (the rename)
	- Commit: 070301a, efa8053
	- Test case: `cicd/linux/test-install-download.bash` (rhtrxr81), the no-sums cases. Fails before the fix, passes after.
	- Verified: rhtrxr81 fails before the fix on both installers (installed with the yes flag, no refusal) and passes after: refused on stable and dev with the yes flag and nothing downloaded, installed with the override, and with the override a release with sums still verified and a tampered build still refused. The lint stage passes.
	- Verified: 20261004, after the rename. rhtrxr81 fails on the 1.3.0 installers, which don't know the new names, and passes on 1.3.1. The lint stage passes. install.ps1 parses clean.
	- Acceptance signoff: Signed off 20261004, with the flag renamed to --no-verify
	- Closed: 20261004-100156

- A cache prune batch holds the write lock longer as thumbnails get bigger.
	- ID: 2026093010493420
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 162 of 162 on 20261003, on cachelock.
	- Priority|Severity: Low
	- Opened: 20260930-104934
	- Opened by: code review 20260928 follow-up
	- Related IDs: 2026092813381411, 2026092813381436
	- Incorrect behavior: a batch is 256 thumbnails whatever their size. With 40 thousand of 16 KB, another window's store waited up to about 1.4 s. Bigger thumbnails make each batch longer, toward the 3 s timeout, past which the store is dropped.
	- Expected behavior: a batch also ends after about 1 MB of thumbnails, so each hold of the lock stays short at any size.
	- Reproduced: yes for the 1.4 s, 20260930, Linux, under item 11.
	- Actual cause: a batch was 256 thumbnails whatever their size, and a thumbnail takes longer to delete the bigger it is.
	- Actual fix: a batch also ends after about 1 MB of thumbnails, in each prune step that deletes them: records no name points at any more, old thumbnails, and the size limit. The size step still stops once the file is under its limit.
	- Swept: every prune step that deletes thumbnails. Forgetting names of missing files deletes names only. The step that hands space back goes 256 pages at a time whatever the thumbnail size.
	- Note: measured again with 40 thousand thumbnails of 16 KB and with 5 thousand of 128 KB. With the limit, the deletes no longer kept another window's store waiting over 0.2 s, against three such waits, up to 0.33 s, at 128 KB before. The longest waits, 1 to 1.4 s, were in the step that hands space back, both before and after, so the 1.4 s above most likely came from there too. Filed as 2026100319191870.
	- Branch: cachelock
	- Commit: 5ddde28
	- Test case: rhd69rjr, File cache prune test, the batch bytes case. Thumbnails of 256 KB go by each of the three steps, and no one write may take out much over 1 MB. Fails before the fix, at 4 to 6 MB in one write, and passes after, on Linux.
	- Verified: rhd69rjr fails before the fix and passes after. 80 runs of it, 16 at a time, passed. The full Linux suite, the Windows cross build and lint pass.
	- Acceptance signoff: Self-closed: how long the cache holds its lock can't be seen on screen. rhd69rjr covers it.
	- Closed: 20261003-193326

- A busy answer to the version check at open wipes the file cache under other copies.
	- ID: 2026100316054301
	- Type: Bug
	- Status: Done
	- Needs external testing: Windows: rhd1cv38 in the native suite, for its new case. Not needed to close.
	- Priority|Severity: Low
	- Opened: 20261003-160543
	- Opened by: work on 2026100113372592
	- Related IDs: 2026100113372592
	- Incorrect behavior: when the version cannot be read at open, as when the file stays busy past the wait, the answer is taken as a file from another version. The cache files are removed and started over, while other copies may still have them open.
	- Expected behavior: a failed read leaves the file alone, and that copy runs with the cache off, as for other open errors.
	- Reproduced: yes, 20261003, Linux, with the version read answered busy on cue. The file was started over and its rows were gone.
	- Actual cause: a version read that failed before it ran came back as -1, which the open took for another version, so it wiped the file. One that failed while running came back as 0, a file with no tables yet, and the tables and version were then written into a file nobody had read.
	- Origin: 6d52b4b, 20260921 (thumbdb). New ground. Confirmed.
	- Actual fix: any failed read of the version leaves the file alone and the open gives up, so that copy runs without the cache. A damaged file still starts over.
	- Swept: every read in the store whose answer can lead to a wipe. The size and free page reads answer -1 and the prune stops. The prune's check marks damage only on a damage code. A failed read of the prune claim is an error. Only the version read led to a wipe.
	- Branch: cachelock
	- Commit: 86634da
	- Test case: rhd1cv38, File cache store test, the unread version case. The version read is answered busy while the store opens, and the row stored before has to be there after. Fails before the fix and passes after, on Linux.
	- Verified: rhd1cv38 fails before the fix, the file started over and the row gone, and passes after. 160 runs of it, 16 at a time, passed. The full Linux suite, the Windows cross build and lint pass.
	- Acceptance signoff: Self-closed: what the cache keeps can't be seen on screen. rhd1cv38 covers it.
	- Closed: 20261003-193326

- The leak tests can pass a small leak, or skip, when a worker thread starts during the counted rounds.
	- ID: 2026100308563229
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The Linux suite passed 162 of 162 on bedccab, none skipped.
	- Needs external testing: none. The leak tests are Linux only.
	- Priority|Severity: Low
	- Opened: 20261003-085632
	- Opened by: review of item 2026092813381423
	- Related IDs: 2026092813381423
	- Incorrect behavior: where a worker thread starts during the counted rounds, the reading is taken again. Memory held at that time can be given back during the new reading and cancel out a leak of one small block a round, so the test passes. A reading that comes out below zero is reported as a heap that cannot be read, and the test skips.
	- Expected behavior: a leak of one block a round fails every time, and a leak test skips only where the heap really cannot be read.
	- Reproduced: yes, 20261003, Linux, under load. With 24 bytes leaked a round, 2 of 48 runs passed. With nothing leaked, the stopped zip and tar.gz tests skipped in about three runs of four of a suite run repeated in parallel.
	- Origin: 377d761, on this item's branch. Confirmed.
	- Actual cause: a reading was judged only by the thread count before and after it. GLib's pools stop a thread that sits idle, and what it gives back can fall in the reading taken again. A thread just joined is still listed for a moment, so its end can go unseen. A thread also takes some memory the first time it does a piece of work, which can come a reading after it started, so a clean run could fail too.
	- Actual fix: GLib's pools keep their threads for the life of a leak test, so a thread only ever starts. A reading counts only where the same threads, by id, were there before and after, leaving out a thread on its way out. Growth past the limit has to show in two such readings, since a leak grows the heap in every one. A reading below zero is a reading; only a heap that cannot be read skips. Where threads come or go in every one of eight readings, the test fails and says so.
	- Note: a leak tied to a pool thread ending is now out of these tests' view, since pool threads no longer end.
	- Sweep: every test that reads the heap.
	- Swept: the leak tests are the only users of the shared reading. The config test's list read check reads the heap itself, but looks for megabytes, which a thread's memory cannot reach. The archive stop time test reads no heap. The leak tests stay inside `if not is_windows` in the test meson.build.
	- Branch: leakretry
	- Commit: bedccab
	- Test case: rjcth67d Leak test self-check. It leaks one small block a round while threads start and end around it, and passes only where the reading calls it a leak.
	- Verified: under load, 12 tests at a time. The self-check failed on the old reading in 119 of 120 runs and passed on the new in 72 of 72. With 24 bytes leaked a round, the stopped zip test passed on the old reading in 2 of 192 runs, and on the new in 0 of 96. With nothing leaked, the stopped zip and tar.gz tests on the old reading skipped in 21 of 256 runs and failed in 4. On the new, 0 of 192 skipped or failed. The other leak tests passed 96 of 96 on the new. Lint is clean.
	- Acceptance signoff: Self-closed: test-only change. rjcth67d fails on the old reading and passes on the new.
	- Closed: 20261003-184621

- A keyboard shortcut change is never saved where the shortcut file's folder is missing, and the file is upstream Nemo's.
	- ID: 2026100315470225
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261003-154702
	- Opened by: work on 2026100113372562
	- Related IDs: 2026100113372562
	- Incorrect behavior: shortcut changes are saved to `~/.gnome2/accels/nemo`, and that folder is never made. With no `~/.gnome2/accels`, nothing is saved, and on Windows it is never there. Where it is there, the file is the one upstream Nemo uses, so the two programs read and overwrite each other's shortcuts.
	- Expected behavior: shortcuts are saved, in a file of the app's own.
	- Reproduced: yes, 20261003, Linux, with an empty home. No file after quit.
	- Possible fix: save beside the settings, making the folder when needed.
	- Decisions:
		- 20261003: the shortcut file moves beside the settings file, and its folder is made when needed. It stays in GTK's own format.
		- 20261003: on the first start after the move, an existing `~/.gnome2/accels/nemo` is read once, so custom shortcuts carry over. The old file is left alone.
	- Actual cause: the path was upstream's, and nothing made its folder. GTK's save gives up quietly when it cannot open the file.
	- Actual fix: the file is `accels` beside `settings.shcl`, and the settings folder is made as before. A start that finds no `accels` reads `~/.gnome2/accels/nemo` if there, then writes `accels` at once. That `accels` exists is how "once" is known, so the old file is read on that start only and is never written. `--reset` empties `accels` rather than removing it, so the next start does not go back to the old file. Its help text still says settings and bookmarks.
	- Note: rjch1b9a no longer makes a folder, and looks for the file in the new place.
	- Swept: every caller of the shortcut path: the load at startup, the delayed and quit-time save, and `--reset`. `GNOME22_USER_DIR` is now only in the old-file path and the test. The other `.gnome2` use, `nemo_is_in_system_dir`, is about trusted desktop files and was left alone. `n8runfm.ps1` only passes `--reset` through. README never named the file. design.md and the changelog now say where it is.
	- Test case: `rjcscb0t Shortcut file beside settings and carried over once test`. With no `~/.gnome2`, a change is saved to `accels` and nothing is made there. With an old file, its shortcut is used on the first start and copied into `accels`, and the old file is unchanged. A later change to the old file is not read. After `--reset`, `accels` is empty and the next start has the default. Fails before the fix and passes after, on Linux, and so does rjch1b9a.
	- Verified: full Linux suite 161 of 161, Windows cross build, lint clean.
	- Branch: accelmove
	- Commit: 3c5c3ab
	- Acceptance signoff: Self-closed: both calls were answered in Decisions, and rjcscb0t fails before the fix and passes after.
	- Closed: 20261003-175916

- The window title does not follow a change to the path separator.
	- ID: 2026100221072783
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Priority|Severity: Low
	- Opened: 20261002-210727
	- Opened by: item 2026092813381422
	- Related IDs: 2026092813381422
	- Incorrect behavior: the window listens for `path-separator` on the main settings group, but the key is in the windows group, so the handler never runs. A title that spells out a path keeps the old separator until something else sets it.
	- Expected behavior: the title follows the separator at once, as the places pane does.
	- Reproduced: yes, 20261003, Linux. With a tab open, a change to `windows.path-separator` in the settings file never reached the window.
	- Origin: 4942625 listened on the main group, and e821544 then moved the key to the windows group. Not seen by an earlier round. Confirmed.
	- Possible fix: listen on the windows group. A lint check of each listened key against the group the schema puts it in would find any others.
	- Actual cause: as above. Nothing checked that a listened key is in the group it is listened on.
	- Actual fix: the window listens on the windows group. The settings handler lint now checks each key in a handler, a read or a write against the group the settings table puts it in. design.md, "Handlers on settings groups", says so.
	- Sweep: every handler, read and write on a settings group with a key known before run time.
	- Swept: 447 calls over `source/`. One more was wrong: the thumbnail size handler in the file code listened on the main group, but the key is in the icon view group, so a change to it waited for a restart. Fixed the same way. The 5 calls whose key is only known at run time were left alone.
	- Note: on Linux the separator changes nothing a person sees, so the title is only worth a look on Windows.
	- Branch: grpfix
	- Commit: 0a17659 (lint), bc533c7 (test), 5f6d314 (fix)
	- Test case: rjahhesy, Held view settings handlers test. A change to the path separator has to make the window spell its path again. It fails without the fix. `lint-pref-handlers.py --self-test`, new cases for a key on the wrong group, a key in no group and a macro it cannot read.
	- Verified: 20261003 on vm925w, with whole paths in the title. A change of `windows.path-separator` to slash in the settings file turned the title from `C:\Users\...` to `C:/Users/...`, and back again on the change back.
	- Acceptance signoff: Self-closed: rjahhesy passes in the full Linux suite, and the title was seen to follow the separator on Windows.
	- Closed: 20261003-174609

- After an icon view closes, icon captions and the label length limits stop following their settings until restart.
	- ID: 2026100221072784
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Priority|Severity: Low
	- Opened: 20261002-210727
	- Opened by: item 2026092813381422
	- Related IDs: 2026092813381422
	- Incorrect behavior: the icon view container connects three settings handlers once per process, with no data, and the first container to be freed removes them. After that, the captions and the label length limits for icon view and desktop no longer follow their settings.
	- Expected behavior: those settings keep working for every icon view until the program quits.
	- Reproduced: yes, 20261003, Linux. Closing an icon view tab removed all three handlers.
	- Origin: upstream. Not seen by an earlier round. Confirmed.
	- Keep: design.md, "Handlers on settings groups", the row for no data or a file static.
	- Possible fix: drop the three disconnects from the container's finalize.
	- Actual cause: as above. The container's finalize removed handlers that every container shares.
	- Actual fix: the three disconnects are gone, and the finalize with them, per the row above. The settings handler lint now reports a disconnect on a settings group whose data is NULL or a file static.
	- Sweep: every disconnect on a settings group with no data or a file static.
	- Swept: the lint over `source/` finds none left. The other disconnects on settings groups are the icon container moving from one group to the other, the Current folder tab's struct, and a test's local.
	- Branch: grpfix
	- Commit: 0a17659 (lint), bc533c7 (test), 4e37c4e (fix)
	- Test case: rjahhesy, Held view settings handlers test. A handler connected with no data or a static must still be there after a list or icon view tab closes. It fails without the fix. `lint-pref-handlers.py --self-test`, a new case for such a disconnect.
	- Verified: 20261003, Linux, on screen. With a second icon view tab opened and closed, a change to `icon-view.captions` in the settings file put the sizes under the icons in the tab left open.
	- Acceptance signoff: Self-closed: rjahhesy passes in the full Linux suite, and the captions were seen to follow their setting after a tab closed.
	- Closed: 20261003-174609

- A hand edit to the settings file can be lost when the program saves at the same moment.
	- ID: 2026100221273001
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: done. rdjjz89r passed on vm925w on 20261003, in a session with a monitor. The native suite skips it where there is none.
	- Priority|Severity: Low
	- Opened: 20261002-212730
	- Opened by: item 2026092813381422
	- Related IDs: 2026092813381422
	- Incorrect behavior: a change made in the program is saved a couple of seconds later, and the save writes the whole file without checking whether it changed on disk since it was read. A hand edit saved just before that, and not yet picked up, is overwritten. The program then takes the event for it as its own write, so the edit is gone with no message.
	- Expected behavior: a hand edit is never lost to the program's own save.
	- Reproduced: yes, 20261003, Linux. A hand edit written while a change made in the program waited to be saved was gone after the save, and stayed gone once the monitor caught up. A file removed by hand was put back, and a file in a newer format was saved over, the same way.
	- Actual cause: the save wrote the document it had in memory without looking at the file. The late event for the edit then matched the save and was ignored as the program's own write.
	- Origin: before this branch. Code review 20260919 item 16 fixed the other direction, a change in the program lost to a hand edit. Not seen by an earlier round. Confirmed.
	- Decisions:
		- When both sides changed, the file wins for every key the program did not change. The program's unsaved keys go on top of a fresh read, and then it saves. No dialog. Call made without asking; reversible.
		- A key changed both ways keeps the program's change. That is the rule item 16 of review 20260919 already follows when the monitor gets there first, so the answer does not depend on which comes first. The hand edit can still be the later of the two. Going by time would need a time per key, checked against the file's.
	- Actual fix: a save reads the file first. If it is not what the program last wrote or read, it is reloaded the way the monitor does it, with the unsaved keys put back and the changed keys announced, and then the save goes ahead. A newer-format file found that way is left alone, and a removed file means defaults plus the unsaved keys, both as the monitor already does. A very short window is left between that read and the write, which no ordinary file write can close.
	- Swept: the monitor's reload and the save now share one reload. The exit flush and `--reset` go through the same save. The bookmarks file is the only other watched file the program writes; it is saved at once on each change with no delay, so it was left alone.
	- Verified: config tests rg6a49ar, rdjjz89r, rjc4dd8z, rjcev513, reqzgh4g, rfazc870 and rf2w8yxr pass on Linux after a clean build. Lint clean.
	- Branch: handedit
	- Commit: 3ad0dcc
	- Test case: rdjjz89r (`test_hand_edit_survives_save`, `test_hand_delete_and_newer_before_save`), red before the fix and green after.
	- Acceptance signoff: Self-closed: rdjjz89r fails before the fix and passes after, and passes on Linux and natively on Windows.
	- Closed: 20261003-174609

- Code review 20260928 item 17. Hardlinking a selected symlink links the symlink, not the file.
	- ID: 2026092813381417
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Needs external testing: done on vm925w, 20261003.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a relative symlink hardlinked into another folder arrives dangling.
	- Expected behavior: the hardlink warning, the same file under a second name.
	- Reproduced: yes, 20260928, Linux.
	- Actual cause: the hardlink is made of the symlink itself. Windows does the same, per its documentation.
	- Origin: 73ec92e, 20260924 (makelink). New ground. Confirmed.
	- Actual fix: a symlink is followed first, so the hardlink is a second name for the file it leads to. A symlink that leads nowhere fails with the usual error.
	- Swept: the hardlink call has one implementation per platform, and Make link is its only caller. Both follow now.
	- Branch: linkfix
	- Commit: 342d30a
	- Test case: rfwwdyvg, Link copy test, a hardlink of a relative symlink made in another folder. Fails before the fix and passes after, on Linux.
	- Verified: the link copy test passes on Linux. The Windows code builds but was not run, since wine makes no symlinks.
	- Note: rfwwdyvg passed on b29w on 20261002, in the native suite, but it skips its symlink checks without a word when symlinks can't be made, so this case is not shown to have run.
	- Verified: 20261003, rfwwdyvg passed natively on vm925w with symlinks allowed, so the hardlink of a symlink case ran.
	- Acceptance signoff: Self-closed: rfwwdyvg passes on Linux and natively on Windows with its symlink cases run.
	- Closed: 20261003-174609

- Code review 20260928 item 24. Settings comments that look like the SHCL info block are removed on save.
	- ID: 2026092813381424
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a line of the user's own that starts like the info block's lines is taken as part of it and dropped, and so is a bare `##` next to one.
	- Expected behavior: a comment of the user's own is kept.
	- Reproduced: yes, 20261003, Linux. A note spelled `##    Aligned   like the info block` between two `##` lines, at the top of the file, was gone after the next save, `##` lines and all.
	- Origin: 851c5aa, 20260925 (shclbanner). New ground. Confirmed.
	- Actual cause: the save took any line starting `##` and four spaces as part of the info block wherever it was, and any `##` next to one.
	- Actual fix: only a run of `##` lines that has the block's SHCL line or its format line is the block, the same test SHCL itself uses. Inside that run only the block's own lines come off, so a `## note` written against it stays too.
	- Sweep: every place that takes the info block out of the file.
	- Swept: `apply_catalog` is the only one. Nothing else strips it, and `shcl_set_banner` is not called.
	- Branch: shclold
	- Commit: e2c055b
	- Test case: rg6a49ar Config defaults list test, new case for notes spelled like the block, at the top and right after it. Fails before the fix, passes after.
	- Acceptance signoff: Self-closed: rg6a49ar fails before the fix and passes after, in the full Linux suite.
	- Closed: 20261003-174609

- The application's quit hook never runs, so a keyboard shortcut changed just before quit is lost.
	- ID: 2026100113372562
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 160 of 160 on 20261003.
	- Needs external testing: none. The test needs X, and on Windows the shortcut file's folder is never there, so nothing is saved there either way (2026100315470225).
	- Priority|Severity: Low
	- Opened: 20261001-133725
	- Opened by: code review 20260928 item 20
	- Related IDs: 2026092813381420, 2026100315470225
	- Incorrect behavior: both application classes put their quit work in `quit_mainloop`, which GLib has not called since 2.32. So a shortcut map change still waiting out its 30 s is never saved, and the "still unmounting" notice is never taken down. The rest of it frees memory the exit frees anyway.
	- Expected behavior: a shortcut changed just before quit is there on the next start.
	- Reproduced: yes for the hook, 20261001, Linux. The lost shortcut and the notice left up, 20261003, Linux.
	- Origin: upstream. Not seen by an earlier round. Confirmed.
	- Possible fix: move what still matters to GApplication's `shutdown`, and drop what the exit makes pointless.
	- Actual cause: GLib calls `shutdown` at the end of a run, not `quit_mainloop`.
	- Actual fix: the quit work moved to `shutdown` in both classes. The base class saves a shortcut change still waiting, and the window class takes down the "still unmounting" notice. Freeing the icon caches, the undo manager and the style provider was dropped, since the exit frees them.
	- Note: the file cache is still written at the end of `main ()`. `shutdown` runs before the thumbnail threads are done, and they may still be storing.
	- Note: the old hook took the notice down through the unmount done step with no message, which logs a critical. The notice is now withdrawn on its own.
	- Note: the shortcut file's folder is never made, so with none nothing is saved at any time. Filed as 2026100315470225. The test makes the folder.
		- Fixed under 2026100315470225: the file is now beside the settings, and the test no longer makes a folder.
	- Swept: these two were the only `quit_mainloop` overrides, and the only application classes.
	- Branch: quitlock
	- Commit: aced497
	- Test case: `rjch1b9a Shortcut saved and notice withdrawn at quit test`. The built program changes a shortcut and puts the notice up once its window is up, and the window is closed inside the 30 s. The shortcut is in the saved file and back on the next start, and the notice was withdrawn. Fails before the fix and passes after, on Linux.
	- Verified: rjch1b9a, rj750n43 and the file cache store and prune tests pass on Linux. Lint is clean.
	- Acceptance signoff: Self-closed: rjch1b9a fails before the fix and passes after, in the full Linux suite.
	- Closed: 20261003-174609

- Code review 20260928 item 25. The .deb changes with the filesystem it is built on.
	- ID: 2026092813381425
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Linux.
	- Incorrect behavior: Installed-Size comes from disk blocks, so one tree read 3106 KB on one filesystem and 5176 KB on another.
	- Expected behavior: README and design.md, Linux builds can be rebuilt from their commit to the same bytes.
	- Reproduced: yes, 20260928, Linux. Again 20261003: one tarball packed on ext4, btrfs and tmpfs gave Installed-Size 5520, 5320 and 5316, and three different .deb files.
	- Origin: f49050b, 20260804. The README claim came in d07af73, 20260925. Not seen by an earlier round. Confirmed.
	- Actual cause: Installed-Size was `du -sk` of the package tree, which counts disk blocks.
	- Actual fix: it is counted the way dpkg-gencontrol counts it, from file sizes: each file or symlink rounded up to a KiB, a hardlink once, anything else 1.
	- Swept: the other `du` calls in the pipeline only print a size to the console. The .rpm was already the same on all three filesystems.
	- Branch: relrepro
	- Commit: f3b4e94
	- Test case: `cicd/linux/test-deb-size.bash` (rjcma0se). Fails before the fix, passes after.
	- Verified: rjcma0se fails before the fix (200 or 128 against 113, and the .deb differs between filesystems) and passes after. The beta2 tarball now packs to the same .deb on ext4, btrfs and tmpfs. The lint stage passes.
	- Acceptance signoff: Self-closed: a packaging check with nothing on screen. rjcma0se covers it.
	- Closed: 20261003-163500

- Code review 20260928 item 27. The Linux release image is not pinned.
	- ID: 2026092813381427
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a box without the image builds it from current Ubuntu 22.04 updates, so later compilers give other bytes.
	- Expected behavior: design.md, the same bytes on any box on any day, and README, dependency versions are pinned.
	- Reproduced: pinned 20261003 by rjcma0t3, which fails on the old Dockerfile. `ubuntu:22.04` and the live archive both move, and the image on this box was built from them on 20260804.
	- Origin: d2b180e and 860904d, before 20260917. Not seen by an earlier round. Plausible.
	- Actual fix: the Dockerfile pins the base by digest, jammy-20260731.1, which is the base the current image was built on. apt reads the archive from snapshot.ubuntu.com as it stood on 20260804, when the current image was built.
	- Decisions:
		- 20261003: pin to the dates the current image was built from, not to today, so a box that builds the image matches the one that built the published releases. Moving on means moving the digest and the snapshot date together.
		- 20261003: the image on this box is kept as it is. It was not replaced, since it builds the same bytes.
	- Swept: the dev image (`Dockerfile.dev`) and the Windows cross build image are not release lanes, and are left unpinned.
	- Branch: relrepro
	- Commit: f3b4e94
	- Test case: `cicd/linux/test-release-image-pin.bash` (rjcma0t3). Fails before the fix, passes after.
	- Verified: rjcma0t3 fails on the old Dockerfile and passes on the new one, and fails when the security source is left out of the rewrite or apt runs first. An image built from the new Dockerfile has the same packages as the current image but two: libsqlite3 is 0.7, where the current image has 0.8 from a later hand install, and the current image also has the beta1 .deb installed. A clean release build of dev `f4d2386` in each gave the same tarball, byte for byte. The lint stage passes.
	- Acceptance signoff: Self-closed: a build image check with nothing on screen. rjcma0t3 covers it.
	- Closed: 20261003-163500

- Code review 20260928 item 28. Release notes can carry a build number no binary has.
	- ID: 2026092813381428
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: artifacts built on `dev` are published under the merge commit on `main`, which has another date, and nothing checks the two match.
	- Reproduced: yes, 20261003, Linux. In a scratch repo, release.bash tagged a `--no-ff` merge with artifacts stamped with the dev commit's date.
	- Origin: `cicd/utility/release.bash`, before 20260917. Not seen by an earlier round. Plausible.
	- Actual fix: release.bash refuses to tag unless every artifact has HEAD's commit date: the tarball, .deb and .rpm file times, the rpm build time, and the stamp in the zip's own exe. The stamps are read by `cicd/utility/release-stamps.py`. So the artifacts must be built on main after the merge.
	- Decisions:
		- 20261003: build on main after the merge rather than tag the dev commit. The tag stays on the merge, as documented, and a rebuild of the tag then matches.
	- Swept: the hosted Windows workflow already stamps the tag's own commit date. The Windows zip's runtime exes keep their packagers' dates, so only the app's exe is read. The sums file is not stamped.
	- Branch: relrepro
	- Commit: f3b4e94
	- Test case: `cicd/utility/test-release-stamp.bash` (rjcma0tt). Fails before the fix, passes after.
	- Verified: rjcma0tt fails before the fix (tagged with dev's artifacts) and passes after, with dev's stamp on the tarball, the zip's exe or the .deb each refused. The stamp reader gives one date for each beta2 artifact, which matches dpkg-deb and rpm. The lint stage passes.
	- Note: the next release cut builds its artifacts on main after the merge, with `cicd/cicd.bash --no-publish`.
	- Acceptance signoff: Self-closed: a release script check with nothing on screen. rjcma0tt covers it.
	- Closed: 20261003-163500

- Code review 20260928 item 29. The Windows GUI smoke check can miss its window and can use someone else's display.
	- ID: 2026092813381429
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: `grep -q` downstream of the window list can end the pipeline early and read as no window. The display number is fixed and not checked first.
	- Reproduced: yes, 20261003, Linux. A long window list read as no window. With another server already on :99, the app was started on that server.
	- Actual cause: `grep -q` quits at its first match, so the window list's writer fails and the pipeline reads as failed. The display was always :99, with no check, and when another server already held it the app was started on that one.
	- Origin: 0caf474, 20260721. Not seen by an earlier round. Confirmed.
	- Actual fix: the window list is read in full, then matched. The display is the first free one from :120 up, and is used only once its lock names the server started there and that server answers. `GUI_SMOKE_DISPLAY` moves the start.
	- Swept: the profiler had a fixed :97 with no check, and now starts its display the same way, from one shared helper. `gui-headless.bash` already refuses a number another server holds. Its default of :99 is only for a run by hand, since the demo recorder passes its own number. `docker-run.bash`, `build-cross.bash` and one check in `lint-c.bash` piped into `grep -q`, and now don't. The Bash lint now fails on `grep -q` or `grep -m` reading a pipe. The `| head -1` sites were left, since each reads only a few short lines.
	- Note: the lint check reads one line at a time, so a pipe split across two lines is not caught.
	- Branch: smokehang
	- Commit: 034e095
	- Test case: rjcbfbx8, GUI smoke check test, and rjcc6jnz, the Bash lint's pipe check. Both fail before the fix and pass after.
	- Verified: rjcbfbx8 fails before the fix on both counts and passes three runs in a row after it. The smoke check passes on the real build, and so do the profiler, the Windows cross build and the lint stage.
	- Acceptance signoff: Self-closed: a pipeline check with nothing on screen. rjcbfbx8 and rjcc6jnz cover it.
	- Closed: 20261003-140636

- Code review 20260928 item 30. A hang found by the fuzzer stops the stage with the wrong label.
	- ID: 2026092813381430
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a timeout exits with libFuzzer's default code, which the stage reads as neither a find nor a clean run, and the targets after it do not run.
	- Reproduced: yes, 20261003, Linux. A target that hangs stopped the stage with "exited 70", and the targets after it did not run.
	- Actual cause: libFuzzer gives each kind of find its own exit code, and only its own reports used the stage's find code. A hang exits 70, a memory error caught by the address checker exits 1, and running out of memory exits 71.
	- Origin: `cicd/linux/fuzz.bash`, fuzzlines, 20260926. New ground. Confirmed.
	- Actual fix: a hang and a memory error now exit with the stage's find code. Any failed run that saved an input also counts as a find, which covers running out of memory, since that code can't be set. The find line names the saved input. One input may run 25 seconds before it counts as a hang, set by `FUZZ_TIMEOUT`.
	- Decisions:
		- The 25 second hang limit is new, and the usual one for fuzzing elsewhere. libFuzzer's own limit is 20 minutes, longer than the whole stage.
	- Swept: each way libFuzzer ends a run with a find was checked for its exit code: its own reports (crash signal, leak), a hang, a memory error, and running out of memory. All four now count as a find. The memory error had the same fault as the hang.
	- Branch: smokehang
	- Commit: 2621f60
	- Test case: rjcbfcx7, fuzz stage exit test. Fails before the fix, passes after.
	- Verified: rjcbfcx7 fails before the fix and passes after. A short run of the real targets is clean with the new limit and codes. The lint stage passes.
	- Acceptance signoff: Self-closed: a pipeline check with nothing on screen. rjcbfcx7 covers it.
	- Closed: 20261003-140636

- The theme vendoring script still starts a process for every name it looks up.
	- ID: 2026093013281956
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260930-132819
	- Opened by: code review 20260928 item 15
	- Incorrect behavior: the lookup asks for its context pattern through a command substitution, one fork per name tried, in a part of the file whose own comment says it is fork-free.
	- Expected behavior: the lookup answers the way the scorer does, with no fork.
	- Reproduced: yes, 20261003, Linux. The fork count for the test set was 36 processes for 9 icons, 20 of them from the name lookups.
	- Origin: left by the fix for code review 20260919 item 12. Confirmed.
	- Actual cause: the context pattern came back on stdout, so every name tried read it through a command substitution.
	- Actual fix: the context pattern comes back through a global, as the score does. The test set now starts 17 processes for 9 icons. The bar went from 4.5 to 2.5 per icon, so one fork more per icon fails it again. The failure message now gives the bar right for an even number of halves too.
	- Swept: the only other substitution below the fork-free comment is the readlink for a symlinked alias. Bash has no builtin for it, and it runs once per alias followed, not per name tried, so it stays. The rest of the count is the mkdir and copy for each icon staged.
	- Test case: `cicd/utility/test-vendor-forks.bash` (rj4j8jk8). Fails before the fix (36 for 9 icons) and with one fork put back at the first lookup of each icon (26), passes after (17).
	- Verified: the fork count, the vendoring self-test and the lint stage pass. shellcheck is clean.
	- Branch: pins2
	- Commit: 76d3176
	- Acceptance signoff: Self-closed: the speed of a build script, with nothing on screen. rj4j8jk8 covers it.
	- Closed: 20261003-134129

- The string list settings read has no check on its cost per read.
	- ID: 2026093013501931
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260930-135019
	- Opened by: code review 20260928 item 15
	- Related IDs: 2026092813381415
	- Incorrect behavior: code review 20260919 item 21 stopped every settings read from building its key path each time. `rj4jbn1b Allocations per read test` reads flags, numbers, strings and enums, but no string list, so a string list read that builds the path each time again still passes. Item 21's entry says the test covers the settings reads.
	- Expected behavior: the test also fails when the string list read builds its path per read, or item 21's entry says why that read has no check.
	- Reproduced: yes, 20260930, Linux.
	- Origin: missed by the first review of code review 20260928 item 15, which named only the string and enum reads. Confirmed.
	- Actual fix: the test reads every string list a thousand times, once with the defaults and once with each list stored in the file. The bar is what the lists cost by themselves, the array and one copy per item, plus half an allocation per read. Both counts match that exactly today.
	- Swept: every typed read in `nemo-config.h` now has a count: flags, whole numbers, decimals, strings, enums and string lists. `nemo_config_get_int` goes through the whole number read, and `nemo_config_get_default_boolean` reads only the key table.
	- Test case: `rj4jbn1b Allocations per read test`, string list cases. Fails with the path built per read (75000 and 80000 allocations against 59000 and 64000), passes on the code as it is.
	- Verified: the test passes on Linux after a clean build, and the lint stage is clean.
	- Branch: pins2
	- Commit: f0afcaa
	- Acceptance signoff: Self-closed: a cost per read, with nothing on screen. rj4jbn1b covers it.
	- Closed: 20261003-134129

- Code review 20260928 item 22. The list view's row shading handlers can outlive the view.
	- ID: 2026092813381422
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The gate passed 145 of 145 on 472f957.
	- Needs external testing: one Windows box run, since the view base's Windows-only dot-files handler changed. The cross build is clean.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: changing row shading while a closed tab's view is still held, as a rename or unmount does, calls into a freed tree view.
	- Reproduced: yes, 20261002, Linux. A closed tab's list view, still held, kept 25 handlers on the settings groups, row shading, its color and folder expansion among them. No crash was seen.
	- Origin: f172064, 20260918 (row shading). Same class as code review 20260919 items 3 and 10. Regression of that class. Confirmed.
	- Actual cause: the list view, the view it is built on and the icon view connected their settings handlers with a plain connect and removed them in finalize. A view held after its tab closes is not finalized until it is let go, but its widgets go when the tab closes, so a settings change in between ran the handlers on freed widgets.
	- Actual fix: every handler on a settings group whose data is an object is now connected with `g_signal_connect_object`, so it goes when its owner is torn down, and the disconnects that went with them are gone. The two that move the icon container from one group to the other stay. The settings handler lint now reports a plain connect for an object on a settings group. design.md, "Handlers on settings groups", has the rule.
	- Sweep: every plain connect on a settings group with an object as its data, per design.md "Handlers on settings groups".
	- Swept: 64 connects in the list view, icon view, view base, window, icon container, icon grid container, path bar, places and tree sidebars, toolbar, action manager, job queue, both plugin settings pages and the main application. The settings handler lint over `source/` reports none left.
	- Note: left as they were: the Current folder tab's struct, handlers with no data or a file static, the separator test's local, and handlers on other objects, which only the table covers. Two defects found nearby are their own items, 2026100221072783 and 2026100221072784.
	- Branch: prefhandlers
	- Commit: 4081143 (lint), bc2cd7d and 472f957 (test), 17a568c (fix)
	- Test case: rjahhesy, Held view settings handlers test. A closed tab's list or icon view, still held, has no handler left on a settings group, and row shading, its color and folder expansion still reach the open tab. It fails with the list and icon view files from before the fix. `lint-pref-handlers.py --self-test`, new cases for a held view, a handler never disconnected and a local not disconnected.
	- Acceptance signoff: Self-closed: a handler left on a held view shows nothing on screen. rjahhesy and the lint self-test cover it, and the design.md rule table stands. The Windows case runs with the next native suite run.
	- Closed: 20261003-112426

- Code review 20260928 item 23. File jobs and the clipboard leak memory on every operation.
	- ID: 2026092813381423
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The gate passed 155 of 155 on d1872a5.
	- Needs external testing: none. Nothing changed is Windows-only code, and the cross build is clean.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Related IDs: 2026100307122400, 2026100308563229, 2026100308563234
	- Incorrect behavior: each move job, each file moved by rename, each job's progress, each drag's clipboard check, and a canceled zip leak a little. A few smaller leaks sit in search, theme and window setup.
	- Reproduced: yes, 20261003, Linux. Every site grew the heap on each repeat, and a stopped zip by about a quarter of a megabyte.
	- Origin: upstream, apart from the zip one from 6c2418f, 20260820. Not seen by an earlier round. Confirmed.
	- Actual cause: each site kept or copied something and never let it go on one way out. The progress manager also kept its own hold on every job's progress after the job finished, so the whole progress went, not only its lock. A stopped zip failed the archive library's last writes, and the library then skipped the step that frees its compressor. Every compress and unpack job also kept two holds on its stop handle and let go of one.
	- Actual fix: each site frees what it owns. The progress manager lets go of a job's progress when the job finishes, and lets go of its handler when it goes itself. The zip writer treats a write after a stop as done rather than failed, and the job still reports the stop. Compress and unpack jobs take one hold on the stop handle. A stopped tar fails its last writes again once the job is done with it, so a stop near the start of a big file ends at once, as it did before.
	- Sweep: whatever these functions own and miss on a way out, in the same files and the progress manager.
	- Swept: copy, duplicate and link jobs free their desktop location already. The progress manager's hold on each finished job. The progress lock beside its condition. The window's view id, visible columns and column order, kept like its sort column. The theme check's first copy when it gives up. The bookmark metadata error when that file is missing. The clipboard check is the only place that waits for the clipboard; every other read is handed its data and freed by the toolkit. The stop handle in the compress and unpack job setup; the file jobs' setup was already right, and nothing else asks a progress for its stop handle.
	- Note: item 2026100307122400, the 150 bytes left by every archive job, was this item's, and closed with it. Leaks seen only at exit, in the startup command line and the undo manager, were left alone.
	- Branch: leaks
	- Commit: 9619c6a (progress), fa3de32 (move), d9c526d (clipboard), 63f91f0 (zip), e98eb39 (search), 0c7b356 (bookmarks), b565bf2 (window), cc31dd3 (theme), 4802a37 (stop handle), 377d761 (leak test readings), d1872a5 (tar stop)
	- Test case: rjbkzvdv Job progress leak test, rjbkzwe7 Move job leak test, rjbmdd0s Clipboard drag check leak test, rjbmh7g1 Stopped zip leak test, rjbn18rq Search query leak test, rjbn19rn Bookmarks load leak test, rjbn328w Bookmarks file load leak test, rjbpmcxy Extract job leak test, rjbqagjf Stopped tar.gz leak test. Each repeats its operation and fails when the heap grows, and each fails with its fix taken out. rjbpyy28 Archive stop time test: a stopped tar.gz or tar.xz of a 16 GiB file has to end in under twice the time a 4 GiB one takes, plus 3 s. The window and theme sites have no test, since only the running program reaches them; a window closing and the theme check were checked by hand to leave nothing behind, before and after.
	- Acceptance signoff: Self-closed: leaks show nothing on screen. The nine leak tests and rjbpyy28 cover it. The window and theme sites have no test, since nothing in the suite reaches them; item 35's sanitizer build is where they get one.
	- Closed: 20261003-112426

- Code review 20260928 item 18. A relative symlink between two shares of one server does not resolve.
	- ID: 2026092813381418
	- Type: Bug
	- Status: Done
	- Needs external testing: Windows, a relative link made on one share to a file on another share of the same server comes out with the full path, and one within a share still comes out relative.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Windows.
	- Incorrect behavior: a link from `\\srv\a\x` to `\\srv\b\y` is written as `..\..\b\y`, which Windows cannot follow above a share.
	- Reproduced: yes, 20260928, in the Windows build under wine, by the relative path it spells. Not tried against a real share.
	- Actual cause: only the first part of the two paths had to match. For a share path that is the server, but the share has to match too.
	- Origin: 1866e56, 20260925 (linkedit). New ground. Confirmed.
	- Actual fix: a share path needs both the server and the share in common, including the long `\\?\UNC\` form. Otherwise the full path is used.
	- Swept: Make link's relative symlinks and the shortcut's relative path both go through the same spelling code.
	- Branch: linkfix
	- Commit: 342d30a
	- Test case: rfwwdyvg, Link copy test, on Windows only. Fails before the fix and passes after, under wine.
	- Verified: the link copy test's share checks pass under wine. Its one failure there is the old one, since wine makes no symlinks.
	- Note: rfwwdyvg passed on b29w on 20261002, in the native suite. Whether the box had two shares to link between is not shown.
	- Acceptance signoff: Self-closed: two shares of one server with a relative link between them is hard to set up by hand. rfwwdyvg covers it on Windows, run under wine.
	- Closed: 20261003-112426

- Code review 20260928 item 21. One Ctrl+click check in the list view was missed by the macOS Cmd change.
	- ID: 2026092813381421
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: macOS.
	- Incorrect behavior: Cmd+click on an unselected row skips the view's own add-to-selection code.
	- Reproduced: no Mac to run it on. Pinned instead by a lint check that fails on the tree before the fix, 20261002.
	- Actual cause: the Cmd change moved the test that opens the click branch to the primary key, but not the Control test inside it.
	- Origin: upstream line, missed by d5fef60, 20260922 (cmdkeys). Missed twin of that fix. Plausible.
	- Actual fix: that test takes the primary key too. The icon view's type-ahead find next and previous (Ctrl+G and Shift+Ctrl+G) and its Ctrl+V guard also take it now, as GTK's own list search and the list view's copy of the Ctrl+V guard already do.
	- Sweep: every remaining Ctrl-as-primary check in the views.
	- Swept: a grep for `GDK_CONTROL_MASK` across `source/` outside vendored code.
		- Changed: the list view's row click, the icon view's find next and previous, and the icon view's Ctrl+V guard.
		- Left on Control, per the cmdkeys item: keyboard moves and Ctrl+space that keep the selection, in both views; Ctrl+F10 for the background menu in both views; the window's block on GTK's emoji keys, which GTK binds to Control itself; the rename label's GtkEntry bindings. The location entry already takes either key.
		- Left alone: the icon view's stretch keys. Nothing shows stretch handles any more.
	- Note: unverified on a Mac. Waits on signoff because two keys beyond the item moved on macOS.
	- Test case: rj9tz3mv, the ClickPrimary lint check. Plain Control in any click or scroll handler fails the lint. Fails before the fix, at the list view line, and passes after.
	- Verified: the Linux build is clean, with no warnings. Lint, the test ID check and the Primary mask test pass.
	- Branch: tidy4
	- Commit: 4078549

- Code review 20260928 item 16. 7z reads `*` and `?` in a left-out linked folder's name as wildcards.
	- ID: 2026092813381416
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 144 of 144 on 20261002.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Design: [20260929-101432_compression.md](design_docs/20260929-101432_compression.md). An edited 7-Zip line gets `-spd` added at run time. Nothing changes until the reset is built.
	- Target OS: Linux, BSD, macOS.
	- Incorrect behavior: a linked folder named `a*`, with store and follow both off, also drops a real folder `abc`. With delete-originals off, the job reports success on an archive that is missing it.
	- Reproduced: yes for 7z, 20260928, Linux.
	- Origin: b8e1401, 20260925 (linktests). New ground. Confirmed.
	- Note: rar has the same class. It drops real files that match, such as `apple.txt` for `a*`, and stores an empty folder for the link. Both tools also read a selected item's name as a pattern, and when extracting the archive's own path, so `s?.7z` brought out `sx.7z` along with it.
	- Actual cause: 7z and rar read `*` and `?` as wildcards in every name on their command line.
	- Actual fix: both built-in 7z lines pass `-spd`, which makes 7-Zip take names as they are. rar has no such switch. Compress to rar refuses a selected item or left-out folder with either character, and says which. Extracting skips rar for such a path and goes on to 7z, which reads rar where it was built with that codec.
	- Swept: 7z left-out folders, selected items, and the archive path when extracting. rar left-out folders, selected items, and the archive path when extracting, folders in it included. rar takes the new archive's own name as it is, checked. A command line edited in the settings keeps what it has, without `-spd`.
	- Branch: arclinks
	- Commit: 334b239
	- Test case: rhr6ggmt, Archive option combinations, new rows with a linked folder `a*` beside `abc`, `a?c` and `apple.txt`, and with `a?c` selected on its own. reww9h2s, Extract job test, extracts `s?.7z` and `r?.rar` beside `sx.7z` and `rx.rar`. rev86z08 and reww9h2r check for `-spd`. All fail before the fix and pass after, on Linux.
	- Verified: same runs as item 4. Debian's 7-Zip has no rar codec, so on Linux the `r?.rar` case ends in an error naming the wildcard, which the test accepts.
	- Verified: the 7z rows of rhr6ggmt and reww9h2s pass on b29w on 20261002, in the native suite. That test's only failures there are item 4's rar rows.
	- Acceptance signoff: Self-closed: reproduced, test fails before and passes after, sweep answered.
	- Closed: 20261002-194800

- Code review 20260928 item 33. Two tests read memory they do not own.
	- ID: 2026092813381433
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The Windows cross build passed on 20261002.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: the content search test writes 3 bytes past a string, and the raw test's patch helper counts on bytes GLib may clear. The raw test fails 21 checks when GLib clears them.
	- Reproduced: yes, 20260928, Linux. Again 20261002.
	- Origin: 4d8f9f7, 20260828, and 4809a54, 20260922 (rawthumbs). New ground. Confirmed.
	- Actual fix: the content search test takes each binary file's length from its literal, so none is counted by hand. The raw test's patch helper writes in place instead of shrinking the buffer and growing it back.
	- Swept: the content search test's other two binary files were counted by hand too. The GIF was one byte short, so it now ends with its trailer. No other test or product code reads back bytes from past a shrink. The three search tests' hit handlers now free the results they are handed.
	- Test case: rffvm2bg, Search content test, now built with the address checker, which stops on a read past a string. rj9v7n76, Camera raw reader test, cleared memory, which runs the raw test again with GLib clearing what a shrunk array gives up. Both fail before the fix and pass after, on Linux. rhg7vh28 still runs it the usual way.
	- Verified: the content search, both raw, search helpers and search engine tests pass on Linux, after a clean build with no warnings. Lint and the test ID check pass.
	- Branch: tidy4
	- Commit: 856f567
	- Acceptance signoff: Self-closed: reproduced, test fails before and passes after, sweep answered.
	- Closed: 20261002-194800

- Code review 20260928 item 31. Two help texts are out of date.
	- ID: 2026092813381431
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: the Windows preflight says the lint stage is cppcheck on changed files, but it now runs every linter. `cicd.bash --help` leaves out fuzzing from what `--quick` skips.
	- Reproduced: yes, the texts were checked against the stages, 20260928.
	- Origin: b21d9cb, 20260920, and 7f65705, 20260916. Not seen by an earlier round. Confirmed.
	- Actual fix: the Windows preflight says lint runs every checker in `lint.bash`. The `--quick` help names fuzzing, and also the private runner, the scroll harness and the demo video, which it had left out too.
	- Swept: the Windows script's header stage list and the comment above its lint call said the same stale thing, and are fixed. The cicd config's lint comment is current. `--gate` help is current.
	- Test case: rj9v18rx, `test-cicd-help.bash`, in the lint stage. Every stage that reports "skipped (--quick)" and every switch `--quick` turns off must be named in `--help`. Fails before the fix and passes after. The Windows preflight line has no test, since it only prints in a Windows run and a test could only match the string.
	- Verified: shellcheck and PSScriptAnalyzer are clean, and the Windows script parses.
	- Acceptance signoff: Self-closed: mechanical.
	- Branch: tidy4
	- Commit: bcf647a
	- Closed: 20261002-143238

- Code review 20260928 item 32. A signed shift in the metadata list mask.
	- ID: 2026092813381432
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: `1 << 31` on an int is undefined in C. It works under gcc today.
	- Reproduced: yes, 20260928, Linux. Again 20261002, in the folder settings test.
	- Origin: upstream macro. One use added in 5d96d9b, 20260722. Not seen by an earlier round. Confirmed.
	- Actual fix: the mask is `1u << 31`.
	- Sweep: the macro's uses, and other `1 << 31` on an int across `source/` outside vendored code.
	- Swept: all five uses of the mask mix it with unsigned ids, so nothing else changed there. No other `1 << 31` in first-party code. The eel canvas color macros shifted an int into the top byte the same way. They shift unsigned now, and have no callers. The directory request bits stop at 11, and the byte readers cast before shifting.
	- Test case: rj9v86cj, the SignShift lint check. A literal 1 shifted by 31, or an int cast shifted by 24, fails the lint. Fails before the fix, on the mask and both eel macros, and passes after. The folder settings test reports both shifts as undefined before the fix and nothing after.
	- Verified: the Linux build is clean, and the folder settings test passes. Lint and the test ID check pass.
	- Acceptance signoff: Self-closed: reproduced, test fails before and passes after, sweep answered.
	- Branch: tidy4
	- Commit: b0ebc2c
	- Closed: 20261002-143238

- Code review 20260928 item 19. An Olympus raw file with a looping directory takes seconds to read.
	- ID: 2026092813381419
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 142 of 142 on 20261001.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Incorrect behavior: a 12 KB file took 8.7 s of a thumbnail thread.
	- Expected behavior: design.md, a thumbnail takes a few milliseconds.
	- Reproduced: yes, 20260928, Linux. Again 20261001, 5.9 s.
	- Actual cause: a directory that names itself as the next one is read 32 times. Each of its entries can start a maker note or preview lookup that reads the file a few bytes at a time, and nothing bounded the total.
	- Origin: 4809a54, 20260922 (rawthumbs). New ground. Confirmed.
	- Actual fix: finding the previews in a file is held to a fixed number of reads, far more than any real camera file tried needed. Reading the preview that was found is not counted.
	- Sweep: every reader in the raw reader that a directory entry can start.
	- Swept: the Olympus maker note, Panasonic's preview tag, the JPEG offset tags, strip lists, values stored outside the entry, EXIF and sub-directories, and the CR3 box walk all go through the one counted read, so the cap covers each. Panasonic's tag was as slow as the maker note in a looping file and is in the test. The properties page reads EXIF through libexif from memory, not a read per entry. Search: every `read_at` call in `nemo-raw.c`, and a grep for `ifd`, `0x927C`, maker note and the EXIF loader across `source/`.
	- Note: reading a directory in one block, and skipping a maker note already read, were also offered. Neither is needed with the cap, and the skip would not stop notes at different offsets.
	- Note: waits on signoff because the fix took one of the three the review offered. A real file that needed more reads than the cap would show the type icon. The fuzz stage still owes a run with the new seed.
	- Test case: `rhg7vh28 Camera raw reader test`: three files with a looping directory, read from disk, each under 0.25 s. They are the review's file, the same with camera settings, and Panasonic's preview tag, which took 8 s, 12 s and 0.8 s before. A tall uncompressed preview is still read after the directories. Fuzz seed `olympus-loop`.
	- Branch: rawloop
	- Commit: 1a95d6a, seed in 03e4206

- Code review 20260928 item 20. The file cache is never closed at quit.
	- ID: 2026092813381420
	- Type: Bug
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 143 of 143 on 20261001.
	- Priority|Severity: Low
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Related IDs: 2026100113372562, 2026100113372592
	- Incorrect behavior: up to 30 s of draw counts are lost at every quit, so the age rule counts recent use short. The log is never trimmed at close.
	- Reproduced: yes, 20260928, Linux. Nothing in the program calls the close. Again 20261001: after each quit the journal held 0.1 to 3 MB, and the draws of the last few seconds were not counted.
	- Actual cause: nothing writes the store out at quit. The quit hook the review named is never called by GLib, so nothing put there would run either.
	- Origin: 6d52b4b, 20260921 (thumbdb). New ground. Confirmed.
	- Actual fix: at the very end of every run, after the thumbnail threads are done, the draw counts held in memory are written and the journal is folded back into the file. The store is left open until the process ends, since a worker the quit does not wait for may still be using it. A run that never used the store does not make one. The cleanup pass is stopped first, as the dead hook meant to do.
	- Note: closing the window, the last window, Close all windows and `--quit` all end in that same code, on Windows too. No signal is handled, so a copy killed by one still loses the counts, as it loses a settings change.
	- Note: the other writes held back for a moment, settings and folder metadata, are already written in the same place. The keyboard shortcut map is saved only from the dead hook, which is its own item, 2026100113372562.
	- Note: waits on signoff because it changes what the program writes at quit, and the fix is neither of the two the review offered.
	- Test case: `rj750n43 File cache written at quit test`. The built program is closed by its window while thumbnails are being made, after they are made, and while drawing them from the store, then taken down by `--quit` from another copy. After each the journal is empty, and the last two raised every draw count. A `--version` run makes no store.
	- Branch: cacheclose
	- Commit: 639757b, test in 66bd445 and 8b78915

- Every zip grows the program's memory by about 150 bytes, whether it finishes or is stopped.
	- ID: 2026100307122400
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261003-071224
	- Opened by: item 2026092813381423
	- Related IDs: 2026092813381423
	- Incorrect behavior: memory in use grows by about 150 bytes for each zip written, and is never given back. It is still reachable from somewhere, so it is something kept rather than lost.
	- Expected behavior: a zip that has finished leaves memory where it was.
	- Reproduced: yes, 20261003, Linux. Growth was the same over 64 and 192 zips per zip, and the same for a zip that finished and one stopped partway.
	- Origin: unknown. Not seen by an earlier round. Confirmed.
	- Cause: every compress job keeps two holds on its stop handle and lets go of one, so it is every archive format, not only zip. Unpacking takes the same two holds. It falls under item 2026092813381423's sweep, so it is fixed there and this item closes with it.
	- Actual fix: compress and unpack jobs take one hold on the stop handle. Fixed under item 2026092813381423, whose sweep it falls under.
	- Branch: leaks
	- Commit: 4802a37
	- Test case: rjbmh7g1 Stopped zip leak test, now held to the same limit as the other leak tests, and rjbpmcxy Extract job leak test. Both fail with the extra hold put back.
	- Acceptance signoff: Self-closed: reproduced, the tests fail before the fix and pass after, and the sweep has its answer on item 2026092813381423.
	- Closed: 20261003-081627

- The thumbnail order test can fail with its two same-time pictures a second apart.
	- ID: 2026100317174376
	- Type: Bug
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261003-171743
	- Opened by: test run on b29w
	- Target OS: all, seen on Windows.
	- Incorrect behavior: rhf905br failed on b29w, on its check that the two twin pictures have the same time.
	- Reproduced: yes, 20261003, b29w natively. On Linux with a one-second pause put between the two pictures.
	- Actual cause: the test read the clock once for each picture, so a second could tick over between them. Slower saves on Windows make that more likely.
	- Actual fix: the time is read once for both pictures.
	- Branch: twintime
	- Test case: rhf905br itself. With the pause, it fails before the fix and passes after on Linux.
	- Verified: the full Linux suite passed 160 of 160 with the fix.
	- Acceptance signoff: Self-closed: test-only fix, red and green both ways.
	- Closed: 20261003-172400

- A stopped 7z made without the 7-Zip program takes as long to end as the rest of the file would have taken.
	- ID: 2026100308563234
	- Type: Enhancement
	- Status: Done
	- Priority|Severity: Low
	- Opened: 20261003-085632
	- Opened by: review of item 2026092813381423
	- Related IDs: 2026092813381423
	- Requirements:
		- A stop on a 7z written by the built-in writer ends about as fast as a stop on a zip or tar.gz.
	- Note: the built-in writer is used where the 7-Zip program is missing, or where link storing is off. On a stop it fills the rest of the open entry with zeros through its compressor, into a temporary file of its own, so how the job handles the output does not reach it. A 2 GB file stopped near the start took 24 s, the same before and after item 2026092813381423. The progress bar stands still meanwhile, and the next queued job waits.
	- Actual fix: after a stop, the 7z writer is marked failed and then closed. The close skips the open entry, so there are no zeros to write, and still frees what it holds. The temporary file and compressor go when the writer is freed. A stop now ends in about 0.01 s, as fast as on a zip.
	- Branch: stop7z
	- Commit: ef05a4f
	- Test case: rjbpyy28 Archive stop time test now has a 7z case. A 4 GiB and a 16 GiB file are stopped at their first progress report. Before the fix, the 4 GiB one took 50 s and the 16 GiB one did not end in 100 s. After, both take 0.01 s. rjbw0rkq Stopped 7z leak test, which fails at 82 bytes a round when the close is left out.
	- Acceptance signoff: waiting. The archive writer's handling of a stop changed again, this time for the 7z.

- Code review 20260928 item 36. Drawing can wait up to 3 s on the file cache.
	- ID: 2026092813381436
	- Type: Enhancement
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 168 of 168 on 20261004, on drawlock.
	- Needs external testing: none. Nothing here is per platform, and rhd1cv38 passes under wine. Its new cases run in the next native Windows suite.
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- The in-memory draw counter shares a lock with database calls that can wait on another window. Give it its own, and flush off the main thread.
		- The window never waits on the cache, however briefly. Asked 20260930. Besides draw counts, the timed flush and the forget behind a thumbnail refresh write on the window's thread today.
		- First measure whether a window really freezes while another prunes. Found by reading only.
	- Related IDs: 2026092813381411, 2026093010493420
	- Note: measured 20261004, Linux, before the change. A window drew 2000 pictures, one every 2 ms, with four thumbnail threads looking up and storing, while another process pruned a cache of 40 thousand thumbnails of 16 KB down to a tenth. Its own thread waited on the cache up to 2.3 s at a time, 55 s out of 60, and 143 waits were over 0.1 s. With 100 pictures drawn instead, up to 0.5 s at a time, 6.7 s out of 60. With no prune at all, up to 0.2 s.
	- Note: measured 20261004, Linux, after the change, the same three runs. The longest call on the window's thread took 0.3 ms, under 50 ms in all over 60 s, and its main loop was never more than 20 ms late. The thumbnail threads still wait on the prune, up to 1.7 s, which the window no longer sees.
	- Actual cause: a draw count took the store's lock, and a thumbnail thread keeps that lock while it waits on another copy, for up to 3 s. The write once 256 files were waiting, and the one on the 30 s timer, ran on the window's thread. A refresh wrote there too, once per file in the folder on Reload.
	- Decisions:
		- Calls made without asking. A refresh is queued like a draw count and written at once by the writer. A refresh still queued is done first by the next read or write of a thumbnail, so the old picture is never read back after it was asked for. A write that fails is tried again after 30 s, not on every draw. The flush before a prune moved onto the prune's thread.
	- Done: draw counts and refreshes are queued under a lock of their own, never held while the file is in use, and a thread of their own writes them. The counts go after 30 s or once 256 files are waiting, as before. A quit stops that thread and then writes what is left, at the end of the program as before.
	- Swept: every call into the cache from the window's thread: the draw count, the refresh from Reload or a thumbnail refresh, the timed flush, and the flush before a prune. Lookups and stores already ran on thumbnail threads, and the settings page reads and empties on a thread of its own. The open happens on whichever thread first asks; a draw count only follows a thumbnail read from the store, so the store is already open by then.
	- Branch: drawlock
	- Commit: d22c7e3
	- Test case: rhd1cv38, File cache store test, three new cases. Another connection holds the file for 1.5 s while a thumbnail thread waits in a store; 300 draw counts and a refresh on the test's thread must each take under 0.3 s, and all of it is written once the file is let go. Draw counts are written on their own with no main loop running. A thumbnail refreshed right after it was stored is never read back, over 200 tries.
	- Verified: 20261004, Linux: rhd1cv38 fails before the change, 4 checks, with the test's thread waiting 3 s on the cache, and passes after. 48 runs of it, 16 at once, passed. rj750n43, which checks draw counts reach the file at quit, and rhd69rjr passed 6 more runs each. The full Linux suite passes 168 of 168, and lint and the Windows cross build are clean. rhd1cv38 passes under wine.
	- Acceptance signoff: Self-closed: its test fails before and passes after, and a wait on the cache is not something to judge on screen. The calls made without asking are in Decisions.
	- Closed: 20261004-184242

- Code review 20260928 item 37. Path and file rows in the cache never age out.
	- ID: 2026092813381437
	- Type: Enhancement
	- Status: Done
	- Needs local test suite run?: no. The full Linux suite passed 168 of 168 on 20261004, on cacheage.
	- Needs external testing: none. The rule is plain SQL with nothing per platform, and its test passes under wine.
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Rows for files that still exist stay after their thumbnails are pruned, and count against the size limit. Add an age rule for rows with no thumbnail.
	- Decisions:
		- A call made without asking: a name with no thumbnail goes once nothing has been stored under it for the thumbnail age limit, or 180 days, that setting's default, when the limit is off. No new setting. Without a fallback, a cache held down by the size limit alone would still fill with these rows.
		- Its age is when a thumbnail was last stored or linked under the name. A draw of a name that still has its thumbnail does not refresh that time, so a name whose thumbnail the size rule took can go at the next pass. It costs one checksum when the file is next thumbnailed, and that read happens anyway.
		- The tables stay at version 4, so the cache does not start over.
	- Done: each prune pass forgets names whose file has no thumbnail left and that are older than the age above, then the file records nothing points at go with the orphans. All such a row keeps is a size, a time and maybe a checksum, and the file gives those again. A name that still has a thumbnail is left to the thumbnail rules.
	- Swept: rows with no thumbnail come only from the age and size rules and a thumbnail refresh. Names and file records are the only rows besides thumbnails and the prune's own row, and orphaned file records already went. Draw counts live on thumbnails. Everything the rule drops can be worked out again from the disk.
	- Branch: cacheage
	- Commit: f37a678
	- Test case: rhd69rjr, File cache prune test, the bare names case. Old names with no thumbnail go, with their file record and checksum, under the age limit and with it off. A recent one, one that still has a thumbnail, and a copy's other name stay. 300 old names go in more than one write. Read back with calls that never put a row back.
	- Verified: 20261004, Linux: rhd69rjr fails before the fix, 8 checks, and passes after. The full Linux suite passes 168 of 168, and lint and the Windows cross build are clean. rhd69rjr passes under wine.
	- Acceptance signoff: Self-closed: its test fails before and passes after, and nothing is left to judge on screen. The age is a call made without asking, in Decisions.
	- Closed: 20261004-175908

- Code review 20260928 item 41. Fuzz the shortcut editing code.
	- ID: 2026092813381441
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- The fuzz target covers only the shortcut reader. Setting paths in a shortcut parses the same untrusted bytes with its own code.
	- Done: a new fuzz target puts three sets of paths in each input: same drive with all three paths, a share path alone, and a relative path alone. An edit that works must read back with the new paths and the old name, Start in, arguments and icon. An edit that is refused must leave the file as it was. The item ID list going is a known gap and is not checked.
	- Found: a UTF-16 string in the shortcut that would not convert, such as one with half a surrogate pair, was dropped while its flag stayed. Every string and block after it then read out of place, so the edited shortcut lost its portable path. Kept UTF-16 strings now go back byte for byte.
	- Note: the fuzz stage puts its temp files on tmpfs. The new target writes a file per input, and on disk it ran far too slowly to find anything.
	- Swept: setting paths is the only code that writes back strings read from a shortcut, on every platform. The reader keeps a string that will not convert as none and reads on. A new shortcut is written from strings it builds itself.
	- Branch: lnkfuzz
	- Commit: 5b04f83 (target), 5134a9d (fix)
	- Test case: rjfa5fnh (fuzz stage) and rjfa5fmh (seed replay in the suite). The `lone-surrogate` seed pins the fix.
	- Verified: rjfa5fmh fails on the old writer and passes now. The new target ran five minutes clean. Linux suite 168 of 168, the fuzz exit check, lint and the Windows cross build are clean.
	- Acceptance signoff: Self-closed: the fuzz target is the test, and its seed pins the writer fix.
	- Closed: 20261004-174202

- Code review 20260928 item 43. On Windows, any process may take the foreground during a tab move.
	- ID: 2026092813381443
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Target OS: Windows.
	- Requirements:
		- Allow only the receiving window's process.
	- Origin: the tab move between windows, b096102.
	- Progress log:
		- A tab moved to another copy's window now hands the right to come to the front only to the process that owns that window, found from the window's handle. A handle that is gone, or none, hands it to nobody.
		- The old right open to every process outlived the copy that gave it. On an idle box any program could take the foreground long after the move.
	- Swept: the one place a tab move hands over the right, used by both the menu and a drop. A tab moved to a new window starts that copy itself, so it needs no right handed over. No other code hands the foreground to another process.
	- Branch: fgtab
	- Commit: 0a70a44
	- Test case: rjf8db1q, Tab move foreground right test, Windows only. Two copies of the test each show a window. The right handed to one copy's window lets that copy come to the front and not the other. It needs a desktop, so it skips over ssh. Fails before the change and passes after.
	- Verified: 20261004, vm925w, in the signed-in session: rjf8db1q fails before the change, both copies taking the foreground, and passes after, five runs. Over ssh it skips. Windows cross build clean, lint passes. On Linux the tab move and window under the pointer tests pass.
	- Acceptance signoff: Self-closed: the change does what the item asked, and its test fails before and passes after.
	- Closed: 20261004-171718

- Code review 20260928 item 40. The shortcut path choice code is only reached by tests.
	- ID: 2026092813381440
	- Type: Enhancement
	- Status: Done
	- Needs external testing: none left. Ran on vm925w on 20261004.
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Make link always writes all three paths since linkdlg, so dropping the relative path and the portable-only case are unused. Remove them, or keep them on purpose for a later "Defaults..." button and say so.
	- Decisions:
		- 20260928: remove them. Git history has them if a Defaults button is ever built.
	- Fixed: every shortcut now has the absolute and relative paths. Taking the relative path out, shortcuts with no absolute path, their "would hold no path" error and the paths field in the Make link answers are gone.
	- Note: the one choice left is whether the portable path goes in. Make link always adds it. A link made on Windows with no dialog leaves it out, as before.
	- Swept: every caller of the shortcut writers, the path flags and the answers field, in the source, tests, fuzz targets, cicd scripts and docs. The moving and copying design doc already says every shortcut gets all three.
	- Branch: guardlnk
	- Commit: 745ac2a
	- Test case: narrowed to what still exists in rhn5ewrg, rhnb1z7g (now checks the portable path), rhr6ggms, rj05egmb, rhmxm5ah, rfwwdyvg and rdcvb368. Commented out with the reason: rhn92e10 (an absolute-only shortcut from Make link), and the cases in the others for shortcuts without the absolute or relative path, taking the relative path out, and the paths field.
	- Verified: Linux suite 167 of 167. Windows cross build and lint clean.
	- Verified: 20261004, Windows, at 0ad01d1: rdcvb368 and the 21 link and shortcut tests built there pass natively on vm925w.
	- Acceptance signoff: Self-closed: the removal follows the Decisions row, nothing a person sees changed, and its tests pass on Linux and natively on Windows. The path flags went with the cases they chose between, leaving only the portable path optional.
	- Closed: 20261004-152800

- Code review 20260928 item 45. Self-tests in the lint stage have no test IDs.
	- ID: 2026092813381445
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Four self-tests run in the lint stage with no ID, and the ID check cannot see them. Give them IDs, or record that they are exempt.
	- Fixed: the four self-tests have IDs, kept in each tool and printed when it runs. The ID check now finds every self-test the lint stage runs, so a new one with no ID fails it. The ID check itself is the gate, not a test, and has no ID.
	- Swept: every `--self-test` and `--check` call in `lint.bash` and `lint-c.bash`. There are no others in the lint stage.
	- Branch: relnotes
	- Commit: 50354c0
	- Test case: `test-id.py --check`, in the lint stage. It failed on the four before the fix and passes after.
	- Verified: lint stage clean, 275 test IDs.
	- Acceptance signoff: Self-closed: mechanical, and the check failed before the fix and passes after.
	- Closed: 20261004-152310

- Code review 20260928 item 38. The test guard dialog resumes a job the user paused.
	- ID: 2026092813381438
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- Resume only if the guard's own pause did the pausing.
	- Origin: `7025cca`, 20260924. Confirmed.
	- Fixed: a pause now says whether it was the one that paused, and the guard resumes only then.
	- Sweep: every other place a job pauses for a question of its own.
	- Swept: the error, conflict and link questions in file operations, and the conflict and password questions in extract, do the same now. The only other pause is the progress window's Pause button.
	- Branch: guardlnk
	- Commit: b64993e
	- Test case: rhnpn188, now also with a job already paused when the guard asks, and a check of what a pause says. Fails before the fix, passes after.
	- Verified: Linux suite 167 of 167. Windows cross build and lint clean.
	- Acceptance signoff: Self-closed: test fails before and passes after, sweep answered.
	- Closed: 20261004-150509

- Code review 20260928 item 42. Dragging the only tab off a window restarts the same window in a new process.
	- ID: 2026092813381442
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Related IDs: 2026100413510329
	- Requirements:
		- The tab menu grays out "New window" for the only tab. A drag to empty screen should do nothing there too.
	- Origin: the tab move between windows, b096102.
	- Progress log:
		- The only tab of a window with one pane now stays put when dropped outside every window, by the same rule that grays out "New window" in its menu. That holds with every window in one process too. When a move onto another window fails, the only tab stays where it is.
		- Moving the only tab onto another window can crash the window it left. Filed as 2026100413510329.
	- Swept: the three ways a tab goes to a window of its own: the menu's "New window", a drop on empty screen with a window per process, and the same drop with every window in one process. All three use one check, `nemo_tab_move_is_only_tab`.
	- Branch: tabmove
	- Commit: cdbdef0
	- Test case: rhmr6qgs, Tab move between processes test. The only tab dropped on empty screen starts no copy and stays, with a window per process and with one process. One of two tabs still goes to a new copy. Fails before the change and passes after.
	- Verified: 20261004, Linux: rhmr6qgs fails before the change, at both only-tab checks, and passes after, three runs in a row. The only tab dragged to empty screen leaves the window as it was. A second tab dragged out opens in a new copy. The argv, instances, keyboard menu, held view and schema drift tests pass. Lint passes.
	- Acceptance signoff: Self-closed: the change does what the item asked, and its test fails before and passes after.
	- Closed: 20261004-135103

- Code review 20260928 item 44. The tab move test checks only the window title.
	- ID: 2026092813381444
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- A move that dropped the view or the selection would still pass. Check both.
	- Origin: the tab move between windows, b096102.
	- Progress log:
		- The test now checks the view and the selection the receiving window shows, three ways: a tab another window took, a window of its own started for a tab, and a tab dragged off into a new copy. These checks run on Linux only.
		- The icon view's compact setting can now be read as well as set.
	- Branch: tabmove
	- Commit: cdbdef0
	- Test case: rhmr6qgs, Tab move between processes test.
	- Verified: 20261004, Linux: rhmr6qgs passes. With the view left out where a window takes a tab, all three new checks fail. The same with the selection left out. It passes again after a clean build.
	- Acceptance signoff: Self-closed: the change does what the item asked, and the new checks fail when the view or the selection is left out.
	- Closed: 20261004-135103

- Code review 20260928 item 39. Four delete confirm functions are marked unused but are called.
	- ID: 2026092813381439
	- Type: Enhancement
	- Status: Done
	- Opened: 20260928-133814
	- Opened by: code review 20260928
	- Parent ID: 2026092813381400
	- Requirements:
		- The mark hides a real warning if one of them stops being called. Drop it on all four.
	- Done: the mark is gone from the move to trash, delete from trash, empty trash and delete directly confirms. All four are still called, and the build has no warning.
	- Swept: the one other function in the tree marked unused, the date type name in `nemo-file.c`, is only called from a debug message that can compile away, so it keeps the mark.
	- Branch: editlbl
	- Commit: 1a45acc
	- Test case: none, the compiler checks it once the mark is gone.
	- Verified: 20261004, Linux: clean rebuild with no warnings, full Linux suite 167 of 167, and the Windows cross build passes.
	- Acceptance signoff: Self-closed: mechanical.
	- Closed: 20261004-132748

## Old format

### Bugs

### Features and enhancements

- 🔘 Take SHCL 3.0.0-beta.1 from its published release, once there is one, and run the config tests against it.
	- Opened: 20260925-122815
	- Note: the vendored header is from SHCL's `dev` branch, ahead of that tag.
	- Done 20260925: moved to a newer `dev` copy, with the calls asked for here. Neither replaces the app's own code yet. Clearing a comment would also take a note written above a key, and the info block call misses a block that is no longer at the end of the file.
	- Note: a later SHCL may back up and convert a file in an older format itself. Look for a call that does or helps with that before wiring in a new version. If it does the job, use it in place of ours in `nemo-config.c`. Never run both on the same file.
	- Test case: none yet, not started.

- **Stop here for a next release**.

- 🔘 Cut 1.0.0-rc.1.
	- Opened: 20260925-122815
	- Design: [20260930-150859_delete_guard.md](design_docs/20260930-150859_delete_guard.md), for the test guard.
	- The delete test guard stays in as a preference setting. (But off in code.) The changelog and the release notes point it out, and say where to turn it off.
	- Note: at rc.2 the setting's default goes to off. The test guard stays in the code.
	- Note: the changelog's vNEXT section is missing most of the work since beta2, such as Compress and Extract, the crash reporter, the delete protections and tab move.
	- Note: `main` still has the installers from 20260804. Their stable channel asks for the latest stable release, which does not exist yet, so the README one-liners fail until this cut.
	- Test case: none, release step.

- 🔘 A fractional display scale is only applied to text, so widgets, icons and spacing stay at the whole step below it.
	- Opened: 20260821-150232
	- Cause: the toolkit scales in whole numbers. At 150% the type is right and everything around it is a third too small.
	- Probable fix: our own stylesheet, with padding, icon sizes and the like driven from the leftover fraction. Only do it once someone has looked at it on a scaled display.
	- Test case: none yet, not started.

- 🔬 Installers: architecture always detected, a version option, and a stable install that still works before any stable release exists.
	- Opened: 20260919-131209
	- Done: `--arch` and `-Arch` are gone. `--version`, `-Version` and `-Help` are new, and the bash one takes `--opt=value` too.
	- Done: releases are ranked by version rather than by the order the API lists them. Stable takes the newest prerelease while no stable release exists, and the plan says so.
	- Verified: both installers, bash and PowerShell 7, show the right plan against the live releases on Linux. The ranking was checked against a list with a two-digit minor and beta10 beside beta2.
	- Done 20260925: `install.ps1` no longer closes the shell that ran the one-liner, on an error or on `-Help`, and always removes its temp folder. Unix installs swap in place the way `install.bash` does. A failed request to GitHub is named as that, not as "no release", and access denied or a file in use each get their own advice. Windows PowerShell 5.1 gets TLS 1.2 and reads the architecture it can. Both installers rank tags the same way.
	- Done 20260925: `install.bash` checks it can write where a user install goes before asking, and a failed download says so in a sentence.
	- Verified: both installers install, reinstall and uninstall the Linux tarball into a scratch home, in the pipeline's new installer check.
	- Left: run both on Windows, in PowerShell 5.1 and 7.
	- Test case: `cicd/linux/test-installers.bash`, `cicd/linux/test-install-download.bash`, `cicd/utility/test-install-path.ps1`, `cicd/win/test-install-holders.ps1`; PowerShell 5.1 is not covered.

- 🔘 A Windows installer exe that installs, or updates an install already there.
	- Opened: 20260919-132409
	- Design: [20260930-145641_windows_exe_packing.md](design_docs/20260930-145641_windows_exe_packing.md).
	- Note: Windows has the portable exe and the zip today, and `install.ps1` for an install with a menu entry and PATH.
	- Note: wants signing first, or it trips the same warnings the exe does.
	- Test case: none yet, not started.

- 🛠️ Real-Windows validation: the paths still not exercised there.
	- Opened: 20260826-103001
	- Done: the test suite now runs and passes on a Windows box, through the pipeline and the gate. The two paths below are still open.
	- Note: the signing path only runs in the hosted release workflow on a tag. The repo has no secrets and no variables set at all, so the signing step is skipped and a release cut today publishes an unsigned exe. That is the documented fallback, but it should be known before a build is announced.
	- Note: the UAC consent prompt itself has not been seen; this box elevates without prompting and the session is already elevated. What is proven is that the relaunch starts an elevated copy at the right folder, not the consent dialog.
	- Note: moving a junction to another drive is untested. The link move test covers it, but needs a second fixed drive: vm925w has one, b29w does not.
	- Note: four Windows-only tests changed in the 20260919 review round. They cross-compile, but nothing has run them on a real box since.
	- Note: the dogfood launcher's copy, held-version and cleanup paths were reworked on 20260920 and have only been reasoned about and probed on Linux.
	- Note: the Make link dialog, junctions and hardlinks made from it, and the junction job test have only been cross-built.
	- Note: a link drop opening Make link, Ctrl+Shift+T, and folder shortcuts sorting with folders have only been cross-built.
	- Note: ImageMagick thumbnails have not run on Windows. Things to see there: no console window flashes up, and the packed exe's file hooks, which every program it starts inherits, do not upset `magick.exe`.
	- Test case: the Windows gate runs the suite natively, plus `cicd/win/gui-launch-smoke.ps1`; signing and the UAC prompt have none.

- 🔘 Linux arm64 release build. Needs an arm64 GTK3 build environment; nothing cross-compiles it today, so the installers' arm64 path has nothing to fetch.
	- Opened: 20260804-133646
	- Note: if arm64 builds turn out much slower, they go behind an `--include-arm` flag rather than the `--no-arm` the engine has now.
	- Test case: none yet, not started.

- 🔘 Target: BSD
	- Opened: 20260730-185314
	- Test case: none yet, not started.

- 🔘 Target: macOS
	- Opened: 20260730-185314
	- Test case: none yet, not started.

- 🛠️ Windows: Need to figure out a way to do GUI testing and demo recording, without interrupting the live console session.
	- Opened: 20260829-071437
	- Note: GUI testing in a throwaway sandbox is done, and is under Done.
	- Left: demo recording, and anything spanning a reboot (that still wants the Hyper-V guest).
	- Test case: `cicd/win/gui-launch-smoke.ps1` for GUI launch; none for demo recording.

- 🛠️ Enable the disabled pipeline stages as the build matures.
	- Opened: 20260725-153058
	- Done: the Windows cross build runs on every full run now, so the zip is never packed from an older exe. `--quick` skips it.
	- Done: the demo recorder runs with `--demo`. The stage that refreshes the README images is still off.
	- Test case: none, pipeline setup.

- 🔘 Move the two side stores to SHCL: `metadata.json` -> `metadata.shcl` and `bookmark-metadata` -> `bookmark-metadata.shcl`. Separate files; neither is folded into `settings.shcl`.
	- Opened: 20260905-112900
	- UPDATE 20260908-111214: Don't do this if it breaks compatibility with plugins or addons.
	- First, on its own: bump the vendored `shcl.h` to the release carrying the coming fix, and run the config tests against it.
		- Done 20260925: the vendored `shcl.h` is SHCL 3, taken ahead of its beta tag. Config tests pass.
	- Probable fix: each URI becomes a quoted section, each metadata key a string or string-array field under it. The store keeps its mutex, its debounced save and its re-keying on rename; only the file format changes.
	- Note: no migration of the old files, the same call as for settings pre-1.0.
	- Probable fix: then the action layout: `actions-tree.json` -> `actions-tree.shcl`. Each node becomes a section named by its uuid, children nested under a submenu, order by file position; the unused `position` field goes. The C side only reads (`nemo-action-manager.c`); the writer is the Python layout editor, which takes shcl's single-file Python binding the way the C side took the header. Its drag-and-drop payload is in-memory and uses the standard library, so it can stay as it is or move to the same format. Fix the pre-fork `~/.config/nemo/` path in the editor and its notes on the way.
	- Note: with both done, json-glib leaves the build.
	- Test case: none yet, not started.

- 🔘 Windows code signing, and reducing AV false positives.
	- Opened: 20260804-095855
	- Design: [20260930-145641_windows_exe_packing.md](design_docs/20260930-145641_windows_exe_packing.md).
	- Note: a paid signing service, around $10 a month for 5,000 signatures, is the option on the table now.
	- Note: SignPath Foundation (free for open source) was applied for and refused, so releases ship an unsigned exe with the `.zip` as the fallback. The release-only workflow at `.github/workflows/release-win.yml` still builds, packs and publishes; its submission step is left dormant behind the token gate. That workflow existed because SignPath would only sign CI-built artifacts, so with it gone nothing forces a release into hosted CI and a local cut is viable again.
	- Note: options weighed (Azure Artifact Signing, Certum open source, commercial cloud, reapplying) are in `cicd/win/signing.md`.
	- Note: also sign the release `.zip` contents and, once it exists, the installer. Blocked on there being any signing identity at all.
	- Note: submit any remaining AV false positives (VirusTotal to find the flagging engines, then vendor FP forms); keep the zip as the FP-free fallback.
	- Test case: none yet, not started.

- 🔘 Take out the rest of the Nemo desktop code.
	- Opened: 20260917-191500
	- Note: the desktop itself went long ago, but the icon view still carries a desktop mode, desktop orphans and desktop sort order, and `--no-desktop` is still accepted and ignored. None of it runs and none of it deletes anything.
	- Note: removing it touches about twenty files, mostly the icon view, so it wants its own pass and a look on screen after.
	- Test case: none yet, not started.

### Done

#### Done - Bugs

- ✅ On Windows, 7z with a password and no link options left files out of the archive.
	- Opened: 20260926-190245
	- Closed: 20260926-195754
	- Reproduced: the archive combinations test on vm925w. The archive read back as `a.txt` and `big.bin` only.
	- Cause: the archive was whole. The test reads it back with libarchive, which cannot decrypt a 7z. From 3.8, which Windows has, the header after an entry it could not read answers "failed", and the test stopped there. Linux has 3.7.
	- Fixed: the test reads on past a failed entry and stops only at the end or a fatal error.
	- Swept: the product's own listing stops on the first failed header too. It reads headers only, with the password, and a short listing keeps the originals, so it fails safe. Left as is.
	- Verified: the combinations test passes on vm925w, and failed there before.
	- Test case: `test-nemo-archive-combos`.

- ✅ The Preferences dialog and the action layout editor looked up their text in upstream Nemo's translations.
	- Opened: 20260926-173000
	- Closed: 20260926-195754
	- Cause: both .glade files said `domain="nemo"`, which beats the program's own domain.
	- Fixed: the attribute is gone. The layout editor names the domain on its builder, since Python's gettext domain is not the one GTK reads.
	- Verified: the lint check fails with the attribute put back into a .glade file and passes without it.
	- Test case: `fCheckUiDomain` in the C lint, over every .glade and .ui file.

- ✅ On Windows, moving a folder junction or a folder symlink ignored the link copy answer in three cases.
	- Opened: 20260926-091948
	- Closed: 20260926-195754
	- Cause: a move on one drive is a rename and never asks about links. The test gave an answer that changed each link's kind and expected the change.
	- Decided: a move on one drive keeps every link as it is and asks nothing, as other file managers do. Only a move to another drive asks. design.md says so now.
	- Fixed: the test expects every link to arrive as it was on a move, whatever the answer.
	- Verified: the link copy job test passes on vm925w and on Linux.
	- Test case: `test-nemo-link-copy-job` (the four move cases).

- ✅ On Windows, the archive option combinations test failed.
	- Opened: 20260926-092051
	- Closed: 20260926-195754
	- Reproduced: with links kept, 7z on Windows stored the file link as a plain file and followed the folder link.
	- Cause: 7z on Windows was never told to keep links, on purpose, but still said it could. 7-Zip there keeps a link as raw Windows data, which libarchive reads back as a file of junk and an empty folder. libarchive is what extracts here.
	- Fixed: on Windows 7z no longer offers to keep links, so the box is grayed for it and a linked folder not followed is left out, as with any writer that cannot keep one.
	- Verified: the combinations test and the archive unit test pass on vm925w. The full native suite there is 132 passed, 9 skipped, none failed.
	- Test case: `test-nemo-archive-combos`, and `check_backends` plus the 7z switch checks in `test-nemo-archive`.

- ✅ A shortcut made off Windows that points at itself was opened again and again on Windows.
	- Opened: 20260926-190245. Closed: 20260926-190245.
	- Cause: the chain of shortcuts ended on the shortcut itself, and that was then opened as if it were the target.
	- Fixed: a shortcut is never opened as the end of its own chain.
	- Test case: `test-nemo-shortcut-win32` (the open decision), Windows only.

- ✅ `install.ps1` did not give back the PATH it found on uninstall when it held empty entries.
	- Opened: 20260926-175808. Closed: 20260926-175808.
	- Cause: an install folded a doubled trailing `;` into one, and an uninstall dropped every empty entry.
	- Fixed: both keep the value as found, apart from the install folder.
	- Test case: `cicd/utility/test-install-path.ps1`.

- ✅ Extract to a folder failed on a split 7z with "The archive could not be read."
	- Opened: 20260926-190000. Closed: 20260926-190000.
	- Cause: making the folder counted as having written something, so the 7z program was never tried after the built-in reader gave up.
	- Fixed: only files from the archive count.
	- Test case: `test-extract-job` (`check_split_volumes`), skips without 7z.

- ✅ A cancelled split 7z left its volumes behind.
	- Opened: 20260926-190000. Closed: 20260926-190000.
	- Cause: 7z names the volumes it has not finished `<volume>.tmp`, and the cleanup stopped at the first volume it could not find.
	- Fixed: the cleanup also removes the unfinished names.
	- Test case: `test-archive-job` (`check_cancel`).

- ✅ A cancelled zip logged a GLib warning about an error set twice.
	- Opened: 20260926-190000. Closed: 20260926-190000.
	- Fixed: only the first write error is kept.
	- Test case: `test-archive-job` (`check_cancel`), which fails on any GLib warning.

- ✅ A setting changed from a background job could run its change handlers on that job's thread, where they touch widgets.
	- Opened: 20260926-150000. Closed: 20260926-160000.
	- Cause: the hand-off to the main thread ran the handler on the spot whenever the main thread was outside its loop at that moment.
	- Fixed: the change is always queued for the main thread.
	- Test case: `test-nemo-config` (`test_changed_on_main_thread`).

- ✅ Relative symlink bug:
	- Opened: n/a
	- Problem: When creating relative symlinks, the entire path is walked back up to root (via '../../' etc.), then back down.
	- Expected: It should remove all common paths from both, only then substitute '../' for any non-common ancestor folder. (Or './<child>' if the link only points down.)
	- Closed: 20260925-152642
	- Cause: both ends were resolved to their real paths first. Where a symlinked folder sat on either path, the real paths shared little or nothing, so the link climbed to the root.
	- Fixed: both the paths as seen and the real paths are tried, and the shortest that still reaches the target wins. When only the climb works, as when the link sits inside a symlinked folder that leads elsewhere, it is kept.
	- Test case: `test-nemo-link-copy` (`check_relative_spelling`); the symlinked-folder cases are POSIX only.

- ✅ `install.ps1` never finished an install on Linux. It stopped at the step that clears the download's web mark, which only Windows has.
	- Opened: 20260925-131500
	- Closed: 20260925-133000
	- Cause: pwsh on Linux has the cmdlet, but it throws there whatever it is told to do with errors. The earlier check only read the plan.
	- Fixed: the step runs on Windows only.
	- Verified: the new installer check fails on the old script and passes on the fixed one.
	- Test case: `cicd/linux/test-installers.bash`, skips without pwsh.

- ✅ The Windows exe on a release had no version in its name and no line in the checksums file.
	- Opened: 20260925-122815
	- Closed: 20260925-133000
	- Fixed: from the next release it is `nemo-anywhere-<version>-windows-x86_64-portable.exe`, and the release workflow adds its line to the sums file.
	- Note: not run yet. It runs on the next release tag.
	- Test case: none, release workflow only; it runs on a release tag.

- ✅ "Preparing" dialog appears, when viewing trash/delete/move debug dialog.
	- Opened: 20260924-140642. Closed: 20260924-184032.
	- Cause: a job's progress window comes up two seconds after the job starts, unless the job is paused. The job's own questions pause it, but the test guard asked without doing so.
	- Fixed: the guard pauses the progress of the job that asked, while it waits for an answer.
	- Swept: every other question asked from inside a job (conflicts, passwords, links, errors) already pauses.
	- Test case: `test-nemo-guard-pause`.

- ✅ A folder's modified date in the list stays old after a file is moved into it.
	- Opened: 20260924-083500. Closed: 20260924-093600.
	- Reproduced: in list view, dragging a file onto a folder row and moving it. The row's item count goes up, but its date does not change, even after the folder is opened in place.
	- Cause: a file added to a folder, taken out or moved in only refreshed the folder's item count. Nothing watching the folder's parent reports its new date.
	- Fixed: the folder's own details are read again along with the count. A removal from a folder that was never opened now refreshes it too, as an add already did.
	- Swept: add, remove and move are the only places these notices go in. Renames, links and changes made by other programs all come through them.
	- Test case: `test-nemo-folder-mtime`.

- ✅ After every file in a folder was removed by another program, icon view's status bar still counted 4 items.
	- Opened: 20260923. Closed: 20260923-161106.
	- Seen once, with 60 files removed one at a time. The view itself was empty. A delete from inside the app, in list view, counted 0 as it should.
	- Note: seen on Linux.
	- Reproduced: every view, not just icon view. With files removed a tenth of a second apart, the count stayed a few too high for good.
	- Cause: the count was read on a timer that starts with the first change of a burst. The last few changes were still waiting to go in when it went off, and nothing counted again after.
	- Fixed: the count is read again after each batch of changes goes in. Files added one at a time by another program were checked the same way.
	- Swept: the view has one place that puts changes in, so there is no second site to cover.
	- Test case: `fCheckStatusAfterChanges` in the C lint.

- ✅ After deleting all the contents of a view, the horizontal scrollbar appears.
	- And it still appears seemingly randomly (when not looking), even though the view has plenty of room in the rules for shrinking content.
	- Opened: 20260923-144941. Closed: 20260923.
	- Cause: column widths only ever grew while a folder was open. A file that left took its name with it and nothing else, so a wide size or type from a file already gone still held its column open. Enough of that and the least the columns could take was more than the view, so it scrolled with nothing on screen needing it. Emptying a folder is the plain case, and files coming and going in the background is the random one.
	- Fixed: once files have left, a layout that would scroll measures the folder again from what is there. It only walks the folder in that case, so a big folder pays nothing while it fits.
	- Still possible: a file whose size or type changes to something narrower keeps its old width until then too. Left alone, since it can only matter once a scrollbar would show, and then the next removal clears it.
	- Test case: `fCheckStaleSamples` in the C lint.

- ✅ With the tab bar set to show for a single tab, closing a tab down to one hid it anyway.
	- Opened: 20260922. Closed: 20260922.
	- Found while adding the Preferences checkbox. Closing a tab reset the tab bar from the tab count alone and skipped the setting.
	- Test case: `test-nemo-notebook` (`test_visibility`).

- ✅ Each change of folder logs a GTK critical: `gtk_widget_draw: assertion '!widget->priv->alloc_needed' failed`.
	- Opened: 20260922-160500
	- Closed: 20260922-193500
	- Seen once per folder change, entering a folder of pictures from the address bar. Present before the thumbnail jump fix.
	- The path bar and the address bar faded into each other. The fade paints the bar going out, and the address bar had just asked to be resized for its clear icon, so a paint before the next layout hit the critical. Upstream nemo logs it too.
	- The two now swap with no fade.
	- Test case: `fCheckToolbarStack` in the C lint.

- ✅ Thumbnails "jump" up, then back down (and also in width), when entering an image folder.
	- Opened: 20260922
	- Closed: 20260922.
	- A picture that came back shorter or wider than the type icon before it kept the old spot until the next full layout. Its name moved up under it, then dropped back down.
	- An icon whose picture changes size is now laid out again before the next frame is drawn.
	- Test case: `fCheckIconRelayout` in the C lint.

- ✅ Scrolling down can still cause the last already cached thumbnails to render (e.g. if you scroll to the bottom soon after entering the folder), while none of the ones above (as judged by scrolling up) are not yet rendered in the file view.
	- Opened: 20260922
	- Closed: 20260922.
	- ✅ Expected behavior: Render all thumbnails to the view in approximate order of the current sort order. (Only varying by threads that return earlier.)
	- The pictures were made in order, but one never on screen was only kept in the store and read back once it scrolled in. So scrolling back up showed type icons for a moment.
	- Each picture is now held as it is made or found, up to a new "Keep in memory" setting on the Preview page, 1 GiB by default. Past it, the pictures within two screens of the view are read back ahead, and the one drawn longest ago makes room.
	- A folder that is left keeps its pictures for a minute, or until another folder of pictures is opened.
	- Test case: `test-nemo-thumbnail-memory`, `test-nemo-thumbnail-hold` (`test_ahead`, `test_ahead_held`).

- ✅ Thumbnail rendering is still trying to follow the thumbnails in the view.
	- Opened: 20260921
	- Closed: 20260922.
	- Observed incorrect behavior:
		- If you immediately scroll to the bottom of a long list of thumbnails, it starts rendering those, long before the "top-down" would get there.
		- If you scroll up (to previously unrendered thumbnails), even before the bottom ones finish rendering, it will begin rendering those ones new to "in-view", immediately.
	- Intended behavior: Depending on the sort order, whatever image is first, gets rendered as a thumbnail first. Whatever is last, gets rendered as a thumbnail last. Nothing the *user* can do, should be able to make the last icon render earlier. It goes purely sequentially.
	- This might have nothing to do with the file reader/thumbnail render, and more to do with the thumbnail reading from the database, and rendering to the thumbnail pane. *Both* should be queued sequentially, top-down.
	- But can be multithreaded. E.g. use up to half of available cores for reading and rendering thumbnails, but still queue them in order - even if they might wind up rendering slightly out of order. But that's due to multithreading, not user activity.
	- A folder of pictures on a local disk is now made top down in the order the view shows it. Scrolling cannot move a file up, and a thumbnail stored on an earlier visit waits its turn too. A new sort or zoom queues the folder again in its new order. Thumbnails start once the folder has finished loading, and up to half the processors make them.
	- A folder that is not mostly pictures, or is on a share, is still only made as it comes into view. Each screenful goes top down.
	- Test case: `test-nemo-thumbnail-order` (`test_scrolled_to_bottom`, `test_stored_waits_its_turn`).

- ✅ Randomly crashes. (At least on Windows, and before the multiple-process work.) Sometimes just with a focus change.
	- Opened: 20260903-130431
	- Closed: 20260921-180912
	- No repro, and nothing in the report to work from, because a crash left nothing behind at all. A windowed build on Windows has no stderr, so it simply vanished.
	- Note: a crash now leaves a report behind. That part is under Done.
	- Left: an actual crash to read. Nothing is known about the cause yet.
	- 20260921-180912: No longer reproducible.
	- Test case: none, closed with no fix to pin.

- ✅ Startup logs a dozen pairs of "invalid (NULL) pointer instance" / `g_signal_connect_data` criticals on this host. Harmless so far - the window comes up fine - and not tied to the release build; the day-to-day container build does the same thing here.
	- Opened: 20260804-133646
	- Closed: 20260921. No longer reproduces.
	- Note: the Windows half, and a second warning logged once per file, are fixed and filed under Done. Whether the Linux host case has the same cause as the Windows one is untested.
	- Found: what produces that exact pair is a signal connected to a settings group that is not open yet. The group handles are NULL until the settings are read, and about seventy places connect to one. Reproduced on demand by starting with no session bus, which is what leaves the store unopened.
	- Not reproduced in the build container. None of these produced a single critical: with and without a session bus, with and without the desktop's own settings present (the container has the full cinnamon schema set already), with a home full of bookmarks including missing and remote ones, bare launch and with a location, with and without the desktop flag.
	- Not reproduced on the Linux host either, with the current build run against the real session's own surroundings: the live config, gvfs and the xdg portals up, at-spi, the xapp GTK module, the XFCE environment variables, and the GTK and icon themes the session is actually set to. Bare launch, with a location, and with the desktop flag; and a second launch forwarding to a running first one, which was the best remaining theory for why the store would not be open yet.
	- Also not the build version: the copy installed here from July, which predates both the resource fix and the config rewrite, is clean in the same harness.
	- Also seen, and not the same thing: with no display at all the default icon theme is NULL, and connecting to it logs the same pair once. Only one pair, and only where there is no screen, so it is not what the real session is doing.
	- Left to find: what the real X session has that a private display does not. Needs one capture run from inside that session; the exact command is in the private notes.
	- Captured inside the real session on 20260921, with the current build: no critical at all. Not with the session's own environment, not with criticals made fatal under a debugger, and none from any of the 28 menu launches in today's session log. Likely fixed along the way by the config and startup work, but nothing pins which change did it.
	- Test case: `test-nemo-startup-clean` (`check_no_bus`), POSIX only; the cause was never found, so it guards the symptom.

- ✅ Every copy past the first gets refused by the XFCE session manager, which logs a critical ("An object is already exported ... org_NemoAnywhere") and a failed waitpid for each one. All copies register as a session client under the same app id.
	- Opened: 20260921-180500
	- Closed: 20260921-182000
	- Fixed: no copy registers now. It was carried over from upstream and nothing used it. Logout is still held off during a copy, since that asks the session manager directly.
	- Note: a lint check fails if registering comes back.
	- Test case: `fCheckNoSessionRegister` in the C lint.

- ✅ Bug: When changing to list view after being in an image folder, the zoom level still doesn't reliably change back to defined.
	- Created 20260921-170804 by JC. Closed: 20260921.
	- With per-folder settings off, list view and icon view shared one held size on the window, and their sizes and defaults differ. Whichever view wrote it first decided what the other one opened at. List view holds its own size now.
	- Test case: `fCheckHeldIconSize` in the C lint.

- ✅ The Windows test box built with link-time optimization one job at a time, because its MSYS2 had no `make`. Installed there, and the Windows pipeline stops with the fix named if it is missing.
	- Opened: 20260921. Closed: 20260921.
	- Test case: `cicd/cicd-win.ps1` stops when MSYS2 has no `make`.

- ✅ The Windows dogfood build was 18 days old. Only a run of the Windows pipeline by hand ever updated it.
	- Opened: 20260921. Closed: 20260921.
	- A full pipeline run now also builds, tests and packs on whichever Windows test box answers, and drops the exe into the synced app folder. It takes the shared lock on the box first. A quick run skips it.
	- The first run found two more bugs, fixed with it. A Windows build dir could not update a library that had lost a source file. And a folder of pictures was never seen as one on Windows, since the type check there never matched an image.
	- Test case: `test-nemo-image-folder` for the picture type check on Windows; the pipeline stage itself is setup.

- ✅ Images view:
	- Opened: 20260921-152951
	- Doesn't render at specified %, until the % is changed. (But afterward seems to remember?)
	- 500% is too big. Let's do 250%
	- The image % affects list views too. At least, when changing to a list view folder from an image folder.
		- If you manually change the view to icon though, that renders correct zoom. Then back to list view, then it's also the correct zoom.
	- An image folder flashes when entering. First list view, then images view.
		- Is this just an inherent limitation of dynamic file listing? If so:
			- Maybe folders one level down can be pre-scanned in a background thread, to know in advance if they are image-heavy.
			- Keep a list in memory of last N folders that are known to contain images, to avoid having to re-scan.
	- Closed: 20260921.
	- With per-folder settings off, the window held one icon size that list view shared. A list folder left its own size there, which the picture folder read as a zoom and so skipped the % setting. Changing the % in a picture folder then wrote the picture size back into it, and the next list folder picked that up. The window now holds a picture size apart from the plain one, the same pair a folder keeps when per-folder settings are on.
	- The picture default is 250% now.
	- The flash is gone for any folder seen lately, and for the folders one level down from the one in front. The answer is kept in memory for the last 512 folders. When a folder finishes loading, its sub-folders are counted in the background, so opening one of them picks icon view first time. Shares, links and non-local folders are not counted ahead, and a big folder is judged on its first 1000 entries. The real count after loading still has the last word.
	- A folder opened cold, from a bookmark or the command line with nothing known, still switches after loading. Nothing can be known about it sooner.
	- Test case: `test-nemo-image-folder`, `fCheckHeldIconSize` and `fCheckImageDefault` in the C lint.

- ✅ The About box license text says "or (at your option) any later version", but the project is GPL-2.0-only.
	- Opened: 20260921. Closed: 20260921.
	- Upstream Nemo is or-later, which can be narrowed to version 2 only. A few files written for the fork are version 2 only, so the whole program has to be. The text now says version 2 only.
	- The file headers inherited from upstream still say or-later. That is right for those files and they were left alone.
	- The lint now fails if text the program shows offers a later version.
	- The copyright lines and the top of the license text now match the README. The two links sit in the license text, since the copyright line cannot hold a link.
	- Test case: `cicd/utility/lint-identity.bash`.

- ✅ The "expand" Chevron next to folders should more reliably appear when a formerly empty folder gains content, especially after user-initiated actions (like drag and drop contents into a previously empty folder).
	- Opened: 20260919-125440. Closed: 20260920.
	- Both views only ever took the expander away. The list view dropped the placeholder row once a folder's item count came back as zero and had no branch for the count going the other way; the tree pane's look-ahead could mark a folder as having no sub-folders but never unmark one, and nothing sent it back to look.
	- Fixed both ways round. The list view puts the placeholder back when the count rises off zero, and the tree pane looks again whenever a folder it had written off reports a change.
	- The tree pane's look-ahead now separates "holds nothing at all" from "holds nothing it would show". The first still takes an expander away, but only the second puts one back, so a folder opened with hidden files off and found empty does not have its expander handed back by the next look.
	- Seen on screen in both panes: a folder copied into a folder that read as empty, which before left no expander on either side.
	- Only an action the app itself took is covered. A folder changed by something else on the machine is not watched at all - opening it is still the only way that shows up - and that has not changed.
	- New `test-nemo-list-expander`, and a case in `test-nemo-tree-folders`.
	- Test case: `test-nemo-list-expander`, `test-nemo-tree-folders`.

- ✅ A theme change does not send the list back to measure its columns.
	- Opened: 20260920-234500. Closed: 20260920.
	- The handler for it only dropped the cached separator and indent sizes. Every width worked out before the change stayed, so a theme with a wider font left values cut off until a zoom or a folder change forced a remeasure.
	- Fixed: the handler keeps the font and the two theme sizes as one string and compares it. Where it moved, every row is measured again; where it did not, nothing happens.
	- The comparison is the point. That signal also fires for a widget state or a CSS class going on and off, and sending 50,000 rows back on each of those would cost more than the whole cache saves.
	- Seen on screen at 17pt against the default: before the fix the Size column reads `7.7 ...` and Date modified `2021-02-1...` after the switch; after it, both are whole.
	- `lint-c.bash` holds both halves - the handler has to remeasure, and it has to gate on what changed.
	- Test case: `fCheckStyleRemeasure` in the C lint.

- ✅ Half of what is left of a big folder load is measuring text for the column widths.
	- Opened: 20260920-233000. Closed: 20260920.
	- Cause: every row asked every column for a full text layout, and most of those layouts were of text the column had already seen. A type, an owner, a group or a set of permissions is the same string down the whole folder.
	- Fixed: each column remembers the width it worked out for a piece of text and hands it back rather than laying it out again. Name is left out, since no two files in a folder share a name, and a column stops remembering past a couple of thousand distinct values, which is where a date would otherwise keep one entry per row.
	- A row drawn in its own weight is measured rather than remembered. Bold and light lay out differently at the same text, and there are never many.
	- 50,000 empty files went from 8.4 s to 5.2 s to show and from 8.9 s to 5.8 s of processor time. Over files whose names, sizes, dates and extensions all vary, the same count went from 11.0 s to 8.4 s. Peak memory did not move.
	- Nothing about the view changed: the same shots across two window widths and three zoom levels, bold rows among them, come out pixel for pixel identical.
	- `lint-c.bash` holds the three rules that keep a remembered width right: only a normal-weight cell may reuse one, they go when the rest of the samples go, and a column stops remembering at a ceiling.
	- What is left of the measuring is the Name column, where nothing repeats by definition. Not filed - there is no obvious way to measure fewer names while the width rule counts every one of them.
	- Test case: `fCheckMeasureCache` in the C lint.

- ✅ Listing a large folder costs about 0.4 ms a file, and nothing in the list view accounts for it.
	- Opened: 20260920-160000. Closed: 20260920.
	- The earlier reading was wrong. It does sit in the list view - the pass that cut per-row drawing work missed it because the cost is in measuring, not drawing.
	- Cause: the tree view was measuring every row to find out how tall it is. It only has to do that when a column is left to size itself, and none of ours are - the widths are worked out for the whole row and handed over. So it was redundant work the whole time.
	- Fixed: the list runs in fixed-height mode, one line. 50,000 empty files went from 11.1 s to 8.4 s to show, from 17.9 s to 8.3 s to settle, and peak memory from 269 MiB to 158 MiB. With varied names and sizes rather than flat ones, 10,000 files went from 3.6 s to 2.0 s of processor time.
	- Nothing about the view changed: the same shots at every zoom level, and a folder of images at thumbnail size, come out pixel for pixel identical.
	- `lint-c.bash` pins the two together, since fixed-height mode is only safe while every column sizes FIXED and it fails quietly rather than loudly.
	- design.md's speed table is remeasured, and what is left is its own item under Bugs.
	- Test case: `fCheckFixedHeight` in the C lint.

- ✅ Re-vendoring the themes would drop 53 icons.
	- Opened: 20260920-230000. Closed: 20260920.
	- Not upstream drift, which is what it was filed as. Two of the four themes are still at the exact commit they were taken from, so nothing upstream could have moved. The vendoring script had stopped finding the aliases.
	- Cause: every one of Qogir's 17,202 aliases under `links/` points at a sibling name that only exists under `src/`, so each one dangles where it sits. `-type f` skipped them for being symlinks and `-xtype f` skipped them for dangling. Neither is a reason to drop one - what the resolver wants from an alias is the name it points at, and `readlink` gives that whether or not anything is there.
	- The committed art was right all along. It was taken on a checkout where git wrote the aliases as text files instead of symlinks, which is the one shape the old code could read.
	- Fixed: the index takes symlinks too, and a link that dangles is followed by name rather than by path. Qogir and Tela now regenerate byte for byte against what is committed. The self-test gained two checks covering it.
	- Real upstream drift, now that it can be told apart, is three repos and nothing that matters: WhiteSur and Colloid produce identical output, and Adwaita's 139 files differ only in attribute order and path syntax from upstream re-running their own optimizer. Left alone rather than churned.
	- Test case: `cicd/utility/vendor-themes.bash --self-test`.

- ✅ Horizontal scrollbar frequently shows up when not needed.
	- Opened: n/a. Closed: 20260920.
	- Settled first: the scrollbar must not appear while anything is left to shrink, so this was a fault rather than the rule being right.
	- Cause: Name was the tree view's expanding column. The layout hands out widths that come to exactly the row, so there was nothing for GTK to expand into - but GTK kept the share it had worked out while the view was wider, and it hands that back only when some width really changes. Name sat 346px over the width it had been given, which is what the scrollbar was for.
	- Fixed: no column expands. The layout already gives Name the leftover, so the row still ends flush and GTK has nothing to add.
	- Why it looked random. Nothing about it needed a resize, which is why driving the window from 1000 to 1200 wide never showed it. It cleared only when a late row happened to change a width, and stayed put otherwise - so the same window could be clean, then carry a scrollbar later with nothing touched.
	- The earlier attempt at this had the right suspicion and no effect: it laid the columns out again, which arrives at the same numbers, sets no width, and therefore leaves GTK holding the old one. That code is gone.
	- Also why it never reproduced before: the old probes gave every file the same name, size and date, so no column could grow after the first row and the case could not arise.
	- `lint-c.bash` now refuses any expanding column in the list view, so this cannot come back quietly.
	- Test case: `fCheckColumnExpand` in the C lint.

- ✅ A theme icon that exists only as a symlink is never found.
	- Opened: 20260920-170000. Closed: 20260920.
	- `vendor-themes.bash` indexed with `find -type f`, which skips symlinks, so nothing under a theme's `links/` directory was ever a candidate on Linux. The resolver has code to follow an alias and the scorer has code to rank one, and neither could run.
	- Fixed: the index is built with `-xtype f`, which takes a link pointing at a regular file and leaves a dangling one out. The self-test's pinned case is turned around and a dangling-link case is new.
	- Superseded on 20260920. `-xtype f` was only half of it: nearly every alias upstream writes dangles where it sits, so this left them out too. See the item above.
	- How many of the 180 names it was: none, today. Four themes have a `links/` directory - WhiteSur, Colloid, Tela and Qogir - and re-vendoring each one both ways gives byte-identical output. Every alias in them points at art the index already had under its own name.
	- So the fix buys nothing on screen right now. It is worth keeping because the alias code can run at all now, and because an upstream that moves a name into `links/` alone would otherwise fall through to Adwaita with nothing to say so.
	- Tela indexes 16,800 more candidates with this on and the run takes the same 3.4s, so the wider index costs nothing.
	- Test case: `cicd/utility/vendor-themes.bash --self-test`.

- ✅ Fourteen `catch { }` blocks in the PowerShell scripts swallow whatever went wrong.
	- Opened: 20260920-190000. Closed: 20260920.
	- In `cicd-win.ps1`, `install.ps1`, `n8runfm.ps1` and `pack-portable.ps1`. All best-effort cleanup, and several read better as `-ErrorAction SilentlyContinue` on the one call inside.
	- Fixed: thirteen are gone. Six became an `-ErrorAction` or a plain guard on the call that could fail. Five now say what went wrong, which is the part that was missing: no transcript for the run, a PATH change that running programs will not see, a packer that had already exited, a message box that could not come up, a log that could not be trimmed.
	- The transcript pair is now a flag rather than a catch each end, so the run no longer tries to stop a transcript that never started.
	- Left on purpose: the logger's own catch. There is nowhere to report a failed log line, since the console is gone on a shortcut click. Suppressed at that one function with a reason, not in the settings file.
	- The rule is on in `PSScriptAnalyzerSettings.psd1` now, so a fresh empty catch fails the lint stage.
	- Found on the way: reaching straight for `.Hash` off a call that may have failed throws under `Set-StrictMode -Version Latest`. The result is held first.
	- Test case: `cicd/utility/lint-powershell.bash`, skips without pwsh.

- ✅ Code review 20260919.
	- Opened: 20260919-175254. Closed: 20260920.
	- Style, performance and prose pass over the whole tree, first-party and inherited, aimed at areas the two earlier rounds did not cover. Worst first. Technical detail is kept out of this file. Numbers match the private detail notes.
	- ✅ High.
		- ✅ Item 1. The Windows exe that gets published and signed is a debug build.
			- Cause: neither the release workflow nor the cross build asks for a release build or for symbols to be stripped, and the project file sets no default, so meson picks debug.
			- Effect: the shipped exe carries full debug information at 15.7 MB. The Linux release binary beside it is 3.0 MB. Every release tag so far has published one.
			- Origin: predates the fork's first release lane. Neither earlier round looked at build flags. Confirmed.
			- Fixed: both Windows lanes and the Linux release lane ask for a release build with symbols stripped. The cross exe went from 15.7 MB to 8.3 MB with no debug sections. A new check reads the flags back out of the exe.
			- Test case: `cicd/utility/check-win-build-flags.bash`.
		- ✅ Item 2. A permanent delete out of the trash can run with no dialog.
			- Cause: the trash branch of the confirmation is the only one of the three that does not also ask when the count alone warrants it. Move-to-trash and direct delete both do.
			- Effect: with confirmation off, a delete over a trash address started anywhere but a window goes through silently. The armed test guard hides this in current builds.
			- Origin: the same gap as the Empty Trash one closed by `dbustrash` on 20260917, in the same file. A regression of that class rather than new ground. Confirmed.
			- Fixed: the trash branch now asks when the count warrants it, the way its two siblings do. One function holds the decision, with a test over it.
			- Test case: `test-nemo-delete-from-trash`.
		- ✅ Item 3. A settings handler outlives the places sidebar.
			- Cause: the handler is connected to the windows settings group and disconnected from the preferences group, which is a different group, so it is never removed.
			- Effect: changing the path separator after a window closes calls into a freed sidebar. Live reload makes it reachable.
			- Origin: introduced with the path separator work; the comment directly above the disconnect describes guarding against exactly this. Confirmed.
			- Fixed: the handler disconnects from the group it was connected to. A new whole-tree check pairs every connect with its disconnect, reading the group names out of the header, and it found a third site this item did not name.
			- Test case: `cicd/utility/lint-pref-handlers.py`.
		- ✅ Item 4. A damaged spreadsheet can hang the content search helper.
			- Cause: the shared-string loop trusts a count read from the file and tests its end against the whole stream rather than the current record, while the reader it calls stops advancing once the record runs out.
			- Effect: a truncated workbook spins up to four billion empty passes. The helper is spawned per file, so one bad file in a folder ties up a core.
			- Origin: written for the search helpers, never fuzzed. The three fuzz targets cover the settings file, the drag payload and command templates, not these parsers. Confirmed.
			- Fixed: the loop has to make progress or it stops, with a truncated workbook in the fixtures. The record-end bound suggested above was not taken, because it drops strings split across continuation records.
			- Test case: `test-nemo-search-helpers` (`test_xls_truncated_sst`), and the `fuzz-xls` target.
	- ✅ Medium.
		- ✅ Item 5. No build in the tree uses link-time optimization.
			- Origin: never set up. Confirmed.
			- Fixed: on the release lanes. It bought no size, so it stays for what it may buy later.
			- Test case: none, build setting only.
		- ✅ Item 6. The Bash linter runs nowhere.
			- Cause: the lint stage runs the C and Python checkers only. Scripts carry suppression comments for a checker that is never invoked.
			- Origin: the lint stage grew around the C checks. Confirmed.
			- Fixed: `cicd/utility/lint.bash` is the lint stage now, and runs shellcheck over the project's own scripts beside the C checks. It is no longer gated on cppcheck, which used to take the whole stage down on a box without it. Fourteen findings fixed; four scripts still turn rules off file-wide, which is filed separately.
			- Test case: `cicd/utility/lint.bash` runs `cicd/utility/lint-bash.bash`.
		- ✅ Item 7. Four checks in the suite can never fail.
			- Cause: one is written so its condition is always true; another compares two searches without first testing that either found anything, so the regression it guards would turn it green.
			- Origin: spread across the suite's growth. Confirmed.
			- Fixed: each now checks what its comment says. The link-copy one asks the file system instead of repeating a call it already made, and the network one compares host names, which is what its comment always claimed. The two Windows-only ones still need a run on a real box.
			- Test case: none, fixes to tests.
		- ✅ Item 8. Three tests report success when they could not run.
			- Cause: they print that they are skipping and then fall through to a success exit rather than the skip exit. A fourth returns the skip code without first reporting failures it already counted.
			- Origin: predates the rule being written down. Confirmed.
			- Fixed: all four return 77 on the paths where they skip, and failures already counted are reported before the skip code. Two were watched both ways; the two Windows-only ones still owe that watch on a real box.
			- Test case: `fCheckTestSkipExit` in the C lint.
		- ✅ Item 9. One test builds and removes its own scratch tree.
			- Cause: it is the only test that does not go through the shared helper, and it removes the tree twice by hand. Two runs at once destroy each other.
			- Origin: written before the helper existed and never moved over. Confirmed.
			- Fixed: it uses `test_scratch_dir` like the rest of the suite. Twelve copies at once over eight rounds: 24 of 96 failed before, none of 96 after.
			- Test case: `fCheckTestScratchDirs` and `fCheckTestTreeWalks` in the C lint.
		- ✅ Item 10. A test has the same settings-group mismatch as item 3.
			- Effect: a later check in the same file fires the handler, which writes through a pointer into a frame that has returned.
			- Origin: copied from the sidebar code it tests. Confirmed.
			- Fixed with item 3, and covered by the same whole-tree check.
			- Test case: `cicd/utility/lint-pref-handlers.py`.
		- ✅ Item 11. List view row measurement and row shading both do far more work than they need to.
			- Cause: measurement is hooked to row changes as well as row arrivals, so it re-runs every time a file's details fill in, rebuilding column and cell lists and reading two style properties each pass. Shading allocates a path and sets a property once per cell per redraw, including when shading is off.
			- Effect: both sit under the per-file listing cost design.md already flags as the one to watch.
			- Origin: measurement came with the column width work, shading with `ownerrows`. Confirmed by reading the hookup and the bodies, not measured.
			- Fixed: the theme sizes and the column list are read once rather than per row, parity is worked out once per row rather than once per cell, and a renderer that already has no background is left alone. The rows look the same as before.
			- Measured over 20,000 files. Listing did not move, at about 7.8 s of processor time either way. Paging through the folder went from 1.16 s to 1.01 s with shading off, which is the default, and did not move with it on. So the suspects here were real but small, and the listing cost is somewhere else.
			- Test case: `fCheckCellPlain` in the C lint; for the speed, `rj4jkr22 List view work per row test`, which counts the per-row and per-cell work against the cells measured.
		- ✅ Item 12. The theme vendoring script forks per icon.
			- Cause: the resolver is called through command substitution up to five times per icon across roughly 3,600 icons. The file's own note two hundred lines above says a substitution there is a fork and that this runs tens of thousands of times, and solves it that way for the scorer.
			- Origin: the scorer was fixed, the resolver that calls it was not. Confirmed.
			- Fixed: the resolver answers through a global and returns a status, the way the scorer already did. The link-stub test reads the head of the file itself instead of calling out three times, and the two `dirname` calls and the two branches picking a directory name are gone. That is about twelve forks an icon removed.
			- New `--self-test` builds a small tree and checks resolution against it: plain name, symbolic name, context filter on and off, both alias forms, the hop limit and a missing name. It runs in the lint stage, since the build container has no git.
			- Test case: `cicd/utility/vendor-themes.bash --self-test` for resolution; for the speed, `cicd/utility/test-vendor-forks.bash` (rj4j8jk8), which counts the processes started per icon.
		- ✅ Item 13. A maintainer's home path is baked into test fixtures.
			- Cause: five lines of one Windows test use a real personal path where the rest of the suite uses a placeholder.
			- Origin: written with a live path and never anonymized. Confirmed.
			- Fixed: the five lines say `somebody`. A checked-in `.pyc` holding a build path went with them. New `lint-identity.bash` holds it.
			- Left alone: this file names a real account in three closed items, which is prose rather than code, so the check does not read it.
			- Test case: `cicd/utility/lint-identity.bash`.
		- ✅ Item 14. Six application sources carry the wrong copyright marker.
			- Cause: they use the form reserved for the shared helper scripts. Fifteen other first-party files carry no copyright line at all.
			- Origin: the link and shortcut files were drafted as helpers. Confirmed.
			- Fixed: all twenty-one carry the project's marker. The same identity check refuses the helper marker under `source/` and refuses any retired marker anywhere.
			- Test case: `cicd/utility/lint-identity.bash`.
	- ✅ Low.
		- ✅ Item 15. The twelve first-party Python files indent with tabs, where the house style for that language is four spaces. Two of them hold hand-aligned tables that a mechanical conversion would damage.
			- Fixed: leading tabs are four spaces, and a run of tab-aligned trailing comments is aligned with spaces instead. The two tables were never at risk - their alignment is relative to a single leading tab, so converting it shifts the whole block and nothing else.
			- Tabs left in place: inside multi-line string bodies, where they are data, and in the `##` header block every script in the tree shares.
			- Proof the conversion changed nothing: each file's parse tree was compared before and after, and the four files with tabs inside string literals were redone with those lines held back until it matched.
			- Three findings that turned up with the checker are fixed too: a one-letter variable, a lambda where a def belongs, and two statements on one line.
			- Test case: `cicd/utility/lint-python.bash`.
		- ✅ Item 16. There is no configuration for the Python, PowerShell, Bash or C static checkers. The absent C formatter config is a settled decision and is not part of this.
			- Python: `pyproject.toml` holds a narrow ruff config, and `cicd/utility/lint-python.bash` runs it in the lint stage. It warn-skips a box with no ruff, the way the Bash check does, and `RUFF_STRICT=1` makes the miss fatal. Inherited and generated Python is excluded, the same way `source/` is excluded from the Bash check.
			- Bash: `.shellcheckrc` now holds the severity and the sourced-file handling the lint stage used to pass as flags, so an editor sees the same rules. Without it the lint stage reports seventeen findings, so it is doing real work.
			- PowerShell: new `PSScriptAnalyzerSettings.psd1` and `cicd/utility/lint-powershell.bash`, sixth in the dispatcher. Five rules are off with a reason each; everything else is on. The one finding left is suppressed where it happens, not in the settings file.
			- C: the two dozen cppcheck suppressions moved out of the command line into `.cppcheck-suppressions`, with the reason for each still beside it. The findings over a 98-file range are the same as before.
			- Test case: none, the configs are the linters.
		- ✅ Item 17. This file records how work was verified in fifteen places. Settled: the rule covers the public docs, so only these fifteen need the pass.
			- Fixed: those lines now say what was checked and leave out how. design.md and the two style guides keep theirs, since describing the build and test rig is what those files are for.
			- A check outside the repo holds it, called from the lint stage only when it is there, so a clone without the private tree still lints.
			- Test case: a check kept outside the repo, run by the lint stage when present.
		- ✅ Item 18. British spellings in comments and prose, including two identifiers.
			- Fixed: about sixty comment and prose lines, plus the two sets of identifiers - the Windows splash colors and the launcher's status color. The release notes and the README are in it, which is where it was visible.
			- Inherited lines are left as they are, here and in every check below: most of the tree came from upstream and spells things its own way.
			- New `cicd/utility/lint-prose.bash` in the lint stage holds this, the banner rule from item 20, and the ASCII rule that goes with it.
			- Test case: `cicd/utility/lint-prose.bash`.
		- ✅ Item 19. Banned verbs in roughly sixty comment lines across C, scripts and Python.
			- Fixed: sixty-six comment and prose lines, a README heading and its table of contents entry, and three test variables named after one of them.
			- Held by the same check as item 17, outside the repo for the same reason.
			- Test case: a check kept outside the repo, run by the lint stage when present.
		- ✅ Item 20. Three competing banner-comment conventions in first-party C, and a prose block at the top of nearly every first-party file. One of those blocks restates a design.md rule that can drift from it.
			- Fixed: forty-four banners in eleven files. The style guide has said "No banner dividers" all along, so the words stay as plain comments and the rules are gone. A fourth form turned up in the tests.
			- The bullet-rule form was also the only non-ASCII in first-party C outside the copyright line, so the new check refuses that too.
			- `nemo-column-layout.h` points at design.md now instead of restating fifteen lines of it.
			- The other module blocks stay. They say why a file exists, which is what they are for; only the one that copied a rule was a problem.
			- Test case: `cicd/utility/lint-prose.bash`.
		- ✅ Item 21. Seventy-four smaller items, grouped so none is left unfiled: repeated work that a hoist would remove, allocation on paths that run per file or per row, duplication across the test suite that the shared helpers should absorb, dead parameters and unreachable branches, and naming that reaches for the same few words. Detail is in the private notes.
			- Done, out of the test-duplication group: the eight hand-rolled tree removals. Seven of them removed the scratch directory the test had just made, which the helper already removes at exit, so they are simply gone. The eighth needed a removal part way through and goes through the helper now. That is 151 lines fewer.
			- Done, the rest of the test-duplication group. The `check` macro was in sixty-seven files in two spellings and is now one header. Twenty-six tests set the same environment variables by hand to get a throwaway config root and now call one helper. The two 46-line blocks are two small headers. That is 819 lines fewer, and a new check refuses a fresh copy of either.
			- Done, the hoists and the per-row allocation. The settings accessors now take a path built once per key instead of building and measuring it on every read, which is the one that matters: reads happen per icon hover. The Ext column stopped copying the file name to look at it, the Windows index search stopped measuring the search folder once per result row, and the archive check answers "is this folder in there" from a set built while the archive is read rather than by walking every entry again.
			- Done, the dead parameters and unreachable branches. An empty function and its twenty-one calls are gone, two icon-generator functions no longer take a theme they never look at, a gif option gated on a constant zero is gone, and a wine fallback branch that could never run went with the always-true test in front of it.
			- Done, the naming: the profiler script carried the Bash `f` prefix into Python and is the only Python file here that did.
			- Not done, with reasons. Two theme-root scans stay: one is startup, the other is opening the preferences dialog, and the only way to skip them is to cache the scan, which means a theme installed while running goes unseen. The per-key ancestor walk in the folder settings stays: it runs once per folder change, not per file. The metadata store keeps its one pass per moved file, since skipping it needs an index of every ancestor of every key, and the comment that read as a contradiction now says what the code does.
			- Not done, and dropped: `out` and `result` as the name of the value a function returns, in eleven Windows files. Every one is a short function that declares it, fills it and returns it. That is the clearest use of the name, so there is nothing to fix.
			- Test case: `fCheckTestHelpers` in the C lint for the test helpers. For the hoists, `rj4jbn1b Allocations per read test` covers the settings reads and the Ext column, and `rj4jewn6 Archive check cost test` the archive check. The Windows index search hoist has none: it saves one length per result row beside a split and a copy the same row already makes, too small for a bar. The dead code has none, since nothing is left to run. The regex hoist in `flame-report.py` and the string build in `svg-min.py` have none either: both are developer scripts, and neither gained as much as two times, too little for a timing bar that holds steady.

- ✅ The Windows cross link compiles its LTO jobs one at a time.
	- Opened: 20260919-203000
	- Closed: 20260920-223000
	- The build asks for four threads and every step takes them but the final link, which reports serial compilation of 37 jobs. Two attempts to pass the count through failed.
	- Origin: came in with link-time optimization on the release lanes. Confirmed.
	- Cause: nothing to do with the thread count. gcc runs its link-time jobs by writing a makefile and calling `make`, and the cross container had no `make` in it. With none on the path it falls back to one job at a time and says so.
	- Fixed: `make` is in the cross image and the running container. The exe link went from 34.2s to 9.9s, and the exe is byte for byte what the serial link produced, so nothing about the shipped artifact changed.
	- The cross build now refuses a log that carries the fallback warning, and names the container to install it in. The other two containers already had `make`.
	- Test case: `cicd/win/build-cross.bash` refuses a serial link.

- ✅ The content search helpers have no fuzz target.
	- Opened: 20260919-203000
	- Closed: 20260920-213000
	- They parse the most hostile input in the tree, and the three existing targets cover the settings file, the drag payload and command templates instead.
	- Origin: raised while fixing the shared-string loop in the xls helper, which is the kind of fault a target would have found. Confirmed.
	- Fixed: three new targets, one each for the workbook records, the presentation records and the Word piece table, with twenty seeds between them. They run under the fuzzer in the pipeline stage and replay their seeds as ordinary tests on every suite run.
	- The zip-of-xml helper is deliberately left out. libgsf does the parsing there, so a target would be fuzzing libgsf.
	- Test case: `fuzz-xls`, `fuzz-ppt`, `fuzz-doc`, run by `cicd/linux/fuzz.bash`, with seeds replayed in the suite.

- ✅ A release build prints four warnings about unused functions and variables.
	- Opened: 20260919-203000
	- Closed: 20260920-210000
	- From `nemo-file.c` and `nemo-view.c`. Not new: the Linux release lane has always been a release build. The Windows lane only started showing them once it became one.
	- All four exist only for a debug line, and a release build compiles those out. Three are a window handle the view fetched on every batch of files and on every selection change; that call, and a uri allocation beside it, now happen only when the debug flag is actually on. The fourth is a small function the compiler is now told may go unused.
	- The cross build used to throw its whole log into `tail -1`, which is why nobody saw these. It reads the log now and refuses an unused-function or unused-variable warning, so this is caught on an ordinary pipeline run rather than at release time.
	- Test case: `cicd/win/build-cross.bash` refuses unused-function and unused-variable warnings.

- ✅ Two archive tests remove a tree without the symlink guard.
	- Opened: 20260920-150000
	- Closed: 20260920-200000
	- The `remove_tree` copies in `test-archive-job.c` and `test-extract-job.c` recursed on whatever the enumeration called a folder, and `test-archive-job.c` plants a symlink in the same tree. Its target was missing, so nothing outside had been removed yet.
	- Six more copies turned up beside them, eight in all, and the one in the delete guard test walked a tree holding a link to the fake home it had just built.
	- Fixed: all eight are gone. Seven were removing the scratch directory the test made, which the helper already removes at exit under its own guard. The eighth calls the helper's new `test_scratch_remove_tree`, which refuses a path outside a directory this process made and takes a link as a link.
	- New `test-scratch-guard`, POSIX only, builds a tree with a link pointing out of it and checks that what the link pointed at is still there afterwards. It goes red when both of the helper's guards are taken off; either one alone still holds.
	- New rule in the C lint: a test may not define a function that calls itself, lists a directory and removes what it finds.
	- Test case: `test-scratch-guard`, POSIX only, and `fCheckTestTreeWalks` in the C lint.

- ✅ Four scripts turn a dozen shellcheck rules off for the whole file.
	- Opened: 20260920-150000
	- Closed: 20260920-190000
	- `cicd.bash`, `config.bash`, `gui-headless.bash` and `include/gfs-rotate.bash` carried a header block of `disable=` lines from a shared template. Each had a reason, but it applied to a handful of lines and covered every line.
	- With the blocks taken off, only two rules fired at all. So eleven of the thirteen were dead suppression, and four of them could never have done anything anyway: they start with `##`, which shellcheck reads as prose.
	- `config.bash` keeps one, the unused-variable rule: it is a settings file and cicd.bash reads every name in it, so the whole file looks write-only. The other three keep none. The three real findings are fixed - two quoted exit codes, and one deliberate word split that now says so at the line.
	- What the blocks were hiding: two dead variables in `cicd.bash`. `quiet` was set by `-q` and never read, so `-q` was only ever an alias for `-y`; the publish step runs quiet either way. `abs_script` was computed and dropped.
	- The narrowed check is the regression test - a dead variable in any of the four fails the lint stage now.
	- Test case: `cicd/utility/lint-bash.bash` refuses a file-wide disable.

- ✅ Archiving with rar: When "delete after confirm" was set, got an error message: "The original files were kend. The archive could not be read back."
	- The archive seems to have been created correctly.
	- Opened: 20260920-153000
	- Closed: 20260920-163000
	- Cause: with "Encrypt the file names too" ticked, rar is run with `-hp` and 7-Zip with `-mhe=on`, and libarchive will not open either. It refuses before it reads a single name, whatever password it is handed. So the check could never pass and the originals were always kept, on an archive that was in fact fine.
	- Fixed: `nemo_archive_can_verify` is the one place that decides whether an archive can be read back afterwards. The Compress dialog greys the delete box on it, the same way it already did for a split archive, and the job answers with a sentence that says what the trouble actually is.
	- Rar with no password and rar with a password both read back and delete as they should; it is only the name encryption that cannot be checked.
	- Test case: `test-archive-job` (`check_predicate`), `test-nemo-archive-combos`.

- ✅ The settings-handler check cannot see one of the config groups.
	- Opened: 20260919-203000
	- Closed: 20260920-103000
	- It matched group names ending in "preferences", so `nemo_window_state` was invisible to it and a mismatched disconnect there would have passed.
	- It reads the group names out of `nemo-global-preferences.h` now, so a group added later is covered the day it is declared.
	- Test case: `cicd/utility/lint-pref-handlers.py --self-test`.

- ✅ "Mount archive" doesn't seem to do anything.
	- Opened: 20260918-163716
	- Closed: 20260918-170628
	- It was a Cinnamon action that ran `gnome-disk-image-mounter`. That only attaches disk images, so a zip or 7z did nothing.
	- Now a built-in menu item beside the Extract items. It opens the archive through gvfs, which mounts it and shows its contents. The mount shows under Network with an eject button.
	- Checked once at startup. With no gvfs archive support, which includes Windows, the item is not shown.
	- Test case: `test-nemo-archive-mount` for the address; the mount itself needs gvfs.

- ✅ Two tests fail on a native Windows build: the test guard arming test and the tree folders test.
	- Opened: 20260918-213000
	- Closed: 20260918-234500
	- Neither had run on Windows before. Both were added after the last real-Windows pass, and b29w is the first box to run them.
	- Both were test problems, not product ones.
	- The arming test looked for `/tmp/...` in text that Windows spells with backslashes. It now looks for each path the way the dialog writes it.
	- The tree folders test hid its folder with a leading dot, which is not what hidden means on Windows. There it sets the hidden attribute instead.
	- Both pass on b29w now, where they failed before.
	- Test case: `test-nemo-delete-testguard`, `test-nemo-tree-folders`, both run by the Windows gate.

- ✅ The search helper for zip-based documents (docx, odt, epub) checks the wrong thing after a read.
	- Opened: 20260918-112900
	- Closed: 20260918-113800
	- `nemo-mso-to-txt.c` tested the buffer it had just read into for NULL, which is never true, instead of what the read returned. The compiler warned about it on a fresh build.
	- Done: it checks the read and stops at the first failure. The helpers now build with that warning as an error, which fails on the old code.
	- Note: no damaged file could make the read fail. The zip reader underneath returns data even for a broken member, so the old check never changed what came out.
	- Test case: the search helpers build with `-Werror=address`.

- ✅ Nothing but a window may trash, delete, move or empty the trash. Other programs could still ask over the bus.
	- Opened: 20260917-185500
	- Closed: 20260917-191500
	- The bus interface that could copy, move and empty the trash came from the Nemo desktop. Nothing here uses it. A move to the trash folder over it was a recycle with no window behind it.
	- Fixed: the interface is removed. Only the freedesktop one is left, and it shows folders and properties. The instances test now checks the bus has no way to copy, move or empty the trash. Its no-bus test is commented out, since what it tested is gone.
	- Lint now keeps three lists: which files may start a trash, delete or move job, which bus methods exist, and which files may delete anything directly. The last only touches the app's own files.
	- The rest of the Nemo desktop code, such as the icon view's desktop mode, is dead but deletes nothing. It is on the backlog.
	- Test case: `fCheckBusMethods`, `fCheckAppActions`, `fCheckJobCallers` and `fCheckRawDeletes` in the C lint, `test-nemo-instances`.

- ✅ Removing a template in Preferences deleted the file outright, with no question and nothing in the log.
	- Opened: 20260917-185500
	- Closed: 20260917-191500
	- Found while checking every delete. It skipped the delete guard entirely.
	- Fixed: it goes to the trash through the same job as any other, so it asks first and is logged. The lint list above catches a delete like it.
	- Test case: `fCheckRawDeletes` and `fCheckJobCallers` in the C lint.

- ✅ `--version` fails with "Cannot open display" when there is no display.
	- Opened: 20260917-183048
	- Closed: 20260917-183306
	- First seen 2026-09-07, still the case on 2026-09-17. Printing a version should need nothing but the binary.
	- Cause: the toolkit's own options open the display while the command line is read, so the read failed before `--version` was looked at. `--about` did the same. `--help` was never affected.
	- Fixed: with either flag the display is left alone. A `--display` given alongside is still honored when a window opens. New test runs both with no display.
	- Test case: `test-nemo-cli-version`, POSIX only.

- ✅ Another program could empty the trash with no question, once "Ask before deleting outright or emptying the Trash" was off.
	- Opened: 20260917-181500
	- Closed: 20260917-182900
	- Found while writing the security part of design.md. `EmptyTrash` on the bus went through the same path as the command in a window, so it took the person's preference as its own. Every other trash or delete asks regardless when nobody at a window asked for it. It also left no line in the log.
	- Masked for now, since the test guard is armed in every build and asks about every delete. It would have shown once that goes back to 0.
	- Fixed: only the Empty Trash command in a window may skip the question, whether from the menu, the trash bar or the sidebar. The question says when a request came from somewhere else. Every empty trash writes a log line saying which it was. A lint rule keeps the bus handler off anything that counts as a person asking.
	- Later the same day the bus method was removed outright, with the rest of that interface. See the item above.
	- Test case: `test-nemo-empty-trash`, `fCheckBusMethods` in the C lint.

- ✅ Preferences|Views: with "Remember per-folder settings" off, the Current tab still opens on a dead page.
	- Opened: 20260917-233000
	- Closed: 20260917-234500
	- Graying the page out is not enough. GTK switches the page on a click whatever the page's own state is, so the tab took the click and showed an empty gray pane.
	- Fixed: the tab label is greyed with the setting, and the notebook refuses the switch, by mouse or by keyboard. Turning the setting off while the Current tab is up drops back to Default.
	- Test case: `test-nemo-prefs-current`.

- ✅ Unselected tabs run together, so one cannot be told from the next.
	- Opened: 20260917-233000
	- Closed: 20260917-234500
	- Most themes draw an unselected tab with no edge of any kind, so three open tabs read as one strip broken only by the close buttons. Nemo has the same problem.
	- Fixed: a divider between any two adjacent tabs that are both unselected. It rides with the rest of the app styling, so it holds whatever theme is in use.
	- Test case: none, look only, judged by eye.

- ✅ Shift+Tab sometimes does not leave a notebook page.
	- Opened: 20260917-143946
	- Closed: 20260917-150800
	- It was the test, not the guard. Nothing in the app was ever wrong and nothing in it changed.
	- The check needs the test window to hold the keyboard. When another window has it, GTK re-grabs the widget that already had the focus and calls that a move, so the traversal stops there and the check reads as a failure. No key press can reach that state, since Shift+Tab goes to whichever window does hold the keyboard.
	- The earlier guess about the notebook page switch was wrong. The page, its size and the tab order are all in order at the moment of the check.
	- Fixed: the test asks for the keyboard before the check and skips rather than fails if it cannot have it. Eight copies on one display used to fail about one run in thirteen; 320 runs are now clean.
	- The same ask fixed a quieter problem. With no window manager, which is how the suite runs, the test was skipping three runs in four while the suite still read as a pass, because its one focus request went out before the window was on screen.
	- Test case: none, a fix to the test itself.

- ✅ A split archive that fits in one volume is still named ".001".
	- Opened: 20260917-213000
	- Closed: 20260917-220000
	- 7z numbers every volume it writes, the only one included, so splitting something small produced "name.7z.001" rather than "name.7z". rar does the same in its own spelling.
	- Fixed: a split that came out as one volume is renamed back to the name that was asked for. A real split keeps its numbering, and a run that failed now clears its volumes instead of leaving them.
	- Test case: `test-nemo-archive` (`check_volume_collapse`).

- ✅ Extract is not offered on a split archive, since ".001" is not a suffix anything recognized.
	- Opened: 20260917-213000
	- Closed: 20260917-220000
	- Fixed: a three-digit volume number is read past, so the format underneath decides as usual and the folder name comes out the same for every part. Selecting several parts unpacks once, from the first volume, whichever one was clicked.
	- Test case: `test-nemo-extract`, `test-extract-job` (`check_split_volumes`), which skips without 7z.

- ✅ Archive options: the default volume size is small, and opening Options walks the dialog down the screen.
	- Opened: 20260917-213000
	- Closed: 20260917-220000
	- Fixed: the default volume size is 2 GiB. The dialog keeps its position on screen when Options opens or closes, and stays inside the work area.
	- The size list is in binary units now, which is what the numbers always meant, and "GiB" can be typed as well as "GB".
	- Test case: `test-nemo-archive`, `test-nemo-archive-settings` for the sizes; the dialog position needs a window manager.

- ✅ Preferences|Views: the Forget and Copy settings buttons sit in the tab header, and the tabs crowd the checkbox above them.
	- Opened: 20260917-210000
	- Closed: 20260917-213000
	- Fixed: each tab now carries its own buttons at the top right of its content, with margins. Default has Copy settings to Current; Current has Forget and Copy settings to Default beside the folder path. The label no longer changes with the tab, and Forget no longer appears and disappears. More room between Inherit view settings and the tabs.
	- Test case: none, layout only, judged by eye.

- ✅ The delete test guard never fires on a move, so nothing asks about the original that leaves or the target that gets written over.
	- Opened: 20260917-200000
	- Closed: 20260917-204500
	- A move on one filesystem is a single `g_file_move`, and an overwrite happens inside glib, so neither reaches the delete path the guard sits on.
	- Fixed: a move job asks once up front, naming the destination and every source that is about to leave where it is. An overwrite asks per file, naming the target whose contents are lost and the source replacing it. Both were watched on screen, with Cancel leaving the files alone.
	- `lint-c.bash` holds the rule that `G_FILE_COPY_OVERWRITE` cannot be set without the ask above it.
	- Test case: `test-nemo-drop-cancel` (`check_move`), `fCheckOverwriteAsk` in the C lint.

- ✅ `make_link_copy` deletes without going through the delete guard.
	- Opened: 20260917-190000
	- Closed: 20260917-193000
	- The two `g_file_delete` calls in it, one for an overwritten destination and one for a moved-from link, skipped `file_delete_wrapper` and so never reached `nemo_delete_guard_check`. Every other delete in `nemo-file-operations.c` was already guarded. Found while wiring the delete test guard.
	- Both go through the wrapper now. `lint-c.bash` holds the rule.
	- Test case: `fCheckDeleteWrapper` in the C lint.

- ✅ Turning hidden files off leaves a "Loading..." row under an open tree folder that holds only hidden folders.
	- Opened: 20260917-060256
	- Closed: 20260917-072556
	- The folder closes when its last row goes, so nothing loads it again, and it keeps an expander it should not have.
	- Found while testing the folders-only tree.
	- One fix was tried and did not take. The cause is known; detail is in the private notes.
	- Fixed: when hiding takes a folder's last sub-folder, the folder is now checked right then and loses its expander. The check in `test-nemo-tree-folders` is switched back on.
	- Test case: `test-nemo-tree-folders`.

- ✅ The tree pane keeps its width when the window is resized, instead of sharing the change in proportion.
	- Opened: 20260916-210500
	- Closed: 20260916-193258
	- Places is right - it holds the width it was given. The content pane then absorbs the whole change on its own, so at 800px wide it is a 60px sliver beside a 500px tree.
	- Measured by taking one window 1278 -> 1500 -> 800 wide: `sidebar-width` stays 240, and `sidebar-tree-width` reads 480, then 498.
	- The arithmetic is not the suspect. `test-nemo-pane-layout` covers it and passes. Either the position never reaches the widget, or something puts it back afterwards.
	- Tried and rejected: giving the tree the same `set_size_request` floor the places pane carries, on the theory that `shrink=FALSE` was clamping the divider to the tree's natural width. It made no difference, so it was taken back out rather than left in on a guess.
	- Everything else on "Places and TreeView can both exist at the same time" works. This is the part left.
	- Fixed: the position was set after GTK had already laid out the panes, so it never took. The divider is now set before the layout, and measured from where it was last placed, so a slow drag of the window edge moves it too. The split view divider gets the same treatment.
	- Test case: `test-nemo-proportional-paned`, `test-nemo-pane-layout`.

- ✅ On Windows the delete guard did not know home by its short 8.3 name, or by a path through a junction.
	- Opened: 20260915-160031
	- Closed: 20260915-161437
	- The folder removal behind the guard also went into junctions, so clearing an extract's staging folder could delete what a junction inside it pointed at.
	- Home and the folders above it are matched by file identity now, as they already were on Linux. A junction counts as a link, and is never walked.
	- Test case: `test-nemo-delete-guard-win32`, Windows only.

- ✅ "Focus" can never be on a column, nor a tab.
	- Opened: 20260914-173549
	- Closed: 20260915-131006
	- If focus would have fallen to a column or tab (e.g. as a result of editable path turning into breadcrumb), move it to the main file/folder interface instead.
	- Clicking a column heading left the keyboard on the heading, and clicking a tab left it on the tab. Closing the path entry left it on a path button. The arrow keys then did nothing to the file list.
	- Headings and tabs never take the keyboard now. A click on either puts it in the file list, and so does closing the path entry.
	- Tab and Shift+Tab go through the tab strip without stopping on it.
	- Test case: `test-eel-focus-guard`.

- ✅ A home folder was deleted again, with no dialog, soon after a copy of the app was opened by accident. The guards added after the first time were not enough.
	- Opened: 20260914-110000
	- Closed: 20260914-121200
	- Nothing in the window was used but About. What removed the files is not known. The app's own log, which could have said, was in the home folder that went.
	- Home, any folder above it, and a folder where a drive or share is mounted are never removed now, however the job came about. A delete that reaches a mount on its way through a folder stops there.
	- One job can no longer take most of what sits directly in home. That is refused outright rather than asked about.
	- Every question that can remove files starts on Cancel.
	- A delete key within a second of a window coming up or taking focus is ignored, since that is typing meant for another window.
	- Each trash or delete line also goes to the system journal, which a rollback of home leaves alone.
	- Test case: `test-nemo-delete-guard` (POSIX only), `test-nemo-delete-guard-win32` (Windows only), and `test-nemo-link-delete-job` in its `delete-asked` mode for Cancel first; the journal line is not checked.

- ✅ Extracting could delete what a link inside the archive pointed at.
	- Opened: n/a
	- Closed: 20260914-121200
	- Clearing the folder it extracts into, or a file being replaced, followed a link to a folder and emptied the folder at the other end. It asked nothing and logged nothing, so an archive holding a link to home could have taken home. A link is removed as a link now.
	- Test case: `test-nemo-delete-guard` and `fCheckTreeWalks` in the C lint.

- ✅ The unattended-delete guard reads GTK's current event, so a delete started from inside an unrelated event handler is recorded as one a person asked for.
	- Opened: 20260914-102940
	- Closed: 20260914-121200
	- The caller says so now. Only the trash and delete commands in a window count as asked for, and undo, drops and anything else always ask. The event is kept for the log.
	- Test case: `fCheckByUser` in the C lint, `test-nemo-delete-guard`, `test-nemo-empty-trash`.

- ✅ Windows: When CTRL+L to the editable current path, CTRL+C doesn't copy the path to the clipboard (right-clicking the selected text and picking Copy does), and the context menu key does nothing on selected text.
	- Opened: 20260903-130431
	- Closed: 20260909-104658
	- Fixed 20260905, the copy half: an entry's own cut and copy only advertised the text, the way the toolkit always has, which is the same write that went missing for "Copy path" and file copy. The selected text is now written out as well, for every entry and text box. The regression check goes red with the fix backed out.
	- The key half works on Windows: with the caret in the entry, with a word selected, and with the whole path selected. The menu that comes up is usable rather than merely present - Select All picked out of it selects the path.
	- No cause was found for the key half, because it does not reproduce. The menu itself belongs to the toolkit; the only code of ours on the way to it is the entry's key handler, which passes the key through untouched.
	- A regression check presses the menu key with the caret, with a word selected, and with the whole path selected, and fails if no menu arrives inside five seconds or if the menu that arrives has not noticed the selection. It goes red on the reported shape - the key swallowed only while something is selected. It needs a keymap that carries a menu key, and reports itself skipped where there is none.
	- Test case: `test-nemo-clipboard-win32` for the copy (Windows only), `test-nemo-entry-menu` for the menu key.

- ✅ When launching fresh on 'C:\opt\0-0\users\collierjr\0_links' in Windows, the view cannot be changed from list to icon (or compact) view. If you change folders, then the view can be changed.
	- Opened: 20260908-011500
	- Closed: 20260908-042000
	- Cause, measured on the box: a view swap waits for the new view to report it has started loading, and that waits on a question asked of every file already listed - its info, its mount and its filesystem info. At startup the folder is still empty, so the question is answered at once and the first view gets through. By the time the view button is pressed the folder is full of links, and a link to a share that is not answering costs about twenty seconds. A folder of them takes minutes.
	- Fixed: a link to a share is no longer asked for filesystem info, the same way it was already left out of the mount question. Windows only; nothing on another platform changes, since a share there is a mount rather than a native path.
	- Measured both ways on a folder holding one link to a share that is not there: twenty-one seconds without the fix, none with it. That is the regression check, and it needs an address on the local subnet handed to it, since a name that resolves nowhere fails at once and proves nothing.
	- Explains the rest of the report too: the answer is kept on each file once it arrives, so leaving the folder and coming back makes the swap instant.
	- Also added: setting `NEMO_DEBUG_IO` logs which question the file-loading queue is waiting on and for how long. The calls are asynchronous, so timing the call itself shows nothing - the wait is in the answer, and that has now had to be worked out from scratch three times.
	- Test case: `fCheckShareGates` in the C lint and `test-nemo-share-win32` (Windows only); the dead-share timing in `test-nemo-metadata-ready-win32` runs only with `NEMO_PROBE_DEAD_SHARE` set.

- ✅ The action layout editor does not run.
	- Opened: 20260908-000856
	- Closed: 20260908-004448
	- Reachable from Preferences > Actions, which spawns it, but it dies at startup: its paths were baked in at configure time and point at an install prefix a portable copy never has.
	- Also still reads and writes the pre-fork `nemo` config and data directories, so even once it starts, the app would not see what it saved.
	- Fixed: it resolves its own prefix, uses the fork's config and data directories, and no longer needs the two Cinnamon libraries it imported. It comes up, lists the shipped actions, reorders them and saves where the action manager reads.
	- The enable/disable checkboxes went with it. They read a GSettings key that no longer exists, and they duplicated Preferences > Actions, which already does the job. A switched-off action still shows grayed out here, read out of the config file.
	- Not part of the Windows build: a /bin/sh launcher and a PyGObject script. The button that starts it is hidden there.
	- Test case: `cicd/linux/test-prefix.bash`; the editor window itself is not driven.

- ✅ Bottom scrollbar: missing when the view is tiny, still there after a resize when nothing overflows, flashing at every step of a resize, and now and then strobing along with the vertical one.
	- Opened: 20260906-110200
	- Closed: 20260907-195000
	- Severe visual bugs, possibly related:
		- Doesn't appear when needed, e.g. when view is tiny.
		- Sometimes appears when not needed, e.g. after resize.
		- Sometimes strobes at a high Hz (~10-30 or so) when visible. Hard to reproduce.
			- And when so, the vertical scrollbar, if present, also strobes but not as visibly.
		- Appears while resizing, even when not needed.
	- Improvement:
		- Don't shrink filename column in list or search views, below a threshold defined below; instead, show horizontal scrollbar to be able to see rightmost columns.
			- Min width: Wide enough for all but the 10% widest outliers in the list. (Make this a config file tunable.)
				- Including adjustments each time a subfolder is expanded.
			- Max width: Wide enough for all names, plus nice visual padding on the right.
		- Apply the same logic to all the "Type" and other variable-width columns, except first organize the calculation list into unique types, each unique type only counts once.
			- That's the default width if constrained. But if there's enough space for everything to fit, show full-width.
			- Max width: Wide enough for all non-name variable-width columns, plus nice visual padding on the right.
			- The difference with the other "type" columns vs other variable-width columns, is that it has a "min" width that's different than the default. If things start getting constrained, allow "type" columns to shrink - proportional to all "type" column default sizes, to a Min width of 2x the default "Ext" column width.
		- Change to all quasi-fixed-width columns:
			- Don't shrink below displayable width. Rely on horizontal scrollbar instead.
		- These directives override ALL previous decisions, and multiple changes, about column widths.
	- Cause of the flashing: the columns were laid out after the tree view had drawn at the new width, so each step of a resize showed one frame at the old widths. The overlay scrollbar's margin was also set on every allocation, and setting it asks for another.
	- Fixed: the columns are laid out for the width the view is about to get, before the tree view sees it, and the margin only moves when it has to. Widths follow the rule above; the share is `column-fit-percent` under list-view, default 90. Collapsing a subfolder gives back the width its rows asked for.
	- Note: This contradicts the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: `test-nemo-column-layout`, `fCheckMarginGuard` and `fCheckColumnExpand` in the C lint; the flash itself is judged by eye.

- ✅ A running copy on Linux moved everything under the home folder to the trash, with nobody asking it to. The mounts under it cannot be trashed, so the "delete immediately?" question came up for each of those, and they went for good.
	- Opened: 20260906-110342
	- Closed: 20260907-190000
	- Cause: not found. The app wrote nothing about a trash or delete job, and the rollback that brought the home folder back took its own log and every other record under home with it. Every trash path in the code starts from a selection, a drop, an undo or a menu; none runs on its own.
	- Fixed what can be. Every trash and delete job now logs its count, folder, first item, window and the key, click or drop that asked for it. A job with no input event behind it (another program, another copy, a timer) always asks first, whatever the preference says, and the question says where it came from. A job of `confirm-many-items` or more (20 by default, 0 turns it off) asks even with confirmation off.
	- The confirmation dialogs keep their usual default button. The dialog itself is the pause.
	- Note: superseded 20260914. Every question that can remove files now starts on Cancel. See [20260930-150859_delete_guard.md](design_docs/20260930-150859_delete_guard.md).
	- Test case: `test-nemo-delete-guard` (`nemo_delete_guard_must_ask`), `fCheckJobCallers` and `fCheckBusMethods` in the C lint; the log line is not checked.

- ✅ Plugins are duplicated.
	- Opened: 20260905-112901
	- Closed: 20260905-184500
	- Cause: the same share folder reaches the data-dir list more than once. The prefix wrapper puts it on when a launcher already has, and on Windows GLib adds the exe's own share folder on top of the one in the environment. Every action file was then found once per copy.
	- Fixed: the list is read through one place that drops repeats, and every scan that walks it (actions, search helpers, themes, thumbnailers) uses that. The wrapper also no longer adds a folder that is already there.
	- The regression check feeds a list full of repeats and expects one of each, in order. It goes red with the fix backed out.
	- Test case: `test-nemo-data-dirs`.

- ✅ Windows: listing a drive root logs a batch of "GFileInfo created without standard::type" criticals.
	- Opened: 20260902-190000
	- Closed: 20260902-191500
	- Cause: Windows will not stat a few of the files at the root of a drive - the page and swap files - so their entry comes back with no type on it at all. GLib now complains rather than answering when something asks for a missing attribute, and five places asked.
	- Fixed: the type is read through one place that answers "unknown" for an entry that has none. The listing looks the same as before; the noise is gone.
	- The regression check lists the drive root and fails on anything logged at warning level or worse. It goes red with the fix backed out.
	- Test case: `test-nemo-directory-load-clean`, whose drive root case is Windows only.

- ✅ Windows: file copy and paste to another program fails the same way "Copy path" did, and for the same reason.
	- Opened: 20260830-153000
	- Closed: 20260831-081500
	- Found: worse than reported. Over a remote desktop session a copy in nemo did nothing at all - not just for other programs, but for nemo's own paste. The toolkit only advertises a file cut or copy and hands it over when asked; the redirector asks the moment the clipboard changes, does not get an answer in time, and puts the client's own clipboard back.
	- Found as well: even with no remote session in the way, nemo published nothing Windows understands, so Explorer could never have pasted a copy made in nemo. Nor the other way round - a copy made anywhere else offered nothing nemo was looking for, so Paste did nothing.
	- Fixed: a file cut or copy is now written out up front in the formats Windows expects, alongside nemo's own. Paste falls back to the Windows one when nemo's is absent, so a copy made in any program can be pasted, and a cut from one moves rather than copies.
	- Fixed as well: only one program can have the clipboard open at a time, and on a busy machine something usually does for a moment. The call was failing outright every few tries, which read as an empty clipboard. Every use retries now. This affected the text copy too.
	- Proved on a live remote session: a copy in nemo pasted into Explorer, a copy in Explorer pasted into nemo, a cut from either moving rather than copying, and the clipboard emptied after a cut is pasted. The regression check goes red with the fix backed out.
	- Test case: `test-nemo-clipboard-win32` (`check_files`), Windows only.

- ✅ Windows network browsing cannot be proved to report a missing network or a refused share.
	- Opened: 20260804-230307
	- Closed: 20260831-071500
	- Fixed so far: the address building and the not-found answer, both verified against real shares. From code review 20260804.
	- Found: the missing-network half never worked. Asking Windows to list the neighborhood on a machine with no network at all succeeds and hands back an empty list, so nemo showed a blank folder that looked like it had loaded. The branch meant to catch it could not fire.
	- Fixed: when the list comes back empty, the machine is asked directly whether it has a network, and "The network is unavailable" is shown when it says no. A list with something in it is left alone - a local provider can offer entries with no network, and the remote desktop channel does.
	- Also fixed: the browse and the lookup used to word the same failure differently, so a refused share came back reading as a missing one. One place decides now, and an answer nobody wrote a case for keeps the system's own words instead of being reworded.
	- Proved on a throwaway machine with its network switched off: with the remote desktop provider present the one entry is listed and nothing is claimed; with it taken out of the order, so there really is nothing, the message appears. The same check goes red on that machine with the fix backed out.
	- Also covered: every failure code's wording, and a server name that cannot exist, which is refused in about a second rather than opening as an empty folder.
	- Test case: `test-nemo-network-win32`, Windows only.

- ✅ Windows: opening a file from the released build breaks the program it opens in, unless that program is already running.
	- Opened: 20260830-141048
	- Closed: 20260830-214500
	- Reported against a symlink and VSCodium, which said "The window terminated unexpectedly". The link turned out to have nothing to do with it, and neither did the file: a plain text file does the same.
	- Cause: the single-exe packer is set to share its virtual file system with child processes, so every program opened from nemo starts with the packer's hooks inside it. A program that runs its own sandboxed child processes - anything built on Chromium, which is a lot of desktop software now - cannot start those, and reports a crash. It only shows on a cold start because a second copy of such a program hands the file to the one already running and exits before it gets that far.
	- Reproduced and controlled: a bare test program packed the same way breaks the editor every time; the identical program packed with sharing off opens it every time. The unpacked build is fine, and so is the same launch made by hand.
	- Note: sharing cannot simply be turned off. Nemo's own helpers - the document converters, the thumbnailers and two toolkit helpers - live inside that virtual file system and need it to find their libraries. The fix has to separate "our own helper" from "somebody else's program".
	- Note: a small launcher of our own does not separate them. The hooks follow the whole process tree, not just the first step - measured: a plain helper started by the packed build reports itself hooked, and so does everything it starts. Where the helper sits on disk makes no difference, and neither does building it for the other architecture. Breaking the chain needs the program to be started by something outside our own process tree.
	- Note: no launch flag or shell indirection helps either. Detaching the child, putting a hidden command prompt in the middle, `start /b` behind that, and the shell's own open verb all leave the program hooked. Only a broker outside our own process tree comes out clean.
	- Fixed: a program is now started by one of two brokers rather than by us. The desktop shell is asked first, since it carries arguments, brings the new window forward and is the ordinary way a file gets opened. When it will not do it - an elevated session refuses the call, and there may be no shell running at all - the system's management service does it instead, which keeps the caller's rights but leaves the window behind. A plain start of our own sits behind both, so a launch can still happen on a box where neither broker answers.
	- Also fixed: a file that is not there is refused before the shell is asked. The shell answers a missing file with a message box of its own and does not return until it is dismissed, which would have held nemo's own thread.
	- Measured, packed and unpacked: the six ways of starting a program ourselves all come out hooked, both brokers come out clean, and opening a file from the packed build in a throwaway machine starts the program with the hooks absent.
	- Unpacking to a real folder instead was considered and dropped - it breaks the dogfood launcher's one-file-per-build pool, and it swaps one thing security software dislikes for another.
	- Left open: the slow cold start, which belongs to the packer and is unaffected by any of this.
	- Test case: `fCheckWinLaunch` in the C lint and `test-nemo-launch-win32` (Windows only; the brokers run only with `NEMO_PROBE_LAUNCH` set).

- ✅ Windows: the released build cannot open a file whose program is 32-bit. Nothing happens, and nothing is reported.
	- Opened: 20260830-161500
	- Closed: 20260830-214500
	- Cause: the single-exe packer is set to leave programs of the other architecture alone, and in practice it stops them starting rather than letting them run unhooked. The call reports success, so nemo has nothing to report either.
	- Measured: a 32-bit program started from a packed build never runs; the same command by hand runs fine. Allowing the other architecture does let it start, but then it carries the packer's hooks like everything else.
	- Fixed by the item above: neither broker is subject to the packer's architecture setting, so a 32-bit program starts and runs unhooked.
	- Test case: `fCheckWinLaunch` in the C lint and `test-nemo-launch-win32`, Windows only; a 32-bit program under the packed exe is not started.

- ✅ Windows: a link pointing at a folder was drawn with a file icon instead of a folder icon.
	- Opened: 20260830-141048
	- Closed: 20260830-150000
	- Cause: Windows reports no type at all for a link the listing does not follow, so the toolkit handed back its plain file icon. The folder icon comes off the type, so a folder link got the document one. Both a directory symlink and a junction were affected.
	- Fixed: a link that is a folder is given the folder icon whatever the type came back as.
	- Note: a new check pins the missing-type behavior the swap exists for, and holds a folder link to the folder icon.
	- Test case: `test-nemo-link-info-win32`, Windows only.

- ✅ "Copy path as" left the clipboard holding whatever was in it before, instead of the path.
	- Opened: 20260830-141048
	- Closed: 20260830-152000
	- Cause: the toolkit only advertises text on the Windows clipboard and hands it over when somebody asks for it. In a remote desktop session the redirector asks straight away, does not get an answer in time, and puts the client's own clipboard back - so the copy read as having done nothing. It affected the plain "Copy path" item too.
	- Fixed: the text goes onto the clipboard up front, so there is nothing left to ask for.
	- Note: a new check reads the clipboard the way another program would, with no message loop running.
	- Test case: `test-nemo-clipboard-win32`, Windows only.

- ✅ The context-menu key did not stand in for a right-click.
	- Opened: 20260830-141048
	- Closed: 20260830-151000
	- Cause: the key did open the menu, but the menu placed itself at the mouse pointer - which, for a key press, can be anywhere, including another window or another monitor. It read as the key having done nothing.
	- Fixed: a menu asked for from the keyboard sits against whatever holds the focus. Ctrl+F10 was going the same way and now does too.
	- Test case: `test-eel-context-menu`.

- ✅ Windows: a first start with a fresh roaming profile moved the local data folder into the settings folder.
	- Opened: 20260829-081500
	- Closed: 20260829-083500
	- Cause: the move of an old-style settings folder into its roaming home fired on any folder found at the old place. On Windows that place is also where actions, scripts and search helpers are kept, so an ordinary data folder was carried off as if it were old settings.
	- Fixed: only a folder holding a settings file is moved. The data folder stays where it is.
	- Test case: `test-nemo-config-root`.

- ✅ Startup warnings on Windows, and one warning per file in the first listing.
	- Opened: 20260804-133646
	- Closed: 20260829-073319
	- Fixed: the Windows half of the NULL-instance criticals was the missing resource bundle.
	- Fixed: the second signature - `g_file_get_child: assertion 'name != NULL'`, one per file listed. Cause: a file's name is not filled in until late in the same update that first applies its info, and the drive-root naming read it early, so every file in the first listing logged one. It also meant a drive root shown as a child kept the bare separator as its name until something refreshed it.
	- Note: a regression check lists a folder and fails on anything logged at warning level or worse.
	- Note: split from "Startup logs a dozen pairs", which stays open for the Linux host.
	- Test case: `test-nemo-directory-load-clean`.

- ✅ Often when right-clicking on the breadcrumb buttons, the menu closes immediately and has to be right-clicked again.
	- Opened: 20260802-095853
	- Closed: 20260828-164500
	- Fixed: by the path-button menu work. The menu opens inside the press itself now, rather than after an attribute load that could finish late.
	- Verified on Windows: eight right-clicks in a row, the menu up and staying up every time.
	- Test case: `fCheckLocationPopup` in the C lint.

- ✅ Dragging a file towards another application crashed the app, before it had even left the window.
	- Opened: n/a
	- Closed: 20260828-163000
	- Reproduced: nothing to do with the other application. Any drag that passed over the empty space below the last row did it, which a drag out of the window does on its way.
	- Cause: the toolkit is asked which row sits under the pointer. Past the last row it answers "none" without filling in the row it was handed, and that leftover value was then read and released.
	- Fixed: the row is only read when the toolkit really filled it in. A new check asks the same question at a position below the rows.
	- Test case: `test-eel-treeview-hit`.

- ✅ Search doesn't fully work.
	- Opened: 20260826-103001
	- Closed: 20260828-160000
	- Note: three faults, all on Windows. Searching by name already worked, and still does - substring, wildcards, a regex, and the switch that keeps the search out of subfolders.
	- Fixed: "Containing:" found nothing at all, ever. Windows calls the extension the file's type, so the test for "is this text" answered no for every file. It converts first now, and where the extension means nothing to Windows it decides from the first few kilobytes instead. A file with no extension is searched, and a binary one is left alone.
	- Fixed: pressing Enter straight after typing did nothing, and left the box outlined in red. The check that decides whether a search may run at all is on a short delay, and Enter threw it away rather than waiting for it.
	- Fixed: a search with only a "Containing:" pattern and no name crashed outright. Nothing typed in the name box means every name, which is what it now says.
	- Note: left open as its own item - nothing that needs a helper program (documents, spreadsheets, PDFs) can be searched on Windows, because none of the helpers are packaged there.
	- Test case: `test-nemo-search-content`, `test-nemo-query-editor`.

- ✅ The settings schema shipped for `shcl check` is kept in step with the key table in the code by hand, and nothing notices when it drifts.
	- Opened: 20260821-144459
	- Closed: 20260826-180755
	- Cause: two files have to be edited for every new setting. Miss the second and a hand-edited config validates against a schema that does not know the key.
	- Reproduced: turned up while adding two settings at once.
	- Fixed: a test walks both and fails on any name, type, allowed set or default that does not line up. It reads the real key table rather than the source text, so the macro-named keys and the per-platform ones are all covered.
	- Note: it found 13 real mismatches on its first run: 8 settings the schema had never heard of, two archive command lines missing the thread count, the two list-view column lists missing the extension column, and the sidebar width. All corrected.
	- Test case: `test-nemo-config-schema`.

- ✅ A leftover helper from the install folder blocks uninstall and in-place upgrade, and the message blames the app.
	- Opened: 20260818-155550
	- Closed: 20260828-134500
	- Cause: the session bus the app autolaunches lives in the install folder and outlives the window, so the in-use check still sees the folder busy. It says "Nemo Anywhere is still running", which reads as wrong to someone who just closed it.
	- Reproduced: uninstall failed on the installer round trip, then succeeded a few seconds later with nothing else changed.
	- Note: wants either a wait-and-retry, or a message that names what is actually holding the folder.
	- Fixed: both. The installer waits up to ten seconds, says what it is waiting on, and if it gives up names the executables actually holding the folder instead of the app.
	- Verified on Windows against a scratch install, which turned up three more faults, all fixed:
		- An in-place upgrade died outright. The in-use check hands back an empty list as nothing at all, and asking that for a count is an error, so every upgrade over an existing folder failed before it started. Only a first install had ever been run.
		- The check compared two spellings of the same folder and so found nothing. It long-forms the folder it was given but takes a running program's path as reported, and those two do not have to agree.
		- If the swap failed anyway, for a reason no process scan can see, the failure came out as a raw runtime error. It now says which folder is stuck and what to do.
	- Verified: the round trip is clean - install, run, close, upgrade over it, uninstall. The PATH comes back byte for byte and nothing is left behind.
	- Test case: `cicd/win/test-install-holders.ps1`, Windows only.

- ✅ The installer leaves the user PATH very slightly different from how it found it.
	- Opened: 20260818-155550
	- Closed: 20260826-180755
	- Cause: adding then removing the entry also drops a pre-existing trailing separator, so an install/uninstall round trip is not byte-identical. Harmless - an empty trailing entry means nothing - but it is a change nobody asked for.
	- Fixed: both halves carry the trailing separator through, so what an uninstall writes back is what the install found.
	- Verified against an empty PATH, one with a trailing separator and one without.
	- Test case: `cicd/utility/test-install-path.ps1`.

- ✅ Listing a folder whose path is past 260 characters quietly lists a different folder instead - whichever one the program happens to be running from.
	- Opened: 20260821-150232
	- Closed: 20260828-114000
	- Found while proving the long-path manifest work. The toolkit's own directory walk is what breaks; every other call on the same path is right, which is why nothing showed up until a folder that deep was actually opened.
	- In the window it reads as an empty folder, because each name it hands back is then checked against the folder that was asked for and none of them are in it. That is the harmless case. The one to worry about is search, which walks folders itself and would follow the wrong tree.
	- Reproduced three ways: the failing call from two different working directories returns the contents of each in turn, while the platform's own call on the same path returns the right thing.
	- The walk is now done here on Windows once a path is long enough that the toolkit cannot be trusted with it. Everything shorter still goes straight to the toolkit, so the ordinary case is untouched.
	- One entry point covers the lot: the file listing, search, copy, move, delete, the deep count and the archive scan all go through it.
	- A folder 308 characters deep lists its real contents in the window now, with sizes, types and dates. New checks cover it both ways round.
	- Test case: `test-nemo-dir-enum-win32`, Windows only.

- ✅ Switching the path separator to `/` does not take effect until the folder is revisited.
	- Opened: 20260826-103001
	- Closed: 20260828-124500
	- Note: the rest of the Paths group on the Display page applies straight away, so this one is the odd man out.
	- Cause: the title, the location entry and the breadcrumb are only rebuilt on a location or view change, so changing how a path is spelled never asked for one.
	- Fixed: all three now refresh as soon as the setting changes. Verified on Linux against the full-path title, which lags the same way.
	- Two more halves showed up on Windows, where the separator can really change. The breadcrumb was redrawing a step behind - it read the separator before the setting had been taken in. And the sidebar, which spells a drive root as `C:\`, was not redrawing at all.
	- Both fixed. Title, breadcrumb, location bar and sidebar now all move together the moment the setting changes, with no navigation. A new check covers the ordering.
	- Test case: `test-nemo-path-separator-win32` for the order handlers see the change in, Windows only; the refresh of title, breadcrumb and sidebar needs a full window.

- ✅ "Show the full path in the title bar and tab bars" does nothing.
	- Opened: 20260826-103001
	- Closed: 20260826-180755
	- Note: on the Display page, under Windows and Tab Titles. Turning it on leaves the window title and the tabs showing the folder name only.
	- Two causes. The title is only recomputed on a location or view change, so the setting did nothing until the next navigation. And the home folder answered "Home" before the setting was ever read, so in the one place most people would try it, it did nothing at all.
	- Both fixed. The home folder now gives way to the setting, and the title, the tabs and the location widgets all refresh the moment it changes.
	- Test case: `test-nemo-window-title` (`check_full_path_preference`); the live refresh needs a full window.

- ✅ The first folder listed after launch is still slow when the start location is full of links.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- Note: same folder and same symptom as the item below, which fixed only the first of two causes. The listing itself now appears in about three seconds; what came after it took another ~minute.
	- Cause: two things chased each link off the machine, one at a time, with the folder waiting: counting a folder's items, and asking what a share is mounted under. One link to a host that is not answering cost a lot of time per query.
	- Fixed: the preference for item counts already says "local only" by default, but a share is native as far as the toolkit is concerned, so nothing ever held it back. Both questions are now skipped for anything on a share, or any link pointing at one.
	- Fixed: the mount question is skipped outright there. A share is not a mount on Windows, so the answer was never of use.
	- Verified against a host that really was not answering: over a minute before, about three seconds after. The item count for such a folder now reads "--", which is what the preference has always meant.
	- Test case: `fCheckShareGates` in the C lint and `test-nemo-share-win32` (Windows only).

- ✅ The first folder listed after launch takes a very long time when the start location is full of links.
	- Opened: 20260827-183930
	- Closed: 20260827-193152
	- Seen launching straight into a folder of shortcuts and junctions. Folders opened after that are normal, so it is the first listing that pays.
	- Measured: one link pointing at a share that is not answering costs ~20s, and the whole listing waits for it. The folder in question has one, and took over a minute to show anything at all. It is only slow the first time because Windows remembers the failure for a while afterwards.
	- Cause: listing a folder asked for each child's details with links followed, so every reparse point was chased to whatever it pointed at, over the network if that is where it led.
	- Windows puts the directory bit on the link itself, so the type still comes out right without the trip. The listing no longer follows them there, and the same folder now appears in about a second.
	- Trade: a link to a file reports the link's own size rather than the target's, and a link whose target is gone no longer shows as broken until it is opened.
	- The drive-root test was reading the real config while it ran, so it failed on any machine where the forward slash had been chosen. It gets its own throwaway config now.
	- Test case: `test-nemo-share-win32` (dangling link case), Windows only.

- ✅ Deleting to the trash puts the progress popup on top of the confirmation prompt.
	- Opened: 20260827-183930
	- Closed: 20260827-191128
	- Note: the yes/no dialog is behind it, so the delete reads as stuck until the popup is dragged out of the way.
	- Cause: the prompt was the Windows shell's, not ours. GLib's trash call leaves the shell confirmation switched on, so every file was asked about twice and the second dialog was not one we could place.
	- Fixed: a delete goes to the Recycle Bin through the shell directly with the confirmations off, so our own prompt is the only one and nothing covers it. Verified on Windows end to end.
	- Note: the trash test drops its private copy of the same code and calls the shipped one, and its timeout goes to ten minutes - a full recycle bin can take four and a half.
	- Test case: `fCheckWinTrash` in the C lint and `test-nemo-trash-win32` (Windows only).

- ✅ The Win32 argument quoting check depends on what is installed on the machine.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- Cause: it split `wt.exe` and expected the name back unchanged, but a box with Windows Terminal installed resolves it to a full path, so the check failed there and nowhere else.
	- Fixed: it uses a name that cannot be on the path. The case where a program is found is still covered, by the check below it that looks one up first.
	- Test case: none, the fix is to `test-nemo-view-win32` itself.

- ✅ The config schema check goes red on a fresh Windows checkout.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- Cause: git checks the schema out with Windows line endings, and the check split it on newlines only, so every field name carried a stray carriage return and matched nothing. It then reported all 169 settings as missing from the schema.
	- Note: the config parser itself was never affected - it treats a carriage return as whitespace. Only the check's own reader did.
	- Test case: `test-nemo-config-schema`, which also reads a CRLF copy of the schema.

- ✅ In dark mode the breadcrumb bar and the checked view buttons kept a light background, unreadable against everything around them.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Cause: a bundled theme is loaded as a stylesheet of our own, but the theme *name* was left pointing at it. GTK cannot resolve a name it has never seen on disk, falls back to its packaged sheet, and drops the dark half while doing so - so the layer under ours was the light one. Anything our sheet did not itself paint showed it through.
	- Fixed: the name now points at a theme GTK really has, so the base follows light/dark while our sheet sits on top. Verified against both the light and the dark base.
	- Also fixed alongside: choosing a theme that cannot be found left the previous one on screen, so a bad name looked like nothing had happened.
	- Test case: `test-nemo-appearance` (`test_dropin_applied`).

- ✅ The three view buttons at the bottom left drew as broken-image placeholders.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Cause: none of the app's own artwork was in the Windows bundle at all. Only the toolkit's icons were packaged, so every one of our own icon names missed - the location button in the toolbar was the same failure.
	- Fixed: the app's artwork now rides inside the executable, the same way the bundled themes do. Costs no extra files, so nothing is added to startup time, and it works on every platform including a relocated install.
	- Test case: `test-nemo-app-resources` for part of the bundled art; the view button icons themselves are not checked.

- ✅ The theme picker offered "macOS" and "Windows 10" twice in dark mode, and one of each was the light theme.
	- Opened: n/a
	- Closed: 20260819-141014
	- Cause: those two themes ship a dark sheet of their own upstream *and* have a separately drawn dark half that we also bundle, so both halves claimed dark.
	- Fixed: where a light/dark pair is named, the pair wins and the redundant sheet is dropped. A theme that states which modes it suits is no longer second-guessed either, so a hand-dropped theme cannot bring the fault back.
	- Test case: `test-nemo-appearance` (`test_named_pair_listed_once`).

- ✅ On Windows a drive root is named `\` everywhere except the sidebar - the window title reads `\` and the breadcrumb reads `(C:) Windows` while the sidebar has `Windows (C:)`. Seen on this box browsing `C:\`.
	- Opened: 20260818-142740
	- Closed: 20260818-155550
	- The volume-label work only ever covered the sidebar, and it built its own name there. Everywhere else falls back to what Windows reports for a drive root, which is a bare separator.
	- Three different sources were in play: the basename, which is `\` for every drive alike; the volume monitor, which says `(C:) Windows`; and the sidebar's own string.
	- Fixed: a drive root is `C:\` everywhere - title, breadcrumb and sidebar all ask the same helper. The volume label moved to the sidebar tooltip, where it cannot be mistaken for the path.
	- Verified on Windows: a new test covers the naming, including that the first folder inside a drive keeps its own name; and all three places agree.
	- Test case: `test-nemo-drive-root-name`, Windows only.

- ✅ "Set as default" in the Open With tab did nothing on Windows, and said nothing either.
	- Opened: n/a
	- Closed: 20260818-155550
	- Cause: Windows keeps the per-user default behind a hash it will not let a program write, so the call fails outright - and the result was thrown away along with the error.
	- Fixed: the failure is reported. The choice still cannot be made on Windows; the difference is the user is told rather than left thinking it worked.
	- Verified on Windows: the underlying call refuses with "Setting default applications not supported yet". Looking a default up still works, but only by extension - asking by mime type answers nothing.
	- Test case: `test-nemo-associations-win32` (`test_set_default`), Windows only.

- ✅ The action layout editor never opens: the app spawns it as `nemo-action-layout-editor`, but the binary installs under the app slug as `nemo-anywhere-action-layout-editor`. One missed rename from the rebrand.
	- Opened: 20260804-133646
	- Closed: 20260818-103142
	- Fixed: it is spawned under the app slug, out of the folder the app itself was started from, and a failure to start now says so instead of doing nothing.
	- Also found and fixed alongside: the Restart button in extension settings was quitting and starting whichever upstream Nemo happened to be installed, not this app.
	- Test case: `cicd/linux/test-prefix.bash`.

- ✅ The Windows build shipped without its compiled-in resources, so it had no menu bar at all and every `.ui`, `.glade` and `.css` lookup failed.
	- Opened: n/a
	- Closed: 20260818-155550
	- Cause: the resource bundle is attached to the extension library. On Linux that is a shared library and the whole thing loads, so the resources register themselves. On Windows it is a static one, and the linker keeps only the members that resolve a symbol - the resources register from a constructor nothing calls by name, so the object was dropped.
	- Nobody noticed because the app still starts and browses: the missing menu bar reads as a design choice, and the fallout was a wall of criticals that had been written off as noise.
	- Fixed: on Windows the resources go straight into the executable. Linux keeps them in the shared library as before.
	- Verified on Windows: the menu bar is back, and startup criticals went from 40 to 9 - none of the remainder about resources or widgets.
	- Test case: `cicd/utility/check-win-build-flags.bash`, run on every Windows build.

- ✅ The Windows executable was not marked long-path aware, so anything past the old 260-character limit was out of reach even with long paths switched on.
	- Opened: 20260818-142740
	- Closed: 20260826-103001
	- Cause: the exe carried no application manifest, which is where that is declared.
	- Fixed: the manifest arrived with the DPI work. Measured on a 427-character folder holding a 462-character file: without the manifest every call failed outright; with it, reading the file, asking for its details, testing that it exists and walking into the folder all work.
	- Note: listing such a folder is still wrong, and worse than a failure - it is its own bug, still open.
	- Test case: `test-nemo-dir-enum-win32`, which carries the same manifest, and `cicd/utility/check-win-build-flags.bash`; Windows only.

- ✅ Code review 20260815.
	- Opened: 20260815-154746
	- Closed: 20260817-210917
	- Full code, security and performance review of the whole tree, first-party and inherited. Everything below was re-checked before it went in, worst first. Technical detail is kept out of this file. Numbers are continuous and match the private detail notes.
	- ✅ High.
		- ✅ Item 1. Windows trash acts on file paths from the address with no check that they belong to the recycle bin.
			- Cause: delete, move and read take the path straight from a `trash:///` address, so a crafted address can read or permanently delete any file.
			- Cause: the one place that does check compares the text as it was typed, so a `..` inside a bin item's path walks back out of the bin.
			- Fixed: a path from a trash address is resolved to its real form and has to name something the recycle bin actually holds before it is read, moved or deleted.
			- Verified on Windows: an address aimed at a file outside the bin, and one walked back out of the bin, are each refused for delete, move and read, and the file is left where it was.
			- Test case: `test-nemo-trash-win32`, Windows only.

		- ✅ Item 2. Reading dragged icon-list data can walk off the end of the buffer.
			- Cause: one branch of the parser skips the length bookkeeping every other branch does, and its end-of-data guard tests something that can never be empty, so the scan runs past the buffer.
			- Fixed: the length is kept up to date on that branch too, and the guard tests the data rather than the pointer.
			- Note: inherited from upstream. Covered by a new check.
			- Test case: `test-nemo-dnd`, POSIX only.

		- ✅ Item 3. "Open in Terminal" crashes when no known terminal is installed.
			- Cause: with nothing found the command prefix is left empty and used anyway. Likely on a minimal or KDE-only box, which is exactly the de-Cinnamon target.
			- Fixed: with nothing found the caller declines instead of going ahead. Covered by a new check.
			- Test case: `test-eel-terminal`.

		- ✅ Item 4. Freeing an extension column object corrupts the heap.
			- Cause: teardown frees memory the type system owns. Latent only because built columns are kept for the life of the process; any extension that discards one hits it.
			- Fixed: it no longer frees what it does not own. Covered by a new check.
			- Test case: `test-nemo-column`.

		- ✅ Item 5. An unreadable settings file is treated as empty, and a queued save can then erase it.
			- Cause: any read failure - a sync, antivirus or editor lock, or the delete half of someone else's non-atomic save - loads defaults into memory, and a save already queued then writes that near-empty document over the real file.
			- Fixed: a failed read keeps what is already in memory. Only a file that is genuinely absent goes back to defaults. Covered by a new check.
			- Test case: `test-nemo-config` (`test_unreadable_file_kept`).

		- ✅ Item 6. A NUL byte anywhere in the settings file truncates it on the next save.
			- Cause: the file is written by text length, which stops at the first NUL and drops every setting after it. The check that follows the write is fooled the same way, so the loss goes unnoticed.
			- Fixed: the write and the check both count bytes. Covered by a new check.
			- Test case: `test-nemo-config` (`test_nul_survives_save`).

		- ✅ Item 7. The thumbnail enable-check reads the disabled-types list without its lock.
			- Cause: one reader skips the lock the writers and the other reader take, so a settings change on another thread can free the list mid-read.
			- Fixed: that reader takes the lock too, and the inner call now says it expects its caller to hold it.
			- Note: a threading race, so there is no check that would fail reliably.
			- Test case: none, a threading race with no reliable check.

		- ✅ Item 8. Replacing a folder deletes through directory symlinks inside it.
			- Cause: the recursive remove never checks what each child is, so a link to another folder is followed and its contents deleted, outside the folder that was agreed to.
			- Fixed: only real folders are recursed into; everything else is removed as itself.
			- Note: the premise has a check of its own. The code path itself sits behind a modal Replace dialog.
			- Test case: `test-nemo-symlink-recurse` (POSIX only) for the premise, and `fCheckTreeWalks` in the C lint.

		- ✅ Item 9. An invalid filename search pattern crashes the search.
			- Cause: a pattern that fails to compile leaves nothing to match with, but the search runs anyway and then releases an uninitialised result for every file. Reachable by pressing Enter before the typing check catches up.
			- Fixed: a pattern that will not compile matches nothing rather than running on. Covered by a new check.
			- Test case: `test-nemo-search-regex`.

		- ✅ Item 10. Restoring an item from the Windows trash drops its file extension.
			- Cause: the original name is taken from the shell display name, which hides known extensions by default, and that shortened name is what restore writes.
			- Fixed: the real extension is taken from the backing file, so the listed name and the restored name both keep it.
			- Verified on Windows: a recycled file is found under its full name, reports the original location it came from, and restores to it.
			- Note: the cause does not reproduce on Windows 11. With "hide extensions for known file types" switched on, the recycle bin still reports full names, in this app and at any setting. So the repair is kept for older Windows rather than being needed here.
			- Note: corrected while checking - it used to give up on any name containing a dot, so `report.2026.txt` would have been repaired to `report.2026`.
			- Test case: `test-nemo-trash-win32`, Windows only.

		- ✅ Item 11. Opening certain images can crash if the tab is closed first.
			- Cause: the image-viewer sort path reads the tab it came from with no check, and that is cleared when the tab closes mid-open. This is the ordinary double-click-an-image path on Mint-family setups.
			- Fixed: guarded. The image still opens, without the wrap-around through the rest of the folder.
			- Note: an asynchronous path through the interface, so no check of its own.
			- Test case: none, an asynchronous path through a full window.

		- ✅ Item 12. The places sidebar keeps reacting to settings after it is destroyed.
			- Cause: two preference handlers are left connected at teardown, so a later settings change - including a live edit of the settings file - fires on freed memory. Triggered by hiding the sidebar or switching to the tree sidebar.
			- Fixed: both are disconnected at teardown.
			- Test case: `cicd/utility/lint-pref-handlers.py` for a disconnect on the wrong group; a missing disconnect is not caught.

		- ✅ Item 13. New Folder in the tree sidebar aborts the app when creation fails.
			- Cause: the callback ignores the failure and passes nothing on, which aborts. A permission race or a dismissed error dialog triggers it.
			- Fixed: it gives up on failure, the way the twin in the folder view already did.
			- Test case: none, needs the tree sidebar in a full window.

		- ✅ Item 14. Jumping more than one step forward corrupts the history lists.
			- Cause: the transfer loop reads one list but edits the other two, so the back and forward lists end up sharing entries, and a later navigation frees ones still in use.
			- Fixed: the entry is taken off the forward list and put on the back list, mirroring how going back already worked.
			- Test case: none, needs a full window's history.

		- ✅ Item 15. On Windows every file reports as changed on every refresh.
			- Cause: the per-type icon is compared against the plain system icon, which never matches, so each refresh marks the whole folder changed and re-sorts, redraws and re-checks thumbnails, with a registry lookup per file on top.
			- Fixed: the icon is judged on where it ends up rather than mid-update, so a refresh no longer reports every file as changed.
			- Verified on Windows: five refreshes over real files of several types, with everything after the first sighting reporting nothing changed, and a real change still coming through.
			- Test case: `test-nemo-file-win32-churn`, Windows only.

		- ✅ Item 16. Sidebar rebuilds block the whole window on filesystem queries.
			- Cause: free-space and drive-type checks run on the interface thread for every drive and mount, on every rebuild. A slow or hung mount freezes the window, and a mount change is often what triggers the rebuild.
			- Fixed: the free-space answer is cached per sidebar, and the rebuild only ever reads the cache, so it never waits. A missing or stale entry starts a query off the interface thread that fills the cache and asks for one rebuild afterwards. A hung mount leaves one entry pending and blocks nothing.
			- Test case: none, needs a live sidebar and a mount that hangs.
	- ✅ Medium.
		- ✅ Item 17. The code-signing password is passed on the command line, visible to other local processes.
			- Fixed: the certificate is imported and signed by fingerprint, so the password never appears on a command line another process can read.
			- Test case: none, pipeline signing step.

		- ✅ Item 18. The Windows sysroot packages are downloaded and unpacked with no integrity check, and those libraries ship in the release.
			- Cause: neither the database signature nor the per-package checksum is verified, though the checksum sits in data the fetcher already parses.
			- Fixed: every package is checked against the checksum the database already carries, and a mismatch stops the build.
			- Test case: none, pipeline setup; the build stops on a checksum mismatch.

		- ✅ Item 19. A malformed D-Bus Open hint from any local process crashes the running app.
			- Cause: a hint with no `=` in it yields nothing, and that is parsed without a check.
			- Fixed: guarded.
			- Test case: `test-nemo-instances` (Open hints with no `=`), POSIX only.

		- ✅ Item 20. A pathological settings file can kill the app during parse.
			- Cause: the file is read with no size cap, the parser keeps every decoded byte for the document's life, and a failed allocation ends the whole process from inside the library.
			- Fixed: an 8 MiB read cap. An oversized file is refused and what is already in memory is kept. Covered by a new check.
			- Test case: `test-nemo-config` (`test_oversized_file_refused`).

		- ✅ Item 21. In the Windows pipeline, an abort between stash and pop strands the working changes, and a rerun can commit conflict markers.
			- Fixed: a conflicting restore stops and says where the work is and how to get it back, instead of leaving a rerun to commit a half-merged tree.
			- Test case: none, pipeline script.

		- ✅ Item 22. In cicd.bash, a remote-sync stash-pop conflict aborts with no guidance and the stash still held.
			- Note: the natural rerun with sync off then builds and publishes a tree missing the stashed changes.
			- Fixed: same as above - it stops with the stash named and the two ways out spelled out.
			- Test case: none, pipeline script.

		- ✅ Item 23. The version-bump guard blocks the beta-to-final release push.
			- Cause: version sort puts `1.0.0` before `1.0.0-beta2`, the reverse of release order, so cutting final over the current beta fails the guard. That exact transition is next.
			- Fixed: a prerelease is made to sort below its release, the way the packaging script already did it.
			- Test case: `cicd/hooks/test-pre-push.bash`.

		- ✅ Item 24. Accessibility paste reads a freed stack value.
			- Cause: a stack value is handed to a clipboard callback that runs after the function has returned.
			- Fixed: it is allocated to last, and released in the callback.
			- Test case: none, accessibility paste path in inherited code.

		- ✅ Item 25. install.bash deletes the existing install before the replacement is in place.
			- Cause: a cross-filesystem move that fails partway leaves nothing installed, and the temporary copy is then wiped on abort.
			- Fixed: the replacement is staged beside the existing install and swapped in, with a rollback on failure, so the old one is only dropped once the new one is there.
			- Test case: `cicd/linux/test-installers.bash` for install over an existing copy; a failed move across file systems is not forced.

		- ✅ Item 26. install.ps1 can half-delete a running install.
			- Cause: a process whose path cannot be read is treated as not running, so the delete goes ahead against a locked copy and throws partway.
			- Fixed: the same stage-beside-then-swap as the bash installer, and the in-use check reads paths in a way that covers protected and cross-session processes.
			- Note: Windows file-locking edge cases still want the real-Windows pass.
			- Test case: `cicd/win/test-install-holders.ps1`, Windows only.

		- ✅ Item 27. A partial extension crashes every location load.
			- Cause: one provider dispatch skips the guard its siblings have, so an extension that leaves the function unset is called through nothing.
			- Fixed: guarded, the way the column provider already was.
			- Test case: none, needs an extension built to leave the function unset.

		- ✅ Item 28. An action's exec condition decides on an uninitialized value when the spawn fails.
			- Cause: a missing program or a parse error leaves the result unset, so menu visibility is decided by whatever happened to be on the stack.
			- Fixed: the result is seeded, and a failed spawn answers no.
			- Test case: `test-nemo-action-exec`.

		- ✅ Item 29. Actions stored in a path with spaces run the wrong command.
			- Cause: the action directory is put in front of the command unquoted, and the whole thing is then split on whitespace. Normal on Windows, and on Linux homes with spaces.
			- Fixed: the directory and its separator are quoted as one word, which also settles the Windows separator.
			- Test case: `test-nemo-action-exec`.

		- ✅ Item 30. Any drag-and-drop clears a pending cut or copy.
			- Cause: the collision check compares the dragged list against itself, so it always matches and always clears the clipboard.
			- Fixed: it searches the clipboard instead.
			- Test case: `test-nemo-clipboard`.

		- ✅ Item 31. The settings-groups table is read from worker threads and grown on the main thread with no lock.
			- Cause: a lazy insert can grow the table while a worker thread is reading it. A narrow window, but memory-unsafe.
			- Fixed: the lookup and the insert are both under the lock. Announcing a change still happens outside it, so a handler can come back in.
			- Test case: none, a threading race with no reliable check.

		- ✅ Item 32. The favorites change-timer id is touched from worker threads without a lock.
			- Cause: a worker can remove a timer the main thread has already reused, silently killing an unrelated one.
			- Fixed: the timer is taken under the lock that already covers the rest of that structure.
			- Test case: none, a threading race with no reliable check.

		- ✅ Item 33. Two favorites with the same name in same-named parents collide.
			- Cause: disambiguation appends only the parent's name, and the displayed name is the favorite's identity, so an operation on one can hit the other.
			- Fixed: the parent path is used, shortened, with a counter behind it so the name is always unique.
			- Test case: `test-nemo-favorites` (`test_dedup_display_names`).

		- ✅ Item 34. Trashing a file drops favorites of unrelated sibling paths.
			- Cause: the removal matches by raw prefix with no path boundary, so trashing `ab` also drops the favorite for `abc.txt`.
			- Fixed: the match has to end on a separator or be exact.
			- Test case: `test-nemo-uri-under`.

		- ✅ Item 35. The mount lookup matches sibling paths by prefix.
			- Cause: the same missing boundary check, so a path can be matched to the wrong mount and then called local when it is not.
			- Fixed: the same boundary guard.
			- Test case: `test-nemo-uri-under`.

		- ✅ Item 36. Successful direct-save drops are reported as failed.
			- Cause: the success branch repeats the test the fallback branch makes, so it can never run and a saved file is reported as a failed drop.
			- Fixed: it tests for success.
			- Test case: none, an X11 direct-save drop from another program.

		- ✅ Item 37. A failed metadata save is silent and throws away the pending metadata.
			- Cause: the write error is ignored and the data marked saved, so it is never written again and is lost on restart.
			- Fixed: the write is checked, it is only marked saved when it worked, and a failure is reported.
			- Test case: none, needs a write that fails on demand.

		- ✅ Item 38. Large-zoom images render blurry on Windows.
			- Cause: the can-load check misses the content-type conversion the rest of the code does, so the full-resolution path never runs.
			- Fixed: the check converts the type first, so the full-resolution path runs.
			- Verified on Windows: real images are accepted by the internal-thumbnail check and text is still refused.
			- Note: the stored type for a `.png` on Windows really is ".png", which is why the conversion is needed at all.
			- Test case: `test-nemo-thumbnail-win32`, Windows only.

		- ✅ Item 39. A trashed folder whose status can't be read is shown as a healthy file.
			- Cause: the fallback invents a regular-file entry without looking at the error, and an item deleted behind the app's back still lists as existing until the next full refresh.
			- Fixed: a folder is shown as a folder, and something that has gone is no longer presented as readable.
			- Verified on Windows: an item removed from the bin behind the backend's back comes back saying outright that it cannot be read.
			- Note: the folder half is covered only by a live trashed folder listing as a folder. Forcing a folder that is present but unreadable was not attempted.
			- Test case: `test-nemo-trash-win32` for an item gone from the bin, Windows only; a folder present but unreadable is not forced.

		- ✅ Item 40. Freshly trashed items get a wrong parent until the next poll.
			- Cause: the top-level check does not refresh on a miss, unlike the sibling lookup, so an item not yet seen is filed under a parent that is not in the bin at all.
			- Fixed: the top-level check refreshes on a miss, like the sibling lookup.
			- Verified on Windows: a freshly recycled file reports the bin root as its parent.
			- Test case: `test-nemo-trash-win32`, Windows only.

		- ✅ Item 41. The bookmarks window's no-selection guard never fires and can abort.
			- Cause: an unsigned row number holds what should be a -1, so the guard is dead and an assert or a wrapped index is reachable.
			- Fixed: the row number is signed, so the guard works.
			- Test case: none, bookmarks window path in a full window.

		- ✅ Item 42. A failed or empty drop on the .desktop launcher editor crashes.
			- Cause: both drag handlers split the data and read the first piece with no length check.
			- Fixed: an empty or failed drop is guarded in both.
			- Test case: none, a drop onto the launcher editor.

		- ✅ Item 43. Rename-pending activation relies on a garbage return value and leaks the selection each tick.
			- Cause: a function that returns nothing is installed as a repeating timer, and the still-renaming early return does not free the selection it fetched.
			- Fixed: a proper wrapper decides whether to repeat, and the selection is freed on that path.
			- Test case: none, list view rename timer in a full window.

		- ✅ Item 44. Two invalid search patterns warn fatally and show the wrong message.
			- Cause: the content check is handed an error the filename check already set.
			- Fixed: the error is cleared between the two.
			- Test case: none, two invalid patterns in the search box are not checked.

		- ✅ Item 45. Tree-sidebar Paste races a freed file and holds a stale view pointer.
			- Cause: the clipboard request keeps no hold on the view, and an idle can free the target first. Paste from another program degrades to nothing, and a closed sidebar leaves a dangling pointer.
			- Fixed: the view is held for the length of the request, and the reply guards against the target having gone.
			- Test case: none, tree sidebar paste in a full window.

		- ✅ Item 46. The script debug log reads a path after freeing it.
			- Cause: the path is freed just before the line that prints it. Fires with the folder-view debug output turned on.
			- Fixed: it is freed after.
			- Test case: none, debug output only.

		- ✅ Item 47. The failed-home fallback reopens the failing location instead of root.
			- Cause: the root fallback is built and never used, so an unreadable home retries itself in a loop. The hardcoded root also resolves to the current drive on Windows.
			- Fixed: it opens root, so an undisplayable home stops retrying.
			- Test case: none, needs a home that cannot be shown, in a full window.

		- ✅ Item 48. The Windows trash test writes past a buffer.
			- Cause: a 64-bit length is written through a 32-bit pointer, so half of it is stack garbage that then sizes and indexes a buffer.
			- Fixed: the length is taken at the right width.
			- Note: the trash test used to report itself skipped on this box whatever it had done. It works the recycle bin directly now and reports a real result, so this code runs natively on every run.
			- Test case: none, the fix is to `test-nemo-trash-win32` itself.

		- ✅ Item 49. The dogfood launcher mangles pass-through arguments containing quotes or trailing backslashes.
			- Cause: the launcher joins its arguments with a plain space and the target splits them again, so quotes, backslashes and even plainly spaced arguments were lost.
			- Fixed: every argument is quoted the way the Windows runtime expects, and the shell round trip passes them through untouched.
			- Verified on Linux: arguments carrying spaces, quotes and a trailing backslash all arrive as written.
			- Test case: none, dogfood launcher only.

		- ✅ Item 50. Typing a UNC path blocks the whole window on a network probe.
			- Cause: the backslash-to-slash retry does its existence checks on the interface thread, so an unreachable host stalls for the whole network timeout before the location even opens.
			- Fixed: input that is structurally a `\\host\share` skips the check and goes straight to the asynchronous load.
			- Test case: none, needs a host that does not answer.

		- ✅ Item 51. Failed thumbnails are re-decoded on every icon fetch.
			- Cause: the app records a failure under its own name, which the system's failed flag never reads, so every failed file re-reads and re-decodes an image on each fetch. In list view that is once per row per draw.
			- Fixed: the negative answer is cached per file, and cleared when the file changes so a changed file is tried again.
			- Test case: none, needs a count of decodes for a failed thumbnail.

		- ✅ Item 52. Content search buffers whole files into memory with no cap.
			- Cause: each candidate text file is read whole, then copied again to check and strip, so a multi-gigabyte file can freeze the search or exhaust memory.
			- Fixed: the per-file read is capped at 16 MB.
			- Test case: none, needs a file of several gigabytes.

		- ✅ Item 53. The list view rebuilds and rescales each icon on every row draw.
			- Cause: the icon, its emblems and a fresh surface are assembled with no caching, and thumbnails are rescaled every time, so any redraw re-does the work for every visible row.
			- Fixed: the rendered row is cached and reused across draws, and dropped when the file changes. Drag and cut highlighting still draw live.
			- Test case: none, speed only.

		- ✅ Item 54. The list view re-invalidates visible thumbnails on every scroll pause.
			- Cause: an already-loaded flag is read and then ignored, so every visible file's thumbnail and extension details are re-read at each scroll settle.
			- Fixed: the work happens once, when a row first comes into view, matching the icon view's twin.
			- Test case: none, speed only.
	- ✅ Low.
		- Terse by design; file and mechanism are in the private detail notes. All confirmed on read, minor impact or rare paths, mostly inherited.
		- ✅ Item 55. Vendored-theme staging uses a fixed temp path instead of a unique one (symlink race on a shared box).
			- Fixed: staged under a unique temp directory that is cleaned up on exit.
			- Test case: none, pipeline script.

		- ✅ Item 56. One version parser in the push hook lacks the guard the others gained; correct only by token order today.
			- Fixed: the guard is in, so it can no longer match the wrong field on the same line.
			- Test case: none, pipeline hook.

		- ✅ Item 57. The portable packer copies the app folder without recursion, silently dropping any subfolder's contents.
			- Fixed: the copy recurses, so subfolders keep their contents.
			- Test case: none, packer script.

		- ✅ Item 58. The packer passes a single unquoted string as arguments, so an output path with spaces splits.
			- Fixed: the argument is quoted, so a path with spaces stays one argument.
			- Test case: none, packer script.

		- ✅ Item 59. The packer's fixed grace-then-kill can truncate an exe still being written.
			- Fixed: it waits for the output to stop growing rather than a fixed grace.
			- Test case: none, packer script.

		- ✅ Item 60. Hand-supplied negative-offset window geometry is computed off-screen and clamped to the primary monitor.
			- Fixed: a negative position now places the window's far edge that far in from the screen edge, as it is meant to.
			- Test case: none, only reached by hand-typed geometry.

		- ✅ Item 61. Extension menu-item setters ref a null value, so a nullable field can't be cleared and an optional widget always warns.
			- Fixed: an optional widget can be left unset, and a menu can be cleared.
			- Test case: none, extension API setters with no caller in the tree.

		- ✅ Item 62. The extension property-page dispose never chains up to the parent.
			- Fixed: dispose chains up.
			- Test case: none, extension API with no caller in the tree.

		- ✅ Item 63. The settings flush reads and clears the save-timer id without the lock.
			- Fixed: the timer is taken under the lock.
			- Test case: none, a threading race with no reliable check.

		- ✅ Item 64. A trashed-file timestamp is formatted and parsed with a type that truncates on 64-bit Windows.
			- Fixed: the timestamp is written and read at full width, so the round-trip survives on 64-bit Windows.
			- Verified on Windows: a freshly recycled file reports a deletion date of the right shape and in this century, which a truncated one would not be.
			- Test case: `test-nemo-trash-win32`, Windows only.

		- ✅ Item 65. An unreadable directory records a confirmed-empty file-type list instead of an unknown one.
			- Fixed: an unreadable directory records an unknown type list rather than a confirmed-empty one.
			- Test case: none, needs a folder that fails to list.

		- ✅ Item 66. One removal helper dispatches to the changed path instead of the removed path.
			- Fixed: it dispatches the removal.
			- Test case: none, one caller in inherited code.

		- ✅ Item 67. A file object leaks for each overwritten destination during a move.
			- Fixed: the reference is released.
			- Test case: none, leak only.

		- ✅ Item 68. The drag URI array writes its null terminator one element past the allocation.
			- Fixed: the array is one longer, so the terminator fits inside it.
			- Test case: none, an allocation one short with no visible effect.

		- ✅ Item 69. A failed filesystem query during a desktop drag unrefs a null.
			- Fixed: guarded, and the drag falls back to no filesystem information.
			- Test case: none, needs a failed query during a desktop drag.

		- ✅ Item 70. A missing favorite name aborts the whole favorites listing rather than skipping the entry.
			- Fixed: a missing entry is skipped rather than aborting the whole listing.
			- Test case: `test-nemo-favorites` (`test_enumerator_skips_missing`).

		- ✅ Item 71. Canceling a favorites listing mid-batch leaks the gathered entries.
			- Fixed: the gathered entries are released on cancellation.
			- Test case: none, leak only.

		- ✅ Item 72. An empty favorites metadata entry reads past the split result.
			- Fixed: guarded, so a malformed entry is kept rather than read past.
			- Test case: none, a malformed stored entry in the vfs file.

		- ✅ Item 73. The favorite-info free dereferences the struct before its null guard.
			- Fixed: the guard comes first.
			- Test case: none, null guard only.

		- ✅ Item 74. Skip-all on a delete or directory copy does not mark the file skipped.
			- Fixed: skip-all marks the file skipped, so the folder is not reported as fully removed.
			- Test case: none, needs a dialog answered with Skip All.

		- ✅ Item 75. The read-only-destination path frees a null error.
			- Fixed: it no longer frees an error that was never set.
			- Test case: none, error path only.

		- ✅ Item 76. A D-Bus-initiated copy passes a null desktop location to an equality test.
			- Fixed: guarded.
			- Test case: none, the bus copy it guarded is gone.

		- ✅ Item 77. The existing-ancestor walk unrefs a null for every missing level.
			- Fixed: it no longer releases something it never got.
			- Test case: none, null guard only.

		- ✅ Item 78. A synthesized Windows file info with no icon makes the update ref a null icon.
			- Fixed: guarded, so a synthesized entry with no icon is accepted.
			- Test case: none, null guard only.

		- ✅ Item 79. The job-queue finalize unrefs plain-malloc structs.
			- Fixed: released the way it was allocated.
			- Test case: none, nothing frees these today.

		- ✅ Item 80. The duplicate-job guard compares a function against user data and never fires.
			- Fixed: it compares the right thing, so a repeated job is caught.
			- Test case: `test-nemo-job-queue`.

		- ✅ Item 81. Launching by URI casts a possibly-null parent window for the scale factor.
			- Fixed: guarded, with a sensible default when there is no parent window.
			- Test case: none, null guard only.

		- ✅ Item 82. Skip-folder setup dereferences a null path for a non-native search location.
			- Fixed: guarded, so a location with no path is handled.
			- Test case: none, null guard only.

		- ✅ Item 83. The count-based recycle-bin monitor misses same-count changes.
			- Fixed: the check now also watches total size, so a change that leaves the count the same is noticed.
			- Verified on Windows: swapping one bin item for a much larger one is reported, while a quiet spell is not.
			- Learned here: rewriting a bin item's backing file does not move the reported size - Windows answers with the size recorded when the item was recycled. An item leaving and a differently-sized one arriving does move it, which is the case the fix is for.
			- Test case: `test-nemo-trash-win32` for the monitor, Windows only; the same-count case is not forced.

		- ✅ Item 84. A static global for the connect-server result is clobbered by concurrent dialogs.
			- Fixed: the result travels with the request, so two dialogs at once no longer clobber each other.
			- Test case: none, needs two connect-server dialogs at once.

		- ✅ Item 85. The desktop-item property page leaks the type string for other launcher kinds.
			- Fixed: released.
			- Test case: none, leak only.

		- ✅ Item 86. The list-model drag binder leaks the per-row path string.
			- Fixed: released.
			- Test case: none, leak only.

		- ✅ Item 87. A file-changed emission uses a stale iterator after bumping the model stamp.
			- Fixed: the position is taken again after the model changes.
			- Test case: none, a model stamp detail in inherited code.

		- ✅ Item 88. Column-reorder leaks the column name array in search views.
			- Fixed: the list owns its own copies and every one of them is released.
			- Test case: none, leak only.

		- ✅ Item 89. The unhandled-URI dialog leaks a file reference and tolerates null poorly.
			- Fixed: released, and a file that is not in the cache is handled.
			- Test case: none, leak only.

		- ✅ Item 90. Launch dereferences the command line with no null check.
			- Fixed: guarded, for a program with no command line of its own.
			- Test case: none, null guard only.

		- ✅ Item 91. Activation uses a weak parent-window pointer with no null guard for the screen and dialogs.
			- Fixed: guarded, so activation survives the tab being closed under it.
			- Test case: none, null guard only.

		- ✅ Item 92. The pathbar leaks file objects on rename and at finalize.
			- Fixed: released on rename and at teardown.
			- Test case: none, leak only.

		- ✅ Item 93. An unstored post-drop timeout can fire on a destroyed sidebar.
			- Fixed: the timeout is kept and canceled when the sidebar goes.
			- Test case: none, a timeout on a sidebar being destroyed.

		- ✅ Item 94. Aggregate progress percentage uses a wrong recurrence for three or more concurrent operations.
			- Fixed: a plain average, so three or more operations report correctly.
			- Test case: `test-nemo-job-queue`.

		- ✅ Item 95. The properties window leaks a pending key when one is already pending for the same files.
			- Fixed: released.
			- Test case: none, leak only.

		- ✅ Item 96. The mount-content callback leaks its mount, cancellable and data when content detection is off.
			- Fixed: released when nothing takes them on.
			- Test case: none, leak only.

		- ✅ Item 97. The copy test has no assertions and can pass before the async work appears.
			- Fixed: it builds its own files, copies them, and checks the result.
			- Test case: `test-nemo-copy`.

		- ✅ Item 98. The editable-label test is not wired into any build, so it never runs.
			- Removed: it was an interactive demo with no build wiring behind it.
			- Test case: none, removed test.

		- ✅ Item 99. The config test never makes warnings fatal, so its negative checks cannot fail.
			- Fixed: an unexpected complaint now fails the run.
			- Test case: `test-nemo-config`.

		- ✅ Item 100. The favorites test never removes its temp directories.
			- Fixed: the temp tree is removed.
			- Test case: `test-nemo-favorites`.

		- ✅ Item 102. The row-under-pointer helper leaks a tree path on every call (per drag-motion).
			- Fixed: released.
			- Test case: none, leak only.

		- ✅ Item 103. The extension simple-button leaks a surface and can use an uninitialized size.
			- Fixed: released, and the size is seeded so an unknown icon size cannot be read before it is set.
			- Test case: none, extension API with no caller in the tree.

		- ✅ Item 104. Every settings save leaks a full copy of the file into the parser arena.
			- Fixed: the settings document is rebuilt from its own canonical form when it has handed out enough, so the memory comes back.
			- Test case: none, memory use only.

		- ✅ Item 105. A move leaks the source's parent object on every non-desktop move.
			- Fixed: released.
			- Test case: none, leak only.

		- ✅ Item 107. Thumbnail creation falls back to a synchronous stat on the main thread.
			- Fixed: the fallback lookup happens on the worker instead of the main loop.
			- Test case: none, which thread does the lookup is not checked.

		- ✅ Item 108. Every mouse-motion event rewrites the whole sidebar tree store.
			- Fixed: only rows that actually change are touched.
			- Test case: none, speed only.

- ✅ Code review 20260804.
	- Opened: 20260804-230307
	- Closed: 20260817-210917
	- Full review of everything written or changed since the fork point. Ordered roughly most serious first. Technical detail is kept out of this file.
	- ✅ Item 1. Every dropdown and radio choice in Settings saved the wrong value.
		- Cause: the settings layer stored the choice by number, but the dialog only ever supplied the name, leaving the number at zero. Whatever was picked, the first option was saved.
		- Note: worst case was "Executable text files", where the first option is "run it" - so any visit to that setting quietly armed scripts to run on double click.
		- Fixed: choices are now saved by name. Regression test added, and confirmed to fail before the fix.
		- Test case: `test-nemo-config` (`test_enum_bind_by_nick`).

	- ✅ Item 2. The settings file grew a duplicate comment line on every write.
		- Cause: setting a comment appends a line rather than replacing one, and the comment was re-applied on every save.
		- Note: the window size is saved shortly after every move or resize, so a session of dragging the window added dozens of identical lines, and they survived restarts.
		- Fixed: the comment is written only when a setting first appears in the file.
		- Test case: `test-nemo-config` (`test_comment_written_once`).

	- ✅ Item 3. Hand-editing a setting that was already in the file did nothing until restart.
		- Cause: the live-reload comparison could only see a setting appear or disappear, never change, so nothing was announced to the app.
		- Fixed: the comparison now reads the values themselves.
		- Test case: `test-nemo-config` (`test_external_edit`).

	- ✅ Item 4. "Make Link" on Windows can destroy an existing file, and can crash.
		- Cause: the shortcut is saved over whatever is already there instead of reporting the clash, so the usual "another link to..." renaming never happens.
		- Cause: dropping a link onto a location that is not a real folder returns a failure with no message attached, and reading that message crashes.
		- Fixed: creating a shortcut now refuses to write over anything already at that name and reports the clash, so the existing renaming retry takes over.
		- Fixed: a link dropped somewhere with no real folder behind it now says so instead of failing silently into a crash.
		- Verified: a file sitting at the name a new shortcut would take survives, and the clash is reported.
		- Note: the shortcut test was failing two checks before any of this, on a correct product - it compared a short-form temporary path against the long form the system reports. Fixed alongside.
		- Test case: `test-nemo-shortcut-win32` for the shortcut round trip, Windows only; the clash refusal is not checked.

	- ✅ Item 5. Repairing the thumbnail cache as an administrator can change ownership of unrelated files.
		- Cause: the repair walks symbolic links instead of skipping them, and changes ownership of whatever they point at.
		- Note: the app itself suggests running this with administrator rights, so an unprivileged process could aim it at system files.
		- Fixed: the repair acts on the link itself instead of following it, so a link planted in the cache can no longer hand away the file it points at.
		- Test case: none, the repair runs as root.

	- ✅ Item 6. Favorites can hang the app or read freed memory.
		- Cause: listing favorites can stop advancing and spin on one entry forever, leaking as it goes.
		- Cause: the favorites list is rebuilt without locking while background threads are reading it.
		- Cause: entries are stored with a separator that occurs in ordinary file names, so a file with two colons in its name silently repoints somewhere else.
		- Cause: a blank entry, or one whose target no longer exists, crashes rather than being skipped.
		- Cause: the "is this folder inside that one" test has its two sides swapped, and reads one byte past the end of the text.
		- Fixed: the listing always moves on, and an entry that has gone away is left out instead of ending the whole folder.
		- Fixed: the list has a lock, and the two lookups the background threads use hand back copies rather than pointers into it.
		- Fixed: entries are stored the other way round, mimetype first, which cannot be split in the wrong place. Entries in the old order are still read, and rewritten on the next change.
		- Fixed: blank entries are dropped, and a favorite with no mimetype or an unreachable target still lists and draws.
		- Fixed: the inside-that-one test compares the right way round and stops at the end of the text.
		- Verified: the listing always finishes, concurrent reads are safe, and a missing target is skipped rather than ending the listing.
		- Note: settings written by older versions keep working - only the write order changed, and both are read.
		- Test case: `test-nemo-favorites`.

	- ✅ Item 7. Favorites and thumbnails keep working after the object they belong to is gone.
		- Cause: both release a shared settings object they never owned.
		- Cause: change handlers and a queued callback are left connected at teardown.
		- Fixed: neither releases the shared settings any more - it belongs to the settings store and outlives them both.
		- Fixed: teardown now cancels the queued callback and disconnects the change handlers before anything else goes.
		- Fixed: the favorites file also stopped taking a hold on the settings it never gave back, and three error paths no longer walk away still holding a lock.
		- Verified: the shared settings object outlives both, and a change after teardown reaches nothing.
		- Test case: `test-nemo-favorites` (`test_borrowed_settings_group`, `test_no_callbacks_after_dispose`).

	- ✅ Item 8. A stuck thumbnail helper is never given up on.
		- Cause: there is no time limit on an external thumbnail program, so one hung file permanently costs a worker slot until restart.
		- Cause: a failed reload of the thumbnail helper list reads the entry it just freed.
		- Cause: a very long, very thin image produces no thumbnail and a warning instead of a graceful fallback.
		- Fixed: a helper that has not finished in 30 seconds is stopped, logged and moved on from, so the slot comes back. Thumbnailing on a one-thread machine no longer ends for the session.
		- Fixed: the reload walk stops at the entry it removed instead of stepping off it.
		- Fixed: a thumbnail is never asked for at zero pixels wide or tall, so a 5000x1 image thumbnails instead of failing.
		- Verified: a hung helper gives up its slot inside the time limit, and a very thin image thumbnails. The freed-entry read is invisible at runtime, so it rests on reading the code.
		- Test case: `test-nemo-thumbnail`.

	- ✅ Item 9. Emptying the Windows trash fails whenever it holds a folder.
		- Cause: trashed folders are reported as folders but refuse to list their contents, and the delete path needs to list them.
		- Note: this affects both "Empty Trash" and permanently deleting a single item.
		- Fixed: a trashed folder now goes with everything inside it, so emptying the trash gets through a bin holding folders.
		- Fixed: a trashed folder lists its contents. Permanently deleting one counts what is in it first, and that count used to fail before the delete even started - a second, separate stopping point.
		- Fixed: with that, a trashed folder can be opened and browsed rather than showing an error page. Its contents carry no original location or deletion date of their own, which is correct - only the folder was trashed.
		- Verified on Windows against a real recycled folder. The old failures were "not a directory" on the listing and "directory not empty" on the delete.
		- Note: a link or junction inside a trashed folder is deleted as the link it is, never followed out of the bin.
		- Test case: `test-nemo-trash-win32`, Windows only.

	- ✅ Item 10. Windows trash items can go missing, and restore can aim at the wrong place.
		- Cause: items the shell describes in a form the code does not expect are skipped silently, while the item count still includes them.
		- Cause: a long original location is cut short, and the shortened path is what a restore would use.
		- Note: only reproducible on real Windows. Belongs with the real-Windows validation pass.
		- Fixed: an item the shell describes in an unexpected form is now reported rather than silently dropped, and the original location is read at full length so a restore aims at the right place.
		- Note: written and cross-built here, exercised only under wine. Belongs to the real-Windows validation pass.
		- Test case: `test-nemo-trash-win32`, Windows only; an item in an unexpected form is not forced.

	- ✅ Item 11. The Windows trash monitor can freeze the app.
		- Cause: it announces changes while still holding its own lock, so a listener that closes or opens a trash view deadlocks.
		- Fixed: the announcement is made after the lock is released, so a listener that opens or closes a trash view cannot deadlock it.
		- Note: written and cross-built here, exercised only under wine. Belongs to the real-Windows validation pass.
		- Test case: none, needs a listener that opens a trash view mid-change.

	- ✅ Item 12. Windows network browsing builds wrong addresses and cannot report a failure.
		- Cause: a share's address is joined to its server without a separator, so shares get malformed addresses and two servers can collide.
		- Cause: no network, or access denied, looks exactly like an empty network - no message either way.
		- Cause: any typed network address is presented as a valid empty folder rather than "not found".
		- Cause: nothing limits how deep the enumeration recurses.
		- Fixed: a share's address is joined with a separator, no-network and access-denied are reported instead of reading as an empty folder, an address that cannot be reached comes back as not found, and the enumeration is depth-limited.
		- Verified on Windows: new test covers the address building - a share now sits under its server, and two server/share pairs that used to run together into one address stay apart.
		- Also verified against real shares: this box serves four of its own, and the test now browses them for real - each comes back as a link to its UNC path, and each is opened to prove the link goes somewhere. The one that does not open is an empty optical drive, which the test names rather than counting against the backend.
		- Still open: the no-network and access-denied halves. Both need a machine that fails in those specific ways, which this one does not.
		- Test case: `test-nemo-network-win32`, Windows only.

	- ✅ Item 13. Windows context-menu actions break on ordinary paths.
		- Cause: "Open as Administrator" passes the folder unquoted, so anything with a space arrives as two separate locations.
		- Cause: "Open in Terminal" at a drive root passes a trailing backslash that swallows the closing quote.
		- Fixed: both paths quote properly, so a folder with spaces and a drive root each work.
		- Verified on Windows: drive roots, UNC roots, spaces and embedded quotes all come out right. The two hand-offs themselves are not covered - one raises a UAC prompt and the other opens a console.
		- Test case: `test-nemo-view-win32`, Windows only.

	- ✅ Item 14. Opening a Windows shortcut can truncate its target or hang the app.
		- Cause: targets past the old length limit are silently cut short and then opened, wrongly.
		- Cause: a shortcut pointing at itself, or at a loop of shortcuts, recurses until the app runs out of stack.
		- Fixed: the target is read at full length, a chain of shortcuts is followed to its end with a loop guard, and a failed read leaves an error behind.
		- Verified on Windows: new test creates and reads back a shortcut, including one aimed past the old length limit.
		- Also found and fixed while checking it: Windows itself refuses to store a target that long, and we were not looking at the answer - so "Make Link" wrote a shortcut pointing at nothing and called it a success. It now refuses and says why, and leaves no file behind.
		- Test case: `test-nemo-shortcut-win32`, Windows only.

	- ✅ Item 15. A duplicated line in the settings file empties a list instead of falling back.
		- Cause: an unreadable list is treated as a deliberately empty one. Only lists behave this way; single values fall back correctly.
		- Note: a duplicated column list opens the list view with no columns at all. Hand-editing is a supported way to use this file, so this is easy to hit.
		- Fixed: a setting listed twice, or holding the wrong kind of value, falls back to its default and says so instead of coming back empty.
		- Test case: `test-nemo-config` (`test_duplicate_key_falls_back`).

	- ✅ Item 16. An external edit arriving mid-change throws the change away.
		- Cause: settings are written a couple of seconds after they are changed, and a file reload in that window replaces the pending change with no warning.
		- Fixed: a change made in the app inside the save delay is carried across the reload instead of being replaced by what is still on disk.
		- Test case: `test-nemo-config` (`test_pending_change_survives_reload`).

	- ✅ Item 17. Settings changes can be announced from a background thread.
		- Cause: deleting files updates favorites from a worker thread, and the change is announced on that same thread.
		- Note: the previous settings system always announced on the main thread, which is what every listener assumes. Nothing fires today, so this is a trap for the next listener added.
		- Fixed: change notifications are always delivered on the main thread, which is what every handler assumes.
		- Test case: `test-nemo-config` (`test_changed_on_main_thread`).

	- ✅ Item 18. A damaged per-folder settings file is discarded without a word, then overwritten.
		- Cause: a parse failure leaves an empty store, and the next change writes that empty store over the file.
		- Note: costs every folder's saved view, zoom, sort and layout. A failed save is likewise ignored.
		- Fixed: an unreadable per-folder settings file is reported and kept aside, so the next change cannot overwrite the only copy.
		- Test case: `test-nemo-metadata-store` (`test_damaged_file_set_aside`).

	- ✅ Item 19. Setting the thumbnail size limit above two gigabytes breaks thumbnails.
		- Cause: the limit is stored in a smaller number than the dialog offers, so the large choices wrap. Eight gigabytes turns every thumbnail off; two and four turn the limit off entirely.
		- Fixed: the size limit is read at full width, so the large choices work instead of turning thumbnails off or on wholesale.
		- Test case: `test-nemo-thumbnail-hold` (`test_size_limit`).

	- ✅ Item 20. Opening a folder on an unresponsive drive freezes the whole window.
		- Cause: the fallback added for unreadable folders asks for the listing in a way that blocks until the system gives up.
		- Cause: it also treats any general failure as that same case, so a passing glitch is remembered as a made-up folder with no way to tell.
		- Cause: a folder with many unreadable entries stops partway and shows an error over a half-listed folder.
		- Fixed: the fallback for an unreadable folder no longer blocks the window, the skip allowance counts a run rather than a total, and a folder that could not be read is recorded as unknown rather than confirmed empty.
		- Test case: `test-nemo-directory-load-clean` for a clean load; the fallback for a drive that does not answer is not checked.

	- ✅ Item 21. Right-clicking a path segment can offer actions the folder will not allow.
		- Cause: the menu is now built before the folder's details have loaded, and the unknown state reads as "everything is permitted", so Delete and New Folder appear on read-only places.
		- Fixed: a path segment whose details have not loaded no longer offers actions the folder may not allow.
		- Test case: none, needs a full window.

	- ✅ Item 22. Changing the default zoom discards a zoom deliberately set in another tab.
		- Cause: every open view reacts, not just the visible one, so background tabs lose their own setting.
		- Fixed: only the folder in front of you gives up its pinned zoom when the default changes.
		- Test case: none, needs several tabs in a full window.

	- ✅ Item 23. The "treat root as a normal user" preference is read before settings are open.
		- Cause: it is consulted while handling the command line, which happens first, so it is answered wrongly and then remembered.
		- Fixed: the preference is no longer answered and remembered before settings are open.
		- Test case: none, startup order of the full program.

	- ✅ Item 24. Folder listing and file moves do more per-file work than they used to.
		- Cause: every file now builds an address and takes a shared lock to check the per-folder store, where before there was a cheap early exit.
		- Cause: every moved file scans the whole store, so a large move gets slower the more is stored.
		- Cause: on Windows the per-type icon is rebuilt for every file on every update, not just when the type changes.
		- Cause: the store is rewritten whole on every save and never pruned.
		- Fixed: an empty store costs nothing per file, a move only scans when there is something to re-key, and the Windows per-type icon is derived once per type instead of once per file.
		- Test case: none, speed only.

	- ✅ Item 25. Reading a setting costs more than it should, and text settings grow memory.
		- Cause: every read searches the whole settings table from the start.
		- Cause: reads of text, list and choice settings allocate inside the settings document and never give it back, and one of them runs on every icon the mouse passes over.
		- Fixed: settings are looked up directly rather than searched from the start, and the memory the settings document hands out is reclaimed instead of growing for the life of the run.
		- Test case: none, speed and memory only.

	- ✅ Item 26. The Windows recycle bin is rescanned far more than needed.
		- Cause: a full scan runs every few seconds for the life of the app, twice more on every look at the trash folder, and once more for every item not already known.
		- Fixed: a look at the trash folder scans once instead of twice, and the periodic check notices a change that leaves the count the same.
		- Test case: none, speed only.

	- ✅ Item 27. The release checksums file can be written wrong.
		- Cause: an empty release folder still writes a bogus line, and any artifact name with a space would be split in two.
		- Note: this is the file both installers verify a download against.
		- Fixed: null-separated, and it no longer runs the checksum tool at all when there is nothing to check.
		- Test case: none, release script.

	- ✅ Item 28. Assorted unsafe or non-portable paths in the pipeline and installer scripts.
		- Fixed: the Windows installer no longer moves the new copy into place in a way that fails across drives after the old one is already gone.
		- Fixed: the installer's own `--help` now prints when run the documented way.
		- Fixed: a prerelease version in an archive name is no longer reported as the plain release number.
		- Fixed: the release archive no longer fails outside a checkout over its timestamp.
		- Fixed: refreshing the bundled themes rewrites only its own section of the notes file, leaving the rest alone.
		- Fixed: the Windows pipeline's publish step fast-forwards, like everything else here.
		- Fixed: the publish step counts untracked files as a dirty tree, so the stash covers them.
		- Fixed: a failed image-loader cache build leaves the previous cache alone instead of an empty one.
		- Fixed: the pipeline stops with an explanation when there is no one to answer its prompt.
		- Fixed: the packaged launcher finds whichever library folder the build produced.
		- Fixed: the publish helper splits the setting instead of running it.
		- Fixed: both delete-and-replace paths check what they are pointing at first.
		- Test case: none, pipeline and installer scripts.

	- ✅ Item 29. Script style and speed debt.
		- Fixed: the backup rotation, the dogfood pruning and the argument parsing all use builtins where they used to start a program per item.
		- Fixed: the unused function is gone.
		- Fixed: the output helpers now live in one file that the helper scripts share, instead of each carrying its own lesser copy.
		- Fixed: the Windows installer gained proper built-in help, so `Get-Help` and `-?` work.
		- Note: the review said three scripts had diverged output helpers; only one actually had. The others define a single matching helper, which is fine.
		- Test case: none, script cleanup.

	- ✅ Item 30. A Windows-only test reports a pass when it did not run.
		- Cause: the trash test exits successfully unless it detects the compatibility layer used for development, so on real Windows it silently skips.
		- Note: that is exactly where items 9 and 10 would have been caught.
		- Fixed: it reports a skip instead of a pass when it cannot run.
		- Test case: none, the fix is to `test-nemo-trash-win32` itself.

- ✅ Setting list view to 66% doesn't affect current list view. It should.
	- Opened: 20260724-135703
	- Closed: 20260725-184516
	- Also, setting default view to List mode, should affect current view immediately as well.
	- Cause: a folder stored its own view and zoom the first time it was opened, even when that just matched the default, so it was pinned to whatever the default was that day and later changes to the default never reached it. Nothing was watching the default view setting at all.
	- Fixed: a setting that only matches the default is no longer stored, so folders keep following it. Changing a default now also applies to the folders already on screen, and folders you deliberately set to their own view or zoom keep it.
	- Test case: `test-nemo-folder-settings`, `test-nemo-config` (`test_default_not_stored`), and `fCheckDefaultsFollowed` in the C lint.

- ✅ Settings don't seem to be persisting.
	- Opened: 20260724-091054
	- Closed: 20260725-172648
	- Verified: settings do persist, on both Linux and Windows. Checked the menus, the Settings dialog, per-folder view state, and window size, each set in one run and read back in the next.
	- Cause: the Settings dialog was crashing the whole app at the time this was filed, so nothing set in that session was kept. That crash is fixed.
	- Fixed as well: window size and position were only written when a window was closed cleanly, so a crash - or the wine launcher replacing the running copy - threw them away. They are now saved shortly after a move or resize settles.
	- Test case: `test-nemo-config` (`test_persistence`) and `fCheckGeometrySave` in the C lint.

- ✅ Windows via Wine: error message at startup. 'The folder contents could not be displayed.', 'Sorry, could not display all the contents of "<username>": Input/output error.' Mouse cursor also stuck at "busy spinner".
	- Opened: n/a
	- Closed: 20260725-153058
	- Reproduced: opening a home folder containing a unix symlink.
	- Cause: one unreadable child failed the whole folder listing. The aborted load also left the busy cursor on.
	- Fixed: the unreadable child is skipped and the rest of the folder lists. The load completes and the cursor clears.
	- Test case: none, only reachable under wine with a unix symlink in the folder.

- ✅ Windows via Wine: cursor seems stuck on the "busy" mouse icon.
	- Opened: n/a
	- Closed: 20260725-153058
	- Cause: same as the startup error above. The folder load never finished, so the busy cursor never cleared.
	- Test case: none, same cause as the item above, only reachable under wine.

- ✅ Icons don't match OG nemo.
	- Opened: 20260724-091054
	- Closed: 20260725-153058
	- Cause: two gaps. Windows reports one generic icon for every file type, and the Windows dependency snapshot was missing its image-loader cache, so no symbolic (SVG) icons rendered.
	- Fixed: per-type icons now derived from the file type on Windows. The loader cache is generated when the snapshot is built.
	- Note: the app's own bundled PNG icons didn't resolve on Windows either. That was a separate item, since done.
	- Test case: `test-nemo-file-win32-churn` (Windows only), and the loaders.cache check in `cicd/win/stage-native.bash` and `cicd/win/pack-zip.bash`.

- ✅ Windows: dot-name folders don't say "Folder". Regular folders say "Program", not "Folder".
	- Opened: 20260724-135703
	- Closed: 20260725-153058
	- Cause: folder type was guessed from the name whenever size read as zero, which every Windows folder does.
	- Fixed: folders always report the folder type, never guessed.
	- Test case: `test-nemo-file-win32-churn`, Windows only.

- ✅ Portable fallbacks for the remaining Mint-flavored theme icon names.
	- Opened: 20260719-190803
	- Closed: 20260725-153058
	- Cause: menus and toolbars referenced icon names only Mint themes ship. Pre-existing gap on non-Mint, cosmetic only.
	- Fixed: all names mapped to standard freedesktop names (mostly a straight prefix strip; the non-standard ones got closest equivalents).
	- Verified: every mapped name present in both the Linux and Windows icon themes.
	- Test case: none, no check yet that each icon name exists in the icon themes.

#### Done - Features and enhancements

- ✅ "Make link" dialog:
	- Opened: 20260926-094941
	- ✅ Add some extra space (ideally according to UI guidelines) between the folder & file options, and the "Symlink path" options - to visually indicate the latter is a different kind of decision.
		- Done: the path row sits 12 pixels below the link type rows, which are 6 apart. That is the GNOME spacing between groups and within one.
	- ✅ Shortcut flyover text: "On Windows, it's limited to programs that use the Windows shell library, such as Explorer.": -> "...such as Explorer (and Nemo Anywhere)."
	- Closed: 20260926-095500
	- Test case: none, spacing and wording only.

- ✅ Link copy dialog:
	- Opened: 20260925-163000
	- ✅ Row labels and choices are singular or plural to match the count, for file symlinks, folder symlinks and junctions. "Copy contents" is singular for one file, and plural for folders.
	- ✅ Shortcuts (.lnk) are always copied as just the shortcut file.
		- They already were. A shortcut is a plain file to a copy, and alone it asks nothing.
	- ✅ A tooltip on "Copy contents" for folders says links inside are copied as links, or junctions.
		- Made true: links inside a followed folder are now kept as links where the destination allows. Before, they took the row's answer, and could be followed again.
	- ✅ Tests for every combination of link copy choices.
	- Closed: 20260925-175728
	- Test case: `test-nemo-link-copy`, `test-nemo-link-copy-job`.

- ✅ Tests for every combination of Make link choices.
	- Opened: 20260925-163000
	- Closed: 20260925-175728
	- Done: every folder and file kind, both paths, every set of shortcut paths, for a file, a folder or both, made beside the originals and elsewhere.
	- Test case: `test-nemo-make-link-job` in its `every` mode.

- ✅ Tests for archive creation combinations, including delete after archive.
	- Opened: 20260925-163000
	- Closed: 20260925-175728
	- Done: every option paired with every other at least once, per format. Delete, one or each, split, password and hidden names are crossed in full.
	- Fixed on the way:
		- rar kept links as absolute links when asked to follow them, and dropped file links when asked not to keep links.
		- 7z and rar followed linked folders with that box off.
		- rar's copy of a duplicate file failed the check before delete, so the originals were never removed.
		- Compressing each item separately failed the whole job when one item was a linked folder that was not followed.
	- Test case: `test-nemo-archive-combos`, for each format whose program is installed.

- ✅ New menu item: "Edit link" (for all link types).
	- Opened: n/a
	- Allow editing target.
	- For .lnk files, allow editing all three target fields
	- Can change link name too.
	- Closed: 20260925-152642
	- Done: symlinks and junctions get a name and a target field; shortcuts get name, absolute, relative and portable path. An empty path is dropped. The shortcut's arguments, Start in and icon are kept.
	- Note: hardlinks are left out. They have no target to change.
	- Note: a changed shortcut loses its item ID list, so Explorer follows it through the portable path. Not yet tried on real Windows.
	- Test case: `test-nemo-link-edit`, `test-nemo-lnk` (`test_set_paths`, POSIX only).

- ✅ When copying symlinks, make it clear that the option "Symlink" is not creating a new one, but copying the existing link, or link's contents. E.g.
	- Opened: n/a
	- Currently:
		File symlinks:  [] Symlink  [] Copy
		Folder symlinks:  [] Symlink  [] Copy
	- New
		File symlinks:  [] Copy link as-is  [] Copy contents
		Folder symlinks:  [] Copy link as-is [] Copy contents
	- Remove the now-redundant description, "A copy holds the contents h a link keeps pointing at the original."
	- And so on for the other link types.
	- Closed: 20260925-152642
	- Done: "Copy link as-is", "Copy contents", "Copy as a junction" and "Copy as a symlink", and "Move" in place of "Copy" on a move. The note is gone; the move note stays, since it says why "Copy contents" is grayed.
	- Test case: `test-nemo-link-copy`.

- ✅ Pipeline pass, 20260925.
	- Opened: 20260925-122815
	- Closed: 20260925-130500
	- Done: `--help` prints before the run starts its CPU cap or touches the containers.
	- Done: shellcheck, ruff, PSScriptAnalyzer and Pillow are pinned beside cppcheck, and drift is reported in gate runs too.
	- Done: the Windows pipeline asks the same helper for its unattended commit message, takes `-Msg`, sends pull and push through gitsby, and rotates its logs like the Linux side.
	- Done: the publisher's pull and push go through gitsby too.
	- Done: neither pipeline replaces the synced dogfood copy while it is running.
	- Done: `--version` prints the copyright line under the version, and the build number rounds to the nearest minute.
	- Done: the camera raw reader has a fuzz target and seeds, run with the others.
	- Done: the demo video fades in and out, and the gif ends on three seconds of black so its loop point is plain. The demo's scenes are written out in plain words in `cicd/utility/demo-video/script.txt`.
	- Decided against: a build number in the Windows version resource. Its fields are small numbers, and the build number is text.
	- Decided against: renaming the dogfood launcher. `runfm` and `n8runfm.ps1` stay, since the desktop's file manager entry and habits use them.
	- Test case: none, pipeline setup; the copyright line is in `test-nemo-cli-version` and the raw reader fuzz target runs in `cicd/linux/fuzz.bash`.

- ✅ Move to SHCL 3.
	- Opened: n/a
	- Closed: 20260925-112355
	- Done: the vendored `shcl.h` is SHCL 3, from its `dev` branch ahead of the beta tag. Three workarounds for older SHCL are gone: rewriting backslash paths before a save, rebuilding the parser memory past 256 KB, and the exit on out-of-memory.
	- Keep: a comment is still only added when new, and raw reads still need a fence. SHCL 3 did not change either.
	- Note: no settings are carried over from the old format while in beta. The file is simply started fresh.
	- Done 20260925-113403: the settings file now ends with SHCL's info block, so a later release can tell which format wrote it before converting.
	- Test case: `test-nemo-config` (`test_backslash_paths_round_trip`, `test_many_reloads`, `test_oversized_file_refused`), `test-nemo-config-catalog` (`test_older_banner_replaced`, `test_line_after_banner`).

- ✅ Mouse cursor color change over the row underneath the cursor, needs to be a different color than "different shade of gray". Ideally something theme-based (per-OS), but adjusted to be more subtle if it's not. And not conflicting or confusable with actual current selected row color. And not confusable with alternating row colors. Whether using light or dark mode. And the most subtle-but-visible color difference of all the current row color differences. Possibly even a subtle text-only effect similar to SilkTerm's "scrim"?
	- Opened: 20260919-125440. Closed: 20260924-195639.
	- Done: hover takes the theme's selection hue, never gray, in the list and both sidebars. It is fainter than a shaded row and well short of a selected one, on light and dark themes alike. A theme that selects in gray gets a soft blue.
	- Near white there is little room for color, so on a white row the tint is also a little darker. It still reads as pale blue.
	- `row-hover-color` in the settings file overrides it.
	- Not done: the text-only effect. It can be its own item if the tint is not enough.
	- Test case: `test-nemo-row-hover` (`test_bundled_themes`, `test_gray_selection`, `test_tree_view`).

- ✅ Dialogs should not have titles in the dialogs themselves. The titles belong on the window decoration.
	- Opened: 20260924-184213. Closed: 20260924-193625.
	- Note: every dialog with a heading of its own now has its title in the title bar. What is left are alerts, which by long habit have an empty title bar and a bold first line, usually a question such as "Replace file?". Those stay as they are: only title-style headings move.
	- Test case: none, a layout convention with no code change of its own to pin.

- ✅ Regenerate the animated gif:
	- Use the updated URL at the end.
	- The icons in icon view are WAY too big for the view. Use smaller thumbnails.
	- Use real images in the image view. Modern fighter jets, puppies, beautiful green nature at hazy golden hour.
	- Opened: 20260924-184213. Closed: 20260924-191200.
	- Done: the ending shows github.com/yottacore/nemo-anywhere. A folder that is mostly pictures opens at one and a half times instead of five, so all nine photos fit. The photos are public domain or CC0, from Wikimedia Commons.
	- Note: the product default for such folders is still five times. That may be worth its own look.
	- Test case: none, demo asset; `cicd/utility/lint-demo-script.py` checks its settings.

- ✅ Valid shortcuts to folders sort with folders.
	- Opened: 20260924-184213. Closed: 20260924-185919.
	- Done: a shortcut sorts with the folders when it records a folder as its target, which is also what gives it the folder icon. Every platform.
	- Note: "valid" is taken as a shortcut that reads and says folder. The target itself is not checked, since that can stall on a share that is not answering, so a shortcut whose folder is gone still sorts as a folder until opened.
	- Verified: a test sorts a folder shortcut ahead of the files and a file shortcut among them.
	- Test case: `test-nemo-lnk-sort`, POSIX only.

- ✅ New tab: CTRL+Shift+T should work too.
	- Opened: 20260924-184213. Closed: 20260924-185428.
	- Done: with no folder selected it opens a new tab. With folders selected it opens them in new tabs, as it did before. A selected file used to open in its program; now it gets a new tab.
	- Test case: `cicd/utility/lint-accels.py` (one action per key, NewTabAccel owns Ctrl+Shift+T).

- ✅ Update to "Make a link" dialog:
	- Move the title from the dialog, to the Window decoration.
	- Remove "The new link goes in 'directory', and the vertical space it used, when it's obvious (e.g. no drag/drop involved).
	- Don't show "Symlink path:" row when it's not applicable. (But reserve the vertical space for it.)
	- Remove "Shortcut paths" options. That wasn't a good idea. Always create all three.
	- Symlink path: "Absolute" comes before "Relative".
	- Opened: 20260924-184213. Closed: 20260924-185000.
	- Done: the first four in the linkdlg chunk, and Absolute now comes before Relative.
	- Note: the "goes in" line comes back for a drop, which can be onto another folder.
	- Test case: `test-nemo-link-copy` (`check_link_options`, `check_labels`); the row that keeps its space is look only.

- ✅ Link names:
	- If links are created next to their originals:
		- Symlink: "<original name> - symlink[ 2 etc]"
		- Hardlink: "<original name> - hardlink[ 2 etc]"
		- Shortcut: "<original name> - shortcut[ 2 etc]"
	- Opened: 20260924-184213. Closed: 20260924-185000.
	- Done: the kind goes before the extension, "photo - symlink.jpg", so the link still opens as its type. A shortcut follows Explorer, "photo.jpg - shortcut.lnk". A junction is "folder - junction".
	- Done: a link made in another folder keeps the original's name, with " 2" and on for a clash. It used to be "Link to ...".
	- Verified: a test makes each kind beside its original, twice for the clash.
	- Test case: `test-nemo-make-link-job` (`check_names`, `check_every`).

- ✅ Dragging one or more files and dropping with "Alt" held, and user selects "Make link" - should open the new links dialog, rather than "Cancel/OK".
	- Opened: 20260924-184213. Closed: 20260924-185000.
	- Done: every link drop opens the Make link dialog, from the Alt menu ("Link here...") or with the link keys held, in any view or sidebar. The drop question leaves links out, since the dialog asks.
	- Verified: both kinds of drop open the dialog, and the link is made in the drop folder.
	- Test case: `test-nemo-drop-cancel` (`check_link`), `test-nemo-drag-confirm`.

- ✅ Debug delete/move/etc:
	- Opened: 20260924-170000
	- Closed: 20260924-170600
	- ✅ Add a setting in preferences to disable it.
		- Done: a checkbox at the end of Trash on the Behavior page. It is grayed out in a build with the guard forced on.
	- ✅ Add brief text to the dialogs, explaining why it's there: Because we have a rule to use it for all pre-releases, but is not necessary for this release candidate, and can be disabled in settings.
		- Done: the note sits under the headline in every guard dialog, and says where the checkbox is.
	- Test case: `test-nemo-delete-testguard`, `test-nemo-prefs-widgets` for the checkbox; the note text is wording only.

- ✅ Windows `.lnk` file support on macOS and Linux
	- Opened: 20260924-104933
	- Closed: 20260924-113840
	- ✅ They should behave mostly as they do on Windows:
		- ✅ .lnk to folders, should *change the directory* to that path
			- Rather than the way folder symlinks work, which is to place that folder virtually in the current path.
		- ✅ .lnk to files, should be treated exactly as symlinks to documents and programs do now, except:
			- The document is opened in the .lnk target's directory, or the program is executed in that directory.
			- Done: the shortcut's own "Start in" folder is used when it can be found here, as on Windows. Otherwise the target's folder.
			- Note: arguments in the shortcut are left out. They were written for a Windows program.
		- ✅ Folder, program, and document icons should display correctly.
			- Done: taken from what the shortcut records, never from the target. The extension is hidden and the shortcut overlay applies, as on Windows.
	- ✅ If an .lnk file has a Windows-style path in it, try to resolve it. (But don't edit it.) That means, in part:
		- ✅ It could be on a windows machine over a network share. If so, try to resolve the path to it's accurate network location.
			- Done: matched by server and share name to a kernel or gvfs mount. An unmounted share opens as smb:// where gvfs can.
		- ✅ If it's on a local filesystem, try to reinterpret the path to an existing local one.
			- Done: the drive is found by its volume serial. Then the path relative to the shortcut is tried.
		- But don't work SO hard that it guesses incorrectly, that would be worse than being unable to resolve.
			- Done: no match means a message naming the path. Two names that differ only in case count as no match.
	- Note: on macOS only the relative path and smb:// are tried until that target is built, since the drive and mount lookups read Linux's tables.
	- ✅ Update the functionality of the "Make symlink ..." menu item:
		- New name: "Make link ..."
		- A dialog opens, with options:
			- "Link type(s)"             ## Radio button options below
				- Label: "N folder(s):"  ## Only show the label if both files and folders are selected; if only folders, collapse the grouping.
					- Junction[s]        ## The default for Windows folders if supported.
					- Symlink[s]         ## The default otherwise
					- Link[s]            ## If not a Windows build, include flyover text that it's a Windows feature, that only Nemo Anywhere supports. Explain briefly how they are different.
				- Label: "N file(s):"    ## Only shown if both files and folders are selected; if only files, collapse the grouping.
					- Symlink[s]         ## The default otherwise
					- Hardlink[s]        ## Flyover text with urgent warning about the risks.
					- Link[s]            ## If not a Windows build, include flyover text that it's a Windows feature, that only Nemo Anywhere supports. Explain briefly how they are different.
			- "Path"                     ## Radio button options (section disabled if everything is Hardlinks and/or Junctions)
				- Relative
				- Absolute
			- Buttons
				- Cancel, OK
		- Done: the dialog is in, on every platform. Junctions only show on Windows. Absolute is the default, and a hardlink is never picked for anyone.
		- Note: the OK button reads "Make link" or "Make links", since buttons are named for what they do.
		- Note: this also covers the relative or absolute symlink option from the private notes.
		- Done: choosing a hardlink asks once more, every time, and lists what can go wrong as spaced bullets. Cancel is the default and goes back to the dialog.
		- Done: the Hardlink choice carries a warning sign after its label.
		- Done: "Link" makes a Windows `.lnk` shortcut, for folders and files, on every platform. Off Windows its tooltip says how it differs from a symlink, and that Windows follows a portable one.
		- Note: Path is for symlinks only, and is grayed for junctions, which are always absolute, and hardlinks, which have no path.
		- Changed: Links get their own row, "Link paths", with three checkboxes that all start checked: Absolute, Relative and Portable. One shortcut can hold all three, and is followed by the first that still leads somewhere. The symlink row is now "Symlink path". OK is grayed while a Link is chosen with none checked.
		- Done: the Windows-only "Make shortcut" item is gone, since Link covers it. A drag with the link modifier still makes a shortcut on Windows.
		- Done: Portable puts a Windows environment variable in the path where one covers the target, such as `%USERPROFILE%`. Off Windows that means the home folder, and a target on a Windows share keeps its `\\server\share` path there instead.
		- Done: variables are read and written as Windows `%NAME%` on every platform, never `$NAME`. Off Windows `%USERPROFILE%` reads as the home folder.
		- Done: paths inside a shortcut are always spelled the Windows way, with backslashes, even off Windows. They are read back for the platform in use.
		- Note: Explorer follows a Link made off Windows through its portable path. It never follows the absolute or relative path alone from one, since Windows 11 wants the part that names the target in its own terms. Nemo Anywhere on Windows follows all of them.
		- Note: on Windows the shell always writes the relative path, so one not asked for is taken back out. Without Absolute the file is written directly instead.
		- Note: on Windows with no symlinks allowed, files now start on Link rather than on a grayed-out Symlink.
		- Changed: the choices are no longer remembered. Every open starts from the defaults. A "Defaults..." button, here and in Preferences, may come later.
		- Changed: the Link choice is now "Shortcut", and its row "Shortcut paths". The menu item and the dialog still say link.
		- Done: every link type and path choice has a tooltip saying what it is good for and where it falls short.
		- Changed: the "Shortcut paths" row is gone. A shortcut always gets all three paths.
		- Changed: the title is in the window's title bar, and the line saying where the links go is gone, since it is always the folder in view.
		- Changed: the "Symlink path" row is hidden when no symlink comes out, but keeps its space so the dialog does not change size.
	- Test case: `test-nemo-lnk` (parse, resolve, `test_follow`, `test_write`, `test_portable`, `test_icon`), `test-nemo-make-link-job`, `test-nemo-link-edit`, `test-nemo-link-copy`.

- ✅ Allow moving tabs to other nemo-anywhere windows.
	- Opened: 20260922. Closed: 20260924-095019.
	- Each window is its own process by default, and GTK can only move a tab within one process. A move between windows has to be handed over as the tab's location instead.
	- Done: a tab's right-click menu has "Move tab to", which lists every other window by its title, and a new window. A tab dragged off its tab bar and dropped on another window goes to that window. Dropped anywhere else, it gets a new window, as before.
	- Done: the folder, the view and the selection go with the tab. Back and forward history stays behind, as it does for any new window. The window that takes the tab comes to the front.
	- Note: a drop needs to know which window is under the pointer, and Wayland does not say, so there a dropped tab always gets a new window. The menu works everywhere. A search tab cannot be moved, since the search only exists in its own window.
	- Note: not tried on real Windows yet.
	- Swept: a tab dragged out to a new window used to lose its view and selection, and keeps them now. With the one-process setting on, a drop on another window's tab bar still moves the tab itself, history and all.
	- Test case: `test-nemo-tab-move`, `test-nemo-window-at-point`, `test-nemo-new-process`, `fCheckBusMethods` in the C lint.

- ✅ Copying a tiny file makes a CoW clone of it, where a plain copy would do better.
	- Opened: 20260923-114627. Closed: 20260924-100500.
	- A copy tries a clone first at any size, then a plain copy. A clone of a tiny file can cost more than it saves.
	- Pick a size below which a copy skips the clone. See [20260930-145641_moving_and_copying.md](design_docs/20260930-145641_moving_and_copying.md#clone-copies).
	- Done: on Linux, a file under 64 KiB is read whole and written out plainly, so it is never cloned. 64 KiB is 16 blocks of 4 KiB, and a clone saves less than that below it while the file system keeps track of the shared extent for as long as both copies exist.
	- Done: the limit is `performance.clone-min-kib` in the settings file, 0 to always clone and 1024 at most. Other platforms have nothing to skip, since only Linux clones on copy there.
	- Note: an overwrite, a link copied as a link, or anything the plain copy cannot start goes the usual way, so conflicts and errors read the same as before.
	- Swept: copy and paste, drag and drop, and new files made from a template all take the plain copy. A hard link inside an archive is still copied the usual way on extract, where a clone is the closer match. Bookmarks and favorites copy their own small files and were left alone.
	- Test case: `test-nemo-small-copy` (`check_not_cloned`, `check_modes_and_links`).

- ✅ Put the drag-move scene back in the demo once the delete test guard's compile-time arm is at 0.
	- Opened: 20260919-161500. Closed: 20260924-083500.
	- The drag question is one of the better features to show, but while the guard is armed a move on camera brings up its dialog and call stack instead.
	- The demo lint fails if a drag goes back in while the guard is still at 1, so this cannot be forgotten.
	- Note: unblocked 20260923. The compile-time arm is 0, and the demo turns the setting off. The lint now fails if a drag goes back in while any of the define, the demo's settings or the default would arm it.
	- Done: the new archive from the Compress scene is dragged onto Invoices, the question asks, and Invoices is opened to show it there. Other scenes were trimmed to keep the gif under a minute; it is 58.9 seconds.
	- Test case: none, demo content; `cicd/utility/lint-demo-script.py` fails if a drag goes back in while the guard would arm.

- ✅ RE Delete/move test guard:
	- ✅ Originally opened 20260917-125536:
		- `NEMO_TESTGUARD_ALL_DELETES` in `nemo-delete-testguard.h` is 1 while the removal that took home on b23 is still unexplained, so every build asks about every delete and the normal confirmations stay out of the way.
		- The define only ever arms. At 1 nothing turns it off. At 0 the `NEMO_TESTGUARD_ALL_DELETES` environment variable and the `debug.testguard-all-deletes` setting arm it instead, so a shipping build can still be armed when needed.
	- ✅ Set it to 0 in code, but default to 1 in the default config file (including my local config).
		- Opened: 20260923-065221. Closed: 20260923-162417.
		- Done: the define is 0 and `debug.testguard-all-deletes` is on by default, so a settings file with nothing in it is armed. Nothing needed changing in a local settings file. Before the settings file is read, the default answers too.
		- Done: the demo turns the setting off, and its lint now reads the demo's settings and the default as well as the define.
	- ✅ If the guard in armed, skip all redundant native prompts.
		- Opened: 20260923-065221. Closed: 20260923-162417.
		- Note: which prompt came up twice was not noted. It was an ordinary one, not one that needs a real choice such as "cannot trash, delete instead?" or the copy links or contents question. Needs steps to reproduce if no obvious one turns up.
		- Fixed: the drop question for a move. A drag that moves files asked "move these?", then the guard asked about the same move. Armed, the drop no longer asks; a copy still does, since the guard does not cover copies.
		- Swept: delete, trash, empty trash and the many-items ask were already replaced by the guard. The "cannot trash" and conflict questions need a real choice and stay. If another one shows up twice, reopen with the steps.
	- ✅ Bug: The list of files can easily be too long for the dialog box.
		- Opened: 20260923-065221. Closed: 20260923-162417.
		- Solution:
			- Dialog a max size:
				- 1/2 of the shortest dimension and 1/4 the longest
					- Note: on a landscape screen that is 1/4 of the width and 1/2 of the height. On a portrait screen, the other way around.
				- Make wider first, to fit long paths without wrapping, if possible.
			- Dialog a min size:
				- No wider than needed for the longest path, and the OK cancel buttons at the bottom.
				- And for introductory text to not be too wrapped to comfortably read.
			- If the file list is still too long, give the list section a scrollbar.
				- But the buttons get dedicated space at the bottom that can't be scrolled, nor pushed below the screen real-estate.
		- Fixed: the paths and call stack sit in a scroll box, and the buttons have their own row under it. The dialog is only as big as its text, up to the caps above, and takes width before height. The headline wraps but keeps about 30 characters.
		- Verified: capped at 480 by 540 on a 1920 by 1080 screen and 540 by 480 on the same screen turned portrait. On 1024 by 768 it goes a little past a quarter of the width to keep the headline readable.
	- Test case: `test-nemo-delete-testguard` (default, variable and setting, `check_caps`), `test-nemo-drag-confirm` (`check_armed`).

- ✅ For macOS, many actions that require CTRL+[something] in Linux or Windows, would more naturally be Command+[something] in macOS. (E.g. keyboard mod behavior in Finder.) Account for these combo key differences. But don't go overboard, e.g. don't require "Cmd+down arrow" to enter a folder. Keep the current keyboard behavior, just remap the sensible things from Ctrl to Cmd on macOS where it makes sense.
	- Opened: 20260919-125440. Closed: 20260922.
	- Settled 20260920: write it now rather than wait for a macOS target. There is no way to run it here, so it goes in behind a platform check and stays unverified until there is a machine to try it on.
	- Menu shortcuts use GTK's "Primary" key, which is Cmd on macOS and Control everywhere else. The same key now adds to a selection by click, opens a new tab or window by click, zooms by scroll, and does cut, copy, paste and select all while renaming.
	- A few stay on Control on macOS, where Cmd already means something: showing hidden files (Cmd+H hides the app), make symlink (Cmd+M minimizes), the shortcuts window (Cmd+F1) and tab switching. Keyboard moves that keep the selection stay on Control too, as GTK's own lists do.
	- Nothing changes on Linux or Windows. Unverified on a Mac until there is one to try it on.
	- Not done: Ctrl+click for the context menu, the macOS habit. Its own item if wanted once a Mac build exists.
	- Test case: `test-eel-primary-mask`, `cicd/utility/lint-accels.py` (Control only on the allow-list); unverified on a Mac.

- ✅ Additional thumbnailer formats:
	- Opened: 20260922-090733
	- Raw files including .dng
	- jp2 (JPEG 2000 in general)
	- Closed: 20260922.
	- Raw files: a small reader of our own draws the JPEG preview each camera stores in the file, so nothing new is linked. DNG, CR2, CR3, NEF, ARW, RAF, RW2, ORF, PEF, SRW and most other TIFF based raws.
	- jp2 and more: handed to ImageMagick when it is installed, one file per run in the background. That also covers HEIC, AVIF, EXR, DDS, TGA, FITS and the older raw containers, and any format on the list that a later ImageMagick learns. Nothing new is linked.
	- Test case: `test-nemo-raw`, `test-nemo-magick`, and the raw fuzz target in `cicd/linux/fuzz.bash`.

- ✅ Folder properties dialog:
	- "Contents" and "Size" don't mean much. Better:
		- Folder count
		- File count
		- File size
			- Adds up live
			- Counting largest files in the tree first
				- Which will require gathering the list first, which is fine.
					- That will at least allow "File count" to populate early.
	- Remove "Help" button
	- Opened: 20260922. Closed: 20260922.
	- "Contents" is now two lines, Folders and Files, each with its hidden count beside it, as in "12 (and 3 hidden)". Both count up live while the folder is walked, and so does Size.
	- Largest first was left out. The size of each file comes with the listing, so the total is final the moment the listing is, and a second pass in size order would only make it later.
	- With several items selected, a selected folder counts as one of the folders. A lone folder shows only what is inside it.
	- The Help button is gone. It opened GNOME help pages, which this project does not have.
	- Test case: `test-nemo-deep-counts`; the removed Help button and the layout are dialog only.

- ✅ Change window title to path (with quotes if it has spaces), then "Nemo Anywhere". Formally:
	`["][preceeding path/][folder]["] - Nemo Anywhere`
	- Opened: 20260922. Closed: 20260922.
	- The title now reads `Documents - Nemo Anywhere`, or `"My Documents" - Nemo Anywhere` with a space. With the full path shown it is the whole path in the same place, quoted if any part of it has a space.
	- This reverses the order settled on 20260917 ("Nemo Anywhere - 'PATH'"). The folder first is the usual order for a window title, and a narrow taskbar button cuts from the end.
	- The format test was updated to the new order rather than kept on the old one, and has new checks for the quotes.
	- Test case: `test-nemo-window-title`.

- ✅ New option in Preferences: "Always show at least one tab".
	- Opened: 20260922. Closed: 20260922.
	- The setting was already in the settings file. It now has a checkbox under Behavior, "Show the tab bar even with only one tab".
	- Test case: `test-nemo-prefs-widgets` for the checkbox.

- ✅ Anytime a new window is spawned from an existing window (e.g. Ctrl+N or dragging a second tab off the window to open it in a new one), it should be in its own process.
	- Opened: 20260922. Closed: 20260922.
	- Already the case since 20260905, with "Open each new window as its own process" on under Behavior, which is the default. Checked again for Ctrl+N and for a tab dragged off the window: each comes up as a new process.
	- With the setting off, both stay in the one process, as before.
	- Test case: `test-nemo-new-process`, `test-nemo-instances`; the choice made by the setting is not pinned.

- ✅ Thumbnail scan progress bars:
	- Currently wrong: The "displayed" bar goes to 100% quickly, based on what is currently shown, and changes upon scrolling.
	- Fix to be correct: The displayed bar should use the total image count as the denominator, and should never be ahead of the "scanning" progress bar.
	- Opened: 20260922. Closed: 20260922.
	- The bottom bar counts every picture in the folder now, on screen or not, and never reads further along than the top one. In list view it follows the top bar.
	- Test case: `test-nemo-thumbnail-jobs` (`test_rendered_never_ahead`).

- ✅ Show two vertically stacked progress bars for thumbnail rendering, in the status bar:
	- E.g.:
		- Building  # I.e. reading files and caching them
		- Rendering # I.e. painting the thumbnails to the view
	- Placement: Between the "places|treeview|contents" buttons, and the [item count + other info] info in the middle.
	- Opened: 20260922. Closed: 20260922.
	- The top bar is how much of the current run of thumbnails is made. The bottom one is how many of the pictures on screen are drawn, in icon view. Hovering over either gives the counts.
	- They come up only when a run lasts more than a moment, and go away shortly after it ends. The space stays reserved, so the status text does not jump sideways.
	- Test case: `test-nemo-thumbnail-jobs` for the counts; when the bars show is look only.

- ✅ Thumbnails:
	- Opened: 20260921-172249
	- ✅ Feature: Show thumbnails for PSD format if possible. IIRC the layers are TIFF format, but maybe a special reader is needed.
		- A small reader of our own. It reads the flattened copy of the picture a .psd or .psb keeps after its layers, so the layers are never read. Grayscale, indexed, RGB and CMYK, 8 or 16 bit. Lab and 32 bit files still show the type icon.
	- ✅ Bug: When rendering thumbnails, there is often flashing when changing between generic icon, and rendered thumbnail.
		- Two causes. While a thumbnail was made the icon switched to a "loading" icon, which most themes do not have, so a stand-in flashed in between. And an edited file dropped its old thumbnail for the type icon until the new one was ready. Now the type icon stays until the picture is there, and an edited file keeps its old picture until then.
	- ✅ For photo folders, render a whole folder from top-down (in order listed in the view), rather than on-demand.
		- A folder of pictures on a local disk gets every thumbnail made once it loads, in view order, behind whatever is on screen. Ones not yet shown go into the cache without being held in memory.
	- Created 20260921-170804 by JC. Closed: 20260921.
	- Test case: `test-nemo-psd`, `test-nemo-thumbnail-hold`, `test-nemo-thumbnail-order`, `test-nemo-thumbnail-memory`.

- ✅ Add a small "Close" button (alt+C) in the right side of an "always visible" bar at the bottom of the Preferences dialog. (Both Enter and Esc activates.) It should be independent of the scrollbar area.
	- Opened: 20260921-172249
	- Created 20260921-170804 by JC. Closed: 20260921.
	- A bar under every page holds Close, outside the scrolling. Close is the default button, so Enter closes unless the focused control uses the key itself, and Escape closes too. On the Context menus page Alt+C also reaches the Copy box, so there it takes a second press.
	- Test case: `test-nemo-prefs-dialog` (Close default, Escape), `test-nemo-prefs-widgets`.

- ✅ The Compress dialog is taller than a 540px screen once Options is expanded, and its buttons fall off the bottom.
	- Opened: 20260919-161500. Closed: 20260921.
	- Found while writing the demo, at 960x540. The preferences dialog was checked down to 1024x600 and fits; this one was not.
	- The options scroll when the screen is too short for them, so the buttons stay on screen. On a taller screen nothing changes.
	- Test case: `test-nemo-archive-dialog`.

- ✅ On Linux, the new nemo-anywhere icon is not shown for the desktop launcher. And the running program shows a generic "Folder" icon.
	- Opened: 20260921-165228
	- Closed: 20260921.
	- The app icons were still the old folder-and-submarine. The logo changed on 20260919 and the icons cut from it were never redone. They are now, the Windows exe icon too, and the lint fails if they drift from the logo again.
	- Every window used to take the icon of the folder it showed, as upstream did. Windows now all show the program's icon.
	- The dogfood menu entry picks up the new icon on the next dogfood run.
	- Test case: `cicd/utility/gen-app-icon.py` check in the lint stage (skips without Pillow), `fCheckWindowIcon` in the C lint.

- ✅ No step in CICD should consume more than 50% CPU.
	- Opened: 20260921-165228
	- Closed: 20260921.
	- Job counts were already half the cores, but link-time optimization and rar run threads of their own past that. A full run now goes inside a user scope with a CPU quota of half the machine, and the three build containers carry the same cap.
	- The Windows pipeline still only caps its job counts.
	- Test case: none, pipeline setup.

- ✅ New default "mostly images" icon size: 320.
	- Opened: 20260921-165228
	- Closed: 20260921.
	- 500%, which is 320 pixels.
	- Test case: none, a default value; the size math is in `test-nemo-icon-size`.

- ✅ Remove Nemo authors from the actual Help|About|License button-expanded text. That text is only for the license title, link, and text.
	- Opened: 20260921-153217
	- Closed: 20260921.
	- The contributors line is gone. The license text opens with the license and its link, and the upstream credit stays on the copyright line.
	- Test case: none, wording only.

- ✅ Include uptime for the current nemo-anywhere session, in the Help|About dialog.
	- Opened: n/a. Closed: 20260921.
	- The About box says "Running for 2 hours, 5 minutes." under the description. Every window is its own process by default, so this is how long that copy has been up.
	- Days, hours and minutes, with any zero part left out. Under a minute reads "less than a minute".
	- Test case: `test-nemo-uptime`.

- ✅ Put archive extraction menu items nested into a "Extract ..." item.
	- Opened: n/a. Closed: 20260921.
	- The three extract items sit under one Extract submenu, in the right-click menu and the Edit menu. It shows only when every selected item is an archive, and the menu setting that hid the three items hides the submenu.
	- Mount archive stays beside it rather than inside. It browses an archive, it does not unpack one.
	- New test: every menu path a setting can hide has to be in the menu files. A wrong path used to fail with no sign.
	- Test case: `test-nemo-menu-paths`.

- ✅ Create a demo GIF at 50 fps (<60 seconds) and demo video (<3 minutes) at 60 fps. Use creation and script harness from project 'silkterm'.
	- Opened: 20260804-230307. Closed: 20260919.
	- The gif is 58 seconds and 1.6 MiB, at the top of the README. The video is the same script at 1080p60 with sound, kept out of the repo.
	- Six scenes: the window opens with Places alone, then the folder tree opens beside it and closes again; F3 opens a second content pane and closes it; icon view thumbnails; search flat then grouped by folder; and Compress to 7z.
		- Note: a seventh scene, the drag question on a move, was added 20260924.
	- The synthetic home is mounted at a generic path, so no account name or working path is on screen.
	- It picks a free display rather than insisting on one number, after a sister project's recorder was found on the one this had claimed.
	- `cicd.bash --demo` records it. Off by default and skipped on a quick run, since it takes about six minutes and only changes when the interface or the script does.
	- Note: merged with an older item from 20260804 that asked for about twenty seconds. The lengths above win.
	- Test case: none, demo production; `cicd/utility/lint-demo-script.py` checks its settings and what it shows.

- ✅ Add a preference: Auto-switch to image thumbnail view for folders with mostly images.
	- Opened: 20260921-131506
	- Store the tunables that define "mostly images" in the config file.
	- Closed: 20260921.
	- A folder that is mostly images opens in icon view, at the image size, unless it has a view of its own. On by default, with a checkbox under Icon view on the Views page.
	- The two limits are in the settings file: at least 2 images, and at least 50% of the files. Sub-folders are not counted.
	- Going back to list view by hand in such a folder is saved on it and sticks. With per-folder settings off, the switch lasts for that visit, and the next folder opens in the window's own view.
	- It only knows once the folder has loaded, so a folder can show in list view for a moment first.
	- Test case: `test-nemo-image-folder` (`test_wants_icon_view`), `test-nemo-prefs-widgets`, `fCheckImageDefault` in the C lint.

- ✅ SQLite icon cache issue reopened. More detail:
	- Downsample the images in the database, to the largest size the user ever requested.
		- Jpeg quality 90, with settings that favor faster decoding, potentially slower encoding if the space saving is worth it.
		- PNG or WebM if there is transparency.
	- Then downsample again at runtime only, if necessary, for actual display, if the current size is lower than the stored thumbnail. (If that is computationally feasible and user-tolerable for e.g. 250 images. If not, store multiple sample sizes as necessary.)
	- In the database, try to detect and avoid duplicates.
		- Fields for fast lookup:
			- Full pathname (or hash?).
		- Fields for dedupe:
			- precise mtime equivalent, file size, binary blake3 checksum (computed only if needed, OR if file is read anyway for icon generation).
			- This will allow recognizing the same file if it moves.
		- Also (only what's necessary):
			- icon size, stored img type, date stored, latest date rendered, render count.
	- Organize the database so that it can also handle non-image files - e.g. for future general filesystem deduplication features.
	- Add GUI Settings for basic automatic pruning control
		- Preferences:
			- [max cache size]; float GiB, default 2.
			- [oldest date rendered]; integer days
			- [local path exists but image no longer does]; boolean
			- [save checksum]; boolean
				- If checked, and extended attributes can be written:
					- Stored:
						- user.blake3.b64u = checksum in base64url
						- user.blake3.mtime = file modification time when checksum calculated
						- user.blake3.bytes = file size when checksum calculated
					- Writing xattrs is slow, so write *after* database updates.
	- FYI: The checksum xattr will also be used for future features.
	- Queue thumbnail creation and xattr updates, but abort cleanly if user changes directory and no longer needs to see thumbnails.
	- Manual buttons for:
		- "Cleanup now", which runs a prune and compact cycle
		- "Empty cache", erases the entire database.
	- Use the proper OS-specific cache locations for the database thumbnail cache.
	- Statically link SQLite3 into all executables. (It will also come in handy for future features.)
	- Opened: 20260921. Started: 20260921. Closed: 20260921.
	- Done: the store itself, its tests, the build dependency, drawing from it, pruning, and the settings.
	- Three tables. A file's contents are one record, the names it is known by are another, and a thumbnail hangs off the contents. So a file that moved keeps its thumbnail, and a file nothing can draw is still a record - which is what a duplicate finder would read.
	- A checksum settles what two records that looked separate really were, and folds them into one. Without one, size and timestamp together are the only guess available.
	- Checksum settled as blake3 rather than SHA-256: GChecksum's SHA-256 is plain C at 291 MB/s, and OpenSSL's is fast but costs 4.8 MB on the Windows exe for one call. blake3 is vendored under `vendor/blake3`, runs at 2459 MB/s and builds to 65,716 bytes. It checks out against the numbers blake3 publishes, which matters because a checksum written to a file outlives this program.
	- The cache location is its own choice, not the config one - local AppData on Windows so a thumbnail database does not sync between machines.
	- WebP is out: the Windows sysroot has no webp pixbuf loader, and gdk-pixbuf only ever writes png, jpeg, tiff, ico and bmp. Transparency means PNG.
	- The checksum goes on the file in the three attributes asked for, checked from outside the program so the names really are `user.blake3.b64u`, `.bytes` and `.mtime`. On Windows they are alternate data streams, and that half passes on an NTFS drive. It passes under wine too, but for the wrong reason: a colon is an ordinary character in a Linux filename, so wine writes a second file beside the first.
	- Thumbnails are read from the store and written to it. The freedesktop cache is still read when the store has nothing, and never written. Each one is kept at the largest size it has been shown at, and made again bigger when a zoom asks for more.
	- A file that could not be drawn is remembered, so it is not tried again every launch.
	- The settings are on the Preview page, under Thumbnail cache: the four asked for, a line saying how many thumbnails there are and the space they take, and the two buttons. Emptying asks first. The pruning schedule stays in the config file.
	- [save checksum] is off by default. Its tooltip says it is fairly cheap, and that the changed ctime can wake a backup tool, though most ignore it (settled 20260921).
	- With it on, a checksum is written onto a file when its thumbnail is made. Files already in the cache get one the next time they are drawn again.
	- On Windows, writing a checksum moved the file's modified time, since NTFS counts a write to any stream as a change to the file. That made each checksum stale as soon as it was written. Fixed.
	- Emptying the cache left more on disk than before, because the rebuilt file sat in the journal. It is folded back in now.
	- Pruning runs on its own thread, one process at a time, at random every 4 to 24 hours once nothing has been drawn for 5 minutes. Its settings are in the config file under `file-cache`. A damaged file is found by the check and rebuilt at the next launch.
	- The older sweep of the shared freedesktop cache is gone, with its two settings (settled 20260921). Nothing here writes to that cache any more.
	- Default for "Only for files smaller than" is 100 MB now, up from 1 MB. The cache limit was already 2 GiB.
	- Tolerant of multiple process access.
	- Tolerant of corrupt cache (db) file.
	- Automatic cleanup:
		- Run on a separate thread.
		- Process:
			- Check DB for errors.
			- Remove stale rows from DB.
			- Compact DB.
		- Don't run every launch. Run randomly after every 4 to 24 hours - at launch time, or even if sitting idle.
			- Ideally only after sitting idle for N minutes.
			- Settings in config file
		- Only one process at a time runs. Check some kind of file - in cache dir or /dev/shm - for:
			- Date/time last started.
			- Date/time last completed. [Cleared on a new run]
			- Last process ID to complete it.
			- Count of thumbnails removed.
			- Process ID that currently wants to clean it.
	- Test case: `test-nemo-cache-db`, `test-nemo-cache-prune`, `test-nemo-thumbnail-store`, `test-nemo-file-xattr` (`check_write_keeps_file_time`), `test-nemo-file-digest`.

- ✅ Problem: Currently, thumbnails are made at 256px at the largest, so a big icon size shows one scaled up.
	- Opened: 20260920-234500. Closed: 20260921.
	- Solved by the file cache. A thumbnail is made at the size it is drawn at, rounded up to a step of 128, up to 640.
	- `nemo-desktop-thumbnail.c` offers two sizes, 128 and 256, which is the older half of what the shared thumbnail spec now names. The spec has gone on to add 512 and 1024.
	- Only worth anything alongside the bigger icon sizes, where a folder of images is meant to be shown at 320 or 640. Below that nothing is being lost.
	- Touches the cache directory names, the size the factory is made with, and the test for whether a cached thumbnail is big enough to use.
	- Note: This will be solved by the cache -> database feature.
	- Test case: `test-nemo-thumbnail-store` (`check_size_step`).

- ✅ Icon view:
	- Opened: 20260919-083140. Closed: 20260920.
	- ✅ If folder is mostly images, increase default size to [max hieght or width = DPI-independent 320px].
		- Expose a separate adjustment for image thumnail size. default 320px.
		- "Mostly images" means at least 2 images and at least half the files, folders not counted (settled 20260920).
		- **Two icon size settings, both a percent of the standard 64px.** Ordinary folders get 100% as now. Folders that are mostly images get 500%, which is 320px. The range runs to 1000%, or 640px.
		- The image setting is a default, not a rule. An image folder opens at it, the slider still moves that folder, and the size sticks per folder where "Remember per-folder settings" is on.
		- **A folder remembers both sizes, not one.** What it should look like full of pictures, and what it should look like otherwise. Zooming sets whichever matches what is in the folder at the time, so zooming a gallery never moves the size its plain sibling opens at.
		- **Both sizes inherit, and the child picks between them by what is in the child.** One setting on a photo library gives every album under it the big size, while a folder of notes filed in the same tree still opens small. Where a parent only ever had one of the two set, the other falls back to its default (settled 20260920).
		- Done 20260920, first part: sizes are pixels and the seven-value zoom enum is gone. The named steps are stops the slider marks and Zoom In and Zoom Out move between, the slider reaches everything in between, the range runs to 640, and both defaults are a percent. A saved size is pixels, and one saved before the change is told apart by being too small to be a real size.
		- Done 20260920, second part: a folder of pictures opens at 500%. The count is taken once the folder has loaded, since that is the first moment anything is known about what is in it, and the size is never written back - a folder with nothing of its own keeps following the setting, and in a window that is not remembering per folder the bigger size does not follow you into the next folder.
		- Done 20260920, third part: the pair, and its inheritance. A folder keeps a second remembered size for when it is full of pictures, and everything that reads or writes a size picks between the two by what is in the folder. Inheriting needed nothing of its own, since a child already reads its parent's whole set and now asks it for the key that matches itself.
		- The size rows in preferences are spin boxes now, and the image one sits beside the icon view row on both the Default and the Current tab. A seven-entry combo could not hold 500%.
		- The whole rule is under "Icon sizes" in design.md.
		- The list view stays on the stops. Its own item covers what taking any size would need.
		- Found while looking, and not part of this: the desktop range is five steps where everything else is seven, so clamping to the widest range puts the desktop outside its own table.
	- ✅ The size slider is jammed too far to the right. Needs proper padding or margin.
		- Done 20260920. It is the last thing packed into the status bar and had only the box's own 2px, so the trough ran into the window edge while the buttons at the other end sat clear of it. A 6px end margin evens the two up.
	- Test case: `test-nemo-icon-size`, `test-nemo-folder-settings`, `test-nemo-image-folder`, `fCheckImageDefault`, `fCheckHeldIconSize` and `fCheckSliderMargin` in the C lint.

- ✅ If "show full path in tabs and window" is enabled:
	- Opened: 20260919-184409
	- Show the entire path of the current tab, if there's enough room.
	- Show the entire path in all tabs if there's enough room.
		- If not, show the entire path in the current tab if there's enough room.
	- Recalculate the possibilities of both on window resize, and redraw if necessary.
	- For window and tabs: Prefer full path, then '[beginning part]/[ellipses]/[end part]/', then '/a/b/c/d/' style.
		- For tabs, use the same shortened form for all non-active tabs. Use own logic loop (but same logic) for fitting the active tab.
	- Closed: 20260920-180000
	- Merged with "Path in window title: Show the whole thing, rather than shortened version, if it will fit" (opened 20260919-083140), which asked for the window half of the same thing.
	- The tab in front is no longer held to the width cap, so it spells its path out whenever the row can spare it. The tabs behind stay capped and take the same step as each other, and they shorten until the one in front fits. It gives way only after they have nothing left.
	- The order above wins over the one settled on 20260918, which put initials first. Folder names with an ellipsis in the middle now outrank them, so the initials form only turns up on a shallow path where an ellipsis would cost more than the folders it replaces.
	- The window title had a flat 52-character cut. It measures the path against the window's own width now, less room for the icon and buttons, and works it out again on every resize. A title bar's real width cannot be read, so this is a guess, but it tells a path that obviously fits from one that does not.
	- Test case: `test-nemo-path-forms` (`nemo_path_forms_fit`, `nemo_path_form_for_width`).

- ✅ Archive:
	- ✅ Remember previous settings except for "delete" and password, across sessions.
	- ✅ If "delete" is checked AND password set, show an additional simple dialog to confirm the password.
	- Opened: 20260920-160000
	- Closed: 20260920-170000
	- Done: the Compress dialog writes twelve settings back and starts from them next time. Format, compression level, the volume size, and every box in the Options expander bar the two named above. Also whether items were compressed separately, which is only put back where the selection allows it.
	- Done: with the delete box ticked and a password set, the password has to be typed a second time before anything starts. Getting it wrong says so and lets another go; canceling puts the Compress dialog back with everything still filled in.
	- `nemo_archive_should_confirm_password` is the one place that decides, next to `nemo_archive_can_verify` which decides whether the delete box is offered at all. New `test-nemo-archive-settings` covers the decision, the defaults and a restart.
	- Confirmed end to end: settings written and read back over two runs, the confirm dialog, a wrong password, canceling out of it, and a right one going through.
	- Test case: `test-nemo-archive-settings`.

- ✅ design.md regrouped: Overview, Architecture, Features, Quality, Building, Delivery, then Open questions.
	- Opened: 20260919-131209
	- Closed: 20260919-132409
	- Done: sections with several topics got sub-sections, such as settings, file operations, the interface and each platform. No text was dropped.
	- Done: two stale lines fixed. The Windows gate runs the suite, and the Windows build is native rather than in a container.
	- Test case: none, docs only.

- ✅ README: what is new since Nemo, where the beta stands, and install commands without the option list.
	- Opened: 20260919-131209
	- Closed: 20260919-132409
	- Done: the archive dialogs, column widths, row shading, side panes, tabs, link handling, content search and the Windows features are listed. The status says the beta is about polish, and names the delete guard and the unsigned exe as the rough edges.
	- Test case: none, docs only.

- ✅ Pipeline: host lint tool pinned, remote git through gitsby, and build boxes made on first use.
	- Opened: 20260919-131209
	- Closed: 20260919-132409
	- Done: cppcheck is pinned in `config.bash`, and drift warns. Fetch and pull go through `gitsby raw git` where it is installed.
	- Done: `nemo-build` and `nemo-winbuild` are made from their Dockerfiles when missing. A fresh clone's gate used to skip every stage with a warning.
	- Verified: the pin warned when set wrong. A throwaway container made from scratch ran a command, and a throwaway cross container built all 402 targets. The image builds themselves were not rerun.
	- Test case: none, pipeline setup.

- ✅ Row visualization enhancement:
	- Opened: n/a
	- Closed: 20260919-125416
	- Currently, there are two visual indicators showing where the cursor is in a list view:
		- A row highlight for one or more lines.
		- An outline showing only one line where the "cursor" is.
	- When user hits "Esc", the row highlight goes away, correctly (per recent requirement). But the subtle outline remains.
	- I think we can do away with this overlapping functionality, by not showing the "cursor" outline.
		- And when the user hits "Esc", not only is there (correctly) no indication of where the "cursor" is, it also actually gets "forgotten", so that cursor movement after that starts over at the top. (Similar to entering a new folder for the first time.)
	- Done. Escape now clears the selection and the cursor, in both the list and the icon views. A second Escape puts nothing back. The next arrow key starts at the top, and a Shift+click starts a new range.
	- The outline stays in every other case. It is the only sign of the cursor after Ctrl+arrow, which moves it without selecting.
	- Test case: `test-eel-forget-cursor` for the list view; the icon view half needs a full view.

- ✅ Owner name and Owner - name columns on Windows.
	- Opened: 20260918-175048
	- Closed: 20260918-184500
	- GIO leaves the display name empty there, so both columns are left out on Windows for now. The account's full name would have to be looked up by us.
	- Done. Both columns are offered on Windows now, off by default. The name is the local account's full name, looked up once per account. A domain account, a service or a file on a share shows none.
	- Test case: `test-nemo-owner-columns` (`check_windows_lookup`), Windows only.

- ✅ Add an option under "Behavior" to include extension on rename (on by default).
	- Opened: 20260918-181654
	- Closed: 20260918-182200
	- The setting was already there, file-only. It now has a checkbox under Behavior, right after the one for click-twice renames. Off selects just the part before the extension.
	- Test case: `test-eel-rename-region`, `test-nemo-prefs-widgets`.

- ✅ Change to username columns (two new columns):
	- Opened: 20260908-133001
	- Closed: 20260918-175048
	- Owner (the short version), with no display name. This is a change to the current column of the same name.
	- Owner Name (the display version)
	- Owner - Name (i.e. "[Owner] - [Owner Name]")
	- Done. The two new columns are left out on Windows, where there is no display name to show. That part is a new open item.
	- An empty display name no longer shows as a stray " - " after the user name.
	- Test case: `test-nemo-owner-columns` (`check_case`).

- ✅ Optional alternating row shading
	- Opened: 20260908-133001
	- Closed: 20260918-175048
	- Subtle
	- Complementary to, and non-visually-conflicting with "selected" or "under-mouse highlighted" colors.
	- Themable, customizable.
	- Done. A checkbox on the Display page, under List view, off by default. The theme or gtk.css can set `nemo_row_shading`, and `row-shading-color` in the settings file overrides it.
	- Test case: `test-nemo-row-hover` (`test_shading`), `fCheckCellPlain` in the C lint.

- ✅ When the full path is shown on tabs, and tabs won't all fit in the tab bar:
	- Opened: 20260918-170700
	- Closed: 20260918-173500
	- Condense the pathnames using SilkTerm-like rules.
	- Folders above the last one drop to their initials first, then an ellipsis eats the middle. The root and the folder's name always stay, and home reads as ~ on Linux.
	- The widest tab gives up a step first. A path wider than a tab may ever get starts shortened even with room to spare.
	- This replaces the fixed 52-character cut on tabs only. The window title still uses it.
	- Superseded 20260920 by the item above it: initials no longer come first, and the tab in front is no longer capped.
	- Test case: `test-nemo-path-forms`.

- ✅ If preferences is too small to show everything, make the scrollbar always visible.
	- Opened: 20260918-163716
	- Closed: 20260918-170628
	- Every scrolled area in the dialog, the page list included, now uses a normal scrollbar. It shows whenever something does not fit, and not otherwise.
	- Test case: `test-nemo-prefs-dialog` (no overlay scrolling).

- ✅ Hi-DPI testing: Make sure preferences dialog box fits on the screen. (Or shrink and use scrollbars if not.)
	- Opened: 20260918-152452
	- Closed: 20260918-233500
	- Checked on Linux at nine screen sizes and scales, from 1920x1080 down to 1024x600 at 2x, and with text alone scaled to 125%, 150% and 200%, which is what Windows does.
	- It already fits every time. The dialog is capped at nine tenths of the screen, and both the page list and the page scroll once it is that tight.
	- No change made. The scrollbars stay hidden until the mouse moves over them, which at 2x can make the page list look cut off when it is not.
	- Test case: `test-nemo-prefs-dialog` (size cap and text scale).

- ✅ Make extra sure that deleting symlinks, junctions, and [.desktop, and .lnk] files only delete or trash the links, and NEVER the contents inside (e.g. never the contents inside a Windows junction). A strict "Don't follow" policy, no matter where they are encountered in a tree to be deleted, and not a user setting that can be changed.
	- Opened: 20260908-021923 by JC.
	- Closed: 20260918-213000
	- Note: clearing an extract's staging folder on Windows went into a junction and deleted what it pointed at. Fixed, with a check. The delete job itself has not been checked against a junction yet.
	- Found: Windows calls a junction a plain folder. Three walks trusted that and would go into one: deleting a folder from the Recycle Bin, replacing a folder on a copy, and a generic empty-trash walk. The first was real. On Windows, taking a recycled folder that held a junction out of the bin deleted what the junction pointed at, and so did a junction recycled on its own. Seen on b29w.
	- Found: a move to another drive, told to take a link's contents, would have moved them out of the folder the link points at. design.md says moves never follow links, so a move now always takes the link, and the dialog greys out the copy option on a move.
	- Fixed: one check for "a real folder, not a link", used by every walk that removes things. The delete job asks it too, before it would ever walk into something that would not delete on its own. Lint fails any new walk that does not ask.
	- Tests: a new test runs the real delete and move jobs on a folder holding a folder link, a file link and a shortcut, and checks the target survives. A new Recycle Bin case covers the junction, and failed on the old code on b29w. `.desktop` and `.lnk` files were already removed as plain files.
	- Not tested yet: a move of a junction to another drive. b29w has one drive. vm925w has two, so the move test runs there when it is back up.
	- Test case: `test-nemo-link-delete-job`, `test-nemo-delete-from-trash`, `test-nemo-trash-win32` (`test_junction_in_bin`, Windows only), `fCheckTreeWalks` in the C lint.

- ✅ Cut the Linux drop down toward a single file.
	- Opened: 20260908-000856
	- Closed: 20260918-112900
	- Note: the first pass, from 102 files down to 44, is under Done.
	- Done: the release build folds the extension library into the program, which exports the same API to extensions. The `lib/` folder is gone from the drop.
	- Done: the compiled resources moved into the program on every platform. They were most of the old library's size.
	- A default build still makes the shared library, for anyone building extensions against it. A new test loads a stand-in extension against either kind of build.
	- Test case: `test-nemo-extension-load`, `test-nemo-app-resources`.

- ✅ Fill the gaps in design.md.
	- Opened: 20260908-133615
	- Closed: 20260917-183048
	- Missing: a status and revision block, the non-functional requirements (startup time, memory, listing speed on a large folder), a security section, what the program logs and how to turn it up, and any diagram at all.
	- Done: all five. The speed and memory figures are measured, not targets; no budget is set yet. Writing the security part turned up the empty trash bug under Done - Bugs.
	- Test case: none, docs only.

- ✅ Write the public UI and UX style guide.
	- Opened: 20260908-133615
	- Closed: 20260917-182027
	- `project/style-guide_code.md` covers the code. Nothing yet covers dialog layout, sentence case, when a prompt is warranted, keyboard behavior or icon use, all of which the lint gate half-enforces already without saying why.
	- Done: `project/style-guide_ux.md`. It collects the rules the closed items settled one at a time, with the reason for each, and says which of them a check enforces. Linked from the README and the code guide.
	- Test case: none, docs only.

- ✅ Archive dialog: Add an option - off by default - to delete what contents were archived, once archive is successfully created, and contents verified by relative pathname and file sizes.
	- Opened: n/a
	- Closed: 20260917-233000
	- "Delete the originals once the archive checks out", last in the Options expander, off by default. It is greyed for a split archive, since one volume will not open on its own and there is nothing to check.
	- Verifying reads the archive back with the library and walks the selection again, separately from the walk that wrote it. Every file has to be there under the same relative path at the same size. A folder counts as there when anything inside it is, since a writer may leave the folder entries out.
	- Anything the walk had to pass over - a dangling link, a linked folder the options said not to follow, a socket - means the archive was never offered all of it, so nothing is deleted whatever does read back.
	- What passes goes through the ordinary trash-or-delete, so it asks again and goes to the trash rather than being gone for good.
	- New checks in the archive job test cover the clean case and each way one can come up short. The accept and the refusal were both confirmed, including an encrypted archive.
	- Test case: `test-archive-job` (`check_predicate`, `check_verify`).

- ✅ Put in the title, not just the path, but "Nemo Anywhere - 'PATH'".
	- Opened: n/a
	- Closed: 20260917-223000
	- The window title now reads `Nemo Anywhere - 'Documents'`, or the whole path in the quotes when "Show the full path in the title bar and tab bars" is on. The tabs are unchanged, since the window around them already says the program name.
	- The old title was the folder alone, plus a "- File browser" suffix on the spatial-mode branch. That suffix said what the program name says better, so both branches collapsed into one.
	- Confirmed in both preference states, with a regression test on the format.
	- Test case: `test-nemo-window-title`.

- ✅ Update so (or validate) that List view column widths follow 'design.md's "List view column widths" section. Column width design has been updated several times, and this 'design.md' will be treated as the canonical, precise, complete, conflict-free definition from now on.
	- Opened: 20260908-133001
	- Settled 20260916: the design.md section wins over every backlog item, closed ones included. Each backlog item that sets column widths now carries a note saying so.
	- Closed: 20260917-103306
	- Four places where the code and that section disagreed, each settled 20260917 by JC and now built:
		- The share rounds down, as the section says. At 90% a three-value column now fits two of them.
		- Search results follow the same rule as any folder. The separate Name and Location split is gone, and so is `search.name-location-split`.
		- Name's hundred-pixel floor is gone. The header text is the only floor now.
		- A hand drag lasts while the folder is in view. `list-view.column-max-widths` is gone. A minor column's width is saved with the folder's settings instead, when "Remember per-folder settings" is on.
	- Two places where the section was at odds with itself, built the way that reading of it makes sense and reworded in design.md on 20260917 to say so:
		- Fixed-width columns were said not to be resizable by hand, and were then given a rule for what a hand resize does. They are not resizable, and that stale resize line is gone.
		- The minor class pointed at the primary class's "Default width if room" for a formula and a percentage, but that rule has no percentage in it. It points at "Min width" now: a minor column is at the 50% share at its narrowest, the `list-view.column-fit-percent` share by default, and all of its values at its widest.
	- Settled 20260916: "Remember per-folder settings" is a new setting rather than a missing one, specced on the "Changes to Preferences|Views" item. Until it is built, the clause it gates cannot be implemented.
		- Per-folder view state already persists as file metadata - visible columns, column order, sort column and direction, zoom, view type. Column widths are the one thing not stored, so that key has to be added.
	- Note 20260917: "Remember per-folder settings" is built, so this is no longer blocked.
	- Done: the three classes, the row split and the drag rules match the section. Ext counts as a minor column rather than a fixed one, every column gets a character of air on its right, and Name and Location share what is left of the row instead of Location taking it all.
	- Test case: `test-nemo-column-layout` (`check_fit`, `check_primaries_share_the_surplus`, `check_classes`), `test-nemo-folder-settings`, `fCheckColumnExpand` in the C lint; which class a column is still lives in the view.

- ✅ Changes to Preferences|Views:
	- Opened: 20260916-120139
	- Fine-tuning the requirements:
		- "Views" section:
			- Checkbox: "Remember per-folder settings" (default off) [this is a new setting].
				- Checkbox (Indented and enabled only if "Remember per-folder settings" is enabled): "Inherit view settings from parent." (default on)
			- Under that, two side-by side tabs, each with identical settings (but unique values):
				- Default
				- Current (entire tab disabled if "Remember per-folder settings" is disabled)
			- On "Default" and "Current" tabs, add a button on each: "Copy settings to Current|Default" (whatever is the opposite of the current tab). (Near the top-right of the tabbed content for each.)
			- Remove "Default[s]" from current heading names.
		- Rename "View new folders using" -> "Folder view" (on both tabs).
		- Remove the "Show only folders" option from "Tree View Defaults".
			- TreeView can only show folders.
			- If there are no sub-folders, don't show the "expand" chevron.
				- And don't show (Empty) when an "empty" bottom-leaf node is expanded (which now shouldn't be possible anyway).
	- Note: design.md's "List view column widths" section depends on "Remember per-folder settings" existing. A hand-resized minor variable-width column is meant to persist per folder only while it is on.
	- Created: 20260916-120249 by JC.
	- Closed: 20260917-093148
	- Note: the tree view part is its own item, under Done.
	- Decided 20260917:
		- "Remember per-folder settings" replaces "Ignore per-folder view preferences". While it is off, saved folder settings are not read and nothing new is saved.
		- "Inherit view settings from parent" replaces "Inherit view type from parent". It covers all view settings, taken from the nearest parent folder that has some saved, else Default.
		- The Current tab follows the folder in the last focused window, and shows its path.
		- The Current tab shows what the folder actually uses. Changing a value saves the whole set for that folder and applies it at once. A "Forget" button clears what is saved.
	- Done:
		- Views has the two checkboxes on top, then Default and Current tabs with the same controls. The copy button sits at the right of the tabs and names the other tab. Forget shows on Current only.
		- Folder view, sort, reverse, folders first, favorites first, the three zoom levels, text beside icons, same-width columns and folder expanders are all kept per folder now. The last five used to be global only.
		- "Ignore per-folder view preferences" is gone from Behavior. The two replaced lines in an old settings file are not read.
		- Opening a folder saves nothing. Only a real change does.
		- Changing a default no longer drops the zoom of a folder that remembers its own. It still does while remembering is off. This changes the fix for item 22 of the 20260804 review, which predates the Current tab.
	- Test case: `test-nemo-folder-settings`, `test-nemo-prefs-widgets`.

- ✅ State in README.md that Nemo Anywhere is "opinionated" and not trying to be a "solve every problem" tool. It does one thing very very well: Manage files, period. With far more useful "file management" features that Nemo has natively without platform-dependent third-party programs, plugins, and extensions.
	- Opened: 20260908-111526
	- Closed: 20260917-073027
	- Added as a paragraph under "Why" in the README.
	- Test case: none, docs only.

- ✅ The tree view shows folders only, and a folder with no sub-folders has no expander. From "Changes to Preferences|Views".
	- Opened: 20260916-120249
	- Closed: 20260917-060256
	- The "Show only folders" setting and its "Tree view defaults" section are gone. A settings file that still has the line is not harmed; it is just not read.
	- Folders in view are checked in the background, one at a time, for any sub-folder. Shares are skipped and keep their expander until opened.
	- No "(Empty)" row any more. Opening a folder that turns out to have nothing to show removes its expander.
	- Left: turning hidden files off with such a folder open. Filed under Bugs.
	- Test case: `test-nemo-tree-folders`.

- ✅ Places and TreeView can both exist at the same time.
	- Opened: 20260916-113649
	- Both remember their unique horizontal user sizing.
	- When the window is resized, "Places" remains fixed size, but the remaining horizontal space is proportionally grown or shrunk among:
		- TreeView if visible
		- Content pane
		- Second content pane if visible.
	- By default, "Tree view" is 2x "Places" width.
	- Tree view can have focus (as it currently does already).
	- Bring back the buttons for both views on the bottom-left.
		- Also the "Hide/show the sidebar" button, but rename it "Show contents only"/"Full view".
		- The "Tree view" and "Places" buttons should no longer act as mutually-exclusive radio buttons, but as individual on/off toggles.
	- Created: 20260916-112242 by JC.
	- Closed: 20260916-193258
	- Done: both panes show at once, each with its own remembered width. Places holds its width when the window is resized. The tree opens at twice the places width. The two buttons are back on the bottom-left as independent on/off toggles, with "Show contents only"/"Full view" beside them, and the View menu entries are toggles now rather than a radio pair.
	- Left: the tree does not share a resize in proportion with the content panes. Filed as a bug under Bugs. Fixed since.
	- Fixed on closing: resizing the window saved the scaled tree width as the remembered one, so a narrow window shrank the tree in every later window too. Only a drag of the divider saves it now.
	- Test case: `test-nemo-pane-layout`, `test-nemo-proportional-paned` (`check_only_a_drag_is_placed`); the two separate toggles need a full window.

- ✅ Focus can never remain on the "Places" pane, after clicking on a place. Focus moves to the main content pane after changing to the place.
	- Opened: n/a
	- Closed: 20260916-122542
	- It depended on the folder. A place whose folder wants a different view type lost the focus, and one that reuses the view kept it, since only a new view is connected to the window.
	- Connecting a content view always grabbed the focus. It now leaves it alone while the sidebar holds it, so the keyboard stays on the place that was clicked.
	- Startup still puts the focus on the view, where the sidebar has not taken it yet. The tree view was never affected, which is why it already behaved.
	- Test case: `test-eel-focus-guard`, `fCheckSidebarFocus` in the C lint.

- ✅ Fuzz the parsers that read untrusted input.
	- Opened: 20260908-133615
	- Closed: 20260916-104101
	- Three targets, one for each parser that is ours to fix: the settings file, the drag payload, and the command lines kept in the config.
	- The item named four parsers, and two of them turned out not to be ours. The settings parser is vendored and `.desktop` files go through GLib, so a find in either is a report upstream rather than a patch here. `.lnk` files are read by Windows itself, which leaves nothing to fuzz and could not run on the Linux host anyway.
	- Each target builds two ways. Ordinarily it replays a checked-in seed corpus as part of the suite, which keeps it compiling and keeps the seeds meaning something. With `-Dfuzzing=true` it builds against libFuzzer and the pipeline searches for a bounded time per target.
	- The replay tests carry AddressSanitizer themselves. Without it they passed clean with a known over-read put back, which made them worth nothing; with it the drag payload test catches it.
	- A time budget running out is a pass. A find exits on a code of its own and leaves the input behind, so the two can never be mistaken for each other.
	- Left out of `--quick` and out of the pre-push gate. A box with no clang skips the stage with a warning rather than failing the run.
	- Test case: The fuzz corpus tests in `source/fuzz`, and `cicd/linux/fuzz.bash` for the timed search.

- ✅ Make the crash reporter better.
	- Opened: 20260909-171500
	- Closed: 20260915-161437
	- Filed off a review of the reporter as it went in. None of these stop it doing its job today.
	- A second crash in the same second from a reused process id loses the second report on Linux and overwrites the first on Windows.
		- Fixed: the second report gets a number on the end of its name, and the first is left as it was.
	- The check before the Windows unwind step reads eight bytes, not the frame the unwinder will read, so a badly shredded stack still costs the stack half of the report.
		- Fixed: every read the unwinder is about to make is checked first. A frame that points at nothing ends the walk with a note, and the frames before it stay in the report.
	- A stack that unwinds to itself fills the report with sixty-four identical lines, which reads the same as genuine deep recursion.
		- Fixed on Windows: the walk stops with a note once a frame fails to move up the stack.
	- On Linux a jump to a null pointer recovers only two frames, because the backtrace call cannot start from an address with no code at it. The handler is already handed the register state that would recover the caller and throws it away.
		- Fixed: the caller and everything above it are in the report now.
	- The signal is handed back with a raise, which puts the reporter's own frame on top of the core file. Only a signal that was really sent needs that; a fault could simply be allowed to happen again, and the difference is in what the handler is told.
		- Fixed: a real fault happens again with no handler in place. A sent signal is still raised, and its report no longer gives an address it does not have.
	- A stack overflow has never been seen to produce a report on either platform - it cannot be driven under the emulator. Needs a run on real hardware before the claim stands.
		- Linux reports one now, and a check covers it.
	- Every case now passes on a real Windows box too, stack overflow included.
		- The check itself had never passed there. It read the report path with the line ending still on it.
	- Test case: `test-nemo-crash`.

- ✅ Run the test suite in the Windows pipeline.
	- Opened: 20260909-145927
	- Closed: 20260915-161437
	- The Windows gate runs lints, build and the launch smoke, but not the suite. That half of the Linux pipeline item was left open rather than guessed at.
	- Blocked on there being nowhere to run it: neither Windows box holds a checkout at all, so nothing can be built or tested there as things stand.
	- The cross build is not a stand-in. Run against the emulator the same suite gives six failures and a timeout, and the keyboard and parts of the shell do not behave there, so a green run would prove nothing and a red one would say nothing either.
	- The full Windows pipeline and the gate both run the suite before the smoke test now, and both pass on a Windows box.
	- Four tests need a monitor, and skip when there is none, as over ssh.
	- Test case: none, pipeline setup.

- ✅ The standard .desktop launcher should be titled "Nemo Anywhere", not "File Manager".
	- Opened: 20260914-173549
	- Closed: 20260915-152159
	- The launchers this project installs already say Nemo Anywhere. The man page still named the Cinnamon file manager, and now names this one.
	- The "File Manager" menu entry on the XFCE box is the desktop's own launcher for whatever file manager is preferred, which is set to the dogfood launcher. It is not a file from this project, and it is left alone.
	- Test case: none, wording only.

- ✅ Searching through a search folder has no test.
	- Opened: 20260909-152800
	- Closed: 20260915-150724
	- Note: the demo that was removed drove one, but it asserted nothing, so no coverage was lost. Split from "Run the test suite in the Linux pipeline".
	- A new check runs a search through a search folder the way a window does. It covers the hits, a reload that starts the list over, and hidden files following the preference.
	- Test case: `test-nemo-search-folder`.

- ✅ Pick one way to leave Windows-only tests out of the Linux build.
	- Opened: 20260909-154342
	- Closed: 20260915-150724
	- Today eighteen are left out of the build entirely, three are built and report a skip, and eight carry a stub for the other platform, of which five are never compiled.
	- Note: split from "Run the test suite in the Linux pipeline".
	- A Windows-only test is left out of the build on other platforms, and carries no stub. The three built-and-skipped ones are left out now, and the stubs are gone. The Linux suite reads 53 passed and none skipped.
	- Test case: `fCheckWinTests` in the C lint.

- ✅ Tests leave their scratch directories behind.
	- Opened: 20260909-160500
	- Closed: 20260915-150724
	- Every test that reads a preference points the home directory at a throwaway one, and the toolkit then writes its own cache in there. Nothing removes it, so one directory per test per run piles up - the build container is holding hundreds. Harmless until the suite started running on every push, which is what turned a slow drip into a steady one.
	- Wants one shared cleanup the tests can call, not a recursive delete copied into each of them.
	- Every test makes its scratch directories through one helper, which removes them when the test exits. It only removes a directory it made, and never follows a link out of one.
	- The Linux test run gets a temp directory of its own and fails if a test leaves anything in it.
	- Test case: `test-nemo-scratch`, `test-scratch-guard`, the leftover check in `cicd/linux/run-tests.bash`, `fCheckTestHelpers` and `fCheckTestTreeWalks` in the C lint.

- ✅ Don't run CICD trigger on commit to dev.
	- Opened: 20260914-182719
	- Closed: 20260915-132239
	- A push to dev no longer runs the gate. Only a push to main does. A chunk is built and tested before it is merged to dev, and a full run's own publish only repeated the checks it had just made.
	- Test case: none, pipeline setup.

- ✅ Run the test suite in the Linux pipeline.
	- Opened: 20260909-112701
	- Closed: 20260909-145927
	- The Linux gate now builds, runs the whole suite, then the launch smoke. Half a minute with nothing to rebuild, longer when there is. A broken check can no longer reach main unnoticed.
	- The gate had no build step at all, so it had been checking whatever was last left lying around. It builds first now, held to the same job limit as the rest of the pipeline, and so is the suite.
	- What it builds is the working tree, not the commit being pushed, so an unfinished edit sitting in the tree will stop a push. Building the pushed commit in a detached worktree would be more correct, and would be a cold build every time; a warm build directory is what keeps the gate at half a minute.
	- The three tests written off as environment failures were all real. One was fixed and two were replaced.
		- The thumbnail test hid the shared mime database along with the box's own thumbnailers, and reading an image back needs it - so the long-thin-image check failed and read as a thumbnailer defect. The test keeps its isolation and reaches the database again.
		- The other two were inherited demo programs rather than tests. Neither asserted anything and neither could ever exit, so both ran until the runner killed them. One searched the whole filesystem, because it named no folder to search.
	- What replaced them. Filename search covers recursion on and off, and a pattern that matches nothing, which still has to come back or the view sits on a spinner. Directory monitoring covers the first listing, a file that appears afterwards, and a forced reload finishing rather than merely starting.
	- Three Windows-only tests had been reporting a pass on Linux while doing nothing at all. They report themselves skipped now, which is why the count reads 49 passed and 3 skipped rather than 52 passed.
	- Note: the Windows lane and two leftovers from this pass are filed as open items.
	- Test case: none, pipeline setup; its new tests are `test-nemo-search-engine` and `test-nemo-directory-monitor`.

- ✅ A crash leaves a report behind.
	- Opened: 20260903-130431
	- Closed: 20260909-090725
	- Done: a crash now writes a report next to the settings file, under `crash/`. It carries the version, what killed it, and the stack. The same text goes to stderr, which is what a launcher log keeps, and on Windows a message box says where the file is, since a windowed build has no stderr. The next start notes a report was left behind, and the oldest are dropped so the folder cannot grow forever.
	- Note: split from "Randomly crashes", which stays open until a report shows the cause.
	- Test case: `test-nemo-crash` (`check_sweep` for the startup note and the oldest dropped).

- ✅ Menu entries and shortcuts that keep working, and one sync path spelling per platform.
	- Opened: 20260908-013000
	- Closed: 20260908-015628
	- Every list of sync-tree paths carries both spellings now - the source dir, the wrapper the menu entry runs, and the launcher itself. `synced` is a link to the Dropbox folder, and a box without the link found nothing at all.
	- The app icon is copied out of the newest version and kept beside the pool under a fixed name. A menu entry used to point into a version directory and go blank the moment that version was pruned.
	- Windows shortcuts get the same treatment the Linux menu entry already had. A Start Menu or taskbar link aimed at this app is repointed at the current launcher and icon, and one is created if there is none. Both dev boxes had a link to a launcher path and an exe drop that were retired weeks ago, so clicking it did nothing.
	- The old by-self exe drop is swept on sight, wherever a run finds one.
	- The desktop step no longer waits on a successful launch, so a box with no build yet still gets its shortcut fixed.
	- A launcher run from outside its deployed home writes no shortcut at all, rather than one naming a path that will not last.
	- Both dev Windows boxes were swept: the stale run log and the last 38 MB copy from the old pool are gone, and each Start Menu and taskbar link now names a launcher and an icon that exist.
	- Note: `exec/synced/util` is a link into the live synced tree on at least one box. Anything swept under a path that looks local can be the real file, and the sync layer then carries the delete everywhere.
	- A shortcut or menu entry now records the wrapper's deploy-managed path rather than whatever a PATH lookup returns. On one box PATH reached the file through two chained links, and the shortcut kept that spelling; both boxes name the plain path now.
	- The wsl copy of the bash wrapper is deployed along with the linux and macos ones. Nothing was keeping it in step and it had fallen a revision behind.
	- A full sweep of both Windows boxes and this one found no stray versions or launchers left to move or trash. The only stale copies remaining sit inside a scheduled local mirror frozen at 20260903, which other tooling owns.
	- Test case: none, launcher setup on the dev boxes.

- ✅ Cut the Linux drop from 102 files to 44.
	- Opened: 20260908-000856
	- Closed: 20260908-005434
	- Note: it started at 102 files and 3.8 MB. Three helper exes were 2.6 MB of that, and nothing spawned two of them.
	- Done: the three helper exes are gone. The connect and open-with dialogs already ran in-process, so those two were dead weight. The extensions lister is now `--extensions-list` on the program itself, which keeps it a separate process without a separate binary.
	- Done: the `bin/` shell wrapper. The program points its own data and program paths at the folder it sits in, and finds the extension library through an rpath, so `bin/nemo-anywhere` is the program itself and `libexec/` is gone.
	- Staying: the four document-to-text converters, and actions, which have to remain user-editable.
	- Done: data that only a system install would use has left the drop - mime, polkit, man pages and the editor syntax files. Nothing reads any of it out of a relocatable prefix or out of /opt, which is where both packages put one. A distro building its own install still gets all of it.
	- Done: the D-Bus activation file is written at startup into the user's own service directory, naming the path this copy really runs from. The shipped one named wherever it was built.
	- Done: what could move into the compiled resources has. The whole icon tree except the app icon itself was a second copy of art already in the binary, kept only for a system icon theme; the two info-bar documents are written out to the cache when the button that opens them is pressed, since another program has to read them.
	- Left as files, deliberately: actions, search helpers and the settings schema. All three are drop-in folders a user adds to or edits, and Preferences has a button that opens two of them.
	- Done: the eight Cinnamon-only actions ship disabled. They call cinnamon-settings, the desktop editor or org.Cinnamon over the bus, and are still listed in Preferences > Actions for anyone running Cinnamon.
	- Note: split from "Cut the Linux drop down toward a single file", which stays open for the static extension library.
	- Test case: `test-nemo-runtime-env`, `test-nemo-extensions-list`, `test-nemo-app-resources`, `test-nemo-startup-clean` (`check_activation_file`).

- ✅ One dogfood location per platform, and a launcher pool that keeps its history.
	- Opened: 20260907-203000
	- Closed: 20260907-224704
	- Every platform's build now goes to `common/exec/app/<platform>/` under its own name: the whole prefix on Linux, the packed exe on Windows. Both pipelines publish there and nowhere else.
	- The launcher keeps the local pool: `<name>_versions/` beside a symlink at the fixed name, in `~/.local/bin` on Linux, `%LOCALAPPDATA%\Programs` on Windows and `~/Applications` on macOS.
	- The pool is rotated on every run instead of aged out after a week. It keeps the newest of each finished hour, day, week, month and year, the most recent few, and the very first build forever, then trims to at most ten versions, at least five, and 1 GB between the two. A version something is running out of is never removed.
	- A build already held is recognized by its bytes rather than its date, so a stamp the sync layer rounded no longer costs a re-copy.
	- `runfm` is the name to type or put in a `.desktop` file, on every platform. The masters live in `utility/`; stage 7 copies them out to the synced util dirs.
	- The menu entry now runs the launcher rather than a dated copy of the app, so a menu click picks up a new build the same way a shell launch does. Its icon comes from the newest version.
	- Test case: `cicd/utility/test-runfm-pool.ps1`.

- ✅ Add default user-tunable settings as comments to config file.
	- Opened: n/a
	- Closed: 20260906-085903
	- Done 20260906. The settings file ends with every key that is not set, commented out, with the value used instead. Uncommenting a line sets it; setting a key takes it off the list.
	- Notes were rewritten to say only what a user would see, and dropped entirely where the key name already says it - which is about half of them. The list in the code and the shipped schema are checked against each other so the two cannot drift.
	- Left off the list: keys the app writes back itself, such as a window size, a sidebar width or the last state of a search toggle. Setting one by hand only gets it overwritten.
	- Two keys that nothing had read since the fork were dropped.
	- Test case: `test-nemo-config-catalog`, `test-nemo-config-schema`.

- ✅ Better thumbnail cache management. Database plus background pruning.
	- Opened: 20260826-103001
	- Closed: 20260905-192349
	- Done 20260905. The cache is swept once a day, on a worker thread a minute after startup. A thumbnail whose file is gone goes first, then anything unused past the age allowed, then oldest-first until the rest fit in the size allowed.
	- Two settings on the Preview page: how long an unused thumbnail is kept, and how big the cache may get. Either can be turned off.
	- No database. The cache is the shared one every file manager on a Linux desktop uses, and a private store would have cost that sharing and added a dependency to three build environments. Growth was the complaint; sweeping fixes it. Reasoning is in design.md.
	- Another program's failure records are left alone.
	- Test case: `test-nemo-cache-prune` for the file cache that replaced this sweep.

- ✅ Change to search mode column sizing:
	- Opened: 20260905-133614
	- Closed: 20260905-184048
	- In search mode when location column is shown:
		- Only give 'Name' and 'Location' columns as much space as they need, not more.
		- Only if they run out of space, shrink column proportional to their space demanded.
			- But never one more than 2x the other.
	- Done 20260905. Search results now leave the rest of the row empty rather than stretching Name across it. When the two do not both fit they give in proportion to what they asked for, and neither ends more than twice the width of the other unless the narrower one did not want the extra.
	- Dragging either column still pins the split, as before, and the pair then fills the row again. Clearing `search.name-location-split` in the settings file goes back to fitting the contents.
	- Note: This may contradict the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: none, replaced by the column width rules, which `test-nemo-column-layout` covers.

- ✅ Need a better icon for "recursive" in search mode. (It currently looks like "press this for enter".)
	- Opened: 20260905-112901
	- Closed: 20260905-114812
	- Done 20260905. A folder with a branch line down into a smaller folder, the usual "include subfolders" shape, in the same flat style as the group-by-folder toggle beside it. Mirrored for right-to-left.
	- Test case: none, icon art.

- ✅ New process for each window. A crash in one shouldn't affect all others. And different versions (e.g. from n8runfm.ps1) should be able to run at once.
	- Opened: 20260722-172504
	- Closed: 20260905-102753
	- Done 20260905. Every launch and, by default, every new window is its own process. A command-line launch never joins a running copy, so two versions run side by side. `--quit` and Close All Windows still reach every copy.
	- A setting under Behavior puts new windows back inside one process. The trade: a tab cannot move to a window in another process, and on Windows a new window takes the packed exe's start-up time.
	- Test case: `test-nemo-instances`, `fCheckNoSessionRegister` in the C lint.

- ✅ Wire the Linux release lane into the pipeline engine itself, rather than leaving it a script to remember to run by hand.
	- Opened: 20260804-133646
	- Closed: 20260904-161518
	- Done: the lane runs as stage 5. `RELEASE_COLLECT=0` keeps the engine's collector out of the artifact dir, since release.bash already writes the tarball and the sums there itself.
	- The release smoke check had been failing since the version string gained a build number, so every release since then repackaged an older tarball. It matches on a prefix now.
	- Test case: none, pipeline setup.

- ✅ Dogfood the Linux build from the pipeline. It had never been set up, so the launcher was serving a build from July.
	- Opened: 20260904-160000
	- Closed: 20260904-161518
	- Stage 7 understands a relocatable prefix, not just a single binary: the fixed install puts the tree beside the bin dir and points the name on PATH into it, and the rotating copy is the whole tree under a dated name.
	- The dated name carries the build's own mtime rather than the run clock, so the pipeline's copy and the launcher's copy of one build agree and neither re-fetches it.
	- Note: superseded on 20260907 by "One dogfood location per platform": the pipeline publishes one drop and writes no dated copies at all, so the second and third bullets here describe how it used to work.
	- Test case: none, pipeline setup.

- ✅ Search options: Flat [ ]  Hierarchical [ ]
	- Opened: 20260819-141014
	- Closed: 20260903-185727
	- Read as a display mode, not another scope switch - the search bar already has a toggle for recursing into subfolders.
	- Done 20260903. A toggle beside the recurse one groups results under the folder holding them, labeled with the path under the folder searched. Flat is still the default.
	- Grouped drops the Location column, since the row above every match already says where it is. Switching either way is instant and does not run the search again.
	- Test case: `test-nemo-search-group`.

- ✅ Confirm mouse-movement-based actions that don't already ask for some kind of confirmation. (E.g. drag and drop to a new folder)
	- Opened: 20260730-112038
	- Closed: 20260903-174842
	- Note: a major enhancement to call out in README, e.g.: "Helps prevent one of the biggest pain points with GUI file managers: Accidental file & folder moves, sometimes without realizing it."
	- Done 20260903. A drop now names what it is about to do and where, and waits for an answer. Two settings under Behavior: moves ask by default, copies and links do not.
	- Covers every drop that moves files: the file list, the icon view, both sidebars, the path bar and the tabs. A drop on the Trash still asks under its own setting, not twice.
	- Test case: `test-nemo-drag-confirm` for the rule; not that every drop place asks it.

- ✅ Allow moving tabs to other windows.
	- Opened: 20260722-172504
	- Closed: 20260903-160000
	- Already worked, and was checked rather than written: a tab dragged onto another window's tab strip moves there, and one dropped on the desktop opens a window of its own.
	- What made it look broken is that a window showing a single tab has no strip to drop onto. Turning the new "always show a tab" option on gives it one.
	- Test case: `test-nemo-tab-move`, `test-nemo-window-at-point`.

- ✅ Option to always show a tab.
	- Opened: 20260722-172504
	- Closed: 20260903-160000
	- `preferences.always-show-tabs` in the settings file, off by default. The other tab options live there too rather than in the preferences dialog.
	- Test case: `test-nemo-notebook` (`test_visibility`).

- ✅ Tabs shouldn't take up the whole space, only what's needed for title (and a reasonable minimum width).
	- Opened: 20260723-133832
	- Closed: 20260903-160000
	- A tab is now as wide as its own title, between `preferences.tab-width-min-percent` (10) and `tab-width-max-percent` (25), both percentages of the tab strip.
	- Tabs used to be set to expand, which is why three of them split the width evenly whatever they were called.
	- Test case: `test-nemo-notebook` (`test_widths`), `test-nemo-path-forms`.

- ✅ Use the new program icon (`assets/logo.png`).
	- Opened: 20260902-193009
	- Closed: 20260903-140000
	- Every size is cut from the one logo by `cicd/utility/gen-app-icon.py`, and the output is committed.
	- ✅ Windows .exe. It carried no icon at all before, so it showed the toolkit's default. It now has one, from the file list up to the largest view.
	- Linux:
		- ✅ Desktop launcher and running icon. The `nemo-anywhere` app icon is redrawn at every size, with 48 through 256 added for launchers and larger views. The old green folder had a vector alongside it; the new art is raster only, so the vector is gone and the sizes cover its place.
		- ✅ n8runfm launcher. A dogfood copy now registers itself: the launcher writes a menu entry pointing at the stamped copy it is about to start, with the program icon taken from the copy's own art. Rewritten on each launch, since the copy is dated and moves.
		- ✅ Dogfood portion of CICD scripts. Nothing to do there. The launcher is what knows which stamped copy is current, so registration belongs to it, and the pipeline's own dogfood stage is disabled on Linux anyway.
	- Note: the window icon itself follows the folder being viewed, by design, so the program icon shows in the launcher and the switcher rather than in the title bar.
	- Test case: none, icon art; `fCheckWindowIcon` in the C lint keeps the window icon.

- ✅ Path button bar returns to buttons any time the path defocuses, not just on Escape.
	- Opened: 20260802-011216
	- Closed: 20260903-120000
	- Clicking into the file list, the sidebar or anywhere else puts the buttons back.
	- Switching to another program does not, so a half-typed path survives the trip.
	- No effect when the entry is the permanent choice in preferences.
	- Test case: `fCheckEntryFocusOut` in the C lint.

- ✅ Escape in the folder pane clears the selection, and Escape again puts it back.
	- Opened: 20260802-011216
	- Closed: 20260903-120000
	- Both the list and the icon views. Reaching the background menu from the keyboard is the point.
	- A rename or a stretch in progress still gets Escape first.
	- What was put aside is dropped on leaving the folder, so Escape in a new one has nothing to restore.
	- Since 20260919, Escape no longer puts anything back. See "Row visualization enhancement".
	- Test case: `test-eel-forget-cursor` for the list view; the icon view half needs a full view.

- ✅ In find mode the status bar shows the whole path rather than just the name.
	- Opened: 20260730-112038
	- Closed: 20260903-120000
	- Only for search results, where a name on its own does not say which file was found. Ordinary folders still show the name.
	- Test case: `test-nemo-status-name`.

- ✅ A value too long for its column gets a mouseover tooltip with the whole value.
	- Opened: 20260730-112038
	- Closed: 20260903-120000
	- Any column, not just the name.
	- Shown whatever the item tooltip preference says, since it is about reading what is already on screen.
	- Test case: `test-eel-clipped-cell`.

- ✅ Always operate on whole rows in list view.
	- Opened: 20260826-103001
	- Closed: 20260903-104500
	- A click anywhere in a row now belongs to that row. Right-clicking past the end of the name used to clear the selection and give the background menu.
	- The only background left is the space below every row, which still deselects and gives the background menu.
	- Right-clicking a row outside the current selection selects it first, then opens its menu.
	- Test case: `test-eel-treeview-hit` for where a row ends; not the list view's own click handling.

- ✅ Better program icon, for both file .exe and running program. (All supported platforms.)
	- Opened: 20260831-164337
	- Closed: 20260902-195000
	- Answered by the new logo, which "Use the new program icon" puts in place everywhere. Reopen if the art itself should change again.
	- Test case: none, icon art.

- ✅ Right-clicking the breadcrumb button for the folder being viewed should offer the same items as right-clicking the empty list background.
	- Opened: 20260826-103001
	- Closed: 20260902-194500
	- Only that one button. The ancestor buttons keep the shorter menu, which is what they had.
	- The two menus were compared side by side and match item for item; the parent button still gives the shorter one.
	- Test case: none, needs a full window; not worth building one for this.

- ✅ Windows: GUI testing in a throwaway sandbox, without touching the live console session.
	- Opened: 20260829-071437
	- Closed: 20260902-193949
	- What works today: a window can be photographed without disturbing anything (it is rendered off-screen, even behind other windows), and most behavior can be driven through the settings file, which is live-reloaded. Clicks and typing reach the app but take the mouse and the focus while they run.
	- Windows Sandbox is the way: a throwaway Windows built from the host's own image, so no second license, started from a small config file with a shared folder. A logon command inside it runs on its own desktop, which is exactly where the driving script has to be. It keeps no state and cannot reboot, so anything that spans a reboot still wants a Hyper-V guest (Hyper-V is already on; the guest would need an Enterprise evaluation image).
	- The rig is in: `cicd/win/sandbox.ps1` stages a shared folder with the app, generates the config and launches the sandbox; `sandbox-agent.ps1` runs at logon in there and works through queued job scripts, writing its results back to the share. `cicd/win/gui.ps1` is the window driver both sides use.
	- `-Dir` takes a whole flattened build instead of the packed exe, so a rebuild can be looked at without packing first. That is the form to use while working.
	- First run inside is clean: the app came up with its menus, icons and columns, and the first-run bookmark seeding worked on a profile that had never seen it.
	- Note: split from "Windows: Need to figure out a way to do GUI testing and demo recording", which stays open for demo recording and anything that spans a reboot.
	- Test case: none, test tooling.

- ✅ The "Open with" submenu names programs by their file name.
	- Opened: 20260902-190000
	- Closed: 20260902-191500
	- It read "Code.exe" and "VSCodium.exe" where the menu item above it already said "Open with VSCodium". The list comes from the toolkit, which has no name for a program beyond the file it found.
	- Fixed: every entry is now named the way the default one already was, from the program's own description, falling back to the file name for a program that carries none. The list sorts by what it shows, so the order matches too.
	- Test case: `test-nemo-associations-win32` (`test_names`), Windows only.

- ✅ "Open With": Opening two text files in VSCodium, should open them in the same editor instance. (E.g. as it works when doing so from nemo-anywhere on Linux, or from Explorer on Windows.)
	- Opened: 20260831-164337
	- Closed: 20260902-190000
	- Opening from the file list, or from the first menu item, was already right: both files reach the running editor.
	- The Open With submenu was not. Everything on that list comes from the toolkit rather than from nemo's own reading of the registry, and those entries were started a different way - directly, as a child of nemo.
	- Two things went wrong because of it. In the packed build the editor came up with a blank window, because a program started as our child inherits the packing, and it also inherited nemo's own environment rather than the user's.
	- Fixed: anything with a command line behind it is now started the same way, whichever list it came from. A store app is the one kind that has none, and still goes the old way.
	- Checked in both builds: the editor is started by the desktop rather than by nemo, comes up normally, and both files open in the one window.
	- Test case: `test-nemo-associations-win32` (`test_command_for_app`), Windows only, and `fCheckWinLaunch` in the C lint.

- ✅ Copying and pasting objects that includes symlinks or junctions, should open up an option dialog. (All OSes.)
	- Opened: 20260831-164337
	- Closed: 20260902-170000
	- Ask whenever the source holds links, on any platform, so a user always knows what they are getting. The dialog names what the source holds, with a row per kind:
		- File symlinks as: symlinks, or copies.
		- Folder symlinks as: symlinks, junctions [Windows], or copies.
		- Folder junctions [Windows] as: junctions, symlinks, or copies.
	- Anything the destination cannot take is greyed out. Junction-related options not shown for non-Windows OSes. Each row starts on the same kind if that is possible, otherwise the nearest kind that still points at the original target, otherwise copies.
	- Done, and on every platform. The dialog names only the kinds the source actually holds, and grays out anything the destination cannot take - including the case where it can take none, where it says why and only the copy is left.
	- Windows had the real gap: a copy always followed the link and left the contents behind, so a link could not be copied at all. POSIX already kept symlinks; what is new there is being able to ask for the contents instead.
	- The kind of a Windows link comes from the reparse tag. Nothing else tells a junction from a folder symlink, and it also keeps cloud placeholders and store app aliases - which are reparse points too - from being read as links.
	- A link keeps its own spelling, so a relative one still points where it pointed. Asking for a junction is the exception: those can only name a full path, so a relative target is resolved first.
	- A link now counts as one item in the copy rather than a folder to walk into, which is what POSIX always did and Windows never did.
	- Test case: `test-nemo-link-copy`, `test-nemo-link-copy-job`.

- ✅ Drag and drop a file to a program should work. (E.g. a '.md' or '.txt' file to VSCodium or Notepad.)
	- Opened: 20260831-164337
	- Closed: 20260902-000000
	- Windows only. Linux drags already reach any program, GTK or not.
	- The toolkit does drive a drag on Windows, but the file formats other programs read were never filled in on its side, and there is no way to add them from outside it. So the drag is ours now, the way the clipboard is.
	- Done. A drag out of either view carries what Explorer's own drags carry, so other programs read it. Checked in the running app both ways: dropping on Explorer, dropping a text file on an editor, and dragging inside nemo, which still moves files as before.
	- A move out to another program now removes the original, unless that program moved it itself or the drop came back into nemo. Control copies and shift moves, the way Windows does it.
	- Drops coming the other way, from another file manager into nemo, copy and move too. They always copied before: nothing is known about a file dragged in from elsewhere, so nemo could not tell whether it was on the same drive and fell back to copying every time.
	- Checked against Directory Opus in both directions, and between two nemo windows: a plain drag moves within a drive, control copies, shift moves.
	- Test case: `test-nemo-dnd-win32` (Windows only), `test-nemo-drop-action`; removing the original after a move out needs a real drop.

- ✅ Move all Windows-related options to a "Windows" preferences pane; and in the config file, to a grouped section.
	- Opened: 20260831-164337
	- Closed: 20260901-183000
	- Some cannot move. Such as "Owner" in list columns.
	- The page carries Light and dark, Theme, Paths, Hidden files and Search, and takes the slot Appearance used to have. It is hidden everywhere else, so Linux no longer offers theme or light/dark settings - those come from the desktop there. The keys still work if hand-edited.
	- "Shortcuts" stayed on Display: .desktop launchers hide their extension too, so it is not a Windows-only setting.
	- Hidden files is a new group with two switches, for the native hidden attribute and for dot names. Both stay on the View menu on Windows, where Show Hidden Files moves the pair together; elsewhere the menu keeps the one meaning it has always had.
	- Config keys moved to a `windows` group: path-separator, allow-slash-input, show-dot-files, use-search-index, associations, terminal-candidates. Existing settings files lose those values, which is accepted before 1.0.
	- Test case: `test-nemo-config-schema`, `test-nemo-prefs-widgets`, `test-nemo-dot-files-win32`.

- ✅ If "Show path in tab" option is enabled, don't show the path twice - shorten it. For example:
	- Opened: 20260831-164337
	- Closed: 20260831-201500
	- Current: "github - C:\opt\0-0\users\collierjr\data\prs\dev\github.com\t00mietum\nemo-anywhere\github"
	- Better: "C:\opt\0-0\users\collierjr\...\nemo-anywhere\github"
	- The folder name in front of the path is gone - the path already ends with it.
	- A path too long for a title keeps its root and its last two folders, with the middle left out. The root says which drive or share it is on, the end is what tells one tab from another.
	- Test case: `test-nemo-path-forms`.

- ✅ .Lnk folder icons should use the same folder icons as the theme, but with an overlay.
	- Opened: 20260831-164337
	- Closed: 20260831-193000
	- The shell hands back its own folder art for a shortcut to a folder, which looks nothing like the folders around it. The theme's folder icon is used instead, with the shortcut overlay on top.
	- Whether the target is a folder is read from what the .lnk itself records, not by looking at the target - a shortcut to a share that is not answering would otherwise cost about twenty seconds on the draw path.
	- Test case: `test-nemo-emblems`, `test-nemo-lnk`, `test-nemo-shortcut-win32`.

- ✅ All .lnk files should have a .lnk overlay (similar to how Explorer does it).
	- Opened: 20260831-164337
	- Closed: 20260831-193000
	- New overlay, drawn as an arrow in a white box the way the shell does it.
	- .desktop launchers get the same one, on every platform.
	- Test case: `test-nemo-emblems`.

- ✅ All symlinks and junctions should have an overlay, but different from .lnk.
	- Opened: 20260831-164337
	- Closed: 20260831-193000
	- Ditto for Linux symlinks, and .desktop files.
		- Like .lnk files, don't show ".desktop" in listings, except when renaming.
			- When renaming, show both .lnk and .desktop extensions.
			- Still show both extensions in the "Ext" column.
		- .desktop files can use the same overlay as Windows .lnk, if necessary/convenient.
	- Symlinks and junctions get a chain-link overlay, so the two never read the same.
	- Both overlays are ours rather than the theme's. An icon added by resource path is only searched after every theme, so a theme that carries its own symlink emblem would always win - and most of them draw the same arrow the shell uses for a shortcut.
	- .desktop now behaves like .lnk: the extension is off the listing, on in the rename box, and still in the "Ext" column. One preference covers both, and it is no longer hidden on Linux.
	- Checked on a junction. A real symlink could not be made without Developer Mode, but both are reparse points and read the same way.
	- Test case: `test-nemo-emblems`, `test-nemo-shortcut-name`.

- ✅ Preferences|Context menus still has a "Desktop" group with a "Customize" box in it. Nothing is behind it. Remove.
	- Opened: 20260831-170000
	- Closed: 20260831-172500
	- Found while taking out the desktop tooltip box.
	- The action it was meant to show never existed in this fork, so the box could only ever hide something that was not there. Gone, along with its setting.
	- Test case: none, removed feature.

- ✅ Remove bottom-left buttons, and bottom-right zoom bar in list view.
	- Opened: 20260831-164337
	- Closed: 20260831-170000
	- Make bottom bar vertically thinner (since don't need room for the buttons on bottom-left any more; just room for status text, and zoom slider in icon view).
	- The four sidebar buttons are gone from the bottom bar. Switching between places and tree, and hiding the sidebar, are still on the View menu and on F9.
	- The zoom slider now shows only where it does anything useful - icon and compact views. List view sizes itself off its columns.
	- The bar is about half its old height, since nothing in it needs button room any more.
	- Partly reversed on 20260916. The places and tree buttons are back on the bottom-left, now as independent toggles rather than a radio pair, and beside them one button collapses both panes. The bar grows again to fit them. The zoom slider rule from this item is untouched. See "Places and TreeView can both exist at the same time".
	- Test case: none, look only, judged by eye.

- ✅ Preferences|Preview: "Show tooltips on the desktop" (and related checkboxes) have no meaning. Remove.
	- Opened: 20260831-164337
	- Closed: 20260831-170000
	- Removed, along with the setting behind it. Nothing read it once the desktop shell went.
	- The icon-view and list-view tooltip boxes stay - those still do something. So do the boxes choosing what a tooltip shows.
	- Test case: none, removed feature.

- ✅ By default, disable all checkboxes related to Preferences|Behavior|Media handling.
	- Opened: 20260831-164337
	- Closed: 20260831-170000
	- Automount, automatic open of a mounted disk, and content detection all start off now. The fourth box in that group was already off.
	- Test case: `test-nemo-config` (`test_product_defaults`).

- ✅ Gray out "Open as Administrator", if already running as such.
	- Opened: 20260831-164337
	- Closed: 20260831-170000
	- Windows has no root account to test for, so the item used to stay live in an already elevated copy and a second prompt bought nothing. The process token is asked instead.
	- Test case: none, a plain read of the process token; the menu state needs a full window.

- ✅ Windows: show the icon the shell would show for .lnk files (without requiring Explorer to run).
	- Opened: 20260827-090000
	- Closed: 20260829-103000
	- Split out of the done item for opening a shortcut the way Explorer does. A shortcut showed a generic icon rather than its target's - the toolkit reports one flat icon for every file on Windows.
	- Fixed: a shortcut is drawn with the shell's own icon for it, at each of the shell's sizes rather than scaled from one, and cached. No Explorer process is involved.
	- Note: the shell's icon for a registered file type (a .docx drawn as Word's) is deliberately not used for ordinary files - it would fight the icon themes. The lookup is by path and could be widened later.
	- Test case: `test-nemo-shell-icon-win32`, Windows only.

- ✅ Windows: edit a `.lnk`'s target from a properties view - the analog of the `.desktop` launcher editor.
	- Opened: 20260826-103001
	- Closed: 20260829-100000
	- Added: Properties on a shortcut shows Target, Arguments, Start in and Comment below the name, each saved as it is edited. A file dropped on Target or Start in fills it in. A shortcut with no file target still opens for editing.
	- Test case: `test-nemo-shortcut-win32` (`test_info_round_trip`), Windows only; filling a field by drop needs a full window.

- ✅ Windows: no shell coupling for file associations - read them from the registry (system defaults only), layered under an override map of our own.
	- Opened: 20260730-203115
	- Closed: 20260829-093000
	- Overrides launch directly. All settings and overrides live in the settings file, never written to the registry.
	- Fixed: the default for a type is the override when one is set, else what the shell itself would open it with, asked the way Explorer asks. The toolkit's own answer could be a print command, and its Open With list carried print entries too; those are gone.
	- Fixed: "Set as default" in Open With records the choice in the settings file, one line per type in the registry's own `%1` shape, and Reset takes it away again.
	- Note: a program is shown under its own description (Notepad, VSCodium), the way Explorer names it.
	- Test case: `test-nemo-associations-win32` (`test_overrides`, `test_registry`), Windows only.

- ✅ Bookmarks are kept in the toolkit's own file, not ours.
	- Opened: 20260828-133604
	- Closed: 20260829-090000
	- Only relevant on Windows. The toolkit's file sits in the local profile while the settings are in the roaming one, so a roaming profile carried the settings and left the bookmarks behind.
	- Fixed: on Windows the list lives beside the settings. A list an older version kept in the toolkit's file is copied across the first time, and a reset clears both so the old list cannot come back.
	- Test case: `test-nemo-first-run-win32` (`test_toolkit_list_copied_across`), Windows only.

- ✅ Windows: content search cannot read documents, because the search helpers are not packaged there.
	- Opened: 20260828-160000
	- Closed: 20260829-083000
	- On Linux a helper turned a document into text so "Containing:" could search it. The Windows layout carried the executable and the toolkit and nothing else, and three of the helpers were a Python script, a shell script and a LibreOffice call.
	- Fixed: the converters are plain C and ship on every platform. Word, Excel and PowerPoint in both the old binary and the newer zip-of-xml forms, OpenDocument and EPUB. The scripts and their dependencies are gone.
	- Fixed: a helper is looked for beside the main program before the search path, so a name that has to gain `.exe` is found all the same.
	- Fixed: a helper's `Priority` is honoured. One helper runs per file; the next is only tried when it cannot read the file at all.
	- Added: a switch in Preferences to answer searches from the Windows Search index for folders it covers. Off by default. Folders outside the index, and content searches by pattern or by case, are still searched directly.
	- Test case: `test-nemo-search-helpers` for the converters, `test-nemo-search-content` for helper priority and fallback (POSIX only), `test-nemo-search-win32` for the index, Windows only.

- ✅ Windows and NTFS: any directory symlink through any mechanism should also allow a junction, preferred over a symlink.
	- Opened: 20260823-142431
	- Closed: 20260828-151500
	- The hidden-files half of this item became "Two kinds of hidden file, two options", now done - it asks for the same thing as two switches rather than one.
	- Note: superseded in part. Make link now asks, and offers a junction or a symlink for a folder. See [20260930-145641_moving_and_copying.md](design_docs/20260930-145641_moving_and_copying.md).
	- A link to a folder is now a junction. One place decides it, so every route into "Make symlink" gets the same answer, and a symlink is still the fallback for anything a junction cannot hold - a file, a share, a relative target.
	- The point of preferring one: a junction needs no privilege. Making a folder link no longer wants Developer Mode or an elevated run, and the menu item stops graying out for a folder on a machine that has neither.
	- Verified: a folder link made from the menu reads back as a mount point rather than a symlink, and a new check covers it.
	- Test case: `test-nemo-link-copy` (`check_link_options`, `check_kinds`), `test-nemo-make-link-job` (`check_every`).

- ✅ New flag: `--reset`. Clears bookmarks, resets to default state. (Maybe just delete the config file?)
	- Opened: 20260730-112038
	- Closed: 20260828-133604
	- Every stored setting is dropped and the settings file itself is removed, so anything hand-written that nemo does not recognize goes too. Bookmarks and their side file go with it.
	- It refuses while a copy is running, and says so. That copy holds the settings in memory and would write them straight back.
	- The first-run marker is cleared along with everything else, so the next start puts the platform defaults back.
	- Test case: `test-nemo-instances` for `--reset`, `test-nemo-first-run-win32` (`test_reset_files`, `test_reset_all`) on Windows.

- ✅ If the Windows version has never run before, the bookmarks should be cleared, and populated with only the main Windows defaults. (C:\, Desktop, Documents, Downloads, Pictures, Videos, AppData). Also, all linux-specific settings and bookmarks should be cleared on first startup.
	- Opened: 20260722-172504
	- Closed: 20260828-133604
	- On the first start the drive root and the user's own folders go in, taken from what Windows reports rather than spelled out, so a machine on another drive or in another language gets the right names.
	- A bookmark that can only be a path from a POSIX machine is dropped, and so is any setting whose value is one. A set someone already curated on Windows is kept rather than replaced - that matters for anyone upgrading from a build without the marker.
	- Marked by `state.first-run-done` in the settings file. Clearing that line by hand puts the defaults back on the next start.
	- Test case: `test-nemo-first-run-win32` (`test_seeds_defaults`, `test_foreign_bookmarks_dropped`, `test_foreign_settings_dropped`, `test_windows_settings_kept`), Windows only.

- ✅ Allow '~' in bookmarks to specify home dir (only if at the start and unquoted).
	- Opened: 20260722-201512
	- Closed: 20260828-133604
	- `~` at the start, and `%NAME%` or `$NAME` anywhere. Both variable spellings work on both platforms so a path can be carried between them.
	- The literal text still wins: a folder really named with a `%` in it opens as itself, and only a name that is actually set in the environment is ever substituted. Verified both ways.
	- Reaches the location bar, the bookmark editor and the command line.
	- ✋ Not done: storing the shorthand *in* the bookmarks file so it follows the home folder around. That needs the file to keep an unexpanded form and re-expand on load, which is a bigger change than the input side.
	- Test case: `test-eel-user-input` (`test_expansion`).

- ✅ Windows: an option to leave the `.lnk` off a shortcut's name.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- The shell never shows it, so nor do we unless the new switch on the Display page is turned on. Off by default.
	- Only the name shown loses the extension. The Ext column still says `lnk`, and a rename typed as the shown name puts the extension back, the same way a renamed `.desktop` file keeps its own - without that a rename would quietly turn the shortcut into an ordinary file.
	- Test case: `test-nemo-shortcut-name` (`check_shown_name`, `check_rename_keeps_extension`).

- ✅ Let the Type column take the width it needs when there is room for it.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- It was held to twice the width of the Ext column, so it read "Folde" and "Link t" in a window with plenty of room to spare.
	- That ceiling is gone. Type still gives its width back first when the window is too narrow, and still stops at a share of Name so one long value cannot take the row.
	- Note: This may contradict the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: none, replaced by the column width rule in design.md, whose arithmetic `test-nemo-column-layout` covers.

- ✅ The preferences dialog opens too short for the Display page, and does not follow a fractional display scale.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- It sized itself to the Views page alone, so every longer page opened behind a scrollbar. Measured: Views 690, Display 832, Behavior 1045, against an opening height of 700.
	- It now measures every page and takes the longest, and the width the widest page needs, both still capped at nine tenths of the screen.
	- The minimum size it will not go below is written in pixels for a 96dpi screen, so at 150% it quietly meant two thirds of what it said. It is scaled by the same font size Windows hands the toolkit.
	- A check now compares the page list the sizing walks against the pages the dialog actually holds. It found one missing on its first run - Document templates, whose page had no name at all.
	- Test case: `test-nemo-prefs-dialog` (`test_size`), `test-nemo-prefs-widgets` (`check_pages_are_all_listed`).

- ✅ The two command fields on the Behavior page crowd their labels and run past the section.
	- Opened: 20260828-083458
	- Closed: 20260828-090000
	- Four pixels between the label and the field, and the field itself pushed past the right-hand margin every other section keeps.
	- Twelve pixels now, the field stops where the rest of the page does, and the two fields start at the same place as each other.
	- Test case: none, spacing only, judged by eye.

- ✅ Move the Ext column between Size and Type in the default order.
	- Opened: 20260827-183930
	- Closed: 20260827-194220
	- It sat between Name and Size. It stays on by default either way.
	- Both platform defaults moved, and the schema with them.
	- Test case: none, a default value; `test-nemo-config-schema` keeps the schema in step with it.

- ✅ Name and Location split the search row evenly.
	- Opened: 20260827-183930
	- Closed: 20260827-194220
	- The default was a third to Name and the rest to Location.
	- A split dragged by hand still stands from then on.
	- Test case: none, replaced by the column width rule in design.md; `test-nemo-column-layout` (`check_primaries_share_the_surplus`) covers the sharing.

- ✅ Location is off by default outside search.
	- Opened: 20260827-183930
	- Closed: 20260827-194220
	- It belongs in the search results list and nowhere else, unless it is turned on by hand.
	- Already the case: it is in the default column order but not the default visible list, and a run against a clean config confirmed it does not appear. Wherever it was seen, it had been turned on for that folder and remembered.
	- Test case: none, already the default, confirmed rather than changed.

- ✅ A preference for which terminal "Open in Terminal" runs.
	- Opened: 20260827-183930
	- Closed: 20260827-194220
	- One field on the Behavior page, holding the command line. Anything the program needs beyond its own name is typed in by hand.
	- Left empty it means the platform default, which is what happened before: the desktop's own choice on Linux, the first of the known shells found on PATH on Windows. Filled in, it wins over both.
	- Splitting one field into a program and its arguments has three rules, in order: a quoted first word, then a string that names a program on its own (so an unquoted path with spaces still works), then the first space. Tested.
	- Verified on Windows: a terminal named with an argument is launched exactly as written.
	- Test case: `test-nemo-desktop-terminal` (POSIX only), `test-nemo-view-win32` for splitting the command line, Windows only.

- ✅ Make link is on by default, and Windows tells a shortcut from a symlink.
	- Opened: 20260827-183930
	- Closed: 20260827-195422
	- The menu item shipped turned off.
	- Renamed "Make symlink" on every platform, since a symlink is what it makes.
	- Windows gained a second item, "Make shortcut", for the .lnk the shell understands. Both are on by default and share one switch in Context menus - two toggles for nearly the same thing would only be confusing.
	- Windows allows a symlink only with Developer Mode on or when running elevated, so the item goes gray when neither holds. The check is made once by making a throwaway symlink and deleting it, which is a plainer answer than reading a token and a registry key.
	- A drag with the link modifier still makes a shortcut on Windows, which is what Explorer does.
	- Both verified on Windows against a real folder. Not covered: the grayed-out state, which needs a box without Developer Mode; and an undo-then-redo of a symlink remakes it as a shortcut, since both share one undo record.
	- Test case: `test-nemo-link-copy` (`check_link_options`) for the fallback; the grayed item needs a box without Developer Mode.

- ✅ Update the vendored SHCL to the current release.
	- Opened: 20260826-103001
	- Closed: 20260827-075015
	- It manages its own file creation and updating now, which is one of the rough edges hit here.
	- Note: Fetch it from the source.
	- Moved from 1.2.0 to 2.0.0. Nothing in the settings layer had to change: none of the calls made here changed shape, and neither of the two breaking changes is reachable from plain key names.
	- What comes with it: parsing holds roughly half the memory it did and loads faster, number handling no longer follows the host locale (under a comma-decimal locale every float read used to fail and the canonical output diverged), and a line that is malformed but still placeable is now kept and written back instead of dropped.
	- Its new file tier was deliberately compiled out at first. The writer reaches Windows through the ANSI calls, which are the system codepage unless the exe asks for UTF-8, so a config under a non-ASCII user name would fail to save.
	- Taken on 20260828, once the manifest asked for UTF-8. Settings now save through it: a temp file beside the target, flushed to disk before it is published, and on Windows a replace that carries the old file's permissions, attributes and alternate streams onto the new one. The previous writer published a brand-new file and left all of that behind.
	- Reading stays where it was. The library reads a file with no size limit, and its allocator ends the process rather than failing, so the cap in front of it stays; the reader also hands back the exact bytes the "was this our own write" check compares against.
	- The trap: the library names its temp file by splitting the path on a forward slash and nothing else, so a Windows path spelled with backslashes puts the temp somewhere impossible and every save fails. The path is handed over spelled with slashes. The existing config checks caught this immediately.
	- Test case: `test-nemo-config` (`test_persistence`, `test_external_edit`, `test_float_under_comma_locale`, which skips with no comma locale installed), plus the SHCL fuzz target.

- ✅ Ask for UTF-8 as the process codepage in the Windows manifest.
	- Opened: 20260827-075015
	- Closed: 20260828-142000
	- Windows 10 1903 and later read `activeCodePage` and make every narrow call UTF-8. Without it a narrow call anywhere in the process is at the mercy of whatever codepage the machine is set to, which is how a non-ASCII user name breaks things that otherwise look fine.
	- Not free: it changes the codepage for everything in the process, not just our own calls, and it does nothing on the older versions the manifest still claims. Wants a look at what else narrows before it goes in.
	- Looked. Nothing of ours narrows: every Windows call in the tree is the wide form, and the only conversions are explicit UTF-8 ones. What the change reaches is the libraries underneath and the C runtime, which is the point of it.
	- In: the code page reads 65001 with the manifest and 1252 without. The app was run with its config under a folder named in German and Japanese, and read, wrote and live-reloaded it. Suite unchanged.
	- Follow-on, taken: the config engine now saves through its library's own writer. See the SHCL item above.
	- Test case: `test-nemo-manifest-win32` (`check_config_round_trip`), Windows only.

- ✅ The whole `desktop` group of settings is dead weight.
	- Opened: 20260827-075015
	- Closed: 20260827-081500
	- Fifteen keys left behind when the desktop shell came out. They still ship in the schema and still appear in a generated starter config, so a user can set them and nothing happens.
	- Two of the fifteen are not clearly dead on a quick look - one leaf name is shared with a live setting in another group - so this wants checking key by key rather than deleting the group.
	- Checked key by key. Twelve had no reader anywhere and are gone from the table, the schema and the preference names. Three still have live readers and stay: the deprecated manage-the-desktop switch, the grid switch, and the desktop text ellipsis limit, which shares its leaf name with the icon view's own.
	- Test case: none, nothing checks that every key still has a reader.

- ✅ Windows: open a `.lnk` the way Explorer does, by what it points at.
	- Opened: 20260826-103001
	- Closed: 20260827-090000
	- A shortcut to a file opens in the file's associated program.
	- A shortcut to a program runs it.
	- A shortcut to a folder goes to that path in the current tab.
	- Use an appropriate icon.
	- Note: following a shortcut through to its target already works. What is missing is treating each kind of target differently.
	- A shortcut to a folder still opens in the current tab, the one case where the shell's way is not followed. Everything else is now handed to the shell as the shortcut, not as its target.
	- That is what fixes the program case. A shortcut carries a command line, a working directory and a window state, and none of them survive being reduced to a target path - a shortcut to a shell with arguments used to open a bare shell. Shortcuts to virtual items (Recycle Bin, a control panel page) now open too, having no path to reduce to in the first place.
	- Verified: a launched shortcut's arguments and working directory both arrive.
	- Icon split off below - it is a bigger piece than the rest of this and applies to more than shortcuts.
	- Test case: `test-nemo-shortcut-win32` (`test_open_action` and the launch block), Windows only.

- ✅ Show a build number in `--version`, `--about`, Help > About, the Windows splash screen, and the release notes.
	- Opened: 20260826-103001
	- Closed: 20260827-100000
	- The build number is the minutes elapsed since the start of 2000, Crockford base32 encoded, lower case.
	- General format: "<program name> v<version> build <build>" [copyright ...]
	- Five characters at the moment. It comes off the same commit date the reproducible builds already use, so two builds of one commit agree; a build outside the release lanes falls back to the clock.
	- `--about` is new, and prints the version line, the copyright, the project home and the license. Help > About gained the copyright and a link to the project, which it had never shown.
	- Checked on both platforms: the two command line outputs, the About dialog, and the splash.
	- Test case: `test-nemo-cli-version` (`check_prints`), POSIX only.

- ✅ Ctrl+H toggles dot-files and Windows hidden files together.
	- Opened: 20260826-103001
	- Closed: 20260827-110000
	- If the two are out of step, take the Windows hidden setting as the current value and match the other to it.
	- Ctrl+Shift+H stays as it is, Windows only.
	- Both the setting and the dot-file menu item move with it now. The item had to be ticked directly - neither of the two toggles watches for a change made anywhere else, they are only read when the menus are built. That wider gap is left for later.
	- Nothing changes off Windows, where one switch already covered both.
	- Test case: `test-nemo-dot-files-win32` (`test_ctrl_h_brings_them_together`), Windows only.

- ✅ Ctrl+, opens Preferences.
	- Opened: 20260826-103001
	- Closed: 20260827-110000
	- Test case: none, a single accelerator entry.

- ✅ Build timestamps come from the commit being built, not the clock, so a release can be reproduced.
	- Opened: 20260826-115717
	- Closed: 20260826-152000
	- The Windows exe was the one that really varied: the linker writes a timestamp into the PE header, and two clean builds of the same commit differed in exactly those four bytes. Everything else was already close.
	- Every lane now sets `SOURCE_DATE_EPOCH` to the commit date and hands it to whatever stamps a time. `zip` has no notion of it, so the staged tree gets the date set on disk and is packed in sorted order; `tar` is told explicitly; the rpm spec has to ask for it before rpm will read it.
	- Verified by building each artifact twice from scratch: the Windows exe, the Linux tarball, the .deb, the .rpm and the Windows zip all came out byte-identical. A build with the stamp removed differed, which is the check that the mechanism is what did it.
	- Also fixed on the way through: the Linux release lane had been failing since the staging script gained a safety guard on its destination name, which no longer matched what the release script passed it.
	- Not covered, and cannot be: a signed exe, since the countersignature carries the real time of signing.
	- Test case: none, release pipeline; proving it takes two full release builds.

- ✅ Windows: two kinds of hidden file, two options.
	- Opened: 20260823-140628
	- Closed: 20260823-142431
	- Supersedes the older item that asked for the same thing as one combined switch.
	- The premise turned out to be worse than described: Windows reports only its own hidden attribute, so dot-files were shown there whatever the setting said. Same for names ending in a tilde, which count as backups elsewhere.
	- "Show dot-files" is now a second switch, Ctrl+Shift+H, next to "Show hidden files" in the View menu and hidden on every other platform, where one switch still covers both. Default is to hide them.
	- The two are independent: revealing attribute-hidden files no longer reveals dot-files, and the listing, the tree sidebar and search all go through the same check.
	- Flipping either one re-reads the open folder, so an edit to the settings file shows up without a restart.
	- Test case: `test-nemo-dot-files-win32` (`test_hidden_by_default`, `test_switch_reveals_them`, `test_independent_of_show_hidden`), Windows only.

- ✅ Windows: choose which separator paths are shown with.
	- Opened: 20260823-140628
	- Closed: 20260823-144331
	- A "Paths" group on the Display page of Preferences, shown only on Windows: "Show separator as" picks `\` or `/`, and a checkbox below it accepts or refuses `/` in a typed location.
	- The checkbox is ticked and grayed out while `/` is the separator on screen, since refusing what is being shown would make no sense.
	- The choice reaches every surface that spells out a path: the location bar, the Location column, path tooltips, the window and tab titles, the sidebar tooltips, drive roots in the sidebar, and the Location row in properties. Breadcrumbs show names only, so there was nothing to change.
	- Changing it re-reads the open folder, so the whole window switches over at once rather than on the next visit.
	- Typed input already took both separators, so what is new is the option to turn `/` off. A location that leans on it is then refused with a beep instead of going anywhere.
	- Also fixed on the way past: the preferences dialog named a widget in a size group that no longer exists, so loading it stopped early and silently. Only an unused list model came after the break, which is why nothing looked wrong.
	- Test case: `test-nemo-path-separator-win32`, Windows only.

- ✅ Windows: "Copy path as [\|/]".
	- Opened: 20260823-140628
	- Closed: 20260823-145852
	- A second clipboard item directly below the existing Copy Path one, spelling out whichever separator the paths are not currently shown with. It follows the same show/hide setting as the first, so the pair travels together.
	- In all four places the first one appears: the Edit menu, the selection menu, the background menu and the breadcrumb menu. Hidden on every other platform.
	- The existing Copy Path now follows the display setting too, so the pair is always "what you see" and "the other one". A remote location still contributes its uri untouched, since a uri's slashes were never separators.
	- Test case: `test-nemo-path-list`.

- ✅ Windows: "Open with Explorer", for a single selected entry.
	- Opened: 20260823-140628
	- Closed: 20260823-145852
	- On the selection menu, below Open With. Shown only when exactly one thing is selected and it has a local path, since a remote location gives Explorer nothing to open.
	- A folder opens in Explorer; anything else is revealed and picked out inside its own folder.
	- A deliberate escape hatch rather than a dependency. The standing "depend on Explorer as little as possible" rule is about core function; this one says Explorer on the label.
	- Two routes, because one is not enough: the shell item API, which handles any name, falling back to a command line when that is refused. Running elevated is when it gets refused, and nemo can be running elevated - "Open as administrator" puts it there.
	- Both routes verified. The item opens a window, so it sits behind `NEMO_PROBE_EXPLORER` rather than running on every pass of the suite.
	- Test case: `test-nemo-view-win32` (`check_quote`) for the fallback command line; opening Explorer only runs with `NEMO_PROBE_EXPLORER` set, since it opens a window.

- ✅ Column widths and the Ext column, second pass. Overrides the earlier column rules where they disagree.
	- Opened: 20260823-130540
	- Closed: 20260823-134341
	- "File extension" is now just "Ext", and shows the extension without its leading dot. It sits directly right of Name, with Location next along whenever that is switched on.
	- Location, on an ordinary folder listing, grows with Name rather than stopping at a share of it: the two split whatever the other columns leave and Name takes no more than half, so Location is never the narrower of the pair and anything Name does not need goes to Location. Dragging Location by hand ends that and pins the width, as it always did.
	- Date created, Date modified and Date read keep their full width. What has to give when the window is too narrow comes off Name, Location and Type first, in proportion, and only reaches the dates once those three are down to their floors.
	- Type, and any other column with no natural length, never ends up wider than Name or Location.
	- Zooming in or out re-measures the rows. Before this the widths were thrown away and never worked out again, so one Ctrl+= left Location taking most of the row and every date cut short. Same for a column switched on that had not been on screen to be measured.
	- A small gap keeps the first and last columns off the window frame.
	- All of it verified in the running app, the zoom case included.
	- Note: This contradicts the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: `test-nemo-filename-extension` for Ext, `fCheckZoomRemeasure` in the C lint for the zoom; the width rules were replaced by the one in design.md.

- ✅ The preferences dialog opens larger, and big enough for the Views page to fit without a scrollbar.
	- Opened: 20260823-130540
	- Closed: 20260823-134341
	- The height is measured from the page itself rather than fixed, so a different theme, font size or translation still fits, up to what the monitor has room for.
	- Test case: `test-nemo-prefs-dialog` (`test_size`).

- ✅ Ask before moving files to Trash defaults to on.
	- Opened: 20260823-130540
	- Closed: 20260823-134341
	- Already the default; confirmed rather than changed.
	- Test case: none, already the default, confirmed rather than changed.

- ✅ Right-click properties wording: ours is plain "Properties" and sits first; the Windows sheet reads "Windows properties (Alt+Enter)" below it. Shortcuts themselves are unchanged.
	- Opened: n/a
	- Closed: 20260822-075741
	- Seen in the running app; the breadcrumb menu says "Windows properties" without the hint, since Alt+Enter acts on the selection rather than a path segment.
	- Test case: none, wording only.

- ✅ New list columns.
	- Opened: n/a
	- Closed: 20260822-075741
	- "File extension", on by default, between Name and Type, dot included the way Explorer shows it. Left empty when the tail after a dot is not really an extension - folders, dot-files, too long, all digits, or not letters and digits. The refusals have a test of their own that fails with the checks taken out.
	- "Owner" now shows on Windows too and is on by default there - the platform reports the file's real owner, so the old fabricated-values reason to hide it no longer applied.
	- Windows only: "Permissions source" - Inherited, Local or Mixed, read from the file's ACL - off by default, listed after Owner. Verified against files with disabled inheritance and added grants.
	- Type now defaults to at most twice the File extension column's width.
	- Note: This may contradict the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: `test-nemo-filename-extension`, `test-nemo-owner-columns` (`check_windows_lookup`), `test-nemo-perm-source-win32` (Windows only).

- ✅ Column widths remember the user's hand. Overrides the earlier auto-sizing rules where they disagree.
	- Opened: n/a
	- Closed: 20260822-075741
	- A column with no natural width limit that the user resizes keeps that width as its ceiling from then on, through any window resizing in either direction, saved in settings.
	- Name still takes all remaining space - except in find mode, where Name and Location split the row one-third/two-thirds by default, and an adjusted split is remembered forever and kept as the window resizes. Supersedes the find-mode column note, now canceled.
	- Both verified in the running app: the dragged ceiling survives narrow-then-wide, and the find-mode split holds at the adjusted ratio across sizes.
	- Note: This contradicts the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: none, replaced by the column width rule in design.md.

- ✅ Properties on Windows opens the one Windows itself shows, instead of ours.
	- Opened: 20260821-180950
	- Closed: 20260821-204030
	- Alt+Enter, Ctrl+I and every Properties item now hand the selection to the shell's own sheet - the same one Explorer shows, third-party tabs included. Only Windows; Linux, BSD and macOS are untouched.
	- Ours stays on a second item, "Advanced properties" (Ctrl+Enter), because the Windows sheet has nowhere to put a custom icon, an emblem, an annotation or the image details page. It is hidden everywhere else, where both items would open the same window.
	- Anything the shell cannot name falls back to ours rather than doing nothing: a virtual location, a selection spanning folders (which is what a search result set is), or an item that has gone away since it was clicked.
	- The sheet runs off the main loop, so the window behind it stays live while it is open, and it is waited out rather than abandoned - the extra threads go when it closes.
	- Verified on Windows, and the fallback rule has checks of its own.
	- Test case: `test-nemo-properties-win32`, Windows only.

- ✅ Every piece of text in the interface reads as a sentence, not as a headline - only the first word capitalised, and anything that is a name left alone.
	- Opened: 20260821-180950
	- Closed: 20260821-211359
	- Menus, buttons, tab and page titles, dialog titles, column headings, tooltips, preference labels, and the bundled actions. About 330 labels in all.
	- A mnemonic stays where it was, so the underlined letter does not move; it is simply lower case now. Keyboard shortcut text is untouched.
	- Names keep their capital: the platforms, the toolkit, Trash and the other places in the sidebar, file and disc formats, acronyms. So does a sentence that names a menu item or a tab, since the item itself is still called that.
	- Left alone on purpose: the license text, which is quoted verbatim, and the name a new folder or document is given, which is written to disk rather than shown.
	- It is checked rather than trusted, because a label copied from upstream arrives in Title Case: `cicd/utility/lint-ui-case.py` reads every translatable string in the tree and fails the lint step on any that is not a sentence. The whole exception list lives in that one file, each entry with its reason.
	- The check found what a first pass by eye did not - the plural labels, where two spellings sit in one call, which is what had left "Copy Paths" and "Make Links" behind.
	- Test case: `cicd/utility/lint-ui-case.py`.

- ✅ One setting for how much of the machine's CPU any compression may use, as a percentage of the cores it finds. Default 50% - the best balance on a hyperthreaded CPU.
	- Opened: 20260821-140715
	- Closed: 20260821-144459
	- `performance.cpu-percent`, global rather than per-format, so a later job that can be spread over cores reads the same number instead of inventing one of its own.
	- Reaches the 7z and rar create lines through a `{{THREADS}}` marker of their own, and tar.xz through the library that writes it. Zip, gzip and the built-in 7z have no such option, so they are left alone rather than handed one they would refuse.
	- It is the one marker that does not stand for a control in the Compress dialog, so a line edited past it says nothing - the program simply picks for itself.
	- Rounds up, so a single-core machine still gets one thread and the answer is never nothing.
	- Verified: each program is handed the switch it spells its own way.
	- Test case: `test-nemo-archive` (`check_cpu_share`, `check_commands`).

- ✅ Per-monitor DPI aware where the platform offers it, and DPI aware at minimum everywhere else.
	- Opened: 20260821-140715
	- Closed: 20260821-150232
	- The Windows executable now carries an application manifest, which is where this is declared and where Windows reads it before any of our code runs. Per-monitor v2 where it exists, per-monitor v1 and then system-wide on older builds.
	- Without it the whole window was stretched as a bitmap on a scaled display - blurry - and a second monitor at a different scale could not be followed at all.
	- The toolkit scales in whole steps only, so a display at 125% or 150% would come out at 100% and read smaller than every other window on that screen. Text is scaled to the monitor's real DPI on top of that, which is not restricted to whole steps, and re-reads it whenever a window moves to a monitor at another scale or a monitor is plugged in. Widgets and icons stay on the whole step.
	- Nothing was needed for Linux or BSD: X11 and Wayland desktops publish their own scaling and the toolkit already follows it.
	- The manifest also declares the run level explicitly (unchanged - what we already had by having none) and the versions of Windows we have run on, so the version APIs stop reporting Windows 8 forever.
	- Verified on this box: the running process reports per-monitor awareness and its window reports the v2 context. The scaling sum is covered by a test. This box runs at 100%, so the fraction itself rests on arithmetic. It still needs a look on a scaled display.
	- Test case: `test-nemo-dpi-win32`, `test-nemo-manifest-win32` (`check_dpi`, per-monitor v2 only checked in a desktop session), Windows only.

- ✅ F2 selects the whole name, extension and all, rather than just the part before the dot. Settings tunable, for anyone who wants it the other way.
	- Opened: 20260821-140715
	- Closed: 20260821-150232
	- Both views. A folder was already selected whole; a file now is too.
	- `preferences.rename-selects-whole-name`, a file-only setting with no control in Preferences.
	- Verified in the running window: F2 on a `.md` file opens the box with the suffix inside the selection.
	- Test case: `test-eel-rename-region` (`check_setting`).

- ✅ List view columns use the window as it is resized, instead of being pushed off the end of it or leaving a gap.
	- Opened: 20260821-140715
	- Closed: 20260821-153301
	- Widening: columns take the new space until one can show the longest value in it, and then that one stops. Name is the only column that keeps growing without limit, so once everything else has what it needs the rest is Name's.
	- Narrowing, which is the same thing read backwards: Name gives its surplus back first, having had all of it. When every column is down to the longest value it holds and it still does not fit, Type gives next, on its own, to about three characters - it is the one least missed that short, where a date or a size that short says nothing. Only then does everything else give ground together, each in proportion to how wide it is, Name included.
	- A column whose values have no natural limit either - Type, Location, Owner, Group - stops at a third of the Name column rather than taking the window for one long value. The cap and Name's width have to agree with each other, so the answer is found rather than guessed, and it does not depend on the order the columns are in.
	- Narrower than the floors add up to and the view scrolls sideways, which is the right answer to a window narrower than its own contents.
	- Every value that no longer fits now says so with an ellipsis instead of being cut off mid-letter. Only Name and Location did before.
	- Widths follow the contents: each row is measured as it arrives and as its details fill in, and the widest seen is what a column aims for. Measured against a five thousand item folder, it costs nothing that can be told apart from the noise.
	- A column dragged wider by hand keeps that width until the window changes shape or the folder does.
	- Refines the earlier "Name column always as large as possible" work under Done, which only made Name take the slack; this is the rule for all of them.
	- Verified at half a dozen widths on two folders, and the rule itself has a test of its own.
	- Note: This contradicts the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: `test-nemo-column-layout`.

- ✅ Twelve more icon sets, all of them asked for by name: BeautyLine, the six Simply Circles colors, Lime Numix 2021, MB Lime Suru GLOW, Material Black Pistachio Suru, Avidity Dusk Mixed Suru, FF-BlackGreen and FF-Flamengo-RJ-BR. Twenty-three sets in the picker now.
	- Opened: 20260819-124028
	- Closed: 20260819-160351
	- All SVG, all trimmed to the names a file manager asks for, and all inside the executable - the whole icon payload is 6.6 MB, so nothing needed to be a separate download after all.
	- Three new fetch shapes were needed: a repository that keeps one theme family per branch, six themes out of one sparse checkout, and two that ship the icons as a tar committed inside a repository of something else.
	- Buuf is deliberately not included. It is CC BY-NC-SA, and the NonCommercial term rules it out of anything shipped and out of the repository. It is still wanted, so `filesystem/` explains where to drop it and gives a one-line fetch for it.
	- Three of the twelve carry no license file upstream and are shipped on weaker evidence than the rest. Each one is named, with what it rests on, in `vendor/README.md`. Check them before a release.
	- Test case: none, icon art and a license review; `test-nemo-appearance` (`test_bundled_set`) covers the bundle itself.

- ✅ A gallery of every icon set in the README, four icons each on a light and a dark background, plus how to drop your own in. Rendered by `cicd/utility/icon-gallery.py`; re-run it when the set list changes.
	- Opened: 20260819-124028
	- Closed: 20260819-160351
	- Each icon is rasterized on its own before being placed. Several sets color themselves through a stylesheet keyed on a class name they all spell the same way, so pasting their markup into one sheet made six differently colored sets come out identical - and renaming the classes apart made them all come out black.
	- Test case: none, docs only.

- ✅ `filesystem/` - a tree mirroring where things go on disk, so a folder can be copied straight across. Carries the icon and widget drop-in folders, what they are called on each platform, and the two optional `index.theme` keys that tell the picker which modes a theme suits.
	- Opened: 20260819-124028
	- Closed: 20260819-160351
	- Test case: none, docs only.

- ✅ Windows icon sets: one per Windows generation, all with yellow folders.
	- Opened: 20260819-124028
	- Closed: 20260819-145557
	- Luna (XP) and Aero (7) were already ours; Metro (10) and Mica (11) are new, so every bundled Windows widget theme now has icons drawn to match it. The picker pairs them automatically.
	- Folders are yellow in all four. Aero's were blue, which is not what Windows 7 shipped, and a yellow folder is the one color that reads on a light background and a dark one alike.
	- The XP and 7 folders were too shallow to read as folders at a glance; the body is taller in every era now.
	- The folder itself is drawn per era rather than shared - chunky and outlined for XP and 7, flat and square for 10, rounded with the front panel falling away for 11. It is the icon a Windows generation is recognised by.
	- The vendored Fluent icon set is gone with them: it drew blue folders and looked nothing like Windows 11, and Mica now covers that style. The Fluent *widget* theme stays. About 390 KB and 179 files lighter.
	- Test case: none, icon art; pairing with the widget themes is under `test-nemo-appearance` (`test_icons_follow_style`).

- ✅ Every bundled SVG run through a size pass: 2.1 MB of icon art down to 1.8 MB, and nemo's own artwork from 142 KB to 50 KB.
	- Opened: 20260819-124028
	- Closed: 20260819-145557
	- Numbers in path data are rounded to a step finer than a two-thousandth of the icon, which is under a tenth of a pixel at any size one is drawn. Colors fold to their short form and unreferenced ids go.
	- Multipliers - transform matrices, gradient vectors - are deliberately left alone: rounding a scale factor moves everything it touches, which is visible where rounding a coordinate is not.
	- All 983 icons were compared before and after. One differs at all, by an amount invisible side by side. Checking caught a real fault first time round: an arc's two flags can be written with nothing between them, and reading path data as a plain run of numbers swallows one and silently reshapes the glyph.
	- Test case: `cicd/utility/svg-min.py --self-test`, run by the lint step; skips with no python.

- ✅ Default settings changed: folder expanders on in list view, binary size prefixes (KiB/MiB), and thumbnail visibility inherited from the parent folder.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Test case: none, default values only.

- ✅ List columns trimmed to one row per idea.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Three dates, the same three everywhere: Date Created, Date Modified (on by default) and Date Read. The "- Time" twins of the first two are gone; they showed the same instant a second way. The times themselves come from whatever each OS keeps them in, so nothing here is per-platform.
	- MIME Type and Detailed Type are no longer offered - neither reads as anything but debug output beside the plain Type column. Off behind a named switch in the source rather than deleted, since the underlying values are still what the properties window and the sort menu use.
	- Test case: none, a deliberate removal behind a compile switch.

- ✅ Appearance page: picking a Style now moves the Icons choice to match it, so a Windows 11 window frame no longer comes with macOS icons. Where a style has no icon set of its own the icons stay put. The note about drop-in theme folders sits further down the page, clear of the two pickers.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Test case: `test-nemo-appearance` (`test_icons_follow_style`); the bundled-theme half runs only on a bundled-themes build, so on Windows.

- ✅ "System default" in both theme pickers now reads "Nemo Anywhere" - on the bundled targets it is the app's own look, not the platform's.
	- Opened: 20260819-124028
	- Closed: 20260819-141014
	- Test case: none, wording only.

- ✅ Settings belong where each platform keeps them: `%APPDATA%\nemo-anywhere` on Windows, `~/Library/Application Support/nemo-anywhere` on macOS. Linux and BSD keep `~/.config`. Themes stay where they were.
	- Opened: 20260819-084600
	- Closed: 20260819-105607
	- A folder left in the old place is moved across on first run, so nobody starts from defaults.
	- Covered by a test over both roots.
	- Test case: `test-nemo-config-root`.

- ✅ The Windows executable takes too long to start. 14.2s down to 3.4s, and the executable from 39.8 MB to 33.5 MB.
	- Opened: 20260819-084600
	- Closed: 20260819-122828
	- Measured first: the packed single exe reached even `--version` in 14.2s against 0.9s for the same build as a plain folder, and all of the difference is spent before our own code runs. The packer charges about 2.8 ms for every file it carries, and the bundled themes were a couple of thousand of them. The packer's own compression and mapping settings were measured and change nothing.
	- The bundled themes now ride inside the executable as one compiled-in resource instead of ~2,200 loose files. The sysroot's full Adwaita and its legacy set - 2,693 files to answer the ~180 names we ask of them, plus 33 X11 cursors that do nothing on Windows - are replaced by our own trimmed copies. The whole folder went from 4,840 files to 152.
	- Trimming Adwaita turned up three faults in the theme resolver that had been quietly costing every bundled theme icons, `emblem-symbolic-link` among them - the one every symlinked file in the view wears. All the bundled themes were rebuilt.
	- A splash appears while it starts, drawn with the platform's own toolkit because it has to be up before GTK is. It lists what startup is doing in a ten-line window that scrolls smoothly, and leaves the moment the real window has drawn.
	- The window itself is now shown at its remembered size and place as soon as it has somewhere to be, rather than after the first folder resolves. The splash goes when the folder has finished listing or a second after the view is up, whichever comes first - a big folder can take twenty seconds to list and there is no sense covering a window that is already usable.
	- Found on the way: the app had never brought its own window to the front on Windows. Showing a window maps it without activating it, so it opened behind whatever you were looking at; on Linux the window manager focuses new windows itself, which is why it had never shown. Fixed.
	- The remaining 2.5s over a plain-folder launch is the packer's own fixed cost and would need a different packer to reach.
	- Test case: `test-nemo-appearance` (`test_bundled_set`, `test_bundled_icons_resolve`, on a bundled-themes build); start time, the splash and the window coming forward need a real desktop.

- ✅ Dimmer highlight of mouseover line. It can easily get confused with line selection.
	- Opened: 20260802-011216
	- Closed: 20260802-015402
	- The hover tint on a file-pane or tree row is dimmed to well under half what the theme sets, and only on rows that are not selected.
	- Test case: `test-nemo-row-hover`.

- ✅ Drag and drop onto a path button, and a fuller right-click menu on one.
	- Opened: 20260802-011216
	- Closed: 20260826-103001
	- Dropping onto a path button already worked, and still does.
	- The right-click menu was the short location one. It gained Open, Open in Terminal, Open as Admin and New Folder, so a path segment behaves like the folder it names.
	- New Folder is only offered on the segment for the folder being viewed, and creates inside it. On any other segment it is grayed.
	- Test case: none, New folder on the current segment only needs a full window; not worth building one for this.

- ✅ Ship with "Copy path(s)" script from current nemo install.
	- Opened: 20260724-091054
	- Closed: 20260820-055722
	- Built in rather than shipped as a script, so it needs no interpreter, no clipboard helper and no per-platform install step.
	- On the selection menu, the background menu (the folder being viewed) and a breadcrumb segment; also on the Edit menu, with Ctrl+Shift+C.
	- Copies the native path of each selected item, one per line, unquoted, with no trailing newline - the line ending being the local one, so a paste into cmd or notepad comes out as separate lines.
	- Anything with no local path (a remote share) contributes its uri instead, and a recent or favorites entry resolves to the file it stands for rather than copying a virtual uri.
	- Label follows the count: "Copy Path" for one, "Copy Paths" for several. Show/hide checkboxes in Preferences like the other context-menu items.
	- Test case: `test-nemo-path-list`.

- ✅ Right-click "Compress...": a cross-platform way to archive the selected files and folders.
	- Opened: 20260819-170512
	- Closed: 20260820-153813
	- Note: superseded in part by 2026092910143202, not built yet. The "store links" and "follow linked folders" boxes become a choice per kind of link. See [20260929-101432_compression.md](design_docs/20260929-101432_compression.md).
	- On the selection menu, the background menu (the folder being viewed) and a breadcrumb segment; also on the Edit menu.
	- A dialog asks for the name, the format and the folder to put it in, prefilled from the selection and the folder being viewed. The name follows the format, so switching from zip to tar.xz swaps the suffix instead of stacking one on top of the other.
	- Compressing one folder - selected, or from the background menu or a breadcrumb - archives the folder itself, so opening the archive shows the folder and the contents are one level in. The archive is named after the folder and offered beside it rather than inside it, which is where a person would look for it. A drive root, having no beside, keeps itself.
	- Selecting everything in a folder and compressing that archives the contents, with no wrapping folder. That one takes the folder's name too, but is offered inside the folder, since that is where the selection was.
	- A part of a folder gets no name suggested, because there is none a person would agree with; the field starts empty and Compress waits until it is filled in.
	- "Compress each item separately" makes an archive apiece instead of one archive, each named after the item it came from and all of them written to the chosen folder. Off by default, and grayed with a single item selected, where there is nothing to separate.
	- With it on there is no name to give, so the name field is grayed - which is also how a part of a folder gets compressed without typing one.
	- Each item keeps its whole name, so "notes.rar" becomes "notes.rar.zip". Swapping the suffix would put the new archive on top of the file being read.
	- However many archives it makes, it is one job: one progress bar, one Cancel, and one question about the ones already there rather than a question apiece.
	- Formats: zip, tar, tar.gz, tar.xz and 7z are written by the built-in library, so they need nothing installed; rar is offered where the rar command is found, and 7z falls back to the 7z command for anything the library cannot write.
	- Options, each offered only where the chosen format and the programs present can honour it: compression level, password (with the option to encrypt the file names too), splitting into volumes with an editable list of the usual sizes, solid archives, storing duplicate files once, storing symlinks and junctions as links, following linked folders (off by default, so a link loop cannot pull in the whole disk), and for rar a recovery record (on by default) and locking.
	- An option nothing can honor is shown grayed rather than hidden, so the dialog does not change shape from one machine to the next.
	- Encryption and splitting are treated as requirements - if nothing installed can do them the job is refused rather than quietly writing a readable archive. Everything else is a preference, honoured where possible and dropped where not.
	- Compression runs as a normal background job: it shows in the same progress popup as copying, can be canceled, and a canceled or failed run leaves no half-written archive behind.
	- Test case: `test-nemo-archive`, `test-archive-job` (`check_cancel`, `check_unreadable_sources`), `test-nemo-archive-combos`, `test-nemo-archive-settings`.

- ✅ The 7z and rar command lines are settings, not code, so a user can edit them.
	- Opened: 20260821-124844
	- Closed: 20260821-133318
	- Four lines in `settings.shcl` under `archive` - create and unpack, for each of the two programs - each with `{{PLACEHOLDER}}` markers for the parts the app fills in. Point one at a different build, add a switch we do not offer, or work around a version that spells something its own way.
	- Every switch the Compress dialog can turn on has a marker of its own, so an edited line keeps the dialog working. Leave one out and the app says which control has gone quiet.
	- Clearing a line puts the shipped one back rather than running nothing, and a line that cannot be read is refused outright rather than half-run.
	- A password is handed to the program as a value, never written into the settings file.
	- `{{LIKE_THIS}}` is now the convention for any setting that needs a placeholder. Braces because no shell or command prompt expands them, so a line can be pasted somewhere to try it out and come back unchanged.
	- Test case: `test-nemo-command-template` (`check_from_config`, `check_unused`, `check_values_stay_one_argument`).

- ✅ Right-click "Extract" for the archive formats we recognize, including shelling out to 7z or rar.
	- Opened: 20260820-174223
	- Closed: 20260821-064823
	- Three items on the selection menu and the Edit menu, shown only when everything selected is an archive: "Extract Here", "Extract Each to Its Own Folder" (singular when one is selected) and "Extract To..." with a folder chooser. Show/hide them in Preferences like the other context-menu items.
	- "Extract Here" unpacks exactly what the archive stores, so one made from a folder brings that folder with it and ends up in one place. The folder-each item is the answer to an archive that would otherwise scatter its contents over the folder being viewed.
	- Reading covers far more than writing does: the tar, zip, 7z, rar, cab, lha, cpio, xar and iso families and the bare compressors all open with nothing installed.
	- A 7z or rar command is reached for when the built-in library will not open the file - a multi-volume set, or headers it cannot decrypt. Both are tried in turn, since a program being installed is no promise it can read the file.
	- A protected archive asks for its password once, and reuses it for the rest of the selection.
	- Collisions ask the same question copying asks, with the same answers - skip, duplicate, rename, replace, and applying that answer to everything after it. A folder arriving on a folder merges without asking. The prompt says which archive the incoming file came from, since several can be unpacked at once.
	- An entry whose stored path climbs out of the folder being unpacked into, or names a drive, is put back inside it.
	- Unpacking runs as a normal background job: it shows in the same progress popup as copying and can be canceled.
	- Test case: `test-nemo-extract`, `test-extract-job`, `test-nemo-menu-paths`; collisions and password reuse need a dialog and are not covered.

- ✅ Depend on Explorer as little as possible.
	- Opened: 20260818-144244
	- Closed: 20260818-155550
	- Audited every place the Windows build reaches into the shell. The only one that handed work to Explorer was a "show this file in the file manager" call, which asked Windows for the default handler for a folder - Explorer, by definition.
	- It was already unreachable: the only caller sits behind a desktop-view check that went permanently false when the desktop shell was removed. On Windows it would also have been asking for a handler that Windows does not answer for - nothing is registered for a folder as a type.
	- Removed, along with its declaration. Nothing in the tree launches Explorer now.
	- What remains is in-process and unavoidable: the recycle bin and `.lnk` files are shell APIs called inside our own process, with no Explorer involved. Two `ShellExecute` calls stay for good reasons - one launches the terminal the user chose (found on PATH, not via the shell's associations), the other relaunches our own executable elevated, which is the only way to ask for elevation.
	- Test case: `fCheckWinLaunch` in the C lint.

- ✅ Code review 20260815 - architecture and UX notes.
	- Opened: 20260815-154746
	- Closed: 20260817-210917
	- Observations and suggestions rather than defects. Not individually reproduced.
	- ✅ Item 110. Two separate desktop-terminal fallbacks disagree: "Open in Terminal" honors the configured terminal, launching a terminal app does not.
		- Fixed: both paths fall back to the same scan of known terminals, so neither silently does nothing.
		- Test case: `test-eel-terminal` (`test_fallback_scan`).

	- ✅ Item 111. Localization is effectively dead on Windows and on relocated installs; the locale directory is baked at build time and no packaging step installs or points to it.
		- Fixed: data, translations and helper programs are found relative to the running program, with the built-in path as a fallback.
		- Test case: `test-nemo-runtime-env`, POSIX only.

	- ✅ Item 112. Windows drive roots are labeled bare, with no volume label.
		- Fixed: the volume label is shown ahead of the drive letter.
		- Verified on Windows: the sidebar reads "Windows (C:)" and "Extra (K:)" against the real volumes on this box.
		- But only the sidebar was covered - see the drive-root naming item under Done - Bugs.
		- Test case: `test-nemo-drive-root-name`, Windows only.

	- ✅ Item 113. The README points Windows users at the wrong settings folder.
		- Fixed.
		- Test case: none, docs only.

	- ✅ Item 114. "Open in Terminal" on Windows is hardcoded with no setting, though the same item is configurable on Linux.
		- Fixed: the list of terminals to try is a setting, tried in order.
		- Test case: `test-nemo-view-win32` for the terminal command split, Windows only; the order the candidates are tried in is not covered.

	- ✅ Item 115. Failed Windows elevation or terminal launch is silent; the shell-execute result is ignored.
		- Fixed: a failure is reported rather than swallowed. The path is also quoted properly now, so a folder with spaces or a drive root works.
		- Test case: `test-nemo-view-win32` for the quoting, Windows only; the failure report needs a real launch and is not covered.

	- ✅ Item 116. Selectable message-dialog text grabs focus pre-selected.
		- Fixed: the text is still selectable but no longer takes focus pre-selected.
		- Test case: `test-eel-stock-dialogs`.

	- ✅ Item 117. The properties window never cancels scheduled owner/group changes on close.
		- Fixed: pending changes are canceled when the window closes.
		- Test case: none, needs a full properties window; not worth building one for this.

	- ✅ Item 118. The Ctrl-key state for tab switching is a stale process-wide global.
		- Fixed: the state belongs to the notebook and is cleared when it loses the keyboard.
		- Test case: none, needs synthesized key events on a live notebook; not worth building for this.

	- ✅ Item 119. The public design doc's code-structure sections are empty scaffolding; the real internal architecture lives only in private notes.
		- Fixed: the code-structure, data-flow, execution, stack, UI and testing sections are written.
		- Test case: none, docs only.

- ✅ Ultra-portable Windows: a single self-contained executable.
	- Opened: 20260730-203115
	- Closed: 20260802-144622
	- ✅ No separate library folder - pack the runtime into one `.exe` (in-memory virtual FS, e.g. Enigma Virtual Box).
		- A pack step flattens the staged bundle into the same layout the release zip uses, which double-click-runs on its own, then packs the lot into one executable. It is stage five of the Windows pipeline.
		- The font-rendering settings are set inside the executable now, so no launcher is needed for the native text look.
		- Verified: a 167 MB bundle packs to one 38.7 MB executable, which runs on a bare PATH and stays responsive.
		- Fixed the leftover console window on launch. The executables were linked as console programs, which is the default, so Windows opened a terminal before the window appeared. They are graphical programs now, and the version output still works when piped.
	- ✅ One binary only - Windows builds just the one executable. The connect-server and open-with dialogs already ran inside it, and the extensions lister has nothing to list with no plugins, so all three helpers are Linux-only now.
		- The extension library went in with them. With no external plugins on Windows the executable was its only reader, so there is no separate library beside it any more. Linux keeps it shared, for third-party extensions.
	- ✅ No external plugin loading on Windows (a bad plugin must never hang the app); keep the extension-management UI in-exe.
		- The plugin folder is never read on Windows, so a stray library cannot load and hang the app. The plugins tab in Settings still appears, listing nothing.
	- Test case: `cicd/utility/check-win-build-flags.bash` for the GUI subsystem, plus the `--version` smoke in `cicd/cicd-win.ps1`; the packing itself is a pipeline stage.

- ✅ Windows look: make it feel native even though it isn't Explorer.
	- Opened: 20260730-203115
	- Closed: 20260818-201822
	- ✅ Fix the thin, poorly anti-aliased text - Segoe UI 9 with full hinting and subpixel (generated settings.ini + fontconfig).
	- ✅ Themes: bundle a curated set of icon and widget styles, light and dark. Permissive licenses only - not Microsoft's own art.
		- Done: eight widget themes (Windows 11, 10, 7, XP light+dark, macOS light+dark) and nine icon styles, about 5 MB all told. Each vendored at a pinned commit with its license kept.
		- Done: icon themes trimmed to the ~180 names a file manager asks for, which took Fluent from 1.8 MB to 261 KB; the rest falls back to Adwaita.
		- Done: standard icon names materialized as real files (themes ship them as symlink aliases, which a Windows checkout breaks).
		- Done: Windows XP and Windows 7 icon sets drawn in-house - no cleanly-licensed set of either exists, only repackaged Microsoft art.
		- Bundled where the platform is unlikely to have themes installed (Windows, macOS). Linux keeps using the desktop's own, so the thin prefix stays thin.
	- ✅ Custom theming: theme search folders beside the settings file, so themes can be dropped in on any platform. Drop-ins are searched before the bundled set.
	- ✅ Theme + light/dark selection stored in config; auto-follow the Windows light/dark setting with a manual override.
		- Done: auto-follow reads Windows AppsUseLightTheme at startup and live (registry watch).
		- Done: an Appearance page in settings with Light / Dark / Follow the system, plus style and icon pickers filtered to the mode in force. Picking one half of a light/dark pair follows the pair when the mode changes.
	- Test case: `test-nemo-appearance` (`test_dropin_shadows_bundled` and the theme and mode checks); the font rendering is look only, judged by eye.

- ✅ Config engine: settings + persistence moved to SHCL in a user-level `settings.shcl`; gconf/dconf and the Windows registry are out of the picture.
	- Opened: 20260718-170501
	- Closed: 20260804-205711
	- Done: GSettings replaced outright rather than kept over a SHCL backend, so no compiled schema is installed or shipped. All 168 settings, ~300 call sites, 84 change handlers and 16 property binds moved over.
	- Done: the file holds only non-default values, carries each key's description as a comment, and is re-read while running so a hand-edit applies immediately.
	- Done: a schema file sits beside the app so `shcl check --schema` validates a hand-edited config (catches typos and bad values).
	- Done: the `compat.*` fallback schemas are gone; desktop-owned settings (terminal, recent files, 12/24h clock) are read from the desktop where it publishes them, ours otherwise.
	- Note: settings do not carry over from a pre-1.0 install - nothing left can read the old store. Fresh defaults on first run after upgrading.
	- Note: nemo actions can still name any GSettings schema in a condition; that reads other programs' settings and is unaffected.
	- Test case: `test-nemo-config`, `test-nemo-config-catalog`, `test-nemo-config-schema`.

- ✅ No autorun, ever, on any platform - not even an option. Notice a new drive; never run anything off it. Remove the autorun-software helper and its media-autorun path.
	- Opened: 20260730-203115
	- Closed: 20260802-002013
	- Done: the autorun-software helper, its menu entry, and the "prompt or autorun programs" preference are gone.
	- Done: the inserted-media bar never offers to run software from media. Other media notices (audio CD, photos) unchanged, and automount / auto-open still work - drives are noticed, nothing runs.
	- Test case: `fCheckNoAutorun` in the C lint.

- ✅ Native Windows shortcuts: create `.lnk` files, the Windows analog of `.desktop` launchers.
	- Opened: 20260725-153058
	- Closed: 20260803-135051
	- ✅ Create: "Make Link" and the drag "_Link Here" now write a `.lnk` shell shortcut on Windows (via `IShellLinkW`), in place of the POSIX symlink the win32 file layer can't make. Round-trip verified by a test that loads the shortcut back through the shell.
	- ✅ Follow on open: opening a `.lnk` now follows through to its target - a folder navigates in place, a file opens as if the target were double-clicked. Reading the target round-trips through the shell (test-verified).
	- Test case: `test-nemo-shortcut-win32`, `test-nemo-make-link-job` (`shortcut`), Windows only for the shell half.

- ✅ Ship the app's own icons and data files on Windows.
	- Opened: 20260725-153058
	- Closed: 20260826-103001
	- Cause: the data dir was a compile-time absolute Unix path, so the sort-menu icons, the eject icon and the emblem art did not resolve on Windows.
	- Fixed: the artwork rides inside the executable as a compiled-in resource, and the data, translation and helper-program folders are found relative to the running program, with the built-in path as a fallback.
	- Test case: `test-nemo-app-resources`, `test-nemo-runtime-env` (POSIX only), `test-nemo-appearance` (`test_app_icons_resolve`).

- ✅ Real-Windows validation pass.
	- Opened: 20260724-140849
	- Closed: 20260826-103001
	- Covers: trash, network browsing, single-instance, default-app setting, the Windows half of the installer, elevated relaunch (UAC prompt), keyboard shortcuts.
	- Note: moving a file to the trash raises a Windows confirmation dialog of its own on this box, on top of ours. Open question: should ours stand down there? The test that hit it now skips that step unless asked for it.
	- Done on real Windows: the recycle bin end to end, network browsing against this box's own shares, single instance and location forwarding, the installer's install/reinstall/uninstall round trip, and elevated relaunch. Each of the code-review items was re-checked here.
	- Found doing it, and fixed: the whole compiled-resource bundle was missing from the Windows build, so there was no menu bar at all; a drive root was named three different ways; "Set as default" failed silently forever; the installer read a prerelease version as the release it precedes.
	- Test case: `test-nemo-drive-root-name`, `test-nemo-trash-win32`, `test-nemo-associations-win32` (Windows only), and `cicd/utility/check-win-build-flags.bash` for the resource bundle in the exe.

- ✅ Get release binaries onto the host, plus an optimized buildtype.
	- Opened: 20260730-185314
	- Closed: 20260804-133646
	- ✅ Done: host dogfood path proven. Release build staged in the container, copied out to a self-contained folder, launched via a small wrapper.
	- ✅ Done: Linux release lane at `cicd/linux/release.bash` - optimized stripped build on an Ubuntu 22.04 box (the glibc floor is what the binary is built against), staged into a relocatable prefix, packed as the tarball plus the sums file.
	- ✅ Done: artifacts come out under the names the installers look for, and the artifact dir is wired in `config.bash` so `utility/release.bash` verifies and attaches them.
	- Test case: none, pipeline setup.

- ✅ Single-exe packaging stage in `cicd-win.ps1` - pack the staged DLL closure into one portable `.exe`.
	- Opened: 20260730-203115
	- Closed: 20260804-095855
	- Done: `cicd/win/pack-portable.ps1` flattens the bundle and packs it with Enigma Virtual Box into one self-contained exe; wired as cicd-win stage 5.
	- Test case: none, pipeline setup.

- ✅ Windows exe signing groundwork.
	- Opened: n/a
	- Closed: 20260804-095855
	- ✅ Embedded VERSIONINFO in the exe (real publisher/version metadata; a blank-metadata binary scores worse with AV heuristics and looks unfinished in Properties).
	- ✅ Local `signtool` signing scaffold in cicd-win stage 5 - env-driven, no-op until a cert is configured (fits a token/store cert: Certum OSS, Azure Trusted Signing, or a commercial EV).
	- Test case: `cicd/utility/check-win-build-flags.bash` for the version resource; the signing scaffold is pipeline setup.

- ✅ Publish the Windows `.zip` alongside the single exe. `install.ps1` only ever looks for the contract-named zip, so on Windows the one-liner installer had nothing to fetch even though the release carried a working exe.
	- Opened: 20260804-133646
	- Closed: 20260804-232326
	- Done: `cicd/win/pack-zip.bash` builds it from the cross build, and every release from `v1.0.0-beta2` on has it.
	- Test case: none, release packaging.

- ✅ Don't continuously spam stdout/stderr with meaningless debug messages.
	- Opened: 20260802-011216
	- Closed: 20260802-012711
	- Cause: on Windows, any file type without a registry MIME mapping fell through a wildcard and got a doomed image-thumbnail attempt - two warnings per file, every folder browsed. A few one-shot startup notices added to the noise.
	- Fixed: unknown types are no longer treated as thumbnailable, the image loader gets a real MIME type, and the per-file / startup notices are debug-level now (visible with G_MESSAGES_DEBUG when wanted).
	- Verified: browsing a mixed folder of images and non-images runs silent; image thumbnails unaffected.
	- Test case: `test-nemo-thumbnail-win32` (Windows only), `test-nemo-directory-load-clean`.

- ✅ Add a C formatter/linter gate and wire it into the format/lint stages.
	- Opened: 20260725-153058
	- Closed: 20260802-011216
	- Done: check-only cppcheck over the changed C files only, wired into both pipelines (Windows stage 1 + gate, Linux lint stage). No in-place formatter - a full-tree reformat of the inherited code would bury history in churn.
	- Done: a box without cppcheck skips with a warning instead of blocking a push.
	- Test case: none, it is the lint gate itself.

- ✅ Change default settings:
	- Opened: 20260724-091054
	- Closed: 20260802-004759
	- ✅ List view, 66% size.
	- ✅ Ask before moving items to trash.
	- ✅ Date display in ISO format.
	- ✅ Showing owner, group, and perms.
	- Done: new out-of-the-box defaults - list view at 66%, trash moves ask first, ISO dates, owner/group/permissions columns visible. Existing installs that changed a setting keep their value.
	- Test case: `test-nemo-config` (`test_product_defaults`).

- ✅ Remove features:
	- Opened: 20260724-091054
	- Closed: 20260802-004759
	- Option to display date in monospace font.
	- Done: the date font style option, its setting, and the mono-font matching are gone. Dates use the regular font.
	- Test case: none, removed feature.

- ✅ Allow select and copy of error message dialogs.
	- Opened: 20260724-102941
	- Closed: 20260802-004759
	- Done: the message text in the stock error/question dialogs is selectable, so it can be copied. The expandable details text already was.
	- Test case: `test-eel-stock-dialogs`.

- ✅ "Name" column should always be as large as possible, the other columns don't auto-adjust. When window grows or shrinks, the Name column does too to as wide as possible without pushing other columns off.
	- Opened: 20260724-091054
	- Closed: 20260727-001739
	- Cause: the Name cell asked for a 40-character width, which acted as a floor the column could never shrink past, so a narrowing window pushed the trailing columns off instead.
	- Fixed: dropped that request, so Name now gives space back down to its existing minimum. Long names ellipsize as before.
	- Verified: at 600px wide all four columns fit where Date Modified used to be cut off; at 1500px Name still takes all the slack; shrinking back from wide re-fits correctly.
	- Note: This contradicts the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: `test-nemo-column-layout`, `fCheckColumnExpand` in the C lint.

- ✅ Wine launcher.
	- Opened: 20260724-091054
	- Closed: 20260725-153058
	- Fixed: launches detached, so the script exits and returns immediately.
	- Fixed: initial directory is the user's home if it exists, falling back to the drive root, then C:\.
	- Test case: none, a dev script that needs wine and a display.

- ✅ Installer script(s) - one-liner install from a shell, for every target.
	- Opened: 20260723-132307
	- Closed: 20260723-133832
	- Done: two standalone installers. The bash one covers Linux, BSD, WSL, and macOS; the PowerShell one covers all of those plus Windows.
	- Done: both take channel, target, and architecture options; print the plan and wait for a yes; verify the download checksum before unpacking; replace an existing install in place; and reverse themselves with an uninstall option.
	- Done: installs as a folder plus a menu entry and a name on PATH. User install is the default; only the system-wide install escalates, and says so in the plan.
	- Done: README gained an Installation section. The release-asset naming the installers depend on is in design.md under Delivery.
	- Verified: end to end on the unix side against a stand-in releases service - channel and asset resolution, checksum pass and tamper-fail, install, reinstall, uninstall, prompt accept and decline, and both installers leaving identical results.
	- Note: the Windows half still needs the real-Windows validation pass.
	- Test case: `cicd/linux/test-installers.bash`, `cicd/linux/test-install-download.bash`.

- ✅ Dogfood launcher script.
	- Opened: 20260723-081328
	- Closed: 20260725-153058
	- Done: keeps date-stamped copies of the latest build in a local pool, prunes aged-out copies not in use, launches the newest with args passed through.
	- Done: one cross-platform PowerShell script for Linux and Windows. Working copy deployed to the common util dir.
	- Done: launches detached and returns immediately. App output goes to a log in the target dir, so it never holds the calling console open.
	- Done: a source on a network share is written off after a moment rather than blocking the launch while the network gives up in its own time.
	- Done: a launch with nothing to copy went from nine seconds to one. Working out which programs are running was the whole cost on Windows, and it was being done twice.
	- Fixed: the newest copy could age out and be re-fetched on every run whenever the source build was itself older than the pruning cutoff.
	- Fixed: copies left by the pre-single-exe layout were invisible to the pruning and sat there for good.
	- Test case: `cicd/utility/test-runfm-pool.ps1`.

- ✅ Adopt the local-only delivery model: dev = integration target, main = release-only (dev to main = release cut). Feature branches merge --no-ff into dev.
	- Opened: 20260718-192018
	- Closed: 20260718-195609
	- Note: copied as high-level concepts (not language tooling) from the sibling project.
	- Test case: none, process.

- ✅ Make the CICD test gate resilient to a down or absent docker daemon.
	- Opened: 20260721-222522
	- Closed: 20260725-153058
	- Done: build and smoke steps go through a wrapper that probes the daemon first.
	- Done: an environmental miss (docker absent, daemon down, container gone) skips with a warning instead of blocking the push. A real build or test failure still gates. A strict mode turns a miss back into a hard failure.
	- Note: the daemon needs root to start, so the unattended hook never auto-starts it. The skip message shows the manual command.
	- Verified: gate passes normally, and skips cleanly when docker is unreachable.
	- Note: whether one smoke test was enough as a gate settled itself. The gate runs the whole suite now, on Linux and on Windows.
	- Note: since 20260919 a missing `nemo-build` is made on first use rather than skipped. Any other missing container still skips.
	- Test case: none, pipeline setup.

- ✅ Stand up the local pipeline: engine, config, git backup+publish, release helper, and a pre-push merge gate.
	- Opened: 20260718-192018
	- Closed: 20260725-153058
	- Verified: container build + smoke test, and backup+publish, all pass.
	- Test case: none, pipeline setup.

- ✅ dbus / single-instance handling.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Verified: single-instance works unchanged on Windows. A second launch hands its arguments to the first. No per-platform gating needed. Details in design.md, "Decisions along the way".
	- Fixed: a bus-less environment (headless or minimal system) crashed the internal file-operations service. It now skips setup cleanly. Regression test added, passes on both platforms.
	- Test case: `test-nemo-startup-clean` (`check_no_bus`), `test-nemo-instances`.

- ✅ Context-menu actions: open in terminal, open elevated, launchers.
	- Opened: 20260718-155447
	- Closed: 20260724-143335
	- Done: on Windows, "open in terminal" opens the native console at the folder, and "open elevated" relaunches the app through the normal elevation prompt. Linux paths unchanged. Menu labels are per-platform.
	- Note: `.desktop` launcher files already degrade cleanly on Windows. Native `.lnk` creation is its own item, since done.
	- Test case: `test-nemo-view-win32` (Windows only), `test-eel-terminal`; the launches themselves open a prompt or a console and are not driven.

- ✅ Thumbnails, icon theme, and default-app association per platform.
	- Opened: 20260718-155447
	- Closed: 20260724-150328
	- Done: the portable file-and-app layer already carries most of this. The real gaps were the two icon bugs (see Done - Bugs) and packaging the thumbnailer tools with the Windows runtime.
	- Verified: default-app lookup, launch, and set-default work on Windows through the portable layer. Image thumbnails render.
	- Note: on Windows 10/11 the per-user default-app choice may not stick. Not worked around.
	- Test case: `test-nemo-associations-win32`, `test-nemo-thumbnail-win32` (Windows only), `test-nemo-thumbnail`.

- ✅ gvfs replacement or scope-out (mounts, network, trash).
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: gvfs stays an optional runtime dep on Linux; gaps filled natively per platform. Details in design.md, "Decisions along the way".
	- ✅ Portable per-file metadata store on all platforms - view/sort state, custom icons, emblems, favorite markers.
		- Done: one file under the app config dir. Entries follow moves and renames, including folder contents.
		- Verified: per-folder view state now persists on Windows, where before it errored on every write.
	- ✅ Show virtual locations (network, computer) only when the platform supports them.
		- Fixed: the sidebar Network entries are now gated on runtime support. The empty section disappears on Windows until the native backend is present.
	- ✅ Windows trash: native Recycle Bin backend for in-app browse, restore, and empty (deleting to the bin already worked).
		- Done: trash is served in-process from the Recycle Bin, so the existing sidebar row, restore/empty bar, monitor, and delete paths all work unchanged.
		- Done: browsing into a trashed folder works. It was a flat item list at first; the code review turned up that the same gap also stopped a trashed folder being permanently deleted, and both were fixed together.
	- ✅ Windows network: native network-neighborhood browsing + UNC paths in the location bar.
		- Done: the network location is served in-process from native enumeration. Servers list their shares, and a share opens as an ordinary folder.
		- Verified: graceful-empty in the dev rig (no real network there). Populated browsing is part of the real-Windows validation pass.
	- ✅ Accept `\` as a separator in typed locations on all platforms.
		- Done: the literal path is tried first, then a `\`-to-`/` retry only if it doesn't resolve. Real backslash-named files and remote URIs are never touched.
	- Test case: `test-nemo-metadata-store`, `test-nemo-trash-win32`, `test-nemo-network-win32` (Windows only), `test-eel-user-input`; the Network sidebar gate is not covered.

- ✅ File operations (copy/move/delete/rename) on native APIs.
	- Opened: 20260718-155447
	- Closed: 20260722-201512
	- Verified: the existing operations engine drives all core operations correctly on Windows - copy, conflict, overwrite, recursive folder copy, move, rename, delete. No porting needed. Probe test added, runs on both platforms.
	- Fixed: link-creation options are hidden on Windows (no symlink support there). The permissions tab, columns, and change-permissions paths are hidden too, since Windows fabricates the mode bits.
	- Test case: `test-gio-fileops`, `test-copy`.

- ✅ File monitoring via portable backends.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Verified: change events deliver through the native monitor backends on both platforms. Nothing to port.
	- Test case: `test-nemo-directory-monitor`.

- ✅ Choose and stand up the Windows toolchain.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: cross-compile from Linux with mingw-w64, smoke-test under wine, in a dedicated container. Details in design.md, "Building (Windows cross)".
	- Test case: none, a decision; the cross build stage exercises it.

- ✅ Get GTK3 + GLib/GIO building on the chosen toolchain.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: cross configure comes up clean with all deps resolved. Unix-only deps guarded out per platform.
	- Test case: none beyond the cross build stage and the native Windows gate build.

- ✅ Compile on Windows, stubbing/excluding hard platform deps.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: the app, its helpers, and the extension library all build and link clean, and run under wine. Linux stays green.
	- Done: POSIX gaps closed via a shared compatibility header plus per-site guards.
	- Test case: none beyond the cross build stage and the native Windows gate build.

- ✅ Launch on Windows and browse the local filesystem.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: the GUI comes up under wine and browses the local drive - sidebar, icon view, per-type icons, item count, free space.
	- Fixed: startup abort caused by desktop settings schemas that only exist on Cinnamon/GNOME. Bundled neutral fallbacks now cover them (see design.md, "Decisions along the way").
	- Done: GUI smoke test scripted.
	- Test case: `cicd/win/gui-launch-smoke.ps1` in the Windows gate, `cicd/win/gui-smoke.bash` under wine after the cross build.

- ✅ Map drive letters / roots into the location model.
	- Opened: 20260718-155447
	- Closed: 20260725-153058
	- Done: on Windows, each fixed drive is a first-class sidebar root with a disk-usage bar, replacing the single Unix filesystem root (meaningless on Windows). Removable and network drives keep the normal devices path, which carries eject.
	- Verified: drives show as roots and open to their contents.
	- Test case: `test-nemo-drive-root-name` for the names, Windows only; listing fixed drives as sidebar roots is not covered.

- ✅ Remove desktop management entirely (Nemo Anywhere is a file manager, not a desktop shell).
	- Opened: 20260718-170501
	- Closed: 20260719-181630
	- Done: the desktop binary, desktop windows, and the Cinnamon session coupling all deleted. Kept the launcher-file editor and the monitor-geometry helper, both real file-manager features.
	- Test case: none, removed feature.

- ✅ Isolate xapp / cinnamon-desktop coupling (reimplement portably, not just disable).
	- Opened: 20260718-155447
	- Closed: 20260719-190803
	- Done: favorites, thumbnails, tray icon, and the icon chooser all reimplemented portably. Details in design.md, "Decisions along the way".
	- Test case: `test-nemo-favorites`, `test-nemo-thumbnail`; the build containers carry neither library, so a dependency coming back breaks the build.

- ✅ Prove a de-Cinnamon Linux build that runs standalone (no xapp, no cinnamon-desktop) on any desktop or none.
	- Opened: 20260718-155447
	- Closed: 20260719-190803
	- Verified: builds and links with neither library. Favorites and thumbnails work on the standalone build.
	- Test case: none beyond the container build, which has neither library installed.

- ✅ Isolate per-file view metadata keys so the two builds don't share view state on the same files.
	- Opened: 20260718-174619
	- Closed: 20260722-172504
	- Done: view/layout keys and the favorite markers carry the app name. Keys other file managers also read (custom icon, emblems, annotation, backgrounds) stay shared on purpose.
	- Test case: `fCheckMetadataSlug` in the C lint.

- ✅ Build upstream as-is on Linux (meson) to confirm a known-good reference.
	- Opened: 20260718-154147
	- Closed: 20260718-155447
	- Done: builds and runs clean on stock Debian 13, in a container (this dev box has newer mixed libs).
	- Test case: none, one-time setup.

- ✅ Note the exact dependency set and versions that produce a working build.
	- Opened: 20260718-154147
	- Closed: 20260718-155447
	- Done: recorded in the build notes outside the repo.
	- Test case: none, notes only.

- ✅ Reorganize into a clean project structure; build consolidated under `source/`, root kept lean.
	- Opened: 20260718-154147
	- Closed: 20260718-161018
	- Done: meson project moved under `source/` with its internal layout intact. Builds and runs green.
	- Test case: none, layout only.

- ✅ Rebrand to "Nemo Anywhere" / `nemo-anywhere` so it co-installs and runs alongside upstream Nemo without conflict.
	- Opened: 20260718-170501
	- Closed: 20260718-174619
	- Done: renamed the installed identity only (binaries, service names, settings schema, config/data dirs, menu entries, icons). Internal code identifiers left as-is; no clash.
	- Done: settings fully isolated from upstream Nemo. Doesn't claim the freedesktop file-manager service when upstream holds it.
	- Verified: staged install has no filename collisions with upstream. The window comes up with no desktop session.
	- Test case: `cicd/linux/test-prefix.bash`, `test-nemo-config-root`.

- ✅ Install nemo-anywhere and upstream Nemo into separate prefixes and confirm both run simultaneously without conflict (real side-by-side runtime proof).
	- Opened: 20260718-191700
	- Closed: 20260719-181454
	- Test case: none, a one-time runtime check; `cicd/linux/test-prefix.bash` holds the file names apart.

- ✅ Clean detached baseline from linuxmint/nemo 6.6.4 (no upstream commit history).
	- Opened: 20260718-154147
	- Closed: 20260718-155447
	- Test case: none, repo history.

- ✅ Fork branding + provenance (README, fork.md), GPL-2.0-only.
	- Opened: 20260718-154147
	- Closed: 20260718-155447
	- Test case: none, docs only.

- ✅ Name chosen: nemo-anywhere.
	- Opened: n/a
	- Closed: 20260718-155447
	- Test case: none, a decision.

- ✅ Create the GitHub repo and push.
	- Opened: 20260718-154147
	- Closed: 20260725-153058
	- Done: created public.
	- Test case: none, process.

- ✅ Strip upstream CI - keep the repo clear of unrelated automation.
	- Opened: 20260718-154147
	- Closed: 20260725-153058
	- Done: workflows and issue templates removed in the fork-setup commit.
	- Test case: none, repo housekeeping.

### Deferred

- ✋ Make regular delete/recycle/overwrite confirmation dialogs default to OK.
	- Opened: 20260917-125804
	- Design: [20260930-150859_delete_guard.md](design_docs/20260930-150859_delete_guard.md).
	- This reverses the earlier design intended to guard against an apparent spontaneous deletion bug.
	- Don't alter the code that optionally provides ultra-protection by showing what will be deleted, how it was invoked, what files, etc.
	- Only do this once nemo-anywhere has been in reliable use for many days or weeks, including archiving.
	- Test case: none, deferred.

- ✋ Code review 20260815.
	- Opened: 20260815-154746
	- ✋ Item 101. Carriage returns in settings values are not escaped and are stripped on reload.
		- Deferred: fixing it means changing both halves of the vendored settings parser and with them the on-disk escaping, for a character no setting ever contains.
	- ✋ Item 106. Sorting by a string attribute allocates and formats both values on every comparison.
		- Deferred: doing it properly needs a per-file cache of the formatted value with its own invalidation - the same machinery as the icon render cache, for much less gain.
	- ✋ Item 109. Platform code is split two ways: dedicated Windows modules alongside inline platform blocks in large shared files.
		- Deferred: Need to decide whether this is "convention" or a "bug".
	- Test case: none, all three items are deferred.

### Canceled

- 🚫 Menu: "Snapshot ..."
	- Only works if folders selected
	- Warn if the total file count would be excessive, or in danger of running into filesystem limits.
	- Dialog:
		- Snapshot type (radio buttons):
			- Copy-On-Write clone  # Default, disable if not supported. With flyover text describing what it is, and that there is no risk to the original files even if the snapshot fails.
			- Hardlink             # With a danger icon, and flyover text description of what's going to happen, and warning of the dangers.
			- If hardlink is chosen when OK is hit, show a dialog again describing what's going to happen, and the dangers. (Including potential confusion over a mix of hardlinks and modified symlinks.)
	- Behavior:
		- Work should happen in a hidden temp folder at the level of the highest selected folder, named:
			- ".wip_snapshot_YYYYmmDD-HHMMSS-NNNN"
			- This is so nothing appears where it should until it all succeeds. If the operation fails, this temp dir can be deleted. (Pseudo-"atomic".)
		- If the parent folder supports CoW cloning, and that's what the user chose:
			- Make a CoW copy of the selected real files and folders (excluding symlinks, sockets, etc.), with the names "<original name> - cow snapshot YYYYmmDD-HHMMSS".
				- Remembering the temp directory requirement above.
		- Otherwise:
			- For each real folder selected:
				- Make a new folder next to it named "<original name> - hardlink snapshot YYYYmmDD-HHMMSS"
					- Remembering the temp directory requirement above.
				- Within that, recreate the entire folder structure (for real folders only, not symlinks).
				- Then, make hardlinks of every real file, within the same subfolder structure.
				- Then, for symlinks in the original, copy them (not hardlink!).
		- Then for both, scan all the symlinks in the new folders, that point to targets within the original structure.
			- Update those to point *relatively* within the new (final) destination folders.
		- If all that succeeds:
			- Only then create the new destination folders named above
			- Move the snapshotted folders from the temp wip folder, to their new destinations.
			- If all that succeeds:
				- Delete the empty wip folder.
		- If anything fails:
			- Try to delete the wip folder.
			- Warn the user that the operation failed and was backed out, but the original files are safe.
	- Opened: 20260924 by JC.
	- Closed: 20260925-062731
	- Canceled: 20260925-062731. For these inherent problems we don't want to be unfairly blamed for:
		- The first option, CoW clone, can already be accomplished just by copy-and-paste on a supported filesystem.
		- The second option, hardlinks, by its nature is too fraught with potential future data loss problems for the user, that we don't want to be viewed as somehow "responsible" for. Those problems are already explained in the "Make hardlink" feature.
		- And finally, updating symlinks inside the new copy - while the more "proper" way to do it - could still lead to unexpected results for users. (And is also, arguably, "inconsistent" since only paths pointed to inside the clone get modified.)
		- The whole thing should probably be left to other third-party utilities that users would have to specifically seek out, with motivation - rather than a feature they stumble upon in their file manager.
	- Test case: none, canceled.

- 🚫 In list view, a folder of pictures shows a horizontal scrollbar even when every column fits. A folder of text files the same size does not.
	- Opened: 20260921. Closed: 20260921.
	- Why canceled: not a bug. The columns did not fit. The pictures were 327 bytes against 4 KiB, and "327 bytes" is wider than "4.0 KiB". "Image" is wider than "Text", and the names were a character longer.
	- Every name in that folder is the same width, so Name has nothing it can cut short. With every column at its least, the row ran about 24px past the view. design.md says to scroll then.
	- Text files of 327 bytes with shorter names fit, and showed no scrollbar.
	- Test case: none, not a bug.

- 🚫 Persist icon view size changes, for both regular and image.
	- Why canceled: Per-folder and global settings do this. Not perfectly, but the overlap might cause confusion.
	- Opened: 20260920-162550
	- Closed: 20260920-162550
	- Test case: none, canceled.

- 🚫 Nothing in the suite can build a window, so a widget's teardown cannot be tested.
	- Opened: 20260919-210000
	- Closed: 20260920-090000
	- The sidebar and the view are compiled into the program, and the tests link the two libraries beside it. A fix in either is pinned by reading the source, never by running it.
	- Would have taken moving the program's sources into a library the executable and the tests both link. Declined: the restructure costs more than the class of fault it would catch.
	- Instead, `cicd/utility/lint-pref-handlers.py` pairs every connect with its disconnect across the whole tree, which covers more sites than a teardown test would and found one the review missed.
	- Test case: none, canceled; `cicd/utility/lint-pref-handlers.py` covers the teardown handlers instead.

- 🚫 Keyboard shortcuts do nothing in the Windows build when run under wine.
	- Opened: 20260725-172648
	- Closed: 20260802-001535
	- Cause: wine has no keyboard layout DLL, so GTK can't turn a keypress into a key value and no shortcut ever matches. Plain keys (arrows, typing) still work, and so do the menus and mouse.
	- Note: a wine limitation, not our code. Expected to work on real Windows - added to the real-Windows validation pass.
	- Test case: none, wine limitation.

- 🚫 Launching `app\nemo-anywhere.exe` straight from the dogfood folder throws missing-dll dialogs (libcairo-goobject-2 and friends) - the exe has to go through the root `nemo-anywhere.vbs`, which wires the dll path. Punted: the single-exe work removes the whole launcher/dll-folder arrangement.
	- Opened: 20260730-185140
	- Closed: 20260802-101032
	- Test case: none, canceled.

- 🚫 In find mode, shrink the Name column to fit and let Location grow with the window, then put it back on leaving find mode.
	- Opened: 20260730-112038
	- Closed: 20260822-075741
	- Superseded by the column-width work: in find mode Name and Location split the row one-third/two-thirds, and an adjusted split is remembered.
	- Note: This may contradict the latest canonical column-sizing definition in 'design.md' under the section "List view column widths", as of 20260916-113519.
	- Test case: none, superseded by the column-width rules.

## Template

### Old format

- 🔘 Not started

- 🛠️ Started, and/or partially complete

- 🔬 Testing not started or finished

- ✋ Defer

- ✅ Complete

- 🚫 Canceled

### New format

- Notes:

	- Only use rows that you actually need or expect will be filled in. Always fill in the title, ID, Type, Status, Opened and Created by.

	- The ID is the local time to the hundredth of a second. Opened is when it was written down, which may differ. (Use a keyboard macro and possibly something like project 'zuid' to generate.)

	- Status values meaning: Testing means the fix is in and checks are running or still to run. Waiting on signoff means automated testing passed. Moot means something else changed that made it irrelevant. Canceled means it still applies but was decided against. Waiting for testing means the fix is in and waits on a long CI run or an outside test host. Can't reproduce means a real attempt to reproduce it failed.

	- As issues are worked, and statuses change, place them in correct sorting order within the list:
		- First by status: Waiting for answers, Waiting on signoff, Testing, Waiting for testing, Can't reproduce, Stalled, Started, Queued, Done, Deferred, Canceled, Moot
		- Then by severity|priority: Critical, High, Avg, Low
		- Then by type: Bugs, [not bugs together]

	- Rows marked [Bug] are for bugs only, and rows marked [Feature] for features and enhancements. Priority and Severity share one row and one scale. Priority is for a Feature or Enhancement, and Severity for a Bug. Children are not nested. They sit at the top level and point back with Parent ID.

Template:

- Title
	- ID: YYYYmmDDHHMMSSNN
	- Type: [Bug|Feature|Enhancement|Task]
	- Status: [Queued|Waiting for answers|Waiting on signoff|Waiting for testing|Started|Testing|Stalled|Can't reproduce|Moot|Canceled|Deferred|Done]
	- Needs local test suite run?:
	- Needs external testing:
	- Priority [Feature|Enhancement] | Severity [Bug]: [Critical|High|Avg|Low]
	- Opened:
	- Opened by:
	- Assigned to:
	- Parent ID:
	- Prereq IDs:
	- Related IDs:
	- Target OS:
	- Test environment:
	- Version and build:
	- Requirements  [Feature]:
		- Hierarchical bulleted list.
	- Steps to reproduce [Bug]:
		- …
	- Incorrect behavior [Bug]:
	- Expected behavior [Bug]:
	- Reproduced [Bug]: [No, or when, where and how]
	- Possible cause [Bug]:
	- Actual cause [Bug]:
		- …
	- Estimated effort: [High|Avg|Low]
	- Actual effort: [High|Avg|Low]
	- Progress log:
		- …
	- Decisions:
		- …
	- Actual fix [Bug]:
	- Branch:
	- Commit:
	- Test case: [Reason not applicable, or CI test case #]
	- Acceptance signoff:
	- Superseded by ID:
	- Closed:
