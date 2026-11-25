#!/usr/bin/env python3

# CodeQL over both compile lists and the tree's Python, from the CLI and query
# packs pinned in the 2 build images. cicd/utility/codeql.bash drives it.
#
#   lane     Inside a build container. Makes a database for C or Python and
#            runs codeql-c.qls or codeql-python.qls over it. The SARIF goes to
#            stdout, and the CLI's own output to stderr. For C the database
#            comes from replay under CodeQL's tracer. For Python it comes from
#            a copy of the files --files lists, so build output and other
#            untracked files under the tree stay out.
#   pyfiles  On the host. Lists the tracked Python files, by extension or
#            shebang, for lane's --files, since the containers have no git.
#   replay   Compiles every C file in a compile_commands.json to /dev/null,
#            once per set of flags it's built with, as analyze.py run sees them.
#            --match keeps only the files whose path from the repo root it
#            matches, a Python regex, so a scoped run takes minutes. It fails
#            when nothing matches.
#   planted  Inside a build container. Fails unless the C queries report both
#            a planted leak and a planted null dereference, built with the
#            lane's compiler, so the tracer is shown to see it. Fails too on
#            a report at the planted GLib idioms the NULL query is taught,
#            or at a format from a translation call, or none at a NULL passed
#            to a function that's NULL only for a NULL argument, or at a
#            format from a function with no body.
#   count    Counts results per area from the lanes' SARIF, as analyze.py does
#            for the analyzers. A vendored result listed in codeql-vendored.txt
#            counts as suppressed. Exits 1 on any result left, 2 on SARIF it
#            can't read, so a crash doesn't read as findings, and 3 on a bad
#            line in the list, or an entry no result matches. --no-stale skips
#            that last check, for a scoped run.
#
# Exit 77 means no codeql in the container.
# --self-test runs count over made-up SARIF.
# Test ID: dfha3
#
# Syntax: codeql.py lane --lang c|python [--db <compile_commands.json>] [--match <regex>] [--files <list>] [--jobs N] [--ram MB]
#         codeql.py pyfiles <repo root>
#         codeql.py replay --db <compile_commands.json> [--match <regex>] [--jobs N]
#         codeql.py planted [--cc <compiler>] [--jobs N] [--ram MB]
#         codeql.py count [--supp <list>] [--no-stale] <repo root> <sarif>...
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
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SUITES = {"c": os.path.join(HERE, "codeql-c.qls"), "python": os.path.join(HERE, "codeql-python.qls")}
LANGS = {"c": "cpp", "python": "python"}
SUPP_FILE = "cicd/utility/codeql-vendored.txt"
# Our own queries, such as missing-null-test taught about GLib.
PACK = os.path.join(HERE, "codeql-cpp")
MATCH_ENV = "CODEQL_REPLAY_MATCH"
PLANTED_WANT = {"cpp/memory-never-freed", "cpp/missing-null-test"}
PLANTED = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int planted_leak(int n) { char *p = malloc(16); if (!p) return -1; memset(p, n, 16); return p[0]; }
int planted_null(int n) { int *q = 0; if (n > 1) return 0; return *q; }
void g_assertion_message (const char *what);
static inline char *g_strdup_inline (const char *s) { if (!s) return 0; return strdup (s); }
#define g_return_val_if_fail(e, v) do { if (!(e)) return (v); } while (0)
#define g_assert_nonnull(e) do { if (({ int b_; if ((e) != 0) b_ = 1; else b_ = 0; b_; })) ; else g_assertion_message (#e); } while (0)
char *planted_get (const char *s) { g_return_val_if_fail (s != 0, 0); return strdup (s); }
char *planted_trunc (const char *s) { if (s == 0) return 0; return strdup (s); }
int planted_trunc_null (void) { char *t = planted_trunc (0); return t[0]; /* loud */ }
int planted_glib (const char *s) {
	char *a = g_strdup_inline (s), *b = planted_get (s), *c = 0;
	char *d = planted_trunc (s);
	int n = a[0] + b[0] + d[0]; /* quiet */
	if (n > 1) c = a;
	g_assert_nonnull (c);
	return n + c[0]; /* quiet */
}
char *libintl_ngettext (const char *one, const char *many, unsigned long n);
const char *g_dpgettext (const char *domain, const char *msgctxtid, unsigned long msgidoffset);
char *planted_unknown (void);
void planted_format (unsigned long n) {
	printf (libintl_ngettext ("%lu file\\n", "%lu files\\n", n), n); /* quiet */
	printf (g_dpgettext (0, "Count\\004%lu\\n", 6), n); /* quiet */
	printf (planted_unknown (), n); /* loud format */
}
void *g_malloc (unsigned long n);
void g_free (void *p);
void g_ptr_array_add (void *array, void *data);
void g_autoptr_cleanup_generic_gfree (void *p) { g_free (*(void **) p); }
#define g_autofree __attribute__ ((cleanup (g_autoptr_cleanup_generic_gfree)))
#define g_new_opt(n) ((int *) ({ unsigned long n_ = (unsigned long) (n); void *p_; p_ = g_malloc (n_ * sizeof (int)); p_; }))
int planted_autofree (int n) {
	g_autofree char *p = g_malloc (16); /* quiet */
	memset (p, n, 16);
	return p[0];
}
int planted_gnew (int n) {
	int *v = g_new_opt (n); /* quiet */
	v[0] = n;
	n = v[0];
	g_free (v);
	return n;
}
int planted_gnew_leak (int n) {
	int *v = g_new_opt (n); /* loud leak */
	v[0] = n;
	return v[0];
}
void planted_taken (void *array) { char *s = g_malloc (8); g_ptr_array_add (array, s); } /* quiet */
"""
# Lines GLib's idioms make look like a NULL dereference, which
# codeql-cpp/MissingNullTest.ql is taught to pass, formats from the
# translation calls codeql-cpp/NonConstantFormat.ql is taught, and GLib's
# frees codeql-cpp/MemoryNeverFreed.ql is taught.
PLANTED_QUIET = {i + 1 for i, line in enumerate(PLANTED.splitlines()) if line.endswith("/* quiet */")}
# And a NULL argument to a function that's NULL only for a NULL argument, a
# format from a function it knows nothing about, and an optimized g_new never
# freed, which they still have to report.
PLANTED_LOUD = {i + 1: rule for i, line in enumerate(PLANTED.splitlines())
                for mark, rule in (("/* loud */", "cpp/missing-null-test"), ("/* loud format */", "cpp/non-constant-format"),
                                   ("/* loud leak */", "cpp/memory-never-freed"))
                if mark in line}

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
            f"--threads={jobs}", f"--ram={ram}", f"--additional-packs={PACK}"])


def cmd_lane(args: list[str]) -> int:
    lang, jobs, ram = opt(args, "--lang", "c"), opt(args, "--jobs", "1"), opt(args, "--ram", "8192")
    if not shutil.which("codeql"):
        print("### SKIP no codeql in this container", file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix="codeql-") as tmp:
        cmd, src = None, analyze.SRC.rstrip("/")
        if lang == "python" and "--files" in args:
            src = os.path.join(tmp, "src")
            with open(opt(args, "--files", ""), encoding="utf-8") as f:
                listed = [line for line in f.read().splitlines() if line]
            if not listed:
                print("[ FAILED: no Python files listed ]", file=sys.stderr)
                return 1
            for rel in listed:
                os.makedirs(os.path.dirname(os.path.join(src, rel)), exist_ok=True)
                shutil.copy2(os.path.join(analyze.SRC, rel), os.path.join(src, rel))
        if lang == "c":
            cmd = shlex.join([sys.executable, os.path.abspath(__file__), "replay", "--db", opt(args, "--db", "/build/compile_commands.json"),
                              "--jobs", jobs])
            # codeql expands $ in --command itself, so a regex goes to replay through the environment.
            if "--match" in args:
                os.environ[MATCH_ENV] = opt(args, "--match", "")
        make_db(lang, os.path.join(tmp, "db"), src, jobs, ram, cmd)
        analyze_db(lang, os.path.join(tmp, "db"), os.path.join(tmp, "out.sarif"), jobs, ram)
        with open(os.path.join(tmp, "out.sarif"), encoding="utf-8") as f:
            sys.stdout.write(f.read())
    return 0


def is_python(path: str) -> bool:
    if path.endswith(".py"):
        return True
    try:
        with open(path, "rb") as f:
            first = f.readline(200)
    except OSError:
        return False
    return first.startswith(b"#!") and b"python" in first


def tracked_python(root: str) -> list[str]:
    out = subprocess.run(["git", "-C", root, "ls-files", "-z"], check=True, capture_output=True).stdout.decode()
    return [f for f in out.split("\0") if f and os.path.isfile(os.path.join(root, f)) and is_python(os.path.join(root, f))]


def cmd_pyfiles(args: list[str]) -> int:
    files = tracked_python(args[0])
    print("\n".join(files))
    return 0 if files else 1


def pick_runs(runs: list[analyze.Run], match: str | None) -> list[analyze.Run]:
    if match is None:
        return runs
    pat = re.compile(match)
    return [r for r in runs if pat.search(r[0][len(analyze.SRC):] if r[0].startswith(analyze.SRC) else r[0])]


def cmd_replay(args: list[str]) -> int:
    match = opt(args, "--match", "") if "--match" in args else os.environ.get(MATCH_ENV)
    runs = pick_runs(analyze.load_db(opt(args, "--db", "/build/compile_commands.json")), match)
    if not runs:
        print(f"[ FAILED: no compile entry matches {match!r} ]", file=sys.stderr)
        return 1

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
    loud = sorted(line for path, line, rule, _ in found if path.endswith("planted.c") and line in PLANTED_QUIET)
    hushed = sorted(set(PLANTED_LOUD) - {line for path, line, rule, _ in found if path.endswith("planted.c") and PLANTED_LOUD.get(line) == rule})
    print(f"[ CodeQL: planted faults {'missed: ' + ', '.join(sorted(missing)) if missing else 'found'} ]")
    if loud:
        print(f"[ CodeQL: planted GLib and gettext idioms reported at line {', '.join(map(str, loud))} ]")
    if hushed:
        print(f"[ CodeQL: planted NULL argument, unknown format or leak not reported at line {', '.join(map(str, hushed))} ]")
    return 1 if missing or loud or hushed else 0


# Counts per lane, results left per lane and rule, and the list's entries no
# result matched.
def count(sarifs: dict[str, list[Result]], inherited: set[str], listed: dict[analyze.SuppKey, str] | None = None) -> tuple[
        dict[str, collections.Counter[str]], collections.Counter[str], list[analyze.SuppKey]]:
    table, rules, used, listed = {}, collections.Counter(), set(), listed or {}
    for name, found in sarifs.items():
        row = collections.Counter({a: 0 for a in analyze.AREAS})
        for path, line, rule, supp in sorted(set(found)):
            if (path, line, rule) in listed:
                used.add((path, line, rule))
                supp = True
            if supp:
                row["suppressed"] += 1
                continue
            row[analyze.area_of(path, inherited)] += 1
            rules[f"{name} {rule}"] += 1
        table[name] = row
    return table, rules, sorted(set(listed) - used)


def cmd_count(args: list[str], inherited: set[str] | None = None) -> int:
    supp_file, no_stale = None, False
    while args and args[0] in ("--supp", "--no-stale"):
        if args[0] == "--supp":
            supp_file, args = args[1], args[2:]
        else:
            no_stale, args = True, args[1:]
    root, paths = args[0], args[1:]
    inherited = analyze.inherited_files(root) if inherited is None else inherited
    supp_file = supp_file or os.path.join(root, SUPP_FILE)
    try:
        with open(supp_file, encoding="utf-8") as f:
            listed, problems = analyze.parse_supp(f.read())
    except OSError as e:
        print(f"[ FAILED: can't read {supp_file}: {e} ]")
        return 3
    sarifs = {}
    for p in paths:
        try:
            with open(p, encoding="utf-8") as f:
                sarifs[os.path.basename(p).rsplit(".", 1)[0]] = results(json.load(f))
        except (OSError, ValueError, KeyError, IndexError, TypeError) as e:
            print(f"[ FAILED: can't read {p}: {e} ]")
            return 2
    table, rules, stale = count(sarifs, inherited, listed)
    # A scoped run reaches only some files, so most entries match nothing there.
    stale = [] if no_stale else stale
    heads = " ".join(f"{a:>9}" for a in analyze.AREAS)
    print(f"{'lane':<14} {heads} {'total':>7} {'suppressed':>10}")
    for name, row in table.items():
        cells = " ".join(f"{row[a]:9d}" for a in analyze.AREAS)
        print(f"{name:<14} {cells} {sum(row[a] for a in analyze.AREAS):7d} {row['suppressed']:10d}")
    for rule, n in sorted(rules.items(), key=lambda x: (-x[1], x[0])):
        print(f"{n:6d}  {rule}")
    for p in problems:
        print(f"[ FAILED: {supp_file}: {p} ]")
    for path, line, rule in stale:
        print(f"[ FAILED: stale entry in {os.path.basename(supp_file)}: {path}:{line} {rule} ]")
    return 3 if problems or stale else 1 if rules else 0


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
        res("vendor/shcl/shcl.h", 30, "cpp/missing-null-test"),
    ]}]}
    listed = "# comment\nvendor/shcl/shcl.h:30 cpp/missing-null-test Set on every path that reaches it.\n"
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        good, broken = os.path.join(tmp, "c-linux.sarif"), os.path.join(tmp, "python.sarif")
        with open(good, "w", encoding="utf-8") as f:
            json.dump(sarif, f)
        supp, problems = analyze.parse_supp(listed)
        table, rules, stale = count({"c-linux": results(sarif)}, {"source/src/nemo-file.c"}, supp)
        row = table["c-linux"]
        want = {"inherited": 1, "ours": 2, "vendored": 1, "tests": 1, "system": 0, "suppressed": 2}
        if problems or stale or {k: row[k] for k in want} != want:
            bad.append(f"counts {dict(row)}, want {want}, with the listed one suppressed")
        if rules["c-linux cpp/memory-never-freed"] != 2 or rules["c-linux cpp/missing-null-test"] != 1:
            bad.append(f"rule counts {dict(rules)}")
        with open(broken, "w", encoding="utf-8") as f:
            f.write('{"runs": [{"results": [{"ruleId": "x"}]}]}')
        lists = {}
        for name, text in (("ok", listed), ("empty", ""),
                           # A line it doesn't match, and an entry outside vendored code.
                           ("bad", listed + "vendor/shcl/shcl.h:31\nsource/src/nemo-file.c:10 cpp/missing-null-test Fine.\n"),
                           ("stale", listed + "vendor/shcl/shcl.h:30 cpp/memory-never-freed Another rule there.\n")):
            lists[name] = os.path.join(tmp, f"{name}.txt")
            with open(lists[name], "w", encoding="utf-8") as f:
                f.write(text)
        clean = os.path.join(tmp, "clean.sarif")
        with open(clean, "w", encoding="utf-8") as f:
            json.dump({"runs": [{"results": [r for r in sarif["runs"][0]["results"] if r.get("suppressions")]}]}, f)
        cases = (
            ("findings", ["--supp", lists["ok"], tmp, good], 1),
            ("SARIF it can't read", ["--supp", lists["ok"], tmp, good, broken], 2),
            ("a bad line in the list", ["--supp", lists["bad"], tmp, good], 3),
            ("a stale entry", ["--supp", lists["stale"], tmp, good], 3),
            ("a stale entry under --no-stale", ["--supp", lists["stale"], "--no-stale", tmp, good], 1),
            ("no findings left", ["--supp", lists["empty"], tmp, clean], 0),
            ("a list it can't read", ["--supp", os.path.join(tmp, "none.txt"), tmp, clean], 3),
        )
        for what, argv, want_rc in cases:
            with contextlib.redirect_stdout(io.StringIO()):
                rc = cmd_count(argv, set())
            if rc != want_rc:
                bad.append(f"count exited {rc} on {what}, want {want_rc}")
        for name, text in (("a.py", ""), ("tool", "#!/usr/bin/env python3\n"), ("run.bash", "#!/usr/bin/env bash\n"), ("notes", "python\n")):
            with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                f.write(text)
        picked = sorted(n for n in ("a.py", "tool", "run.bash", "notes", "missing") if is_python(os.path.join(tmp, n)))
        if picked != ["a.py", "tool"]:
            bad.append(f"Python files picked {picked}, want a.py and tool")
    runs = [("/src/source/src/a.c", {}, ""), ("/src/source/test/b.c", {}, ""), ("/src/vendor/c.c", {}, "")]
    if [r[0] for r in pick_runs(runs, r"^source/(src|test)/")] != ["/src/source/src/a.c", "/src/source/test/b.c"]:
        bad.append("--match picked the wrong entries")
    if pick_runs(runs, None) != runs or pick_runs(runs, "^nothing") != []:
        bad.append("--match not given should keep every entry, and one matching nothing none")
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
    return {"lane": cmd_lane, "replay": cmd_replay, "planted": cmd_planted, "count": cmd_count,
            "pyfiles": cmd_pyfiles}[args[0]](args[1:])


if __name__ == "__main__":
    sys.exit(main())
