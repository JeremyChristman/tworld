/* tw_engine_digest.h: driving an engine over a level record and hashing it.
 *
 * MOD (Jeremy). Extracted from golden.c when test/nofix/nofix.c needed exactly
 * the same three hard-won pieces:
 *
 *   1. a state digest that is reproducible across compilers and platforms,
 *   2. a die() that neither returns nor exits, and
 *   3. the discipline of a fresh engine per run.
 *
 * Getting any of the three wrong produces a harness that reports confidently
 * and means nothing, so there is one copy of each rather than two that drift.
 *
 * 🔴 THE DIGEST IS A COMMITTED FORMAT. test/golden/engine-snapshot.tsv holds
 * digests produced by hash_state() below. Changing what it hashes, or the order
 * it hashes in, invalidates that file -- every row moves at once. That is
 * recoverable (regenerate the baseline) but it must be DELIBERATE, and the
 * giveaway is in the data: if the digests move while the outcome and tick
 * columns do not, the formula changed rather than the engine.
 *
 * This header defines objects, not just declarations -- it is included by
 * exactly one translation unit per program, which is how the tests here are
 * built. It is not a general-purpose library header.
 */

#ifndef	HEADER_tw_engine_digest_h_
#define	HEADER_tw_engine_digest_h_

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<setjmp.h>

#include	"../../defs.h"
#include	"../../state.h"
#include	"../../logic.h"
#include	"../../encoding.h"
#include	"../../random.h"

typedef unsigned long long u64;

/* ---------------------------------------------------------------- hashing -- */

/* FNV-1a, 64-bit. Eight lines and no dependencies. Not chosen for strength --
 * nothing here is adversarial, the digest only has to change when the state
 * does. */
#define	FNV_OFFSET	14695981039346656037ULL
#define	FNV_PRIME	1099511628211ULL

static void hash_byte(u64 *h, unsigned int b)
{
    *h ^= (u64)(b & 0xFF);
    *h *= FNV_PRIME;
}

static void hash_int(u64 *h, long v)
{
    /* Fixed width and byte order, so a digest computed on one machine matches
     * one computed on another. Going through unsigned long makes the shift of
     * a negative value defined rather than implementation-defined. */
    unsigned long u = (unsigned long)v;
    int i;
    for (i = 0 ; i < 4 ; ++i)
	hash_byte(h, (unsigned int)((u >> (i * 8)) & 0xFF));
}

/* ------------------------------------------- the error surface, stubbed --- */

/* A warning is EXPECTED -- plenty of real levels have bad tiles, and a
 * generated one has them constantly -- so warn_ counts rather than prints.
 *
 * 🔴 die_ MUST NOT RETURN, AND MUST NOT EXIT EITHER. Both halves matter:
 *
 *   not exit    a level that trips an engine assert has to be RECORDED as
 *               such, not take the whole run down with it. That is not
 *               hypothetical -- it is exactly what jc-51 was, and a run that
 *               aborted on the first one would hide every level after it.
 *
 *   not return  in the shipped program die() calls exit(), so the engines are
 *               written on the assumption that control never comes back. A
 *               stub that sets a flag and returns lets the engine carry on
 *               through a state it has just declared impossible -- past
 *               `_assert(!"lookupblock() called on blockless location")`, for
 *               instance, straight into the deref that assert exists to
 *               prevent. That is a crash in the harness masquerading as a
 *               finding in the engine.
 *
 * longjmp is the honest emulation: control leaves the engine immediately and
 * does not come back, exactly as exit() would.
 *
 * ⚠ The cost, stated plainly: the jump abandons whatever the engine had
 * allocated for that run. That is why every run gets a FRESH engine and a
 * shutdown() -- the leak is bounded by one level, and no state crosses into
 * the next. Do not "optimize" the per-level startup away. */
char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;
static int	died = 0;
static jmp_buf	diejmp;
static int	dieready = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *pfx, char const *fmt, ...) { (void)pfx; (void)fmt; }

void die_(char const *fmt, ...)
{
    (void)fmt;
    died = 1;
    if (dieready) {
	dieready = 0;
	longjmp(diejmp, 1);
    }
    /* Outside a run -- nothing has set a landing pad, so there is nothing
     * sensible to do but stop. Reaching here is a bug in the caller. */
    fprintf(stderr, "die() outside a level run\n");
    exit(2);
}

/* ------------------------------------------------------- state digesting -- */

/* Hash what a player could observe, plus the bookkeeping that decides what
 * happens next.
 *
 * ⚠ FIELD BY FIELD, NEVER memcpy OF THE STRUCT. gamestate contains padding,
 * and padding bytes are indeterminate: hashing them would make the digest
 * depend on the compiler's layout choices and on whatever happened to be in
 * memory, which is the kind of "test" that fails on somebody else's machine
 * for no reason and then gets deleted. The Linux CI job checking a
 * Windows-generated baseline is what keeps this honest. */
