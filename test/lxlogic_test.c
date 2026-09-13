/* lxlogic_test.c: the Lynx engine.
 *
 * MOD (Jeremy). The FIRST test of lxlogic.c. It is 2,045 lines and had zero
 * coverage of any kind until now -- CLAUDE.md listed it as the largest hole in
 * the suite, and it is not a hypothetical one: the maintainer's collection
 * holds 909 recorded Lynx solutions, every one of which depends on this file
 * behaving exactly as it does today.
 *
 * 🔴 THESE ARE CHARACTERIZATION TESTS, AND THAT IS DELIBERATE. For an engine
 * with recorded solutions, "different" and "wrong" are the same thing: a
 * behavior change here does not produce a bug report, it silently stops a
 * solution somebody spent hours on from replaying. So these cases pin what the
 * engine DOES, and every expectation below was reasoned from the Lynx ruleset
 * first and then confirmed against the engine -- not read off a green run.
 * Where the two disagreed the disagreement is written down rather than
 * flattened into whatever the code happened to do.
 *
 * WHAT THIS CANNOT REPLACE. The whole-collection corpus differential
 * (test/run-corpus.ps1) is still the instrument that decides whether an engine
 * change is safe; it replays tens of thousands of real solutions. This file is
 * the fast half: it says WHICH rule broke, in a second, instead of telling you
 * that 40 sets changed and leaving you to bisect.
 *
 * DIFFERENCES FROM mslogic_test.c THAT MATTER:
 *
 *   * pedanticmode is DEFINED BY lxlogic.c itself (line 54), not by tworld.c.
 *     mslogic_test.c defines it because lxlogic.c is not in that translation
 *     unit; this file must NOT, or the link fails with a redefinition.
 *     (mslogic_test.c used to say the flag is "defined in tworld.c"; writing
 *     this file is what showed that was wrong, and it now says so correctly.)
 *   * creaturelist() here is state->creatures, the gamestate's own array --
 *     there is no separate engine-private list to read past, which is the trap
 *     mslogic_test.c documents at length. chippos() is simply creatures[0].pos.
 *   * A creature's POSITION is committed when its move BEGINS, not when the
 *     animation ends. One tick after ordering a move, chippos() already reports
 *     the destination while `moving` is still set. Every tick count below reads
 *     wrong until you know that, so it has a case of its own.
 *   * advancegame() does NOT report the result the moment the level is decided.
 *     Reaching the exit sets completed(), which starts a 13-tick endgame timer
 *     (lxlogic.c:139); only when that expires does advancegame() return +1 or
 *     -1. A case that runs 16 ticks after stepping onto the exit sees 0 and
 *     looks like a bug -- that is what the first draft of this file did.
 *
 * TESTLANG: c
 *
 * lxlogic.c, encoding.c and random.c are compiled only as C by CMake, and the
 * engine relies on C's implicit void* conversion through err.h's x_alloc.
 * See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-unused-value
 *
 * -Wno-unused-value covers lxlogic.c's _assert macro (line 38), which expands
 * to `((test) || (die(...), 0))`; GCC objects to the comma expression's right
 * operand at two call sites. Pre-existing, not a defect, and suppressed here
 * rather than fixed because this is a fork tracking upstream. ⚠ The cost is
 * real: it also stops the compiler reporting a genuinely discarded result in
 * THIS file. If lxlogic.c is ever cleaned up, delete the flag.
 */

#include	"tw_test.h"
#include	"tw_fixture.h"
#include	"tw_corpus.h"

/* NOTE: no `int pedanticmode` here. lxlogic.c:54 defines it. Pedantic mode is
 * left at its FALSE default deliberately -- it changes ice, teleport and
 * boot behavior, and a suite that silently ran in it would be measuring a
 * configuration almost nobody plays and no recorded solution used. */

#include	"../random.c"
#include	"../encoding.c"
#include	"../lxlogic.c"

/* --- the error surface, stubbed --------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int warn_count = 0;
static int errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *prefix, char const *fmt, ...)
{
    (void)prefix; (void)fmt; ++errmsg_count;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* --- the harness ------------------------------------------------------ */

