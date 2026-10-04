#!/usr/bin/env python3
"""Shrink an SVG for bundling: drop editor bookkeeping, keep the drawing.

Themes are drawn in Inkscape, and what ships is mostly not the drawing - it is
sodipodi/inkscape namespaces, RDF metadata blocks, and the indentation between
elements. Removing only those is safe for every renderer, and typically halves
the file. Nothing that affects rendering is touched: styles, classes, gradient
ids and xlink references all survive verbatim, which is what keeps GTK's
symbolic recoloring working.

Past that comes a numeric pass, which is where the rest of the weight is. Icons
are traced, and a traced path carries coordinates to eight decimal places in a
box sixteen units wide - four or five digits per number that no renderer can
act on. Numbers are rounded to a quantum finer than a two-thousandth of the
viewBox, which is under a tenth of a pixel at any size an icon is ever drawn,
and re-emitted without the separators SVG does not require. Colors fold to
their short form and unreferenced ids go. Elements, attributes that paint,
classes and every #id anything points at are left exactly as they were.

Syntax: svg-min.py <in.svg> <out.svg>
        svg-min.py --tree <in-dir> <out-dir>       (--tree in place is allowed)
        svg-min.py --self-test

--self-test minifies a made-up document that carries each of the traps above
and checks what comes out, so a change that rounds a multiplier or loses an
arc flag fails the lint stage rather than showing up as a misdrawn icon.

Test ID: rhtrxr84
"""

import math
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

SVG_NS = "http://www.w3.org/2000/svg"
XLINK_NS = "http://www.w3.org/1999/xlink"

# Editor bookkeeping. Everything under these namespaces is inert at render time.
#
# Matched as substrings, not exact URIs: themes carry several spellings of the
# same namespace, including a "sodipodi-0.dtd" that is missing a digit. An exact
# list let that one through, which left <ns1:namedview> in the output - harmless
# where the file keeps its own xmlns declaration, but it survives as dead weight
# and breaks anything that inlines the body into another document.
DROP_NS = (
    "sodipodi.sourceforge.net",
    "inkscape.org/namespaces",
    "ns.adobe.com/AdobeIllustrator",
    "22-rdf-syntax-ns#",
    "purl.org/dc/elements",
    "creativecommons.org/ns#",
    "openswatchbook.org/uri",
)

DROP_TAGS = ("metadata", "title", "desc", "foreignObject")

ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", XLINK_NS)


def _in_dropped_ns(name):
    if not name.startswith("{"):
        return False
    uri = name[1:name.index("}")] if "}" in name else ""
    return any(fragment in uri for fragment in DROP_NS)


def _clean(element):
    for child in list(element):
        tag = child.tag
        if not isinstance(tag, str):  # comment or processing instruction
            element.remove(child)
            continue
        if _in_dropped_ns(tag) or tag.split("}")[-1] in DROP_TAGS:
            element.remove(child)
            continue
        _clean(child)

    for key in list(element.attrib):
        if _in_dropped_ns(key):
            del element.attrib[key]

    # Indentation only - a text node that is all whitespace draws nothing.
    if element.text is not None and element.text.strip() == "":
        element.text = None
    if element.tail is not None and element.tail.strip() == "":
        element.tail = None


# ---------------------------------------------------------------------------
# The numeric pass.
#
# Only path geometry is rounded. Transform matrices, gradient vectors and the
# rest keep their digits, because a number there is a multiplier: rounding a
# scale of .38956 to .39 moves a gradient that runs from 445 to 200 user units
# by a fifth of a unit in a box seventeen units wide, which is visible. Path
# data is the bulk of an icon file anyway - the other numbers are a handful per
# document. What makes even that safe is the accumulated transform: a path
# inside a group scaled by 4 is rounded a digit finer, so the error on screen is
# the same wherever in the tree the path sits.

# Inert once the drawing is what is wanted.
DROP_ATTRS = {"version", "baseProfile", "requiredFeatures"}

