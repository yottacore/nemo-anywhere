<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere UI style

How the interface behaves and reads, and why. Companion to [design.md](design.md), which has the reasoning at more length, and [style-guide_code.md](style-guide_code.md).

Most of this came out of a bug. Where a rule looks fussy, there is usually a closed item in [backlog.md](backlog.md) behind it.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Words](#words)

- [When to ask](#when-to-ask)

- [Dialogs](#dialogs)

- [Keyboard and focus](#keyboard-and-focus)

- [What a platform cannot do](#what-a-platform-cannot-do)

- [Icons](#icons)

- [Layout](#layout)

- [What the checks catch](#what-the-checks-catch)

<!-- /TOC -->

## Words

- Sentence case everywhere: menu items, labels, buttons, tooltips and dialog text. Only the first word gets a capital.

- A name keeps its capital wherever it stands. That covers the platforms, the toolkit, formats and acronyms, and the named places in the sidebar such as Trash and Home. A command, tab or page named inside a sentence keeps its capital too, as in "the Paste command" or "Copy settings to Default".

- Upstream Nemo is mostly Title Case. A label copied from it has to be brought down, and the lint gate will say so if it is not.

- American spelling. "Color", "gray", "canceled".

- Say what happens, in plain words. "Delete permanently" beats "Remove from disk", and "Move here" beats "OK".

- The Windows word where the thing is a Windows thing. "Open as Administrator" on Windows and "Open as Root" on Linux, "Windows properties" for the shell's own sheet.

- Shortcut text is left alone. It is written the way the toolkit writes it.

## When to ask

A question is a speed bump. It is worth it where a slip costs something that cannot be taken back, and nowhere else, since a question asked too often gets answered without being read.

- Ask before anything that removes or overwrites a file: trash, delete, and a copy or move over something already there.

- Ask before a drop that moves files. A mouse drag is the easiest way to move a folder by accident, and often nobody notices it happened. Copies by drag do not ask by default, since nothing is lost. A link drop opens the Make link dialog, which is its own question.

- The question names what is about to happen, to what, and where. `Move 3 items to "Archive"?`, not "Are you sure?".

- Some things are refused rather than asked. Nobody deletes their home folder from a file manager on purpose, so that is not a question.

- Some questions are asked whatever the preferences say: a large job, or one that no trash or delete command in a window started. The question says why it is being asked.

- One question per action. A drop on the Trash asks under the trash setting, not twice. On Windows the shell's own recycle prompt is switched off, so ours is the only one.

- A progress window never covers a question that is waiting for an answer.

The details and the history are in [design.md, File operations](design.md#file-operations).

## Dialogs

- Buttons are named for what they do: "Delete", "Replace", "Skip", "Move here". Each has a mnemonic.

- A question that can remove files starts with Cancel as the default. A stray Enter should cost nothing. This reversed an earlier choice, and going back to it is deferred on the backlog until the app has been in steady daily use for a while.

- Message text in an error or question dialog can be selected and copied. People paste errors into bug reports and search boxes.

- A dialog that grows, such as the archive dialog when Options opens, stays where it was on screen. It does not walk down the screen, and it stays inside the work area.

- A settings window opens big enough for its largest page, so no page starts out behind a scrollbar.

- Something that cannot be done is not offered and then refused. Gray it out, and where the reason is not obvious, say it in the dialog. The link copy dialog is the example: where the destination cannot hold a link, the other choices are grayed and a line says why.

## Keyboard and focus

The file list is where the keyboard belongs. Anything that leaves the focus somewhere else by accident is a bug, because the arrow keys then do nothing and it looks like the app has stopped listening.

- Column headings, tabs and path buttons never keep the focus. A click on one puts the keyboard back in the file list. Tab and Shift+Tab pass over the tab strip.

- Closing the path entry returns the focus to the file list. Clicking anywhere else closes it. Switching to another program does not, so a half-typed path survives the trip.

- A click on a place in the sidebar leaves the keyboard in the sidebar. Whoever clicked it may want the next place down.

- A menu opened from the keyboard, with the menu key or with Ctrl+F10 for the folder itself, opens against whatever has the focus, not at the mouse pointer, which could be on another monitor.

- A trash or delete key within a second of a window appearing or taking the focus is ignored. That key was meant for whatever window had the focus a moment ago.

- One keystroke should do the obvious whole job. Ctrl+H shows both kinds of hidden file on Windows at once, and preferences can still set the two apart.

- A tab that is grayed out must also refuse to switch. GTK switches to an insensitive page on a click, so graying the page alone leaves a dead pane on screen.

## What a platform cannot do

- A whole feature a platform does not have is left out on that platform. The Windows preferences page is not built into the other platforms' dialogs. A menu entry for a service that is not running hides rather than failing.

- A choice inside a dialog that only some machines can use is grayed, not hidden, so the dialog keeps the same layout everywhere.

- An action that would do nothing is grayed. "Open as Administrator" in a copy that is already elevated is one.

## Icons

- Use a standard freedesktop icon name wherever one fits, so every theme has it. Names only Mint themes carry were all mapped to standard ones, and new code should not bring them back.

- The app's own artwork is compiled into the program under names of its own. A theme cannot override it, which is why the link overlays live there.

- A missing icon falls back through the theme's `Inherits` line to Adwaita and then hicolor. The result can be a glyph that does not match, but never a blank.

- A shortcut and a symlink have to look different at a glance. A shortcut gets the arrow and a symlink or junction gets a chain link.

- A shortcut to a folder shows the theme's folder icon with the overlay, not the shell's folder art, which looks out of place next to the theme's folders.

- New art matches the flat style of the icons beside it. An icon that points one way is mirrored for right-to-left.

- Check new art on both a light and a dark theme. The breadcrumb bar once looked fine in light mode and was unreadable in dark.

How theme lookup works is in [design.md, Appearance and themes](design.md#appearance-and-themes).

## Layout

- 12 pixels between a label and its field. A field stops at the same right-hand margin as the rest of the section, and fields in a group start at the same point as each other. The command fields on the Behavior page are the pattern.

- A button that acts on a tab's content sits at the top right of that content, not in the tab header. Each tab has its own buttons, and they do not come and go as tabs change.

- Two unselected tabs next to each other get a divider. Most themes draw none, and a row of tabs then reads as one strip.

- A value too long for its column gets a tooltip with the whole value. That holds whatever the item tooltip setting says, since it is only showing what is already on screen.

- Styling the app needs goes in its own stylesheet, so it holds under whatever theme is in use.

## What the checks catch

- `cicd/utility/lint-ui-case.py` checks every translatable string in the tree for sentence case. It runs from `cicd/utility/lint-c.bash` and fails the gate. A name that trips it goes in its `NAMES` list, and a whole string it reads wrong goes in `KEEP`, each with a one-line reason.

- `lint-c.bash` also fails if a delete or an overwrite in `nemo-file-operations.c` can get past the delete guards without being seen. It also fails if something other than a window could start a trash, delete or move, such as a new bus method.

- `test-nemo-prefs-widgets` fails if a preferences widget the code looks up by name has gone missing from the layout file.

Everything else here is checked by eye, so a change that touches the interface gets looked at on screen, in both a light and a dark theme.
