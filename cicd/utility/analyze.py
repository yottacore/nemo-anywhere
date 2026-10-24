#!/usr/bin/env python3

# Static analyzers over the C build: gcc's -fanalyzer and the clang analyzer.
# cicd/utility/analyze.bash drives it; see there for the stage itself.
#
#   run      Inside a build container. Runs one analyzer over every C file in a
#            compile_commands.json, once per set of flags it's built with, and
#            prints each run's diagnostics, paths made relative to the repo.
#   cover    Fails when a tracked .c is in neither compile list, or when one
#            under source/unused/, which nothing should build, is in one.
#   count    Counts unique findings per area from run's logs. Vendored ones on
#            analyze-vendored.txt count as suppressed instead, and an entry
#            there that matches nothing is listed as stale.
#   planted  Inside a build container. Runs one analyzer over a planted leak and
#            null dereference, and fails unless it reports both.
#
# --self-test runs cover and count over made-up input, the -Werror form of a
# gcc finding included, since a parser that only knows "warning:" counts 0.
# Test ID: rktsh374
#
# Syntax: analyze.py run --tool gcc|clang --db <compile_commands.json> [--jobs N]
#         analyze.py cover <repo root> <db>...
#         analyze.py count <repo root> <log>...
#         analyze.py planted --tool gcc|clang --cc <compiler>
#         analyze.py --self-test

# Copyright (c) 2026 t00mietum
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import collections
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile

SRC = "/src/"
TU_TIMEOUT = int(os.environ.get("ANALYZE_TU_TIMEOUT", "900"))
VENDORED = ("vendor/", "source/cut-n-paste-code/")
TESTS = ("source/test/", "source/fuzz/")
AREAS = ("vendored", "inherited", "ours", "tests", "system")
SUPP_FILE = "cicd/utility/analyze-vendored.txt"

# gcc: "f.c:1:2: warning: ... [-Wanalyzer-x]", or "error: ... [-Werror=analyzer-x]".
GCC_RE = re.compile(r"^(\S[^:]*):(\d+):(\d+): (?:warning|error): .*\[-W(?:error=)?(analyzer-[a-z0-9-]+)\]$")
# clang: checker names are dotted, such as core.NullDereference or unix.Malloc.
CLANG_RE = re.compile(r"^(\S[^:]*):(\d+):(\d+): warning: .*\[((?:alpha|apiModeling|core|cplusplus|deadcode|nullability|optin|security|unix|valist)\.[A-Za-z0-9_.]+)\]$")
DIAG_RE = re.compile(r"^(\S[^:]*):(\d+):(\d+): ")
DROP_WITH_ARG = {"-o", "-MF", "-MQ", "-MT"}
DROP = {"-MD", "-MMD", "-Werror", "-pipe"}


def private_dir(entry):
    out = entry.get("output")
    if not out:
        argv = entry.get("arguments") or shlex.split(entry["command"])
        out = argv[argv.index("-o") + 1] if "-o" in argv else ""
    return os.path.dirname(out)


# (file, entry, tag) once per file and set of flags, since a file built into 2
# targets can see different headers (2026102408163141). Meson gives each target
# its own -I<target>.p, which holds only objects, so that one doesn't count.
# The tag names the target when a file has more than one run.
def load_db(path):
    with open(path) as f:
        entries = json.load(f)
    runs = {}
    for e in entries:
        full = os.path.normpath(os.path.join(e["directory"], e["file"]))
        priv = private_dir(e)
        flags = tuple(a for a in analyzer_argv(e, "gcc", None) if a != "-I" + priv)
        runs.setdefault((full, e["directory"], flags), (full, e, priv))
    per_file = collections.Counter(full for full, _, _ in runs.values())
    return sorted(((full, e, priv if per_file[full] > 1 else "") for full, e, priv in runs.values()),
                  key=lambda r: (r[0], r[2]))


