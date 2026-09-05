/* nofix.c: the NO_FIX_* differential matrix.
 *
 * MOD (Jeremy). THE PROBLEM. mslogic.c carries 32 NO_FIX_* toggles. Each one
 * isolates a single engine fix from the jc-2..jc-29 desync work, so that a
 * future investigation can put the old behavior back and measure against it
 * (docs/adr/0002). They are the machinery this whole fork was built with.
 *
 * And until now NOTHING TESTED ANY OF THEM. CLAUDE.md said so plainly and had
 * said so for a long time: "each is a documented behavior difference with a
 * known direction, and compiling one test both ways would be a real oracle.
 * Nobody has done it." The golden master (test/golden/golden.c) then measured
 * exactly how bad it was -- over all 903 committed levels it can distinguish
 * only TWO of the 32 -- and measured that neither a longer walk (400 -> 2000
 * ticks) nor more walks (1 -> 12) finds a third.
 *
 * WHY RANDOM WALKING OVER REAL LEVELS FAILS. A real level puts Chip a long way
 * from the interesting furniture, and these fixes are about specific
 * arrangements: a block resting on a teleport, a tank standing on a cloner, a
 * creature in a beartrap whose button is pressed this very tick. A walker
 * bumping around CCLP2 #43 will not build one of those in 400 ticks, or 2000.
 *
 * WHAT THIS DOES INSTEAD. It generates TINY levels -- a 9x9 room packed with
 * exactly that furniture -- and plays a short random game in each. In a room
 * that small, with a block, a teleport, a trap and a cloner all within a few
 * squares of Chip, the arrangements happen by accident constantly. Then it runs
 * the SAME generated input under a build with the fix on and a build with the
 * fix off, and asks one question:
 *
 *              DOES THIS INPUT TELL THE TWO BUILDS APART?
 *
 * An input that does is a WITNESS: proof that the fix is live, reachable, and
 * doing something. The witness seed is committed to nofix-matrix.tsv with both
 * digests, and re-checking it later is a regression test with real teeth --
 * it fails if the fix stops being reachable, if the toggle stops working, or if
 * either behavior changes.
 *
 * 🔴 WHAT A WITNESS DOES AND DOES NOT PROVE. It proves the two builds DIFFER on
 * that input. It does NOT prove the fix is correct, and it does not say the
 * difference is the one the fix was written for -- a witness for a teleport fix
 * may well be distinguishing it through some downstream ripple. This is a
 * reachability and liveness oracle, not a specification. The correctness
 * argument for every one of these fixes is in FORK.md, measured against
 * SuperCC over a real solution corpus, and nothing here replaces that.
 *
 * ⚠ A TOGGLE WITH NO WITNESS IS NOT A TOGGLE THAT DOES NOTHING. It means this
 * generator did not happen to build the arrangement it needs. Absence of
 * evidence, and the matrix file says so on every such row rather than implying
 * the fix is dead code. Widening the search is a legitimate way to attack a
 * blank row; deleting the fix on the strength of one is not.
 *
 * DETERMINISM. Everything derives from the seed: the room, its furniture, the
 * wiring, and the moves. The engine's own PRNG is restarted per run with a
 * fixed value. As in golden.c the MOVE stream comes from a private generator,
 * never from random.c, so a change to random.c cannot silently alter the input
 * as well as the output.
 */

#include	"../golden/tw_engine_digest.h"
#include	"../tw_fixture.h"

/* The room. Small on purpose: the whole point is that Chip is never more than a
 * few squares from every kind of furniture. 9x9 leaves room for a wall border
 * inside the 32x32 map and still gives 81 cells of things to collide with. */
#define	ROOM_X		1
#define	ROOM_Y		1
#define	ROOM_W		9
#define	ROOM_H		9

/* 40 moves at 4 ticks each. Long enough for a block to be pushed somewhere
 * awkward and for a cloner to fire several times; short enough that a search
 * over a hundred thousand seeds finishes while you wait. */
#define	NOFIX_TICKS	320
#define	NOFIX_STEPS	(NOFIX_TICKS / 4)

/* ------------------------------------------------------------- generation -- */

static u64 genstate;

