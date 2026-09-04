/* series_test.c: reading a level record out of a .dat.
 *
 * MOD (Jeremy, jc-44). Compiles ../fileio.c and ../series.c directly, so the
 * static readleveldata() is reachable. It takes a fileinfo, so these cases
 * write a small file to the working directory and read it back -- the only
 * suite here that touches the disk, and it cleans up after itself.
 *
 * WHY THIS EXISTS. readleveldata() is the FIRST thing that touches a downloaded
 * level set, and it is the gate that every later parser implicitly relies on:
 * encoding.c is safe from a whole class of over-read only because this function
 * rejects any level without a valid four-character password. A regression here
 * would not look like a crash in this file; it would quietly re-open a hole
 * somewhere else.
 *
 * TESTLANG: c
 *
 * series.c and fileio.c are compiled only as C by CMake, and both rely on C's
 * implicit void* conversion. See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-use-after-free
 */

#include	"tw_test.h"
#include	"tw_fixture.h"

#include	"../fileio.c"
#include	"../series.c"

/* --- the surface series.c reaches for, stubbed ------------------------- */

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

/* Nothing under test reaches these: they belong to the solution and
 * unsolvable-list machinery that readseriesfile() calls after the level data is
 * already in memory. Stubbed rather than dragging in two more modules. */
int readsolutions(gameseries *series) { (void)series; return TRUE; }
void clearsolutions(gameseries *series) { (void)series; }
int markunsolvablelevels(gameseries *series) { (void)series; return 0; }
void readextensions(gameseries *series) { (void)series; }

/* --- helpers ----------------------------------------------------------- */

static char const *scratchname = "tw_series_test.dat";

/* Write raw bytes as a file and run readleveldata() over them. The record must
 * carry its own leading 2-byte length, exactly as it does inside a .dat. */
static int readrecord(unsigned char const *record, int reclen, gamesetup *game)
{
    fileinfo file;
    FILE *f;
    int r;

    f = fopen(scratchname, "wb");
    if (!f)
	return -1;
    fputc(reclen & 0xFF, f);
    fputc((reclen >> 8) & 0xFF, f);
    fwrite(record, 1, (size_t)reclen, f);
    fclose(f);

    memset(game, 0, sizeof *game);
    clearfileinfo(&file);
    if (!fileopen(&file, scratchname, "rb", NULL))
	return -1;
    warn_count = 0;
    errmsg_count = 0;
    r = readleveldata(&file, game);
    fileclose(&file, NULL);
    remove(scratchname);
    return r;
}

static void put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

