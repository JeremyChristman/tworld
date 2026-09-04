# 0005 — What level data may be committed, and what may not

**Status:** Accepted (inherited from upstream; restated here 2026-09-03) · **Applies to:** `data/`, `sets/`, `.gitignore`, CI

## Context

Tile World is an emulation of the *Chip's Challenge* engines and **not** a distribution of the game.
Three different kinds of level data get confused with each other, and only one of them is a problem:

1. **The community packs.** `data/CCLP1–CCLP5.dat`, `data/CCLXP2.dat` and their `.ccx` author files
   are fan-made level sets released for free distribution, and upstream ships them. They are here
   because upstream put them here, and they are legitimately redistributable.
2. **`data/intro.dat`** — a nine-level demonstration set, also upstream's.
3. **`CHIPS.DAT`** — the original Microsoft level set. **Copyrighted, not redistributable, and not
   in this repository.** `sets/cc-ms.dac` and `sets/cc-lynx.dac` reference it by name so that a
   player who owns a copy can drop it in; the file itself never arrives.

A fourth category is not level data at all but gets committed by the same accident: **`.tws`
solution files, `save/`, and `tw_settings.ini`.** These are personal state. A `.tws` is somebody's
solution collection, and `tw_settings.ini` carries whatever paths that person's machine uses.

## Decision

- **The bundled community packs stay.** They are upstream's, they are redistributable, and removing
  them would make a fresh clone unable to play anything.
- **`CHIPS.DAT` is never committed**, in any casing.
- **`.tws`, `save/` and `tw_settings.ini` are never committed.** `.gitignore` covers them and CI
  fails on any tracked one, because `.gitignore` does nothing about `git add -f` — which is exactly
  what happens while reproducing a bug.
- **Unit tests synthesize their own fixtures** rather than reading `data/`. A test that needs a
  level with a cloner wired one cell off the bottom of the map, or a solution truncated mid-header,
  cannot get one from a real set; and a unit test that opens a 143 KB file to check an integer
  round-trip is not a unit test.
- **Integration and end-to-end tests use `data/intro.dat` with `sets/intro-ms.dac`.** It is real,
  it is committed, it is nine levels and 3,415 bytes, and it exercises the whole stack — file IO,
  `.dac` parsing, series loading, the engine — against data the program actually ships with.

## Consequences

- The repository can be cloned and played without acquiring anything.
- Any test that wants a *large* real set must **skip**, not fail, when it is absent. Nobody else's
  checkout has the maintainer's 528-set collection.
- The CI guard is a deny-list of specific names, not a blanket ban on `.dat` — a blanket ban would
  fail on the first commit and get switched off, which is worse than not having it.
