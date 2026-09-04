# 0006 — `fork.h` is the only definition of the build tag, and the tag defaults off

**Status:** Accepted (jc-33 for the default, jc-34 for the consolidation) · **Applies to:** `fork.h`, `help.c`, `oshw-qt/TWApp.cpp`, `package.ps1`

## Context

The build tag `jc-N` is what a build claims about itself. It appears in `Help > About`, in `-V`
output, optionally in the window title, and in the name of the release zip.

It used to live in **three or four places at once**: a C++ `QStringLiteral` in `TWApp.cpp`, which
`help.c` (which is C) could not see; the repository URL typed out again in `help.c`; the header line
of `README.txt`; and a regular expression in `package.ps1` that parsed the C++ literal. A release
meant remembering every one of them, and the one that got forgotten was never the loud one —
**an About box quietly claiming the wrong build number looks exactly like a correct one.**

Separately, the tag was originally shown in the window title by default. That is right for the
maintainer, who needs to know which of several builds is in front of him, and wrong for everyone
else: this is a public download, and a stranger's title bar should not carry a private build number.

## Decision

**`fork.h` holds `FORK_BUILD_TAG`, `FORK_AUTHOR`, `FORK_REPO_URL`, `FORK_ISSUES_URL` and
`FORK_UPSTREAM_URL`, and nothing else defines them.** They are plain string macros rather than C++
constants, because `help.c` is C and `TWApp.cpp` is C++ and both need them, and because
string-literal concatenation (`"[" FORK_BUILD_TAG "]"`) keeps them usable inside `QStringLiteral()`.

**Bump `FORK_BUILD_TAG` and nothing else.**

**The window-title tag is opt-in**: `showbuildtag` in `tw_settings.ini`, shipped as `false`. The
About box shows the tag always — that is where you go to ask what you are running.

## Consequences

- `package.ps1` reads the `#define` out of `fork.h` to name the zip, and then **verifies that the
  compiled executable actually contains that tag** by searching the binary for the UTF-16LE bytes of
  `[jc-N]`. A zip cannot be named for a build it does not contain.
- ⚠ **If the `#define` is ever reformatted, update the regular expression in `package.ps1`.**
  Packaging then fails loudly, which is the intended direction, but the reason will not be obvious.
- **The title bar cannot be used to confirm which build is running** on a stock configuration.
  `Help > About` can, and so can `strings` on the executable.
- A downloader gets stock behavior from a stock file. This is the same rule SuperCC records in its
  own ADR 0005: every opt-in switch defaults to off.
