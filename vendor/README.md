<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->
<!-- markdownlint-disable MD055 -- Table pipe style [Expected: leading_and_trailing; Actual: leading_only; Missing trailing pipe] -->
# Vendored

## Code

SHCL config engine, single-header C binding, **MIT** - compiled into nemo, so unlike the themes below this one is linked. Keeps its `LICENSE.md`. MIT sits fine under our GPL-2.0-only. Update by copying `source/c/shcl.h` from a newer tag and re-pinning here.

- `shcl/shcl.h` <- https://github.com/yottacore/shcl @ `f2a8ad2aed34e6b78f812a6c5ef375288a6e5494` (`dev`, before the `v3.0.0-beta.1` tag)

The two SHCL releases settings were written with before format 3 are kept in `shcl-old/`, for a test only. A test program built against each writes a settings file the way the app did then, and the app has to read it the same way. Never linked into nemo. Same MIT license, kept in `shcl-old/LICENSE.md`. Leave these as they are.

- `shcl-old/1.2.0/shcl.h` <- https://github.com/yottacore/shcl @ `192d206e7eeb196598d4bfc385dd9527e698ed8a` (tag `v1.2.0`), used by v1.0.0-beta2

- `shcl-old/2.0.0/shcl.h` <- https://github.com/yottacore/shcl @ `fd1068a629701068f07a197de45212d267c20f46` (tag `v2.0.0`)

blake3 hash, C implementation, dual **CC0-1.0** and **Apache-2.0** - compiled into nemo, and keeps its `LICENSE` (the CC0 one, which is the simpler of the two to carry under GPL-2.0-only). Update by copying the named files out of `c/` at a newer tag and re-pinning here.

- `blake3/` <- https://github.com/BLAKE3-team/BLAKE3 @ `df610ddc3b93841ffc59a87e3da659a15910eb46` (tag `1.8.2`)

It is here rather than linked because there is nothing to link to. No Linux distribution ships a libblake3 old enough to rely on, and the Windows sysroot has none at all. The C sources are 113 KB and build to 65,716 bytes of code, which is cheaper than the alternatives were:

- GLib's own SHA-256 is plain C at 291 MB/s. Nothing is vendored, but a folder of large files would wait on it.

- OpenSSL's SHA-256 is fast, 1450 MB/s where the processor has SHA-NI and 502 where it does not. Linking `libcrypto.a` statically for the one call costs **4.8 MB** on an unpacked Windows executable of 8.3 MB, because the digest goes through the provider machinery and none of it strips out.

- blake3 as vendored runs at 2459 MB/s single-threaded. Measured on b23, a Ryzen 3950X with AVX2, against a 64 MB buffer.

Only the parts that are used are here: the plain C routine, the dispatcher, and the SSE2, SSE4.1 and AVX2 routines. AVX-512 is left out, and so are the hand-written assembly versions of the same routines, the NEON one and the thread-pool wrapper. Which routine runs is decided at run time by what the processor reports. A machine that is not x86 gets the plain C one, so a future arm64 build works but is slow - `blake3_neon.c` is the file to add when that matters.

## Themes

Regenerate with `cicd/utility/vendor-themes.bash` - do not hand-edit the table. Bundled as mere aggregation: GTK reads them at runtime, nothing is linked into nemo. Each theme keeps its own `COPYING`. Our own Windows-look icon sets are not here - Luna, Aero, Metro and Mica are first-party art in `assets/icons`, built by `gen-icon-theme.py`. Nothing Windows-styled is vendored as icons: every such set that circulates draws blue folders, and Windows folders are yellow.

