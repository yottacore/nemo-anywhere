#!/usr/bin/env python3
"""Draw the first-party Windows-look icon sets into assets/icons.

There are 4 eras, one per Windows generation worth offering: Luna for XP, Aero
for 7, Metro for 10, and Mica for 11. There's no cleanly-licensed SVG set of any
of them. What circulates is Microsoft's own shell art extracted and repackaged,
which we won't bundle. So these are ours, drawn here from a shared set of base
drawings and glyphs, recolored per theme. The base drawings are folder, page,
disc, drive, and monitor. Original work, GPL-2.0-only like the rest of the
tree, and about a kilobyte an icon.

Folders are yellow in all 4, because that's what Windows does and because a
yellow folder is the one color that reads on a light background and a dark one
alike. The folder drawing itself differs per era, since the folder is the icon
people recognize a Windows generation by. XP and 7 get a chunky outlined one,
10 a flat rectangle, and 11 a rounded tapered panel.

Coverage is the part of the file manager that defines its look: folders,
file types, and drives. Everything else falls through Inherits to Adwaita, which is
what the freedesktop fallback chain is for; a toolbar arrow looks the same in
every theme anyway.

Output is committed, so no build step depends on this script. Re-run it after
changing a drawing or a palette.

Syntax: gen-icon-theme.py [<repo-root>]
"""

import os
import sys

CANVAS = 48

# ---------------------------------------------------------------------------
# Palettes
#
# Luna is the warm amber-and-cream of Windows XP: saturated fills, a firm
# outline, everything a little chunky. Aero is Windows 7 - glassier and softly
# lit, its cool blues kept for glass, screens, and discs while the folder stays
# the amber Windows has always used. Metro is Windows 10: flat, no gradient at
# all past a 2-tone split, square corners. Mica is Windows 11: rounded, the
# front panel tapering away from the viewer, no outline anywhere.
#
# folderShape picks the folder drawing: "classic" (XP/7), "metro", "mica".

THEMES = {
    "Luna": {
        "style": "Windows XP",
        "folderShape": "classic",
        "comment": "Warm, chunky, saturated - the Windows XP look",
        "folder": ("#FFDC8A", "#F0A22E"),
        "folderBack": ("#FFCF63", "#E08A16"),
        "folderLine": "#B96F11",
        "paper": ("#FFFFFF", "#F3EFE4"),
        "paperLine": "#B3A78C",
        "fold": "#E4DCC6",
        "metal": ("#F4F1E8", "#C3B9A3"),
        "metalLine": "#8A8172",
        "glass": ("#9CC4E8", "#3C6FA8"),
        "accent": "#2A5DA8",
        "warm": "#D9822B",
        "green": "#4E9A2F",
        "red": "#C4331F",
        "purple": "#7B4FA8",
        "glyph": "#FFFFFF",
        "glyphOn": "#4A2E06",
        "gloss": 0.30,
    },
    "Aero": {
        "style": "Windows 7",
        "folderShape": "classic",
        "comment": "Glassy and softly lit - the Windows 7 look",
        "folder": ("#FFE3A8", "#F0B03A"),
        "folderBack": ("#F9CE7C", "#DD9A22"),
        "folderLine": "#A86F17",
        "paper": ("#FFFFFF", "#E8F0F8"),
        "paperLine": "#A8BDD2",
        "fold": "#CFDEEC",
        "metal": ("#FBFDFF", "#AFC3D6"),
        "metalLine": "#6C8095",
        "glass": ("#DCEEFC", "#4E92CC"),
        "accent": "#1E6FB8",
        "warm": "#E08A1E",
        "green": "#4FA03A",
        "red": "#CC3A2A",
        "purple": "#7E57C2",
        "glyph": "#FFFFFF",
        "glyphOn": "#14364F",
        "gloss": 0.45,
    },
    "Metro": {
        "style": "Windows 10",
        "folderShape": "metro",
        "comment": "Flat, square, one accent - the Windows 10 look",
        "folder": ("#FFC943", "#FFC943"),
        "folderBack": ("#E5A521", "#E5A521"),
        "folderLine": "#C4890F",
        "paper": ("#FFFFFF", "#FFFFFF"),
        "paperLine": "#C8CDD2",
        "fold": "#E4E7EA",
        "metal": ("#D8DEE4", "#D8DEE4"),
        "metalLine": "#98A2AD",
        "glass": ("#0078D7", "#0078D7"),
        "accent": "#0078D7",
        "warm": "#E5A521",
        "green": "#107C10",
        "red": "#E81123",
        "purple": "#5C2D91",
        "glyph": "#FFFFFF",
        "glyphOn": "#5A3E00",
        "gloss": 0.0,
    },
    "Mica": {
        "style": "Windows 11",
        "folderShape": "mica",
        "comment": "Rounded and softly shaded - the Windows 11 look",
        "folder": ("#FFDA7C", "#FBBE2C"),
        "folderBack": ("#F4B62C", "#E09811"),
        "folderLine": "#C4871A",
        "paper": ("#FFFFFF", "#F5F7F9"),
        "paperLine": "#C6CBD2",
        "fold": "#E7EAEE",
        "metal": ("#F3F5F8", "#D2D8E0"),
        "metalLine": "#98A1AD",
        "glass": ("#CFE4FA", "#4A90D9"),
        "accent": "#0F6CBD",
        "warm": "#D97706",
        "green": "#0F7B0F",
        "red": "#C42B1C",
        "purple": "#8764B8",
        "glyph": "#FFFFFF",
        "glyphOn": "#4A3200",
        "gloss": 0.18,
    },
}


