/* mslogic_test.c: the MS ruleset engine, driven a tick at a time.
 *
 * MOD (Jeremy). This is the test that covers what this fork actually IS.
 * mslogic.c is 4,800 lines carrying more than eighty `MOD (Jeremy)` edits and
 * thirty-odd `NO_FIX_*` behavior toggles -- the entire jc-2 through jc-29 desync
 * project lives in this one file -- and until now nothing exercised a single
 * line of it outside a full GUI or a full batch verification over somebody's
 * private solution collection.
 *
 * It needs no CMake tree and no level set. mslogic.c links against exactly nine
 * symbols outside libc: random4, randomp3 and randomp4 (compiled in from
 * random.c), fileidtotileid (from encoding.c), and the warn_/die_/errmsg_ error
 * surface, stubbed below. logic.h's gamelogic struct is the whole driving
 * interface: initgame, advancegame, endgame, shutdown.
 *
 * THE TICK MODEL. One call to advancegame() is one tick. The MS ruleset gives
 * Chip a move every four ticks, so a test that expects motion must run at least
 * four. tick() below mirrors what doturn() in play.c does per tick, minus the
 * replay bookkeeping and the move recording -- currenttime counts up from zero
 * and currentinput carries the command.
 *
 * advancegame() returns +1 when the level has been won, -1 when it has been
 * lost, and 0 to keep going. Those are the same values doturn() passes up.
 *
 * ⚠ THIS FILE MUST NEVER BE MADE TO PASS BY CHANGING AN EXPECTATION. Every
 * assertion here is either the documented CC1 rule or a behavior this fork
 * deliberately changed and measured against a solution corpus. If one starts
 * failing, an engine change has altered replay behavior, and stored solutions
 * are what pay for it. Find the change; do not adjust the number.
 *
 * TESTLANG: c
 *
 * mslogic.c is compiled only as C by CMake, and it uses C constructs C++
 * rejects. See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-unused-value -Wno-unused-variable
 *
 * ⚠ BOTH -Wno FLAGS ARE FOR mslogic.c's OWN WARNINGS, NOT FOR THIS TEST. The
 * suite compiles with -Werror, and mslogic.c does not build warning-clean:
 *
 *   -Wunused-value    the _assert macro (mslogic.c:356) expands to
 *                     `((test) || (die(...), 0))`, and GCC objects to the comma
 *                     expression's right operand at four call sites.
 *   -Wunused-variable `value` is declared but unused in resetdata()
 *                     (mslogic.c:2648).
 *
 * Five warnings, all pre-existing and none of them a defect. They are suppressed
 * here rather than fixed in the shipped source because this is a fork tracking
 * upstream and every cosmetic edit is a diff to carry forever. The cost is real
 * and worth stating: -Wno-unused-value also stops the compiler pointing out a
 * discarded result in THIS file. If mslogic.c is ever cleaned up, delete these
 * flags rather than leaving them as cover.
 *
 * A third flag, -Wno-unused-function, used to be here and was REMOVED. It was
 * described as covering mslogic.c and in fact covered tw_fixture.h -- and what
 * it was hiding was that no test called fix_setbot() or fix_addtrap(), so the
 * lower map layer and optional field 4 were never exercised by anything. Both
 * now have cases below. Do not add it back to silence a warning; the warning is
 * telling you a fixture helper has no test behind it.
 */

#include	<stdarg.h>
#include	"tw_test.h"
#include	"tw_fixture.h"
#include	"tw_corpus.h"

/* The high-fidelity flag, declared extern in logic.h. Left FALSE: pedantic mode
 * changes several of the behaviors asserted below, and a test that silently ran
 * in the other mode would be measuring a configuration almost nobody plays.
 *
 * ⚠ This comment used to say the flag is "defined in tworld.c". It is not --
 * lxlogic.c:54 defines it, as tworld.c:96 itself points out. Defining it HERE
 * is correct only because lxlogic.c is not in this translation unit; the Lynx
 * test (test/lxlogic_test.c) must not, and gets a redefinition error if it
 * tries. Fixed when that test was written, which is what surfaced it.
 */
int	pedanticmode = 0;

#include	"../random.c"
#include	"../encoding.c"
#include	"../mslogic.c"

/* --- the error surface, stubbed --------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int warn_count = 0;
static int warn_offmap = 0;
static int errmsg_count = 0;

/* warn_offmap counts warnings whose FORMAT names an off-map position ("Off-map
 * trap opening attempted"). The wording, not the count, is what tells a bounds
 * guard firing from whatever the engine does after reading past the map. */
void warn_(char const *fmt, ...)
{
    ++warn_count;
    if (fmt && strstr(fmt, "ff-map"))
	++warn_offmap;
}
void errmsg_(char const *prefix, char const *fmt, ...)
{
    (void)prefix; (void)fmt; ++errmsg_count;
}
/* 🔴 PRINT THE MESSAGE. This used to be `(void)fmt; exit(1);`, and an engine
 * _assert firing therefore killed the run with no output at all -- the runner
 * reported "no-result" and there was nothing to read. Measured: disabling
 * FIX_CHIP_PICKUP_ON_BLOCK trips an assertion inside the engine, and finding out
 * WHICH one took a hand-compiled binary and a guess. mslogic.c's _assert expands
 * to `((test) || (die(...), 0))` and die() formats the file and line, so the
 * information exists and was being thrown away one stub short of the reader. */
