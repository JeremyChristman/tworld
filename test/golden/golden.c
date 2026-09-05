/* golden.c: a golden-master snapshot of both engines over the committed levels.
 *
 * MOD (Jeremy). THE GAP THIS CLOSES. Until this existed, nothing in CI could
 * detect an engine behavior change. Six jobs went green on a push that
 * silently broke replay: the whole automated replay gate was ONE end-to-end
 * case driving a synthesized set with one valid and one invalid solution. The
 * instrument that can actually answer the question -- test/run-corpus.ps1 --
 * needs the maintainer's private collection and his desktop, and runs by hand.
 *
 * Meanwhile 903 levels sit committed in data/ (CCLP1-5, CCLXP2, intro; the
 * freely redistributable ones, see docs/adr/0005) doing nothing as an oracle.
 *
 * WHAT IT DOES. Every level in every committed .dat, through BOTH engines,
 * driven by a deterministic move stream, hashing the whole gamestate after
 * every tick. One 64-bit digest per (file, ruleset, level). The digests are
 * committed to test/golden/engine-snapshot.tsv; -check recomputes them and
 * fails on any difference.
 *
 * 🔴 WHAT A GOLDEN MASTER IS AND IS NOT. It pins what the engine DOES. It can
 * never say the behavior is right -- only that it changed. A digest that moves
 * is a question ("which rule did I just alter, and did I mean to?"), never by
 * itself a bug report. And re-baselining is a DELIBERATE act: run -update, read
 * the diff, and justify every line of it in the commit message, exactly as a
 * corpus differential is justified. A reflexive -update makes this file worth
 * nothing at all, and it will be the easiest thing in this repository to abuse.
 *
 * ⚠ IT DOES NOT REPLACE THE SOLUTION CORPUS. 18,640 recorded human solutions
 * walk paths a synthetic move stream never will; a random walker mostly bumps
 * into the furniture near where it started. This is the cheap gate that runs
 * on every push. run-corpus.ps1 stays the instrument that decides whether a
 * release ships. Nothing here should ever be quoted as "replay is safe".
 *
 * 🔴 THE MOVE STREAM USES ITS OWN PRNG, NOT THE ENGINE'S. This matters more
 * than it looks. random.c's generator is part of the system under test -- the
 * engines draw from it for blob movement and random slides. If the input were
 * drawn from it too, an intentional change to random.c would silently change
 * every level's INPUT as well as its output, and the resulting diff would be
 * unreadable: you could not tell a behavior change from a different walk. The
 * walker below is a self-contained xorshift64* seeded per level, so the input
 * is fixed forever no matter what happens to the engines.
 *
 * DETERMINISM, THE OTHER HALF. state.mainprng is restarted with a fixed seed
 * per level, a FRESH engine is started for every level (both keep file-scope
 * state, and reusing one would make every digest depend on the levels before
 * it), and only named fields are hashed -- never a raw struct, whose padding
 * bytes are indeterminate and would produce a digest that differs between
 * compilers for no reason at all.
 *
 * 🔴 HOW MUCH THIS ACTUALLY COVERS -- MEASURED, AND THE NUMBER IS HUMBLING.
 * Every one of the 32 NO_FIX_* engine toggles was built separately and checked
 * against this baseline. It detects TWO of them: NO_FIX_BLUE_BUTTON_TIMING
 * (26 rows) and NO_FIX_CHIP_ONTO_CLONER (1 row). The other 30 are invisible.
 *
 * That is not a bug to be tuned away, and two experiments say so:
 *
 *   ticks 400 -> 2000     detected nothing further (6 sample toggles, 0 -> 0)
 *   walks 1 -> 4 -> 12    detected nothing further (same 6, 0 -> 0 -> 0)
 *
 * Depth and breadth both buy nothing because the limit is not how long or how
 * often the walker wanders: it is that a random walker does not CONSTRUCT the
 * situations the desync work is about. A block resting on a teleport, a tank on
 * a cloner, a creature in a trap whose button is pressed this tick -- those are
 * arranged, not stumbled into. Reaching them needs designed fixtures, which is
 * the NO_FIX_* differential matrix that CLAUDE.md has wanted for a while and
 * that this file does NOT replace.
 *
 * So state the claim narrowly. This catches GROSS engine change: a mutation to
 * Chip's idle timer moved 577 of 1,806 rows. It is a smoke alarm, not an audit.
 *
 * ⭐ It did earn its keep immediately, in a way worth recording: building all
 * 32 toggles one at a time found that TWO OF THEM DID NOT COMPILE
 * (NO_FIX_RFF_DRAW_ONCE, NO_FIX_TELEPORT_STALE_FG -- each declared its state
 * variable under one toggle and read it under another). ADR 0002 keeps that
 * scaffolding so a future desync investigation can flip a switch; two of the
 * switches were broken, and nothing would have said so until somebody needed
 * one. Fixed in mslogic.c; see the notes there.
 *
 * WHY IT IS NOT A UNIT TEST. test/run-tests.ps1 compiles one .c file that
 * #includes the source under test. mslogic.c and lxlogic.c cannot both be
 * included into one translation unit -- they define colliding file-scope names
 * and macros (getchip, chipisalive, creatures...) and are separate TUs in the
 * real build. So this links them the way tworld2 itself does, which has the
 * side benefit of compiling exactly what ships.
 */