def grad(gid, top, bottom, x1=0, y1=0, x2=0, y2=1):
    return (
        '<linearGradient id="%s" x1="%s" y1="%s" x2="%s" y2="%s">'
        '<stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/>'
        "</linearGradient>" % (gid, x1, y1, x2, y2, top, bottom)
    )


# ---------------------------------------------------------------------------
# Base shapes. Each returns (defs, body).

def base_folder(t, open_lid=False):
    """The folder, drawn the way the theme's era drew it.

	This is the icon a Windows generation is recognized by, so it's the one
	drawing that isn't shared. XP and 7 get the chunky outlined folder, 10 the
	flat rectangle, and 11 the rounded panel tapering away from the viewer. The
	body is 44 wide by 35 tall in every era. Any shallower and it stops
	reading as a folder and starts reading as an envelope.
	"""
    shape = t.get("folderShape", "classic")
    if shape == "mica":
        return folder_mica(t, open_lid)
    if shape == "metro":
        return folder_metro(t, open_lid)
    return folder_classic(t, open_lid)


def folder_classic(t, open_lid):
    """XP and 7: firm outline, rounded corners, a highlight along the top."""
    defs = grad("gb", *t["folderBack"]) + grad("gf", *t["folder"])
    back = (
        '<path d="M5 7h13l4 4.5h21a3 3 0 0 1 3 3V39a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3V10a3 3 0 0 1 3-3z"'
        ' fill="url(#gb)" stroke="%s" stroke-width="1.2"/>' % t["folderLine"]
    )
    if open_lid:
        front = (
            '<path d="M9.5 19h38.2a1 1 0 0 1 1 1.3l-4.9 18.4a3 3 0 0 1-2.9 2.3H5a3 3 0 0 1-3-3z"'
            ' fill="url(#gf)" stroke="%s" stroke-width="1.2"/>' % t["folderLine"]
        )
        gloss = '<path d="M10.5 20.4h36.4l-1.5 5.2H9z" fill="#fff" opacity="%s"/>' % t["gloss"]
    else:
        front = (
            '<path d="M2 19h44v20a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3z"'
            ' fill="url(#gf)" stroke="%s" stroke-width="1.2"/>' % t["folderLine"]
        )
        gloss = '<path d="M3.5 20.5h41v5.5H3.5z" fill="#fff" opacity="%s"/>' % t["gloss"]
    return defs, back + front + gloss


def folder_metro(t, open_lid):
    """10: square corners, 2 flat tones, no outline, and no highlight."""
    defs = ""
    back = '<path d="M2 7h15.6l3.4 4H46v31H2z" fill="%s"/>' % t["folderBack"][0]
    if open_lid:
        front = '<path d="M8 18h38l-4 24H2V21z" fill="%s"/>' % t["folder"][0]
    else:
        front = '<path d="M2 18h44v24H2z" fill="%s"/>' % t["folder"][0]
    return defs, back + front


