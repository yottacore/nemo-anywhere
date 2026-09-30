<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# nemo-anywhere Windows exe packing

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Summary](#summary)
- [Specification](#specification)
- [Goals](#goals)
	- [Non-goals](#non-goals)
- [Design](#design)
	- [How the exe is built](#how-the-exe-is-built)
	- [Startup time](#startup-time)
	- [Programs the app starts](#programs-the-app-starts)
	- [Antivirus and signing](#antivirus-and-signing)
	- [The zip](#the-zip)
- [Alternative ideas](#alternative-ideas)
	- [Unconsidered](#unconsidered)
	- [Rejected](#rejected)
	- [Superseded](#superseded)
- [Research findings](#research-findings)
- [Roadmap](#roadmap)
- [Related backlog issues](#related-backlog-issues)

<!-- /TOC -->

## Summary

On Windows, nemo-anywhere is one `nemo-anywhere.exe` with the whole GTK runtime packed inside it. There's no library folder, no launcher, and nothing installed or registered. It's an exe to copy anywhere and run.

- The packer is Enigma Virtual Box. It keeps the runtime as a virtual filesystem in memory, with nothing extracted at run time.

- A plain zip of the same files is published beside it, for antivirus false alarms and while the exe is unsigned.

- Two costs came with the packer, and both are handled: a slow start, and hooks that follow every program the app starts. See [Design](#design).

## Specification

- One self-contained exe. Nothing extracted to disk at run time, nothing installed, nothing registered.

- It runs from any folder, on a bare PATH.

- The pack source is the same flat layout the zip uses, and that layout also runs unpacked with a double-click.

- The zip is always published beside the exe.

- The app never starts another program itself. The desktop is asked to start it.

- No compressing packer such as UPX on top.

- Every file the packer carries costs startup time, so the file count is kept down. Themes and the app's own art are compiled into the program.

- Enigma Virtual Box is only needed for the exe. Without it everything else still builds, tests and stages, and only the packing step skips.

## Goals

- Copy it and run it. Nothing to install, nothing to clean up.

- Start fast, since a file manager is opened dozens of times a day.

- Programs opened from the app work exactly as they would opened any other way.

- Trip antivirus as little as possible.

### Non-goals

- An installer. `install.ps1` already covers an install with a menu entry and PATH, and a Windows installer exe is its own backlog item.

- Code signing. It's its own backlog item, blocked on having any signing identity.

- External plugins on Windows. The plugin folder is never read there, so a bad plugin can't hang the app.

## Design

### How the exe is built

- A pack step flattens the staged bundle into the same layout the release zip uses: exe and dlls at the root, with `lib/`, `share/` and `etc/` beside them. GLib-stack libraries find their data next to their own dll, so that tree also runs unpacked with a bare double-click. The step then packs the lot into one exe.

- It is stage five of the Windows pipeline, after the native build and stage.

- One binary only. The connect-server and open-with dialogs run inside the main program, and the extension library is folded into it, so there's nothing beside the exe.

- The exe is a graphical program, not a console one, so Windows doesn't open a terminal before the window. `--version` output still works when piped.

- The font settings for the native text look are set inside the exe, so no launcher is needed for them.

- It carries a version resource with real publisher and version details. A blank one scores worse with antivirus and looks unfinished in Properties.

- It's rebuilt before it's packed. The pack step packs what's already staged.

### Startup time

- The packer charges for every file it carries, about 2.8 ms each, and all of it before the program's own code runs. Bytes hardly matter.

- So the file count is what gets cut. The bundled themes and the app's own artwork are compiled into the program as one resource, instead of a couple of thousand loose files. The toolkit's full icon set was replaced by trimmed copies of just the names a file manager asks for. That took the bundle from 4,840 files to about 150, and startup from 14.2 s to 3.4 s.

- About 2.5 s over an unpacked folder is left. That's the packer's own cost, and only a different packer could reach it.

- A splash comes up while it starts, drawn with Windows' own toolkit because it has to be up before GTK is. It goes when the first folder has listed, or a second after the view is up, whichever comes first. Nothing inside the program can cover the time before that, since the packer's loader owns the process first.

- Each window is its own process, so on Windows a new window carries this startup time too. That's one reason it's a setting.

### Programs the app starts

The packed exe shares its virtual filesystem with every program it starts, by putting its hooks into each one. A program that runs sandboxed child processes of its own, which is anything built on Chromium, can't start them with the hooks in place, and reports a crash. A 32-bit program never starts at all, and nothing says so.

- Sharing can't just be turned off. The app's own helpers, the document converters for search, the thumbnailers and two toolkit helpers, live inside the virtual filesystem and need it to find their libraries.

- So the app never starts another program itself. It asks one of two brokers outside its own process tree:
	- The desktop shell first. It passes arguments, brings the new window forward, and is the ordinary way a file gets opened.
	- The system's management service when the shell won't do it. An elevated session refuses the shell call, and there may be no shell running at all. It keeps the caller's rights, but the new window opens behind.
	- A plain start of its own is last, so a launch can still happen where neither broker answers.

- A file that isn't there is refused before the shell is asked. The shell answers a missing file with a message box of its own and doesn't return until it's dismissed.

- The programs offered under "Open with" go the same way. A store app has no command line and is left to the toolkit.

- `nemo-launch-win32.c` is the one place that starts another program on Windows. A check in the C lint fails any other.

### Antivirus and signing

- Packed exes are sometimes flagged by antivirus as a false alarm. The plain zip is the fallback that doesn't get flagged.

- The exe is unsigned for now, since there's no signing identity yet. The release workflow has a signing step left off until there is one.

- False alarms that still show up get reported to each vendor that flags the exe.

### The zip

- The zip has the same flat layout the exe was packed from, and runs with a double-click.

- `install.ps1` fetches the zip, not the exe, and installs it with a menu entry and a name on PATH.

## Alternative ideas

### Unconsidered

- A different packer, to get under the last 2.5 s of startup.

- Linking the GTK runtime into the program statically, so there's nothing to pack.

### Rejected

- Unpacking to a real folder at run time. It breaks the dogfood launcher's one file per build, and swaps one thing security software dislikes for another.

- Turning off the packer's sharing with started programs. The app's own helpers need it.

- A small launcher of our own between the app and the program. The hooks follow the whole process tree, so the launcher and everything it starts are hooked too.

- Launch flags or a command prompt in between. Detaching, a hidden `cmd.exe`, `start /b`, and the shell's open verb called in-process all leave the program hooked.

- The management service alone. The new window opens behind, and starting everything that way is a known malware pattern that antivirus watches for.

- Letting the packer run 32-bit programs. They start, but hooked like everything else.

- Shrinking the exe with UPX or a similar compressor. Packers of that kind trip antivirus.

### Superseded

- A library folder beside the exe, started through a `.vbs` launcher that set the library path. The single exe removed the whole arrangement.

- Signing through SignPath Foundation, which only signs artifacts built in hosted CI. The application was refused.

- Themes as loose files in the bundle. They cost most of a 14 s start.

## Research findings

- The packer's cost is per file, not per byte, and all before `main`. To `--version`: 4,840 files took 14.3 s, 1,038 took 4.0 s, 124 took 1.9 s, and the unpacked folder 0.9 s.

- The packer's own compression and mapping options changed startup by nothing measurable.

- The hooks follow the process tree. A helper started by the packed exe is hooked, and so is everything it starts, wherever it sits on disk.

- Of six ways to start a program from inside the process, only the management service came out clean. The desktop shell, asked through its own COM object, also comes out clean.

| What                          | Shell          | Management service
| :---                          | :---           | :---
| Program starts with no hooks  | yes            | yes
| Starts a 32-bit program       | yes            | yes
| New window comes forward      | yes            | no
| Keeps the caller's rights     | no, drops them | yes
| Can pass arguments            | yes            | yes
| Needs the shell running       | yes            | no
| Works from an elevated copy   | no             | yes

- A window started by the management service can't be brought forward afterward. Granting the right to take the foreground, and raising the window directly, both report success and change nothing.

- The packer has four options in all, and none of them can limit sharing to some programs.

- A packed test program that had started a 32-bit program kept a processor busy waiting on it, for nine days, until the child was killed.

## Roadmap

- Find out whether the released exe keeps a processor busy the same way while a program it started is still running.

- Run ImageMagick thumbnails under the packed exe on Windows. No console window should flash up, and `magick.exe` should work with the hooks in it.

- Signing, once there is an identity (backlog: Windows code signing).

- A Windows installer exe, after signing.

## Related backlog issues

- Ultra-portable Windows: a single self-contained executable. Done.

- Single-exe packaging stage in `cicd-win.ps1`. Done.

- The Windows executable takes too long to start. Done.

- Windows: opening a file from the released build breaks the program it opens in. Done.

- Windows: the released build cannot open a file whose program is 32-bit. Done.

- Windows exe signing groundwork. Done.

- Publish the Windows `.zip` alongside the single exe. Done.

- The Windows exe on a release had no version in its name. Done.

- Windows code signing, and reducing AV false positives. Open.

- A Windows installer exe that installs, or updates an install already there. Open.

- Real-Windows validation: the paths still not exercised there. Open.

- Launching `app\nemo-anywhere.exe` straight from the dogfood folder throws missing-dll dialogs. Canceled.