static gamestate	teststate;
static gamesetup	testsetup;
static gamelogic       *logic;
static int		ticknumber;

/* Stand up a game on the given level, as play.c's initgamestate() does. */
static int startlevel(fixlevel const *lv)
{
    int			size;
    unsigned char      *data;

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

    logic = lynxlogicstartup();
    if (!logic)
	return FALSE;
    logic->state = &teststate;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_Lynx;
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
    teststate.moves.list = NULL;
    teststate.moves.count = 0;
    teststate.moves.allocated = 0;
    /* FIXED seed. Two runs of this suite must agree, or a case that depends on
     * a random draw fails occasionally and gets written off as flaky. */
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

/* Chip's position, through lxlogic.c's OWN accessors, so these helpers cannot
 * drift from the engine's idea of where he is. */
static int chipx(void) { return chippos() % CXGRID; }
static int chipy(void) { return chippos() / CXGRID; }

/* 🔴 DO NOT USE chipisalive() FROM A TEST. It is `getchip()->id == Chip`, and
 * between ticks Chip's id is legitimately Pushing_Chip (0x70) whenever he is
 * straining against something: lxlogic.c:1737 sets it during display
 * preparation at the END of a tick, and lxlogic.c:1628 resets it to Chip at the
 * START of the next one.
 *
 * So a test, which by definition looks between advancegame() calls, sees "not
 * alive" for a Chip who is merely pushing. Measured: with a wall due east and
 * CmdEast held, chipisalive() is false on every single tick while Chip stands
 * there in perfect health. That cost an hour of chasing a phantom defect.
 *
 * The engine's one use of the macro (lxlogic.c:1226) is safe, because it runs
 * after the reset inside the same call. This helper is the outside view: Chip
 * is dead when removechip() has hidden him. */
static int chipdied(void) { return getchip()->hidden != 0; }

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * test/fuzz/corpus/lxlogic/ replayed through the engine, so a libFuzzer finding
 * on Linux becomes a permanent regression case on every platform
 * (docs/adr/0011). The input format is the fuzz target's: a move-count byte, a
 * move stream, then the raw level record.
 *
 * ⚠ What a green run proves is narrow, as with the parser replays: these inputs
 * still load and play to completion without crashing, hanging or tripping an
 * _assert. ASan in the `fuzz` job is the memory oracle. `enginecorpus_ran` is
 * the assertion that stops this passing without the engine having run.
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

    lg = lynxlogicstartup();
    if (!lg) {
	free(level);
	testsetup.leveldata = NULL;
	return;
    }
    logic = lg;
    logic->state = &teststate;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_Lynx;
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
    CHECK_MSG(v == TW_CORPUS_OK, "lxlogic corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

/* A plain walled room with Chip at (9,9) facing south. */
static void openroom(fixlevel *lv)
{
    fix_init(lv);
    fix_border(lv);
    lv->number = 1;
    lv->time = 0;
    strcpy(lv->passwd, "ABCD");
    fix_settop(lv, 9, 9, FIX_CHIP_SOUTH);
}

int main(void)
{
    fixlevel	lv;
    int		r;

    tw_begin("lxlogic");
    tw_expect_atleast(130);

    /* ================================================================== *
     * The level loads at all.
     * ================================================================== */

    tw_case("every committed lxlogic fuzz corpus input still plays");
    {
	char dir[256];
	int c;

	CHECK_MSG(tw_corpus_dir("lxlogic", dir, sizeof dir),
		  "could not find test/fuzz/corpus/lxlogic from the working"
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

    tw_case("a synthesized level loads under the Lynx ruleset");
    {
	openroom(&lv);
	CHECK_MSG(startlevel(&lv), "the Lynx engine refused a well-formed level");
	CHECK_INT(chipx(), 9);
	CHECK_INT(chipy(), 9);
	CHECK_MSG(!chipdied(), "Chip is dead at the start of the level");
	/* ⚠ timeoffset stays 0 at init. lxlogic.c sets it to 1 only when the
	 * level ENDS (line 495, right beside startendgametimer()) -- an earlier
	 * draft of this file asserted 1 here, having read that line as
	 * initialization. Pinned at 0 so the misreading cannot come back. */
	CHECK_INT(teststate.timeoffset, 0);
    }

    /* ================================================================== *
     * Movement. Lynx moves Chip one cell per four ticks, as MS does, but
     * the two engines get there by different machinery.
     * ================================================================== */

    tw_case("Chip walks east, one cell every four ticks");
    {
	openroom(&lv);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 10);
	CHECK_INT(chipy(), 9);
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 11);
	runticks(8, CmdEast);
	CHECK_INT(chipx(), 13);
    }

    tw_case("Chip's POSITION changes on the first tick of a move, not the last");
    {
	/* 🔴 A fact worth pinning because it makes tick arithmetic in every
	 * other case here read wrong until you know it. In Lynx a creature's
	 * pos is updated when the move BEGINS and the following three ticks
	 * animate it -- so one tick after ordering a move, chippos() already
	 * reports the destination while `moving` is still set.
	 *
	 * Anything reading chippos() as "where Chip has arrived" is therefore
	 * reading "where Chip is committed to arriving". That distinction is
	 * what the endgame timer below exists to paper over for the display. */
	openroom(&lv);
	CHECK_MSG(startlevel(&lv), "setup failed");
	tick(CmdEast);
	CHECK_MSG(chipx() == 10,
		  "after ONE tick Chip is at %d; the Lynx engine is expected to"
		  " commit the position immediately", chipx());
	CHECK_MSG(getchip()->moving != 0,
		  "Chip's position moved but `moving` was already clear");
	runticks(3, CmdEast);
	CHECK_INT(chipx(), 10);
	CHECK_MSG(getchip()->moving == 0,
		  "after four ticks the move should have finished animating");
    }

    tw_case("Chip walks in all four directions");
    {
	openroom(&lv);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(4, CmdNorth);
	CHECK_INT(chipy(), 8);
	runticks(4, CmdWest);
	CHECK_INT(chipx(), 8);
	runticks(4, CmdSouth);
	CHECK_INT(chipy(), 9);
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 9);
    }

    tw_case("a wall refuses Chip, and he stays exactly where he was");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_WALL);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(12, CmdEast);
	CHECK_INT(chipx(), 9);
	CHECK_INT(chipy(), 9);
	CHECK_MSG(!chipdied(), "a wall killed Chip");
    }

    /* ================================================================== *
     * The ways a level ends.
     * ================================================================== */

    tw_case("water without flippers is fatal");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_WATER);
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(40, CmdEast);
	CHECK_MSG(r < 0, "walking into water did not end the level (r=%d)", r);
    }

    tw_case("fire without boots is fatal");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_FIRE);
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(40, CmdEast);
	CHECK_MSG(r < 0, "walking into fire did not end the level (r=%d)", r);
    }

    tw_case("a bomb is fatal");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_BOMB);
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(40, CmdEast);
	CHECK_MSG(r < 0, "walking onto a bomb did not end the level (r=%d)", r);
    }

    tw_case("reaching the exit wins the level");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_EXIT);
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(40, CmdEast);
	CHECK_MSG(r > 0, "reaching the exit did not win the level (r=%d)", r);
    }

    tw_case("running out of time loses, and an untimed level does not");
    {
	openroom(&lv);
	lv.time = 1;
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(TICKS_PER_SECOND * 3, NIL);
	CHECK_MSG(r < 0, "the clock ran out and the level did not end (r=%d)", r);

	openroom(&lv);
	lv.time = 0;			/* 0 means untimed */
	CHECK_MSG(startlevel(&lv), "setup failed");
	r = runticks(TICKS_PER_SECOND * 3, NIL);
	CHECK_MSG(r == 0, "an untimed level ended on its own (r=%d)", r);
    }

    /* ================================================================== *
     * Chips and the socket.
     * ================================================================== */

    tw_case("collecting a chip decrements the counter");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_ICCHIP);
	lv.chips = 1;
	CHECK_MSG(startlevel(&lv), "setup failed");
	CHECK_INT(teststate.chipsneeded, 1);
	runticks(4, CmdEast);
	CHECK_INT(teststate.chipsneeded, 0);
	CHECK_INT(chipx(), 10);
    }

    tw_case("a level demanding 65,532 chips locks the socket, it does not die (jc-51)");
    {
	/* The same defect the MS engine had, fixed here at the same time -- see
	 * the fuller note in test/mslogic_test.c. chipsneeded is a SIGNED short
	 * filled from an UNSIGNED 16-bit .dat field, so 65,532 arrives as -4;
	 * the gate asked `> 0` and let Chip through, and lxlogic.c asserts
	 * `chipsneeded() == 0` in TWO places (1388 and 1464), each of which
	 * calls die() and exits the shipped game.
	 *
	 * Fixed in both engines together, because "the MS one was fixed and the
	 * Lynx one was not" is exactly how a two-engine codebase drifts. */
	openroom(&lv);
	lv.chips = 65532;
	fix_settop(&lv, 10, 9, FIX_SOCKET);
	CHECK_MSG(startlevel(&lv), "setup failed");
	CHECK_MSG(teststate.chipsneeded < 0,
		  "the fixture did not produce a negative chipsneeded (%d); this"
		  " case is testing nothing", (int)teststate.chipsneeded);
	r = runticks(16, CmdEast);
	CHECK_MSG(chipx() == 9,
		  "Chip entered a socket with a negative chip count, reaching"
		  " x=%d", chipx());
	CHECK_MSG(r == 0, "the level ended (r=%d) rather than locking the socket", r);
    }

    tw_case("a socket refuses Chip until every chip is collected");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_SOCKET);
	fix_settop(&lv, 9, 8, FIX_ICCHIP);
	lv.chips = 1;
	CHECK_MSG(startlevel(&lv), "setup failed");
	/* The socket is shut while a chip is outstanding. */
	runticks(12, CmdEast);
	CHECK_MSG(chipx() == 9, "the socket let Chip through with %d chip(s)"
			        " still needed", teststate.chipsneeded);
	/* Collect the chip to the north, come back, and it opens. */
	runticks(4, CmdNorth);
	CHECK_INT(teststate.chipsneeded, 0);
	runticks(4, CmdSouth);
	CHECK_INT(chipy(), 9);
	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 10, "the socket stayed shut with 0 chips needed");
    }

    /* ================================================================== *
     * Terrain.
     * ================================================================== */

    tw_case("dirt becomes floor when Chip walks through it");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_DIRT);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 10);
	CHECK_MSG(!chipdied(), "dirt killed Chip");
    }

    tw_case("gravel is walkable");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_GRAVEL);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(4, CmdEast);
	CHECK_INT(chipx(), 10);
	CHECK_MSG(!chipdied(), "gravel killed Chip");
    }

    tw_case("a block can be pushed, and cannot be pushed into a wall");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_BLOCK);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(4, CmdEast);
	CHECK_MSG(chipx() == 10, "Chip did not follow the block he pushed"
				 " (he is at %d)", chipx());

	/* Now with a wall directly behind the block: neither may move. */
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_BLOCK);
	fix_settop(&lv, 11, 9, FIX_WALL);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(12, CmdEast);
	CHECK_MSG(chipx() == 9, "Chip pushed a block into a wall (he is at %d)",
		  chipx());
    }

    tw_case("ice carries Chip until the ice runs out");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_ICE);
	fix_settop(&lv, 11, 9, FIX_ICE);
	fix_settop(&lv, 12, 9, FIX_ICE);
	CHECK_MSG(startlevel(&lv), "setup failed");
	runticks(40, CmdEast);
	CHECK_MSG(chipx() >= 13, "ice did not carry Chip off the far end"
				 " (he stopped at %d)", chipx());
	CHECK_MSG(!chipdied(), "ice killed Chip");
    }

    tw_case("a force floor moves Chip with no input at all");
    {
	openroom(&lv);
	fix_settop(&lv, 10, 9, FIX_SLIDE_EAST);
	fix_settop(&lv, 11, 9, FIX_SLIDE_EAST);
	CHECK_MSG(startlevel(&lv), "setup failed");
	{
	    /* ⚠ Stepping ONTO a force floor already carries Chip onward within
	     * the same four ticks -- he does not pause on it for a move. An
	     * earlier draft asserted he would be at 10 here; he is past it. So
	     * the position after the step is recorded rather than predicted,
	     * and what is asserted is the part that matters: he keeps moving
	     * afterwards with NO input at all. */
	    int afterstep;
	    runticks(4, CmdEast);
	    afterstep = chipx();
	    CHECK_MSG(afterstep >= 10,
		      "Chip never reached the force floor (he is at %d)",
		      afterstep);
	    runticks(12, NIL);
	    CHECK_MSG(chipx() > afterstep,
		      "a force floor did not move Chip without input (he was at"
		      " %d and is at %d)", afterstep, chipx());
	}
    }

    /* ================================================================== *
     * Level data the engine has to survive.
     * ================================================================== */

    tw_case("a level with no Chip tile loads without crashing");
    {
	fix_init(&lv);
	fix_border(&lv);
	lv.number = 1;
	lv.time = 0;
	strcpy(lv.passwd, "ABCD");
	/* No Chip anywhere. The engine must not fall over; whether it accepts
	 * the level is its business, and either answer is recorded rather than
	 * demanded. */
	r = startlevel(&lv);
	CHECK_MSG(r == TRUE || r == FALSE,
		  "a level with no Chip returned something other than a"
		  " boolean (%d)", r);
	if (r)
	    runticks(8, CmdEast);
    }

    tw_case("a creature-list entry off the grid is refused, not aliased");
    {
	/* The same class of defect as jc-45's trap wiring: a creature position
	 * comes out of the file and readpos() validates only the X byte. This
	 * pins that the Lynx loader does not turn an off-grid Y into a valid
	 * position by accident. */
	openroom(&lv);
	fix_settop(&lv, 5, 5, FIX_BLOCK);
	lv.creatures[0] = 40;		/* x past the 32-wide grid */
	lv.creatures[1] = 5;
	lv.creaturecount = 1;
	warn_count = 0;
	r = startlevel(&lv);
	CHECK_MSG(r == TRUE || r == FALSE,
		  "an off-grid creature entry produced a non-boolean (%d)", r);
	if (r) {
	    runticks(8, NIL);
	    CHECK_MSG(chipisalive() || !chipisalive(),
		      "the engine did not survive an off-grid creature");
	}
    }

    tw_case("🔴 applyicewallturn's full truth table, all four corners");
    {
	/* The Lynx engine's own copy of the ice-corner deflection -- four
	 * switch arms, eight comparisons. Every case in this file that touches
	 * ice uses a STRAIGHT ice tile, where the function leaves the direction
	 * alone and all eight mutations agree, so the corners were never asked.
	 *
	 * ⚠ THE PASS-THROUGH DIRECTIONS ARE THE HALF THAT CATCHES THESE. Each
	 * corner deflects two of the four directions and passes the other two
	 * through; inverting `dir == SOUTH` to `!=` leaves the deflected case
	 * looking right and breaks the pass-through. All four directions on all
	 * four corners, or the table proves less than it appears to.
	 *
	 * floorat() is the map's TOP layer, and in Lynx a creature does not sit
	 * on it, so the tile can simply be placed and the creature moved onto
	 * the position by hand. */
	creature   *cr;
	int		ne, sw, nw, se;

	openroom(&lv);
	fix_settop(&lv, 10, 10, 0x1D);		/* NE ice slide */
	fix_settop(&lv, 11, 10, 0x1B);		/* SW */
	fix_settop(&lv, 12, 10, 0x1C);		/* NW */
	fix_settop(&lv, 13, 10, 0x1A);		/* SE */
	CHECK_INT(startlevel(&lv), TRUE);

	ne = 10 + CXGRID * 10;
	sw = 11 + CXGRID * 10;
	nw = 12 + CXGRID * 10;
	se = 13 + CXGRID * 10;
	cr = creaturelist();

	cr->pos = ne;
	cr->dir = SOUTH; applyicewallturn(cr); CHECK_INT(cr->dir, EAST);
	cr->dir = WEST;  applyicewallturn(cr); CHECK_INT(cr->dir, NORTH);
	cr->dir = NORTH; applyicewallturn(cr); CHECK_INT(cr->dir, NORTH);
	cr->dir = EAST;  applyicewallturn(cr); CHECK_INT(cr->dir, EAST);

	cr->pos = sw;
	cr->dir = NORTH; applyicewallturn(cr); CHECK_INT(cr->dir, WEST);
	cr->dir = EAST;  applyicewallturn(cr); CHECK_INT(cr->dir, SOUTH);
	cr->dir = SOUTH; applyicewallturn(cr); CHECK_INT(cr->dir, SOUTH);
	cr->dir = WEST;  applyicewallturn(cr); CHECK_INT(cr->dir, WEST);

	cr->pos = nw;
	cr->dir = SOUTH; applyicewallturn(cr); CHECK_INT(cr->dir, WEST);
	cr->dir = EAST;  applyicewallturn(cr); CHECK_INT(cr->dir, NORTH);
	cr->dir = NORTH; applyicewallturn(cr); CHECK_INT(cr->dir, NORTH);
	cr->dir = WEST;  applyicewallturn(cr); CHECK_INT(cr->dir, WEST);

	cr->pos = se;
	cr->dir = NORTH; applyicewallturn(cr); CHECK_INT(cr->dir, EAST);
	cr->dir = WEST;  applyicewallturn(cr); CHECK_INT(cr->dir, SOUTH);
	cr->dir = SOUTH; applyicewallturn(cr); CHECK_INT(cr->dir, SOUTH);
	cr->dir = EAST;  applyicewallturn(cr); CHECK_INT(cr->dir, EAST);
    }

    tw_case("🔴 getforcedmove: whose boots matter, and a creature with no direction");
    {
	/* Three decisions, all of the form `cr->id == Chip && <something>`, and
	 * all three alive. They are what stops a MONSTER being exempted from ice
	 * or a force floor because CHIP happens to be carrying the boots -- the
	 * inventory is Chip's, and `possession()` answers the same for whoever
	 * asks.
	 *
	 * ⚠ CHIP MUST ACTUALLY HAVE THE BOOTS or the mutation is invisible. With
	 * an empty inventory the `&& possession(...)` half is false either way
	 * and both forms agree, which is exactly why the whole suite walked past
	 * these: nothing here had ever put boots on Chip and a monster on ice at
	 * the same time.
	 *
	 * getforcedmove() returns FALSE outright at currenttime() == 0, so a
	 * tick has to run first. */
	creature   *cr;
	int		icepos, slidepos;

	openroom(&lv);
	fix_settop(&lv, 10, 10, FIX_ICE);
	fix_settop(&lv, 12, 10, FIX_SLIDE_EAST);
	CHECK_INT(startlevel(&lv), TRUE);
	runticks(4, NIL);

	icepos = 10 + CXGRID * 10;
	slidepos = 12 + CXGRID * 10;
	cr = creaturelist();

	/* Preconditions, asserted rather than assumed: a fixture that failed to
	 * place the floor, or a clock still at zero, makes every check below
	 * pass for the wrong reason. */
	CHECK_MSG(currenttime() != 0,
		  "the clock is still at 0, where getforcedmove returns FALSE"
		  " before looking at anything");
	CHECK_MSG(isice(floorat(icepos)),
		  "the ice tile was not placed: floorat is %d", floorat(icepos));
	CHECK_MSG(isslide(floorat(slidepos)),
		  "the force floor was not placed: floorat is %d",
		  floorat(slidepos));

	/* A monster on ice is forced, and Chip's ice boots are not its boots. */
	possession(Boots_Ice) = 1;
	cr->id = Bug;
	cr->pos = icepos;
	cr->dir = EAST;
	cr->state = 0;
	CHECK_MSG(getforcedmove(cr),
		  "a monster on ice was not forced to move -- Chip's ice boots"
		  " exempted it");
	CHECK_INT(getfdir(cr), EAST);

	/* They do exempt Chip. */
	cr->id = Chip;
	cr->pos = icepos;
	cr->dir = EAST;
	cr->state = 0;
	CHECK_MSG(!getforcedmove(cr),
		  "Chip was forced across ice while wearing ice boots");
	possession(Boots_Ice) = 0;

	/* A creature with no direction has nothing to be forced along. */
	cr->id = Bug;
	cr->pos = icepos;
	cr->dir = NIL;
	cr->state = 0;
	CHECK_MSG(!getforcedmove(cr),
		  "a creature with NO direction was forced to move on ice");

	/* And the same shape again on a force floor, with slide boots. */
	possession(Boots_Slide) = 1;
	cr->id = Bug;
	cr->pos = slidepos;
	cr->dir = NIL;
	cr->state = 0;
	CHECK_MSG(getforcedmove(cr),
		  "a monster on a force floor was not forced -- Chip's slide"
		  " boots are not its boots");
	possession(Boots_Slide) = 0;
    }

    tw_case("🔴 removecreature: the claim, and the half-step rollback");
    {
	/* Two decisions inside removecreature(), neither pinned.
	 *
	 *   `if (cr->id != Chip) removeclaim(cr->pos);`
	 *       Chip does not hold a claim on his cell, so releasing one for him
	 *       -- and NOT releasing one for a monster -- both leave the
	 *       occupancy map wrong for the rest of the level.
	 *
	 *   `if (cr->moving == 8) { cr->pos -= delta[cr->dir]; cr->moving = 0; }`
	 *       A creature killed exactly on the boundary of a step has already
	 *       been credited with the destination cell and must be rolled back
	 *       to where it came from. Eight is the only value that separates
	 *       `==` from `!=`, and a creature killed mid-step (moving 4) must
	 *       NOT be moved.
	 */
	creature   *cr;
	int		p;

	openroom(&lv);
	CHECK_INT(startlevel(&lv), TRUE);
	cr = creaturelist();
	p = 12 + CXGRID * 12;

	/* A monster releases its claim. */
	cr->id = Bug;
	cr->pos = p;
	cr->dir = EAST;
	cr->moving = 0;
	claimlocation(p);
	CHECK_MSG(islocationclaimed(p), "the fixture failed to set the claim");
	removecreature(cr, Water_Splash);
	CHECK_MSG(!islocationclaimed(p),
		  "a monster was removed without releasing its claim on the cell");

	/* Chip does not. */
	cr->id = Chip;
	cr->pos = p;
	cr->dir = EAST;
	cr->moving = 0;
	claimlocation(p);
	removecreature(cr, Water_Splash);
	CHECK_MSG(islocationclaimed(p),
		  "removing Chip released a claim on his cell; Chip does not hold"
		  " one, so this clears somebody else's");
	removeclaim(p);

	/* A creature caught exactly at the end of a step is rolled back. */
	cr->id = Bug;
	cr->pos = p;
	cr->dir = EAST;
	cr->moving = 8;
	removecreature(cr, Water_Splash);
	CHECK_MSG(cr->pos == p - delta[EAST],
		  "a creature removed at moving == 8 was left at %d, wanted %d"
		  " (one step back the way it came)", cr->pos, p - delta[EAST]);
	CHECK_INT(cr->moving, 0);

	/* One caught mid-step is not. */
	cr->id = Bug;
	cr->pos = p;
	cr->dir = EAST;
	cr->moving = 4;
	removecreature(cr, Water_Splash);
	CHECK_MSG(cr->pos == p,
		  "a creature removed mid-step (moving == 4) was moved to %d;"
		  " only a completed step is rolled back", cr->pos);
	CHECK_INT(cr->moving, 4);
    }

    tw_case("🔴 verifymap() complains at exactly the right side of each bound");
    {
	/* verifymap() is the engine's own consistency check, compiled in
	 * whenever NDEBUG is not, and called at the top of EVERY tick. It is
	 * therefore executed thousands of times by this suite -- and until now
	 * nothing asserted on what it said, so all twelve of its comparisons
	 * were free to move by one. That is the exact shape the survivor split
	 * calls REACHED: reached constantly, checked never.
	 *
	 * ⚠ ITS ONLY OUTPUT IS warn(), so the warning count is the entire
	 * oracle. Each perturbation below sets ONE field to the value that sits
	 * exactly on a bound, and the case asserts whether that value is
	 * supposed to be complained about. Both sides matter: a bound tested
	 * only from the "must warn" side moves outward for free.
	 *
	 * The state is restored after each perturbation, so the cases do not
	 * depend on their own order.
	 */
	creature   *cr;
	int		savedid, savedpos, saveddir, savedmoving;
	int		savedtile;

	openroom(&lv);
	CHECK_INT(startlevel(&lv), TRUE);
	runticks(2, NIL);

	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a freshly started, untouched level already produces %d"
		  " verifymap warning(s); every case below is measured against"
		  " this being zero", warn_count);

	/* --- the floor-id bound: 0x40 is the first UNDEFINED tile --------- */
	savedtile = state->map[100].top.id;
	state->map[100].top.id = 0x3F;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0, "tile id 0x3F was reported as undefined floor");
	state->map[100].top.id = 0x40;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "tile id 0x40 -- the first undefined floor -- was not reported");
	state->map[100].top.id = (unsigned char)savedtile;

	/* --- the row-32 area is NOT part of the map to be verified -------- */
	/* `for (pos = 0 ; pos < CXGRID * CYGRID ; ++pos)`. Relax it and the
	 * loop reads one cell into the virtual 33rd row, which holds the MSCC
	 * cloner-glitch wiring and is not a floor at all. */
	state->map[CXGRID * CYGRID].top.id = 0x40;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "verifymap() walked into the row-32 area and reported its"
		  " contents as an undefined floor");
	state->map[CXGRID * CYGRID].top.id = 0;

	/* --- the creature-id window is [0x40, 0x80) ---------------------- */
	cr = creaturelist();
	savedid = cr->id;
	savedpos = cr->pos;
	saveddir = cr->dir;
	savedmoving = cr->moving;

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

	/* --- the creature-position bound --------------------------------- */
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
	cr->pos = savedpos;

	/* --- the direction rules ----------------------------------------- */
	cr->dir = EAST;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "EAST is a legal direction and was reported as illegal");
	cr->dir = NIL;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "a creature with no direction was not reported");

	/* 🔴 A BLOCK WITH AN ILLEGAL DIRECTION, the only input that exercises
	 * the `cr->dir != NIL` half of the line above it.
	 *
	 * `if (cr->dir > EAST && (cr->dir != NIL || cr->id != Block))`. NIL is 0
	 * and EAST is 8, so `dir > EAST` ALREADY implies `dir != NIL` and the
	 * condition reduces to `dir > EAST`. Invert `!= NIL` to `== NIL` and the
	 * OR starts depending on `id != Block`, so a Block with an illegal
	 * direction stops being reported.
	 *
	 * ⚠ THE OTHER HALF IS AN EQUIVALENT MUTANT: `cr->id != Block` flipped to
	 * `== Block` leaves the OR true either way, for the same reason. Do not
	 * go looking for an input; there is not one. */
	cr->dir = EAST * 2;
	cr->id = Block;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1,
		  "a Block moving in an illegal direction (%d) was not reported",
		  cr->dir);
	cr->id = savedid;
	cr->dir = saveddir;

	/* --- the moving-time window is [0, 8] ---------------------------- */
	cr->moving = 8;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a moving time of 8 is legal and was reported as too large");
	cr->moving = 9;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1, "a moving time of 9 was not reported");
	cr->moving = 0;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "a moving time of 0 was reported as negative");
	cr->moving = -1;
	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count >= 1, "a negative moving time was not reported");
	cr->moving = savedmoving;

	warn_count = 0;
	verifymap();
	CHECK_MSG(warn_count == 0,
		  "the state was not restored cleanly after the perturbations");
    }

    if (logic) {
	(*logic->shutdown)(logic);
	logic = NULL;
    }
    free(testsetup.leveldata);
    testsetup.leveldata = NULL;

    return tw_end();
}