def analyzer_argv(entry, tool, clang):
    argv = entry.get("arguments") or shlex.split(entry["command"])
    out = []
    skip = False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if a in DROP_WITH_ARG:
            skip = True
            continue
        if a in DROP or a.startswith("-flto") or a.startswith("-fdiagnostics-color"):
            continue
        out.append(a)
    if tool == "gcc":
        return [argv[0]] + out + ["-fanalyzer", "-fdiagnostics-plain-output", "-o", "/dev/null"]
    # The mingw driver name tells clang the target; it reads the same headers.
    target = ["--target=x86_64-w64-mingw32"] if "mingw32" in os.path.basename(argv[0]) else []
    out = [a for a in out if a != "-c"]
    return [clang] + target + out + ["--analyze", "--analyzer-output", "text", "-fno-color-diagnostics",
                                     "-Wno-unknown-warning-option", "-Wno-unused-command-line-argument", "-o", "/dev/null"]


def rel(directory, path):
    full = os.path.normpath(os.path.join(directory, path))
    return full[len(SRC):] if full.startswith(SRC) else full


def run_one(full, entry, tool, clang, tag=""):
    argv = analyzer_argv(entry, tool, clang)
    # Its own group, so a timeout takes cc1 down too, not just the driver.
    p = subprocess.Popen(argv, cwd=entry["directory"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
    try:
        text = p.communicate(timeout=TU_TIMEOUT)[0]
        rc = p.returncode
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.communicate()
        rc, text = 124, "timed out after %d s\n" % TU_TIMEOUT
    lines = []
    for line in text.splitlines():
        m = DIAG_RE.match(line)
        if m:
            line = rel(entry["directory"], m.group(1)) + line[len(m.group(1)):]
        lines.append(line)
    name = rel("/", full) + (" [%s]" % tag if tag else "")
    return "### %s rc=%d\n%s" % (name, rc, "\n".join(lines) + ("\n" if lines else ""))


def cmd_run(args):
    tool = args[args.index("--tool") + 1]
    db = args[args.index("--db") + 1]
    jobs = int(args[args.index("--jobs") + 1]) if "--jobs" in args else max(1, (os.cpu_count() or 2) // 2)
    clang = None
    if tool == "clang":
        clang = shutil.which("clang") or shutil.which("clang-19")
        if not clang:
            print("### SKIP no clang in this container")
            return 77
    runs = [r for r in load_db(db) if r[0].startswith(SRC) and r[0].endswith(".c")]
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for block in pool.map(lambda r: run_one(r[0], r[1], tool, clang, r[2]), runs):
            sys.stdout.write(block)
            sys.stdout.flush()
    return 0


PLANTED = """#include <stdlib.h>
int planted_leak(int n) { int *p = malloc(4 * sizeof *p); if (!p) return -1; p[0] = n; return p[0]; }
int planted_null(int n) { int *q = 0; if (n > 1) return 0; return *q; }
"""


def cmd_planted(args):
    tool = args[args.index("--tool") + 1]
    cc = args[args.index("--cc") + 1]
    clang = None
    if tool == "clang":
        clang = shutil.which("clang") or shutil.which("clang-19")
        if not clang:
            print("### SKIP no clang in this container")
            return 77
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "planted.c"), "w") as f:
            f.write(PLANTED)
        block = run_one(os.path.join(tmp, "planted.c"),
                        {"directory": tmp, "file": "planted.c", "arguments": [cc, "-Wall", "-c", "planted.c", "-o", "planted.o"]},
                        tool, clang)
    sys.stdout.write(block)
    found = {m.group(4) for m in (GCC_RE.match(x) or CLANG_RE.match(x) for x in block.splitlines()) if m}
    want = {"gcc": {"analyzer-malloc-leak", "analyzer-null-dereference"}, "clang": {"unix.Malloc", "core.NullDereference"}}[tool]
    missing = want - found
    print("[ %s: planted faults %s ]" % (tool, "missed: " + ", ".join(sorted(missing)) if missing else "found"))
    return 1 if missing else 0


UNUSED = "source/unused/"


def check_cover(tracked, built):
    problems = []
    for f in sorted(tracked):
        if f.startswith(UNUSED):
            if f in built:
                problems.append("built from %s: %s" % (UNUSED, f))
        elif f not in built:
            problems.append("in neither compile list: %s" % f)
    return problems


def cmd_cover(args):
    root, dbs = args[0], args[1:]
    tracked = set(subprocess.run(["git", "-C", root, "ls-files", "*.c"], check=True,
                                 capture_output=True, text=True).stdout.split())
    built = set()
    for db in dbs:
        built |= {f[len(SRC):] for f, _, _ in load_db(db) if f.startswith(SRC)}
    problems = check_cover(tracked, built)
    for p in problems:
        print("[ FAILED: %s ]" % p)
    unused = {f for f in tracked if f.startswith(UNUSED)}
    print("[ %d tracked .c, %d in a compile list, %d under %s ]"
          % (len(tracked), len(tracked & built), len(unused), UNUSED))
    return 1 if problems else 0


def area_of(path, inherited):
    if path.startswith("/"):
        return "system"
    if path.startswith(VENDORED):
        return "vendored"
    if path.startswith(TESTS):
        return "tests"
    return "inherited" if path in inherited else "ours"


def parse_log(text):
    findings, failed, skipped = set(), [], False
    tu, rc, seen_error = None, 0, False
    for line in text.splitlines() + ["### end rc=0"]:
        if line.startswith("### SKIP"):
            skipped = True
            continue
        if line.startswith("### "):
            # A file that didn't compile was never analyzed, which isn't a clean result.
            if tu and rc not in (0,) and seen_error:
                failed.append(tu)
            tu, _, rcs = line[4:].rpartition(" rc=")
            rc, seen_error = int(rcs), False
            continue
        m = GCC_RE.match(line) or CLANG_RE.match(line)
        if m:
            findings.add((m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)))
        elif ": error: " in line or ": fatal error: " in line or line.startswith("timed out"):
            seen_error = True
    return findings, failed, skipped