/* The engine driver, the state digest and the die()-does-not-return surface
 * all live here, shared with test/nofix/nofix.c so the two cannot drift. */
#include	"tw_engine_digest.h"

/* How far to drive each level. 400 ticks is 100 of Chip's moves in the MS
 * ruleset (he gets one every four): enough for a random walker to reach the
 * furniture near the start, and cheap enough that 903 levels times two
 * rulesets stays well under a minute. */
#ifndef	TICKS_PER_LEVEL
#define	TICKS_PER_LEVEL		400
#endif

/* How many independent walks per level. Each uses a different walker seed and
 * folds into the same digest, so more walks reach more of the level for a
 * proportional increase in run time. See the coverage note at the foot of this
 * comment block before changing either number: they were chosen by measurement,
 * not taste. */
#ifndef	WALKS_PER_LEVEL
#define	WALKS_PER_LEVEL		1
#endif

/* ------------------------------------------------------------ the walker -- */

/* xorshift64*, self-contained. See the header: the input to this harness must
 * never come from the code under test. */
static u64 walkstate;

static void walk_seed(u64 seed)
{
    /* Zero is a fixed point of xorshift; never let one in. */
    walkstate = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static u64 walk_next(void)
{
    walkstate ^= walkstate >> 12;
    walkstate ^= walkstate << 25;
    walkstate ^= walkstate >> 27;
    return walkstate * 2685821657736338717ULL;
}

/* ------------------------------------------------------------- the drive -- */

/* The move source handed to tw_run_level(): one command per MS move, drawn
 * from the walker rather than from the engine's own PRNG. */
static int golden_nextmove(int step, void *ctx)
{
    static int const cmds[8] = {
	NIL, CmdNorth, CmdWest, CmdSouth, CmdEast, CmdNorth, CmdEast, NIL
    };
    (void)step; (void)ctx;
    return cmds[walk_next() & 7];
}

/* Run one level through one engine and return its digest. *outcome receives a
 * short word for how it ended, committed alongside the digest because "the
 * digest changed" is far easier to read when the outcome says the level went
 * from being won to being lost. */
static u64 runlevel(gamelogic *logic, gamestate *st, gamesetup *setup,
		    unsigned char *data, int size, int number, int ruleset,
		    int walknum, char const **outcome, int *ticksrun)
{
    /* Seeded from the level number AND the ruleset, so the two engines get
     * different streams on the same level. Giving them the same one is a worse
     * test: identical input tends to wedge both walkers against one wall. */
    walk_seed(0xA5A5A5A5ULL * (u64)(number + 1) + (u64)ruleset
	      + (u64)walknum * 7919ULL);

    return tw_run_level(logic, st, setup, data, size, number, ruleset,
			/* the ENGINE's seed, fixed per level: */
			0x5EED0000UL + (unsigned long)number,
			TICKS_PER_LEVEL, golden_nextmove, NULL,
			outcome, ticksrun);
}

/* ------------------------------------------------------- the .dat reader -- */

/* The container walk, written from the FORMAT rather than from series.c, for
 * the reason test/tw_fixture.h gives: a reader written by reading the parser
 * only proves the two agree with each other.
 *
 * 4-byte signature, 2-byte level count, then each level as a 2-byte record
 * length followed by that many bytes. The record BODY is what encoding.c is
 * handed; its inner header (number, time, chips, detail, layer sizes) is
 * encoding.c's business, not ours. */
static unsigned char *readfile(char const *path, long *size)
{
    FILE	   *f;
    unsigned char  *buf;
    long	    n;

    f = fopen(path, "rb");
    if (!f)
	return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 6) { fclose(f); return NULL; }
    buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
	free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *size = n;
    return buf;
}

static unsigned readword(unsigned char const *p) { return p[0] | (p[1] << 8); }

/* ------------------------------------------------------------------ rows -- */

