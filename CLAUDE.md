powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1           # NO_FIX_* differential matrix
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1 -Search   # REDISCOVER witnesses (slow, deliberate)
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
powershell -ExecutionPolicy Bypass -File coverage.ps1                 # gcov, unit layer
powershell -ExecutionPolicy Bypass -File test\run-golden.ps1          # golden-master engine snapshot
powershell -ExecutionPolicy Bypass -File test\run-golden.ps1 -Update  # REWRITE the baseline (deliberate)
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1           # NO_FIX_* differential matrix
powershell -ExecutionPolicy Bypass -File test\run-nofix.ps1 -Search   # REDISCOVER witnesses (slow, deliberate)
```

Two more, neither of which is PowerShell:

```bash
test/run-corpus.ps1 ...          # replay differential over the whole collection -- see its header
test/run-sanitizers.sh           # ASan+UBSan over the unit tests. LINUX ONLY; the CI job runs it
test/run-fuzz.sh                 # libFuzzer over the .tws/.dat parsers. LINUX ONLY (needs clang)
FUZZ_SECONDS=0 test/run-fuzz.sh  # just replay the committed corpus, no fuzzing
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
run-tests.ps1              entry point: unit, then end-to-end
  test\run-tests.ps1       unit — compiles the source under test directly; needs only gcc
  test\run-e2e.ps1         end-to-end — drives the real executable's GUI-free command line
