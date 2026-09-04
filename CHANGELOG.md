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
- The suite grew from 17,059 to **17,089 checks**.

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
