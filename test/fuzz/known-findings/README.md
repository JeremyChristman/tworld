# Reproducers for findings that are NOT yet fixed

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
the finding is real and unfixed. Do not silence it.

## `mslogic-socket-assert`

`_assert(chipsneeded() == 0)` in `endmovement()`'s `Socket` case (`mslogic.c:3240`) fails, so
`die()` runs — in the shipped program that **exits the game** with "internal error: failed
sanity check".

* **Upstream's**, from the 2.3.1 import (`git log -L3240,3240:mslogic.c` → `929d9c6`).
* `canmakemove()` gates socket entry at `mslogic.c:1822`
  (`if (floor == Socket && chipsneeded() > 0) return FALSE;`), so something reaches
  `endmovement()` with a socket destination **without** going through that gate — a slide or
  teleport path is the obvious suspect, but this was not pinned down.
* Found by `fuzz_mslogic` after ~43 s, on the run immediately after jc-49 fixed the first
  engine finding.
* Reproduce: `gcc -std=gnu11 -I test/stub -g -O1 -w -o repro driver.c test/fuzz/fuzz_mslogic.c`
  and feed it this file — it aborts. No sanitizer needed.

**Why it was not fixed in the same pass:** the assert says the state is impossible; the fuzzer
proved it is not. Choosing what should happen instead — refuse the move, open the socket anyway,
or fail gracefully — is a change to MS engine *semantics* on a path every recorded solution
depends on. That wants a deliberate decision and a corpus differential, not a quick patch at the
end of a long session.