def folder_mica(t, open_lid):
    """11: rounded throughout, the front panel narrowing as it falls away."""
    defs = grad("gb", *t["folderBack"]) + grad("gf", *t["folder"])
    back = (
        '<path d="M6 7h11.4a3 3 0 0 1 2.2 1l2.7 2.9H42a4 4 0 0 1 4 4V38a4 4 0 0 1-4 4'
        'H6a4 4 0 0 1-4-4V11a4 4 0 0 1 4-4z" fill="url(#gb)"/>'
    )
    if open_lid:
        front = (
            '<path d="M9.6 17h34.2a2 2 0 0 1 2 2.4l-4 19.4a4 4 0 0 1-3.9 3.2H6a4 4 0 0 1-4-4'
            'V21.6z" fill="url(#gf)"/>'
        )
        gloss = '<path d="M10.4 18.6h33.2l-.9 4H9.6z" fill="#fff" opacity="%s"/>' % t["gloss"]
    else:
        front = (
            '<path d="M4.6 17h38.8a2 2 0 0 1 2 2.3l-2.5 19.5a4 4 0 0 1-4 3.2H9.1a4 4 0 0 1-4-3.2'
            'L2.6 19.3a2 2 0 0 1 2-2.3z" fill="url(#gf)"/>'
        )
        gloss = '<path d="M5.3 18.6h37.4l-.5 4H5.8z" fill="#fff" opacity="%s"/>' % t["gloss"]
    return defs, back + front + gloss


def base_page(t):
    defs = grad("gp", *t["paper"])
    body = (
        '<path d="M11 4h18l9 9v29a2 2 0 0 1-2 2H11a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z"'
        ' fill="url(#gp)" stroke="%s" stroke-width="1.2"/>' % t["paperLine"]
    )
    fold = (
        '<path d="M29 4l9 9h-7a2 2 0 0 1-2-2z" fill="%s" stroke="%s" stroke-width="1.2"/>'
        % (t["fold"], t["paperLine"])
    )
    return defs, body + fold


def base_disc(t):
    defs = grad("gd", *t["metal"], x2=1, y2=1)
    body = (
        '<circle cx="24" cy="24" r="19" fill="url(#gd)" stroke="%s" stroke-width="1.2"/>'
        '<path d="M24 5a19 19 0 0 1 15.6 8.2L24 24z" fill="%s" opacity=".55"/>'
        '<circle cx="24" cy="24" r="5.5" fill="#fff" stroke="%s" stroke-width="1.2"/>'
        % (t["metalLine"], t["accent"], t["metalLine"])
    )
    return defs, body


def base_drive(t):
    defs = grad("gm", *t["metal"])
    body = (
        '<path d="M7 15h34a3 3 0 0 1 3 3v12a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3V18a3 3 0 0 1 3-3z"'
        ' fill="url(#gm)" stroke="%s" stroke-width="1.2"/>'
        '<path d="M5.5 16.5h37v6h-37z" fill="#fff" opacity="%s"/>'
        '<circle cx="38" cy="28" r="2" fill="%s"/>'
        % (t["metalLine"], t["gloss"], t["green"])
    )
    return defs, body


def base_stick(t):
    defs = grad("gm", *t["metal"])
    body = (
        '<path d="M17 4h14v9H17z" fill="%s" stroke="%s" stroke-width="1.2"/>'
        '<path d="M13 13h22a2 2 0 0 1 2 2v25a3 3 0 0 1-3 3H14a3 3 0 0 1-3-3V15a2 2 0 0 1 2-2z"'
        ' fill="url(#gm)" stroke="%s" stroke-width="1.2"/>'
        '<path d="M14 30h20v3H14z" fill="%s" opacity=".5"/>'
        % (t["metal"][1], t["metalLine"], t["metalLine"], t["accent"])
    )
    return defs, body


def base_monitor(t):
    defs = grad("gm", *t["metal"]) + grad("gs", *t["glass"])
    body = (
        '<path d="M5 8h38a3 3 0 0 1 3 3v20a3 3 0 0 1-3 3H5a3 3 0 0 1-3-3V11a3 3 0 0 1 3-3z"'
        ' fill="url(#gm)" stroke="%s" stroke-width="1.2"/>'
        '<rect x="5.5" y="11.5" width="37" height="19" rx="1" fill="url(#gs)"/>'
        '<path d="M6 12h36v6H6z" fill="#fff" opacity="%s"/>'
        '<path d="M19 34h10l1.5 7h-13z" fill="%s" stroke="%s" stroke-width="1.2"/>'
        '<rect x="13" y="41" width="22" height="3.5" rx="1.7" fill="%s" stroke="%s" stroke-width="1.2"/>'
        % (t["metalLine"], t["gloss"], t["metal"][1], t["metalLine"],
           t["metal"][1], t["metalLine"])
    )
    return defs, body


