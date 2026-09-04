# 0002 — Engine fixes ship on by default, behind `NO_FIX_*` opt-out macros

**Status:** Accepted (jc-2, 2026-07-26) · **Applies to:** `mslogic.c`, `lxlogic.c`, `encoding.c`

## Context

Between jc-2 and jc-29 this fork changed the MS engine's behavior twenty-eight times. Each change
reconciles one Tile World behavior with SuperCC so that SuperCC-made solutions replay here, and each
one was measured over the whole solution corpus and reported as *levels fixed / levels broken*.

Every one of those changes is a deliberate divergence from upstream, and any one of them could turn
out to be the wrong call on some level nobody has played yet. A behavior change buried in a 200 KB
`mslogic.c` cannot be isolated after the fact: bisecting the git history rebuilds twenty-eight other
things at the same time, and reverting one by hand months later means reconstructing what it was.

## Decision

**Each engine fix is compiled in by default and can be switched back off at build time by defining a
`NO_FIX_<NAME>` macro**, with the guarded code left in place:

```sh
cmake -S . -B build-nofix -DCMAKE_C_FLAGS=-DNO_FIX_ROW32_CLONER ...
```

The name says what turning it *off* does, so the default reads as the plain, unguarded behavior.

## Consequences

- **Any single fix can be isolated from one build tree**, which is how each one was measured: the
  same source built both ways over 269 MS sets and 20,332 solutions, with an identical stderr
  warning census, is far stronger evidence than a before-and-after of two different commits.
- `mslogic.c` carries permanent `#ifdef` scaffolding it would not otherwise need. That is the cost,
  and it is accepted: this file is the one where nearly every engine change lives, and the ability
  to answer "was it this fix?" in one build is worth the clutter.
- **Do not delete a `NO_FIX_*` guard to tidy up.** Removing one does not change shipped behavior, so
  nothing fails and nothing is noticed — until the next desync investigation needs the switch that
  is no longer there.
- Turning one off is a **debugging tool, never a release configuration**. A build with any
  `NO_FIX_*` defined must not be packaged: it would carry a `jc-N` tag describing behavior it does
  not have.
- The related `TRACE_DESYNC` macro is the mirror image — off by default, a complete no-op in normal
  builds, and turned *on* for investigation.
