/* dirinput_test.c: the direction-key arbitration introduced in jc-43.
 *
 * Compiles generic/dirinput.c directly, as C, against the real defs.h -- so
 * these cases run against the same code and the same Cmd* values the game uses,
 * not a reimplementation. Nothing is stubbed because the code under test is
 * pure. Building this as C also keeps the dual-language rule at the head of
 * generic/in.c honest: the shipped Qt build compiles that file as C++, and a
 * C++-only construct sneaking in here would break the SDL build silently.
 *
 * This file covers resolvedirections() and commandaxis(). The scan loop that
 * FILLS the dirsurvivors struct is covered by test/input_test.c, which drives
 * the real input(); several behaviors people expect to find here -- notably
 * two keys on the SAME axis, which the scan loop has already collapsed by the
 * time this code runs -- can only be tested there.
 *
 * Run with test/run-tests.ps1.
 *
 * WHY THIS EXISTS: the defect these cases cover is silent. A wrong arbitration
 * does not crash or look broken -- it makes a block slap fail to fire, which
 * surfaces months later as a level that cannot be solved.
 */

#include	<stdio.h>
#include	<string.h>

#include	"../generic/dirinput.c"

/* The two behavior modes, spelled out so the cases read the way the rulesets do.
 */
#define	LYNX	1 /* joystick behavior: diagonals exist, block slapping works */
#define	MSDOS	0 /* keyboard behavior: one direction only, ever */

static int failures = 0;
static int checks = 0;

static char const* cmdname(int cmd) {
    switch (cmd) {
        case 0: return "none";
        case CmdNorth: return "N";
        case CmdSouth: return "S";
        case CmdWest: return "W";
        case CmdEast: return "E";
        case CmdNorth | CmdWest: return "NW";
        case CmdNorth | CmdEast: return "NE";
        case CmdSouth | CmdWest: return "SW";
        case CmdSouth | CmdEast: return "SE";
    }
    return "?";
}

static void check(char const* what, int got, int want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    printf("FAIL: %s\n      got %s (%d), wanted %s (%d)\n",
           what, cmdname(got), got, cmdname(want), want);
}

/* Builders that route by command rather than by argument position.
 *
 * The earlier positional make() was a trap waiting to spring: commands are
 * 1, 2, 4, 8 and press stamps are 1, 2, 3, 4, so every argument was a small
 * integer, a transposition compiled silently, and it was possible to express
 * states the program cannot produce -- East sitting in the vertical slot, for
 * one. Here the command picks its own slot and an impossible state cannot be
 * written down.
 */
static dirsurvivors nothing(void) {
    dirsurvivors s;
    memset(&s, 0, sizeof s);
    return s;
}

static void hold(dirsurvivors* s, int cmd, unsigned order) {
    int axis = commandaxis(cmd);
    s->held[axis] = cmd;
    s->heldorder[axis] = order;
}

static void struck(dirsurvivors* s, int cmd, unsigned order) {
    int axis = commandaxis(cmd);
    s->struck[axis] = cmd;
    s->struckorder[axis] = order;
}

static void muted(dirsurvivors* s, int cmd, unsigned order) {
    int axis = commandaxis(cmd);
    s->suppressed[axis] = cmd;
    s->suppressedorder[axis] = order;
}

static void test_commandaxis(void) {
    check("commandaxis(North)", commandaxis(CmdNorth), AXIS_VERT);
    check("commandaxis(South)", commandaxis(CmdSouth), AXIS_VERT);
    check("commandaxis(West)", commandaxis(CmdWest), AXIS_HORZ);
    check("commandaxis(East)", commandaxis(CmdEast), AXIS_HORZ);
    check("commandaxis(none)", commandaxis(CmdNone), AXIS_NONE);
    /* The diagonal is the case defs.h's directionalcmd() would get wrong. */
    check("commandaxis(NE diagonal)", commandaxis(CmdNorth | CmdEast), AXIS_NONE);
}

/* Exhaustive rather than spot-checked, so that inserting a new command into
 * the enum cannot quietly acquire an axis. */