| Theme                           | Kind   | Style           | Upstream                                                                       | Commit
| :---                            | :---   | :---            | :---                                                                           | :---
| `Fluent`                        | Widget | Windows 11      | https://github.com/vinceliuice/Fluent-gtk-theme                                | `7a49a464b0188c340101c52965c18190b1c694cf`
| `macOS`                         | Widget | macOS           | https://github.com/B00merang-Project/macOS                                     | `3951a4224ebab3c6a37b7abcf8690a1de9a42914`
| `macOS-dark`                    | Widget | macOS           | https://github.com/B00merang-Project/macOS-Dark                                | `85f6339b864d40299c2a250131ae56b3940cb59f`
| `Windows-10`                    | Widget | Windows 10      | https://github.com/B00merang-Project/Windows-10                                | `3a4116603b66a9adcb78f3987d7ea6f01de1cbce`
| `Windows-10-dark`               | Widget | Windows 10      | https://github.com/B00merang-Project/Windows-10-Dark                           | `10e4bd54b8ca14f5efb741c891d19090493ff476`
| `Windows-7`                     | Widget | Windows 7       | https://github.com/B00merang-Project/Windows-7                                 | `943b5307b349d3526068be0fa32f7549ee37ab45`
| `Windows-XP`                    | Widget | Windows XP      | https://github.com/B00merang-Project/Windows-XP                                | `7637830906823af40a3cd7e7079be753d8b7d679`
| `Windows-XP-dark`               | Widget | Windows XP      | https://github.com/B00merang-Project/Windows-XP                                | `7637830906823af40a3cd7e7079be753d8b7d679`
| `Adwaita`                       | Icon   | Adwaita         | https://gitlab.gnome.org/GNOME/adwaita-icon-theme                              | `e58656b901e8366f20b74ae33755ac7d8026fa4c`
| `AdwaitaLegacy`                 | Icon   | Adwaita         | https://gitlab.gnome.org/GNOME/adwaita-icon-theme-legacy                       | `7642b102c4a7c4088f170f548ae37960f2443522`
| `Colloid`                       | Icon   | Rounded         | https://github.com/vinceliuice/Colloid-icon-theme                              | `fa07485895a2443f7cfbceefe9dcdde798a05215`
| `Colloid-dark`                  | Icon   | Rounded         | https://github.com/vinceliuice/Colloid-icon-theme                              | `fa07485895a2443f7cfbceefe9dcdde798a05215`
| `Papirus`                       | Icon   | Flat            | https://github.com/PapirusDevelopmentTeam/papirus-icon-theme                   | `5f8b701d7521e27b4859d7e4f9b0da4c423c036c`
| `BeautyLine`                    | Icon   | Outline         | https://github.com/gvolpe/BeautyLine                                           | `6ba5aeaeea2efb5f9d25ea39bed5dfda07b3ab70`
| `Simply-Blue-Circles`           | Icon   | Circles Blue    | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Simply-Cyan-Circles`           | Icon   | Circles Cyan    | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Simply-Orange-Circles`         | Icon   | Circles Orange  | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Simply-Purple-Circles`         | Icon   | Circles Purple  | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Simply-Red-Circles`            | Icon   | Circles Red     | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Simply-White-Circles`          | Icon   | Circles White   | https://github.com/ju1464/Simply_Circles_Icons                                 | `6e2985c5affa161df3daed95be6b2dc1cfb2b4d6`
| `Lime-Numix-2021`               | Icon   | Numix Lime      | https://github.com/rtlewis88/rtl88-Themes (branch `MBC-Icon-SuperPack`)        | `ddbf2329bce0f8b4005665863bdde3f4c7eed2a2`
| `MB-Lime-Suru-GLOW`             | Icon   | Suru Lime       | https://github.com/rtlewis88/rtl88-Themes (branch `MBC-Icon-SuperPack`)        | `ddbf2329bce0f8b4005665863bdde3f4c7eed2a2`
| `Material-Black-Pistachio-Suru` | Icon   | Suru Pistachio  | https://github.com/rtlewis88/rtl88-Themes (branch `MBC-Icon-SuperPack`)        | `ddbf2329bce0f8b4005665863bdde3f4c7eed2a2`
| `Avidity-Dusk-Mixed-Suru`       | Icon   | Suru Dusk       | https://github.com/rtlewis88/rtl88-Themes (branch `Avidity-Icons-and-Folders`) | `7b94756dcc0fdf25c45efa25bb7da30cc4295556`
| `FF-BlackGreen`                 | Icon   | Black and Green | https://www.opencode.net/felipefacundes/ff-blackgreen                          | `df6ff8f4aac05010d07817e8ebbdd9ffd2176ab9`
| `FF-Flamengo-RJ-BR`             | Icon   | Flamengo        | https://www.opencode.net/felipefacundes/ff-flamengo-rj-br                      | `7f23a786048c61ca5926d6563847098f2138a163`
| `Qogir`                         | Icon   | Soft            | https://github.com/vinceliuice/Qogir-icon-theme                                | `c633057ba0d27a504b3255144071c9691ed0264a`
| `Tela`                          | Icon   | Circles         | https://github.com/vinceliuice/Tela-icon-theme                                 | `a1fffc5bfab716bd022dd228ee96fe3965cdb33d`
| `WhiteSur`                      | Icon   | macOS           | https://github.com/vinceliuice/WhiteSur-icon-theme                             | `f6a78df1c9ea8c5f804b6c72d03408ca3db3521b`

### Licenses worth knowing about

Most of the above state their license in the repository and keep it beside the theme here. Three do not, and are bundled on weaker evidence than the rest. Any of them may need a second look if it ever matters:

- **`BeautyLine`**: The mirror we take it from carries no license file, and the page it points at is behind an anti-bot wall that will not serve a license string. nixpkgs packages the same theme and records it as public domain, which is the basis for shipping it.

- **The four `rtl88-Themes` sets** (`Lime-Numix-2021`, `MB-Lime-Suru-GLOW`, `Material-Black-Pistachio-Suru`, `Avidity-Dusk-Mixed-Suru`): No license file upstream. Its README states the artwork derives from Suru (GPL-3), Numix (GPL-3), La Capitaine (GPL-3 / MIT) and Twemoji (MIT), which is what it is shipped on.

- **`FF-BlackGreen`**: No license file. Its sibling project by the same author, `FF-Flamengo-RJ-BR`, is LGPL-3.0, and the two are the same work in two colorways.

**Buuf is deliberately not here.** It was asked for, and it is CC BY-NC-SA - the NonCommercial term rules it out of anything shipped, and out of this repository entirely. Anyone who wants it can drop it into their own icons folder; `filesystem/` explains how.
