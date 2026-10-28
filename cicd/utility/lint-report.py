#!/usr/bin/env python3

##	Purpose: List the warning and error lines in the newest run log under
##		cicd/artifacts/lint that no older log there has. Counters, times, pids
##		and temp names are evened out first, so a warning that only moved or
##		came from another run still counts as the same one. Runs two ways:
##		plain (print them) and --check (print them only when the newest log is
##		newer than the one last recorded in a local marker, then record it -
##		meant for session startup beside flame-report.py --check).
##	Syntax: lint-report.py [--dir DIR | --file LOG] [--check [--force] [--no-mark]]
##		lint-report.py --self-test
##	Test ID: dehzr
##	History: At bottom of script.

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


import argparse, os, re, sys, tempfile

SEEN_FILE = ".lint-seen"  # marker basename, kept in the lint dir - outside the git repo
NAME_RE   = re.compile(r"run_(\d{8}-\d{6})(_\w+)?\.log$")
## Whole words only, so -Werror, halt_on_error=1 and "errors test" stay out.
FLAG_RE   = re.compile(r"(?i)\b(warn(ing)?s?|error|deprecat\w*|critical|fatal)\b")
EVEN_OUT  = [
    (re.compile(r"\x1b\[[0-9;]*m"), ""),
    (re.compile(r"/var/folders/[^/\s]+/[^/\s]+/T/"), "$TMP/"),
    (re.compile(r"\btmp\.[A-Za-z0-9]{6,}"), "tmp.X"),
    (re.compile(r"-[A-Za-z0-9]{6}(?=/)"), "-X"),
    (re.compile(r"\d+"), "N"),
]


def even_out(line):
    for pat, rep in EVEN_OUT:
        line = pat.sub(rep, line)
    return line.strip()


def flagged(path):
    """Evened-out flagged lines of one log, in first-seen order."""
    out = {}
    with open(path, errors="replace") as f:
        for line in f:
            if FLAG_RE.search(line):
                out.setdefault(even_out(line), None)
    return list(out)


def logs_in(d):
    """(stamp, name) of every run log, oldest first."""
    found = []
    for name in os.listdir(d):
        m = NAME_RE.match(name)
        if m:
            found.append((m.group(1), name))
    return sorted(found)


def unseen(d, newest):
    """Flagged lines of one log that no log older than it has."""
    m = NAME_RE.match(newest)
    key = (m.group(1) if m else "99999999-999999", newest)
    older = set()
    for stamp, name in logs_in(d):
        if (stamp, name) < key:
            older.update(flagged(os.path.join(d, name)))
    return [line for line in flagged(os.path.join(d, newest)) if line not in older]


def skip(msg):
    sys.stderr.write(f"lint-report: {msg}\n")
    sys.exit(0)


def self_test():
    print("[ Test %s lint-report.py --self-test ]" % re.search(r"(?m)^##\tTest ID: (\S+)$", open(__file__).read()).group(1))
    failures = []
    with tempfile.TemporaryDirectory() as d:
        def log(name, text):
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        log("run_20261001-100000_day.log",
            "[12/700] Compiling\n(test-a:4242): Gtk-WARNING **: 10:00:01.123: old one\n"
            "cc: warning: unused x at foo.c:10\n")
        log("run_20261002-100000_week.log", "    werror: true\n")
        log("run_20261003-100000.log",
            "(test-a:9999): Gtk-WARNING **: 11:22:33.444: old one\n"
            "cc: warning: unused x at foo.c:11\n"
            "-Werror halt_on_error=1 errors test OK\n"
            "x: deprecated thing in /tmp/tmp.Ab12Cd34/f-Zq9xYw/g\n"
            "x: deprecated thing in /tmp/tmp.Ab12Cd34/f-Zq9xYw/g\n"
            "ERROR: brand new\n")
        got = unseen(d, logs_in(d)[-1][1])
        want = ["x: deprecated thing in /tmp/tmp.X/f-X/g", "ERROR: brand new"]
        if got != want:
            failures.append(f"unseen gave {got}")
        if len(unseen(d, "run_20261001-100000_day.log")) != 2:
            failures.append("the oldest log was read against newer ones")
        if logs_in(d)[-1][1] != "run_20261003-100000.log":
            failures.append("newest log picked by name, not stamp")
    for failure in failures:
        print(f"[ FAIL: lint-report self-test: {failure} ]")
    if failures:
        print(f"[ FAILED: lint-report self-test, {len(failures)} problem(s) ]")
        return 1
    print("[ OK: lint-report self-test ]")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    here = os.path.dirname(os.path.abspath(__file__))
    default_dir = os.path.normpath(os.path.join(here, "..", "artifacts", "lint"))

    ap = argparse.ArgumentParser(description="List warnings in the newest run log that no older one has.")
    ap.add_argument("--dir", default=default_dir, help="lint log directory (default: %(default)s)")
    ap.add_argument("--file", help="read this log instead of the newest in --dir; older ones come from its own dir")
    ap.add_argument("--check", action="store_true",
                    help="startup gate: print only if newer than the local marker, then record it")
    ap.add_argument("--force", action="store_true", help="with --check, report even if already seen")
    ap.add_argument("--no-mark", action="store_true", help="with --check, do not update the marker")
    a = ap.parse_args()

    if a.file:
        if not os.path.isfile(a.file):
            skip(f"no such file: {a.file}")
        a.dir, name = os.path.split(os.path.abspath(a.file))
        m = NAME_RE.match(name)
        ts = m.group(1) if m else ""
    else:
        if not os.path.isdir(a.dir):
            skip(f"no lint dir: {a.dir}")
        found = logs_in(a.dir)
        if not found:
            skip(f"no run logs in {a.dir}")
        ts, name = found[-1]

    marker = os.path.join(a.dir, SEEN_FILE)
    if a.check and not a.force:
        seen = ""
        try:
            seen = open(marker).read().strip()
        except OSError:
            pass
        if ts and seen and ts <= seen:
            print(f"SEEN {name}  (nothing newer than {seen})")
            return 0

    lines = unseen(a.dir, name)
    print(f"{'NEW' if a.check else 'LINT'} {name}  ({len(lines)} warning line(s) no older log has)")
    for line in lines:
        print(f"  {line}")

    if a.check and not a.no_mark and ts:
        try:
            with open(marker, "w") as f:
                f.write(ts + "\n")
        except OSError as e:
            sys.stderr.write(f"lint-report: could not write marker: {e}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())


##	History:
##		- 20261028: Created.
