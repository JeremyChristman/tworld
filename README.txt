==============================================================================
  Tile World  --  Jeremy Christman's fork                    build jc-38
==============================================================================

  1. What this is
  2. What you need to run it
  3. What is in this download
  4. Getting started
  5. tw_settings.ini -- how the file works
  6. tw_settings.ini -- every setting, one by one
  7. Revision history: what each build changed and what it accomplished
  8. License and credits


------------------------------------------------------------------------------
1. WHAT THIS IS
------------------------------------------------------------------------------

Tile World is an emulator for the original Chip's Challenge. It plays CC1 level
sets under both the MS and the Lynx rulesets, records and replays solutions as
.tws files, and keeps a scorecard of your best times.

This download is a FORK of Tile World, maintained by Jeremy Christman.

  Upstream project (the original this fork is built on):
      https://github.com/SicklySilverMoon/tworld

  This fork:
      https://github.com/JeremyChristman/tworld

WHAT THE FORK IS ACTUALLY FOR. Tile World and SuperCC are the two emulators
serious Chip's Challenge players use, and for years they disagreed: a solution
recorded in one would desync in the other, usually within a few seconds, on
levels nobody could explain. Every disagreement is a bug in somebody's emulation
of MS behavior. This fork closed them, one at a time, by finding the exact rule
each engine applied and making Tile World match the reference.

  When the work started, 135 solutions replayed correctly in SuperCC and
  desynced in Tile World. As of build jc-32 that count is ZERO, across a
  collection of 274 level sets, with no release ever costing a level that
  replayed before.

Most of the revision history below is that project. The rest is quality of life:
a build tag, a choosable background color, a settings file of its own, and an
About box that says whose build this actually is.

The engine fixes are ALL ON BY DEFAULT and each has a compile-time escape hatch
(-DNO_FIX_*) for anyone who wants the old behavior back.


------------------------------------------------------------------------------
2. WHAT YOU NEED TO RUN IT
------------------------------------------------------------------------------

A 64-bit version of Windows. That is the whole list.

This is a portable build: no installer, no registry entries, no dependencies to
download. Qt and SDL are linked into the executable, which is why it is large
(about 24 MB). The two DLLs beside it are the only external pieces.

There is nothing to uninstall -- delete the folder and it is gone.


------------------------------------------------------------------------------
3. WHAT IS IN THIS DOWNLOAD
------------------------------------------------------------------------------

    Tile World.exe      The program.
    tw_settings.ini     Its settings file, with stock defaults.
    zlib1.dll           Required. Keep it next to the exe.
    libzstd.dll         Required. Keep it next to the exe.
    README.txt          This file.
    COPYING             The GNU General Public License, version 2.

NOT included: level sets, and the res/ and data/ folders the game needs to run.
This zip is the PROGRAM, meant to be dropped into an existing Tile World
installation -- see the next section.

  >> UPGRADING? TWO THINGS BEFORE YOU EXTRACT.
     1. tw_settings.ini REPLACES save\settings in this build. Read the
        upgrading note in section 4 first, or you will start with defaults.
     2. If you already have a tw_settings.ini, DO NOT let the extraction
        replace it -- the copy in this zip is the stock one, and it would
        discard your settings. Extract somewhere else and copy the exe and
        the DLLs across by hand.


------------------------------------------------------------------------------
4. GETTING STARTED
------------------------------------------------------------------------------

  +------------------------------------------------------------------------+
  |  UPGRADING FROM jc-32 OR EARLIER? READ THIS FIRST.                      |
  |                                                                         |
  |  Settings used to live in a file called "settings", with no extension,  |
  |  inside the save\ folder. From jc-33 they live in tw_settings.ini next  |
  |  to the executable instead, and the old file is NOT read or converted.  |
  |                                                                         |
  |  If you do nothing, the game starts with default settings: default      |
  |  window background, default ruleset, no remembered level set, and the   |
  |  volume back at its default.                                            |
  |                                                                         |
  |  TO KEEP YOUR SETTINGS: open both files in Notepad and EDIT THE VALUES  |
  |  IN PLACE. tw_settings.ini already contains every setting, so for each  |
  |  line in your old save\settings, find the same name in the new file and |
  |  change its value to match.                                             |
  |                                                                         |
  |  DO NOT paste your old lines in as a block. If a name ends up in the    |
  |  file twice, the LAST one wins -- so a pasted block above the existing  |
  |  lines is silently overridden by them, and the duplicate disappears the |
  |  first time the game exits. Editing in place cannot go wrong that way.  |
  |                                                                         |
  |  Nothing is deleted -- your old save\settings is left exactly where it  |
  |  is, so this is always recoverable. Your SOLUTIONS are not affected at  |
  |  all: they are .tws files and they stay in save\ where they always were.|
  +------------------------------------------------------------------------+

