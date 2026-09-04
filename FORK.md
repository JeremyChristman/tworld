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

8. **Fork identity in Help > About** (`fork.h` (new), `help.c`, `oshw-qt/TWApp.cpp`, `oshw-qt/TWMainWnd.cpp`, `package.ps1`).
   Upstream's About text names only the original authors and points bug reports at their tracker.
   Shipping that unchanged from a fork sends this fork's bugs to people who did not write them, so
   the `vourzhon` table now says plainly that this is an unofficial fork, names the build
   (`2.3.1 -- Jeremy Christman's fork, build jc-N`), keeps the original credit intact while stating
   that the upstream maintainers neither wrote nor reviewed these changes, discloses that the fork's
   code was written with AI assistance (Claude), and routes bug reports to
   `JeremyChristman/tworld/issues`.

   The table stays **plain text** because `-V` prints it to a terminal; `ShowAbout()` escapes it and
   linkifies the URLs at display time, so the dialog gets clickable links and the console does not
   get tag soup. That meant replacing `QMessageBox::about()` (which exposes neither the text format
   nor the interaction flags) with a configured `QMessageBox`, reproducing its icon behavior.
   `settimer(+1)` on close, for the same reason as mod 7 — About is reachable mid-level too.

   `mslogic.c`'s `_assert` message carried the last hardcoded upstream tracker URL in the tree and
   now uses `FORK_ISSUES_URL`. It is invisible in a release build — `NDEBUG` reduces `_assert` to
   `((void)0)`, and `grep -a "failed sanity check"` finds nothing in the shipped exe — but this is
   the file nearly every engine change lives in, so a debug build tripping it must not blame
   upstream.

   **`fork.h` is now the one definition of the build tag.** It was previously a C++ `QStringLiteral`
   in `TWApp.cpp`, which `help.c` (C) cannot see and which `package.ps1` parsed with its own regular
   expression — three places to keep in step, and a stale one looks exactly like a correct one.
   `TWApp.cpp` builds its tag as `"[" FORK_BUILD_TAG "]"` and the packaging script reads the
   `#define`. **Bump `FORK_BUILD_TAG` and nothing else.**

9. **Ignore Passwords** (`tworld.c`, `settings.cpp`, `oshw-qt/TWMainWnd.{cpp,ui}`, `package.ps1`).
   `Options > Ignore Passwords` makes every level in a set reachable and turns Ctrl+G into a
   level-NUMBER prompt. Saved as `ignorepasswords` in `tw_settings.ini`, written the moment it is
   clicked, read in the window constructor.

   The engine already had the concept — `usepasswds` (the `-p` flag) and the `.dac`
   ignore-passwords line — so this is mostly *exposing* it, not new access-control logic. Two
   things made it more than a checkbox:

   - **`gs->usepasswds` is a SNAPSHOT** taken in `initgamestate()`, so writing to it would not take
     effect until the next set was opened. A new global `ignorepasswds` (defined in `tworld.c`,
     `extern`-declared in `TWMainWnd.cpp` exactly as `pedanticmode` already is) plus a
     `passwdsactive(gs)` helper — `gs->usepasswds && !ignorepasswds` — moves the decision to the
     moment of each check. Six gates call it: `setcurrentgame`, `changecurrentgame`,
     `melindawatching`, the score list, `findlevelfromhistory` and the `defaultlevel` startup path.
   - **`passwordseen()` returns early while the option is on.** That flag is persistent — setting
     it writes `SGF_HASPASSWD` into the solution file and nothing can unset it from inside the
     program. Without the guard, switching the option on and browsing would permanently record the
     password of every level visited, and switching it back off would NOT re-lock the set: the
     option would have silently rewritten his save files as a side effect of being turned on.
     Upstream's `-p` does record; that is defensible for a per-launch flag, not for a saved setting.

   `selectlevelbynumber()` reuses `INPUT_ALPHA` rather than adding an `INPUT_NUMBER` prompt type —
   that enum is switched on by every oshw backend, including the SDL one this fork does not build
   and cannot test, and upper-casing does nothing to digits. It parses with `strtol` (`atoi` cannot
   tell `0` from `banana`), looks the level up by its own number via `findlevelinseries()`, and
   falls back to 1-based position when that fails — which covers sets with numbering gaps and sets
   with duplicate numbers, where `findlevelinseries` deliberately returns -1 rather than guess.

   Measured on CCLP1 with a virgin save directory: **off** → `n` stays on level 1 and Ctrl+G says
   "Enter Password"; **on** → `n` reaches level 2, Ctrl+G says "Enter Level Number" and `47` lands
   on *Bombs Away* (confirmed against `-p -s`). Toggling the menu item mid-session unlocked the set
   with no restart, and the save directory stayed free of solution files throughout.