static void test_commandaxis_exhaustive(void) {
    int cmd, want, bad = 0;
    for (cmd = 0; cmd < CmdCount; ++cmd) {
        want = AXIS_NONE;
        if (cmd == CmdNorth || cmd == CmdSouth)
            want = AXIS_VERT;
        else if (cmd == CmdWest || cmd == CmdEast)
            want = AXIS_HORZ;
        if (commandaxis(cmd) != want) {
            printf("      commandaxis(%d) = %d, wanted %d\n",
                   cmd, commandaxis(cmd), want);
            bad = 1;
        }
    }
    check("commandaxis over every command value", bad, 0);
}

static void test_nothing_held(void) {
    dirsurvivors s = nothing();
    check("no keys, Lynx", resolvedirections(&s, LYNX), 0);
    check("no keys, MS", resolvedirections(&s, MSDOS), 0);
}

static void test_single_key(void) {
    dirsurvivors n = nothing(), e = nothing();
    hold(&n, CmdNorth, 1);
    hold(&e, CmdEast, 1);
    check("North alone, Lynx", resolvedirections(&n, LYNX), CmdNorth);
    check("North alone, MS", resolvedirections(&n, MSDOS), CmdNorth);
    check("East alone, Lynx", resolvedirections(&e, LYNX), CmdEast);
    check("East alone, MS", resolvedirections(&e, MSDOS), CmdEast);
}

/* THE MECHANIC. Two perpendicular keys held at once must still produce the
 * diagonal, which lxlogic.c resolves into a block slap. "Two Sets of Rules"
 * and "Piston It Away" are unsolvable without it. If these regress, do not
 * ship.
 *
 * Note the press stamps here are inert by design: with both axes held the
 * code returns the bitwise OR without consulting orders[] at all. Order
 * sensitivity is a MS-only concern and is tested separately below.
 */
static void test_diagonals_survive(void) {
    dirsurvivors ne = nothing(), sw = nothing(), nw = nothing(), se = nothing();
    hold(&ne, CmdNorth, 1); hold(&ne, CmdEast, 2);
    hold(&sw, CmdSouth, 1); hold(&sw, CmdWest, 2);
    hold(&nw, CmdNorth, 2); hold(&nw, CmdWest, 1);
    hold(&se, CmdSouth, 2); hold(&se, CmdEast, 1);
    check("held N+E -> NE", resolvedirections(&ne, LYNX), CmdNorth | CmdEast);
    check("held S+W -> SW", resolvedirections(&sw, LYNX), CmdSouth | CmdWest);
    check("held N+W -> NW", resolvedirections(&nw, LYNX), CmdNorth | CmdWest);
    check("held S+E -> SE", resolvedirections(&se, LYNX), CmdSouth | CmdEast);
}

/* DEFECT 2: a direction tapped and released inside one polling cycle used to
 * be discarded whenever another direction was held, so no diagonal formed and
 * no slap fired. It must now complete the diagonal. */
static void test_struck_completes_diagonal(void) {
    dirsurvivors a = nothing(), b = nothing();
    hold(&a, CmdEast, 1); struck(&a, CmdNorth, 2);
    hold(&b, CmdNorth, 1); struck(&b, CmdEast, 2);
    check("held E, struck N -> NE", resolvedirections(&a, LYNX), CmdNorth | CmdEast);
    check("held N, struck E -> NE", resolvedirections(&b, LYNX), CmdNorth | CmdEast);
}

/* A struck key must never displace a key still being held on its own axis --
 * that would flicker Chip backwards for a move.
 *
 * These are built with the OTHER axis held, which is the only shape where the
 * promotion is live at all. With the other axis empty the promotion is already
 * blocked for a different reason, and a test built that way passes no matter
 * what the own-axis guard does.
 */
static void test_struck_never_displaces_held(void) {
    dirsurvivors newer = nothing(), older = nothing(), vert = nothing();
    hold(&newer, CmdNorth, 1); hold(&newer, CmdWest, 2); struck(&newer, CmdEast, 3);
    hold(&older, CmdNorth, 3); hold(&older, CmdWest, 4); struck(&older, CmdEast, 1);
    hold(&vert, CmdNorth, 1); hold(&vert, CmdWest, 2); struck(&vert, CmdSouth, 3);
    check("held NW, struck E newer -> NW",
          resolvedirections(&newer, LYNX), CmdNorth | CmdWest);
    check("held NW, struck E older -> NW",
          resolvedirections(&older, LYNX), CmdNorth | CmdWest);
    check("held NW, struck S -> NW",
          resolvedirections(&vert, LYNX), CmdNorth | CmdWest);
}