NUMBER = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
HEX6 = re.compile(r"#([0-9a-fA-F]{6})\b")
REFERENCE = re.compile(r"#([A-Za-z_][\w.:-]*)")
TRANSFORM = re.compile(r"(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\)")

COLOR_ATTRS = ("fill", "stroke", "stop-color", "color", "flood-color",
        "lighting-color", "solid-color", "style")


def _round(text, decimals):
    """One number, rounded and spelled as short as SVG allows."""
    try:
        value = round(float(text), decimals)
    except ValueError:
        return text
    out = ("%.*f" % (decimals, value)).rstrip("0").rstrip(".")
    if out in ("", "-", "-0"):
        return "0"
    if out.startswith("0."):
        out = out[1:]
    elif out.startswith("-0."):
        out = "-" + out[2:]
    return out


# How many parameters each path command takes, and where an elliptical arc
# keeps its two flags. A flag is a single character and may be written with
# nothing around it - "a.6.6 0 00-.418 1.029" carries flags 0 and 0 inside that
# "00", and reading path data as a plain run of numbers swallows one of them and
# silently reshapes the icon. Adwaita's terminal glyph is drawn exactly that way.
PATH_ARGS = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6, "s": 4,
         "q": 4, "t": 2, "a": 7, "z": 0}
ARC_FLAGS = (3, 4)


def _path_tokens(data):
    """(is_command, text) through the path, arc flags read as single digits."""
    position = 0
    command = ""
    argument = 0
    length = len(data)

    while position < length:
        char = data[position]
        if char in " ,\t\r\n":
            position += 1
            continue
        if char.isalpha():
            command = char
            argument = 0
            position += 1
            yield True, char
            # After a moveto the repeats are linetos, which take the same two.
            continue

        lowered = command.lower()
        if lowered == "a" and argument % 7 in ARC_FLAGS:
            yield False, char
            position += 1
            argument += 1
            continue

        match = NUMBER.match(data, position)
        if match is None:  # not ours to interpret
            yield False, char
            position += 1
            continue
        yield False, match.group(0)
        position = match.end()
        argument += 1
        if lowered in PATH_ARGS and PATH_ARGS[lowered] and argument >= PATH_ARGS[lowered]:
            argument = 0


def _shrink_path(data, decimals):
    """Path data with its numbers rounded and its separators cut to the bone.

	Only the space before a minus sign is dropped. A leading dot could be run
	onto the previous number too, but not after an arc's flags, where the pair
	is a single token - so the rule that is safe everywhere is the one used, and
	the few bytes it leaves behind are not worth the risk.
	"""
    parts = []
    for is_command, token in _path_tokens(data):
        if not is_command and NUMBER.fullmatch(token):
            token = _round(token, decimals)
        if not parts:
            parts.append(token)
        elif is_command or parts[-1][-1].isalpha() or token[0] == "-":
            parts.append(token)
        else:
            parts.append(" " + token)
    return "".join(parts)


def _transform_scale(value):
    """How much @value magnifies lengths, as one number. 1.0 if it cannot tell."""
    scale = 1.0
    for name, args in TRANSFORM.findall(value or ""):
        numbers = [float(n) for n in NUMBER.findall(args)] or [0.0]
        if name == "scale":
            sx = numbers[0]
            sy = numbers[1] if len(numbers) > 1 else sx
            scale *= math.sqrt(abs(sx * sy)) or 1.0
        elif name == "matrix" and len(numbers) >= 4:
            determinant = abs(numbers[0] * numbers[3] - numbers[1] * numbers[2])
            scale *= math.sqrt(determinant) or 1.0
    return scale or 1.0


def _base_decimals(root):
    """Enough decimals that the quantum is under a 2000th of the viewBox."""
    box = root.get("viewBox")
    width = 0.0
    if box:
        parts = box.replace(",", " ").split()
        if len(parts) == 4:
            try:
                width = abs(float(parts[2]))
            except ValueError:
                width = 0.0
    if width <= 0:
        digits = re.match(r"[\d.]+", (root.get("width") or "48").strip())
        try:
            width = abs(float(digits.group(0))) if digits else 48.0
        except ValueError:
            width = 48.0
    if width <= 0:
        width = 48.0

    decimals = 2
    while decimals < 4 and 10.0 ** decimals < 2000.0 / width:
        decimals += 1
    return decimals