def base_box(t):
    defs = ""
    body = (
        '<path d="M24 5l19 8.5L24 22 5 13.5z" fill="%s" stroke="%s" stroke-width="1.1"/>'
        '<path d="M5 13.5L24 22v21L5 34.5z" fill="%s" stroke="%s" stroke-width="1.1"/>'
        '<path d="M43 13.5L24 22v21l19-8.5z" fill="%s" stroke="%s" stroke-width="1.1"/>'
        % (t["folder"][0], t["folderLine"], t["folderBack"][1], t["folderLine"],
           t["folder"][1], t["folderLine"])
    )
    return defs, body


def base_bin(t, full=False):
    defs = grad("gg", *t["glass"])
    ## Full is the same bin with paper standing above the rim, so the 2 read
    ## apart at 16px. A tint difference alone doesn't.
    spill = (
        '<path d="M11 13l3.5-8 4.5 4 4-7 4.5 6.5 4.5-3.5 2.5 8z"'
        ' fill="%s" stroke="%s" stroke-width="1.2"/>'
        '<path d="M17 8.5l2 3.5M27 6l1.5 6" stroke="%s" stroke-width="1.1" opacity=".5" fill="none"/>'
        % (t["fold"], t["metalLine"], t["metalLine"])
        if full else ""
    )
    body = (
        ('' if full else
         '<path d="M20 4h8a2 2 0 0 1 2 2v6H18V6a2 2 0 0 1 2-2z" fill="%s" stroke="%s" stroke-width="1.2"/>'
         % (t["metal"][1], t["metalLine"]))
        + spill
        + '<rect x="8" y="12" width="32" height="5" rx="2" fill="%s" stroke="%s" stroke-width="1.2"/>'
        '<path d="M11 18h26l-2.4 23a3 3 0 0 1-3 2.7H16.4a3 3 0 0 1-3-2.7z"'
        ' fill="url(#gg)" stroke="%s" stroke-width="1.2"/>'
        '<path d="M17 22l1.3 18M24 22v18M31 22l-1.3 18" stroke="%s" stroke-width="1.6"'
        ' opacity=".45" fill="none"/>'
        % (t["metal"][0], t["metalLine"], t["metalLine"], t["accent"])
    )
    return defs, body


def base_window(t):
    defs = grad("gs", *t["glass"]) + grad("gm", *t["metal"])
    body = (
        '<rect x="4" y="8" width="40" height="32" rx="3" fill="url(#gm)"'
        ' stroke="%s" stroke-width="1.2"/>'
        '<path d="M4 11a3 3 0 0 1 3-3h34a3 3 0 0 1 3 3v6H4z" fill="%s"/>'
        '<rect x="7" y="20" width="34" height="17" rx="1" fill="url(#gs)"/>'
        '<circle cx="39" cy="12.5" r="1.8" fill="#fff" opacity=".85"/>'
        % (t["metalLine"], t["accent"])
    )
    return defs, body


def base_camera(t):
    defs = grad("gm", *t["metal"]) + grad("gs", *t["glass"], x2=1, y2=1)
    body = (
        '<path d="M18 8h12l2.5 5H41a3 3 0 0 1 3 3v20a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3V16a3 3 0 0 1 3-3h8.5z"'
        ' fill="url(#gm)" stroke="%s" stroke-width="1.2"/>'
        '<circle cx="24" cy="27" r="9" fill="url(#gs)" stroke="%s" stroke-width="1.2"/>'
        '<circle cx="21" cy="24" r="3" fill="#fff" opacity=".7"/>'
        % (t["metalLine"], t["metalLine"])
    )
    return defs, body


