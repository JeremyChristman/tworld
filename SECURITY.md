# Security policy

## What this program is, in security terms

Tile World is a single-player desktop game. It listens on no network port, runs with no elevated
privilege, has no plugin system, and stores nothing but level data and your own solutions.

**Its entire meaningful attack surface is the files it parses** — and it parses files that other
people made. That is not incidental to how it is used; it is the normal case. Players routinely
download third-party level sets (`.dat`), set configurations (`.dac`), and solution collections
(`.tws`) from community sites and drop them into the game's folders. None of that content is signed
or reviewed. The parsers live in:

| File | Reads |
|---|---|
| `encoding.c` | `.dat` level records — map layers, trap and cloner wiring, the creature list |
| `series.c` | `.dat` level headers and the `.dac` text configuration |
| `solution.c` | `.tws` solution files, including a five-format variable-length move codec |
| `oshw-qt/CCMetaData.cpp` | `.ccx` level metadata — XML shipped inside a level pack |
| `unslist.c` | the bundled unsolvable-level list — but see below |
| `settings.cpp` | `tw_settings.ini` |

⚠ **`unslist.c` is not actually reached in the shipped configuration.** Its loader runs only if the
`unsolvablelist` resource names a file, and that is set neither in `res/rc` nor in the compiled-in
defaults — so `res/unslist.txt` ships and is never read. Listed because the code is there and a
distributor could enable it, not because a stock build parses it.

⚠ **`.ccx` is parsed only when a main window exists.** `readextensions()` returns immediately in
batch mode, so a headless run never touches one.

Realistic worst case is memory corruption in the user's own session — a crash, or in principle code
execution as that user. There is no privilege boundary to cross and nothing to escalate to. Please
calibrate reports accordingly; that is a real bug worth fixing, and it is not a remote exploit.

## Reporting

