#!/usr/bin/env python3

##	- Purpose:
##		Static checks on the demo recorder. It drives the app through settings keys
##		and coordinates, and everything it gets wrong is silent: a renamed setting
##		just stops configuring anything, and a column that prints an account name
##		only shows up once the gif is published. Reading the file with ast keeps
##		this out of numpy, scipy and PIL, which the recorder itself needs.
##	- Syntax:
##		lint-demo-script.py [repo-root]
##	- History: At bottom of file.

##	Copyright (c) 2026 Bubbles
##	SPDX-License-Identifier: MIT

import ast
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
RECORDER = ROOT / "cicd/utility/demo-video/demo-video.py"
SCHEMA = ROOT / "source/data/nemo-anywhere.schema.shcl"
GUARD_H = ROOT / "source/libnemo-private/nemo-delete-testguard.h"

# these print the account name of whoever ran the recording
SECRET_COLUMNS = {"owner", "owner_name", "owner_and_name", "group", "permissions",
    "octal_permissions"}

problems = []

def fail(msg):
    problems.append(msg)

def dict_keys_and_values(node):
    out = {}
    for key, value in zip(node.keys, node.values):
        if isinstance(key, ast.Constant) and isinstance(key.value, str):
            out[key.value] = value.value if isinstance(value, ast.Constant) else None
    return out

def settings_written(tree):
    """Every dotted key the recorder writes: the starting set, plus live changes."""
    found = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if (isinstance(target, ast.Name) and target.id == "START_SETTINGS"
                        and isinstance(node.value, ast.Dict)):
                    found.update(dict_keys_and_values(node.value))
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "set_cfg"):
            for arg in node.args:
                if isinstance(arg, ast.Dict):
                    found.update(dict_keys_and_values(arg))
    return found

def main():
    if not RECORDER.is_file():
        print("demo lint: no recorder to check")
        return 0
    source = RECORDER.read_text(encoding="utf-8")
    tree = ast.parse(source)

    ## Every setting the demo writes has to still exist, or the run silently
    ## configures nothing and the recording comes out with the wrong defaults.
    schema_text = SCHEMA.read_text(encoding="utf-8")
    schema = set(re.findall(r"(?m)^field:\s*(\S+)", schema_text))
    written = settings_written(tree)
    if not written:
        fail("no settings found in the recorder - has START_SETTINGS been renamed?")
    for key in sorted(written):
        if key not in schema:
            fail(f"demo writes '{key}', which is not a field in the schema")

    ## Nothing on screen may carry the account name of the box it was recorded on.
    columns = written.get("list-view.default-visible-columns") or ""
    for col in (c.strip() for c in columns.strip().strip("[]").split(",")):
        if col in SECRET_COLUMNS:
            fail(f"demo shows the '{col}' column, which prints a real account name")

    ## A move or a trash on camera hits the delete test guard while it is armed, so
    ## the recording would show its dialog and call stack instead of the feature.
    ## The compile-time arm beats everything. Below it the demo's own settings
    ## decide, and with nothing there the schema default does.
    forced = re.search(r"(?m)^#define\s+NEMO_TESTGUARD_ALL_DELETES\s+(\d)",
        GUARD_H.read_text(encoding="utf-8"))
    default = re.search(r"(?m)^field:\s*debug\.testguard-all-deletes\n(?:\t.*\n)*?\tdefault:\s*(\S+)",
        schema_text)
    setting = written.get("debug.testguard-all-deletes")
    if setting is None:
        setting = default.group(1) if default else "false"
    armed = (forced and forced.group(1) == "1") or setting == "true"
    if armed and re.search(r"\.drag\s*\(", source):
        fail("demo drags files while the delete test guard is armed - the guard's "
            "dialog would be what the scene shows")

    for msg in problems:
        print(f"demo lint: {msg}", file=sys.stderr)
    if problems:
        print(f"FAILED: demo script lint: {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print("demo lint: clean")
    return 0

if __name__ == "__main__":
    sys.exit(main())

##	History:
##		- 20260919 JC: Created.
##		- 20260923 JC: Guard check also reads the demo settings and the schema default.