def base_phone(t):
    defs = grad("gm", *t["metal"]) + grad("gs", *t["glass"])
    body = (
        '<rect x="13" y="3" width="22" height="42" rx="4" fill="url(#gm)"'
        ' stroke="%s" stroke-width="1.2"/>'
        '<rect x="16" y="9" width="16" height="26" rx="1" fill="url(#gs)"/>'
        '<circle cx="24" cy="40" r="2.4" fill="%s" opacity=".6"/>'
        % (t["metalLine"], t["metalLine"])
    )
    return defs, body


BASES = {
    "folder": lambda t: base_folder(t, False),
    "folder-open": lambda t: base_folder(t, True),
    "page": base_page,
    "disc": base_disc,
    "drive": base_drive,
    "stick": base_stick,
    "monitor": base_monitor,
    "box": base_box,
    "bin": base_bin,
    "window": base_window,
    "camera": base_camera,
    "phone": base_phone,
}


# ---------------------------------------------------------------------------
# Glyphs, drawn in a 24x24 box and placed by the caller.

GLYPHS = {
    "lines":     "M4 6h16v2.4H4zm0 5h16v2.4H4zm0 5h11v2.4H4z",
    "down":      "M12 3h4.6v9H21l-9 9-9-9h4.4V3z",
    "up":        "M12 21H7.4v-9H3l9-9 9 9h-4.4v9z",
    "note":      "M18 3v12.2a4 4 0 1 1-2.6-3.7V7.4l-6 1.6v9.2a4 4 0 1 1-2.6-3.7V6z",
    "photo":     "M2 5h20v14H2zm2.5 11.5L9 11l3.4 4L16 10l4 6.5zm11-9.2a2 2 0 1 0 0 4 2 2 0 0 0 0-4z",
    "film":      "M2 5h20v14H2zm2 2h2.4v2.2H4zm0 4h2.4v2.2H4zm0 4h2.4v2.2H4zm13.6-8H20v2.2h-2.4zm0 4H20v2.2h-2.4zm0 4H20v2.2h-2.4zM8.4 7h7.2v10H8.4z",
    "play":      "M8 4l11 8-11 8z",
    "people":    "M8 11a3.4 3.4 0 1 0 0-6.8A3.4 3.4 0 0 0 8 11zm8.5 0a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM2 20v-2.6C2 14.8 4.7 13 8 13s6 1.8 6 4.4V20zm14 0v-2.6c0-1.5-.6-2.8-1.6-3.7 3 .2 5.6 1.9 5.6 4.1V20z",
    "globe":     "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm0 2c1.6 0 3.3 2.4 3.8 6H8.2C8.7 6.4 10.4 4 12 4zM7.9 10h8.2a20 20 0 0 1 0 4H7.9a20 20 0 0 1 0-4zm.3 6h7.6c-.5 3.6-2.2 6-3.8 6s-3.3-2.4-3.8-6z",
    "clock":     "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm1.2 4v6.3l4.4 2.6-1.2 2-5.4-3.2V6z",
    "search":    "M10 2a8 8 0 1 0 4.9 14.3l5.1 5.1 2-2-5.1-5.1A8 8 0 0 0 10 2zm0 2.6a5.4 5.4 0 1 1 0 10.8 5.4 5.4 0 0 1 0-10.8z",
    "house":     "M12 2l10 9h-3v11h-5v-6.5h-4V22H5V11H2z",
    "plus":      "M10 3h4v7h7v4h-7v7h-4v-7H3v-4h7z",
    "bookmark":  "M6 2h12v20l-6-5-6 5z",
    "gear":      "M12 8.4a3.6 3.6 0 1 0 0 7.2 3.6 3.6 0 0 0 0-7.2zm9.2 5.1l2.3 1.8-2.3 4-2.8-1.1a9 9 0 0 1-2 1.2L15.9 22h-4.6l-.5-2.9a9 9 0 0 1-2-1.2L6 19l-2.3-4L6 13.2a9 9 0 0 1 0-2.4L3.7 9 6 5l2.8 1.1a9 9 0 0 1 2-1.2L11.3 2h4.6l.5 2.9a9 9 0 0 1 2 1.2L21.2 5l2.3 4-2.3 1.8a9 9 0 0 1 0 2.7z",
    "terminal":  "M3 5l7 7-7 7-2-2 5-5-5-5zm9 12h10v2.6H12z",
    "grid":      "M2 4h20v16H2zm0 5.3h20M2 14.6h20M9 4v16M15.5 4v16",
    "letterA":   "M12 2l9 20h-4.2l-1.8-4.3H9L7.2 22H3zm0 6.4L10.3 14h3.4z",
    "question":  "M12 2a7 7 0 0 0-7 7h4a3 3 0 1 1 4.4 2.7c-1.5.9-2.4 2-2.4 3.8V17h4v-1.1c0-.8.4-1.3 1.3-1.9A6.9 6.9 0 0 0 12 2zm-2 17h4v4h-4z",
    "person":    "M12 3a4.4 4.4 0 1 0 0 8.8A4.4 4.4 0 0 0 12 3zM3.5 21v-2.4C3.5 15.4 7.3 13.5 12 13.5s8.5 1.9 8.5 5.1V21z",
    "calendar":  "M4 4h16v17H4zm0 5h16M8 1.5v5M16 1.5v5",
    "seal":      "M12 2l2.6 2.3 3.4-.6.9 3.3 3 1.7-1.7 3 1.7 3-3 1.7-.9 3.3-3.4-.6L12 22l-2.6-2.3-3.4.6-.9-3.3-3-1.7 1.7-3-1.7-3 3-1.7.9-3.3 3.4.6z",
    "zip":       "M10 2h4v3h-4zm0 4h4v3h-4zm0 4h4v3h-4zm0 4h4v3.5l-2 3-2-3z",
    "eye":       "M12 5C6.5 5 2.4 8.6 1 12c1.4 3.4 5.5 7 11 7s9.6-3.6 11-7c-1.4-3.4-5.5-7-11-7zm0 3.2a3.8 3.8 0 1 1 0 7.6 3.8 3.8 0 0 1 0-7.6z",
    "star":      "M12 2l3 6.8 7.4.7-5.6 4.9 1.7 7.2L12 17.8 5.5 21.6l1.7-7.2L1.6 9.5 9 8.8z",
    "wave":      "M12 3a15 15 0 0 1 10.6 4.4l-2.6 2.6A11.3 11.3 0 0 0 12 6.7 11.3 11.3 0 0 0 4 10L1.4 7.4A15 15 0 0 1 12 3zm0 6.4a8.6 8.6 0 0 1 6 2.4l-2.6 2.6A5 5 0 0 0 12 13a5 5 0 0 0-3.4 1.4L6 11.8a8.6 8.6 0 0 1 6-2.4zm0 6a3 3 0 1 1 0 6 3 3 0 0 1 0-6z",
    "plug":      "M9 2h2.6v6H9zm4.4 0H16v6h-2.6zM6 9h13v5a6.5 6.5 0 0 1-5.2 6.4V24h-2.6v-3.6A6.5 6.5 0 0 1 6 14z",
    "server":    "M4 4h16v6H4zm0 8h16v6H4zM7 6.4h2.4v1.2H7zm0 8h2.4v1.2H7z",
    "paper":     "M7 2h10l4 4v16H7z",
}

