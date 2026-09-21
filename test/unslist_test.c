/* unslist_test.c: the unsolvable-levels list.
 *
 * MOD (Jeremy). unslist.c is 288 lines that ship in every release, run at every
 * startup, and had NO test of any kind.
 *
 * 🔴 AND THE REPOSITORY HAD TWICE WRITTEN DOWN THAT IT WAS DEAD CODE. FORK.md
 * first claimed it was "exercised by the corpus run"; that was corrected, and
 * the CORRECTION claimed the opposite -- that the `unsolvablelist` resource "is
 * set neither in res/rc nor in initresourcedefaults()", so the loader never
 * runs. Both were wrong, and the second one is the interesting failure:
 *
 *   res/rc line 6 says   UnsolvableList=unslist.txt
 *   res.c rclist[] says  "unsolvablelist"
 *
 * A grep for the table's spelling finds nothing in res/rc, which reads exactly
 * like proof of absence. It is not: readrcfile() LOWERCASES the key before
 * comparing (res.c:323), so the two match and the file is loaded. Absence of a
 * grep hit is not absence of a caller -- follow the call.
 *
 * So this file is live: res/unslist.txt is read at startup, and series.c:404
 * calls markunsolvablelevels() for every series that loads.
 *
 * WHAT IT DOES. It reads a text file of levels known to be unsolvable -- levels
 * that shipped broken in real level packs -- and marks them so the program can
 * tell a player "this one cannot be completed" instead of letting them grind at
 * it. Matching is by (level number, level size, level hash), never by name, so
 * that a renamed or repackaged set still resolves.
 *
 * WHY IT DESERVES A TEST BEYOND COVERAGE. It is a PARSER OF A SHIPPED TEXT
 * FILE, and this project's most consistent lesson is that the parser with no
 * test is the parser with the defects: jc-48 was found by writing the .dac
 * parser's first test, jc-51 by covering an engine path nothing had touched.
 * Everything below drives the real readunslist() over real bytes.
 *
 * TESTLANG: c
 *
 * unslist.c and fileio.c are compiled only as C by CMake, and rely on C's
 * implicit void* conversion through err.h's x_alloc. See docs/adr/0004.
 */

#include	"tw_test.h"

/* The two directory globals unslist.c's loadunslistfromfile() searches. They
 * live in res.c and tworld.c fills them in at startup; res.c is not compiled in
 * here because it would drag the whole resource and tileset surface along for
 * two pointers.
 *
 * ⚠ Both are left NULL, and that is a deliberate limit on what this file tests:
 * the cases below drive readunslist() directly over bytes, never
 * loadunslistfromfile(), so the directory search is NOT covered here. Setting
 * them to something plausible would look like coverage without adding any. */
char	       *resdir = NULL;
char	       *savedir = NULL;

#include	"../fileio.c"
#include	"../unslist.c"

/* --- the error surface, stubbed ---------------------------------------- *
 *
 * warn_ counts rather than prints. readunslist() warns on every malformed line
 * and keeps going, so the count is the oracle for the "flagged but not fatal"
 * behavior several cases below assert. */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;
