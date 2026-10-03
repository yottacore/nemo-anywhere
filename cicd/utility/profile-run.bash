#!/usr/bin/env bash

##	- Purpose: Profile the app under a realistic workload and write a flamegraph
##	  SVG, for the pipeline's profiler stage and for looking at by hand.
##	- Sampling is done by attaching a debugger and taking a backtrace of every
##	  thread, repeatedly. Deliberately NOT perf: perf needs a privileged sysctl on
##	  this box, and a profiler that cannot run without root is a profiler nobody
##	  runs. This costs accuracy - samples are wall-clock, not CPU cycles, so a
##	  blocked thread shows up - which is why the report discounts the wait bucket.
##	- Workload: browse a generated folder tree headlessly, so the run exercises
##	  folder loading, icon lookup and thumbnailing rather than an idle window.
##	- Everything runs on a private headless display; the visible session is never
##	  touched.
##	- Syntax:
##	  cicd/utility/profile-run.bash <output.svg> [options]
##	  Options:
##	   --secs N     sample for this long (default 12)
##	   --hz N       samples per second (default 20)
##	   --bin PATH   profile this binary (default: the staged release, else the
##	                container debug build copied out)
##	   --probe      check the box can profile at all, then exit. Lets the caller
##	                tell "this box cannot" from "the run went wrong".

##	History: At bottom of script.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
SLUG="nemo-anywhere"

# shellcheck source=include/echo.bash
source "${HERE}/include/echo.bash"
# shellcheck source=include/xvfb.bash
source "${HERE}/include/xvfb.bash"

out=""; secs=12; hz=20; bin=""; probeOnly=0
while (($#)); do case "$1" in
	--secs)    secs="${2:?--secs needs a number}"; shift 2 ;;
	--hz)      hz="${2:?--hz needs a number}"; shift 2 ;;
	--bin)     bin="${2:?--bin needs a path}"; shift 2 ;;
	--probe)   probeOnly=1; shift ;;
	-h|--help) sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	-*)        fDie "unknown option: $1 (try --help)" ;;
	*)         out="$1"; shift ;;
esac; done
((probeOnly)) || [[ -n "$out" ]] || fDie "usage: profile-run.bash <output.svg> [--secs N] [--hz N] [--bin PATH]"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Preconditions
#
# Anything missing here is the box, not the app, so say so and let the caller
# decide whether that is a skip or a failure. --probe is how it asks, so a box
# that simply cannot profile reads as a skipped stage rather than a broken build.

