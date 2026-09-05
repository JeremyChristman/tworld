/* play_test.c: doturn() and the game-state bookkeeping in play.c.
 *
 * MOD (Jeremy). play.c is 565 lines and had NO unit test. Only the end-to-end
 * layer touched it, with two solutions, and only through the whole program.
 *
 * That mattered more than the line count suggests, because play.c is the seam
 * between a stored solution and an engine. doturn() decides, every single tick,
 * whether the command came from the player or from a recorded move list; when a
 * replay has run past its own end; and what gets appended to the move list that
 * will later be written to a .tws. A defect here does not crash -- it silently
 * records or replays the wrong thing, which is the exact failure mode this fork
 * exists to prevent.
 *
 * It is also where jc-47 lived: a 64-byte leak on every failed playback, in
 * prepareplayback(), found by LeakSanitizer rather than by any test. The
 * behavioral half of that fix is pinned below.
 *
 * 🔴 THE FAKE ENGINE IS THE POINT. doturn() calls (*logic->advancegame)(logic),
 * and driving a REAL engine here would test the engine -- which mslogic_test.c,
 * lxlogic_test.c and the golden master already do far better. Installing a fake
 * gamelogic instead makes play.c's own decisions observable: the fake records
 * exactly what currentinput held when it was called, so a case can assert which
 * command actually reached the engine on a given tick. That is the question
 * these tests are about, and no other layer can ask it.
 *
 * ⚠ WHAT THIS DELIBERATELY DOES NOT COVER. drawscreen(), setgameplaymode() and
 * the display plumbing are stubbed away: they are calls into oshw, and asserting
 * that a stub was called proves nothing about the program. checksolution() and
 * replacesolution() touch the real solution file surface and belong with the
 * .tws tests.
 *
 * TESTLANG: c
 *
 * play.c, solution.c, fileio.c and encoding.c are compiled only as C by CMake
 * (CMakeLists.txt), and they rely on C's implicit void* conversion through
 * err.h's x_alloc. See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-use-after-free -Wno-unused-parameter
 *
 * -Wno-use-after-free is for solution.c:462, a documented GCC false positive on
 * a guarded realloc failure path -- the same suppression solution_test.c
 * carries, and for the same line.
 *
 * ⚠ -Wno-unused-parameter is for a REAL ODDITY in play.c, not a false positive,
 * and it is worth knowing about: writesteppingstring(buf, stepping) ignores its
 * `stepping` argument entirely and formats state.stepping instead
 * (play.c:220-226). Nothing is broken today, because both call sites pass
 * state.stepping -- setstepping() assigns it one line before calling. But the
 * function's signature promises something it does not do, and the first caller
 * to pass anything else will silently get a string describing a different
 * stepping. Upstream's. Left alone under the don't-reformat-upstream rule
 * (CLAUDE.md section 9); recorded here so it is not rediscovered as a mystery.
 *
 * Neither flag is cover for anything in this file.
 */

#include	"tw_test.h"
#include	"tw_fixture.h"

/* logic.h for gamelogic, which the fake engine below implements. Without it
 * `gamelogic *` is an unknown type, C infers int, and the fake links against
 * the real declarations with a conflicting signature. */
#include	"../logic.h"
#include	"../oshw.h"
#include	"../res.h"
#include	"../settings.h"

/* The high-fidelity flag, declared extern in logic.h and defined by lxlogic.c
 * in the real build. Neither engine is compiled in here, so this file must
 * define it. Left FALSE, as every other test does. */
int	pedanticmode = 0;

/* --- the settings surface, faked with a tiny store ---------------------- *
 *
 * play.c's death counter reads and writes settings, and its rules are worth
 * testing precisely BECAUSE they are defensive: it clamps on read as well as on
 * write, suppresses itself when settings cannot be read at all, and saturates
 * rather than wrapping. A fake store lets a case put a hostile value in --
 * "deathcount=-5", or one above the ceiling -- which is the case a real
 * settings file makes awkward to arrange. */

static int	fake_readable = TRUE;
static int	fake_optedin = TRUE;
static int	fake_intsetting = -1;	/* -1 is what a real absent key answers */
static int	saves = 0;
static int	lastdeathcountchanged = -999;

