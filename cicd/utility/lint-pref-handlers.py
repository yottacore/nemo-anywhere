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
# A plain connect on a group also has to fit a row of the table in design.md,
# "Handlers on settings groups". Its data is NULL, a file static, a local whose
# handler goes before its function returns, or a struct whose handlers go in
# the function that frees it. Anything else is an object, and a disconnect in
# finalize is too late for a view that is still held, so that is reported and
# g_signal_connect_object is the fix. A handler whose data is NULL or a file
# static is connected once for the process, so a disconnect of one is reported
# too: the first owner to go would take it from every other.
#
# Each key is checked against the group the settings table puts it in, for a
# "changed::" handler and for a read or write through nemo_config_*. A handler
# on a group that does not have its key is never called, and nothing says so.
#
# --self-test runs the checks over made-up files, wrong and right, so a change
# that blinds them to a group or a spelling shows up here rather than as a
# clean run over a tree that is not clean.
# Test ID: rhtg2yer
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


# The table the program looks every key up in, the name each group variable is
# given, and the string macros keys are written with.
KEYS_TABLE = "libnemo-private/nemo-config-keys.h"
GROUP_NAMES = "libnemo-private/nemo-global-preferences.c"
TABLE_ROW = re.compile(r'^\s*\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*NEMO_CONFIG_\w+', re.MULTILINE)
GROUP_NAME = re.compile(r'^\s*(\w+)\s*=\s*nemo_config_get_group\s*\(\s*"([^"]+)"\s*\)', re.MULTILINE)
STRING_DEFINE = re.compile(
    r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+"([^"\n]*)"[ \t]*(?:/\*.*?\*/[ \t]*|//[^\n]*)?$',
    re.MULTILINE)


def string_defines(text):
    found = {}
    for name, value in STRING_DEFINE.findall(text):
        # Two values for one name cannot be told apart here, so neither is used.
        found[name] = value if found.get(name, value) == value else None
    return found


def key_context(root):
    def read(rel):
        path = root / rel
        return path.read_text(errors="replace") if path.is_file() else ""

    keys = set(TABLE_ROW.findall(read(KEYS_TABLE)))
    names = dict(GROUP_NAME.findall(read(GROUP_NAMES)))
    declared = DECLARED.findall(read(GROUPS_HEADER))
    if not keys:
        raise SystemExit(f"[ FAILED: no keys found in {KEYS_TABLE} ]")
    unnamed = [group for group in declared if group not in names]
    if unnamed:
        raise SystemExit(f"[ FAILED: no group name for {', '.join(unnamed)} in {GROUP_NAMES} ]")

    macros = {}
    for header in sorted(root.rglob("*.h")):
        for name, value in string_defines(header.read_text(errors="replace")).items():
            macros[name] = value if macros.get(name, value) == value else None
    return {"keys": keys, "names": names, "macros": macros}



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


# Comments, string and char literals and preprocessor lines blanked to spaces,
# newlines kept, so offsets and line numbers still match the file. A comma or a
# brace inside one of them then cannot throw off the argument split or the
# function bounds.
MASKED = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|^[ \t]*#(?:\\\n|[^\n])*',
    re.DOTALL | re.MULTILINE)


def masked(text):
    def blank(match):
        piece = match.group(0)
        if piece[0] in "\"'":
            return piece[0] + re.sub(r"[^\n]", " ", piece[1:-1]) + piece[-1]
        return re.sub(r"[^\n]", " ", piece)
    return MASKED.sub(blank, text)


# Offsets of each top-level brace block. Every connect sits in a function body,
# so the block around one is its function.
def functions(code):
    spans, depth, start = [], 0, 0
    for pos, char in enumerate(code):
        if char == "{":
            if depth == 0:
                start = pos
            depth += 1
        elif char == "}" and depth > 0:
            depth -= 1
            if depth == 0:
                spans.append((start, pos))
    return spans


def function_at(spans, pos):
    for start, end in spans:
        if start <= pos <= end:
            return start, end
    return None


# Arguments of the call whose "(" is at open_pos, split on top-level commas,
# each with its runs of whitespace made one space.
def call_args(code, open_pos):
    spans, close = call_arg_spans(code, open_pos)
    if spans is None:
        return None, close
    return [re.sub(r"\s+", " ", code[start:end]).strip() for start, end in spans], close


# The same split as offsets, so an argument can be read from the unmasked text.
def call_arg_spans(code, open_pos):
    spans, depth, start = [], 0, open_pos + 1
    for pos in range(open_pos, len(code)):
        char = code[pos]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                spans.append((start, pos))
                return spans, pos
        elif char == "," and depth == 1:
            spans.append((start, pos))
            start = pos + 1
    return None, len(code)


