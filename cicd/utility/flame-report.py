#!/usr/bin/env python3

##	Purpose: Summarize the newest profiler flamegraph: self-time hot spots,
##		inclusive call buckets, and the caller chain of the top leaves. It parses
##		the pprof/inferno SVG, whose fg:w attribute is the raw sample count.
##		Plain prints the report, and is meant for every cicd run. --check prints
##		only when the newest flamegraph is newer than the one last recorded in a
##		local marker, then records it. That's meant for session startup, so a
##		look is a no-op until there's something new to read.
##	History: At bottom of script.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


import argparse, html, os, re, sys

STEP      = 16             # flamegraph row height in the SVG, px; a child is at parent_y - STEP
SELF_TOP  = 22             # self-time leaders to list
INCL_TOP  = 14             # inclusive-time buckets to list
CHAIN     = 4              # leaves whose caller chain we walk up to the root
SEEN_FILE = ".flame-seen"  # marker basename, kept in the profiling dir - outside the git repo

NAME_RE   = re.compile(r"flame_(\d{8}-\d{6})_\w+\.svg$")
FRAME_RE  = re.compile(r"<title>(.*?)</title><rect ([^>]*?)/>", re.S)
##	One flamegraph has tens of thousands of frames, so these are compiled once
##	rather than rebuilt per frame.
ATTR_RE   = {k: re.compile(re.escape(k) + r'="([\d.]+)"') for k in ("y", "fg:x", "fg:w")}
SAMPLES_RE = re.compile(r"\s*\(\d[\d,]* samples.*$")


def skip(msg):
    ##	2 = environmental skip, such as no dir or an unparseable file. Non-fatal,
    ##	to match the cicd profiler stage, which treats these as a warning.
    sys.stderr.write(f"flame-report: {msg}\n")
    sys.exit(2)


def newest_report(pdir):
    ##	Sort on the timestamp rather than the role suffix. GFS rotation retags
    ##	the role, frequent -> latest -> hour/day/..., as time passes, but the
    ##	timestamp in the name is stable.
    best = None
    for name in os.listdir(pdir):
        m = NAME_RE.match(name)
        if m and (best is None or m.group(1) > best[0]):
            best = (m.group(1), name)
    return best


def parse_flamegraph(path):
    text = open(path, encoding="utf-8").read()
    m = re.search(r'total_samples="(\d+)"', text)
    total = int(m.group(1)) if m else 0
    frames = []                                      # each: (name, x, y, w) in raw samples
    for fm in FRAME_RE.finditer(text):
        attrs = fm.group(2)
        found = [ATTR_RE[k].search(attrs) for k in ("y", "fg:x", "fg:w")]
        if None in found:
            continue
        y, x, w = (float(vm.group(1)) for vm in found)
        name = SAMPLES_RE.sub("", html.unescape(fm.group(1)))
        frames.append((name, x, y, w))
    if not total or not frames:
        skip(f"couldn't parse a flamegraph out of {path}")
    return total, frames


