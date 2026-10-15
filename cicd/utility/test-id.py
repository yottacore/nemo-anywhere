#!/usr/bin/env python3

# Test IDs.
#
# Every CI test carries an ID: the time it was written, as milliseconds since
# 2000-01-01 00:00 UTC, in lower-case Crockford base 32, zero-padded to 8
# digits. A meson test has it at the front of its name, so a failure in the log
# already names it. A lint-c check and a test script carry it on a "Test ID:"
# comment line, and read it back from there to print it when they run. So does
# a tool's self-test that the lint stage runs, on a line in the tool. A fuzz
# target has it at the front of its entry in fuzz.bash.
#
# With no option, prints the ID for right now, for a new test.
# --at TIME prints the one for an ISO time, when dating an older test.
# --check fails on a test with no ID, a malformed one, or one used twice.
#
# Syntax: test-id.py [--at TIME | --check [root]]

# Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
# Licensed under The MIT License (MIT). Full text at:
#     https://mit-license.org/
# SPDX-License-Identifier: MIT

import re
import sys
from datetime import datetime, timezone
from pathlib import Path

DIGITS = '0123456789abcdefghjkmnpqrstvwxyz'
EPOCH = datetime(2000, 1, 1, tzinfo=timezone.utc)


def encode(when):
    ms = (when - EPOCH) // datetime.resolution // 1000
    if ms <= 0:
        sys.exit(f'test-id: {when.isoformat()} is before 2000')
    out = ''
    while ms:
        ms, rem = divmod(ms, 32)
        out = DIGITS[rem] + out
    return out.rjust(8, '0')


def decode(text):
    ms = 0
    for ch in text:
        ms = ms * 32 + DIGITS.index(ch)
    return ms


def found_ids(root):
    # (id or None, where) for every test that should have one
    for build in sorted((root / 'source').rglob('meson.build')):
        for num, line in enumerate(build.read_text(encoding='utf-8').splitlines(), 1):
            m = re.match(r"\s*test\('([^']*)'", line)
            if m:
                tag = re.match(r'([0-9a-z]+) ', m.group(1))
                yield (tag.group(1) if tag else None), f'{build.relative_to(root)}:{num}'

    lint = root / 'cicd/utility/lint-c.bash'
    lines = lint.read_text(encoding='utf-8').splitlines()
    for num, line in enumerate(lines, 1):
        if not re.match(r'fCheck\w+\(\)\{', line):
            continue
        tag = None
        above = num - 2
        while above >= 0 and lines[above].startswith('##'):
            m = re.match(r'##\s+Test ID: (\S+)$', lines[above])
            tag = tag or (m and m.group(1))
            above -= 1
        yield tag, f'{lint.relative_to(root)}:{num}'

    scripts = [p for p in (root / 'cicd').rglob('*') if p.is_file() and p.suffix in ('.bash', '.ps1')
        and (p.name.startswith('test-') or 'smoke' in p.name or p.name == 'check-win-build-flags.bash')]
    for script in sorted(scripts):
        m = re.search(r'(?m)^##\s+(?:- )?Test ID: (\S+)$', script.read_text(encoding='utf-8-sig'))
        yield (m.group(1) if m else None), str(script.relative_to(root))

    # A tool's own self-test, found where the lint stage runs it, so a new one
    # cannot go in unseen. Its ID is in the tool, on a "Test ID:" line. This
    # script's --check is the gate itself, not a test.
    tools = {}
    for driver in ('cicd/utility/lint.bash', 'cicd/utility/lint-c.bash'):
        path = root / driver
        for num, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            m = re.search(r'([\w.-]+\.(?:bash|py))"?\s+(--self-test|--check)\b', line)
            if not m or line.lstrip().startswith('#') or m.group(1) == 'test-id.py':
                continue
            tools.setdefault(m.group(1) + ' ' + m.group(2), f'{driver}:{num}')
    for run, where in tools.items():
        name = run.split(' ')[0]
        tool = next((p for p in (root / 'cicd').rglob(name) if p.is_file()), None)
        if tool in scripts:
            continue
        m = tool and re.search(r'(?m)^(?:#+\s+(?:- )?)?Test ID: (\S+)$', tool.read_text(encoding='utf-8'))
        yield (m.group(1) if m else None), f'{where}: {run}'

    fuzz = root / 'cicd/linux/fuzz.bash'
    for num, line in enumerate(fuzz.read_text(encoding='utf-8').splitlines(), 1):
        m = re.match(r'\t"([^"|]*\|)?fuzz-[^"|]*\|[^"|]*"$', line)
        if m:
            yield (m.group(1)[:-1] if m.group(1) else None), f'{fuzz.relative_to(root)}:{num}'


def check(root):
    now = (datetime.now(timezone.utc) - EPOCH) // datetime.resolution // 1000
    seen = {}
    bad = 0
    for tag, where in found_ids(root):
        if tag is None:
            print(f'FAIL: {where}: test has no ID (make one with cicd/utility/test-id.py)')
        elif not re.fullmatch(f'[{DIGITS}]{{8,}}', tag) or decode(tag) > now:
            print(f'FAIL: {where}: "{tag}" is not a lower-case Crockford ID from the past')
        elif tag in seen:
            print(f'FAIL: {where}: ID {tag} is also used at {seen[tag]}')
        else:
            seen[tag] = where
            continue
        bad += 1
    if bad:
        sys.exit(2)
    print(f'[ OK: test IDs: {len(seen)} tests, all unique ]')


if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--check']:
        check(Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parents[2])
    elif args[:1] == ['--at'] and len(args) == 2:
        when = datetime.fromisoformat(args[1])
        print(encode(when if when.tzinfo else when.replace(tzinfo=timezone.utc)))
    elif not args:
        print(encode(datetime.now(timezone.utc)))
    else:
        sys.exit('Syntax: test-id.py [--at TIME | --check [root]]')