def squeeze(expr):
    return re.sub(r"\s+", "", expr)


PLAIN_CONNECT = re.compile(r"\bg_signal_connect(?:_swapped|_after|_data)?\s*\(")
DISCONNECT_BY_DATA = re.compile(r"\bg_signal_handlers_disconnect_by_data\s*\(")
FREED = re.compile(r"\b(g_free|g_slice_free\w*)\s*\(")
ASSIGNED_TO = re.compile(r"((?:\w+\s*(?:->|\.)\s*)*\w+)\s*=\s*$")


def is_file_static(code, name):
    decl = re.compile(r"\bstatic\b[^;{}()=]*\b" + re.escape(name) + r"\s*(?:\[[^\]]*\]\s*)?[=;]")
    spans = functions(code)
    return any(function_at(spans, m.start()) is None for m in decl.finditer(code))


# Functions that remove every handler a struct has on a group and then free it.
def struct_rows(code, spans, groups):
    rows = set()
    for start, end in spans:
        body = code[start:end]
        dropped = set()
        for match in DISCONNECT_BY_DATA.finditer(body):
            args, _ = call_args(body, match.end() - 1)
            if args and len(args) == 2 and re.fullmatch(groups, args[0]):
                dropped.add((args[0], squeeze(args[1])))
        freed = set()
        for match in FREED.finditer(body):
            args, _ = call_args(body, match.end() - 1)
            if args:
                freed.add(squeeze(args[-1]))
        rows |= {pair for pair in dropped if pair[1] in freed}
    return rows


def lifetime_problems(text, groups):
    code = masked(text)
    spans = functions(code)
    structs = None
    problems = []

    for match in PLAIN_CONNECT.finditer(code):
        args, close = call_args(code, match.end() - 1)
        if not args or len(args) < 4 or not re.fullmatch(groups, args[0]):
            continue
        group_name = args[0]
        data = args[3]
        callback = re.sub(r"^G_CALLBACK\s*\(\s*(.*?)\s*\)$", r"\1", args[2])

        if data == "NULL":
            continue

        if data.startswith("&") and re.fullmatch(r"&\s*\w+", data):
            name = data[1:].strip()
            if is_file_static(code, name):
                continue
            span = function_at(spans, match.start())
            assigned = span and ASSIGNED_TO.search(code[span[0]:match.start()])
            if assigned:
                disconnect = re.compile(
                    r"\bg_signal_handler_disconnect\s*\(\s*" + re.escape(group_name) + r"\s*,\s*"
                    + r"\s*".join(re.escape(part) for part in re.split(r"\s*(->|\.)\s*", assigned.group(1)))
                    + r"\s*\)")
                if disconnect.search(code, close, span[1]):
                    continue
            problems.append((line_of(text, match.start()), "lifetime",
                             f"{callback} on {group_name} with local {data} is not disconnected "
                             "from that group before its function returns"))
            continue

        if structs is None:
            structs = struct_rows(code, spans, groups)
        if (group_name, squeeze(data)) in structs:
            continue
        problems.append((line_of(text, match.start()), "lifetime",
                         f"{callback} on {group_name} with {data} can outlive it; use "
                         "g_signal_connect_object, per design.md \"Handlers on settings groups\""))

    for match in DISCONNECT_ONCE.finditer(code):
        args, _ = call_args(code, match.end() - 1)
        if not args or len(args) < 2 or not re.fullmatch(groups, args[0]):
            continue
        data = args[-1]
        if data == "NULL" or (re.fullmatch(r"&\s*\w+", data) and is_file_static(code, data[1:].strip())):
            problems.append((line_of(text, match.start()), "once",
                             f"a handler on {args[0]} with {data} is there for the whole process; "
                             "never disconnect it, per design.md \"Handlers on settings groups\""))

    return problems


DISCONNECT_ONCE = re.compile(r"\bg_signal_handlers_disconnect_by_(?:func|data)\s*\(")
KEYED_CALL = re.compile(r"\b(g_signal_connect\w*|nemo_config_(?:get|set|reset|bind)\w*)\s*\(")
KEY_PIECES = re.compile(r'\s*(?:"((?:\\.|[^"\\])*)"|(\w+))')