```

Current state: **13 unit runs, 17,373 checks; 12 end-to-end cases, 35 checks; 2 Qt runs, 117 checks;
1,806 golden-master digests; 13 NO_FIX_* witnesses; 0 failures.**

A third layer runs on Windows but not from `run-tests.ps1`, because it needs no test harness at all
— it links the engines the way `tworld2` does and drives real level data:

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
  **13 have such a witness**, committed in `test/nofix/nofix-matrix.tsv` and replayed on every push;
  the check asserts both digests are unchanged *and that the two still differ*.

  🔴 **It is the only check on the desync machinery.** Those toggles are opt-out macros, so a broken
  one changes no shipped behavior and nothing goes red — two of them had already rotted to the point
  of not compiling. `-Search` rediscovers witnesses and takes about half an hour; the check is
  seconds. See [`docs/adr/0012`](docs/adr/0012-engine-toggles-need-a-differential-witness.md).

Two more layers do not run from `run-tests.ps1`, because neither can run on Windows:

- **`test/run-sanitizers.sh`** — the unit suite rebuilt under ASan+UBSan (the `sanitizers` job). It
  found jc-46 on its first run. See §8.
- **`test/run-fuzz.sh`** — libFuzzer, **six targets**, 60 s each per push (the `fuzz` job). Four
  parsers: `expandsolution()`, `readleveldata()`, `expandleveldata()`, `readconfigfile()`. And
  **both engines**: `fuzz_mslogic.c` and `fuzz_lxlogic.c` load a level *and play it*. It found
  jc-47 on its first run — a 64-byte leak in `prepareplayback()`.

  🔴 **The engine targets cover a different class, and it is the one jc-45 was.** A parser target
  proves a bad file is *refused*; jc-45 was a file that was **accepted** and then dereferenced out
  of bounds inside `initgame()`. Their input is split — a move-count byte, a move stream, then the
  raw level record — so a reproducer encodes both the level and the play.

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
- 🔴 **19 of the 32 `NO_FIX_*` toggles have no differential witness — down from 30, and every number
  here is measured.** `test/nofix/` searches for an input whose result differs between a fix-on and a
  fix-off build; such an input is a **witness** that the fix is live and reachable, and the 13 found
  so far are committed in `test/nofix/nofix-matrix.tsv` and re-checked by CI on every push. See
  [`docs/adr/0012`](docs/adr/0012-engine-toggles-need-a-differential-witness.md).

  **What moved the number, and what did not.** The golden master over all 903 real levels finds
  **2**. Raising its tick count 400 → 2000 and its walk count 1 → 4 → 12 each found **nothing
  further** — the limit is the shape of the input, not its quantity, because a random walk through a
  real level never *constructs* a block on a teleport or a tank on a cloner. Generating 9×9 rooms
  packed with that furniture found **13**. The single biggest step was **deliberately stacking**
  cells — a creature or block placed on top of machinery — which is what nearly every one of these
  fixes is actually about; before that the same search found **1**.

  ⚠ **A blank row is a statement about the SEARCH, not about the fix.** All 32 were separately
  confirmed to change the preprocessed source, so none is dead code, and a blank is never grounds for
  deleting a fix. `nofix -stats` reports what arrangements the generator is producing, which is how
  to tell "never built it" from "built it and nothing changed" before drawing any conclusion.

  ⭐ The sweep paid for itself twice over. **Two toggles turned out not to compile at all**
  (`NO_FIX_RFF_DRAW_ONCE`, `NO_FIX_TELEPORT_STALE_FG`) — see §8; all 32 build now. And two *pairs*
  share a witness seed exactly (`RFF_DRAW_ONCE`/`RFF_CHIP_REARM` at 1109,
  `TELEPORT_STALE_FG`/`TELEPORT_BROKEN_DYNAMIC` at 3624), which is a real signal rather than a
  coincidence: each pair is the pair whose declarations were tangled together, and they touch the
  same path.
- **No WIDGET is tested**, still — the score table's column spans, the color picker, the tileset
  menu, the death counter are all verified by hand, because each needs a `QApplication` and a paint
  device and asserting on painted pixels is a much weaker test than it looks. **2 of `oshw-qt/`'s 8
  files are covered**, and both were picked on the same principle: cover the ones that are *not*
  widgets.
  - `test/qt/ccmetadata_test.cpp` — `CCMetaData.cpp`, the `.ccx` parser, 90 checks. The only parser
    in the tree **no other layer can reach**: `readextensions()` returns immediately when
    `g_pMainWnd` is null, so batch mode, the e2e cases and every fuzz target skip it by construction.
  - `test/qt/textcoder_test.cpp` — `TWTextCoder.cpp`, the CC1↔Unicode codec every level name,
    password and hint passes through, 27 checks. ⚠ It **pins a defect rather than asserting
    correctness** in one case: `encode()` is shifted one byte below `decode()` for eleven characters.
    See §8.
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

| File | Lines | Branches |
|---|---|---|
| `random.c` | 100.0% | **100.0%** |
| `generic/dirinput.c` | 100.0% | **97.8%** |
| `unslist.c` | 90.7% | **84.8%** |
| `encoding.c` | 89.9% | **82.8%** |
| `generic/in.c` | 55.7% | **50.9%** |
| `lxlogic.c` | 55.4% | **46.3%** |
| `solution.c` | 47.7% | **30.1%** |
| `mslogic.c` | 44.8% | **33.3%** |
| `fileio.c` | 40.1% | **31.3%** |
| `play.c` | 26.6% | **33.8%** |
| `series.c` | 19.3% | **24.4%** |
| **overall** | 46.7% | **39.8%** |

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

**Fixed in jc-46**: signed-shift overflow assembling a `.tws`'s 32-bit fields. `solutiondata[N]` is
an `unsigned char`, which promotes to a **signed** `int`, so `<< 24` on a byte `>= 0x80` overflowed
the sign bit — in `expandsolution()`'s seed and `readsolution()`'s best time. Upstream's. Seeds are
random, so it fired on about **half of every solution file ever recorded**; no replay was ever
affected because both consumers mask the damaged bits off. Replay-neutral: 289 sets, 0 of 303
outputs changed. See `FORK.md` item 18.

🔴 **jc-46 is the first defect here found by a TOOL rather than by a person**, on the sanitizer
job's first run, in a line nobody had reason to suspect — through a test that had been green for
weeks and had no way to report what it was already exercising. Prefer running a tool over reading
another parser by hand.

⚠ **And "sanitizers cannot run on Windows" is only half true.** `-fsanitize=address` cannot —
mingw-w64 ships no `libasan`. But **`-fsanitize=undefined -fsanitize-undefined-trap-on-error` needs
no runtime library** and works here now; each check becomes `__builtin_trap()`, so you get `SIGILL`
instead of a report, which is all a gate or a mutation check needs. It requires `-O1`, which turns on
`-Wformat-truncation` in `tw_test.h`, so add `-w` for a one-off run.

**Fixed in jc-47**: a 64-byte leak on every failed playback. `prepareplayback()`'s `solutioninfo` is
a **stack local**, and `expandsolution()` has already called `initmovelist()` by the time it can
fail — so both early exits dropped the allocation. Reachable from any malformed, truncated or
empty solution record. Upstream's. Replay-neutral: 289 sets, 0 of 303 outputs changed.

⭐ **Found by LeakSanitizer on the fuzz job's first run.** With jc-46, that is two consecutive
releases where a *tool* found a defect nobody had gone looking for. The lesson has now been paid for
twice: **run the instrument before reading another parser by hand.**

**Fixed in jc-48**, both upstream's, and **both found by writing the first unit test the `.dac`
parser ever had** — not by a fuzzer or a sanitizer:

1. **A `.dac` could name a file outside the data directory.** `readconfigfile()` asked
   `haspathname()` to reject a path; that tests only `DIRSEP_CHAR` (a **backslash** on Windows) and
   additionally `stat()`s the name, so it answers "is there an existing file behind a path".
   `openfileindir()` then *joins* the name onto the data directory and `../../../x.dat` resolves out
   of it. Now tested for both separators at the call site — **and for Windows device names**, which
   need no separator at all: `CON`/`NUL`/`COM1`/`LPT1` resolve to the device from inside any
   directory. This fork already guarded *tilesets* against that in jc-42 and left level sets open, so
   the check moved from `res.c` to `fileio.c` as `isreservedfilename()` and both callers share it.
   0 of 598 real `.dac` files are affected by either rule.
2. **Twenty-two ctype casts on a signed `char`** — `isspace`/`tolower`/`isalpha` are defined only
   for `unsigned char` values or `EOF`, and every byte `>= 0x80` arrived negative. `series.c` 6,
   `solution.c` 6, `tworld.c` 5, `res.c` 3, `unslist.c` 1, `fileio.c` 1. `tworld.c` also needed a
   range check: `Cmd` runs past 255. ⚠ **Nothing observable was broken** — all 256 byte values give
   identical answers signed or unsigned on this toolchain, so **no test can distinguish the two
   states**, and the high-bit cases in `series_test.c` are a crash net, not a regression net.

⚠ Three ctype instances remain in `oshw-sdl` (`sdlout.c:812`, `sdltext.c:110`, `:336`), deliberately
left: **those three files** are not compiled here, so the change could not be built or tested. Note
the precise claim — `oshw-qt/CMakeLists.txt` *does* compile `oshw-sdl/sdlsfx.c`, which simply has no
ctype calls. **Do not "finish the sweep" without building what you touch.**

🔴 **The lesson of jc-48 is the cheapest one in this file.** The `.dac` parser was the only one in the
C core with no test, and it was the one with the bugs. Before reaching for a fuzzer, check what has
no coverage at all. That lesson was applied straight away: `oshw-qt/CCMetaData.cpp`, the `.ccx`
parser, was the next thing with none, and it now has `test/qt/ccmetadata_test.cpp`. It found no
defect — the level index really is bounds-checked and Qt does the parsing — and **"we looked and it
was fine" is a result worth having**, because until it existed nobody could say so.

**Fixed in jc-50 — and this one is THIS FORK'S, not upstream's.** `movelaws[]` has exactly 64
entries, one per *terrain* id, but a cell's **bottom layer can hold a creature**, and creature ids
start at `Chip == 64`. So `movelaws[cellat(to)->bot.id]` read up to 47 entries past the array and
used whatever followed it in `.rodata` as a movement rule. Not exotic: **5,743 of 31,090 real levels
(18%) do this, including CC1 and every CCLP.** All three sites came in with the desync work
(`FIX_KEEPSLOT_OCCUPANT`, jc-17 era) and now go through `movelaw_block()`/`movelaw_creature()`.
Replay-neutral: 0 of 303 outputs changed. See `FORK.md` item 21.

⭐ **Found by the MS-engine fuzz target one second into its first run** — the third consecutive find
by tooling nobody aimed at a line (jc-46 UBSan, jc-47 LSan, jc-50 the engine fuzzer), and the first
from fuzzing an **engine** rather than a parser. A parser target structurally could not reach it: it
needs a level that *loads* and a creature that *tries to move*. jc-45 was the same shape and had to
be found by hand.

⚠ **When you fix an out-of-bounds read, do not assume there is a correct old value to preserve.** The
old read here was undefined — what it returned depended on what the linker put after the array, so
replay was never guaranteed stable across toolchains for these levels. Pick the defensible answer and
**measure it against the corpus**, which is what settled this one.

**Fixed in jc-51 — the finding jc-50 shipped with OPEN.** `chipsneeded` is a **signed** `short`
(`state.h:251`) filled from the `.dat`'s **unsigned** 16-bit word (`encoding.c:187`), so a level
demanding `0x8000` chips or more arrives **negative**. `canmakemove()` gated the socket on
`chipsneeded() > 0` — false for a negative count, **so the socket opened** — and `endmovement()` then
asserted `chipsneeded() == 0`, which is also false, so `die()` ran and **the shipped game exited**.
Both gates now ask `chipsneeded() != 0`. **Upstream's** (`929d9c6`). Both engines. No real level
reaches it: 31,090 levels across 393 `.dat` files, zero asking 32,768 or more. Replay-neutral: 0 of
303 outputs differ. See `FORK.md` item 22.

🔴 **The suspicion recorded here was wrong, and it pointed at the wrong file.** This entry used to
say "something reaches `endmovement()` with a socket destination without passing that gate — a slide
or teleport path is the suspect." **Nothing bypasses the gate; the gate itself says yes.** An assert
tells you which invariant broke. Only tracing the reproducer tells you why — and when a value can be
negative, check the *type* before you go looking for an exotic control-flow path.

⚠ **The type mismatch is still there, deliberately.** `chipsneeded` remains a signed `short` holding
an unsigned file value. Widening it touches a struct every engine path reads; rejecting the file in
`expandleveldata()` would refuse input upstream accepts. Making the two predicates agree is the
minimal fix, and the only one that is provably behavior-preserving — **for every non-negative count,
`> 0` and `!= 0` are the same predicate.**

**Fixed, unreleased — two `NO_FIX_*` toggles that could not be switched on at all.** This fork's own.
`rff_keepdir` was declared under `#ifdef FIX_RFF_DRAW_ONCE` but also written by the
`FIX_RFF_CHIP_REARM` block; `prepush_destfloor` was declared under `NO_FIX_TELEPORT_STALE_FG` but
also read under `FIX_TELEPORT_BROKEN_DYNAMIC`. So `-DNO_FIX_RFF_DRAW_ONCE` and
`-DNO_FIX_TELEPORT_STALE_FG` each dropped a declaration while leaving a use standing, and
`mslogic.c` **did not compile**. Each declaration is now guarded by *either* toggle. Shipped
behavior is unchanged — at the defaults the declaration is present either way, and all 1,806 golden
digests are identical across the change. **All 32 toggles build now.**

