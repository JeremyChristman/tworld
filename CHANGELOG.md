# Changelog

Every release of this fork, newest first, in the spirit of
[Keep a Changelog](https://keepachangelog.com/).

Versions are build tags, `jc-N`, not semantic versions — the fork tracks upstream Tile World rather
than versioning independently. Dates are the tagged commit's date.

**Two executables must never report the same build tag.** The tag is compiled in from `fork.h` and
is what the About box and `-V` claim about the bytes in front of you. Once a tag is published it
describes those exact bytes forever.

This file is a summary. **`README.txt` section 7** is the user-facing history that ships inside the
download, and **`FORK.md`** is the engineering record: why each change exists, what broke first, and
what was measured. All three are updated together — see
[`.github/RELEASING.md`](.github/RELEASING.md).

Changes that do not ship in the executable — tests, documentation, developer tooling — accumulate
under **Unreleased** and fold into the next release a user can actually observe. A build tag should
stay attached to something someone can see.

---

## Unreleased

Nothing yet.

## jc-52 — 2026-09-06

Three fixes a user could in principle observe, and a great deal of testing. **Every engine and
`series.c` edit in this release was verified the same way: golden master 1,806 digests unchanged, all
18 `NO_FIX_*` witnesses holding, and a corpus differential of 0 of 303 per-set outputs different with
warnings identical across all 303 sets.**

⭐ **Two of the three fixes were found by tools nobody aimed at a line** — the uninitialized pointer
by cppcheck on the `analysis` job's first run, and the codec off-by-one by the first test
`TWTextCoder` has ever had. That is now the pattern this project runs on rather than a happy accident.

### Added — `tworld.c` has its first test, aimed at the two documented traps

`tworld.c` is 2,505 lines and was the largest file with no unit coverage. The end-to-end layer drives
its command line; the navigation logic runs only from the pre-level screen, which needs a GUI and a
keystroke, so no automated layer reached it.

- **`test/tworld_test.c`** — 51 checks over 21 cases, covering exactly the two behaviors CLAUDE.md §7
  flags as load-bearing: `changecurrentgamewrapped()` being separate from `changecurrentgame()`
  (item 6), and "last level" being `count - 1` rather than `islastinseries()` (item 7). Plus the
  password gate, clamping, and the Melinda counter reset.
- ⚠ **`main` is renamed, not removed** (`#define main tworld_main`), so the test drives the real file
  rather than a copy — the point of [ADR 0003](docs/adr/0003-tests-compile-the-source-under-test-directly.md).
  The cost is a 91-symbol stub block, generated from the real headers so a signature cannot drift.
- **Mutation-proven, 4 mutations**, and two of them are worth recording:

  🔴 **A stub silently disabled half the logic.** `hassolution()` was auto-stubbed to `return 0`, so
  the password gate saw every level as unsolved and two cases failed for a reason that had nothing to
  do with the code. It is now faithful to `play.c:491` and marked as the one non-inert stub.

  🔴 **Clearing a struct is not initializing it.** `hassolution()` is `besttime != TIME_NIL`, and
  `TIME_NIL` is `0x7FFFFFFF` — so a `memset` gamesetup reads as **solved**. The first harness had
  every level solved and the password cases passed for the wrong reason.

  🔴 **Mutation testing found a missing case.** Removing the `currentgame == count - 1` half of the
  wrap test was NOT caught: the existing `-10` case cannot reach it, because a clamped `-10`
  *succeeds* and returns before the wrap logic runs. Only a password-blocked move away from the end
  gets there — precisely the scenario the jc-39 comment describes. That case now exists and catches
  it.

⚠ **Overall coverage fell 47.0% → 37.6% while the suite grew**, the fourth time and by far the
starkest: `tworld.c` is 1,338 instrumented lines and this test aims at five functions, so it entered
the denominator at 2.9%. Every other file is unchanged or better. Read the per-file column.

### Changed — the `NO_FIX_*` search finds 18 of 32, up from 13

`test/nofix/nofix.c` now lays a **focus profile** beside Chip on every seed: a block against a
teleport, a tank on a cloner with its blue button, a creature in a wired beartrap, a switch-wall
pair, and eight more. The rest of the room stays random, and the profile is chosen **by the seed**,
so `-scan`, `-diff`, `-one` and the committed matrix all work unchanged.

The reason the previous search stalled at 13 was not effort: a scattered room rarely puts Chip next
to the one square that matters, and these fixes need the arrangement *and* Chip positioned to drive
it this turn. Five more toggles now have witnesses, including **`NO_FIX_TANK_ON_CLONER`**, which had
resisted every earlier attempt including a 1,000,000-seed sweep.

⚠ Adding a profile renumbers the seed space, so all 18 witnesses were re-searched from scratch and
the matrix regenerated. That is documented at the profile switch: it is the cost of changing the
generator, and the reason not to change it casually.

### Added — static analysis in CI

- **`.github/workflows/ci.yml`** gains an `analysis` job running **cppcheck** over everything the
  shipped program compiles, with `.cppcheck-suppressions` alongside it.
- 🔴 **It gates on `error` severity only.** This is a 2001 C codebase carrying an upstream fork's
  idiom; enabling every style rule would produce hundreds of findings nobody intends to act on, and
  a red X nobody can act on is how a build gets ignored — the same argument CLAUDE.md makes against
  gating on coverage. Warning, performance and portability findings are **printed** so they are
  visible without holding the branch hostage.
- ⚠ Every suppression must carry a reason. A suppression without one is a finding somebody hid
  rather than judged.

**What the first run actually found: no live defects.** Every error-severity finding was checked
against the source individually and every one is a false positive, recorded with its reason in
`.cppcheck-suppressions`:

- **`memleakOnRealloc`, ~12 sites.** `x_alloc(p, n)` is
  `((p) = realloc((p), (n))) || (memerrexit(), 0)` and `memerrexit()` is `die("out of memory")` —
  which **exits the process**. The original pointer really is lost and it cannot matter. ⚠ If
  `x_alloc` is ever changed to return on failure, that suppression becomes wrong and every one of
  those sites becomes a real leak; the file says so.
- **`uninitvar` at `series.c:698`.** `mapfileinfo key;` sets only `key.filename` before
  `bsearch()`. cppcheck cannot see through the function pointer to `compare_mapfileinfo()`, which
  reads **only** `->filename`.
- **`incorrectStringBooleanError`.** The engines' `_assert(!"a message")` idiom — the string-to-bool
  conversion is the point. The unit suite already suppresses the matching `-Wunused-value`.

"We looked and it was fine" is a result worth having; until this job existed nobody could say it.

### Fixed — an uninitialized pointer dereference on a path-qualified command line

⭐ **The analysis job's first real finding, and no test could have caught it.**

`getseriesfiles()` initializes five fields of its local `seriesdata s` and leaves `s.curdir` holding
stack garbage. `curdir` is assigned **only** by the two `findfiles()` calls in the *else* branch — so
when `preferred` names a file with a path in it, `getseriesfile(preferred, &s)` is called directly
with `curdir` never set, and hands it to `openfileindir()`, whose first test is `!dir || !*dir`. That
**dereferences the uninitialized pointer** before the `strchr(filename, DIRSEP_CHAR)` short-circuit
can save it.

⚠ **What it costs in practice: almost always nothing, which is why it survived.** Whatever byte is
read, the call reaches `fileopen()` with the full path either way — `!*dir` true goes there directly,
`!*dir` false falls to the `strchr`, which must match because a name without a separator could not
have reached this branch. The one bad outcome is a pointer into unmapped memory, and then the program
**segfaults on startup** for an ordinary command line: `tworld2 sets\CCLP1.dac`.

**Measured rather than assumed**: before the fix that command line was confirmed to reach the
uninitialized path (no directory-scan warnings, unlike the else branch) and to survive; after it, it
still works. Upstream's (`929d9c6`).

`s.curdir = NULL` is the right value, not `seriesdir`: `openfileindir()` checks `!dir` first and
falls straight through to `fileopen()` with the path the user gave, which is what this branch means.

🔴 **This is the case for static analysis in one finding.** No unit test could reach it — the
behavior is correct on any machine where the read does not fault, so a test would pass either way.
No fuzzer either: it is not input-dependent. Only reading the code without running it finds this
class, which is exactly what the job is for.

Verified: golden master 1,806 digests unchanged, all 18 `NO_FIX_*` witnesses hold, and the corpus
differential — which loads all 303 sets through `series.c` — is **0 of 303 outputs different, with
warnings identical too**.

### Fixed — two more unguarded `movelaws[]` indexes, this time defensively

cppcheck's one substantive warning: `mslogic.c` indexes `movelaws[floor]` at two sites where `floor`
has passed `!iscreature()`. That negation does **not** bound the value — `iscreature()` is a *range*
test (`>= Chip && < Water_Splash`), so its negation admits terrain (< 64, in range) **and animation
ids ≥ 0x7C = 124**, against a 64-entry array. Both now go through jc-50's `movelaw_block()` /
`movelaw_creature()` helpers.

⚠ **Unlike jc-50, this is defensive rather than a live fix, and the distinction is stated in the
code.** jc-50's three sites indexed by a cell's *bottom* layer, which really does hold a creature on
18% of real levels. The animation case here cannot occur in this engine: `mslogic.c` does not contain
`Water_Splash`, `Bomb_Explosion` or `Entity_Explosion` anywhere — animations are Lynx's business —
and `encoding.c` cannot produce such an id from a `.dat` byte either.

Verified rather than argued: **golden master 1,806 digests unchanged, all 18 `NO_FIX_*` witnesses
hold, and the corpus differential is 0 of 303 outputs different with warnings identical too.**

### Added — the build toolchain is pinned

CI ran `pacman -S mingw-w64-x86_64-gcc` and built with whatever version was current that morning,
with nothing recording which one or noticing when it changed. For a program whose purpose is
byte-exact replay, the compiler is an input.

- **`docs/toolchain.lock`** records the exact compiler package; **`.github/install-toolchain.ps1`**
  installs *that file by URL* rather than by name and then asserts `gcc -dumpversion` matches. All
  three MSYS2 install steps (unit, build-and-e2e, release) go through it.
- ⚠ **MSYS2 has no native pinning** — there is no `pacman -S gcc=16.2.0`. Pinning means fetching the
  exact package from repo.msys2.org, which is a real dependency on an external mirror: if the file is
  removed the step fails **loudly**. Deliberate. A fallback to the current package would silently
  undo the pin.
- The verification runs **after** the extra packages, so a dependency dragging the compiler forward
  is caught rather than missed — the failure mode that would have made the pin decorative.
- 🔴 Only the compiler is pinned. cmake, ninja, Qt and SDL2 drive the build or link into the GUI;
  none decides what the engine computes. Pinning twenty packages multiplies the ways a mirror can
  break the build for no extra protection.

**Found on the way in: CI and the maintainer's desktop were already on different compilers** — 16.2.0
against 16.1.0 — and nothing had ever said so. That turns out to be reassuring rather than alarming,
and there is direct evidence: `test/golden/engine-snapshot.tsv` is generated on the 16.1.0 desktop
and the `golden` CI job recomputes all 1,806 digests on Linux with a third compiler and gets the same
answers, on every push. Two toolchains, identical engine behavior, checked continuously.

### Added — `unslist.c` has its first test, and it is NOT dead code

- **`test/unslist_test.c`** — 45 checks over 20 cases, `unslist.c` **0% → 90.7% lines, 84.8%
  branches**. Covers the record format, the `ok` retraction line, malformed input (flagged but not
  fatal), the level-number range guard, CRLF files, `markunsolvablelevels()` and `clearunslist()` —
  plus a case that parses **the real `res/unslist.txt` that ships**, so a hand-edit that breaks a
  line is caught here rather than by a startup warning nobody reads.
- **Mutation-proven, 5 mutations**, and the fifth is the one worth recording: dropping the set-id
  check in `markunsolvablelevels()` was **not** caught by the first draft, because the fixture had
  only one set in the list. A case with the same level listed under two names was added, and it now
  fails. A vacuous case found by mutation rather than by luck.

🔴 **AND THE REPOSITORY HAD TWICE RECORDED THAT THIS FILE WAS DEAD.** `FORK.md` first said it was
"exercised by the corpus run"; that was corrected, and **the correction was also wrong** — it claimed
the `unsolvablelist` resource is "set neither in `res/rc` nor in `initresourcedefaults()`". It is set,
on line 6 of `res/rc`. Both records are corrected.

The reason the second attempt failed is the useful part: `res/rc` spells the key `UnsolvableList`,
`rclist[]` spells it `unsolvablelist`, and `readrcfile()` **lowercases the key before comparing**
(`res.c:323`). A grep for the table's spelling finds nothing in `res/rc` and reads exactly like proof
of absence. **Absence of a grep hit is not absence of a caller — follow the call.**

### Added — a second `oshw-qt/` test, which found a shipped defect

- **`test/qt/textcoder_test.cpp`** — 26 checks over 16 cases covering `TWTextCoder`, the CC1↔Unicode
  codec that every level name, password and hint passes through. Chosen on the same principle as the
  `.ccx` test: cover the files in `oshw-qt/` that are **not** widgets. 2 of 8 files there are now
  covered.
- Drives **all 255 non-NUL byte values** rather than a sample, because the interesting risk is the
  `static_cast<unsigned char>` in `decode()`: `char` is signed here, so without it every byte ≥ 0x80
  would index the table at a negative offset — the same class as jc-48's ctype defect, on exactly the
  accented characters European level packs are full of. Mutation-proven: removing the cast fails 9
  checks.

🔴 **The round-trip case failed on its first run, and the defect was real — now fixed.** `encode()`
was shifted one byte below `decode()` for eleven characters (U+20A1 through U+0152): `decode`
reserves `0x81` as an undefined slot, `encode`'s hand-written switch was built against a table with
no gap there. **11 of 255 byte values did not round-trip.** `decode` was the correct side — it
matches CP1252 across `0x83..0x8C`. Upstream's (`929d9c6`).

**Fixed by deleting the switch, not by correcting eleven constants.** `encode()` now reverse-looks-up
`encodeTable`, the same table `decode()` reads, so the two are inverse *by construction*. The bug
existed because two hand-maintained tables had to agree and nothing checked that they did; correcting
the constants fixes this instance and leaves the next edit free to reintroduce it. There is now one
table, and adding to it needs no matching change in `encode()`.

Verified both directions: with the old code the test reports exactly 11 failures, with the new code
none, and breaking a single table entry in the lookup trips 2 checks. The static build still links,
and the golden master, corpus-independent layers and full suite are unchanged — this file is Qt
display code and touches no engine path.

### Fixed — `run-tests.ps1` did not run the two layers that watch the engine

**A green local suite meant less than it looked.** `run-tests.ps1` ran unit, e2e and Qt; the golden
master and the `NO_FIX_*` matrix — both built specifically to catch an engine change — were reachable
only by running their scripts directly or by pushing to CI. A contributor could edit `mslogic.c`, run
the documented entry point, get "all green", and never touch either.

Both are now in the default set (`-Golden` and `-NoFix` narrow to one). They need a compiler but no
built executable and no Qt, and together they cost about fifteen seconds. Verified: with a fix
switched off in the engine, the top-level runner now exits 1 and names the failing layer.

⚠ That verification also showed why the two are **not** redundant — the mutation tripped `nofix` and
left `golden` green, because `NO_FIX_TRAP_REFRESH` is one of the 30 toggles the golden master cannot
distinguish over real levels.

### Added — the first unit test for `play.c`

`play.c` is 565 lines and had **no unit test**: only the end-to-end layer touched it, with two
solutions, through the whole program. It is the seam between a stored solution and an engine —
`doturn()` decides every tick whether the command came from the player or from a recorded move list,
when a replay has overrun its own end, and what gets appended to the move list that becomes a `.tws`.
A defect there does not crash; it silently records or replays the wrong thing.

- **`test/play_test.c`** — 63 checks over 35 cases. `play.c` **0% → 26.6% lines, 33.8% branches**.
- 🔴 **It installs a FAKE engine, deliberately.** Driving a real one would test the engine, which
  three other layers already do better. The fake records what `currentinput` held when it was called,
  which makes *play.c's own decisions* observable — including the fact that **`doturn()` ignores its
  `cmd` argument entirely during a replay**, the property CLAUDE.md §3.5's warning rests on and that
  nothing had ever pinned.
- Covers: live input and `CmdPreserve`; move recording and the `lastmove` reset; replay delivery and
  index advance; the "got ahead of saved solution" warning; overrun against `besttime` including
  `timeoffset`; the `MAXIMUM_TICK_COUNT` ceiling; stepping arithmetic including the MS `n &= ~3` mask;
  the death counter's read clamp, write clamp, saturation and suppression; and `prepareplayback()`'s
  refusal paths — jc-47's site.
- **Mutation-proven, 7 mutations:** removing the `CmdPreserve` guard, the replay-recording guard, the
  `lastmove` reset, the overrun check, the MS stepping mask, or the death-count read clamp each turns
  the suite red.

⚠ **The seventh mutation was NOT caught, and the test now says so.** Removing the `if (n <
DEATHCOUNT_MAX)` guard inside `recorddeath()` changes nothing observable, because `setdeathcount()`
clamps again on the way in. Removing **both** does fail (3 checks). So that case pins the ceiling as a
*behavior*, not the particular guard implementing it — written into the case rather than left for
someone to over-read a green run.

⚠ **Overall coverage fell 46.9% → 45.6% while the suite grew**, for the third time and the same
reason: `play.c`'s 297 lines entered the denominator at 26.6%. Read the per-file column.

**Noted, not fixed:** `writesteppingstring(buf, stepping)` ignores its `stepping` argument and
formats `state.stepping` instead (`play.c:220`). Harmless today — both call sites pass exactly that —
but the signature promises something it does not do. Upstream's; left alone under the
don't-reformat-upstream rule and recorded in the test header so it is not rediscovered as a mystery.

### Added — the `NO_FIX_*` differential matrix: 13 of 32 engine toggles are now provably live

`mslogic.c` carries **32 `NO_FIX_*` toggles**, each isolating one engine fix from the jc-2..jc-29
desync project so a future investigation can put the old behavior back ([ADR 0002](docs/adr/0002-engine-fixes-are-opt-out-macros.md)).
They are the machinery this entire fork was built with, and **nothing tested any of them.** ADR 0002
named that risk exactly — *"deleting one changes no shipped behavior, so nothing fails, until the
next desync investigation needs the switch that is gone"* — and it had already come true: two of the
32 did not compile.

- **`test/nofix/nofix.c`** generates a 9×9 room packed with the furniture the toggles concern, plays
  a short deterministic game, and hashes the result. The search runs the same input through a fix-on
  and a fix-off build and keeps the first seed where they **differ**. Such a seed is a **witness**:
  proof the fix is live and reachable.
- **13 of the 32 now have a committed witness**, in `test/nofix/nofix-matrix.tsv`, replayed by the
  new **`nofix` CI job** on every push. The check asserts both digests are unchanged **and that the
  two still differ** — the last clause is the one with teeth. Runners: `test\run-nofix.ps1` and
  `test/run-nofix.sh`.
- 🔴 **Witnesses are searched for, not hand-written**, and [ADR 0012](docs/adr/0012-engine-toggles-need-a-differential-witness.md)
  says why: a fixture written from a `MOD` comment tests the comment. The search found
  `NO_FIX_KEEPSLOT_OCCUPANT` at seed 132,433 and `NO_FIX_CONTROLLERDIR_STALLED` at 744,176 — nowhere
  anybody would have thought to construct.

**How the number moved, measured at each step.** The golden master over all 903 real levels
distinguishes **2**. Raising its ticks 400 → 2000 and its walks 1 → 4 → 12 each found **nothing
more**. Generating small packed rooms found **1**. Adding **deliberately stacked cells** — a creature
or block placed on top of machinery, which is what nearly every one of these fixes is actually about
— took it to **13**. The limit was never search effort; it was that random play does not *construct*
a tank on a cloner.

⚠ **A blank row is a statement about the search, not the fix.** All 32 were separately confirmed to
change the preprocessed source, so none is dead code, and a blank is never grounds for deleting one.
`nofix -stats` reports which arrangements the generator is actually producing, so "never built it"
can be told apart from "built it and nothing changed".

### Fixed — two `NO_FIX_*` toggles that could not be switched on at all

Found by building all 32 one at a time, which nobody had ever done. Both this fork's own.

- **`NO_FIX_RFF_DRAW_ONCE`** — `rff_keepdir` was declared under `#ifdef FIX_RFF_DRAW_ONCE` but also
  written by the `FIX_RFF_CHIP_REARM` block, so disabling the first left an orphaned write:
  *"'rff_keepdir' undeclared"*.
- **`NO_FIX_TELEPORT_STALE_FG`** — same shape: `prepush_destfloor` declared under one teleport toggle
  and read under `FIX_TELEPORT_BROKEN_DYNAMIC`.

Each declaration is now guarded by **either** toggle, matching the existing idiom at `mslogic.c:1183`.
**Shipped behavior is unchanged** — at the defaults the declaration is present either way, and all
1,806 golden-master digests are identical across the change. **All 32 build now.**

⚠ Both pairs later turned up sharing a witness seed exactly (1109 and 3624), which is not a
coincidence: they are the same pairs whose declarations were tangled, and they touch the same path.

### Added — a golden-master snapshot of both engines, and CI can finally see an engine change

**Until now nothing in CI could detect an engine behavior change.** All six jobs went green on a
push that silently broke replay: the entire automated replay gate was **one** end-to-end case
driving a synthesized set with a single valid and a single invalid solution. The instrument that
can answer the question — `test/run-corpus.ps1` — needs the maintainer's private collection and his
desktop, and runs by hand. Meanwhile **903 levels sat committed in `data/`** doing nothing as an
oracle.

- **`test/golden/golden.c`** drives every committed level through **both engines** with a
  deterministic move stream and hashes the whole gamestate after every tick — **1,806 digests over
  903 levels in 1.6 s**. `test/golden/engine-snapshot.tsv` holds the baseline; `-check` recomputes
  and fails on any difference. Runners: `test\run-golden.ps1` and the new **`golden` CI job**.
- 🔴 **The move stream uses its own PRNG, never `random.c`.** The engines draw from `random.c` for
  blob movement and random slides, so an input drawn from it would make a change to `random.c`
  alter every level's *input* as well as its output — and the resulting diff could not distinguish
  a behavior change from a different walk.
- **The job runs on Linux although the baseline is committed from a Windows build**, so a green run
  is also a standing check that the digests are reproducible across compilers and platforms. That
  is what keeps the hashing honest about struct padding and undefined shifts.
- Each row carries **outcome and tick count beside the digest**, which turned out to matter
  immediately: when the digest formula changed, all 1,806 digests moved while every outcome and
  tick count stayed identical — telling the two cases apart at a glance.

🔴 **What it does NOT cover, measured rather than assumed.** Every one of the 32 `NO_FIX_*` engine
toggles was built separately and checked against the baseline. **It detects two of them**
(`NO_FIX_BLUE_BUTTON_TIMING`, `NO_FIX_CHIP_ONTO_CLONER`). Raising the tick count 400 → 2000 and the
walk count 1 → 4 → 12 each detected **nothing further**. The limit is not how far the walker
wanders: a random walker does not *construct* a block resting on a teleport or a tank on a cloner.
Those need designed fixtures — the `NO_FIX_*` differential matrix, which this does not replace. It
catches **gross** engine change (a mutation to Chip's idle timer moved 577 of 1,806 rows): a smoke
alarm, not an audit. `run-corpus.ps1` still decides whether a release ships.


## jc-51 — 2026-09-05

### Fixed

- 🔴 **A level declaring 32,768 or more chips could kill the running program**, in **both engines**.
  `state.chipsneeded` is a **signed** `short` (`state.h:251`) filled from the `.dat`'s **unsigned**
  16-bit word (`encoding.c:187`), so any level asking for `0x8000` or more chips arrives **negative**.

  Two predicates then disagree about the same state. The socket gate asks `chipsneeded() > 0`
  (`mslogic.c:1846`, `lxlogic.c:790`) — false for a negative count, so **the socket opens for a level
  whose requirement was never met**. Moments later `endmovement()`'s `Socket` case asserts
  `chipsneeded() == 0` (`mslogic.c:3264`, and `lxlogic.c:1399`/`:1475`) — also false, so `_assert`
  calls `die()` and **the shipped game exits** with *"internal error: failed sanity check"*. Not a
  wrong result on screen: the process terminates.

  Both gates now ask **`chipsneeded() != 0`**, which agrees with the assert. The socket stays locked,
  which is the defensible answer for a level demanding an unreachable number of chips.

  ⚠ **For every non-negative count the two predicates are identical**, so no sane level can observe
  the change. That is the whole safety argument, and it was still measured rather than trusted:
  **289 sets, 0 of 303 per-set outputs differ.**

  **Upstream's** (`929d9c6`, the 2.3.1 import). **No real level reaches it:** the collection was
  scanned — **31,090 levels across 393 `.dat` files, zero** asking for 32,768 chips or more. It takes
  a damaged or hand-crafted file, which is exactly what found it.

### Notes

- ⭐ **This is the finding jc-50's notes listed as OPEN and left unfixed on purpose**, with the `fuzz`
  job red because of it. It was deferred because it sits on a path every recorded solution depends on
  and deserved a decision rather than a patch. Diagnosing it took tracing the reproducer rather than
  reading the assert: the suspicion recorded at the time was *"a slide or teleport path"* reaching
  `endmovement()` around the gate. **That was wrong.** Nothing bypasses the gate — the gate itself
  says yes, because the count is negative.
- The reproducer moved from `test/fuzz/known-findings/mslogic-socket-assert` into the replayed corpus
  as `test/fuzz/corpus/mslogic/socket-negative-chipsneeded`, per
  [`docs/adr/0011`](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md) — **a finding
  is not fixed until its input is committed and replayed.** `known-findings/` is now empty of
  findings, and the `fuzz` job should return to green.
- Both engine tests gained a case (`mslogic_test.c` 122 checks / 29 cases, `lxlogic_test.c` 79 / 21).
  **Mutation-proven:** with the gate reverted to `> 0`, both test binaries `die()` and the runner
  reports `[no-result]`; with the fix, both are green.
- 🔴 **The underlying type mismatch is untouched, deliberately.** `chipsneeded` is still a signed
  `short` holding an unsigned file value. Widening it changes a struct every engine path reads, for a
  case no real level reaches; making the parser reject `>= 0x8000` would refuse a file upstream
  accepts. Fixing the *disagreement between the two predicates* is the minimal change that makes the
  program safe, and it is the one that could be measured against the corpus.

### Repository work folded into this release

None of what follows changes the executable; it accumulated under **Unreleased** while jc-50 was the
current build and ships with jc-51 under the rule that a build tag stays attached to something
someone can see.

### Changed — the corpus instrument now watches stderr too

`test/run-corpus.ps1` records each set's stdout **and** stderr, and until now compared only stdout.
Verifying jc-51 by hand turned up that **29 of 303 sets printed different warnings between the two
builds and the script said "IDENTICAL"**. Every one was a moved `__LINE__` — `err.c` stamps the
source line into every message, so adding a *comment* to `mslogic.c` changes 29 files' worth of
output. Harmless. **"Nothing noticed" was the finding**, not the diff.

- `.err` is now compared after normalizing the two things that change for reasons that are not
  behavior: the `[path/file.c:NNN]` stamp, and the scratch corpus directory's per-run GUID, which
  appears inside file-name messages.
- 🔴 **Advisory, deliberately: it reports but does not fail the run.** No recorded solution's verdict
  depends on a warning, and failing a release over a reworded message is how a script gets ignored.
  The `.out` comparison remains the byte-for-byte desync gate that sets the exit code.
- Mutation-proven: injecting two fabricated warning lines into a recording makes the check name both
  sets while the verdict stays `IDENTICAL` and the exit code stays 0.

### Added — both engines are fuzzed

The four parser targets prove a malformed file is **refused**. They say nothing about a file that is
**accepted** and then breaks the engine — and that is not a hypothetical class, it is **jc-45**: a
beartrap wiring with an out-of-range `to` that sailed through every parser check and was dereferenced
inside `initgame()`. Seven real level sets in circulation carry one. No parser target could ever have
found it; it took a person reading the code and hand-building a level.

- **`test/fuzz/fuzz_mslogic.c` and `test/fuzz/fuzz_lxlogic.c`** — six fuzz targets now, four parsers
  and both engines, 60 s each per push (~6 minutes of fuzzing).
- 🔴 **The input is split so the fuzzer explores the level and the play together:** a move-count
  byte, a move stream, then the raw level record. A level alone only reaches `expandleveldata()` and
  `initgame()`; the interesting failures need Chip to walk into something. A reproducer therefore
  encodes both halves.
- Seeded from the same real CCLP level records the `encoding` corpus uses, paired with four move
  plans (still, walk-east, mixed, long-play) — so the fuzzer starts from levels that actually load.
- **Both corpora are replayed by the unit suite**, per [`docs/adr/0011`](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md) —
  `mslogic_test.c` and `lxlogic_test.c` each gained a replay case that drives the engine exactly as
  the fuzz target does. This is the only layer that reaches an engine with a hostile level on
  Windows.
- ⚠ **Every execution is independent, deliberately.** Both engines keep file-scope state, so
  `shutdown()` is called on every path out and the PRNG is reseeded to a fixed value each run.
  Without both, a crash would depend on the inputs before it and the reproducer would not reproduce.
- ⚠ **An `_assert` failure counts as a finding.** The engines' `_assert` calls `die()`, which in the
  shipped program exits — so a downloaded file that can violate an engine invariant kills the game.
  The targets abort on it and let libFuzzer save the input.

Verified locally before CI by driving both targets over their corpora under
`-fsanitize=undefined -fsanitize-undefined-trap-on-error`, which needs no libFuzzer.

### Added — the Lynx engine has a test

`lxlogic.c` is 2,045 lines and had **zero coverage of any kind**. It was the largest hole in the
suite, and not a hypothetical one: the maintainer's collection holds **909 recorded Lynx solutions**,
every one of which depends on this file behaving exactly as it does.

- **`test/lxlogic_test.c`** — 64 checks over 19 cases: movement and timing, walls, water, fire,
  bombs, the exit, the clock, chips and the socket, dirt, gravel, block pushing, ice, force floors,
  a level with no Chip, and an off-grid creature entry.
- **`lxlogic.c` coverage went from 0% to 49.1% lines / 40.7% branches** — making it the
  best-covered engine in the tree, ahead of `mslogic.c`'s 38.1%. Overall coverage 37.4% → **42.6%**
  lines, 28.3% → **34.0%** branches. Baseline updated.
- Mutation-proven three ways: making water non-fatal, opening the socket with chips outstanding, and
  removing the exit's `completed()` each turn the suite red.

🔴 **These are characterization tests on purpose.** For an engine with recorded solutions,
"different" and "wrong" are the same thing — a change here does not produce a bug report, it
silently stops somebody's solution replaying. The corpus differential remains the instrument that
*decides* whether an engine change is safe; this is the fast half that says **which rule** broke.

**Three Lynx facts that make naive tick arithmetic look like engine bugs**, all found the hard way
and now documented in the test's header:

- A creature's **position is committed when its move begins**, not when the animation ends.
- `advancegame()` withholds the verdict for a **13-tick endgame timer** after the level is decided;
  a case that ran 16 ticks after reaching the exit saw 0 and looked like a defect.
- ⚠ **`chipisalive()` must not be used from a test.** It is `id == Chip`, and between ticks Chip's id
  is legitimately `Pushing_Chip` whenever he is straining against a wall — so it reports "not alive"
  for a Chip in perfect health. The engine's own single use of it is safe; an outside observer's is
  not.

### Fixed — another comment that was wrong

- `test/mslogic_test.c` said `pedanticmode` is "defined in `tworld.c`". It is defined in
  **`lxlogic.c:54`**, as `tworld.c:96` itself points out. Defining it in the MS test is correct only
  because `lxlogic.c` is not in that translation unit — writing the Lynx test surfaced this, because
  doing the same there is a redefinition error.

### Added — the first test of anything in `oshw-qt/`

Applying jc-48's lesson immediately: **the next thing with no coverage** was `CCMetaData.cpp`, the
`.ccx` metadata parser. `.ccx` is XML that ships *inside* a level pack, so it sits on the same trust
boundary as `.dat`, `.dac` and `.tws` — and nothing had ever parsed one under test.

- **`test/qt/ccmetadata_test.cpp`** — 90 checks. Level-number bounds, author and ruleset
  inheritance, prologue/epilogue pages, the direct-child stylesheet rule, `Clear()`, and a hostile
  deeply-nested/huge-attribute input. It also parses **all six real `.ccx` files this repo ships**
  (224 KB of CDATA, HTML and entities no hand-written fixture would imitate) — which nothing had
  done before.
- **`test/run-qt-tests.ps1`** — a third layer, because Qt-linked tests cannot use the gcc-only
  runner (`CCMetaData.cpp` needs QDomDocument, QString and QColor). It **skips loudly** when Qt is
  absent so a developer without it still gets every other layer; the CI step asserts it did *not*
  skip, so that can never quietly become normal.

🔴 **`.ccx` is the one parser no other layer can reach.** `readextensions()` returns immediately when
`g_pMainWnd` is null — precisely the batch case — so no corpus run, no end-to-end case and none of
the four fuzz targets has ever touched it, and none can. A Qt-linked unit test was the only option.

**It found no defect**, and that is a fine result: the level index really is bounds-checked and Qt
does the parsing. Mutation-proven all the same — deleting the bounds guard kills the run. "We looked
and it was fine" is worth having; until now nobody could say it.

### Fixed — documentation that was wrong

- **`FORK.md`'s jc-48 verification table claimed `unslist.c` was "exercised by the corpus run, which
  reads the `.ccx` extension files".** `.ccx` is `CCMetaData.cpp`, not `unslist.c`, and is never
  parsed in batch mode at all. Corrected, with the reasoning kept: verify what exercises a file
  before writing it down.

  🔴 **The replacement text was ALSO wrong, and is corrected again below** — it claimed the
  `unsolvablelist` resource "is set neither in `res/rc` nor in `initresourcedefaults()`". It is set,
  on line 6 of `res/rc`. See the Unreleased section.
- A stray carriage return in `CLAUDE.md` (`test<CR>un-e2e.ps1`) from an old `sed` whose `\r` was
  taken as an escape. Repository swept; no other text file has one.

## jc-50 — 2026-09-05

> ⚠ **There is no jc-49 release, and the tag exists.** This fix was first tagged `jc-49`, and the
> release workflow failed at its Tests step: `test/run-qt-tests.ps1` probed pkg-config with `2>$null`
> under `ErrorActionPreference = "Stop"`, and PowerShell 5.1 turns a native command's stderr into a
> terminating error — so on the release runner, which has no pkg-config metadata for Qt5, an intended
> *skip* became a *failure*. Nothing was ever published as jc-49.
>
> The tag could not be moved onto the fix: the `refs/tags/jc-*` ruleset refused the force-push,
> which is precisely what it is there for. So the fix ships as jc-50 and `jc-49` remains a tag with
> no release. That is the honest outcome, and cheaper than weakening the rule.

### Fixed

- 🔴 **`movelaws[]` was indexed out of bounds on ordinary levels.** The array has exactly **64
  entries, one per terrain id** — but a cell's *bottom* layer can hold a **creature**, and creature
  ids start at `Chip == 64`. So `movelaws[cellat(to)->bot.id]` read past the end by up to 47 entries
  (the highest such id is 111), and used whatever followed the array in `.rodata` as a movement rule.

  **This is not an exotic malformed-file case.** Scanning the whole collection — **328 `.dat` files,
  31,090 levels — 5,743 of them (18%)** put such a tile in a lower layer, including **CC1 itself and
  every one of CCLP1–5 and CCLXP2**. Ordinary, official level data.

  **It is this fork's own defect, not upstream's.** All three call sites were added during the
  desync work (`FIX_KEEPSLOT_OCCUPANT`, jc-17 era); `git log -L` puts them on commits `c69ed2b`,
  `42537ae` and `5d076b3`. Two are in `canmakemove()`; the third is inside a `TRACE_DESYNC` block and
  so never in a shipped binary.

  ⚠ **There is no "correct" old behavior to preserve** — the old read was undefined and could differ
  between compilers or builds, which means replay was never guaranteed stable across toolchains for
  these levels. Any deterministic answer is an improvement. Zero (*"this terrain refuses every
  direction"*) is chosen because the predicate asks "would the terrain underneath have refused too?",
  and a cell whose bottom layer is a creature has no terrain that permits anything.

  **Replay-neutral, measured rather than assumed:** jc-48 against jc-50 — **289 sets, 18,640 valid /
  1,107 invalid under both, 0 of 303 per-set outputs differ.** Whatever the out-of-bounds read was
  picking up, no recorded solution depended on it.

### Notes

- ⭐ **Found by the new MS-engine fuzz target on its first run**, as a UBSan out-of-bounds report at
  `mslogic.c:1970`, about one second in. That is three consecutive finds by tooling nobody pointed at
  a specific line: jc-46 by UBSan, jc-47 by LeakSanitizer, jc-50 by the engine fuzzer.
- 🔴 **And it is the first defect found by fuzzing the ENGINES rather than the parsers** — the class
  the parser targets structurally cannot reach. jc-45 was the same shape and had to be found by hand.
- The reproducer is committed as `test/fuzz/corpus/mslogic/movelaws-oob-bottom-creature` and replayed
  by the unit suite. Mutation-proven locally: with the fix, 8 corpus inputs run clean; reverted to the
  raw index, the same input dies with `SIGILL` under `-fsanitize-undefined-trap-on-error`.

## jc-48 — 2026-09-05

Both defects were found by **writing the first unit test the `.dac` parser has ever had**.
`readconfigfile()` was the last untrusted-input parser in the C core with no coverage of any kind —
`CLAUDE.md` §5 listed it as a known gap, and the end-to-end layer covered its happy path by accident.

### Fixed

- 🔴 **A `.dac` could name a file outside the data directory, and the guard against it did not
  work on Windows.** `readconfigfile()` asked `haspathname()` (`fileio.c:354`) to reject a data-file
  name containing a path. That function tests only `DIRSEP_CHAR` — a **backslash** on Windows — so a
  forward slash passed, and Windows accepts forward slashes as separators perfectly well.
  `openfileindir()` (`fileio.c:428`) then makes the same backslash-only test, finds none, and
  **joins** the name onto the data directory, so `file = ../../../x.dat` resolves straight out of it.
  `haspathname()` also `stat()`s the name and answers FALSE when nothing is there, so what it
  really reports is "an existing file behind a path" — not the question this call site asked.

  Now tested directly for both separators. The exposure was modest — an arbitrary-file **open**,
  read-only, whose bytes are parsed as level data and almost always rejected, with no channel back
  to whoever wrote the `.dac` — but the check was written to stop exactly this and did not.
  **Upstream's.**

  **And a device name needs no separator at all.** `CON`, `NUL`, `COM1`, `LPT1` and friends resolve
  to the **device** from inside any directory, extension ignored, so `file = LPT1` reached a
  parallel port. This fork made precisely that argument for *tileset* names in jc-42 and left level
  sets open; the check has moved from `res.c` to `fileio.c` as `isreservedfilename()` and both
  callers now share it — where it also finally gets a unit test, since `res.c` has none.

  **Nothing legitimate is refused, and that is measured rather than assumed:** of the maintainer's
  598 real `.dac` files, none contains a separator in its `file=` line and none names a device; the
  full corpus run, which opens every one of them, is byte-identical. ⚠ One qualification: a
  backslash is a legal filename character on Linux and macOS, so rejecting it *is* a behavior change
  there — one a Windows-only collection cannot speak to.

- **Undefined behavior wherever a `char` was handed to `<ctype.h>`.** `isspace()`, `tolower()` and
  `isalpha()` are defined only for values representable as `unsigned char`, or `EOF`. `char` is
  **signed** on both toolchains this builds with, so every byte `>= 0x80` arrived as a negative int.
  **Twenty-two casts across six files:** `series.c` 6 (the `.dac` parser), `solution.c` 6 (its *two*
  `.dat`-suffix comparisons), `tworld.c` 5 (level-name word-wrap), `res.c` 3 (the `res/rc` tileset
  config), `unslist.c` 1, `fileio.c` 1. **Level packs carry accented characters**, so this was
  ordinary input, not an attack.

  `tworld.c` additionally gained a range check: its argument is a command code and the `Cmd` enum
  runs past 255 (`CmdReservedLast` is 511), so `isalpha()` could be handed a value that is neither a
  valid `unsigned char` nor `EOF`. `oshw-qt/TWMainWnd.cpp:566` already used that idiom. Values in
  0–255 behave exactly as before. **Upstream's.**

  ⚠ **Nothing observable was broken, and the fix changes nothing.** All 256 byte values were run
  through `isspace`/`isalpha`/`tolower`/`toupper` as signed and as unsigned on the shipping
  toolchain: **zero differing results.** This removes undefined behavior; it does not repair a
  visible fault. There is consequently no test that can distinguish the two, and the high-bit cases
  in `series_test.c` are a crash net rather than a regression net for the casts — stated there too.

  ⚠ **Three instances in `oshw-sdl` are deliberately left**: `sdlout.c:812` and `sdltext.c:110,336`.
  Those files are not compiled by this fork's build, so the change could not be built or tested.
  (`oshw-qt/CMakeLists.txt` *does* compile one `oshw-sdl` file — `sdlsfx.c` — but it has no ctype
  calls, so nothing that ships is affected.)

### Added

- **The `.dac` parser has a test suite** — `test/series_test.c` went from 33 checks to 110, covering every
  directive, the comment and blank-line skip, nine ways of being malformed, high-bit bytes, and the
  254-byte line boundary. Every REFUSED case is mutation-proven: removing the guard it names turns
  it red. Removing only the backslash arm of the path fix gives 1 failure; removing the device-name
  guard gives 3.
- **A fourth fuzz target**, `test/fuzz/fuzz_dac.c`, over `readconfigfile()`, with thirteen seeds and the
  usual corpus replay in the unit suite. It is the one parser here whose interesting inputs are text
  rather than byte patterns, which is why it needed its own harness.

  🔴 Worth knowing what it is watching: the parser's two `sscanf` calls have **no width specifiers**
  and are safe only because `filegetline()` caps the line at 254 characters first — a bound in a different
  function, which is the same shape as jc-44's third defect.

### Notes

- **Replay-neutral, measured over the whole collection.** jc-47 against jc-48: **289 sets, 18,640
  valid / 1,107 invalid under both, and 0 of 303 per-set outputs differ.**
- Suite: **17,169 unit checks**, up from 17,092.

## jc-47 — 2026-09-04

### Fixed

- **A 64-byte leak every time a solution failed to load.** `prepareplayback()` (`play.c:141`)
  declares a stack-local `solutioninfo`, and `expandsolution()` has already called `initmovelist()`
  — which allocates 16 entries — by the time it can fail. Both of that function's early exits
  returned without freeing it, so the allocation was leaked on **every attempt to play back a
  solution record that is malformed, truncated, or simply has no moves in it.** Upstream's; `git
  blame` puts it on the 2.3.1 import. Bounded and small, but unbounded in count.

  🔴 **Found by the fuzz job on its first run**, by LeakSanitizer, from a seed that is now
  `test/fuzz/corpus/solution/fmt3-packed`. That is two consecutive releases where a tool found a
  defect nobody had gone looking for: jc-46 by UBSan, jc-47 by LSan. Replay-neutral, measured over
  the whole collection — jc-46 against jc-47: **289 sets, 18,640 valid / 1,107 invalid under both,
  0 of 303 per-set outputs differ.**

### Added — fuzzing the file parsers

Every defect this fork has shipped a fix for lives in one surface: jc-44's three, jc-45's one and
jc-46's two are all code that reads files strangers made. Five of those six were found by a person
reading a parser and suspecting a specific line; the sixth was found by UBSan in seconds, in a line
nobody had looked at. This is the generalization of the sixth.

- **Three libFuzzer targets** over `expandsolution()`, `readleveldata()` and `expandleveldata()`,
  built with ASan+UBSan, 60 s each on every push (`test/run-fuzz.sh`, the `fuzz` CI job).
  `expandleveldata()` is fuzzed **ungated** on purpose — it normally runs only on records the
  password gate accepted, and jc-44's third defect was a guard in `encoding.c` that was safe only
  because of a check in `series.c`. Fuzzing through the gate alone could never reach that class.
- 🔴 **The committed corpus is the part that lasts.** libFuzzer generates fresh inputs every run, so
  a green run proves nothing durable. Every seed and every reproducer is committed under
  `test/fuzz/corpus/`, and **the ordinary unit suite replays all of it on Windows too**
  (`test/tw_corpus.h`). **A finding is not fixed until its input is in that corpus.**
  See [`docs/adr/0011`](docs/adr/0011-a-fuzz-finding-is-not-fixed-until-it-is-committed.md).
- ⚠ **What the replay proves is narrow, and the first version of it claimed more.** It proves the
  inputs still parse without crashing, and that the parser did not modify its own input. It is not a
  memory oracle; ASan is. The original design fenced each input with 64 poison bytes and claimed that
  caught over-writes without a sanitizer — measured, no parser here writes to its input at all, so
  the fences could never fire, **and the 64 legally-allocated bytes sat where ASan's redzone belongs
  and blunted it.** The buffer is now sized exactly to the input. Retracted in the ADR rather than
  quietly deleted.
- **Seeds for the MS mouse-move format**, which the first corpus missed. It is the only branch in
  the `.tws` decoder that assembles a direction from raw bits rather than through `indextodir()`,
  and the only one with its own variable-length truncation check — so it was reachable by
  luck-of-mutation and by nothing else. Three seeds now cover its two-byte form, its five-byte form,
  and a record that declares five bytes and supplies two.
- The suite grew from 17,059 to **17,092 checks**.

### Changed

- **CodeQL now observes a real build** (`build-mode: manual`) instead of buildless extraction. The
  comment in `codeql.yml` had described this as a hypothetical upgrade needing a Linux build that
  "has never been tried"; `linux-build` tried it and it takes 40 seconds. ⚠ It trades one blind spot
  for a smaller one, stated in the file: the observed build is the Linux one, so `#ifdef WIN32`
  branches are analyzed only in their POSIX form.
- **`SECURITY.md` no longer claims sanitizers and fuzzing are unavailable** — that went stale the
  moment jc-46 shipped. It now lists what actually runs, and states three residual gaps plainly:
  nothing analyzes the Windows build that ships, 60 s per push is a regression check and not a soak,
  and neither engine is fuzzed.

## jc-46 — 2026-09-04

### Fixed

- **Signed-shift overflow reading a `.tws`.** `game->solutiondata[N]` is an `unsigned char`, which
  integer-promotes to a **signed** `int`, so `solutiondata[11] << 24` on a high byte of `0x80` or
  more overflowed into the sign bit — undefined behavior. Two sites: the RNG seed in
  `expandsolution()` and the recorded best time in `readsolution()`. Both now assemble in unsigned
  types, the idiom `fileio.c:308` already used. **Upstream's**, on the 2.3.1 import.

  **Not a corner case:** seeds are random 32-bit values, so this fired on roughly *half of every
  solution file ever recorded* — ordinary files, not malformed ones. It was also sign-extending:
  `rndseed` is `unsigned long`, 64 bits on LP64, so a high-bit seed widened to `0xFFFFFFFF........`
  there.

  **No replay was ever affected, and that is not luck** — `restartprng()` masks the seed with
  `0x7FFFFFFF` (`play.c:153`) and the writer keeps only four bytes (`solution.c:406`), so the
  damaged bits were discarded at both consumers. The fix is for the undefined step in the middle,
  which a future compiler is entitled to treat as license.

### Notes

- 🔴 **This is the first defect found by a sanitizer rather than by reading the code**, and it
  arrived on the sanitizer job's first run. Every previous memory-safety fix here (jc-44's three,
  jc-45's one) was found by a human suspecting a specific line and then building a test that could
  observe it — a method that works and does not scale. This one was in a line nobody had reason to
  suspect, and the test that exposed it (`0xDEADBEEF`, whose `0xDE` is the "222" in the report) had
  been passing for weeks. That is the whole argument for the job.
- **Sanitizers are not as Linux-only as this repo claimed.** `-fsanitize=address` genuinely cannot
  run under mingw-w64, which ships no `libasan` — but `-fsanitize=undefined` **with
  `-fsanitize-undefined-trap-on-error`** needs no runtime library and works on Windows today. That
  is how the fix was mutation-tested locally: the pre-fix build dies with `SIGILL`, the post-fix
  build passes all 1,195 checks.
- **Replay-neutral, measured over the whole collection.** jc-45 against jc-46: **289 sets, 18,640
  valid / 1,107 invalid under both, and 0 of 303 per-set outputs differ.**
- `test/run-sanitizers.sh` now passes `-Dstricmp=strcasecmp` on non-Windows, matching what
  `CMakeLists.txt` does for every non-Windows build. Without it `series_test` failed to link.

### Fixed — tooling and tests

None of these changes the executable. All three were found by the jc-46 review rather than by the
change itself.

- 🔴 **`package.ps1` defaulted to `-Exe build-jc35\tworld2.exe`** — a gitignored leftover of the
  desync project. The **documented** release command, `package.ps1` with no arguments, therefore
  staged a binary from ten builds and three weeks earlier; packaging jc-46 produced a zip whose exe
  reported `build jc-35`. Nothing ever shipped wrong, because `release.yml` passes `-Exe` explicitly
  and because the tag check refused the stale exe — that check is the entire reason this was a caught
  mistake rather than a shipped one. The default is now `build-static\tworld2.exe`, which is what
  `build.ps1` with no arguments produces.
- 🔴 **`series_test.c` compiled a `series.c` the released game does not contain.** `CMakeLists.txt`
  defines `TWPLUSPLUS` unconditionally for the shipped Qt build, and `series.c` branches on it three
  times — so the test was building `gameseriescmp_name()`, which never ships, while never building
  `removefilenamesuffixes()`, which does. Same trap `CLAUDE.md` §3.3 documents for `WIN32` and
  `fileio.c`; `input_test.c` had carried the flag correctly all along. Fixed with the one-line
  `TESTFLAGS` declaration the convention already provides.
- **`readsolution()`'s `besttime` read had no coverage at all**, so half of this release's own fix
  was untested: nothing in the suite opened a `.tws` through that path, and `test/mkfixture.c` writes
  zeros at offsets 12–15, so even the end-to-end layer never saw a high byte there. A file-based case
  now does. ⚠ It cannot tell fixed from broken *by value* — both forms land in an `int` of the same
  width — so its actual job is to make the line **execute** with bit 31 set, which is what gives the
  sanitizer something to catch. Verified by reverting that hunk alone: `SIGILL`.
- `test/run-sanitizers.sh` no longer prints "clean under ASan+UBSan" regardless of what ran. The
  label is derived from the flags, so overriding `SAN` with UBSan alone — the combination that works
  on Windows — reports UBSan alone.

## jc-45 — 2026-09-04

### Fixed

- **The last unguarded map index.** `readpos()` validates only the X byte of a coordinate pair, so a
  beartrap wiring out of a `.dat` could send `initgame()`'s spring-the-traps loop reading past
  `map[]`. Every other consumer of those wirings already checked; this one did not. **Upstream's**,
  and the fourth defect from the jc-44 review — held back then because it sits in the engine and
  needed a corpus run first.

  **Real, not theoretical:** 7 malformed wirings in 4 sets in circulation (`BHLS1` #148, `CheeseT1`
  #69, `TCCLP2` #11, `ZK2` #73), out of 42,433 wirings across 22,323 levels. All of them read
  exactly one cell past the array, onto `msstate`, which is why it never surfaced.

### Notes

- **Replay-neutral, measured over the whole collection.** Batch verification of every set with a
  recorded solution, jc-44 against jc-45: **290 sets, 18,739 valid / 1,108 invalid under both, and
  0 of 303 per-set outputs differ.** `BHLS1` #148 — a level carrying a malformed wiring whose
  solution is valid — replays identically.
- 🔴 **A behavioral test could not catch this, and the first one did not.** Written the obvious way
  it passed with the guard *removed*, because the out-of-bounds byte happened not to be
  `Block_Static`. The real case poisons that byte: `map[POS_INVALID]` coincides exactly with
  `msstate`, so writing `Block_Static` into `msstate.chipwait` is what the unguarded read would see.
  It asserts the address equality first, so it fails loudly rather than silently going vacuous again.

## jc-44 — 2026-09-04

### Fixed — memory safety in the file parsers

All three are **upstream's**, not this fork's (`git blame` puts every one on the 2.3.1 import), and
**none of them changes how the game plays** — no level, no solution, no timing, no score.

- 🔴 **A `.tws` could smash a 256-byte stack buffer.** `loadsolutionsetname()`
  (`solution.c:703`) read a 32-bit record length straight out of the file and `fread`ed that many
  bytes into the caller's buffer, unbounded and unterminated; the caller passes `char buf[256]` on
  the stack and then `strcpy`s out of it. **Measured against the shipped jc-43 release binary: a
  `.tws` declaring a 1000- or 2000-byte set name segfaults it; 400 and 600 bytes end it abnormally.**
  jc-44 refuses all of them cleanly. Reachable by dragging a `.tws` onto the executable — a
  documented workflow, and how solution files get opened in practice.

  The function now takes the buffer size, clamps to it, and terminates. `readsolution()` in the same
  file already clamped the identical record to 255; this path was forgotten. The size parameter is
  the fork's addition: a clamp alone would leave the next caller free to reintroduce the bug, because
  the old signature made the constraint invisible at the call site.

- **`readleveldata()` advanced a pointer by a file-supplied size and dereferenced it**
  (`series.c`), with the existing check covering only the upper map layer. Worst case is a two-byte
  read about 64 KB past a 13-byte allocation. Read-only; triggerable from any `.dat`.

- **The lower map layer's RLE guard reserved two fewer bytes than the upper layer's**
  (`encoding.c`), while both decode loops read `data[++n]` twice on a `0xFF` escape. Not reachable
  from a `.dat` on disk — `readleveldata()`'s password gate guarantees slack — but the safety was an
  accident of an unrelated check in a different file. ⚠ `getenddisplaysetup()`'s built-in level
  passes the stricter check with **zero** margin; `test/encoding_test.c` pins that.

**Deliberately not fixed here:** `mslogic.c`'s trap-wiring dereference (`readpos()` never validates
the *y* byte). One byte, read-only — but it is engine behavior, and a change there needs a full
solution-corpus run first.

### Fixed — packaging

- **The stock `tw_settings.ini` in the release zip was missing `lynxtileset` and `mstileset`**, added
  in jc-41. `README.txt` section 6 has listed both all along as part of "the complete stock file", so
  the download's own documentation described a file the download did not contain. `verify-defaults.ps1`
  now checks the two against each other. **The only change here a downloader can see.**

### Added — the repository, not the program

None of this ships in the executable; it rides along with jc-44 per `.github/RELEASING.md`, which
is also why there is no jc-45 for it. It is what found the three defects above.

- **The repository is set up for coding agents.** [`CLAUDE.md`](CLAUDE.md) is the full brief and
  [`AGENTS.md`](AGENTS.md) the short one, in the cross-tool [agents.md](https://agents.md)
  convention. `.claude/settings.json` carries an exact — not wildcarded — permission allowlist.
- **Continuous integration.** `.github/workflows/ci.yml` builds and tests on every push and pull
  request; `codeql.yml` runs static analysis; `release.yml` packages a draft release from a `jc-*`
  tag push. Every action is pinned to a commit SHA, with Dependabot keeping the pins current.
- **A real test suite**, in two layers — unit and end-to-end. See `CLAUDE.md` §5.
- **`build.ps1`** — the MSYS2 + CMake incantation that used to live only in prose, as one command.
- **`coverage.ps1`** — gcov-based line and branch coverage, with a committed baseline so the numbers
  in the documentation cannot quietly stop being true.
- **`verify-defaults.ps1`** — checks that the stock `tw_settings.ini` shipped in the release zip
  still declares every setting `settings.cpp` does, and that `SECTION_MAXKEYS` has headroom.
- **`docs/adr/`** — architecture decision records for the choices in this repo that look like bugs
  and are load-bearing.
- **`SECURITY.md`**, `.editorconfig`, issue and pull-request templates, and `.github/CONTRIBUTING.md`
  / `RELEASING.md`.
- **This file.**


## jc-43 — 2026-08-28

### Fixed

- **Direction keys follow the player, not a lookup table.** Two defects in the keyboard layer,
  reported by a player who found that pressing a new direction while still holding the old one
  sometimes cost him a move in the direction he was leaving. Direction conflicts had been settled by
  position in the `keycmds` table — North always beat South, West always beat East, regardless of
  which was pressed more recently — and a direction pressed *and released* inside one 50 ms polling
  cycle was discarded whenever another key was held. Conflicts are now arbitrated per **axis** by
  press recency, in the new `generic/dirinput.c`.

  ⚠ **Block slapping is load-bearing and is unchanged.** The diagonal produced by two perpendicular
  keys is what `lxlogic.c` turns into a block flick; CCLP3 #16, CCLP5 #84 and several Lynx-only
  CCLXP2 levels are unsolvable without it.

  ⚠ The struck-key half rescues only sub-50 ms taps, and caps at roughly one time in four while
  moving (100% while standing still). It is not true that "tapping now slaps" — hold the
  perpendicular key.

### Added

- **The first tests in the repository**: `test/dirinput_test.c` and `test/input_test.c`, run by
  `test/run-tests.ps1`, which `package.ps1` now runs before packaging and refuses to package
  without.

### Notes

- The full corpus — 303 sets, 18,734 solutions — is byte-identical to jc-42, and that is expected
  rather than reassuring: `doturn()` ignores its `cmd` argument whenever `state.replay >= 0`, and
  batch mode never enables joystick behavior, so `input()` is never called during verification.
  **The corpus cannot see input-layer changes.** Hand playtesting is the only oracle for this class
  of change.

## jc-42 — 2026-08-22

### Fixed

- **Tileset picker hardening**: four defects found by the review of jc-41, all four reviewers
  returning CHANGES REQUIRED. The headline one — a failed pick persisted as though it had
  succeeded — was missed by the jc-41 self-review and found independently by three reviewers. That
  gap is why 41 and 42 are two releases.

## jc-41 — 2026-08-22

### Added

- **User-selectable tileset, per ruleset**, from `Options > Tileset`. Scans `res/tilesets`, stored
  as `mstileset` / `lynxtileset`; `res/rc` remains the failsafe.

### Notes

- Its multi-agent review did not complete — the agents died on a spend limit — so it shipped on
  self-review, disclosed in the commit and the release notes. jc-42 is the consequence.

## jc-40 — 2026-08-22

### Fixed

- **A startup crash.** With `-R -L -D` all given but no `-S`, and `$HOME` unset — the ordinary
  Windows GUI case — `initdirs()` left `root` NULL and `combinepath(savedir, NULL, "save")` called
  `strlen(NULL)`. Measured on jc-39: exit `0xC0000005`, no window, no output. `root` is now computed
  unconditionally. **Not a fork bug** — `git blame` puts it in the upstream 2.3.1 import.
- **`Options > Set Death Counter...` is hand-built**, from a `QSpinBox` and a `QDialogButtonBox`
  rather than `QInputDialog::getInt()`. The dead `?` title-bar button is gone and the dialog opens
  248 px instead of 216, so its own title stops being elided to `Set Death C...`.

### Notes

- ⚠ **`tw_settings.ini` did not move.** With `$TWORLDDIR` unset, `root` is `"."` before and after,
  so the file still lives in the **working directory**. Nothing in the tree resolves the
  executable's own path. "Beside the executable" is true only because double-clicking makes the two
  the same folder. The behavior that actually changed is the `$TWORLDDIR` one.

## jc-39 — 2026-08-22

### Added

- **Level navigation wraps around the ends of a set.** Previous on level 1 lands on the last level;
  Next on the last level lands on level 1. PgUp/PgDn wrap too, but only when already parked against
  an end.

### Notes

- Implemented as a **separate `changecurrentgamewrapped()`**, not a change to
  `changecurrentgame()`: three of that function's callers are not the player navigating, and
  wrapping breaks each.
- **Passwords survive by construction** — the wrap is expressed as an ordinary offset back through
  `changecurrentgame()`, so the existing accessibility scan runs unchanged. It cannot reach a level
  a hand-typed offset could not.
- "Last level" is `count - 1`, deliberately **not** `islastinseries()`, which also answers TRUE for
  a `.dac`'s `lastlevel=` line.

## jc-38 — 2026-08-21

### Changed

- The death counter reads **white** instead of sharing the message bar's dark red. Appearance only.

## jc-37 — 2026-08-21

### Added

- **`Options > Death Counter`** — a lifetime death total in the short-message bar. Off by default,
  stored as `deathcount` alongside the opt-in `showdeathcounter`. Counts every death in both engines
  plus a restart of a live run; does **not** double-count the restart after a death.

## jc-36 — 2026-08-18

### Fixed

- **Table cells that declare a column span actually span it.** The score screen's last line was
  clipped — `Total Score` rendered as `Total S`, and a grand total of `443,476,450` as `443,47`.
  `TWTableModel::SetTableSpec()` had always parsed the leading span digit only to throw it away.

### Notes

- Spans are applied **after** `resizeColumnsToContents()`, never before, and re-applied on every
  Find-box filter change because spans live in view coordinates.
- ⚠ The grand-total row is 32 px where its neighbors are 20. Pre-existing; the obvious cure
  (`setWordWrap(false)`) was built, measured, and **backed out** because it would elide long level
  names on sets past 1,000 levels.

## jc-35 — 2026-08-17

### Added

- **`Options > Ignore Passwords`** — every level in a set becomes reachable and Ctrl+G becomes a
  level-*number* prompt. Saved as `ignorepasswords`.

### Notes

- `passwordseen()` returns early while the option is on. Without that guard, browsing with the
  option on would permanently write `SGF_HASPASSWD` into the solution file for every level visited,
  and turning the option back off would **not** re-lock the set.

## jc-34 — 2026-08-17

### Added

- **Fork identity in `Help > About`**, and **`fork.h`** as the one definition of the build tag,
  author, repository and issue URLs. The About text states plainly that this is an unofficial fork,
  that the upstream maintainers neither wrote nor reviewed these changes, and that the fork's code
  was written with AI assistance. Bug reports route to this fork's tracker.
- `package.ps1` reads `FORK_BUILD_TAG` from `fork.h` — **bump that `#define` and nothing else.**

## jc-33 — 2026-08-14

### Changed

- **`tw_settings.ini` replaces `save/settings`** as the settings file.
- **`legacyscores`** restores the 2.2-style score list.
- **The build tag defaults to OFF**, so a downloader's title bar carries no private build number.

## jc-32 — 2026-08-12

### Fixed

- The background-color picker rebases the tick timer on close, and stops duplicating the stock blue.

## jc-31 — 2026-08-12

### Added

- **`Options > Background Color...`** with a live preview, and `Options > Restore Default
  Background`. Written to `tw_settings.ini` as `bgcolor=#rrggbb` the moment it is chosen.

### Notes

- The stock look is one color plus five derived shades; `TWTheme::recolor()` reproduces that
  derivation from whatever color is chosen, so the window retints as one theme. Rendering with the
  default color is pixel-identical to jc-30 — 0 of 673,360 pixels differ.
- Foreground text flips between white and black by WCAG relative luminance.

## jc-30 — 2026-08-05

### Added

- **Toggleable build tag** (`showbuildtag`), and a null-window guard on `setsubtitle()` for batch
  mode.

---

## The desync project: jc-2 through jc-29

These builds exist to make **SuperCC-made solutions replay in Tile World**. Each one reconciles a
single MS-engine behavior against SuperCC, measured over the whole solution corpus and reported as
*levels fixed / levels broken*. The count reached **zero** at jc-28.

| Build | Date | Change | Result |
|---|---|---|---|
| jc-29 | 2026-08-05 | A keyboard move abandons an outstanding mouse goal (`FIX_KEY_CLEARS_GOAL`) | 0 / 0 — required for SuperCC's TWSWriter click fix |
| jc-28 | 2026-08-04 | `FIX_CLICK_EARLY` — BlakeE1 #118, the last one | 1 / 0 — **the count reached zero** |
| jc-27 | 2026-08-04 | `FIX_CHIP_PICKUP_ON_BLOCK` — TCCLP #147 | 1 / 0 |
| jc-26 | 2026-08-04 | `FIX_TRAP_SHUT_BEHIND` + `FIX_TRAP_REFRESH` — PB_Gourami #254 | 1 / 0 |
| jc-25 | 2026-08-04 | `FIX_KEEPSLOT_FIRE` — fire refuses bugs and walkers | 3 / 0 |
| jc-24 | 2026-08-03 | Block-exposed teleports honored, and the matching double pop | 1 / 0 |
| jc-23 | 2026-08-03 | `FIX_PUSH_CANTLEAVE` made teleport-aware | 1 / 0 |
| jc-22 | 2026-08-03 | `FIX_BLOCKED_MOVE_REDRAW` | 2 / 0 |
| jc-21 | 2026-08-03 | `FIX_TELEPORT_EXIT_CHIP_TILE` | — |
| jc-20 | 2026-08-02 | `FIX_TANK_IN_TRAP_STALL` | — |
| jc-19 | 2026-08-02 | `FIX_KEEPSLOT_BLOCK_OCCUPANT` | — |
| jc-18 | 2026-08-02 | Three probe-derived fixes | — |
| jc-17 | 2026-08-02 | `FIX_KEEPSLOT_OCCUPANT` | — |
| jc-16 | 2026-08-01 | Keep tile-less duplicate creatures alive | 1 / 0 |
| jc-15 | 2026-07-29 | Chip starts from the foreground only | 1 / 0 |
| jc-14 | 2026-07-28 | Chip-half random-force-floor double draw | 5 / 0 |
| jc-13 | 2026-07-28 | Random-force-floor double draw | 6 / 1 (ZK-Adventure #304) |
| jc-12 | 2026-07-28 | Bounce re-face | 0 / 0 — eliminated the FACING hypothesis |
| jc-11 | 2026-07-28 | Ignore Chip tiles in the monster list | 1 / 0 |
| jc-10 | 2026-07-28 | Blue-button timing + sliding-tank guard | 11 / 0 |
| jc-9 | 2026-07-28 | Defer buttons pressed while choosing Chip's teleport exit | 9 / 0 |
| jc-8 | 2026-07-28 | Chip may collect a key or boots on a clone machine | 6 / 0 |
| jc-7 | 2026-07-28 | Tanks may step off clone machines | 10 / 1 |
| jc-6 | 2026-07-27 | Chip may walk onto a clone machine | 19 / 0 — the largest single win |
| jc-5 | 2026-07-27 | Slide re-facing | 6 / 0 |
| jc-4 | 2026-07-26 | Controller direction from stalled creatures | 11 / 0, with GAP-sSub #10 accepted as a known cost |
| jc-3 | 2026-07-26 | Broken-teleport slide state, and blocks teleported onto Chip | 2 / 0 |
| jc-2 | 2026-07-26 | The MSCC row-32 cloner glitch | 4 / 0 |

Every one of these fixes is compiled in by default and can be switched back off with a `NO_FIX_*`
macro — see [`docs/adr/0002-engine-fixes-are-opt-out-macros.md`](docs/adr/0002-engine-fixes-are-opt-out-macros.md).

---

## jc-1 — 2026-07-25

### Added

- **The first build of this fork.** The window title shows which *set* is being played, where
  upstream showed only the level name. Also the point at which the build became a single
  self-contained executable instead of one needing 27 DLLs beside it.
- Per-tick desync trace instrumentation in the MS engine, a no-op in normal builds
  (`#ifdef TRACE_DESYNC`).

---

## Upstream

| Version | Year | What it was |
|---|---|---|
| 2.3.1 | 2025 | Input handling made more lax by default. **The base of this fork.** |
| 2.3.0 | 2024 | Rebuilt on Qt5 and SDL2 instead of Qt4 and SDL1 — the release that changed how the score list looks, which `legacyscores` exists to undo. |
| 2.2.0 | 2015 | The version whose score list `legacyscores` reproduces. |

The full upstream history is in [`Changelog`](Changelog).