static void test_both_axes_held_and_struck(void) {
    dirsurvivors s = nothing();
    hold(&s, CmdNorth, 1); hold(&s, CmdEast, 2);
    struck(&s, CmdSouth, 3); struck(&s, CmdWest, 4);
    check("held NE, struck S and W -> NE",
          resolvedirections(&s, LYNX), CmdNorth | CmdEast);
}

/* A lone struck key is deliberately NOT promoted: it is left to input()'s
 * ordinary fallback, which is what stops a tapped arrow outranking a virtual
 * menu command arriving in the same cycle. */
static void test_struck_promotion_limits(void) {
    dirsurvivors lone = nothing(), both = nothing(), lonew = nothing();
    struck(&lone, CmdNorth, 1);
    struck(&both, CmdNorth, 1); struck(&both, CmdEast, 2);
    struck(&lonew, CmdWest, 1);
    check("struck N alone -> none", resolvedirections(&lone, LYNX), 0);
    check("struck N and struck E, nothing held -> none",
          resolvedirections(&both, LYNX), 0);
    check("struck W alone -> none", resolvedirections(&lonew, LYNX), 0);
}

/* A struck key pairs with the HELD key on the other axis, never with another
 * struck one. */
static void test_promotion_pairs_with_held(void) {
    dirsurvivors s = nothing(), hs = nothing();
    hold(&s, CmdEast, 1); struck(&s, CmdNorth, 2); struck(&s, CmdWest, 3);
    hold(&hs, CmdEast, 1); struck(&hs, CmdWest, 2);
    check("struck N, held E, struck W -> NE",
          resolvedirections(&s, LYNX), CmdNorth | CmdEast);
    check("held E, struck W, nothing vertical -> E",
          resolvedirections(&hs, LYNX), CmdEast);
}

/* Up and KP8 carry the same command, so one command can be both held and
 * struck in the same cycle. */
static void test_same_command_held_and_struck(void) {
    dirsurvivors s = nothing();
    hold(&s, CmdNorth, 1); hold(&s, CmdEast, 2); struck(&s, CmdNorth, 3);
    check("held N (arrow) + struck N (keypad) + held E -> NE",
          resolvedirections(&s, LYNX), CmdNorth | CmdEast);
}

/* MS rules never produce a diagonal; with one key on each axis the more
 * recently pressed wins. */
static void test_ms_never_diagonal(void) {
    dirsurvivors a = nothing(), b = nothing();
    hold(&a, CmdNorth, 1); hold(&a, CmdEast, 2);
    hold(&b, CmdNorth, 2); hold(&b, CmdEast, 1);
    check("MS: N then E -> E", resolvedirections(&a, MSDOS), CmdEast);
    check("MS: E then N -> N", resolvedirections(&b, MSDOS), CmdNorth);
}

/* The struck clause is gated on joystick mode, so MS is untouched by it. */
static void test_ms_ignores_struck(void) {
    dirsurvivors s = nothing(), bh = nothing(), be = nothing();
    dirsurvivors lone = nothing(), ss = nothing();
    hold(&s, CmdEast, 1); struck(&s, CmdNorth, 2);
    hold(&bh, CmdNorth, 1); hold(&bh, CmdWest, 2); struck(&bh, CmdEast, 3);
    hold(&be, CmdNorth, 1); struck(&be, CmdSouth, 2);
    struck(&lone, CmdNorth, 1);
    struck(&ss, CmdNorth, 1); struck(&ss, CmdEast, 2);
    check("MS: held E, struck N -> E", resolvedirections(&s, MSDOS), CmdEast);
    check("MS: held N older, held W newer, struck E -> W",
          resolvedirections(&bh, MSDOS), CmdWest);
    check("MS: held N, struck S -> N", resolvedirections(&be, MSDOS), CmdNorth);
    check("MS: struck N alone -> none", resolvedirections(&lone, MSDOS), 0);
    check("MS: struck N and struck E -> none", resolvedirections(&ss, MSDOS), 0);
}

