#!/usr/bin/env python3

##	Purpose:
##		Record the nemo-anywhere demo video and README gif: drives a real build on
##		a private Xvfb (never :0) inside a decorated window, works a synthetic home
##		with the mouse and keyboard, lays down click and key foley synced to the
##		actual input timestamps, overlays per-segment narration, and encodes the
##		deliverables. Two recordings from one script:
##		  video: 1920x1080@60 h265, GDK_SCALE=2, with audio
##		  gif:   960x540@50, GDK_SCALE=1, optimized palette, silent
##		Both render the SAME layout - the video is the gif at twice the scale - so
##		every scene coordinate is written once, in gif pixels, and multiplied by
##		the profile's scale. That is also why the client area is a fixed logical
##		size rather than whatever is left after the window decoration: the black
##		border absorbs the decoration instead of the layout doing it.
##		There is no GPU path here. GTK3 draws through cairo, and a file manager
##		moves far less of the screen per frame than a terminal does, so llvmpipe
##		keeps up with both capture rates.
##		Narration lives in a black band above the window (BAND px of bare root,
##		the window sits below it) - plain yellow text, no box, so nothing ever
##		covers the app. The band is static, which costs a gif almost nothing.
##		The window decoration is generated at record time (a square-cornered dark
##		theme recolored slate blue-gray) so it reads as chrome rather than as part
##		of the app.
##		Settings changed mid-run are written straight into the settings file,
##		which the app live-reloads - the same thing a hand edit does, and the
##		reason no preferences dialog has to be driven on camera.
##		Everything on screen is synthetic: a throwaway HOME under a neutral path,
##		generated files, and no column that would print a real account name.
##	Syntax:
##		demo-video.py [--profile video,gif] [--segments a,b,...] [--seed N]
##		              [--keep-work] [--no-rotate] [--no-asset] [--display :94]
##		              [--out-dir DIR] [--shot FILE]
##		Env: NEMO_BIN overrides the binary (default: the release prefix staged at
##		cicd/artifacts/dogfood).
##		--shot brings the display and the app up, writes one screenshot and stops.
##		That is how scene coordinates are measured; they are all in gif pixels
##		relative to the client area's top-left corner.
##	Notes:
##		AV sync needs no calibration: before the app launches, the bare root is
##		flashed white (xsetroot) at a recorded wall-clock time; the bright frame
##		is found in the capture afterwards, anchoring every event epoch to video
##		time exactly. Sound assets + licenses live in ./sounds/ (see LICENSES.txt).
##	History: at bottom.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

import argparse
import json
import math
import os
import pwd
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import wave
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import signal as spsig

ME_DIR   = Path(__file__).resolve().parent
REPO     = ME_DIR.parents[2]                  # github/cicd/utility/demo-video -> github
OUT_DIR  = REPO.parent / "demo-video"         # beside the repo, not in it
SOUNDS   = ME_DIR / "sounds"

SR         = 48000                            # audio mix rate
BANNER_TTF = "/usr/share/fonts/truetype/lato/Lato-Semibold.ttf"
BANNER_FG  = "0xFFD866"                       # warm yellow, on the black band above the window
LEAD_S     = 0.8                              # quiet lead-in kept before the first segment
TAIL_HOLD_S  = 2.0                            # freeze the final frame this long at the end...
# ...then black. The gif cuts to it, since a fade is a fresh frame every step and
# bloats a gif; three seconds makes the loop point plain. The video fades in, and
# fades the held frame out to black instead.
GIF_TAIL_BLACK_S   = 3.0
VIDEO_FADE_IN_S    = 0.5
VIDEO_TAIL_BLACK_S = 0.5
TAIL_EXTRA = {"gif": TAIL_HOLD_S + GIF_TAIL_BLACK_S, "video": TAIL_HOLD_S + VIDEO_TAIL_BLACK_S}
FOLEY_LAG  = 0.03                             # foley sits this far after the input event

# Logical geometry, in gif pixels. The video profile multiplies all of it by 2
# (GDK_SCALE=2), so one set of scene coordinates serves both. The client is a
# fixed size and the black border takes up the slack from the decoration, which
# does NOT scale with GDK_SCALE - otherwise the two profiles would lay out
# differently and no coordinate could be shared.
VIEW_W, VIEW_H = 960, 540
BAND     = 52                                 # narration strip above the window
BORDER   = 6                                  # nominal black margin around the window
# The client is small enough that the decoration fits in either profile with room
# to spare; place_window centers what is left, so the real margin is whatever the
# titlebar did not use.
CLIENT_W, CLIENT_H = 940, 440
FRAME_L, FRAME_R, FRAME_T, FRAME_B = 2, 2, 32, 2   # fallback until the WM sets the hint

# The decoration is built at record time from a square-cornered dark theme (its
# parts are flat one-color SVGs, so a color swap is the whole job) and dropped in
# the WM's own throwaway HOME - nothing is installed system-wide.
WM_BASE_THEME = "Material-Black-Pistachio"    # SQUARE corners (opaque top-left)
WM_THEME      = "NemoDemo"
DECO_BG       = "#44506b"                     # active titlebar + frame
DECO_BG_OFF   = "#2e3547"                     # inactive
DECO_GLYPH    = "#dde5f2"                     # button glyphs
DECO_TEXT     = "#e9eefa"                     # title text

# The app's own look. Adwaita ships with GTK itself, so a recording does not
# depend on whatever themes a box happens to have installed.
GTK_THEME  = "Adwaita"
ICON_THEME = "Adwaita"
UI_FONT    = "Lato"
UI_PT      = 10

# Xvfb display. :95 through :99 are claimed by other tooling on the build box,
# this project's own headless GUI checks included, and a sister project's demo
# recorder was found squatting on :94 with no static claim on it anywhere. So
# the number below is only where the search STARTS: a taken display is stepped
# over rather than failed on, since a recording that dies because something else
# got there first is a bad trade for a number nobody can reserve. Pass --display
# to pin one.
DEFAULT_DISPLAY = ":93"
DISPLAY_TRIES   = 6         # how far down from the default to look

# Where the app thinks its home is. The search results' Location column, the
# breadcrumb and any error dialog all print real paths, so the synthetic home is
# bind-mounted here in an unprivileged user namespace and every path on screen
# comes out generic. Nothing in this needs root.
DEMO_HOME = "/home/juno"
# the app's own prefix, mounted somewhere fixed inside that namespace - the build
# it was recorded from usually sits under the real home, which the fake one hides
DEMO_PREFIX = "/opt/nemo-anywhere"

# run as its own process by reserve_band; the window lives as long as it does
BAND_STRUT = """
import sys, time
from Xlib import X, display
disp = display.Display()
scr = disp.screen()
width, band = int(sys.argv[1]), int(sys.argv[2])
win = scr.root.create_window(0, 0, 1, 1, 0, scr.root_depth, X.InputOutput,
    X.CopyFromParent, background_pixel=scr.black_pixel)
atom = disp.intern_atom
win.change_property(atom("_NET_WM_WINDOW_TYPE"), atom("ATOM"), 32,
    [atom("_NET_WM_WINDOW_TYPE_DOCK")])
win.change_property(atom("_NET_WM_STRUT"), atom("CARDINAL"), 32, [0, 0, band, 0])
win.change_property(atom("_NET_WM_STRUT_PARTIAL"), atom("CARDINAL"), 32,
    [0, 0, band, 0, 0, 0, 0, 0, 0, width - 1, 0, 0])
win.map()
disp.sync()
while True:
    time.sleep(3600)
"""

PROFILES = {
    "video": dict(scale=2, cap_fps=60, out_fps=60, banner_fs=36, audio=True, banner_min=4.0),
    "gif":   dict(scale=1, cap_fps=50, out_fps=50, banner_fs=22, audio=False, banner_min=3.0),
}

def log(msg):
    print(f"[demo] {msg}", flush=True)

def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)

