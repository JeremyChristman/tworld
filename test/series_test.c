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

/* Like readrecord(), but WITHOUT clearing *game first -- so a case can poison a
 * field and assert the parser never wrote it. That is the only way to see a
 * bound whose failure changes nothing but what was written before refusing. */
static int readrecordinto(unsigned char const *record, int reclen, gamesetup *game);

/* Write raw bytes as a file and run readleveldata() over them. The record must
 * carry its own leading 2-byte length, exactly as it does inside a .dat. */
static int readrecord(unsigned char const *record, int reclen, gamesetup *game)
{
    memset(game, 0, sizeof *game);
    return readrecordinto(record, reclen, game);
}

static int readrecordinto(unsigned char const *record, int reclen, gamesetup *game)
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
    tw_expect_atleast(143);

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

    /* ================================================================== *
     * 🔴 THE EXACT BOUNDARIES OF readleveldata()'s GUARDS.
     *
     * Added after an adversarial audit loosened every guard in this function by
     * one byte and the whole suite -- unit, sanitize, golden -- stayed green,
     * which CLAUDE.md contradicted in writing ("every guard on the untrusted
     * path dies" -- true of readconfigfile(), not of this). Each case below sits
     * on the one input where the guard and its off-by-one disagree.
     *
     * ⚠ AND "REFUSED" IS NOT THE ORACLE. Both forms of every one of these
     * refuse the record, a line or two apart. What differs is what the parser
     * READ or WROTE before refusing -- so each case poisons the field that only
     * the wrong form would write, or counts the warning only it would reach.
     * Two of the audit's survivors have no such difference and are recorded as
     * equivalent where they are: `data + 2 >= dataend` at the upper layer reads
     * in bounds either way, and the password loop's `n < 15` writes into a
     * 256-byte buffer either way.
     * ================================================================== */
    tw_case("🔴 a ONE-byte record is refused before its level number is read");
    {
	/* series.c `size < 2`. A lone byte has no second byte for the number,
	 * so the refusal must come BEFORE `data[0] | (data[1] << 8)`; one byte
	 * looser and that reads past a one-byte allocation. */
	memset(&game, 0, sizeof game);
	game.number = 0x5A5A;
	raw[0] = 0x07;
	r = readrecordinto(raw, 1, &game);
	CHECK_MSG(r == FALSE, "a one-byte level record was accepted");
	CHECK_MSG(game.number == 0x5A5A,
		  "a one-byte record was refused only AFTER its level number was read"
		  " (0x%X) -- one byte past the record", game.number);
    }

    tw_case("🔴 a NINE-byte record is refused before its time is read; TEN gets further");
    {
	/* series.c `size < 10`: the fixed header is ten bytes, and at nine the
	 * guard must fire before `game->time` is assembled. The ten-byte half is
	 * what stops a guard that refuses everything from passing -- it is still
	 * refused, a line later, but only after legitimately reading the time. */
	n = 0;
	put16(raw + n, 1);      n += 2;    /* level number */
	put16(raw + n, 999);    n += 2;    /* time */
	put16(raw + n, 0);      n += 2;    /* chips */
	put16(raw + n, 1);      n += 2;    /* map detail */
	raw[n++] = 0;                      /* half of the upper-layer size */
	memset(&game, 0, sizeof game);
	game.time = 0x5A5A;
	r = readrecordinto(raw, n, &game);
	CHECK_MSG(r == FALSE, "a nine-byte level record was accepted");
	CHECK_MSG(game.time == 0x5A5A,
		  "a nine-byte record was refused only AFTER its time was read (%d)", game.time);

	raw[n++] = 0;                      /* the other half: now a full header */
	memset(&game, 0, sizeof game);
	game.time = 0x5A5A;
	r = readrecordinto(raw, n, &game);
	CHECK_MSG(r == FALSE, "a header-only record with no map was accepted");
	CHECK_INT(game.time, 999);
    }

    tw_case("🔴 a record ending ONE byte into the field-block size word is refused quietly");
    {
	/* series.c `data + 2 > dataend`, after the lower layer. The optional
	 * fields are introduced by a two-byte size word; with one byte of it
	 * present, the correct guard refuses straight away. One byte looser and
	 * it reads the second byte from past the allocation, then complains
	 * about "inconsistent size data" on its way to the same refusal -- a
	 * warning the correct guard never reaches. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;    /* upper layer: empty */
	put16(raw + n, 0);      n += 2;    /* lower layer: empty */
	raw[n++] = 0;                      /* ONE byte of the field-block size */
	r = readrecord(raw, n, &game);
	CHECK_MSG(r == FALSE, "a record with half a field-block size word was accepted");
	CHECK_MSG(warn_count == 0,
		  "that record was refused only after reading past it: %d warning(s)"
		  " about a size word the record does not contain", warn_count);
	CHECK_MSG(errmsg_count == 1, "the refusal was not reported (%d)", errmsg_count);
    }

    tw_case("a field claiming ONE byte more than remains is clamped to what remains");
    {
	/* series.c `if (size > dataend - data) size = dataend - data;`. A name
	 * field declaring three bytes with two left must copy two. One byte
	 * looser and the memcpy takes a byte from past the allocation.
	 *
	 * ⚠ Which byte that is depends on the heap, so on the plain pass the
	 * name check below catches the loosened clamp only when that byte is
	 * non-zero. The Linux sanitizer job's ASan reports it every time -- which
	 * is why this case exists even though a plain run cannot promise it. */
	n = 0;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 0);      n += 2;
	put16(raw + n, 1);      n += 2;
	put16(raw + n, 0);      n += 2;    /* upper layer: empty */
	put16(raw + n, 0);      n += 2;    /* lower layer: empty */
	put16(raw + n, 10);     n += 2;    /* ten bytes of fields follow */
	raw[n++] = 6; raw[n++] = 4;        /* field 6, password, four bytes */
	raw[n++] = 'A' ^ 0x99; raw[n++] = 'B' ^ 0x99;
	raw[n++] = 'C' ^ 0x99; raw[n++] = 'D' ^ 0x99;
	raw[n++] = 3; raw[n++] = 3;        /* field 3, name, claims THREE bytes */
	raw[n++] = 'X'; raw[n++] = 'Y';    /* ...and the record ends after two */
	r = readrecord(raw, n, &game);
	CHECK_MSG(r == TRUE, "a record whose last field over-claims by one was refused");
	CHECK_STR(game.name, "XY");
	if (r == TRUE) free(game.leveldata);
    }

    tw_case("🔴 the Lynx fixups refuse a level too short for them -- at EXACTLY its length");
    {
	/* undomschanges() writes fixed bytes into CHIPS.DAT's level data when a
	 * .dac says fixlynx=y, and first checks every target offset lies inside
	 * its level: `levelsize <= fixup->pos` refuses. At levelsize == pos the
	 * write would land one byte past the allocation -- a heap WRITE from an
	 * untrusted file -- and one byte looser the check lets it through.
	 *
	 * The plain oracle is the return value and the count: refused, nothing
	 * changes; let through, level 145 is deleted and count drops to 148. */
	gameseries	series;
	int		k, ok = 1;

	memset(&series, 0, sizeof series);
	series.count = 149;
	series.games = calloc(149, sizeof *series.games);
	CHECK_MSG(series.games != NULL, "allocation failed");
	for (k = 0 ; series.games && k < 149 ; ++k) {
	    /* 0x400 clears every offset in the fixup table (the largest is 0x392). */
	    series.games[k].levelsize = (k == 5) ? 0x011D : 0x400;
	    series.games[k].leveldata = calloc((size_t)series.games[k].levelsize, 1);
	    if (!series.games[k].leveldata) ok = 0;
	}
	CHECK_MSG(ok, "allocation failed");
	if (series.games && ok) {
	    /* Level 5's fixup writes offset 0x011D; its data is exactly that long. */
	    CHECK_MSG(undomschanges(&series) == FALSE,
		      "a level exactly as long as its fixup offset was accepted -- the"
		      " fixup would write one byte past it");
	    CHECK_INT(series.count, 149);

	    /* And one byte longer is enough, so the refusal is about the bound. */
	    free(series.games[5].leveldata);
	    series.games[5].levelsize = 0x011E;
	    series.games[5].leveldata = calloc(0x011E, 1);
	    CHECK_MSG(series.games[5].leveldata != NULL, "allocation failed");
	    if (series.games[5].leveldata) {
		CHECK_MSG(undomschanges(&series) == TRUE,
			  "a level one byte longer than its fixup offset was refused");
		CHECK_INT(series.count, 148);
		CHECK_INT(series.games[5].leveldata[0x011D], 'P' ^ 0x99);
	    }
	}
	/* undomschanges() freed the old level 145 and shifted the rest down, so
	 * free whatever the array holds now, up to the count it left. */
	for (k = 0 ; series.games && k < series.count ; ++k)
	    free(series.games[k].leveldata);
	free(series.games);
    }

    tw_case("🔴 TWO trailing bytes in the field block are not read as a field");
    {
	/* `while (data + 2 < dataend)` is the loop over [type][size][payload]
	 * records, and it must stop with two bytes left: a field header needs
	 * two bytes AND a payload position after them. An audit loosened it to
	 * `data + 1 < dataend` and every layer stayed green.
	 *
	 * The damage is not a read past the record -- those two bytes are
	 * present. It is that they get PARSED. A trailing pair whose first byte
	 * is 6 becomes a zero-length password field, and `case 6` ends with
	 * `game->passwd[n] = '\0'` with n == 0, wiping the real password read a
	 * moment earlier; the level is then refused for having no password. So
	 * a level that loads today would stop loading, which for a downloaded
	 * set is the whole set past that point.
	 *
	 * The two bytes are added INSIDE the declared block (its size word is
	 * patched) so the record stays self-consistent and raises no warning --
	 * otherwise "inconsistent size data" would be the thing under test. */
	fix_init(&lv);
	fix_border(&lv);
	lv.number = 7;
	lv.time = 120;
	strcpy(lv.name, "TRAILER");
	strcpy(lv.passwd, "WXYZ");
	fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
	rec = fix_build(&lv, &size);
	CHECK_MSG(rec != NULL, "the fixture builder returned nothing");
	if (rec) {
	    unsigned char *wide = malloc((size_t)size + 2);
	    int upperlen = rec[8] | (rec[9] << 8);
	    int lowerpos = 10 + upperlen;
	    int lowerlen = rec[lowerpos] | (rec[lowerpos + 1] << 8);
	    int metapos = lowerpos + 2 + lowerlen;
	    int meta = rec[metapos] | (rec[metapos + 1] << 8);
	    CHECK_MSG(metapos + 2 + meta == size,
		      "the fixture's field block is not where this case thinks it is"
		      " (block ends at %d, record is %d) -- do not trust the result",
		      metapos + 2 + meta, size);
	    if (wide && metapos + 2 + meta == size) {
		memcpy(wide, rec, (size_t)size);
		wide[metapos] = (unsigned char)((meta + 2) & 0xFF);
		wide[metapos + 1] = (unsigned char)(((meta + 2) >> 8) & 0xFF);
		wide[size] = 6;		/* would be a password field ... */
		wide[size + 1] = 0;	/* ... of length zero */
		r = readrecord(wide, size + 2, &game);
		CHECK_MSG(r == TRUE,
			  "a level with two trailing bytes in its field block was"
			  " REFUSED: the loop read them as a zero-length password"
			  " field and wiped the password it had just read");
		if (r) {
		    CHECK_STR(game.passwd, "WXYZ");
		    CHECK_STR(game.name, "TRAILER");
		    CHECK_MSG(warn_count == 0,
			      "the record raised %d warning(s); it is meant to be"
			      " self-consistent", warn_count);
		    free(game.leveldata);
		}
	    }
	    free(wide);
	    free(rec);
	}

	/* ⚠ TWO NEIGHBORING BOUNDS ARE EQUIVALENT MUTANTS -- do not spend an
	 * afternoon on them. An audit reported all three of this function's
	 * pointer bounds as survivors; only the loop above is reachable.
	 *
	 *   series.c:234  `data + 2 >= dataend` -> `data + 1 >=`   (loosened)
	 *   series.c:253  `data + 2 >  dataend` -> `data + 3 >`    (tightened)
	 *
	 * Each disagrees with the shipped form on exactly one input shape: a
	 * record with two bytes left at that point. MEASURED, baseline against
	 * each mutant, on both shapes -- a 12-byte record with an empty upper
	 * layer, and a 16-byte record whose field block is the size word alone:
	 * readrecord() returns FALSE with one errmsg and no warning in ALL
	 * THREE builds. The reason is structural rather than lucky: after the
	 * loosened bound there are no bytes left for the next check to accept,
	 * and a record whose field block is empty carries no password, which
	 * the function refuses a few lines later. Nothing observable differs,
	 * so no case here can kill them. */
    }

    tw_case("🔴 a .dat whose SIGNATURE is wrong is refused; the right one is read");
    {
	/* readseriesheader() is the first thing a downloaded .dat meets, and
	 * deleting its signature test survived every layer: nothing in the suite
	 * ever handed it a file that was not a level set. One bit off (0xAAAD),
	 * with a valid ruleset word and a nonzero level count after it, so only
	 * the signature test can refuse it -- and the correctly signed control
	 * proves the refusal is not something else about the file. */
	static unsigned short const sigs[2] = { 0xAAAD, SIG_DATFILE };
	int k;
	for (k = 0 ; k < 2 ; ++k) {
	    gameseries series;
	    unsigned char hdr[6];
	    FILE *f = fopen(scratchname, "wb");
	    int r;
	    if (!f) {
		tw_skip("could not create a temporary .dat in the working directory");
		break;
	    }
	    put16(hdr, sigs[k]);
	    put16(hdr + 2, SIG_DATFILE_MS);
	    put16(hdr + 4, 1);                  /* one level */
	    fwrite(hdr, 1, sizeof hdr, f);
	    fclose(f);
	    memset(&series, 0, sizeof series);
	    series.ruleset = Ruleset_None;
	    clearfileinfo(&series.mapfile);
	    if (!fileopen(&series.mapfile, scratchname, "rb", NULL)) {
		tw_skip("could not reopen the temporary .dat");
		remove(scratchname);
		break;
	    }
	    r = readseriesheader(&series);
	    fileclose(&series.mapfile, NULL);
	    remove(scratchname);
	    if (k == 0) {
		CHECK_MSG(!r, "a file signed 0xAAAD was accepted as a level set");
	    } else {
		CHECK_MSG(r, "a correctly signed MS .dat header was refused");
		CHECK_INT(series.ruleset, Ruleset_MS);
		CHECK_INT(series.count, 1);
	    }
	}
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

    tw_case("🔴 the level hash has a VALUE, a width, and covers the LAST byte");
    {
	/* 🔴 NOTHING IN THIS TREE PINNED WHAT hashvalue() RETURNS, and that is
	 * the gap these two constants close. Measured, each mutation applied and
	 * the whole file re-run: changing the final `^ 0xFFFFFFFFUL`, changing
	 * the initial `accum`, or changing ONE ENTRY of the remainders table all
	 * left the suite green. Both hash cases compared two COMPUTED hashes
	 * with each other, so any change that moves both survives, and series.c
	 * is compiled into no other unit test.
	 *
	 * That matters because this value is matched against res/unslist.txt,
	 * a text file of hashes shipped in the release. Move the algorithm and
	 * every entry in it silently stops matching -- the same quiet death of a
	 * feature that jc-59 fixed for LP64, and it was open on every platform.
	 *
	 * 🔴 THE WIDTH COMES FREE, AND ON EVERY PLATFORM. hashvalue() relies on
	 * `accum << 8` discarding what leaves the top of an `unsigned long`:
	 * true at 32 bits, false at 64, where the result carries 32 bits of
	 * intermediate state above the value meant. A value carrying bits above
	 * 31 cannot equal a 32-bit constant, so these assertions fail on an LP64
	 * build without jc-59's mask. An assertion on the WIDTH ALONE cannot do
	 * that -- it is a tautology wherever the defect is absent, so it reads
	 * `ok` on Windows for something nothing checked. A first draft of this
	 * case did exactly that, and named `linux-build` as an oracle; that job
	 * runs no unit test at all (it builds and runs `tworld2 -V`). The
	 * constants need no runner named.
	 *
	 * ⚠ Nothing caught the width defect because no test computed a hash and
	 * then matched one: unslist_test.c sets levelhash by hand through
	 * makegame(), and the case above compares two computed hashes. It lived
	 * in the seam between two test files. unslist_test.c now has a case that
	 * carries a real hash all the way through "%08lX" and back.
	 *
	 * 🔴 THE LAST BYTE. `j < size` decides how much of the record is hashed,
	 * and one byte short is invisible to a test comparing two levels that
	 * differ in many bytes -- as the case above does. Two buffers differing
	 * ONLY in their final byte are what tells those bounds apart.
	 *
	 * ⚠ THE THIRD CONSTANT PINS THE REMAINDERS TABLE, and it is here because
	 * the first two do not: six bytes consult six of the table's 256 entries,
	 * so corrupting any of the other 250 left this case green -- measured,
	 * changing `0x04C11DB7` (index 1) to `0x04C11DB6`.
	 *
	 * The LENGTH is the whole point and is not arbitrary. Each step indexes
	 * the table by `(accum >> 24) ^ data[j]`, so coverage depends on the
	 * running state as much as on the bytes; 0..255 repeated reaches 156 of
	 * 256 entries at 256 bytes, 219 at 512, 253 at 1,024 and **all 256 at
	 * 2,048**. Measured, not assumed. At that length one wrong entry
	 * anywhere in the table changes this hash, which is the only way to pin a
	 * 256-entry constant table from the outside without re-deriving it.
	 *
	 * hashvalue() is reachable directly because this test compiles series.c
	 * rather than linking it. The constants were derived from the algorithm,
	 * independently of the shipped code, and confirmed against it. */
	static unsigned char const a[] = { 'l', 'e', 'v', 'e', 'l', 0x01 };
	static unsigned char const b[] = { 'l', 'e', 'v', 'e', 'l', 0x02 };
	static unsigned char all[2048];
	unsigned long ha = hashvalue(a, (unsigned int)sizeof a);
	unsigned long hb = hashvalue(b, (unsigned int)sizeof b);
	unsigned long hall;
	int k;

	for (k = 0 ; k < (int)sizeof all ; ++k)
	    all[k] = (unsigned char)k;
	hall = hashvalue(all, (unsigned int)sizeof all);

	CHECK_MSG(ha == 0x4939747FUL,
		  "the level hash algorithm has changed: expected 0x4939747F,"
		  " got 0x%lX. Every entry in res/unslist.txt is matched by this"
		  " value and has just stopped matching."
		  " (A value wider than 32 bits means jc-59's mask is gone.)",
		  ha);
	CHECK_MSG(hb == 0x447A52A6UL,
		  "the level hash algorithm has changed: expected 0x447A52A6,"
		  " got 0x%lX", hb);
	CHECK_MSG(hall == 0xBABED19BUL,
		  "the level hash algorithm has changed: expected 0xBABED19B"
		  " over the 2,048-byte buffer, got 0x%lX. That buffer consults"
		  " every entry of the remainders table, so one corrupt entry"
		  " lands here", hall);
	CHECK_MSG(ha != hb,
		  "two records differing only in their LAST byte hashed"
		  " identically (0x%lX) -- the hash stops one byte short", ha);
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
