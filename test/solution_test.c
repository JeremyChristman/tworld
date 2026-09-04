/* solution_test.c: the .tws move codec.
 *
 * MOD (Jeremy). Compiles ../solution.c (and ../fileio.c, which it calls)
 * directly. expandsolution() and contractsolution() work entirely in memory on
 * gamesetup->solutiondata, so this whole file needs no fixture on disk and no
 * level set -- which is why it is the cheapest high-value test in the suite.
 *
 * WHY THIS EXISTS. A .tws is a THIRD-PARTY FILE. Players download solution
 * collections made by other people and by other programs, and expandsolution()
 * is the first thing that touches those bytes. It is a five-format,
 * variable-length, bit-packed decoder driven by a length field inside the data
 * it is decoding. That is the shape of parser that goes wrong.
 *
 * It is also the format every recorded solution in every save directory is
 * stored in, so a change to the codec that is merely DIFFERENT -- not wrong --
 * silently invalidates work people cannot recreate.
 *
 * TWO KINDS OF CASE, deliberately:
 *
 *   1. ROUND TRIPS through contract -> expand. These prove the two halves agree
 *      with each other, which is necessary and not sufficient: two halves can
 *      agree on a format that is not the format.
 *   2. HAND-BUILT BYTE STREAMS decoded against expectations transcribed from
 *      the FORMAT SPECIFICATION in solution.c's header comment -- deliberately
 *      from the prose, not from the switch statement below it. This is the half
 *      that would catch both halves drifting together, and the only way to
 *      decode a stream this program would not itself have produced.
 *
 * ⚠ ABOUT FORMAT #3 (00DDEEFF, three moves in one byte). An earlier version of
 * this comment claimed contractsolution() never emits it. That was WRONG:
 * solution.c:436-445 has an explicit emission branch for three consecutive moves
 * exactly four ticks apart, and the round-trip case below at "contract then
 * expand preserves a simple orthogonal solution" (moves at 0, 4, 8, 12) reaches
 * it. What ignores format 3 is only the SIZE-ESTIMATE loop, which is why the
 * buffer is merely over-allocated.
 *
 * The hand-built case for format 3 still earns its place -- it is the only way
 * to decode a packed byte whose three moves are not what the encoder would have
 * chosen, which is what a file from another tool looks like.
 *
 * ⚠ -Wno-use-after-free is a SUPPRESSION OF A KNOWN GCC FALSE POSITIVE, not a
 * blanket relaxation. solution.c:462-464 reads
 *
 *     game->solutiondata = realloc(data, size);
 *     if (!game->solutiondata)
 *         game->solutiondata = data;
 *
 * which GCC 12+ flags as using `data` after realloc. It is correct C: a realloc
 * that FAILS leaves the original block allocated and the original pointer valid,
 * and that is exactly the branch guarded here. The suppression is scoped to this
 * one test file rather than fixed in the shipped source, because this is a fork
 * tracking upstream and rewriting correct upstream code to placate a compiler
 * adds a diff to carry forever. If the warning ever fires anywhere else, look at
 * that one properly -- do not widen this.
 *
 * TESTLANG: c
 *
 * That narrowing is a claim, not a convenience. fileio.c and
 * solution.c are compiled ONLY as C by CMake, and both rely on C's implicit
 * void* conversion (directly, and through err.h's x_alloc macro), which C++
 * rejects. Building them as C++ would not be testing the shipped program. The
 * dual-language rule in docs/adr/0004 exists for generic/in.c, which genuinely
 * is compiled both ways; it does not generalize to the whole tree.
 *
 * TESTFLAGS: -Wno-use-after-free
 */

#include	"tw_test.h"
#include	"tw_corpus.h"

/* NOTE: savedir and readonly are DEFINED BY solution.c itself (not by tworld.c,
 * as the extern declarations in solution.h might suggest), so this file must not
 * define them. `readonly` defaults to FALSE there, which would matter if any case
 * below reached a write path -- none does; everything under test operates on
 * gamesetup->solutiondata in memory and never opens a file.
 */

#include	"../fileio.c"
#include	"../solution.c"

/* solution.c calls this from createsolutionfilelist(); nothing under test here
 * reaches it. Stubbed rather than pulling in all of series.c, which would drag
 * six more modules in for a function no case below calls.
 */
int findlevelinseries(gameseries const *series, int number, char const *passwd)
{
    (void)series; (void)number; (void)passwd;
    return -1;
}

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

/* Counted rather than printed: two cases below assert that a malformed input is
 * REPORTED, not merely survived. A parser that returns FALSE silently and one
 * that says why are different products, and the difference is what a player
 * with a corrupt file sees.
 */