SUPP_RE = re.compile(r"^(\S+):(\d+)\s+(\S+)\s+(\S.*)$")


# (path, line, checker) -> reason, and a problem per bad line.
def parse_supp(text):
    supp, problems = {}, []
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = SUPP_RE.match(line)
        if not m:
            problems.append("line %d: want <path>:<line> <checker> <reason>" % n)
            continue
        key = (m.group(1), int(m.group(2)), m.group(3))
        # Ours and inherited code are suppressed at the line, not here.
        if not key[0].startswith(VENDORED):
            problems.append("line %d: not vendored: %s" % (n, key[0]))
        elif key in supp:
            problems.append("line %d: listed twice: %s:%d %s" % ((n,) + key))
        else:
            supp[key] = m.group(4)
    return supp, problems


def count(logs, inherited, supp=None):
    supp = supp or {}
    table, failed, used = {}, {}, set()
    for name, text in logs.items():
        findings, bad, skipped = parse_log(text)
        row = dict.fromkeys(AREAS + ("suppressed",), 0)
        for f in findings:
            key = (f[0], f[1], f[3])
            if key in supp:
                used.add(key)
                row["suppressed"] += 1
            else:
                row[area_of(f[0], inherited)] += 1
        table[name] = None if skipped else row
        failed[name] = bad
    return table, failed, sorted(set(supp) - used)