def report_busy_time(total, frames, top):
    byY = {}
    for fr in frames:
        byY.setdefault(fr[2], []).append(fr)
    eps = 1e-6

    def kids(fr):
        _, x, y, w = fr
        return [c for c in byY.get(y - STEP, []) if c[1] >= x - eps and c[1] + c[3] <= x + w + eps]

    def parent(fr):
        _, x, y, w = fr
        for p in byY.get(y + STEP, []):
            if p[1] <= x + eps and p[1] + p[3] >= x + w - eps:
                return p
        return None

    def self_width(fr):
        return fr[3] - sum(c[3] for c in kids(fr))

    ##	Buckets for this app's hot subsystems. Matched on the ancestor chain, so a
    ##	leaf deep inside cairo still counts under drawing. Order matters: waiting is
    ##	tested first because a blocked thread's stack also passes through GLib.
    BUCKETS = [
        ("waiting",   ("poll", "epoll_wait", "g_cond_wait", "futex", "pthread_cond",
                       "g_main_context_iterate", "__select")),
        ("thumbnails",("thumbnail", "gdk_pixbuf", "pixbuf_loader", "_pixbuf_")),
        ("loading",   ("nemo_directory", "nemo_file_", "g_file_", "g_local_file",
                       "enumerate", "query_info", "g_io_")),
        ("drawing",   ("gtk_", "gdk_", "cairo_", "pango_", "_gtk_", "render", "draw")),
        ("settings",  ("nemo_config", "shcl_", "metadata_store")),
    ]

    selfBy, inclBy, byName = {}, {}, {}
    buckets = {label: 0.0 for label, _ in BUCKETS}
    other = 0.0
    for fr in frames:
        name = fr[0]
        byName.setdefault(name, []).append(fr)
        inclBy[name] = inclBy.get(name, 0.0) + fr[3]
        s = self_width(fr)
        selfBy[name] = selfBy.get(name, 0.0) + s
        if s <= 0:
            continue
        anc, cur = [], fr                            # ancestor names (self up to root)
        while cur:
            anc.append(cur[0])
            cur = parent(cur)
        for label, needles in BUCKETS:
            if any(n in a for a in anc for n in needles):
                buckets[label] += s
                break
        else:
            other += s

    def pct(v):
        return f"{v / total * 100:5.1f}%"

    ##	Samples are wall-clock, so a GUI app sitting in its main loop reads as
    ##	almost entirely "waiting". The share of the time that isn't waiting is
    ##	the part worth looking at, so give both denominators.
    busy = total - buckets["waiting"]

    def bpct(v):
        return f"{v / busy * 100:5.1f}%" if busy > 0 else "    -"

    print("attribution (self-time)            of total   of busy")
    print(f"  drawing, main thread .........:  {pct(buckets['drawing'])}    {bpct(buckets['drawing'])}")
    print(f"  folder loading + file info ...:  {pct(buckets['loading'])}    {bpct(buckets['loading'])}")
    print(f"  thumbnails ...................:  {pct(buckets['thumbnails'])}    {bpct(buckets['thumbnails'])}")
    print(f"  settings .....................:  {pct(buckets['settings'])}    {bpct(buckets['settings'])}")
    print(f"  other ........................:  {pct(other)}    {bpct(other)}")
    print(f"  waiting ......................:  {pct(buckets['waiting'])}         -   (idle)")
    print()
    print(f"  busy = {int(busy)} of {total} samples. Wall-clock sampling, so a blocked")
    print("  thread counts as waiting, not as work.")
    print()

    print("top self-time (where CPU burns):")
    for name, v in sorted(selfBy.items(), key=lambda kv: -kv[1])[:top]:
        if v < 1:
            break
        print(f"  {pct(v)} {int(v):4d}  {name}")
    print()

    print("top inclusive (call buckets):")
    for name, v in sorted(inclBy.items(), key=lambda kv: -kv[1])[:INCL_TOP]:
        print(f"  {pct(v)} {int(v):4d}  {name}")
    print()

    print(f"caller chains of the top {CHAIN} leaves:")
    for name, v in sorted(selfBy.items(), key=lambda kv: -kv[1])[:CHAIN]:
        fr = max(byName[name], key=self_width)
        print(f"  {name}  ({pct(v)} self)")
        cur, depth = parent(fr), 0
        while cur and depth < 12:
            print(f"      {cur[0]}")
            if cur[0] == "all":
                break
            cur, depth = parent(cur), depth + 1


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_dir = os.path.normpath(os.path.join(here, "..", "artifacts", "profiling"))

    ap = argparse.ArgumentParser(description="Summarize the newest profiler flamegraph.")
    ap.add_argument("--dir", default=default_dir, help="profiling directory (default: %(default)s)")
    ap.add_argument("--file", help="analyze this SVG instead of the newest in --dir")
    ap.add_argument("--top", type=int, default=SELF_TOP, help="self-time leaders to list")
    ap.add_argument("--check", action="store_true",
                    help="startup gate: print only if newer than the local marker, then record it")
    ap.add_argument("--force", action="store_true", help="with --check, report even if already seen")
    ap.add_argument("--no-mark", action="store_true", help="with --check, don't update the marker")
    a = ap.parse_args()

    if a.file:
        path = a.file
        if not os.path.isfile(path):
            skip(f"no such file: {path}")
        name = os.path.basename(path)
        m = NAME_RE.match(name)
        ts = m.group(1) if m else ""
    else:
        if not os.path.isdir(a.dir):
            skip(f"no profiling dir: {a.dir}")
        nb = newest_report(a.dir)
        if not nb:
            skip(f"no flamegraphs in {a.dir}")
        ts, name = nb
        path = os.path.join(a.dir, name)

    marker = os.path.join(a.dir, SEEN_FILE)
    if a.check and not a.force:
        seen = ""
        try:
            seen = open(marker, encoding="utf-8").read().strip()
        except OSError:
            pass
        if ts and seen and ts <= seen:
            print(f"SEEN {name}  (nothing newer than {seen})")
            return

    total, frames = parse_flamegraph(path)
    print(f"{'NEW' if a.check else 'FLAME'} {name}  ({ts or 'n/a'}, {total} samples)")
    print()
    report_busy_time(total, frames, a.top)

    if a.check and not a.no_mark and ts:
        try:
            open(marker, "w", encoding="utf-8").write(ts + "\n")
        except OSError as e:
            sys.stderr.write(f"flame-report: couldn't write marker: {e}\n")


if __name__ == "__main__":
    main()


##	History:
##		- 20260709: Created.
##		- 20260804: Adopted here; attribution buckets re-derived for a GTK file
##		  manager (drawing, folder loading, thumbnails, settings, waiting).