def _referenced_ids(root):
    """Every id something in the document points at, by any of the three ways."""
    found = set()
    for element in root.iter():
        for key, value in element.attrib.items():
            local = key.split("}")[-1]
            if local in ("href", "begin", "end") or "url(" in value or value.startswith("#"):
                found.update(REFERENCE.findall(value))
        # A stylesheet can select by id, and it is text rather than an attribute.
        if element.tag.split("}")[-1] == "style" and element.text:
            found.update(REFERENCE.findall(element.text))
    return found


def _shorten_color(text):
    def fold(match):
        digits = match.group(1)
        if digits[0] == digits[1] and digits[2] == digits[3] and digits[4] == digits[5]:
            return "#" + digits[0] + digits[2] + digits[4]
        return match.group(0)
    return HEX6.sub(fold, text)


def _tighten(element, base, keep_ids, scale, is_root):
    scale *= _transform_scale(element.get("transform"))
    decimals = base + max(0, int(math.ceil(math.log10(scale))) if scale > 1 else 0)

    for key in list(element.attrib):
        local = key.split("}")[-1]

        if local in DROP_ATTRS and not is_root:
            del element.attrib[key]
            continue
        if local == "version" and is_root:
            del element.attrib[key]
            continue
        # A stylesheet's id is how color-scheme tooling finds the block to
        # rewrite, and nothing in the document points at it, so the reference
        # scan cannot see that anything needs it. Left alone.
        if (local == "id" and not is_root
            and element.tag.split("}")[-1] != "style"
            and element.attrib[key] not in keep_ids):
            del element.attrib[key]
            continue

        if local in ("d", "points"):
            element.attrib[key] = _shrink_path(element.attrib[key], decimals)
        elif local in COLOR_ATTRS:
            element.attrib[key] = _shorten_color(element.attrib[key])

    for child in element:
        if isinstance(child.tag, str):
            _tighten(child, base, keep_ids, scale, False)


