## What this changes

<!-- What it does and what that accomplishes, in plain English. If it fixes something, say what
     broke and how it showed up. A measurement beats an adjective. -->

## Checks

- [ ] `run-tests.ps1` is green, **both layers**. Paste the summary line:
      <!-- e.g. 8 run(s), 16,877 checks total / 12 case(s), 35 checks, 0 failures -->
- [ ] New behavior has a test, and **I proved the test can fail** by planting the defect it claims to
      catch. (Three cases in this suite passed against a deliberately broken engine before anyone
      checked — see CONTRIBUTING.md.)
- [ ] Any new test declares `tw_expect_atleast(N)` with the exact check count, and no existing floor
      was **lowered**.
- [ ] No level set, `.tws`, `save/` or `tw_settings.ini` is committed. Fixtures are synthesized.
- [ ] No PowerShell 7 syntax (`&&`, `||`, ternary, `??`); the target is Windows PowerShell 5.1.
- [ ] American English throughout.
- [ ] Upstream code I did not need to touch is unreformatted — the diff against upstream 2.3.1 is
      this fork's most useful artifact.

## If this touches the engine (`mslogic.c`, `lxlogic.c`, `encoding.c`, `random.c`)

- [ ] Batch-verified a solution corpus: `tworld2.exe -b -r -S <savedir> <set>.dac`, reading
      **stdout**, from a scratch working directory. Result: <!-- N valid / N invalid -->
- [ ] I understand that a green corpus run **cannot see input-layer changes** (`CLAUDE.md` §3.5), and
      if this touches `generic/` I hand-playtested instead.
- [ ] If this changes what `random.c` produces, I have read
      [ADR 0009](../docs/adr/0009-the-rng-must-never-change.md) and understand that stored solutions
      are what pay for it.

## If this changes shipped behavior

- [ ] `FORK_BUILD_TAG` bumped in `fork.h` — and nowhere else
- [ ] `README.txt` header names the new build, and section 7 has an entry
- [ ] Any new or changed **setting** is documented in `README.txt` section 6, added to
      `settings.cpp`'s `SECTIONS[]`, and added to the stock file in `package.ps1`
      (`verify-defaults.ps1` checks the last two agree)
- [ ] `CHANGELOG.md` and `FORK.md` updated
- [ ] Launched the game **from the packaged zip** and played it

See [`RELEASING.md`](RELEASING.md) for the full sequence.

## If this changes something that looks like a bug but was deliberate

Say which ADR you read (`docs/adr/`) and why the decision no longer holds. Supersede the record
rather than deleting it — the history of a reversal is what stops it being re-reversed.