# The key's text, with its macros put in. None when it is not known until run
# time, such as a variable. A name in capitals that cannot be found is reported
# rather than skipped, so a moved header cannot blind the check.
def key_text(expr, macros):
    found, pos = [], 0
    for piece in KEY_PIECES.finditer(expr):
        if piece.start() != pos:
            return None, None
        pos = piece.end()
        found.append(piece.groups())
    if not found or expr[pos:].strip():
        return None, None

    pieces = []
    for literal, name in found:
        if name is None:
            pieces.append(literal)
        elif macros.get(name) is not None:
            pieces.append(macros[name])
        elif re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
            return None, name
        else:
            return None, None
    return "".join(pieces), None


def key_problems(text, groups, context):
    code = masked(text)
    macros = dict(context["macros"])
    macros.update((name, value) for name, value in string_defines(text).items() if value is not None)
    problems = []

    for match in KEYED_CALL.finditer(code):
        spans, _ = call_arg_spans(code, match.end() - 1)
        if not spans or len(spans) < 2:
            continue
        group_name = squeeze(code[spans[0][0]:spans[0][1]])
        if not re.fullmatch(groups, group_name):
            continue
        line = line_of(text, match.start())
        key, unknown = key_text(text[spans[1][0]:spans[1][1]], macros)
        if unknown:
            problems.append((line, "group", f"{unknown} on {group_name} is not a string macro this can read"))
            continue
        if key is None:
            continue
        if match.group(1).startswith("g_signal"):
            if not key.startswith("changed::"):
                continue
            key = key[len("changed::"):]
        group = context["names"].get(group_name)
        if (group, key) in context["keys"]:
            continue
        holders = sorted(owner for owner, name in context["keys"] if name == key)
        where = "is in " + ", ".join(holders) if holders else "is in no group"
        problems.append((line, "group", f"{key} on {group_name} ({group}), but the key {where}"))

    return problems


def check_file(path, pats, groups, context=None):
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
                (line_of(text, match.start()), "mismatch",
                 f"{callback} is disconnected from {group} but connected to "
                 + ", ".join(sorted(connected)))
            )

    for match in pats["disconnect_by_id"].finditer(text):
        group, variable = match.group(1), match.group(2)
        connected = by_id.get(variable)
        if connected and group not in connected:
            problems.append(
                (line_of(text, match.start()), "mismatch",
                 f"handler {variable} is disconnected from {group} but connected to "
                 + ", ".join(sorted(connected)))
            )

    problems += lifetime_problems(text, groups)
    if context is not None:
        problems += key_problems(text, groups, context)
    return problems


SELF_TEST_HEADER = """\
extern NemoConfigGroup *nemo_preferences;
extern NemoConfigGroup *nemo_list_view_preferences;
extern NemoConfigGroup *nemo_window_state;
extern NemoConfigGroup *nemo_windows_preferences;

#define NEMO_PREFERENCES_PATH_SEPARATOR\t\t"path-separator"
#define NEMO_PREFERENCES_DATE_FORMAT   "date-format"    /* the trailing comment is part of the test */
"""

SELF_TEST_KEYS = """\
static const NemoConfigKey nemo_config_keys[] = {
\t{ "preferences", "date-format", NEMO_CONFIG_ENUM, "locale", NULL, NULL, NULL },
\t{ "list-view", "row-shading", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
\t{ "window-state", "sidebar-width", NEMO_CONFIG_INT, "200", NULL, NULL, NULL },
\t{ "window-state", "start-with-sidebar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
\t{ "windows", "path-separator", NEMO_CONFIG_ENUM, "backslash", NULL, NULL, NULL },
\t{ NULL }
};
"""

SELF_TEST_NAMES = """\
void
nemo_global_preferences_init (void)
{
\tnemo_preferences          = nemo_config_get_group ("preferences");
\tnemo_list_view_preferences = nemo_config_get_group ("list-view");
\tnemo_window_state          = nemo_config_get_group ("window-state");
\tnemo_windows_preferences   = nemo_config_get_group ("windows");
}
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

# Item 22: the group is right, but finalize is too late for a view still held.
SELF_TEST_HELD_VIEW = """\
static void
thing_init (Thing *view)
{
\tg_signal_connect_swapped (nemo_list_view_preferences, "changed::row-shading",
\t\t\t\t  G_CALLBACK (shading_changed), view);
}

