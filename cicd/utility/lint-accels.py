#!/usr/bin/env python3

# Keyboard accelerators in the action tables.
#
# One key, one action. When two actions in the window claim the same key, GTK
# gives it to whichever was merged first, so the key quietly does the other
# thing. Ctrl+Shift+T is the case that bit: the view's "Open in new tab" and
# the window's new-tab key both had it, and which one ran depended on merge
# order. NewTabAccel owns it now.
#
# Menu keys are spelled <Primary>, which is Cmd on macOS and Control
# everywhere else. A few stay on Control because macOS already uses Cmd with
# that key (Cmd+H hides the app, Cmd+M minimizes, Cmd+F1 is the system's), or
# because tab switching is Control on every platform. Those are the only
# Control spellings allowed, in the tables and in the shortcuts window.
#
# Both sides of every #if are read, so a key claimed on one platform only
# still counts.
#
# Syntax: lint-accels.py [root]   (default: the repo's source/)

# Copyright (c) 2026 Bubbles
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import html
import re
import sys
from pathlib import Path

CODE_DIRS = ("src", "libnemo-private", "eel")
UI_SUFFIXES = (".ui", ".xml", ".glade")

# Actions that share a key because only one of them is ever shown at a time.
SHARED = {
    frozenset({"NEMO_ACTION_PIN_FILE", "NEMO_ACTION_UNPIN_FILE"}),  # one toggle, two labels
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

# Owner of a key, and the ui file that has to list it for it to work.
OWNED = {"NewTabAccel": ("primary+shift+t", "gresources/nemo-shell-ui.xml")}

TABLE = re.compile(r"Gtk(?:Toggle|Radio)?ActionEntry\s+\w+\s*\[\s*\]\s*=\s*\{")
MODS = {"control": "control", "ctrl": "control", "primary": "primary", "shift": "shift",
        "alt": "alt", "mod1": "alt", "super": "super", "meta": "meta", "hyper": "hyper"}


def strip_comments(text):
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


def configurations(text):
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


def split_fields(body):
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


def entries(text):
    """(line, action name, accelerator) for each table entry that sets a key."""
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
                    fields = split_fields(text[start:i])
                    accel = re.fullmatch(r'"(.*)"', fields[3]) if len(fields) > 3 else None
                    if accel and accel.group(1):
                        name = fields[0].strip('"')
                        yield text.count("\n", 0, start) + 1, name, accel.group(1)
            i += 1


def canonical(accel, keep_control):
    mods = set()
    for mod in re.findall(r"<(\w+)>", accel):
        mod = MODS.get(mod.lower(), mod.lower())
        if mod == "control" and not keep_control:
            mod = "primary"
        mods.add(mod)
    key = re.sub(r"(?:<\w+>)+", "", accel).lower()
    return "+".join(sorted(mods) + [key])


def control_problem(accel):
    if not re.search(r"<(control|ctrl)>", accel, re.IGNORECASE):
        return None
    if canonical(accel, keep_control=True) in CONTROL_OK:
        return None
    return f"{accel} is spelled Control; use <Primary>, or add it to CONTROL_OK with its reason"


def check(root):
    problems = []
    owners = {}
    # Per configuration: key -> [(file, line, action name)], across every file,
    # since the view's table and the window's are merged into one window.
    claimed = ({}, {})

    for path in sorted(p for d in CODE_DIRS for p in (root / d).rglob("*.c")):
        rel = path.relative_to(root.parent)
        text = strip_comments(path.read_text(errors="replace"))

        for match in re.finditer(r'"((?:<\w+>)+[^"<>\s]*)"', text):
            problem = control_problem(match.group(1))
            if problem:
                problems.append((rel, text.count("\n", 0, match.start()) + 1, problem))

        for config, variant in enumerate(configurations(text)):
            for line, name, accel in entries(variant):
                key = canonical(accel, keep_control=False)
                owners.setdefault(name, set()).add(key)
                claimed[config].setdefault(key, []).append((rel, line, name))

    # One name in two tables is one action: the clipboard group stands in for
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

    for name, (key, ui) in OWNED.items():
        if key not in owners.get(name, set()):
            problems.append(("action tables", 0, f"{name} must own {key}"))
        ui_path = root / ui
        if ui_path.is_file() and f'<accelerator action="{name}"/>' not in ui_path.read_text(errors="replace"):
            problems.append((ui_path.relative_to(root.parent), 0, f"{name} is not listed, so its key does nothing"))

    for path in sorted(p for p in root.rglob("*") if p.suffix in UI_SUFFIXES):
        rel = path.relative_to(root.parent)
        for number, line in enumerate(path.read_text(errors="replace").split("\n"), 1):
            match = re.search(r'name="accelerator"[^>]*>([^<]*)<', line)
            if not match:
                continue
            for accel in html.unescape(match.group(1)).split():
                problem = control_problem(accel)
                if problem:
                    problems.append((rel, number, problem))

    return problems


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "source"
    if not root.is_dir():
        print(f"[ FAILED: {root} is not a directory ]")
        return 1

    problems = check(root)
    for rel, line, message in problems:
        where = f"{rel}:{line}" if line else str(rel)
        print(f"[ FAIL: {where}: {message} ]")

    if problems:
        print(f"[ FAILED: accelerators: {len(problems)} problem(s) ]")
        return 1
    print("[ OK: accelerators: one action per key, Control only where macOS needs it ]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
