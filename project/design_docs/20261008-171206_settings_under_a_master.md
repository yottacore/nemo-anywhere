<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->
<!-- markdownlint-disable MD055 -- Table pipe style -->

<!-- TOC ignore:true -->
# nemo-anywhere settings under a master

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [Words used here](#words-used-here)
	- [What is stored](#what-is-stored)
	- [The table of masters](#the-table-of-masters)
	- [Reading a setting](#reading-a-setting)
	- [Changing a setting](#changing-a-setting)
	- [Why a hand change clears the flags](#why-a-hand-change-clears-the-flags)
	- [The settings file](#the-settings-file)
	- [The settings dialog](#the-settings-dialog)
	- [Tests](#tests)
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

Some settings only count when another setting lets them. An "Automatic" switch can decide a group of values by itself, and while it's on, the values under it are ignored. Most programs disable those. To change one, a person first has to find which switch is in the way and turn it off. Then every value under that switch comes back at once, when maybe only one was wanted.

Here a setting that doesn't count right now is grayed out, but it still takes input. Changing it un-grays that one setting, and turns its master off by itself. The rest of the group stays gray and keeps following the master.

Every group like this works the same way, through one table and one function.

## Specification

- A master is any setting that, in some of its values, makes a group of other settings moot. It can be:
	- A switch that is on for automatic, such as "Automatic layout".
	- A switch that is on for manual, such as "Disable automatic layout". It works the same, inverted.
	- A dropdown with presets and a "Custom" entry. A preset is automatic, and Custom is manual.

- A setting under a master that is moot right now:
	- Is grayed out, but not disabled. It can still be clicked, focused and changed.
	- Shows and uses its automatic value, not its own stored value.

- Changing a grayed setting:
	- Un-grays it at once, with the new value.
	- Turns the master to manual by itself: the switch goes off, or the dropdown goes to Custom.
	- Leaves every other setting in the group as it was. The ones still gray stay gray, and keep using their automatic values.

- Changing the master by hand moves the whole group:
	- To automatic: every setting in the group goes gray and uses its automatic value.
	- To manual: every setting in the group un-grays and uses its own stored value.
	- To another preset: every setting in the group goes gray and uses that preset's values.

- So after one change has turned the master off by itself, the rest of the group can all be un-grayed at once by turning the master on, then off again. It's a little clumsy, but rare, and the common case gets faster.

- A grayed setting stays gray after a restart.

## Goals

- Change one setting in one step, without hunting for what is blocking it.

- Never bring back a pile of old stored values the person didn't ask for.

- One way for every group, so a new master and its settings need only a row in a table.

- The settings file and the dialog always agree, after a restart and after a hand edit to the file.

### Non-goals

- Masters inside masters. A master is never itself under another master.

- A setting under 2 masters. Each setting has at most one.

- Values locked from outside the settings, such as by the build or an environment variable. Those are disabled for real, since nothing the person does in the dialog can change them.

## Design

### Words used here

| Word            | Meaning
| :---            | :---
| Master          | The setting that can make a group moot.
| Child           | A setting in the group under a master.
| Automatic value | What a child acts as having while it's moot. It can come from a preset, from a rule worked out at run time, or be a plain "off".
| Own value       | The value a person gave the child. It is kept even while the child is gray, so it can come back.
| Mode            | Which children use their own value right now. One of `overridesAll`, `overridesNone` or `overridesSome`.
| Override flag   | A yes or no on each child. It only counts while the mode is `overridesSome`.

### What is stored

- For each master:
	- Its own value, where it has one worth storing. For a dropdown, that is the preset in use. For a plain switch, the mode already says on or off, so the switch is shown from the mode. Storing both would only give them a way to disagree.
	- The mode, in a second key beside the master's, named after it with `-overrides` on the end.

- For each child:
	- Its own value, always.
	- Its override flag.

### The table of masters

One table lists every master. Each row has:

- The master's key.

- The keys of its children.

- Which master values are automatic and which are manual, so an inverted switch needs no special code.

- How to get each child's automatic value. For a preset dropdown, the preset's value. Otherwise a function, since some automatic values are worked out at run time.

The table sits with the types and defaults of every other setting, so there is one place to look.

### Reading a setting

One function answers 2 questions for a child: which value it uses, and whether it is grayed.

~~~text
uses_own_value(child):
	mode = mode of child's master
	if mode is overridesAll:  yes
	if mode is overridesSome: child's override flag
	otherwise:                no

value(child)   = own value if uses_own_value(child), else automatic value
grayed(child)  = not uses_own_value(child)
~~~

A setting with no master always uses its own value and is never gray.

Nothing reads a child's own value directly, outside this function. A check in the lint stage can refuse code that does.

### Changing a setting

| What happens                             | Mode after      | Override flags   | Master shows
| :---                                     | :---            | :---             | :---
| A grayed child is changed, mode was None | `overridesSome` | that child's set | manual
| A grayed child is changed, mode was Some | `overridesSome` | that child's set | manual
| A child that isn't gray is changed       | same as before  | no change        | same as before
| The master is set to automatic by hand   | `overridesNone` | all cleared      | automatic
| The master is set to manual by hand      | `overridesAll`  | all cleared      | manual
| Another preset is picked by hand         | `overridesNone` | all cleared      | the new preset

- Changing a child always stores its new own value, gray or not.

- "By hand" means the person changed the master itself, in the dialog or in the file. The master moving by itself, after a child change, never touches the other children.

### Why a hand change clears the flags

Without it, old flags come back. Say a group goes to Some with child A flagged. Then the master is turned on by hand, so the mode is None and A goes gray. Later child B is changed, so the mode is Some again. If A's old flag were still set, A would un-gray too, though nobody touched it this time.

Clearing every flag on a hand change of the master stops that. It's the only time the children are touched as a group.

### The settings file

- The file keeps only values that differ from their defaults. So:
	- A mode equal to its default isn't written.
	- Only set flags are written.
	- Clearing the flags on a hand change deletes lines. It never writes one per child.

- Grays survive a restart, since the mode and the flags are in the file.

- A hand edit to the file is read the same as the dialog would set it.
	- A changed mode, or a changed master value, counts as a hand change of the master, so the flags are cleared.
	- A flag that is set while the mode isn't Some is ignored. It does no harm, and the next hand change of the master removes it.
	- A mode value the app doesn't know is read as the default, like any other bad value.

### The settings dialog

- A grayed child is drawn in the theme's disabled text color, but it still takes clicks, keys and focus. A screen reader is told it follows its master.

- When a change moves the master or the mode, every child in the group redraws from the function at once.

- A change in the file picked up while the dialog is open redraws the same way.

### Tests

- The function, for every mode, flag and kind of master, including an inverted switch and a preset dropdown.

- Every row of the table under [Changing a setting](#changing-a-setting).

- The stale flag case in [Why a hand change clears the flags](#why-a-hand-change-clears-the-flags).

- A restart: grays and values are read back the same.

- Hand edits to the file, including a flag with the wrong mode adn an unknown mode.

- The table: every child named in it exists, has one master, and no master is a child.

### Open questions

- Whether a grayed child gets a tooltip naming its master, and saying that a change turns it off.

## Alternative ideas

### Unconsidered

- Moving the mode to All once every child in a Some group has been changed by itself. It would act the same, so there is little to gain.

- A button per group that un-grays it, in place of turning the master on and off.

### Rejected

- Disabling moot settings, as most programs do. That's the problem this design is for.

- Only an override flag per child, with the master's state worked out from the flags. Turning the master off by hand would then write one line per child, and it still needs the flags cleared at the right times.

- "A child uses its own value when its flag is set or the master is off." Once a child change turns the master off by itself, that rule un-grays every child in the group.

### Superseded

- None yet.

## Research findings

- Most programs disable a setting whose master makes it moot, so nothing tells a person which master to look for, or what turning it off will bring back.

## Roadmap

- Build the table, the function and the dialog drawing.

- Move every existing master and its settings onto it in one go, so there is never a mix of old and new.

- Add the lint check that refuses a direct read of a child.

## Related backlog issues

- A setting that another setting makes moot is grayed, but can still be changed (2026100816170959). Queued.
