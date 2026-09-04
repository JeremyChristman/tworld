# 0011 — A fuzz finding is not fixed until its input is committed and replayed

**Status:** Accepted (2026-09-04) · **Applies to:** `test/run-fuzz.sh`, `test/fuzz/`, `test/tw_corpus.h`, the `fuzz` job in `.github/workflows/ci.yml`

## Context

Every defect this fork has shipped a fix for lives in one surface:

| Release | Defect | Where |
|---|---|---|
| jc-44 | a `.tws` could smash a 256-byte stack buffer | `solution.c` |
| jc-44 | a pointer advanced by a file-supplied size, then dereferenced | `series.c` |
| jc-44 | an RLE guard reserving two fewer bytes than its neighbor | `encoding.c` |
| jc-45 | an unguarded map index from a `.dat`'s trap wiring | `mslogic.c` |
| jc-46 | signed-shift overflow reading the `.tws` seed | `solution.c` |
| jc-46 | the same overflow reading the recorded best time | `solution.c` |

Six defects, four releases, one root cause: **this program's main job is parsing files that strangers
made.** Level packs and solution collections are downloaded and traded, and dragging a `.tws` onto
the executable is a documented workflow.

Five of the six were found the same way — a person read a parser, suspected a specific line, and
hand-built a test that could observe that one thing. That method works, and it does not scale: it
finds what somebody already suspected. The sixth was different. UndefinedBehaviorSanitizer found it
on the **first run** of the `sanitizers` job, in a line nobody had any reason to look at, through an
ordinary test that had been green for weeks and had no way to report what it was already exercising.

Fuzzing is the generalization of that: stop guessing which line is wrong and generate inputs until
the program objects.

## Decision

Three libFuzzer targets over the parsers — `expandsolution()`, `readleveldata()`, and
`expandleveldata()` — built with ASan+UBSan, run for 60 seconds each on every push.

**And, the part that is actually load-bearing:** every input that has ever mattered — each seed, and
every reproducer the fuzzer has produced — is **committed** under `test/fuzz/corpus/<target>/`, and
**replayed by the ordinary unit suite on every platform**, with no clang and no sanitizer, through
`test/tw_corpus.h`.

A finding is not considered fixed until its input is in that corpus with a case replaying it.

## Why the corpus, and not just the fuzzer

libFuzzer generates fresh inputs every run. A green run therefore proves nothing durable, and a crash
found today can vanish tomorrow because the generator went somewhere else. A fuzzer is a **discovery**
tool and it is not a regression suite. Treating it as one is the common failure: the job goes green,
nobody notices it stopped covering the interesting case, and the same bug ships again.

Committing the input converts a probabilistic finding into a deterministic test that runs on the
maintainer's Windows machine, in the Windows CI job, and in the sanitizer job — everywhere, forever,
in under a second.

**The guard bytes make the replay a real oracle without a sanitizer.** Each input is copied into the
middle of an allocation with 64 poison bytes on each side, and those are checked after the parser
returns. Without that, "the parser returned" asserts nothing — a parser that ran off the end and came
back is indistinguishable from one that did not. With it, every over-**write** within 64 bytes of
either end is caught on a plain mingw build. That is precisely the shape of jc-44's stack smash.

## Consequences

- The `fuzz` job is Linux-only, for three reasons rather than one: mingw-w64 ships no `libasan`, no
  libFuzzer, and `fuzz_leveldata.c` uses `fmemopen()` to avoid a disk write per execution.
- `expandleveldata()` is fuzzed **ungated**, separately from `readleveldata()`. That is deliberate:
  it normally runs only on records the password gate already accepted, and jc-44's third defect was
  a guard in `encoding.c` that was safe only because of a check in `series.c`. Fuzzing only through
  the gate could never have reached it. Findings there are triaged accordingly — they may not be
  reachable from a `.dat` today, and still matter for the built-in level and any future caller.
- New coverage-increasing inputs go to a **scratch** directory, not the committed corpus. libFuzzer
  writes into the first corpus directory it is given; passing the committed one would mean an
  ordinary local run silently added dozens of uncurated files and left `git status` dirty. Growing
  the corpus is a deliberate act.
- ⚠ **60 seconds per target is a regression check, not a soak.** It catches shallow breakage on every
  push. A real campaign is `FUZZ_SECONDS=600` by hand. No scheduled soak job exists, and pretending
  a one-minute run is a security audit would be theater.
- ⚠ **The engines are not fuzzed, only the parsers.** A crash reachable from a malformed level that
  survives `readleveldata()` would not be found here. `lxlogic.c` has no coverage of any kind.
- ⚠ **Nothing here analyzes the Windows build, which is the one that ships.** The portable core is
  identical and that is where all six defects were — but `#ifdef WIN32` branches are only ever seen
  in their POSIX form. Stated in `SECURITY.md` rather than left implied.

## Alternatives considered

**Fuzz through the public entry points only (open a real file).** Rejected: it would be a disk
benchmark rather than a fuzzer at tens of thousands of executions per second, every input would share
one filename so reproducers would race, and it could not reach `expandleveldata()` ungated.

**Keep reproducers in `findings/` and leave it there.** Rejected — that directory is gitignored
scratch. It is where libFuzzer drops artifacts, not where regressions live. Anything worth keeping
gets copied into the corpus and given a case; anything not worth keeping gets deleted.

**Skip the unit-suite replay and rely on the CI job.** Rejected, and this is the decision the ADR
exists to record. It would make every finding depend on a Linux runner and a random generator to stay
fixed, and it would give the maintainer no way to reproduce one on the machine where the work happens.