WHERE TO PUT THESE FILES

Tile World needs a res\ folder (its graphics and fonts), a data\ folder and a
sets\ folder (level sets), and a save\ folder (your solutions). This zip does
not include them, so:

  * ALREADY HAVE TILE WORLD? Copy Tile World.exe, the two DLLs, README.txt and
    tw_settings.ini into that folder, replacing the old exe. Read the upgrading
    note above first.

  * STARTING FRESH? Get a full Tile World distribution from the upstream
    project, then copy these files over its executable.

Put the folder somewhere you can write to -- your Documents or a games folder.
Installing under C:\Program Files is a bad idea: Windows blocks writes there,
and since tw_settings.ini now sits beside the executable, the game would not be
able to save your settings.

RUNNING IT

Double-click Tile World.exe. Pick a level set and play. Useful keys:

    arrow keys      move
    backspace       pause
    Ctrl+R          restart the level
    tab             replay your saved solution
    s               the score list (Games > Scores)
    p / n           previous / next level
    escape          leave the level
    Help > Keys     the full key list

USEFUL COMMAND LINE OPTIONS

Tile World accepts arguments, which is handy for testing:

    "Tile World.exe" -l                       list the level sets it can see
    "Tile World.exe" NAME.dat-ms.dac 42       jump straight to level 42
    "Tile World.exe" -p NAME.dat-ms.dac 42    ...even without the password
    "Tile World.exe" -r ...                   read-only: never rewrite a .tws
    "Tile World.exe" -b -r NAME.dat-ms.dac    batch-verify every solution
    "Tile World.exe" -S "PATH\save"           use a specific save folder
    "Tile World.exe" -h                       the full list

Pass -r whenever you are only checking something. Without it the game may
rewrite the .tws file, and with it your solution files are never touched.


------------------------------------------------------------------------------
5. tw_settings.ini -- HOW THE FILE WORKS
------------------------------------------------------------------------------

It is a plain text file of "name=value" lines, grouped under [Section] headings.
Edit it in Notepad or any text editor. Tile World reads it ONCE at startup, so
a hand edit takes effect the NEXT TIME YOU START THE GAME.

