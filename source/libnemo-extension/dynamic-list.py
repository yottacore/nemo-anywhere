#!/usr/bin/env python3
"""Write an ld --dynamic-list naming every function and variable the static
extension library defines, so the exe exports that API and nothing else.
With "ld64" last, write ld64's -exported_symbols_list form instead."""

import subprocess
import sys

nm, archive, out = sys.argv[1:4]
ld64 = sys.argv[4:5] == ["ld64"]
# Mach-O puts constant data in its own section, which nm shows as S.
kinds = "TDBRS" if ld64 else "TDBR"
listing = subprocess.run([nm, "-g", "--defined-only", "-P", archive],
                         capture_output=True, text=True, check=True).stdout
names = set()
for line in listing.splitlines():
    fields = line.split()
    # Archive member headers end in ':' and carry no type field.
    if len(fields) >= 2 and fields[1] in kinds:
        names.add(fields[0])
if not names:
    sys.exit("dynamic-list.py: no symbols found in " + archive)

with open(out, "w") as f:
    if ld64:
        # nm already gives the Mach-O names, underscore and all.
        f.write("".join(name + "\n" for name in sorted(names)))
        sys.exit(0)
    f.write("{\n")
    for name in sorted(names):
        f.write("\t" + name + ";\n")
    f.write("};\n")