static void
thing_finalize (GObject *object)
{
\tg_signal_handlers_disconnect_by_func (nemo_list_view_preferences, shading_changed, object);
}
"""

SELF_TEST_OBJECT = """\
static void
thing_init (Thing *view)
{
\tg_signal_connect_object (nemo_list_view_preferences, "changed::row-shading",
\t\t\t\t G_CALLBACK (shading_changed), view, G_CONNECT_SWAPPED);
}
"""

SELF_TEST_NO_DISCONNECT = """\
static void
thing_init (Thing *self)
{
\tg_signal_connect (nemo_preferences, "changed::date-format",
\t\t\t  G_CALLBACK (format_changed), self);
}
"""

SELF_TEST_NULL = """\
void
thing_class_init (ThingClass *klass)
{
\tg_signal_connect_swapped (nemo_preferences, "changed::date-format",
\t\t\t\t  G_CALLBACK (format_changed), NULL);
}
"""

SELF_TEST_STATIC = """\
static int counter = 0;

void
thing_once (void)
{
\tg_signal_connect_swapped (nemo_preferences, "changed::date-format",
\t\t\t\t  G_CALLBACK (count_it), &counter);
}
"""

SELF_TEST_LOCAL = """\
static void
test_it (void)
{
\tchar seen = 0;
\tgulong id;

\tid = g_signal_connect_swapped (nemo_window_state, "changed::sidebar-width",
\t\t\t\t       G_CALLBACK (note_it), &seen);
\tnemo_config_set_int (nemo_window_state, "sidebar-width", 3);
\tg_signal_handler_disconnect (nemo_window_state, id);
}
"""

SELF_TEST_LOCAL_KEPT = """\
static void
test_it (void)
{
\tchar seen = 0;

\tg_signal_connect_swapped (nemo_window_state, "changed::sidebar-width",
\t\t\t\t  G_CALLBACK (note_it), &seen);
}
"""

SELF_TEST_LOCAL_OTHER_GROUP = SELF_TEST_LOCAL.replace("handler_disconnect (nemo_window_state",
                                                      "handler_disconnect (nemo_preferences")

SELF_TEST_STRUCT = """\
static void
tab_destroyed (GtkWidget *dialog, Tab *tab)
{
\tg_signal_handlers_disconnect_by_data (nemo_preferences, tab);
\tg_free (tab);
}

static void
tab_new (GtkWidget *dialog)
{
\tTab *tab = g_new0 (Tab, 1);

\tg_signal_connect (dialog, "destroy", G_CALLBACK (tab_destroyed), tab);
\tg_signal_connect_swapped (nemo_preferences, "changed::date-format",
\t\t\t\t  G_CALLBACK (refresh), tab);
}
"""

# Item 2026100221072783: the key moved to the windows group and the window
# kept listening on the main one, so its handler never ran.
SELF_TEST_WRONG_GROUP = """\
static void
window_init (Window *window)
{
\tg_signal_connect_object (nemo_preferences, "changed::" NEMO_PREFERENCES_PATH_SEPARATOR,
\t\t\t\t G_CALLBACK (title_changed), window, G_CONNECT_SWAPPED);
\tg_signal_connect_swapped (nemo_list_view_preferences, "changed::date-format",
\t\t\t\t  G_CALLBACK (format_changed), NULL);
\tg_signal_connect_swapped (nemo_preferences, "changed::no-such-key",
\t\t\t\t  G_CALLBACK (format_changed), NULL);
\tg_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_GONE,
\t\t\t\t  G_CALLBACK (format_changed), NULL);
\tg_free (nemo_config_get_string (nemo_window_state, NEMO_PREFERENCES_PATH_SEPARATOR));
}
"""

SELF_TEST_RIGHT_GROUP = """\
#define LOCAL_KEY "sidebar-width"

static void
window_init (Window *window, const char *key)
{
\tg_signal_connect_object (nemo_windows_preferences, "changed::" NEMO_PREFERENCES_PATH_SEPARATOR,
\t\t\t\t G_CALLBACK (title_changed), window, G_CONNECT_SWAPPED);
\tg_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_DATE_FORMAT,
\t\t\t\t  G_CALLBACK (format_changed), NULL);
\tg_signal_connect_swapped (nemo_preferences, "changed",
\t\t\t\t  G_CALLBACK (any_changed), NULL);
\tnemo_config_set_int (nemo_window_state, LOCAL_KEY, 3);
\tnemo_config_get_boolean (nemo_preferences, key);
}
"""

# Item 2026100221072784: connected once for the process, and the first
# container freed took them from every other.
SELF_TEST_ONCE = """\
static GQuark *captions;

static void
container_init (Container *container)
{
\tstatic gboolean done = FALSE;

\tif (!done) {
\t\tg_signal_connect_swapped (nemo_preferences, "changed::date-format",
\t\t\t\t\t  G_CALLBACK (limit_changed), NULL);
\t\tg_signal_connect (nemo_preferences, "changed::date-format",
\t\t\t\t  G_CALLBACK (update_quarks), &captions);
\t\tdone = TRUE;
\t}
}

