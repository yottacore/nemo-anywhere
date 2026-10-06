#!/usr/bin/env bash

##	- Purpose: Check-only C lint. Nothing is rewritten.
##	- On a feature branch, cppcheck covers the .c/.h files changed since the
##	  merge base with the integration branch (dev, else main), plus anything
##	  uncommitted (tracked, staged, untracked). On dev, main or the named base,
##	  which only take merges, it covers every first-party .c/.h in the tree,
##	  plus untracked ones. Vendored code (vendor/, source/cut-n-paste-code/) is
##	  left out either way.
##	- cppcheck findings (error/warning/portability) fail the gate. A missing
##	  cppcheck skips with a warning so an unprovisioned box can't hard-block a
##	  push; CPPCHECK_STRICT=1 turns that miss into a hard failure.
##	- Then the UI-case check (cicd/utility/lint-ui-case.py), which is whole-tree
##	  rather than diff-scoped: the tree is already clean, so there is no legacy
##	  noise to drown in, and a Title Case label pasted from upstream is caught
##	  wherever it sits. A missing python skips it the same way cppcheck does.
##	- Then the settings-handler check (cicd/utility/lint-pref-handlers.py), also
##	  whole-tree, which pairs each disconnect with the connect it belongs to
##	  and checks each handler against its row in design.md, "Handlers on settings
##	  groups", and each key against the group the settings table puts it in, and
##	  the accelerator check (cicd/utility/lint-accels.py), whole-tree too.
##	- Then the ownership check (cicd/utility/lint-ownership.py), whole-tree:
##	  each pointer a header's function returns has a "(transfer ...)" line
##	  above its definition.
##	- Runs the same everywhere bash + git + cppcheck exist (Linux host, MSYS2).
##	- Syntax: lint-c.bash [--list-files] [base-branch]
##	  --list-files prints the C files the cppcheck pass would cover, and stops.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

set -Eeuo pipefail

listOnly=0
if [[ "${1:-}" == "--list-files" ]]; then listOnly=1; shift; fi
base="${1:-}"
strict="${CPPCHECK_STRICT:-0}"
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

fEcho(){ echo "[ $* ]"; }