struct row {
    char	file[64];
    char	ruleset[8];
    int		number;
    u64		digest;
    int		ticks;
    char	outcome[16];
};

static struct row     *rows = NULL;
static int		rowcount = 0, rowalloc = 0;

static void addrow(char const *file, char const *ruleset, int number,
		   u64 digest, int ticks, char const *outcome)
{
    struct row *r;
    if (rowcount == rowalloc) {
	rowalloc = rowalloc ? rowalloc * 2 : 2048;
	rows = realloc(rows, (size_t)rowalloc * sizeof *rows);
	if (!rows) { fprintf(stderr, "out of memory\n"); exit(2); }
    }
    r = &rows[rowcount++];
    memset(r, 0, sizeof *r);
    snprintf(r->file, sizeof r->file, "%s", file);
    snprintf(r->ruleset, sizeof r->ruleset, "%s", ruleset);
    snprintf(r->outcome, sizeof r->outcome, "%s", outcome);
    r->number = number;
    r->digest = digest;
    r->ticks = ticks;
}

static char const *basename_of(char const *path)
{
    char const *s = path, *p;
    for (p = path ; *p ; ++p)
	if (*p == '/' || *p == '\\')
	    s = p + 1;
    return s;
}

static int scanfile(char const *path)
{
    unsigned char  *buf;
    long	    size;
    unsigned long   off;
    unsigned	    count, k;
    gamestate      *st;
    gamesetup	    setup;
    char const     *base = basename_of(path);

    buf = readfile(path, &size);
    if (!buf) {
	fprintf(stderr, "cannot read %s\n", path);
	return 0;
    }

    /* gamestate is tens of kilobytes; on the heap so a deep stack is never the
     * reason this fails on somebody's CI runner. */
    st = calloc(1, sizeof *st);
    if (!st) { fprintf(stderr, "out of memory\n"); exit(2); }

    count = readword(buf + 4);
    off = 6;
    for (k = 0 ; k < count ; ++k) {
	gamelogic      *ms, *lx;
	unsigned	len;
	int		number, ticks, t1, w;
	u64		d, d1;
	char const     *outcome, *out1;

	if (off + 2 > (unsigned long)size) break;
	len = readword(buf + off);
	off += 2;
	if (len < 10 || off + len > (unsigned long)size) break;
	number = (int)readword(buf + off);

	/* Each walk gets a FRESH engine, for the reason in the header: the
	 * engines keep file-scope state, and reusing one would make a digest
	 * depend on the walks before it. */
	d = FNV_OFFSET; ticks = 0; outcome = "norun";
	for (w = 0 ; w < WALKS_PER_LEVEL ; ++w) {
	    ms = mslogicstartup();
	    if (!ms) break;
	    ms->state = st;
	    d1 = runlevel(ms, st, &setup, buf + off, (int)len, number,
			  Ruleset_MS, w, &out1, &t1);
	    hash_int(&d, (long)(d1 & 0xFFFFFFFFUL));
	    hash_int(&d, (long)((d1 >> 32) & 0xFFFFFFFFUL));
	    ticks += t1;
	    /* The first walk names the outcome, unless a later one DIED --
	     * an assert failure is the one result that must never be averaged
	     * away by whichever walk happened to be first. */
	    if (w == 0 || !strcmp(out1, "DIED"))
		outcome = out1;
	    (*ms->shutdown)(ms);
	}
	addrow(base, "ms", number, d, ticks, outcome);

	d = FNV_OFFSET; ticks = 0; outcome = "norun";
	for (w = 0 ; w < WALKS_PER_LEVEL ; ++w) {
	    lx = lynxlogicstartup();
	    if (!lx) break;
	    lx->state = st;
	    d1 = runlevel(lx, st, &setup, buf + off, (int)len, number,
			  Ruleset_Lynx, w, &out1, &t1);
	    hash_int(&d, (long)(d1 & 0xFFFFFFFFUL));
	    hash_int(&d, (long)((d1 >> 32) & 0xFFFFFFFFUL));
	    ticks += t1;
	    if (w == 0 || !strcmp(out1, "DIED"))
		outcome = out1;
	    (*lx->shutdown)(lx);
	}
	addrow(base, "lynx", number, d, ticks, outcome);

	off += len;
    }

    free(st);
    free(buf);
    return 1;
}