# Glyphs that are strokes, not fills.
STROKE_GLYPHS = {"grid", "calendar"}

# Subpaths meant as holes, not as more ink.
EVENODD_GLYPHS = {"photo", "film", "letterA", "server"}


def glyph(name, color, x, y, size, opacity="1"):
    if name not in GLYPHS:
        return ""
    scale = size / 24.0
    if name in STROKE_GLYPHS:
        paint = 'fill="none" stroke="%s" stroke-width="1.8"' % color
    elif name in EVENODD_GLYPHS:
        paint = 'fill="%s" fill-rule="evenodd"' % color
    else:
        paint = 'fill="%s"' % color
    return (
        '<g transform="translate(%s %s) scale(%s)" opacity="%s">'
        '<path d="%s" %s/></g>' % (fmt(x), fmt(y), fmt(scale), opacity, GLYPHS[name], paint)
    )


def fmt(value):
    text = ("%.3f" % value).rstrip("0").rstrip(".")
    return text if text else "0"


def band(color):
    """A color band down the left edge of a page, the way office types read."""
    return (
        '<path d="M9 30h29v12a2 2 0 0 1-2 2H11a2 2 0 0 1-2-2z" fill="%s"/>'
        '<path d="M9 30h29v2.5H9z" fill="#000" opacity=".12"/>' % color
    )