/* A key inside the MS mute window still CLAIMS its axis. When such a key wins,
 * the cycle must yield nothing at all rather than falling back to the older
 * key -- input() then returns CmdPreserve and the previous command stays live.
 * Falling back is what made Chip take one move in the direction the player was
 * trying to leave. */
static void test_muted_key_holds_its_axis(void) {
    dirsurvivors cross = nothing(), same = nothing(), lone = nothing();
    hold(&cross, CmdNorth, 1); muted(&cross, CmdEast, 2);
    muted(&same, CmdSouth, 2); hold(&same, CmdNorth, 1);
    muted(&lone, CmdEast, 1);
    check("MS: held N, muted E newer -> none (preserve)",
          resolvedirections(&cross, MSDOS), 0);
    check("MS: muted lone E -> none (preserve)",
          resolvedirections(&lone, MSDOS), 0);
    /* Same axis: the muted key is newer, so the axis yields nothing rather
     * than reverting to the key still held on it. */
    check("MS: held N, muted S newer -> none (preserve)",
          resolvedirections(&same, MSDOS), 0);
}

/* A muted key that is OLDER than a live one must not veto the live one. */
static void test_muted_key_does_not_veto_newer(void) {
    dirsurvivors s = nothing(), sameaxis = nothing();
    muted(&s, CmdEast, 1); hold(&s, CmdNorth, 2);
    muted(&sameaxis, CmdNorth, 1); hold(&sameaxis, CmdSouth, 2);
    check("MS: muted E older, held N newer -> N",
          resolvedirections(&s, MSDOS), CmdNorth);
    check("MS: muted N older, held S newer -> S",
          resolvedirections(&sameaxis, MSDOS), CmdSouth);
}

/* Lynx never produces a muted key -- joystick_trans has no DOWNBUTOFF state --
 * but the resolver stays self-consistent if one ever arrives. */
static void test_muted_under_lynx(void) {
    dirsurvivors s = nothing();
    hold(&s, CmdNorth, 1); muted(&s, CmdEast, 2);
    check("Lynx: a muted axis yields nothing rather than a wrong diagonal",
          resolvedirections(&s, LYNX), 0);
}

/* The accepted trade-off, pinned so a future change cannot alter it silently.
 * With three directions held the vertical axis is decided by recency like any
 * other, so the diagonal follows the newer key. Before jc-43 table order made
 * North win here unconditionally. */
static void test_three_keys_held(void) {
    dirsurvivors s = nothing();
    /* North pressed first, South later, East held throughout. */
    hold(&s, CmdNorth, 1);
    hold(&s, CmdSouth, 3); /* same axis: South is what survived the scan loop */
    hold(&s, CmdEast, 2);
    check("N held, S pressed later, E held -> SE",
          resolvedirections(&s, LYNX), CmdSouth | CmdEast);
}

/* A contract pin, not a reachable state. input() stamps a distinct nonzero
 * value per press, so two live keys cannot tie; asserting which way a tie
 * falls stops a stray >= from passing unnoticed. */
static void test_equal_stamps_contract(void) {
    dirsurvivors s = nothing();
    hold(&s, CmdNorth, 1); hold(&s, CmdEast, 1);
    check("MS: equal stamps resolve to horizontal",
          resolvedirections(&s, MSDOS), CmdEast);
}

int main(void) {
    test_commandaxis();
    test_commandaxis_exhaustive();
    test_nothing_held();
    test_single_key();
    test_diagonals_survive();
    test_struck_completes_diagonal();
    test_struck_never_displaces_held();
    test_both_axes_held_and_struck();
    test_struck_promotion_limits();
    test_promotion_pairs_with_held();
    test_same_command_held_and_struck();
    test_ms_never_diagonal();
    test_ms_ignores_struck();
    test_muted_key_holds_its_axis();
    test_muted_key_does_not_veto_newer();
    test_muted_under_lynx();
    test_three_keys_held();
    test_equal_stamps_contract();

    /* A suite that runs nothing must not report success: the ordinary way a
     * test is lost is a function that stops being called from here. */
    if (checks < 44) {
        printf("only %d checks ran; expected at least 44 -- a test is missing\n",
               checks);
        return 1;
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
