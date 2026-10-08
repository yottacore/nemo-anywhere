#!/usr/bin/env python3

# Who frees what a function hands back.
#
# C has no way to say who owns a returned pointer, so the comment above the
# function says it, in GLib's own form: a "Returns: (transfer full)" line, or
# none, container or floating, with the free function where it isn't obvious.
# Every function a header declares that returns a pointer needs one. Static
# helpers are left out, and so is vendored code.
#
# This reads text, not what the compiler sees, so both sides of every #if are
# read and a Windows-only function is checked on Linux too. A definition starts
# in column 0 with its name, or with its return type and name, and has a body.
# The comment counts when it ends on the line above the return type, or one
# blank line above it.
#
# --self-test runs the check over made-up files, so a change that blinds it to
# a spelling shows up there rather than as a clean run over the tree.
# Test ID: rjh9pts1
#
# Syntax: lint-ownership.py [root]   (default: the repo's source/)
#         lint-ownership.py --self-test

# Copyright (c) 2026 Bubbles
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import re
import sys
import tempfile
from pathlib import Path

CODE_DIRS = ("archive-core", "eel", "libnemo-private", "libnemo-extension", "src", "search-helpers", "test", "fuzz")
POINTER_TYPEDEFS = {"gpointer", "gconstpointer", "GStrv"}
NOT_NAMES = {"if", "for", "while", "switch", "return", "sizeof", "defined"}
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
DEF_START = re.compile(r"^(.*?[\s*])?([A-Za-z_]\w*)\s*\(")


def blank_out(text):
    """Comments, string and char literals, and preprocessor lines become
    spaces. Newlines stay, so line numbers still match the file."""
    out = []
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#" or (line_start and c in " \t" and text[i:].lstrip(" \t").startswith("#")):
            # a directive, with its continuation lines
            while i < n:
                if text[i] == "\n" and (i == 0 or text[i - 1] != "\\"):
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
            line_start = False
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (min(j + 1, n) - i))
            i = j + 1
            line_start = False
            continue
        out.append(c)
        line_start = c == "\n"
        i += 1
    return "".join(out)


def header_names(root):
    """Every name a first-party header follows with a parenthesis. More than
    the declarations, but only definitions are looked up in it."""
    names = set()
    for d in CODE_DIRS:
        for h in sorted((root / d).rglob("*.h")):
            names.update(CALL.findall(blank_out(h.read_text(encoding="utf-8", errors="replace"))))
    return names


def comment_above(raw, top):
    """The comment that ends on the line above @top, or one blank line above
    it, as text; None when there is none."""
    k = top - 1
    if k >= 0 and not raw[k].strip():
        k -= 1
    if k < 0:
        return None
    if raw[k].rstrip().endswith("*/"):
        s = k
        while s >= 0 and "/*" not in raw[s]:
            s -= 1
        return "\n".join(raw[max(s, 0):k + 1])
    if raw[k].lstrip().startswith("//"):
        s = k
        while s >= 0 and raw[s].lstrip().startswith("//"):
            s -= 1
        return "\n".join(raw[s + 1:k + 1])
    return None


def check_file(path, names):
    text = path.read_text(encoding="utf-8", errors="replace")
    raw = text.split("\n")
    flat = blank_out(text)
    lines = flat.split("\n")
    offsets = []
    pos = 0
    for line in lines:
        offsets.append(pos)
        pos += len(line) + 1

    found = []
    for i, line in enumerate(lines):
        if not line or line[0] in " \t{}":
            continue
        m = DEF_START.match(line)
        if not m or m.group(2) in NOT_NAMES:
            continue
        name = m.group(2)

        # a body after the parameter list, not a semicolon
        p = offsets[i] + m.end() - 1
        depth = 0
        while p < len(flat):
            if flat[p] == "(":
                depth += 1
            elif flat[p] == ")":
                depth -= 1
                if depth == 0:
                    break
            p += 1
        rest = flat[p + 1:p + 200].lstrip()
        if not rest.startswith("{"):
            continue

        ret = [m.group(1) or ""]
        top = i
        if not ret[0].strip():
            j = i - 1
            while j >= 0 and lines[j].strip() and not lines[j].rstrip().endswith((";", "}", "{")):
                ret.insert(0, lines[j])
                top = j
                j -= 1
        rettype = " ".join(ret)
        if not rettype.strip() or re.search(r"\b(static|typedef)\b", rettype):
            continue
        if "*" not in rettype and not POINTER_TYPEDEFS & set(re.findall(r"\w+", rettype)):
            continue
        if name not in names:
            continue

        comment = comment_above(raw, top)
        if comment is None or "(transfer " not in comment:
            found.append((i + 1, name))
    return found


