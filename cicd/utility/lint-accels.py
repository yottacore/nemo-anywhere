#!/usr/bin/env python3

# Keyboard accelerators in the action tables.
#
# One key, one action. When 2 actions in the window claim the same key, GTK
# gives it to whichever was merged first, so the key quietly does the other
# thing. Ctrl+Shift+T is the case that bit: the view's "Open in new tab" and
# the window's new-tab key both had it, and which one ran depended on merge
# order. NewTabAccel owns it now.
#
# Menu keys are spelled <Primary>, which is Cmd on macOS and Control
# everywhere else. A few stay on Control because macOS already uses Cmd with
# that key, or because tab switching is Control on every platform. Cmd+H hides
# the app, Cmd+M minimizes, and Cmd+F1 is the system's. Those are the only
# Control spellings allowed, in the tables and in the shortcuts window.
#
# Every action has a name a person can read, marked for translation, since
# Settings' Keyboard tab lists them by it.
#
# Every action name looked up, by a NEMO_ACTION_ macro or by a quoted name
# passed to gtk_action_group_get_action, belongs to an action something
# makes. A lookup of a name nothing makes gets NULL, and GTK only says so at
# run time. UpAccel was removed with its lookup left behind.
#
# Both sides of every #if are read, so a key claimed on one platform only
# still counts.
#
# Syntax: lint-accels.py [root]   root defaults to the repo's source/

# Copyright (c) 2026 Bubbles
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import html
import re
import sys
from collections.abc import Iterator
from pathlib import Path

CODE_DIRS = ("src", "libnemo-private", "eel")
UI_SUFFIXES = (".ui", ".xml", ".glade")

# Actions that share a key because only one of them is ever shown at a time.
SHARED = {
    frozenset({"NEMO_ACTION_PIN_FILE", "NEMO_ACTION_UNPIN_FILE"}),  # one toggle, 2 labels
}

# Keys left on Control, as canonical accelerators.
CONTROL_OK = {
    "control+f1",  # keyboard shortcuts window; Cmd+F1 is taken on macOS
    "control+page_up",  # tab switching
    "control+page_down",
    "control+shift+page_up",  # tab moving
    "control+shift+page_down",
    "control+h",  # hidden files; Cmd+H hides the app
    "control+shift+h",
    "control+m",  # make link; Cmd+M minimizes
}

# Owner of a key, and the ui file that has to list it, as an accelerator or a
# menu item, for it to work.
OWNED = {
    "NewTabAccel": ("primary+shift+t", "gresources/nemo-shell-ui.xml"),
    "Preferences": ("primary+comma", "gresources/nemo-shell-ui.xml"),
}

TABLE = re.compile(r"Gtk(?:Toggle|Radio)?ActionEntry\s+\w+\s*\[\s*\]\s*=\s*\{")
MODS = {"control": "control", "ctrl": "control", "primary": "primary", "shift": "shift",
        "alt": "alt", "mod1": "alt", "super": "super", "meta": "meta", "hyper": "hyper"}


