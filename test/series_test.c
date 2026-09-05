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
 * TESTFLAGS: -Wno-use-after-free -DTWPLUSPLUS
 *
 * 🔴 -DTWPLUSPLUS IS NOT DECORATION, and its absence was a real hole found by
 * the jc-46 review. CMakeLists.txt defines it unconditionally for the shipped
 * Qt build, and series.c branches on it in three places -- so without it this
 * test compiled a series.c the released game does not contain. Concretely:
 * removefilenamesuffixes() (series.c:68) existed only in the shipped build and
 * was never compiled here, while gameseriescmp_name() (series.c:668) is the
 * opposite and was compiled here despite never shipping. That is the same trap
 * CLAUDE.md section 3.3 documents for WIN32 and fileio.c, one file over.
 * input_test.c has carried this flag for the same reason all along.
 */

#include	"tw_test.h"
#include	"tw_fixture.h"
#include	"tw_corpus.h"

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

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * test/fuzz/corpus/leveldata/ replayed through readleveldata(), so a libFuzzer
 * finding on Linux becomes a permanent regression case on every platform. See
 * test/tw_corpus.h.
 *
 * ⚠ THIS IS THE WEAKEST OF THE THREE REPLAYS, and saying so matters.
 * readleveldata() takes a fileinfo and reads into an allocation of its own, so
 * it never touches the buffer tw_corpus.h handed us -- the no-write check
 * cannot fail here even in principle. What a green run proves is exactly one
 * thing: these inputs still parse to completion without crashing, hanging or
 * aborting. The memory oracle for this parser is ASan in the `fuzz` CI job.
 *
 * 🔴 SO corpus_parsed IS THE ASSERTION THAT CARRIES THIS CASE. The replay goes
 * through a scratch file, and the first version simply returned when it could
 * not create one -- leaving a case whose name says the parser read every input
 * safely, passing without the parser having run at all. That is the exact
 * shape of lie CLAUDE.md section 3 catalogs, and it is reachable: a read-only
 * checkout, or a sandboxed working directory. The count is now asserted against
 * the number of files replayed.
 *
 * The fuzz target uses fmemopen() to avoid a disk write per execution; that is
 * POSIX-only, so the replay goes through a scratch file instead -- twenty files
 * once per suite run rather than tens of thousands per second.
 */
static char const *corpusscratch = "tw_corpus_test.dat";
static int corpus_replayed = 0;
static int corpus_parsed = 0;

static void corpus_read(twcorpusinput const *in)
{
    gamesetup	game;
    fileinfo	file;
    FILE       *f;

    f = fopen(corpusscratch, "wb");
    if (!f)
	return;
    if (fwrite(in->data, 1, (size_t)in->size, f) != (size_t)in->size) {
	fclose(f);
	remove(corpusscratch);
	return;
    }
    fclose(f);

    memset(&game, 0, sizeof game);
    clearfileinfo(&file);
    if (fileopen(&file, corpusscratch, "rb", NULL)) {
	warn_count = 0;
	errmsg_count = 0;
	readleveldata(&file, &game);
	fileclose(&file, NULL);
	free(game.leveldata);
	++corpus_parsed;
    }
    remove(corpusscratch);
}

