# 0008 — Accidental diagonals are load-bearing: never "fix" block slapping away

**Status:** Accepted (restated at jc-43, 2026-08-28) · **Applies to:** `generic/in.c`, `generic/dirinput.c`, `lxlogic.c`

## Context

Holding two perpendicular direction keys at once makes Tile World produce a **diagonal** command.
To anyone reading the input layer for the first time this looks like a bug — the player has asked
for two directions and the game has invented a third, and no ruleset has a diagonal move.

It is not a bug. `choosechipmove()` in `lxlogic.c` turns that diagonal into a **sideways block
push**: it probes `f2 = canmakemove(cr, cr->dir ^ dir, CMM_PUSHBLOCKS)`, and *that probe is the
slap*. It is not a test that precedes the real move; the push happens inside it.

Block slapping is a technique real levels are designed around. **CCLP3 #16 *Two Sets of Rules*,
CCLP5 #84 *Piston It Away* and several Lynx-only CCLXP2 levels are unsolvable without it.**

## Decision

**The diagonal-formation behavior is preserved exactly.** Any change to the input layer must leave
the predicate "at least one vertical and at least one horizontal direction qualified" intact.

When jc-43 rewrote direction arbitration to follow press recency instead of table order, it was
constrained by this: the new predicate was shown to be **provably identical** to the old
`mergeable[]` logic — both reduce to that same statement — so no diagonal that used to form can stop
forming. Only *which key represents an axis* changed.

## Consequences

- A bug report of the form "pressing two arrows makes Chip move diagonally" is **working as
  intended**. Close it with this record.
- Any future input work — a focus-loss handler, an input latch, key remapping — must state
  explicitly what it does to diagonal formation, and must be tested for it.
  `test/dirinput_test.c` and `test/input_test.c` exist mainly for this.
- **The regression is silent.** A broken slap does not crash and does not look wrong; it makes a
  level quietly unsolvable, and the report arrives months later from a player who assumes he has
  got worse at the game. That is why this behavior has tests and an ADR rather than a comment.
- **The solution corpus cannot see this class of change.** `doturn()` ignores its `cmd` argument
  whenever `state.replay >= 0`, and batch mode never enables joystick behavior, so `input()` is
  never called during verification. A green corpus run says nothing about the input layer. Hand
  playtesting and the unit tests are the only oracles.
- The related limit is worth stating so nobody "fixes" it either: a sub-50 ms tap of a perpendicular
  key slaps roughly one time in four while moving, because Chip consumes input only when
  `cr->moving <= 0`. Telling players to *hold* the key is correct advice; an input latch would
  change Lynx move selection far beyond this, and is deliberately out of scope.
