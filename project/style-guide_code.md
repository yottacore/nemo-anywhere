<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere code style

How the C in this repo is written, and why. Companion to [design.md](design.md), [style-guide_ui.md](style-guide_ui.md) and [../contributing.md](../contributing.md).

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [The one rule that beats the rest](#the-one-rule-that-beats-the-rest)

- [No formatter config yet](#no-formatter-config-yet)

- [Layout](#layout)

- [Language and types](#language-and-types)

- [Naming](#naming)

- [Files](#files)

- [Platform-specific code](#platform-specific-code)

- [Comments](#comments)

- [Memory and errors](#memory-and-errors)

- [What the lint gate rejects](#what-the-lint-gate-rejects)

- [Tests](#tests)

- [Other languages in the tree](#other-languages-in-the-tree)

<!-- /TOC -->

## The one rule that beats the rest

This began as a fork of a twenty-year-old GNOME codebase. It no longer follows upstream and never will, so the inherited code is ours to improve. New code looks like the code beside it, so each file reads as one piece. Restyling or cleaning up inherited code is welcome, as its own change rather than mixed into a fix.

Vendored code is left alone entirely. Do not restyle it, do not rename in it, do not "modernize" it. The vendored trees are `vendor/` at the repo root (icons, themes, the SHCL header and blake3) and `source/cut-n-paste-code/`.

## No formatter config yet

There is no `.clang-format` yet. Nothing ties the tree to upstream Nemo, so a config may cover the whole tree and reformat inherited files. Vendored code stays out of it.

Until one is added, the rules are written down here.

## Layout

- Tabs to indent. A tab is eight columns, so deep nesting gets uncomfortable fast, which is the point.

- Alignment runs to the nearest tab stop with tabs, then the rest of the way with spaces. The continuation lines in the sample below are two tabs and five spaces, not twenty-one spaces.

- Roughly two thirds of the files carry an Emacs mode line saying the same thing: `/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */`. It came with the inherited code and is optional. Keep it on a file that has one; a new file does not need it.

- Function definitions put the return type on its own line, the name at column zero, and the opening brace on its own line. Parameters that wrap are aligned under the first one.

- Inner blocks are K&R: brace on the same line, closing brace on its own.

- A space between a function name and its opening paren, in calls and in definitions both. `g_free (path)`, not `g_free(path)`. This one is near-universal in the tree and is the fastest way to spot a patch written somewhere else.

- No hard line-length limit. Long is fine when breaking it would hurt.

A sample, from `source/src/nemo-column-layout.c`:

~~~c
static int
move_proportionally (const NemoColumnLayoutItem *items,
		     int                        *widths,
		     const int                  *limits,
		     int                         n_items,
		     int                         direction,
		     int                         amount)
{
	while (amount > 0) {
		gint64 weight = 0;
		int moved = 0;
~~~

## Language and types

- The build sets no `-std`, so the compiler's default applies - gnu17 on current gcc. The inherited code uses GNU extensions freely, so do not move the tree to a strict `-std=c11` without building all of it first.

- Declare at first use rather than at the top of the function. `-Wno-declaration-after-statement` is passed for exactly that. Initialize at the declaration, and `= {0}` for a struct.

- GLib's type names are what the tree uses: `gint`, `gchar`, `gboolean`, and `gsize` for a size or an index. New code matches its neighbor. Never `int` for a length.

- The silent integer rules catch people out. A signed/unsigned comparison and the usual promotions both compile without a word and both go wrong at the edges. Cast on purpose, or change the type.

- A `static inline` in a header beats a function-like macro. Where a macro is the only way, parenthesize every argument and wrap a multi-statement body in `do { ... } while (0)`.

- Headers are self-contained: a header compiles on its own and includes what it uses, so nothing depends on include order. `<glib.h>` comes before any `G_OS_WIN32` guard, because `windows.h` defines `DELETE`.

- No undefined behavior. The ones that actually turn up in C of this age are strict aliasing, signed overflow and reading past the end of an array. Type-pun through `memcpy`, never a pointer cast. Assume nothing about endianness, char signedness, struct padding or pointer width, and never write a struct to a file as raw bytes.

## Naming

- Public functions are `nemo_<file>_<verb>`, matching the file they live in.

- Static helpers are plain lowercase with underscores, no prefix.

- Types are `NemoThing`, macros and enum members are `NEMO_THING`.

- Names are searchable. `upper_bound` beats `ub`. A single letter is fine for a loop index and nowhere else.

- GObject boilerplate follows whatever GObject wants. It is generated shape, not a style choice.

## Files

- One pair per subject: `nemo-thing.c` and `nemo-thing.h`, lowercase with hyphens.

- Header guards are `#ifndef NEMO_THING_H`, not `#pragma once`.

- A file under `src/` or `libnemo-private/` carries a one-line description, then the copyright and the GPL-2.0-only notice, after the mode line if it has one. Two comment shapes are in use and both are fine: an indented block after the description, as in `nemo-column-layout.c`, or a star-continuation comment, as in `nemo-launch-win32.c`. Copy from whichever neighbor you are working next to rather than retyping. Tests carry no block.

- `libnemo-extension/` is LGPL, not GPL-2.0-only, because it is the public API other people's extensions compile against. Do not put a GPL notice in there and do not take an LGPL one out.

- A file that keeps an upstream copyright keeps it. Add a line, do not replace one.

## Platform-specific code

Both conventions are in the tree, and which one wins in general is still an open question in [design.md](design.md). What is settled for new code: anything more than a few lines of Windows-only work gets its own file, named `*-win32.c`, with a header that declares a small portable-looking API. `nemo-clipboard-win32.c` and `nemo-trash-win32.c` are the pattern.

Short branches stay inline under `#ifdef G_OS_WIN32`. A branch that grows past a screen is a sign it wants its own file.

Reach for GLib before writing platform code at all. `GSubprocess` already terminates a process on both sides, so no `windows.h` and no branch.

## Comments

Terse, and about why. The code says what.

Comment the things that cost somebody a day: a workaround for a toolkit bug, an ordering that looks arbitrary but is not, a decision that a later reader would otherwise undo. Do not comment a line that reads fine on its own, and do not open a file with a summary of its own contents.

No banner dividers. ASCII only, except the copyright symbol.

Where a piece of reasoning is longer than a few lines, it goes at the top of the file it belongs to, or into `design.md` with the code pointing at it. `source/src/nemo-column-layout.h` is an example of the first.

## Memory and errors

- Explicit `g_free` and `g_object_unref` are the norm, because that is what the surrounding code does. `g_autofree` and `g_autoptr` appear in newer files and are fine there, but do not go converting old ones.

- Failures come back as `GError`. Check them or pass them up. Do not swallow one silently.

- GLib allocators abort rather than returning NULL, so a NULL check on `g_new` is dead code.

- Anything asked once per file on every pass of the async file engine must not allocate. `g_file_peek_path` over `g_file_get_path`.

- A function that allocates says who frees the result, in one line in the header. The language cannot express it, so either it is written down or the next reader guesses.

- `goto out` is how cleanup is written here: one label, resources released in reverse order. Around twenty-five files do it, and it is the only sanctioned goto.

- No VLAs and no `alloca`; the lint gate below rejects them. A size is either bounded, and a fixed array covers it, or it is not, and it belongs on the heap.

- Build text with `g_strdup_printf`, `g_strconcat` or `GString`. No `sprintf`, `strcpy` or `strcat`, and `g_snprintf` only with its return checked.

## What the lint gate rejects

`cicd/utility/lint.bash` is the gate's lint stage. It runs `lint-c.bash`, which is cppcheck plus the checks on user-facing strings and on the delete guards, then `lint-bash.bash`, which is shellcheck over the project's own scripts, and the Python, PowerShell, identity and prose checks after it. A finding from any of them fails the gate. Where a tool itself is missing, that step warns and is skipped, so a box without cppcheck, python or shellcheck cannot hard-block a push; `CPPCHECK_STRICT=1` and `SHELLCHECK_STRICT=1` turn those misses into failures.

- `alloca`, and therefore `g_newa`. Use `g_new0` and `g_free` even for three ints.

- Any cppcheck finding at all, including informational ones. A known false positive gets an inline suppression with the reason written next to it.

- Title case in a user-facing string. Menu items, labels and dialog text are sentence case. A proper noun that trips the check goes in the checker's own list, with its reason.

- Any shellcheck finding at all, down to style level. A rule that has to be off carries a `shellcheck disable=` line with its reason, either at the site or in the file's header block, and new ones go at the site so the rest of the file keeps its cover.

cppcheck is scoped to the files a change touched, but it lints the whole of each of those files, so touching a large old file can raise findings that were already sitting in it. On dev and main, which only take merges, a change is what the latest merge brought in, so the release merge the gate checks on main is linted too. The string check is whole-tree, because the tree is already clean and a Title Case label pasted in from upstream should be caught wherever it turns up. shellcheck is whole-tree too, and covers every script written for this project: the `*.bash` files, plus `cicd/hooks/pre-push` and `utility/runfm`, which are named by whatever runs them. The scripts under `source/` are upstream's, and `n8git_backup-and-publish` is a shared copy maintained elsewhere, so neither is linted here.

## Tests

New behavior arrives with a test. A fix arrives with a test that fails without the fix, and showing that it fails is part of the work.

Tests live in `source/test/`. A POSIX-only test is registered under `if not is_windows` in `source/test/meson.build`, or it breaks the Windows cross build. A Windows-only test is registered under `if is_windows`, and its source carries no stub for other platforms.

Scratch directories come from `test_scratch_dir` in `test-scratch.h`, not `g_dir_make_tmp`. It takes the same arguments, and the directory is removed when the test exits.

A test that reads real user configuration is a test that fails on somebody else's machine. Point `HOME`, `APPDATA` and `XDG_CONFIG_HOME` at a scratch directory first.

The suite needs a display. `cicd/linux/run-tests.bash` starts one of its own, so run the suite through it rather than a bare `meson test`. Without a display, a good part of it fails in ways that read like real assertion failures.

## Other languages in the tree

- Bash files are named `*.bash` and must pass shellcheck. No formatter. Tabs.

- PowerShell is tabs, `Set-StrictMode -Version Latest`, and no BOM on anything carrying a shebang.

- Python is tabs in helpers written for this project. The inherited Python is four spaces and stays that way: the action layout editor, the meson install helpers, the sample action file.
