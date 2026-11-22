# Reproducers for findings that are NOT yet fixed

**There are none right now.** The directory is kept, empty of findings, because the workflow it
exists for is the one that turns a fuzz hit into a permanent regression case.

🔴 **This directory is deliberately not read by anything.** `test/tw_corpus.h` replays
`test/fuzz/corpus/`, and `test/run-fuzz.sh` seeds from it — neither looks here.

That is the point. A reproducer for an **unfixed** defect cannot go in the corpus: the unit
suite would run it, the engine would die inside it, and the whole suite would go red for a
defect nobody has decided how to fix yet. Deleting it instead would throw away an input that
took real fuzzing time to find.

So it lives here, committed, until the defect is fixed — at which point it **moves** into
`test/fuzz/corpus/<target>/` and becomes a permanent regression case, per
[`docs/adr/0011`](../../../docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md).

⚠ **An entry here means the `fuzz` job is expected to be RED**, and that is the honest state:
the finding is real and unfixed. Do not silence it. Equally, **an empty directory is a claim** —
it says every reproducer this project has found is now in the corpus, replayed on every run.

## What has passed through here

* **`mslogic-socket-assert`** — held here through jc-50, **fixed in jc-51**, and moved to
  `test/fuzz/corpus/mslogic/socket-negative-chipsneeded`.

  `_assert(chipsneeded() == 0)` in `endmovement()`'s `Socket` case could fail, so `die()` ran and
  a debug build exited with "internal error: failed sanity check" (the shipped Release build
  compiles `_assert` away; this note said otherwise until jc-63). Found by `fuzz_mslogic`
  ~43 s in. The note kept here at the time guessed that "something reaches `endmovement()` with a
  socket destination **without** going through that gate — a slide or teleport path is the obvious
  suspect". **That guess was wrong**, and it is worth leaving on the record: nothing bypassed the
  gate. `chipsneeded` is a signed `short` fed an unsigned 16-bit word from the `.dat`, so a level
  demanding ≥ 32,768 chips made the count **negative** — and `> 0` is false for a negative number,
  so the gate itself opened the socket. Both gates now ask `!= 0`. See `FORK.md` item 22.

  The lesson for the next entry in this directory: **an assert names the invariant that broke, not
  the reason it broke.** Trace the reproducer before writing down a suspect.

* **`mslogic` red button / Swimming Chip** — found by the weekly soak **three times**: runs
  35501929929 (2026-09-20), 43711434868 (2026-11-15) and 45023261090 (2026-11-22). It was **never
  parked here**: the third run was traced and fixed the same day, in jc-63, and its input went
  straight to `test/fuzz/corpus/mslogic/redbutton-swimming-chip-template`. 🔴 **The first two red
  runs were not acted on**, so for nine weeks this directory's "none right now" was false — those
  reproducers existed only as CI artifacts (both replay the same defect, checked by hand on
  2026-11-22). A red button wired to a cell holding a Swimming Chip woke it as a monster, and its
  first move hit `_assert(!"Unknown creature trying to move")`. This time the trace was the first
  thing done, and the warning printed just before the assert (`Non-creature 6C`) led straight to
  the guard: the clone path excluded plain Chip and nothing else. See `FORK.md` item 53.
