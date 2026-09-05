# 0012 — An engine toggle is not tested until an input tells the two builds apart

**Status:** Accepted (2026-09-05) · **Applies to:** `test/nofix/`, `test/run-nofix.ps1`, `test/run-nofix.sh`, the `nofix` job in `.github/workflows/ci.yml`, and every `NO_FIX_*` macro in `mslogic.c`

## Context

[ADR 0002](0002-engine-fixes-are-opt-out-macros.md) established that every engine fix from the
desync project is an **opt-out macro**: the fix is on by default, and `-DNO_FIX_x` puts the old
behavior back so a future investigation can measure against it. There are 32 of them in `mslogic.c`.
They are the machinery this fork was built with.

ADR 0002 named the risk it was accepting, and named it correctly:

> Deleting one changes no shipped behavior, so nothing fails — until the next desync investigation
> needs the switch that is gone.

That is exactly what happened, and worse than "deleted". Nothing ever exercised the toggles, so:

* **Two of the 32 did not compile at all.** `NO_FIX_RFF_DRAW_ONCE` and `NO_FIX_TELEPORT_STALE_FG`
  each declared a state variable under one toggle and read it under another, so switching either on
  produced `'rff_keepdir' undeclared`. Found by building all 32 one at a time — which nobody had
  ever done — and fixed in the same pass.
* **The rest were unmeasured.** `CLAUDE.md` had said so for a long time: *"each is a documented
  behavior difference with a known direction, and compiling one test both ways would be a real
  oracle. Nobody has done it."*

The obvious oracle — run real levels and see whether the toggle changes anything — was built first
(`test/golden/golden.c`, all 903 committed levels through both engines) and **measured at 2 of 32.**
Neither a longer walk (400 → 2000 ticks) nor more walks (1 → 12) found a third. The reason is not
effort: a real level puts Chip a long way from the interesting furniture, and these fixes are about
arrangements — a block resting on a teleport, a tank standing on a clone machine, a creature in a
beartrap whose button is pressed this tick. A random walk does not *construct* those.

## Decision

**A `NO_FIX_*` toggle is considered tested when, and only when, a committed input produces a
different result under a build with the fix on and a build with the fix off.** That input is called a
**witness**, and it is recorded in `test/nofix/nofix-matrix.tsv` with both digests.

Witnesses are **found by search, not written by hand.** `test/nofix/nofix.c` generates a 9×9 room
packed with exactly the furniture the toggles concern — including deliberately *stacked* cells, a
creature or block on top of machinery — plays a short deterministic game, and hashes the result. The
search runs the same generated input through both builds and keeps the first seed where they differ.

The CI `nofix` job replays every recorded witness and asserts three things: the fix-on digest is
unchanged, the fix-off digest is unchanged, and **the two still differ.**

## Why search rather than 32 hand-written levels

Hand-authoring one fixture per toggle was the obvious alternative and was rejected for three reasons:

1. **It encodes the author's belief about what the fix does.** A fixture written from a `MOD` comment
   tests the comment. The search tests the code, and it found witnesses in places nobody would have
   thought to construct — the witness for `NO_FIX_KEEPSLOT_OCCUPANT` turned up at seed 132,433, well
   past where any earlier probe had looked.
2. **32 hand-written levels is 32 chances to be subtly wrong** in a way that makes a case vacuous,
   and a vacuous case here is invisible: it passes.
3. **The generator keeps working for toggle 33.** A future desync fix gets a witness by running the
   search, not by writing another level.

The cost is honest and worth stating: a searched witness proves *reachability*, not *correctness*,
and it may distinguish the two builds through a downstream ripple rather than the mechanism the fix
was written for. That is acceptable because correctness is not this instrument's job — every one of
these fixes was justified against SuperCC over a real solution corpus, and that record is in
`FORK.md`.

## Consequences

**A blank row is a statement about the search, never about the fix.** It records that the generator
did not build the arrangement that toggle needs. It is explicitly **not** grounds for deleting a fix,
and the matrix file, the tool's header and both runners all say so. `nofix -stats` exists precisely
so that "the search never built it" can be told apart from "the search built it and nothing changed".

**Re-searching is deliberate.** `-Search` keeps a witness that still works and only replaces one that
has gone stale, so the committed file stays stable and its diffs stay readable.

**The check is cheap; the search is not.** Replaying the recorded witnesses costs one build per
witness and runs in CI on every push. Rediscovering them sweeps a million seeds per toggle and takes
roughly half an hour across all 32 — a maintainer action, not a CI one.

**This does not replace the corpus differential.** `test/run-corpus.ps1`, over the maintainer's real
solution collection, remains the instrument that decides whether a release ships. The matrix answers
a different and narrower question: *are the switches still switches?*

## Alternatives considered

**Delete the unmeasurable toggles.** Rejected outright. The whole point of ADR 0002 is that their
value is realized years later, and "no witness" is a limitation of the search, not evidence the fix
is inert. Every one of the 32 was separately confirmed to change the preprocessed source, so none is
dead code.

**Gate CI on full coverage of all 32.** Rejected: it would make the build red for a reason nobody can
act on, which is how a red X gets ignored. The blank rows are reported on every run instead.

**Reuse the golden master with a bigger budget.** Measured and rejected — see Context. Depth and
breadth both bought exactly nothing, because the limit is the shape of the input, not its quantity.
