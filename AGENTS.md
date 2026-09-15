# AGENTS.md

Instructions for any coding agent working in this repository, in the cross-tool
[AGENTS.md](https://agents.md) convention.

**The full brief is [`CLAUDE.md`](CLAUDE.md). Read it before making changes.** This file is the
subset that will keep you out of trouble if you read nothing else. It is short on purpose; every
line of it is a mistake somebody already made here.

## What this is

Jeremy Christman's public fork of [Tile World 2](https://github.com/SicklySilverMoon/tworld) (tag
2.3.1) — a C/C++ emulator of the *Chip's Challenge* game engines, Qt5 GUI, Windows. GPLv2-or-later.
Builds are tagged `jc-N` and published as GitHub releases that real people download.

Its reason for existing: jc-2 through jc-29 fixed the MS engine so that **SuperCC-recorded solutions
replay correctly**. That count reached zero at jc-28. `mslogic.c` carries 80+ fork edits, and a
careless change there invalidates solutions people spent years recording.

## Setup

- **Windows and MSYS2 at `C:\msys64`**: `mingw-w64-x86_64-{gcc,cmake,ninja,SDL2}`, plus
  `mingw-w64-x86_64-qt5-static` for the shipping build. No package manager, no dependency manifest.
- All tooling is PowerShell, and it targets **Windows PowerShell 5.1** on purpose.

## Build, test, package

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1                  # -> build-static\tworld2.exe (ships)
powershell -ExecutionPolicy Bypass -File build.ps1 -Flavor dynamic  # much faster; for development
powershell -ExecutionPolicy Bypass -File run-tests.ps1              # ALL SIX layers, then verify-docs
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Build       # build first
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Sanitize    # just the UBSan layer (~12s)
powershell -ExecutionPolicy Bypass -File verify-docs.ps1            # the docs still match the code
powershell -ExecutionPolicy Bypass -File verify-docs.ps1 -SelfTest  # plant a defect per check class; it must bite
powershell -ExecutionPolicy Bypass -File package.ps1                # -> dist\TileWorld-<tag>.zip
powershell -ExecutionPolicy Bypass -File verify-defaults.ps1         # stock ini vs. settings.cpp
powershell -ExecutionPolicy Bypass -File test\run-golden.ps1        # engine snapshot; run after ANY engine edit
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1         # NO_FIX_* toggles; run after engine edits
powershell -ExecutionPolicy Bypass -File mutate.ps1 -SelfTest       # prove the census harness is honest (~1 min)
powershell -ExecutionPolicy Bypass -File mutate.ps1                 # mutation census; SLOW (~30 min), not a layer
powershell -ExecutionPolicy Bypass -File mutate.ps1 -Escalate <tsv> # re-run survivors under -Sanitize; measures the yield
powershell -ExecutionPolicy Bypass -File mutate.ps1 -Split <tsv>    # sort survivors: test reaches the line, or not
```

Machine-readable results: `run-tests.ps1 -ResultsPath test-results` writes JUnit XML and JSON. Exit
code is 0 only if every layer that ran passed. ⚠ **A layer that SKIPPED still exits 0** (no built
executable, no Qt5) — so read the summary's last lines: it names each skip under `NOT RUN` and will
not print `all green` over one. The Qt layer is the only thing that reaches the `.ccx` parser.

## The six rules

1. **Bump `FORK_BUILD_TAG` in `fork.h`, and nothing else.** It is the single definition of the build
   tag — `help.c`, `TWApp.cpp` and `package.ps1` all read it from there. Two executables must never
   report the same tag.

2. **Never assume a PowerShell call to the game waited or captured anything.** The exe is a
   Windows-GUI-subsystem binary: `& .\tworld2.exe -b ...` returns in ~11 ms against a one-second
   runtime, `$LASTEXITCODE` comes back **empty**, and stdout is lost. Use
   `Start-Process -Wait -PassThru -NoNewWindow` with redirects. **And batch verify's exit code is not
   a verdict** — parse stdout for `Valid solutions:   N`.

3. **Tests compile with `-std=gnu11`/`-std=gnu++11`, never `-std=c99`.** Strict ANSI leaves `WIN32`
   undefined, and `fileio.c` branches on it to pick `DIRSEP_CHAR` — so a strict test would exercise
   the POSIX branch, which is not the branch that ships.

4. **Never "fix" the diagonal produced by two perpendicular arrow keys.** It is a *block slap*, and
   real levels are unsolvable without it. See `docs/adr/0008`.

5. **Never commit `CHIPS.DAT`, a `.tws`, `save/`, or `tw_settings.ini`.** The `.dat`/`.dac` files
   already in `data/` and `sets/` are upstream's freely redistributable community packs and are
   supposed to be there — do not "clean them up". See `docs/adr/0005`.

6. **Check `CLAUDE.md` §7 and `docs/adr/` before "fixing" anything that looks wrong.** A dozen things
   here look like bugs and are load-bearing: thirty-two dead-looking `NO_FIX_*` macros, an
   off-by-default build tag, hand-built zip entry names, a separate wrapped-navigation function, and
   a deliberately over-tall row on the score screen.

## Tests

Each test is one C file that `#include`s the source under test directly, so no CMake tree and no
built executable are needed. Every test is built **twice**, as C and as C++, unless it narrows itself
with a `TESTLANG:` comment and says why. Extra flags go in a `TESTFLAGS:` comment on the same lines.

