# Jeremy's Tile World fork — notes

A personal fork of **[SicklySilverMoon/tworld](https://github.com/SicklySilverMoon/tworld)**
(Tile World 2, the modern Qt build), tag **2.3.1**, with a couple of small mods and some
reverse-engineering instrumentation. Upstream is GPL; see `COPYING`.

## What's changed vs upstream 2.3.1

Each mod is its own commit on top of the pristine `2.3.1` import, so `git log`/`git diff` show
exactly what's mine:

1. **Pack-name window title** (`tworld.c`, `CMakeLists.txt`, `oshw-qt/TWApp.cpp`).
   The window title shows the level **pack** name (`Tile World - Joshie`) instead of the current
   level name — matching the same mod in SuperCC. Also adds the guarded static-Qt build plumbing so
   the deployed exe is self-contained (no bundled Qt DLLs). Dynamic builds are unaffected.

2. **MSCC row-32 cloner glitch** (`mslogic.c`, `encoding.c`, `state.h`, `NO_FIX_ROW32_CLONER`).
   A cloner wired to `(x, 32)` addresses one cell past the bottom of
   the map; in `CHIPS.EXE` that lands in the game's variable block, so the cloner reads its creature
   template out of row 0's bottom layer and spills MSCC's internal variables back into that row when
   it fires. Levels were built on it deliberately (TLFC3's *BLOCKED* / *REENTRY* / *THROUGH THE
   GATES*). Tile World discarded these wirings, so SuperCC solutions for such levels could not
   replay. **On by default**; build with `-DCMAKE_C_FLAGS=-DNO_FIX_ROW32_CLONER` to get the old
   discard-the-wiring behavior back.
   Measured over 269 MS sets / 20,332 valid solutions plus 909 Lynx solutions, same tree built both
   ways: **on = 4 fixes, 0 regressions**; off reproduces the old behavior exactly, down to an
   identical stderr warning census.

3. **Broken-teleport slide state** (`mslogic.c`, `NO_FIX_BROKEN_TELEPORT_SLIDE`).
   A teleport lying under a floor tile is flagged `FS_BROKEN` at load, and every place that *acts* on
   a teleport honors that — `teleportcreature()` and `endmovement()` both skip it. The slide code did
   not: after bouncing Chip off a blocked slide it re-armed his slip state, and `choosechipmove()`
   discards input from a slipping Chip, so the player's next move was swallowed. Fixes `TCCLP#283`.

4. **Teleporting a block onto Chip** (`mslogic.c`, `NO_FIX_TELEPORT_BLOCK_ONTO_CHIP`).
   A block may normally shove into Chip — that is how blocks squash him — but as a *teleport
   destination* test MSCC judges the exit by what lies **under** Chip. Tile World answered TRUE
   unconditionally and so picked a different exit teleport. Monsters already get this treatment in
   the next branch of `canmakemove()`; blocks did not. Scoped to `CMM_TELEPORTPUSH`, which
   `teleportcreature()` is the only caller to pass. Fixes `PeterM2#25`.

5. **Controller direction from stalled creatures** (`mslogic.c`, `NO_FIX_CONTROLLERDIR_STALLED`).
   A Bug / Paramecium / Teeth standing on a beartrap or clone machine does not choose its own
   direction — it takes `controllerdir()`, the MSCC "controller bug". Tile World wiped that to NIL
   whenever it skipped a `CS_HASMOVED` creature (in practice a stalled tank), leaving the trapped
   creature with no direction and costing it a move it should have made. SuperCC never clears it: its
   list walk does `direction = monster.getDirection()` for every creature where `!isAffectedByCB()`
   (all but Teeth/Bug/Paramecium), moved or not. **Fixes 11 desyncs** — the largest single win so far.
   **Known cost:** `GAP'sSub#10 "Dimension Hole"` no longer replays. That was accepted deliberately.
   Measured against SuperCC on that level, the old engine diverges at tick 119 and this one at tick
   353, and at 119 SuperCC agrees with *this* engine — the old replay was completing by luck. It now
   reaches a separate, unrelated bug: a spurious tank clone at (12,1) from the cloner wired to the
   button at (1,19). That spurious clone is the next thing to chase.

6. **Per-tick desync trace** (`mslogic.c`, `#ifdef TRACE_DESYNC`).
   A **no-op in normal builds.** Emits one canonical per-tick line (Chip, all creatures, all blocks,
   as grid positions) tagged with the level number, matching the format SuperCC's `TraceLevel.java`
   produces so the two can be diffed directly. Set `TW_TRACE_LEVEL=<n>` to trace a single level out
   of a batch run. When compiled with `-DTRACE_DESYNC`, `advancegame()` dumps the
   shared-RNG value + blob/walker positions each engine tick to stderr, for diffing against
   SuperCC's trace to pin SuperCC→Tile World solution-replay desyncs. See the RE writeup below.

7. **User-selectable background color** (`oshw-qt/TWTheme.{h,cpp}` (new), `oshw-qt/TWMainWnd.{h,cpp,ui}`).
   **Options > Background Color...** opens a full color picker with a live preview; **Options >
   Restore Default Background** goes back to the stock Tile World blue. The choice is written to
   `tw_settings.ini` as `bgcolor=#rrggbb` the moment it is made (not just at exit) and is read
   back in `TileWorldMainWnd`'s constructor, so it survives restarts. Any color name `QColor`
   understands works if the line is hand-edited (`bgcolor=darkorange`); an unparseable value falls
   back to the default rather than failing to start.

   The stock look is **one color plus five shades of it** — `TWMainWnd.ui` hardcodes #285080 for
   Window/Button and its `lighter(150)/lighter(125)/darker(200)/darker(150)` derivations for the
   frame borders. `TWTheme::recolor()` reproduces exactly that derivation from whatever color is
   chosen, so the whole window retints as one theme instead of one panel changing and the trim
   staying blue. Rendering with the default color is **pixel-identical to jc-30** (verified: 0 of
   673,360 pixels differ on the same level). Foreground text flips between white and black by WCAG
   relative luminance, so pale backgrounds stay readable; roles that sit on the deliberately black
   `Base` (the level list, the find box, the LCD panels) keep white text and are left alone, as are
   the green list highlight and the game view itself.

   Known behavior: the picker is modal and the menu bar only exists on the game page, so changing
   the color means being on a level. A level that has not been started costs nothing. On a level
   already **running**, the dialog's nested event loop blocks the game loop, so `waitfortick()` is
   not called while it is open; `settimer(+1)` on close rebases the tick clock, without which
   `generic/timer.c` (which has no clamp) replayed every missed tick in a burst with no input
   sampled. Measured with a ~10 s dialog: **13 seconds of clock lost before that fix, 3 after**.

## Building (Windows, MSYS2)

The deployed flavor is a single self-contained exe via **static Qt**. From the MSYS2 MINGW64 shell:

```sh
export PATH=/mingw64/bin:$PATH
unset NoDefaultCurrentDirectoryInExePath      # else the comptime.bat step fails
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DOSHW=qt \
      -DCMAKE_PREFIX_PATH=/mingw64/qt5-static
cmake --build build-static
strip build-static/tworld2.exe                # -> rename to "Tile World.exe"
```

Ship `zlib1.dll` + `libzstd.dll` from `/mingw64/bin` beside the exe (Qt's static CMake config pins
those two as dynamic imports; SDL2 and the qwindows platform plugin are baked in). Packages needed:
`mingw-w64-x86_64-{gcc,cmake,ninja,qt5-static,SDL2}`. **Never build inside a Dropbox/synced folder**
(sync fights the compiler); copy the source to a local scratch dir first.

To build the instrumented tracer instead, add `-DCMAKE_C_FLAGS=-DTRACE_DESYNC` at configure time and
rename the output (e.g. `tworld_trace.exe`) so the normal install is untouched.

Batch-verify solutions (GUI-free): `tworld_trace.exe -b -r -S <savedir> <set>.dat-ms.dac 2> trace.txt`
(`-r` = read-only so the `.tws` is never rewritten; `-S` must be quoted if the path has spaces).

## Why the instrumentation exists (the desync project)

Goal: fix Tile World's MS engine so **SuperCC-made solutions always replay**. Findings so far:

- SuperCC and Tile World share a **byte-identical** RNG (glibc-style LCG); MSCC (`CHIPS.EXE`) uses a
  different generator. The RNG is **not** the desync cause — Tile World already matches SuperCC on it.
- The two engines just count ticks at **different granularity** (`TW_currenttime = 2·SuperCC_tick − 2`).
- For `geodave4fixes #6` the first divergence is a **walker cloned at a clone machine stepping off
  ~3 ticks earlier in Tile World than in SuperCC**, which shifts its RNG draw and flips its direction
  (survive → drown), cascading the level. It is **not** the blob/teeth timing phase.

The eventual fix is to reconcile the two engines' clone step-off timing, then regress against a large
corpus of known-good solutions. Full RE writeups live outside this repo (personal notes).
