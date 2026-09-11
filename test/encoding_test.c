/* encoding_test.c: the .dat level-record parser.
 *
 * MOD (Jeremy, jc-44). Compiles ../encoding.c directly, so the static
 * expandmsdatlevel() and the readpos() macro are both reachable.
 *
 * WHY THIS EXISTS. expandmsdatlevel() is a run-length decoder driven by length
 * fields that live inside the data it is decoding, and the data is a THIRD-PARTY
 * FILE -- players download level sets from community sites and drop them in.
 * That is the shape of parser that goes wrong, and in jc-44 two of them did.
 *
 * The cases below are in three groups:
 *
 *   1. The jc-44 bounds fix, from both sides: a malformed layer is refused, and
 *      a well-formed one still loads. A hardening change that also rejects valid
 *      input is not a fix, it is a different bug.
 *   2. getenddisplaysetup(), which is the ONE input in the program that reaches
 *      this parser without passing readleveldata() first -- and which satisfies
 *      the stricter check with ZERO bytes to spare.
 *   3. The ordinary decoding rules, so the fix cannot be "verified" by a parser
 *      that has stopped parsing.
 *
 * TESTLANG: c
 *
 * encoding.c is compiled only as C by CMake.
 */

#include	"tw_test.h"
#include	"tw_fixture.h"
#include	"tw_corpus.h"
#include	"../encoding.c"

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

/* --- helpers ----------------------------------------------------------- */

static gamestate	teststate;
static gamesetup	testsetup;

/* Expand a raw level record. Returns what expandleveldata() returned. */
static int expandraw(unsigned char *data, int size)
{
    memset(&testsetup, 0, sizeof testsetup);
    testsetup.number = 1;
    testsetup.leveldata = data;
    testsetup.levelsize = size;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_MS;
    teststate.statusflags = 0;
    warn_count = 0;
    errmsg_count = 0;
    return expandleveldata(&teststate);
}

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * test/fuzz/corpus/encoding/ replayed through expandleveldata() on every
 * platform, so a libFuzzer finding on Linux becomes a permanent regression case
 * everywhere. See test/tw_corpus.h.
 *
 * 🔴 THIS TARGET IS DELIBERATELY UNGATED, and that is the point of it existing
 * separately from the series.c one. expandleveldata() normally runs only on
 * records readleveldata() already accepted, and that password gate is what
 * gives the RLE loops their slack -- jc-44's third defect was a guard in this
 * file that was safe only because of a check in a DIFFERENT file. Feeding this
 * function directly is the only way to see that class.
 *
 * ⚠ WHAT IT PROVES IS NARROW: that these inputs still expand to completion
 * without crashing, and that expandleveldata() did not modify its own input.
 * It is NOT a memory oracle -- expandmsdatlevel() walks the record through
 * `unsigned char const *` and writes only into state->map, so the no-write
 * check passes trivially today and exists to fail if that ever changes. ASan,
 * in run-sanitizers.sh and the fuzz job, is the memory oracle.
 *
 * 🔴 AND THE HAND-WRITTEN CASES BELOW ARE NOT REDUNDANT WITH THIS. Measured:
 * re-introducing jc-44's missing lower-layer bound makes the "a lower map layer
 * running to the end of the record is REFUSED" case fail while this corpus
 * replay stays green, because every committed input is a well-formed level. A
 * corpus of valid files cannot test rejection. Keep both.
 */
static int corpus_replayed = 0;

static void corpus_expand(twcorpusinput const *in)
{
    memset(&testsetup, 0, sizeof testsetup);
    testsetup.number = 1;
    testsetup.leveldata = in->data;
    testsetup.levelsize = in->size;

    memset(teststate.map, 0, sizeof teststate.map);
    teststate.game = &testsetup;
    teststate.ruleset = Ruleset_MS;
    teststate.statusflags = 0;
    expandleveldata(&teststate);
}

