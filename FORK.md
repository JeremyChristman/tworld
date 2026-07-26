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
   discard-the-wiring behaviour back.
   Measured over 269 MS sets / 20,332 valid solutions plus 909 Lynx solutions, same tree built both
   ways: **on = 4 fixes, 0 regressions**; off reproduces the old behaviour exactly, down to an
   identical stderr warning census.

3. **Per-tick desync trace** (`mslogic.c`, `#ifdef TRACE_DESYNC`).
   A **no-op in normal builds.** When compiled with `-DTRACE_DESYNC`, `advancegame()` dumps the
   shared-RNG value + blob/walker positions each engine tick to stderr, for diffing against
   SuperCC's trace to pin SuperCC→Tile World solution-replay desyncs. See the RE writeup below.

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