🔴 **`tw_expect_atleast(N)` in each test is load-bearing.** It fails the run if fewer than N checks
executed, which is what catches a test function that has silently stopped being called. Raise it when
you add cases; **never lower it to make a run pass.** ⚠ `input_test.c` and `dirinput_test.c` predate
`tw_test.h` and carry the same guard hand-written (`if (checks < N)`), so grepping for the macro
under-counts — an audit read that as two unguarded tests. Both floors are exact.

🔴 **AND IT IS A MUTATION ORACLE, WHICH MAKES "RAISE IT" A CORRECTNESS RULE RATHER THAN TIDINESS.**
Some mutants are caught by nothing except the check count: they change how many times a loop runs,
no assertion looks at that, and the floor is what fails. Measured 2026-09-12 — adding cases to
`tile_test.c` without raising its floor from 4,885 took `generic/tile.c:696` and `:776` from KILLED
back to SURVIVED, because the new checks gave the mutation enough headroom to stay above the old
floor. **Adding tests can LOWER the kill rate if you leave the floor behind.** Re-run
`mutate.ps1 -Recheck` after adding cases, and compare the per-file numbers in both directions.

🔴 **And run the sixth layer: `run-tests.ps1` includes `-Sanitize`**, the same cases under
UndefinedBehaviorSanitizer. Eleven seconds, and it sees memory-safety guards the plain pass cannot.
⚠ The old line here said "reverting jc-50 leaves every other layer green" — **measured 2026-09-11,
that is no longer true**: jc-57's direct cases for `movelaw_block()` and `movelaw_creature()` now
fail the plain pass with real assertions. Escalating the whole 2026-09-11 census through `-Sanitize`
found it catches **7 of 949 plain-pass survivors, 0.7%** — small, and not a reason to skip it.

⚠ **Check counts are a smoke alarm, not a measure of reach.** Three files are 94% of the 23,336.
Mutation kill rate is the number that means something, and since 2026-09-11 it is measured rather
than asserted: `mutate.ps1` breaks each source on purpose and counts how often the suite notices.
The figures live in [`docs/mutation-baseline.tsv`](docs/mutation-baseline.tsv), never in prose.
**Run `mutate.ps1 -SelfTest` before believing any census** — it plants five mutants whose verdicts
are known in advance and refuses to measure if it gets one wrong. `-Split` then sorts the survivors
into the 39% a test already reaches (cheap: add an assertion) and the 60% nothing reaches (needs a
new case). See `CLAUDE.md` §5 and
[`docs/adr/0013`](docs/adr/0013-the-kill-rate-is-measured-by-a-committed-harness.md).

`CLAUDE.md` §5 lists what is deliberately **not** covered — anything the GUI **draws** (its
*decisions* are tested under Qt's offscreen platform; its pixels are not), and **five of the 32
`NO_FIX_*` toggles**. Both are measured rather than impressions: the differential matrix
(`test/run-nofix.ps1`) holds a witness for 18 of the 32, a named unit case guards 9 of the other 14,
and the golden master alone distinguishes only 2. The remaining five are intra-tick, recorded with
the measurement that withdrew each. The matrix declares both counts on its `#!EXPECT` line and the
runner requires them **exactly**, so losing a witness or a guard fails the run instead of quietly
moving a toggle into the unguarded list. Read §5 before claiming a green run means more than it does.

## Style

- **American English** everywhere: `color`, `behavior`, `gray`, `analyze`, `center`, `-ize`.
- Comment *why*, not *what*. Mark fork edits `/* MOD (Jeremy, jc-N): ... */` and name the trap that
  motivated them.
- Indentation is **four spaces**, matching upstream; tabs after `#include`/`#define` are alignment
  and should be left alone. **Do not reformat upstream code** — whitespace churn poisons the diff
  against upstream, which is what keeps this fork reviewable.
- No PowerShell 7 syntax (`&&`, `||`, ternary, `??`).

## Pull requests

Run `run-tests.ps1` and include the summary. CI runs the build, both test layers and CodeQL. See
`.github/CONTRIBUTING.md` for the workflow and `.github/RELEASING.md` for shipping.
