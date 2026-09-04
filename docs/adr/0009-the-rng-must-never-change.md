# 0009 — The random-number generator must never change, and must not match the original game

**Status:** Accepted (inherited from upstream; the second half established by this fork's reverse engineering) · **Applies to:** `random.c`

## Context

`random.c` implements a linear congruential generator: multiplier 1103515245, increment 12345,
modulus 2³¹ — the standard glibc constants. Its own header comment explains why it exists at all,
and the reason is not quality:

> It is here simply because it is necessary for the game to use the same generator FOREVER. In order
> for playback of solutions to work correctly, the game must use the same sequence of random numbers
> as when it was recorded.

Blobs, walkers and random force floors all draw from it. A solution is a list of *moves and the ticks
they happen on* — it does not record what the creatures did. Replaying it re-simulates them, and that
re-simulation only reproduces the recorded run if every draw comes out the same, in the same order.

Two things follow that are easy to get wrong in opposite directions.

**First**, "improving" the generator breaks every solution ever recorded. A better-distributed PRNG
is strictly worse here.

**Second** — and this is the one that has actually tempted people — **the generator deliberately does
NOT match the original `CHIPS.EXE`.** This fork spent jc-2 through jc-29 making the MS engine
reproduce MSCC's behavior, so "make the RNG match MSCC too" looks like the obvious next step. It was
investigated and the answer is no: MSCC uses a different generator, and **Tile World and SuperCC
already share this one byte for byte.** Changing it to match MSCC would desynchronize Tile World from
SuperCC — the exact problem the desync project existed to eliminate — and invalidate every solution
recorded by either program.

## Decision

**`random.c` is frozen.** Not the file, but its output: for a given seed and sequence of calls, the
values it produces are a compatibility contract with every `.tws` in existence.

`test/random_test.c` pins it two ways, deliberately:

- **Golden sequences**, captured from this implementation and checked back against it. These catch
  any change to the arithmetic, including one that is arguably an improvement.
- **Independent restatements** of the documented rule — the LCG constants walked from a known value,
  "one draw per permutation", "the result is a permutation", the 31-bit mask that makes 32- and
  64-bit builds agree. A golden value alone would bless whatever the code happened to do on the day
  it was captured; these say what it is supposed to do.

The test carries a note saying that a failure is not a bug in the test.

## Consequences

- **If `random_test.c` starts failing, stored solutions have stopped replaying.** Revert the change;
  do not update the numbers.
- The permutation functions (`randomp3`, `randomp4`) must keep drawing **exactly one** value each and
  deriving every swap from it. Drawing twice would shift every later draw. The test asserts the draw
  count directly, by comparing against a second generator advanced by hand.
- `resetprng()` seeds from `time(NULL)`, so anything that needs determinism must call `restartprng()`
  with a fixed seed. `mslogic_test.c` does; a test that used `resetprng()` would fail roughly one run
  in four and be dismissed as flaky.
- The separation between the **shared** sequence (`gen->shared`, driven by the file-scope
  `lastvalue`) and an **independent** one is load-bearing: jc-13 and jc-14 were both defects about
  drawing the wrong number of times from these sequences.
- This ADR is the answer to any future proposal to "fix the RNG to match Chip's Challenge". The
  answer is no, and the reason is written down here so it does not have to be rediscovered.