int main(void)
{
    unsigned char raw[256];
    gamesetup game;
    fixlevel lv;
    unsigned char *rec;
    int size, n, r;

    tw_begin("series");
    tw_expect_atleast(22);

    /* ================================================================== */
    tw_case("a lower-layer size that runs past the record is REFUSED (jc-44)");
    {
	/* 🔴 THE jc-44 DEFECT IN readleveldata().
	 *
	 * The bounds check here covered only the UPPER layer. The lower layer's
	 * 16-bit size was added to the pointer with no validation at all, and
	 * the next line dereferenced the result. With a tiny record and a
	 * 65535-byte declared lower layer, that read landed roughly 64 KB past
	 * the end of the allocation.
	 *
	 * The level is refused either way -- it has no password -- but before
	 * the fix it was refused only AFTER the wild read had happened, which
	 * is no comfort at all. What this case actually pins is that the
	 * refusal now happens without the parser walking off the buffer first;
	 * run it under a sanitizer and the difference is visible directly.
	 */
	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 0);      n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* map detail */
	put16(raw + n, 0);      n += 2;    /* upper layer: zero bytes */
	put16(raw + n, 0xFFFF); n += 2;    /* lower layer: 65535 bytes, a lie */
	r = readrecord(raw, n, &game);
	CHECK_MSG(r == FALSE, "a record declaring a 65535-byte lower layer was accepted");
	CHECK_MSG(game.leveldata == NULL,
		  "the rejected level left its buffer attached to the gamesetup");
    }

    tw_case("an upper-layer size that runs past the record is refused too");
    {
	/* The pre-existing check, asserted so the jc-44 one cannot be "verified"
	 * by a parser that has stopped bounding anything. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0xFFFF); n += 2;    /* upper layer: a lie */
	r = readrecord(raw, n, &game);
	CHECK_MSG(r == FALSE, "a record declaring a 65535-byte upper layer was accepted");
    }

    tw_case("a record too short to hold a header is refused");
    {
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	r = readrecord(raw, n, &game);
	CHECK_MSG(r == FALSE, "a 4-byte level record was accepted");
    }

    /* ================================================================== */
    tw_case("a well-formed level is read, with its number, time, name and password");
    fix_init(&lv);
    fix_border(&lv);
    lv.number = 7;
    lv.time = 120;
    strcpy(lv.name, "TEST LEVEL");
    strcpy(lv.passwd, "WXYZ");
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    rec = fix_build(&lv, &size);
    CHECK_MSG(rec != NULL, "the fixture builder returned nothing");
    if (rec) {
	r = readrecord(rec, size, &game);
	CHECK_MSG(r == TRUE, "a well-formed level record was refused");
	CHECK_INT(game.number, 7);
	CHECK_INT(game.time, 120);
	CHECK_STR(game.name, "TEST LEVEL");
	CHECK_STR(game.passwd, "WXYZ");
	CHECK_MSG(game.levelsize == size,
		  "levelsize is %d, expected %d", game.levelsize, size);
	CHECK_MSG(game.leveldata != NULL, "leveldata was not retained");
	CHECK_MSG(warn_count == 0,
		  "a well-formed level produced %d warning(s)", warn_count);
	free(game.leveldata);
	free(rec);
    }

    /* ================================================================== */
    tw_case("the password gate: a level without four characters is refused");
    {
	/* 🔴 THIS GATE IS LOAD-BEARING BEYOND THIS FILE. encoding.c's map
	 * decoder is safe from a two-byte over-read only because every level
	 * reaching it has passed here, and a four-character password guarantees
	 * several bytes of slack after the map layers. Weakening this check
	 * would silently re-open that hole in a different file.
	 * See docs/adr/0005 and FORK.md item 16. */
	static char const *const bad[] = { "", "AB", "ABC", "ABCDE" };
	int k;
	for (k = 0 ; k < 4 ; ++k) {
	    fix_init(&lv);
	    fix_border(&lv);
	    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	    strcpy(lv.passwd, bad[k]);
	    rec = fix_build(&lv, &size);
	    if (rec) {
		r = readrecord(rec, size, &game);
		CHECK_MSG(r == FALSE,
			  "a level whose password is \"%s\" (%d characters) was accepted",
			  bad[k], (int)strlen(bad[k]));
		free(rec);
	    }
	}
	/* And exactly four still works, so the gate is not simply refusing
	 * everything. */
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	strcpy(lv.passwd, "ABCD");
	rec = fix_build(&lv, &size);
	if (rec) {
	    r = readrecord(rec, size, &game);
	    CHECK_MSG(r == TRUE, "a level with a four-character password was refused");
	    if (r == TRUE) free(game.leveldata);
	    free(rec);
	}
    }

    tw_case("the password is XOR-0x99 decoded, not taken raw");
    fix_init(&lv);
    fix_border(&lv);
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    strcpy(lv.passwd, "BDHP");     /* the real password of CCLP1 level 1 */
    rec = fix_build(&lv, &size);
    if (rec) {
	r = readrecord(rec, size, &game);
	CHECK_MSG(r == TRUE, "the level was refused");
	CHECK_STR(game.passwd, "BDHP");
	if (r == TRUE) free(game.leveldata);
	free(rec);
    }

    tw_case("a level hash is computed, and differs between different levels");
    {
	/* levelhash is what identifies a level across .dat revisions; two
	 * different levels hashing the same would silently confuse solutions
	 * between them. */
	unsigned long h1 = 0, h2 = 0;
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	rec = fix_build(&lv, &size);
	if (rec) {
	    if (readrecord(rec, size, &game) == TRUE) {
		h1 = game.levelhash;
		free(game.leveldata);
	    }
	    free(rec);
	}
	fix_init(&lv);
	fix_border(&lv);
	fix_settop(&lv, 9, 9, FIX_CHIP_SOUTH);   /* Chip somewhere else */
	rec = fix_build(&lv, &size);
	if (rec) {
	    if (readrecord(rec, size, &game) == TRUE) {
		h2 = game.levelhash;
		free(game.leveldata);
	    }
	    free(rec);
	}
	CHECK_MSG(h1 != 0, "the first level produced no hash");
	CHECK_MSG(h1 != h2, "two different levels hashed identically (0x%lX)", h1);
    }

    return tw_end();
}
