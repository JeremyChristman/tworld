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
powershell -ExecutionPolicy Bypass -File test\run-qt-tests.ps1        # the oshw-qt layer (needs Qt)
powershell -ExecutionPolicy Bypass -File package.ps1                  # -> dist\TileWorld-<tag>.zip
powershell -ExecutionPolicy Bypass -File verify-defaults.ps1          # stock ini vs. settings.cpp
powershell -ExecutionPolicy Bypass -File verify-docs.ps1              # the docs still agree with the code
powershell -ExecutionPolicy Bypass -File coverage.ps1                 # gcov, unit layer
powershell -ExecutionPolicy Bypass -File mutate.ps1 -SelfTest         # prove the census harness is honest (~1 min)
powershell -ExecutionPolicy Bypass -File mutate.ps1                   # mutation census, unit layer (SLOW, ~30 min)
powershell -ExecutionPolicy Bypass -File test\run-golden.ps1          # golden-master engine snapshot
powershell -ExecutionPolicy Bypass -File test\run-golden.ps1 -Update  # REWRITE the baseline (deliberate)
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1           # NO_FIX_* differential matrix
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1 -Search   # REDISCOVER witnesses (slow, deliberate)
powershell -ExecutionPolicy Bypass -File test\run-playtest.ps1        # extract the release zip and prove it runs
```

Two more, neither of which is PowerShell:

```bash
test/run-corpus.ps1 ...          # replay differential over the whole collection -- see its header
test/run-sanitizers.sh           # ASan+UBSan over the unit tests. LINUX ONLY; the CI job runs it
test/run-fuzz.sh                 # libFuzzer over the .tws/.dat parsers. LINUX ONLY (needs clang)
FUZZ_SECONDS=0 test/run-fuzz.sh  # just replay the committed corpus, no fuzzing
FUZZ_CORPUS_OUT=dir test/run-fuzz.sh  # KEEP what it discovers; the weekly soak does this
```

🔴 **A UB check you CAN run on Windows**, which this repo wrongly believed impossible until jc-46 —
`-fsanitize=undefined` needs no `libubsan` if you pair it with `-fsanitize-undefined-trap-on-error`:

```bash
gcc -std=gnu11 -w -I test/stub -fsanitize=undefined -fsanitize-undefined-trap-on-error \
    -g -O1 -x c -o /tmp/t.exe test/solution_test.c && /tmp/t.exe
```

Undefined behavior becomes `SIGILL` (exit 132) instead of a readable report, which is all a gate or
a mutation check needs. `-w` is there because `-O1` turns on `-Wformat-truncation` in `tw_test.h`.

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
while `fileio.c:21` branches on `#ifdef WIN32` to choose `DIRSEP_CHAR` and `createdir()`. A
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

⚠ **And read BOTH streams it records.** Each set is saved as `<set>.out` (the verdict — which levels
were judged invalid) and `<set>.err` (the warnings). Only `.out` sets the exit code; `.err` is
compared **advisorily**, after normalizing `err.c`'s `[file.c:NNN]` stamp and the scratch directory's
per-run GUID. That normalization is not cosmetic: **adding a comment to `mslogic.c` moves `__LINE__`
and changes 29 of 303 sets' stderr**, which is what a raw diff shows you. jc-51 added the check after
a by-hand comparison found the script had been silent about that class the whole time.

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
| `test/golden/` | The golden-master engine snapshot and its committed baseline |
| `test/nofix/` | The `NO_FIX_*` differential matrix: which engine toggles are provably live |
| `docs/adr/` | Why the surprising things here are deliberate |
| `docs/toolchain.lock` | The pinned compiler. CI installs THAT package, then verifies it |

---

## 5. Tests

```
run-tests.ps1              entry point: runs ALL SIX layers below
  test\run-tests.ps1       unit — compiles the source under test directly; needs only gcc
  test\run-e2e.ps1         end-to-end — drives the real executable's GUI-free command line
  test\run-qt-tests.ps1    the oshw-qt layer; skips cleanly without Qt
  test\run-golden.ps1      the golden-master engine snapshot
  test\run-nofix.ps1       the NO_FIX_* differential matrix
  test\run-tests.ps1 -Sanitize   the same unit cases under UndefinedBehaviorSanitizer
```

🔴 **The sixth layer is new in jc-57 and it exists because of one measurement.** An adversarial
audit reverted **jc-50** — this fork's own headline defect, `movelaws[]` indexed by a cell's bottom
layer — and the unit suite, the golden master and the `NO_FIX_*` matrix all stayed green. So did
the committed fuzz reproducer for that exact defect, which is replayed on Windows every run: its
only oracle is "did it crash", and an out-of-bounds read of `.rodata` does not crash. The Linux
`sanitizers` job would have caught it; nothing a developer runs before pushing would.
`-fsanitize=undefined -fsanitize-undefined-trap-on-error` needs no `libubsan`, takes **11 seconds**
over the whole suite, and turns that revert into an exit-132 failure.

⚠ **A sanitizer is an oracle, not coverage** — it sees only what a test actually executes. Measured
on the same revert: `movelaw_creature` traps (the fuzz corpus happens to drive it) and
`movelaw_block` does **not**, because nothing called it with a bad id. Both halves are needed, which
is why jc-57 also added direct cases for those helpers.

Current state: **18 unit runs, 22,764 checks; 13 end-to-end cases, 38 checks; 2 Qt runs, 116 checks;
1,806 golden-master digests; 18 NO_FIX_* witnesses; 0 failures.**

🔴 **DO NOT READ 22,764 AS A MEASURE OF REACH. Three files are 95% of it.**
`random_test.c` alone is **15,534** — 68%, because it asserts a handful of properties a couple of
thousand times each — then `tile_test.c` 4,885 and `solution_test.c` 1,207. That leaves about
**1,100 checks for everything else**, including `mslogic.c` (210 KB), `tworld.c`, `lxlogic.c`,
`series.c`, `encoding.c`, `play.c`, `res.c` and `generic/`.