fCheckPreconditions(){
	local tool missing=()
	for tool in gdb Xvfb xdpyinfo inferno-flamegraph; do
		command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
	done
	((${#missing[@]})) && fDie "missing: ${missing[*]}"
	[[ "$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)" == 0 ]] \
		|| fDie "ptrace is restricted (yama/ptrace_scope is not 0), cannot sample"
	:
}

fCheckPreconditions
((probeOnly)) && exit 0

work="$(mktemp -d)"

## Profile the DEBUG build, not the release one: the release binaries are
## stripped, and a sampler that cannot name a function produces a flamegraph of
## six boxes. The prefix is staged out of the build container once and reused.
PREFIX="${ROOT}/cicd/artifacts/profiling/prefix"
CONTAINER="${NEMO_CONTAINER:-nemo-build}"

if [[ -z "$bin" ]]; then
	if [[ ! -x "${PREFIX}/bin/${SLUG}" ]]; then
		docker exec "$CONTAINER" true 2>/dev/null \
			|| fDie "no staged debug prefix and the build container '${CONTAINER}' is not running - pass --bin"
		fEcho_Clean
		fEcho "Staging the debug build out of ${CONTAINER}"
		docker exec "$CONTAINER" bash /src/cicd/linux/stage-prefix.bash /build /build-profile-prefix >/dev/null 2>&1 || true
		rm -rf "$PREFIX"
		mkdir -p "$(dirname "$PREFIX")"
		docker cp "${CONTAINER}:/build-profile-prefix" "$PREFIX" >/dev/null
	fi
	bin="${PREFIX}/bin/${SLUG}"
fi
[[ -x "$bin" ]] || fDie "no binary to profile at ${bin}"

## A stripped binary still runs, but every frame comes out as an address.
if ! nm "$bin" >/dev/null 2>&1; then
	fEcho "WARNING: ${bin} has no symbol table - expect an unreadable flamegraph"
fi


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Workload: a folder worth loading

disp=""; app_pid=""; driver_pid=""
XVFB_PID=""

cleanup(){
	[[ -n "$driver_pid" ]] && kill "$driver_pid" 2>/dev/null || true
	[[ -n "$app_pid"    ]] && kill "$app_pid"    2>/dev/null || true
	[[ -n "$XVFB_PID"   ]] && kill "$XVFB_PID"   2>/dev/null || true
	rm -rf "$work"
}
trap cleanup EXIT

fEcho_Clean
fEcho "Building a workload folder"
tree="${work}/tree"
mkdir -p "${tree}/sub"
## A few hundred mixed files: enough to make folder loading, sorting, icon lookup
## and thumbnailing do real work, small enough to stay quick.
for i in $(seq 1 300); do
	printf 'sample text %s\n' "$i" > "${tree}/file-${i}.txt"
done
for i in $(seq 1 40); do
	printf 'P1\n2 2\n1 0 0 1\n' > "${tree}/image-${i}.pbm"
	: > "${tree}/sub/nested-${i}.dat"
done
fEcho_Clean "$(find "$tree" -type f | wc -l) files"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Run it headless

fEcho_Clean
fXvfbStart :120 1280x900x24 "${work}/xvfb.log" || fDie "no free headless display from :120"
disp="$XVFB_DISPLAY"
fEcho "Started headless display ${disp}"

## A throwaway HOME, so profiling never reads or writes the real settings.
fake_home="${work}/home"
mkdir -p "$fake_home"

fEcho "Launching ${SLUG}"
## No session bus: with one, a copy already running on the real desktop would
## take the arguments and this launch would hand off and exit, leaving nothing
## to sample. The app handles a bus-less environment on purpose.
prefix_root="$(cd "$(dirname "$(dirname "$bin")")" && pwd)"
DBUS_SESSION_BUS_ADDRESS="disabled:" DISPLAY="$disp" \
	HOME="$fake_home" XDG_CONFIG_HOME="${fake_home}/.config" \
	LD_LIBRARY_PATH="$(printf '%s:' "${prefix_root}"/lib/*-linux-gnu*)${prefix_root}/lib" \
	XDG_DATA_DIRS="${prefix_root}/share:/usr/local/share:/usr/share" \
	"$bin" "$tree" > "${work}/app.log" 2>&1 &
app_pid=$!

## Give the window time to come up before sampling, or the profile is mostly
## process start-up.
sleep 3
if ! kill -0 "$app_pid" 2>/dev/null; then
	fEcho_Clean "last output from the app:"
	tail -5 "${work}/app.log" 2>/dev/null || true
	fDie "the app exited before sampling could start"
fi


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Drive the window while it is being sampled
#
# Without this the app is idle in its main loop the whole time and the profile is
# nothing but poll(). Keystrokes rather than clicks: they need no coordinates and
# no assumptions about where anything is on screen.

drive(){
	local win
	while :; do
		win="$(DISPLAY="$disp" xdotool search --name "" getactivewindow 2>/dev/null | head -1 || true)"
		[[ -n "$win" ]] || win="$(DISPLAY="$disp" xdotool search --class . 2>/dev/null | tail -1 || true)"
		[[ -n "$win" ]] || { sleep 0.5; continue; }

		## Sending to the display rather than a window id: GTK ignores the
		## synthetic events xdotool aims at a specific window.
		DISPLAY="$disp" xdotool windowactivate --sync "$win" 2>/dev/null || true
		for key in ctrl+2 Page_Down Page_Down ctrl+1 Page_Down F5 ctrl+2 Home End F5; do
			DISPLAY="$disp" xdotool key --clearmodifiers "$key" 2>/dev/null || true
			sleep 0.25
		done
	done
}

drive >/dev/null 2>&1 &
driver_pid=$!


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Sample

folded="${work}/folded.txt"
: > "$folded"
interval="$(awk -v h="$hz" 'BEGIN{printf "%.3f", 1/h}')"
deadline=$(( SECONDS + secs ))
taken=0

fEcho "Sampling ${secs}s at ${hz}Hz"
while (( SECONDS < deadline )); do
	kill -0 "$app_pid" 2>/dev/null || break

	## -nx skips the user's .gdbinit (it loads pretty-printers that error out
	## under batch). One backtrace per thread, folded into "a;b;c 1" lines.
	gdb -nx -q -p "$app_pid" -batch -ex 'thread apply all bt' 2>/dev/null |
		awk '
			/^Thread /       { if (n) print_stack(); n = 0; next }
			/^#[0-9]+ /      {
				line = $0
				## "#3  0x... in gtk_main () at ..."  ->  gtk_main
				if (match(line, / in [^ (]+ ?\(/)) {
					fn = substr(line, RSTART + 4, RLENGTH - 5)
				} else if (match(line, /^#[0-9]+ +[^ (]+ ?\(/)) {
					fn = substr(line, RSTART, RLENGTH)
					sub(/^#[0-9]+ +/, "", fn); sub(/ ?\($/, "", fn)
				} else next
				gsub(/^ +| +$/, "", fn)
				if (fn != "") { frames[++n] = fn }
				next
			}
			END { if (n) print_stack() }
			function print_stack(   i, s) {
				## gdb prints innermost first; a folded stack reads root first.
				s = ""
				for (i = n; i >= 1; i--) s = s (s == "" ? "" : ";") frames[i]
				if (s != "") print s " 1"
				n = 0
			}
		' >> "$folded"

	taken=$((taken + 1))
	sleep "$interval"
done

kill "$app_pid" 2>/dev/null || true
app_pid=""

lines="$(wc -l < "$folded")"
(( lines > 0 )) || fDie "sampling produced no stacks (${taken} attempts) - is the binary stripped of symbols?"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Fold and render

## Identical stacks are summed, which is what the flamegraph renderer expects and
## keeps the SVG small.
sorted="${work}/sorted.txt"
awk '{ c = $NF; $NF = ""; sub(/ $/, ""); total[$0] += c } END { for (s in total) print s, total[s] }' \
	"$folded" | sort > "$sorted"

mkdir -p "$(dirname "$out")"
inferno-flamegraph --title "${SLUG} - ${secs}s, ${taken} samples" \
	--countname samples < "$sorted" > "$out"

[[ -s "$out" ]] || fDie "the flamegraph renderer produced nothing"

fEcho_Clean
fEcho "OK: $(basename "$out") (${taken} sweeps, $(wc -l < "$sorted") unique stacks)"
fEcho_Clean


##	History:
##		- 2026-08-04: Created. Debugger-based sampling rather than perf, so
##		  profiling needs no elevated privileges.
##		- 2026-10-03: The display is the first free one from :120, proven ours,
##		  instead of a fixed :97 other projects also use.
