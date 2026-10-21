<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere affirmative checkmark selection

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Terms](#terms)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [What it looks like](#what-it-looks-like)
	- [Mouse](#mouse)
	- [Keyboard](#keyboard)
	- [The space key](#the-space-key)
	- [Tree view](#tree-view)
	- [Menus](#menus)
	- [Status bar and dialogs](#status-bar-and-dialogs)
	- [What a command acts on](#what-a-command-acts-on)
	- [The setting](#the-setting)
	- [Where this breaks habits](#where-this-breaks-habits)
	- [Open questions](#open-questions)
- [Alternative ideas](#alternative-ideas)
	- [Unconsidered](#unconsidered)
	- [Rejected](#rejected)
	- [Superseded](#superseded)
- [Research findings](#research-findings)
- [Roadmap](#roadmap)
- [Related backlog issues](#related-backlog-issues)
- [Copyright and license](#copyright-and-license)

<!-- /TOC -->

## Summary

Problem: The content view shows where you are, or what is selected, 3 ways at once:

1. The highlighted selection,

2. The focus outline, and

3. The mouseover tint

Multi-select from the keyboard fights with all of them. A stray click or arrow key throws a selection away, that the user may have spent time carefully planning.

- This doc defines the "Affirmative checkmark" selection model. Selecting works as it does today. On top of it, a deliberate pick gets a lasting checkmark that a plain click or arrow key can't undo, and commands act on the checkmarked objects. When nothing is deliberately picked, the selection stands in, drawn with dim checkmarks, so a command always has something to act on and the view always shows what that is. The updated metaphor:

	- The "focus outline" goes away, except for the few Ctrl+arrow moves that take the focus off the selection.

	- The selection highlight does most of the outline's old job. (And what it doesn't do, isn't needed anymore.)

	- A partially opaque checkmark sits on every selected object, and is drawn only while no object in the view is affirmatively checkmarked. It means "act on these" (e.g. from the Edit menu or right-click).

	- Fully visible checkmarks define what is affirmatively checked, regardless of what is or isn't selected.

- It's one of 6 models behind the "File and folder selection model" setting, and the default. The other 5 are "Nemo Classic", "Nemo Anywhere v1", "Windows Explorer", "macOS Finder", and "Commander". Nemo Anywhere v1 and Nemo Classic are carve-outs of code that exists or existed, and both come before this one. (Our focus and selection code is already too modified to pass for "Classic", so that one is a re-implementation from upstream source.) The other 3 are named here only.

- The name isn't final. "Affirmative checkmark" says what it is, and a friendlier name may replace it.

- Terms are defined first, since (for example) my notes this grew from used "selected" for 3 distinct different meanings, and needed to be carefully and painstakingly clarified.

## Terms

- Content view: the part of the window that lists what's in the folder, upstream's name for it. It comes in 3 (upstream) flavors: Icon view, List view, and Compact view. "Content view" means whichever view is currently showing. A split window has 2 Content views, and every tab has its own.

- Tree view: the side pane that lists folders only. (Not to be confused with folder trees in the list view name column.) Its folders are objects too, and the one open in the content view is the viewed folder.

- Object: anything listed in the content view or the tree view. A file, a folder, any kind of link or a shortcut, etc. In list view with the tree column on, an expanded folder's children are objects too.

- Focus: the current object. There's one or none. Plain arrow keys, Home, End, Page Up, Page Down, type-ahead, and a plain mouse click move it, and after any of those it's one of the selected objects, so the selection highlight shows where it is and nothing else is drawn. Ctrl+Up and Ctrl+Down (and Ctrl+Left and Ctrl+Right in icon views) move the focus on its own. Only then does it get Nemo Classic's outline, which goes away as soon as the focus is back on a selected object. Escape clears it.

- Anchor: the object a Shift range is counted from. A plain click, a Ctrl+click, Ctrl+Space, or an unmodified key move sets the anchor to the focused object. Shift+click, Ctrl+Shift+click, and Shift with an arrow key keep the anchor and move only the focus, so the range can be resized. It is invisible in this model.

- Range ends: the first and the last object of the selection, in view order. Both are null when nothing is selected (never only one of them), and both are the same object for a single selection. An unmodified Up or Down counts from them, as under [Keyboard](#keyboard). They're invisible, and they're worked out from the selection rather than stored.

- Selection: the highlighted objects. One object, a run of them, or scattered ones. It's made the way it is today, by click, drag, and keyboard, and the app makes one on its own after a paste, an undo, a new folder, or for the object named by `--select`. Those are an app-made selection. Selecting never affirmatively checkmarks anything.

- Checkmarked: a state an object is in, in one of 2 ways. Selected and checkmarked are 2 different things that work independently. Commands act on checkmarks.
	- Affirmatively checkmarked: the user did something that leaves no doubt they meant to pick the object. Only a few things count: the space key, Ctrl+A, Ctrl+I, the checkmark menu entries, and a drag from the checkmark column. The rules under [Design](#design) say exactly how.
	- Temporarily checkmarked: the stand-in. Every selected object is temporarily checkmarked, so if anything is selected, something is checkmarked. The marks are drawn only while nothing in the view is affirmatively checkmarked, and they follow the selection.

- Command: anything that acts on objects. Cut, copy, trash, delete, open, rename, compress, properties, a drag, and the right-click menu.

- View change, either of:
	- The path in the path bar changes. Going into a folder, back, up, or to a bookmark. A reload isn't one.
	- A folder is collapsed in list view's tree by the user, and at least one affirmatively checkmarked object goes out of sight with it.
	- The same in the tree view: a folder collapsed by the user over an affirmatively checkmarked child is a view change for the tree.
	- A view change clears every affirmative checkmark in that tab, not only the ones that went out of sight. Switching between icon, list, and compact, sorting, a reload, the hidden-files toggle, and a change on disk aren't view changes. An object that goes away or gets hidden takes its checkmark with it.

## Specification

- Selecting works as it does today, and never changes an affirmative checkmark. Neither does a plain click on empty space or an unmodified key move.

- A command acts on the affirmative checkmarks when the view has any, and otherwise on the selection, which is temporarily checkmarked. Never on both. When the 2 lists happen to be the same, that's a coincidence.

- Affirmative checkmarks stay until a view change, Escape, the user unchecks them, or a command removes the objects or finishes with them. Cut and copy keep them until the paste succeeds.

- Checkmarks belong to one content view. One tab's, or one pane's, never affect another's, and actions on checkmarked files only affect those in the active content view.

- The tree view takes checkmarks under the same rules. The user sets affirmative checkmarks in the tree view or in the content view, never in both at once. A change to one side's checkmarks removes every affirmative checkmark on the other side.

- Temporary checkmarks are drawn only while the view has no affirmative ones.

- The status bar and every file-operation dialog say which of the 2 lists the command acts on, and whether the other one differs.

- The model is a setting on the Advanced tab, and the default is "Affirmative checkmark".

## Goals

- A multi-object pick from the keyboard that a stray key or click can't throw away.

- A clear answer, at every moment, to "what will this command act on", and a view that shows it.

- One visual language in all 3 views.

- Indirectly supports the other design of "A drag on a row or selection in list view always performs the drag action, not something ambiguous like a re-select".

### Non-goals

- The Explorer, Finder, and Commander models in detail. Each gets a doc of its own before it's built.

- Changing what any command does once it has its objects.

- Places. What it marks follows the open folder, as now.

## Design

### What it looks like

- Where the mark goes:
	- List view: a thin new column left of Name, with nothing else in it. It can't be hidden, or moved. (But can be sorted on. When the last checkmark goes, the sort reverts to what it was previously.)
	- Icon view: the top-left corner of the icon, on a small scrim that contrasts with it, so the mark reads on any thumbnail.
	- Compact view: left of the name text.

- Affirmative: a checkmark drawn by the view. It isn't a checkbox, and clicking on it does the same as clicking on the row. The object's text turns bold, in an "affirmatively checkmarked" color each theme defines, so it fits the theme's own palette and reads well on its selection background. Green is the fallback for a theme that doesn't define one, and the checkmark takes the same color.
	- These sit on top of the usual selection background, which is unchanged.

- Temporary: the same checkmark, dimmer and less saturated but still easy to see, on every selected object. Drawn only while the view has no affirmative checkmark. The text doesn't change.

- No focus outline, except while a Ctrl+arrow move has put the focus on an object that isn't selected. The content view then shows 3 things: the selection highlight, the checkmarks for what a command acts on, and the mouseover tint. A fourth sign on top of those is too many.
	- Nemo Anywhere v1 and Nemo Classic keep their outline. Windows Explorer, macOS Finder, and Commander draw what their conventions want.

### Mouse

- Plain click on an object: selects it alone, as now. The focus and anchor move there. Affirmative checkmarks don't change, and the temporary ones follow the selection.

- Plain click on empty space: clears the selection and the focus, as now. Affirmative checkmarks stay, since a plain click never changes them. Escape is the way to drop them all.

- Double-click: opens the object under the pointer. A folder opening is a view change. A file opening changes nothing.

- Ctrl+click (Cmd+click on macOS): adds the object to the selection, or removes it when it's already selected, as now. The focus and anchor move to the clicked object. Checkmarks don't change.

- Shift+click: replaces the selection with the run from the invisible anchor to the clicked object, both ends included. The focus moves, the anchor stays.

- Ctrl+Shift+click: adds that run to the selection, whether it overlaps or not. The focus moves, the anchor stays.

- Right-click-and-release on an object: as now. If the object is selected, the menu is for the selection. If not, a plain click first, then the menu. Either way the menu's commands act per [What a command acts on](#what-a-command-acts-on), so while something is affirmatively checkmarked they act on that and not on the object under the pointer. The dialogs say so.

- Right-click on empty space: the folder's own menu, as now.

- Plain left drag from an object, by what the view holds. Checkmarks don't change in any case.
	- No affirmative checkmarks, and the object isn't selected: as now, the object becomes the selection on its own, and the drag takes it.
	- No affirmative checkmarks, and the object is selected: as now, the drag takes the whole selection.
	- The object is affirmatively checkmarked: the drag takes every affirmatively checkmarked object, and ignores the selection. The status bar says that only checked items are going, for as long as the drag lasts plus 3 seconds, or 5 seconds, whichever is longer.
	- Affirmative checkmarks exist, and the object isn't one of them, selected or not: nothing is dragged. There's no safe answer to invent here, so the pointer turns to the nearest "not allowed" cursor, and the status bar says why, for as long as the drag attempt lasts plus 3 seconds, or 5 seconds, whichever is longer.

- Plain left drag from empty space: a band that replaces the selection, as now.
	- Icon and compact views: from anywhere between the icons.
	- List view: only from the blank space below the rows, or from the checkmark column (see the override below). A plain drag from a row is a file drag.

- Ctrl+drag: from empty space, or from an object that isn't selected, a band that adds what it touches to the selection. List view included, from any column. From a selected object it's a file drag, with Ctrl meaning copy as before.

- Shift+drag: the same as Ctrl+drag, except the band replaces. From a selected object it's a file drag, with Shift meaning move.

- Ctrl+Shift+drag: the same as Ctrl+drag.

- Right drag: from empty space, or from an object that isn't selected, a band that replaces. List view included. From a selected object it's a file drag, and ends with a drop menu, if possible depending on what is dragged to, and the OS. A right-click that doesn't move, opens the menu on release as usual.

- A band moves the focus and anchor to the last object it touched. Checkmarks don't change, with one override:
	- A left or right drag, with or without modifiers, that starts in list view's checkmark column does what the same drag does from anywhere else, and also affirmatively checkmarks every object the band touched. The affirmative checkmarks get the same treatment as the selection: kept when the drag adds (Ctrl, Ctrl+Shift), dropped when it replaces (plain, Shift, right). This is the only mouse gesture that sets affirmative checkmarks. Icon and compact views have no column, so there a band from the background is a plain band with no checkmarks.

### Keyboard

- Up, Down, Left, Right, Home, End, Page Up, Page Down, and type-ahead replace the selection with one object, as now, and move the focus and anchor there. Checkmarks don't change. The temporary mark follows.
	- Up and Down count from the range ends, text-editor style. Up selects the object above the first selected one, and Down the object below the last. Left and Right do the same sideways in icon views, and Page Up and Page Down count from the same ends. With a single object selected, both ends are that object, so nothing differs from today.
	- After a Ctrl+arrow move has taken the focus off the selection, they count from the focus instead, as Explorer does.

- Shift with any of those: replaces the selection with the run from the anchor to the new focus. The anchor stays. As now.

- Ctrl+Up and Ctrl+Down, and Ctrl+Left and Ctrl+Right in icon views: move the focus alone. The selection stays as it is, and the outline shows the focus until it's back on a selected object. (Explorer's rule.)

- Ctrl+Space: adds the focused object to the selection, or removes it when it's selected. The anchor moves to it. (Explorer's rule.) Checkmarks don't change.

- Ctrl+Shift+Up and Ctrl+Shift+Down, and Left and Right in icon views: add the run from the anchor to the new focus to the selection, without dropping what's already selected. (Explorer's rule.)

- Ctrl+Home and Ctrl+End: the same as the key alone. Ctrl+Page Up and Ctrl+Page Down switch tabs, and keep doing that.

- Space: see [The space key](#the-space-key).

- Ctrl+A: affirmatively checkmarks and selects every object.

- Ctrl+I: inverts the affirmative checkmarks, and the selection then matches the new checkmarks. If none are left, the selection goes too. The temporary marks count as checked, so with 2 objects selected and nothing affirmative, the result is everything but those 2. (Currently Ctrl+I opens properties. That changes to just Alt+Enter, an existing alias.)

- Ctrl+D: unchecks every object. The selection, and so the temporary marks, stay as they were. This is the one key that changes checkmarks without touching the selection. (Currently it adds a bookmark, see [Open questions](#open-questions).)

- Escape: clears every affirmative checkmark, the selection, and the focus. The range ends and the anchor are null. A second Escape does nothing, and the next arrow key starts at the first object.

- Enter, F2, Delete, Ctrl+X, Ctrl+C, and the rest act per [What a command acts on](#what-a-command-acts-on): on the affirmative checkmarks when there are any, else on the selection. F2 on several is the bulk rename.

### The space key

What it does depends on the selection and on what's already affirmatively checkmarked. Checkmarks outside the selection never change.

- One object is selected: toggle its checkmark. A temporary one becomes affirmative. An affirmative one goes back to temporary, that is, it's unchecked, and its dim mark shows again only once the view has no affirmative checkmark left.

- Several objects are selected, and at least one of them isn't affirmatively checkmarked: every selected object becomes affirmatively checkmarked. This covers the first group, where the whole selection was temporary, and a later selection outside the group, which joins it.

- Several objects are selected, and every one of them is affirmatively checkmarked: uncheck them all. The selection stays. If the view then has no affirmative checkmark left, the selection shows its temporary marks again.

- Nothing is selected: nothing happens.

- An app-made selection is a selection like any other, so the rules above apply to it.

### Tree view

- The tree view has a focus, an anchor, and checkmarks of its own, and every mouse and keyboard rule above applies to it. Its checkmark goes in an invisible column to the left of the tree. (But gets "line highlight" and selection color too.)

- When the tree view has the focus and the viewed folder is checkmarked there, affirmatively or temporarily, every object in the content view is checkmarked the same way. The tree's pick of a folder means the folder itself (in this mode with full tree edit capability defined in the backlog); and by implication its contents that may be visible in the content view.

- Affirmative checkmarks come from the user on one side only: the tree view or the content view, not both. Checkmarks made on one side show on the other, as above, but they're owned by the side they were made on.

- When all objects in the content view are affirmatively checkmarked because one or more folders are affirmatively checkmarked in the tree view, any change to the checkmarks in the content view removes every tree view checkmark. The content view's checkmarks then stand on their own.

- The other way round too. A change to the tree view's checkmarks removes every affirmative checkmark in the content view.

- Changing folders on the tree is a view change for the content view. The tree keeps previous affirmative checkmarks, since the content view's were cleared by the change and not by a user pick.

### Menus

- The right-click menu on an object gets 4 new entries:
	- Toggle checkmarks. The same as the space key.
	- Invert checkmarks. The same as Ctrl+I.
	- Add selection to checkmarks.
	- Remove selection from checkmarks.

- The Edit menu gets the same 4, plus Uncheck all (Ctrl+D).

- Edit > Select all (Ctrl+A) checkmarks and selects every object. Edit > Invert selection becomes Invert checkmarks (Ctrl+I), as under [Keyboard](#keyboard). Select items matching works on the selection, as now, and touches no checkmark.

### Status bar and dialogs

- The status bar says what a command would act on. Which line shows depends on whether the 2 lists are the same:

	~~~text
	Ready to act on X items checked [and selected] (size);  Free space: Z
	Ready to act on X items checked (size)  [Note: Y different items selected];  Free space: Z
	Ready to act on Y items selected;  Free space: Z
	~~~

	- The first line is for affirmative checkmarks with the selection the same set, or empty. "and selected" shows when it's the same set.
	- The second is for affirmative checkmarks with a selection that differs.
	- The third is for no affirmative checkmarks, where the selection is what a command acts on.

- A dialog that says what a file operation is about to do starts with an alert symbol, and a line that the checked objects it will act on differ from the selection, when they do. No alert when it acts on the selection (temporary checkmarks), or when the 2 lists are the same.

- Messages about file operations say "checked items", not "selected items", when the operation acts on affirmative checkmarks. When the 2 lists are the same, "checked items" is enough.

- The 2 drag messages under [Mouse](#mouse), that only checked items are going, and that an unchecked object can't be dragged while others are checked, show on the status bar for the drag plus 3 seconds, or 5 seconds, whichever is longer.

### What a command acts on

- The affirmative checkmarks when the view has any, else the selection. Always and only one of the 2. They can differ, and the status bar and dialogs say when they do.

- A command that removes its objects, or finishes with them, clears the affirmative checkmarks once it has started: trash, delete, a drag that was dropped, compress, and open. Properties, a canceled drag, an abandoned paste, and a dismissed menu leave them.

- Cut and copy leave them initially too. The paste that succeeds clears them in the content view they were cut or copied from, even when that's another tab or window. A paste that fails leaves them. A cut that's never pasted leaves them as if nothing had acted on them yet, and whatever interrupts the cut, Escape, a view change, or another command, takes its usual course.

- A selection the app makes on its own, after a paste, an undo, a new folder, or for `--select`, clears the affirmative checkmarks and selects the new objects, which are then temporarily checkmarked.

- A drag follows the same rule, under [Mouse](#mouse): checked objects go when there are any, and a drag from an unchecked object while others are checked goes nowhere.

- Where a paste goes when the selection is a folder is its own backlog item, and it should be written in these terms.

### The setting

- Advanced tab, labeled "File and folder selection model", a dropdown with 6 entries, in this order: Affirmative checkmark, Nemo Classic, Nemo Anywhere v1, Windows Explorer, macOS Finder, and Commander. The default is Affirmative checkmark. An entry whose model isn't built yet isn't shown.

- Nemo Anywhere v1 is exactly how the app behaves at the time of the carve-out, tree view and content view, behind conditionals on the setting. Nemo Classic is exactly upstream Nemo's behavior, re-implemented from upstream source behind the same conditionals, since our code has moved too far to pass for it. Both are built and verified by tests before any Affirmative checkmark code is written.

- Windows Explorer and macOS Finder follow that platform's conventions. Commander is the Norton and Total Commander way, where Insert marks an object and moves down, Space marks it, and the numeric pad's plus, minus, and star mark by pattern and invert. Each of the 3 gets a doc of its own before it's built.

### Where this breaks habits

Each of these goes against what every file manager, and most lists, have done for decades. They're the point of the model, but a first-time user will trip on some.

- A plain click doesn't change what a command acts on once anything is affirmatively checkmarked. Someone who checked 3 files, clicked a fourth, and pressed Delete expects the fourth to go. Here the 3 go. The colored text, the status bar, and the alert in the dialog are what tell them, so those have to be impossible to miss.

- A click on empty space doesn't clear the affirmative checkmarks. Everyone does that to deselect, and here it only drops the selection. Escape does the clearing.

- The space key unchecks a selection that's already fully checked. In Explorer, the space key selects the focused item alone, replacing the rest.

- Up and Down after a multi-selection count from the selection's ends, as Finder and text editors do. Explorer and GTK count from the focused object.

- A drag from an unchecked object goes nowhere while anything is checked. Everywhere else it would drag that object.

### Open questions

- The checkmark color in each bundled theme. Each one needs its own, and the fallback green may clash with some. The checkmark does the same job as the text color for anyone who can't tell the color apart.

- Ctrl+D is Add bookmark today. Either Uncheck all takes another key, or the bookmark one moves.

- Ctrl+Up and Ctrl+Down as a jump between folders at the same level in the tree column lost its key to the Explorer focus move. It needs another key, or goes.

## Alternative ideas

### Unconsidered

- A lock key that freezes the current selection against plain clicks, with no checkmarks.

- Explorer's "use check boxes to select items", where a box appears on hover and only a click on the box adds.

### Rejected

- A real checkbox in the column. A click on it would be one more way to pick, and a stray click in the column would toggle. The mark is drawn, and a click on it is a click on the row.

- One model with no setting. The current, upstream, Explorer, Finder, and Commander models are kept so each can be tried against this one.

### Superseded

- Selection and checkmarks as one list, where a Ctrl+click, a Shift+click, or a band checkmarked affirmatively (an earlier draft of this doc). Now selecting works as before, and only the space key, Ctrl+I, the menu entries, and a drag from the checkmark column set affirmative checkmarks.

- One temporary checkmark, on the focused object only (an earlier draft). Now every selected object has one, drawn while nothing is affirmative.

- A plain left drag below the rows in list view did nothing under this model (an earlier draft). It bands again, as the 2026-10-14 drag-to-select item says.

- A right-click on an object outside the affirmative checkmarks did nothing, not even a menu (an earlier draft). Now it selects as before, and the menu acts on the checkmarks.

- Ctrl+Up and Ctrl+Down as a jump between folders at the same level (an earlier draft). Now they're Explorer's focus move.

- The focus outline as a second sign of the cursor beside the selection (2026-09-19). Under this model it's drawn only after a Ctrl+arrow move. It stays under Nemo Anywhere v1 and Nemo Classic.

## Research findings

- Explorer: a plain click replaces, Ctrl+click toggles, Shift+click ranges from the anchor, Ctrl+Shift+click adds the range. Ctrl+arrow moves the focus alone, Ctrl+space toggles the focused item and sets the anchor there, and Ctrl+Shift+arrow adds a range without dropping the rest. The space key selects the focused item, replacing the rest. Up and Down after a range move from the focused item, the end the range was last grown from. A band starts from empty space with either button, and a right drag from an item moves with a drop menu. Its check-box option shows a box on hover, and a click on the item's name still replaces.

- Finder: Cmd+click toggles. Shift+click ranges in list view but toggles in icon view. Arrow keys replace the selection, and there's no focus apart from it. Up and Down with several items selected go to the item above the topmost, or below the bottommost. The range-ends rule here copies that.

- Text editors: with a selection, Left goes to its start and Right to its end in about every editor. Up and Down vary. macOS text views count from the selection's start and end, and most Windows and GTK editors from the caret, which sits at the end the selection was last grown from.

- GTK and the app today: Ctrl+arrow moves the cursor without selecting, Ctrl+space toggles and sets the anchor, Shift+arrow extends from the anchor, and a plain click replaces. Up and Down move from the cursor, as Explorer does. Escape clears the selection and the cursor, since the 2026-09-19 item.

- Anchor memory across operations: Explorer, GTK, and Finder all set the anchor on a plain click and a Ctrl+click, and none of them move it on a Shift+click. Explorer and GTK leave it alone on a Ctrl+arrow move and set it on Ctrl+Space. The anchor rules in this doc match all of that, so a Shift+click after any run of clicks and Ctrl+clicks ranges from where every one of those programs would. The one place this doc differs from Explorer and GTK is where an unmodified Up or Down starts, and there it follows Finder.

- The earlier notes had 2 things that didn't fit together, fixed here by the terms. Shift+click was measured from "the previous temporarily selected item", which doesn't exist once anything is affirmative, so the anchor does that job. And "selected" meant highlighted, checkmarked, or focused by turns, so the 3 got names.

## Roadmap

- Carve out "Nemo Anywhere v1": the app's selection and focus behavior as it is, tree view and content view, behind conditionals on the setting. Nothing else changes in that step.

- Carve out "Nemo Classic": upstream's behavior, from upstream source, behind the same conditionals.

- Verify both with tests, every mouse and keyboard rule per view, before any Affirmative checkmark code is written. The ones that need a screen run on the Linux rig.

- Apply these terms to the paste-target item, so that way the 2 are written the same.

- Pick the checkmark color for each bundled theme, and the rules above against Explorer and Finder on a real desktop.

- Build the Affirmative checkmark model behind the setting, with the same tests per rule and view.

- Then the Explorer, Finder, and Commander models, each with its own doc first.

- Before the next cut, per the backlog item.

## Related backlog issues

- 2026101816495208: the separate highlight and selection is causing too many problems. Open. This doc.

- 2026102016594100: carve out Nemo Anywhere v1 behind the selection model setting. Queued. First step above.

- 2026102016594200: carve out Nemo Classic behind the selection model setting. Queued. Second step above.

- 2026101816495200: a consistent design for copying or moving files when a folder is selected. Open.

- 2026101405542891: clicking anywhere on a row selects it at once, and when a drag bands. Done. Its band rules stand under this model.

- Row visualization enhancement: Escape clears the selection and the cursor. Done.

- Dimmer highlight of the mouseover line. Done.

- Cut 1.0.0-rc.1. Open.

## Copyright and license

> Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)<br>
> Licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) license. No warranty.