def strip_comments(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def configurations(text: str) -> Iterator[str]:
    """The text with every #if taken as true, then as false. Line numbers kept."""
    lines = text.split("\n")
    directive = [re.match(r"\s*#\s*(\w+)\s*(.*)", line) for line in lines]

    # How many branches each conditional has, keyed by the line of its #if.
    count = {}
    stack = []
    for number, match in enumerate(directive):
        word = match.group(1) if match else ""
        if word in ("if", "ifdef", "ifndef"):
            stack.append(number)
            count[number] = 1
        elif word in ("elif", "else") and stack:
            count[stack[-1]] += 1
        elif word == "endif" and stack:
            stack.pop()

    for truth in (True, False):
        kept = []
        stack = []  # [branch being read, branch that is live, or -1 for none]
        for number, line in enumerate(lines):
            match = directive[number]
            word = match.group(1) if match else ""
            if word in ("if", "ifdef", "ifndef"):
                cond = match.group(2).strip()
                last = count[number] - 1 if count[number] > 1 else -1
                if cond == "0":
                    live = last
                elif cond == "1" or truth:
                    live = 0
                else:
                    live = last
                stack.append([0, live])
            elif word in ("elif", "else") and stack:
                stack[-1][0] += 1
            elif word == "endif" and stack:
                stack.pop()
            else:
                kept.append(line if all(read == live for read, live in stack) else "")
                continue
            kept.append("")
        yield "\n".join(kept)


def split_fields(body: str) -> list[str]:
    fields = []
    current = ""
    depth = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            current += body[i:j + 1]
            i = j + 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            fields.append(current.strip())
            current = ""
        else:
            current += c
        i += 1
    fields.append(current.strip())
    return fields


def table_rows(text: str) -> Iterator[tuple[int, list[str]]]:
    """(line, fields) for each entry of each action table."""
    for table in TABLE.finditer(text):
        i = table.end()
        depth = 1
        start = None
        while depth > 0 and i < len(text):
            c = text[i]
            if c == '"':
                j = i + 1
                while text[j] != '"':
                    j += 2 if text[j] == "\\" else 1
                i = j + 1
                continue
            if c == "{":
                depth += 1
                if depth == 2:
                    start = i + 1
            elif c == "}":
                depth -= 1
                if depth == 1 and start is not None:
                    yield text.count("\n", 0, start) + 1, split_fields(text[start:i])
            i += 1


def entries(text: str) -> Iterator[tuple[int, str, str]]:
    """(line, action name, accelerator) for each table entry that sets a key."""
    for line, fields in table_rows(text):
        accel = re.fullmatch(r'"(.*)"', fields[3]) if len(fields) > 3 else None
        if accel and accel.group(1):
            yield line, fields[0].strip('"'), accel.group(1)


def bare_labels(text: str) -> Iterator[tuple[int, str, str]]:
    """(line, action name, label) for each entry whose label isn't marked for
    translation. Settings' Keyboard tab lists actions by label, so a code
    name such as "ZoomInAccel" would show there as is."""
    for line, fields in table_rows(text):
        if len(fields) > 2 and re.fullmatch(r'"(.+)"', fields[2]):
            yield line, fields[0].strip('"'), fields[2]


ACTION_MACRO = re.compile(r"^\s*#\s*define\s+(NEMO_ACTION_\w+)\s+\"([^\"]*)\"", re.M)
# Ways an action gets made outside the tables.
MADE = (re.compile(r"\bgtk_(?:toggle_|radio_)?action_new\s*\(\s*([^,()]+?)\s*,"),
        re.compile(r"\bg_object_new\s*\([^;]*?\"name\"\s*,\s*([^,()]+?)\s*,"))
LOOKUP = re.compile(r"\bgtk_action_group_get_action\s*\([^,]+,\s*(\"[^\"]*\")\s*\)")


def orphan_lookups(root: Path) -> list[tuple[Path | str, int, str]]:
    """Action names used that no table entry or *_action_new makes."""
    problems: list[tuple[Path | str, int, str]] = []
    macros: dict[str, str] = {}
    code = {}
    for path in sorted(p for d in CODE_DIRS for p in (root / d).rglob("*") if p.suffix in (".c", ".h")):
        rel = path.relative_to(root.parent)
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in ACTION_MACRO.finditer(text):
            # Only the same value twice; 2 values can be the 2 sides of an #if.
            if macros.get(match.group(1)) == match.group(2):
                problems.append((rel, text.count("\n", 0, match.start()) + 1, f"{match.group(1)} is defined again"))
            macros[match.group(1)] = match.group(2)
        if path.suffix == ".c":
            code[rel] = text

    def value(token: str) -> str | None:
        quoted = re.fullmatch(r'"([^"]*)"', token.strip())
        return quoted.group(1) if quoted else macros.get(token.strip())

    made = set()
    for text in code.values():
        for variant in configurations(text):
            for _, fields in table_rows(variant):
                made.add(value(fields[0]))
        for pattern in MADE:
            made.update(value(match.group(1)) for match in pattern.finditer(text))

    for rel, text in code.items():
        uses = [(m.start(), m.group(0), macros[m.group(0)]) for m in re.finditer(r"\bNEMO_ACTION_\w+\b", text) if m.group(0) in macros]
        uses += [(m.start(1), m.group(1), m.group(1).strip('"')) for m in LOOKUP.finditer(text)]
        for start, token, name in uses:
            if name not in made:
                problems.append((rel, text.count("\n", 0, start) + 1, f"{token} names action {name}, which nothing makes"))
    return problems


def canonical(accel: str, keep_control: bool) -> str:
    mods = set()
    for mod in re.findall(r"<(\w+)>", accel):
        mod = MODS.get(mod.lower(), mod.lower())
        if mod == "control" and not keep_control:
            mod = "primary"
        mods.add(mod)
    key = re.sub(r"(?:<\w+>)+", "", accel).lower()
    return "+".join(sorted(mods) + [key])


def control_problem(accel: str) -> str | None:
    if not re.search(r"<(control|ctrl)>", accel, re.IGNORECASE):
        return None
    if canonical(accel, keep_control=True) in CONTROL_OK:
        return None
    return f"{accel} is spelled Control; use <Primary>, or add it to CONTROL_OK with its reason"


def check(root: Path) -> list[tuple[Path | str, int, str]]:
    problems: list[tuple[Path | str, int, str]] = []
    owners = {}
    # Per configuration: key -> [(file, line, action name)], across every file,
    # since the view's table and the window's are merged into one window.
    claimed = ({}, {})
    labeled = set()

    for path in sorted(p for d in CODE_DIRS for p in (root / d).rglob("*.c")):
        rel = path.relative_to(root.parent)
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))

        for match in re.finditer(r'"((?:<\w+>)+[^"<>\s]*)"', text):
            problem = control_problem(match.group(1))
            if problem:
                problems.append((rel, text.count("\n", 0, match.start()) + 1, problem))

        for config, variant in enumerate(configurations(text)):
            for line, name, accel in entries(variant):
                key = canonical(accel, keep_control=False)
                owners.setdefault(name, set()).add(key)
                claimed[config].setdefault(key, []).append((rel, line, name))
            for line, name, label in bare_labels(variant):
                if (rel, line) not in labeled:
                    labeled.add((rel, line))
                    problems.append((rel, line, f"{name} is labeled {label}; give it a name to list it by, in N_()"))

    # One name in 2 tables is one action: the clipboard group stands in for
    # the view's while a text field has the focus.
    reported = set()
    for config in claimed:
        for key, claims in config.items():
            names = frozenset(name for _, _, name in claims)
            if len(names) > 1 and names not in SHARED and (key, names) not in reported:
                reported.add((key, names))
                rel, line, _ = claims[-1]
                where = ", ".join(f"{name} ({r}:{n})" for r, n, name in claims)
                problems.append((rel, line, f"{key} is claimed by {len(names)} actions: {where}"))

    problems += orphan_lookups(root)

    for name, (key, ui) in OWNED.items():
        if key not in owners.get(name, set()):
            problems.append(("action tables", 0, f"{name} must own {key}"))
        ui_path = root / ui
        listed = re.compile(rf'<(?:accelerator|menuitem)\b[^>]*\baction="{re.escape(name)}"')
        if ui_path.is_file() and not listed.search(ui_path.read_text(encoding="utf-8", errors="replace")):
            problems.append((ui_path.relative_to(root.parent), 0, f"{name} isn't listed, so its key does nothing"))

    for path in sorted(p for p in root.rglob("*") if p.suffix in UI_SUFFIXES):
        rel = path.relative_to(root.parent)
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").split("\n"), 1):
            match = re.search(r'name="accelerator"[^>]*>([^<]*)<', line)
            if not match:
                continue
            for accel in html.unescape(match.group(1)).split():
                problem = control_problem(accel)
                if problem:
                    problems.append((rel, number, problem))

    return problems


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "source"
    if not root.is_dir():
        print(f"[ FAILED: {root} isn't a directory ]")
        return 1

    problems = check(root)
    for rel, line, message in problems:
        where = f"{rel}:{line}" if line else str(rel)
        print(f"[ FAIL: {where}: {message} ]")

    if problems:
        print(f"[ FAILED: accelerators: {len(problems)} problem(s) ]")
        return 1
    print("[ OK: accelerators: one action per key, Control only where macOS needs it, every action named and made ]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