def cmd_count(args):
    root, paths = args[0], args[1:]
    base = subprocess.run(["git", "-C", root, "rev-list", "--max-parents=0", "HEAD"], check=True,
                          capture_output=True, text=True).stdout.split()[-1]
    # The baseline commit holds upstream 6.6.4 under original/, moved to source/ right after.
    inherited = {re.sub(r"^original/", "source/", f) for f in subprocess.run(
        ["git", "-C", root, "ls-tree", "-r", "--name-only", base], check=True, capture_output=True, text=True).stdout.split()}
    with open(os.path.join(root, SUPP_FILE)) as f:
        supp, problems = parse_supp(f.read())
    for p in problems:
        print("[ FAILED: %s: %s ]" % (SUPP_FILE, p))
    logs = {}
    for p in paths:
        with open(p, errors="replace") as f:
            logs[os.path.basename(p).rsplit(".", 1)[0]] = f.read()
    table, failed, stale = count(logs, inherited, supp)
    print("%-14s %s %7s %10s %9s" % ("log", " ".join("%9s" % a for a in AREAS), "total", "suppressed", "failed"))
    for name, row in table.items():
        if row is None:
            print("%-14s not run (analyzer missing in its container)" % name)
            continue
        print("%-14s %s %7d %10d %9d" % (name, " ".join("%9d" % row[a] for a in AREAS), sum(row[a] for a in AREAS),
                                         row["suppressed"], len(failed[name])))
    for name, bad in failed.items():
        for tu in bad:
            print("not analyzed (%s): %s" % (name, tu))
    for path, line, checker in stale:
        print("stale suppression: %s:%d %s" % (path, line, checker))
    return 1 if problems else 0