void die_(char const *fmt, ...)
{
    va_list args;
    fprintf(stderr, "ENGINE ASSERTION: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

/* --- the harness ------------------------------------------------------ */

static gamestate	teststate;
static gamesetup	testsetup;
static gamelogic       *logic;
static int		ticknumber;

/* Stand up a game on the given level, exactly as play.c's initgamestate() does.
 * Returns FALSE if the level data was rejected.
 */
static int startlevel(fixlevel const *lv)
{
    int size;
    unsigned char *data;

    if (logic) {
	(*logic->shutdown)(logic);
	logic = NULL;
    }
    free(testsetup.leveldata);
    memset(&testsetup, 0, sizeof testsetup);

    data = fix_build(lv, &size);
    if (!data)
	return FALSE;

    testsetup.number = lv->number;
    testsetup.time = lv->time;
    testsetup.leveldata = data;
    testsetup.levelsize = size;

    logic = mslogicstartup();
    if (!logic)
	return FALSE;
    logic->state = &teststate;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_MS;
    teststate.replay = -1;
    teststate.currenttime = -1;
    teststate.timeoffset = 0;
    teststate.currentinput = NIL;
    teststate.lastmove = NIL;
    teststate.initrndslidedir = NIL;
    teststate.stepping = -1;
    teststate.statusflags = 0;
    teststate.soundeffects = 0;
    teststate.timelimit = lv->time * TICKS_PER_SECOND;
    /* initmovelist() lives in solution.c, which this test does not compile in
     * -- nothing here records moves, because state.replay stays -1 and the
     * recording happens in doturn(), not in the engine. An empty list is what
     * the engine sees either way. */
    teststate.moves.list = NULL;
    teststate.moves.count = 0;
    teststate.moves.allocated = 0;
    /* A FIXED seed, not resetprng()'s clock-derived one. Two runs of this suite
     * must produce identical results, or a case that depends on a random draw
     * fails one time in four and gets dismissed as flaky. */
    restartprng(&teststate.mainprng, 12345);

    ticknumber = -1;
    if (!expandleveldata(&teststate))
	return FALSE;
    return (*logic->initgame)(logic);
}

/* One tick. Mirrors doturn() minus replay and move recording. */
static int tick(int cmd)
{
    teststate.currenttime = ++ticknumber;
    teststate.currentinput = (short)cmd;
    return (*logic->advancegame)(logic);
}

/* Run ticks until the game ends or the budget runs out. Returns the last
 * advancegame() result, so 0 means "still running after n ticks". */
static int runticks(int n, int cmd)
{
    int i, r = 0;
    for (i = 0 ; i < n ; ++i) {
	r = tick(cmd);
	if (r)
	    return r;
    }
    return r;
}

/* Chip's position, through mslogic.c's OWN accessor.
 *
 * ⚠ Not teststate.creatures[0]. The gamestate's creature list is the one the
 * renderer walks; the engine's working list is mslogic.c's file-scope
 * `creatures` array, and `getchip()` is `creatures[0]` there. Reading the
 * gamestate copy gave position 0 for every case below while the engine was in
 * fact moving Chip correctly -- the water, fire and exit cases passed the whole
 * time, which is what showed the accessor rather than the engine was wrong.
 * Using the module's own macro means these helpers cannot drift from it.
 */
static int chipx(void) { return chippos() % CXGRID; }
static int chipy(void) { return chippos() / CXGRID; }

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * test/fuzz/corpus/mslogic/ replayed through the engine, so a libFuzzer finding
 * on Linux becomes a permanent regression case everywhere (docs/adr/0011). The
 * input format is the fuzz target's: a move-count byte, a move stream, then the
 * raw level record.
 *
 * 🔴 THIS IS THE ONLY LAYER THAT REACHES THE ENGINE WITH A HOSTILE LEVEL. The
 * parser targets stop at "the file was refused"; jc-45 was a file that was
 * ACCEPTED and then dereferenced out of bounds inside initgame(). That is the
 * class this replay keeps pinned.
 *
 * ⚠ A green run proves these inputs still load and play without crashing,
 * hanging or tripping an _assert -- ASan in the `fuzz` job is the memory
 * oracle. `enginecorpus_ran` stops it passing without the engine having run.
 */
static int enginecorpus_replayed = 0;
static int enginecorpus_ran = 0;

static void enginecorpus_run(twcorpusinput const *in)
{
    gamelogic	       *lg;
    unsigned char      *level;
    int			movecount, levelsize, i, t, tk;

    if (in->size < 3)
	return;
    movecount = in->data[0] & 0x3F;
    if (1 + movecount >= in->size)
	return;
    levelsize = in->size - 1 - movecount;

    level = (unsigned char *)malloc((size_t)levelsize);
    if (!level)
	return;
    memcpy(level, in->data + 1 + movecount, (size_t)levelsize);

    if (logic) {
	(*logic->shutdown)(logic);
	logic = NULL;
    }
    free(testsetup.leveldata);
    memset(&testsetup, 0, sizeof testsetup);
    testsetup.number = 1;
    testsetup.leveldata = level;
    testsetup.levelsize = levelsize;

    lg = mslogicstartup();
    if (!lg) {
	free(level);
	testsetup.leveldata = NULL;
	return;
    }
    logic = lg;
    logic->state = &teststate;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_MS;
    teststate.replay = -1;
    teststate.currenttime = -1;
    teststate.timeoffset = 0;
    teststate.currentinput = NIL;
    teststate.lastmove = NIL;
    teststate.initrndslidedir = NIL;
    teststate.stepping = -1;
    teststate.statusflags = 0;
    teststate.soundeffects = 0;
    teststate.timelimit = 0;
    teststate.moves.list = NULL;
    teststate.moves.count = 0;
    teststate.moves.allocated = 0;
    restartprng(&teststate.mainprng, 12345);

    if (expandleveldata(&teststate) && (*logic->initgame)(logic)) {
	static int const cmds[5] = { NIL, CmdNorth, CmdWest, CmdSouth, CmdEast };
	tk = -1;
	for (i = 0 ; i < movecount ; ++i) {
	    int cmd = cmds[in->data[1 + i] % 5];
	    for (t = 0 ; t < 4 ; ++t) {
		teststate.currenttime = ++tk;
		teststate.currentinput = (short)cmd;
		if ((*logic->advancegame)(logic))
		    goto done;
	    }
	}
    }
  done:
    ++enginecorpus_ran;
}

static void enginecorpus_report(twcorpusverdict v, char const *name)
{
    ++enginecorpus_replayed;
    CHECK_MSG(v == TW_CORPUS_OK, "mslogic corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

int main(void)
{
    fixlevel lv;
    int i, r;
    int warn_before;

    tw_begin("mslogic");
    tw_expect_atleast(308);

    /* ================================================================== */
    tw_case("every committed mslogic fuzz corpus input still plays");
    {
	char dir[256];
	int c;

	CHECK_MSG(tw_corpus_dir("mslogic", dir, sizeof dir),
		  "could not find test/fuzz/corpus/mslogic from the working"
		  " directory -- the replay would have proved nothing");
	if (dir[0]) {
	    c = tw_corpus_run(dir, enginecorpus_run, enginecorpus_report);
	    CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	    CHECK_INT(enginecorpus_replayed, c);
	    CHECK_MSG(enginecorpus_ran == c,
		      "the engine ran on only %d of %d corpus inputs",
		      enginecorpus_ran, c);
	}
    }

    tw_case("a synthesized level loads, and its header reaches the state");
    fix_init(&lv);
    fix_border(&lv);
    lv.time = 100;
    lv.chips = 3;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 8, 5, FIX_ICCHIP);
    CHECK_INT(startlevel(&lv), TRUE);
    CHECK_INT(teststate.chipsneeded, 3);
    CHECK_INT(teststate.timelimit, 100 * TICKS_PER_SECOND);
    CHECK_INT(chipx(), 5);
    CHECK_INT(chipy(), 5);

    /* ================================================================== */
    tw_case("Chip walks east across floor, one cell every four ticks");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    CHECK_INT(startlevel(&lv), TRUE);
    CHECK_INT(chipx(), 5);
    r = runticks(4, CmdEast);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 6, "after four ticks of East, Chip is at x=%d, wanted 6", chipx());
    CHECK_INT(chipy(), 5);
    r = runticks(4, CmdEast);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 7, "after eight ticks of East, Chip is at x=%d, wanted 7", chipx());

    /* ================================================================== */
    tw_case("Chip walks in all four directions");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 10, 10, FIX_CHIP_SOUTH);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdNorth);
    CHECK_INT(chipy(), 9);
    runticks(4, CmdWest);
    CHECK_INT(chipx(), 9);
    runticks(4, CmdSouth);
    CHECK_INT(chipy(), 10);
    runticks(4, CmdEast);
    CHECK_INT(chipx(), 10);

    /* ================================================================== */
    tw_case("a wall refuses Chip, and he stays exactly where he was");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_WALL);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(20, CmdEast);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 5, "Chip walked through a wall to x=%d", chipx());
    CHECK_INT(chipy(), 5);

    /* ================================================================== */
    tw_case("water without flippers is fatal");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_WATER);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(40, CmdEast);
    CHECK_MSG(r == -1, "walking into water returned %d, wanted -1 (dead)", r);

    /* ================================================================== */
    tw_case("fire without fire boots is fatal");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_FIRE);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(40, CmdEast);
    CHECK_MSG(r == -1, "walking into fire returned %d, wanted -1 (dead)", r);

    /* ================================================================== */
    tw_case("reaching the exit wins the level");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_EXIT);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(40, CmdEast);
    CHECK_MSG(r == 1, "reaching the exit returned %d, wanted 1 (won)", r);

    /* ================================================================== */
    tw_case("collecting a chip decrements the counter");
    fix_init(&lv);
    fix_border(&lv);
    lv.chips = 2;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_ICCHIP);
    fix_settop(&lv, 7, 5, FIX_ICCHIP);
    CHECK_INT(startlevel(&lv), TRUE);
    CHECK_INT(teststate.chipsneeded, 2);
    runticks(4, CmdEast);
    CHECK_INT(teststate.chipsneeded, 1);
    runticks(4, CmdEast);
    CHECK_INT(teststate.chipsneeded, 0);

    /* ================================================================== */
    tw_case("🔴 a SPARE chip leaves the counter at zero, and the socket still opens");
    {
	/* mslogic.c's chip pickup is `if (chipsneeded()) --chipsneeded();`. Levels
	 * routinely hold more chips than they demand, and the guard is what keeps
	 * the surplus from driving the counter to -1 -- at which point the
	 * socket's `chipsneeded() != 0` gate (jc-51) shuts it for good and the
	 * level becomes unwinnable. An adversarial audit replaced the guard with
	 * `if (1)` and every layer stayed green: no case ever collected a chip
	 * the level did not need. */
	fix_init(&lv);
	fix_border(&lv);
	lv.chips = 1;
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 6, 5, FIX_ICCHIP);      /* the one the level needs */
	fix_settop(&lv, 7, 5, FIX_ICCHIP);      /* the spare */
	fix_settop(&lv, 8, 5, FIX_SOCKET);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(teststate.chipsneeded, 1);
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 6);
	CHECK_INT(teststate.chipsneeded, 0);
	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 7, "Chip did not reach the spare chip (x=%d); this case"
				" is testing nothing", chipx());
	CHECK_MSG(teststate.chipsneeded == 0,
		  "collecting a chip the level did not need left the counter at %d",
		  (int)teststate.chipsneeded);
	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 8,
		  "Chip, holding every chip the level needs, was refused by the socket"
		  " (x=%d)", chipx());
    }

    /* ================================================================== */
    tw_case("a level demanding 65,532 chips locks the socket, it does not die (jc-51)");
    {
	/* 🔴 chipsneeded is a SIGNED short (state.h:251) filled from an UNSIGNED
	 * 16-bit field in the .dat (encoding.c:187), so a level declaring 32768
	 * or more required chips arrives NEGATIVE.
	 *
	 * The two tests then disagreed: canmakemove()'s gate asked
	 * `chipsneeded() > 0` and let Chip onto the socket, and endmovement()'s
	 * Socket case asserts `chipsneeded() == 0` and called die() -- which in
	 * the shipped program EXITS THE GAME. A downloaded level could kill Tile
	 * World by putting Chip on a socket.
	 *
	 * Both are now `!= 0`. For every non-negative count the two are the same
	 * test, so no sane level changed. Found by the MS engine fuzz target; the
	 * reproducer is test/fuzz/corpus/mslogic/socket-negative-chipsneeded.
	 *
	 * ⚠ This case cannot fail by crashing politely -- if the fix regresses,
	 * die() runs and the whole test binary exits. That is still a detection
	 * (the runner reports a failed run) but there will be no per-case report,
	 * so do not go looking for a tidy assertion failure. */
	fix_init(&lv);
	fix_border(&lv);
	lv.chips = 65532;			/* -4 as a signed short */
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 6, 5, FIX_SOCKET);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.chipsneeded < 0,
		  "the fixture did not produce a negative chipsneeded (%d); this"
		  " case is testing nothing", (int)teststate.chipsneeded);
	r = runticks(16, CmdEast);
	CHECK_MSG(chipx() == 5,
		  "Chip entered a socket with a negative chip count, reaching"
		  " x=%d -- the gate and the assertion disagree again", chipx());
	CHECK_MSG(r == 0, "the level ended (r=%d) rather than simply locking"
			  " the socket", r);
    }

    tw_case("a socket refuses Chip until every chip is collected");
    fix_init(&lv);
    fix_border(&lv);
    lv.chips = 1;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_SOCKET);
    fix_settop(&lv, 5, 6, FIX_ICCHIP);
    CHECK_INT(startlevel(&lv), TRUE);
    /* The socket is shut: Chip has collected nothing yet. */
    runticks(12, CmdEast);
    CHECK_MSG(chipx() == 5, "Chip passed a shut socket, reaching x=%d", chipx());
    /* Collect the chip below him, then try again. */
    runticks(4, CmdSouth);
    CHECK_INT(teststate.chipsneeded, 0);
    runticks(4, CmdNorth);
    CHECK_INT(chipy(), 5);
    /* Four ticks, not eight: the socket vanishes when Chip enters it, so eight
     * would carry him through to x=7 and the assertion below would be reading
     * the cell past the one under test. */
    runticks(4, CmdEast);
    CHECK_MSG(chipx() == 6, "the socket stayed shut after the last chip; Chip is at x=%d", chipx());

    /* ================================================================== */
    tw_case("dirt becomes floor when Chip walks through it");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_DIRT);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdEast);
    CHECK_INT(chipx(), 6);
    /* Step back and forth: the dirt is gone, so the return trip is free. */
    runticks(4, CmdWest);
    CHECK_INT(chipx(), 5);
    runticks(4, CmdEast);
    CHECK_INT(chipx(), 6);

    /* ================================================================== */
    tw_case("a block can be pushed, and cannot be pushed into a wall");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_BLOCK);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdEast);
    CHECK_MSG(chipx() == 6, "Chip failed to push a block; he is at x=%d", chipx());

    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_BLOCK);
    fix_settop(&lv, 7, 5, FIX_WALL);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(20, CmdEast);
    CHECK_MSG(chipx() == 5, "Chip pushed a block into a wall; he is at x=%d", chipx());

    /* ================================================================== */
    tw_case("running out of time loses the level");
    fix_init(&lv);
    fix_border(&lv);
    lv.time = 1;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(TICKS_PER_SECOND * 2, CmdNone);
    CHECK_MSG(r == -1, "a one-second level ran out and returned %d, wanted -1", r);

    /* ================================================================== */
    tw_case("an untimed level does not run out");
    fix_init(&lv);
    fix_border(&lv);
    lv.time = 0;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(TICKS_PER_SECOND * 5, CmdNone);
    CHECK_MSG(r == 0, "an untimed level ended on its own, returning %d", r);

    /* ================================================================== *
     * The fork's own behavior. See docs/adr/0002.
     * ================================================================== */

    tw_case("a row-32 cloner wiring stays distinct from an invalid position");
    {
	/* The LOADING half of the MSCC row-32 cloner glitch (jc-2). A cloner
	 * wired to (x, 32) addresses one cell past the bottom of a 32-row map;
	 * in CHIPS.EXE that lands in the game's variable block, and levels were
	 * built on the behavior deliberately -- TLFC3's BLOCKED, REENTRY and
	 * THROUGH THE GATES.
	 *
	 * What this case actually pins is encoding.c's readpos(): the
	 * out-of-range marker used to be CXGRID*CYGRID, which is the SAME number
	 * a legitimate (0, 32) wiring produces, so the two were indistinguishable
	 * and row-32 wirings were thrown away with the genuinely broken ones.
	 * POS_INVALID now sits one row further out.
	 *
	 * ⚠ SCOPE, stated so nobody reads more into a green run than it earns.
	 * This half is UNCONDITIONAL -- readpos() carries no #ifdef. The part
	 * that NO_FIX_ROW32_CLONER actually guards is what happens when such a
	 * cloner FIRES (mslogic.c:2612-2844, 2869, 4464), and that is NOT covered
	 * here: building this file with -DNO_FIX_ROW32_CLONER still passes all 22
	 * cases, which was measured, not assumed. Covering the firing half needs
	 * a button, a clone machine and a creature template, and belongs in its
	 * own case. Listed as a known gap in CLAUDE.md. */
	int found = 0, invalid = 0;
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 10, 10, FIX_BUTTON_RED);
	fix_settop(&lv, 12, 12, FIX_CLONEMACHINE);
	/* 🔴 x = 0, NOT 12. The old out-of-range marker was CXGRID*CYGRID, and a
	 * legitimate (x, 32) wiring computes x + CYGRID*32 -- so old and new
	 * collide at EXACTLY ONE value of x: zero. Wiring this to (12, 32) gave
	 * 1036 under both the fixed and the broken code, and the case passed with
	 * POS_INVALID reverted to its pre-jc-2 value. Measured, not supposed. */
	fix_addcloner(&lv, 10, 10, 0, 32);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.clonercount == 1,
		  "expected exactly one cloner wiring, got %d", teststate.clonercount);
	for (i = 0 ; i < teststate.clonercount ; ++i) {
	    if (teststate.cloners[i].to == ROW32POS(0))
		found = 1;
	    if (teststate.cloners[i].to == POS_INVALID)
		invalid = 1;
	}
	CHECK_MSG(found, "the row-32 cloner wiring was discarded; %d cloner(s) survive",
		  teststate.clonercount);
	CHECK_MSG(!invalid,
		  "a (0,32) wiring was collapsed into POS_INVALID -- the two are"
		  " distinguishable only because POS_INVALID sits a row further out");
    }

    tw_case("an ordinary cloner wiring is kept, and a nonsense one is not");
    {
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 10, 10, FIX_BUTTON_RED);
	fix_settop(&lv, 12, 12, FIX_CLONEMACHINE);
	fix_addcloner(&lv, 10, 10, 12, 12);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.clonercount == 1,
		  "expected one cloner wiring, got %d", teststate.clonercount);
	if (teststate.clonercount == 1) {
	    CHECK_INT(teststate.cloners[0].to, 12 + CXGRID * 12);
	    CHECK_INT(teststate.cloners[0].from, 10 + CXGRID * 10);
	}
    }

    tw_case("an out-of-range trap wiring cannot reach past the map (jc-45)");
    {
	/* 🔴 THE jc-45 DEFECT, in the two shapes it actually takes.
	 *
	 * readpos() validates only the X byte of a coordinate pair, so a trap
	 * wiring's `to` can address anywhere from POS_INVALID (1056, one cell
	 * past map[]) to 8191, against a 1056-entry array. initgame()'s
	 * spring-the-traps loop dereferenced it without a bound.
	 *
	 * Both shapes are exercised because they are genuinely different:
	 *
	 *   to-x >= 32  -> readpos returns POS_INVALID. This is the shape that
	 *                  occurs in the wild: all 7 malformed trap wirings in
	 *                  the maintainer's 286 sets are of this kind (BHLS1
	 *                  #148, CheeseT1 #69, TCCLP2 #11, ZK2 #73). The read
	 *                  landed one cell past the array, inside msstate,
	 *                  which is why nobody ever saw it.
	 *   to-x <  32  -> readpos returns x + 32*y, up to 8191. Does not occur
	 *                  in that collection, but a downloaded .dat can carry
	 *                  it, and it is the one that reads far out of bounds.
	 *
	 * The assertion is BEHAVIORAL, not "it didn't crash": a level carrying
	 * the malformed wiring must play exactly like the same level without it.
	 * A guard that merely avoided the read but sprang a trap it should not
	 * have would pass a crash test and fail this one.
	 */
	int plain_x, plain_y, plain_r;

	/* The reference: no trap wiring at all. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	CHECK_INT(startlevel(&lv), TRUE);
	plain_r = runticks(40, CmdEast);
	plain_x = chipx();
	plain_y = chipy();

	/* Shape 1: to-x >= 32, the one real levels have. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 35, 70);
	CHECK_MSG(startlevel(&lv) == TRUE,
		  "a level with an out-of-range trap wiring failed to start");
	CHECK_MSG(teststate.trapcount == 1,
		  "expected the wiring to be retained, got trapcount %d", teststate.trapcount);
	if (teststate.trapcount == 1)
	    CHECK_MSG(teststate.traps[0].to == POS_INVALID,
		      "a to-x past the grid should read as POS_INVALID (%d), got %d",
		      POS_INVALID, teststate.traps[0].to);
	r = runticks(40, CmdEast);
	CHECK_MSG(r == plain_r && chipx() == plain_x && chipy() == plain_y,
		  "the malformed wiring changed play: Chip at (%d,%d) r=%d, expected (%d,%d) r=%d",
		  chipx(), chipy(), r, plain_x, plain_y, plain_r);

	/* Shape 2: to-x < 32 with a huge y -- the far out-of-bounds form. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 10, 255);
	CHECK_MSG(startlevel(&lv) == TRUE,
		  "a level with a far out-of-range trap wiring failed to start");
	if (teststate.trapcount == 1)
	    CHECK_MSG(teststate.traps[0].to == 10 + CYGRID * 255,
		      "a to-x inside the grid keeps the raw arithmetic: expected %d, got %d",
		      10 + CYGRID * 255, teststate.traps[0].to);
	r = runticks(40, CmdEast);
	CHECK_MSG(r == plain_r && chipx() == plain_x && chipy() == plain_y,
		  "the far-out-of-range wiring changed play: Chip at (%d,%d) r=%d, expected (%d,%d) r=%d",
		  chipx(), chipy(), r, plain_x, plain_y, plain_r);
    }

    tw_case("the out-of-range read is PROVEN not to happen, by poisoning it");
    {
	/* 🔴 THE CASE THAT ACTUALLY BITES. The two above do not.
	 *
	 * A behavioral test cannot catch this fix, and that was measured: with
	 * the guard removed, every case above still passes. Of course it does --
	 * the fix is a memory-safety fix whose whole point is that behavior does
	 * NOT change. The out-of-bounds read simply returns whatever byte
	 * happens to be there, and that byte happens not to be Block_Static.
	 *
	 * So make it Block_Static. `map[POS_INVALID]` is exactly `msstate` --
	 * verified by address comparison, and mapcell's top.id sits at offset 0,
	 * so the first byte of msstate IS the tile id the unguarded code would
	 * read. That first byte is `chipwait`, and initgame() does not assign it
	 * until well AFTER the trap loop.
	 *
	 * With the poison in place and no guard, the loop reads Block_Static,
	 * calls springtrap(button), and springtrap's OWN bound check then rejects
	 * the off-map trap and warns -- so warn_count becomes the detector. With
	 * the guard, springtrap is never reached and nothing warns.
	 *
	 * ⚠ This deliberately depends on the layout of `gamestate`. If `msstate`
	 * ever stops following `map`, or gains a different first member, this
	 * case stops testing what it says. It asserts the layout first so that it
	 * fails loudly rather than quietly becoming another green no-op.
	 */
	CHECK_MSG((void*)&teststate.map[POS_INVALID] == (void*)&teststate.msstate,
		  "map[POS_INVALID] no longer coincides with msstate -- this case's"
		  " poison lands somewhere else and proves nothing");

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 35, 70);          /* to-x >= 32 -> POS_INVALID */

	/* Poison the byte one cell past the map. startlevel() memsets only
	 * teststate.map, so this survives into initgame(). */
	teststate.msstate.chipwait = (unsigned char)Block_Static;
	/* ⚠ A DELTA, not `warn_count == 0`. warn_count is a running total that
	 * nothing resets, so an absolute assertion silently couples this case to
	 * every case above it: add one legitimately-warning case earlier and this
	 * goes red for an unrelated reason, and the natural "fix" is to delete the
	 * only oracle that catches this regression. It also made the guard-removed
	 * run report a SECOND, false failure below. */
	warn_before = warn_count;
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(warn_count == warn_before,
		  "the engine read one cell past the map: it saw the poisoned"
		  " Block_Static and tried to spring an off-map trap (%d new warning(s))",
		  warn_count - warn_before);

	/* And the guard must not have broken the ordinary case: a button wired
	 * to a real trap holding a real block still springs it. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_setbot(&lv, 7, 7, FIX_BEARTRAP);
	fix_settop(&lv, 7, 7, FIX_BLOCK);        /* a block sitting on the trap */
	fix_addtrap(&lv, 3, 3, 7, 7);
	teststate.msstate.chipwait = 0;
	warn_before = warn_count;
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.trapcount == 1,
		  "the in-range wiring was lost (trapcount %d)", teststate.trapcount);
	if (teststate.trapcount == 1) {
	    CHECK_INT(teststate.traps[0].from, 3 + CXGRID * 3);
	    CHECK_INT(teststate.traps[0].to, 7 + CXGRID * 7);
	}
	CHECK_MSG(warn_count == warn_before,
		  "a perfectly ordinary trap wiring produced %d new warning(s)",
		  warn_count - warn_before);
    }

    tw_case("a beartrap wiring is read at the right stride");
    {
	/* Field 4 is ten bytes per entry against field 5's eight, and nothing
	 * else in the suite emits one -- so until now the field-4 branch of
	 * expandmsdatlevel() was never executed by any test, despite
	 * tw_fixture.h documenting its layout in detail. Two entries, because a
	 * stride error is invisible with one. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_settop(&lv, 4, 9, FIX_BUTTON_BROWN);
	fix_settop(&lv, 20, 21, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 7, 7);
	fix_addtrap(&lv, 4, 9, 20, 21);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.trapcount == 2,
		  "expected two trap wirings, got %d", teststate.trapcount);
	if (teststate.trapcount == 2) {
	    CHECK_INT(teststate.traps[0].from, 3 + CXGRID * 3);
	    CHECK_INT(teststate.traps[0].to, 7 + CXGRID * 7);
	    CHECK_INT(teststate.traps[1].from, 4 + CXGRID * 9);
	    CHECK_INT(teststate.traps[1].to, 20 + CXGRID * 21);
	}
    }

    tw_case("the lower map layer is read, not just the upper one");
    {
	/* Every other case here leaves the lower layer as floor, so encoding.c's
	 * SECOND decode loop -- a copy of the first, and the one whose bounds
	 * check is the weaker of the two -- was never given anything to decode.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_setbot(&lv, 8, 8, FIX_GRAVEL);
	fix_setbot(&lv, 9, 8, FIX_WATER);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(teststate.map[8 + CXGRID * 8].bot.id, Gravel);
	CHECK_INT(teststate.map[9 + CXGRID * 8].bot.id, Water);
	CHECK_INT(teststate.map[8 + CXGRID * 8].top.id, Empty);
    }

    tw_case("a creature-list entry with x past the grid is refused, not aliased");
    {
	/* readpos() answers POS_INVALID when x >= CXGRID rather than computing
	 * x + CYGRID*y. The difference is not academic: the naive arithmetic
	 * ALIASES an out-of-range x onto an unrelated in-bounds cell, and
	 * SuperCC's equivalent bug -- the same one, in its own monster-list
	 * loader -- made three real levels fail to open at all (its jc-7). */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_addcreature(&lv, 40, 3);   /* x=40 is off a 32-wide grid */
	CHECK_INT(startlevel(&lv), TRUE);
	/* 🔴 ASSERT THE POSITIVE, not merely "it is not the aliased value".
	 * The old form checked only `crlist[i] != 40 + CXGRID*3` (136). Real
	 * readpos returns POS_INVALID (1056); the naive aliasing implementation
	 * this case exists to catch returns 104. Both satisfy "not 136", so
	 * replacing readpos with the aliasing form left every case passing --
	 * measured. Pinning the exact value is the only version that bites.
	 *
	 * The count is asserted separately, because if field-10 parsing regressed
	 * to producing nothing, the loop below would run zero times and a
	 * loop-only case would report success while proving nothing. */
	CHECK_MSG(teststate.crlistcount == 1,
		  "expected one creature-list entry, got %d", teststate.crlistcount);
	for (i = 0 ; i < teststate.crlistcount ; ++i)
	    CHECK_MSG(teststate.crlist[i] == POS_INVALID,
		      "an out-of-range creature position became %d; POS_INVALID (%d) was expected",
		      teststate.crlist[i], POS_INVALID);
    }

    tw_case("an in-range creature position is kept exactly");
    {
	/* The other half of the case above: a rejection test that also rejects
	 * valid input proves nothing, and nothing else here asserts that the
	 * creature list is read correctly at all. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_addcreature(&lv, 9, 4);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.crlistcount == 1,
		  "expected one creature-list entry, got %d", teststate.crlistcount);
	if (teststate.crlistcount == 1)
	    CHECK_INT(teststate.crlist[0], 9 + CXGRID * 4);
    }

    /* ================================================================== */
    tw_case("ice carries Chip to the first tile that is not ice");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    for (i = 6 ; i <= 10 ; ++i)
	fix_settop(&lv, i, 5, FIX_ICE);
    /* x=11 is ordinary floor, so that is where the slide must end. */
    CHECK_INT(startlevel(&lv), TRUE);
    /* One deliberate step east onto the ice, then NO further input at all --
     * anything past x=6 is the ice moving him, not the player. */
    runticks(4, CmdEast);
    r = runticks(60, CmdNone);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 11, "ice left Chip at x=%d; the ice ends at 10, so 11 was expected",
	      chipx());

    tw_case("ice against a wall REVERSES Chip rather than stopping him");
    /* This is the case that catches a plausible-looking "fix". Sliding into an
     * obstacle on ice does not halt the slide -- it turns it around, and Chip
     * travels all the way back. An implementation that simply stopped him at
     * the wall would look correct in every screenshot and would silently break
     * every level whose route depends on the bounce.
     *
     * Ice runs 6..12 with a wall at 13, so Chip slides east, reverses, slides
     * back west, and leaves the ice at x=5 -- the floor tile he started on. */
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    for (i = 6 ; i <= 12 ; ++i)
	fix_settop(&lv, i, 5, FIX_ICE);
    fix_settop(&lv, 13, 5, FIX_WALL);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdEast);
    r = runticks(120, CmdNone);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 5, "after the bounce Chip is at x=%d, wanted 5", chipx());
    CHECK_INT(chipy(), 5);

    /* ================================================================== */
    tw_case("a force floor moves Chip without input");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    for (i = 6 ; i <= 10 ; ++i)
	fix_settop(&lv, i, 5, FIX_SLIDE_EAST);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdEast);
    r = runticks(60, CmdNone);
    CHECK_INT(r, 0);
    CHECK_MSG(chipx() == 11, "the force floor left Chip at x=%d, wanted 11 (one past its end)",
	      chipx());

    /* ================================================================== */
    tw_case("a bomb is fatal");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_BOMB);
    CHECK_INT(startlevel(&lv), TRUE);
    r = runticks(40, CmdEast);
    CHECK_MSG(r == -1, "walking onto a bomb returned %d, wanted -1 (dead)", r);

    /* ================================================================== */
    tw_case("gravel is walkable, and a red button is walkable");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_GRAVEL);
    fix_settop(&lv, 7, 5, FIX_BUTTON_RED);
    CHECK_INT(startlevel(&lv), TRUE);
    runticks(4, CmdEast);
    CHECK_INT(chipx(), 6);
    runticks(4, CmdEast);
    CHECK_INT(chipx(), 7);

    /* ================================================================== */
    tw_case("a level with no Chip tile loads without crashing");
    /* Upstream does NOT reject this -- measured, not assumed. The level loads
     * and Chip ends up at cell 0. That is worth pinning: a .dat is a
     * third-party download, "no Chip anywhere" is a thing a corrupt or
     * hand-edited file really does, and the invariant that matters is that the
     * engine stays inside its own map rather than that it refuses the level.
     * If a future change makes this REJECT the level instead, that is a
     * defensible improvement -- but it should be a deliberate one, so the case
     * accepts either outcome and only insists on the safety property. */
    fix_init(&lv);
    fix_border(&lv);
    /* deliberately no Chip tile anywhere */
    r = startlevel(&lv);
    if (r == TRUE) {
	CHECK_MSG(chippos() >= 0 && chippos() < CXGRID * CYGRID,
		  "with no Chip in the level, Chip was placed off the map at %d", chippos());
	/* Must not crash or wander off the map with input applied. */
	runticks(20, CmdEast);
	CHECK_MSG(chippos() >= 0 && chippos() < CXGRID * CYGRID,
		  "Chip left the map, ending at %d", chippos());
    } else {
	CHECK_MSG(r == FALSE, "startlevel returned %d, which is neither TRUE nor FALSE", r);
    }

    /* ================================================================== */
    /* jc-57: the memory-safety bounds that had NO witness.
     *
     * 🔴 WHY THESE EXIST. An adversarial audit mutated twelve bound checks in
     * this file and ELEVEN survived every layer that runs on Windows -- unit,
     * golden and nofix -- including reverting jc-50 wholesale, this fork's own
     * headline defect. CLAUDE.md section 8.1 already contained the technique
     * that catches this class ("poison the out-of-bounds byte", called there
     * "the single most reusable idea in the file"); it had been applied to
     * exactly ONE of at least four analogous guards. These are the other three,
     * plus a direct test of the jc-50 helpers.
     *
     * ⚠ AND NOTE WHAT THE SANITIZE LAYER DOES AND DOES NOT DO. run-tests.ps1
     * -Sanitize (added the same build) catches an out-of-bounds read only where
     * a test EXECUTES it. Measured on the reverted jc-50: movelaw_creature is
     * trapped, because the committed fuzz corpus happens to drive that path
     * with a bad id -- and movelaw_block SURVIVES, because nothing did. A
     * sanitizer is an oracle, not coverage. Both halves are needed.
     */
    tw_case("jc-50: the movelaw helpers refuse an out-of-range tile id");
    {
	/* The direct test, which the engine-level cases below cannot be.
	 *
	 * movelaws[] is indexed by a cell's BOTTOM layer, which can hold a
	 * creature: ids run past the 64-entry array by up to 47 on 18% of real
	 * levels. The old read was undefined -- what it returned depended on
	 * what the linker placed after the array -- so there is no "correct old
	 * value" to preserve and the fix PICKS one: zero, meaning "this terrain
	 * refuses every direction", which is the honest answer for a cell whose
	 * bottom layer is not terrain at all.
	 *
	 * Asserting that chosen answer pins the decision itself, and calling
	 * these helpers directly means the case does not depend on finding a
	 * level that happens to route through them. With the guard removed the
	 * read is out of bounds and -Sanitize traps it here too. */
	CHECK_MSG(MOVELAWCOUNT == 64,
		  "movelaws[] is %d entries, not 64 -- the ids below were chosen"
		  " against 64 and may no longer be out of range", MOVELAWCOUNT);
	CHECK_INT(movelaw_block(MOVELAWCOUNT), 0);
	CHECK_INT(movelaw_creature(MOVELAWCOUNT), 0);
	/* 47 past the end: the worst case the jc-50 note measured. */
	CHECK_INT(movelaw_block(MOVELAWCOUNT + 47), 0);
	CHECK_INT(movelaw_creature(MOVELAWCOUNT + 47), 0);
	/* Negative too. `id` arrives as an int and nothing upstream promises
	 * it is unsigned. */
	CHECK_INT(movelaw_block(-1), 0);
	CHECK_INT(movelaw_creature(-1), 0);
	/* And the guard must not have broken the ordinary lookup: a real
	 * terrain id still reports what the table says. Wall refuses both. */
	CHECK_INT(movelaw_block(Wall), movelaws[Wall].block);
	CHECK_INT(movelaw_creature(Wall), movelaws[Wall].creature);
	CHECK_INT(movelaw_block(Empty), movelaws[Empty].block);
    }

    tw_case("a creature list position off the map is REFUSED, not indexed");
    {
	/* The bound at the crlist walk in initgame(). Dropping it survives the
	 * unit suite, the golden master AND the sanitize layer -- nothing was
	 * driving it with an off-map position, so there was no read to trap.
	 *
	 * The oracle is the WARNING the guard itself emits. That makes the
	 * mutation directly visible: remove the guard and the warning stops.
	 * A delta, not an absolute, for the reason given on the jc-45 case
	 * above -- warn_count is a running total nothing resets. */
	/* 🔴 AND THE ORACLE IS THE PHANTOM CREATURE, NOT THE WARNING. The
	 * obvious oracle -- "the guard warns, so count warnings" -- does not
	 * work here and looked like it did. Remove the guard and the very next
	 * check warns too, "no creature at location", from the out-of-bounds
	 * bytes it just read. Same count, different sentence: the first version
	 * of this case asserted `warn_count > warn_before`, passed, and killed
	 * nothing. Ask what only happens on the WRONG side of the bound.
	 *
	 * What only happens there is a creature getting BUILT out of memory
	 * past the map. map[POS_INVALID] is msstate (asserted below), whose
	 * first byte is chipwait, which initgame() does not assign until well
	 * after this loop -- so poisoning it with a monster id makes the
	 * unguarded path sail through iscreature() and allocate a creature that
	 * does not exist. creaturecount is mslogic.c's own file-scope total. */
	CHECK_MSG((void*)&teststate.map[POS_INVALID] == (void*)&teststate.msstate,
		  "map[POS_INVALID] no longer coincides with msstate -- this case's"
		  " poison lands somewhere else and proves nothing");

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	/* x=40 is off a 32-wide map: readpos folds it to POS_INVALID, which is
	 * exactly the shape a malformed .dat produces. */
	fix_addcreature(&lv, 40, 3);
	teststate.msstate.chipwait = (unsigned char)Tank;
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(creaturecount == 1,
		  "the engine built %d creature(s) from a level containing only"
		  " Chip: it read the poisoned monster id one cell past the map"
		  " and made a creature out of it", creaturecount);
	teststate.msstate.chipwait = 0;

	/* An ordinary creature position is asserted two cases above ("an
	 * in-range creature position is kept exactly"), deliberately not
	 * repeated here: a creature-list entry with no matching creature TILE
	 * warns for an unrelated reason, which would make a control here read
	 * as a failure of this guard. */
    }

    tw_case("row 32 is empty when the trap loop runs -- the premise of its bound");
    {
	/* 🔴 THIS CASE EXISTS BECAUSE A MUTATION SURVIVED AND THE RIGHT ANSWER
	 * TURNED OUT TO BE "IT IS EQUIVALENT", WHICH IS ONLY DEFENSIBLE IF
	 * SOMETHING CHECKS THE PREMISE.
	 *
	 * An audit turned initgame()'s `xy->to < CXGRID * CYGRID` into `<=` and
	 * nothing failed. The tempting reading is "another untested bound". It
	 * is not one. The map is `mapcell map[CXGRID * (CYGRID + 1)]` -- 1056
	 * entries -- so 1024..1055 is the row-32 area that exists ON PURPOSE and
	 * POS_INVALID is 1056, one past the end. Letting 1024 through therefore
	 * reads an IN-BOUNDS cell, and that cell is provably zero at this
	 * moment: play.c:117 memsets the whole array, expandmsdatlevel() memsets
	 * it again, encoding.c's decode loops are bounded to pos < 1024 so level
	 * data can never reach row 32, and the one thing that DOES give row 32 a
	 * live cell -- activaterow32cloner() -- runs during play, long after
	 * this loop. `cellat(1024)->top.id == Block_Static` is false either way.
	 *
	 * So the mutant is equivalent, and no test can kill it. What a test CAN
	 * do is pin the invariant the equivalence rests on, so that the day
	 * something starts writing row 32 before initgame, this says so instead
	 * of a memory-safety argument quietly going stale.
	 *
	 * ⚠ Do NOT "fix" this by bounding the play-time sites. The comment at
	 * mslogic.c:4605 is explicit: activatecloner() must stay unbounded or
	 * the row-32 glitch breaks outright, and that glitch is load-bearing. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 7, 7);
	CHECK_INT(startlevel(&lv), TRUE);
	{
	    int rowclean = TRUE;
	    for (i = CXGRID * CYGRID ; i < POS_INVALID ; ++i)
		if (teststate.map[i].top.id != 0 || teststate.map[i].bot.id != 0)
		    rowclean = FALSE;
	    CHECK_MSG(rowclean,
		      "row 32 (map[%d..%d]) is not empty after level load. The"
		      " initgame() trap bound's off-by-one is only harmless"
		      " BECAUSE it is empty -- re-examine mslogic.c:4617.",
		      CXGRID * CYGRID, POS_INVALID - 1);
	}
	/* And the array really is a row longer than the grid, which is what
	 * makes 1024 an in-bounds index rather than an overrun. */
	CHECK_INT((int)(sizeof teststate.map / sizeof *teststate.map), POS_INVALID);
    }

    /* ================================================================== */
    tw_case("🔴 two monster-list entries for one cell make TWO creatures, both alive");
    {
	/* FIX_STACKED_CREATURE_CULL (`mslogic.c:213`), unguarded until now.
	 *
	 * SuperCC's monster list is AUTHORITATIVE -- a creature exists because it
	 * is in the list, not because a tile says so. Tile World tied creatures
	 * to map tiles, so when two entries name the SAME cell (both engines duly
	 * create two creatures) and the first one moves off, the tile leaves with
	 * it and the second is left tile-less -- and was then culled.
	 *
	 * Measured on A_Strange_Journey#60 "DeathSwap", whose list names (27,13)
	 * twice and (27,11) twice: at t=5 SuperCC still has four tanks, Tile World
	 * had three.
	 *
	 * ⚠ COUNT THE LIVING, NOT THE LIST. creaturecount does not shrink -- the
	 * cull HIDES a creature rather than removing it -- so the oracle is how
	 * many are not hidden after the first one has moved off the shared cell.
	 */
	int	n, alive;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 2, 2, FIX_CHIP_SOUTH);
	fix_settop(&lv, 10, 10, 0x4F);		/* a Tank, facing east  */
	fix_addcreature(&lv, 10, 10);
	fix_addcreature(&lv, 10, 10);		/* the SAME cell, twice */
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(creaturecount == 3,
		  "two list entries for one cell must make two creatures plus"
		  " Chip; got %d", creaturecount);

	runticks(12, NIL);
	alive = 0;
	for (n = 0 ; n < creaturecount ; ++n)
		if (!creatures[n]->hidden) ++alive;
	CHECK_MSG(alive == 3,
		  "%d of %d creatures are still alive. The tank left tile-less when"
		  " its twin moved off the shared cell was CULLED -- the monster list"
		  " is what makes a creature exist, not the tile", alive, creaturecount);
	CHECK_MSG(!creatures[2]->hidden,
		  "the second entry for (10,10) was hidden; it is a real creature and"
		  " SuperCC still has it");
	CHECK_MSG(creatures[1]->pos != creatures[2]->pos,
		  "both tanks are still on the shared cell, so neither was ever left"
		  " tile-less and this case exercised nothing");
    }

    /* ================================================================== */
    tw_case("🔴 a Walker blocked by fire KEEPS its slip-list slot");
    {
	/* FIX_KEEPSLOT_FIRE (`mslogic.c:2103`), unguarded until now.
	 *
	 * The keep-or-drop predicate asks "would the terrain underneath have
	 * refused this move too?" and answers with movelaws[].creature -- a
	 * per-TILE mask with no notion of which creature is asking. SuperCC's
	 * canEnter is creature-type aware: FIRE refuses BUG and WALKER. So for a
	 * walker the entry guard fails outright, tryEnter never runs, `sliding`
	 * is never cleared, and the slider KEEPS ITS SLOT. Tile World saw
	 * movelaws[Fire] = {NWSE,NWSE,NWSE}, concluded the terrain permits, and
	 * dropped the slot instead. Measured on Jacques #922 at ct=834.
	 *
	 * ⚠ ONE SLIPPER PROVES NOTHING. Dropping a slot re-appends it at the END
	 * of the slip list, so with a single slider it lands back at index 0 and
	 * both forms agree. The reorder is the whole effect, so the list needs a
	 * SECOND slider for the walker to be reordered past.
	 *
	 * ⚠ AND THE DESTINATION NEEDS A CREATURE ON TOP OF THE FIRE. The clause
	 * lives inside `if (iscreature(floor))` -- it is the occupied-destination
	 * branch. Fire alone is not enough to reach it.
	 */
	int	n, walkerslot, ballslot;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 2, 2, FIX_CHIP_SOUTH);

	/* ⚠ THE CREATURES MUST WALK ONTO THE ICE, not start on it. Sliding
	 * begins when a creature ENTERS a slide tile, so one placed on ice at
	 * load time is never enlisted -- measured, slipcount stays 0 forever. */
	fix_settop(&lv, 10, 8, 0x5A);		/* a Walker, facing south */
	fix_addcreature(&lv, 10, 8);
	fix_settop(&lv, 10, 9, FIX_ICE);
	fix_settop(&lv, 10, 10, FIX_ICE);
	fix_settop(&lv, 10, 11, 0x46);		/* a Fireball on top ...    */
	fix_setbot(&lv, 10, 11, FIX_FIRE);	/* ... over FIRE            */

	fix_settop(&lv, 20, 18, 0x4A);		/* a Ball: the second slider */
	fix_addcreature(&lv, 20, 18);
	fix_settop(&lv, 20, 19, FIX_ICE);
	fix_settop(&lv, 20, 20, FIX_ICE);

	CHECK_INT(startlevel(&lv), TRUE);
	/* Tick 8 is when the walker meets the fire. Before that nothing is
	 * sliding yet; after it the list has drained again. */
	runticks(8, NIL);

	walkerslot = -1; ballslot = -1;
	for (n = 0 ; n < slipcount ; ++n) {
	    if (slips[n].cr->id == Walker) walkerslot = n;
	    if (slips[n].cr->id == Ball) ballslot = n;
	}
	CHECK_MSG(slipcount > 0,
		  "nothing is on the slip list at all, so this case proves nothing");
	CHECK_MSG(walkerslot >= 0,
		  "the Walker was DROPPED from the slip list when a fireball over FIRE"
		  " blocked it (list holds %d, ball at %d). FIRE refuses a Walker, so"
		  " the entry guard fails outright and the slider keeps its slot",
		  slipcount, ballslot);
    }

    tw_case("🔴 a Ball blocked by a creature over GRAVEL keeps its slip-list slot");
    {
	/* FIX_KEEPSLOT_OCCUPANT's CREATURE half -- the jc-17 rule itself. The
	 * predicate is two terms, `!(movelaw_creature(bot) & dir)` OR the fire
	 * clause, and the case above reaches only the second. Deleting the first
	 * survived every layer, because the toggle's committed witness (seed
	 * 487376) is a BLOCK slider: `#if defined(FIX_KEEPSLOT_BLOCK_OCCUPANT) &&
	 * defined(FIX_KEEPSLOT_OCCUPANT)` means switching OCCUPANT off switches
	 * the block half off too, so that witness proves the block half live and
	 * says nothing about this one.
	 *
	 * Same shape as the Walker case, for the same reasons: two sliders so a
	 * drop is visible as a reorder, a creature ON TOP of the destination so
	 * the occupied branch runs, sliders that WALK onto the ice. The terrain
	 * under the Fireball is GRAVEL, which refuses every monster -- the
	 * movelaws term, not the fire clause, is what must keep the slot. The
	 * Fireball is a bare tile, not on the monster list, so it never moves. */
	int	n, ballslot, gliderslot;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 2, 2, FIX_CHIP_SOUTH);

	fix_settop(&lv, 10, 8, 0x4A);		/* a Ball, facing south      */
	fix_addcreature(&lv, 10, 8);
	fix_settop(&lv, 10, 9, FIX_ICE);
	fix_settop(&lv, 10, 10, FIX_ICE);
	fix_settop(&lv, 10, 11, 0x46);		/* a Fireball on top ...     */
	fix_setbot(&lv, 10, 11, FIX_GRAVEL);	/* ... over GRAVEL           */

	fix_settop(&lv, 20, 18, 0x52);		/* a Glider: the second slider */
	fix_addcreature(&lv, 20, 18);
	fix_settop(&lv, 20, 19, FIX_ICE);
	fix_settop(&lv, 20, 20, FIX_ICE);

	CHECK_INT(startlevel(&lv), TRUE);

	/* Tick 6: both are sliding, the Ball at slot 0. This is the tick the
	 * Ball meets the Fireball, so the precondition is the whole setup. */
	runticks(6, NIL);
	CHECK_MSG(slipcount == 2 && slips[0].cr->id == Ball
		  && slips[1].cr->id == Glider,
		  "at tick 6 the slip list should hold the Ball then the Glider"
		  " (it holds %d) -- the case no longer sets up the collision",
		  slipcount);

	/* Tick 8, SWEPT rather than reasoned (ticks 1-16, all three builds): the
	 * correct engine keeps the Ball at slot 0, it bounces north, and the
	 * Glider gets its move and slides off the ice. Drop the Ball's slot and
	 * the Glider shifts DOWN into slot 0 -- the slot the iterator has just
	 * left -- so it is SKIPPED, and at tick 8 it is still on the ice while
	 * the Ball is gone: exactly the TomP2#56 shape the fix's header records.
	 * -DNO_FIX_KEEPSLOT_OCCUPANT and deleting the movelaws term both give
	 * that second picture, measured. */
	runticks(2, NIL);
	ballslot = -1; gliderslot = -1;
	for (n = 0 ; n < slipcount ; ++n) {
	    if (slips[n].cr->id == Ball) ballslot = n;
	    if (slips[n].cr->id == Glider) gliderslot = n;
	}
	CHECK_MSG(gliderslot < 0,
		  "the Glider is still sliding at slot %d on tick 8 -- it was SKIPPED"
		  " when the Ball blocked by a creature over GRAVEL dropped its slot"
		  " instead of keeping it", gliderslot);
	CHECK_MSG(ballslot == 0,
		  "the Ball is at slip slot %d on tick 8 (list holds %d); gravel"
		  " refuses it, so it must keep slot 0", ballslot, slipcount);
    }

    tw_case("🔴 a push that exposes a teleport pops Chip's old cell TWICE, as SuperCC does");
    {
	/* FIX_TELEPORT_STALE_FG's OWN code -- step 2, the second poptile(oldpos) --
	 * which its committed witness never proved. The TELEPORT pair shares seed
	 * 2294, and turning off either toggle gives one digest: prepush_destfloor
	 * is WRITTEN only under STALE_FG and READ by BROKEN_DYNAMIC, so switching
	 * STALE_FG off switches BROKEN_DYNAMIC off too, and the witness proves the
	 * shared override and nothing else. Measured: replacing the second pop
	 * with a no-op survived unit, sanitize, golden and all 18 witnesses.
	 *
	 * SuperCC captures the destination's top tile BEFORE the push, and when
	 * that was a block, pops the mover's old cell a second time (measured with
	 * shadow_poplayer.ps1 -- see the fix's header). So the observable is what
	 * Chip was standing ON: gravel under him survives one pop and not two.
	 * The teleport is covered by a block at load, so it is FS_BROKEN and only
	 * BROKEN_DYNAMIC's block-exposed override lets Chip through at all -- the
	 * one route by which a teleport is reached with a block as its old top.
	 * Swept: the correct engine teleports on tick 1 and leaves Empty; the
	 * no-op mutant teleports and leaves Gravel; -DNO_FIX_TELEPORT_STALE_FG
	 * never teleports. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_EAST);
	fix_setbot(&lv, 5, 5, FIX_GRAVEL);	/* what the second pop removes */
	fix_settop(&lv, 6, 5, FIX_BLOCK);
	fix_setbot(&lv, 6, 5, FIX_TELEPORT);	/* covered at load: FS_BROKEN */
	fix_settop(&lv, 20, 20, FIX_TELEPORT);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.map[6 + CXGRID * 5].bot.state & FS_BROKEN,
		  "the block-covered teleport was not flagged FS_BROKEN at load, so"
		  " this case no longer reaches the block-exposed override");
	tick(CmdEast);
	CHECK_MSG(chipx() == 20 && chipy() == 20,
		  "pushing the block off the teleport did not send Chip through it"
		  " (he is at %d,%d) -- the block-exposed override refused, or the"
		  " case no longer reaches the second pop", chipx(), chipy());
	CHECK_MSG(teststate.map[7 + CXGRID * 5].top.id == Block_Static,
		  "the block was not pushed to (7,5): its cell holds %02X",
		  teststate.map[7 + CXGRID * 5].top.id);
	CHECK_MSG(teststate.map[5 + CXGRID * 5].top.id == Empty,
		  "Chip's old cell holds %02X, not Empty: the gravel under him"
		  " survived, so it was popped ONCE -- SuperCC pops it twice when the"
		  " teleport was exposed by a push",
		  teststate.map[5 + CXGRID * 5].top.id);
    }

    tw_case("🔴 a block BOUNCING off ice onto a random force floor costs ONE draw");
    {
	/* FIX_RFF_DRAW_ONCE's BLOCK-AND-MONSTER half -- the jc-13 fix itself,
	 * credited with 6 desyncs -- which no automated layer guarded. The RFF
	 * pair shares seed 7572, and the consumer of rff_keepdir in
	 * startfloormovement() is compiled only under DRAW_ONCE, so switching
	 * DRAW_ONCE off silently disables CHIP_REARM as well: the witness is a
	 * CHIP scenario and proves the chip half. Measured: making this half's
	 * `rff_keepdir = ac ? keepdir : NIL` always NIL survived unit, sanitize,
	 * golden and all 18 witnesses.
	 *
	 * ⚠ THE DOUBLE DRAW IS NOT ON EVERY MOVE, and the first two fixtures
	 * written for this proved nothing because they assumed it was. A block
	 * sliding from one random force floor to another draws ONCE in every
	 * build. The second draw comes only from the re-arm after a SUCCESSFUL
	 * BOUNCE, and in this slip pass a bounce exists only on ICE: slide into a
	 * wall, bounce back the way you came -- and if that lands on a random
	 * force floor, entry draws once and the re-arm draws again. SuperCC draws
	 * once per move. So: one random force floor, ice north of it, a wall past
	 * the ice, walls either side, and a conveyor that returns the block
	 * whenever it slides back south.
	 *
	 * The oracle is the draw count itself, read off the PRNG: every tick is
	 * stepped with nextvalue() from its old value to its new one. Swept over
	 * 120 ticks: the correct engine never spends more than one draw in a
	 * tick; the half removed, and -DNO_FIX_RFF_DRAW_ONCE, spend two on every
	 * bounce, the first at tick 31. */
	unsigned long before, v;
	int y, t, p, draws, pos, prevpos = -1, bounces = 0, bouncedraws = -1;
	int maxdraws = 0;

	fix_init(&lv);
	fix_border(&lv);
	for (y = 12 ; y <= 23 ; ++y) {
	    fix_settop(&lv, 9, y, FIX_WALL);
	    fix_settop(&lv, 11, y, FIX_WALL);
	}
	fix_settop(&lv, 10, 12, FIX_WALL);
	fix_settop(&lv, 10, 13, FIX_ICE);
	fix_settop(&lv, 10, 14, FIX_SLIDE_RANDOM);
	for (y = 15 ; y <= 22 ; ++y)
	    fix_settop(&lv, 10, y, FIX_SLIDE_NORTH);	/* the conveyor */
	fix_settop(&lv, 10, 23, FIX_BLOCK);
	fix_settop(&lv, 10, 24, FIX_CHIP_NORTH);
	CHECK_INT(startlevel(&lv), TRUE);

	for (t = 1 ; t <= 120 ; ++t) {
	    before = teststate.mainprng.value;
	    tick(t <= 2 ? CmdNorth : NIL);
	    for (v = before, draws = 0 ; v != teststate.mainprng.value && draws < 16 ; ++draws)
		v = nextvalue(v);
	    if (draws > maxdraws)
		maxdraws = draws;
	    pos = -1;
	    for (p = 10 + CXGRID * 13 ; p <= 10 + CXGRID * 23 ; p += CXGRID)
		if (teststate.map[p].top.id == Block_Static
			|| (iscreature(teststate.map[p].top.id)
			    && creatureid(teststate.map[p].top.id) == Block))
		    pos = p;
	    if (prevpos == 10 + CXGRID * 13 && pos == 10 + CXGRID * 14) {
		++bounces;
		if (bouncedraws < 0)
		    bouncedraws = draws;
	    }
	    if (pos >= 0)
		prevpos = pos;
	}
	CHECK_MSG(bounces > 0,
		  "the block never bounced off the ice back onto the random force"
		  " floor in 120 ticks, so this case proves nothing");
	CHECK_MSG(bouncedraws == 1,
		  "the first bounce onto the random force floor cost %d draw(s);"
		  " SuperCC draws once per move", bouncedraws);
	CHECK_MSG(maxdraws == 1,
		  "some tick spent %d draws; a block on this field must never cost"
		  " more than one", maxdraws);
    }

    /* ================================================================== */
    tw_case("🔴 a block BURIED under a creature cannot be pushed");
    {
	/* FIX_CHIP_ONTO_BURIED_BLOCK (`mslogic.c:281`), unguarded until now.
	 *
	 * floorat() deliberately looks PAST a creature, so a monster standing on
	 * a block reports floor == Block_Static and Chip falls into the push
	 * branch -- Tile World shoved a block it could not even see. SuperCC
	 * judges the move on the background, where creatures are transparent and
	 * every entry term fails, so pushing is never reached.
	 *
	 * ⚠ A MONSTER ON PLAIN FLOOR MUST STILL ADMIT CHIP, and the second half
	 * of this case is not decoration: the fix refuses ONLY the buried-block
	 * case, and a version that refused every creature cell would break
	 * ordinary collisions everywhere while this case still passed.
	 *
	 * Measured on Jacques#513 "Error": a Teeth over a Block with water beyond
	 * it. Tile World pushed the block into the water and walked Chip in.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 6, 6, FIX_CHIP_SOUTH);
	fix_settop(&lv, 7, 6, 0x56);		/* a Teeth tile ...         */
	fix_setbot(&lv, 7, 6, FIX_BLOCK);	/* ... standing on a block  */
	CHECK_INT(startlevel(&lv), TRUE);

	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 6 && chipy() == 6,
		  "Chip moved to (%d,%d). A block buried under a creature cannot"
		  " be pushed, so the move east must be refused outright",
		  chipx(), chipy());
	CHECK_MSG(teststate.map[8 + CXGRID * 6].bot.id != Block_Static
		  && teststate.map[8 + CXGRID * 6].top.id != Block_Static,
		  "the buried block was shoved east to (8,6) -- this is the"
		  " Jacques#513 divergence");
    }

    /* ================================================================== */
    tw_case("🔴 a Chip tile BURIED under a monster is not where Chip starts");
    {
	/* FIX_CHIP_START_FOREGROUND (`mslogic.c:4525`), unguarded until now.
	 *
	 * SuperCC's io/LevelFactory.findMSPlayer scans the FOREGROUND backwards
	 * and falls back to position 0 if it finds no Chip tile; it never looks
	 * at the bottom layer. Tile World used to take Chip's start from a Chip
	 * tile buried under a monster-list creature, which starts him INSIDE that
	 * creature and ends the game on tick 1. DaveB2#3 "Where am I?" is exactly
	 * that level -- its only Chip tile is buried at (22,14) under a Teeth,
	 * SuperCC plays it for 204 ticks, Tile World did not survive one.
	 *
	 * ⚠ THE LEVEL MUST HAVE NO FOREGROUND CHIP AT ALL. With one anywhere,
	 * both forms find it and agree, and the case proves nothing. Hence no
	 * border and no Chip tile on top of anything: the only Chip in this level
	 * is the buried one, so the two forms disagree about the answer.
	 */
	int	buried;

	fix_init(&lv);
	fix_settop(&lv, 22, 14, 0x57);		/* a Teeth, facing east     */
	fix_setbot(&lv, 22, 14, FIX_CHIP_SOUTH);	/* Chip, buried under it */
	fix_addcreature(&lv, 22, 14);
	CHECK_INT(startlevel(&lv), TRUE);

	buried = 22 + CXGRID * 14;
	CHECK_MSG(chippos() != buried,
		  "Chip was started on the buried Chip tile at (22,14), inside a"
		  " Teeth. The foreground is the only layer that decides where he"
		  " starts");
	CHECK_MSG(chippos() == 0,
		  "with no foreground Chip anywhere, Chip falls back to position"
		  " 0; he is at %d (%d,%d)", chippos(), chipx(), chipy());
    }

    /* ================================================================== */
    tw_case("🔴 a key resting on a BLOCK is collected, and the block is not shoved");
    {
	/* FIX_CHIP_PICKUP_ON_BLOCK (`mslogic.c:1831`), and until now it was one
	 * of fourteen shipped engine fixes with NO automated guard of any kind.
	 *
	 * 🔴 AN ADVERSARIAL AUDIT DISABLED THIS EXACT FIX AND ALL SIX LAYERS
	 * STAYED GREEN -- unit, sanitize, e2e, qt, golden and nofix, with the
	 * golden master's 1,806 digests over 903 levels unmoved. Only
	 * test\run-corpus.ps1, over a private collection that exists on one
	 * computer and is wired into no CI job, noticed: TCCLP #147 stopped
	 * replaying. Reproduced here before this case was written.
	 *
	 * ⚠ WHY THE FUZZ MATRIX CANNOT FIND IT. nofix.c derives a PROFILE from
	 * each seed that lays out one interaction in the three cells east of
	 * Chip, and none of the twelve profiles places a key UNDER a block. The
	 * conjunction the fix needs -- key on top, Block_Static beneath, Chip
	 * adjacent and facing it -- is not in the generator's vocabulary, so the
	 * blank row in nofix-matrix.tsv was never going to fill however long the
	 * search ran. A named case expresses in four lines what a million seeds
	 * could not reach.
	 *
	 * THE RULE. Block_Static is the ONLY background tile for which a pickup
	 * changes the answer: SuperCC admits the move on the pickup and never
	 * consults the block, so the block stays put and is revealed when Chip
	 * steps off. Without the fix Tile World shoves it. Measured on TCCLP #147
	 * "Testing Lab", whose row 9 is keys on blocks.
	 */
	int	dest, beyond;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 6, 5, 0x64);		/* a blue key ...           */
	fix_setbot(&lv, 6, 5, FIX_BLOCK);	/* ... resting on a block   */
	CHECK_INT(startlevel(&lv), TRUE);

	dest = 6 + CXGRID * 5;
	beyond = 7 + CXGRID * 5;
	CHECK_INT(teststate.map[dest].top.id, Key_Blue);
	CHECK_INT(teststate.map[dest].bot.id, Block_Static);

	runticks(4, CmdEast);

	CHECK_MSG(chipx() == 6 && chipy() == 5,
		  "Chip did not step onto the key: he is at (%d,%d)",
		  chipx(), chipy());
	CHECK_MSG(possession(Key_Blue) == 1,
		  "Chip walked onto a key resting on a block and did not collect"
		  " it; he holds %d blue keys", possession(Key_Blue));
	CHECK_MSG(teststate.map[beyond].top.id != Block_Static,
		  "the block under the key was SHOVED east to (7,5) instead of"
		  " being left alone -- this is the TCCLP #147 divergence, and"
		  " the whole point of the fix");
	CHECK_MSG(teststate.map[dest].bot.id == Block_Static,
		  "the block should still be under Chip, revealed when he steps"
		  " off; the cell now holds %d", teststate.map[dest].bot.id);
    }

    /* ================================================================== */
    tw_case("🔴 Teeth move at HALF speed, and ordinary monsters do not");
    {
	/* `if (cr->id == Teeth || cr->id == Blob) { if ((currenttime() +
	 * stepping()) & 4) return; }` is the half-speed rule, and it had no test:
	 * the suite walks Chip around and never timed a monster at all.
	 *
	 * ⚠ ONE ASSERTION ABOUT AN ORDINARY MONSTER KILLS BOTH HALVES, which is
	 * why the Ball below is not padding. Invert either comparison and the
	 * condition becomes true for nearly everything -- `id != Teeth` holds for
	 * a Ball, `id != Blob` likewise -- so the slow path swallows every
	 * creature in the game. Timing a Ball is what notices; timing only the
	 * Teeth would leave the rule looking right while every monster crawled.
	 *
	 * ⚠ THE NUMBERS ARE 7 AND 3 OVER 32 TICKS, NOT 8 AND 4, and that is not
	 * slack in the case. A monster only picks a move on a tick where
	 * `currenttime() & 2` is clear, so its first move does not begin on tick
	 * zero; the span loses one move to that offset. 7 against 3 is the 2:1 the
	 * rule describes, measured rather than assumed, and either mutation moves
	 * one of the two numbers to the other.
	 */
	int	start, moved;

	/* A Ball runs straight until something stops it: full speed. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 2, 2, FIX_CHIP_SOUTH);
	fix_settop(&lv, 5, 10, 0x4B);			/* Ball, facing east */
	fix_addcreature(&lv, 5, 10);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(creaturecount, 2);
	start = creatures[1]->pos;
	runticks(32, NIL);
	moved = creatures[1]->pos - start;
	CHECK_MSG(moved == 7,
		  "a Ball moved %d cells in 32 ticks, wanted 7. If this is about"
		  " half of 7, the half-speed rule is catching creatures it was"
		  " never meant to", moved);

	/* Teeth chase Chip, and do it at half speed. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 20, 10, FIX_CHIP_SOUTH);
	fix_settop(&lv, 5, 10, 0x57);			/* Teeth, facing east */
	fix_addcreature(&lv, 5, 10);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(creaturecount, 2);
	start = creatures[1]->pos;
	runticks(32, NIL);
	moved = creatures[1]->pos - start;
	CHECK_MSG(moved == 3,
		  "a Teeth moved %d cells in 32 ticks, wanted 3 -- half the 7 a"
		  " full-speed monster manages over the same span", moved);
	CHECK_MSG(creatures[1]->pos > start,
		  "the Teeth did not chase Chip at all, so this case timed"
		  " nothing");
    }

    /* ================================================================== */
    tw_case("🔴 a beartrap holds Chip when shut, and lets him through when open");
    {
	/* endmovement()'s trap-entry block decides whether a creature stepping
	 * INTO a beartrap is caught or walks straight through:
	 *
	 *     if (floor == Beartrap) {
	 *         ... shut-behind check ...
	 *         if (istrapopen(newpos, oldpos)) cr->state |= CS_RELEASED;
	 *     }
	 *
	 * Skip that block and nobody is ever released, so an OPEN trap starts
	 * catching people -- which breaks every level that routes you through a
	 * trap held open by a button somewhere else.
	 *
	 * ⚠ THE CLOSED TRAP CANNOT TELL THE TWO APART. Chip is held either way:
	 * once because the trap is shut, once because the release never ran. Only
	 * the OPEN trap distinguishes them, and holding a trap open means putting
	 * something on its button -- istrapbuttondown() is "the button tile is
	 * covered", so a Block parked on it does the job.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 7, 5, FIX_BEARTRAP);
	fix_settop(&lv, 20, 20, FIX_BUTTON_BROWN);
	fix_addtrap(&lv, 20, 20, 7, 5);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(teststate.trapcount, 1);

	runticks(24, CmdEast);
	CHECK_MSG(chipx() == 7,
		  "with its button UNCOVERED the trap should have caught Chip at"
		  " x=7; he is at x=%d", chipx());

	/* Now the same level with a Block parked on the button, so the trap is
	 * open before Chip ever reaches it. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 7, 5, FIX_BEARTRAP);
	fix_settop(&lv, 20, 20, FIX_BLOCK);
	fix_setbot(&lv, 20, 20, FIX_BUTTON_BROWN);
	fix_addtrap(&lv, 20, 20, 7, 5);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(istrapbuttondown(20 + CXGRID * 20),
		  "the Block did not register as holding the brown button down,"
		  " so this half of the case proves nothing");

	runticks(24, CmdEast);
	CHECK_MSG(chipx() > 7,
		  "the trap was held open by a covered button and still caught"
		  " Chip: he is at x=%d", chipx());
    }

    tw_case("🔴 stepping on a button wired OFF THE MAP is refused by springtrap itself");
    {
	/* jc-45 bounded the wiring in initgame(), but that loop only declines to
	 * READ an off-map trap -- it leaves the wiring in place, so pressing the
	 * button during play still calls springtrap() with position 6407, and
	 * springtrap's own `pos >= CXGRID * CYGRID` test is the only thing
	 * between that and `cellat(6407)`. Nothing pressed such a button: an
	 * audit deleted that test and all seven layers stayed green. Chip walks
	 * over the button here; -Sanitize traps the read if the guard goes, and
	 * the plain pass sees the guard's own warning disappear. */
	int o0;
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 7, 5, FIX_BUTTON_BROWN);
	fix_addtrap(&lv, 7, 5, 7, 200);          /* x in range, y far off the map */
	o0 = warn_offmap;
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.trapcount == 1 && teststate.traps[0].to >= CXGRID * CYGRID,
		  "the off-map wiring did not load as off-map (count %d) -- the case"
		  " tests nothing", teststate.trapcount);
	CHECK_MSG(warn_offmap == o0,
		  "loading alone produced %d off-map warning(s); the button has not"
		  " been pressed yet", warn_offmap - o0);
	runticks(24, CmdEast);
	CHECK_MSG(chipx() > 7,
		  "Chip never crossed the button (he is at x=%d), so it was never"
		  " pressed", chipx());
	/* ⚠ AT LEAST once, not exactly: the button is pressed on every tick Chip
	 * stands on it, and this walk produced five. Without the guard there are
	 * none at all, which is the difference that matters. */
	CHECK_MSG(warn_offmap - o0 > 0,
		  "pressing an off-map-wired button produced no off-map warning --"
		  " springtrap() went on to read the map at position %d",
		  7 + CXGRID * 200);
    }

    /* ================================================================== */
    tw_case("🔴 a RELEASED tank in a beartrap is not stalled FOR GOOD");
    {
	/* FIX_TANK_IN_TRAP_STALL (`mslogic.c:2432`), unguarded until now.
	 *
	 * choosecreaturemove() ends with
	 *
	 *     if (cr->id == Tank) {
	 *         if ((cr->state & CS_RELEASED) ||
	 *             (floor != Beartrap && floor != CloneMachine))
	 *             cr->state |= CS_HASMOVED;
	 *
	 * The second clause exists precisely so a trapped tank keeps trying every
	 * tick. The CS_RELEASED clause in front of it overrides that, so a tank
	 * whose trap has been OPENED takes CS_HASMOVED on its first failed move
	 * and is skipped for good -- the only per-tick clear touches CS_TURNING
	 * creatures, and a stalled tank has none.
	 *
	 * 🔴 WHAT MAKES THIS OBSERVABLE, after three toggles were withdrawn for
	 * being intra-tick: the effect is a creature that never moves AGAIN. That
	 * survives any number of ticks, so it only needs the right trigger --
	 * released, blocked, and then UNBLOCKED:
	 *
	 *     trap open      a Block parked on the brown button holds it, and
	 *                    FIX_TRAP_REFRESH re-grants CS_RELEASED every tick
	 *     blocked        a Block sits on the tank's target cell
	 *     unblocked      Chip pushes that Block out of the way and steps aside
	 *
	 * With the fix the tank is still eligible and drives out of the trap; with
	 * it off the tank sat down at the first failed tick and stays there.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 8, 8, FIX_CHIP_NORTH);

	fix_settop(&lv, 7, 5, 0x4F);		/* a Tank, facing east */
	fix_setbot(&lv, 7, 5, FIX_BEARTRAP);
	fix_addcreature(&lv, 7, 5);
	fix_settop(&lv, 8, 5, FIX_BLOCK);	/* in its way, for now */

	fix_settop(&lv, 20, 20, FIX_BLOCK);	/* holds the trap open */
	fix_setbot(&lv, 20, 20, FIX_BUTTON_BROWN);
	fix_addtrap(&lv, 20, 20, 7, 5);

	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(creaturecount >= 2, "the tank was not created");
	CHECK_MSG(creatures[1]->id == Tank,
		  "creatures[1] is not the tank, so the assertions below read the"
		  " wrong creature");
	CHECK_MSG(istrapbuttondown(20 + CXGRID * 20),
		  "the Block is not holding the brown button down, so the tank is"
		  " never RELEASED and this case proves nothing");

	/* Let it fail against the Block first: that is the tick on which the
	 * unfixed build writes CS_HASMOVED and gives up. */
	runticks(8, NIL);
	CHECK_MSG(creatures[1]->pos == 7 + CXGRID * 5,
		  "the tank left the trap while it was still blocked, so nothing"
		  " below distinguishes the two builds");

	/* Chip walks north, pushing the Block off the tank's target cell, then
	 * steps east out of that cell himself. */
	runticks(12, CmdNorth);
	runticks(8, CmdEast);
	CHECK_MSG(cellat(8 + CXGRID * 5)->top.id != Block_Static,
		  "the Block was not pushed off the tank's target cell");

	runticks(24, NIL);
	CHECK_MSG(creatures[1]->pos != 7 + CXGRID * 5,
		  "the tank is STILL in the beartrap at (7,5) after its way was"
		  " cleared -- it took CS_HASMOVED on the tick it failed while"
		  " RELEASED and was never asked again");
    }

    /* ================================================================== */
    tw_case("🔴 a cloner wired into ROW 32 still fires -- the MSCC glitch");
    {
	/* FIX_ROW32_CLONER (`mslogic.c:2972`), unguarded until now.
	 *
	 * A red button may be wired to a "cloner" at y == 32, one row below a
	 * 32-row map. MSCC has no bounds check there and reads the memory that
	 * follows the map, which in practice is its own variable block; four real
	 * solutions depend on what comes out. activatecloner() bails on any
	 * off-map position, so the fix routes row 32 to activaterow32cloner(),
	 * which takes its template from ROW 0'S BOTTOM LAYER in that column --
	 * that being the memory MSCC is really reading -- and walks the clone in
	 * from below.
	 *
	 * ⚠ THE MATRIX SAID THIS NEEDED A FIXTURE ESCAPE HATCH. It does not.
	 * fix_settop() does refuse y >= 32, but no TILE is needed at row 32: what
	 * points there is the cloner WIRING, and fix_addcloner() writes the
	 * coordinate through unchecked. The template goes in row 0's bottom
	 * layer, which is an ordinary in-range cell.
	 *
	 * Only a BLOCK template is emulated (a non-block one warns and stops at
	 * the variable spill), so the template is file code 0x0E, a cloning block
	 * facing NORTH -- the direction that also exercises the resetdata() spill.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 4, 5, FIX_CHIP_EAST);
	fix_settop(&lv, 5, 5, FIX_BUTTON_RED);

	/* The template MSCC would find past the end of the map. */
	fix_setbot(&lv, 10, 0, 0x0E);		/* cloning block, facing north */
	fix_addcloner(&lv, 5, 5, 10, 32);	/* button -> (10, ROW 32) */

	/* The clone enters at (10,32) and steps north onto (10,31), so that cell
	 * cannot be the border wall or it is removed again for being unable to
	 * get out -- and the two builds would look identical. */
	fix_settop(&lv, 10, 31, FIX_FLOOR);

	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(teststate.clonercount == 1,
		  "the row-32 cloner wiring was dropped by the parser, so this"
		  " case cannot distinguish anything; got %d",
		  teststate.clonercount);
	CHECK_MSG(cellat(10 + CXGRID * 31)->top.id != Block_Static,
		  "there is already a block on the landing cell");

	runticks(8, CmdEast);		/* Chip steps onto the red button */

	CHECK_MSG(cellat(10 + CXGRID * 31)->top.id == Block_Static,
		  "the row-32 cloner did not fire: (10,31) holds %02X, not a"
		  " block. activatecloner() rejected the off-map position"
		  " outright instead of routing it to activaterow32cloner()",
		  cellat(10 + CXGRID * 31)->top.id);
    }

    /* ================================================================== */
    tw_case("🔴 a KEY press abandons an outstanding MOUSE goal");
    {
	int afterclick;

	/* FIX_KEY_CLEARS_GOAL (`mslogic.c`), unguarded until now.
	 *
	 * SuperCC drops the click outright at the foot of MSLevel.tick:
	 *     if (moveType == KEY || chip.getPosition().getIndex() == mouseGoal)
	 *         mouseGoal = NO_CLICK;
	 * Tile World cancels the goal on several paths but NOT on an ordinary
	 * successful key move, so a click stayed live indefinitely and kept
	 * steering Chip on every later tick that carried no input -- measured on
	 * JacquesOld #159, where Tile World was still walking toward the stale
	 * target 60 ticks after 29 keyboard moves had been played.
	 *
	 * ⚠ THE MATRIX SAID THIS NEEDED A MOUSE COMMAND THE HARNESS DOES NOT
	 * HAVE. That is true of test\nofix\nofix.c, whose generated move alphabet
	 * is NIL plus the four arrows -- it is not true here: this file drives
	 * the engine by writing currentinput() directly, and an absolute mouse
	 * command is just CmdAbsMouseMoveFirst + the target cell.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_EAST);
	CHECK_INT(startlevel(&lv), TRUE);

	/* Click on (15,5), then let the goal steer him for a while. */
	tick(CmdAbsMouseMoveFirst + (13 + CXGRID * 5));
	runticks(12, NIL);
	afterclick = chipx();
	CHECK_MSG(afterclick > 5,
		  "the click never started Chip moving (x=%d), so nothing below"
		  " distinguishes the two builds", afterclick);
	CHECK_MSG(afterclick < 13, "Chip already reached the goal; widen the gap");

	/* One keyboard move, then no input at all for a long time. */
	tick(CmdNorth);
	runticks(40, NIL);

	CHECK_MSG(chipx() == afterclick,
		  "Chip kept walking toward the clicked cell after a key press:"
		  " he was at x=%d when the key went in and is at x=%d now. The"
		  " mouse goal outlived the keyboard move", afterclick, chipx());
    }

    /* ================================================================== */
    tw_case("🔴 a red button clones, and the cloner re-arms when the clone leaves");
    {
	/* CLONING HAD NO TEST OF ANY KIND. The wiring was covered -- which button
	 * points at which cloner, and what happens to a nonsense entry -- but
	 * nothing had ever pressed a button and watched a creature appear.
	 *
	 * The rule being pinned is the FS_CLONING interlock, which is two lines
	 * in two different functions:
	 *
	 *   activatecloner():  if (cellat(pos)->bot.state & FS_CLONING) return;
	 *                      ... cellat(pos)->bot.state |= FS_CLONING;
	 *   endmovement():     if (cellat(oldpos)->bot.id == CloneMachine)
	 *                          cellat(oldpos)->bot.state &= ~FS_CLONING;
	 *
	 * Together they mean: a cloner fires once, and cannot fire again until
	 * the clone it made has stepped off. Invert endmovement's test and the
	 * flag is cleared on every cell EXCEPT the cloner -- so the cloner latches
	 * on after one clone and never produces another. A level built around a
	 * repeating cloner simply stops working, with nothing crashing.
	 *
	 * ⚠ PRESSING THE BUTTON TWICE IS THE WHOLE POINT. One press looks
	 * identical either way; the interlock only decides anything on the second.
	 */
	int	cloner, button;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 6, 5, FIX_BUTTON_RED);
	fix_settop(&lv, 10, 10, 0x4B);			/* a Ball, facing east */
	fix_setbot(&lv, 10, 10, FIX_CLONEMACHINE);
	fix_addcloner(&lv, 6, 5, 10, 10);
	CHECK_INT(startlevel(&lv), TRUE);

	cloner = 10 + CXGRID * 10;
	button = 6 + CXGRID * 5;
	CHECK_MSG(creaturecount == 1,
		  "a creature standing on a clone machine must not be woken at"
		  " level start; got %d creatures", creaturecount);
	CHECK_INT(teststate.clonercount, 1);
	CHECK_INT(clonerfrombutton(button), cloner);

	/* First press. */
	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 6, "Chip did not reach the button: x=%d", chipx());
	CHECK_MSG(creaturecount == 2,
		  "pressing a red button produced no clone; got %d creatures",
		  creaturecount);

	/* While the clone is still standing on it, the cloner is latched. */
	CHECK_MSG((cellat(cloner)->bot.state & FS_CLONING) != 0,
		  "the cloner did not latch after firing, so a second press"
		  " would clone again with the first clone still on top of it");

	/* Let the clone walk away east, then check the latch released. */
	runticks(16, NIL);
	CHECK_MSG((cellat(cloner)->bot.state & FS_CLONING) == 0,
		  "the clone left the cloner but FS_CLONING was never cleared --"
		  " this cloner can never fire again");

	/* Second press: step off the button and back onto it. */
	runticks(4, CmdEast);
	runticks(4, CmdWest);
	CHECK_MSG(chipx() == 6, "Chip did not return to the button: x=%d", chipx());
	CHECK_MSG(creaturecount == 3,
		  "the second button press produced no clone; got %d creatures."
		  " The cloner latched on and never released", creaturecount);
    }

    /* ================================================================== */
    tw_case("🔴 trapfrombutton answers for the button asked about, not another");
    {
	/* `for (i = traplistsize(); i; ++traps, --i) if (traps->from == pos)
	 * return traps->to;` -- invert the test and the function returns the
	 * trap belonging to SOME OTHER button, and answers a wired trap for a
	 * position that has no button at all.
	 *
	 * ⚠ TWO TRAPS, NOT ONE. With a single trap in the list, "the one that
	 * matches" and "the first one that does not" are the same entry for the
	 * failure case and the function looks right either way. The second trap
	 * is what makes the wrong answer a DIFFERENT answer. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 3, 3, FIX_BUTTON_BROWN);
	fix_settop(&lv, 4, 4, FIX_BEARTRAP);
	fix_addtrap(&lv, 3, 3, 4, 4);
	fix_settop(&lv, 6, 6, FIX_BUTTON_BROWN);
	fix_settop(&lv, 7, 7, FIX_BEARTRAP);
	fix_addtrap(&lv, 6, 6, 7, 7);
	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_INT(teststate.trapcount, 2);

	CHECK_INT(trapfrombutton(3 + CXGRID * 3), 4 + CXGRID * 4);
	CHECK_INT(trapfrombutton(6 + CXGRID * 6), 7 + CXGRID * 7);
	CHECK_MSG(trapfrombutton(20 + CXGRID * 20) == -1,
		  "a position with no button on it was given a trap: %d",
		  trapfrombutton(20 + CXGRID * 20));
    }

    /* ⚠ movelaw_block(0) AND movelaw_creature(0) ARE EQUIVALENT MUTANTS, and the
     * reason is in the table rather than the code. Their lower bound is
     * `id >= 0`, so the only value that separates it from `id > 0` is zero --
     * and movelaws[0] is the entry for Nothing, which is {0, 0, 0}. Both forms
     * therefore answer 0 for id 0: one by reading the table, the other by
     * refusing to. No input can tell them apart. The UPPER bound of both is
     * pinned by the jc-57 cases further up, which are the ones that matter. */

    /* ================================================================== */
    tw_case("🔴 removefromsliplist removes the right entry, or none at all");
    {
	/* Four comparisons in eleven lines, and all four survived. The slip list
	 * decides who is still sliding, so removing the wrong entry leaves a
	 * creature sliding forever and removing none leaves a dead creature on
	 * the list -- both of which desync a replay rather than crash.
	 *
	 * The list is driven directly here rather than through gameplay: the
	 * function takes a creature and edits an array, and a level that makes
	 * the engine populate the list its own way cannot put the boundary
	 * cases where they need to be.
	 *
	 * ⚠ THE SLOT PAST THE END IS POISONED ON PURPOSE. Two of the four bounds
	 * (`n < slipcount` in each loop) only do anything on the entry AFTER the
	 * list, so the only way to see them is to put something recognizable
	 * there and assert on what happened to it.
	 */
	creature   *a, *b, *c, *d;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	CHECK_INT(startlevel(&lv), TRUE);

	a = allocatecreature();
	b = allocatecreature();
	c = allocatecreature();
	d = allocatecreature();
	CHECK_MSG(a && b && c && d && a != b && b != c && c != d,
		  "the creature pool handed out duplicates");

	resetsliplist();
	appendtosliplist(a, NORTH);
	appendtosliplist(b, WEST);
	appendtosliplist(c, SOUTH);
	CHECK_INT(slipcount, 3);
	slips[3].cr = NULL;		/* the poison, one past the list */
	slips[3].dir = EAST;

	/* A creature that is not on the list at all. The search must stop at
	 * the end; running one entry further makes it fall through the
	 * "not found" test and shorten the list without removing anything. */
	removefromsliplist(d);
	CHECK_MSG(slipcount == 3,
		  "removing a creature that was never on the list changed the"
		  " count to %d", slipcount);

	/* Removing the MIDDLE entry: the count drops, the other two survive in
	 * order, and the slot past the new end is not disturbed. */
	removefromsliplist(b);
	CHECK_MSG(slipcount == 2,
		  "removing a creature that WAS on the list left the count at %d",
		  slipcount);
	CHECK_MSG(slips[0].cr == a,
		  "the first entry was removed instead of the middle one");
	CHECK_MSG(slips[1].cr == c,
		  "the surviving entries are in the wrong order");
	CHECK_MSG(slips[2].cr == c,
		  "the shift ran one entry too far and pulled the poisoned slot"
		  " past the end of the list into it");

	removefromsliplist(a);
	CHECK_INT(slipcount, 1);
	CHECK_MSG(slips[0].cr == c, "the last surviving entry is wrong");

	resetsliplist();
    }

    /* ================================================================== */
    tw_case("🔴 lookupcreature finds the creature AT the cell, and hides Chip on request");
    {
	/* `if (creatures[n]->pos == pos) if (creatures[n]->id != Chip ||
	 * includechip)`. Invert the first and the function returns the first
	 * creature that is NOT where you asked; invert the second and the
	 * includechip flag means the opposite of what it says. Both survived,
	 * because every caller in the suite asks about a cell that does hold the
	 * creature it expects, so a wrong answer is never visible.
	 *
	 * ⚠ THE EMPTY CELL IS THE ONE THAT CATCHES THE FIRST. A lookup that
	 * succeeds looks identical either way when the answer happens to be
	 * right; only asking about a cell with NOTHING in it can tell "found the
	 * match" from "found the first non-match". */
	creature   *found;
	int		mpos, empty;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 8, 8, 0x40);			/* a Bug */
	fix_addcreature(&lv, 8, 8);
	CHECK_INT(startlevel(&lv), TRUE);

	mpos = 8 + CXGRID * 8;
	empty = 20 + CXGRID * 20;

	found = lookupcreature(mpos, FALSE);
	CHECK_MSG(found != NULL, "the monster at (8,8) was not found");
	if (found)
	    CHECK_MSG(found->pos == mpos,
		      "lookupcreature(%d) returned the creature at %d instead",
		      mpos, found->pos);

	CHECK_MSG(lookupcreature(empty, FALSE) == NULL,
		  "an empty cell returned a creature -- the position test is"
		  " matching everything EXCEPT the cell asked about");
	CHECK_MSG(lookupcreature(empty, TRUE) == NULL,
		  "an empty cell returned a creature even with includechip");

	CHECK_MSG(lookupcreature(chippos(), FALSE) == NULL,
		  "Chip was returned with includechip FALSE");
	CHECK_MSG(lookupcreature(chippos(), TRUE) == getchip(),
		  "Chip was NOT returned with includechip TRUE");
    }

    /* ================================================================== */
    tw_case("🔴 lookupblock caches one block per cell, and does not confuse two");
    {
	/* `if (blocks[n]->pos == pos && !blocks[n]->hidden) return blocks[n];`
	 * -- the lookup that keeps the block list from growing a duplicate every
	 * time a block is touched. Invert the position test and it returns the
	 * block from SOME OTHER cell, which is a block teleporting across the
	 * level as far as the rest of the engine is concerned.
	 *
	 * ⚠ NEITHER SIDE IS VISIBLE FROM ONE CALL. The first call to a fresh
	 * list always allocates, so it looks right whatever the test says; the
	 * bug only shows on the SECOND call, and only by comparing pointers. */
	creature   *b1, *b2, *b3;
	int		p, q;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 8, 8, FIX_BLOCK);
	fix_settop(&lv, 12, 12, FIX_BLOCK);
	CHECK_INT(startlevel(&lv), TRUE);

	p = 8 + CXGRID * 8;
	q = 12 + CXGRID * 12;

	b1 = lookupblock(p);
	CHECK_MSG(b1 != NULL, "no block was produced for (8,8)");
	if (b1) {
	    CHECK_INT(b1->pos, p);
	    CHECK_INT(b1->id, Block);
	}

	b2 = lookupblock(p);
	CHECK_MSG(b1 == b2,
		  "asking twice about the same cell produced two different"
		  " blocks; the cached one was not recognized");

	b3 = lookupblock(q);
	CHECK_MSG(b3 != b1,
		  "a different cell returned the block from (8,8) -- the lookup"
		  " is matching every block EXCEPT the one at the position asked"
		  " about");
	if (b3)
	    CHECK_INT(b3->pos, q);
    }

    /* ================================================================== */
    tw_case("🔴 icewallturn's full truth table, all four corners");
    {
	/* Four nested ternaries, eight comparisons, and not one of them pinned:
	 * every existing case that touches ice uses a straight ice tile, where
	 * icewallturn() returns its argument unchanged and all eight mutations
	 * agree. A corner that deflects the wrong way sends a creature -- or
	 * Chip -- off in a direction the level was not built for, which is a
	 * silent desync rather than a crash.
	 *
	 * ⚠ THE UNCHANGED DIRECTIONS ARE THE HALF THAT CATCHES THESE. Each
	 * corner deflects two of the four directions and passes the other two
	 * through. Inverting `dir == SOUTH` to `!=` leaves the deflected case
	 * looking right and breaks the PASS-THROUGH, so a table that only
	 * checked the turns would go on agreeing. All four directions, all four
	 * corners.
	 */
	CHECK_INT(icewallturn(IceWall_Northeast, SOUTH), EAST);
	CHECK_INT(icewallturn(IceWall_Northeast, WEST), NORTH);
	CHECK_INT(icewallturn(IceWall_Northeast, NORTH), NORTH);
	CHECK_INT(icewallturn(IceWall_Northeast, EAST), EAST);

	CHECK_INT(icewallturn(IceWall_Southwest, NORTH), WEST);
	CHECK_INT(icewallturn(IceWall_Southwest, EAST), SOUTH);
	CHECK_INT(icewallturn(IceWall_Southwest, SOUTH), SOUTH);
	CHECK_INT(icewallturn(IceWall_Southwest, WEST), WEST);

	CHECK_INT(icewallturn(IceWall_Northwest, SOUTH), WEST);
	CHECK_INT(icewallturn(IceWall_Northwest, EAST), NORTH);
	CHECK_INT(icewallturn(IceWall_Northwest, NORTH), NORTH);
	CHECK_INT(icewallturn(IceWall_Northwest, WEST), WEST);

	CHECK_INT(icewallturn(IceWall_Southeast, NORTH), EAST);
	CHECK_INT(icewallturn(IceWall_Southeast, WEST), SOUTH);
	CHECK_INT(icewallturn(IceWall_Southeast, SOUTH), SOUTH);
	CHECK_INT(icewallturn(IceWall_Southeast, EAST), EAST);

	/* A tile that is not a corner at all passes everything through. */
	CHECK_INT(icewallturn(Ice, NORTH), NORTH);
	CHECK_INT(icewallturn(Empty, EAST), EAST);
    }

    /* ================================================================== */
    tw_case("🔴 istrapbuttondown's position bounds, from both sides");
    {
	/* `pos >= 0 && pos < CXGRID * CYGRID && cellat(pos)->top.id !=
	 * Button_Brown`. The two bounds are what stop a trap wiring that points
	 * off the map from dereferencing it, and both survived: every trap in
	 * the suite is wired to a sensible cell, so 0 and CXGRID * CYGRID -- the
	 * only two values that separate `>=` from `>` and `<` from `<=` -- were
	 * never asked.
	 *
	 * The function answers "is the button at this position covered", so a
	 * plain floor reads TRUE and an exposed brown button reads FALSE. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 0, 0, FIX_FLOOR);	/* position 0, not a wall */
	fix_settop(&lv, 7, 7, FIX_BUTTON_BROWN);
	CHECK_INT(startlevel(&lv), TRUE);

	CHECK_MSG(istrapbuttondown(0),
		  "position 0 was refused as out of range; it is the top-left"
		  " cell of the map");
	CHECK_MSG(!istrapbuttondown(-1), "position -1 was accepted");
	CHECK_MSG(!istrapbuttondown(CXGRID * CYGRID),
		  "position CXGRID * CYGRID was accepted -- that is the row-32"
		  " cloner area, not a cell a trap can be wired to");
	CHECK_MSG(istrapbuttondown(30 + CXGRID * 30),
		  "an ordinary floor cell was refused");
	CHECK_MSG(!istrapbuttondown(7 + CXGRID * 7),
		  "an EXPOSED brown button reads as held down");
    }

    /* ================================================================== */
    tw_case("🔴 a Glider crosses Water and a Fireball crosses Fire, unharmed");
    {
	/* endmovement()'s hazard rules for MONSTERS -- the branch that is
	 * neither Chip nor Block:
	 *
	 *     case Water: if (crid != Glider)   dead = TRUE;
	 *     case Fire:  if (crid != Fireball) dead = TRUE;
	 *
	 * Invert either and the one creature that is supposed to cross its own
	 * hazard drowns or burns instead. A glider that dies in water breaks the
	 * solution to a great many levels. Both survived the census, because no
	 * case had ever moved a monster onto a hazard at all.
	 *
	 * 🔴 THE SURVIVAL IS THE WHOLE ORACLE, AND THAT IS NOT A WEAKNESS -- it
	 * is the only side of these rules that exists. A monster that is not
	 * immune CANNOT REACH ITS HAZARD: canmakemove() refuses the move, so a
	 * bug beside water simply turns away. Measured, including from a force
	 * floor, which does not override it -- the bug stepped north off the
	 * force floor instead, at (10,14). So the line is only ever evaluated
	 * with crid already equal to Glider (or Fireball), and asserting that
	 * THAT creature lives is exactly what tells the two forms apart.
	 *
	 * Chip's own drowning is a different branch of the same function and is
	 * covered by "water without flippers is fatal" further up.
	 *
	 * The monsters must be in the monster list or initgame() never wakes
	 * them; creatures[0] is Chip and 1..2 follow the fix_addcreature calls.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);

	fix_settop(&lv, 10, 10, 0x53);		/* Glider, facing east   */
	fix_addcreature(&lv, 10, 10);
	fix_settop(&lv, 11, 10, FIX_WATER);
	fix_settop(&lv, 12, 10, FIX_WATER);

	fix_settop(&lv, 10, 20, 0x47);		/* Fireball, facing east */
	fix_addcreature(&lv, 10, 20);
	fix_settop(&lv, 11, 20, FIX_FIRE);
	fix_settop(&lv, 12, 20, FIX_FIRE);

	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(creaturecount == 3,
		  "wanted Chip and two monsters, got %d creatures", creaturecount);
	runticks(12, NIL);

	CHECK_MSG(!creatures[1]->hidden,
		  "the Glider drowned; Water is the one hazard a Glider crosses."
		  " It is at (%d,%d), id %02X",
		  creatures[1]->pos % CXGRID, creatures[1]->pos / CXGRID,
		  creatures[1]->id);
	CHECK_MSG(creatures[1]->pos % CXGRID > 10,
		  "the Glider never entered the water, so the rule was not"
		  " exercised: it is at (%d,%d)",
		  creatures[1]->pos % CXGRID, creatures[1]->pos / CXGRID);

	CHECK_MSG(!creatures[2]->hidden,
		  "the Fireball burned; Fire is the one hazard a Fireball"
		  " crosses. It is at (%d,%d), id %02X",
		  creatures[2]->pos % CXGRID, creatures[2]->pos / CXGRID,
		  creatures[2]->id);
	CHECK_MSG(creatures[2]->pos % CXGRID > 10,
		  "the Fireball never entered the fire, so the rule was not"
		  " exercised: it is at (%d,%d)",
		  creatures[2]->pos % CXGRID, creatures[2]->pos / CXGRID);
    }

    /* ================================================================== */
    tw_case("🔴 canmakemove refuses exactly the moves that leave the grid");
    {
	/* `if (y < 0 || y >= CYGRID || x < 0 || x >= CXGRID) return FALSE;` is
	 * the only thing standing between a move and `cellat()` on an index off
	 * the map -- and all four of its comparisons survived the census.
	 *
	 * ⚠ WHY EVERY EXISTING CASE MISSES THEM. They walk Chip around the
	 * middle of a walled room, so the destination is always comfortably
	 * inside the grid and all eight spellings of this line agree. The bound
	 * only decides anything at the very edge, and a walled border means
	 * nothing ever gets there. This level has NO border for that reason, and
	 * the moves are asked of canmakemove() directly rather than played.
	 *
	 * Both sides of each bound, because a bound tested only from the
	 * "refused" side moves outward for free and one tested only from the
	 * "allowed" side moves inward.
	 */
	creature   *chip;

	fix_init(&lv);				/* no border: all floor */
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	CHECK_INT(startlevel(&lv), TRUE);
	chip = getchip();

	chip->pos = 1 * CXGRID + 5;
	CHECK_MSG(canmakemove(chip, NORTH, 0),
		  "a move to row 0 was refused; row 0 is on the map");
	chip->pos = 0 * CXGRID + 5;
	CHECK_MSG(!canmakemove(chip, NORTH, 0),
		  "a move NORTH off the top of the map was allowed");

	chip->pos = 30 * CXGRID + 5;
	CHECK_MSG(canmakemove(chip, SOUTH, 0),
		  "a move to row %d was refused; it is the last row", CYGRID - 1);
	chip->pos = (CYGRID - 1) * CXGRID + 5;
	CHECK_MSG(!canmakemove(chip, SOUTH, 0),
		  "a move SOUTH off the bottom of the map was allowed -- that is"
		  " cellat() on the row-32 cloner area");

	chip->pos = 5 * CXGRID + 1;
	CHECK_MSG(canmakemove(chip, WEST, 0),
		  "a move to column 0 was refused; column 0 is on the map");
	chip->pos = 5 * CXGRID + 0;
	CHECK_MSG(!canmakemove(chip, WEST, 0),
		  "a move WEST off the left edge was allowed");

	chip->pos = 5 * CXGRID + (CXGRID - 2);
	CHECK_MSG(canmakemove(chip, EAST, 0),
		  "a move to column %d was refused; it is the last column",
		  CXGRID - 1);
	chip->pos = 5 * CXGRID + (CXGRID - 1);
	CHECK_MSG(!canmakemove(chip, EAST, 0),
		  "a move EAST off the right edge was allowed -- without this"
		  " bound it wraps onto the next row");
    }

    /* ================================================================== */
    tw_case("🔴 initgame breaks a teleport under a creature, and NOTHING else");
    {
	/* initgame()'s first pass sets FS_BROKEN on a teleport or toggle wall
	 * that starts underneath floor, Chip or a Block -- MSCC's rule that such
	 * a tile is dead for the whole level.
	 *
	 * ⚠ FIVE COMPARISONS, AND THE EXISTING CASES PIN NONE OF THEM, because
	 * a level that never puts anything on a teleport agrees with every
	 * mutation of them. Each condition below is therefore given both a
	 * combination it must fire on and one it must not:
	 *
	 *   Block over Teleport   -> broken   (the rule)
	 *   Bug   over Teleport   -> NOT      (kills `== Chip` and `== Block`,
	 *                                      which inverted admit any creature)
	 *   Block over Floor      -> NOT      (kills all three `bot ==` tests,
	 *                                      which inverted admit any floor)
	 */
	int		broken, notcreature, notteleport;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	fix_settop(&lv, 10, 10, FIX_BLOCK);
	fix_setbot(&lv, 10, 10, FIX_TELEPORT);
	fix_settop(&lv, 12, 10, 0x40);			/* Bug, facing north */
	fix_setbot(&lv, 12, 10, FIX_TELEPORT);
	fix_settop(&lv, 14, 10, FIX_BLOCK);
	fix_setbot(&lv, 14, 10, FIX_FLOOR);
	CHECK_INT(startlevel(&lv), TRUE);

	broken = (cellat(10 + CXGRID * 10)->bot.state & FS_BROKEN) != 0;
	notcreature = (cellat(12 + CXGRID * 10)->bot.state & FS_BROKEN) != 0;
	notteleport = (cellat(14 + CXGRID * 10)->bot.state & FS_BROKEN) != 0;

	CHECK_MSG(broken,
		  "a teleport under a Block was not marked broken at level start");
	CHECK_MSG(!notcreature,
		  "a teleport under a BUG was marked broken; only floor, Chip and"
		  " Block start a tile broken");
	CHECK_MSG(!notteleport,
		  "plain floor under a Block was marked broken; only teleports and"
		  " toggle walls can be");
    }

    /* ================================================================== */
    tw_case("🔴 the monster list admits monsters, and refuses the three junk kinds");
    {
	/* initgame()'s second pass walks state->crlist and decides what becomes
	 * a creature. Four separate conditions, each of which silently changes
	 * the creature population when it moves by one, and the population is
	 * the oracle for all of them.
	 *
	 * ⚠ THE CHIP-TILE FILTER IS jc-?? / FIX_MONSTERLIST_CHIP_TILES, and it
	 * is why Jacques#1 "Welcome" desynced: its monster list points at a
	 * SWIMMING CHIP tile, SuperCC ignores it, and Tile World used to wake a
	 * phantom creature out of it. That fix had no test.
	 *
	 * Expected population: Chip, the bug at (8,8), and the bug at (0,0).
	 * Everything else in the list is junk and must be refused.
	 */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);

	fix_settop(&lv, 8, 8, 0x40);			/* a real monster    */
	fix_addcreature(&lv, 8, 8);

	/* 🔴 POSITION ZERO, which is the only value that tells `pos < 0` from
	 * `pos <= 0`. Every other monster in the suite sits comfortably inside
	 * the grid, so both forms agree on all of them and the bound was free to
	 * swallow the top-left cell. The border wall there is overwritten on
	 * purpose. */
	fix_settop(&lv, 0, 0, 0x40);
	fix_addcreature(&lv, 0, 0);

	fix_settop(&lv, 9, 9, FIX_BLOCK);		/* a Block: refused  */
	fix_addcreature(&lv, 9, 9);

	fix_settop(&lv, 11, 11, 0x40);			/* on a cloner: ditto */
	fix_setbot(&lv, 11, 11, FIX_CLONEMACHINE);
	fix_addcreature(&lv, 11, 11);

	fix_settop(&lv, 13, 13, FIX_CHIP_NORTH);	/* a Chip tile: ditto */
	fix_addcreature(&lv, 13, 13);

	CHECK_INT(startlevel(&lv), TRUE);
	CHECK_MSG(creaturecount == 3,
		  "the monster list produced %d creatures, wanted 3 (Chip and two"
		  " bugs). A Block, a monster on a cloner and a Chip tile are all"
		  " in the list and none of them may become a creature.",
		  creaturecount);
    }

    /* ================================================================== */
    tw_case("🔴 verifymap() complains at exactly the right side of each bound");
    {
	/* The MS engine's own consistency check, compiled in whenever NDEBUG is
	 * not and run at the top of every tick -- so this suite executes it
	 * thousands of times while asserting nothing about what it said. All
	 * seven of its comparisons were free to move by one.
	 *
	 * ⚠ ITS ONLY OUTPUT IS warn(), so the warning count is the whole oracle,
	 * and each bound is pinned from BOTH sides: the value that must be
	 * complained about, and the one next to it that must not. A bound tested
	 * only from the "must warn" side moves outward for free.
	 *
	 * See the matching case in lxlogic_test.c; the two engines carry
	 * separate copies of this function and neither was covered. */
	creature   *cr;
	int		savedid, savedpos, saveddir, savedhidden;

	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	CHECK_INT(startlevel(&lv), TRUE);
	runticks(2, NIL);

	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a freshly started, untouched level already produces %d"
		  " verifymap warning(s); every check below is measured against"
		  " this being zero", warn_count);

	CHECK_MSG(creaturecount > 0, "no creatures to perturb");
	cr = creatures[0];
	savedid = cr->id;
	savedpos = cr->pos;
	saveddir = cr->dir;
	savedhidden = cr->hidden;

	/* --- the creature-id window is [0x40, 0x80) ---------------------- */
	cr->id = 0x40;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "creature id 0x40 is the FIRST legal id and was reported as"
		  " undefined");
	cr->id = 0x80;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "creature id 0x80 is one past the last legal id and was not"
		  " reported");
	cr->id = savedid;

	/* --- the position bound, which only applies to a VISIBLE creature -- */
	cr->hidden = FALSE;
	cr->pos = 0;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0, "position 0 was reported as off the map");
	cr->pos = CXGRID * CYGRID;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "position CXGRID * CYGRID is the first one off the map and was"
		  " not reported");
	cr->hidden = TRUE;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a HIDDEN creature off the map was reported; the bound is"
		  " guarded by !cr->hidden and that guard is load-bearing");
	cr->hidden = (unsigned char)savedhidden;
	cr->pos = savedpos;

	/* --- the direction rule, and its Block exemption ------------------ */
	cr->dir = EAST;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "EAST is a legal direction and was reported as illegal");
	cr->dir = NIL;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a NIL direction was reported by the `dir > EAST` test, which"
		  " NIL cannot reach: NIL is 0 and EAST is 8");

	/* 🔴 A BLOCK WITH AN ILLEGAL DIRECTION, which is the only input that
	 * exercises the `cr->dir != NIL` half of the guard.
	 *
	 * `if (cr->dir > EAST && (cr->dir != NIL || cr->id != Block))`. Because
	 * NIL is 0 and EAST is 8, `dir > EAST` ALREADY implies `dir != NIL`, so
	 * the left side of the OR is always true once the left of the AND is --
	 * and the whole condition reduces to `dir > EAST`. Invert `!= NIL` to
	 * `== NIL` and the OR starts depending on `id != Block` instead, so a
	 * Block with an illegal direction stops being reported. Nothing else
	 * tells the two apart.
	 *
	 * ⚠ AND THE OTHER HALF IS AN EQUIVALENT MUTANT. `cr->id != Block`
	 * flipped to `== Block` leaves the OR true either way, for the same
	 * reason. No input can distinguish it; do not go looking. */
	cr->dir = EAST * 2;
	cr->id = Block;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "a Block moving in an illegal direction (%d) was not reported",
		  cr->dir);
	cr->id = savedid;
	cr->dir = saveddir;

	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "the state was not restored cleanly after the perturbations");
    }

    if (logic)
	(*logic->shutdown)(logic);
    free(testsetup.leveldata);
    testsetup.leveldata = NULL;

    return tw_end();
}
