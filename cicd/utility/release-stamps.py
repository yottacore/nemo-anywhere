#!/usr/bin/env python3
"""Print the build stamp each release artifact carries, or check them against one.

Every lane stamps SOURCE_DATE_EPOCH, the commit date of what was built, into its
output: tar entry times, the .deb and .rpm file times, the rpm build time and the
PE header of a Windows exe. So the stamp says which commit an artifact came from,
and the build number in it follows from the same date.

Syntax: release-stamps.py [--expect EPOCH] [--exe NAME] FILE...
With --expect, exits 1 when any artifact carries another stamp. In a zip only
the exe called NAME is read: the runtime's own exes keep their packagers' dates,
and zip entry times are local time. A file it has no reader for is listed as
unchecked and does not fail the run. So is a packed exe, since the packer writes
the outer header.
"""

import io
import lzma
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile


def tar_stamps(fileobj):
    with tarfile.open(fileobj=fileobj, mode="r:*") as tar:
        return {member.mtime for member in tar}


def pe_stamp(data):
    if data[:2] != b"MZ" or len(data) < 0x40:
        return None
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[offset:offset + 4] != b"PE\0\0":
        return None
    return struct.unpack_from("<I", data, offset + 8)[0]


def deb_stamps(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive")
    pos = 8
    while pos + 60 <= len(data):
        name = data[pos:pos + 16].decode().strip().rstrip("/")
        size = int(data[pos + 48:pos + 58].decode().strip())
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if not name.startswith("data.tar"):
            continue
        if name.endswith(".zst"):
            # tarfile reads zstd only from Python 3.14 on.
            if not shutil.which("zstd"):
                return None
            body = subprocess.run(["zstd", "-dc"], input=body, capture_output=True, check=True).stdout
        elif name.endswith(".xz"):
            body = lzma.decompress(body)
        return tar_stamps(io.BytesIO(body))
    raise ValueError("no data.tar member")


def rpm_header(data, pos):
    if data[pos:pos + 3] != b"\x8e\xad\xe8":
        raise ValueError("bad rpm header magic")
    count, store_size = struct.unpack_from(">II", data, pos + 8)
    index = pos + 16
    store = index + 16 * count
    tags = {}
    for i in range(count):
        tag, kind, offset, n = struct.unpack_from(">IIII", data, index + 16 * i)
        tags[tag] = (kind, store + offset, n)
    return tags, store + store_size


def rpm_stamps(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\xed\xab\xee\xdb":
        raise ValueError("not an rpm")
    _, end = rpm_header(data, 96)
    end += -end % 8  # the signature header is padded to 8 bytes
    tags, _ = rpm_header(data, end)
    stamps = set()
    for tag in (1006, 1034):  # BUILDTIME, FILEMTIMES
        if tag in tags:
            _, at, n = tags[tag]
            stamps.update(struct.unpack_from(">%dI" % n, data, at))
    return stamps


def zip_stamps(path, exe):
    stamps = set()
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            if exe and info.filename.rsplit("/", 1)[-1] == exe:
                stamp = pe_stamp(z.read(info))
                if stamp is not None:
                    stamps.add(stamp)
    return stamps or None


def stamps_for(path, exe):
    lower = path.lower()
    if lower.endswith((".tar.gz", ".tgz", ".tar.xz", ".tar")):
        with open(path, "rb") as f:
            return tar_stamps(f)
    if lower.endswith(".deb"):
        return deb_stamps(path)
    if lower.endswith(".rpm"):
        return rpm_stamps(path)
    if lower.endswith(".zip"):
        return zip_stamps(path, exe)
    return None


def main(argv):
    expect = None
    exe = None
    while len(argv) >= 2 and argv[0] in ("--expect", "--exe"):
        if argv[0] == "--expect":
            expect = int(argv[1])
        else:
            exe = argv[1]
        argv = argv[2:]
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    wrong = 0
    for path in argv:
        name = path.rsplit("/", 1)[-1]
        try:
            stamps = stamps_for(path, exe)
        except (OSError, ValueError, EOFError, lzma.LZMAError, tarfile.TarError,
                zipfile.BadZipFile, struct.error, subprocess.CalledProcessError) as e:
            print("%s: unreadable (%s)" % (name, e))
            wrong += 1
            continue
        if stamps is None:
            print("%s: unchecked" % name)
            continue
        print("%s: %s" % (name, " ".join(str(s) for s in sorted(stamps))))
        if expect is not None and stamps != {expect}:
            wrong += 1
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