That is not padding: `random_test.c` kills 9 of 10 mutations, including all four LCG constants, so
it is a strong test whose *counting* happens to dominate. But an audit was right that leading with
the aggregate oversells the suite, and no document said so. **Mutation kill rate is the number that
means something; check count is a smoke alarm.** See §5's coverage note and `docs/coverage-baseline.tsv`.

⚠ **Four of those six numbers are now CHECKED rather than typed** — the unit pair against
`docs/test-counts.tsv` (written by `test\run-tests.ps1` on a complete run), the digests against
`test/golden/engine-snapshot.tsv`, the witnesses against `test/nofix/nofix-matrix.tsv`. Run
`verify-docs.ps1`. This sentence said 21,008 for several builds while a full run reported 21,012:
nothing broke, which is exactly why nobody noticed, and it was the last hand-typed count in this
file after the coverage table and the toggle count had already been moved to generated sources.

Two of those five need no test harness at all — they link the engines the way `tworld2` does and
drive real level data. They run from `run-tests.ps1` like everything else, and **they are the only
layers that can see an engine behavior change**, so run them after any edit to `mslogic.c`,
`lxlogic.c`, `encoding.c` or `random.c`:

- **`test\run-golden.ps1`** — the **golden-master engine snapshot** (the `golden` job). Every level
  in every committed `.dat`, through **both engines**, driven by a deterministic move stream and
  hashed: **1,806 digests over 903 levels in 1.6 s**, recorded in
  `test/golden/engine-snapshot.tsv`.

  🔴 **It is the only thing in CI that can see an engine behavior change.** Before it existed, the
  entire automated replay gate was one end-to-end case with a single valid and a single invalid
  solution, and a push that altered engine behavior went green everywhere.

  ⚠ **Know its reach before quoting it: 2 of 32 `NO_FIX_*` toggles, measured** — and neither more
  ticks nor more walks helps. It catches gross change (a mutation to Chip's idle timer moved 577 of
  1,806 rows). It is a smoke alarm, not an audit, and **`run-corpus.ps1` still decides a release.**
  Read the header of `test/golden/golden.c` before changing anything there; `-Update` rewrites the
  baseline and is a deliberate act, not a way to make a red run go green.

- **`test\run-nofix.ps1`** — the **`NO_FIX_*` differential matrix** (the `nofix` job). For each of
  the 32 engine toggles it asks whether any input tells a fix-on build apart from a fix-off one.
  **18 have such a witness**, committed in `test/nofix/nofix-matrix.tsv` and replayed on every push;
  the check asserts both digests are unchanged *and that the two still differ*. (This sentence said
  13 for five builds after the search was widened to 18, while three other places in this same file
  were corrected. `test/nofix/nofix-matrix.tsv` is the source; count it there, not here.)

  🔴 **It is the only check on the desync machinery.** Those toggles are opt-out macros, so a broken
  one changes no shipped behavior and nothing goes red — two of them had already rotted to the point
  of not compiling. `-Search` rediscovers witnesses and takes about half an hour; the check is
  seconds. See [`docs/adr/0012`](docs/adr/0012-engine-toggles-need-a-differential-witness.md).

Two more layers do not run from `run-tests.ps1`, because neither can run on Windows:

- **`test/run-sanitizers.sh`** — the unit suite rebuilt under ASan+UBSan (the `sanitizers` job). It
  found jc-46 on its first run. See §8.
- **`test/run-fuzz.sh`** — libFuzzer, **eight targets**, 60 s each per push (the `fuzz` job). Four
  parsers: `expandsolution()`, `readleveldata()`, `expandleveldata()`, `readconfigfile()`. **Both
  engines**: `fuzz_mslogic.c` and `fuzz_lxlogic.c` load a level *and play it*. And **two PROPERTY
  targets**, which assert that the code means something rather than merely that it does not crash:
  `fuzz_rc.c` re-derives the tileset-name rule independently and aborts on disagreement (a guard
  that wrongly returns TRUE does not crash) — it found jc-47 on its first run, a 64-byte leak in
  `prepareplayback()`; and `fuzz_settings.cpp` asserts that reading `tw_settings.ini` is idempotent
  under writing it, and found a real defect on ITS first run too (jc-54, a value ending in a
  carriage return did not survive its own round trip).
  ⚠ **`fuzz_settings.cpp` is C++ and the script has two lanes for that reason.** `settings.cpp` is
  C++ and CMake compiles it only as C++; the C targets, conversely, cannot be built as C++ at all
  because `fileio.c` and friends rely on C's implicit `void*` conversion. Same split as ADR 0004.

  🔴 **The engine targets cover a different class, and it is the one jc-45 was.** A parser target
  proves a bad file is *refused*; jc-45 was a file that was **accepted** and then dereferenced out
  of bounds inside `initgame()`. Their input is split — a move-count byte, a move stream, then the
  raw level record — so a reproducer encodes both the level and the play.

- **`.github/workflows/soak.yml`** — the **fuzz soak**: weekly on a schedule, plus
  `workflow_dispatch`. 15 minutes per target instead of 60 seconds, with the discovered corpus
  **cached between runs** so each soak starts where the last one stopped rather than re-deriving the
  same easy coverage.

  🔴 **Until this existed, nothing had ever explored past the first minute of any target** — and
  every fuzz find this fork has shipped landed right at that ceiling: jc-47 within seconds, jc-50 at
  one second, jc-51 at forty-three. A budget that keeps finding defects at its own edge is telling
  you where to look.

  ⚠ The cached corpus is **not** `test/fuzz/corpus/`, which stays curated: an input earns a place
  there by being attached to a fixed defect and a test case
  ([ADR 0011](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md)). Nothing in the
  soak writes to the repository. A finding fails the job and uploads the reproducer.

