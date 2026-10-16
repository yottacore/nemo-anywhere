<!-- markdownlint-disable MD007 -- Unordered list indentation -->
<!-- markdownlint-disable MD010 -- No hard tabs -->
<!-- markdownlint-disable MD033 -- No inline html -->
<!-- markdownlint-disable MD041 -- First line in a file should be a top-level heading -->

<!-- TOC ignore:true -->
# Contributing

Thanks for looking. This is a small, independent hard fork of [linuxmint/nemo](https://github.com/linuxmint/nemo), run by one person, so the process here is short.

<!-- TOC ignore:true -->
## Table of contents

<!-- TOC -->

- [Before anything else](#before-anything-else)
- [Reporting a bug](#reporting-a-bug)
- [Reporting a security problem](#reporting-a-security-problem)
- [Suggesting a feature](#suggesting-a-feature)
- [Sending code](#sending-code)
- [Code of conduct](#code-of-conduct)

<!-- /TOC -->

## Before anything else

Report issues [here](https://github.com/yottacore/nemo-anywhere/issues), never to Linux Mint or the Cinnamon team. This fork isn't affiliated with them and nothing from it goes upstream. [fork.md](fork.md) has the provenance.

Check [project/backlog.md](project/backlog.md) first. A lot of what looks like a missing feature is already sitting there, sometimes with a note on why it hasn't been done.

The license is GPL-2.0-only, version two and not "or later". Anything contributed comes in under that, except in `source/libnemo-extension/`, which is LGPL.

## Reporting a bug

Useful reports have four things:

- Which build, from `nemo-anywhere --version`.

- Which OS and version, and on Linux which desktop, or none.

- What was expected and what happened instead.

- Steps that reproduce it from a fresh start.

If it involves a file operation, say whether the source and destination were on the same filesystem, and whether either was a network path. That distinction is behind a good share of the bugs so far.

## Reporting a security problem

Don't open a public issue for anything that could be exploited. Use a [private security advisory](https://github.com/yottacore/nemo-anywhere/security/advisories/new), which is the only private channel this repository has.

## Suggesting a feature

Read the goals in [project/design.md](project/design.md) first. The project is opinionated about a few things and deliberately narrow about others. Drawing the desktop, autorun of any kind, and migrating settings from a pre-1.0 install are all out of scope and will stay that way.

Otherwise, open an issue and say what problem the feature solves rather than what the feature is. That is usually the more interesting half.

## Sending code

- Read [project/style-guide_code.md](project/style-guide_code.md). New code looks like the code beside it, and a patch that reformats what it didn't need to touch will be asked to go back.

- Branch off `dev`, not `main`. `main` is release-only.

- Build and run the tests before sending. [building.md](building.md) gets a dev setup going, and [project/design.md](project/design.md) has the package list and the build steps.

- A fix comes with a test that fails without it.

- Keep the commit message short and plain. What changed, not how it was arrived at.

- One subject per pull request. A branch that fixes a bug and also renames forty things is two branches.

The Windows and Linux builds come out of the same source with no per-platform forks of a file, so a change that only works on one of them needs the other side handled or explicitly gated.

## Code of conduct

Be civil. The full text is in [code_of_conduct.md](code_of_conduct.md).
