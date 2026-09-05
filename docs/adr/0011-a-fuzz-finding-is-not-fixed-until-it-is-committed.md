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

Four libFuzzer targets over the parsers — `expandsolution()` (.tws), `readleveldata()` and
`expandleveldata()` (.dat), and `readconfigfile()` (.dac) — built with ASan+UBSan, run for 60 seconds each on every push.

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

**What the replay proves, stated exactly.** Two things, and they are narrower than they sound:

1. Every committed input still parses to completion without crashing, hanging or aborting. That is a
   real regression check and it is most of the value — a reproducer that used to segfault does not.
2. The parser did not **modify** its own input. Every target today is a read-only walker, so this
   passes trivially; it exists to fail the day one starts decoding in place, which would break every
   caller handing it a shared buffer.

It is **not** a memory oracle. An over-read or over-write past the allocation is invisible to plain
C. ASan — in `run-sanitizers.sh` and the `fuzz` job — is the memory oracle, and the replay's buffer
is sized **exactly** to the input so ASan's redzone starts at the first byte past the end.

### ⚠ The first version of this decision was wrong, and the reason is worth keeping

It put 64 poison bytes on each side of the input and claimed that caught over-writes without a
sanitizer. Review measured it, and the claim was empty twice over:

- **None of the parsers writes to its input at all** — `expandsolution()` and
  `expandmsdatlevel()` both walk it through `unsigned char const *`, and `readleveldata()` never
  touches the buffer, reading into its own allocation instead. Whole-block `memcmp` before and after,
  on all twenty inputs: not one byte changed anywhere. The fences could never fire.
- **Worse, they actively blunted ASan.** Those 64 bytes were legally allocated, so in the sanitizer
  job they sat exactly where the redzone belongs. jc-44's two-byte over-read would have landed in the
  fence and gone unreported. The fuzz targets had the exact-size `malloc` right and said why; the
  replay did the opposite while claiming the same benefit.

A mutation test settled it: re-introducing jc-44's missing lower-layer bound made the hand-written
`encoding_test` case fail while the corpus replay stayed green — because every committed input is a
well-formed level, and **a corpus of valid files cannot test rejection.** The hand-written behavioral
cases are not redundant with the corpus and must not be replaced by it.

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
