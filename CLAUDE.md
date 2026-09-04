# Tile World — agent brief

You are working in **Jeremy Christman's fork of Tile World 2**, a C/C++ desktop emulator of the
*Chip's Challenge* game engines (MS and Lynx rulesets), with a Qt5 GUI on Windows.

Upstream is [SicklySilverMoon/tworld](https://github.com/SicklySilverMoon/tworld) at tag **2.3.1**;
this fork is public at <https://github.com/JeremyChristman/tworld> and is **GPLv2-or-later**. Builds
are tagged `jc-N` and published as GitHub releases that real people download and play.

Read this file completely before you change anything. Everything below is load-bearing, and most of
it is a mistake somebody already made here.

If you only read one thing, read [`AGENTS.md`](AGENTS.md) — it is the short version.

---

## 1. What this fork is for

Between jc-2 and jc-29 this fork existed to fix one problem: **solutions recorded in SuperCC would
not replay in Tile World.** Each of those builds reconciled a single MS-engine behavior against
SuperCC, measured over the whole solution corpus, and reported *levels fixed / levels broken*. **The
desync count reached zero at jc-28 and has stayed there.**

Everything since (jc-30 onward) is quality-of-life: settings, a background color, a death counter,
level-navigation wrapping, a tileset picker, keyboard fixes.

That history is why the engine is treated the way it is here. **`mslogic.c` carries more than eighty
`MOD (Jeremy)` edits and thirty-two `NO_FIX_*` behavior toggles.** A change to it does not just risk
a bug; it risks silently invalidating solutions that took people years to record.

---

## 2. Commands

Everything is PowerShell, targeting **Windows PowerShell 5.1**. The toolchain is **MSYS2** at
`C:\msys64` (gcc, cmake, ninja, Qt5 — and `qt5-static` for the shipping build).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1                    # -> build-static\tworld2.exe (ships)
powershell -ExecutionPolicy Bypass -File build.ps1 -Flavor dynamic    # -> build-dynamic\tworld2.exe (fast)
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
powershell -ExecutionPolicy Bypass -File run-tests.ps1                # unit + end-to-end
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Build         # build first, then both layers
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -ResultsPath test-results   # JUnit XML + JSON
powershell -ExecutionPolicy Bypass -File test\run-tests.ps1 -Filter mslogic        # one unit test
powershell -ExecutionPolicy Bypass -File test\run-e2e.ps1             # end-to-end only
powershell -ExecutionPolicy Bypass -File package.ps1                  # -> dist\TileWorld-<tag>.zip
powershell -ExecutionPolicy Bypass -File verify-defaults.ps1          # stock ini vs. settings.cpp
powershell -ExecutionPolicy Bypass -File coverage.ps1                 # gcov, unit layer
```

`build.ps1` does three things by hand that are easy to forget and that fail confusingly:

1. **Puts `C:\msys64\mingw64\bin` first on `PATH`.** The gcc driver cannot spawn `cc1` unless its own
   directory is on `PATH`, and when it cannot it fails with a nonzero exit and **not one word of
   diagnostic**. Calling gcc by full path is not enough.
2. **Removes `NoDefaultCurrentDirectoryInExePath` from the environment.** `CMakeLists.txt` generates
   a `comptime.bat` in the build directory and invokes it by bare name; with that variable set (the
   Windows default) the current directory is excluded from the search and the custom command cannot
   find its own script.
3. **Verifies the built exe actually contains `[jc-N]`**, by searching the binary for the UTF-16LE
   bytes. An incremental build can relink without recompiling the file that carries the tag.

### Running it

```powershell
.\build-dynamic\tworld2.exe        # needs C:\msys64\mingw64\bin on PATH for Qt's DLLs
```

🔺 **A scratch copy of the executable needs `zlib1.dll` and `libzstd.dll` beside it**, or it dies
with `0xC0000135` (`STATUS_DLL_NOT_FOUND`) — **no window, no stderr, and the process lingers**, so it
looks like a hang. Worse, the resulting "System Error" dialog survives killing the process and then
steals focus. If keys mysteriously go nowhere during a playtest, hunt for a `#32770` window titled
`Tile World.exe - System Error` and close it.

---

## 3. 🔴 The traps that make a test or a script LIE

These are not style notes. Each one produces a green result while proving nothing, and each was
measured here.

### 3.1 The executable is a Windows-GUI-subsystem binary

`CMakeLists.txt` uses `add_executable(... WIN32 ...)`, and `-mconsole` is applied only for Debug
builds. So from PowerShell:

```powershell
$out = & .\build-dynamic\tworld2.exe -b -r -S $save -L sets -D data -R res intro-ms.dac
```

**does not wait, and captures nothing.** Measured: it returns in about 11 ms against a real runtime
near a second, `$LASTEXITCODE` comes back **empty** (not 0, not 1), `$out` is the empty string — and
the process runs on detached afterwards. A test written this way asserts nothing and reports success.

**Always use `Start-Process -Wait -PassThru -NoNewWindow` with stdout and stderr redirected to
files.** With `-PassThru` but no `-Wait`, `$p.ExitCode` can still read back empty even after
`WaitForExit(ms)`. `test\run-e2e.ps1` has the working form.

(From a POSIX shell such as Git Bash, ordinary redirection *does* work — which is exactly how this
trap stays hidden until someone writes the PowerShell version.)

### 3.2 Batch verify's exit code is not a verdict

`batchverify()` reaches `exit()` with the invalid count **only when `-q` is given**. Without it the
program returns `EXIT_SUCCESS` however many solutions failed. And even with `-q`, "zero invalid" and
"no solutions were found at all" are both exit 0.

**Parse stdout and require `Valid solutions:   N` with a real N.** Never trust the exit code alone.

### 3.3 The test harness must compile in the same preprocessor world as the build

`test\run-tests.ps1` compiles with **`-std=gnu11` / `-std=gnu++11`, never `-std=c99`.** Under
`-std=c99` GCC sets `__STRICT_ANSI__`, and MinGW then defines `_WIN32` but **not** bare `WIN32` —
while `fileio.c:20` branches on `#ifdef WIN32` to choose `DIRSEP_CHAR` and `createdir()`. A
strict-ANSI test compiles the POSIX branch, which is not the branch that ships. (It happens not to
compile at all, which is the lucky outcome; a module that differed more quietly would just lie.)

### 3.4 `-r` protects the `.tws`, and nothing else

A `-b -r` run still creates `save\history` and, through `atexit(shutdownsystem)` → `savesettings()`,
**rewrites `tw_settings.ini` in the working directory**. Anything that runs the program must set its
working directory to a scratch folder. `test\run-e2e.ps1` asserts the repository's own settings file
was untouched, which is what proves that discipline held.

### 3.5 The corpus cannot see input-layer changes

`doturn()` ignores its `cmd` argument entirely whenever `state.replay >= 0`, and batch mode never
enables joystick behavior, so **`input()` is never called during verification**. A green full-corpus
run says *nothing* about `generic/in.c`, `generic/dirinput.c`, or anything else in the keyboard path.
jc-43 was byte-identical to jc-42 across 303 sets and 18,734 solutions, and that was expected rather
than reassuring. Hand playtesting and `test\input_test.c` are the only oracles for that class.

---

## 4. Repo map

| Path | What it is |
|---|---|
| `tworld.c` | `main()`, option parsing, `initdirs()`, the navigation commands, batch verify |
| `mslogic.c` | **The MS engine.** 4,800 lines; where this fork lives. 80+ `MOD (Jeremy)` edits |
| `lxlogic.c` | The Lynx engine. Barely touched by this fork |
| `encoding.c` | `.dat` level-record expansion — the untrusted-input parser |
| `series.c` | `.dac` config parsing, `.dat` level reading, series enumeration |
| `solution.c` | `.tws` reading/writing and the five-format move codec |
| `play.c` | `initgamestate()`, `doturn()` — the tick loop that drives the engine |
| `fileio.c`, `err.c`, `random.c`, `unslist.c` | Support |
| `settings.cpp` | `tw_settings.ini` |
| `fork.h` | **The one definition of the build tag**, author, and URLs |
| `generic/` | `in.c`, `dirinput.c`, `timer.c`, `tile.c` — shared by both front ends |
| `oshw-qt/` | The Qt front end. **This is what ships** |
| `oshw-sdl/` | The SDL front end. Not built or shipped by this fork |
| `data/`, `sets/` | Upstream's redistributable community level packs. See ADR 0005 |
| `test/` | The test suite. See §5 |
| `docs/adr/` | Why the surprising things here are deliberate |

---

## 5. Tests

```
run-tests.ps1              entry point: unit, then end-to-end
  test\run-tests.ps1       unit — compiles the source under test directly; needs only gcc
  test\run-e2e.ps1         end-to-end — drives the real executable's GUI-free command line
```

Current state: **8 unit runs, 16,877 checks; 11 end-to-end cases, 32 checks; 0 failures.**

### How a unit test is built

Each test is one C file that **`#include`s the source under test directly** (`#include
"../random.c"`), so no CMake tree and no built executable are needed — only a compiler. That reaches
`static` functions and file-scope state, which is most of this codebase.
See [`docs/adr/0003`](docs/adr/0003-tests-compile-the-source-under-test-directly.md).

**Every test is built twice, as C and as C++**, because `generic/in.c` is compiled as C by the SDL
build and as C++ by the shipped Qt build (through `generic/_in.cpp`).
See [`docs/adr/0004`](docs/adr/0004-every-test-is-built-as-c-and-as-cpp.md).

A test declares its own needs in comments on its first lines — the runner reads them, so the
knowledge lives with the test rather than in a table nobody updates:

```c
 * TESTLANG: c                       narrow to one language, with the reason
 * TESTFLAGS: -Wno-use-after-free    extra compiler flags
```

`TESTLANG` narrowing is a **claim** that the code under test is never compiled the other way.
`solution_test.c` and `mslogic_test.c` narrow to C because `fileio.c`, `solution.c` and `mslogic.c`
are compiled only as C by CMake and rely on C's implicit `void*` conversion.

⚠ Put the directive on its own line. The runner validates `TESTLANG` strictly and will reject prose
that runs on from it.

### The assertion harness

`test/tw_test.h` — header-only, C and C++, no allocation. `tw_begin`, `tw_case`, `tw_skip`,
`CHECK`, `CHECK_MSG`, `CHECK_INT`, `CHECK_NE_INT`, `CHECK_STR`, `CHECK_MEM`, `tw_end`.

🔴 **`tw_expect_atleast(N)` is the load-bearing part.** It fails the run if fewer than N checks
executed. A test function that stops being called — an early return, a case commented out during
debugging and never restored — removes coverage while leaving the suite green. **Raise the number
when you add cases; never lower it to make a run pass.** Lowering it is the bug it exists to report.

`run-tests.ps1` sets `TW_TEST_MACHINE`, which turns on `TWCASE`/`TWSUMMARY` marker lines that the
runner parses into JUnit XML and JSON. Run a test binary by hand and you get clean output instead.

### `-Werror`, and the warnings that are not yours

The unit suite compiles with `-Wall -Wextra -Werror`. The **release build deliberately does not** — a
future gcc inventing a new warning must never be able to stop a release going out.

Some modules are not warning-clean, and their tests suppress specifically:

| Module | Warning | Why it is suppressed rather than fixed |
|---|---|---|
| `solution.c:462` | `-Wuse-after-free` | GCC false positive. A failed `realloc` leaves the original pointer valid, which is the guarded branch |
| `mslogic.c:356` | `-Wunused-value` | The `_assert` macro's comma expression, at four call sites |
| `mslogic.c:2648` | `-Wunused-variable` | `value` in `resetdata()` |

All are pre-existing and none is a defect. They are suppressed per-test rather than fixed in the
source because **this is a fork tracking upstream and every cosmetic edit is a diff to carry
forever.** If a module is ever cleaned up, delete the suppression rather than leaving it as cover.

### Fixtures

`test/tw_fixture.h` builds a CC1 level record in memory from a 32×32 grid; `test/mkfixture.c` writes
a complete `.dat` + `.dac` + `.tws` set for the end-to-end tests, including one solution that must
verify and one that must not.

🔴 **Both are written from the FORMAT SPECIFICATION, not from `encoding.c`.** If a fixture builder is
written by reading the parser, a round trip proves only that the two agree with each other, and a
misreading in the parser is faithfully reproduced and never caught.

### What is NOT covered — read this before trusting a green run

- **The Lynx engine (`lxlogic.c`) has no unit test at all.** `mslogic_test.c` covers MS only.
- **The row-32 cloner glitch is only half covered.** `mslogic_test.c` pins the *loading* half
  (`readpos()` keeping `(x, 32)` distinct from `POS_INVALID`), which is unconditional. The half that
  `NO_FIX_ROW32_CLONER` actually guards — what happens when such a cloner **fires** — is not tested:
  building that file with `-DNO_FIX_ROW32_CLONER` still passes every case. That was measured.
- **The other 31 `NO_FIX_*` toggles have no differential test.** Each is a documented behavior
  difference with a known direction, and compiling one test both ways would be a real oracle. Nobody
  has done it.
- **No GUI is tested.** Everything in `oshw-qt/` — the score table's column spans, the color picker,
  the tileset menu, the death counter — is verified by hand only.
- **`series.c`'s `.dac` parser has no unit test**, though the e2e layer exercises it.

### Coverage — what the suite actually reaches

```powershell
powershell -ExecutionPolicy Bypass -File coverage.ps1
powershell -ExecutionPolicy Bypass -File coverage.ps1 -CheckBaseline
```

gcov, unioned across the C and C++ builds, **unit layer only** — the end-to-end tests drive an
uninstrumented executable, so what they reach is not counted and these figures understate the suite.

| File | Lines | Branches |
|---|---|---|
| `random.c` | 100.0% | **100.0%** |
| `generic/dirinput.c` | 100.0% | **97.8%** |
| `encoding.c` | 78.4% | **70.1%** |
| `generic/in.c` | 55.7% | **50.9%** |
| `solution.c` | 42.4% | **27.4%** |
| `mslogic.c` | 38.1% | **26.1%** |
| `fileio.c` | 30.6% | **16.7%** |
| `series.c` | 9.7% | **8.0%** |
| **overall** | 37.4% | **28.3%** |

**Read the branch column.** An emulator is mostly conditionals, and a line count flatters an
unexercised `switch` badly.

Three of these deserve explanation rather than embarrassment. **`series.c` at 8%** and **`fileio.c`
at 15%** are each compiled into a test for one function apiece — `readleveldata()` and the file
primitives it needs — so the other five hundred lines of series enumeration and `.dac` handling count
against them without being aimed at. **`mslogic.c` at 25.4%** is 4,800 lines of two rulesets' worth
of creature behavior against a suite that walks Chip around; it was 0% before this suite existed, and
the cheapest way to move it a long way is the `NO_FIX_*` differential matrix described above.

⚠ **The overall figure went DOWN between jc-44's first and second coverage runs, from 30.2% to
27.7%, while the suite grew.** Nothing regressed: adding `series_test.c` pulled `series.c`'s 558
lines into the denominator at 8%. That is exactly why the per-file column is the one to read, and why
`-CheckBaseline` compares files individually rather than the total.

`docs/coverage-baseline.tsv` records these. **There is no CI gate on them, deliberately** — adding a
test is supposed to move the numbers, and gating every push on a stale figure trains people to ignore
a red X. `-CheckBaseline` exists for a release to assert the documented numbers are still true.

---

## 6. `tw_settings.ini`

A plain `name=value` INI file, read **once at startup** and rewritten on a clean exit.
See [`docs/adr/0007`](docs/adr/0007-settings-live-in-tw-settings-ini.md).

- **Section headings are decoration.** A setting works the same wherever it sits. (The opposite of
  SuperCC's `succ_settings.ini`, where the section is part of a setting's identity.)
- **Comments are whole-line only**, `;` or `#`. There are no end-of-line comments —
  `legacyscores=true   ; roomy` has the value `true   ; roomy`, so the setting stays off.
- A duplicate key is not an error; **the last one wins**.
- Unrecognized keys are preserved under `[Other]` rather than dropped.

⚠ **The file is in the WORKING DIRECTORY, not beside the executable.** Nothing in this tree resolves
the executable's own path — no `GetModuleFileName`, no `applicationDirPath`, no `chdir`. "Beside the
executable" is true only because double-clicking makes the two the same folder. Do not restate the
stronger claim; it was wrong in the README and in two source comments before a review caught it.

**Adding a setting touches three places:** `settings.cpp`'s `SECTIONS[]`, the stock file generated by
`package.ps1`, and `README.txt` section 6. ⚠ `SECTION_MAXKEYS` is 12 and `[Display]` already holds 10
keys plus a terminator — the next `[Display]` setting must raise that constant.

---

## 7. Do not "fix" these — they are deliberate

Check [`docs/adr/`](docs/adr/) before changing anything that looks wrong.

1. **Two perpendicular arrow keys produce a diagonal.** That diagonal is a **block slap**, and CCLP3
   #16, CCLP5 #84 and several Lynx-only CCLXP2 levels are unsolvable without it.
   [ADR 0008](docs/adr/0008-accidental-diagonals-are-load-bearing.md).
2. **Thirty-two `NO_FIX_*` macros with dead-looking `#ifdef` scaffolding.** Each isolates one engine
   fix for differential measurement. Deleting one changes no shipped behavior, so nothing fails —
   until the next desync investigation needs the switch that is gone.
   [ADR 0002](docs/adr/0002-engine-fixes-are-opt-out-macros.md).
3. **The build tag defaults to OFF.** A downloader's title bar should not carry a private build
   number. [ADR 0006](docs/adr/0006-fork-h-owns-the-build-tag.md).
4. **`data/` and `sets/` contain committed level packs.** They are upstream's freely redistributable
   community sets. `CHIPS.DAT` is the copyrighted one and is never committed.
   [ADR 0005](docs/adr/0005-what-level-data-may-be-committed.md).
5. **`package.ps1` builds zip entry names by hand.** On PowerShell 5.1 both `Compress-Archive` and
   `ZipFile::CreateFromDirectory` write **backslash** entry names, which Info-ZIP on Linux and macOS
   does not treat as separators. This is a public download that has to open off Windows.
6. **`changecurrentgamewrapped()` is separate from `changecurrentgame()`.** Three of that function's
   callers are not the player navigating, and wrapping breaks each.
7. **"Last level" is `count - 1`, not `islastinseries()`.** The latter also answers TRUE for a
   `.dac`'s `lastlevel=` line, which stock `CCLP1-MS.dac` sets to 144 over a 149-level `.dat`.
8. **The static link, and the two DLLs that survive it.** `zlib1.dll` and `libzstd.dll` are pinned as
   dynamic imports by Qt's static CMake config.
   [ADR 0001](docs/adr/0001-one-statically-linked-executable.md).
9. **The score screen's grand-total row is 32 px where its neighbors are 20.** Pre-existing; the
   obvious cure was built, measured and backed out because it would elide long level names on sets
   past 1,000 levels.

---

## 8. Known defects

**Fixed in jc-44** (all upstream's; `git blame` puts them on the 2.3.1 import): a `.tws` could smash
a 256-byte stack buffer through `loadsolutionsetname()` — measured on the shipped jc-43 release
binary as a segfault at a 1000-byte declared set name; `readleveldata()` advanced a pointer by a
file-supplied size before dereferencing it; and the lower map layer's RLE guard reserved two fewer
bytes than the upper layer's.

**Fixed in jc-45**: the last unguarded map index. `initgame()`'s spring-the-traps loop dereferenced a
trap wiring's `to` with no bound, and `readpos()` validates only the X byte. Real: 7 malformed
wirings in 4 sets in circulation. Verified replay-neutral over the maintainer's whole collection —
290 sets, 0 of 303 outputs changed. See `FORK.md` item 17.

🔴 **The lesson from jc-45 is worth more than the fix.** A *behavioral* test cannot catch a
memory-safety fix whose entire point is that behavior does not change: the first version of that test
passed with the guard removed. The one that works **poisons the out-of-bounds byte** —
`map[POS_INVALID]` coincides exactly with `msstate`, so writing `Block_Static` into
`msstate.chipwait` is what the unguarded read sees. Reach for that shape whenever you fix a bound
here, and assert the layout assumption first so the case fails loudly rather than going quietly
vacuous.

What follows is what is still live.

### 8.1 `-v` cannot work as documented

The option string at `tworld.c:2205` is `"abD:dFfHhL:lm:n:PpqR:rS:stVv:c"` — `v:` declares that `-v`
takes an argument, while its handler takes none and the usage text says "Display version number and
exit". `tworld2 -v` prints "option requires an argument"; `tworld2 -v x` prints `2.3.1`. One
character. `testun-e2e.ps1` pins the current behavior deliberately, so fixing it turns that case
red and tells you to invert it. Upstream's.

### 8.2 The smaller ones

- **`combinepath()` reads `dest[-1]` when `dir` is empty** (`fileio.c:394`). No shipped configuration
  reaches it: `SAVEDIR` is defined for non-Windows Debug only, and `root` cannot be empty since
  jc-40. The one way in is an explicit `-R ""` on the command line. Latent, not live. Upstream's.
- **`series.c:41`** passes `sizeof g->list` (a pointer) where `sizeof *g->list` was meant. It
  over-allocates today, and becomes an under-allocation the moment that element type grows.
  Upstream's.
- **Both new bounds checks form a pointer before comparing it** (`series.c`, `encoding.c`) — e.g.
  `data + size + 2 > dataend`, which is technically undefined when the sum leaves the object. gcc and
  clang do not exploit that for byte pointers, and the idiom matches the surrounding upstream code.
  The fully-defined form is `(size_t)(dataend - data) < (size_t)size + 2`.

## 9. Conventions

- **American English** everywhere: `color`, `behavior`, `gray`, `analyze`, `center`, `-ize`. In code,
  comments, identifiers, output strings and documentation.
- **Indentation is spaces, four wide**, matching upstream. Tabs appear after `#include` and `#define`
  for alignment; leave those alone. `.editorconfig` carries this.
- Mark every fork-specific edit `/* MOD (Jeremy, jc-N): ... */` and **say what trap motivated it**.
  The comments in this codebase explain *why*, never *what*.
- **Do not reformat upstream code.** Whitespace churn poisons the diff against upstream, which is how
  this fork's changes stay reviewable.
- SOLID and GoF patterns only where they genuinely make the code cleaner. This is a C codebase from
  2001; do not import ceremony into it.
- No PowerShell 7 syntax — no `&&`, `||`, ternary, or `??`. The target is Windows PowerShell 5.1.
- When you change behavior a comment describes, **update the comment in the same edit**.

---

## 10. Shipping a release

The full sequence is in [`.github/RELEASING.md`](.github/RELEASING.md). The short version:

> **Two executables must never report the same build tag.**

1. **Bump `FORK_BUILD_TAG` in `fork.h`, and nothing else.** It is the only definition.
2. Update `README.txt` — the **header must name the new build**, or `package.ps1` refuses to
   package. Add a section 7 entry, and document any new setting in section 6.
3. Update `FORK.md` (the engineering record) and `CHANGELOG.md` (the summary).
4. `run-tests.ps1` green, both layers.
5. `package.ps1`, then **extract the zip somewhere clean and actually play it**. Reviews audit
   artifacts; this audits reality.
6. Commit, push, tag, push the tag. The tag push drafts a GitHub release; publishing stays a human
   act.

**Work that does not ship in the executable rides along with the next release that does.** Tests,
documentation and tooling do not earn a build tag of their own — accumulate them under `## Unreleased`
in `CHANGELOG.md`.

---

## 11. Working alongside other agents

- `build.ps1` writes into `build-<flavor>\`, and **`package.ps1` wipes all of `dist/`**. Those are
  shared mutable paths. Two agents building in one checkout will produce confusing, irreproducible
  failures. Give each agent its own `git worktree`, or its own `-BuildDir`.
- **One agent owns `FORK_BUILD_TAG` per release.** A published tag cannot be un-published.
- Announce your file set. `tworld.c`, `mslogic.c`, `package.ps1` and `README.txt` conflict with
  almost everything.
- **Leave no stray `tworld2.exe` running.** A live process holds the exe open and the next build's
  link step fails with a lock error that reads like a permissions problem. Kill by **PID**, captured
  from `Start-Process -PassThru` — never by process name, which would also kill an instance the
  maintainer is playing.
- ⚠ The working tree may contain a very large number of `build-*` directories from the desync
  project. They are gitignored, so CI never sees them, but they slow every recursive search. Scope
  your `Glob` and `grep` rather than sweeping the tree.

### `.claude/settings.json` is a convenience, not a security boundary

It is committed, so it applies to anyone running a coding agent in a clone of this repo.

**It reduces prompts; it does not contain an agent.** The `allow` entries are deliberately exact
rather than wildcarded, because a trailing `:*` would permit arbitrary extra arguments — and these
scripts have arguments that matter: `build.ps1 -BuildDir` and `-Manifest` write to any path. The
`deny` list is a typo-catcher for the obvious forms and nothing more; it is literal prefix matching,
so `git push origin main --force` sails straight past it. **The real protection for "two builds must
never report the same tag" is a GitHub ruleset on `refs/tags/jc-*` blocking deletion and
non-fast-forward** — enforced server-side, where no client-side pattern can be talked around.

**A pull request that edits `.claude/settings.json` is a privilege-escalation attempt against your
own agent.** Review diffs to it the way you review code, not the way you skim config.

---

## 12. When you are stuck

- **A behavior question about the engine**: `FORK.md` has the engineering record for every change,
  including what broke first and what was measured. It is long and it is worth reading the relevant
  entry in full.
- **"Was this deliberate?"**: `docs/adr/`, then `CLAUDE.md` §7, then `git log` — the commit messages
  here are unusually substantive.
- **"Does my change affect replay?"**: build the exe and batch-verify a corpus:
  `tworld2.exe -b -r -S <savedir> <set>.dac`, reading **stdout**, from a scratch working directory.
  Remember §3.5: this cannot see input-layer changes.
- **A GUI question**: there is no automated coverage. Build it, run it, and look.
- **Driving the GUI for a playtest**: press-and-hold opens a menu but releasing on an item does not
  pick it, and arrow keys inside an open menu do nothing. What works is clicking the menu title
  (down+up in place), then a **second separate click** on the item, locating both through UIA
  `FromHandle` → `FindFirst(Descendants, NameProperty)` — the item lives under the desktop root, not
  under the window. The title bar carries the level name, which makes `MainWindowTitle` a real oracle
  for which level is current, but read it after `$proc.Refresh()`, and note it reports a bare
  `Tile World` while a menu popup is open.