static void writerows(FILE *out)
{
    int i;
    fprintf(out, "# Golden-master engine snapshot."
		 " Regenerate with: golden -update <files>\n");
    fprintf(out, "# A digest that moves is a QUESTION, never by itself a bug"
		 " report. Read the\n");
    fprintf(out, "# diff and justify every line of it -- see"
		 " test/golden/golden.c.\n");
    fprintf(out, "#file\truleset\tlevel\tdigest\tticks\toutcome\n");
    for (i = 0 ; i < rowcount ; ++i)
	fprintf(out, "%s\t%s\t%d\t%016llx\t%d\t%s\n",
		rows[i].file, rows[i].ruleset, rows[i].number,
		rows[i].digest, rows[i].ticks, rows[i].outcome);
}

static int checkrows(char const *path)
{
    FILE       *f;
    char	line[512];
    int		i = 0, bad = 0, seen = 0;

    f = fopen(path, "r");
    if (!f) {
	fprintf(stderr, "no baseline at %s -- run with -update first\n", path);
	return 2;
    }
    while (fgets(line, sizeof line, f)) {
	char	file[64], ruleset[8], outcome[16];
	int	number, ticks;
	u64	digest;
	if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
	    continue;
	if (sscanf(line, "%63s %7s %d %llx %d %15s",
		   file, ruleset, &number, &digest, &ticks, outcome) != 6) {
	    fprintf(stderr, "malformed baseline line: %s", line);
	    ++bad;
	    continue;
	}
	++seen;
	if (i >= rowcount) {
	    fprintf(stderr, "MISSING  %s %s %d (the baseline has it, this run"
			    " does not)\n", file, ruleset, number);
	    ++bad;
	    continue;
	}
	if (strcmp(rows[i].file, file) || strcmp(rows[i].ruleset, ruleset)
					|| rows[i].number != number) {
	    fprintf(stderr, "ORDER    at row %d: the baseline says %s %s %d,"
			    " this run has %s %s %d\n",
		    i + 1, file, ruleset, number,
		    rows[i].file, rows[i].ruleset, rows[i].number);
	    ++bad;
	} else if (rows[i].digest != digest) {
	    fprintf(stderr, "CHANGED  %s %s level %d: %016llx -> %016llx"
			    "  (%s/%dt -> %s/%dt)\n",
		    file, ruleset, number, digest, rows[i].digest,
		    outcome, ticks, rows[i].outcome, rows[i].ticks);
	    ++bad;
	}
	++i;
    }
    fclose(f);

    if (seen != rowcount) {
	fprintf(stderr, "COUNT    the baseline has %d row(s), this run"
			" produced %d\n", seen, rowcount);
	++bad;
    }
    return bad;
}

int main(int argc, char **argv)
{
    int		update = 0, check = 0, i, first = 1;
    char const *baseline = "test/golden/engine-snapshot.tsv";

    for (i = 1 ; i < argc ; ++i) {
	if (!strcmp(argv[i], "-update")) { update = 1; first = i + 1; }
	else if (!strcmp(argv[i], "-check")) { check = 1; first = i + 1; }
	else if (!strcmp(argv[i], "-baseline") && i + 1 < argc) {
	    baseline = argv[++i]; first = i + 1;
	} else break;
    }
    if (first >= argc) {
	fprintf(stderr,
	    "usage: golden [-update | -check] [-baseline FILE] <level.dat>...\n"
	    "  no flag   print the snapshot to stdout\n"
	    "  -update   REWRITE the baseline. A deliberate act: read the diff\n"
	    "            and justify it, exactly as for a corpus differential.\n"
	    "  -check    recompute and fail on any difference\n");
	return 2;
    }

    for (i = first ; i < argc ; ++i)
	if (!scanfile(argv[i]))
	    return 2;

    /* 🔴 Refusing to report success on an empty run. A glob that matches
     * nothing is the classic way a green check proves nothing at all, and it
     * has already happened once in this project's history. */
    if (rowcount == 0) {
	fprintf(stderr, "no levels were read -- refusing to report success\n");
	return 2;
    }

    if (update) {
	FILE *f = fopen(baseline, "wb");   /* "wb": LF endings on Windows too */
	if (!f) { fprintf(stderr, "cannot write %s\n", baseline); return 2; }
	writerows(f);
	fclose(f);
	printf("wrote %s: %d row(s) over %d level(s)\n",
	       baseline, rowcount, rowcount / 2);
	return 0;
    }
    if (check) {
	int bad = checkrows(baseline);
	if (bad) {
	    fprintf(stderr,
		"\n%d difference(s). An engine behavior change is not"
		" automatically wrong --\nread each line, decide whether you"
		" meant it, and only then run -update.\n", bad);
	    return 1;
	}
	printf("golden master: %d row(s) over %d level(s) unchanged"
	       " (%d warning(s) from the level data)\n",
	       rowcount, rowcount / 2, warn_count);
	return 0;
    }
    writerows(stdout);
    return 0;
}
