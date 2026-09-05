/* fuzz_mslogic.c: libFuzzer target for the MS ENGINE, not just the parser.
 *
 * MOD (Jeremy).
 *
 *     test/run-fuzz.sh mslogic
 *
 * WHY THE ENGINES NEEDED THEIR OWN TARGETS. The four parser targets stop where
 * readleveldata() and expandleveldata() stop: they prove a malformed file is
 * REFUSED. They say nothing about a file that is accepted and then breaks the
 * engine -- and that is not a hypothetical class, it is jc-45. A beartrap
 * wiring with an out-of-range `to` sailed through every check in the parser and
 * was dereferenced by initgame()'s spring-the-traps loop. Seven real level sets
 * in circulation carry one.
 *
 * jc-45 was found by reading the code and then hand-building a level that could
 * observe it. This is the instrument that would have found it without anyone
 * suspecting the line.
 *
 * 🔴 THE INPUT IS SPLIT, so the fuzzer can explore the LEVEL and the PLAY at
 * once. A level alone only exercises expandleveldata() and initgame(); the
 * interesting failures need Chip to walk into things.
 *
 *     data[0]              M, the number of move bytes (masked to 0..63)
 *     data[1 .. M]         the move stream, one command per four ticks
 *     data[1+M .. size]    the level record handed to expandleveldata()
 *
 * A reproducer therefore encodes both halves, which is what makes it replayable
 * from the corpus later (docs/adr/0011).
 *
 * ⚠ EVERY EXECUTION MUST BE INDEPENDENT. The engine keeps file-scope state --
 * `creatures`, `blocks`, `slips` and their counts -- so shutdown() is called on
 * every path out, and the PRNG is reseeded to a FIXED value each run. Without
 * both, a crash would depend on the inputs that came before it and the
 * reproducer would not reproduce.
 *
 * ⚠ AN _assert FAILURE IS A FINDING, NOT NOISE. mslogic.c's _assert calls
 * die(), which in the shipped program exits. If a file somebody downloaded can
 * violate an engine invariant, the game dies on it -- so the stub below aborts
 * and lets libFuzzer save the input. Triage it as a real report.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../tw_test.h"
#include "../tw_fixture.h"

/* Defined here because lxlogic.c -- which owns the definition (line 54) -- is
 * not in this translation unit. See test/mslogic_test.c. */
int	pedanticmode = 0;

#include "../../random.c"
#include "../../encoding.c"
#include "../../mslogic.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; abort(); }

/* --- the target --------------------------------------------------------- */

static gamestate	fuzzstate;
static gamesetup	fuzzsetup;

static int const	cmds[5] = { NIL, CmdNorth, CmdWest, CmdSouth, CmdEast };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    gamelogic	       *logic;
    unsigned char      *level;
    size_t		movecount, levelsize;
    size_t		i;
    int			tick;

    if (size < 3 || size > 65536)
	return 0;

    movecount = data[0] & 0x3F;
    if (1 + movecount >= size)
	return 0;
    levelsize = size - 1 - movecount;

    /* Exactly-sized, so ASan's redzone begins at the first byte past the record
     * -- the same reasoning as the parser targets. */
    level = (unsigned char *)malloc(levelsize);
    if (!level)
	return 0;
    memcpy(level, data + 1 + movecount, levelsize);

    memset(&fuzzsetup, 0, sizeof fuzzsetup);
    fuzzsetup.number = 1;
    fuzzsetup.leveldata = level;
    fuzzsetup.levelsize = (int)levelsize;

    logic = mslogicstartup();
    if (!logic) {
	free(level);
	return 0;
    }
    logic->state = &fuzzstate;

    memset(fuzzstate.map, 0, sizeof fuzzstate.map);
    fuzzstate.game = &fuzzsetup;
    fuzzstate.ruleset = Ruleset_MS;
    fuzzstate.replay = -1;
    fuzzstate.currenttime = -1;
    fuzzstate.timeoffset = 0;
    fuzzstate.currentinput = NIL;
    fuzzstate.lastmove = NIL;
    fuzzstate.initrndslidedir = NIL;
    fuzzstate.stepping = -1;
    fuzzstate.statusflags = 0;
    fuzzstate.soundeffects = 0;
    fuzzstate.timelimit = 0;
    fuzzstate.moves.list = NULL;
    fuzzstate.moves.count = 0;
    fuzzstate.moves.allocated = 0;
    restartprng(&fuzzstate.mainprng, 12345);

    if (expandleveldata(&fuzzstate) && (*logic->initgame)(logic)) {
	tick = -1;
	for (i = 0 ; i < movecount ; ++i) {
	    int cmd = cmds[data[1 + i] % 5];
	    int t;
	    /* Four ticks per command, which is one Chip move. */
	    for (t = 0 ; t < 4 ; ++t) {
		fuzzstate.currenttime = ++tick;
		fuzzstate.currentinput = (short)cmd;
		if ((*logic->advancegame)(logic))
		    goto done;
	    }
	}
    }

  done:
    (*logic->shutdown)(logic);
    free(level);
    return 0;
}