## Each check prints its test ID, read from the "## Test ID:" line above it, so
## the comment stays the only copy. A check fails by exiting, hence the trap.
declare -A testIds=()
while read -r fn tag; do testIds[$fn]="$tag"; done < <(awk '
	/^##[[:space:]]+Test ID: / { id = $NF; next }
	/^##/ { next }
	/^fCheck[A-Za-z0-9_]+\(\)\{/ { fn = $0; sub(/\(\).*/, "", fn); if (id != "") print fn, id }
	{ id = "" }' cicd/utility/lint-c.bash)
curTest=""
cacheDir=""
fCleanCache(){
	[[ "$cacheDir" == cicd/artifacts/cppcheck.* && -d "$cacheDir" ]] || return 0
	rm -rf -- "$cacheDir"
}
trap 'fCleanCache; [[ -n "$curTest" ]] && printf "%s %-22s FAIL\n" "${testIds[$curTest]:-?}" "${curTest#fCheck}"' EXIT
fRun(){
	if ((listOnly)); then return 0; fi
	curTest="$1"
	"$1"
	printf '%s %-22s OK\n' "${testIds[$1]:-?}" "${1#fCheck}"
	curTest=""
}

## Every delete in nemo-file-operations.c goes through file_delete_wrapper, so
## the delete guard sees it. Two calls in make_link_copy sat outside it until
## 20260917 and were reachable with no guard at all. Whole-tree, since the rule
## is about that one file whether or not this change touched it.
## Test ID: rh30we0r
fCheckDeleteWrapper(){
	local src='source/libnemo-private/nemo-file-operations.c'
	local n

	[[ -f "$src" ]] || return 0

	n="$(grep -c -F 'g_file_delete (' "$src" || true)"
	if [[ "$n" != 1 ]]; then
		fEcho "FAIL: ${src}: ${n} g_file_delete calls, expected the 1 in file_delete_wrapper"
		grep -n -F 'g_file_delete (' "$src" || true
		exit 2
	fi
}
fRun fCheckDeleteWrapper

## G_FILE_COPY_OVERWRITE destroys the target inside glib, where there is nothing
## of ours to hook, so the test guard has to ask before the flag goes on. Both
## uses sit a few lines after the ask at their retry label; anything further
## away has grown a path that reaches the flag without asking. Whole-tree, same
## reason as above.
## Test ID: rh34b4zr
fCheckOverwriteAsk(){
	local src='source/libnemo-private/nemo-file-operations.c'
	local bad

	[[ -f "$src" ]] || return 0

	bad="$(awk '
		/testguard_allows_overwrite \(/ { asked = NR }
		/G_FILE_COPY_OVERWRITE/ {
			if (NR - asked > 15) print NR ": " $0
		}
	' "$src")"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: ${src}: G_FILE_COPY_OVERWRITE with no test guard ask above it"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckOverwriteAsk

## Nothing may trash, delete or move a person's files unless it started in a
## window. Another program must have no way in: the bus exported CopyURIs,
## MoveURIs and EmptyTrash, left over from the Nemo desktop, until 20260917.
## These four checks hold that. A new entry on any of their lists wants a
## reason, and whoever adds one should be able to say what window starts it.

## Bus methods: the freedesktop file manager interface, and the tab hand-over
## between our own windows, which lists windows and opens a folder in a tab.
## None of them touches a file. Tests are left out: a test that stands in for
## another program's server is not something the app exports.
## Test ID: rh3qr9y8
fCheckBusMethods(){
	local allowed=' ShowFolders ShowItems ShowItemProperties ListWindows TakeTab '
	local name bad=""

	while read -r name; do
		[[ -z "$name" ]] && continue
		[[ "$allowed" == *" ${name} "* ]] || bad+="${name} "
	done <<< "$(grep -rhoE --exclude-dir=test "<method name=['\"][A-Za-z0-9_]+" source | sed -E "s/.*=['\"]//" | sort -u || true)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: bus methods not on the list in lint-c.bash: ${bad% }"
		grep -rnE --exclude-dir=test "<method name=['\"]" source || true
		exit 2
	fi
}
fRun fCheckBusMethods

## Application actions are exported on the bus too. Only quit.
## Test ID: rh3qr9y9
fCheckAppActions(){
	local hits

	hits="$(grep -rnE 'g_simple_action_new|GActionEntry|g_action_map_add_action_entries' \
		source/src source/libnemo-private source/eel 2>/dev/null \
		| grep -v -F 'g_simple_action_new ("quit"' || true)"
	if [[ -n "$hits" ]]; then
		fEcho "FAIL: a new application action; another program can call it over the bus"
		printf '%s\n' "$hits"
		exit 2
	fi
}
fRun fCheckAppActions

## Who may start a trash, delete, empty trash or move job. Each file here is
## reached from something done in a window.
## Test ID: rh3qr9ya
fCheckJobCallers(){
	local allowed=' '
	allowed+='source/src/nemo-view.c '				# trash and delete commands, drops, paste
	allowed+='source/src/nemo-tree-sidebar.c '		# trash and delete commands in the tree
	allowed+='source/src/nemo-places-sidebar.c '		# empty trash, drops
	allowed+='source/src/nemo-trash-bar.c '			# empty trash button
	allowed+='source/src/nemo-mime-actions.c '			# broken link dialog
	allowed+='source/src/nemo-template-config-widget.c '	# remove button in preferences
	allowed+='source/libnemo-private/nemo-archive.c '	# compress dialog, delete originals
	allowed+='source/libnemo-private/nemo-file-undo-operations.c '	# undo
	allowed+='source/libnemo-private/nemo-dnd-win32.c '	# a move dragged out of a window
	local file bad=""

	while read -r file; do
		[[ -z "$file" ]] && continue
		[[ "$allowed" == *" ${file} "* ]] || bad+="${file} "
	done <<< "$(grep -rlE 'nemo_file_operations_(trash_or_delete|delete|empty_trash|move|copy_move)(_by_user)?[[:space:]]*\(' \
		source --include='*.c' \
		| grep -v -e '^source/test/' -e '^source/libnemo-private/nemo-file-operations\.c$' || true)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: trash, delete or move started from a file not on the list in lint-c.bash: ${bad% }"
		exit 2
	fi
}
fRun fCheckJobCallers

## Raw deletes outside the jobs. What is left only touches files the app made
## for itself.
## Test ID: rh3qr9yb
fCheckRawDeletes(){
	local allowed=' '
	allowed+='source/libnemo-private/nemo-file-operations.c '	# the jobs
	allowed+='source/libnemo-private/nemo-delete-guard.c '		# the guarded delete the jobs use
	allowed+='source/libnemo-private/nemo-trash-win32.c '		# the recycle bin, behind the jobs
	allowed+='source/libnemo-private/nemo-link-win32.c '		# its own probe, and a link it failed to finish
	allowed+='source/libnemo-private/nemo-archive.c '			# an archive it was writing
	allowed+='source/libnemo-private/nemo-crash.c '			# old crash reports
	allowed+='source/libnemo-private/nemo-cache-db.c '		# a damaged cache file it is replacing
	allowed+='source/libnemo-private/nemo-desktop-thumbnail.c '	# thumbnail cache
	allowed+='source/src/nemo-bookmark-list.c '			# --reset
	allowed+='source/src/nemo-main-application.c '			# --reset
	local file bad=""

	while read -r file; do
		[[ -z "$file" ]] && continue
		[[ "$allowed" == *" ${file} "* ]] || bad+="${file} "
	done <<< "$(grep -rlE '(^|[^>.[:alnum:]_])(g_file_delete|g_file_delete_async|g_file_trash|g_file_trash_async|g_unlink|g_remove|g_rmdir|unlink|rmdir|remove|_wunlink|_wremove|_wrmdir|DeleteFileW|DeleteFileA|RemoveDirectoryW|RemoveDirectoryA|SHFileOperationW|SHEmptyRecycleBinW)[[:space:]]*\(' \
		source/src source/libnemo-private source/libnemo-extension source/eel --include='*.c' || true)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a raw delete in a file not on the list in lint-c.bash: ${bad% }"
		exit 2
	fi
}
fRun fCheckRawDeletes

## A tree removal walks into a real folder only, never through a symlink or a
## junction. GIO calls a junction a folder on Windows even with NOFOLLOW, so the
## type alone let three of these walks through one until 20260918. Any function
## that lists a folder and removes things has to ask
## nemo_delete_guard_is_real_folder, or be on the list with a reason.
## Test ID: rh5nme6j
fCheckTreeWalks(){
	local allowed=' '
	allowed+='copy_move_directory '	# walks a link only to copy it; a move through one is turned into a copy
	allowed+='sweep_old_reports '		# its own crash-*.txt files, one level, no walk
	local bad

	bad="$(awk -v allowed="$allowed" '
		FNR == 1 { fn = "" }
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ {
			fn = $1; sub(/\(.*/, "", fn)
			walks = 0; removes = 0; gated = 0
		}
		/(nemo_enumerate_children|g_file_enumerate_children|g_dir_open) *\(/ { walks = 1 }
		/(file_delete_wrapper|g_file_delete|g_remove|g_rmdir|g_unlink|delete_file|delete_dir|delete_trash_file|delete_real_tree|remove_target_recursively|nemo_delete_guard_remove_tree) *\(/ { removes = 1 }
		/nemo_delete_guard_is_real_folder/ { gated = 1 }
		/^}/ {
			if (fn != "" && walks && removes && !gated && index(allowed, " " fn " ") == 0)
				print FILENAME ": " fn
			fn = ""
		}
	' source/libnemo-private/*.c source/src/*.c)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: lists a folder and removes things without nemo_delete_guard_is_real_folder"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckTreeWalks

## Same rule for the suite, narrowed to the shape that can do the damage: a
## function that calls itself, lists a directory and removes what it finds.
## The fixtures plant links on purpose, so one of those walking into a link
## takes out something the test never made. test-scratch.c owns the one guarded
## walk; everything else goes through it.
## Test ID: rhaqte91
fCheckTestTreeWalks(){
	local bad

	bad="$(awk '
		FNR == 1 { fn = "" }
		FILENAME ~ /test-scratch\.c$/ { next }
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ {
			fn = $1; sub(/\(.*/, "", fn)
			walks = 0; removes = 0; recurses = 0; defline = 1
		}
		/(nemo_enumerate_children|g_file_enumerate_children|g_dir_open) *\(/ { walks = 1 }
		/(g_file_delete|g_remove|g_rmdir|g_unlink) *\(/ { removes = 1 }
		fn != "" && !defline && (index($0, fn "(") > 0 || index($0, fn " (") > 0) { recurses = 1 }
		/^}/ {
			if (fn != "" && walks && removes && recurses)
				print FILENAME ": " fn
			fn = ""
		}
		{ defline = 0 }
	' source/test/*.c)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a test rolls its own tree removal - use test_scratch_remove_tree"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckTestTreeWalks

## Two test helpers that used to be copied instead of shared. The check macro
## was in the tree in two spellings across sixty-odd files, and twenty-odd
## tests each set the same three environment variables by hand to get a
## throwaway config root. A fresh copy of either drifts from the rest.
## Test ID: rhas2bm0
fCheckTestHelpers(){
	local bad

	bad="$(grep -ln '^#define check' source/test/*.c || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a test defines its own check macro - include test-check.h"
		printf '%s\n' "$bad"
		exit 2
	fi

	## test-scratch.c is the helper. test-nemo-config-root.c is the one test
	## about how the config root is picked, so it has to point the variables
	## at separate directories itself.
	bad="$(grep -ln 'g_setenv ("XDG_CONFIG_HOME"' source/test/*.c | grep -vE '(test-scratch|test-nemo-config-root)\.c$' || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a test points the config root by hand - use test_scratch_config_home"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckTestHelpers

## Row shading remembers which renderers it has already told they have no
## background, so it can skip saying it again on every redraw. That is only
## safe while cell_set_plain is the one place the property is turned off. A
## second writer would leave the memory wrong and the shading with it, quietly.
## Test ID: rhabc73r
fCheckCellPlain(){
	local src='source/src/nemo-list-view.c'
	local n

	[[ -f "$src" ]] || return 0

	n="$(grep -c -F '"cell-background-set"' "$src" || true)"
	if [[ "$n" != 1 ]]; then
		fEcho "FAIL: ${src}: ${n} writes of cell-background-set, expected the 1 in cell_set_plain"
		grep -n -F '"cell-background-set"' "$src" || true
		exit 2
	fi

	n="$(grep -rl -F '"cell-background' source/src source/libnemo-private source/eel | grep -cv 'nemo-list-view.c' || true)"
	if [[ "$n" != 0 ]]; then
		fEcho "FAIL: cell-background is written outside nemo-list-view.c"
		grep -rn -F '"cell-background' source/src source/libnemo-private source/eel | grep -v 'nemo-list-view.c' || true
		exit 2
	fi
}
fRun fCheckCellPlain

## The list view works out every column width itself and hands out widths that
## come to exactly the row. An expanding column lets GTK add more on top, from a
## share it worked out while the view was wider, and it will not give that back
## until some width really changes - so the row scrolls sideways while fitting.
## The places sidebar has its own tree view and is not covered by any of this.
## Test ID: rhaz9ph0
fCheckColumnExpand(){
	local src='source/src/nemo-list-view.c'

	[[ -f "$src" ]] || return 0

	if grep -n -F 'gtk_tree_view_column_set_expand' "$src"; then
		fEcho "FAIL: ${src}: no column expands; the layout owns the widths"
		exit 2
	fi
}
fRun fCheckColumnExpand

## Column samples only ever grow, so a file that leaves keeps its widths in
## every column but Name. That held a scrollbar on a folder with nothing left
## needing it, emptied or not. A removal marks the samples stale, and a layout
## whose minimums overflow while they are stale samples the folder again.
## Test ID: rhjrtq18
fCheckStaleSamples(){
	local src='source/src/nemo-list-view.c'
	local body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^drop_name_sample / { on=1 } on { print } on && /^}/ { on=0 }' "$src")"
	if ! grep -q -F 'samples_stale = TRUE' <<<"$body"; then
		fEcho "FAIL: ${src}: drop_name_sample must mark the samples stale"
		exit 2
	fi

	body="$(awk '/^layout_columns / { on=1 } on { print } on && /^}/ { on=0 }' "$src")"
	if ! grep -q -F 'resample_rows_soon' <<<"$body"; then
		fEcho "FAIL: ${src}: layout_columns must resample stale samples when the minimums overflow"
		exit 2
	fi
}
fRun fCheckStaleSamples

## The status bar count is read on a timer that starts with the first change
## of a burst, while the changes themselves go in on a shorter one. Files
## removed one after another by some other program left the count a few too
## high, for good, since nothing counted again once the last ones were in.
## Test ID: rhjvme48
fCheckStatusAfterChanges(){
	local src='source/src/nemo-view.c'
	local body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^process_old_files / { on=1 } on { print } on && /^}/ { on=0 }' "$src")"
	if ! grep -q -F 'schedule_update_status (view)' <<<"$body"; then
		fEcho "FAIL: ${src}: process_old_files must schedule a status update once the changes are in"
		exit 2
	fi
}
fRun fCheckStatusAfterChanges

## The list view runs in fixed-height mode, which halves what a big folder
## costs to load. GTK only allows it while every column sizes FIXED, and it
## goes wrong quietly rather than loudly: a column left to size itself makes
## the tree view measure every row again, which is the cost being avoided.
## So the two are pinned together.
## Test ID: rhb15mg0
fCheckFixedHeight(){
	local src='source/src/nemo-list-view.c'
	local other

	[[ -f "$src" ]] || return 0

	if ! grep -q -F 'gtk_tree_view_set_fixed_height_mode' "$src"; then
		fEcho "FAIL: ${src}: the list view must ask for fixed-height mode"
		exit 2
	fi

	## The call wraps in this file, so read it up to its semicolon rather than
	## a line at a time.
	other="$(awk '
		/gtk_tree_view_column_set_sizing/ { open=1; at=NR; text="" }
		open { text = text $0 }
		open && /;/ {
			if (text !~ /GTK_TREE_VIEW_COLUMN_FIXED/) print at ": " text
			open=0
		}' "$src")"
	if [[ -n "$other" ]]; then
		echo "$other"
		fEcho "FAIL: ${src}: every column sizes FIXED, or fixed-height mode is unsafe"
		exit 2
	fi
}
fRun fCheckFixedHeight

## The measuring cache in the list view. Each rule below is what keeps a
## remembered width honest; without one the columns come out wrong or the
## cache grows per row.
## Test ID: rhb35y3r
fCheckMeasureCache(){
	local src='source/src/nemo-list-view.c'
	local body

	[[ -f "$src" ]] || return 0
	grep -q -F 'samples->measured' "$src" || return 0

	body="$(awk '/^row_width_for_column/ { on=1 } on { print } on && /^}/ { exit }' "$src")"

	## Bold and light rows lay out wider or narrower at the same text.
	if ! grep -q -F 'weight == NORMAL_TEXT_WEIGHT' <<< "$body"; then
		fEcho "FAIL: ${src}: only a normal-weight cell may reuse a remembered width"
		exit 2
	fi

	## Thrown away with the samples, or a zoom leaves widths from the old font.
	if ! awk '/^column_samples_free/ { on=1 } on { print } on && /^}/ { exit }' "$src" |
	     grep -q -F 'samples->measured'; then
		fEcho "FAIL: ${src}: the remembered widths must go when the samples do"
		exit 2
	fi

	## A date is nearly all distinct values, so the table needs a ceiling.
	if ! grep -q -F 'MEASURED_TEXTS_MAX' <<< "$body"; then
		fEcho "FAIL: ${src}: remembering a width must stop at MEASURED_TEXTS_MAX"
		exit 2
	fi
}
fRun fCheckMeasureCache

## A theme change can bring a new font, which makes every remembered width
## wrong. The handler has to send the rows back to be measured - but only on a
## change that moved something, since style-updated also fires for a state or
## a CSS class and 50,000 rows is not free.
## Test ID: rhb4ppn8
fCheckStyleRemeasure(){
	local src='source/src/nemo-list-view.c'
	local body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^tree_view_style_updated/ { on=1 } on { print } on && /^}/ { exit }' "$src")"

	if ! grep -q -F 'remeasure_rows' <<< "$body"; then
		fEcho "FAIL: ${src}: a theme change must send the rows back to be measured"
		exit 2
	fi

	if ! grep -q -F 'measure_style_id' <<< "$body"; then
		fEcho "FAIL: ${src}: remeasure only where the font or the theme sizes moved"
		exit 2
	fi
}
fRun fCheckStyleRemeasure

## The zoom slider is the last thing in the status bar, so it needs a margin of
## its own or the trough runs into the window edge.
## Test ID: rhb4yasr
fCheckSliderMargin(){
	local src='source/src/nemo-statusbar.c'

	[[ -f "$src" ]] || return 0

	if ! grep -q -F 'gtk_widget_set_margin_end (GTK_WIDGET (zoom_slider)' "$src"; then
		fEcho "FAIL: ${src}: the zoom slider needs a margin at the window edge"
		exit 2
	fi
}
fRun fCheckSliderMargin

## An icon whose picture changes size has to be laid out again before the next
## paint, or its name jumps up under a short thumbnail and back down a frame later.
## Test ID: rhfgzbc8
fCheckIconRelayout(){
	local src='source/libnemo-private/nemo-icon-container.c' body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^schedule_redo_layout \(/,/^}/' "$src")"
	if ! grep -q -F 'GDK_PRIORITY_REDRAW' <<< "$body"; then
		fEcho "FAIL: ${src}: schedule_redo_layout must run ahead of the redraw"
		exit 2
	fi

	body="$(awk '/^nemo_icon_container_update_icon \(/,/^}/' "$src")"
	if ! grep -q -F 'schedule_redo_layout' <<< "$body"; then
		fEcho "FAIL: ${src}: nemo_icon_container_update_icon must relayout when the picture changes size"
		exit 2
	fi
}
fRun fCheckIconRelayout

## A transition on the path bar / location bar stack paints the bar going out
## while its resize can still be pending, which logs a GTK critical on a folder
## change.
## Test ID: rhfvghdg
fCheckToolbarStack(){
	local src='source/src/nemo-toolbar.c' types

	[[ -f "$src" ]] || return 0

	types="$(grep -E -o 'GTK_STACK_TRANSITION_TYPE_[A-Z_]+' "$src" | sort -u || true)"
	if [[ "$types" != 'GTK_STACK_TRANSITION_TYPE_NONE' ]]; then
		fEcho "FAIL: ${src}: the path bar stack must switch with no transition"
		exit 2
	fi
}
fRun fCheckToolbarStack

## Sizing a folder of images reads: either the folder's own image size, which is
## already stored, or the image default, which has to stay a default so the
## preference can still move it. Writing here would pin a folder at whatever the
## setting said the first time it was opened, and in a window that is not
## remembering per folder it would follow you into the next folder.
## Test ID: rhbe2n9h
fCheckImageDefault(){
	local src='source/src/nemo-icon-view.c'
	local body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^(size_for_mostly_images|update_mostly_images)/ { on=1 } on { print } on && /^}/ { on=0 }' "$src")"

	if ! grep -q -F 'nemo_directory_is_mostly_images' <<< "$body"; then
		fEcho "FAIL: ${src}: update_mostly_images must ask what is in the folder"
		exit 2
	fi

	if grep -qE '(^|[[:space:](])set_icon_size \(|nemo_folder_settings_set|nemo_window_set_ignore_meta_icon_size' <<< "$body"; then
		fEcho "FAIL: ${src}: sizing a folder of images must not write anything back"
		exit 2
	fi
}
fRun fCheckImageDefault

## With per-folder settings off the window holds three icon sizes: plain and
## pictures for the icon view, and one for list view. Anything in the icon view
## reaching past held_icon_size/hold_icon_size to the window picks one without
## asking which kind of folder is in front, and that is how the picture size
## once ended up on the rows of the next list. List view touches only its own,
## or the icon view's 64 turns up on the next list after a folder of pictures.
## Test ID: rhdncew0
fCheckHeldIconSize(){
	local src='source/src/nemo-icon-view.c'
	local stray

	[[ -f "$src" ]] || return 0

	stray="$(awk '/^(held_icon_size|hold_icon_size) / { on=1 } !on && /nemo_window_[gs]et_ignore_meta_(image_)?icon_size/ { print FNR": "$0 } on && /^}/ { on=0 }' "$src")"

	if [[ -n "$stray" ]]; then
		fEcho "FAIL: ${src}: read or write the window's held icon size through held_icon_size/hold_icon_size:"
		fEcho "${stray}"
		exit 2
	fi

	stray="$(grep -n -E 'nemo_window_[gs]et_ignore_meta_(image_)?icon_size' source/src/nemo-list-view.c || true)"
	if [[ -n "$stray" ]]; then
		fEcho "FAIL: source/src/nemo-list-view.c: list view holds its size in the window's list slot only:"
		fEcho "${stray}"
		exit 2
	fi
}
fRun fCheckHeldIconSize

## Windows show the program's icon, set once as the default. Upstream set each
## window to its folder's icon, so a taskbar full of them showed generic folders
## and nothing said which program they were.
## Test ID: rhdsqqx0
fCheckWindowIcon(){
	local stray

	if ! grep -q -F 'gtk_window_set_default_icon_name ("nemo-anywhere")' source/src/nemo-application.c; then
		fEcho "FAIL: source/src/nemo-application.c: the program icon must be the default window icon"
		exit 2
	fi

	stray="$(grep -n -E 'gtk_window_set_icon(_name)? \(' source/src/nemo-window*.c source/src/nemo-file-management-properties.c || true)"
	if [[ -n "$stray" ]]; then
		fEcho "FAIL: a main window or the preferences sets its own icon over the program's:"
		fEcho "${stray}"
		exit 2
	fi
}
fRun fCheckWindowIcon

## Every copy is its own process under one app id, so a session manager
## takes the first one to register and refuses the rest. Nothing here needs
## registering; the logout block during a copy asks the session manager itself.
## Test ID: rhdx93tr
fCheckNoSessionRegister(){
	local stray
	stray="$(grep -rn -F 'register-session' source/src source/libnemo-private || true)"
	if [[ -n "$stray" ]]; then
		fEcho "FAIL: no copy may register with the session manager:"
		fEcho "${stray}"
		exit 2
	fi
}
fRun fCheckNoSessionRegister

## A checksum is kept on a file in three attributes, and the order they are
## written in is the only thing standing between a torn write and a checksum
## that vouches for contents it has never seen. Reading requires the size and
## the time to match, so the time has to be written last: then a write that
## stops part way leaves the old time next to the new checksum, and the reader
## throws it away. Write the time first and a half-finished write leaves the
## old checksum under a size and time that both match, which no reader can
## catch. No test can hold this - the bad state is one a read cannot tell from
## a good one.
## Test ID: rhd29h29
fCheckDigestAttrOrder(){
	local src='source/libnemo-private/nemo-file-digest.c'
	local body order

	[[ -f "$src" ]] || return 0

	body="$(awk '/^nemo_file_digest_write_attr/ { on=1 } on { print } on && /^}/ { on=0 }' "$src")"

	order="$(grep -o 'nemo_file_xattr_set (file, ATTR_[A-Z]*' <<< "$body" | sed 's/.*ATTR_//' | tr '\n' ' ')"

	if [[ "${order}" != "DIGEST BYTES MTIME " ]]; then
		fEcho "FAIL: ${src}: the checksum attributes must be written DIGEST, BYTES, MTIME (got: ${order:-none})"
		exit 2
	fi
}
fRun fCheckDigestAttrOrder

## A test that cannot run exits 77, which meson counts as a skip. Three tests
## said they were skipping and then exited 0, so the suite counted a pass for
## something that never ran. Two shapes are caught: a skip message whose block
## goes on to exit 0, and one that jumps to the end of a main with no way to
## exit 77. A skip of one case that lets the rest run is not either shape.
## Test ID: rhtg2ye8
fCheckTestSkipExit(){
	local bad

	bad="$(awk '
		function report(){ if (jumped != "" && !has77) print prev ":" jumped ": skips, then jumps to an end that never exits 77" }
		FNR == 1 { if (NR > 1) report(); prev = FILENAME; inmain = 0; on = 0; has77 = 0; jumped = "" }
		/(return|exit)[^;]*(77|TEST_SKIPPED)/ { has77 = 1 }
		/^main *\(/ { inmain = 1; depth = 0 }
		inmain {
			line = $0
			gsub(/"([^"\\]|\\.)*"/, "", line)
			opens = gsub(/\{/, "", line); closes = gsub(/\}/, "", line)
			if (!on && $0 ~ /(g_print|printf)/ && tolower($0) ~ /(skip|nothing to check)/) {
				on = 1; at = FNR; lvl = depth; ok = 0; bad = 0; jumps = 0
			}
			if (on) {
				if ($0 ~ /(return|exit)[^;]*(77|TEST_SKIPPED)/) ok = 1
				if ($0 ~ /(return|exit)[ (]*(0|EXIT_SUCCESS)[ )]*;/) bad = 1
				if ($0 ~ /goto /) jumps = 1
			}
			depth += opens - closes
			if (on && depth < lvl) {
				if (bad && !ok) print FILENAME ":" at ": skips, then exits 0"
				if (jumps) jumped = jumped " " at
				on = 0
			}
			if (/^}/) inmain = 0
		}
		END { report() }
	' source/test/*.c)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a test says it skips and exits 0 - a test that cannot run exits 77"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckTestSkipExit

## Scratch directories come from test_scratch_dir, which removes them at exit
## and keeps two runs apart. One test made and removed its own by hand, and two
## copies running at once destroyed each other's tree. test-scratch.c is the
## helper; test-nemo-scratch.c needs one the helper has no claim on.
## Test ID: rhtg2ye9
fCheckTestScratchDirs(){
	local bad

	bad="$(grep -nE '(^|[^[:alnum:]_])(g_dir_make_tmp|g_mkdtemp|g_mkdtemp_full|mkdtemp)[[:space:]]*\(' source/test/*.c \
		| grep -vE '^source/test/(test-scratch|test-nemo-scratch)\.c:' || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a test makes its own temporary directory - use test_scratch_dir"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckTestScratchDirs

## Only a trash or delete command in a window counts as asked for, which gets
## the lighter confirmation. The guard used to read GTK's current event
## instead, so a delete started from inside any unrelated handler was taken
## for one a person asked for. The caller says so now, and the event only
## names the trigger in the log.
## Test ID: rhtg2yea
fCheckByUser(){
	local allowed=' '
	allowed+='source/src/nemo-view.c '				# trash, delete and empty trash commands
	allowed+='source/src/nemo-tree-sidebar.c '		# trash and delete commands in the tree
	allowed+='source/src/nemo-places-sidebar.c '		# empty trash command
	allowed+='source/src/nemo-trash-bar.c '			# empty trash button
	allowed+='source/src/nemo-mime-actions.c '			# move to trash button in the launcher dialog
	allowed+='source/libnemo-private/nemo-archive.c '	# delete originals, ticked in the compress dialog; still asks
	local file bad=""

	while read -r file; do
		[[ -z "$file" ]] && continue
		[[ "$allowed" == *" ${file} "* ]] || bad+="${file} "
	done <<< "$(grep -rlE 'nemo_file_operations_[a-z_]+_by_user[[:space:]]*\(' source --include='*.c' \
		| grep -v -e '^source/test/' -e '^source/libnemo-private/nemo-file-operations\.c$' || true)"

	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a _by_user job started from a file not on the list in lint-c.bash: ${bad% }"
		exit 2
	fi

	bad="$(awk '
		FNR == 1 { fn = "" }
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ { fn = $1; sub(/\(.*/, "", fn); ev = 0; sets = 0 }
		/gtk_get_current_event *\(/ { ev = 1 }
		/->unattended *=[^=]/ { sets = 1 }
		/^}/ {
			if (fn != "" && ev && sets) print FILENAME ": " fn
			fn = ""
		}
	' source/libnemo-private/*.c source/src/*.c)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: unattended is decided from GTK's current event - the caller says whether a person asked"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckByUser

## A link to a share that is not answering costs about twenty seconds for each
## question asked of it, and a folder waits on all of them. These three leave
## anything on a share alone; without them a folder of such links took minutes
## to list or to switch view.
## Test ID: rhtg2yeb
fCheckShareGates(){
	local want src fn body

	for want in nemo-directory-async.c:lacks_filesystem_info nemo-directory-async.c:lacks_mount nemo-file.c:file_is_cheap_to_read; do
		src="source/libnemo-private/${want%%:*}"
		fn="${want#*:}"
		[[ -f "$src" ]] || continue
		body="$(awk -v fn="$fn" '$0 ~ "^" fn " \\(" { on=1 } on { print } on && /^}/ { exit }' "$src")"
		if ! grep -q -F 'nemo_file_is_on_a_share (file)' <<< "$body"; then
			fEcho "FAIL: ${src}: ${fn} must leave files on a share alone (nemo_file_is_on_a_share)"
			exit 2
		fi
	done
}
fRun fCheckShareGates

## Setting the list's bottom margin asks for another allocation. The size
## handler set it on every one, which redrew the view at the frame rate - the
## strobing scrollbar. Each set sits behind a check that the value moved.
## Starting a folder load resets it once, which cannot loop.
## Test ID: rhtg2yec
fCheckMarginGuard(){
	local src='source/src/nemo-list-view.c'
	local bad

	[[ -f "$src" ]] || return 0

	bad="$(awk '
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ { fn = $1; sub(/\(.*/, "", fn) }
		/gtk_widget_set_margin_bottom *\(/ && fn != "nemo_list_view_begin_loading" {
			if (prev !~ /gtk_widget_get_margin_bottom/ || prev !~ /!=/) print FNR ": " $0
		}
		/[^[:space:]]/ { prev = $0 }
	' "$src")"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: ${src}: set the bottom margin only when it changes"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckMarginGuard

## On Windows every program nemo starts goes through nemo-launch-win32.c.
## Anything started directly inherits the single-exe packer's hooks: programs
## built on Chromium reported a crash on a cold start, and 32-bit ones never
## ran. The other calls into the shell are on the list with their reasons.
## Test ID: rhtg2yed
fCheckWinLaunch(){
	local allowed=' '
	allowed+='nemo-view-win32.c:nemo_view_win32_open_elevated '		# runas on our own exe, the only way to ask for elevation
	allowed+='nemo-view-win32.c:nemo_view_win32_open_in_explorer '	# explore verb, for the item that says Explorer on it
	allowed+='nemo-view-win32.c:explorer_select_by_command_line '	# names explorer.exe, but starts it through the broker
	allowed+='test-nemo-raise-win32.c:start_copy '					# a test starting copies of itself detached from its console
	local bad

	bad="$(find source \( -name '*.c' -o -name '*.h' \) -exec awk -v allowed="$allowed" '
		FNR == 1 { fn = ""; base = FILENAME; sub(/.*\//, "", base) }
		base == "nemo-launch-win32.c" { next }
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ { fn = $1; sub(/\(.*/, "", fn) }
		/(^|[^A-Za-z0-9_])(CreateProcess[AW]?|ShellExecute(Ex)?[AW]?) *\(|"explorer\.exe"/ {
			if (index(allowed, " " base ":" fn " ") == 0) print FILENAME ":" FNR ": " $0
		}
	' {} +)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a program started outside nemo-launch-win32.c, and not on the list in lint-c.bash"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckWinLaunch

## GLib's own spawn on Windows goes through a helper program, which gives a
## console tool a console window and never starts it at all from the single
## exe. Off Windows it is fine, so every call that starts a program is on this
## list with why Windows never reaches it, or why it does no harm there.
## Test ID: rjm8a6xr
fCheckGlibSpawn(){
	local allowed=' '
	allowed+='nemo-tool-run.c:nemo_tool_run_start '						# Windows goes through nemo-launch-win32.c
	allowed+='nemo-magick.c:run_magick '								# same
	allowed+='nemo-desktop-thumbnail.c:run_thumbnailer_script '			# same
	allowed+='nemo-new-process.c:spawn_argv '							# our own exe, which has no console window to show
	allowed+='nemo-view.c:open_as_root '								# not built on Windows
	allowed+='nemo-view.c:open_in_terminal '							# same
	allowed+='nemo-action-config-widget.c:on_layout_editor_clicked '	# same
	allowed+='nemo-extension-config-widget.c:on_restart_clicked '		# same
	allowed+='nemo-extension-config-widget.c:detect_extensions '		# same
	allowed+='nemo-extension-config-widget.c:on_config_clicked '		# no extensions on Windows, so no link to click
	allowed+='nemo-file-utilities.c:update_xdg_user_dir '				# no such program on Windows
	allowed+='nemo-thumbnail-problem-bar.c:thumbnail_problem_bar_response_cb '	# same, sh and pkexec
	allowed+='nemo-action.c:nemo_action_activate '						# Windows goes through nemo-launch-win32.c
	allowed+='nemo-action.c:check_exec_condition '						# same
	local bad

	bad="$(find source/src source/libnemo-private source/libnemo-extension source/eel \( -name '*.c' -o -name '*.h' \) -exec awk -v allowed="$allowed" '
		FNR == 1 { fn = ""; base = FILENAME; sub(/.*\//, "", base) }
		/^[a-zA-Z_][a-zA-Z0-9_]* *\(/ { fn = $1; sub(/\(.*/, "", fn) }
		/(^|[^A-Za-z0-9_])(g_subprocess_newv?|g_subprocess_launcher_spawnv?|g_spawn_(async|sync|command_line_async|command_line_sync|async_with_pipes|async_with_fds|async_with_pipes_and_fds)) *\(/ {
			if (index(allowed, " " base ":" fn " ") == 0) print FILENAME ":" FNR ": " $0
		}
	' {} +)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a program started through GLib, which on Windows needs nemo-tool-run.c or nemo-launch-win32.c, and not on the list in lint-c.bash"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckGlibSpawn

## A right-click on a path button pops its menu inside the press. It used to
## wait for the folder's attributes and pop up from their callback, which came
## after the release with a stale event, so the menu opened and shut at once.
## Test ID: rhtg2yee
fCheckLocationPopup(){
	local src='source/src/nemo-view.c'
	local last body

	[[ -f "$src" ]] || return 0

	last="$(awk '/^schedule_pop_up_location_context_menu \(/ { on=1 } on && /^}/ { print prev; exit } on && /[^[:space:]]/ { prev=$0 }' "$src")"
	if ! grep -q -F 'real_pop_up_location_context_menu (view);' <<< "$last"; then
		fEcho "FAIL: ${src}: schedule_pop_up_location_context_menu must end by popping the menu up"
		exit 2
	fi

	body="$(awk '/^location_popup_file_attributes_ready \(/,/^}/' "$src")"
	if grep -q -E 'pop_up[a-z_]* *\(' <<< "$body"; then
		fEcho "FAIL: ${src}: location_popup_file_attributes_ready must not pop a menu up"
		exit 2
	fi
}
fRun fCheckLocationPopup

## On Windows a trash goes to the Recycle Bin through nemo_trash_win32_recycle
## with the shell's confirmations off. g_file_trash leaves them on, so every
## file was asked about twice, the second time from behind the progress window.
## Test ID: rhtg2yef
fCheckWinTrash(){
	local src='source/libnemo-private/nemo-file-operations.c'
	local bad

	[[ -f "$src" ]] || return 0

	bad="$(awk '
		/^#[[:space:]]*ifdef[[:space:]]+G_OS_WIN32/ { win = 1; recycled = 0; other = 0; next }
		win && /^#[[:space:]]*else/ { other = 1; next }
		win && /^#[[:space:]]*endif/ { win = 0; other = 0; next }
		win && !other && /nemo_trash_win32_recycle *\(/ { recycled = 1 }
		/(^|[^A-Za-z0-9_])g_file_trash(_async)? *\(/ && !(win && other && recycled) { print FNR ": " $0 }
	' "$src")"
	bad+="$(grep -rnE '(^|[^A-Za-z0-9_])g_file_trash(_async)?[[:space:]]*\(' source/src source/libnemo-private source/eel --include='*.c' \
		| grep -v "^${src}:" || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: g_file_trash outside the non-Windows branch after nemo_trash_win32_recycle"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckWinTrash

## An open view follows its default zoom and the default view. Nothing
## watched them, so a changed default reached only folders opened after it.
## Test ID: rhtg2yeg
fCheckDefaultsFollowed(){
	local want

	for want in \
		'source/src/nemo-list-view.c:"changed::" NEMO_PREFERENCES_LIST_VIEW_DEFAULT_ICON_SIZE' \
		'source/src/nemo-icon-view.c:"changed::" NEMO_PREFERENCES_ICON_VIEW_DEFAULT_ICON_SIZE' \
		'source/src/nemo-icon-view.c:"changed::" NEMO_PREFERENCES_ICON_VIEW_DEFAULT_IMAGE_ICON_SIZE' \
		'source/src/nemo-icon-view.c:"changed::" NEMO_PREFERENCES_COMPACT_VIEW_DEFAULT_ICON_SIZE' \
		'source/src/nemo-window.c:"changed::" NEMO_PREFERENCES_DEFAULT_FOLDER_VIEWER'; do
		[[ -f "${want%%:*}" ]] || continue
		if ! grep -q -F "${want#*:}" "${want%%:*}"; then
			fEcho "FAIL: ${want%%:*}: must watch ${want#*:} so an open view follows it"
			exit 2
		fi
	done
}
fRun fCheckDefaultsFollowed

## Window size and place were written only on a clean close, so a crash, or
## the launcher replacing a running copy, lost them. A move or resize saves
## them once it settles.
## Test ID: rhtg2yeh
fCheckGeometrySave(){
	local src='source/src/nemo-window.c'
	local body

	[[ -f "$src" ]] || return 0

	body="$(awk '/^nemo_window_configure_event \(/,/^}/' "$src")"
	if ! grep -q -F 'wclass->configure_event = nemo_window_configure_event;' "$src" ||
	   ! grep -q -F 'save_geometry_cb' <<< "$body"; then
		fEcho "FAIL: ${src}: a move or resize must schedule the geometry save"
		exit 2
	fi
}
fRun fCheckGeometrySave

## "Places" never keeps the keyboard: its tree refuses the focus, and so does the
## scrolled window around it, which would take it from Tab or F6. Opening a
## place or ending a rename hands it to the folder. Only the tree pane keeps the
## focus it was given, so connecting a content view must not grab from it, nor
## from a rename in "Places" while it lasts.
## Test ID: rhtg2yej
fCheckSidebarFocus(){
	local src='source/src/nemo-window.c' places='source/src/nemo-places-sidebar.c'
	local body bad fn

	[[ -f "$src" && -f "$places" ]] || return 0

	body="$(awk '/^nemo_window_connect_content_view \(/,/^}/' "$src")"
	bad="$(awk '
		/eel_gtk_focus_is_within \(window->details->places_sidebar\)/ { p = NR }
		/eel_gtk_focus_is_within \(window->details->tree_sidebar\)/ { t = NR }
		/(nemo_view|gtk_widget)_grab_focus *\(/ { if (!p || !t || NR - p > 4 || NR - t > 4) print }
	' <<< "$body")"
	if [[ -z "$body" || -n "$bad" ]]; then
		fEcho "FAIL: ${src}: nemo_window_connect_content_view grabs the focus only while neither side pane holds it"
		[[ -n "$bad" ]] && printf '%s\n' "$bad"
		exit 2
	fi

	body="$(awk '/^nemo_places_sidebar_init \(/,/^}/' "$places")"
	if ! grep -q -F 'gtk_widget_set_can_focus (GTK_WIDGET (tree_view), FALSE);' <<< "$body" ||
	   ! grep -q -F 'gtk_widget_set_can_focus (GTK_WIDGET (sidebar), FALSE);' <<< "$body"; then
		fEcho "FAIL: ${places}: the places tree and the scrolled window around it must refuse the keyboard focus"
		exit 2
	fi
	for fn in open_selected_bookmark bookmarks_edited bookmarks_editing_canceled; do
		body="$(fn="$fn" awk '$0 ~ "^" ENVIRON["fn"] " \\(", /^}/' "$places")"
		if ! grep -q -F 'focus_folder_view (' <<< "$body"; then
			fEcho "FAIL: ${places}: ${fn} must hand the focus to the folder"
			exit 2
		fi
	done
	if grep -q 'widget_class->focus *=' "$places"; then
		fEcho "FAIL: ${places}: a focus handler on the sidebar moves its cursor on every Tab past it"
		exit 2
	fi
}
fRun fCheckSidebarFocus

## A Windows-only test is left out of the build elsewhere and carries no stub
## for the other platform. Some were built and reported a skip and some had
## stubs never compiled, so a Linux run could not say what it had covered.
## Test ID: rhtg2yek
fCheckWinTests(){
	local build='source/test/meson.build'
	local bad

	[[ -f "$build" ]] || return 0

	bad="$(awk '
		function side(s){ return s ~ /not[[:space:]]+is_windows/ ? -1 : (s ~ /is_windows/ ? 1 : 0) }
		/^[[:space:]]*if[[:space:]]/ { w[++n] = side($0); next }
		/^[[:space:]]*elif[[:space:]]/ { w[n] = side($0); next }
		/^[[:space:]]*else/ { w[n] = -w[n]; next }
		/^[[:space:]]*endif/ { n--; next }
		/test-[a-z0-9-]*win32[a-z0-9-]*\.c/ {
			inwin = 0
			for (i = 1; i <= n; i++) if (w[i] == 1) inwin = 1
			if (!inwin) print FNR ": " $0
		}
	' "$build")"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: ${build}: a Windows-only test outside an if is_windows block"
		printf '%s\n' "$bad"
		exit 2
	fi

	bad="$(grep -nE '^#[[:space:]]*(if|ifdef|ifndef|elif).*G_OS_WIN32' source/test/test-*win32*.c || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a Windows-only test carries a stub for the other platform"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckWinTests

## Taking the focus off the path entry puts the buttons back, not only Escape.
## Switching to another program does not, so a half-typed path survives it.
## Test ID: rhtg2yem
fCheckEntryFocusOut(){
	local src='source/src/nemo-window-pane.c'
	local body

	[[ -f "$src" ]] || return 0

	if ! grep -q -E 'nemo_location_bar_get_entry \([^;]*"focus-out-event",[[:space:]]*G_CALLBACK \(toolbar_focus_out_callback\)' <<<"$(tr -d '\n' < "$src")"; then
		fEcho "FAIL: ${src}: the path entry's focus-out-event must go to toolbar_focus_out_callback"
		exit 2
	fi

	body="$(awk '/^toolbar_focus_out_callback \(/,/^}/' "$src")"
	if ! grep -q -F 'g_signal_emit_by_name (pane->location_bar, "cancel")' <<< "$body"; then
		fEcho "FAIL: ${src}: toolbar_focus_out_callback must put the buttons back"
		exit 2
	fi
	if ! grep -q -F 'gtk_window_has_toplevel_focus' <<< "$body"; then
		fEcho "FAIL: ${src}: toolbar_focus_out_callback must keep the entry when the whole window loses the focus"
		exit 2
	fi
}
fRun fCheckEntryFocusOut

## Same idea as fCheckStyleRemeasure, for the other two things that make every
## remembered width wrong: a zoom changes the font and the icon, and a column
## coming or going changes what the rest have to fit into.
## Test ID: rhtg2yen
fCheckZoomRemeasure(){
	local src='source/src/nemo-list-view.c'
	local fn body

	[[ -f "$src" ]] || return 0

	for fn in nemo_list_view_set_icon_size apply_columns_settings; do
		body="$(awk -v fn="$fn" '$0 ~ "^" fn " \\(" { on=1 } on { print } on && /^}/ { exit }' "$src")"
		if ! grep -q -F 'remeasure_rows' <<< "$body"; then
			fEcho "FAIL: ${src}: ${fn} must send the rows back to be measured"
			exit 2
		fi
	done
}
fRun fCheckZoomRemeasure

## Nothing runs off inserted media, on any platform, and there is no option to
## turn it on. The media bar offered to run the software types until the
## autorun helper was removed, so those three stay refused there. The
## translation catalogs keep upstream's strings until they are regenerated.
## Test ID: rhtg2yep
fCheckNoAutorun(){
	local src='source/src/nemo-window-manage-views.c'
	local body type bad

	if [[ -f "$src" ]]; then
		body="$(awk '/^nemo_window_slot_show_x_content_bar \(/,/^}/' "$src")"
		for type in software unix-software win32-software; do
			if ! grep -q -F "\"x-content/${type}\"" <<< "$body"; then
				fEcho "FAIL: ${src}: nemo_window_slot_show_x_content_bar must refuse x-content/${type}"
				exit 2
			fi
		done
	fi

	bad="$(grep -rli 'autorun' source --exclude='*.po' --exclude='*.pot' || true)"
	bad+="$(find source -iname '*autorun*' ! -name '*.po' ! -name '*.pot')"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: autorun is back under source/"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckNoAutorun

## View and layout state on a file is ours alone, so its keys carry the app
## name; upstream Nemo reads the same files, and the two builds fought over the
## view. Keys other file managers read too stay bare, or they stop sharing.
## Test ID: rhtg2yeq
fCheckMetadataSlug(){
	local src='source/libnemo-private/nemo-metadata.h'
	local bad

	[[ -f "$src" ]] || return 0

	bad="$(awk '
		/^#define NEMO_METADATA_KEY_/ {
			k = $2
			if ((k ~ /_(ICON|LIST|COMPACT)_VIEW_/ || k == "NEMO_METADATA_KEY_DEFAULT_VIEW") && $3 != "NEMO_APP_SLUG")
				print FNR ": " k " is ours and needs NEMO_APP_SLUG"
			if (k ~ /_(LOCATION_BACKGROUND_[A-Z]+|ANNOTATION|CUSTOM_ICON|CUSTOM_ICON_NAME|EMBLEMS|ICON_SCALE)$/ && $0 ~ /NEMO_APP_SLUG/)
				print FNR ": " k " is shared and must stay bare"
		}
	' "$src")"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: ${src}: metadata key naming"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckMetadataSlug

## A domain named in a UI file beats the one the code sets, and upstream's
## "nemo" sent two windows to Nemo's catalog instead of ours.
## Test ID: rhtzhbqg
fCheckUiDomain(){
	local bad

	bad="$(grep -rn -E '<interface[^>]*domain=' source --include='*.glade' --include='*.ui' || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a UI file names its own translation domain; leave it to the code"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckUiDomain

## A click or scroll takes the primary modifier (eel_gtk_primary_mask), which
## is Cmd on macOS. Plain Control in a button or scroll handler is a click that
## missed that change; the list view's add-to-selection on Ctrl+click did.
## Keyboard handlers mix both on purpose and are not checked.
## Test ID: rj9tz3mv
fCheckClickPrimary(){
	local bad f
	local -a handlerFiles=()

	while IFS= read -r f; do
		[[ "$f" == source/vendor/* || "$f" == source/cut-n-paste-code/* ]] || handlerFiles+=("$f")
	done < <(grep -rl -E 'GdkEvent(Button|Scroll)' source --include='*.c' || true)
	if ((${#handlerFiles[@]} == 0)); then
		fEcho "FAIL: no click or scroll handlers found; the check has lost its files"
		exit 2
	fi

	## A function starts at a column-0 brace, or a signature line ending in one,
	## and ends at a column-0 closing brace.
	bad="$(awk '
		FNR == 1 { body = 0; sig = "" }
		body && /^}/ { body = 0; sig = ""; next }
		body {
			if (sig ~ /GdkEvent(Button|Scroll)/ && /GDK_CONTROL_MASK/)
				print FILENAME ":" FNR ": " $0
			next
		}
		/^\{/ { body = 1; next }
		/^[A-Za-z_].*\)[[:space:]]*\{[[:space:]]*$/ && !/=/ { sig = sig " " $0; body = 1; next }
		/^}/ || /;[[:space:]]*$/ || /^#/ { sig = ""; next }
		{ sig = sig " " $0 }
	' "${handlerFiles[@]}")"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: Control in a click or scroll handler; use eel_gtk_primary_mask"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckClickPrimary

## Shifting into the sign bit of an int is undefined, though gcc does what was
## meant today. The metadata list mask was 1<<31; a top bit is 1u << 31.
## Test ID: rj9v86cj
fCheckSignShift(){
	local bad

	bad="$(grep -rn -E '(^|[^0-9A-Za-z_.])1[[:space:]]*<<[[:space:]]*31([^0-9]|$)|\(int\)[^;]*<<[[:space:]]*24([^0-9]|$)' \
		source --include='*.c' --include='*.h' \
		| grep -v -E '^source/(vendor|cut-n-paste-code)/' || true)"
	if [[ -n "$bad" ]]; then
		fEcho "FAIL: a shift into the sign bit of an int; shift an unsigned value"
		printf '%s\n' "$bad"
		exit 2
	fi
}
fRun fCheckSignShift

## On Windows GLib's mount list asks the shell for every drive letter's name as
## it is made, so a letter mapped to a share that is not answering held the side
## pane for the network timeout. Every list goes through nemo_get_mounts, and no
## bin is asked for with a NULL root, which takes in every drive, mapped too.
## Test ID: rjhw99yh
fCheckMountList(){
	local bad bins

	bad="$(grep -rn -E '(^|[^A-Za-z0-9_])g_volume_monitor_get_mounts[[:space:]]*\(' \
		source/src source/libnemo-private source/eel --include='*.c' \
		| grep -v -E '^source/libnemo-private/nemo-file-utilities\.c:[0-9]+:[[:space:]]*return g_volume_monitor_get_mounts \(monitor\);' || true)"
	bins="$(grep -rn -E 'SHQueryRecycleBin[AW]?[[:space:]]*\([[:space:]]*NULL' \
		source/src source/libnemo-private --include='*.c' || true)"
	if [[ -n "$bad" || -n "$bins" ]]; then
		fEcho "FAIL: a mount list or bin query that asks every drive; use nemo_get_mounts, one root per bin"
		[[ -z "$bad" ]] || printf '%s\n' "$bad"
		[[ -z "$bins" ]] || printf '%s\n' "$bins"
		exit 2
	fi
}
fRun fCheckMountList

## Under MSYS2, use the Windows git that made this checkout - the msys one has
## its own HOME/config, so its line-ending view marks every CRLF file modified.
GIT=(git)
if [[ "$(uname -o 2>/dev/null)" == "Msys" ]]; then
	for cand in "/c/Program Files/Git/cmd/git.exe" "/c/Program Files (x86)/Git/cmd/git.exe"; do
		[[ -x "$cand" ]] && { GIT=("$cand"); break; }
	done
fi
## Read-only use; keep eol-normalization advice out of the gate output.
GIT+=(-c core.safecrlf=false)

## UI case first, and unconditionally: the checks below bail early when nothing
## C changed, and a label is just as wrong on a .glade-only change.
PY=""
for cand in python3 python; do
	command -v "$cand" >/dev/null 2>&1 && { PY="$cand"; break; }
done
if ((listOnly)); then
	:
elif [[ -n "$PY" ]]; then
	"$PY" cicd/utility/lint-ui-case.py source
	## Same reasoning: the demo recorder's settings keys and columns go stale
	## silently, and nothing C has to change for that to happen.
	"$PY" cicd/utility/lint-demo-script.py .
	## Whole-tree too: a disconnect aimed at the wrong preference group removes
	## nothing and says nothing, and the handler then runs on a freed object.
	## So does one left for finalize while a view is still held, which is why
	## a plain connect for an object is reported too. A handler listening on a
	## group that does not have its key is never called, and says nothing.
	"$PY" cicd/utility/lint-pref-handlers.py --self-test
	"$PY" cicd/utility/lint-pref-handlers.py source
	## A key claimed by two actions does whichever GTK merged first, and says
	## nothing about it.
	"$PY" cicd/utility/lint-accels.py source
	## Who frees a returned pointer is written above the function, since C
	## can't say it. Whole-tree, so a Windows-only file is read on Linux too.
	"$PY" cicd/utility/lint-ownership.py --self-test
	"$PY" cicd/utility/lint-ownership.py source
else
	fEcho "WARNING: UI case SKIPPED: no python" >&2
fi

if ((! listOnly)) && ! command -v cppcheck >/dev/null 2>&1; then
	if [[ "$strict" == "1" ]]; then
		fEcho "FAILED: C lint: cppcheck not installed" >&2
		exit 1
	fi
	fEcho "WARNING: C lint SKIPPED: cppcheck not installed" >&2
	exit 0
fi

## Integration branch: explicit arg wins, else dev if it exists, else main.
if [[ -z "$base" ]]; then
	if "${GIT[@]}" show-ref --verify --quiet refs/heads/dev; then base="dev"; else base="main"; fi
fi

## Collect candidates. dev and main only take --no-ff merges, so base...HEAD is
## always empty there, even on main right after the release merge the pre-push
## gate is checking. They get the whole tree instead. A feature branch gets its
## commits since the merge base, then everything not yet committed. Untracked
## files count on both. Deletions can't be linted; the -f test below drops them.
candidates=""
range=""
wholeTree=0
head_branch="$("${GIT[@]}" rev-parse --abbrev-ref HEAD)"
if [[ "$head_branch" == "$base" || "$head_branch" == "dev" || "$head_branch" == "main" ]]; then
	wholeTree=1
	candidates+="$("${GIT[@]}" ls-files)"$'\n'
else
	if "${GIT[@]}" rev-parse --verify --quiet "$base" >/dev/null; then
		range="${base}...HEAD"
		candidates+="$("${GIT[@]}" diff --name-only --diff-filter=d "$range")"$'\n'
	fi
	candidates+="$("${GIT[@]}" diff --name-only --diff-filter=d HEAD)"$'\n'
	candidates+="$("${GIT[@]}" diff --cached --name-only --diff-filter=d)"$'\n'
fi
candidates+="$("${GIT[@]}" ls-files --others --exclude-standard)"

## Keep C sources that still exist, dedup. Vendored code is upstream's to
## fix, so bumping it must not light up our gate.
files=()
while IFS= read -r f; do
	[[ "$f" == *.c || "$f" == *.h ]] || continue
	[[ "$f" == vendor/* || "$f" == source/cut-n-paste-code/* ]] && continue
	[[ -f "$f" ]] && files+=("$f")
done < <(printf '%s\n' "$candidates" | LC_ALL=C sort -u)

if ((listOnly)); then
	if ((${#files[@]})); then printf '%s\n' "${files[@]}"; fi
	exit 0
fi

if ((wholeTree)); then
	scope="in the tree on ${head_branch}"
else
	since="${range:-HEAD}"
	scope="changed since ${since%%...*}"
fi
if ((${#files[@]} == 0)); then
	fEcho "OK: C lint: no C files ${scope}"
	exit 0
fi

fEcho "C lint (cppcheck, check-only) over ${#files[@]} file(s) ${scope}..."
## The suppression list is in .cppcheck-suppressions at the repo root, with the
## reason for each one written beside it. --inline-suppr stays for the handful
## that belong to a single line rather than a whole file.
## -j on its own drops the cross-file checks (the ctu* ones); a build dir keeps
## them, with the same findings as one process. Half the cores, since something
## else is usually running. The dir is relative so MSYS2 hands it over as is.
jobs=$(( $(nproc 2>/dev/null || echo 2) / 2 ))
((jobs >= 1)) || jobs=1
mkdir -p cicd/artifacts
cacheDir="$(mktemp -d cicd/artifacts/cppcheck.XXXXXX)"
cppcheck --enable=warning,portability --library=gtk --inline-suppr \
	--suppressions-list=.cppcheck-suppressions -j "$jobs" --cppcheck-build-dir="$cacheDir" \
	--quiet --error-exitcode=2 "${files[@]}"
fEcho "OK: C lint: no findings"