static void hash_state(u64 *h, gamestate const *st, int result)
{
    creature const *cr;
    int i;

    hash_int(h, st->currenttime);
    hash_int(h, st->chipsneeded);
    hash_int(h, st->statusflags);
    hash_int(h, st->lastmove);
    hash_int(h, st->xviewpos);
    hash_int(h, st->yviewpos);
    hash_int(h, st->timeoffset);
    hash_int(h, result);
    for (i = 0 ; i < 4 ; ++i) {
	hash_int(h, st->keys[i]);
	hash_int(h, st->boots[i]);
    }

    /* The map, including the fork's virtual row 32 -- the MSCC row-32 cloner
     * glitch writes there, and a change to that path must show up here. */
    for (i = 0 ; i < CXGRID * (CYGRID + 1) ; ++i) {
	hash_byte(h, st->map[i].top.id);
	hash_byte(h, st->map[i].top.state);
	hash_byte(h, st->map[i].bot.id);
	hash_byte(h, st->map[i].bot.state);
    }

    /* The creature list, walked the way generic/tile.c:497 walks it: id == 0
     * terminates. That is the renderer's convention and the only one reachable
     * from outside; the engines' private working lists are not. */
    if (st->creatures) {
	for (cr = st->creatures ; cr->id ; ++cr) {
	    hash_byte(h, cr->id);
	    hash_byte(h, cr->dir);
	    hash_byte(h, cr->hidden);
	    hash_byte(h, cr->state);
	    hash_byte(h, cr->tdir);
	    hash_int(h, cr->pos);
	    hash_int(h, cr->moving);
	    hash_int(h, cr->frame);
	}
    }
    hash_byte(h, 0xFF);		/* terminator, so list LENGTH is part of it */
}

/* ------------------------------------------------------------- the drive -- */

/* Supplies the command for a tick. Called once every four ticks (one MS move);
 * `step` counts those, not ticks. */
typedef int (*tw_movesource)(int step, void *ctx);

/* Run one level record through one engine and return the digest of every tick.
 * *outcome receives a short word for how it ended, worth carrying beside the
 * digest because "the digest changed" reads far better when the outcome says
 * the level went from being won to being lost. */
static u64 tw_run_level(gamelogic *logic, gamestate *st, gamesetup *setup,
			unsigned char *data, int size, int number, int ruleset,
			unsigned long engineseed, int maxticks,
			tw_movesource nextmove, void *ctx,
			char const **outcome, int *ticksrun)
{
    /* 🔴 STATIC, NOT AUTOMATIC, AND THAT IS NOT AN ACCIDENT. After a longjmp,
     * an automatic variable in the function that called setjmp has an
     * INDETERMINATE value unless it is volatile -- so a plain `u64 h` would
     * hold garbage on exactly the runs that trip an assert, which are the ones
     * worth recording. Static storage is untouched by longjmp, and unlike
     * `volatile u64` it leaves `&h` a plain `u64 *` for hash_state().
     * Single-threaded, one run at a time; there is nothing to race. */
    static u64	h;
    static int	result;
    static int	ticks;
    int		t;

    h = FNV_OFFSET;
    result = 0;
    ticks = 0;
    died = 0;
    *ticksrun = 0;

    memset(setup, 0, sizeof *setup);
    setup->number = number;
    setup->time = 0;
    setup->leveldata = data;
    setup->levelsize = size;

    memset(st->map, 0, sizeof st->map);
    st->game = setup;
    st->ruleset = ruleset;
    st->replay = -1;
    st->currenttime = -1;
    st->timeoffset = 0;
    st->currentinput = NIL;
    st->lastmove = NIL;
    st->initrndslidedir = NIL;
    st->stepping = -1;
    st->statusflags = 0;
    st->soundeffects = 0;
    st->timelimit = 0;
    /* solution.c is not linked; nothing here records moves, because replay
     * stays -1 and the recording happens in doturn(), not in the engine. */
    st->moves.list = NULL;
    st->moves.count = 0;
    st->moves.allocated = 0;
    /* Feeds the ENGINE's own random draws (blobs, random slides). Fixed per
     * run, and NOT the same generator that supplies the moves. */
    restartprng(&st->mainprng, engineseed);

    /* The landing pad. Everything from here to the end of the tick loop runs
     * with die() wired to come back to this line instead of returning into an
     * engine that has just declared its own state impossible. */
    if (setjmp(diejmp)) {
	dieready = 0;
	*ticksrun = ticks;
	*outcome = "DIED";
	/* No endgame() call: the engine was abandoned mid-tick and asking it
	 * to tidy up would run more of the code that just failed. The caller's
	 * shutdown() is what releases it. */
	return h;
    }
    dieready = 1;

    if (!expandleveldata(st)) {
	dieready = 0;
	*outcome = "badlevel";
	return 0;
    }
    if (!(*logic->initgame)(logic)) {
	dieready = 0;
	*outcome = "initfailed";
	return 0;
    }

    hash_state(&h, st, 0);
    for (t = 0 ; t < maxticks ; ++t) {
	/* One command held for four ticks, which is one MS move. Re-drawing
	 * every tick would mostly cancel itself out and go nowhere. */
	if ((t & 3) == 0)
	    st->currentinput = (short)(*nextmove)(t >> 2, ctx);
	st->currenttime = t;
	result = (*logic->advancegame)(logic);
	++ticks;
	hash_state(&h, st, result);
	if (result)
	    break;
    }
    dieready = 0;
    *ticksrun = ticks;

    if (died)
	*outcome = "DIED";
    else if (result > 0)
	*outcome = "won";
    else if (result < 0)
	*outcome = "lost";
    else
	*outcome = "running";

    (*logic->endgame)(logic);
    return h;
}

#endif