🔴 **But note what found jc-48: an ordinary unit test.** The `.dac` parser was the one untrusted-input
parser with no coverage, and writing its first test turned up two shipped defects in minutes. Tools
are not a substitute for covering a parser at all — **check what has no test before reaching for the
fuzzer.**

🔴 **But the fuzz corpus is replayed BY the unit suite, on Windows, every run.** Every seed and every
reproducer lives in `test/fuzz/corpus/<target>/`, and `solution_test.c`, `series_test.c` and
`encoding_test.c` each replay their directory through `test/tw_corpus.h`. **A finding is not fixed
until its input is in that corpus.** libFuzzer discovers; the corpus remembers.
See [`docs/adr/0011`](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md).

⚠ **Know exactly what that replay proves, because an earlier version of this file overclaimed it.**
It proves these inputs still parse without crashing or hanging, and that the parser did not modify
its own input. **It is not a memory oracle** — every one of these parsers is a read-only walker, so the
no-write check passes trivially and exists only to catch a future in-place decoder. ASan is the
memory oracle. And **a corpus of valid files cannot test rejection**: re-introducing jc-44's missing
bound was measured to leave the corpus replay green while the hand-written `encoding_test` case
failed. Never replace a behavioral case with a corpus input.

⚠ When you add a corpus input, the replay case's check count goes up — raise `tw_expect_atleast` in
that test. Do not lower it.

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
| `solution.c:474` | `-Wuse-after-free` | GCC false positive. A failed `realloc` leaves the original pointer valid, which is the guarded branch |
| `mslogic.c:356` | `-Wunused-value` | The `_assert` macro's comma expression, at four call sites |
| `mslogic.c:2748` | `-Wunused-variable` | `value` in `resetdata()` |

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

- ~~`play.c` has no unit test~~ — **closed**: `test/play_test.c`, 63 checks over 35 cases,
  0% → 26.6% lines. It installs a **fake engine** so that `play.c`'s own decisions are observable
  rather than the engine's, and it is where the §3.5 property is finally pinned: **`doturn()` ignores
  its `cmd` argument entirely during a replay.** ⚠ One case is deliberately narrower than it looks —
  the death-counter ceiling is enforced twice, so the case pins the *behavior*, not either guard; the
  case says so.
- ~~The Lynx engine (`lxlogic.c`) has no unit test at all~~ — **closed**: `test/lxlogic_test.c`, 79
  checks over 21 cases, 0% → 55.4% lines. ⚠ Read its header before adding to it: Lynx commits a
  creature's **position when the move begins**, not when it ends, and `advancegame()` withholds the
  result for a **13-tick endgame timer** after the level is decided. Both make naive tick arithmetic
  look like engine bugs. And **never use `chipisalive()` from a test** — it is `id == Chip`, which is
  false whenever Chip is `Pushing_Chip`, i.e. straining against a wall while perfectly alive.
- **The row-32 cloner glitch is only half covered.** `mslogic_test.c` pins the *loading* half
  (`readpos()` keeping `(x, 32)` distinct from `POS_INVALID`), which is unconditional. The half that
  `NO_FIX_ROW32_CLONER` actually guards — what happens when such a cloner **fires** — is not tested:
  building that file with `-DNO_FIX_ROW32_CLONER` still passes every case. That was measured, and
  **the golden master does not catch it either** — it was measured there too.
- 🔴 **14 of the 32 `NO_FIX_*` toggles have no differential witness — down from 30, and every number
  here is measured.** `test/nofix/` searches for an input whose result differs between a fix-on and a
  fix-off build; such an input is a **witness** that the fix is live and reachable, and the 18 found
  so far are committed in `test/nofix/nofix-matrix.tsv` and re-checked by CI on every push. See
  [`docs/adr/0012`](docs/adr/0012-engine-toggles-need-a-differential-witness.md).

  **What moved the number, and what did not — each step measured:**

  | approach | witnesses |
  |---|---|
  | golden master over all 903 real levels | 2 |
  | + ticks 400 → 2000, + walks 1 → 12 | 2 (nothing) |
  | generated 9×9 rooms, random fill | 1 |
  | + deliberately **stacked** cells | 13 |
  | + a **focus profile** built next to Chip | **18** |

  The limit was never search effort. A random walk does not *construct* a tank on a cloner, and a
  scattered room rarely puts Chip next to the one square that matters. Each seed now also lays a
  specific interaction in the three cells east of Chip — a block against a teleport, a tank on a
  cloner with its blue button, a creature in a wired beartrap — while the rest of the room stays
  random. That alone found five more, including `NO_FIX_TANK_ON_CLONER`, which had resisted every
  earlier attempt.

  ⚠ **A blank row is a statement about the SEARCH, not about the fix.** All 32 were separately
  confirmed to change the preprocessed source, so none is dead code, and a blank is never grounds for
  deleting a fix. `nofix -stats` reports what arrangements the generator is producing, which is how
  to tell "never built it" from "built it and nothing changed" before drawing any conclusion.

  ⭐ The sweep paid for itself twice over. **Two toggles turned out not to compile at all**
  (`NO_FIX_RFF_DRAW_ONCE`, `NO_FIX_TELEPORT_STALE_FG`) — see §8; all 32 build now. And **three**
  *pairs* share a witness seed with byte-identical fix-on and fix-off digests:

  | pair | seed |
  |---|---|
  | `RFF_DRAW_ONCE` / `RFF_CHIP_REARM` | 7572 |
  | `TELEPORT_STALE_FG` / `TELEPORT_BROKEN_DYNAMIC` | 2294 |
  | `KEEPSLOT_OCCUPANT` / `KEEPSLOT_BLOCK_OCCUPANT` | 487376 |

  For the first two that is explainable: each is the pair whose declarations were tangled together,
  and they touch the same path. ⚠ **The third is not explained, and nobody has looked** — this
  passage said "two pairs" until an independent review counted three, and the reasoning built on
  "two" never had to account for it. Identical digests mean the generator cannot tell the two
  toggles apart, and `mslogic.c:234` gates `FIX_KEEPSLOT_OCCUPANT` in a way that suggests
  `NO_FIX_KEEPSLOT_BLOCK_OCCUPANT` may be **subsumed** by it rather than independent. Worth an hour
  before anyone trusts that matrix row as two separate witnesses.
