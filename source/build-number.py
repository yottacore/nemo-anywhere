#!/usr/bin/env python3
"""Print the build number: minutes since the start of 2000, rounded, Crockford base32,
lower case. Taken from SOURCE_DATE_EPOCH when set, so a release build stamps the
commit rather than the clock and stays reproducible; otherwise from HEAD's
commit date, and only failing that from now. --epoch adds the Unix time of that
same minute, so the app can show its local date."""

import os
import subprocess
import sys
import time

EPOCH_2000 = 946684800
# Crockford base32: no i, l, o or u, so nothing reads as a digit by mistake.
DIGITS = "0123456789abcdefghjkmnpqrstvwxyz"


def stamp():
    source_date = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date:
        try:
            return int(source_date)
        except ValueError:
            pass

    here = os.path.dirname(os.path.abspath(__file__))
    try:
        out = subprocess.run(["git", "log", "-1", "--format=%ct"],
                             cwd=here, capture_output=True, text=True, timeout=10)
        if out.returncode == 0 and out.stdout.strip():
            return int(out.stdout.strip())
    except (OSError, ValueError, subprocess.SubprocessError):
        pass

    return int(time.time())


def crockford(n):
    if n <= 0:
        return "0"
    out = ""
    while n:
        out = DIGITS[n & 31] + out
        n >>= 5
    return out


minutes = (stamp() - EPOCH_2000 + 30) // 60
out = crockford(minutes)
if "--epoch" in sys.argv[1:]:
    out += " %d" % (EPOCH_2000 + minutes * 60)
sys.stdout.write(out)