10. **Multi-column table cells actually span** (`oshw-qt/TWMainWnd.{h,cpp}`).
   The score screen's last line was clipped: `Total Score` rendered as `Total S` and Joshie's
   grand total `443,476,450` as `443,47` plus half a digit. Both are `tablespec` items that
   declare a column span — `"2-Total Score"` covers Level+Name, `"3+<total>"` covers
   Base+Bonus+Score — and `TWTableModel::SetTableSpec()` had always *parsed* that leading digit
   only to throw it away, filling the covered columns with empty cells and never calling
   `QTableView::setSpan()`. A level's own base score fits inside the Base column; a whole set's
   does not. The SDL/`-s` text renderer honors the spans, which is why `tworld2 -s` printed the
   total in full while the GUI cut it off.

   The span now travels as a data role (`TWSpanRole = Qt::UserRole + 1`) rather than as a method
   on the model, because the view is wired to a `QSortFilterProxyModel`, and a proxy forwards
   arbitrary roles untouched — so `ApplyTableSpans()` reads spans through the proxy with no
   downcast and no second pointer to keep alive. Two ordering constraints, both load-bearing:

   - **Spans are applied AFTER `resizeColumnsToContents()`, never before.** Widths must come
     from the ordinary one-value-per-column rows — let the widest merged string into that
     calculation and it shoves its starting column out (Level as wide as `Total Score`, Name as
     wide as an entire unsolved-level row). Widths first, then spans, leaves the table looking
     exactly as it did and merely stops the long cells being clipped. Row *heights* are not
     decided by this ordering: Qt's height hint takes no notice of spans, and the call measured
     identically on either side of `resizeRowsToContents()`.
   - **Spans live in VIEW coordinates**, so filtering with the Find box renumbers the rows out
     from under them. `ApplyTableSpans()` therefore re-runs on every filter change, walking the
     proxy's surviving rows. Sorting is not enabled on this table; if it ever is, that path needs
     the same treatment.

   Scope, audited rather than assumed. The score list is the only table whose spanned body cells
   were ever too narrow for their text, but it is **not** the only table with spans:
   `generic/in.c`'s `keyhelp_twplusplus` carries four span-2 body items — two blank spacers and
   the headings `Before level playing starts:` and `During solution playback:` — shown by the Keys
   command through `LIST_HELP`. Those now merge across both columns, which is what their `"2-"`
   prefix was asking for all along — and because the Key column was already wide enough to hold
   them, the screen does not actually move: Help > Keys rendered on jc-35 and jc-36 differs by
   **0 of 561,925 pixels**. Two more
   spanned items — the help topic list's `"2-"` and the solution list's
   `"2-Select a solution file"` — are row 0, which `headerData()` serves and `ApplyTableSpans()`
   never reaches. The level-set picker and `createtimelist()` are span-1 throughout. Alongside the
   total, the fix also un-clips `*BAD*` markers and the names of unsolved levels.

   **KNOWN AND DELIBERATELY NOT FIXED: the grand-total row is 32px tall where every other row is
   20** (non-legacy style only — the 2.2 style's fixed 25px row hides it). It is pre-existing:
   jc-35 measures 32px too. Cause: `QAbstractItemView::wordWrap` defaults to true and
   `resizeRowsToContents()` measures each cell against its own column's width, so the eleven
   characters of `Total Score` count as two lines inside the narrow Level column.

   The obvious cure, `setWordWrap(false)` on the score list, was built, measured (20px, correct)
   and then **backed out**, because it trades a cosmetic win for a legibility loss:
   `resizeColumnsToContents()` honors `QHeaderView::resizeContentsPrecision()`, which defaults to
   sampling **1000 rows** — the same sampling limit that caused the clipped total in the first
   place. On a set past 1000 levels, a solved level whose name is wider than anything in rows
   1–1000 currently *wraps* and stays fully readable; with wrapping off it would be elided to
   `A Very Long Lev…` instead. Joshie is 1,325 levels, so this is not hypothetical. A too-tall
   row is a worse-looking screen; a truncated level name is a screen that lies.

   Note also that applying the spans before `resizeRowsToContents()` does **not** help: measured
   32px with the call on either side. Do not generalize that into "Qt ignores spans when sizing
   rows" — it does consult them (33px → 23px in a stripped-down table on Qt 5.15.19). What defeats
   it in this particular table was not pinned down.

   Measured on Joshie (1,325 levels): GUI total now reads `443,476,450`, byte-identical to
   `tworld2 -s`, with `legacyscores` both on and off; the total row is 20px like its neighbors;
   filtering to `ota` moves the total to row 4 and it keeps its full width. Help > Keys, the only
   other list whose body cells span, is pixel-identical to jc-35. Every other row keeps the height
   and the column widths it had. `-b -r` batch verify over the set: 1,325 valid, 0 invalid.

11. **Death counter** (`play.{c,h}`, `settings.cpp`, `oshw.h`, `oshw-sdl/sdlout.c`,
   `oshw-qt/TWMainWnd.{h,cpp,ui}`, `package.ps1`).
   `Options > Death Counter` puts a lifetime death total in the short-message bar under the hint box.
   **Off by default**, so a fresh install shows nothing; two further items, `Reset Death Counter` and
   `Set Death Counter...`, appear beneath it only while it is on. The total is stored in
   `tw_settings.ini` as `deathcount`, gated by the opt-in `showdeathcounter`, and is therefore shared
   across every level set and owned by the installation rather than by a save file — two copies of the
   game keep two totals, and a synced folder has the two machines overwrite each other's number.
   `DEATHCOUNT_MAX` (`play.h`) is the one definition of the ceiling, used by the dialog, the read clamp
   and the saturation alike.

   **What counts:** every death in both rulesets — monsters, water, fire, bombs, block squish, and
   running out of time — plus restarting a level that is still in progress, on the reasoning that
   giving up on a run is a run you lost. **What does not:** restarting after you have *already* died.
   That distinction is the whole difficulty, because every way out of the "Oops" prompt restarts the
   level (`R`, `Ctrl+R` and Space alike), so counting restarts naively scores two deaths for every one.
   The increment therefore lives only inside `playgame()`'s live loop, which the post-death prompt is
   not part of. Deliberately **not** keyed on the death sound, which looks like the signal and is not
   one: Lynx plays its own sounds for drowning and bombs and none at all for a timeout, and MS plays
   the time-out sound rather than `SND_CHIP_LOSES`.

   *jc-38* then recolored it: the counter reads **white**, where it had shared the bar's dark red with
   ordinary messages. The bar's palette carries three text colors and `RefreshShortMsgLabel()` picks by
   role — `BrightText` for a message inside its bold window, `Text` for one that has aged, `WindowText`
   for the counter. `WindowText` was previously unused, so it was free to take. Appearance only:
   nothing about what counts, what the counter does, or where it is stored changed.

12. **Level navigation wraps around the ends of a set** (`tworld.c`).
   Previous Level on level 1 and Next Level on the last level both did nothing — `changecurrentgame()`
   clamps to the ends and returns FALSE. (Only `startinput()`'s `leveldelta` macro looks at that
   return value and bells; the other four navigation call sites discard it and fail silently, which
   is why the key looked dead rather than refused.) They now wrap: off the front
   of the set lands on the last level, off the back lands on level 1. PgUp/PgDn (skip ten) wrap too,
   but only when they are already parked against an end and cannot move at all; PgUp on level 5 still
   lands on level 1 rather than ten from the end.

   The wrap is a **separate function, `changecurrentgamewrapped()`, called only from the navigation
   commands** — not a change to `changecurrentgame()` itself, because three of that function's
   callers are not the player navigating and wrapping would break each of them:
   `findlevelfromhistory()` and the `-defaultlevel` path call it with `-1` purely to back down from a
   level whose password is not known (wrap that and a locked start level flings the player to the far
   end of the set); `endinput()` calls it with `+1` for Melinda's free pass and again for "Onward!"
   after a win, where solving the LAST level must fall through to the end-of-series screen rather
   than silently restarting at level 1; and `showsolutionfiles()` calls it with an arbitrary offset
   just to restore the current level after switching solution files. The wrapped version is used by
   `startinput()`'s `leveldelta` macro, by `endinput()`'s six navigation cases, by `playgame()`'s
   quitloop (where `n` is only ever the −1/+1 the navigation keys set), and by `playbackgame()` and
   `verifyplayback()`.

   **Password protection is preserved by construction.** The wrap does not assign an index; it
   expresses itself as an ordinary offset back through `changecurrentgame()`, so the existing
   accessibility scan runs exactly as it would for the same offset typed by hand. With passwords
   enforced, Previous on level 1 therefore lands on the furthest level legitimately unlocked — on a
   fresh save that is level 1 again, i.e. nothing moves. It cannot reach a level that a direct jump
   could not. Wrapping is also refused when the move failed for any reason other than being against
   that end, so a password gate mid-set still refuses exactly as it did.

   **"The last level" is `count - 1`, not `islastinseries()`**, and the difference is visible on the
   stock upstream configurations: `sets/CCLP1-MS.dac` declares `lastlevel=144` over a 149-level
   `.dat`, so `islastinseries()` answers TRUE at level 144 while the file runs to 149. `lastlevel`
   decides only where the end-of-series screen fires; Previous and Next have always walked through
   the levels past it as ordinary levels, and still do. Wrapping to 144 would drop the player into
   the middle of the navigable range and make 145–149 unreachable by wrapping, so the two directions
   would stop being inverses of each other. (Moot for Jeremy's own collection — none of the 528
   `.dac` files in his `sets\` declare `lastlevel` at all, so there `islastinseries()` already
   reduces to `index == count - 1`.)

   Two navigation surfaces deliberately keep their old behavior. `finalinput()` — the screen shown
   after the last level of a set is solved — still sends both Previous and Next to level 1; there is
   no current level to step off, and both keys there mean "start the set over". And `endinput()`'s
   `CmdProceed` ("Onward!" after a win) still ends the series on the last level rather than wrapping.

   Measured with a harness that slices the two real functions out of `tworld.c` and stubs the
   gamespec around them: 26 checks, 0 failures, covering both wrap directions at ±1 and ±10, sets of
   1, 2, 10 and 1,325 levels, and the password matrix (fresh save, partially unlocked, fully solved).
   GUI-playtested on CCLP1 and on Joshie (1,325 levels). All five wrapped call sites were exercised in
   the real GUI, `verifyplayback()` included: queueing `Ctrl+P` immediately behind the `Shift+Tab` that
   starts a verification leaves it in the input queue for that function's own first `input()` poll, and
   the abort is distinguishable from a completed verify because `runcurrentlevel()` runs `endinput()`
   only when `verifyplayback()` returns TRUE — so the level changing with **no result dialog on screen**
   can only have come from inside `verifyplayback()`.

13. **The directory root is resolved unconditionally** (`tworld.c`, `initdirs()`).
   `root` — `$TWORLDDIR`, or `ROOTDIR` on a system build, or `"."` for the portable Windows release —
   used to be computed only inside `if (!res || !series || !seriesdat)`, on the reasoning that nothing
   needs it once `-R`, `-L` and `-D` have each been given by hand. Two things needed it anyway:

   - **`savedir`.** With no `-S` and no `$HOME` — the ordinary case for a Windows GUI launch, where
     `HOME` is simply not in the environment — the fallback is `combinepath(savedir, root, "save")`,
     and `combinepath()` calls `strlen(dir)` with no null check. **Measured on jc-39: exit
     `0xC0000005`, no window, no output.** A crash on startup, not a misconfiguration.
   - **`appdir`, and with it `tw_settings.ini`**, which fell back to `"."` with `$TWORLDDIR` never
     consulted — so setting that variable moved the level and data directories but silently failed to
     move the settings file with them. The comment above that line had claimed since jc-33 that this
     case was handled.

   Neither is a fork bug: `git blame` puts the guard and the `savedir` fallback in the upstream 2.3.1
   import (`929d9c6`). jc-33 added only the `appdir` line, and that one was null-guarded.

   ⚠ **Be precise about what did not change.** With `$TWORLDDIR` unset — the ordinary case — `root` is
   `"."` before and after, so `tw_settings.ini` is still read and written in the **working directory**.
   Nothing in the tree resolves the executable's own path (no `GetModuleFileName`, no
   `applicationDirPath`, no `chdir`), so jc-33's "beside the executable" holds only because launching
   by double-click makes the working directory the program's folder. The behavior that actually
   changed is the `$TWORLDDIR` one.

   Nothing else changes: res/sets/data still each prefer their own option and fall back to `root` only
   when that option is absent, which is exactly when the old code computed it too. The one new outcome
   is that `root` is never NULL. Measured after: the same command line exits 0 and reports
   `Solution files saved in: .\save`; with `$TWORLDDIR` set and all three options given, the save
   directory follows it and the GUI picks up that directory's `tw_settings.ini` (proved by launching
   from a different working directory and watching the build tag appear, which only that file enables).

   **Known and NOT fixed, one line below the repair:** in the `#ifdef SAVEDIR` branch, `save = SAVEDIR`
   assigns a local that the `else` branch never reads, so `savedir` stays the empty string it was
   allocated as. `CMakeLists.txt` defines `SAVEDIR` for non-Windows Debug builds only, so no Windows
   build can reach it; there, `combinepath(dest, "", path)` would read `dest[-1]` and write solutions to
   the filesystem root. Upstream's, untouched by this fork, and out of scope for jc-40.

14. **The Set Death Counter dialog is sized and stripped of its help button**
   (`oshw-qt/TWMainWnd.cpp`).
   jc-37 raised that dialog through `QInputDialog::getInt()`, a static convenience function that
   exposes neither the window flags nor the size, and it showed: a `?` in the title bar that does
   nothing (nothing in this program installs What's-This text, so clicking it enters What's-This mode
   and the next click leaves again), above a dialog too narrow to display its own title, which arrived
   elided as `Set Death C...`. Qt sizes a dialog from its contents — here a short `Deaths:` label and a
   spin box — and never consults the window title. It is now assembled by hand from a `QSpinBox` and a
   `QDialogButtonBox`, with the help hint cleared and a minimum width computed from the title's own
   measured width plus 120px for the icon, close button, frame and gaps.

   ⚠ **The width has to go on the layout as a spacer.** Three other ways to set it were built and
   measured on this dialog, and all three are silent no-ops — it opened at exactly its natural 216px
   every time: `setMinimumWidth()` on a `QInputDialog` (measured at title+120 *and* title+400, 216px
   both times — activating its layout discards the widget's explicit minimum); adding the spacer to a
   `QInputDialog`'s `layout()`, which never runs at all because that class builds its layout lazily
   when shown, so `layout()` is still null while the dialog is being configured (proved by a probe
   build that retitled the dialog when `layout()` was null — it opened reading `NO LAYOUT YET`); and
   `setMinimumWidth()` on the hand-built dialog with `QLayout::SetMinimumSize`, which is worse than
   useless because that constraint *writes the layout's own computed minimum onto the widget*,
   overwriting the explicit minimum being asked for. A zero-height `QSpacerItem` raises the layout's
   own minimum width, which is the number every constraint mode then derives from. Measured:
   title+220 opens 348px against the 216px baseline, so the mechanism binds; the shipped title+120
   opens **248px**, with the full title shown and the `?` gone. Behavior is otherwise identical —
   integers only, clamped to `[0, DEATHCOUNT_MAX]`, opening with the current total selected, OK stores
   and Cancel does not (both verified: setting 7 wrote `deathcount=7` and showed `Deaths: 7`;
   cancelling out of 99 left it at 7).

15. **Direction keys follow the player, not the table** (`generic/in.c`, `generic/dirinput.c` (new),
   `test/` (new)).
   Two defects in the keyboard layer, reported by a player who found that pressing a new direction
   while still holding the old one sometimes cost him a move in the direction he was leaving.

   **The table-order defect.** When two direction keys were down, `input()` returned whichever came
   first in the `keycmds` table — order `UP, LEFT, DOWN, RIGHT` — so North always beat South and West
   always beat East no matter which the player had pressed more recently. Two of the four reversals
   turned at once and two were ignored until the older key was released. Direction keys are now
   collected one **axis** at a time (vertical = North/South, horizontal = West/East) and each axis goes
   to its most recently pressed key, stamped by a counter in `keypressorder[]`. This applies to both
   rulesets; MS settled *every* pair by table position, including perpendicular ones, because it
   returned at the first match.

   **The discarded-tap defect.** A direction pressed *and released* inside one 50ms polling cycle
   becomes `KS_STRUCK`, which was routed to a fallback that lost to any held key — so tapping a
   perpendicular direction while running was thrown away entirely, forming no diagonal and firing no
   block slap. Such a key can now fill an **empty** axis when the other axis is held. Deliberately no
   further than that: it never displaces a key still held on its own axis (that would flicker Chip
   backwards for a move), and a *lone* struck arrow is still left to the ordinary fallback, because
   promoting it would let a tapped arrow outrank a virtual menu command arriving in the same cycle —
   `PulseKey()` delivers those as `KS_STRUCK` too.

   ⚠ **Block slapping is the reason this is delicate, and it is unchanged.** Two perpendicular arrows
   held at once produce a diagonal, and `choosechipmove()` turns that into a sideways block push whose
   `f2 = canmakemove(cr, cr->dir ^ dir, CMM_PUSHBLOCKS)` probe *is* the slap. CCLP3 #16 *Two Sets of
   Rules*, CCLP5 #84 *Piston It Away* and several Lynx-only CCLXP2 levels are unsolvable without it.
   The diagonal-formation predicate is provably identical to the old `mergeable[]` logic — both reduce
   to "at least one vertical and at least one horizontal direction qualified" — so no diagonal that
   used to form can stop forming. Only *which key represents an axis* changed.

   ⚠ **A muted key still claims its axis.** Under keyboard behavior a freshly pressed key spends a
   cycle or two in `KS_DOWNBUTOFF1/2` — physically down, deliberately silenced so one tap yields one
   move. First-match-wins never noticed, because table position does not care whether a key is muted;
   recency does. Left alone, the axis fell back to the older key and the player got exactly the wrong
   move this change exists to prevent — measured as `E, N, E, E` where it should read `E, ·, E, E`.
   A muted key therefore occupies its axis, and when a muted key *wins*, the cycle yields no direction
   at all rather than the runner-up: `input()` then reaches its `lingerflag` and returns `CmdPreserve`,
   which is precisely what that command has always been for.

   `mergeable[]` is deleted — the change orphans it completely. `casualinputs` now assigns
   `keyboard_trans[KS_DOWNBUTOFF1]` both ways; it previously only ever wrote the casual value into a
   static table and never put it back.

   **Scope, stated rather than discovered.** Three behavior changes fall outside the two defects and
   were accepted deliberately: MS perpendicular pairs now follow recency; with three or more
   directions held the diagonal's composition follows the newer key, so a `N+S+perpendicular`
   combination can move from slap-while-straight to turn-with-push (it needs Chip facing *along* the
   doubled axis, which needs two opposite keys held while moving that way); and in a text prompt
   Backspace and Space, which map to `CmdWest`/`CmdEast`, now resolve by recency instead of table
   order.

   **Known limits, not fixed here.** A tap is visible for one polling cycle and Chip consumes input
   only when `cr->moving <= 0`, about one cycle in four, so the struck-key fix takes a sub-50ms tap
   from never slapping to roughly one time in four — and to *always*, when Chip is standing still.
   Taps longer than a cycle already worked through the held path. Adding an input latch would fix that
   properly and is explicitly out of scope, since it would change Lynx move selection far beyond these
   two defects. A key pressed *twice* inside one cycle lands in `KS_REPEATING` and reaches neither
   survivor slot, so its diagonal is a cycle late; pre-existing. And a lost key-up (Alt-Tab away
   mid-move) leaves a phantom held key that now wins on recency — a net improvement, since the common
   single-arrow case recovers where it used to stick forever, but a phantom on the *same axis* as a
   real key is worse than before. There is no focus-loss handler in `oshw-qt` at all; adding one is
   the real fix and belongs in its own change.

   Nothing here can affect a `.tws`, the batch verifier, or the desync catalog: `doturn()` ignores its
   `cmd` argument entirely whenever `state.replay >= 0` and drives `currentinput` from the solution
   move list, and `batchmode` never even turns joystick behavior on.

16. **Three memory-safety fixes in the file parsers** (`solution.c`, `solution.h`, `tworld.c`,
   `series.c`, `encoding.c`, `test/` (two new suites)).
   All three are **upstream's**, not this fork's — `git blame` puts every one on the 2.3.1 import —
   and none of them changes how the game plays. They were found by a security review of the parsers
   while this repository was being made agent-ready, and they are grouped into one build because
   they are one class of defect in one surface: **length fields read out of files that other people
   made**.

   **(a) A `.tws` could smash a 256-byte stack buffer. This is the one that mattered.**
   `loadsolutionsetname()` read a 32-bit record length straight out of the solution file and
   `fread()` that many bytes into the caller's buffer, with no upper bound and no terminator. Its one
   caller (`tworld.c`) passes `char buf[256]` on the stack, and then `strcpy()`s out of it.
   **Measured against the shipped jc-43 binary** (the static release build, not a dev build): a
   `.tws` declaring a **1000- or 2000-byte** set name segfaults it, and 400 or 600 bytes terminate it
   abnormally (exit 127). The overwriting bytes come from the file. jc-44 exits cleanly at every
   length tested, 260 through 60000.

   ⚠ **Be precise about which binary.** The first measurement of this was taken on a *dynamic* dev
   build, where 400 bytes was the segfault threshold; the shipped static build has a different frame
   layout and needs more. Both crash — but quoting the dev build's number as the release's was wrong,
   and it is the same class of overstatement `git blame` caught in the jc-40 README.

   It is reachable by an ordinary gesture. `tworld.c` runs this on the first positional argument
   whenever exactly one is given — which is what **dragging a `.tws` onto the executable** does, and
   what `docs/tworld2.6` documents. Solution files are passed between players constantly.

   The fix takes a buffer size, clamps to it, and terminates. `readsolution()` in the same file
   already clamped the **identical** record to 255 and terminated it; this path was simply forgotten.
   🔴 The size parameter is the fork's own addition and is the point of the change: a clamp alone
   fixes today's bug and leaves the next caller free to reintroduce it, because the old signature
   made the constraint invisible at the call site. `solution.h` had promised "up to 255" for years
   with nothing enforcing it.

   **(b) `readleveldata()` advanced a pointer by a file-supplied size and then dereferenced it**
   (`series.c`). The existing check covers only the upper map layer; the lower layer's 16-bit size
   was added to `data` unvalidated. Worst case — a zero upper layer, a 65535-byte lower layer and a
   13-byte record — is a two-byte read roughly 64 KB past a 13-byte allocation. Read-only, and the
   level is rejected moments later for having no password, but only after the wild read. Triggerable
   from any `.dat`.

   **(c) The lower map layer's RLE bounds check reserved two fewer bytes than the upper layer's**
   (`encoding.c`). Both layers are decoded by the same loop, and on a `0xFF` escape that loop reads
   `data[++n]` **twice** — so a layer whose last byte is `0xFF` reads two bytes past its declared
   end. The upper layer's guard happens to reserve exactly those two bytes, but only because it is
   reserving the *lower layer's own length word*; nothing connected the two. Fixing this also fixes
   the metadata size word read below it, which had the same overshoot without even needing the
   `0xFF`.

   ⚠ **(c) is NOT reachable from a `.dat` on disk**, and that was verified rather than assumed:
   `readleveldata()` runs first and ends in an unconditional gate rejecting any level whose decoded
   password is not exactly four characters, which guarantees several bytes of slack past anything the
   loop can touch. It is fixed anyway, because "safe" resting on an unrelated check in a different
   file is not a property that stays true.

   🔺 **The one input that bypasses that gate is `getenddisplaysetup()`** — the end-of-series screen,
   whose level is a static 139-byte array handed straight to the parser — **and it satisfies the
   stricter check with exactly ZERO margin** (121 + 16 + 2 = 139). One more byte of tightening would
   break the screen players see after finishing a set, and break it only there, months later.
   `test/encoding_test.c` pins that, rather than leaving it as arithmetic in a comment.

   ⚠ **The obvious form of the termination fix was not enough, and a test caught it.** Terminating
   the buffer once on entry looks sufficient and is not: `fileread()` is `fread(data, size, 1, fp)`,
   which on a truncated file returns FAILURE having already written up to `size-1` bytes into the
   buffer — overwriting the terminator that was just placed there — and then jumps to the error exit.
   The buffer is therefore terminated on entry AND at both failure labels. The case that found this
   was written from the review comment describing the hazard, and failed on the first run.

   **Verified.** Each fix was reverted individually and the suite re-run: removing the clamp turns
   the canary case red (and crashes the test binary), and reverting the `encoding.c` guard turns its
   case red. Two new suites — `test/encoding_test.c` (10 cases) and `test/series_test.c` (7) — plus
   four new cases in `test/solution_test.c` cover both directions, because a hardening change that
   also rejects valid input is not a fix but a different bug. The suite went from 16,944 to 17,015
   unit checks.

   **On the corpus.** A full 303-set batch verification was **not** run — that collection is not on
   this machine — and it is not the right instrument anyway, because no engine code changed. Three
   things stand in for it, the last of which is conclusive.

   *Empirically:* a scanner evaluated the old and the new `encoding.c` guard against all 903 levels
   in the seven bundled sets. **Zero** would newly be rejected.

   *Algebraically — and this is the real answer:* **the two new guards are the same inequality.**
   With `U` the upper-layer size and `L` the lower-layer size, `series.c`'s new check accepts only
   when `levelsize >= U + L + 14`, and `encoding.c`'s new check requires exactly `levelsize >=
   U + L + 14`. They ship together, so the stricter `encoding.c` guard cannot reject anything
   `readleveldata()` accepts — by construction, not by sampling.

   *And with margin:* any level that a PREVIOUS build accepted must have entered the optional-field
   loop at least once, which forces `levelsize >= U + L + 17`. So the new guard passes with three
   bytes to spare on every level any earlier release ever loaded.

   ⚠ An earlier draft of this note argued the same conclusion from the four-character password gate
   and put the guaranteed slack at nine bytes. That reasoning is sound but indirect, and the number
   was **wrong** — the field loop clamps its length, so a password field with no terminator passes
   and the true minimum is eight. The algebraic form above needs no password argument at all.
   `test/series_test.c` still pins the password gate, because other things lean on it.

   **Deliberately NOT fixed here:** one of `mslogic.c`'s trap-wiring dereferences reads a map cell
   through a position that `readpos()` only partly validates, so a malformed wiring can index past
   `map[]`. One byte, read-only, and almost certainly still mapped memory. Every other consumer of
   those wirings is already guarded and `lxlogic.c` sanitizes both endpoints up front, so this is one
   missed call site rather than a systemic gap.

   It is held back because unlike the three above it is **engine behavior**, and the obvious guard is
   the dangerous kind: positions past the end of the map are *load-bearing here* — `POS_INVALID` and
   the MSCC row-32 cloner glitch both deliberately produce them (see mod 2 and `encoding.c`). A naive
   `pos < CXGRID * CYGRID` test at that call site would quietly change emulation behavior, which is
   exactly what a full solution-corpus run exists to catch. That run has to come first.

   ⚠ The exact triggering byte pattern is deliberately not written down here. `SECURITY.md` asks
   researchers to disclose privately so a fix can ship first, and this file should not do the
   opposite for a defect that is still open.

## Testing

**`run-tests.ps1` at the repository root is the entry point**, and it runs two layers:

| Layer | Script | Needs | What it covers |
|---|---|---|---|
| unit | `test/run-tests.ps1` | a C compiler | one module at a time: the RNG, the `.tws` codec, the MS engine, the keyboard arbitration |
| end-to-end | `test/run-e2e.ps1` | a built executable | the real program's GUI-free command line, including a batch verification of a synthesized level set |

As of 2026-09-04: **10 unit runs / 17,015 checks and 12 end-to-end cases / 35 checks, 0 failures.**
Machine-readable results with `-ResultsPath test-results` (JUnit XML + JSON). Coverage, unit layer
only, is measured by `coverage.ps1`: **28.2% of branches overall**, from 100% of `random.c` down to
0% of `fileio.c` (which is compiled in but never called by any case). `verify-defaults.ps1` checks
that the stock `tw_settings.ini` in `package.ps1` still declares every setting `settings.cpp` does —
it did not, for two releases.

The full brief for anyone working here, including the traps that make a test *lie*, is
[`CLAUDE.md`](CLAUDE.md); the deliberate decisions are recorded in [`docs/adr/`](docs/adr/).

⚠ **Three of these tests were written, reviewed, passed, and pinned nothing** — the row-32 cloner
case used an x value where the fixed and broken code agree, the creature-position case asserted only
"not the aliased value" when both implementations satisfy that, and the diagonal round trip used gaps
too small to reach the encoding it was written to protect. All three were caught by planting the
defect and watching the suite stay green. **Prove a new test can fail before believing it.**

### The jc-43 input tests, in detail

`package.ps1` runs the unit layer first and refuses to package if it
fails (`-SkipTests` overrides). Every test is built **twice**, as C and as C++, because `generic/in.c`
is compiled as C by the SDL build and as C++ by the shipped Qt build (through `generic/_in.cpp`,
which is nothing but `#include "in.c"`), and a construct valid in only one of them would break a build
nobody here runs by hand. A test declares extra compiler flags in a `TESTFLAGS:` comment.

- `test/dirinput_test.c` — the pure arbitration in `generic/dirinput.c`, compiled directly against the
  real `defs.h` so it sees the same `Cmd*` values the game does.
- `test/input_test.c` — the real `input()`, driven end to end against `test/stub/oshwbind.h`. This is
  the only place the scan loop, the `KS_*` classification and the press stamping are covered; the pure
  tests cannot reach them, because two keys on one axis have already been collapsed by the time
  `resolvedirections()` runs. ⚠ Deliver synthetic key events from **inside** the `eventupdate` stub,
  never before calling `input()` — `input()` runs `resetkeystates()` first, and `joystick_trans` maps
  `KS_STRUCK` to `KS_OFF`, so events injected beforehand are retired before the scan sees them and the
  block-slap cases return a plausible-looking single direction and prove nothing.

Batch verification over the full corpus -- all 303 recorded solution sets, 286 MS and
17 Lynx, 18,734 valid solutions -- is byte-for-byte identical to jc-42, as expected:
`doturn()` ignores its `cmd` argument entirely whenever `state.replay >= 0`, and
`batchmode` never turns joystick behavior on, so `input()` is never called during a
verification run.

Both suites were checked against pre-change `HEAD`, where 21 of the 40 `input_test` checks fail with
exactly the reported symptoms (`hold Left, press Right` → `W`; sub-cycle tap → `E`, no diagonal).

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