- **No WIDGET is tested**, still — the score table's column spans, the color picker, the tileset
  menu, the death counter are all verified by hand, because each needs a `QApplication` and a paint
  device and asserting on painted pixels is a much weaker test than it looks. **2 of `oshw-qt/`'s 8
  files are covered**, and both were picked on the same principle: cover the ones that are *not*
  widgets.
  - `test/qt/ccmetadata_test.cpp` — `CCMetaData.cpp`, the `.ccx` parser, 90 checks. The only parser
    in the tree **no other layer can reach**: `readextensions()` returns immediately when
    `g_pMainWnd` is null, so batch mode, the e2e cases and every fuzz target skip it by construction.
  - `test/qt/textcoder_test.cpp` — `TWTextCoder.cpp`, the CC1↔Unicode codec every level name,
    password and hint passes through, 26 checks. ⭐ **It found a shipped defect on its first run** —
    `encode()` was shifted one byte below `decode()` for eleven characters — now fixed by making
    `encode()` a reverse lookup of the decode table, so the two are inverse by construction. See §8.
  Qt-linked tests need their own runner: `test\run-qt-tests.ps1`.
- ~~`unslist.c` is never exercised~~ — **closed, and the claim was wrong twice over.** `unslist.c`
  is live and shipped: `res/rc` line 6 sets `UnsolvableList=unslist.txt` and `series.c:404` marks
  every series. `test/unslist_test.c` covers it with 45 checks, **0% → 90.7% lines**, including a
  case that parses the real `res/unslist.txt`. 🔴 The reason it was twice written off as dead is
  worth carrying: the rc file spells the key `UnsolvableList`, `rclist[]` spells it
  `unsolvablelist`, and `readrcfile()` lowercases before comparing — so grepping for the table's
  spelling finds nothing and reads exactly like proof of absence. **Follow the call, not the grep.**
- ~~`series.c`'s `.dac` parser has no unit test~~ — **closed in jc-48**, and writing that test found
  two shipped defects immediately (a path guard that could not work on Windows, and eleven ctype
  calls on a signed `char`). It has 40 unit checks and a fuzz target now. The lesson is the cheapest
  one in this file: **the parser with no test was the parser with the bugs.**
- ~~**Neither engine is fuzzed, only the parsers.**~~ — **closed**: `test/fuzz/fuzz_mslogic.c` and
  `fuzz_lxlogic.c` load a level *and play it*, which is the class a parser target structurally cannot
  reach — a file that is **accepted** and then breaks the engine. It paid for itself twice
  immediately: **jc-50** (one second into the first run) and **jc-51** (43 s into the next). jc-45 was
  the same shape and had to be found by hand.
  ⚠ **What is still uncovered is the other ruleset's depth.** Both targets exist, but `mslogic.c` sits
  at 44.8% lines with thirty-two `NO_FIX_*` branches largely unexercised; the fuzzer reaches what a
  short random move plan reaches. The `NO_FIX_*` differential matrix below is still the cheapest way
  to move it.

### Coverage — what the suite actually reaches

```powershell
powershell -ExecutionPolicy Bypass -File coverage.ps1
powershell -ExecutionPolicy Bypass -File coverage.ps1 -CheckBaseline
```

gcov, unioned across the C and C++ builds, **unit layer only** — the end-to-end tests drive an
uninstrumented executable, so what they reach is not counted and these figures understate the suite.

🔴 **THE NUMBERS LIVE IN [`docs/coverage-baseline.tsv`](docs/coverage-baseline.tsv), AND THERE IS NO
COPY OF THEM HERE ON PURPOSE.** That file is generated by `coverage.ps1 -UpdateBaseline` and checked
by `-CheckBaseline`; a table in this document is a hand-maintained duplicate of it, and duplicates
drift. This one did: an independent review found it listing thirteen files when the baseline had
sixteen, missing `settings.cpp` — which at 91.3% would have been the second-best-covered file in the
tree — and quoting an overall figure two points stale, plus one file's branch percentage in a
sentence about lines. **ADR 0006 makes `fork.h` the single definition of the build tag and CI
enforces it; the same principle applies to facts, and this is where it was not being applied.**

🔴 **READ THE PER-FILE COLUMN, NOT THE TOTAL.** The overall figure has fallen twice while nothing
regressed and coverage was *added*: a large, barely-tested file entering the denominator drags the
total down. `tworld.c` is 1,338 instrumented lines against a test aimed at five functions.
`-CheckBaseline` compares files individually for exactly that reason, and the total is the least
useful number the tool prints.

⭐ **`lxlogic.c` went from 0% to the best-covered engine in the tree** — ahead of `mslogic.c`, which
has more cases behind it. Not because the Lynx test is cleverer: `lxlogic.c` is 1,073 instrumented
lines against `mslogic.c`'s 1,654, and its core movement paths are dense rather than spread across
thirty-two `NO_FIX_*` branches. The cheapest way to move `mslogic.c` is still the differential matrix
described below.

**Read the branch column.** An emulator is mostly conditionals, and a line count flatters an
unexercised `switch` badly.

⭐ **The engine fuzz corpora are why `mslogic.c` and `encoding.c` moved so far in jc-51** — 38.1% →
44.8% and 82.7% → 89.9% lines, with branches up 7 points apiece. Nobody wrote a case aimed at those
lines. `mslogic_test.c` and `lxlogic_test.c` each replay their fuzz corpus through the real engine,
so **every reproducer a fuzzer finds becomes permanent coverage of whatever path it happened to
reach.** That is a second, unadvertised return on the corpus discipline in
[`docs/adr/0011`](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md).