static void gen_seed(u64 seed)
{
    genstate = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static u64 gen_next(void)
{
    genstate ^= genstate >> 12;
    genstate ^= genstate << 25;
    genstate ^= genstate >> 27;
    return genstate * 2685821657736338717ULL;
}

static int gen_below(int n) { return (int)(gen_next() % (u64)n); }

/* The tile alphabet, as RAW DATA-FILE CODES.
 *
 * 🔴 Taken from encoding.c's fileids[] table, which is the authority in this
 * tree for what a .dat byte means -- not from memory of the CC1 format, and not
 * from the (much smaller) FIX_* list in tw_fixture.h, which only covers what the
 * older unit tests happened to need.
 *
 * The selection is deliberate rather than "0x00..0x70": it is the furniture the
 * 32 toggles are ABOUT. Teleports, beartraps and their brown buttons, clone
 * machines and their red buttons, the blue (tank) button, tanks, blocks and
 * cloning blocks, ice and force floors including random force floor, switch
 * walls and the green button, popup and hidden walls, plus enough monsters to
 * populate the creature list and enough hazards to end games. Death tiles and
 * the "not used" codes are left out; they are not arrangements anyone fixed. */
static unsigned char const alphabet[] = {
    0x00, 0x00, 0x00, 0x00,		/* floor, weighted heavily -- room to move */
    0x01,				/* wall */
    0x02,				/* chip */
    0x03, 0x04,				/* water, fire */
    0x0A, 0x0A,				/* block, weighted: many fixes involve one */
    0x0B, 0x0C, 0x0D,			/* dirt, ice, force south */
    0x0E, 0x0F, 0x10, 0x11,		/* cloning blocks N/W/S/E */
    0x12, 0x13, 0x14,			/* force north/east/west */
    0x15,				/* exit */
    0x16, 0x17, 0x18, 0x19,		/* doors */
    0x1A, 0x1B, 0x1C, 0x1D,		/* ice corners */
    0x1E, 0x1F,				/* blue wall fake/real */
    0x21, 0x22,				/* thief, socket */
    0x23, 0x24,				/* green button, red button */
    0x25, 0x26,				/* switch wall closed/open */
    0x27, 0x27,				/* brown button, weighted (traps) */
    0x28,				/* blue button (tanks) */
    0x29, 0x29,				/* teleport, weighted */
    0x2A,				/* bomb */
    0x2B, 0x2B,				/* beartrap, weighted */
    0x2C, 0x2D, 0x2E,			/* hidden wall temp, gravel, popup wall */
    0x30,				/* blocked SE */
    0x31, 0x31,				/* clone machine, weighted */
    0x32,				/* force random */
    0x40, 0x42,				/* bugs */
    0x44, 0x46,				/* fireballs */
    0x48, 0x4A,				/* pink balls */
    0x4C, 0x4D, 0x4E, 0x4F,		/* tanks, all four facings */
    0x50, 0x52,				/* gliders */
    0x54, 0x56,				/* teeth */
    0x58, 0x5A,				/* walkers */
    0x5C,				/* blob */
    0x60, 0x62,				/* parameciums */
    0x64, 0x65, 0x66, 0x67,		/* keys */
    0x68, 0x69, 0x6A, 0x6B		/* boots */
};

#define	ALPHABET_N	((int)(sizeof alphabet / sizeof *alphabet))

/* Where the designed stacks put a beartrap or a clone machine, so the wiring
 * below can point at one that actually exists. */
static int trapx[8], trapy[8], trapcount;
static int clonerx[8], clonery[8], clonercount;

/* Build one level from the current generator state. */
static void gen_level(fixlevel *lv)
{
    int x, y, n, i;

    fix_init(lv);

    /* Everything outside the room is wall. A creature that escaped the room
     * would wander a 32x32 emptiness and the run would prove nothing. */
    for (y = 0 ; y < FIX_HEIGHT ; ++y)
	for (x = 0 ; x < FIX_WIDTH ; ++x)
	    fix_settop(lv, x, y, FIX_WALL);

    for (y = 0 ; y < ROOM_H ; ++y)
	for (x = 0 ; x < ROOM_W ; ++x)
	    fix_settop(lv, ROOM_X + x, ROOM_Y + y,
		       alphabet[gen_below(ALPHABET_N)]);

    /* 🔴 DESIGNED STACKS -- THE PART THAT MAKES THIS SEARCH WORK AT ALL.
     *
     * A first version set the lower layer to a random tile on a handful of
     * random cells. Measured over 200,000 seeds, that found a witness for
     * exactly ONE of the 32 toggles. The reason is arithmetic: nearly every
     * fix here is about a SPECIFIC PAIR -- a tank standing on a clone machine,
     * a block resting on a teleport, a creature sitting in a beartrap -- and
     * the chance that an independently random top and an independently random
     * bottom land on the pair you need is negligible. Random fill produces
     * furniture; it does not produce ARRANGEMENTS.
     *
     * So the pairs are built on purpose: something that MOVES on top, something
     * that ACTS ON IT underneath. The choice of each half is still random, so
     * the search covers the whole cross product rather than a fixture author's
     * imagination -- which is the point of doing it this way instead of hand
     * writing 32 levels. */
    {
	static unsigned char const movers[] = {
	    0x0A, 0x0A,				/* block, weighted */
	    0x4C, 0x4D, 0x4E, 0x4F,		/* tanks (blue button, cloners) */
	    0x50, 0x52,				/* gliders (teleports, fire) */
	    0x44, 0x46,				/* fireballs */
	    0x48, 0x4A,				/* pink balls */
	    0x40, 0x42,				/* bugs */
	    0x54, 0x56,				/* teeth */
	    0x58, 0x5A,				/* walkers */
	    0x5C,				/* blob */
	    0x60, 0x62				/* parameciums */
	};
	static unsigned char const machinery[] = {
	    0x31, 0x31,				/* clone machine, weighted */
	    0x2B, 0x2B,				/* beartrap, weighted */
	    0x29, 0x29,				/* teleport, weighted */
	    0x0D, 0x12, 0x13, 0x14, 0x32,	/* force floors, incl. random */
	    0x0C,				/* ice */
	    0x03, 0x04,				/* water, fire */
	    0x24, 0x27, 0x28, 0x23,		/* red, brown, blue, green buttons */
	    0x25, 0x26,				/* switch walls */
	    0x2E,				/* popup wall */
	    0x00				/* plain floor, as a control */
	};
	int	nstack = 3 + gen_below(6);
	int	ntrap = 0, ncloner = 0;

	for (i = 0 ; i < nstack ; ++i) {
	    unsigned char under = machinery[gen_below(
				    (int)(sizeof machinery / sizeof *machinery))];
	    x = ROOM_X + gen_below(ROOM_W);
	    y = ROOM_Y + gen_below(ROOM_H);
	    fix_setbot(lv, x, y, under);
	    fix_settop(lv, x, y, movers[gen_below(
				    (int)(sizeof movers / sizeof *movers))]);
	    /* Remember the machinery that needs wiring to be worth anything. A
	     * beartrap nobody can open and a cloner nobody can fire are just
	     * scenery, and the trap/cloner toggles never engage. */
	    if (under == 0x2B && ntrap < 8) {
		trapx[ntrap] = x; trapy[ntrap] = y; ++ntrap;
	    } else if (under == 0x31 && ncloner < 8) {
		clonerx[ncloner] = x; clonery[ncloner] = y; ++ncloner;
	    }
	}
	trapcount = ntrap;
	clonercount = ncloner;
    }

    /* Chip, last, so nothing overwrites him -- with the four cells around him
     * cleared to floor.
     *
     * 🔴 THE POCKET IS NOT COSMETIC, IT IS WHAT MAKES THE SEARCH WORK. Without
     * it a room this densely packed kills Chip almost immediately: measured,
     * 4,185 of 5,000 trials ended "lost" and the mean run was 36 of a possible
     * 160 ticks, with a large mass dying on tick 1 against an adjacent monster
     * or a square of water. A trial that ends on tick 1 exercises nothing and
     * is a wasted seed. Clearing the four orthogonal neighbors costs four
     * cells of randomness and buys Chip enough time to actually push a block
     * onto something. */
    x = ROOM_X + gen_below(ROOM_W);
    y = ROOM_Y + gen_below(ROOM_H);
    if (x > ROOM_X)			fix_settop(lv, x - 1, y, FIX_FLOOR);
    if (x < ROOM_X + ROOM_W - 1)	fix_settop(lv, x + 1, y, FIX_FLOOR);
    if (y > ROOM_Y)			fix_settop(lv, x, y - 1, FIX_FLOOR);
    if (y < ROOM_Y + ROOM_H - 1)	fix_settop(lv, x, y + 1, FIX_FLOOR);
    fix_settop(lv, x, y, (unsigned char)(0x6C + gen_below(4)));

    /* Trap and cloner wiring, aimed at the machinery the stacks actually
     * placed. A brown button wired to open a beartrap that exists, with a
     * creature standing in it, is the arrangement the trap toggles are about;
     * a wiring to a random empty square is not. Each gets a button placed on
     * the map too, so Chip or a creature can step on it.
     *
     * ⚠ A few wirings are still left fully random on purpose. The engine does
     * not require an endpoint to hold the matching tile, real level sets
     * contain wirings that point at nothing (jc-45 was found in exactly that
     * data), and those paths deserve to be generated too. */
    for (i = 0 ; i < trapcount ; ++i) {
	int bx = ROOM_X + gen_below(ROOM_W), by = ROOM_Y + gen_below(ROOM_H);
	fix_settop(lv, bx, by, 0x27);			/* brown button */
	fix_addtrap(lv, bx, by, trapx[i], trapy[i]);
    }
    for (i = 0 ; i < clonercount ; ++i) {
	int bx = ROOM_X + gen_below(ROOM_W), by = ROOM_Y + gen_below(ROOM_H);
	fix_settop(lv, bx, by, 0x24);			/* red button */
	fix_addcloner(lv, bx, by, clonerx[i], clonery[i]);
    }
    n = gen_below(3);
    for (i = 0 ; i < n ; ++i)
	fix_addtrap(lv, ROOM_X + gen_below(ROOM_W), ROOM_Y + gen_below(ROOM_H),
			ROOM_X + gen_below(ROOM_W), ROOM_Y + gen_below(ROOM_H));
    n = gen_below(3);
    for (i = 0 ; i < n ; ++i)
	fix_addcloner(lv, ROOM_X + gen_below(ROOM_W), ROOM_Y + gen_below(ROOM_H),
			  ROOM_X + gen_below(ROOM_W), ROOM_Y + gen_below(ROOM_H));

    lv->number = 1;
    lv->time = 0;
    lv->chips = gen_below(4);
}

/* ------------------------------------------------------------- the moves -- */

/* A separate generator for the moves, seeded from the same seed but advanced
 * independently, so that changing the level alphabet does not shuffle the move
 * stream of every previously recorded witness. */
static u64 movestate;

static int nofix_nextmove(int step, void *ctx)
{
    static int const cmds[8] = {
	NIL, CmdNorth, CmdWest, CmdSouth, CmdEast, CmdNorth, CmdWest, CmdEast
    };
    (void)step; (void)ctx;
    movestate ^= movestate >> 12;
    movestate ^= movestate << 25;
    movestate ^= movestate >> 27;
    return cmds[(movestate * 2685821657736338717ULL) & 7];
}

/* ------------------------------------------------------------- one trial -- */

static gamestate       *st = NULL;

static u64 trial(u64 seed, char const **outcome, int *ticks)
{
    fixlevel		lv;
    gamesetup		setup;
    gamelogic	       *ms;
    unsigned char      *data;
    int			size;
    u64			d;

    if (!st) {
	st = calloc(1, sizeof *st);
	if (!st) { fprintf(stderr, "out of memory\n"); exit(2); }
    }

    gen_seed(seed);
    gen_level(&lv);
    movestate = seed ^ 0xD1B54A32D192ED03ULL;

    data = fix_build(&lv, &size);
    if (!data) { *outcome = "buildfail"; *ticks = 0; return 0; }

    /* A fresh engine per trial: mslogic.c keeps file-scope state, and reusing
     * one would make every digest depend on the trials before it -- which would
     * make a witness seed unreproducible on its own, and a witness you cannot
     * replay in isolation is worthless. */
    ms = mslogicstartup();
    if (!ms) { free(data); *outcome = "startupfail"; *ticks = 0; return 0; }
    ms->state = st;

    d = tw_run_level(ms, st, &setup, data, size, 1, Ruleset_MS,
		     0x5EED0000UL, NOFIX_TICKS, nofix_nextmove, NULL,
		     outcome, ticks);

    (*ms->shutdown)(ms);
    free(data);
    return d;
}

/* ------------------------------------------------------------------ main -- */

/* Which toggle this binary was built with, for the -id mode. The driver uses it
 * to confirm it is running the binary it thinks it is -- a matrix built by
 * accidentally comparing a build against itself would be all blanks, and would
 * look exactly like "no witnesses found". */
/* 🔴 THE BUILD ID IS PASSED AS A BARE SUFFIX AND REASSEMBLED HERE. Two traps
 * were walked into on the way to this, and both produced a confident wrong
 * answer rather than an error:
 *
 *   -DNOFIX_BUILD_ID='"NO_FIX_x"'   the quotes have to survive bash, Windows
 *                                   PowerShell and a CI shell. PowerShell 5.1
 *                                   strips them, so the macro expands to a bare
 *                                   identifier and this function does not
 *                                   compile -- which the driver reports as "the
 *                                   fix-off build does not compile", i.e. as a
 *                                   finding about the ENGINE rather than about
 *                                   quoting.
 *
 *   -DNOFIX_BUILD_ID=NO_FIX_x       stringifying that gives "1", not the name:
 *                                   -DNO_FIX_x already defines NO_FIX_x as 1,
 *                                   so the stringify macro expands it one step
 *                                   too far. Every binary then reports its id
 *                                   as "1" and the identity check fails on all
 *                                   32 at once.
 *
 * Passing the SUFFIX (-DNOFIX_BUILD_ID_TOKEN=x, where x is not a macro) avoids
 * both: nothing to quote, and nothing to expand. */
#define	NOFIX_STR2(x)	#x
#define	NOFIX_STR(x)	NOFIX_STR2(x)

static char const *build_id(void)
{
#if defined(NOFIX_BUILD_ID_TOKEN)
    return "NO_FIX_" NOFIX_STR(NOFIX_BUILD_ID_TOKEN);
#else
    return "default";
#endif
}

int main(int argc, char **argv)
{
    char const *outcome;
    int		ticks;
    u64		seed, start, count, i, d;

    if (argc >= 2 && !strcmp(argv[1], "-id")) {
	printf("%s\n", build_id());
	return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "-scan")) {
	start = strtoull(argv[2], NULL, 0);
	count = strtoull(argv[3], NULL, 0);
	if (count == 0) {
	    fprintf(stderr, "a scan of zero seeds proves nothing\n");
	    return 2;
	}
	for (i = 0 ; i < count ; ++i) {
	    d = trial(start + i, &outcome, &ticks);
	    printf("%llu\t%016llx\t%s\t%d\n",
		   (unsigned long long)(start + i), d, outcome, ticks);
	}
	return 0;
    }
    /* -stats answers the question that comes up the moment a toggle shows no
     * witness: "is the generator even BUILDING the arrangement this fix is
     * about, or am I searching for something that never occurs?" Those are very
     * different problems and the search output alone cannot tell them apart. */
    if (argc == 4 && !strcmp(argv[1], "-stats")) {
	long tankcloner = 0, crtrap = 0, blockteleport = 0, crcloner = 0;
	long blockcloner = 0, crforce = 0, blocktrap = 0, levels = 0;
	fixlevel lv;
	start = strtoull(argv[2], NULL, 0);
	count = strtoull(argv[3], NULL, 0);
	for (i = 0 ; i < count ; ++i) {
	    int c;
	    gen_seed(start + i);
	    gen_level(&lv);
	    ++levels;
	    for (c = 0 ; c < FIX_CELLS ; ++c) {
		unsigned char t = lv.top[c], b = lv.bot[c];
		int iscr = (t >= 0x40 && t <= 0x63);
		if (t >= 0x4C && t <= 0x4F && b == 0x31) ++tankcloner;
		if (iscr && b == 0x2B) ++crtrap;
		if (iscr && b == 0x31) ++crcloner;
		if (t == 0x0A && b == 0x29) ++blockteleport;
		if (t == 0x0A && b == 0x31) ++blockcloner;
		if (t == 0x0A && b == 0x2B) ++blocktrap;
		if (iscr && (b == 0x0D || b == 0x12 || b == 0x13
					|| b == 0x14 || b == 0x32)) ++crforce;
	    }
	}
	printf("over %ld generated level(s):\n", levels);
	printf("  tank on clone machine      %ld\n", tankcloner);
	printf("  creature on clone machine  %ld\n", crcloner);
	printf("  creature in beartrap       %ld\n", crtrap);
	printf("  creature on force floor    %ld\n", crforce);
	printf("  block on teleport          %ld\n", blockteleport);
	printf("  block on clone machine     %ld\n", blockcloner);
	printf("  block in beartrap          %ld\n", blocktrap);
	return 0;
    }
    /* -diff is the search proper. It reads the default build's digests for a
     * seed range and reports the first few seeds where THIS build disagrees,
     * stopping as soon as it has enough.
     *
     * Doing the comparison in here rather than by piping two scans through
     * diff(1) is not premature cleverness: the searches that matter run to
     * millions of seeds, and writing two multi-hundred-megabyte scan files per
     * toggle -- 32 times -- makes the run disk-bound and the early exit
     * impossible. A toggle whose witness turns up at seed 900 should cost 900
     * trials, not a million. */
    if (argc >= 5 && !strcmp(argv[1], "-diff")) {
	FILE   *bf;
	char	line[256];
	u64	want, got;
	int	hits = 0, maxhits;
	unsigned long long bseed;

	start = strtoull(argv[3], NULL, 0);
	count = strtoull(argv[4], NULL, 0);
	maxhits = (argc >= 6) ? atoi(argv[5]) : 3;
	bf = fopen(argv[2], "r");
	if (!bf) {
	    fprintf(stderr, "cannot read the baseline scan %s\n", argv[2]);
	    return 2;
	}
	for (i = 0 ; i < count ; ++i) {
	    if (!fgets(line, sizeof line, bf)) {
		fprintf(stderr, "the baseline scan ran out after %llu seed(s)"
				" -- it is shorter than the range asked for\n",
			(unsigned long long)i);
		fclose(bf);
		return 2;
	    }
	    if (sscanf(line, "%llu %llx", &bseed, &want) != 2) {
		fprintf(stderr, "malformed baseline line: %s", line);
		fclose(bf);
		return 2;
	    }
	    seed = start + i;
	    if (bseed != seed) {
		fprintf(stderr, "the baseline scan is for a different range"
				" (expected seed %llu, found %llu)\n",
			(unsigned long long)seed, bseed);
		fclose(bf);
		return 2;
	    }
	    got = trial(seed, &outcome, &ticks);
	    if (got != want) {
		printf("%llu\t%016llx\t%016llx\t%s\t%d\n",
		       (unsigned long long)seed, want, got, outcome, ticks);
		if (++hits >= maxhits)
		    break;
	    }
	}
	fclose(bf);
	/* Exit 1 means "no witness in this range". That is a legitimate result,
	 * not an error -- the driver records it as a blank row. */
	return hits ? 0 : 1;
    }
    if (argc == 3 && !strcmp(argv[1], "-one")) {
	seed = strtoull(argv[2], NULL, 0);
	d = trial(seed, &outcome, &ticks);
	printf("%016llx\t%s\t%d\n", d, outcome, ticks);
	return 0;
    }

    fprintf(stderr,
	"usage: nofix -scan  <start> <count>            digest a range of seeds\n"
	"       nofix -diff  <baseline> <start> <count> [maxhits]\n"
	"                                              find seeds where THIS build\n"
	"                                              disagrees with a -scan file\n"
	"       nofix -one   <seed>                    digest one seed\n"
	"       nofix -stats <start> <count>           what arrangements the\n"
	"                                              generator is producing\n"
	"       nofix -id                              which toggle this build has\n"
	"\n"
	"Build once per toggle (-DNO_FIX_x -DNOFIX_BUILD_ID='\"NO_FIX_x\"'), scan\n"
	"once with the default build, then -diff each toggle build against it. A\n"
	"seed whose digest differs is a WITNESS that the fix is live and reachable.\n"
	"\n"
	"-diff exits 1 when it finds nothing. That is a legitimate result, not an\n"
	"error: it means the search did not build the arrangement, NOT that the fix\n"
	"does nothing. Reach for -stats before concluding anything from a blank.\n"
	"\n"
	"Drivers: test/run-nofix.ps1 (Windows, and -Search rebuilds the matrix),\n"
	"test/run-nofix.sh (the CI check).\n");
    return 2;
}
