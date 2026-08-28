/* input_test.c: the keyboard scan loop in generic/in.c, driven end to end.
 *
 * MOD (Jeremy, jc-43). test/dirinput_test.c covers resolvedirections(), which
 * is pure. This covers the half that is not: the KS_* classification, the
 * per-axis routing, the joystickstyle/kc->hold/axis gating, and the press-order
 * stamping in _keyeventcallback(). Those are where the fix for defect 1 (same
 * axis conflicts decided by recency rather than table order) actually lives --
 * resolvedirections() never sees two keys on one axis, because the scan loop
 * has already collapsed them.
 *
 * It compiles the REAL generic/in.c, unmodified, against test/stub/oshwbind.h,
 * and drives the real input(). Only six symbols need stubbing.
 *
 * THREE TRAPS, each of which makes this harness LIE rather than fail:
 *
 *  1. Key events must be delivered from inside the eventupdate stub, never
 *     before calling input(). input() runs resetkeystates() BEFORE
 *     eventupdate(), and joystick_trans maps KS_STRUCK -> KS_OFF. Injecting
 *     beforehand retires every struck key before the scan ever sees it, and
 *     the block-slap cases then return a plausible-looking single direction
 *     and prove nothing.
 *  2. The stub key codes must not collide with ASCII; see test/stub/oshwbind.h.
 *  3. Build as C and as C++. The shipped Qt build compiles in.c as C++.
 *
 * Timing model: one call to input() is one 50ms polling cycle. Everything
 * queued for a cycle is delivered inside that cycle, so a key queued down and
 * up in the same cycle is the sub-cycle tap that becomes KS_STRUCK.
 *
 * TESTFLAGS: -DTWPLUSPLUS
 */

#include	<stdio.h>
#include	<string.h>
#include	<stdint.h>

/* The real headers come FIRST, so that every stub below is defined against the
 * declaration the game itself compiles against. Under C++ this is not
 * cosmetic: err.h and oshw.h declare these with extern "C" linkage, and
 * defining them before those declarations are seen is a linkage conflict, not
 * a warning. */
#include	"stub/oshwbind.h"
#include	"../gen.h"
#include	"../defs.h"
#include	"../err.h"
#include	"../oshw.h"

/* --- stubs, the entire external surface of in.c ------------------------ */

int casualinputs = 0; /* the -c command line flag; off unless a test sets it */

char const* err_cfile_ = 0;
unsigned long err_lineno_ = 0;
void warn_(char const* fmt, ...) { (void)fmt; }
int setkeyboardrepeat(int enable) { (void)enable; return 1; }

/* The keyboard as the OS sees it, returned by TW_GetKeyState() and consulted
 * by restartkeystates(). Kept in step with the events we deliver. */
static uint8_t physicalkeys[TWK_LAST];

uint8_t* TW_GetKeyState(int* pNumKeys) {
    *pNumKeys = (int)(sizeof physicalkeys);
    return physicalkeys;
}

/* --- the code under test ---------------------------------------------- */

#include	"../generic/in.c"

/* The globals struct the generic layer hangs its callbacks on; normally
 * defined by the OS/hardware layer. */
genericglobals geng;

/* --- the event pump --------------------------------------------------- */

typedef struct keyevent {
    int scancode;
    int down;
} keyevent;

static keyevent pending[32];
static int pendingcount;

/* Queue an event for the NEXT cycle. */
static void queue(int scancode, int down) {
    pending[pendingcount].scancode = scancode;
    pending[pendingcount].down = down;
    ++pendingcount;
}

/* Stands in for the windowing layer. input() calls this after
 * resetkeystates(), which is exactly where real key events arrive. */
static void testeventupdate(int wait) {
    int i;
    (void)wait;
    for (i = 0; i < pendingcount; ++i) {
        physicalkeys[pending[i].scancode] = (uint8_t)(pending[i].down ? 1 : 0);
        _keyeventcallback(pending[i].scancode, pending[i].down);
    }
    pendingcount = 0;
}

/* Run one polling cycle and return the command input() produced. */
static int cycle(void) {
    return input(FALSE);
}

