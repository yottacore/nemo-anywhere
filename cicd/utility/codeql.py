#!/usr/bin/env python3

# CodeQL over both compile lists and the tree's Python, from the CLI and query
# packs pinned in the 2 build images. cicd/utility/codeql.bash drives it.
#
#   lane     Inside a build container. Makes a database for C or Python and
#            runs codeql-c.qls or codeql-python.qls over it. The SARIF goes to
#            stdout, and the CLI's own output to stderr. For C the database
#            comes from replay under CodeQL's tracer.
#   replay   Compiles every C file in a compile_commands.json to /dev/null,
#            once per set of flags it's built with, as analyze.py run sees them.
#   planted  Inside a build container. Fails unless the C queries report both
#            a planted leak and a planted null dereference, built with the
#            lane's compiler, so the tracer is shown to see it.
#   count    Counts results per area from the lanes' SARIF, as analyze.py does
#            for the analyzers. Exits 1 on any result left, and 2 on SARIF it
#            can't read, so a crash doesn't read as findings.
#
# Exit 77 means no codeql in the container.
# --self-test runs count over made-up SARIF.
# Test ID: dfha3
#
# Syntax: codeql.py lane --lang c|python [--db <compile_commands.json>] [--jobs N] [--ram MB]
#         codeql.py replay --db <compile_commands.json> [--jobs N]
#         codeql.py planted [--cc <compiler>] [--jobs N] [--ram MB]
#         codeql.py count <repo root> <sarif>...
#         codeql.py --self-test

# Copyright (c) 2026 t00mietum
# Licensed under the GNU General Public License, version 2 only.
# SPDX-License-Identifier: GPL-2.0-only

import collections
import concurrent.futures
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SUITES = {"c": os.path.join(HERE, "codeql-c.qls"), "python": os.path.join(HERE, "codeql-python.qls")}
LANGS = {"c": "cpp", "python": "python"}
PLANTED_WANT = {"cpp/memory-never-freed", "cpp/missing-null-test"}
PLANTED = """#include <stdlib.h>
#include <string.h>
int planted_leak(int n) { char *p = malloc(16); if (!p) return -1; memset(p, n, 16); return p[0]; }
int planted_null(int n) { int *q = 0; if (n > 1) return 0; return *q; }
"""

Result = tuple[str, int, str, bool]


def opt(args: list[str], name: str, default: str) -> str:
    return args[args.index(name) + 1] if name in args else default


def codeql(argv: list[str]) -> None:
    subprocess.run(argv, check=True, stdout=sys.stderr)


def make_db(lang: str, dbdir: str, src: str, jobs: str, ram: str, command: str | None) -> None:
    argv = ["codeql", "database", "create", dbdir, f"--language={LANGS[lang]}", f"--source-root={src}",
            f"--threads={jobs}", f"--ram={ram}"]
    codeql(argv + (["--command", command] if command else []))


def analyze_db(lang: str, dbdir: str, out: str, jobs: str, ram: str) -> None:
    codeql(["codeql", "database", "analyze", dbdir, SUITES[lang], "--format=sarif-latest", f"--output={out}",
            f"--threads={jobs}", f"--ram={ram}"])


def cmd_lane(args: list[str]) -> int:
    lang, jobs, ram = opt(args, "--lang", "c"), opt(args, "--jobs", "1"), opt(args, "--ram", "8192")
    if not shutil.which("codeql"):
        print("### SKIP no codeql in this container", file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix="codeql-") as tmp:
        cmd = None
        if lang == "c":
            cmd = f"{sys.executable} {os.path.abspath(__file__)} replay --db {opt(args, '--db', '/build/compile_commands.json')} --jobs {jobs}"
        make_db(lang, os.path.join(tmp, "db"), analyze.SRC.rstrip("/"), jobs, ram, cmd)
        analyze_db(lang, os.path.join(tmp, "db"), os.path.join(tmp, "out.sarif"), jobs, ram)
        with open(os.path.join(tmp, "out.sarif"), encoding="utf-8") as f:
            sys.stdout.write(f.read())
    return 0


def cmd_replay(args: list[str]) -> int:
    runs = analyze.load_db(opt(args, "--db", "/build/compile_commands.json"))

    def one(r: analyze.Run) -> tuple[str, int, str]:
        full, e, _ = r
        argv = [a for a in analyze.analyzer_argv(e, "gcc", None) if a not in ("-fanalyzer", "-fdiagnostics-plain-output")]
        p = subprocess.run(argv, cwd=e["directory"], capture_output=True, text=True)
        return full, p.returncode, p.stderr

    bad = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(opt(args, "--jobs", "1"))) as pool:
        for full, rc, err in pool.map(one, runs):
            if rc:
                bad += 1
                sys.stderr.write(err)
                print(f"[ FAILED: did not compile: {full} ]", file=sys.stderr)
    print(f"[ {len(runs) - bad} of {len(runs)} compiled ]", file=sys.stderr)
    return 1 if bad else 0


def results(sarif: dict) -> list[Result]:
    out = []
    for run in sarif["runs"]:
        for r in run.get("results", []):
            loc = r["locations"][0]["physicalLocation"]
            uri = loc["artifactLocation"]["uri"]
            path = uri[len("file://"):] if uri.startswith("file://") else uri
            path = path[len(analyze.SRC):] if path.startswith(analyze.SRC) else path
            out.append((path, loc.get("region", {}).get("startLine", 0), r["ruleId"], bool(r.get("suppressions"))))
    return out