static int	errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *pfx, char const *fmt, ...)
{
    (void)pfx; (void)fmt; ++errmsg_count;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* --- the harness -------------------------------------------------------- */

static char const *scratchname = "tw_unslist_test.txt";

/* Run the real readunslist() over a block of text.
 *
 * ⚠ clearunslist() first, every time. The list, the set-name table and the
 * string pool are all file-scope in unslist.c, so without the reset each case
 * would inherit whatever the previous one loaded -- and a case that only passes
 * because an earlier case left something behind is the kind that survives the
 * defect it was written for. */
static int loadtext(char const *text)
{
    fileinfo	file;
    FILE       *f;
    int		r;

    clearunslist();

    f = fopen(scratchname, "wb");
    if (!f)
	return -1;
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    clearfileinfo(&file);
    if (!fileopen(&file, scratchname, "r", NULL))
	return -1;
    warn_count = 0;
    errmsg_count = 0;
    r = readunslist(&file);
    fileclose(&file, NULL);
    remove(scratchname);
    return r;
}

/* A gamesetup carrying just the three fields islevelunsolvable() matches on. */
static gamesetup makegame(int number, int size, unsigned long hash)
{
    gamesetup g;
    memset(&g, 0, sizeof g);
    g.number = number;
    g.levelsize = size;
    g.levelhash = hash;
    return g;
}

/* --- the format --------------------------------------------------------- */

static void test_basicparse(void)
{
    gamesetup	g;
    char	note[256];

    tw_case("a level listed under a set is found by number, size and hash");
    loadtext("[SomeSet.dat]\n"
	     "246: 01B1 87F61A80: no exit\n");
    g = makegame(246, 0x01B1, 0x87F61A80UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);

    tw_case("the annotation is copied out when asked for");
    note[0] = '\0';
    CHECK_INT(islevelunsolvable(&g, note), TRUE);
    CHECK_STR(note, "no exit");

    tw_case("🔴 all THREE of number, size and hash must match");
    /* Matching on the number alone would mark the wrong level in any set that
     * happens to share a number -- which is every set. The size and hash are
     * what make the entry survive a repackaged .dat, and what stop it firing on
     * an unrelated level. Each of these differs from the entry in exactly one
     * field. */
    g = makegame(247, 0x01B1, 0x87F61A80UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE, "matched on a wrong number");
    g = makegame(246, 0x01B2, 0x87F61A80UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE, "matched on a wrong size");
    g = makegame(246, 0x01B1, 0x87F61A81UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE, "matched on a wrong hash");

    tw_case("an entry with no annotation still matches");
    loadtext("[SomeSet.dat]\n"
	     "12: 0100 DEADBEEF\n");
    g = makegame(12, 0x0100, 0xDEADBEEFUL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);

    tw_case("several sets and several levels coexist");
    loadtext("[A.dat]\n"
	     "1: 0010 00000001: first\n"
	     "2: 0020 00000002: second\n"
	     "[B.dat]\n"
	     "1: 0030 00000003: third\n");
    g = makegame(1, 0x0010, 1UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);
    g = makegame(2, 0x0020, 2UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);
    g = makegame(1, 0x0030, 3UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);
    g = makegame(2, 0x0030, 3UL);
    CHECK_INT(islevelunsolvable(&g, NULL), FALSE);
}

/* --- "ok": taking an entry back out ------------------------------------- */

static void test_okremoval(void)
{
    gamesetup g;

    tw_case("an `ok` line removes a level listed earlier");
    /* The file is additive, and `ok` is how a level later found to be solvable
     * gets retracted without editing the original line out -- which matters for
     * a file that is maintained by hand over years. */
    loadtext("[A.dat]\n"
	     "5: 0050 00000005: broken\n"
	     "5: ok\n");
    g = makegame(5, 0x0050, 5UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE,
	      "an `ok` line did not retract the entry above it");

    tw_case("`ok` only retracts within its own set");
    loadtext("[A.dat]\n"
	     "5: 0050 00000005: broken\n"
	     "[B.dat]\n"
	     "5: ok\n");
    g = makegame(5, 0x0050, 5UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == TRUE,
	      "`ok` under set B retracted an entry belonging to set A");

    tw_case("an `ok` for a level that was never listed is harmless");
    loadtext("[A.dat]\n"
	     "9: ok\n");
    CHECK_INT(warn_count, 0);
}

/* --- malformed input ----------------------------------------------------- */

static void test_malformed(void)
{
    gamesetup g;

    tw_case("a syntax error is flagged and the rest of the file still loads");
    /* "Errors in the file are flagged but do not prevent the function from
     * reading the rest of the file" -- the function's own contract. A shipped
     * text file with one bad line must not silently lose everything after it. */
    loadtext("[A.dat]\n"
	     "this is not a valid line\n"
	     "7: 0070 00000007: fine\n");
    CHECK_MSG(warn_count == 1, "expected one warning, got %d", warn_count);
    g = makegame(7, 0x0070, 7UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == TRUE,
	      "the line after a syntax error was dropped");

    tw_case("an entry before any set header is refused");
    /* setid is zero until a [Set] line appears, and the guard requires it.
     * Without that, a stray line at the top of the file would attach to set
     * zero and match against every set at once. */
    loadtext("3: 0030 00000003: orphan\n");
    g = makegame(3, 0x0030, 3UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE,
	      "an entry with no set header was accepted");
    CHECK_MSG(warn_count == 1, "expected one warning, got %d", warn_count);

    tw_case("level numbers outside 1..65535 are refused");
    loadtext("[A.dat]\n"
	     "0: 0010 00000001: zero\n"
	     "65536: 0010 00000002: too big\n"
	     "-1: 0010 00000003: negative\n");
    CHECK_MSG(warn_count == 3, "expected three warnings, got %d", warn_count);
    g = makegame(0, 0x0010, 1UL);
    CHECK_INT(islevelunsolvable(&g, NULL), FALSE);

    tw_case("🔴 ...and 1 and 65535, the numbers just INSIDE, are accepted");
    /* The case above only ever drives the refused side, so tightening the
     * range to `levelnum < 65535` threw away a legitimate entry with the whole
     * suite green. The two ends of an accepted range need pinning as much as
     * the two just outside it: a level quietly missing from the unsolvable
     * list is a level Tile World offers as playable when it is not. */
    loadtext("[A.dat]\n"
	     "1: 0010 00000001: the first\n"
	     "65535: 0020 00000002: the last\n");
    CHECK_MSG(warn_count == 0, "expected no warnings, got %d", warn_count);
    g = makegame(1, 0x0010, 1UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == TRUE, "level 1 was refused");
    g = makegame(65535, 0x0020, 2UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == TRUE, "level 65535 was refused");

    tw_case("🔴 a truncated entry is a syntax error, not a half-read one");
    /* `n >= 3` relaxed to `n >= 2` accepts a line carrying a level number and
     * a size but NO HASH, and calls addtounslist() with `hashval` never
     * assigned -- an entry whose matching depends on a stack value. The file
     * had no line of that shape, so the bound was carried by nothing.
     *
     * The bare `5:` below is the other half: a line the first sscanf matches
     * only a number from. The shipped answer to both is "syntax error". */
    loadtext("[ok]\n"
	     "5: 0050 00000005: still listed\n"
	     "6: 0060\n"
	     "5: \n");
    CHECK_MSG(warn_count == 2, "expected two warnings, got %d", warn_count);
    g = makegame(5, 0x0050, 5UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == TRUE,
	      "a malformed line silently retracted level 5");
    g = makegame(6, 0x0060, 0UL);
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE,
	      "an entry with no hash was accepted");

    /* ⚠ BOTH `n > 0` GUARDS ARE EQUIVALENT MUTANTS UNDER `n > -1`, and this
     * note is here because the first draft of this case claimed otherwise and
     * was measured wrong. Do not spend the afternoon on them.
     *
     * The OUTER one (`n > 0 && levelnum > 0 && ...`): when the first sscanf
     * assigns nothing, neither `n == 1` nor `n >= 3` holds either, so control
     * reaches the same warn() by a longer route. Only `levelnum` is read
     * uninitialized on the way, which no assertion can observe.
     *
     * The INNER one (`n > 0 && !strcmp(token, "ok")`): the draft reasoned that
     * `n > -1` would reach strcmp() with a stale `token`, and built the file
     * above -- a set literally named `ok`, so the set-name sscanf leaves "ok"
     * in that shared buffer -- to make a bare `5:` retract level 5. It does
     * not, and cannot: `sscanf(p, "%*d: %s", token)` can only return 1 or EOF.
     * A suppressed conversion is not counted, so running out of input yields
     * EOF (-1), never 0, and `%s` cannot fail on input it has. `n > -1` is
     * false for -1 exactly as `n > 0` is. There is no distinguishing input.
     * (The file is kept as written; it earns its place on the `n >= 3` bound
     * above, and the `ok` set name costs nothing.) */

    tw_case("comments and blank lines are skipped without complaint");
    loadtext("# a comment\n"
	     "\n"
	     "   \n"
	     "   # an indented comment\n"
	     "[A.dat]\n"
	     "4: 0040 00000004: real\n");
    CHECK_INT(warn_count, 0);
    g = makegame(4, 0x0040, 4UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);

    tw_case("an empty file is not an error");
    CHECK_INT(loadtext(""), TRUE);
    CHECK_INT(warn_count, 0);

    tw_case("a CRLF file parses the same as an LF one");
    /* ⚠ Not hypothetical for a shipped text file people edit on Windows.
     * readunslist() scans the note with %[^\n\r], and dropping the \r from that
     * set would put a carriage return on the end of every annotation. */
    loadtext("[A.dat]\r\n"
	     "8: 0080 00000008: has a note\r\n");
    {
	char note[256];
	g = makegame(8, 0x0080, 8UL);
	CHECK_INT(islevelunsolvable(&g, note), TRUE);
	CHECK_STR(note, "has a note");
    }
}