🔴 **This is the rot [ADR 0002](docs/adr/0002-engine-fixes-are-opt-out-macros.md) exists to prevent,
and it had already set in.** The toggles are kept precisely so a future desync investigation can
flip one. Two of them were unflippable, and **nothing in the repository would have said so** — not
the unit suite, not the fuzzers, not CI — until somebody reached for one of those switches
mid-investigation, years from now, and lost an afternoon to a compile error in code they had not
touched. Found only because the golden-master work built all 32 one at a time.

⚠ **The general lesson: `#ifdef` scaffolding is untested code, and untested code rots.** If you add
a `NO_FIX_*`, build with it defined at least once before committing. It costs one command.

What follows is what is still live.

🔴 **OPEN, and pinned by a test rather than fixed: `TWTextCoder::encode()` is shifted one byte.**
`decode()` maps `0x81` to a space (an undefined slot, as CP1252 does) and `0x82..0x8C` to
U+20A1, U+0192, U+201E … U+0152. `encode()` was written against a table with **no gap at 0x81** and
packs those same eleven characters from `0x81` upward — so each encodes to the byte *below* the one
it decoded from. Measured: **244 of 255 byte values round-trip; 11 do not.** `decode` is the correct
side (it agrees with CP1252 across `0x83..0x8C`). Upstream's (`929d9c6`).