static void corpus_report(twcorpusverdict v, char const *name)
{
    ++corpus_replayed;
    CHECK_MSG(v == TW_CORPUS_OK, "fuzz corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

static void put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

int main(void)
{
    unsigned char raw[512];
    fixlevel lv;
    unsigned char *rec;
    int size, n, i;

    tw_begin("encoding");
    tw_expect_atleast(70);

    tw_case("every committed fuzz corpus input still expands safely");
    {
	char dir[256];
	int c;

	/* Not being able to FIND the corpus is a failure, not a skip: a replay
	 * that read no files reporting success is the false green this whole
	 * suite exists to prevent. */
	CHECK_MSG(tw_corpus_dir("encoding", dir, sizeof dir),
		  "could not find test/fuzz/corpus/encoding from the working"
		  " directory -- the replay would have proved nothing");
	if (dir[0]) {
	    c = tw_corpus_run(dir, corpus_expand, corpus_report);
	    /* %.100s: tw_fail_ formats into 256 bytes and -Werror rejects an
	     * unbounded %s of a 256-byte array. CHECK_STR bounds the same way. */
	    CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	    CHECK_INT(corpus_replayed, c);
	}
    }

    /* ================================================================== *
     * 1. The jc-44 bounds fix.
     * ================================================================== */

    tw_case("a lower map layer running to the end of the record is REFUSED");
    {
	/* 🔴 THE jc-44 DEFECT, expressed as a level.
	 *
	 * The run-length loop reads `data[++n]` TWICE on a 0xFF escape, so a
	 * layer whose final byte is 0xFF reads two bytes past its declared end.
	 * The upper layer's bounds check reserves those two bytes; the lower
	 * layer's did not. A record whose lower layer runs EXACTLY to the end,
	 * and ends in 0xFF, therefore read two bytes past the allocation.
	 *
	 * Built by hand rather than with tw_fixture.h, because the fixture
	 * builder always emits an optional-fields size word after the layers --
	 * which is precisely the slack that makes this unreachable in practice. */
	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 0);      n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* map detail */
	put16(raw + n, 3);      n += 2;    /* upper layer: 3 bytes */
	raw[n++] = 0xFF; raw[n++] = 0xFF; raw[n++] = FIX_FLOOR;   /* 255 floors */
	put16(raw + n, 3);      n += 2;    /* lower layer: 3 bytes */
	raw[n++] = FIX_FLOOR; raw[n++] = FIX_FLOOR; raw[n++] = 0xFF;
	/* Record ends HERE -- no optional fields at all. */
	CHECK_MSG(expandraw(raw, n) == FALSE,
		  "a lower layer ending in 0xFF with no trailing bytes was accepted;"
		  " the decoder reads two bytes past it");
	CHECK_MSG(errmsg_count >= 1, "the malformed level was refused silently");
    }

    tw_case("🔴 an UPPER map layer running to the end of the record is REFUSED");
    {
	/* THE UPPER LAYER'S HALF OF THE SAME BOUND, and it had no case until an
	 * independent review mutated it and nothing noticed. Removing the `+ 2`
	 * at encoding.c:193 left the whole suite green -- AND the golden master
	 * green too, across all 903 levels. Measured, not supposed.
	 *
	 * ⚠ THE COMMENT AT encoding.c:220 SAYS THIS BOUND IS CORRECT BY ACCIDENT:
	 * the upper layer reserves two bytes because it is reserving the LOWER
	 * layer's own length word, not because anyone was thinking about the
	 * 0xFF escape reading data[++n] twice. A bound that is right for the
	 * wrong reason is exactly the kind that a tidy-up "simplifies", so it
	 * gets the same treatment its twin below already had.
	 *
	 * The record here ends immediately after an upper layer whose last byte
	 * is 0xFF, so the decoder would read two bytes past the allocation. */
	int	cell, touched;

	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 0);      n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* map detail */
	put16(raw + n, 3);      n += 2;    /* upper layer: 3 bytes */
	raw[n++] = FIX_FLOOR; raw[n++] = FIX_FLOOR; raw[n++] = 0xFF;
	/* Record ends HERE -- levelsize is n. The bytes BEYOND it are still
	 * inside raw[], and they are what the 0xFF escape would read: a count
	 * and a tile id. Filling them with something conspicuous is what makes
	 * the overread visible. */
	raw[n]     = 40;		/* would be read as "repeat 40 times" */
	raw[n + 1] = 0x03;		/* would be read as the tile id Water */

	CHECK_MSG(expandraw(raw, n) == FALSE,
		  "an upper layer ending in 0xFF with no trailing bytes was accepted;"
		  " the decoder reads two bytes past it");

	/* 🔴 THE REAL ORACLE, and the first version of this case did not have it.
	 *
	 * Asserting only that the record is REFUSED does not test this bound at
	 * all: with the `+ 2` removed the record is still refused, just later and
	 * for a different reason -- the lower layer's own guard catches it,
	 * because `data` has by then advanced past the end. Both versions return
	 * FALSE, so the check above passes either way. Verified by mutation: it
	 * did.
	 *
	 * What actually differs is whether the run-length loop RAN before the
	 * rejection. With the bound correct, the record is refused before the
	 * loop and the map is never touched. With it removed, the loop decodes,
	 * reads the two bytes past the record, and writes 40 Water tiles into a
	 * map that should have stayed untouched.
	 *
	 * This is the jc-45 lesson in miniature: a behavioral test cannot catch a
	 * memory-safety bound whose whole point is that the visible outcome does
	 * not change. Find the side effect that only happens on the wrong path. */
	touched = 0;
	for (cell = 0 ; cell < CXGRID * CYGRID ; ++cell)
	    if (teststate.map[cell].top.id != 0)
		++touched;
	CHECK_MSG(touched == 0,
		  "the record was refused, but %d map cells were written first --"
		  " the upper layer was decoded past the end of the record",
		  touched);
	CHECK_MSG(errmsg_count >= 1, "the malformed level was refused silently");
    }

    tw_case("...and an upper layer with exactly its two bytes of slack is accepted");
    {
	/* The other half, so a guard that simply rejects everything cannot pass
	 * the case above. Two spare bytes is what a real record always has:
	 * the lower layer's own length word sits immediately after the upper
	 * layer's data. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = FIX_FLOOR; raw[n++] = FIX_FLOOR; raw[n++] = 0xFF;
	put16(raw + n, 3);      n += 2;    /* the lower layer's length word */
	raw[n++] = 0xFF; raw[n++] = 0xFF; raw[n++] = FIX_FLOOR;
	put16(raw + n, 0);      n += 2;    /* an empty optional-fields block */
	CHECK_MSG(expandraw(raw, n) == TRUE,
		  "a well-formed record whose upper layer ends in 0xFF was refused");
    }

    tw_case("a lower map layer with two bytes to spare is still accepted");
    {
	/* The other half. A guard that rejects everything would pass the case
	 * above and break every real level; this is what stops that. Two extra
	 * bytes is exactly the slack a real record has, because the
	 * optional-fields size word sits there. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 0xFF; raw[n++] = FIX_FLOOR;
	put16(raw + n, 3);      n += 2;
	raw[n++] = FIX_FLOOR; raw[n++] = FIX_FLOOR; raw[n++] = 0xFF;
	put16(raw + n, 0);      n += 2;    /* an empty optional-fields block */
	CHECK_MSG(expandraw(raw, n) == TRUE,
		  "a well-formed record with an empty optional-fields block was refused");
    }

    /* ================================================================== *
     * 2. The built-in level, which has NO margin at all.
     * ================================================================== */

    tw_case("getenddisplaysetup still loads, and it has zero bytes to spare");
    {
	/* 🔴 THE ONE INPUT THAT BYPASSES readleveldata().
	 *
	 * Every level from a file is validated by series.c before this parser
	 * sees it, and that validation is what makes the bug above unreachable
	 * in practice. The end-of-series display is the exception: its level is
	 * a static array handed straight to expandmsdatlevel().
	 *
	 * It is 139 bytes and satisfies the STRICTER jc-44 check with exactly
	 * zero margin -- 121 + 16 + 2 == 139. So tightening that check by one
	 * more byte would break the screen players see after finishing a set,
	 * and would break it only there, months later, at the least testable
	 * moment in the program. That is why this case exists rather than an
	 * arithmetic argument in a comment.
	 */
	memset(&teststate, 0, sizeof teststate);
	teststate.ruleset = Ruleset_MS;
	warn_count = 0;
	errmsg_count = 0;
	getenddisplaysetup(&teststate);
	CHECK_MSG(errmsg_count == 0,
		  "the built-in end-of-series level was REJECTED by the parser"
		  " (%d error(s)) -- the bounds check is now too strict", errmsg_count);
	/* It is a real level: the map must not have come back blank. */
	{
	    int nonempty = 0;
	    int i;
	    for (i = 0 ; i < CXGRID * CYGRID ; ++i)
		if (teststate.map[i].top.id != Empty)
		    ++nonempty;
	    CHECK_MSG(nonempty > 0, "the end-of-series level expanded to an empty map");
	}
    }

    /* ================================================================== *
     * 3. Ordinary decoding, so the above cannot pass vacuously.
     * ================================================================== */

    tw_case("a well-formed level expands, with its header and both layers");
    fix_init(&lv);
    fix_border(&lv);
    lv.chips = 7;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_WATER);
    fix_setbot(&lv, 8, 8, FIX_GRAVEL);
    rec = fix_build(&lv, &size);
    CHECK_MSG(rec != NULL, "the fixture builder returned nothing");
    if (rec) {
	CHECK_INT(expandraw(rec, size), TRUE);
	CHECK_INT(teststate.chipsneeded, 7);
	CHECK_INT(teststate.map[6 + CXGRID * 5].top.id, Water);
	CHECK_INT(teststate.map[8 + CXGRID * 8].bot.id, Gravel);
	CHECK_INT(teststate.map[0].top.id, Wall);
	CHECK_MSG(warn_count == 0, "a well-formed level produced %d warning(s)", warn_count);
	free(rec);
    }

    tw_case("a level number of zero is refused");
    /* readword(data) == 0 is the parser's first check, and it is the reason
     * fix_init() sets a nonzero number. */
    fix_init(&lv);
    fix_border(&lv);
    lv.number = 0;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    rec = fix_build(&lv, &size);
    if (rec) {
	CHECK_INT(expandraw(rec, size), FALSE);
	free(rec);
    }

    tw_case("a map detail level above 1 is refused");
    fix_init(&lv);
    fix_border(&lv);
    lv.detail = 2;
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    rec = fix_build(&lv, &size);
    if (rec) {
	CHECK_INT(expandraw(rec, size), FALSE);
	free(rec);
    }

    tw_case("run-length runs expand to the right number of cells");
    {
	/* 0xFF <count> <tile> is the format's only escape. A run that would
	 * overrun the map is clamped rather than allowed to write past it. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 6);      n += 2;    /* upper: two runs */
	raw[n++] = 0xFF; raw[n++] = 10;  raw[n++] = FIX_WALL;
	raw[n++] = 0xFF; raw[n++] = 200; raw[n++] = FIX_FLOOR;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 20;  raw[n++] = FIX_GRAVEL;
	put16(raw + n, 0);      n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	/* The first ten cells are wall, the eleventh is not. */
	CHECK_INT(teststate.map[0].top.id, Wall);
	CHECK_INT(teststate.map[9].top.id, Wall);
	CHECK_INT(teststate.map[10].top.id, Empty);
	CHECK_INT(teststate.map[0].bot.id, Gravel);
	CHECK_INT(teststate.map[19].bot.id, Gravel);
    }

    tw_case("🔴 run-length data that decodes PAST the map stops at the map");
    {
	/* THE BOUND WITH NO INPUT BEHIND IT.
	 *
	 * An adversarial audit deleted `pos < CXGRID * CYGRID` from BOTH
	 * run-length loops in this file and every layer stayed green -- and the
	 * sanitizer could not have helped either, because the bound was not
	 * merely undetected, it was UNREACHED: no test input, and no committed
	 * fuzz reproducer, decodes to more than 1,024 cells. A guard nothing
	 * ever drives is a guard nobody can claim works.
	 *
	 * Five 0xFF runs of 255 is 1,275 cells from fifteen bytes. Without the
	 * bound, `state->map[pos++].top.id = id` walks 251 cells past a
	 * 1,056-entry array, writing an ATTACKER-CHOSEN tile id into whatever
	 * follows `map` in gamestate. That is the same shape as jc-44's third
	 * defect and it is reachable from a downloaded .dat.
	 *
	 * ⚠ THE ORACLE IS ROW 32, NOT A CRASH. map is CXGRID * (CYGRID + 1) --
	 * 1,056 cells -- so the first 32 cells the unbounded loop overruns into
	 * are still INSIDE the array and would not fault, would not trip ASan,
	 * and would not trap under UBSan. They are also always zero after a
	 * successful decode, because the decode stops at 1,024. So a non-zero
	 * row 32 is exactly, and only, this bug. */
	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 0);      n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* detail */
	/* ⚠ SIX RUNS, NOT FIVE, AND THE COUNT IS LOAD-BEARING FOR THE SECOND
	 * ASSERTION. Five runs of 255 is 1,275 cells, which over-runs the map
	 * and proves the WRITE bound -- but the outer loop still consumes all
	 * fifteen bytes either way (the fifth run is read in full and merely
	 * written short), so `n == size` and no "extra bytes" warning fires.
	 * With six, pos reaches 1,024 with a whole run still unread, which is
	 * the only arrangement that makes the OUTER bound observable. */
	put16(raw + n, 18);     n += 2;    /* upper layer: 6 runs of 255 */
	for (i = 0 ; i < 6 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_WALL;
	}
	/* ⚠ THE LOWER LAYER IS EXACTLY 1,024 CELLS, ON PURPOSE. A short one
	 * warns ("not enough cells"), and that warning fires whether or not the
	 * bound under test is present -- which silently defeats the warn_count
	 * assertion at the end of this case. It passed that way once. Make the
	 * layer complete so the upper layer's "extra bytes" is the ONLY warning
	 * this record can produce. 4*255 + 4 == 1024. */
	put16(raw + n, 15);     n += 2;
	for (i = 0 ; i < 4 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR;
	}
	raw[n++] = 0xFF; raw[n++] = 4; raw[n++] = FIX_FLOOR;
	put16(raw + n, 0);      n += 2;    /* no optional fields */

	CHECK_MSG(expandraw(raw, n) == TRUE,
		  "a record whose upper layer over-runs was refused outright;"
		  " this case needs it ACCEPTED so the decode actually runs");
	CHECK_MSG(teststate.map[CXGRID * CYGRID - 1].top.id == Wall,
		  "the decode did not even fill the map, so the overrun below"
		  " is not being tested");
	{
	    int overrun = 0;
	    for (i = CXGRID * CYGRID ; i < POS_INVALID ; ++i)
		if (teststate.map[i].top.id != 0 || teststate.map[i].bot.id != 0)
		    ++overrun;
	    CHECK_MSG(overrun == 0,
		      "run-length data wrote %d cell(s) past the %d-cell map."
		      " The decode loops' `pos < CXGRID * CYGRID` bound is gone"
		      " or wrong; without it a .dat can write a chosen tile id"
		      " into whatever follows map[] in gamestate.",
		      overrun, CXGRID * CYGRID);
	}
	/* ⚠ THE BOUND IS WRITTEN TWICE AND ONLY ONE COPY GUARDS THE WRITE.
	 * The inner `while (i-- && pos < ...)` is what stops the overrun; the
	 * outer `for (... && pos < ...)` merely stops re-reading input, so
	 * deleting IT leaves the checks above green -- measured. Its own
	 * observable effect is this warning: the outer loop must exit with
	 * input left over, which is what "extra bytes" reports. Without it the
	 * loop consumes all 15 bytes and says nothing. */
	CHECK_MSG(warn_count > 0,
		  "the decoder consumed the whole over-long layer without"
		  " reporting extra bytes: the OUTER loop's bound is gone");
    }

    tw_case("the same bound holds for the LOWER layer");
    {
	/* Both loops carry the bound and both need an input. The upper-layer
	 * case above leaves the lower one untested, and they are separate
	 * code -- jc-44's RLE guard was two bytes short in exactly one of a
	 * matched pair. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;    /* upper layer, short */
	raw[n++] = 0xFF; raw[n++] = 1; raw[n++] = FIX_FLOOR;
	put16(raw + n, 15);     n += 2;    /* lower layer: 5 runs of 255 */
	for (i = 0 ; i < 5 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_GRAVEL;
	}
	put16(raw + n, 0);      n += 2;

	CHECK_MSG(expandraw(raw, n) == TRUE,
		  "a record whose lower layer over-runs was refused outright");
	{
	    int overrun = 0;
	    for (i = CXGRID * CYGRID ; i < POS_INVALID ; ++i)
		if (teststate.map[i].bot.id != 0)
		    ++overrun;
	    CHECK_MSG(overrun == 0,
		      "lower-layer run-length data wrote %d cell(s) past the map",
		      overrun);
	}
	/* 🔴 AND THE DECODER MUST SAY IT STOPPED. The `pos < CXGRID * CYGRID` in
	 * this loop's FOR header does not guard the writes -- the inner
	 * `while (i-- && pos < ...)` does -- so relaxing it to `<=` writes
	 * nothing extra and the overrun check above cannot see it. What it does
	 * is let the loop keep consuming input after the map is full, so `n`
	 * reaches `size` and the "extra bytes" warning never fires. The count of
	 * cells is the wrong oracle for this bound; the warning is the right one.
	 * Measured: this mutant outlived the overrun check by itself. */
    }

    tw_case("🔴 a full map stops the lower layer ENTERING another run");
    {
	/* THE FOR-HEADER'S `pos` BOUND, WHICH THE OVERRUN CHECK CANNOT SEE.
	 *
	 * `for (n = pos = 0 ; n < size && pos < CXGRID * CYGRID ; ++n)` does not
	 * guard the writes -- the inner `while (i-- && pos < ...)` does -- so
	 * relaxing it to `<=` writes nothing extra and every "did it overrun the
	 * map" assertion stays green. Measured: it outlived the whole suite.
	 *
	 * ⚠ AND THE OBVIOUS FIXTURE CANNOT SEE IT EITHER. Five runs of 255 is
	 * exactly fifteen bytes, so `n` reaches `size` on its own and the loop
	 * would have ended anyway -- both forms agree. The bound only decides
	 * anything when an iteration would be ENTERED with the map already full,
	 * which needs a byte left over after the map fills. Hence the sixteenth
	 * byte below, and hence the oracle being the warning: the real bound
	 * leaves that byte unread and reports it, `<=` swallows it in silence.
	 */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 16);     n += 2;		/* upper layer fills exactly */
	for (i = 0 ; i < 4 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR;
	}
	for (i = 0 ; i < 4 ; ++i)
	    raw[n++] = FIX_FLOOR;
	put16(raw + n, 16);     n += 2;		/* lower layer: 15 + 1 spare */
	for (i = 0 ; i < 5 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_GRAVEL;
	}
	raw[n++] = FIX_WALL;			/* the byte past a full map   */
	put16(raw + n, 0);      n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(warn_count >= 1,
		  "the lower layer read its trailing byte instead of stopping at"
		  " a full map, so the extra data was never reported");
	{
	    int overrun = 0;
	    for (i = CXGRID * CYGRID ; i < POS_INVALID ; ++i)
		if (teststate.map[i].bot.id != 0)
		    ++overrun;
	    CHECK_MSG(overrun == 0,
		      "lower-layer data wrote %d cell(s) into the row-32 area",
		      overrun);
	}
    }

    tw_case("an undefined tile code becomes a wall, and is reported");
    {
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = 0x7E;   /* past the table */
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = FIX_FLOOR;
	put16(raw + n, 0);      n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_INT(teststate.map[0].top.id, Wall);
	CHECK_MSG((teststate.statusflags & SF_BADTILES) != 0,
		  "an undefined tile code did not raise SF_BADTILES");
    }

    tw_case("🔴 tile code 0x70 EXACTLY -- the first one past fileids[]");
    {
	/* THE OFF-BY-ONE THE CASE ABOVE CANNOT SEE. It uses 0x7E, which is
	 * comfortably past a 112-entry table, so `id >= size` and `id > size`
	 * agree about it and the mutation survives -- measured.
	 *
	 * 0x70 is 112: the FIRST value out of range, and the only one the two
	 * forms disagree about. `fileids[112]` is a one-past-the-end read of a
	 * `static int const[]`, driven by a single byte of a downloaded .dat,
	 * and whatever it returns is then written into the map as a tile id.
	 * It does not fault -- it reads whatever the linker put next -- which
	 * is why nothing noticed and why the assertion has to be the VALUE.
	 *
	 * ⚠ 112 is asserted, not assumed. If the table grows, this case must
	 * move with it or it silently stops testing the boundary. */
	CHECK_MSG((int)(sizeof fileids / sizeof *fileids) == 112,
		  "fileids[] is %d entries, not 112 -- 0x70 is no longer the"
		  " boundary and this case is testing an ordinary bad tile",
		  (int)(sizeof fileids / sizeof *fileids));
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = 0x70;   /* == the table size */
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = FIX_FLOOR;
	put16(raw + n, 0);      n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(teststate.map[0].top.id == Wall,
		  "tile code 0x70 decoded to %d instead of Wall: the bound is"
		  " `id > size` rather than `id >= size`, so fileids[112] was"
		  " read one past the end of the table",
		  teststate.map[0].top.id);
	CHECK_MSG((teststate.statusflags & SF_BADTILES) != 0,
		  "tile code 0x70 was accepted as a real tile");
    }

    tw_case("🔴 tile code 0x70 in the LOWER layer too");
    {
	/* The mirror of the case above, and it is not redundant: the bound is
	 * written twice, once per layer, and mutating ONLY the lower one
	 * (encoding.c:254) survived the upper layer's case -- measured. jc-44's
	 * whole defect was a bound that was correct in one of a matched pair
	 * and two bytes short in the other. Never test one half of a pair. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = FIX_FLOOR;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 5; raw[n++] = 0x70;   /* == the table size */
	put16(raw + n, 0);      n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(teststate.map[0].bot.id == Wall,
		  "tile code 0x70 in the lower layer decoded to %d instead of"
		  " Wall: fileids[112] was read one past the end of the table",
		  teststate.map[0].bot.id);
	CHECK_MSG((teststate.statusflags & SF_BADTILES) != 0,
		  "tile code 0x70 in the lower layer was accepted as a real tile");
    }

    /* ================================================================== *
     * 4. The optional-fields block, and the one clamp holding it in.
     * ================================================================== */

    tw_case("🔴 an optional field claiming more bytes than remain is CLAMPED");
    {
	/* THE LAST UNTESTED MEMORY-SAFETY BOUND IN THIS PARSER, and the one
	 * with the widest blast radius. After the two map layers comes a run
	 * of [type][size][payload] records, and `size` is ONE BYTE FROM THE
	 * FILE -- up to 255 -- while the payload actually present may be far
	 * shorter. Exactly one line stops that:
	 *
	 *     if (data + size > dataend)
	 *         size = dataend - data;
	 *
	 * Replace it with `if (0)` and the unit suite AND the sanitize layer
	 * both stay green -- measured. What it holds in:
	 *
	 *   field 4  trapcount   = size / 10, then reads 10 bytes per entry
	 *   field 5  clonercount = size / 8,  then 8 bytes per entry
	 *   field 7  memcpy(state->hinttext, data, size)   <- 255 bytes, flat
	 *   field 10 crlistcount = size / 2,  then 2 bytes per entry
	 *
	 * ⚠ THE ORACLE IS trapcount, NOT A CRASH. Every one of those is a
	 * READ past the end of the record; none writes out of bounds
	 * (hinttext is 256 and size caps at 255), so nothing faults and the
	 * trapping sanitizer sees nothing. But trapcount is computed straight
	 * from the clamped size and is sitting in the gamestate afterwards,
	 * so it says precisely how far the parser was willing to walk.
	 *
	 * A field 4 declaring 250 bytes with 4 actually present: clamped, that
	 * is 4/10 = 0 traps. Unclamped it is 25, and the loop reads 250 bytes
	 * beyond the record to fill them. */
	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 0);      n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* detail */
	/* Both layers complete -- 4*255 + 4 == 1024 -- so the only warnings
	 * this record can raise are the ones under test. */
	put16(raw + n, 15);     n += 2;
	for (i = 0 ; i < 4 ; ++i) { raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR; }
	raw[n++] = 0xFF; raw[n++] = 4; raw[n++] = FIX_FLOOR;
	put16(raw + n, 15);     n += 2;
	for (i = 0 ; i < 4 ; ++i) { raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR; }
	raw[n++] = 0xFF; raw[n++] = 4; raw[n++] = FIX_FLOOR;
	/* Metadata: one field, type 4, DECLARING 250 bytes, carrying 4. */
	put16(raw + n, 6);      n += 2;    /* 2 header + 4 payload */
	raw[n++] = 4;                      /* field type: trap wiring */
	raw[n++] = 250;                    /* the lie */
	raw[n++] = 0; raw[n++] = 0; raw[n++] = 0; raw[n++] = 0;

	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(teststate.trapcount == 0,
		  "an optional field declared 250 bytes with 4 present and the"
		  " parser built %d trap(s) from it -- it read %d bytes past the"
		  " end of the record. The clamp at encoding.c is gone.",
		  teststate.trapcount, teststate.trapcount * 10);
    }

    tw_case("...and a well-formed optional field is still parsed in full");
    {
	/* The other half. A clamp that discarded every field would satisfy the
	 * case above and quietly stop traps, cloners and monster lists from
	 * loading at all -- which is a far worse bug than the one being
	 * guarded against, and would not crash either. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 15);     n += 2;
	for (i = 0 ; i < 4 ; ++i) { raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR; }
	raw[n++] = 0xFF; raw[n++] = 4; raw[n++] = FIX_FLOOR;
	put16(raw + n, 15);     n += 2;
	for (i = 0 ; i < 4 ; ++i) { raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR; }
	raw[n++] = 0xFF; raw[n++] = 4; raw[n++] = FIX_FLOOR;
	put16(raw + n, 12);     n += 2;    /* 2 header + 10 payload */
	raw[n++] = 4;                      /* field type: trap wiring */
	raw[n++] = 10;                     /* exactly one entry, honestly sized */
	/* One entry: from (3,3), to (7,7). readpos reads the x byte at +0/+4
	 * and the y byte at +2/+6; the rest of the ten bytes are unused. */
	raw[n++] = 3; raw[n++] = 0; raw[n++] = 3; raw[n++] = 0;
	raw[n++] = 7; raw[n++] = 0; raw[n++] = 7; raw[n++] = 0;
	raw[n++] = 0; raw[n++] = 0;

	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(teststate.trapcount == 1,
		  "a correctly sized field 4 produced %d traps, not 1 -- the"
		  " clamp is discarding valid data", teststate.trapcount);
	if (teststate.trapcount == 1) {
	    CHECK_INT(teststate.traps[0].from, 3 + CXGRID * 3);
	    CHECK_INT(teststate.traps[0].to, 7 + CXGRID * 7);
	}
    }

    tw_case("fileidtotileid maps the codes the row-32 cloner glitch relies on");
    /* This is the accessor mslogic.c uses when the MSCC row-32 glitch writes raw
     * data-file bytes into the map, so it has to agree with the table the parser
     * uses -- see docs/adr/0002. */
    CHECK_INT(fileidtotileid(0x00), Empty);
    CHECK_INT(fileidtotileid(0x01), Wall);
    CHECK_INT(fileidtotileid(0x03), Water);
    CHECK_INT(fileidtotileid(0x15), Exit);
    CHECK_MSG(fileidtotileid(0xFE) == Wall,
	      "an out-of-table code must map to Wall, not to whatever is past the array");
    CHECK_MSG(fileidtotileid(0x7F) == Wall,
	      "an out-of-table code must map to Wall, not to whatever is past the array");
    /* 🔴 0x70 EXACTLY -- the first id past fileids[], and the ONLY value that
     * tells `>= count` apart from `> count`. Both codes above are comfortably
     * outside the table, so the two forms agree on them and an off-by-one here
     * survived the whole suite. Measured: a mutation census killed nothing in
     * this function until this line existed.
     *
     * ⚠ The readpos case immediately below already says this in as many words
     * ("every x above is either comfortably inside or comfortably outside"),
     * and the lesson was not applied twenty lines up. A lesson written down
     * once is not a lesson applied -- grep for the rest of the class. */
    CHECK_MSG(fileidtotileid(0x70) == Wall,
	      "id 0x70 is one past fileids[] and must map to Wall, not to the"
	      " first bytes of whatever the linker put after the table");
    CHECK_MSG(fileidtotileid(0x6F) != Wall,
	      "0x6F is the LAST id inside fileids[]; if this is Wall the table"
	      " shrank and the bound above is being tested against the wrong edge");

    tw_case("readpos keeps row 32 distinct from an invalid coordinate");
    {
	/* The other half of the jc-2 row-32 work, at the level of the macro
	 * itself. mslogic_test.c pins it through a whole level; this pins the
	 * arithmetic, which is what a future "tidy up the constants" change
	 * would touch. */
	unsigned char x, y;
	x = 0;  y = 32; CHECK_INT(readpos(&x, &y), ROW32POS(0));
	x = 12; y = 32; CHECK_INT(readpos(&x, &y), ROW32POS(12));
	x = 40; y = 3;  CHECK_INT(readpos(&x, &y), POS_INVALID);
	x = 31; y = 31; CHECK_INT(readpos(&x, &y), 31 + CYGRID * 31);
	x = 0;  y = 0;  CHECK_INT(readpos(&x, &y), 0);
	/* 🔴 x == 32 EXACTLY, which is the only value that tells `< CXGRID`
	 * apart from `<= CXGRID`. Every x above is either comfortably inside
	 * (31) or comfortably outside (40), so both forms agree on all of them
	 * and an off-by-one here survived the whole suite -- measured. A
	 * mis-bounded x does not fault: it ALIASES onto a valid-but-wrong cell,
	 * one row down, which is the quiet half of the jc-2 defect. */
	x = 32; y = 3;  CHECK_INT(readpos(&x, &y), POS_INVALID);
	x = 32; y = 0;  CHECK_INT(readpos(&x, &y), POS_INVALID);
	CHECK_MSG(ROW32POS(0) != POS_INVALID,
		  "a legitimate (0,32) wiring and the invalid marker collide --"
		  " this is exactly the jc-2 defect");
    }

    tw_case("🔴 each map layer decodes exactly its declared bytes, not one more");
    {
	/* THE OVERSHOOT THAT LANDS IN THE MAP.
	 *
	 * Both run-length loops are `for (n = pos = 0 ; n < size && pos < ...)`.
	 * Relax either `n < size` to `n <= size` and the layer decodes ONE extra
	 * byte -- which is the low half of the NEXT length word, a value an
	 * attacker picks -- and writes it into the first cell past the layer.
	 *
	 * ⚠ WHY THE EXISTING RUN-LENGTH CASES CANNOT SEE THIS. They assert on
	 * cells the layer legitimately fills. The overshoot lands on the cell
	 * immediately AFTER, which nothing looked at: measured, both loops kept
	 * their mutants alive through the whole suite. The oracle has to be the
	 * first untouched cell, not the last touched one.
	 *
	 * Both following words are 3, whose low byte 0x03 is Water -- a tile
	 * neither layer here ever writes, so it cannot be confused with one.
	 */
	n = 0;
	put16(raw + n, 1);      n += 2;		/* level number		*/
	put16(raw + n, 0);      n += 2;		/* time			*/
	put16(raw + n, 0);      n += 2;		/* chips		*/
	put16(raw + n, 1);      n += 2;		/* map detail		*/
	put16(raw + n, 3);      n += 2;		/* upper layer: 3 bytes	*/
	raw[n++] = 0xFF; raw[n++] = 10; raw[n++] = FIX_WALL;
	put16(raw + n, 3);      n += 2;		/* lower layer: 3 bytes	*/
	raw[n++] = 0xFF; raw[n++] = 20; raw[n++] = FIX_GRAVEL;
	put16(raw + n, 3);      n += 2;		/* metadata: 3 bytes	*/
	raw[n++] = 7; raw[n++] = 1; raw[n++] = 'x';	/* a hint field	*/
	CHECK_INT(expandraw(raw, n), TRUE);

	CHECK_INT(teststate.map[9].top.id, Wall);
	CHECK_MSG(teststate.map[10].top.id != Water,
		  "the upper layer read one byte past its declared size: cell 10"
		  " holds the low half of the lower layer's length word");
	CHECK_MSG(teststate.map[10].top.id == Nothing,
		  "cell 10 lies past the upper layer and must still hold the"
		  " zero expandmsdatlevel memsets, not a decoded tile");

	CHECK_INT(teststate.map[19].bot.id, Gravel);
	CHECK_MSG(teststate.map[20].bot.id != Water,
		  "the lower layer read one byte past its declared size: cell 20"
		  " holds the low half of the metadata length word");
	CHECK_MSG(teststate.map[20].bot.id == Nothing,
		  "cell 20 lies past the lower layer and must still hold the"
		  " zero expandmsdatlevel memsets, not a decoded tile");
    }

    tw_case("🔴 the trap, cloner and creature lists stop exactly at their count");
    {
	/* Three `for (i = 0 ; i < state->Xcount ; ++i)` loops fill fixed arrays
	 * from a length-delimited field. Relax any of them to `<=` and the loop
	 * reads one entry past the field -- bytes belonging to the NEXT field --
	 * and writes it into the array past the count the rest of the engine
	 * will trust.
	 *
	 * ⚠ THE ORACLE IS THE ENTRY PAST THE COUNT, POISONED FIRST. Asserting
	 * the count is right cannot see this: the count is assigned before the
	 * loop and the mutation does not change it. Asserting entry [0] cannot
	 * see it either. Only the slot the loop must NOT touch can.
	 */
	teststate.traps[1].from = -12345;
	teststate.traps[1].to = -12345;
	teststate.cloners[1].from = -12345;
	teststate.cloners[1].to = -12345;
	teststate.crlist[1] = -12345;

	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;		/* upper layer		*/
	raw[n++] = 0xFF; raw[n++] = 10; raw[n++] = FIX_WALL;
	put16(raw + n, 3);      n += 2;		/* lower layer		*/
	raw[n++] = 0xFF; raw[n++] = 20; raw[n++] = FIX_GRAVEL;
	put16(raw + n, 26);     n += 2;		/* metadata: 26 bytes	*/
	/* field 4: one trap, (1,1) -> (2,2). Ten bytes per entry, x at +0,
	 * y at +2 for the button and +4/+6 for the trap. */
	raw[n++] = 4; raw[n++] = 10;
	raw[n++] = 1; raw[n++] = 0; raw[n++] = 1; raw[n++] = 0;
	raw[n++] = 2; raw[n++] = 0; raw[n++] = 2; raw[n++] = 0;
	raw[n++] = 0; raw[n++] = 0;
	/* field 5: one cloner, (3,3) -> (4,4). Eight bytes per entry. */
	raw[n++] = 5; raw[n++] = 8;
	raw[n++] = 3; raw[n++] = 0; raw[n++] = 3; raw[n++] = 0;
	raw[n++] = 4; raw[n++] = 0; raw[n++] = 4; raw[n++] = 0;
	/* field 10: one creature, (5,5). Two bytes per entry. */
	raw[n++] = 10; raw[n++] = 2;
	raw[n++] = 5; raw[n++] = 5;
	CHECK_INT(expandraw(raw, n), TRUE);

	CHECK_INT(teststate.trapcount, 1);
	CHECK_INT(teststate.clonercount, 1);
	CHECK_INT(teststate.crlistcount, 1);

	CHECK_MSG(teststate.traps[1].from == -12345
		  && teststate.traps[1].to == -12345,
		  "the trap loop ran one entry past trapcount and overwrote the"
		  " slot after the list with bytes from the next field");
	CHECK_MSG(teststate.cloners[1].from == -12345
		  && teststate.cloners[1].to == -12345,
		  "the cloner loop ran one entry past clonercount");
	CHECK_MSG(teststate.crlist[1] == -12345,
		  "the creature-list loop ran one entry past crlistcount");
    }

    tw_case("🔴 an upper layer with EXACTLY two bytes of slack is accepted");
    {
	/* `if (data + size + 2 > dataend)` reserves the lower layer's own length
	 * word. The only record that separates `>` from `>=` is the one where
	 * the upper layer plus that word ends the file EXACTLY.
	 *
	 * ⚠ THE VERDICT CANNOT BE THE ORACLE: both forms end in badlevel, because
	 * a record with no lower layer is refused a few lines further down
	 * either way. What differs is how far it gets first -- the real bound
	 * decodes the upper layer, and that decode warns about the cells it could
	 * not fill. `>=` refuses before reaching it and warns about nothing. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;		/* upper layer: 3 bytes	*/
	raw[n++] = 0xFF; raw[n++] = 10; raw[n++] = FIX_WALL;
	put16(raw + n, 0);      n += 2;		/* lower length word, then EOF */
	CHECK_INT(expandraw(raw, n), FALSE);
	CHECK_MSG(warn_count >= 1,
		  "the upper layer was never decoded: its bounds check refused a"
		  " record that ends exactly at the lower layer's length word");
	CHECK_INT(teststate.map[0].top.id, Wall);
    }

    tw_case("🔴 a trailing two-byte field header with no payload is not parsed");
    {
	/* `while (data + 2 < dataend)` needs more than a bare header to call
	 * something a field. Relax it to `<=` and two trailing bytes are read as
	 * a field of size zero.
	 *
	 * ⚠ BOTH LAYERS FILL THE MAP EXACTLY HERE -- four runs of 255 plus four
	 * singles is 1,024 -- so the decoder warns about nothing and the warning
	 * count is a clean oracle. With a short layer the "missing bytes"
	 * warnings would swamp the one this case is looking for. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 16);     n += 2;		/* upper layer: 16 bytes	*/
	for (i = 0 ; i < 4 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR;
	}
	for (i = 0 ; i < 4 ; ++i)
	    raw[n++] = FIX_FLOOR;		/* 4 * 255 + 4 == 1024		*/
	put16(raw + n, 16);     n += 2;		/* lower layer: 16 bytes	*/
	for (i = 0 ; i < 4 ; ++i) {
	    raw[n++] = 0xFF; raw[n++] = 255; raw[n++] = FIX_FLOOR;
	}
	for (i = 0 ; i < 4 ; ++i)
	    raw[n++] = FIX_FLOOR;
	put16(raw + n, 2);      n += 2;		/* metadata: 2 bytes		*/
	raw[n++] = 2; raw[n++] = 0;		/* a field-2 header, no payload	*/
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_INT(teststate.map[1023].top.id, Empty);
	CHECK_MSG(warn_count == 0,
		  "two trailing bytes were parsed as a zero-length field; a field"
		  " header with no payload behind it is not a field");
    }

    /* ⚠ ONE MUTANT IN THIS FILE IS EQUIVALENT AND NOBODY SHOULD SPEND AN
     * AFTERNOON ON IT. `if (data + size > dataend) size = dataend - data;` in the
     * optional-field clamp reads `>=` identically: at data + size == dataend the
     * relaxed form runs the assignment, and the assignment stores exactly the
     * size it already had. No input can tell the two apart. */

    tw_case("a field 2 of exactly two bytes sets the chip count");
    {
	/* `if (size < 2) warn else chipsneeded = readword(data)`. Two is the
	 * only size that tells `< 2` from `<= 2`, and getting it wrong silently
	 * ignores a well-formed field rather than failing. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 5);      n += 2;		/* header says 5 chips	*/
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 10; raw[n++] = FIX_WALL;
	put16(raw + n, 3);      n += 2;
	raw[n++] = 0xFF; raw[n++] = 20; raw[n++] = FIX_GRAVEL;
	put16(raw + n, 4);      n += 2;		/* metadata: 4 bytes	*/
	raw[n++] = 2; raw[n++] = 2;		/* field 2, exactly 2 bytes */
	put16(raw + n, 77);     n += 2;
	CHECK_INT(expandraw(raw, n), TRUE);
	CHECK_MSG(teststate.chipsneeded == 77,
		  "a two-byte field 2 was ignored as too short, leaving the"
		  " header's chip count in place");
    }

    tw_case("a ten-byte record is exactly a header, and is not refused for size");
    {
	/* `if (setup->levelsize < 10)` is the minimum-header check, and ten is
	 * the only length that separates `< 10` from `<= 10`. Both forms reject
	 * this record -- there is no map behind the header -- so the verdict
	 * cannot be the oracle. What differs is WHERE it is rejected: the real
	 * bound reads the header first, so chipsneeded is assigned; `<= 10`
	 * refuses before that and leaves it alone. */
	teststate.chipsneeded = -999;
	n = 0;
	put16(raw + n, 1);      n += 2;		/* level number		*/
	put16(raw + n, 0);      n += 2;		/* time			*/
	put16(raw + n, 77);     n += 2;		/* chips needed		*/
	put16(raw + n, 1);      n += 2;		/* map detail		*/
	put16(raw + n, 0);      n += 2;		/* upper layer: 0 bytes	*/
	CHECK_INT(n, 10);
	CHECK_INT(expandraw(raw, n), FALSE);
	CHECK_MSG(teststate.chipsneeded == 77,
		  "a ten-byte record was refused by the minimum-length check"
		  " before its header was read; the bound is off by one");
    }

    return tw_end();
}