def self_test():
    own = open(__file__).read()
    print("[ Test %s analyze.py --self-test ]" % re.search(r"(?m)^# Test ID: (\S+)$", own).group(1))
    bad = []
    log = "\n".join([
        "### source/src/a.c rc=1",
        "source/src/a.c:3:5: error: dereference of NULL 'p' [CWE-476] [-Werror=analyzer-null-dereference]",
        "source/test/t.c:9:1: warning: leak of 'q' [CWE-401] [-Wanalyzer-malloc-leak]",
        "vendor/x/y.c:2:9: warning: Potential leak of memory pointed to by 'r' [unix.Malloc]",
        "source/libnemo-private/old.c:4:4: warning: Value stored to 'x' is never read [deadcode.DeadStores]",
        "/usr/include/glib-2.0/glib/gmacros.h:1:1: warning: Null pointer passed [core.NonNullParamChecker]",
        "source/src/a.c:7:1: warning: unused variable 'z' [-Wunused-variable]",
        "### source/src/b.c rc=1",
        "source/src/b.c:1:10: fatal error: missing.h: No such file or directory",
        "### source/src/c.c rc=0",
    ])
    table, failed, _ = count({"gcc-linux": log}, {"source/libnemo-private/old.c"})
    want = {"vendored": 1, "inherited": 1, "ours": 1, "tests": 1, "system": 1, "suppressed": 0}
    if table["gcc-linux"] != want:
        bad.append("counts %r, want %r" % (table["gcc-linux"], want))
    if failed["gcc-linux"] != ["source/src/b.c"]:
        bad.append("not analyzed %r, want b.c only" % failed["gcc-linux"])
    if count({"x": "### SKIP no clang"}, set())[0]["x"] is not None:
        bad.append("a skipped log counted as clean")
    # The vendored finding is listed; a second entry matches nothing, by line and by checker.
    supp, problems = parse_supp("\n".join([
        "# comment",
        "vendor/x/y.c:2 unix.Malloc freed by its owner",
        "vendor/x/y.c:3 unix.Malloc gone after a bump",
        "vendor/x/y.c:2 core.NullDereference other checker",
    ]))
    table, _, stale = count({"clang-linux": log}, {"source/libnemo-private/old.c"}, supp)
    if problems or (table["clang-linux"]["vendored"], table["clang-linux"]["suppressed"]) != (0, 1):
        bad.append("suppressed counts %r, problems %r" % (table["clang-linux"], problems))
    if stale != [("vendor/x/y.c", 2, "core.NullDereference"), ("vendor/x/y.c", 3, "unix.Malloc")]:
        bad.append("stale %r" % stale)
    _, problems = parse_supp("vendor/x/y.c:2 unix.Malloc\nsource/src/a.c:3 analyzer-null-dereference ours\n"
                             "vendor/x/y.c:4 unix.Malloc a\nvendor/x/y.c:4 unix.Malloc b\n")
    if len(problems) != 3:
        bad.append("bad list lines %r, want 3 (no reason, not vendored, twice)" % problems)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", SUPP_FILE)) as f:
        problems = parse_supp(f.read())[1]
    bad += ["%s: %s" % (SUPP_FILE, p) for p in problems]
    tracked = {"a.c", "b.c", "c.c", UNUSED + "d.c", UNUSED + "e.c"}
    got = check_cover(tracked, {"a.c", "b.c", UNUSED + "e.c"})
    want_p = ["in neither compile list: c.c", "built from %s: %se.c" % (UNUSED, UNUSED)]
    if sorted(got) != sorted(want_p):
        bad.append("cover %r" % got)

    # w.c against 2 header folders runs twice; t.c, whose 3 entries differ only by
    # the target's own folder, runs once.
    def entry(src, target, *extra):
        out = "%s.p/%s.o" % (target, os.path.basename(src))
        return {"directory": "/build", "file": "../src/" + src, "output": out,
                "command": "cc -I%s.p %s -MD -MF %s.d -o %s -c ../src/%s" % (target, " ".join(extra), out, out, src)}
    db = [entry("source/test/w.c", "test/w-1", "-I../src/vendor/old/1"), entry("source/test/w.c", "test/w-2", "-I../src/vendor/old/2"),
          entry("source/test/t.c", "test/a", "-Itest"), entry("source/test/t.c", "test/b", "-Itest"),
          entry("source/test/t.c", "test/c", "-Itest"), entry("source/src/s.c", "src/app")]
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "compile_commands.json"), "w") as f:
            json.dump(db, f)
        got = [(full, tag) for full, _, tag in load_db(os.path.join(tmp, "compile_commands.json"))]
    want_r = [("/src/source/src/s.c", ""), ("/src/source/test/t.c", ""),
              ("/src/source/test/w.c", "test/w-1.p"), ("/src/source/test/w.c", "test/w-2.p")]
    if got != want_r:
        bad.append("runs %r, want %r" % (got, want_r))
    # A finding both runs reach counts once, and a run that didn't compile is named by its target.
    log = "\n".join([
        "### source/test/w.c [test/w-1.p] rc=0",
        "vendor/old/1/h.h:5:3: warning: leak of 'p' [CWE-401] [-Wanalyzer-malloc-leak]",
        "source/test/w.c:8:3: warning: leak of 'q' [CWE-401] [-Wanalyzer-malloc-leak]",
        "### source/test/w.c [test/w-2.p] rc=1",
        "source/test/w.c:8:3: warning: leak of 'q' [CWE-401] [-Wanalyzer-malloc-leak]",
        "source/test/w.c:1:10: fatal error: h.h: No such file or directory",
    ])
    table, failed, _ = count({"gcc-linux": log}, set())
    if (table["gcc-linux"]["vendored"], table["gcc-linux"]["tests"]) != (1, 1):
        bad.append("2 runs counted %r, want vendored 1, tests 1" % table["gcc-linux"])
    if failed["gcc-linux"] != ["source/test/w.c [test/w-2.p]"]:
        bad.append("not analyzed %r, want the w-2 run" % failed["gcc-linux"])
    for b in bad:
        print("[ FAILED: %s ]" % b)
    print("[ %s ]" % ("FAILED" if bad else "OK: analyze.py self-test"))
    return 1 if bad else 0


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(open(__file__).read().split("\n\n")[1].replace("# ", "").replace("#", ""))
        return 0
    if args[0] == "--self-test":
        return self_test()
    return {"run": cmd_run, "cover": cmd_cover, "count": cmd_count, "planted": cmd_planted}[args[0]](args[1:])


if __name__ == "__main__":
    sys.exit(main())
