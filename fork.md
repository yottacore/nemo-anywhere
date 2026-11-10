# Fork provenance

nemo-anywhere is a hard fork of the Nemo file manager. It's a portable, standalone build that doesn't need Cinnamon, for Windows, plain Linux, BSD, and macOS.

- Upstream: https://github.com/linuxmint/nemo

- Fork point: tag `6.6.4` (commit `8d23a398`)

- This repository starts from a clean, detached baseline at the fork point. It doesn't include upstream's commit history. Lineage is recorded here and in the retained per-file copyright and license headers.

- No upstream contribution and no downstream sync are planned. This is a divergent, independent project.

## Affiliation

nemo-anywhere is not affiliated with, endorsed by, or supported by the Linux Mint project, the Cinnamon team, or GNOME. "Nemo", "Cinnamon", and "Linux Mint" belong to their respective owners. Direct all issues and support requests to this repository.

## License

GPL-2.0-only. Some components are under LGPL or other compatible terms. See `license.txt`, `license-lib.txt`, `license-docs.txt`, and `license-for-extensions.txt`. Original copyright and license headers are retained in every file.

## Marking changes (GPLv2 sec. 2a)

Files that were changed a lot for the port have a short notice that says they were changed and when, for example:

~~~c
/* Modified 2026 for nemo-anywhere. */
~~~

Small mechanical edits don't each need a notice. The git history from the baseline commit on is the record of waht changed.
