#!/usr/bin/env python3

##	- Purpose: The archive core's own build keeps out GTK and the app. Every
##	  core file is compiled with nothing of theirs on its include path and no
##	  app define, and the library it makes calls no gtk_, gdk_, nemo_ or eel_
##	  function. lint-c.bash reads the sources and meson.build for the same
##	  thing; this reads what the build did.
##	- Syntax: check-arc-core-build.py <source-root> <build-root> <libarc-core.a> [nm]
##	  With no nm the symbol half answers 77, so the test shows as a skip.
##	- Test ID: ddx6z

##	Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)
##	Licensed under the GNU General Public License, version 2 only.
##	SPDX-License-Identifier: GPL-2.0-only

import json
import os
import re
import shlex
import subprocess
import sys

# A path part naming one of these is GTK, its stack, or the app.
OUTSIDE = re.compile(r'(^|[/\\])(gtk|gdk|gdk-pixbuf|pango|cairo|atk|harfbuzz|libnemo[^/\\]*|eel)([-/\\.0-9]|$)')
APP_SYMBOL = re.compile(r'^_?(gtk|gdk|nemo|eel|pango|cairo|atk)_')


def inside(path, root):
    return path == root or path.startswith(root + os.sep)


def check_compiles(source_root, build_root):
    with open(os.path.join(build_root, 'compile_commands.json'), encoding='utf-8') as f:
        entries = json.load(f)
    own = [os.path.join(source_root, 'archive-core'), os.path.join(build_root, 'archive-core')]
    found = 0
    bad = []
    for entry in entries:
        if 'libarc-core.a.p' not in entry.get('output', ''):
            continue
        found += 1
        words = entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])
        for i, word in enumerate(words):
            path = None
            if word == '-I' or word == '-isystem':
                path = words[i + 1] if i + 1 < len(words) else ''
            elif word.startswith('-I'):
                path = word[2:]
            elif word.startswith('-isystem'):
                path = word[8:]
            elif word.startswith('-DNEMO_') or word.startswith('-DEEL_'):
                bad.append('%s: %s' % (entry['file'], word))
            if path is None:
                continue
            full = os.path.realpath(os.path.join(entry['directory'], path))
            project = inside(full, source_root) or inside(full, build_root)
            if OUTSIDE.search(full) or (project and not any(inside(full, o) for o in own)):
                bad.append('%s: -I%s' % (entry['file'], path))
    if found == 0:
        bad.append('no archive core file in compile_commands.json')
    return bad


def check_symbols(library, nm):
    run = subprocess.run([nm, '-u', library], capture_output=True, text=True)
    if run.returncode != 0:
        return ['%s -u failed: %s' % (nm, run.stderr.strip())]
    names = set()
    for line in run.stdout.splitlines():
        parts = line.split()
        if parts and APP_SYMBOL.match(parts[-1]):
            names.add(parts[-1])
    return ['calls %s' % name for name in sorted(names)]


def main():
    if len(sys.argv) < 4:
        print('usage: check-arc-core-build.py <source-root> <build-root> <libarc-core.a> [nm]', file=sys.stderr)
        return 2
    source_root = os.path.realpath(sys.argv[1])
    build_root = os.path.realpath(sys.argv[2])
    library = sys.argv[3]
    nm = sys.argv[4] if len(sys.argv) > 4 else ''

    bad = check_compiles(source_root, build_root)
    if nm:
        bad += check_symbols(library, nm)
    if bad:
        print('FAIL: the archive core is built with or against GTK or the app')
        for line in bad:
            print('  ' + line)
        return 1
    if not nm:
        print('SKIP: compiles checked, but no nm here to read the library')
        return 77
    print('OK: the archive core builds and links with nothing of GTK or the app')
    return 0


if __name__ == '__main__':
    sys.exit(main())