int settingsarereadable(void) { return fake_readable; }
int settingoptedin(char const *key) { (void)key; return fake_optedin; }
int getintsetting(char const *key) { (void)key; return fake_intsetting; }
void setintsetting(char const *key, int value) { (void)key; fake_intsetting = value; }
void savesettings(void) { ++saves; }
void deathcountchanged(int count) { lastdeathcountchanged = count; }

/* --- the rest of the surface play.c links against, stubbed -------------- */

static int	faketick = 0;

int gettickcount(void) { return faketick; }

/* ⚠ These signatures must match oshw.h and res.h EXACTLY. They are not free to
 * be convenient: the real declarations are already in scope through play.c's
 * includes, so a stub that differs is a compile error rather than a silent
 * mismatch -- which is the good outcome, and the reason to keep them in step. */
void settimer(int action) { (void)action; }
void settimersecond(int ms) { (void)ms; }
void setsoundeffects(int action) { (void)action; }
void playsoundeffects(unsigned long sfx) { (void)sfx; }
int setkeyboardrepeat(int enable) { (void)enable; return TRUE; }
int setkeyboardarrowsrepeat(int enable) { (void)enable; return TRUE; }
int setdisplaymsg(char const *msg, int msecs, int bold)
{
    (void)msg; (void)msecs; (void)bold; return TRUE;
}
int displaygame(struct gamestate const *s, int timeleft, int besttime,
		int showinit)
{
    (void)s; (void)timeleft; (void)besttime; (void)showinit; return TRUE;
}
int creategamedisplay(void) { return TRUE; }
int loadgameresources(int ruleset) { (void)ruleset; return TRUE; }

/* From series.c, reached only by solution.c's readsolutions() -- which nothing
 * here calls. series.c is not compiled in because it would drag the whole
 * series-enumeration surface in for one symbol. Answering "no such level" is
 * the honest stub: if a future case does reach it, the case will fail rather
 * than quietly matching level 1. */
int findlevelinseries(gameseries const *series, int number, char const *passwd)
{
    (void)series; (void)number; (void)passwd;
    return -1;
}

/* --- the fake engine ---------------------------------------------------- */

static int	advance_calls = 0;
static int	advance_result = 0;	/* what the fake reports back to doturn */
static short	seen_input[64];		/* currentinput as the engine saw it */
static int	seen_count = 0;
static short	force_lastmove = NIL;	/* what the fake claims Chip did */

static int fake_initgame(gamelogic *l) { (void)l; return TRUE; }
static int fake_endgame(gamelogic *l) { (void)l; return TRUE; }
static void fake_shutdown(gamelogic *l) { (void)l; }

static int fake_advancegame(gamelogic *l)
{
    ++advance_calls;
    if (seen_count < (int)(sizeof seen_input / sizeof *seen_input))
	seen_input[seen_count++] = l->state->currentinput;
    /* A real engine sets lastmove when Chip actually moved. doturn() reads it
     * to decide what to record, so the fake has to be able to say so. */
    l->state->lastmove = force_lastmove;
    return advance_result;
}

static gamelogic fakelogic;

static gamelogic *fake_startup(void)
{
    memset(&fakelogic, 0, sizeof fakelogic);
    fakelogic.ruleset = Ruleset_MS;
    fakelogic.initgame = fake_initgame;
    fakelogic.advancegame = fake_advancegame;
    fakelogic.endgame = fake_endgame;
    fakelogic.shutdown = fake_shutdown;
    return &fakelogic;
}

gamelogic *mslogicstartup(void) { return fake_startup(); }
gamelogic *lynxlogicstartup(void) { return fake_startup(); }

/* --- the source under test ---------------------------------------------- */

#include	"../random.c"
#include	"../fileio.c"
#include	"../encoding.c"
#include	"../solution.c"
#include	"../play.c"

/* --- the error surface, stubbed ---------------------------------------- *
 *
 * AFTER the includes: err.h declares these, and fileio.c/solution.c reference
 * them. warn_ counts rather than prints -- doturn() warns deliberately when a
 * replay gets ahead of its own move list, and a case below asserts that it
 * does, so the count is an oracle rather than noise. */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;
