# Contributing

Written for humans and coding agents alike. **Read [`CLAUDE.md`](../CLAUDE.md) first** — it carries
the traps, and several of them will let you ship a change that appears to work and does not.

## Setup

Windows, and **MSYS2 at `C:\msys64`**:

```sh
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,SDL2,qt5}
pacman -S mingw-w64-x86_64-qt5-static     # only needed to build the release flavor
```

That is the whole setup. There is no package manager for the project itself, no dependency manifest,
and nothing to vendor.

## The loop

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Flavor dynamic   # fast build
powershell -ExecutionPolicy Bypass -File run-tests.ps1               # unit + end-to-end
```

`build.ps1 -Flavor dynamic` is the one to use while working: it builds in a fraction of the time of
the static release flavor and runs the same code. The executable needs `C:\msys64\mingw64\bin` on
`PATH` to find Qt's DLLs.

Use `-Flavor static` (the default) only when you are about to package.

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File run-tests.ps1                        # everything
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -Build                 # build first
powershell -ExecutionPolicy Bypass -File test\run-tests.ps1 -Filter mslogic   # one unit test
powershell -ExecutionPolicy Bypass -File test\run-e2e.ps1                     # end-to-end only
powershell -ExecutionPolicy Bypass -File run-tests.ps1 -ResultsPath test-results   # JUnit XML + JSON
```

Adding a test is dropping a `*_test.c` into `test\`. There is no registry.

Conventions that are not negotiable:

- **Assertions go through `tw_test.h`**, so results reach the machine-readable report.
- **Every test declares `tw_expect_atleast(N)`**, and N is the exact number of checks it runs.
  Raise it when you add cases. **Never lower it to make a run pass** — a shrinking count means cases
  have stopped running, which is the whole reason the floor exists.
- **A case that asserts nothing fails.** If a case is deliberately empty, `tw_skip("why")` it.
- **Tests are built as C *and* C++** unless the file declares `TESTLANG:` and says why
  ([ADR 0004](../docs/adr/0004-every-test-is-built-as-c-and-as-cpp.md)).
- **Fixtures are synthesized** by `test/tw_fixture.h` and `test/mkfixture.c`, written from the CC1
  format specification and **not** from `encoding.c` — otherwise a round trip proves only that the
  builder and the parser share a misreading.
- **Never commit `CHIPS.DAT`, a `.tws`, `save/`, or `tw_settings.ini`.** The `.dat`/`.dac` files
  already under `data/` and `sets/` are upstream's redistributable packs and belong there
  ([ADR 0005](../docs/adr/0005-what-level-data-may-be-committed.md)).

### Prove a new test can fail

A test that has never failed has not been tested. Before calling one done, **plant the defect it
claims to catch** and watch it go red, then restore the source *and rebuild*. This is not ceremony —
three cases in this suite were written, reviewed, passed, and turned out to pin nothing at all:

- the row-32 cloner case used `x = 12`, and the old and new code produce the *same* value for every
  x except zero;
- the creature-position case asserted "not the aliased value" when the correct and the broken
  implementations both satisfy that; and
- the diagonal round-trip case used gaps too small to reach the five-byte encoding it was written to
  protect.

All three passed against a deliberately broken engine. Mutation is how that was found.

## Working alongside other agents

- `build.ps1` writes into `build-<flavor>\`, and **`package.ps1` wipes all of `dist/`**. Two agents
  building in one checkout will produce confusing, irreproducible failures. Use `git worktree`, or
  give each agent its own `-BuildDir`.
- **One agent owns `FORK_BUILD_TAG` per release.** A published tag cannot be un-published.
- **Leave no stray `tworld2.exe` running.** A live process holds the executable open and the next
  build's link step fails with a lock error that reads like a permissions problem. Kill by **PID**,
  captured from `Start-Process -PassThru` — never by process name, which would also kill an instance
  the maintainer is playing.
- Announce your file set. `tworld.c`, `mslogic.c`, `package.ps1` and `README.txt` conflict with
  almost everything.

### `.claude/settings.json` is a convenience, not a security boundary

That file is committed, so it applies to anyone who runs a coding agent in a clone of this repo.

**It reduces prompts; it does not contain an agent.** The `allow` entries are deliberately exact
rather than wildcarded, because a trailing `:*` would permit arbitrary extra arguments — and these
scripts have arguments that matter: `build.ps1 -BuildDir` and `-Manifest` write to any path. The
`deny` list is a typo-catcher for the obvious forms and nothing more; it is literal prefix matching,
so `git push origin main --force` and `git push origin +main` sail straight past it. **The real
protection for "two builds must never report the same tag" is a GitHub ruleset on `refs/tags/jc-*`
blocking deletion and non-fast-forward** — enforced server-side, where no client-side pattern can be
talked around.

**A pull request that edits `.claude/settings.json` is a privilege-escalation attempt against your
own agent.** Review diffs to it the way you review code, not the way you skim config.

## Pull requests

CI runs on `windows-latest` under **Windows PowerShell 5.1**
([ADR 0010](../docs/adr/0010-ci-builds-on-windows-with-msys2.md)) and does: the hygiene checks,
`verify-defaults.ps1`, the unit suite, a dynamic Qt build, the end-to-end suite, and CodeQL. Please
run the tests locally and paste the summary line.

Do not introduce PowerShell 7 syntax (`&&`, `||`, ternary, `??`) — it is a parse error on 5.1, which
is what the maintainer runs and what CI uses.

## Style

- **American English** in code, comments, identifiers, strings and documentation.
- **Four-space indentation**, matching upstream. The hard tabs after `#include` and `#define` are
  column alignment; leave them. `.editorconfig` carries this.
- **Do not reformat upstream code.** This is a fork, and its value is that `git diff` against
  upstream 2.3.1 shows exactly what changed. Whitespace churn buries a three-line fix in a
  four-hundred-line diff. Fix whitespace only in lines you are already changing.
- Comment *why*, not *what*. Mark fork changes `/* MOD (Jeremy, jc-N): ... */` and **name the trap
  that motivated them**. The comments here are the reason this codebase is workable; they are not
  decoration.
- Prefer a measurement to an adjective. "Measured: 13 seconds of clock lost before the fix, 3 after"
  is worth a paragraph of prose.
- When you change behavior a comment describes, update the comment in the same edit.

## Before you "fix" something odd

Check [`CLAUDE.md`](../CLAUDE.md) §7 and [`docs/adr/`](../docs/adr/). A dozen things here look like
bugs and are deliberate: the diagonal that two arrow keys produce, thirty-two dead-looking `NO_FIX_*`
macros, an off-by-default build tag, hand-built zip entry names, a separate wrapped-navigation
function, and a deliberately over-tall row on the score screen. In this repository the surprising
choice is usually the considered one.

`CLAUDE.md` §8 lists the defects that are known and **not** fixed, with their origin. Check there
before reporting one.