def minify(src, dst):
    tree = ET.parse(src)
    root = tree.getroot()
    _clean(root)
    _tighten(root, _base_decimals(root), _referenced_ids(root), 1.0, True)

    out = ET.tostring(root, encoding="unicode")
    # ET emits the default namespace on every child it cannot attribute; collapse
    # the redundant redeclarations it leaves behind.
    out = out.replace(' xmlns:ns0="%s"' % SVG_NS, "")
    out = re.sub(r"\s+", " ", out)
    out = out.replace("> <", "><").strip()

    with open(dst, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(out)


SELF_TEST_SVG = """\
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
     xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
     width="16" height="16" viewBox="0 0 16 16" version="1.1">
  <metadata>editor notes</metadata>
  <sodipodi:namedview id="namedview1" pagecolor="#ffffff"/>
  <style id="current-color-scheme" type="text/css">#by-style { opacity: .5 }</style>
  <defs>
    <linearGradient id="grad" x1="445.123456" y1="0" x2="200.987654" y2="16"
        gradientUnits="userSpaceOnUse" gradientTransform="matrix(.38956 0 0 .38956 1.234567 -2.345678)">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" style="stop-color:#aabbcc;stop-opacity:1"/>
    </linearGradient>
    <path id="shape" d="M0 0h1v1z"/>
  </defs>
  <g id="unused-group" transform="matrix(.38956 0 0 .38956 1.23456789 2)" inkscape:label="Layer 1">
    <path id="unused-path" fill="url(#grad)" d="M1.23456789 2.3456789a5 5 0 015 5a.6.6 0 00-.418 1.029"/>
  </g>
  <g transform="scale(4)"><path id="scaled" d="M1.23456789 1l2 2" fill="#abcdef" stroke="#FFFFFF"/></g>
  <use xlink:href="#shape"/>
  <rect id="by-style" width="1" height="1"/>
</svg>
"""


def self_test():
    print("[ Test %s svg-min.py --self-test ]" % re.search(r"(?m)^Test ID: (\S+)$", __doc__).group(1))
    failures = []

    def check(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, expected {want!r}")

    # Flags 0 and 1 run straight into the x that follows them.
    tokens = [text for is_command, text in _path_tokens("a5 5 0 015 5") if not is_command]
    check("arc flags tokenized", tokens, ["5", "5", "0", "0", "1", "5", "5"])

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "in.svg")
        dst = os.path.join(tmp, "out.svg")
        with open(src, "w", encoding="utf-8") as handle:
            handle.write(SELF_TEST_SVG)
        minify(src, dst)
        with open(dst, encoding="utf-8") as handle:
            text = handle.read()

    root = ET.fromstring(text)
    by_tag = {}
    for element in root.iter():
        by_tag.setdefault(element.tag.split("}")[-1], []).append(element)
    ids = sorted(element.get("id") for element in root.iter() if element.get("id"))
    paths = by_tag.get("path", [])
    groups = by_tag.get("g", [])
    stops = by_tag.get("stop", [])
    gradient = by_tag.get("linearGradient", [None])[0]

    for gone in ("sodipodi", "inkscape", "metadata", 'version="'):
        check(f"{gone} dropped", gone in text, False)
    # A path inside the 0.39 scale keeps the base 3 decimals; both arcs keep
    # their seven parameters, flags included.
    check("arcs survive", paths[1].get("d") if len(paths) > 1 else None,
          "M1.235 2.346a5 5 0 0 1 5 5a.6 .6 0 0 0-.418 1.029")
    # Scaled up by 4, one digit finer, so the error on screen stays the same.
    check("scaled path rounded finer", paths[2].get("d") if len(paths) > 2 else None, "M1.2346 1l2 2")
    check("transform kept", groups[0].get("transform") if groups else None,
          "matrix(.38956 0 0 .38956 1.23456789 2)")
    check("gradient transform kept", gradient.get("gradientTransform") if gradient is not None else None,
          "matrix(.38956 0 0 .38956 1.234567 -2.345678)")
    check("gradient vector kept",
          [gradient.get(k) for k in ("x1", "x2")] if gradient is not None else None,
          ["445.123456", "200.987654"])
    check("stop color shortened", stops[0].get("stop-color") if stops else None, "#fff")
    check("style color shortened", stops[1].get("style") if len(stops) > 1 else None,
          "stop-color:#abc;stop-opacity:1")
    check("colors", [paths[2].get("fill"), paths[2].get("stroke")] if len(paths) > 2 else None,
          ["#abcdef", "#FFF"])
    check("ids", ids, ["by-style", "current-color-scheme", "grad", "shape"])

    for failure in failures:
        print(f"[ FAIL: svg-min self-test: {failure} ]")
    if failures:
        print(f"[ FAILED: svg-min self-test, {len(failures)} problem(s) ]")
        return 1
    print("[ OK: svg-min self-test ]")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()

    if len(argv) == 4 and argv[1] == "--tree":
        src_root, dst_root = argv[2], argv[3]
        kept = 0
        for base, _dirs, files in os.walk(src_root):
            for name in files:
                src = os.path.join(base, name)
                dst = os.path.join(dst_root, os.path.relpath(src, src_root))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                # A few names exist only as bitmaps upstream (Adwaita's
                # emblems). Nothing to minify - pass them through.
                if not name.endswith(".svg"):
                    with open(src, "rb") as fh:
                        data = fh.read()
                    with open(dst, "wb") as fh:
                        fh.write(data)
                    kept += 1
                    continue
                try:
                    minify(src, dst)
                except Exception:
                    # An SVG we cannot parse still renders; ship it untouched.
                    with open(src, "rb") as fh:
                        data = fh.read()
                    with open(dst, "wb") as fh:
                        fh.write(data)
                kept += 1
        print(kept)
        return 0

    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2

    minify(argv[1], argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