static void corpus_report(twcorpusverdict v, char const *name)
{
    ++corpus_replayed;
    CHECK_MSG(v == TW_CORPUS_OK, "fuzz corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

/* The .dac corpus, replayed through readconfigfile(). Same contract as above
 * (docs/adr/0011): the fuzzer discovers on Linux, this remembers everywhere.
 * The fuzz target uses fmemopen(); this uses a scratch file of its own -- a
 * DIFFERENT one from the .dac unit cases below, so the two cannot tread on
 * each other, for the same POSIX-only reason. */
static char const *daccorpusscratch = "tw_daccorpus_test.dac";
static int daccorpus_replayed = 0;
static int daccorpus_parsed = 0;

static void daccorpus_read(twcorpusinput const *in)
{
    gameseries	series;
    fileinfo	file;
    FILE       *f;

    f = fopen(daccorpusscratch, "wb");
    if (!f)
	return;
    if (fwrite(in->data, 1, (size_t)in->size, f) != (size_t)in->size) {
	fclose(f);
	remove(daccorpusscratch);
	return;
    }
    fclose(f);

    memset(&series, 0, sizeof series);
    clearfileinfo(&file);
    if (fileopen(&file, daccorpusscratch, "rb", NULL)) {
	warn_count = 0;
	errmsg_count = 0;
	readconfigfile(&file, &series);
	fileclose(&file, NULL);
	++daccorpus_parsed;
    }
    remove(daccorpusscratch);
}

static void daccorpus_report(twcorpusverdict v, char const *name)
{
    ++daccorpus_replayed;
    CHECK_MSG(v == TW_CORPUS_OK, "dac corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

static void put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

/* --- the .dac configuration parser --------------------------------------- *
 *
 * readconfigfile() had NO unit test until now, which CLAUDE.md section 5 listed
 * as a known gap. It is the other half of the untrusted-input surface: every
 * level set in a sets\ directory is reached through a .dac, and a .dac is a
 * text file somebody else wrote. The end-to-end layer opens the stock ones, so
 * the happy path was covered by accident; nothing covered a malformed one.
 *
 * The parser is unusual in being LINE-BASED TEXT rather than a binary record,
 * which is why it needs its own harness and its own fuzz target -- it is the
 * one parser here whose interesting inputs are not byte patterns.
 */
static char const *dacscratch = "tw_dac_test.dac";

/* Write text as a .dac and run readconfigfile() over it. Returns the data-file
 * name the parser reported, or NULL if it refused the file.
 */
/* 🔴 Counted, and asserted zero at the end of the .dac cases. readdac()
 * returns NULL both when the PARSER refused the file and when the HARNESS could
 * not stage it, and the nine `== NULL` assertions below cannot tell those
 * apart -- so in a read-only working directory every one of them would pass
 * without the parser running. That is the same conflation the leveldata corpus
 * replay was fixed for last round; the counter is how it stays honest. */
static int dac_harness_failures = 0;

static char *readdac(char const *text, gameseries *series)
{
    fileinfo	file;
    FILE       *f;
    char       *r;
    size_t	len;

    len = strlen(text);
    f = fopen(dacscratch, "wb");
    if (!f) {
	++dac_harness_failures;
	return NULL;
    }
    if (fwrite(text, 1, len, f) != len) {
	++dac_harness_failures;
	fclose(f);
	remove(dacscratch);
	return NULL;
    }
    fclose(f);

    memset(series, 0, sizeof *series);
    clearfileinfo(&file);
    if (!fileopen(&file, dacscratch, "rb", NULL)) {
	++dac_harness_failures;
	remove(dacscratch);
	return NULL;
    }
    warn_count = 0;
    errmsg_count = 0;
    r = readconfigfile(&file, series);
    fileclose(&file, NULL);
    remove(dacscratch);
    return r;
}

int main(void)
{
    unsigned char raw[256];
    gamesetup game;
    fixlevel lv;
    unsigned char *rec;
    int size, n, r;

    tw_begin("series");
    tw_expect_atleast(110);

    tw_case("every committed fuzz corpus input still reads safely");
    {
	char dir[256];
	int c;

	CHECK_MSG(tw_corpus_dir("leveldata", dir, sizeof dir),
		  "could not find test/fuzz/corpus/leveldata from the working"
		  " directory -- the replay would have proved nothing");
	if (dir[0]) {
	    c = tw_corpus_run(dir, corpus_read, corpus_report);
	    /* %.100s: tw_fail_ formats into 256 bytes; an unbounded %s of a
	     * 256-byte array trips -Werror=format-truncation. */
	    CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	    CHECK_INT(corpus_replayed, c);
	    /* The one that stops this case passing without the parser having
	     * run at all -- see the note above corpus_read(). */
	    CHECK_MSG(corpus_parsed == c,
		      "readleveldata() ran on only %d of %d corpus inputs; the"
		      " rest could not be staged to a scratch file, so this case"
		      " would otherwise have passed without parsing them",
		      corpus_parsed, c);
	}
    }

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

    /* ================================================================== *
     * The .dac configuration parser. See the note above readdac().
     * ================================================================== */

    tw_case("every committed .dac fuzz corpus input still parses safely");
    {
	char dir[256];
	int c;

	CHECK_MSG(tw_corpus_dir("dac", dir, sizeof dir),
		  "could not find test/fuzz/corpus/dac from the working"
		  " directory -- the replay would have proved nothing");
	if (dir[0]) {
	    c = tw_corpus_run(dir, daccorpus_read, daccorpus_report);
	    CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	    CHECK_INT(daccorpus_replayed, c);
	    CHECK_MSG(daccorpus_parsed == c,
		      "readconfigfile() ran on only %d of %d corpus inputs",
		      daccorpus_parsed, c);
	}
    }

    tw_case("a minimal .dac names its data file");
    {
	gameseries s;
	char *r = readdac("file = CCLP1.dat\n", &s);
	CHECK_MSG(r != NULL, "a well-formed .dac was refused");
	if (r)
	    CHECK_STR(r, "CCLP1.dat");
    }

    tw_case("ruleset, lastlevel and the three flag directives are read");
    {
	gameseries s;
	char *r;

	r = readdac("file = a.dat\nruleset = ms\nlastlevel = 144\n", &s);
	CHECK_MSG(r != NULL, "a valid ms .dac was refused");
	CHECK_INT(s.ruleset, Ruleset_MS);
	CHECK_INT(s.final, 144);

	/* Mixed case, because the parser lowercases both name and value. */
	r = readdac("file = a.dat\nRuleSet = LyNx\n", &s);
	CHECK_MSG(r != NULL, "a mixed-case ruleset was refused");
	CHECK_INT(s.ruleset, Ruleset_Lynx);

	r = readdac("file = a.dat\nusepasswords = n\n", &s);
	CHECK_MSG(r != NULL, "usepasswords = n was refused");
	CHECK_INT(s.gsflags & GSF_IGNOREPASSWDS, GSF_IGNOREPASSWDS);

	r = readdac("file = a.dat\nusepasswords = y\n", &s);
	CHECK_INT(s.gsflags & GSF_IGNOREPASSWDS, 0);

	r = readdac("file = a.dat\nfixlynx = y\n", &s);
	CHECK_INT(s.gsflags & GSF_LYNXFIXES, GSF_LYNXFIXES);

	r = readdac("file = a.dat\nfileinsetsdir = y\n", &s);
	CHECK_INT(s.gsflags & GSF_DATFORDACSERIESDIR, GSF_DATFORDACSERIESDIR);
	(void)r;
    }

    tw_case("comments and blank lines are skipped, not parsed");
    {
	gameseries s;
	char *r = readdac("file = a.dat\n"
			  "# a comment\n"
			  "\n"
			  "   \t  \n"
			  "   # an indented comment\n"
			  "ruleset = lynx\n", &s);
	CHECK_MSG(r != NULL, "comments or blank lines were treated as syntax errors");
	CHECK_INT(s.ruleset, Ruleset_Lynx);
    }

    tw_case("a .dac that is wrong is REFUSED, not half-accepted");
    {
	gameseries s;
	/* 🔴 These are the cases that matter for a file somebody else wrote.
	 * Each must return NULL -- a parser that accepts a malformed config and
	 * carries on with a partly-filled gameseries is how a level set loads
	 * with the wrong ruleset, which silently invalidates every solution
	 * recorded against it. */
	CHECK_MSG(readdac("ruleset = ms\n", &s) == NULL,
		  "a .dac with no 'file =' first line was accepted");
	/* BOTH separators, and that is not belt-and-braces. With only the
	 * forward-slash case here, deleting the backslash arm of the fix left
	 * this whole file green -- measured. A .dac written on either platform
	 * can be read on the other, so both arms are load-bearing. */
	CHECK_MSG(readdac("file = sub/dir/a.dat\n", &s) == NULL,
		  "a .dac naming a forward-slash path was accepted");
	CHECK_MSG(readdac("file = sub\\dir\\a.dat\n", &s) == NULL,
		  "a .dac naming a backslash path was accepted");
	CHECK_MSG(readdac("file = ../../../x.dat\n", &s) == NULL,
		  "a .dac naming a relative parent path was accepted");
	/* No separator is needed to reach somewhere unintended on Windows:
	 * CON, NUL, COM1 and LPT1 resolve to the DEVICE from inside any
	 * directory, extension ignored. jc-42 guarded tileset names against
	 * exactly this and level sets were left open. */
	CHECK_MSG(readdac("file = LPT1\n", &s) == NULL,
		  "a .dac naming a device was accepted");
	CHECK_MSG(readdac("file = com1.dat\n", &s) == NULL,
		  "a .dac naming a device WITH an extension was accepted");
	CHECK_MSG(readdac("file = NUL\n", &s) == NULL,
		  "a .dac naming NUL was accepted");
	CHECK_MSG(readdac("file = a.dat\nnosuchdirective = 1\n", &s) == NULL,
		  "an unknown directive was accepted");
	CHECK_MSG(readdac("file = a.dat\nruleset = klingon\n", &s) == NULL,
		  "an invalid ruleset was accepted");
	CHECK_MSG(readdac("file = a.dat\nlastlevel = 0\n", &s) == NULL,
		  "lastlevel = 0 was accepted");
	CHECK_MSG(readdac("file = a.dat\nlastlevel = -5\n", &s) == NULL,
		  "a negative lastlevel was accepted");
	CHECK_MSG(readdac("file = a.dat\nlastlevel = 12x\n", &s) == NULL,
		  "lastlevel with trailing garbage was accepted");
	CHECK_MSG(readdac("file = a.dat\nthisline has no equals sign\n", &s) == NULL,
		  "a line with no '=' was accepted");
	CHECK_MSG(readdac("", &s) == NULL, "an empty .dac was accepted");
    }

    tw_case("a .dac with high-bit bytes does not misbehave");
    {
	/* 🔴 THE ctype TRAP. readconfigfile() calls isspace() and tolower() on
	 * a plain `char`, which is SIGNED on both toolchains this builds with.
	 * Any byte >= 0x80 therefore reaches those functions as a NEGATIVE int,
	 * which is undefined behavior -- the argument must be representable as
	 * unsigned char, or EOF.
	 *
	 * Level packs really do carry accented characters, so this is ordinary
	 * input rather than an attack. These cases do not assert a particular
	 * verdict for the file (either refusing it or reading it is defensible);
	 * they assert that the parser RETURNS, does not crash, and does not
	 * produce a garbage ruleset.
	 *
	 * ⚠ THEY ARE NOT A REGRESSION NET FOR THE (unsigned char) CASTS, and an
	 * earlier version of this comment claimed they were. Measured: reverting
	 * all six casts in series.c leaves this file green. It cannot be
	 * otherwise -- every one of the 256 byte values gives the same answer
	 * through ctype signed or unsigned on both shipping toolchains, so there
	 * is no observable difference to assert. The casts are hardening against
	 * undefined behavior, verified by inspection; these cases are a crash
	 * net. Do not mistake the second for the first. */
	gameseries s;
	char *r;

	/* Asserted EXACTLY, not "looks plausible". The first version of this
	 * case checked `strlen(r) < 256`, which cannot fail: filegetline() caps
	 * the line at 254 characters, so the name can never reach 256 whatever
	 * the parser does. Measured with a deliberately shrunk destination
	 * buffer, that check stayed green through a 215-byte overflow. */
	r = readdac("file = \xE9t\xE9.dat\n", &s);
	CHECK_MSG(r != NULL, "a high-bit filename was refused outright");
	if (r)
	    CHECK_STR(r, "\xE9t\xE9.dat");

	r = readdac("file = a.dat\n\xE9\xE9\xE9 = 1\n", &s);
	CHECK_MSG(r == NULL, "a high-bit directive name was accepted");

	r = readdac("file = a.dat\n\xA0ruleset = ms\n", &s);
	CHECK_MSG(r == NULL || s.ruleset == Ruleset_MS || s.ruleset == Ruleset_None,
		  "a high-bit leading byte produced a garbage ruleset (%d)", s.ruleset);

	r = readdac("file = a.dat\nruleset = \xE9s\n", &s);
	CHECK_MSG(r == NULL, "a high-bit ruleset value was accepted");
    }

    tw_case("a line at and past the buffer boundary is handled");
    {
	/* filegetline() reads at most sizeof buf - 1 = 255 bytes and then
	 * discards to end of line. The sscanf conversions below it have no
	 * width specifiers, so their safety depends entirely on that limit --
	 * which is worth pinning, because it is the same shape as jc-44 (a
	 * buffer that is safe only because of a bound in a different place). */
	gameseries s;
	char line[600];
	char *r;
	int i;

	memset(line, 0, sizeof line);
	strcpy(line, "file = ");
	for (i = 7 ; i < 500 ; ++i)
	    line[i] = 'a';
	line[500] = '\n';
	line[501] = '\0';
	/* 🔴 EXACTLY 247, and that number is the whole point of the case.
	 * filegetline() is handed *len = 255, so fgets stores at most 254
	 * characters; "file = " is 7 of them, leaving 247 for the name. The
	 * destination is char[256], so the real headroom on that un-widthed
	 * `sscanf(buf, "file = %[^\n\r]", datfilename)` is EIGHT bytes, and it
	 * exists only because of a cap in a different function.
	 *
	 * An earlier version asserted `strlen(r) < 256`, which is unfalsifiable
	 * -- 247 is always less than 256. Shrinking datfilename to char[32]
	 * overflows a static by 215 bytes and that check stayed green. */
	r = readdac(line, &s);
	CHECK_MSG(r != NULL, "an over-long filename was refused outright");
	if (r)
	    CHECK_INT((int)strlen(r), 247);

	memset(line, 0, sizeof line);
	strcpy(line, "file = a.dat\n");
	for (i = 13 ; i < 500 ; ++i)
	    line[i] = 'b';
	line[500] = '\n';
	line[501] = '\0';
	r = readdac(line, &s);
	CHECK_MSG(r == NULL, "an over-long directive line was accepted");
    }

    tw_case("a line that exactly fills the buffer does not eat the next one");
    {
	/* 🔴 filegetline() (fileio.c:220) tests `buf[n] != '\n'` where n is
	 * strlen(buf) -- so it indexes the NUL TERMINATOR, which is never a
	 * newline. The condition degenerates to "the buffer filled", and a line
	 * that filled it exactly, newline included, takes the discard-to-end-of-
	 * line branch and swallows the WHOLE NEXT LINE.
	 *
	 * That is not cosmetic here. The line eaten below is `ruleset = ms`, and
	 * losing it leaves series->ruleset at Ruleset_None, whereupon
	 * readseriesheader() (series.c:189) falls back to the .dat's own
	 * signature. A set can load under the WRONG RULESET from a .dac that
	 * looks perfectly fine -- and that silently invalidates every solution
	 * recorded against it, which is the worst outcome this program has.
	 *
	 * Latent: it needs a line of exactly the boundary length. filegetline()
	 * is called here with *len = 255 and fgets stores at most 254
	 * characters, so the boundary is 254 including the newline. 252 and 253
	 * are checked either side of it so a future change to the buffer size
	 * cannot quietly move the cliff without turning this red. */
	gameseries s;
	char buf[700];
	char *r, *q;
	int i, pad;

	for (pad = 252 ; pad <= 254 ; ++pad) {
	    q = buf;
	    memcpy(q, "file = a.dat\n", 13);
	    q += 13;
	    *q++ = '#';
	    for (i = 1 ; i < pad - 1 ; ++i)
		*q++ = 'x';
	    *q++ = '\n';
	    strcpy(q, "ruleset = ms\n");

	    r = readdac(buf, &s);
	    CHECK_MSG(r != NULL, "a %d-byte comment line got the file refused", pad);
	    CHECK_MSG(s.ruleset == Ruleset_MS,
		      "a comment line of exactly %d bytes swallowed the"
		      " 'ruleset = ms' line after it -- ruleset came back %d",
		      pad, s.ruleset);
	}
    }

    tw_case("both settings of every flag directive are honored");
    {
	/* The `= y` side of each was covered above; these are the CLEAR
	 * branches, which nothing reached. usepasswords has both already. */
	gameseries s;

	readdac("file = a.dat\nfixlynx = n\n", &s);
	CHECK_INT(s.gsflags & GSF_LYNXFIXES, 0);
	readdac("file = a.dat\nfileinsetsdir = n\n", &s);
	CHECK_INT(s.gsflags & GSF_DATFORDACSERIESDIR, 0);
    }

    tw_case("an INDENTED directive is refused, and that is a real inconsistency");
    {
	/* Pinning current behavior, not endorsing it. readconfigfile() skips
	 * leading whitespace into `p` (series.c:499) and then scans **buf**
	 * (series.c:502), so `%[^= \t]` matches zero characters and an indented
	 * directive is a syntax error -- while an indented COMMENT works,
	 * because that check does use `p`.
	 *
	 * unslist.c:165 performs the identical skip and then scans `p`, so the
	 * two configuration parsers in this tree disagree about indentation.
	 * Left alone deliberately: changing it would make files the game
	 * currently refuses start loading, and no real .dac needs it. Recorded
	 * so the next person finds a pinned decision rather than a surprise. */
	gameseries s;
	CHECK_MSG(readdac("file = a.dat\n   ruleset = ms\n", &s) == NULL,
		  "an indented directive was accepted -- if that was deliberate,"
		  " update this case and unslist.c's matching skip");
	CHECK_MSG(readdac("file = a.dat\n   # indented comment\nruleset = ms\n",
			  &s) != NULL,
		  "an indented COMMENT was refused; only directives should be");
    }

    tw_case("isreservedfilename knows a Windows device from a level set");
    {
	/* This moved out of res.c in jc-48, where it had guarded tileset names
	 * since jc-42 with no test of any kind. It is testable here because
	 * series_test.c compiles fileio.c -- which is half the reason for
	 * moving it. Polarity: TRUE means the name IS reserved. */
	CHECK_MSG(isreservedfilename("CON"), "CON not recognized");
	CHECK_MSG(isreservedfilename("nul"), "lowercase nul not recognized");
	CHECK_MSG(isreservedfilename("CoM1"), "mixed-case COM1 not recognized");
	CHECK_MSG(isreservedfilename("LPT9"), "LPT9 not recognized");
	/* The extension is ignored, because "COM1.dat" resolves to the device. */
	CHECK_MSG(isreservedfilename("COM1.dat"), "COM1.dat not recognized");
	CHECK_MSG(isreservedfilename("aux.bmp"), "aux.bmp not recognized");
	/* And ordinary names are NOT swept up -- the failure mode that would
	 * quietly make real level sets vanish. */
	CHECK_MSG(!isreservedfilename("CCLP1.dat"), "CCLP1.dat rejected");
	CHECK_MSG(!isreservedfilename("CONCERT.dat"), "CONCERT.dat rejected");
	CHECK_MSG(!isreservedfilename("COM10"), "COM10 rejected (only 1-9 exist)");
	CHECK_MSG(!isreservedfilename("NULL.dat"), "NULL.dat rejected");
	CHECK_MSG(!isreservedfilename(""), "the empty name rejected");
	CHECK_MSG(!isreservedfilename("GAP'sSub.dat"),
		  "the one exotic name in the maintainer's 598 files rejected");
	/* Longer than the internal base[16] buffer: must not overflow, and is
	 * far too long to be a device name. */
	CHECK_MSG(!isreservedfilename("averyverylongfilenameindeed.dat"),
		  "an over-long name was treated as a device");
    }

    tw_case("every .dac case above actually reached the parser");
    {
	/* The assertion that stops all of the `== NULL` expectations above from
	 * passing for the wrong reason. See the note above readdac(). */
	CHECK_MSG(dac_harness_failures == 0,
		  "%d .dac case(s) could not be staged to a scratch file, so"
		  " their results say nothing about the parser",
		  dac_harness_failures);
    }

    return tw_end();
}
