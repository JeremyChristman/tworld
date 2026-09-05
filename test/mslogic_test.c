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
    tw_expect_atleast(122);

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

    if (logic)
	(*logic->shutdown)(logic);
    free(testsetup.leveldata);
    testsetup.leveldata = NULL;

    return tw_end();
}
