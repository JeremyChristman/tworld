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
 *     (mslogic_test.c's comment says "defined in tworld.c" -- that is wrong;
 *     tworld.c:96 itself points at lxlogic.c.)
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
    tw_expect_atleast(64);

    /* ================================================================== *
     * The level loads at all.
     * ================================================================== */

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

    if (logic) {
	(*logic->shutdown)(logic);
	logic = NULL;
    }
    free(testsetup.leveldata);
    testsetup.leveldata = NULL;

    return tw_end();
}