**Open a [private security advisory](https://github.com/JeremyChristman/tworld/security/advisories/new).**
That is preferred over a public issue for anything memory-safety related, so a fix can ship before
the details are public. If advisories are unavailable to you, open a normal issue describing the
*class* of problem without a working input file, and say you have details to share privately.

Please include:

- the **build tag** (`jc-N`) from `Help > About`, or the release you downloaded;
- what kind of file triggers it, and the **structural property** that matters — "a level whose lower
  map layer declares a size that runs exactly to the end of the record and ends in `0xFF`" is far
  more useful than an attached binary;
- whether it reproduces in **upstream Tile World 2.3.1**. Most of these parsers are upstream's, and a
  defect that is also upstream's should reach them too. This fork's own changes are marked
  `MOD (Jeremy)` and listed in `FORK.md`.

**Please do not attach a `.dat` or `.tws` from a real level set.** Level sets are third-party content
that this project does not redistribute. A description of the byte pattern is what a fix needs.

There is no bounty, and no formal response-time commitment — this is one person's hobby fork. Expect
a reply in days rather than hours.

## Scope

**In scope**

- Memory-safety defects in any of the parsers above, reachable from a file a user could plausibly
  download: out-of-bounds read or write, integer overflow in a length computation, unterminated
  string, `memcpy` with an attacker-influenced length.
- Anything that **reads or writes outside the directories the program was pointed at** — path
  traversal through a `.dac`'s `file=` directive or a solution filename, or a name that resolves to
  a Windows device (`CON`, `LPT1`) rather than a file.

  ⚠ Both halves of that were true of the `.dac` path until jc-48, and the honest description of the
  read half is an arbitrary-file **open**, read-only: the bytes are parsed as level data, almost
  always rejected, and there is no channel back to whoever wrote the `.dac`. This line previously
  said only "writes", which promised more than the defect could do — the sort of overstatement that
  costs credibility with a reporter who checks.
- Anything that makes the program destroy a user's saved solutions.

**Out of scope**

- Crashes on files that are simply corrupt, where the program reports the problem and exits. Refusing
  bad input is the intended behavior.
- The absence of a sandbox. This is a game, not a document viewer.
- Vulnerabilities in Qt, SDL2, zlib or zstd themselves — report those upstream. Note that the shipped
  build links Qt statically, so a Qt fix reaches users only through a new Tile World release; see
  `.github/dependabot.yml` for why nothing here watches those versions automatically.
- Issues that require the attacker to already be able to run code as the user.

## Known issues

Defects that are already recorded rather than hidden are listed in **`CLAUDE.md` section 8**, with
their origin (upstream or this fork) and why each is or is not yet fixed. Please check there before
reporting.

## Supported versions

Only the **latest `jc-N` release**. This is a fork maintained by one person; there is no branch to
backport to. Fixes ship in the next tagged build.

## What is done to reduce the risk

- The test suite includes malformed-input cases for the `.tws` codec — truncation at every length of
  every variable-length encoding — and the level-record builder used by the engine tests is written
  from the format specification rather than from the parser, so a misreading in the parser is not
  reproduced in the fixtures.
- **AddressSanitizer and UndefinedBehaviorSanitizer** run over the whole unit suite on every push
  (the `sanitizers` job). This is not decoration: it found undefined behavior in the `.tws` seed and
  best-time reads (jc-46) **on its first run**, in a line nobody had reason to suspect, through a
  test that had been passing for weeks.
- **Every untrusted-input parser is fuzzed on every push** (the `fuzz` job, `test/run-fuzz.sh`) —
  libFuzzer with ASan+UBSan over `expandsolution()` (`.tws`), `readleveldata()` and
  `expandleveldata()` (`.dat`), and `readconfigfile()` (`.dac`). `expandleveldata()` is deliberately
  ungated, because its safety otherwise depends on a check in a different file.
- **The `.ccx` metadata parser is covered too** (`test/qt/ccmetadata_test.cpp`, 90 checks). It is
  the only parser here reachable ONLY from a running GUI -- readextensions() returns early in batch
  mode -- so no corpus run, e2e case or fuzz target can touch it. No defect was found; the level
  index is bounds-checked and Qt does the parsing.
- ⚠ **The parser that had no test was the parser with the defects.** `readconfigfile()` was the last
  one with no coverage of any kind; writing its first unit test in jc-48 immediately found a path
  check that could not work on Windows and eleven `<ctype.h>` calls on a signed `char`. Coverage of
  a surface is not a substitute for tooling, and tooling is not a substitute for coverage.
- **Every input that ever mattered is committed and replayed.** `test/fuzz/corpus/` holds the seeds
  and every reproducer, and the ordinary unit suite replays all of them on both platforms
  (`test/tw_corpus.h`). A fuzzer that forgets what it found buys nothing; this is what turns a
  finding into a permanent regression test. ⚠ That replay proves the inputs still parse without
  crashing and that the parser did not modify its input — **it is not itself a memory oracle**, and
  a corpus of valid files cannot test rejection. ASan is the memory oracle; the hand-written
  malformed-input cases are what test rejection.
- CodeQL runs on every push and weekly (`.github/workflows/codeql.yml`), observing a real Linux
  build rather than buildless extraction.

### Known gaps, stated rather than implied

- ⚠ **Nothing analyzes the Windows build, which is the one that ships.** The sanitizer, fuzz and
  CodeQL jobs all run on Linux, because mingw-w64 ships no `libasan` and no libFuzzer. The portable
  core — every parser, both engines — is identical between the two, and every defect found so far
  has been there; but `#ifdef WIN32` branches are analyzed in their POSIX form only.
- ⚠ **Fuzzing is 60 seconds per target per push, not a soak.** That catches shallow regressions. A
  deep campaign (`FUZZ_SECONDS=600` or more) is a manual act, and no scheduled soak job exists.
- ✅ **Both engines are now fuzzed and unit-tested**, closing what this section used to name as its
  largest gap: an engine crash reachable from a malformed level that *survives* `readleveldata()`.
  That is jc-45's exact shape — a file the parser accepted, then dereferenced out of bounds inside
  `initgame()` — and no parser target could have found it. `fuzz_mslogic.c` and `fuzz_lxlogic.c`
  load a level and play it; `lxlogic.c` went from 0% to 49.1% line coverage.
- ⚠ **What remains uncovered in the engines is behavior, not memory safety.** The thirty-two
  `NO_FIX_*` toggles have no differential test, and `mslogic.c` sits at 38.1% lines. A change that
  is merely *different* rather than unsafe is caught by the solution-corpus differential, not by
  anything in CI.