def cmd_planted(args: list[str]) -> int:
    jobs, ram, cc = opt(args, "--jobs", "1"), opt(args, "--ram", "4096"), opt(args, "--cc", "gcc")
    if not shutil.which("codeql"):
        print("### SKIP no codeql in this container")
        return 77
    with tempfile.TemporaryDirectory(prefix="codeql-planted-") as tmp:
        src = os.path.join(tmp, "src")
        os.mkdir(src)
        with open(os.path.join(src, "planted.c"), "w", encoding="utf-8") as f:
            f.write(PLANTED)
        make_db("c", os.path.join(tmp, "db"), src, jobs, ram, f"{cc} -Wall -c planted.c -o /dev/null")
        analyze_db("c", os.path.join(tmp, "db"), os.path.join(tmp, "out.sarif"), jobs, ram)
        with open(os.path.join(tmp, "out.sarif"), encoding="utf-8") as f:
            found = results(json.load(f))
    for path, line, rule, _ in found:
        print(f"{path}:{line}: {rule}")
    missing = PLANTED_WANT - {rule for path, _, rule, _ in found if path.endswith("planted.c")}
    print(f"[ CodeQL: planted faults {'missed: ' + ', '.join(sorted(missing)) if missing else 'found'} ]")
    return 1 if missing else 0


def count(sarifs: dict[str, list[Result]], inherited: set[str]) -> tuple[dict[str, collections.Counter[str]], collections.Counter[str]]:
    table, rules = {}, collections.Counter()
    for name, found in sarifs.items():
        row = collections.Counter({a: 0 for a in analyze.AREAS})
        for path, line, rule, supp in sorted(set(found)):
            if supp:
                row["suppressed"] += 1
                continue
            row[analyze.area_of(path, inherited)] += 1
            rules[f"{name} {rule}"] += 1
        table[name] = row
    return table, rules


def cmd_count(args: list[str], inherited: set[str] | None = None) -> int:
    root, paths = args[0], args[1:]
    inherited = analyze.inherited_files(root) if inherited is None else inherited
    sarifs = {}
    for p in paths:
        try:
            with open(p, encoding="utf-8") as f:
                sarifs[os.path.basename(p).rsplit(".", 1)[0]] = results(json.load(f))
        except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
            print(f"[ FAILED: can't read {p}: {e} ]")
            return 2
    table, rules = count(sarifs, inherited)
    heads = " ".join(f"{a:>9}" for a in analyze.AREAS)
    print(f"{'lane':<14} {heads} {'total':>7} {'suppressed':>10}")
    for name, row in table.items():
        cells = " ".join(f"{row[a]:9d}" for a in analyze.AREAS)
        print(f"{name:<14} {cells} {sum(row[a] for a in analyze.AREAS):7d} {row['suppressed']:10d}")
    for rule, n in sorted(rules.items(), key=lambda x: (-x[1], x[0])):
        print(f"{n:6d}  {rule}")
    return 1 if rules else 0


def self_test() -> int:
    def res(uri: str, line: int, rule: str, supp: bool = False) -> dict:
        r = {"ruleId": rule, "locations": [{"physicalLocation": {"artifactLocation": {"uri": uri, "uriBaseId": "%SRCROOT%"},
                                                                 "region": {"startLine": line}}}]}
        return r | ({"suppressions": [{"kind": "inSource"}]} if supp else {})
    sarif = {"runs": [{"results": [
        res("source/src/nemo-file.c", 10, "cpp/missing-null-test"),
        res("source/src/nemo-file.c", 10, "cpp/missing-null-test"),   # the same one twice counts once
        res("source/src/nemo-new.c", 3, "cpp/memory-never-freed"),
        res("vendor/blake3/blake3.c", 7, "cpp/memory-never-freed"),
        res("source/test/test-x.c", 2, "cpp/unused-local-variable"),
        res("file:///src/cicd/utility/x.py", 4, "py/unused-import"),
        res("source/src/nemo-file.c", 20, "cpp/missing-null-test", supp=True),
    ]}]}
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        good, broken = os.path.join(tmp, "c-linux.sarif"), os.path.join(tmp, "python.sarif")
        with open(good, "w", encoding="utf-8") as f:
            json.dump(sarif, f)
        table, rules = count({"c-linux": results(sarif)}, {"source/src/nemo-file.c"})
        row = table["c-linux"]
        want = {"inherited": 1, "ours": 2, "vendored": 1, "tests": 1, "system": 0, "suppressed": 1}
        if {k: row[k] for k in want} != want:
            bad.append(f"counts {dict(row)}, want {want}")
        if rules["c-linux cpp/memory-never-freed"] != 2:
            bad.append(f"rule counts {dict(rules)}")
        with open(broken, "w", encoding="utf-8") as f:
            f.write('{"runs": [{"results": [{"ruleId": "x"}]}]}')
        with contextlib.redirect_stdout(io.StringIO()):
            rc_found = cmd_count([tmp, good], set())
            rc_broken = cmd_count([tmp, good, broken], set())
        if rc_found != 1:
            bad.append(f"count exited {rc_found} on findings, want 1")
        if rc_broken != 2:
            bad.append(f"count exited {rc_broken} on SARIF it can't read, want 2")
    for b in bad:
        print(f"[ FAILED: {b} ]")
    print(f"[ {'FAILED' if bad else 'OK: codeql.py self-test'} ]")
    return 1 if bad else 0


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        with open(__file__, encoding="utf-8") as f:
            print(f.read().split("\n\n")[1].replace("# ", "").replace("#", ""))
        return 0
    if args[0] == "--self-test":
        return self_test()
    return {"lane": cmd_lane, "replay": cmd_replay, "planted": cmd_planted, "count": cmd_count}[args[0]](args[1:])


if __name__ == "__main__":
    sys.exit(main())