Five things to know:

  (1) THE SECTION HEADINGS ARE DECORATION. They exist to make the file readable.
      A setting works the same wherever it sits, so moving a line between
      sections changes nothing. (This is the opposite of SuperCC's
      succ_settings.ini, where sections are part of a setting's identity.)

  (2) SPACES AROUND THE "=" ARE FINE. "volume=8" and "volume = 8" are the same
      setting. Leading and trailing spaces are trimmed from both the name and
      the value, so a stray space at the end of a line cannot break anything.

      A name that appears TWICE is not an error, and the LAST one wins. Worth
      knowing when you are editing by hand -- see the upgrading note in
      section 4.

  (3) COMMENTS START A LINE WITH ";" OR "#". There are no end-of-line comments:
      everything after the "=" is the value, so

          legacyscores=true   ; nice and roomy

      has the value "true   ; nice and roomy", which is not "true", and the
      setting stays OFF.

  (4) TILE WORLD REWRITES THIS FILE when it exits normally. Your values are all
      preserved -- including any setting this build does not recognize, which is
      rewritten under an [Other] heading rather than dropped -- but your
      comments and your blank lines are not. Keep notes somewhere else.

  (5) DO NOT EDIT IT WHILE TILE WORLD IS RUNNING. It holds the settings in
      memory and writes them back on exit, which would overwrite your edit.
      Close the game, edit, then start it again.

If the file is missing, Tile World starts with defaults and writes a fresh one
when it exits, so deleting it is a safe way to reset your settings.

Be aware that the regenerated file is SHORT. Tile World writes back only the
settings it is actually holding, and most of them are recorded only once you
change them -- so a file it writes from scratch may contain little more than
the level set you last opened. That is not a fault: a missing line means "use
the default", which is exactly what you asked for by deleting the file. If you
want the fully populated file back as a starting point for editing, copy
tw_settings.ini out of the download again.


------------------------------------------------------------------------------
6. tw_settings.ini -- EVERY SETTING, ONE BY ONE
------------------------------------------------------------------------------

This is the complete stock file:

    [Display]
    bgcolor=#285080
    deathcount=0
    displayccx=1
    forceshowtimer=0
    legacyscores=false
    showbuildtag=false
    showdeathcounter=false
    showinitstate=0

    [Game]
    ignorepasswords=false
    selectedruleset=2
    selectedseries=

    [Sound]
    volume=10

ON/OFF SETTINGS TAKE "true" OR "1". Anything else -- "false", "0", a typo, a
missing line, a missing file -- means OFF. Nothing switches itself on by
accident.


[Display]
-----------

bgcolor         The window background color.
                Values:  "#rrggbb", or an English color name like "darkorange".
                Default: #285080 (the stock Tile World blue)
                Set it in the game with Options > Background Color..., which
                shows a full color picker with live preview and writes this
                line for you; Options > Restore Default Background puts it
                back. The whole window retints, including the shades derived
                from this color; the level view, the level list and the green
                highlight are deliberately left alone. Text automatically
                flips between white and black for contrast. Garbage falls back
                to the stock blue.

deathcount      Your lifetime death total, the number the death counter shows.
                Values:  any whole number from 0 to 999999999.
                Default: 0
                This is the counter's storage, not a switch -- it only appears
                on screen when showdeathcounter is on. The game rewrites this
                line every time it changes, so it survives closing the game.
                Safe to edit by hand while the game is NOT running; if it is
                running, your edit is overwritten the next time the number
                changes. Garbage, a negative number and a missing line all read
                as 0. Options > Set Death Counter... does the same thing from
                inside the game, and Options > Reset Death Counter sets it to 0.

displayccx      Whether to show the story text ("CCX") that some level sets
                include between levels.
                Values:  1 to show it, 0 to hide it.
                Default: 1
                Toggled in the game under the Options menu.

forceshowtimer  Whether to show the clock on levels that have no time limit.
                Values:  1 to always show it, 0 for only timed levels.
                Default: 0
                Toggled in the game under the Options menu.

legacyscores    Whether the score list (Games > Scores) uses the appearance
                Tile World had in version 2.2.
                Values:  true or 1 turns it on. ANYTHING ELSE IS OFF.
                Default: false
                Version 2.3 rebuilt Tile World on a newer version of the Qt
                toolkit, and the score list's look changed with it rather than
                by intent: the column headers went from individually boxed
                cells to one flat strip, the font lightened, and the rows
                tightened up. Turn this on to get the older look back --
                boxed headers, bold text, roomier rows.
                Only the SCORE LIST changes. The level-set picker, the
                solution-file list and the help pages keep today's appearance.
                An honest caveat: this reproduces the 2.2 look, it does not
                resurrect Qt 4. It is a close match, not a pixel-for-pixel one.

showbuildtag    Whether the title bar shows which build of this fork you are
                running.
                Values:  true or 1 turns it on. ANYTHING ELSE IS OFF.
                Default: false -- off, including when the key is absent or the
                         file does not exist.
                On:   Tile World [jc-33] - CCLP5 - Lesson Zero
                Off:  Tile World - CCLP5 - Lesson Zero
                Opt-in on purpose: a version number is useful while the fork is
                being worked on and just noise otherwise. Turning it off does
                not hide which build you have -- the tag is still a string
                inside the executable.

showdeathcounter  Whether the message bar shows a running lifetime death total.
                Values:  true or 1 turns it on. ANYTHING ELSE IS OFF.
                Default: false -- off, including when the key is absent or the
                         file does not exist.
                Toggled in the game under Options > Death Counter, which also
                reveals Reset Death Counter and Set Death Counter... beneath it.
                The total itself is the deathcount setting above.

                It reads "Deaths: 12" in the small black bar under the hint box,
                bottom right. That bar is shared with other messages, and the
                counter takes priority over all of them EXCEPT "(paused)" and
                "Verifying ...", which replace it until they are done. Note that
                while the counter is on you will not see the volume readout, the
                stepping readout, or -- if you play with the volume at 0 -- the
                written-out sound effects ("Bummer", "Chack!", and the rest),
                because those all share the same bar.

                WHAT COUNTS AS A DEATH. Every way Chip can die: monsters, water,
                fire, bombs, being squashed by a block, and running out of time.
                Restarting a level that is IN PROGRESS also counts, whether you
                use Ctrl+R or Level > Restart -- giving up on a run is a run you
                lost. Restarting after you have already died does NOT count
                again, and neither does replaying a level you just solved,
                leaving a level with Escape, switching levels, or watching a
                solution play back.

                The total is shared across every level set, and it is kept in
                this file, so it is per-installation: two copies of the game
                keep two separate totals, and if this folder is synced between
                two computers they will overwrite each other's number.

showinitstate   Whether to show the initial random state of a level, a detail
                that matters when hunting for the best possible solution.
                Values:  1 to show it, 0 to hide it.
                Default: 0
                Toggled in the game under the Options menu.


[Game]
--------

ignorepasswords Whether passwords are ignored, letting you open any level in a
                set without its password and without having beaten the one
                before it.
                Values:  true or 1 turns it on. ANYTHING ELSE IS OFF.
                Default: false
                Toggled in the game with Options > Ignore Passwords, which
                writes this line for you the moment you click it. The change
                takes effect immediately -- no restart, no reopening the set.
                Two things change while it is on:
                  * Every level in the set is reachable. Next/previous level,
                    the score list and the level list stop hiding what you have
                    not unlocked.
                  * Ctrl+G asks for a LEVEL NUMBER instead of a password, and
                    jumps straight there.
                Melinda's offer to skip a level you keep dying on does not
                appear while this is on -- with every level already reachable
                there is nothing for it to grant.
                It does NOT touch your solution files. In particular it does
                not record the passwords of levels you visit this way, so
                turning it back off really does lock the set again rather than
                leaving everything permanently unlocked. Your solved levels,
                times and scores are untouched either way.

selectedruleset Which ruleset the game starts in.
                Values:  1 = Lynx, 2 = MS.
                Default: 2
                The game writes this when you pick a ruleset for a set. Note
                that a set's own file decides which rulesets it offers.

selectedseries  The level set to reopen at startup.
                Values:  a level set filename, e.g. "CCLP5.dat-ms.dac".
                Default: empty (start at the level-set picker)
                The game writes this itself when you open a set, so you
                normally never touch it.


[Sound]
---------

volume          Sound volume.
                Values:  0 (silent) to 10 (loudest).
                Default: 10
                Adjust it in the game with the volume control; it writes this
                line for you.


[Other]
---------

Not a real section. If a settings file contains a name this build does not
recognize -- one from a newer version, or a typo -- it is kept and written back
under this heading instead of being thrown away. If you find something here you
did not put there, it is probably a misspelling of a setting above.


------------------------------------------------------------------------------
7. REVISION HISTORY
------------------------------------------------------------------------------

Every release is tagged jc-N in the repository, and that tag is what the title
bar shows when showbuildtag is on. Newest first.

Where a release says "N levels", that is the number of recorded solutions that
replayed incorrectly before the fix and correctly after it, measured across a
collection of 274 level sets and roughly 22,000 solutions. "0 regressions"
means no solution that replayed correctly before stopped doing so -- every
release below was measured that way before shipping.


jc-38  --  The death counter is white
--------------------------------------

  * The death counter now reads in WHITE, where it was previously the same dark
    red as everything else in that bar. Accomplishes: the running total is told
    apart at a glance from the messages that share the bar with it -- it is a
    standing readout, not a notification, and it no longer looks like one.

  * Messages are unchanged: still bright red while new, dark red as they age.
    The bar's palette now carries three text colors instead of two, one of them
    reserved for the counter.

  * The counter was already centered in the bar and still is.

  * No behavior change of any kind: nothing about what counts as a death, what
    the counter does, or where it is stored is different from jc-37.


jc-37  --  Death counter
-------------------------

  * NEW SETTING AND MENU ITEMS: Options > Death Counter. Turn it on and the
    small black bar under the hint box, bottom right, shows a running lifetime
    death total -- "Deaths: 12". Two more items appear beneath it while it is
    on: Reset Death Counter, and Set Death Counter... for typing in a number.
    Accomplishes: a count of how many times Chip has died, kept across sessions.
    OFF BY DEFAULT, so nothing changes for anyone who does not turn it on.

  * The total is stored in tw_settings.ini as deathcount, so it survives closing
    the game and can be edited by hand. It is shared across all level sets and
    belongs to the installation, not to a save file: two copies of the game keep
    two separate totals, and a folder synced between two computers will have the
    two machines overwrite each other's number.

  * WHAT COUNTS. Every way Chip can die -- monsters, water, fire, bombs, being
    squashed by a block, and running out of time -- under BOTH rulesets.
    Restarting a level that is in progress counts too, by Ctrl+R or by
    Level > Restart, on the reasoning that giving up on a run is a run you lost.

  * WHAT DOES NOT COUNT, and this is the part that took the care: restarting
    after you have ALREADY died. Every way out of the "Oops" prompt restarts the
    level -- R, Ctrl+R, and Space alike -- so counting restarts naively would
    have scored two deaths for every one. The counter is incremented only from
    inside the live game loop, which the post-death prompt is not part of.
    Replaying a level you just solved, leaving with Escape, changing levels, and
    watching a solution play back all correctly count for nothing.

  * DETECTION does not use the death SOUND, which looks like the obvious signal
    and is not one: under Lynx, drowning and bombs play their own sound and a
    timeout plays none, and under MS a timeout plays the time-out sound instead.
    A counter built on that would have missed drownings in Lynx entirely. It
    uses the game loop's own "this level ended badly" result, which every death
    in both engines goes through.

  * SHARING THE BAR. That bar already carried other messages, so the counter has
    a priority rule: it replaces anything with a timeout -- the volume readout,
    the stepping readout, and the written-out sound effects you see when the
    volume is 0 -- but it steps aside for "(paused)" and "Verifying ...", which
    stay until they are finished. That exception is not cosmetic: under Lynx the
    board is not blanked when you pause, so "(paused)" in that bar is the ONLY
    sign the game is stopped, and a counter that covered it would have made a
    paused game look like a running one.

  * The number is capped at 999999999. At 17 characters "Deaths: 999999999"
    fits the bar comfortably -- it already displays 19-character strings. It
    stops there rather than wrapping round to a negative number, and that same
    ceiling applies to the Set Death Counter... box and to a value edited into
    the file by hand. Nonsense in the file -- a negative number, letters, an
    empty value, a missing line -- reads as 0.

  * If tw_settings.ini exists but cannot be opened, the counter is hidden rather
    than shown as 0. The stored total is unavailable in that state, and a
    confident wrong zero that then gets thrown away at exit is worse than
    showing nothing.

  * ALSO FIXED, unrelated to the counter but next to it: the key list in section
    4 of this README was wrong. It said "p" paused (it goes to the previous
    level), that backspace restarted the level (it pauses), and that Ctrl+R
    replayed your solution (it restarts the level; tab replays). It also pointed
    at "? or F1" for the full key list, which are not bound in this build --
    that list is under Help > Keys.


jc-36  --  The grand total on the score screen stopped being cut off
--------------------------------------------------------------------

  * FIXED: on the Games > Scores screen, the last line -- "Total Score" and the
    grand total itself -- was chopped off partway through. On a large set it was
    unreadable: Joshie's total showed as "443,47" with the next digit sliced in
    half, when the real number is 443,476,450, and the label read "Total S".
    Accomplishes: the number the whole screen exists to show can be read.

  * WHY IT HAPPENED. The score table describes its own layout, and two of its
    entries are written to run across more than one column: "Total Score" is
    meant to occupy the Level and Name columns together, and the grand total is
    meant to occupy Base, Bonus and Score together. The Qt front end read those
    widths and then discarded them, drawing each string inside its first column
    only. A level's own base score fits in the Base column; a whole set's total
    does not, so it was clipped at the column edge. The command-line score dump
    (-s) never had the bug, which is why the two disagreed.

  * The columns are unchanged. Widths are still measured from the ordinary
    one-value-per-column rows, so Level stays narrow and Base stays sized to a
    level's score, exactly as before -- the long lines simply stop being cut.

  * KNOWN, AND LEFT ALONE FOR NOW: with legacyscores off, the total line is
    still a little taller than the lines above it. That is a separate old
    quirk, not new here, and the obvious fix for it would have started
    truncating long level names on sets over 1000 levels -- a worse problem
    than a tall line. It is written up in FORK.md. The 2.2 style, which uses a
    fixed line height, never showed it.

  * Present in both score-screen styles, so the fix applies to legacyscores
    on and off alike. It also repairs the two other multi-column lines the
    score table can produce: *BAD* markers on a replaceable solution, and the
    name of a level you have not solved.

  * The Find box still works on the fixed lines: filter the list and the total
    keeps its full width wherever it lands.


jc-35  --  Ignore Passwords
----------------------------

  * NEW SETTING AND MENU ITEM: Options > Ignore Passwords. Turn it on and every
    level in a set is reachable -- no password, no need to have beaten the
    level before it.
    Accomplishes: browsing, testing and revisiting sets stops being a chore.
    Tile World could already do this, but only as a command-line flag (-p) that
    you had to remember on every launch and could not see or change from inside
    the program. Now it is a checkbox that remembers itself.

  * WHILE IT IS ON, Ctrl+G ASKS FOR A LEVEL NUMBER instead of a password, and
    jumps straight to that level. Asking for a password would be a question
    with no useful answer when nothing is locked.

  * It takes effect the instant you click it -- no restart, no reopening the
    set -- and it is saved to tw_settings.ini as ignorepasswords immediately.

  * It does NOT write to your solution files. Levels you visit this way are not
    recorded as "password known", so turning the option back off genuinely
    re-locks the set instead of leaving it permanently open. Solved levels,
    times and scores are untouched. (This is deliberately unlike -p, which does
    record -- acceptable for a flag you opt into each launch, not for a saved
    setting.)

  * See section 6 for the full description.


jc-34  --  An About box that tells the truth about this build
-------------------------------------------------------------

  * HELP > ABOUT NOW IDENTIFIES THIS AS A FORK, names the build it is running,
    and sends bug reports to this fork's issue tracker.
    Accomplishes: the previous text was the upstream project's, unchanged. It
    credited only the original authors and pointed bug reports at their tracker
    -- so a problem caused by a change made HERE went to people who did not
    write it, could not reproduce it, and had to work out that the binary was
    not theirs. They keep the credit; this fork now takes the blame.

  * The About box states that these changes were written with AI assistance
    (Claude, by Anthropic). Nobody has to guess how the work was done.

  * All three URLs in the About box are clickable -- this fork, the upstream
    project, and the issue tracker.

  * The last hardcoded upstream URL left in the source -- the one in the
    "internal error: failed sanity check" crash message -- now points here too.
    That message is compiled out of this release build (it only exists in debug
    builds), so it changes nothing you can see; it is fixed so that the rule
    "this fork's problems come to this fork" has no exceptions left.

  * The build tag now lives in one place, fork.h, instead of being typed
    separately into the program and the packaging script.
    Accomplishes: a release can no longer ship a binary whose About box or
    title bar names a different build than the download it came in.

  * The clock no longer loses time while the About box is open -- the same
    modal-dialog fix jc-32 made for the color picker.


jc-33  --  Its own settings file, and the 2.2 score list back
------------------------------------------------------------

  * SETTINGS NOW LIVE IN tw_settings.ini, next to the executable, instead of in
    a file named "settings" inside save\. The old file is not read or converted;
    see the upgrading note in section 4.
    Accomplishes: the settings file ships WITH the download, so every setting is
    visible and editable without hunting for an extension-less file inside a
    save folder. It also stops colliding with SuperCC, which lives in the same
    folder here and reads succ_settings.ini.
    The file gained [Section] headings for readability, tolerates spaces around
    "=", accepts ";" and "#" comments, and preserves any setting it does not
    recognize instead of dropping it.

  * NEW SETTING: legacyscores. The score list can be drawn the way Tile World
    2.2 drew it -- boxed column headers, bold text, roomier rows. Off by
    default. See section 6 for what it does and does not restore.

  * THE BUILD TAG IS NOW OFF BY DEFAULT. Previously a fresh install showed
    "[jc-N]" in the title bar until you found the setting.
    Accomplishes: people this is handed to never see a build number they have
    no use for.

  * THIS README, shipped in the download and updated with every release.


jc-32  --  Two fixes to the color picker
----------------------------------------

  The clock no longer loses time while the background-color dialog is open: a
  modal dialog stops the game loop, and the missed ticks were being burned off
  back-to-back afterwards. Measured on a 10-second dialog: 13 seconds of clock
  lost before, 3 after. Also stopped duplicating the stock blue in two places.

  A review finding that was WRONG and is recorded so nobody re-fixes it: keys
  typed into the dialog do NOT leak into the game as moves. Verified by typing
  arrow keys into the picker -- Chip moves zero pixels.


jc-31  --  Choose your own background color
-------------------------------------------

  Options > Background Color... opens a full color picker with live preview
  (Cancel undoes it), and Options > Restore Default Background puts the stock
  blue back. Saved as bgcolor in the settings file the instant it is chosen.
  The whole window retints: the stock look is one blue plus five shades derived
  from it, and the derivation is reproduced for whatever color you pick, so
  nothing looks half-painted. Rendering at the default is pixel-identical to
  jc-30 -- 0 of 673,360 pixels differ. Text flips white or black for contrast.


jc-30  --  The build tag became switchable
------------------------------------------

  Added showbuildtag so the "[jc-N]" tag in the title bar could be turned on and
  off without rebuilding. (In jc-33 its default flipped to off.)


jc-29  --  A keyboard move cancels a pending mouse move
-------------------------------------------------------

  Changes nothing about existing solutions, but it makes re-exporting a
  mouse-click solution safe.


jc-28  --  THE LAST DESYNC
--------------------------

  BlakeE1 #118 "Technical Difficulties" -- 1 fixed, 0 regressions. A mouse click
  made by a SLIDING Chip is scheduled between his own slide and everything
  else's, where a keyboard move goes after. The fix landed exactly where Tile
  World's own source said "not yet included". Only 19 of 22,927 solutions
  contain a click at all.

  With this release the desync count reached ZERO.


jc-27  --  Chip picks up an item while standing on a block
----------------------------------------------------------

  TCCLP #147 "Testing Lab" -- 1 fixed, 0 regressions. Chip collects a key or
  boots that sit on top of a block, and stands on the block rather than pushing
  it.

jc-26  --  Beartraps, finally
-----------------------------

  PB_Gourami #254 -- 1 fixed, 0 regressions. Closed the trap family, open since
  the beginning of the project and four failed attempts deep. Two rules, and
  neither works without the other: stepping into a trap from one of its own
  buttons shuts it, and a creature standing still on a button re-opens its trap
  every tick rather than only once.

jc-25  --  Fire refuses bugs and walkers
----------------------------------------

  3 levels, 0 regressions. Also proved an old theory wrong: a suspected bug in
  how the slip list is walked was measured at 0 fixed / 0 broken across 21,830
  levels, retiring an idea that had been carried for months.

jc-24  --  A teleport uncovered by pushing a block off it
---------------------------------------------------------

  JacquesS2 #7 "Slippertele" -- 1 fixed, 0 regressions. Nine attempts; the one
  that worked came from counting how often the suspect branch ran and finding
  the answer was zero.

jc-23  --  A push that fails because the block cannot leave
-----------------------------------------------------------

  TomR1 #100 -- 1 fixed, 0 regressions. Six attempts; the five rejected ones
  broke 34, 4, 2, 2 and 2 other levels.

jc-22  --  A blocked creature stops repainting its own tile
-----------------------------------------------------------

  2 levels, 0 regressions. One of them, RolfNewMSMaps #1, had been marked
  IMPOSSIBLE and turned out to be solvable.

jc-21  --  A Chip tile on a teleport landing square
---------------------------------------------------

  1 level, 0 regressions. Chip only -- a swimming Chip stays solid.

jc-20  --  A released tank in a beartrap no longer stalls forever
-----------------------------------------------------------------

  1 level, 0 regressions.

jc-19  --  Block movers keep their slip slot
--------------------------------------------

  1 level, 0 regressions. An extension of jc-17's rule to blocks.

jc-18  --  Three fixes found by instrumentation
-----------------------------------------------

  3 levels, 0 regressions: a teleport bounce drawing from the random number
  generator, the template a dead clone leaves behind, and a block buried under
  a creature being unpushable.

jc-17  --  Creatures keep their place in the slip list
------------------------------------------------------

  14 levels, 0 regressions -- the single biggest release of the project.

jc-16  --  The multiple tank glitch
-----------------------------------

  1 level, 0 regressions.

jc-15  --  Chip's starting square comes from the foreground only
----------------------------------------------------------------

  1 level, 0 regressions.

jc-14  --  Random force floors, Chip's half
-------------------------------------------

  5 levels, 0 regressions.

jc-13  --  Random force floors draw twice
-----------------------------------------

  6 levels. Knowingly cost one level (ZK-Adventure #304), which was Jeremy's
  call with the measurement in front of him.

jc-12  --  A bounced creature turns to face where it bounced
------------------------------------------------------------

  Eliminated a whole class of disagreements about which way something is facing.

jc-11  --  Monster list entries that name a Chip tile are ignored
-----------------------------------------------------------------

  1 level -- Jacques #1 "Welcome", the oldest unexplained desync in the
  collection, and a level-loading bug rather than a movement one.

jc-10  --  Blue buttons fire after the mover lands
--------------------------------------------------

  11 levels. Also recovered both levels that earlier releases had cost, so from
  this point no shipped release costs a level that replayed before.

jc-9   --  Buttons pressed during a teleport are deferred
---------------------------------------------------------

  9 levels. The ordering root cause, found with a new scheduling trace.

jc-8   --  Chip collects a key or boots sitting on a clone machine
------------------------------------------------------------------

  6 levels. Rides the same switch as jc-6.

jc-7   --  A tank may step off a clone machine
----------------------------------------------

  10 levels.

jc-6   --  Chip may walk onto a clone machine
---------------------------------------------

  19 levels -- the biggest single win of the project.

jc-5   --  Slide re-facing
--------------------------

  6 levels.

jc-4   --  Controller direction from stalled creatures
------------------------------------------------------

  11 levels.

jc-3   --  Broken teleports and blocks teleported onto Chip
-----------------------------------------------------------

jc-2   --  The MS row-32 cloner glitch
--------------------------------------

jc-1   --  The window title shows the level set
-----------------------------------------------

  The first build of this fork. The title bar shows which SET you are playing
  (and later the level too), where upstream showed only the level name. Also
  the point at which the build became a single self-contained executable
  instead of one needing 27 DLLs beside it.


UPSTREAM RELEASES

  2.3.1 (2025)  Input handling made more lax by default.
  2.3.0 (2024)  Rebuilt on Qt5 and SDL2 instead of Qt4 and SDL1 -- this is the
                release that changed how the score list looks, which
                legacyscores exists to undo. Also added author info from the
                level file and emulation of several MS glitches.
  2.2.0 (2015)  The version whose score list legacyscores reproduces.

  The full upstream history is in the Changelog file in the repository.


------------------------------------------------------------------------------
8. LICENSE AND CREDITS
------------------------------------------------------------------------------

Tile World is free software under the GNU General Public License, version 2.
The full text is in COPYING, included in this download. You may use, study,
share and modify it, and any version you pass on must carry the same freedoms
and its source.

Tile World was created by Brian Raiter, and has been maintained since by Eric
Schmidt, Michael Hansen, David Stolp, A Sickly Silver Moon, G lander, ChosenID
and others. This fork is a set of changes on top of their work, and none of the
above are responsible for it. Chip's Challenge is the property of its
respective owners; no game data is included in this download.

The changes in this fork were developed by Jeremy Christman together with
Claude, Anthropic's AI coding assistant. Jeremy owned the vision, direction,
and final decisions and the code was developed by Claude. That is said plainly
here, and in Help > About, so that nobody has to guess how the work was done.

Bug reports about THIS FORK belong at
https://github.com/JeremyChristman/tworld/issues -- please do not take them
upstream, since these changes are not theirs.