static void reset(int lynx) {
    memset(physicalkeys, 0, sizeof physicalkeys);
    pendingcount = 0;
    keycmds = gamekeycmds;
    _genericinputinitialize(); /* wires _keyeventcallback into geng */
    geng.eventupdatefunc = testeventupdate;
    setkeyboardarrowsrepeat(lynx); /* also calls restartkeystates() */
}

/* --- assertions -------------------------------------------------------- */

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
        case CmdPreserve: return "preserve";
        case CmdQuitLevel: return "quitlevel";
    }
    return "other";
}

static void check(char const* what, int got, int want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    printf("FAIL: %s\n      got %s (%d), wanted %s (%d)\n",
           what, cmdname(got), got, cmdname(want), want);
}

/* === DEFECT 1: same-axis conflicts follow the player, not the table ===== */

/* Before jc-43 these returned the table-first key (North beats South, West
 * beats East) for as long as the older key was held, in BOTH rulesets. */
static void test_defect1_reversals(void) {
    int ruleset;
    for (ruleset = 0; ruleset < 2; ++ruleset) {
        int lynx = ruleset == 0;
        char const* tag = lynx ? "Lynx" : "MS";
        char buf[64];

        reset(lynx);
        queue(TWK_LEFT, TRUE);
        cycle();
        cycle(); /* let Left settle past the MS suppression window */
        cycle();
        queue(TWK_RIGHT, TRUE);
        sprintf(buf, "%s: hold Left, press Right -> E", tag);
        check(buf, cycle(), CmdEast);

        reset(lynx);
        queue(TWK_UP, TRUE);
        cycle();
        cycle();
        cycle();
        queue(TWK_DOWN, TRUE);
        sprintf(buf, "%s: hold Up, press Down -> S", tag);
        check(buf, cycle(), CmdSouth);

        /* The two that happened to work before, by table luck. */
        reset(lynx);
        queue(TWK_RIGHT, TRUE);
        cycle();
        cycle();
        cycle();
        queue(TWK_LEFT, TRUE);
        sprintf(buf, "%s: hold Right, press Left -> W", tag);
        check(buf, cycle(), CmdWest);

        reset(lynx);
        queue(TWK_DOWN, TRUE);
        cycle();
        cycle();
        cycle();
        queue(TWK_UP, TRUE);
        sprintf(buf, "%s: hold Down, press Up -> N", tag);
        check(buf, cycle(), CmdNorth);
    }
}

/* The keypad is a second set of scancodes carrying the same commands, with
 * their own stamps. */
static void test_defect1_keypad(void) {
    reset(TRUE);
    queue(TWK_KP4, TRUE);
    cycle();
    cycle();
    queue(TWK_KP6, TRUE);
    check("Lynx: hold KP4, press KP6 -> E", cycle(), CmdEast);
}

/* === DEFECT 2: a sub-cycle tap completes the diagonal =================== */

/* THE BLOCK SLAP. Before jc-43 the tap was discarded outright and this
 * returned a bare East, so no diagonal reached lxlogic.c and no block moved. */
