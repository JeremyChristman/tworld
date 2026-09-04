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
 * The guard bytes are a real oracle here: expandleveldata() reads straight out
 * of the fenced buffer, so an over-write near either end is caught with no
 * sanitizer at all.
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

static int corpus_report(int ok, char const *name)
{
    ++corpus_replayed;
    CHECK_MSG(ok, "fuzz corpus input '%s' wrote outside its own buffer -- the"
		  " guard bytes around it were modified", name);
    return ok;
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
    int size, n;

    tw_begin("encoding");
    tw_expect_atleast(45);

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
	CHECK_MSG(ROW32POS(0) != POS_INVALID,
		  "a legitimate (0,32) wiring and the invalid marker collide --"
		  " this is exactly the jc-2 defect");
    }

    return tw_end();
}