/* --- markunsolvablelevels ------------------------------------------------- */

static void test_markseries(void)
{
    gameseries	series;
    gamesetup	games[3];

    memset(&series, 0, sizeof series);
    memset(games, 0, sizeof games);
    series.games = games;
    series.count = 3;
    games[0] = makegame(1, 0x0010, 1UL);
    games[1] = makegame(2, 0x0020, 2UL);
    games[2] = makegame(3, 0x0030, 3UL);

    tw_case("a series is marked by name, and only the listed levels are");
    loadtext("[MySet.dat]\n"
	     "1: 0010 00000001: broken one\n"
	     "3: 0030 00000003: broken three\n");
    strcpy(series.name, "MySet.dat");
    CHECK_INT(markunsolvablelevels(&series), 2);
    CHECK_MSG(games[0].unsolvable != NULL, "level 1 was not marked");
    CHECK_MSG(games[1].unsolvable == NULL, "level 2 was marked and should not be");
    CHECK_MSG(games[2].unsolvable != NULL, "level 3 was not marked");
    if (games[0].unsolvable)
	CHECK_STR(games[0].unsolvable, "broken one");

    tw_case("a series whose name is not in the list marks nothing");
    /* ⚠ And it must still CLEAR the field first. The loop that blanks every
     * game runs before the name lookup returns; if it did not, a second series
     * would inherit the first one's marks. */
    strcpy(series.name, "NotListed.dat");
    CHECK_INT(markunsolvablelevels(&series), 0);
    CHECK_MSG(games[0].unsolvable == NULL,
	      "a mark survived a lookup that found no such set");

    tw_case("🔴 a hash written the way the FILE writes it still matches");
    {
	/* 🔴 THIS CASE EXISTS BECAUSE ITS ABSENCE WAS A SHIPPED DEFECT. Every
	 * other case in this file builds its gamesetup with makegame(), which
	 * assigns levelhash by hand -- so nothing here ever exercised the value
	 * series.c actually produces, and series_test.c only compared two
	 * computed hashes with each other. The round trip
	 *
	 *     hashvalue()  ->  "%08lX" in the file  ->  sscanf  ->  ==
	 *
	 * was covered at neither end, and jc-59 was a defect that lived exactly
	 * there: on any build where `long` is 64 bits the hash came back wider
	 * than the field, nothing matched, and the unsolvable-levels list
	 * silently did nothing. Six layers and two Linux CI jobs missed it.
	 *
	 * So this case takes a hash the way the program computes one, formats it
	 * the way the shipped res/unslist.txt carries it, and requires the
	 * lookup to find it. It guards the CLASS -- a future change to the
	 * format string, to levelhash's type, or to the hash's width fails here
	 * -- and unlike an assertion on the width, it is live on every platform.
	 *
	 * ⚠ THE CHAIN IS DELIBERATELY TWO CASES IN TWO FILES, because no single
	 * translation unit holds both halves: hashvalue() is static in series.c,
	 * which this test does not compile, and islevelunsolvable() is
	 * unslist.c's, which series_test.c stubs out. That split is the seam
	 * itself, and it cannot be closed by moving one case. So they are linked
	 * by a shared literal instead:
	 *
	 *   series_test.c  "the level hash has a VALUE"   hashvalue(...) == this
	 *   here                                          this survives the file
	 *
	 * Change the algorithm and the first fails; change the format string or
	 * the field's width and this one does. Keep the two constants equal. */
	unsigned long const knownhash = 0x4939747FUL;	/* see series_test.c */
	gamesetup hashed;
	char line[128];

	sprintf(line, "[H.dat]\n7: 00AB %08lX: the hash round trip\n", knownhash);
	loadtext(line);
	CHECK_MSG(warn_count == 0, "expected no warnings, got %d", warn_count);

	hashed = makegame(7, 0x00AB, knownhash);
	CHECK_MSG(islevelunsolvable(&hashed, NULL) == TRUE,
		  "a hash formatted with %%08lX did not match the value it was"
		  " formatted from (0x%lX) -- the unsolvable list is dead",
		  knownhash);
    }

    tw_case("🔴 the clear covers the LAST level too, not count - 1 of them");
    /* The case above proves the blanking loop runs; it reads games[0], so
     * stopping the loop one short leaves it green. games[count - 1] is the one
     * that tells the bounds apart, and a stale mark there is a level Tile World
     * keeps calling unsolvable after the list stopped saying so.
     *
     * ⚠ The other direction writes games[count], one past the array, and is
     * the Linux ASan job's to catch -- writing NULL into whatever follows has
     * no consequence any assertion here can name. Same for the two reading
     * loops below. */
    strcpy(series.name, "MySet.dat");
    loadtext("[MySet.dat]\n"
	     "3: 0030 00000003: broken three\n");
    CHECK_INT(markunsolvablelevels(&series), 1);
    CHECK_MSG(games[2].unsolvable != NULL, "the last level was not marked");
    loadtext("[MySet.dat]\n"
	     "1: 0010 00000001: broken one\n");
    CHECK_INT(markunsolvablelevels(&series), 1);
    CHECK_MSG(games[2].unsolvable == NULL,
	      "the last level kept a mark the list no longer carries");

    tw_case("🔴 an identical level listed under a DIFFERENT set is not marked");
    /* ⚠ THIS CASE EXISTS BECAUSE ITS ABSENCE WAS MEASURED. The first version of
     * this file loaded a list containing only one set, so deleting the
     * `unslist[i].setid != setid` guard in markunsolvablelevels() changed
     * nothing and the suite stayed green -- the case was vacuous against the
     * one mutation it most needed to catch.
     *
     * The list below carries the SAME number, size and hash under two names.
     * Only the entry belonging to the series being marked may fire; without the
     * set-id guard both would, and every set sharing a level's bytes with any
     * listed set would be marked unsolvable. */
    loadtext("[Other.dat]\n"
	     "1: 0010 00000001: belongs to another set entirely\n"
	     "[MySet.dat]\n"
	     "2: 0020 00000002: belongs here\n");
    strcpy(series.name, "MySet.dat");
    CHECK_INT(markunsolvablelevels(&series), 1);
    CHECK_MSG(games[0].unsolvable == NULL,
	      "level 1 was marked from an entry under Other.dat");
    CHECK_MSG(games[1].unsolvable != NULL, "level 2 was not marked");

    tw_case("a listed set whose levels have different bytes marks nothing");
    /* The set name matched, so this is the case that proves the size and hash
     * are re-checked per level rather than trusted from the header. */
    loadtext("[MySet.dat]\n"
	     "1: 9999 12345678: a different build of level 1\n");
    strcpy(series.name, "MySet.dat");
    CHECK_INT(markunsolvablelevels(&series), 0);
    CHECK_MSG(games[0].unsolvable == NULL, "level 1 matched on the name alone");
}