🔴 **MEASURE THE HALF THAT MATTERS BEFORE ACTING ON A FILE'S SCORE.** An audit reported `res.c` at a
33% mutation kill rate and `series.c` at 55%, the two worst outside `generic/tile.c`, and framed both
as untrusted-input parsers left uncovered. Checked one guard at a time, that framing is wrong — every
guard on the untrusted path dies:

| guard | result |
|---|---|
| `res.c` `istilesetname()`: separators, colon, control chars, `..`, reserved names | all killed |
| `series.c` `readconfigfile()`: path separators, reserved filename, `lastlevel` range | all killed |

What drags those numbers down is the *other* half: `res.c`'s loaders (`loadimages`, `loadcolors`,
`loadfont`, `loadsounds`) need a real resource-file environment and parse no attacker-controlled
structure — their failure mode is a visible "cannot load" — and `series.c` is compiled into a test
aimed at two of its functions, so five hundred lines of series enumeration count against it. **Neither
number is evidence of an exposed parser, and writing tests to move them would buy coverage of the
least dangerous code in each file.** Recorded so the next reader spends the effort where the last
measurement says it pays.

Two of these deserve explanation rather than embarrassment. **`series.c` at 19.3%** and **`fileio.c`
at 40.1%** are each compiled into a test aimed at a couple of functions — `readleveldata()`,
`readconfigfile()`, and the file primitives they need — so the other five hundred lines of series
enumeration count against them without being aimed at. **`mslogic.c` at 44.8%** is 4,800 lines of two
rulesets' worth of creature behavior against a suite that walks Chip around; it was 0% before this
suite existed, and the cheapest way to move it further is the `NO_FIX_*` differential matrix
described above.

⚠ **The overall figure went DOWN between jc-44's first and second coverage runs, from 30.2% to
27.7%, while the suite grew.** Nothing regressed: adding `series_test.c` pulled `series.c`'s 570
lines into the denominator at 8%. That is exactly why the per-file column is the one to read, and why
`-CheckBaseline` compares files individually rather than the total.

`docs/coverage-baseline.tsv` records these. **There is no CI gate on them, deliberately** — adding a
test is supposed to move the numbers, and gating every push on a stale figure trains people to ignore
a red X. `-CheckBaseline` exists for a release to assert the documented numbers are still true.

### Mutation — what the suite actually *catches*

```powershell
powershell -ExecutionPolicy Bypass -File mutate.ps1 -SelfTest   # ~1 min; do this first
powershell -ExecutionPolicy Bypass -File mutate.ps1             # ~30 min
```

Coverage says a line was executed. **Mutation says whether anything would have noticed if the line
were wrong**, which is the question the check count and the coverage percentages both dodge.
`mutate.ps1` breaks each source on purpose — 1,267 single-token edits over the sixteen sources the
tests compile — and counts how often the suite fails. See
[`docs/adr/0013`](docs/adr/0013-the-kill-rate-is-measured-by-a-committed-harness.md) for why this is
a committed harness rather than an audit.

🔴 **THE NUMBERS LIVE IN [`docs/mutation-baseline.tsv`](docs/mutation-baseline.tsv), AND THERE IS NO
COPY OF THEM HERE**, for exactly the reason the coverage table above is not duplicated either.

🔴 **NOTHING MAY QUOTE "45% → X%".** The 2026-09-08 audit's 45% was 233 mutations a person chose by
hand, aimed at guards. This is a mechanical census, and its blended rate is a function of the
operator mix — turning on a second operator moves the headline without one thing about the suite
changing. The two numbers measure different quantities. Read the per-file column.

⚠ **`-SelfTest` is not optional before believing a census.** It plants four mutants whose verdicts
are known in advance — one that must be killed, one that must not compile, one that must survive
because it sits on a line the compiler never sees, and one that must hang — and refuses to measure if
any comes back wrong. Between them they prove the mutation really reached disk, the tests really ran,
INVALID is not scoring as KILLED, and the timeout and process-tree-kill path recovers.

⭐ **SURVIVED IS A REAL GAP, AND THAT IS MEASURED RATHER THAN ASSUMED.** The obvious worry about a
unit-layer kill rate is that the other five layers catch what it misses, so the survivor list is
padded and nobody should act on it. `mutate.ps1 -Escalate <mutants.tsv>` re-runs every survivor under
`-Sanitize` and settles it: of the 2026-09-11 census's **949 survivors, the sanitize layer caught 7
— 0.7%.** Read SURVIVED as a gap. Do not hedge it.

⚠ **That is not a verdict on the sanitizer.** It judges only what a test actually *executes*, and it
answers a different question than a boundary mutation asks. Its value on the class it was added for
is on the record: jc-50 was invisible to every other local layer.

⭐ **THE SURVIVOR LIST IS TWO DIFFERENT PROBLEMS, AND `mutate.ps1 -Split <mutants.tsv>` SEPARATES
THEM.** "Nothing noticed this mutation" has two causes with different fixes and very different costs,
and joining the survivors against gcov's per-line map tells them apart:

| bucket | meaning | the fix | count |
|---|---|---|---|
| **REACHED** | a test runs the line and does not assert enough to notice | usually a few lines in a test that already exists | **370 (39%)** |
| **UNREACHED** | no test runs the line at all | a new case that gets there first; no assertion can help | **573 (60%)** |
| **NO-RECORD** | gcov has no record for the line | see below — **not** a synonym for unreached | **6** |

**Spend on REACHED first.** The test already gets there; it just does not look. `mslogic.c` has 148
of them and `lxlogic.c` 96 — between them 66% of the cheap queue.