# ---------------------------------------------------------------------------
# The set. name -> (context, base, glyph, glyph color key, placement)
#
# placement: "front" is on a folder's front panel, "badge" is the lower-right
# corner of a page, "band" pairs with a colored office band, "none" is the bare
# shape.

FRONT = (14.0, 22.0, 20.0, ".85")
BADGE = (24.0, 26.0, 17.0, "1")
MIDDLE = (12.0, 12.0, 24.0, ".9")

ICONS = [
    # name, context, base, glyph, color, placement
    ("folder",                  "places", "folder",      None,       None,     None),
    ("inode-directory",         "places", "folder",      None,       None,     None),
    ("folder-open",             "places", "folder-open", None,       None,     None),
    ("folder-visiting",         "places", "folder-open", None,       None,     None),
    ("folder-drag-accept",      "places", "folder-open", "plus",     "green",  FRONT),
    ("folder-new",              "places", "folder",      "plus",     "glyph",  FRONT),
    ("folder-documents",        "places", "folder",      "lines",    "glyph",  FRONT),
    ("folder-download",         "places", "folder",      "down",     "glyph",  FRONT),
    ("folder-music",            "places", "folder",      "note",     "glyph",  FRONT),
    ("folder-pictures",         "places", "folder",      "photo",    "glyph",  FRONT),
    ("folder-videos",           "places", "folder",      "film",     "glyph",  FRONT),
    ("folder-publicshare",      "places", "folder",      "people",   "glyph",  FRONT),
    ("folder-templates",        "places", "folder",      "star",     "glyph",  FRONT),
    ("folder-remote",           "places", "folder",      "globe",    "glyph",  FRONT),
    ("folder-recent",           "places", "folder",      "clock",    "glyph",  FRONT),
    ("folder-saved-search",     "places", "folder",      "search",   "glyph",  FRONT),
    ("user-home",               "places", "folder",      "house",    "glyph",  FRONT),
    ("user-bookmarks",          "places", "folder",      "bookmark", "glyph",  FRONT),
    ("user-desktop",            "places", "monitor",     None,       None,     None),
    ("user-trash",              "places", "bin",         None,       None,     None),
    ("network-workgroup",       "places", "monitor",     "globe",    "accent", BADGE),
    ("network-server",          "places", "drive",       "globe",    "accent", BADGE),

    ("text-x-generic",          "mimetypes", "page", "lines",    "paperLine", MIDDLE),
    ("text-x-generic-template", "mimetypes", "page", "star",     "warm",      BADGE),
    ("text-x-preview",          "mimetypes", "page", "eye",      "accent",    BADGE),
    ("text-x-script",           "mimetypes", "page", "terminal", "accent",    BADGE),
    ("application-x-shellscript", "mimetypes", "page", "terminal", "green",   BADGE),
    ("text-html",               "mimetypes", "page", "globe",    "accent",    BADGE),
    ("application-x-generic",   "mimetypes", "page", "gear",     "metalLine", BADGE),
    ("application-x-executable", "mimetypes", "window", None,    None,        None),
    ("application-certificate", "mimetypes", "page", "seal",     "warm",      BADGE),
    ("application-zip",         "mimetypes", "page", "zip",      "warm",      BADGE),
    ("package-x-generic",       "mimetypes", "box",  None,       None,        None),
    ("image-x-generic",         "mimetypes", "page", "photo",    "green",     BADGE),
    ("audio-x-generic",         "mimetypes", "page", "note",     "purple",    BADGE),
    ("video-x-generic",         "mimetypes", "page", "play",     "red",       BADGE),
    ("font-x-generic",          "mimetypes", "page", "letterA",  "accent",    BADGE),
    ("x-office-address-book",   "mimetypes", "page", "person",   "accent",    BADGE),
    ("x-office-calendar",       "mimetypes", "page", "calendar", "red",       BADGE),
    ("unknown",                 "mimetypes", "page", "question", "metalLine", BADGE),

    ("computer",                "devices", "monitor", None,   None,   None),
    ("drive-harddisk",          "devices", "drive",   None,   None,   None),
    ("drive-harddisk-system",   "devices", "drive",   "house", "glyph", BADGE),
    ("drive-harddisk-usb",      "devices", "drive",   "plug",  "glyph", BADGE),
    ("drive-multidisk",         "devices", "drive",   "grid",  "glyph", BADGE),
    ("drive-removable-media",   "devices", "stick",   None,   None,   None),
    ("media-flash",             "devices", "stick",   None,   None,   None),
    ("media-removable",         "devices", "stick",   None,   None,   None),
    ("drive-optical",           "devices", "disc",    None,   None,   None),
    ("media-optical",           "devices", "disc",    None,   None,   None),
    ("media-tape",              "devices", "drive",   None,   None,   None),
    ("camera-photo",            "devices", "camera",  None,   None,   None),
    ("multimedia-player",       "devices", "phone",   "note", "accent", BADGE),
    ("phone",                   "devices", "phone",   None,   None,   None),
    ("network-wired",           "devices", "drive",   "plug", "accent", BADGE),
    ("network-wireless",        "devices", "drive",   "wave", "accent", BADGE),
]

