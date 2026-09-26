#!/usr/bin/env python3

# Connect and disconnect a settings handler on the same preference group.
#
# There are several groups - nemo_preferences, nemo_windows_preferences,
# nemo_desktop_preferences and the rest - and they are separate objects. A
# disconnect aimed at the wrong one removes nothing and reports nothing, so the
# handler outlives whatever it was connected for and is called with a pointer to
# freed memory the next time the setting changes. Live settings reload makes
# that reachable in normal use.
#
# Two spellings are checked. Disconnect by function pairs a callback name with
# the groups it was connected to. Disconnect by id pairs the variable holding
# the id with the group the connect that filled it used.
#
# --self-test runs the check over a made-up pair of files, one wrong and one
# right, so a change that blinds it to a group or a spelling shows up here
# rather than as a clean run over a tree that is not clean.
#
# Syntax: lint-pref-handlers.py [root]   (default: the repo's source/)
#         lint-pref-handlers.py --self-test

# Copyright (c) 2026 Bubbles
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import re
import sys
import tempfile
from pathlib import Path

# The group names come from the header that declares them, so a new group is
# covered the day it is added. Matching a pattern instead missed nemo_window_state,
# whose name does not end in "preferences".
GROUPS_HEADER = "libnemo-private/nemo-global-preferences.h"
DECLARED = re.compile(r"^extern\s+NemoConfigGroup\s*\*\s*(\w+)\s*;", re.MULTILINE)


def groups_pattern(root):
    header = root / GROUPS_HEADER
    names = DECLARED.findall(header.read_text(errors="replace")) if header.is_file() else []
    if not names:
        raise SystemExit(f"[ FAILED: no config groups declared in {GROUPS_HEADER} ]")
    return "(?:" + "|".join(sorted(names, key=len, reverse=True)) + ")"



def patterns(group):
    return {
        "connect_by_func": re.compile(
            r"g_signal_connect\w*\s*\(\s*(" + group + r")\s*,.*?G_CALLBACK\s*\(\s*(\w+)\s*\)",
            re.DOTALL),
        "disconnect_by_func": re.compile(
            r"g_signal_handlers_disconnect_by_func\s*\(\s*(" + group + r")\s*,\s*(\w+)\s*,",
            re.DOTALL),
        "connect_to_id": re.compile(
            r"(\w+)\s*=\s*g_signal_connect\w*\s*\(\s*(" + group + r")\s*,",
            re.DOTALL),
        # The id is usually a struct member, as in self->handler.
        "disconnect_by_id": re.compile(
            r"g_signal_handler_disconnect\s*\(\s*(" + group + r")\s*,\s*(?:\w+\s*(?:->|\.)\s*)*(\w+)\s*\)",
            re.DOTALL),
    }


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def check_file(path, pats):
    text = path.read_text(errors="replace")
    problems = []

    by_func = {}
    for match in pats["connect_by_func"].finditer(text):
        by_func.setdefault(match.group(2), set()).add(match.group(1))

    by_id = {}
    for match in pats["connect_to_id"].finditer(text):
        by_id.setdefault(match.group(1), set()).add(match.group(2))

    for match in pats["disconnect_by_func"].finditer(text):
        group, callback = match.group(1), match.group(2)
        connected = by_func.get(callback)
        # No connect in this file means the pair is somewhere else and there is
        # nothing here to compare against.
        if connected and group not in connected:
            problems.append(
                (line_of(text, match.start()),
                 f"{callback} is disconnected from {group} but connected to "
                 + ", ".join(sorted(connected)))
            )

    for match in pats["disconnect_by_id"].finditer(text):
        group, variable = match.group(1), match.group(2)
        connected = by_id.get(variable)
        if connected and group not in connected:
            problems.append(
                (line_of(text, match.start()),
                 f"handler {variable} is disconnected from {group} but connected to "
                 + ", ".join(sorted(connected)))
            )

    return problems


SELF_TEST_HEADER = """\
extern NemoConfigGroup *nemo_preferences;
extern NemoConfigGroup *nemo_window_state;
"""

# nemo_window_state is the group whose name does not end in "preferences".
SELF_TEST_WRONG = """\
static void
thing_init (Thing *self)
{
\tg_signal_connect_swapped (nemo_window_state, "changed::sidebar-width",
\t\t\t\t  G_CALLBACK (width_changed), self);
\tself->handler = g_signal_connect (nemo_window_state, "changed::start-with-sidebar",
\t\t\t\t\t  G_CALLBACK (sidebar_changed), self);
}

static void
thing_finalize (Thing *self)
{
\tg_signal_handlers_disconnect_by_func (nemo_preferences, width_changed, self);
\tg_signal_handler_disconnect (nemo_preferences, self->handler);
}
"""

SELF_TEST_RIGHT = SELF_TEST_WRONG.replace("disconnect_by_func (nemo_preferences",
                                          "disconnect_by_func (nemo_window_state")
SELF_TEST_RIGHT = SELF_TEST_RIGHT.replace("handler_disconnect (nemo_preferences",
                                          "handler_disconnect (nemo_window_state")


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / GROUPS_HEADER).parent.mkdir(parents=True)
        (root / GROUPS_HEADER).write_text(SELF_TEST_HEADER)
        (root / "wrong.c").write_text(SELF_TEST_WRONG)
        (root / "right.c").write_text(SELF_TEST_RIGHT)

        pats = patterns(groups_pattern(root))
        wrong = check_file(root / "wrong.c", pats)
        right = check_file(root / "right.c", pats)

    failed = False
    if len(wrong) != 2:
        print(f"[ FAIL: self-test: expected both mismatches reported, got {len(wrong)}: {wrong} ]")
        failed = True
    if right:
        print(f"[ FAIL: self-test: a matched pair was reported: {right} ]")
        failed = True
    if failed:
        return 1
    print("[ OK: settings handlers self-test ]")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()

    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "source"
    if not root.is_dir():
        print(f"[ FAILED: {root} is not a directory ]")
        return 1

    pats = patterns(groups_pattern(root))
    failures = 0
    for path in sorted(root.rglob("*.c")):
        for line, message in check_file(path, pats):
            rel = path.relative_to(root.parent)
            print(f"[ FAIL: {rel}:{line}: {message} ]")
            failures += 1

    if failures:
        print(f"[ FAILED: settings handlers: {failures} mismatched group(s) ]")
        return 1
    print("[ OK: settings handlers: every disconnect matches its connect ]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