🔴 **NO-RECORD IS A THIRD DIAGNOSIS AND MUST NEVER BE FOLDED INTO UNREACHED.** Two things produce it.
One is file-scope data — gcov emits no line record for an initializer, so a mutation inside
`movelaws[]`, the table this fork's headline defect indexed out of bounds, would be filed under
"nothing runs it, deprioritize". The other is what all six actually are here: **constant-folded
code.** `mslogic.c:2251` is `if (FALSE && …)` and `:3452` is `if (TRUE || …)`, both upstream
short-circuits, so the compiler deletes the rest of the condition and **any mutation inside it is
provably an equivalent mutant.** That is the equivalent-mutant registry, derived instead of asserted.

🔴 **The seven the sanitizer caught are worth reading individually — they are guards nothing pins.**
Each is a bound the plain suite runs straight through without noticing:

| site | mutation | what it opens |
|---|---|---|
| `res.c:251`, `res.c:258` | `ruleset >= Ruleset_Count` → `>` | lets `ruleset == Ruleset_Count` reach `tilesetkey[ruleset]`, one past the array — the jc-45/jc-50 shape exactly |
| `generic/tile.c:1191` | `n < sizeof tileptr / sizeof *tileptr` → `<=` | one past the tile-pointer table |
| `generic/tile.c:1194` | `m < 16` → `<=` | one past a 16-entry row |
| `generic/in.c:367` | `n < TWK_LAST` → `<=` | one past the key table |
| `lxlogic.c:1994` | `putwall() != -1` → `==` | inverts the wall-placement failure test |
| `fileio.c:470` | `dest != dir` → `==` | inverts a path comparison (caught by assertion, not UB) |

⚠ **`settings.cpp` reports 22 INVALID, and they are generator noise, not a codebase fact.** `<` and
`>` inside a C++ template argument list are not relational operators, so `map<string, string>`
becomes `map<=string, string>` and does not compile. INVALID is excluded from the denominator, so the
rates are still computed over real mutants — but a per-file INVALID rate this far above the global
one is the signature to look at before trusting a file's row.

---

## 6. `tw_settings.ini`