# Office types get a colored band rather than a badge glyph.
OFFICE = {
    "x-office-document":     ("accent", "lines"),
    "x-office-spreadsheet":  ("green", "grid"),
    "x-office-presentation": ("warm", "play"),
    "application-pdf":       ("red", "paper"),
}

# Same drawing, second name. Cheaper than a second file only if the theme spec
# allowed aliases, and it doesn't. GTK wants a real file per name.
ALIASES = {
    "user-trash-full": "user-trash",
    "start-here": "user-home",
}


def build(theme_name, theme, out_root):
    out_dir = os.path.join(out_root, theme_name)
    written = {}

    def emit(name, context, defs, body):
        path = os.path.join(out_dir, "scalable", context, name + ".svg")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        svg = (
            '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
            'viewBox="0 0 %d %d">' % (CANVAS, CANVAS, CANVAS, CANVAS)
        )
        if defs:
            svg += "<defs>%s</defs>" % defs
        svg += body + "</svg>"
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(svg)
        written[name] = (context, defs, body)

    for name, context, base, glyph_name, color, place in ICONS:
        defs, body = BASES[base](theme)
        if glyph_name and place:
            x, y, size, opacity = place
            body += glyph(glyph_name, theme[color], x, y, size, opacity)
        emit(name, context, defs, body)

    for name, (color, glyph_name) in OFFICE.items():
        defs, body = base_page(theme)
        body += band(theme[color])
        body += glyph(glyph_name, "#FFFFFF", 27.0, 32.0, 14.0, ".95")
        body += glyph("lines", theme["paperLine"], 13.0, 12.0, 15.0, ".8")
        emit(name, "mimetypes", defs, body)

    defs, body = base_bin(theme, full=True)
    emit("user-trash-full", "places", defs, body)

    for alias, source in ALIASES.items():
        if alias in written:
            continue
        context, defs, body = written[source]
        emit(alias, context, defs, body)

    write_index(out_dir, theme_name, theme)
    return out_dir


def write_index(out_dir, theme_name, theme):
    contexts = sorted(os.listdir(os.path.join(out_dir, "scalable")))
    labels = {"places": "Places", "mimetypes": "MimeTypes", "devices": "Devices"}
    lines = [
        "[Icon Theme]",
        "Name=%s" % theme_name,
        "Comment=%s" % theme["comment"],
        "X-Nemo-Style=%s" % theme["style"],
        # Colorful art on either background, and the monochrome half comes from
        # Adwaita, which GTK recolors to the foreground either way.
        "X-Nemo-Modes=light;dark",
        "Inherits=Adwaita,hicolor",
        "Example=folder",
        "Directories=%s" % ",".join("scalable/" + c for c in contexts),
        "",
    ]
    for context in contexts:
        lines += [
            "[scalable/%s]" % context,
            "Size=48",
            "MinSize=8",
            "MaxSize=512",
            "Context=%s" % labels.get(context, "Applications"),
            "Type=Scalable",
            "",
        ]
    with open(os.path.join(out_dir, "index.theme"), "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
    out_root = os.path.abspath(os.path.join(root, "assets", "icons"))

    for theme_name, theme in THEMES.items():
        out_dir = build(theme_name, theme, out_root)
        total = 0
        count = 0
        for base, _dirs, files in os.walk(out_dir):
            for name in files:
                total += os.path.getsize(os.path.join(base, name))
                count += 1
        print("[ %s (%s): %d files, %d KB ]" % (theme_name, theme["style"], count, total / 1024))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
