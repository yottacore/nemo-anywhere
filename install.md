<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# Installation

Everything is on the [releases page](https://github.com/yottacore/nemo-anywhere/releases). Pick whichever of the three below suits you. Building from source is for working on it, not for using it.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Packages and installers](#packages-and-installers)
- [Direct stable and dev install scripts](#direct-stable-and-dev-install-scripts)
- [DIY](#diy)
- [Copyright and license](#copyright-and-license)

<!-- /TOC -->

## Packages and installers

- **Windows**: run `nemo-anywhere-<version>-windows-x86_64-setup.exe`. It installs for your account only and needs no admin rights. It uses the same folder, Start menu entry and PATH entry as the PowerShell script below, so either one can update or remove what the other put there. To remove it, use Settings -> Apps. It isn't signed yet, so SmartScreen will probably ask before it runs. Releases after 1.0.0-beta2 have it.

- **Windows, portable**: download `nemo-anywhere-<version>-windows-x86_64-portable.exe` and run it. That is the whole program - the runtime is inside it. Nothing is installed and nothing is registered. Releases up to 1.0.0-beta2 name it plain `nemo-anywhere.exe`.

- **Debian, Ubuntu, Mint**: Run `sudo apt install ./nemo-anywhere-<version>-linux-x86_64.deb`.

- **Fedora, openSUSE, RHEL**: Run `sudo dnf install ./nemo-anywhere-<version>-linux-x86_64.rpm`.

Both packages install to `/opt/nemo-anywhere` with a menu entry and `nemo-anywhere` on PATH, and use the GTK3 your distro already provides.

## Direct stable and dev install scripts

It's one command. It downloads the right build for the machine, verifies its checksum, tells you exactly what it is about to do, and waits for a yes. The defaults suit most people. Add `--help` (`-Help` in PowerShell) to see the options.

This one is for Linux and WSL, and for BSD and macOS once those are built:

~~~bash
bash <(curl -fsSL https://raw.githubusercontent.com/yottacore/nemo-anywhere/main/install.bash)
~~~

This one is for Windows, or anywhere else with PowerShell. It is a full installer on its own, not a wrapper around the one above:

~~~powershell
& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/yottacore/nemo-anywhere/main/install.ps1')))
~~~

A release with no checksums file isn't installed, even with `--yes`. Add `--no-verify` (`-NoVerify` in PowerShell) to install it anyway.

Reinstalling over an existing copy is fine - it replaces it. `--help` also says how to remove it.

This is where it goes:

| OS      | User install (default)                 | Launcher                                                                            | (or) System install             | Launcher
| :---    | :---                                   | :---                                                                                | :---                            | :---
| Linux   | ~/.local/share/nemo-anywhere/          | ~/.local/share/applications/nemo-anywhere.desktop, ~/.local/bin/nemo-anywhere       | /opt/nemo-anywhere/             | /usr/local/share/applications/nemo-anywhere.desktop, /usr/local/bin/nemo-anywhere
| Windows | %LOCALAPPDATA%\Programs\Nemo Anywhere\ | %APPDATA%\Microsoft\Windows\Start Menu\Programs\Nemo Anywhere.lnk, and a PATH entry | C:\Program Files\Nemo Anywhere\ | %ProgramData%\Microsoft\Windows\Start Menu\Programs\Nemo Anywhere.lnk, and a PATH entry
| BSD     | *pending a BSD build*                  |                                                                                     |                                 |
| macOS   | *pending a macOS build*                |                                                                                     |                                 |

Settings go in per-platform standard locations: `~/.config/nemo-anywhere` on Linux and BSD, `%APPDATA%\nemo-anywhere` on Windows, `~/Library/Application Support/nemo-anywhere` on macOS - and are left alone by an uninstall.

## DIY

Extract `nemo-anywhere-<version>-linux-x86_64.tar.gz` wherever you like and run `bin/nemo-anywhere` from inside it. It is relocatable, so no fixed path is required. Verify the download against the `nemo-anywhere-<version>-sha256sums.txt` file published beside it.

A Linux build needs GTK 3.24.33 or newer and glibc 2.35 or newer at runtime, which means Ubuntu 22.04, Debian 12, Mint 21, Fedora 36 or anything more recent.

## Copyright and license

> Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu)<br>
> Licensed under [GNU GPL v2](https://opensource.org/license/GPL-2.0) license. No warranty.