⚠ **Impact is close to nil, which is why it survived.** `encode()` has three callers
(`TWMainWnd.cpp` 1291, 1754, 2164) and the only one carrying user text is the input prompt, where
the text is a Chip's Challenge password — four characters, A–Z. Nobody types a per-mille sign into
it. There is no buffer risk either: the codec is one byte per `QChar` and the prompt truncates to
`nMaxLen` first, so `char passwd[5]` with `maxlen 4` fits exactly.

**It is characterized, not fixed, on purpose** — fixing it changes what bytes the program writes,
which is a maintainer's decision rather than a test's. `test/qt/textcoder_test.cpp` asserts the
current behavior *and* the exact shape of the defect, so a fix will show up as that case going red
with a lower count, which is the signal to update it to zero.

⚠ Separately odd and NOT part of the shift: `decode` maps `0x82` to **U+20A1** (colon sign) where
CP1252 has **U+201A** (low quotation mark). Those two code points are a digit transposition apart.
Left alone; it is `decode`'s business and nothing observable depends on it.

### 8.1 `-v` cannot work as documented

The option string at `tworld.c:2205` is `"abD:dFfHhL:lm:n:PpqR:rS:stVv:c"` — `v:` declares that `-v`
takes an argument, while its handler takes none and the usage text says "Display version number and
exit". `tworld2 -v` prints "option requires an argument"; `tworld2 -v x` prints `2.3.1`. One
character. `test/run-e2e.ps1` pins the current behavior deliberately, so fixing it turns that case
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