static void test_defect2_struck_completes_diagonal(void) {
    reset(TRUE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    queue(TWK_UP, TRUE);
    queue(TWK_UP, FALSE); /* down AND up inside one cycle -> KS_STRUCK */
    check("Lynx: running E, sub-cycle tap Up -> NE",
          cycle(), CmdNorth | CmdEast);

    reset(TRUE);
    queue(TWK_KP6, TRUE);
    cycle();
    cycle();
    queue(TWK_KP8, TRUE);
    queue(TWK_KP8, FALSE);
    check("Lynx: running E on keypad, sub-cycle tap KP8 -> NE",
          cycle(), CmdNorth | CmdEast);
}

/* A tap long enough to span a cycle boundary needs no promotion: it is
 * KS_PRESSED in the first cycle and the ordinary held path forms the diagonal.
 * This worked before jc-43 and must still work. */
static void test_spanning_tap_still_works(void) {
    reset(TRUE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    queue(TWK_UP, TRUE);
    check("Lynx: running E, Up pressed (spanning tap) -> NE",
          cycle(), CmdNorth | CmdEast);
}

/* MS has no diagonals, and the struck promotion is gated out of it. */
static void test_ms_never_diagonal(void) {
    reset(FALSE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_UP, TRUE);
    queue(TWK_UP, FALSE);
    check("MS: running E, sub-cycle tap Up -> E (no diagonal)",
          cycle(), CmdEast);
}

/* === THE MS SUPPRESSION WINDOW (the jc-43 regression this fix closes) === */

/* Under keyboard behavior a freshly pressed key spends a cycle in
 * KS_DOWNBUTOFF1, muted so that one tap yields one move. The muted key must
 * still hold its axis: if it dropped out of arbitration the axis would revert
 * to the older key the player is leaving, and Chip would take one move in the
 * wrong direction. CmdPreserve keeps the correct command alive instead. */
static void test_ms_suppression_does_not_revert(void) {
    reset(FALSE);
    queue(TWK_UP, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_RIGHT, TRUE);
    check("MS: hold Up, press Right -> E", cycle(), CmdEast);
    check("MS: next cycle must NOT revert to N", cycle(), CmdPreserve);
    check("MS: and then settles on E", cycle(), CmdEast);
    check("MS: and stays E", cycle(), CmdEast);

    /* Same shape on one axis. */
    reset(FALSE);
    queue(TWK_UP, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_DOWN, TRUE);
    check("MS: hold Up, press Down -> S", cycle(), CmdSouth);
    check("MS: next cycle must NOT revert to N", cycle(), CmdPreserve);
    check("MS: and then settles on S", cycle(), CmdSouth);
}

/* With -casualinputs the mute lasts two cycles instead of one. */
static void test_ms_suppression_casualinputs(void) {
    casualinputs = 1;
    reset(FALSE);
    queue(TWK_UP, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_RIGHT, TRUE);
    check("MS -c: hold Up, press Right -> E", cycle(), CmdEast);
    check("MS -c: muted cycle 1 must not revert", cycle(), CmdPreserve);
    check("MS -c: muted cycle 2 must not revert", cycle(), CmdPreserve);
    check("MS -c: then settles on E", cycle(), CmdEast);
    casualinputs = 0;
}

/* A lone key in its suppression window behaves exactly as it always has:
 * one command, then CmdPreserve while muted. */
static void test_ms_single_tap_unchanged(void) {
    reset(FALSE);
    queue(TWK_RIGHT, TRUE);
    check("MS: press Right -> E", cycle(), CmdEast);
    check("MS: muted cycle -> preserve", cycle(), CmdPreserve);
    check("MS: held -> E", cycle(), CmdEast);
}

/* Lynx has no suppression window at all (joystick_trans has no DOWNBUTOFF
 * state), so a held direction reports every cycle without interruption. */
static void test_lynx_has_no_suppression_window(void) {
    reset(TRUE);
    queue(TWK_RIGHT, TRUE);
    check("Lynx: press Right -> E", cycle(), CmdEast);
    check("Lynx: still E", cycle(), CmdEast);
    check("Lynx: still E", cycle(), CmdEast);
}

/* === STAMPING ========================================================== */

/* A key tapped and pressed again inside one cycle goes KS_STRUCK -> down.
 * That path does not pass through KS_OFF, so stamping only on KS_OFF would
 * leave it carrying its previous press's stamp and losing a comparison it
 * should win. */
static void test_restamp_after_struck(void) {
    reset(TRUE);
    queue(TWK_UP, TRUE);
    cycle();
    cycle();
    /* Up has been held a while. Now tap Right and immediately re-press it, all
     * inside one cycle, then let it settle: Right is the newer key and must
     * take the horizontal axis. */
    queue(TWK_RIGHT, TRUE);
    queue(TWK_RIGHT, FALSE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    check("Lynx: Up held, Right re-pressed -> NE", cycle(), CmdNorth | CmdEast);
}

/* A second press on a key already down must NOT restamp: recency should date
 * from the original press, not from a repeat. */
static void test_repeat_does_not_restamp(void) {
    reset(FALSE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_UP, TRUE);
    cycle();
    cycle();
    cycle();
    check("MS: Up newer than Right -> N", cycle(), CmdNorth);
    /* A repeat press of Right must not make it newer than Up. */
    queue(TWK_RIGHT, TRUE);
    check("MS: repeat press of Right does not restamp -> N",
          cycle(), CmdNorth);
}

/* === GATING ============================================================ */

/* setkeyboardinputmode() swaps in inputkeycmds, whose arrows have hold=FALSE.
 * The struck promotion must not reach them. (The HELD path is deliberately
 * unchanged and still merges, before and after jc-43 alike -- pinned below so
 * that stays a decision rather than a discovery.) */
static void test_input_mode_gating(void) {
    reset(TRUE);
    keycmds = inputkeycmds;
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    queue(TWK_UP, TRUE);
    queue(TWK_UP, FALSE);
    /* North, not a diagonal: the tapped arrow was refused promotion and fell
     * through to the ordinary struck fallback. Right contributes nothing at
     * all here -- inputkeycmds arrows are hold=FALSE, so a held one stops
     * reporting after the cycle it was pressed in. */
    check("prompt: sub-cycle tap does not promote -> N", cycle(), CmdNorth);

    reset(TRUE);
    keycmds = inputkeycmds;
    queue(TWK_UP, TRUE);
    queue(TWK_LEFT, TRUE);
    check("prompt: two arrows pressed together still merge -> NW",
          cycle(), CmdNorth | CmdWest);
    keycmds = gamekeycmds;
}

/* In inputkeycmds, Backspace is CmdWest and Space is CmdEast. Pressing both in
 * one cycle used to yield West by table order and now yields East by recency.
 * Pathological input, accepted deliberately; pinned so it cannot drift back. */
static void test_input_mode_backspace_space(void) {
    reset(TRUE);
    keycmds = inputkeycmds;
    queue(TWK_BACKSPACE, TRUE);
    queue(' ', TRUE);
    check("prompt: Backspace then Space -> E (recency)", cycle(), CmdEast);
    keycmds = gamekeycmds;
}

/* A held direction outranks a virtual menu command in the same cycle -- true
 * before jc-43 and after. The deliberate refusal to promote a LONE struck key
 * is what keeps a tapped arrow from newly stealing one. */
static void test_menu_command_not_stolen(void) {
    reset(TRUE);
    queue(TWC_QUITLEVEL, TRUE);
    queue(TWC_QUITLEVEL, FALSE);
    queue(TWK_UP, TRUE);
    queue(TWK_UP, FALSE);
    check("Lynx: lone tapped arrow does not steal a menu command",
          cycle(), CmdQuitLevel);
}

/* === RESTART =========================================================== */

/* A ruleset change replays held keys in scancode order, which can invert true
 * recency. Documented in in.c and pinned here: the stub codes rank
 * West < North < East < South exactly as Qt's do, so Right wins after a
 * restart even though Left was pressed later. */
static void test_restartkeystates_uses_scancode_order(void) {
    reset(FALSE);
    queue(TWK_RIGHT, TRUE);
    cycle();
    cycle();
    cycle();
    queue(TWK_LEFT, TRUE);
    cycle();
    cycle();
    cycle();
    check("MS: Left pressed later -> W", cycle(), CmdWest);
    setkeyboardarrowsrepeat(FALSE); /* forces restartkeystates() */
    /* The replay leaves both keys freshly KS_PRESSED, so they spend one cycle
     * muted before reporting again. */
    check("MS: the cycle after a restart is muted", cycle(), CmdPreserve);
    check("MS: after a restart, scancode order puts Right last -> E",
          cycle(), CmdEast);
}

int main(void) {
    test_defect1_reversals();
    test_defect1_keypad();
    test_defect2_struck_completes_diagonal();
    test_spanning_tap_still_works();
    test_ms_never_diagonal();
    test_ms_suppression_does_not_revert();
    test_ms_suppression_casualinputs();
    test_ms_single_tap_unchanged();
    test_lynx_has_no_suppression_window();
    test_restamp_after_struck();
    test_repeat_does_not_restamp();
    test_input_mode_gating();
    test_input_mode_backspace_space();
    test_menu_command_not_stolen();
    test_restartkeystates_uses_scancode_order();

    /* A suite that runs nothing must not report success: the ordinary way a
     * test is lost is a function that stops being called from here. */
    if (checks < 40) {
        printf("only %d checks ran; expected at least 40 -- a test is missing\n",
               checks);
        return 1;
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