/* --- the shipped file itself --------------------------------------------- */

static void test_shippedfile(void)
{
    fileinfo	file;

    tw_case("res/unslist.txt -- the file that actually ships -- parses cleanly");
    /* 🔴 THE POINT OF THIS CASE. Everything above drives synthesized text. This
     * one drives the real resource, from the repository, through the real
     * parser -- so a hand-edit to res/unslist.txt that introduces a malformed
     * line is caught here rather than by a warning nobody reads at startup.
     *
     * The path is relative to the repository root because the unit runner
     * compiles and runs from there. */
    clearunslist();
    clearfileinfo(&file);
    if (!fileopen(&file, "res/unslist.txt", "r", NULL)) {
	tw_skip("res/unslist.txt not readable from the working directory");
	return;
    }
    warn_count = 0;
    CHECK_INT(readunslist(&file), TRUE);
    fileclose(&file, NULL);
    CHECK_MSG(warn_count == 0,
	      "res/unslist.txt has %d malformed line(s)", warn_count);
    CHECK_MSG(listcount > 0,
	      "res/unslist.txt parsed to %d entries; it is not empty",
	      listcount);
}

/* --- clearunslist -------------------------------------------------------- */

static void test_clear(void)
{
    gamesetup g;

    tw_case("clearing the list forgets everything, and reloading works");
    loadtext("[A.dat]\n"
	     "1: 0010 00000001: gone after the clear\n");
    g = makegame(1, 0x0010, 1UL);
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);
    clearunslist();
    CHECK_MSG(islevelunsolvable(&g, NULL) == FALSE,
	      "an entry survived clearunslist()");
    CHECK_INT(listcount, 0);

    /* Reloading after a clear is what res.c does on every ruleset change, so a
     * clear that left the allocator in a bad state would show up there and
     * nowhere else. */
    loadtext("[A.dat]\n"
	     "1: 0010 00000001: back again\n");
    CHECK_INT(islevelunsolvable(&g, NULL), TRUE);
}