A plain `name=value` INI file, read **once at startup** and rewritten **every time a setting
changes** — six call sites, not just at exit (corrected in jc-54; the old wording said "on a clean
exit" and had been wrong since jc-31).
See [`docs/adr/0007`](docs/adr/0007-settings-live-in-tw-settings-ini.md).

🔴 **The write is atomic, and the retry matters more than the atomicity.** `savesettings()` stages
the file as a sibling `tw_settings.ini.tmp-<pid>-<seq>` and moves it over the target. Measured, on
Windows, with a scanner holding the destination open: one move attempt loses **19%** of writes,
four attempts lose **none** — so a naive atomic write without the retry would be a *worse* bug than
the torn file it fixes. The backoff steps are deliberately not multiples of each other. Do not
"regularize" them, do not open the staging file in binary (the text-mode CRLF translation IS the
file format), and do not add `MOVEFILE_WRITE_THROUGH` or `ReplaceFile` — `settings.cpp` records
what each one measured.

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

**Adding a setting touches FOUR places:** `settings.cpp`'s `SECTIONS[]`, the stock file generated by
`package.ps1`, `README.txt` section 6, and the string literal in `settings_test.c`'s "comes back
BYTE FOR BYTE" case. ⚠ `SECTION_MAXKEYS` is 16 and `[Display]` holds 12 keys plus a terminator.

Run **`verify-defaults.ps1`** after: it compares all three machine-readable copies against each
other — the code's table, the shipped file, and the test's literal — and reports which one is
behind. It said "three places" here until jc-56, when the fourth was found by having drifted.

🔴 **Two switches, two predicates, and they are not interchangeable.** `settingoptedin()` is for a
setting that defaults OFF; `settingoptedout()` is for one that defaults ON. Both answer FALSE for an
absent, blank or unparseable value — each reading it as "no opinion, keep MY default" — so
`!settingoptedin()` is *not* the opt-out predicate, and using it would turn a default-on feature off
the moment somebody typed `yes`. Match the predicate to the default; never share one across two
switches whose defaults differ.

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

## 8. What the defects taught

🔴 **THE PER-DEFECT NARRATIVE LIVES IN [`FORK.md`](FORK.md), AND THERE IS NO SECOND COPY OF IT HERE.**
This section used to retell every fix in full, which meant every fix was written out three times —
here, in `FORK.md`, and in `CHANGELOG.md`. That triplication was not harmless: an independent review
traced four separate wrong statements in this repository to it, because a correction would land in
one telling and not the others. `SECURITY.md` spent five builds asserting something two other
documents had already disproven.

So this section keeps only what does not belong anywhere else — **the lessons that transfer to the
next change** — and points at `FORK.md` for what actually happened. If you are about to add a defect
story here, add it to `FORK.md` instead and put the lesson here, once.

| Build | What it was | Detail |
|---|---|---|
| jc-44 | Three `.tws`/`.dat` memory-safety defects: a 256-byte stack smash, a pointer advanced by a file-supplied size, an RLE guard two bytes short | `FORK.md` items 14–16 |
| jc-45 | The last unguarded map index — a trap wiring's `to`, dereferenced with no bound. 7 malformed wirings in 4 sets in circulation | `FORK.md` item 17 |
| jc-46 | Signed-shift overflow assembling a `.tws`'s 32-bit fields. Fired on about half of every solution file ever recorded; no replay was ever affected | `FORK.md` item 18 |
| jc-47 | A 64-byte leak on every failed playback | `FORK.md` item 19 |
| jc-48 | A `.dac` could name a file outside the data directory, including a Windows device name; and 22 ctype casts on a signed `char` | `FORK.md` item 20 |
| jc-50 | **This fork's own.** `movelaws[]` indexed by a cell's bottom layer, which can hold a creature — up to 47 entries past a 64-entry array, on 18% of real levels | `FORK.md` item 21 |
| jc-51 | `chipsneeded` is a signed `short` filled from an unsigned file word, so a level demanding ≥ 0x8000 chips opened the socket and then killed the program | `FORK.md` item 22 |
| jc-52 | `TWTextCoder::encode()` shifted one byte for eleven characters; two more unguarded `movelaws[]` indexes; an uninitialized pointer on a path-qualified command line | `FORK.md` items 23–25 |
| jc-54 | `tw_settings.ini` was rewritten by truncating it in place, so an interrupted write destroyed it; and a value ending in a carriage return did not survive its own round trip | `FORK.md` items 26–27 |
| jc-56 | Not shipped defects — a feature, and five quiet failures found by building its guards: an unchecked third copy of the stock settings file, a documented count four out, a `foreach` variable that had been eating a script parameter since the file was written, a **flaky wall-clock test that burned the jc-55 tag**, and `package.ps1` deleting the build manifest RELEASING.md tells you to write one command earlier | `FORK.md` items 28–32 |
| jc-57 | **An adversarial audit's findings.** A release gate that reported replaying solutions it had skipped; eleven of twelve engine bound-mutations surviving every local layer, jc-50 revertible wholesale among them; `encoding.c`'s run-length bound unREACHED rather than undetected; `verify-docs.ps1` failing open; and two latent `generic/tile.c` defects | `FORK.md` items 33–38 |

Every one of those is replay-neutral where it touches the engine, and the evidence is in `FORK.md`
with the release that carries it.

### 8.1 The lessons, which is why this section exists

**🔴 A behavioral test cannot catch a memory-safety fix whose entire point is that behavior does not
change.** The first version of jc-45's test passed with the guard removed. The one that works
**poisons the out-of-bounds byte**: `map[POS_INVALID]` coincides exactly with `msstate`, so writing
`Block_Static` into `msstate.chipwait` is what the unguarded read sees. Assert the layout assumption
first, so the case fails loudly rather than going quietly vacuous.

⚠ **This is the single most reusable idea in the file, and it was needed again in jc-54.** An
independent review mutated `encoding.c:193` — the upper map layer's bound — and *nothing* caught it,
not the unit suite and not the golden master. The obvious fix did not work either: asserting the
record is refused passes either way, because the lower layer's guard rejects it later for a different
reason. The oracle had to be **whether the decode loop ran at all**. When you bound something, ask
what observable side effect only happens on the wrong side of the bound.

**🔴 Run the instrument before reading another parser by hand.** Three consecutive releases were
found by a tool nobody had aimed at a line: jc-46 by UndefinedBehaviorSanitizer on the sanitizer
job's first run, jc-47 by LeakSanitizer on the fuzz job's first run, jc-50 by the MS-engine fuzz
target **one second into its first run**. All three were in lines nobody had reason to suspect,
through tests that had been green for weeks. (`-fsanitize=undefined` runs on Windows too — see §2.)

**⚠ A parser fuzzer structurally cannot find an engine defect.** jc-50 needed a level that *loads*
and a creature that *tries to move*. jc-45 was the same shape and had to be found by hand. That is
why `fuzz_mslogic.c` and `fuzz_lxlogic.c` exist alongside the parser targets.

**🔴 Before reaching for a fuzzer, check what has no coverage at all.** jc-48's two defects were both
in `readconfigfile()` — the only parser in the C core with no test of any kind — and both fell out of
writing its first one. The lesson was applied immediately afterwards: `oshw-qt/CCMetaData.cpp` was
the next thing with none, and now has `test/qt/ccmetadata_test.cpp`. It found nothing, and **"we
looked and it was fine" is a result worth having**, because until it existed nobody could say so.
The same reasoning produced the first tests for `settings.cpp`, `generic/tile.c` and `score.cpp`;
the first two of those found defects.

**⚠ When you fix an out-of-bounds read, do not assume there is a correct old value to preserve.** The
old read in jc-50 was undefined — what it returned depended on what the linker placed after the array
— so replay was never guaranteed stable across toolchains for those levels. Pick the defensible
answer and **measure it against the corpus**, which is what settled it.

**🔴 An assert tells you which invariant broke. Only tracing the reproducer tells you why.** This file
used to record a suspicion about jc-51 — "something reaches `endmovement()` with a socket destination
without passing that gate" — and it was wrong, and it pointed at the wrong file. Nothing bypassed the
gate; the gate itself said yes, because the value was negative. **When a value can be negative, check
the type before you go looking for an exotic control-flow path.**

**🔴 Two hand-maintained tables that must agree, with nothing checking that they do, will disagree.**
jc-52's `TWTextCoder::encode()` was fixed by **deleting** its switch rather than correcting eleven
constants: `encode()` now reverse-looks-up the same table `decode()` uses, so the two are inverse *by
construction*. Correcting the constants would have fixed that instance and left the next edit free to
reintroduce it. Prefer removing the second source over synchronising it — the same reasoning retired
the coverage table in §5 and produced `verify-docs.ps1`.

**⚠ `#ifdef` scaffolding is untested code, and untested code rots.** Two `NO_FIX_*` toggles had
decayed to the point that defining them stopped `mslogic.c` compiling — a declaration guarded by one
toggle and used under another. Nothing in the repository would have said so, because a broken opt-out
changes no shipped behavior: not the unit suite, not the fuzzers, not CI. This is exactly the rot
[ADR 0002](docs/adr/0002-engine-fixes-are-opt-out-macros.md) exists to prevent, and it had already
set in. **If you add a `NO_FIX_*`, build with it defined at least once before committing.** The
`nofix` job now builds all 32.

**⚠ Do not "finish a sweep" without building what you touch.** Three ctype instances remain in
`oshw-sdl` (`sdlout.c:812`, `sdltext.c:110`, `:336`), deliberately left: those three files are not
compiled here, so the change could not be built or tested. Note the precise claim —
`oshw-qt/CMakeLists.txt` *does* compile `oshw-sdl/sdlsfx.c`, which simply has no ctype calls.

**🔴 A comment that correctly predicts a trap is still not a check.** `settings.cpp` said, in as
many words, "the NEXT `[Display]` setting must raise `SECTION_MAXKEYS`". jc-56 added two and the
comment was read and acted on — which is the good outcome, and it is luck, because nothing would
have failed if it had not been. The rule that generalizes: **when you write a comment predicting
how the next edit will break something, you have just specified a test.** Write that instead, or as
well. Both were cheap here — one asserts every row is terminated inside the bound, and it fires
with a readable message.

**🔴 A LESSON WRITTEN DOWN AND APPLIED ONCE IS NOT APPLIED.** The poisoned-byte technique above is
called, three paragraphs up, "the single most reusable idea in the file". An audit found it had been
used on **exactly one** of at least four analogous guards, and the other three were all revertible
with the whole suite green — including jc-50, this fork's own headline defect. When you solve a
class of problem, **grep for the rest of the class in the same sitting.** The lesson costs nothing
to write and everything to leave unapplied.

**🔴 "NOT DETECTED" AND "NOT EXERCISED" ARE DIFFERENT DIAGNOSES AND NEED DIFFERENT FIXES.** Separate
them before reaching for a tool. `encoding.c`'s run-length bound was not undetected — no input in
the tree, and no committed fuzz reproducer, decoded past 1,024 cells, so nothing ever *ran* it and
no sanitizer could have helped. Meanwhile jc-50's revert was executed and simply unnoticed, which a
sanitizer fixes instantly. **A sanitizer is an oracle, not coverage.** Measured on one revert:
`movelaw_creature` traps, `movelaw_block` sails through, and the only difference is whether a test
happened to call it.

**⚠ ASK WHAT ONLY HAPPENS ON THE WRONG SIDE OF THE BOUND — AND THEN CHECK THAT NOTHING ELSE CAUSES
IT.** The obvious oracle is usually contaminated. "The guard warns, so count warnings" failed twice
in one sitting: remove the creature-list bound and the *next* check warns instead, from the
out-of-bounds bytes it just read; give a test record a short lower layer and it warns for its own
unrelated reason. Both versions passed while killing nothing. The working oracles were a phantom
creature and a non-zero row 32 — side effects with exactly one possible cause.

**⚠ A CHECK THAT CRIES WOLF IS WORSE THAN THE GAP IT CLOSES.** Broadening `verify-docs.ps1`'s
patterns to catch six rewordings immediately failed on three *correct* sentences — a negation, an
unrelated fact sharing a number, and a per-file figure. The fix people reach for then is deleting
the check. Run a widened checker against the **real** documents before believing it.

**🔴 NEVER MAKE ELAPSED TIME THE ORACLE WHEN THE PROPERTY IS A COUNT.** jc-54 proved its settings
retry loop ran by asserting 30 ms had passed, arguing "Sleep can only ever overshoot, so this cannot
flake in the fast direction." It flaked and **burned the jc-55 tag**, on the same commit whose CI
job had just passed. Two reasons the argument missed: `GetTickCount64` advances in ~15.6 ms steps,
so the *measurement* undershoots however faithfully `Sleep` overshoots; and how long `Sleep(2)`
takes depends on the system timer resolution, which **any other process** can change globally with
`timeBeginPeriod`. 71–80 ms on the desktop, 16 ms on the runner. The fix was to count the attempts —
exact, free, and the thing actually meant. **Ask what you are really asserting: "four attempts
happened" is a count, and a count that has to be inferred from a clock is a count you should just
keep.**

**⚠ A shadowed parameter does not fail; it answers wrongly, somewhere else.** `foreach ($lang in
...)` in `test/run-tests.ps1` **is** the `-Lang` parameter, because PowerShell variable names are
case-insensitive, and it had been overwriting the caller's argument since the file was written. The
top of that same script warns about this exact trap for `$OutDir`. Nothing was red: the damage
showed up in a skip message naming the wrong language, and years later in a new guard that read
`$Lang` and silently did nothing. **Knowing a trap is not the same as being immune to it** — grep
your own loop variables against your parameter names, case-insensitively.

**⚠ Some fixes are provably behavior-preserving and some are merely believed to be.** jc-51 kept the
signed `short` and changed both predicates to `!= 0`, because for every non-negative count `> 0` and
`!= 0` are the same predicate — that is a proof. Widening the field would have touched a struct every
engine path reads, which is not.

What follows is what is still live.

### 8.2 `-v` cannot work as documented

The option string at `tworld.c:2256` is `"abD:dFfHhL:lm:n:PpqR:rS:stVv:c"` — `v:` declares that `-v`
takes an argument, while its handler takes none and the usage text says "Display version number and
exit". `tworld2 -v` prints "option requires an argument"; `tworld2 -v x` prints `2.3.1`. One
character. `test/run-e2e.ps1` pins the current behavior deliberately, so fixing it turns that case
red and tells you to invert it. Upstream's.

### 8.3 The smaller ones

- **`combinepath()` reads `dest[-1]` when `dir` is empty** (`fileio.c:472`, inside `combinepath()` at `:452`). No shipped configuration
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
- **A GUI question**: essentially no automated coverage — build it, run it, and look. The exception
  is `CCMetaData.cpp` (`test/qt/ccmetadata_test.cpp`, run by `test/run-qt-tests.ps1`); if what you
  are touching is Qt-linked but not a widget, that runner is where a test can go.
- **Driving the GUI for a playtest**: press-and-hold opens a menu but releasing on an item does not
  pick it, and arrow keys inside an open menu do nothing. What works is clicking the menu title
  (down+up in place), then a **second separate click** on the item, locating both through UIA
  `FromHandle` → `FindFirst(Descendants, NameProperty)` — the item lives under the desktop root, not
  under the window. The title bar carries the level name, which makes `MainWindowTitle` a real oracle
  for which level is current, but read it after `$proc.Refresh()`, and note it reports a bare
  `Tile World` while a menu popup is open.