def out_of(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Recorder: display/app/capture lifecycle + the event/banner logs

def run_user():
    # gui-headless.bash names its run folder the same way; USER is unset under
    # cron and in some ssh contexts
    return os.environ.get("USER") or pwd.getpwuid(os.getuid()).pw_name

def display_taken(num):
    """True if an X server is alive on :num, by the lock file's pid."""
    lock = Path(f"/tmp/.X{num}-lock")
    if not lock.exists():
        return False
    try:
        pid = int(lock.read_text().strip())
    except (ValueError, OSError):
        return True                            # unreadable: treat as taken
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False                           # stale lock, nothing holding it
    except PermissionError:
        return True                            # somebody else's server
    return True

def pick_display(wanted, explicit):
    if explicit or not display_taken(wanted.lstrip(":")):
        return wanted
    base = int(wanted.lstrip(":"))
    for num in range(base - 1, base - DISPLAY_TRIES, -1):
        if not display_taken(num):
            log(f"display {wanted} is taken; using :{num}")
            return f":{num}"
    raise RuntimeError(f"no free display from {wanted} down {DISPLAY_TRIES}")

def app_prefix():
    # the app is a relocatable prefix, not one binary: its icons, resources and
    # helper programs all hang off the same root, so the root is what matters
    env = os.environ.get("NEMO_BIN")
    if env:
        return Path(env).resolve().parents[1]
    return REPO / "cicd/artifacts/dogfood/nemo-anywhere"


class Rec:
    def __init__(self, args, profile):
        self.p        = profile
        self.scale    = profile["scale"]
        self.size     = (VIEW_W * self.scale, VIEW_H * self.scale)
        self.band     = BAND * self.scale
        self.cap_fps  = profile["cap_fps"]
        self.out_fps  = profile["out_fps"]
        self.display  = args.display
        self.num      = self.display.lstrip(":")
        self.auth     = f"/tmp/cicd-gui-headless-{run_user()}/Xauthority-{self.num}"
        self.prefix   = app_prefix()
        self.bin      = f"{DEMO_PREFIX}/bin/nemo-anywhere"
        self.work     = Path(tempfile.mkdtemp(prefix="nemo-demo-"))
        # two names for one directory: where this script writes it, and where the
        # app sees it through the bind mount (see DEMO_HOME)
        self.home     = self.work / "home" / "juno"
        self.wmhome   = self.work / "wmhome"    # the WM's HOME: theme + its own xfconf
        self.keep     = args.keep_work
        self.events   = []      # (epoch, kind) kind: key:NAME / mouse:NAME
        self.banners  = []      # (epoch_start, epoch_end, text)
        self.app      = None
        self.ff       = None
        self.flash_e  = 0.0     # wall-clock epoch of the white sync flash
        self.t0_e     = 0.0     # wall-clock epoch where trimmed content starts
        self.origin   = (BORDER * self.scale, (BAND + BORDER) * self.scale)
        self.seg_marks = {}     # segment name -> wall-clock epoch it started

    @property
    def settings(self):
        return self.home / ".config/nemo-anywhere/settings.shcl"

    def env(self):
        e = dict(os.environ)
        e.update(DISPLAY=self.display, XAUTHORITY=self.auth)
        # setting DISPLAY is not enough on a Wayland session - a GTK app prefers
        # Wayland whenever WAYLAND_DISPLAY is set, and the window would open on the
        # real desktop where nothing here could find it
        for k in ("WAYLAND_DISPLAY", "XDG_SESSION_TYPE"):
            e.pop(k, None)
        return e

    def xdo(self, *a):
        subprocess.run(["xdotool", *a], env=self.env(), check=False,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # --- geometry --------------------------------------------------------------
    def pt(self, lx, ly):
        """A point inside the client area, from gif-pixel coordinates."""
        return (self.origin[0] + int(lx * self.scale),
            self.origin[1] + int(ly * self.scale))

    def dlg(self, lx, ly):
        """A point in the dialog that has focus, in gif pixels from its client
        area's top-left, or from its bottom edge when ly is negative.

        A dialog cannot share the main window's coordinates. The titlebar does
        not scale with GDK_SCALE, so a dialog centered on the window sits at a
        different logical height in each profile, and one sized to the work
        area (Compress with its options open) is taller in the video too. So
        the dialog is found and measured each time. A popup it opens, such as
        a dropdown list, is placed from the dialog, so it follows along.
        """
        win = out_of(["xdotool", "getactivewindow"], env=self.env()).strip()
        x, y = self._client_xy(win)
        if ly < 0:
            info = out_of(["xwininfo", "-id", win], env=self.env())
            y += int(re.search(r"Height:\s*(\d+)", info).group(1))
        return x + int(lx * self.scale), y + int(ly * self.scale)

    def _frame_extents(self, win):
        # _NET_FRAME_EXTENTS = left, right, top, bottom (px)
        r = subprocess.run(["xprop", "-id", win, "_NET_FRAME_EXTENTS"],
            env=self.env(), capture_output=True, text=True)
        m = re.search(r"=\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", r.stdout)
        return tuple(map(int, m.groups())) if m else (FRAME_L, FRAME_R, FRAME_T, FRAME_B)

    def _client_xy(self, win):
        r = subprocess.run(["xwininfo", "-id", win], env=self.env(),
            capture_output=True, text=True).stdout
        x = int(re.search(r"Absolute upper-left X:\s*(-?\d+)", r).group(1))
        y = int(re.search(r"Absolute upper-left Y:\s*(-?\d+)", r).group(1))
        return x, y

    def place_window(self, win):
        # center the decorated window in what is left below the narration band, and
        # remember where its CLIENT area starts - every scene coordinate is measured
        # from there. xdotool's move semantics against a reparenting frame are fuzzy,
        # so measure the real frame after each move and correct by the residual.
        left, r, t, b = self._frame_extents(win)
        cw, ch = CLIENT_W * self.scale, CLIENT_H * self.scale
        want_x = max(0, (self.size[0] - (cw + left + r)) // 2) + left
        want_y = self.band + max(0, (self.size[1] - self.band - (ch + t + b)) // 2) + t
        target = [want_x, want_y]
        for _ in range(4):
            self.xdo("windowmove", win, str(target[0]), str(target[1]))
            time.sleep(0.25)
            cx, cy = self._client_xy(win)
            dx, dy = want_x - cx, want_y - cy
            if abs(dx) <= 1 and abs(dy) <= 1:
                break
            target[0] += dx
            target[1] += dy
        self.origin = self._client_xy(win)

    def make_theme(self):
        base = Path("/usr/share/themes") / WM_BASE_THEME / "xfwm4"
        if not base.is_dir():
            log(f"WARNING: theme {WM_BASE_THEME} not installed - using the WM default")
            return "Default"
        dst = self.wmhome / ".themes" / WM_THEME / "xfwm4"
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(base, dst, dirs_exist_ok=True)
        for svg in dst.rglob("*.svg"):
            text = svg.read_text()
            svg.write_text(text.replace("#09090a", DECO_BG)
                .replace("#1a1c1e", DECO_BG_OFF).replace("#a3a3a3", DECO_GLYPH))
        rc = dst / "themerc"
        rc.write_text(re.sub(r"(?m)^(active_text(_shadow)?_color)=.*",
            rf"\1={DECO_TEXT}", rc.read_text()))
        return WM_THEME

    def start_display(self):
        gh = str(REPO / "cicd/utility/gui-headless.bash")
        e = dict(os.environ, CICD_HEADLESS_DISPLAY=self.display,
            CICD_HEADLESS_SIZE=f"{self.size[0]}x{self.size[1]}x24")
        subprocess.run([gh, "stop"], env=e, capture_output=True)
        run([gh, "start"], env=e)
        self.start_wm(self.make_theme())
        time.sleep(2.0)
        self.reserve_band()
        # pure black, so the margin around the window reads as black rather than a tint
        subprocess.run(["xsetroot", "-solid", "#000000"], env=self.env(), check=False)

    def reserve_band(self):
        # A dialog that sizes itself to the work area (Compress with its options
        # open) would otherwise grow up into the band and sit under the captions.
        # xfwm4's margin_top does not change _NET_WORKAREA; a strut does. One
        # pixel of dock window is enough to carry it, and keeps the sync flash
        # nearly all white.
        self.strut = subprocess.Popen([sys.executable, "-c", BAND_STRUT,
            str(self.size[0]), str(self.band)], env=self.env(),
            stdout=subprocess.DEVNULL, stderr=open(self.work / "strut.log", "w"))
        time.sleep(0.6)
        if self.strut.poll() is not None:
            log("WARNING: could not reserve the caption band (see strut.log)")

    def wm_env(self):
        # xfconfd keeps its channels under XDG_CONFIG_HOME, not HOME, so with the
        # desktop's own one inherited a recording would write its theme and title
        # font over the real desktop's. Every base folder goes inside wmhome.
        e = self.env()
        e["HOME"] = str(self.wmhome)
        for var, sub in (("XDG_CONFIG_HOME", ".config"), ("XDG_DATA_HOME", ".local/share"),
                ("XDG_CACHE_HOME", ".cache"), ("XDG_STATE_HOME", ".local/state"),
                ("XDG_RUNTIME_DIR", "run")):
            d = self.wmhome / sub
            d.mkdir(parents=True, exist_ok=True, mode=0o700)
            e[var] = str(d)
        return e

    def start_wm(self, theme, wm="xfwm4 --compositor=off --vblank=off"):
        # its own session, so stop_wm can end the bus and xfconfd with it
        title_pt = 9 * self.scale
        self.wm = subprocess.Popen(["dbus-run-session", "--", "sh", "-c",
            f'xfconf-query -c xfwm4 -p /general/theme --create -t string -s "{theme}"; '
            f'xfconf-query -c xfwm4 -p /general/title_font --create -t string -s "Lato Bold {title_pt}"; '
            'xfconf-query -c xfwm4 -p /general/button_layout --create -t string -s "O|HMC"; '            f"exec {wm}"],
            env=self.wm_env(), stdout=open(self.work / "wm.log", "w"),
            stderr=subprocess.STDOUT, start_new_session=True)

    def stop_wm(self):
        # dbus-run-session dies on SIGTERM and leaves its bus, xfconfd and the WM
        # running under init. They all stay in its process group, so end that.
        if not getattr(self, "wm", None):
            return
        for sig in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.killpg(self.wm.pid, sig)
            except ProcessLookupError:
                break
            deadline = time.time() + 5
            while time.time() < deadline and self.wm_survivors():
                time.sleep(0.1)
            if not self.wm_survivors():
                break
        try:
            self.wm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        self.wm = None

    def wm_survivors(self):
        self.wm.poll()                         # reap the leader, or it counts as alive
        try:
            os.killpg(self.wm.pid, 0)
            return True
        except ProcessLookupError:
            return False

    def stop_display(self):
        if getattr(self, "strut", None):
            self.strut.kill()
            self.strut.wait()
            self.strut = None
        self.stop_wm()
        gh = str(REPO / "cicd/utility/gui-headless.bash")
        e = dict(os.environ, CICD_HEADLESS_DISPLAY=self.display)
        subprocess.run([gh, "stop"], env=e, capture_output=True)

    def start_capture(self):
        self.raw = self.work / "raw.mkv"
        self.ff = subprocess.Popen([
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-progress", str(self.work / "ffprogress.txt"),
            "-f", "x11grab", "-draw_mouse", "1", "-framerate", str(self.cap_fps),
            "-video_size", f"{self.size[0]}x{self.size[1]}", "-i", self.display,
            "-c:v", "libx264", "-preset", "ultrafast", "-qp", "0",
            "-pix_fmt", "yuv444p", str(self.raw)],
            env=self.env(), stdin=subprocess.DEVNULL,
            stderr=open(self.work / "ffmpeg.log", "w"))
        # flash only once frames are actually flowing - a slow-opening ffmpeg would
        # otherwise miss the sync flash and break the whole AV anchor
        prog = self.work / "ffprogress.txt"
        deadline = time.time() + 30
        while time.time() < deadline:
            if prog.exists() and re.search(r"(?m)^frame=([1-9]\d*)", prog.read_text()):
                break
            time.sleep(0.3)
        else:
            raise RuntimeError("x11grab produced no frames (see ffmpeg.log)")
        time.sleep(0.8)
        subprocess.run(["xsetroot", "-solid", "white"], env=self.env(), check=False)
        self.flash_e = time.time()
        time.sleep(0.25)
        subprocess.run(["xsetroot", "-solid", "#000000"], env=self.env(), check=False)
        self.mouse_park()
        time.sleep(0.4)

    def stop_capture(self):
        if self.ff:
            self.ff.send_signal(signal.SIGINT)
            try:
                self.ff.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.ff.kill()
            self.ff = None

    def data_dirs(self):
        """System data the app may see: everything but the installed applications.

		A misplaced double-click otherwise opens whatever the box has registered
		for that file type, on this display, in the middle of a take - and the
		stray window carries the real working paths with it. With no applications
		directory in reach there is no handler to find, so the worst case is a
		dialog saying so.
		"""
        d = self.work / "datadirs"
        d.mkdir(exist_ok=True)
        for sub in ("icons", "mime", "glib-2.0", "themes", "pixmaps"):
            src = Path("/usr/share") / sub
            link = d / sub
            if src.is_dir() and not link.exists():
                link.symlink_to(src)
        return f"{DEMO_PREFIX}/share:{d}"

    def sandbox(self):
        """Run the app with the synthetic home mounted where it looks ordinary."""
        # the two tmpfs lines are what make the mount points: the namespace has no
        # more right to mkdir in the real / than this account does
        return ["bwrap", "--dev-bind", "/", "/", "--tmpfs", "/home", "--tmpfs", "/opt",
            "--ro-bind", str(self.prefix), DEMO_PREFIX,
            "--bind", str(self.home), DEMO_HOME, "--chdir", DEMO_HOME,
            "--die-with-parent", "--"]

    def app_env(self):
        e = self.env()
        e.update(
            HOME=DEMO_HOME,
            XDG_CONFIG_HOME=f"{DEMO_HOME}/.config",
            XDG_DATA_HOME=f"{DEMO_HOME}/.local/share",
            XDG_CACHE_HOME=f"{DEMO_HOME}/.cache",
            XDG_DATA_DIRS=self.data_dirs(),
            # the app queues on its own bus name; a private "off" bus keeps the run
            # clear of the real session entirely
            DBUS_SESSION_BUS_ADDRESS="disabled:",
            GDK_SCALE=str(self.scale),
            GDK_BACKEND="x11",
        )
        e.pop("GDK_DPI_SCALE", None)
        return e

    def launch_app(self, uri=None):
        cw, ch = CLIENT_W * self.scale, CLIENT_H * self.scale
        # GDK_SCALE doubles whatever geometry is asked for, so the request is in
        # logical pixels - the same number for both profiles
        cmd = self.sandbox() + [self.bin,
            f"--geometry={CLIENT_W}x{CLIENT_H}+{BORDER}+{BAND + BORDER}"]
        if uri:
            cmd.append(uri)
        self.launch_e = time.time()
        self.app = subprocess.Popen(cmd, env=self.app_env(),
            stdout=open(self.work / "nemo.log", "w"), stderr=subprocess.STDOUT)
        self.win = self.wait_for_window()
        self.place_window(self.win)
        time.sleep(2.5)
        self.xdo("windowactivate", self.win)
        time.sleep(0.4)
        # the window can come up at the size the WM felt like; the client size is
        # what every coordinate is measured against, so insist on it
        self.xdo("windowsize", self.win, str(cw), str(ch))
        time.sleep(0.6)
        self.place_window(self.win)
        self.mouse_rest()

    def wait_for_window(self):
        # `search --class` also answers with hidden utility windows, so walk the
        # named ones and take a real top-level with a title
        deadline = time.time() + 60
        while time.time() < deadline:
            r = subprocess.run(["xdotool", "search", "--onlyvisible", "--class",
                "nemo-anywhere"], env=self.env(), capture_output=True, text=True)
            for win in r.stdout.split():
                info = subprocess.run(["xwininfo", "-id", win], env=self.env(),
                    capture_output=True, text=True).stdout
                m = re.search(r"Width:\s*(\d+)", info)
                if m and int(m.group(1)) > 200:
                    return win
            time.sleep(0.5)
        raise RuntimeError("the window never appeared (see nemo.log)")

    def kill_app(self):
        if self.app:
            self.app.terminate()
            try:
                self.app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.app.kill()
            self.app = None

    def shot(self, path):
        run(["import", "-window", "root", str(path)], env=self.env())
        log(f"screenshot: {path}")

    # --- event log -------------------------------------------------------------
    def ev(self, kind):
        self.events.append((time.time(), kind))

    def mouse_park(self):
        # the very bottom-right pixel: the arrow's hotspot is its tip, so the whole
        # glyph draws past the screen edge and no pointer is left in frame
        self.xdo("mousemove", str(self.size[0] - 1), str(self.size[1] - 1))

    def mouse_rest(self):
        # unlike a terminal demo the pointer is the thing doing the work, so it
        # starts inside the window rather than off screen
        x, y = self.pt(CLIENT_W // 2, CLIENT_H - 40)
        self.xdo("mousemove", str(x), str(y))

    def cleanup(self):
        self.stop_capture()
        self.kill_app()
        self.stop_display()
        if not self.keep and self.work.exists():
            shutil.rmtree(self.work, ignore_errors=True)


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Typing engine

# qwerty neighbors for plausible typos
NEIGH = {
    "a": "sq", "b": "vn", "c": "xv", "d": "sf", "e": "wr", "f": "dg", "g": "fh",
    "h": "gj", "i": "uo", "j": "hk", "k": "jl", "l": "k", "m": "n", "n": "bm",
    "o": "ip", "p": "o", "q": "wa", "r": "et", "s": "ad", "t": "ry", "u": "yi",
    "v": "cb", "w": "qe", "x": "zc", "y": "tu", "z": "x",
}
# char -> XT scancode: the key bank has one unique slice per physical key, so
# every key thocks with its own sample; a shifted symbol thocks with its base key
_SHIFTED = dict(zip('!@#$%^&*()_+{}:"<>?~|', "1234567890-=[];',./`\\"))
_SCAN = {c: 2 + i for i, c in enumerate("1234567890-=")}
_SCAN |= {c: 16 + i for i, c in enumerate("qwertyuiop[]")}
_SCAN |= {c: 30 + i for i, c in enumerate("asdfghjkl;'")}
_SCAN |= {c: 44 + i for i, c in enumerate("zxcvbnm,./")}
_SCAN |= {"`": 41, "\\": 43, " ": 57}
KEY_CODES = {"SPACE": 57, "ENTER": 28, "RETURN": 28, "BACKSPACE": 14, "TAB": 15,
    "ESC": 1, "ESCAPE": 1, "UP": 57416, "DOWN": 57424, "LEFT": 57419,
    "RIGHT": 57421, "PGUP": 3657, "PGDN": 3665}

def key_sound(ch):
    c = _SHIFTED.get(ch, ch.lower())
    return f"key:{_SCAN.get(c, 30)}"          # unknown falls back to 'a'

def keysym_sound(keysym):
    last = keysym.split("+")[-1]              # ctrl+f thocks as f
    if len(last) == 1:
        return key_sound(last)
    return f"key:{KEY_CODES.get(last.upper(), 30)}"

class Typist:
    def __init__(self, rec, rng):
        self.rec = rec
        self.rng = rng
        self.wpm = rng.uniform(120, 160)

    def _delay(self):
        self.wpm += self.rng.uniform(-8, 8)
        self.wpm = max(100.0, min(220.0, self.wpm))
        d = 12.0 / self.wpm                      # 60 / (5 * wpm)
        return d * self.rng.lognormvariate(0.0, 0.22)

    def _emit(self, ch):
        # timestamp AFTER the send, so the xdotool spawn latency never skews the
        # foley; the event epoch is the moment X actually got the key
        if ch == " ":
            self.rec.xdo("key", "--clearmodifiers", "space")
            self.rec.ev("key:SPACE")
        else:
            subprocess.run(["xdotool", "type", "--delay", "0", "--", ch],
                env=self.rec.env(), check=False,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.rec.ev(key_sound(ch))

    def _backspace(self, n):
        for _ in range(n):
            time.sleep(self.rng.uniform(0.09, 0.16))
            self.rec.xdo("key", "--clearmodifiers", "BackSpace")
            self.rec.ev("key:BACKSPACE")

    def type(self, text, typos=0.0, wpm=None):
        if wpm is not None:
            self.wpm = wpm
        i = 0
        while i < len(text):
            ch = text[i]
            time.sleep(self._delay() * (1.6 if ch == " " else 1.0))
            # an expert's slip: wrong neighbor, catch it, fix it
            if ch.lower() in NEIGH and self.rng.random() < typos:
                wrong = self.rng.choice(NEIGH[ch.lower()])
                self._emit(wrong)
                time.sleep(self.rng.uniform(0.22, 0.45))   # the "oops" beat
                self._backspace(1)
                time.sleep(self.rng.uniform(0.08, 0.2))
            self._emit(ch)
            i += 1

    def enter(self):
        time.sleep(self.rng.uniform(0.15, 0.4))
        self.rec.xdo("key", "--clearmodifiers", "Return")
        self.rec.ev("key:ENTER")

    def key(self, keysym, sound=None):
        self.rec.xdo("key", "--clearmodifiers", keysym)
        if sound is None:
            sound = keysym_sound(keysym)
        if sound:
            self.rec.ev(sound)

    def keys(self, keysym, n, hz=8.0, sound=None):
        for _ in range(n):
            self.key(keysym, sound)
            time.sleep(max(0.03, self.rng.uniform(0.8, 1.2) / hz))


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Mouse

class Mouse:
    def __init__(self, rec, rng):
        self.rec = rec
        self.rng = rng
        self.pos = rec.pt(CLIENT_W // 2, CLIENT_H - 40)

    def move(self, lx, ly, dur=0.55):
        self.move_px(*self.rec.pt(lx, ly), dur)

    def move_px(self, x, y, dur=0.55):
        x0, y0 = self.pos
        steps = max(6, int(dur * 40))
        for i in range(1, steps + 1):
            t = i / steps
            t = t * t * (3 - 2 * t)              # smoothstep
            self.rec.xdo("mousemove", str(int(x0 + (x - x0) * t)),
                str(int(y0 + (y - y0) * t)))
            time.sleep(dur / steps)
        self.pos = (x, y)

    def click(self, quiet=False, button="1"):
        self.rec.xdo("click", button)
        self.rec.ev("mouse:CLICK_Q" if quiet else "mouse:CLICK")

    def rclick(self):
        self.click(button="3")

    def at(self, lx, ly, dur=0.55, settle=0.45):
        self.move(lx, ly, dur)
        time.sleep(0.12)
        self.click()
        time.sleep(settle)

    def at_dlg(self, lx, ly, dur=0.55, settle=0.45):
        self.move_px(*self.rec.dlg(lx, ly), dur)
        time.sleep(0.12)
        self.click()
        time.sleep(settle)

    def double(self, lx=None, ly=None, dur=0.55, settle=0.9):
        if lx is not None:
            self.move(lx, ly, dur)
            time.sleep(0.12)
        self.rec.ev("mouse:CLICK")
        time.sleep(0.11)
        self.rec.ev("mouse:CLICK")
        self.rec.xdo("click", "--repeat", "2", "--delay", "110", "1")
        time.sleep(settle)

    def drag(self, lx1, ly1, lx2, ly2, dur=1.1):
        # the pause before the release matters: without it the drop target has not
        # settled and the drag icon is left stranded on screen
        self.move(lx1, ly1, 0.5)
        time.sleep(0.2)
        self.rec.ev("mouse:CLICK")
        self.rec.xdo("mousedown", "1")
        time.sleep(0.25)
        self.move(lx2, ly2, dur)
        time.sleep(0.45)
        self.rec.ev("mouse:CLICK_Q")
        self.rec.xdo("mouseup", "1")

    def wheel(self, up, n, hz=6.0):
        for _ in range(n):
            self.rec.ev("mouse:WHEEL")
            self.rec.xdo("click", "4" if up else "5")
            time.sleep(self.rng.uniform(0.8, 1.2) / hz)

    def rest(self):
        self.move(CLIENT_W - 30, CLIENT_H - 24, dur=0.5)


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Banner bookkeeping

class Banner:
    # every caption sits in the band above the window, so there is no position to
    # choose - only the text and the span it covers
    def __init__(self, rec, text):
        self.rec, self.text = rec, text

    def __enter__(self):
        self.start = time.time()
        return self

    def __exit__(self, *exc):
        self.rec.banners.append((self.start, time.time(), self.text))


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	The synthetic home

# Names are generic on purpose. Nothing here may carry a real account name, a
# real path or anything that identifies the box the recording was made on.
TREE = {
    "Documents": {
        "Reports": ["annual-report.odt", "q1-report.odt", "q2-report.odt",
            "q3-report.odt", "site-survey-report.pdf"],
        "Invoices": ["invoice-0041.pdf", "invoice-0042.pdf", "invoice-0043.pdf"],
        "Notes": ["meeting-notes.md", "reading-list.md", "todo.md"],
        "_files": ["budget.ods", "lease.pdf", "packing-list.txt"],
    },
    "Downloads": {
        "_files": ["field-report.pdf", "handbook.epub", "photos.zip",
            "toolkit-1.4.tar.gz", "wallpaper-pack.zip"],
    },
    "Music": {
        "Live Sets": ["harbour-lights.flac", "night-ferry.flac"],
        "_files": ["kettle-drum.mp3", "slow-tide.mp3", "windfall.mp3"],
    },
    "Pictures": {
        "Photos": ["f-22-over-clouds.jpg", "f-22-pair.jpg", "f-35-mojave.jpg",
            "forest-sunrise.jpg", "meadow-deer.jpg", "puppy-blanket.jpg",
            "puppy-doorway.jpg", "puppy-on-lap.jpg", "river-mist.jpg"],
        "Wallpapers": ["dusk.png", "lattice.png", "meadow.png"],
        "_files": ["profile.png", "sketch.png"],
    },
    "Projects": {
        "lantern": ["README.md", "build.sh", "lantern.c", "lantern.h", "notes.md"],
        "signal-box": ["README.md", "main.c", "report-generator.c", "util.c"],
    },
    "Videos": {
        "_files": ["clip-dockside.mp4", "clip-rooftop.mp4"],
    },
    "Desktop": {"_files": []},
}

# a few sizes that read as plausible rather than uniform
SIZE_HINT = {".flac": 32_000_000, ".mp4": 180_000_000, ".mp3": 5_400_000,
    ".tar.gz": 41_000_000, ".zip": 18_000_000, ".epub": 2_100_000,
    ".pdf": 380_000, ".odt": 24_000, ".ods": 31_000}

REPORT_TEXT = ("Quarterly summary\n\nThe report covers the period to date. "
    "Figures are provisional until the audit closes.\n")

# Real photos for the icon view, so its thumbnails look like something. All are
# public domain or CC0 on Wikimedia Commons, so nothing needs crediting on
# screen. Fetched once at 800px into a cache beside the repo, not shipped.
PHOTO_CACHE = OUT_DIR / "photo-cache"
PHOTOS = {
    "f-22-over-clouds.jpg": ("F-22 Raptor - 080608-F-0154C-103.jpg", "Public domain, US Air Force"),
    "f-22-pair.jpg": ("F-22 Raptor (2566025826).jpg", "Public domain, US Air Force"),
    "f-35-mojave.jpg": ("F-35A Lightning II flies above the Mojave Desert.jpg", "Public domain, US Air Force"),
    "forest-sunrise.jpg": ("Forest sunrise.jpg", "CC0"),
    "meadow-deer.jpg": ("Deer Enjoying Meadow Grass (51840554986).jpg", "Public domain"),
    "puppy-blanket.jpg": ("Golden Retriever puppy (3813393).jpg", "CC0"),
    "puppy-doorway.jpg": ("Puppy-Golden-Retriever.JPG", "Public domain"),
    "puppy-on-lap.jpg": ("Golden Retriever - 7 weeks.jpg", "Public domain"),
    "river-mist.jpg": ("Hoh sunrise mist fog scenic j preston (17095320637).jpg", "Public domain"),
}

def photo(path):
    from urllib.parse import quote
    cached = PHOTO_CACHE / path.name
    if not cached.exists():
        PHOTO_CACHE.mkdir(parents=True, exist_ok=True)
        url = ("https://commons.wikimedia.org/wiki/Special:FilePath/"
            + quote(PHOTOS[path.name][0]) + "?width=800")
        run(["curl", "-sfL", "-A", "nemo-anywhere-demo/1.0", "-o", str(cached), url])
    shutil.copyfile(cached, path)

def demo_image(path, seed, size=(640, 400)):
    # generated: a thumbnail has to be real image data, and these folders are
    # not the ones the icon view scene shows
    rng = random.Random(seed)
    hue = rng.random()
    img = Image.new("RGB", size)
    draw = ImageDraw.Draw(img)
    for y in range(size[1]):
        f = y / size[1]
        top = (0.10 + 0.35 * hue, 0.16 + 0.3 * f, 0.30 + 0.45 * (1 - f))
        draw.line([(0, y), (size[0], y)], fill=tuple(int(255 * c) for c in top))
    for _ in range(rng.randint(3, 6)):
        x0 = rng.randint(0, size[0] - 60)
        y0 = rng.randint(0, size[1] - 60)
        w = rng.randint(50, 220)
        h = rng.randint(40, 160)
        shade = tuple(rng.randint(90, 240) for _ in range(3))
        if rng.random() < 0.5:
            draw.ellipse([x0, y0, x0 + w, y0 + h], outline=shade, width=3)
        else:
            draw.rectangle([x0, y0, x0 + w, y0 + h], outline=shade, width=3)
    img.save(path)

def write_file(path, seed):
    name = path.name
    if name in PHOTOS:
        photo(path)
        return
    if name.endswith(".png"):
        demo_image(path, seed)
        return
    if name.endswith(".zip"):
        with zipfile.ZipFile(path, "w") as z:
            for i in range(6):
                z.writestr(f"pack/item-{i:02d}.txt", f"item {i}\n" * 40)
        return
    suffix = "".join(path.suffixes[-2:]) if name.endswith(".tar.gz") else path.suffix
    body = REPORT_TEXT if "report" in name else f"{path.stem}\n\n"
    path.write_text(body)
    size = SIZE_HINT.get(suffix)
    if size and size > len(body):
        with open(path, "r+b") as f:
            f.truncate(size)

def write_tree(rec, rng):
    home = rec.home
    home.mkdir(parents=True)
    made = []
    seed = 0
    for top, sub in TREE.items():
        (home / top).mkdir()
        made.append(home / top)
        for name, files in sub.items():
            folder = home / top if name == "_files" else home / top / name
            folder.mkdir(exist_ok=True)
            made.append(folder)
            for fname in files:
                seed += 1
                write_file(folder / fname, seed)
                made.append(folder / fname)
    # a couple of hidden entries, so the hidden-files switch has something to show
    (home / ".config").mkdir(exist_ok=True)
    (home / ".profile").write_text("# nothing here\n")
    # every file written in the same second reads as a fixture; spread the dates
    # over the last couple of years, newest at the top of the tree
    now = time.time()
    for path in made:
        age = rng.uniform(1.0, 700.0) * 86400
        os.utime(path, (now - age, now - age))

def write_gtk_settings(rec):
    # GTK reads its theme and font from here with no dconf database involved,
    # which keeps a recording independent of whatever the box has configured
    d = rec.home / ".config/gtk-3.0"
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.ini").write_text(
        "[Settings]\n"
        f"gtk-theme-name={GTK_THEME}\n"
        f"gtk-icon-theme-name={ICON_THEME}\n"
        f"gtk-font-name={UI_FONT} {UI_PT}\n"
        "gtk-cursor-theme-name=Adwaita\n"
        "gtk-cursor-theme-size=24\n"
        "gtk-application-prefer-dark-theme=1\n"
        "gtk-enable-animations=1\n"
        "gtk-xft-antialias=1\n"
        "gtk-xft-hinting=1\n"
        "gtk-xft-hintstyle=hintslight\n"
        "gtk-xft-rgba=none\n")

# what the app starts the recording with. Only non-defaults are listed, the same
# as the file the app writes itself.
START_SETTINGS = {
    "state.first-run-done": "true",
    "appearance.mode": "dark",
    "window-state.start-with-places": "true",
    "window-state.start-with-tree": "false",
    "window-state.start-with-menu-bar": "true",
    "window-state.start-with-toolbar": "true",
    "window-state.start-with-status-bar": "true",
    "window-state.sidebar-width": "150",
    "window-state.sidebar-tree-width": "170",
    # no owner, group or permissions column: those print the real account name of
    # whoever ran the recording, and nothing on screen may. Type goes too, because
    # Ext already says it and the window is only so wide.
    "list-view.default-visible-columns": "name, size, extension, date_modified",
    "preferences.date-format": "informal",
    "preferences.default-folder-viewer": "list-view",
    "preferences.show-image-thumbnails": "true",
    # a folder of pictures opens at 5x by default, two thumbnails to a window
    # this size. At 1.5x all of Photos fits.
    "icon-view.default-image-icon-size": "150",
    "preferences.show-hidden-files": "false",
    "preferences.confirm-drag-move": "true",
    "preferences.sort-directories-first": "true",
    # off until the stripes scene turns it on
    "list-view.row-shading": "false",
    "search.group-by-folder": "false",
    # the delete test guard is on by default for now, and its dialog is not a feature
    "debug.testguard-all-deletes": "false",
}

def render_settings(values):
    # the file is groups of `key: value`, one tab of indent per level - the same
    # shape the app writes, and the same thing a hand edit produces
    tree = {}
    for dotted, value in values.items():
        parts = dotted.split(".")
        node = tree
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = value
    def emit(node, depth):
        lines = []
        for key, value in node.items():
            pad = "\t" * depth
            if isinstance(value, dict):
                lines.append(f"{pad}{key}:")
                lines += emit(value, depth + 1)
            else:
                lines.append(f"{pad}{key}: {value}")
        return lines
    return "\n".join(emit(tree, 0)) + "\n"

def write_settings(rec, values=None):
    rec.settings.parent.mkdir(parents=True, exist_ok=True)
    rec.cfg = dict(START_SETTINGS if values is None else values)
    rec.settings.write_text(render_settings(rec.cfg))

def set_cfg(rec, changes, settle=1.6):
    """Change settings the way a hand edit does - the app live-reloads the file."""
    rec.cfg.update(changes)
    rec.settings.write_text(render_settings(rec.cfg))
    time.sleep(settle)

def prep_content(rec, rng):
    write_tree(rec, rng)
    write_gtk_settings(rec)
    write_settings(rec)


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Segments (each takes the recorder, typist, mouse)
##	Every coordinate is in gif pixels, measured from the client area's top-left.

# Landmarks, measured off a --shot with the starting settings. The sidebar widths
# are pinned in START_SETTINGS, so these hold as long as those do.
LIST_X     = 430         # in the Name column of the file list
TOOL_Y     = 48
ROW_Y, ROW_DY   = 109, 26        # first file row, and the row pitch

def row(n):
    return ROW_Y + n * ROW_DY

CRUMB_HOME   = (182, TOOL_Y)     # the leftmost breadcrumb button, always home
# Pictures is mostly images, so it opens in icon view, Photos first
PHOTOS_ICON  = (220, 133)
ICONS_EMPTY  = (720, 330)    # right of the last thumbnail in Photos
SEARCH_GROUP = (879, 96)     # the group-by-folder toggle in the search bar
# A context menu that would run off the bottom of the screen is pushed up to
# fit, so for a row this far down each item sits at the same height wherever
# the right-click was. Folder and file menus differ.
COMPRESS_ITEM  = (496, 356)
MAKE_LINK_ITEM = (494, 280)      # folder menu
FILE_LINK_ITEM = (495, 331)      # file menu
# On a dialog, from its own corner (see Rec.dlg); a negative y is from the
# bottom. Open, the Compress options push the dialog up to the top of the work
# area, which reserve_band keeps below the captions.
ARCHIVE_FORMAT  = (222, 71)      # the Format dropdown
FORMAT_7Z       = (222, 190)     # the 7z row in the list it drops down
ARCHIVE_OPTIONS = (53, 177)      # the Options expander, closed
SOLID_CHECK     = (38, 389)      # with the options open
COMPRESS_GO     = (336, -19)
LINK_SHORTCUT   = (207, 25)      # Make link for a folder
LINK_GO         = (236, -19)
LINK_RELATIVE   = (215, 59)      # Make link for a file: wider, one more choice
FILE_LINK_GO    = (345, -19)
COPY_CONTENT    = (253, 81)      # the copy question's second choice
COPY_GO         = (287, -17)
# how far the corner drag takes the window in: past where Name stops giving
# way, so the scrollbar comes up
SQUEEZE_W = 420

# Best first. Compress and the shortcut happen in Documents, and the next two
# in Reports. Both stay in list view, so row() holds throughout.

def seg_compress(r, t, m):
    # quicker than the other scenes: the dialog is plain to read, and Options
    # needs the time
    m.double(LIST_X, row(1), settle=0.8)          # Documents
    with Banner(r, "Compress, with no helper program"):
        m.at(LIST_X, row(3), dur=0.4, settle=0.2)     # budget.ods
        r.xdo("keydown", "ctrl")
        m.at(LIST_X, row(5), dur=0.4, settle=0.2)     # packing-list.txt
        r.xdo("keyup", "ctrl")
        time.sleep(0.2)
        m.rclick()
        time.sleep(0.7)
        m.at(*COMPRESS_ITEM, dur=0.5, settle=0.7)
        t.type("paperwork", wpm=190)
        time.sleep(0.3)
    with Banner(r, "zip, tar or 7z, with options like a solid archive"):
        m.at_dlg(*ARCHIVE_FORMAT, dur=0.4, settle=0.5)
        m.at_dlg(*FORMAT_7Z, dur=0.3, settle=0.4)
        m.at_dlg(*ARCHIVE_OPTIONS, dur=0.4, settle=0.8)
        m.at_dlg(*SOLID_CHECK, dur=0.5, settle=0.7)
        m.at_dlg(*COMPRESS_GO, dur=0.5, settle=1.0)

def seg_lnk(r, t, m):
    # A folder shortcut, then opened. It sorts with the folders, so it comes up
    # right under Reports. Opening it on Linux is the part worth seeing.
    with Banner(r, "Make a Windows shortcut, on Linux or macOS"):
        m.at(LIST_X, row(2), dur=0.5, settle=0.3)     # Reports
        m.rclick()
        time.sleep(0.7)
        m.at(*MAKE_LINK_ITEM, dur=0.5, settle=0.8)
        m.at_dlg(*LINK_SHORTCUT, dur=0.5, settle=0.5)
        m.at_dlg(*LINK_GO, dur=0.5, settle=0.9)
    with Banner(r, "It opens the same as it does on Windows"):
        m.double(LIST_X, row(3), settle=1.5)

def seg_rellink(r, t, m):
    # inside Reports now, by way of the shortcut. The new link sorts under its
    # original and stays selected, which the copy scene counts on.
    with Banner(r, "Symlinks can be relative, or absolute"):
        m.at(LIST_X, row(3), dur=0.5, settle=0.3)     # q3-report.odt
        m.rclick()
        time.sleep(0.7)
        m.at(*FILE_LINK_ITEM, dur=0.5, settle=0.8)
        m.at_dlg(*LINK_RELATIVE, dur=0.5, settle=0.6)
        m.at_dlg(*FILE_LINK_GO, dur=0.5, settle=1.0)

def seg_copylink(r, t, m):
    # pasted back into the same folder: the question is the point, not where
    # the copy goes
    with Banner(r, "Copying a link asks: keep the link, or copy what it points to"):
        t.key("ctrl+c")
        time.sleep(0.4)
        t.key("ctrl+v")
        time.sleep(1.0)
        m.at_dlg(*COPY_CONTENT, dur=0.6, settle=0.6)
        m.at_dlg(*COPY_GO, dur=0.5, settle=1.2)

def seg_columns(r, t, m):
    # The window corner is dragged in until Name has given up all it can and a
    # scrollbar comes up, then back out. The decoration does not scale with
    # GDK_SCALE, so the corner is found in screen pixels, not through pt().
    # Every later scene needs the window back at its exact size, so that is
    # asked for again at the end rather than trusted to the drag.
    sc = r.scale
    cx = r.origin[0] + CLIENT_W * sc + 1
    cy = r.origin[1] + CLIENT_H * sc + 1
    with Banner(r, "Columns fit what is in them, and scroll before they get too narrow"):
        m.move_px(cx, cy, dur=0.7)
        time.sleep(0.2)
        r.ev("mouse:CLICK")
        r.xdo("mousedown", "1")
        time.sleep(0.2)
        m.move_px(cx - SQUEEZE_W * sc, cy, dur=1.4)
        time.sleep(1.0)
        m.move_px(cx, cy, dur=0.9)
        time.sleep(0.2)
        r.ev("mouse:CLICK_Q")
        r.xdo("mouseup", "1")
        time.sleep(0.3)
    r.xdo("windowsize", r.win, str(CLIENT_W * sc), str(CLIENT_H * sc))
    time.sleep(0.3)
    r.place_window(r.win)
    m.rest()

def seg_stripes(r, t, m):
    # straight into the settings file, as a hand edit would be; the app picks it
    # up live, so there is no menu to drive
    with Banner(r, "Striped rows, if you want them"):
        time.sleep(0.4)
        set_cfg(r, {"list-view.row-shading": "true"}, settle=2.0)

def seg_pictures(r, t, m):
    # no view buttons: a folder that is mostly images switches to icons by itself,
    # and switches back on the way out, which is the point of the scene
    with Banner(r, "Picture folders switch to thumbnails by themselves"):
        m.at(*CRUMB_HOME, dur=0.6, settle=0.7)            # back to Home
        m.double(LIST_X, row(4), settle=0.8)             # Pictures
        m.double(*PHOTOS_ICON, settle=0.8)               # Photos
    with Banner(r, "Improved image thumbnail caching engine"):
        m.move(*ICONS_EMPTY, dur=0.5)     # not rest(): that spot has a tooltip
        time.sleep(1.3)

def seg_search(r, t, m):
    # From Home the matches come from four folders, so the grouped result has
    # groups to show. A flat list says nothing about where the matches came
    # from, so it gets a beat to read before the grouped one replaces it.
    m.at(*CRUMB_HOME, dur=0.7, settle=0.9)
    with Banner(r, "Search anywhere under the folder"):
        m.move(CLIENT_W // 2, row(2), dur=0.5)
        t.key("ctrl+f")
        time.sleep(0.7)
        t.type("report", wpm=150)
        t.enter()
        time.sleep(1.0)
    with Banner(r, "Or grouped under the folder each came from"):
        m.at(*SEARCH_GROUP, dur=0.8, settle=1.6)

def seg_outro(r, t, m):
    with Banner(r, "github.com/yottacore/nemo-anywhere"):
        m.rest()
        time.sleep(1.0)

# Left out to stay under the cap, weakest first: F3's second pane (upstream
# already had it), the tree beside Places, and the drag that asks before a
# move. They are in git history before 2026-10-05.
_SCRIPT = [
    ("compress", seg_compress),
    ("lnk",      seg_lnk),
    ("rellink",  seg_rellink),
    ("copylink", seg_copylink),
    ("columns",  seg_columns),
    ("stripes",  seg_stripes),
    ("pictures", seg_pictures),
    ("search",   seg_search),
    ("outro",    seg_outro),
]
SEGMENTS = {"video": _SCRIPT, "gif": _SCRIPT}


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Audio: process the key bank, mix the event log into a wav

SOUND_FILES = {
    "mouse:CLICK":    SOUNDS / "mouse/click.wav",
    "mouse:CLICK_Q":  SOUNDS / "mouse/click_quiet.wav",
}
KEYPACK = SOUNDS / "keys-oreo"     # mechvibes "EG Oreo": one recording, one slice per key
GAIN = {"key": 0.8, "mouse:CLICK": 0.55, "mouse:CLICK_Q": 0.38, "mouse:WHEEL": 0.5}

# the bank is quiet and slice loudness wanders ~6 dB; even each slice out to a
# consistent body presence but keep every key's own transient and timbre
KEY_BODY = {57: 0.085, 28: 0.085, 14: 0.07}

def shape_slice(s, code):
    rms = np.sqrt((s ** 2).mean()) + 1e-9
    s = s * (KEY_BODY.get(code, 0.062) / rms)
    n_in, n_out = int(SR * 0.001), int(SR * 0.006)
    s[:n_in] *= np.linspace(0.0, 1.0, n_in)[:, None]      # slice edges must not click
    s[-n_out:] *= np.linspace(1.0, 0.0, n_out)[:, None]
    peak = np.abs(s).max()
    if peak > 0.7:                                # keep one loud hit from owning the mix
        s *= 0.7 / peak
    return s.astype(np.float32)

def load_keypack(work, cache):
    cfg = json.loads((KEYPACK / "config.json").read_text())
    raw = work / "keypack.pcm"
    run(["ffmpeg", "-v", "error", "-y", "-i", str(KEYPACK / cfg["sound"]),
        "-ar", str(SR), "-ac", "2", "-f", "s16le", str(raw)])
    pcm = np.frombuffer(raw.read_bytes(), dtype=np.int16) \
        .astype(np.float32).reshape(-1, 2) / 32768.0
    for code, span in cfg["defines"].items():
        if not span:
            continue
        start, dur = span
        s = pcm[int(start * SR / 1000):int((start + dur) * SR / 1000)].copy()
        if len(s) < SR // 100:
            continue
        cache[f"key:{code}"] = shape_slice(s, int(code))
    for name, code in KEY_CODES.items():
        if f"key:{code}" in cache:
            cache[f"key:{name}"] = cache[f"key:{code}"]

def synth_wheel(sr):
    # a soft scroll-wheel detent: a short muffled tick, much softer and darker
    # than a mouse click - a hair of noise on a low damped thonk, low-passed
    n = int(sr * 0.030)
    tt = np.arange(n) / sr
    noise = np.random.default_rng(3).standard_normal(n) * np.exp(-tt * 320)
    body = np.sin(2 * math.pi * 175 * tt) * np.exp(-tt * 150)
    mix = noise * 0.45 + body * 0.55
    sos = spsig.butter(2, 1700, btype="low", fs=sr, output="sos")
    mix = spsig.sosfilt(sos, mix)
    mix /= np.abs(mix).max() + 1e-9
    return np.stack([mix, mix], axis=1).astype(np.float32) * 0.28

def load_samples(work):
    cache = {}
    for kind, path in SOUND_FILES.items():
        wav = work / (re.sub(r"[^A-Za-z0-9]", "_", kind) + ".wav")
        run(["ffmpeg", "-v", "error", "-y", "-i", str(path),
            "-ar", str(SR), "-ac", "2", "-f", "wav", str(wav)])
        with wave.open(str(wav), "rb") as w:
            data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        cache[kind] = data.astype(np.float32).reshape(-1, 2) / 32768.0
    load_keypack(work, cache)
    cache["mouse:WHEEL"] = synth_wheel(SR)
    return cache

def build_audio(rec, work, duration, rng):
    cache = load_samples(work)
    mix = np.zeros((int(duration * SR) + SR, 2), dtype=np.float32)
    for epoch, kind in rec.events:
        t_rel = epoch - rec.t0_e + FOLEY_LAG
        if t_rel < -0.5 or t_rel > duration:
            continue
        s = cache.get(kind)
        if s is None:
            continue
        gain = GAIN.get(kind, GAIN.get(kind.split(":")[0], 0.8))
        gain *= rng.uniform(0.85, 1.05)           # stroke-force wobble; samples are raw
        at = int(max(0.0, t_rel) * SR)
        end = min(at + len(s), len(mix))
        mix[at:end] += s[: end - at] * gain
    peak = np.abs(mix).max()
    if peak > 0:
        mix *= min(0.40 / peak, 4.0)              # ~ -8 dBFS, bounded boost
    out = work / "audio.wav"
    with wave.open(str(out), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((mix * 32767.0).astype(np.int16).tobytes())
    return out


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Post: sync-flash location, banners, encode

def check_drift(rec, video_end_e):
    dur = float(out_of(["ffprobe", "-v", "error", "-show_entries", "format=duration",
        "-of", "csv=p=0", str(rec.raw)]))
    expect = (video_end_e - rec.flash_e) + rec.flash_vt
    if abs(dur - expect) > max(0.5, expect * 0.02):
        log(f"WARNING: capture drift - raw {dur:.1f}s vs expected {expect:.1f}s; "
            "AV sync may be off (X server starved the grab loop?)")

def find_flash(raw, work):
    stats = work / "stats.txt"
    run(["ffmpeg", "-v", "error", "-t", "8", "-i", str(raw),
        "-vf", f"signalstats,metadata=print:key=lavfi.signalstats.YAVG:file={stats}",
        "-f", "null", "-"])
    best_t, best_y, pts = 0.0, -1.0, 0.0
    for line in stats.read_text().splitlines():
        mo = re.search(r"pts_time:([0-9.]+)", line)
        if mo:
            pts = float(mo.group(1))
        mo = re.search(r"YAVG=([0-9.]+)", line)
        if mo and float(mo.group(1)) > best_y:
            best_y, best_t = float(mo.group(1)), pts
    if best_y < 180:
        raise RuntimeError(f"sync flash not found (max YAVG {best_y})")
    return best_t

def esc_drawtext(work, i, text):
    f = work / f"banner{i}.txt"
    f.write_text(text)
    return f

def banner_xy(rec):
    return "(w-text_w)/2", f"({rec.band}-text_h)/2"

# a quick damped-spring vertical bounce for the pop-in / pop-out (~0.6s each)
def wobble_y(base, s, e, amp):
    win = 0.6
    ring = f"{amp}*exp(-6*T)*cos(2*PI*2.6*T)"
    win_in  = ring.replace("T", f"(t-{s:.3f})")
    win_out = ring.replace("T", f"({e:.3f}-t)")
    return (f"({base})"
        f"+if(between(t,{s:.3f},{s + win:.3f}),{win_in},0)"
        f"+if(between(t,{e - win:.3f},{e:.3f}),{win_out},0)")

def vf_chain(rec, work, trim, dur, tail=False):
    p = rec.p
    def to_vt(epoch):
        return rec.flash_vt + (epoch - rec.flash_e)
    filters = [f"fps={rec.out_fps}"]
    # resolve each banner's [s,e]; then clamp every end to the next banner's start
    # minus a gap, so only ONE banner is ever on screen
    spans = []
    for s_e, e_e, text in rec.banners:
        s = max(0.0, to_vt(s_e) - trim)
        e = max(s + p["banner_min"], to_vt(e_e) - trim)
        spans.append([s, e, text])
    spans.sort(key=lambda b: b[0])
    GAP = 0.4
    for i in range(len(spans) - 1):
        spans[i][1] = min(spans[i][1], spans[i + 1][0] - GAP)
    amp = max(4, int(rec.band * 0.18))         # bounce stays inside the band
    x, base_y = banner_xy(rec)
    for i, (s, e, text) in enumerate(spans):
        if e <= s:
            continue
        tf = esc_drawtext(work, i, text)
        y = wobble_y(base_y, s, e, amp)
        fade = f"clip((t-{s:.3f})/0.15,0,1)*clip(({e:.3f}-t)/0.15,0,1)"
        filters.append(
            f"drawtext=fontfile={BANNER_TTF}:textfile={tf}:fontsize={p['banner_fs']}:"
            f"fontcolor={BANNER_FG}:"
            f"x={x}:y='{y}':alpha='{fade}':enable='between(t,{s:.3f},{e:.3f})'")
    filters.append("format=rgb24")
    if tail == "video":
        filters.append(f"fade=t=in:st=0:d={VIDEO_FADE_IN_S}")
        filters.append(f"tpad=stop_mode=clone:stop_duration={TAIL_HOLD_S}")
        filters.append(f"fade=t=out:st={dur:.3f}:d={TAIL_HOLD_S}")
        filters.append(f"tpad=stop_mode=add:color=black:stop_duration={VIDEO_TAIL_BLACK_S}")
    elif tail == "gif":
        filters.append(f"tpad=stop_mode=clone:stop_duration={TAIL_HOLD_S}")
        filters.append(f"tpad=stop_mode=add:color=black:stop_duration={GIF_TAIL_BLACK_S}")
    return ",".join(filters)

def encode_video(rec, work, out_mp4, video_end_e):
    rec.flash_vt = find_flash(rec.raw, work)
    log(f"sync flash at video t={rec.flash_vt:.3f}s")
    check_drift(rec, video_end_e)
    trim = rec.flash_vt + (rec.t0_e - rec.flash_e)
    dur = video_end_e - rec.t0_e
    vf = vf_chain(rec, work, trim, dur, tail="video")
    rng = random.Random(1)
    audio = build_audio(rec, work, dur, rng)   # the tail is silent (freeze + black)
    run(["ffmpeg", "-v", "error", "-y",
        "-ss", f"{trim:.3f}", "-i", str(rec.raw), "-i", str(audio),
        "-t", f"{dur + TAIL_EXTRA['video']:.3f}", "-vf", vf,
        "-c:v", "libx265", "-preset", "slow", "-crf", "20", "-pix_fmt", "yuv420p",
        "-tag:v", "hvc1", "-x265-params", "log-level=error",
        "-r", str(rec.out_fps), "-c:a", "aac", "-b:a", "160k",
        "-movflags", "+faststart", str(out_mp4)])
    return out_mp4

GIF_COLORS = 128

def gif_pass(rec, work, out_gif, trim, dur, colors=GIF_COLORS, tail=False):
    vf = vf_chain(rec, work, trim, dur, tail="gif" if tail else None)
    pal = work / "pal.png"
    cut = ["-ss", f"{trim:.3f}", "-t", f"{dur + (TAIL_EXTRA['gif'] if tail else 0.0):.3f}"]
    # ONE global palette (stats_mode=full) applied uniformly. Ordered bayer stays
    # temporally stable; error diffusion shimmers and bloats a gif.
    run(["ffmpeg", "-v", "error", "-y", *cut, "-i", str(rec.raw),
        "-vf", f"{vf},palettegen=stats_mode=full:max_colors={colors}", str(pal)])
    run(["ffmpeg", "-v", "error", "-y", *cut, "-i", str(rec.raw), "-i", str(pal),
        "-lavfi", f"{vf}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=4",
        str(out_gif)])
    return out_gif

def gif_optimize(gif):
    # gifsicle -O3 re-cuts every frame to the smallest changed rectangle, so the
    # static band above the window and the held tail frames cost near nothing
    if not shutil.which("gifsicle"):
        log("WARNING: gifsicle not found - gif left unoptimized")
        return gif
    before = gif.stat().st_size / (1 << 20)
    opt = gif.with_name(gif.stem + "-opt.gif")
    run(["gifsicle", "-O3", "--no-warnings", "-o", str(opt), str(gif)])
    after = opt.stat().st_size / (1 << 20)
    log(f"gifsicle: {before:.1f} -> {after:.1f} MiB")
    return opt

def encode_gif(rec, work, out_gif, video_end_e):
    rec.flash_vt = find_flash(rec.raw, work)
    log(f"sync flash at video t={rec.flash_vt:.3f}s")
    check_drift(rec, video_end_e)
    trim = rec.flash_vt + (rec.t0_e - rec.flash_e)
    dur = video_end_e - rec.t0_e
    gif_pass(rec, work, out_gif, trim, dur, tail=True)
    return gif_optimize(out_gif)


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Output placement + rotation

# The README carries the gif, and GitHub stops animating one much past this.
GIF_ASSET_MAX_MB = 10
# The backlog asked for under a minute; the links scene and the archive options
# took it past that, so the cap moved. Scene holds drift a little run to run, so
# this is checked rather than assumed - the trimming that keeps it under is easy
# to undo by accident while editing a scene.
GIF_MAX_SECONDS = 66.0

def gif_seconds(path):
    try:
        return float(out_of(["ffprobe", "-v", "error", "-show_entries",
            "format=duration", "-of", "csv=p=0", str(path)]).strip())
    except (subprocess.CalledProcessError, ValueError):
        return 0.0

def rotate(out_dir, prefix, ext, no_rotate):
    if no_rotate:
        return
    inc = REPO / "cicd/utility/include/gfs-rotate.bash"
    subprocess.run(["bash", "-c",
        f'source "{inc}" && gfs_rotate "{out_dir}" {prefix} {ext}'], check=False)

def place_video(mp4, out_dir, no_rotate):
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    dst = out_dir / f"nemo-anywhere-demo_{stamp}.mp4"
    shutil.copy2(mp4, dst)
    mb = dst.stat().st_size / (1 << 20)
    rotate(out_dir, "nemo-anywhere-demo", "mp4", no_rotate)
    log(f"video: {dst} ({mb:.1f} MiB)")

def place_gif(gif, out_dir, no_rotate, no_asset=False):
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    dst = out_dir / f"nemo-anywhere-demo_{stamp}.gif"
    shutil.copy2(gif, dst)
    mb = dst.stat().st_size / (1 << 20)
    secs = gif_seconds(dst)                   # before the rotate renames it
    rotate(out_dir, "nemo-anywhere-demo", "gif", no_rotate)
    log(f"gif: {dst} ({mb:.1f} MiB, {secs:.1f}s)")
    # The fixed tail is not a scene, and a longer black end should not read as one
    # running long.
    scenes = secs - TAIL_EXTRA["gif"]
    if scenes > GIF_MAX_SECONDS:
        log(f"WARNING: gif scenes run {scenes:.1f}s (> {GIF_MAX_SECONDS:.0f}); shorten a scene's holds")
    if no_asset:                              # partial/tuning runs must not clobber it
        log("gif (README): skipped (--no-asset)")
    elif mb <= GIF_ASSET_MAX_MB:
        asset = REPO / "assets" / "demo.gif"
        asset.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(gif, asset)
        log(f"gif (README): {asset} ({mb:.1f} MiB)")
    else:
        log(f"WARNING: gif is {mb:.1f} MiB (> {GIF_ASSET_MAX_MB}); assets/demo.gif "
            "left untouched - shorten the scenes that move the most pixels")


##•••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
##	Entry

def take_shot(args, name, seed):
    # the scene-writing tool: run whatever --segments asks for with no capture
    # running, then photograph where it ended up. Every coordinate in the segments
    # above was measured off one of these.
    rng = random.Random(seed)
    rec = Rec(args, PROFILES[name])
    try:
        prep_content(rec, rng)
        rec.start_display()
        rec.launch_app()
        time.sleep(2.0)
        t = Typist(rec, rng)
        m = Mouse(rec, rng)
        want = [s.strip() for s in args.segments.split(",") if s.strip()]
        for seg, fn in SEGMENTS[name]:
            if want and seg not in want:
                continue
            log(f"segment: {seg}")
            fn(rec, t, m)
        time.sleep(0.6)
        rec.shot(Path(args.shot))
        log(f"client origin {rec.origin}, home {rec.home}")
        if rec.keep:
            log(f"work dir kept: {rec.work}")
    finally:
        rec.cleanup()

def record(args, name, seed):
    rng = random.Random(seed)
    rec = Rec(args, PROFILES[name])
    try:
        prep_content(rec, rng)
        rec.start_display()
        rec.start_capture()
        log(f"[{name}] capture running; launching app")
        rec.launch_app()
        time.sleep(1.5)
        rec.t0_e = time.time() - LEAD_S

        t = Typist(rec, rng)
        m = Mouse(rec, rng)
        want = [s.strip() for s in args.segments.split(",") if s.strip()]
        for seg, fn in SEGMENTS[name]:
            if want and seg not in want:
                continue
            log(f"[{name}] segment: {seg}")
            rec.seg_marks[seg] = time.time()
            fn(rec, t, m)
        time.sleep(0.3)                       # the hold at the end is added at encode
        video_end_e = time.time()
        # the encoder freezes the last frame for a few seconds, and the caption
        # showing at that instant is what gets frozen with it. Without this the
        # demo ends on a silent still, having dropped the one line worth reading.
        if rec.banners:
            last = rec.banners[-1]
            rec.banners[-1] = (last[0], last[1] + TAIL_HOLD_S, last[2])

        rec.stop_capture()
        rec.kill_app()

        if name == "video":
            out = rec.work / "demo.mp4"
            encode_video(rec, rec.work, out, video_end_e)
            place_video(out, Path(args.out_dir) / "video", args.no_rotate)
        else:
            out = rec.work / "demo.gif"
            gif = encode_gif(rec, rec.work, out, video_end_e)
            place_gif(gif, Path(args.out_dir) / "gif", args.no_rotate, args.no_asset)
        if rec.keep:
            log(f"[{name}] work dir kept: {rec.work}")
    finally:
        rec.cleanup()

def main():
    ap = argparse.ArgumentParser(description="Record the nemo-anywhere demo video + gif.")
    ap.add_argument("--display", default="",
        help=f"pin a display; default searches from {DEFAULT_DISPLAY} down")
    ap.add_argument("--profile", default="video,gif", help="comma list: video,gif")
    ap.add_argument("--segments", default="", help="comma list; default all")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--keep-work", action="store_true")
    ap.add_argument("--no-rotate", action="store_true")
    ap.add_argument("--no-asset", action="store_true",
        help="do not overwrite assets/demo.gif (for partial/tuning runs)")
    ap.add_argument("--out-dir", default=str(OUT_DIR))
    ap.add_argument("--shot", default="",
        help="write one screenshot of the launched app and stop")
    args = ap.parse_args()

    explicit = bool(args.display) or bool(os.environ.get("NEMO_DEMO_DISPLAY"))
    args.display = pick_display(
        args.display or os.environ.get("NEMO_DEMO_DISPLAY") or DEFAULT_DISPLAY, explicit)
    seed = args.seed if args.seed is not None else int(time.time()) & 0xFFFF
    log(f"seed {seed}, display {args.display}")
    names = [p.strip() for p in args.profile.split(",") if p.strip()]
    for name in names:
        if name not in PROFILES:
            sys.exit(f"unknown profile: {name}")
    if args.shot:
        take_shot(args, names[0], seed)
        return
    for name in names:
        record(args, name, seed)

if __name__ == "__main__":
    main()


##	Script history:
##		- 20260919: Created, from the SilkTerm recorder. No GPU path (GTK3 draws
##		  through cairo); both profiles share one set of scene coordinates via
##		  GDK_SCALE; settings changes go through the settings file the app
##		  live-reloads rather than a control socket.
##		- 20260924: The icon view scene shows public domain photos, cached
##		  beside the repo, and image folders open at 1.5x instead of 5x.
##		- 20260925: Links scene, and Compress shows its options. The caption band
##		  is reserved with a strut, so no dialog grows up under it.
##		- 20260925: The video fades in and fades out to black; the gif ends on
##		  three seconds of black. The length warning leaves the tail out.
##		- 20261005: Best features first. New relative link, link copy, column
##		  and striped row scenes; F3, the tree and the drag scene left out.