static int errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...)
{
    (void)prefix; (void)fmt;
    ++errmsg_count;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* --- helpers ------------------------------------------------------------ */

/* Build a gamesetup carrying the given raw solution bytes. The 16-byte header
 * is filled with recognizable values so that a decoder reading the wrong offset
 * produces an obviously wrong answer rather than a plausible zero.
 */
static void setsolution(gamesetup *game, unsigned char const *moves, int movecount,
			unsigned char flags, unsigned char slidestep,
			unsigned long seed)
{
    static unsigned char buffer[512];
    int i;

    memset(buffer, 0, sizeof buffer);
    buffer[6] = flags;
    buffer[7] = slidestep;
    buffer[8]  = (unsigned char)(seed & 0xFF);
    buffer[9]  = (unsigned char)((seed >> 8) & 0xFF);
    buffer[10] = (unsigned char)((seed >> 16) & 0xFF);
    buffer[11] = (unsigned char)((seed >> 24) & 0xFF);
    for (i = 0 ; i < movecount ; ++i)
	buffer[16 + i] = moves[i];

    memset(game, 0, sizeof *game);
    game->number = 1;
    game->solutiondata = buffer;
    game->solutionsize = 16 + movecount;
}

static void addmove(solutioninfo *s, int dir, int when)
{
    action act;
    act.dir = dir;
    act.when = when;
    addtomovelist(&s->moves, act);
}

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * Every input under test/fuzz/corpus/solution/ goes through expandsolution()
 * here, on every platform, with no clang and no sanitizer. That is what makes a
 * libFuzzer finding permanent: the fuzzer discovers it once on Linux, the input
 * is committed, and this replays it forever. See test/tw_corpus.h.
 *
 * The guard bytes are a real oracle in this file, because expandsolution()
 * reads directly out of the buffer tw_corpus.h fenced -- an over-write within
 * 64 bytes of either end is caught here without ASan.
 */
static int corpus_replayed = 0;

static void corpus_expand(twcorpusinput const *in)
{
    gamesetup		g;
    solutioninfo	s;

    memset(&g, 0, sizeof g);
    memset(&s, 0, sizeof s);
    g.number = 1;
    g.solutiondata = in->data;
    g.solutionsize = in->size;
    if (expandsolution(&s, &g))
	destroymovelist(&s.moves);
}

static int corpus_report(int ok, char const *name)
{
    ++corpus_replayed;
    CHECK_MSG(ok, "fuzz corpus input '%s' wrote outside its own buffer -- the"
		  " guard bytes around it were modified", name);
    return ok;
}

int main(void)
{
    gamesetup game;
    /* ZERO-INITIALIZED, and that is not tidiness. expandsolution() calls
     * initmovelist(), which is `if (!list->allocated || !list->list) { x_alloc }`
     * -- so with an indeterminate non-zero `allocated` and an indeterminate
     * non-NULL `list`, it takes the no-allocation branch and addtomovelist()
     * writes through a garbage pointer. It worked only because main's frame
     * happened to land on a fresh zero page. play.c:147 prepareplayback()
     * explicitly zeroes both fields before calling expandsolution for exactly
     * this reason. */
    solutioninfo sol = {0}, back = {0};
    int i;

    tw_begin("solution");
    tw_expect_atleast(1204);

    /* ================================================================== *
     * Decoding hand-built streams, against the format specification.
     * ================================================================== */

    tw_case("format 1, one byte: NNDDDTTT with NN=01");
    {
	/* NN=01, D=0 (NORTH), T=0. The FIRST move's time is not decremented,
	 * so T=0 means tick 0 -- the spec calls this out as the exception. */
	unsigned char const moves[] = { 0x01 };
	setsolution(&game, moves, 1, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 1);
	CHECK_INT(sol.moves.list[0].dir, NORTH);
	CHECK_INT(sol.moves.list[0].when, 0);
	destroymovelist(&sol.moves);
    }
    {
	/* D=3 (EAST), T=5 -> six ticks after the previous move, and this is the
	 * first move, so when = 5. */
	unsigned char const moves[] = { (unsigned char)(0x01 | (3 << 2) | (5 << 5)) };
	setsolution(&game, moves, 1, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 1);
	CHECK_INT(sol.moves.list[0].dir, EAST);
	CHECK_INT(sol.moves.list[0].when, 5);
	destroymovelist(&sol.moves);
    }

    tw_case("format 1, two bytes: T widens to 11 bits");
    {
	/* NN=10, D=1 (WEST), T=100. Low three bits of T in byte 0's top bits,
	 * the rest in byte 1. */
	int const t = 100;
	unsigned char const moves[] = {
	    (unsigned char)(0x02 | (1 << 2) | ((t & 0x07) << 5)),
	    (unsigned char)(t >> 3)
	};
	setsolution(&game, moves, 2, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 1);
	CHECK_INT(sol.moves.list[0].dir, WEST);
	CHECK_INT(sol.moves.list[0].when, t);
	destroymovelist(&sol.moves);
    }

    tw_case("format 3 packs three moves into one byte, four ticks apart");
    {
	/* 00DDEEFF: DD=0 NORTH, EE=1 WEST, FF=2 SOUTH. Each move is four ticks
	 * after the one before; the first is four after "tick -1", i.e. 3.
	 * This format is decoded but never written -- see the file header. */
	unsigned char const moves[] = {
	    (unsigned char)(0x00 | (0 << 2) | (1 << 4) | (2 << 6))
	};
	setsolution(&game, moves, 1, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 3);
	CHECK_INT(sol.moves.list[0].dir, NORTH);
	CHECK_INT(sol.moves.list[0].when, 3);
	CHECK_INT(sol.moves.list[1].dir, WEST);
	CHECK_INT(sol.moves.list[1].when, 7);
	CHECK_INT(sol.moves.list[2].dir, SOUTH);
	CHECK_INT(sol.moves.list[2].when, 11);
	destroymovelist(&sol.moves);
    }

    tw_case("format 2, four bytes: 27 bits of time, orthogonal moves only");
    {
	/* 11DD0TTT then three more time bytes. Bit 4 clear is what separates
	 * this from format 4. */
	unsigned long const t = 300005UL;   /* low three bits NONZERO on purpose: with
					     * 300000 the field in byte 0 is all zeros and
					     * the case cannot tell "read" from "ignored" */
	unsigned char const moves[] = {
	    (unsigned char)(0x03 | (2 << 2) | ((t & 0x07) << 5)),   /* DD=2 SOUTH */
	    (unsigned char)((t >> 3) & 0xFF),
	    (unsigned char)((t >> 11) & 0xFF),
	    (unsigned char)((t >> 19) & 0xFF)
	};
	setsolution(&game, moves, 4, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 1);
	CHECK_INT(sol.moves.list[0].dir, SOUTH);
	CHECK_INT(sol.moves.list[0].when, (long)t);
	destroymovelist(&sol.moves);
    }

    tw_case("format 4 carries a seven-bit direction, for mouse moves");
    {
	/* 11NN1DDD DDDDDDTT. NN=0 (two bytes), direction is the raw value, not
	 * an index -- that is the whole point of this format. */
	int const dir = 0x2A;
	unsigned char const moves[] = {
	    (unsigned char)(0x03 | (0 << 2) | 0x10 | ((dir & 0x07) << 5)),
	    (unsigned char)(((dir >> 3) & 0x3F) | (1 << 6))   /* T = 1 */
	};
	setsolution(&game, moves, 2, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.moves.count, 1);
	CHECK_INT(sol.moves.list[0].dir, dir);
	CHECK_INT(sol.moves.list[0].when, 1);
	destroymovelist(&sol.moves);
    }

    /* ================================================================== *
     * The header fields.
     * ================================================================== */

    tw_case("the per-level header fields are read from their documented offsets");
    {
	unsigned char const moves[] = { 0x01 };
	/* byte 7: slide direction in the low three bits, stepping in the next
	 * three. Index 2 is SOUTH; stepping 5. */
	setsolution(&game, moves, 1, 0x00, (unsigned char)(2 | (5 << 3)), 0xDEADBEEFUL);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.rndslidedir, SOUTH);
	CHECK_INT(sol.stepping, 5);
	CHECK_INT(sol.rndseed, (long)0xDEADBEEFUL);
	CHECK_INT(sol.flags, 0);
	destroymovelist(&sol.moves);

	/* The top two bits of byte 7 are documented as unused, and must not
	 * leak into the stepping value. */
	setsolution(&game, moves, 1, 0x00, (unsigned char)(0xC0 | 1 | (3 << 3)), 0);
	CHECK_INT(expandsolution(&sol, &game), TRUE);
	CHECK_INT(sol.stepping, 3);
	CHECK_INT(sol.rndslidedir, WEST);
	destroymovelist(&sol.moves);
    }

    /* 🔴 REGRESSION, jc-46. The seed is the only field assembled out of four
     * bytes, and the old expression shifted bytes that had promoted to a
     * SIGNED int -- solutiondata[11] << 24 with a high byte >= 0x80 overflowed
     * into the sign bit, which is undefined. UndefinedBehaviorSanitizer caught
     * it on the first Linux run this project ever made, on the 0xDE of the
     * 0xDEADBEEF case immediately above.
     *
     * Every other case in this file used a seed under 0x80000000, and the long
     * round-trip below happens to use 0x7FFFFFFF -- one short of the bit that
     * matters. So the boundary gets walked explicitly here.
     *
     * This is a real oracle with no sanitizer at all: rndseed is unsigned
     * long, which is 64 bits on LP64, so the old code sign-extended a high-bit
     * seed to 0xFFFFFFFF........ and these assertions fail on Linux whether or
     * not the sanitizer is switched on. */
    tw_case("a seed with the high bit set survives byte reassembly");
    {
	unsigned char const moves[] = { 0x01 };
	static unsigned long const seeds[] = {
	    0x7FFFFFFFUL, 0x80000000UL, 0x80000001UL,
	    0xDEADBEEFUL, 0xFF000000UL, 0xFFFFFFFFUL
	};
	for (i = 0 ; i < (int)(sizeof seeds / sizeof *seeds) ; ++i) {
	    setsolution(&game, moves, 1, 0x00, 0, seeds[i]);
	    CHECK_INT(expandsolution(&sol, &game), TRUE);
	    CHECK_MSG(sol.rndseed == seeds[i],
		      "seed %lu came back as %lu", seeds[i], sol.rndseed);
	    /* Whatever width unsigned long is, nothing above bit 31 may appear. */
	    CHECK_MSG((sol.rndseed & ~0xFFFFFFFFUL) == 0,
		      "seed %lu sign-extended past bit 31", seeds[i]);
	    destroymovelist(&sol.moves);
	}

	/* And it must still be that value after a write and a read back. */
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	sol.rndseed = 0xFEDCBA98UL;
	sol.flags = 0;
	sol.rndslidedir = NORTH;
	sol.stepping = 0;
	addmove(&sol, NORTH, 0);
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_INT(game.solutiondata[11], 0xFE);
	CHECK_INT(expandsolution(&back, &game), TRUE);
	CHECK_MSG(back.rndseed == 0xFEDCBA98UL,
		  "high-bit seed round-tripped to %lu", back.rndseed);
	destroymovelist(&sol.moves);
	destroymovelist(&back.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    /* 🔴 REGRESSION, jc-46 -- and the ONLY thing in this repository that
     * reaches the second half of that fix. besttime is assembled in
     * readsolution(), which needs a real file, so nothing touched it: this file
     * never opened one, test/mkfixture.c writes all-zero bytes at offsets 12-15
     * so the end-to-end layer never sees a high byte either, and
     * series_test.c stubs readsolutions() out entirely. That hunk could have
     * been reverted and shipped without one thing going red.
     *
     * ⚠ ON ANY MAINSTREAM COMPILER THIS CANNOT TELL FIXED FROM BROKEN BY
     * VALUE. Both forms assemble into an int of the same width, so the bits
     * agree and the assertion below passes either way. Its real job is to make
     * the line EXECUTE with bit 31 set, which is what gives
     * test/run-sanitizers.sh something to catch. Do not mistake it for a
     * value oracle, and do not delete it for "not testing anything". */
    tw_case("a recorded best time with the high bit set is read back (jc-46)");
    {
	char const *path = "tw_besttime_test.tws";
	FILE *f;

	f = fopen(path, "wb");
	if (!f) {
	    tw_skip("could not create a temporary .tws in the working directory");
	} else {
	    /* A 32-bit record length, then the record: level number, password,
	     * flags, slide/stepping, the seed, the time, one move byte. 17 and
	     * not 16 because readsolution() refuses any size <= 16 that is not
	     * exactly 6. */
	    fileinfo file;
	    gamesetup g;
	    unsigned char rec[17];
	    unsigned char len[4];

	    memset(rec, 0, sizeof rec);
	    rec[0] = 0x01;                                  /* level number 1 */
	    rec[2] = 'A'; rec[3] = 'B'; rec[4] = 'C'; rec[5] = 'D';
	    rec[8] = 0x11; rec[9] = 0x22; rec[10] = 0x33; rec[11] = 0xC4;
	    /* 0xFEDCBA98: bit 31 set, which is the entire point of the case. */
	    rec[12] = 0x98; rec[13] = 0xBA; rec[14] = 0xDC; rec[15] = 0xFE;
	    rec[16] = 0x01;
	    len[0] = (unsigned char)(sizeof rec); len[1] = 0; len[2] = 0; len[3] = 0;
	    fwrite(len, 1, 4, f);
	    fwrite(rec, 1, sizeof rec, f);
	    fclose(f);

	    clearfileinfo(&file);
	    file.name = (char*)path;
	    file.fp = fopen(path, "rb");
	    if (!file.fp) {
		tw_skip("could not reopen the temporary .tws");
	    } else {
		memset(&g, 0, sizeof g);
		CHECK_INT(readsolution(&file, &g), TRUE);
		CHECK_INT(g.number, 1);
		CHECK_MSG(g.besttime == (int)0xFEDCBA98UL,
			  "besttime came back as %d, expected %d",
			  g.besttime, (int)0xFEDCBA98UL);
		fclose(file.fp);
		free(g.solutiondata);
	    }
	    remove(path);
	}
    }

    tw_case("every committed fuzz corpus input still parses safely");
    {
	char dir[256];
	int n;

	/* A corpus that cannot be FOUND must fail, not skip. Reporting success
	 * for a replay that read no files is precisely the false green this
	 * suite exists to prevent -- and it is the failure mode a corpus rots
	 * into, silently, the first time someone moves a directory. */
	CHECK_MSG(tw_corpus_dir("solution", dir, sizeof dir),
		  "could not find test/fuzz/corpus/solution from the working"
		  " directory -- the replay would have proved nothing");
	if (dir[0]) {
	    n = tw_corpus_run(dir, corpus_expand, corpus_report);
	    /* %.100s, not %s: tw_fail_ formats into a 256-byte buffer and
	     * -Werror=format-truncation rejects an unbounded %s of a 256-byte
	     * array. CHECK_STR bounds its operands the same way. */
	    CHECK_MSG(n > 0, "corpus directory %.100s held no inputs", dir);
	    CHECK_INT(corpus_replayed, n);
	}
    }

    /* ================================================================== *
     * Malformed input. These are the cases that matter for a file somebody
     * else made.
     * ================================================================== */

    tw_case("a solution shorter than its own header is refused");
    {
	unsigned char buffer[32];
	memset(buffer, 0, sizeof buffer);
	memset(&game, 0, sizeof game);
	game.solutiondata = buffer;
	for (i = 0 ; i <= 16 ; ++i) {
	    game.solutionsize = i;
	    CHECK_MSG(expandsolution(&sol, &game) == FALSE,
		      "a %d-byte solution was accepted; 16 or fewer must be refused", i);
	}
    }

    tw_case("a two-byte move truncated to one byte is reported, not misread");
    {
	/* Format 1's two-byte form, with the second byte missing. Read past the
	 * end, this would take whatever followed in memory as the high bits of
	 * the timestamp. */
	unsigned char const moves[] = { (unsigned char)(0x02 | (1 << 2)) };
	errmsg_count = 0;
	setsolution(&game, moves, 1, 0, 0, 0);
	CHECK_INT(expandsolution(&sol, &game), FALSE);
	CHECK_MSG(errmsg_count == 1, "expected one diagnostic, got %d", errmsg_count);
	CHECK_INT(sol.moves.count, 0);
	destroymovelist(&sol.moves);
    }

    tw_case("a four-byte move truncated at every length is reported");
    {
	unsigned long const t = 300005UL;   /* low three bits NONZERO on purpose: with
					     * 300000 the field in byte 0 is all zeros and
					     * the case cannot tell "read" from "ignored" */
	unsigned char const full[] = {
	    (unsigned char)(0x03 | (2 << 2) | ((t & 0x07) << 5)),
	    (unsigned char)((t >> 3) & 0xFF),
	    (unsigned char)((t >> 11) & 0xFF),
	    (unsigned char)((t >> 19) & 0xFF)
	};
	for (i = 1 ; i < 4 ; ++i) {
	    errmsg_count = 0;
	    setsolution(&game, full, i, 0, 0, 0);
	    CHECK_MSG(expandsolution(&sol, &game) == FALSE,
		      "a format-2 move truncated to %d byte(s) was accepted", i);
	    CHECK_MSG(errmsg_count == 1,
		      "truncation at %d byte(s) produced %d diagnostics", i, errmsg_count);
	}
    }

    tw_case("a five-byte move truncated at every length is reported");
    {
	unsigned char const full[] = {
	    (unsigned char)(0x03 | (3 << 2) | 0x10 | (1 << 5)),  /* NN=3: five bytes */
	    0x00, 0x00, 0x00, 0x00
	};
	for (i = 1 ; i < 5 ; ++i) {
	    errmsg_count = 0;
	    setsolution(&game, full, i, 0, 0, 0);
	    CHECK_MSG(expandsolution(&sol, &game) == FALSE,
		      "a format-4 move truncated to %d byte(s) was accepted", i);
	}
    }

    /* ================================================================== *
     * Round trips.
     * ================================================================== */

    tw_case("contract then expand preserves a simple orthogonal solution");
    {
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	sol.rndseed = 0x12345678UL;
	sol.flags = 0;
	sol.rndslidedir = NORTH;
	sol.stepping = 0;
	addmove(&sol, NORTH, 0);
	addmove(&sol, EAST, 4);
	addmove(&sol, SOUTH, 8);
	addmove(&sol, WEST, 12);

	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_MSG(game.solutiondata != NULL, "contractsolution produced no data");
	CHECK_MSG(game.solutionsize > 16, "contractsolution produced %d bytes", game.solutionsize);

	CHECK_INT(expandsolution(&back, &game), TRUE);
	CHECK_INT(back.moves.count, sol.moves.count);
	for (i = 0 ; i < sol.moves.count && i < back.moves.count ; ++i) {
	    CHECK_INT(back.moves.list[i].dir, sol.moves.list[i].dir);
	    CHECK_INT(back.moves.list[i].when, sol.moves.list[i].when);
	}
	CHECK_INT(back.rndseed, (long)sol.rndseed);
	destroymovelist(&sol.moves);
	destroymovelist(&back.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    tw_case("every gap width round-trips: 1, 8, 9, 2048, 2049, and a huge one");
    {
	/* These straddle the boundaries the encoder switches on -- 1 << 3 and
	 * 1 << 11 -- so each choice of encoding is exercised in both
	 * directions. An off-by-one at a boundary is the classic defect here
	 * and would corrupt only some solutions, which is worse than all. */
	static int const gaps[] = { 1, 7, 8, 9, 100, 2047, 2048, 2049, 100000 };
	int when = 0;
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	sol.rndseed = 99;
	sol.flags = 0;
	sol.rndslidedir = NORTH;
	sol.stepping = 3;
	addmove(&sol, NORTH, 0);
	for (i = 0 ; i < (int)(sizeof gaps / sizeof *gaps) ; ++i) {
	    when += gaps[i];
	    addmove(&sol, (i & 1) ? EAST : WEST, when);
	}
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_INT(expandsolution(&back, &game), TRUE);
	CHECK_INT(back.moves.count, sol.moves.count);
	for (i = 0 ; i < sol.moves.count && i < back.moves.count ; ++i) {
	    CHECK_MSG(back.moves.list[i].when == sol.moves.list[i].when,
		      "move %d: wrote when=%d, read back %d", i,
		      sol.moves.list[i].when, back.moves.list[i].when);
	    CHECK_INT(back.moves.list[i].dir, sol.moves.list[i].dir);
	}
	CHECK_INT(back.stepping, 3);
	destroymovelist(&sol.moves);
	destroymovelist(&back.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    tw_case("Lynx diagonals survive the round trip");
    {
	/* Diagonal moves are not orthogonal, so the encoder must reach for the
	 * five-byte format. If it ever took a shortcut and used format 2, the
	 * two direction bits would silently discard half of each diagonal --
	 * and a diagonal is a block slap (docs/adr/0008), so those solutions
	 * would stop replaying. */
	static int const dirs[] = {
	    NORTH | WEST, SOUTH | WEST, NORTH | EAST, SOUTH | EAST
	};
	int when;
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	sol.rndseed = 5;
	sol.flags = 0;
	sol.rndslidedir = NORTH;
	sol.stepping = 0;
	addmove(&sol, NORTH, 0);
	for (i = 0 ; i < 4 ; ++i)
	    addmove(&sol, dirs[i], 10 + i * 6);
	/* 🔴 AND FOUR MORE WITH A GAP PAST 2048.
	 *
	 * contractsolution() reaches for the five-byte format only when
	 * `isdiagonal(dir) && delta >= (1 << 11)` (solution.c:409). Every gap
	 * above is under twenty, so the small diagonals go through the ordinary
	 * 1-/2-byte form with a 3-bit direction index and NEVER TOUCH the branch
	 * this case was written to protect -- deleting that clause entirely left
	 * all 16 cases passing. Measured.
	 *
	 * With the clause gone, such a move encodes as `0x03 | (index << 2)` with
	 * an index of 4-7, which sets bit 4 -- the format-4 discriminator -- and
	 * the decoder then reads pure garbage. That corrupts exactly the
	 * block-slap solutions docs/adr/0008 is about. The boundary values 2047
	 * and 2048 are included because that is where an off-by-one would live. */
	when = 2000;
	addmove(&sol, NORTH | WEST, when += 2047);
	addmove(&sol, SOUTH | EAST, when += 2048);
	addmove(&sol, SOUTH | WEST, when += 3000);
	addmove(&sol, NORTH | EAST, when += 100000);
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_INT(expandsolution(&back, &game), TRUE);
	CHECK_INT(back.moves.count, sol.moves.count);
	for (i = 0 ; i < sol.moves.count && i < back.moves.count ; ++i) {
	    CHECK_MSG(back.moves.list[i].dir == sol.moves.list[i].dir,
		      "move %d: wrote dir=%d, read back %d", i,
		      sol.moves.list[i].dir, back.moves.list[i].dir);
	    CHECK_INT(back.moves.list[i].when, sol.moves.list[i].when);
	}
	destroymovelist(&sol.moves);
	destroymovelist(&back.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    tw_case("the null solution contracts to nothing, and is not an error");
    {
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_INT(game.solutionsize, 0);
	CHECK_MSG(game.solutiondata == NULL, "the null solution allocated data");
	destroymovelist(&sol.moves);
    }

    tw_case("a long solution round-trips move for move");
    {
	int when = 0;
	memset(&game, 0, sizeof game);
	initmovelist(&sol.moves);
	sol.rndseed = 0x7FFFFFFFUL;
	sol.flags = 0;
	sol.rndslidedir = EAST;
	sol.stepping = 7;
	addmove(&sol, NORTH, 0);
	for (i = 1 ; i < 500 ; ++i) {
	    /* An irregular gap pattern, so that the encoder is forced to change
	     * its mind about the format repeatedly rather than settling on one. */
	    when += 1 + ((i * 37) % 3000);
	    addmove(&sol, ((i % 4) == 0) ? NORTH : ((i % 4) == 1) ? WEST
		    : ((i % 4) == 2) ? SOUTH : EAST, when);
	}
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	CHECK_INT(expandsolution(&back, &game), TRUE);
	CHECK_INT(back.moves.count, sol.moves.count);
	for (i = 0 ; i < sol.moves.count && i < back.moves.count ; ++i) {
	    CHECK_INT(back.moves.list[i].dir, sol.moves.list[i].dir);
	    CHECK_INT(back.moves.list[i].when, sol.moves.list[i].when);
	}
	CHECK_INT(back.rndseed, (long)sol.rndseed);
	CHECK_INT(back.stepping, 7);
	CHECK_INT(back.rndslidedir, EAST);
	destroymovelist(&sol.moves);
	destroymovelist(&back.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    tw_case("the level number, password and time are written where they belong");
    {
	/* 🔴 THESE BYTES ARE WRITE-ONLY as far as expandsolution() is concerned,
	 * so NO round trip can see them -- and both were wrong in a mutation that
	 * the whole suite passed. They matter anyway: readsolution() and
	 * savesolutions() use the number and password to match a .tws record to a
	 * level, and besttime is the recorded score. Reversing the password bytes,
	 * or writing garbage into byte 12, left 1,134 checks green.
	 *
	 * The only way to cover a write with no matching read is to assert the
	 * raw bytes, against the offsets in solution.c's format comment. */
	memset(&game, 0, sizeof game);
	game.number = 0x1234;
	strcpy(game.passwd, "WXYZ");
	game.besttime = 0x00ABCDEF;
	initmovelist(&sol.moves);
	sol.rndseed = 0;
	sol.flags = 0;
	sol.rndslidedir = NORTH;
	sol.stepping = 0;
	addmove(&sol, NORTH, 0);
	addmove(&sol, EAST, 4);
	CHECK_INT(contractsolution(&sol, &game), TRUE);
	if (game.solutiondata && game.solutionsize > 16) {
	    CHECK_INT(game.solutiondata[0], 0x34);
	    CHECK_INT(game.solutiondata[1], 0x12);
	    CHECK_INT(game.solutiondata[2], 'W');
	    CHECK_INT(game.solutiondata[3], 'X');
	    CHECK_INT(game.solutiondata[4], 'Y');
	    CHECK_INT(game.solutiondata[5], 'Z');
	    CHECK_INT(game.solutiondata[12], 0xEF);
	    CHECK_INT(game.solutiondata[13], 0xCD);
	    CHECK_INT(game.solutiondata[14], 0xAB);
	    CHECK_INT(game.solutiondata[15], 0x00);
	} else {
	    CHECK_MSG(0, "contractsolution produced no usable data");
	}
	destroymovelist(&sol.moves);
	free(game.solutiondata);
	game.solutiondata = NULL;
    }

    tw_case("a .tws cannot write past the buffer it was handed (jc-44)");
    {
	/* 🔴 THE jc-44 SECURITY FIX.
	 *
	 * loadsolutionsetname() read a 32-bit record length straight out of the
	 * file and fread() that many bytes into the caller's buffer, unbounded
	 * and unterminated. Its one caller passes a 256-byte array on the STACK,
	 * so a .tws declaring a 400-byte set name smashed it -- measured against
	 * the shipped jc-43 binary as a segmentation fault, with the overwriting
	 * bytes coming from the file.
	 *
	 * Reachable by dragging a .tws onto the executable, which is a documented
	 * workflow and how solution files get opened in practice.
	 *
	 * The buffer here is deliberately SMALL and fenced with a canary, so that
	 * a regression overruns the canary rather than something that happens to
	 * be harmless. Upstream's, not this fork's -- git blame puts it in the
	 * 2.3.1 import.
	 */
	struct { char buf[32]; unsigned char canary[32]; } fenced;
	char const *path = "tw_setname_test.tws";
	FILE *f;
	int payload = 400;      /* far past the 32 bytes offered below */
	int got, k, intact;

	memset(&fenced, 0, sizeof fenced);
	memset(fenced.canary, 0xA5, sizeof fenced.canary);

	f = fopen(path, "wb");
	if (!f) {
	    tw_skip("could not create a temporary .tws in the working directory");
	} else {
	    /* Header: signature, then a 32-bit word whose HIGH byte is the count
	     * of extra header bytes (zero here). */
	    unsigned char hdr[8] = { 0x35, 0x33, 0x9B, 0x99, 0x00, 0x00, 0x00, 0x00 };
	    unsigned char rec[16];
	    fwrite(hdr, 1, 8, f);
	    /* A set-name record: a 32-bit length of payload+16, then a level
	     * number and password of zero (which is what marks it as a set name
	     * rather than a solution), then ten skipped bytes, then the payload. */
	    rec[0] = (unsigned char)((payload + 16) & 0xFF);
	    rec[1] = (unsigned char)(((payload + 16) >> 8) & 0xFF);
	    rec[2] = 0; rec[3] = 0;
	    fwrite(rec, 1, 4, f);
	    memset(rec, 0, sizeof rec);
	    fwrite(rec, 1, 2, f);     /* level number 0 */
	    fwrite(rec, 1, 4, f);     /* password 0 */
	    fwrite(rec, 1, 10, f);    /* the ten skipped bytes */
	    for (k = 0 ; k < payload ; ++k)
		fputc('A', f);
	    fclose(f);

	    got = loadsolutionsetname(path, fenced.buf, (int)sizeof fenced.buf);

	    CHECK_MSG(got <= (int)sizeof fenced.buf - 1,
		      "loadsolutionsetname returned %d for a %d-byte set name into a"
		      " %d-byte buffer; it must clamp", got, payload, (int)sizeof fenced.buf);
	    intact = 1;
	    for (k = 0 ; k < (int)sizeof fenced.canary ; ++k)
		if (fenced.canary[k] != 0xA5)
		    intact = 0;
	    CHECK_MSG(intact,
		      "the bytes AFTER the buffer were overwritten -- this is the"
		      " stack smash jc-44 exists to fix");
	    CHECK_MSG(fenced.buf[sizeof fenced.buf - 1] == '\0',
		      "the buffer was not NUL-terminated; the caller strcpy()s out of it");
	    remove(path);
	}
    }

    tw_case("a .tws with a set name that fits is still read normally");
    {
	/* The other half: a hardening change that also breaks legitimate files
	 * is not a fix. A short set name must still come back intact. */
	struct { char buf[256]; unsigned char canary[16]; } fenced;
	char const *path = "tw_setname_ok.tws";
	char const *setname = "CCLP3";
	FILE *f;
	int got;

	memset(&fenced, 0, sizeof fenced);
	memset(fenced.canary, 0x5A, sizeof fenced.canary);

	f = fopen(path, "wb");
	if (!f) {
	    tw_skip("could not create a temporary .tws in the working directory");
	} else {
	    unsigned char hdr[8] = { 0x35, 0x33, 0x9B, 0x99, 0x00, 0x00, 0x00, 0x00 };
	    unsigned char zero[16];
	    int len = (int)strlen(setname) + 1;
	    fwrite(hdr, 1, 8, f);
	    memset(zero, 0, sizeof zero);
	    zero[0] = (unsigned char)((len + 16) & 0xFF);
	    zero[1] = (unsigned char)(((len + 16) >> 8) & 0xFF);
	    fwrite(zero, 1, 4, f);
	    memset(zero, 0, sizeof zero);
	    fwrite(zero, 1, 16, f);   /* number, password, and the skipped bytes */
	    fwrite(setname, 1, (size_t)len, f);
	    fclose(f);

	    got = loadsolutionsetname(path, fenced.buf, (int)sizeof fenced.buf);
	    CHECK_MSG(got == len, "expected a set name of %d bytes, got %d", len, got);
	    CHECK_STR(fenced.buf, setname);
	    CHECK_MSG(fenced.canary[0] == 0x5A, "a short set name still overran the buffer");
	    remove(path);
	}
    }

    tw_case("a zero-length or absurd buffer size is refused rather than trusted");
    {
	char tiny[4];
	CHECK_MSG(loadsolutionsetname("does-not-exist.tws", tiny, 0) < 0,
		  "a buffer size of zero was accepted");
	CHECK_MSG(loadsolutionsetname("does-not-exist.tws", tiny, -1) < 0,
		  "a negative buffer size was accepted");
    }

    tw_case("the buffer is terminated even when the read FAILS");
    {
	/* solution.h promises the result is always terminated. Before this was
	 * pinned, that held only on the successful path: fileread() is
	 * fread(data, size, 1, fp), which on a TRUNCATED file returns failure
	 * having already deposited up to size-1 bytes of the file into the
	 * caller's buffer -- and the function then returned -1 with those bytes
	 * unterminated. Today's only caller checks the return value first, so
	 * nothing was exploitable; the next caller is who this protects.
	 *
	 * The file below declares a 200-byte set name and then stops early. */
	char const *path = "tw_setname_trunc.tws";
	struct { char buf[64]; unsigned char canary[16]; } fenced;
	FILE *f;
	int got, k, intact;

	memset(&fenced, 0, sizeof fenced);
	memset(fenced.buf, 'Q', sizeof fenced.buf);      /* no terminator anywhere */
	memset(fenced.canary, 0x7E, sizeof fenced.canary);

	f = fopen(path, "wb");
	if (!f) {
	    tw_skip("could not create a temporary .tws");
	} else {
	    unsigned char hdr[8] = { 0x35, 0x33, 0x9B, 0x99, 0x00, 0x00, 0x00, 0x00 };
	    unsigned char zero[16];
	    fwrite(hdr, 1, 8, f);
	    memset(zero, 0, sizeof zero);
	    zero[0] = (unsigned char)((200 + 16) & 0xFF);
	    zero[1] = (unsigned char)(((200 + 16) >> 8) & 0xFF);
	    fwrite(zero, 1, 4, f);
	    memset(zero, 0, sizeof zero);
	    fwrite(zero, 1, 16, f);
	    for (k = 0 ; k < 5 ; ++k)      /* five bytes, not two hundred */
		fputc('Q', f);
	    fclose(f);

	    got = loadsolutionsetname(path, fenced.buf, (int)sizeof fenced.buf);
	    CHECK_MSG(got <= 0, "a truncated set-name record reported success (%d)", got);
	    CHECK_MSG(fenced.buf[0] == '\0',
		      "the buffer was left unterminated after a failed read");
	    intact = 1;
	    for (k = 0 ; k < (int)sizeof fenced.canary ; ++k)
		if (fenced.canary[k] != 0x7E)
		    intact = 0;
	    CHECK_MSG(intact, "a truncated record still wrote past the buffer");
	    remove(path);
	}
    }

    tw_case("the clamp holds at buffer sizes of exactly 1, 2 and 3");
    {
	/* The boundary arithmetic is the riskiest line in the jc-44 fix:
	 * `size` is clamped to `buffersize - 1` and then `buffer[size]` is
	 * written, so a buffer of ONE byte must end up writing only buffer[0],
	 * and a buffer of two must write at most buffer[0..1]. An off-by-one
	 * here is a one-byte overflow, which is the kind that survives casual
	 * testing and shows up as corruption somewhere unrelated.
	 *
	 * Each size is exercised against a file whose declared set name is far
	 * longer than the buffer, with a canary immediately after it. */
	char const *path = "tw_setname_edge.tws";
	int bufsize;
	for (bufsize = 1 ; bufsize <= 3 ; ++bufsize) {
	    struct { char buf[3]; unsigned char canary[16]; } fenced;
	    FILE *f;
	    int got, k, intact;
	    int payload = 200;

	    memset(&fenced, 0, sizeof fenced);
	    memset(fenced.canary, 0xC3, sizeof fenced.canary);

	    f = fopen(path, "wb");
	    if (!f) { tw_skip("could not create a temporary .tws"); break; }
	    {
		unsigned char hdr[8] = { 0x35, 0x33, 0x9B, 0x99, 0x00, 0x00, 0x00, 0x00 };
		unsigned char zero[16];
		fwrite(hdr, 1, 8, f);
		memset(zero, 0, sizeof zero);
		zero[0] = (unsigned char)((payload + 16) & 0xFF);
		zero[1] = (unsigned char)(((payload + 16) >> 8) & 0xFF);
		fwrite(zero, 1, 4, f);
		memset(zero, 0, sizeof zero);
		fwrite(zero, 1, 16, f);
		for (k = 0 ; k < payload ; ++k)
		    fputc('Z', f);
	    }
	    fclose(f);

	    got = loadsolutionsetname(path, fenced.buf, bufsize);
	    CHECK_MSG(got <= bufsize - 1,
		      "with a %d-byte buffer the call returned %d; at most %d is writable",
		      bufsize, got, bufsize - 1);
	    intact = 1;
	    for (k = 0 ; k < (int)sizeof fenced.canary ; ++k)
		if (fenced.canary[k] != 0xC3)
		    intact = 0;
	    /* buf[] is three bytes; for bufsize 1 and 2 the bytes above the
	     * declared size must also be untouched. */
	    for (k = bufsize ; k < 3 ; ++k)
		if (fenced.buf[k] != '\0')
		    intact = 0;
	    CHECK_MSG(intact,
		      "a %d-byte buffer was written past its end", bufsize);
	    remove(path);
	}
    }

    tw_case("the direction index tables are inverses of each other");
    {
	/* diridx8 and idxdir8 are the only translation between the three-bit
	 * stored form and the four-bit internal one. If they ever disagree,
	 * every solution decodes to the wrong directions. */
	for (i = 0 ; i < 8 ; ++i)
	    CHECK_INT(diridx8[idxdir8[i]], i);
    }

    return tw_end();
}
