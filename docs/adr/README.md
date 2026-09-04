# Architecture Decision Records

Short records of the decisions in this repository that are **counter-intuitive on purpose**. Each one
looks like a bug, an oversight, or a thing to tidy up, and each one is load-bearing.

They exist because an agent or contributor arriving with no context will otherwise "fix" them, and
several of those fixes are silent — the build succeeds, the tests pass, and the shipped program is
wrong or a player's solutions stop replaying. Read the relevant record before changing the behavior
it describes.

| # | Decision | Status |
|---|---|---|
| [0001](0001-one-statically-linked-executable.md) | The release is one statically linked executable | Accepted |
| [0002](0002-engine-fixes-are-opt-out-macros.md) | Engine fixes ship on by default, behind `NO_FIX_*` opt-out macros | Accepted |
| [0003](0003-tests-compile-the-source-under-test-directly.md) | Tests `#include` the source under test instead of linking a build tree | Accepted |
| [0004](0004-every-test-is-built-as-c-and-as-cpp.md) | Every test is built as C and as C++, unless it says why not | Accepted |
| [0005](0005-what-level-data-may-be-committed.md) | What level data may be committed, and what may not | Accepted |
| [0006](0006-fork-h-owns-the-build-tag.md) | `fork.h` is the only definition of the build tag, and the tag defaults off | Accepted |
| [0007](0007-settings-live-in-tw-settings-ini.md) | Settings live in `tw_settings.ini`, in the working directory | Accepted |
| [0008](0008-accidental-diagonals-are-load-bearing.md) | Accidental diagonals are load-bearing: never "fix" block slapping away | Accepted |
| [0009](0009-the-rng-must-never-change.md) | The RNG must never change, and must not match the original game | Accepted |
| [0010](0010-ci-builds-on-windows-with-msys2.md) | CI builds on Windows with MSYS2, and tests the dynamic Qt flavor | Accepted |

## The short version, for someone in a hurry

If you are about to change one of these, you are probably about to break something:

- the diagonal two arrow keys produce (0008)
- anything in `random.c` (0009)
- a `NO_FIX_*` macro that appears to guard nothing (0002)
- the `.dat` and `.dac` files committed under `data/` and `sets/` (0005)
- the build tag being off by default (0006)
- `-std=` in the test runner (0003)

## Writing a new one

Copy the shape of an existing record: **Context** (the forces, with the evidence), **Decision** (what
we do, stated plainly), **Consequences** (what this costs and what it forbids). Number it
sequentially, add it to the table, and link it from `CLAUDE.md` §7 if it is something a newcomer
would otherwise try to fix.

Prefer measurements to assertions. The records here that have earned their keep are the ones that say
*"measured: 13 seconds of clock lost before that fix, 3 after"* rather than *"this is faster"* — a
number is the thing a future reader cannot argue with.

Supersede rather than delete. If a decision is reversed, say so in the record and keep the original
reasoning visible; the history of a reversal is exactly what stops it being re-reversed by the next
person who finds the original argument persuasive.