int main(void)
{
    tw_begin("unslist_test.c");

    test_basicparse();
    test_okremoval();
    test_malformed();
    test_markseries();
    test_shippedfile();
    test_clear();

    clearunslist();

    /* ⭐ THIS FILE'S OFF QUEUE IS WORKED OUT, and the remainder is written down
     * so the next reader does not re-derive it. Of unslist.c's eleven REACHED
     * offset survivors, three now die -- the level-number range's accepted end
     * (`< 65536`), the `n >= 3` field count, and the blanking loop's last
     * element -- and the other eight are not actionable, each for a reason
     * proven rather than assumed:
     *
     *   :69  -1, :100 -1   the pool and table grow ONE ALLOCATION EARLY. Both
     *                      sites then double, so every later index is in
     *                      bounds and no caller can tell. Equivalent.
     *   :174 col 10, :177  the two `n > 0` sscanf guards. See the note in "a
     *                      truncated entry is a syntax error" above -- neither
     *                      has a distinguishing input, and the second one was
     *                      claimed killable here before it was measured.
     *   :69  +1            one byte past the string pool: a heap overflow with
     *                      no behavioral consequence. ASan, on the Linux job.
     *   :225 +1, :232 +1,  one element past the games array, the list, and the
     *   :235 +1            games array again. Same class, same oracle.
     *
     * Four of those eight are the shape CLAUDE.md section 5 records for res.c:
     * a plain-pass census will report them forever, and that is the census
     * measuring one layer of six rather than a hole. */

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(57);
    return tw_end();
}