static int	errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *pfx, char const *fmt, ...)
{
    (void)pfx; (void)fmt; ++errmsg_count;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* --- the harness -------------------------------------------------------- */

static gamesetup	testgame;

/* Put play.c's own file-scope state into a known condition and install the
 * fake engine. Reaching `state` and `logic` directly is what including the
 * source under test buys (docs/adr/0003); initgamestate() cannot be used here
 * because it wants a real level to expand. */
static void setupgame(int ruleset, int replay)
{
    memset(&testgame, 0, sizeof testgame);
    testgame.number = 1;
    testgame.besttime = 0;

    memset(&state, 0, sizeof state);
    state.game = &testgame;
    state.ruleset = ruleset;
    state.replay = replay;
    state.currenttime = -1;
    state.timeoffset = 0;
    state.currentinput = NIL;
    state.lastmove = NIL;
    state.stepping = 0;
    initmovelist(&state.moves);

    logic = fake_startup();
    logic->state = &state;

    faketick = 0;
    advance_calls = 0;
    advance_result = 0;
    seen_count = 0;
    force_lastmove = NIL;
    warn_count = 0;
    errmsg_count = 0;
}

/* One tick at a given clock reading. doturn() takes the time from
 * gettickcount(), not from an argument, so the fake clock is how a test
 * controls it. */
static int tickat(int when, int cmd)
{
    faketick = when;
    return doturn(cmd);
}

/* --- live play: where the command comes from ---------------------------- */

static void test_liveinput(void)
{
    tw_case("in live play the command reaches the engine");
    setupgame(Ruleset_MS, -1);
    tickat(0, CmdEast);
    CHECK_INT(advance_calls, 1);
    CHECK_MSG(seen_count == 1 && seen_input[0] == CmdEast,
	      "the engine saw %d, not CmdEast", seen_count ? seen_input[0] : -1);

    tw_case("CmdPreserve leaves the previous command standing");
    /* 🔴 This is a real rule, not a curiosity: `if (cmd != CmdPreserve)` is how
     * the GUI says "nothing new this tick, keep what you had". Treating
     * CmdPreserve as an ordinary command would overwrite currentinput with the
     * sentinel and Chip would stop dead whenever the player held a key without
     * generating a fresh event. */
    setupgame(Ruleset_MS, -1);
    tickat(0, CmdNorth);
    tickat(1, CmdPreserve);
    CHECK_INT(advance_calls, 2);
    CHECK_MSG(seen_input[1] == CmdNorth,
	      "the second tick saw %d; CmdPreserve should have left CmdNorth"
	      " in place", seen_input[1]);

    tw_case("the clock comes from gettickcount(), not from the caller");
    setupgame(Ruleset_MS, -1);
    tickat(37, CmdWest);
    CHECK_INT(state.currenttime, 37);
}

/* --- live play: what gets recorded -------------------------------------- */

static void test_recording(void)
{
    tw_case("a move Chip actually made is appended to the move list");
    setupgame(Ruleset_MS, -1);
    force_lastmove = CmdSouth;
    tickat(4, CmdSouth);
    CHECK_INT(state.moves.count, 1);
    if (state.moves.count == 1) {
	CHECK_INT(state.moves.list[0].when, 4);
	CHECK_INT(state.moves.list[0].dir, CmdSouth);
    }

    tw_case("lastmove is cleared after recording, so one move is stored once");
    /* Without the reset, every subsequent tick would append the same move
     * again and the .tws would fill with phantom entries. */
    CHECK_INT(state.lastmove, NIL);
    force_lastmove = NIL;
    tickat(5, CmdSouth);
    CHECK_MSG(state.moves.count == 1,
	      "a tick where Chip did not move appended anyway: count is %d",
	      state.moves.count);

    tw_case("a tick where Chip did not move records nothing");
    setupgame(Ruleset_MS, -1);
    force_lastmove = NIL;
    tickat(0, CmdEast);
    tickat(1, CmdEast);
    CHECK_INT(state.moves.count, 0);

    tw_case("nothing is recorded during a replay");
    /* ⚠ The guard is `state.replay < 0`. If a replay recorded its own moves,
     * playing a solution back and then saving would double every move in it. */
    setupgame(Ruleset_MS, 0);
    force_lastmove = CmdEast;
    tickat(0, CmdEast);
    CHECK_INT(state.moves.count, 0);
}

/* --- replay: the input comes from the list, never from the caller ------- */

static void test_replay(void)
{
    action a;

    tw_case("a recorded move is delivered on its own tick, and only then");
    setupgame(Ruleset_MS, 0);
    a.when = 2; a.dir = CmdWest;  addtomovelist(&state.moves, a);
    a.when = 5; a.dir = CmdNorth; addtomovelist(&state.moves, a);

    tickat(0, CmdEast);
    CHECK_MSG(seen_input[0] == NIL,
	      "tick 0 fed the engine %d; nothing was recorded for it",
	      seen_input[0]);
    tickat(2, CmdEast);
    CHECK_MSG(seen_input[1] == CmdWest,
	      "tick 2 fed the engine %d, not the recorded CmdWest",
	      seen_input[1]);

    tw_case("🔴 the cmd argument is IGNORED during a replay");
    /* This is the fact CLAUDE.md section 3.5 is built on: because doturn()
     * never looks at cmd while replaying, a full corpus verification says
     * NOTHING about the keyboard path. A green corpus run is quoted often
     * enough that the reason it cannot cover input() deserves a test of its
     * own. */
    CHECK_MSG(seen_input[0] == NIL && seen_input[1] == CmdWest,
	      "the caller's CmdEast leaked into a replay");

    tw_case("the replay index advances only when a move is consumed");
    CHECK_INT(state.replay, 1);
    tickat(3, CmdEast);
    CHECK_INT(state.replay, 1);
    tickat(5, CmdEast);
    CHECK_INT(state.replay, 2);

    tw_case("getting ahead of the recorded solution warns");
    /* A tick number past the next recorded move means the clock and the list
     * have diverged -- the solution cannot replay faithfully from here, and
     * saying so is the only warning a desync of this kind ever produces. */
    setupgame(Ruleset_MS, 0);
    a.when = 1; a.dir = CmdWest; addtomovelist(&state.moves, a);
    tickat(9, CmdEast);
    CHECK_MSG(warn_count == 1, "expected exactly one warning, got %d",
	      warn_count);
}

/* --- replay: running past the end --------------------------------------- */

static void test_replay_overrun(void)
{
    tw_case("a replay past its last move keeps going up to the best time");
    /* Once the move list is exhausted the level is not over: Chip may still be
     * sliding, or a creature may still be about to kill him. doturn() lets the
     * clock run to the recorded best time before giving up. */
    setupgame(Ruleset_MS, 0);
    testgame.besttime = 20;
    state.replay = 0;			/* an empty list: already past the end */
    CHECK_INT(tickat(5, CmdEast), 0);
    CHECK_INT(advance_calls, 1);

    tw_case("a replay that outruns the best time is judged a failure");
    /* `n = currenttime + timeoffset - 1; if (n > besttime) return -1;` -- the
     * engine is never even called on that tick. */
    setupgame(Ruleset_MS, 0);
    testgame.besttime = 20;
    CHECK_INT(tickat(22, CmdEast), -1);
    CHECK_MSG(advance_calls == 0,
	      "the engine was advanced %d time(s) on a tick that had already"
	      " overrun the best time", advance_calls);

    tw_case("timeoffset counts toward the overrun");
    setupgame(Ruleset_MS, 0);
    testgame.besttime = 20;
    state.timeoffset = 15;
    CHECK_INT(tickat(10, CmdEast), -1);
}

/* --- the timer ceiling --------------------------------------------------- */

static void test_timercap(void)
{
    tw_case("the tick counter's maximum ends the game rather than wrapping");
    /* MAXIMUM_TICK_COUNT is not decoration: `when` in a .tws move is a fixed
     * width, and a clock allowed past this point would write move times that
     * cannot be read back. */
    setupgame(Ruleset_MS, -1);
    CHECK_INT(tickat(MAXIMUM_TICK_COUNT, CmdEast), -1);
    CHECK_MSG(advance_calls == 0,
	      "the engine was advanced past MAXIMUM_TICK_COUNT");
    CHECK_MSG(errmsg_count == 1, "expected one error message, got %d",
	      errmsg_count);

    tw_case("one tick below the maximum still plays");
    setupgame(Ruleset_MS, -1);
    CHECK_INT(tickat(MAXIMUM_TICK_COUNT - 1, CmdEast), 0);
    CHECK_INT(advance_calls, 1);
}

/* --- the engine's verdict is passed straight through --------------------- */

static void test_result(void)
{
    tw_case("doturn() returns what the engine returned");
    setupgame(Ruleset_MS, -1);
    advance_result = +1;
    CHECK_INT(tickat(0, CmdEast), +1);
    advance_result = -1;
    CHECK_INT(tickat(1, CmdEast), -1);
    advance_result = 0;
    CHECK_INT(tickat(2, CmdEast), 0);
}

/* --- secondsplayed ------------------------------------------------------- */

static void test_secondsplayed(void)
{
    tw_case("seconds played counts the offset as well as the clock");
    setupgame(Ruleset_MS, -1);
    state.currenttime = TICKS_PER_SECOND * 3;
    state.timeoffset = 0;
    CHECK_INT(secondsplayed(), 3);
    state.timeoffset = TICKS_PER_SECOND * 2;
    CHECK_INT(secondsplayed(), 5);

    tw_case("a part-second does not round up");
    state.currenttime = TICKS_PER_SECOND * 3 - 1;
    state.timeoffset = 0;
    CHECK_INT(secondsplayed(), 2);
}

/* --- stepping ------------------------------------------------------------ */

static void test_stepping(void)
{
    tw_case("changestepping wraps within 0..7");
    setupgame(Ruleset_Lynx, -1);
    state.stepping = 6;
    changestepping(3, FALSE);
    CHECK_INT(state.stepping, 1);

    tw_case("a negative stepping is treated as zero before the delta");
    setupgame(Ruleset_Lynx, -1);
    state.stepping = -1;
    changestepping(2, FALSE);
    CHECK_INT(state.stepping, 2);

    tw_case("🔴 the MS ruleset only has even-four steppings");
    /* `n &= ~3` -- MS steppings are 0 and 4 only. Losing this would offer a
     * player a stepping the MS engine cannot actually run, and any solution
     * recorded at it would replay wrong. */
    setupgame(Ruleset_MS, -1);
    state.stepping = 0;
    changestepping(1, FALSE);
    CHECK_MSG(state.stepping == 0, "MS accepted stepping %d", state.stepping);
    changestepping(4, FALSE);
    CHECK_MSG(state.stepping == 4, "MS refused stepping 4, got %d",
	      state.stepping);
    changestepping(3, FALSE);
    CHECK_MSG(state.stepping == 4, "MS accepted stepping %d", state.stepping);

    tw_case("Lynx keeps the odd steppings MS masks away");
    setupgame(Ruleset_Lynx, -1);
    state.stepping = 0;
    changestepping(3, FALSE);
    CHECK_INT(state.stepping, 3);
}

/* --- the death counter --------------------------------------------------- */

static void test_deathcount(void)
{
    tw_case("the counter is inactive when settings cannot be read");
    /* Suppressed rather than shown as zero: an unreadable settings file means
     * the total is UNKNOWN, and displaying "Deaths: 0" would be a lie the
     * player has no way to detect. */
    fake_readable = FALSE;
    fake_optedin = TRUE;
    CHECK_INT(deathcounteractive(), FALSE);

    tw_case("the counter is inactive when it has not been opted into");
    fake_readable = TRUE;
    fake_optedin = FALSE;
    CHECK_INT(deathcounteractive(), FALSE);
    fake_optedin = TRUE;
    CHECK_INT(deathcounteractive(), TRUE);

    tw_case("a negative stored count reads as zero, not as a negative");
    /* getintsetting() answers -1 for absent, unparsable and out-of-range
     * alike, so a fresh install and a hand-edited "deathcount=-5" both arrive
     * here negative. Clamping on READ is what stops a fresh install showing
     * "Deaths: 0" and then jumping. */
    fake_intsetting = -5;
    CHECK_INT(getdeathcount(), 0);
    fake_intsetting = -1;
    CHECK_INT(getdeathcount(), 0);

    tw_case("a stored count above the ceiling reads as the ceiling");
    fake_intsetting = DEATHCOUNT_MAX + 1000;
    CHECK_INT(getdeathcount(), DEATHCOUNT_MAX);

    tw_case("an ordinary stored count is returned unchanged");
    fake_intsetting = 42;
    CHECK_INT(getdeathcount(), 42);

    tw_case("setting a count clamps it and writes immediately");
    /* Written the instant it changes rather than at exit: savesettings() runs
     * from an atexit handler that a crash skips. */
    saves = 0;
    fake_intsetting = 0;
    setdeathcount(-3);
    CHECK_INT(fake_intsetting, 0);
    setdeathcount(DEATHCOUNT_MAX + 5);
    CHECK_INT(fake_intsetting, DEATHCOUNT_MAX);
    CHECK_MSG(saves == 2, "expected two writes, got %d", saves);

    tw_case("recording a death increments, and saturates at the ceiling");
    /* ⚠ KNOW EXACTLY WHAT THIS CHECK CAN AND CANNOT SEE -- it was measured.
     * The ceiling is enforced TWICE: recorddeath() guards with
     * `if (n < DEATHCOUNT_MAX)` and setdeathcount() clamps again on the way in.
     * The two are redundant, so removing EITHER ONE ALONE changes nothing
     * observable and this case still passes; removing BOTH makes it fail (3
     * checks). It therefore pins the ceiling as a behavior, not the particular
     * guard that implements it. That is worth having and worth not overstating:
     * do not read a green run here as cover for the guard in recorddeath(). */
    fake_readable = TRUE;
    fake_optedin = TRUE;
    fake_intsetting = 7;
    recorddeath();
    CHECK_INT(fake_intsetting, 8);
    fake_intsetting = DEATHCOUNT_MAX;
    recorddeath();
    CHECK_MSG(fake_intsetting == DEATHCOUNT_MAX,
	      "the counter wrapped past its ceiling to %d", fake_intsetting);

    tw_case("recording a death does nothing while the counter is inactive");
    fake_optedin = FALSE;
    fake_intsetting = 7;
    saves = 0;
    recorddeath();
    CHECK_INT(fake_intsetting, 7);
    CHECK_MSG(saves == 0, "an inactive counter still wrote settings");

    tw_case("refreshing an inactive counter reports -1, not a count");
    fake_optedin = FALSE;
    refreshdeathcount();
    CHECK_INT(lastdeathcountchanged, -1);
    fake_optedin = TRUE;
    fake_intsetting = 12;
    refreshdeathcount();
    CHECK_INT(lastdeathcountchanged, 12);

    /* Leave the fake store where the other cases expect it. */
    fake_readable = TRUE;
    fake_optedin = TRUE;
    fake_intsetting = -1;
}

/* --- prepareplayback ----------------------------------------------------- */

static void test_prepareplayback(void)
{
    tw_case("a level with no stored solution cannot be played back");
    setupgame(Ruleset_MS, -1);
    testgame.solutionsize = 0;
    CHECK_INT(prepareplayback(), FALSE);
    CHECK_MSG(state.replay == -1,
	      "a failed playback left replay armed at %d", state.replay);

    tw_case("a solution record too short to hold a header is refused");
    /* jc-47's site. The value here is not the return code -- it is that the
     * failure path is taken at all, on every malformed record, which is what
     * made the leak reachable from any bad .tws. */
    setupgame(Ruleset_MS, -1);
    {
	static unsigned char tiny[4] = { 0, 0, 0, 0 };
	testgame.solutionsize = sizeof tiny;
	testgame.solutiondata = tiny;
	CHECK_INT(prepareplayback(), FALSE);
	CHECK_MSG(state.replay == -1,
		  "a refused solution still armed replay at %d", state.replay);
    }

    tw_case("a failed playback leaves the game's own move list intact");
    /* ⚠ prepareplayback() only adopts solution.moves after expandsolution()
     * has succeeded. If it swapped first and validated second, a bad .tws would
     * destroy the moves the player had already made. */
    setupgame(Ruleset_MS, -1);
    {
	action a;
	static unsigned char tiny[4] = { 0, 0, 0, 0 };
	a.when = 3; a.dir = CmdEast;
	addtomovelist(&state.moves, a);
	testgame.solutionsize = sizeof tiny;
	testgame.solutiondata = tiny;
	CHECK_INT(prepareplayback(), FALSE);
	CHECK_INT(state.moves.count, 1);
    }
}

int main(void)
{
    tw_begin("play_test.c");

    test_liveinput();
    test_recording();
    test_replay();
    test_replay_overrun();
    test_timercap();
    test_result();
    test_secondsplayed();
    test_stepping();
    test_deathcount();
    test_prepareplayback();

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(63);
    return tw_end();
}