def run(root):
    names = header_names(root)
    bad = []
    for d in CODE_DIRS:
        for c in sorted((root / d).rglob("*.c")):
            for line, name in check_file(c, names):
                bad.append(f"{c.relative_to(root)}:{line}: {name}")
    return bad


SELF_TEST_HEADER = """\
#ifndef THING_H
#define THING_H
G_DECLARE_FINAL_TYPE (Thing, thing, THING, THING, GObject)
char        *thing_name        (Thing *thing);
GList       *thing_list        (void);
GStrv        thing_words       (void);
const char  *thing_peek        (Thing *thing);
Thing       *thing_new         (void);
int          thing_count       (void);
char        *thing_doc         (void);
char        *thing_far         (void);
char        *thing_wordy       (void);
GtkWidget   *thing_widget      (void);
gpointer     thing_data        (Thing *thing);
char        *thing_line        (void);
char        *thing_twice       (void);
#endif
"""

SELF_TEST_SOURCE = """\
#include "thing.h"

static char *helper (void);

/* Returns: (transfer full): free with g_free */
char *
thing_name (Thing *thing)
{
\treturn g_strdup ("x");
}

GList *
thing_list (void)
{
\treturn NULL;
}

GStrv
thing_words (void)
{
\treturn NULL;
}

/* Kept by @thing.
 * Returns: (transfer none) */
const char *
thing_peek (Thing *thing)
{
\treturn "";
}

#ifdef G_OS_WIN32
/* Returns: (transfer full): unref with g_object_unref */
Thing *
thing_new (void)
{
\treturn NULL;
}
#else
Thing *
thing_new (void)
{
\treturn NULL;
}
#endif

int
thing_count (void)
{
\treturn 0;
}

static char *
helper (void)
{
\treturn NULL;
}

char *
not_in_a_header (void)
{
\treturn NULL;
}

/**
 * thing_doc:
 *
 * Returns: (transfer full): a string
 */

char *
thing_doc (void)
{
\treturn NULL;
}

/* Returns: (transfer full): too far away */


char *
thing_far (void)
{
\treturn NULL;
}

/* A comment that says nothing about who frees it. */
char *
thing_wordy (void)
{
\treturn NULL;
}

// Returns: (transfer floating)
GtkWidget *
thing_widget (void)
{
\treturn NULL;
}

gpointer thing_data (Thing *thing)
{
\treturn thing;
}

char *thing_line (void) { return "(transfer full)"; }

/* Returns: (transfer full) */
char *
thing_twice (void)
{
\tconst char *s = "/* not a comment";
\treturn helper ();
}
"""

# thing_new is reported once, for its #else side.
SELF_TEST_EXPECTED = ["thing_list", "thing_words", "thing_new", "thing_far", "thing_wordy", "thing_data", "thing_line"]


def self_test():
    own = Path(__file__).read_text(encoding="utf-8")
    print("[ Test %s lint-ownership.py --self-test ]" % re.search(r"(?m)^# Test ID: (\S+)$", own).group(1))
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "src").mkdir()
        (root / "src" / "thing.h").write_text(SELF_TEST_HEADER)
        (root / "src" / "thing.c").write_text(SELF_TEST_SOURCE)
        got = [b.split(": ")[-1] for b in run(root)]
    if got != SELF_TEST_EXPECTED:
        print(f"[ FAIL: ownership self-test: reported {got}, expected {SELF_TEST_EXPECTED} ]")
        return 1
    print("[ OK: ownership self-test ]")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()

    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "source"
    if not root.is_dir():
        print(f"[ FAILED: {root} is not a directory ]")
        return 1

    bad = run(root)
    if bad:
        print("[ FAIL: a function a header declares returns a pointer, and the comment above it has no (transfer ...) line ]")
        for b in bad:
            print(f"  {b}")
        return 1
    print("[ OK: every pointer a header's function returns says who frees it ]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