static void
container_finalize (GObject *object)
{
\tg_signal_handlers_disconnect_by_func (nemo_preferences, limit_changed, NULL);
\tg_signal_handlers_disconnect_by_func (nemo_preferences, update_quarks, &captions);
}
"""

# Each case and every report it must give, by kind and a piece of its text.
# Nothing else may be reported.
SELF_TEST_CASES = [
    ("wrong.c", SELF_TEST_WRONG, [
        ("mismatch", "width_changed is disconnected from nemo_preferences"),
        ("mismatch", "handler handler is disconnected from nemo_preferences"),
        ("lifetime", "width_changed on nemo_window_state with self"),
        ("lifetime", "sidebar_changed on nemo_window_state with self"),
    ]),
    ("right.c", SELF_TEST_RIGHT, [
        ("lifetime", "width_changed on nemo_window_state with self"),
        ("lifetime", "sidebar_changed on nemo_window_state with self"),
    ]),
    ("held-view.c", SELF_TEST_HELD_VIEW, [
        ("lifetime", "shading_changed on nemo_list_view_preferences with view"),
    ]),
    ("no-disconnect.c", SELF_TEST_NO_DISCONNECT, [
        ("lifetime", "format_changed on nemo_preferences with self"),
    ]),
    ("local-kept.c", SELF_TEST_LOCAL_KEPT, [
        ("lifetime", "note_it on nemo_window_state with local &seen"),
    ]),
    ("local-other-group.c", SELF_TEST_LOCAL_OTHER_GROUP, [
        ("mismatch", "handler id is disconnected from nemo_preferences"),
        ("lifetime", "note_it on nemo_window_state with local &seen"),
    ]),
    ("wrong-group.c", SELF_TEST_WRONG_GROUP, [
        ("group", "path-separator on nemo_preferences (preferences), but the key is in windows"),
        ("group", "date-format on nemo_list_view_preferences (list-view), but the key is in preferences"),
        ("group", "no-such-key on nemo_preferences (preferences), but the key is in no group"),
        ("group", "NEMO_PREFERENCES_GONE on nemo_preferences is not a string macro"),
        ("group", "path-separator on nemo_window_state (window-state), but the key is in windows"),
    ]),
    ("once.c", SELF_TEST_ONCE, [
        ("once", "on nemo_preferences with NULL is there for the whole process"),
        ("once", "on nemo_preferences with &captions is there for the whole process"),
    ]),
    ("right-group.c", SELF_TEST_RIGHT_GROUP, []),
    ("object.c", SELF_TEST_OBJECT, []),
    ("null.c", SELF_TEST_NULL, []),
    ("static.c", SELF_TEST_STATIC, []),
    ("local.c", SELF_TEST_LOCAL, []),
    ("struct.c", SELF_TEST_STRUCT, []),
]


def self_test():
    own = Path(__file__).read_text(encoding="utf-8")
    print("[ Test %s lint-pref-handlers.py --self-test ]" % re.search(r"(?m)^# Test ID: (\S+)$", own).group(1))
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / GROUPS_HEADER).parent.mkdir(parents=True)
        (root / GROUPS_HEADER).write_text(SELF_TEST_HEADER)
        (root / KEYS_TABLE).write_text(SELF_TEST_KEYS)
        (root / GROUP_NAMES).write_text(SELF_TEST_NAMES)
        groups = groups_pattern(root)
        pats = patterns(groups)
        context = key_context(root)

        for name, source, expected in SELF_TEST_CASES:
            (root / name).write_text(source)
            left = check_file(root / name, pats, groups, context)
            for kind, piece in expected:
                found = next((p for p in left if p[1] == kind and piece in p[2]), None)
                if found is None:
                    print(f"[ FAIL: self-test: {name}: no {kind} report with '{piece}': {left} ]")
                    failed = True
                else:
                    left.remove(found)
            if left:
                print(f"[ FAIL: self-test: {name}: reported what is right: {left} ]")
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

    groups = groups_pattern(root)
    pats = patterns(groups)
    context = key_context(root)
    failures = 0
    for path in sorted(root.rglob("*.c")):
        for line, _, message in check_file(path, pats, groups, context):
            rel = path.relative_to(root.parent)
            print(f"[ FAIL: {rel}:{line}: {message} ]")
            failures += 1

    if failures:
        print(f"[ FAILED: settings handlers: {failures} problem(s) ]")
        return 1
    print("[ OK: settings handlers: every disconnect matches its connect, every handler fits a row, and every key is on its group ]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
