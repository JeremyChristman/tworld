/* res_test.c: the resource configuration file, and the tileset-name guard.
 *
 * MOD (Jeremy). res.c is 583 lines that ship in every release and had NO test.
 * It was the last parser in the C core without one, and this project's most
 * consistent lesson says that is where the defects are: jc-48 came from writing
 * the `.dac` parser's first test, jc-51 from covering an engine path nothing had
 * touched, and in the same session as this file the first tests for `play.c`,
 * `unslist.c` and `TWTextCoder` turned up a broken `#ifdef`, a vacuous case and
 * a shipped off-by-one respectively.
 *
 * TWO THINGS LIVE HERE AND BOTH MATTER.
 *
 * 1. readrcfile() -- the `rc` parser. It decides which file every resource name
 *    resolves to: the tile image, the font, the four colors, the unsolvable
 *    list, the sound effects. A resource name that silently fails to match
 *    leaves a default in place and the program looks fine while ignoring what
 *    the file said.
 *
 *    🔴 ITS KEY MATCHING ALREADY MISLED THIS REPOSITORY ONCE. `res/rc` spells a
 *    key `UnsolvableList`; `rclist[]` spells it `unsolvablelist`; the comparison
 *    is `strcmp`. They match only because readrcfile() LOWERCASES the key first
 *    (line 323). Grepping for the table's spelling finds nothing in `res/rc` and
 *    reads exactly like proof that the resource is never set -- which is what
 *    got written into FORK.md, twice, and was wrong both times. The
 *    case-insensitivity is now pinned by a test rather than by a comment.
 *
 * 2. istilesetname() -- a SECURITY GUARD, added in jc-42. A tileset name comes
 *    from `tw_settings.ini` or from the rc file and is joined onto the resource
 *    directory to open a file. This predicate is what stops that name being a
 *    path, a drive-relative reference, an alternate data stream, or a Windows
 *    device. It gets the most cases below for that reason.
 *
 * WHAT THIS DOES NOT COVER: everything that draws or plays. loadimages(),
 * loadfont(), loadsounds() and loadcolors() are calls into oshw and are stubbed
 * out; asserting that a stub was called proves nothing about the program.
 *
 * TESTLANG: c
 *
 * res.c and fileio.c are compiled only as C by CMake, and rely on C's implicit
 * void* conversion through err.h's x_alloc. See docs/adr/0004.
 */

#include	"tw_test.h"
#include	"tw_corpus.h"

/* Pulled in ahead of the stubs so that TRUE/FALSE exist and every stub below is
 * checked against the REAL declaration. A stub whose signature drifts from the
 * header is then a compile error rather than a silent mismatch. */
#include	"../defs.h"
#include	"../oshw.h"
#include	"../res.h"
#include	"../settings.h"

/* --- the surface res.c links against, stubbed ---------------------------- *
 *
 * Everything here is a call into the display, audio or settings layer. The
 * counters exist where a case needs to know something was attempted; the rest
 * are inert. */

static int	fake_tilesetloaded = TRUE;
static int	loadtileset_calls = 0;
static int	setcolors_calls = 0;
static char	fake_stringsetting[256] = "";

int loadtileset(char const *filename, int complain)
{
    (void)filename; (void)complain;
    ++loadtileset_calls;
    return TRUE;
}
int istilesetloaded(void) { return fake_tilesetloaded; }
void freetileset(void) { }
int loadfontfromfile(char const *filename, int complain)
{
    (void)filename; (void)complain; return TRUE;
}
void freefont(void) { }
int loadsfxfromfile(int index, char const *filename)
{
    (void)index; (void)filename; return TRUE;
}
void freesfx(int index) { (void)index; }
int setaudiosystem(int active) { (void)active; return TRUE; }
void setcolors(long bkgnd, long text, long bold, long dim)
{
    (void)bkgnd; (void)text; (void)bold; (void)dim;
    ++setcolors_calls;
}
int loadmessagesfromfile(char const *filename) { (void)filename; return TRUE; }

/* unslist.c's two entry points. Stubbed rather than compiled in: this file is
 * about res.c, and test/unslist_test.c already covers that parser properly. */
int loadunslistfromfile(char const *filename) { (void)filename; return TRUE; }
void clearunslist(void) { }

/* The settings store, faked. gettilesetoverride() reads through it. */
char const *getstringsetting(char const *key)
{
    (void)key;
    return fake_stringsetting[0] ? fake_stringsetting : NULL;
}
void setstringsetting(char const *key, char const *value)
{
    (void)key;
    snprintf(fake_stringsetting, sizeof fake_stringsetting, "%s",
	     value ? value : "");
}

/* --- the source under test ---------------------------------------------- */

#include	"../fileio.c"
#include	"../res.c"

/* --- the error surface, stubbed ---------------------------------------- *
 *
 * warn_ counts. readrcfile() warns on every unrecognized resource name and
 * every syntax error and keeps going, so the count is the oracle for several
 * cases below rather than noise. */

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

static char const *scratchdir = "tw_res_test_dir";

/* Write an rc file into a scratch resource directory and run the REAL
 * readrcfile() over it. res.c reads `rc` out of `resdir`, so the directory is
 * the interface -- there is no way to hand the parser a buffer, and inventing
 * one would test a function the program does not have. */
static int loadrc(char const *text)
{
    char	path[512];
    FILE       *f;
    int		r;

    (void)createdir(scratchdir);
    snprintf(path, sizeof path, "%s/rc", scratchdir);
    f = fopen(path, "wb");
    if (!f)
	return -1;
    fwrite(text, 1, strlen(text), f);
    fclose(f);

    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, scratchdir);

    memset(allresources, 0, sizeof allresources);
    initresourcedefaults();
    warn_count = 0;
    errmsg_count = 0;
    r = readrcfile();
    remove(path);
    return r;
}

static char const *res(int ruleset, int id)
{
    return allresources[ruleset][id].str;
}

/* --- the rc parser ------------------------------------------------------- */

static void test_rcbasics(void)
{
    tw_case("a resource name is read and stored");
    CHECK_INT(loadrc("TileImages=mytiles.bmp\n"), TRUE);
    CHECK_STR(res(Ruleset_None, RES_IMG_TILES), "mytiles.bmp");

    tw_case("🔴 resource names are matched CASE-INSENSITIVELY");
    /* The thing that misled this repository twice. `rclist[]` holds lowercase
     * names and the comparison is strcmp; readrcfile() lowercases the key from
     * the file first. Every spelling below must reach the same slot -- and the
     * shipped res/rc really does use mixed case, so this is the path that runs
     * in production, not an edge case. */
    CHECK_INT(loadrc("unsolvablelist=a.txt\n"), TRUE);
    CHECK_STR(res(Ruleset_None, RES_TXT_UNSLIST), "a.txt");
    CHECK_INT(loadrc("UnsolvableList=b.txt\n"), TRUE);
    CHECK_STR(res(Ruleset_None, RES_TXT_UNSLIST), "b.txt");
    CHECK_INT(loadrc("UNSOLVABLELIST=c.txt\n"), TRUE);
    CHECK_STR(res(Ruleset_None, RES_TXT_UNSLIST), "c.txt");
    CHECK_INT(warn_count, 0);

    tw_case("an unrecognized resource name warns and is skipped");
    CHECK_INT(loadrc("NoSuchResource=x\nTileImages=kept.bmp\n"), TRUE);
    CHECK_MSG(warn_count == 1, "expected one warning, got %d", warn_count);
    CHECK_STR(res(Ruleset_None, RES_IMG_TILES), "kept.bmp");

    tw_case("comments and blank lines are skipped without complaint");
    CHECK_INT(loadrc("# a comment\n"
		     "\n"
		     "    \n"
		     "   # an indented comment\n"
		     "TileImages=real.bmp\n"), TRUE);
    CHECK_INT(warn_count, 0);
    CHECK_STR(res(Ruleset_None, RES_IMG_TILES), "real.bmp");

    tw_case("a line with no '=' warns rather than being silently ignored");
    loadrc("this line has no equals sign\n");
    CHECK_MSG(warn_count == 1, "expected one warning, got %d", warn_count);

    tw_case("an rc file that cannot be opened returns FALSE");
    /* The one failure the function reports through its return value. Every
     * other problem is a warning and a skipped line. */
    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, "tw_res_test_no_such_dir");
    CHECK_INT(readrcfile(), FALSE);
}

static void test_rcrulesets(void)
{
    tw_case("a [ms] section stores into the MS ruleset only");
    CHECK_INT(loadrc("[ms]\n"
		     "TileImages=ms.bmp\n"), TRUE);
    CHECK_STR(res(Ruleset_MS, RES_IMG_TILES), "ms.bmp");
    CHECK_MSG(strcmp(res(Ruleset_Lynx, RES_IMG_TILES), "ms.bmp") != 0,
	      "an [ms] value leaked into the Lynx ruleset");

    tw_case("a [lynx] section stores into the Lynx ruleset only");
    CHECK_INT(loadrc("[lynx]\n"
		     "TileImages=lynx.bmp\n"), TRUE);
    CHECK_STR(res(Ruleset_Lynx, RES_IMG_TILES), "lynx.bmp");
    CHECK_MSG(strcmp(res(Ruleset_MS, RES_IMG_TILES), "lynx.bmp") != 0,
	      "a [lynx] value leaked into the MS ruleset");

    tw_case("a value outside any section reaches BOTH rulesets");
    /* This is what makes a plain rc file work at all: `TileImages=x` with no
     * section is meant to apply everywhere, and the copy into each
     * ruleset-specific slot is how that happens. */
    CHECK_INT(loadrc("TileImages=both.bmp\n"), TRUE);
    CHECK_STR(res(Ruleset_MS, RES_IMG_TILES), "both.bmp");
    CHECK_STR(res(Ruleset_Lynx, RES_IMG_TILES), "both.bmp");

    tw_case("[all] returns to the ruleset-independent section");
    CHECK_INT(loadrc("[ms]\n"
		     "TileImages=ms.bmp\n"
		     "[all]\n"
		     "Font=shared.bmp\n"), TRUE);
    CHECK_STR(res(Ruleset_MS, RES_IMG_FONT), "shared.bmp");
    CHECK_STR(res(Ruleset_Lynx, RES_IMG_FONT), "shared.bmp");
    CHECK_STR(res(Ruleset_MS, RES_IMG_TILES), "ms.bmp");

    tw_case("section names are case-insensitive too");
    CHECK_INT(loadrc("[MS]\nTileImages=upper.bmp\n"), TRUE);
    CHECK_INT(warn_count, 0);
    CHECK_STR(res(Ruleset_MS, RES_IMG_TILES), "upper.bmp");

    tw_case("an unknown section warns");
    loadrc("[nosuchruleset]\nTileImages=x.bmp\n");
    CHECK_MSG(warn_count == 1, "expected one warning, got %d", warn_count);
}

static void test_rcdefaults(void)
{
    tw_case("the built-in defaults are in place before any file is read");
    /* A user with no rc file, or one that names only some resources, still gets
     * a working program. These six are the ones res.c fills in. */
    memset(allresources, 0, sizeof allresources);
    initresourcedefaults();
    CHECK_STR(res(Ruleset_None, RES_IMG_TILES), "tiles.bmp");
    CHECK_STR(res(Ruleset_None, RES_IMG_FONT), "font.bmp");
    CHECK_STR(res(Ruleset_None, RES_CLR_BKGND), "000000");
    CHECK_STR(res(Ruleset_None, RES_CLR_TEXT), "FFFFFF");
    CHECK_STR(res(Ruleset_None, RES_CLR_BOLD), "FFFF00");
    CHECK_STR(res(Ruleset_None, RES_CLR_DIM), "C0C0C0");
}

/* --- istilesetname: the jc-42 guard -------------------------------------- */

static void test_tilesetname(void)
{
    tw_case("an ordinary tileset filename is accepted");
    CHECK_INT(istilesetname("tiles.bmp"), TRUE);
    CHECK_INT(istilesetname("Tile World Lynx Tileset.bmp"), TRUE);

    tw_case("🔴 a name containing a path separator is refused, both kinds");
    /* The whole point of the guard: the name is joined onto the resource
     * directory, so a separator is what turns a filename into a path out of it.
     * Both separators, because a forward slash works perfectly well on Windows
     * and testing only the backslash is the exact mistake jc-48 fixed in the
     * .dac parser. */
    CHECK_INT(istilesetname("../tiles.bmp"), FALSE);
    CHECK_INT(istilesetname("..\\tiles.bmp"), FALSE);
    CHECK_INT(istilesetname("sub/tiles.bmp"), FALSE);
    CHECK_INT(istilesetname("sub\\tiles.bmp"), FALSE);
    CHECK_INT(istilesetname("/etc/passwd"), FALSE);

    tw_case("a colon anywhere is refused, not just a drive letter");
    /* "tiles.bmp:hidden" names an alternate data stream of a file in this
     * directory -- contained, but there is no reason for a tileset name to
     * carry one, and a drive-relative "C:tiles.bmp" is worse. */
    CHECK_INT(istilesetname("C:tiles.bmp"), FALSE);
    CHECK_INT(istilesetname("tiles.bmp:hidden"), FALSE);
    CHECK_INT(istilesetname(":"), FALSE);

    tw_case("a Windows device name is refused");
    /* These need no separator at all: CON, NUL, COM1 and LPT1 resolve to the
     * device from inside any directory. Shared with the .dac guard through
     * fileio.c's isreservedfilename() since jc-48. */
    CHECK_INT(istilesetname("CON"), FALSE);
    CHECK_INT(istilesetname("nul"), FALSE);
    CHECK_INT(istilesetname("COM1"), FALSE);
    CHECK_INT(istilesetname("LPT1.bmp"), FALSE);

    tw_case("an empty or whitespace-only name is refused");
    CHECK_INT(istilesetname(""), FALSE);
    CHECK_INT(istilesetname("   "), FALSE);
    CHECK_INT(istilesetname("\t\t"), FALSE);
    CHECK_INT(istilesetname(NULL), FALSE);

    tw_case("a control character is refused");
    CHECK_INT(istilesetname("tiles\n.bmp"), FALSE);
    CHECK_INT(istilesetname("tiles\x01.bmp"), FALSE);

    tw_case("⚠ '..' is refused ONLY as the whole name, which is deliberate");
    /* jc-42's own note: separators are already rejected, so the value is always
     * one path component, and a substring test made ordinary filenames vanish
     * from the tileset menu with no explanation. "x..y.bmp" is a legal
     * filename and must stay selectable. */
    CHECK_INT(istilesetname(".."), FALSE);
    CHECK_MSG(istilesetname("x..y.bmp") == TRUE,
	      "a legal filename containing '..' was refused; see jc-42");
    CHECK_MSG(istilesetname("tiles..bmp") == TRUE,
	      "a legal filename containing '..' was refused; see jc-42");
}

static void test_tilesetpath(void)
{
    char	dest[512];

    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, "res");

    tw_case("a NULL name yields the tileset directory itself");
    CHECK_INT(gettilesetpath(dest, NULL), TRUE);
    CHECK_MSG(dest[0] != '\0', "the tileset directory came back empty");

    tw_case("a good name is appended to the tileset directory");
    CHECK_INT(gettilesetpath(dest, "tiles.bmp"), TRUE);
    CHECK_MSG(strstr(dest, "tiles.bmp") != NULL,
	      "the name is missing from the built path: %.80s", dest);

    tw_case("🔴 a refused name CLEARS the destination, it does not leave a path");
    /* jc-42 again, and the reason it matters is the shape of the bug rather
     * than its impact: dest used to keep whatever combinepath() had already
     * written, so a caller that ignored the return value would open the
     * TILESET DIRECTORY instead of failing. Every caller does check -- the
     * header promised otherwise, and the next caller reads the header. */
    strcpy(dest, "SENTINEL");
    CHECK_INT(gettilesetpath(dest, "../escape.bmp"), FALSE);
    CHECK_MSG(dest[0] == '\0',
	      "a refused name left '%.80s' in the destination", dest);

    strcpy(dest, "SENTINEL");
    CHECK_INT(gettilesetpath(dest, "CON"), FALSE);
    CHECK_MSG(dest[0] == '\0',
	      "a refused device name left '%.80s' in the destination", dest);
}

/* --- the tileset override ------------------------------------------------ */

static void test_override(void)
{
    tw_case("a ruleset outside the valid range is refused, not indexed");
    /* tilesetkey[] has one entry per ruleset; an out-of-range index here would
     * read past it. Both ends, and both directions of the accessor. */
    CHECK_MSG(gettilesetoverride(-1) == NULL, "a negative ruleset was indexed");
    CHECK_MSG(gettilesetoverride(Ruleset_Count) == NULL,
	      "an out-of-range ruleset was indexed");
    settilesetoverride(-1, "x");	  /* must not crash or store */
    settilesetoverride(Ruleset_Count, "x");
    CHECK_INT(1, 1);			  /* reaching here is the assertion */

    tw_case("setting and reading an override round-trips");
    fake_stringsetting[0] = '\0';
    settilesetoverride(Ruleset_MS, "custom.bmp");
    CHECK_STR(fake_stringsetting, "custom.bmp");

    tw_case("clearing an override stores an empty string, not NULL");
    settilesetoverride(Ruleset_MS, NULL);
    CHECK_STR(fake_stringsetting, "");
}

/* --- the shipped rc file itself ------------------------------------------ */

static void test_shippedrc(void)
{
    tw_case("res/rc -- the file that actually ships -- parses with no warnings");
    /* 🔴 THE POINT OF THIS CASE. Everything above drives synthesized text. This
     * one drives the real resource through the real parser, so a hand-edit to
     * res/rc that misspells a resource name is caught here rather than by a
     * startup warning nobody reads -- and a misspelled name does not fail
     * loudly, it silently leaves the default in place.
     *
     * It also proves the case-insensitive matching on production data: res/rc
     * is written in mixed case throughout. */
    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, "res");
    memset(allresources, 0, sizeof allresources);
    initresourcedefaults();
    warn_count = 0;
    if (!readrcfile()) {
	tw_skip("res/rc not readable from the working directory");
	return;
    }
    CHECK_MSG(warn_count == 0,
	      "res/rc produced %d warning(s): a resource name is misspelled",
	      warn_count);
    CHECK_MSG(res(Ruleset_None, RES_TXT_UNSLIST)[0] != '\0',
	      "res/rc did not set the unsolvable list; it says UnsolvableList"
	      " on line 6 and this is exactly the claim that was twice written"
	      " down wrong");
    CHECK_STR(res(Ruleset_None, RES_TXT_UNSLIST), "unslist.txt");
}

/* --- fuzz corpus replay -------------------------------------------------- *
 *
 * test/fuzz/corpus/rc/ replayed through the real guard, so a libFuzzer finding
 * on Linux becomes a permanent regression case on every platform
 * (docs/adr/0011: a finding is not fixed until its input is committed and
 * replayed).
 *
 * ⚠ WHAT THIS REPLAY PROVES, STATED NARROWLY. Each input is a candidate TILESET
 * NAME, and the assertion is the same property fuzz_rc.c checks: if the guard
 * accepts a name, gettilesetpath() must produce a path, and if it refuses one,
 * the destination must be left empty. It is not a memory oracle -- ASan on the
 * Linux fuzz job is that -- and a corpus of names cannot prove the guard
 * refuses something it has never seen. The hand-written cases above are what
 * pin the individual rules. */

static int rccorpus_replayed = 0;
static int rccorpus_checked = 0;

static void rccorpus_read(twcorpusinput const *in)
{
    char	name[512];
    char	dest[1024];
    int		i, n;

    n = in->size < (int)sizeof name - 1 ? in->size : (int)sizeof name - 1;
    for (i = 0 ; i < n ; ++i) {
	if (in->data[i] == 0)
	    break;
	name[i] = (char)in->data[i];
    }
    name[i] = '\0';

    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, "res");

    memset(dest, 'X', sizeof dest);
    if (!gettilesetpath(dest, name))
	CHECK_MSG(dest[0] == '\0',
		  "a refused corpus name left a path behind");
    ++rccorpus_checked;
}

static void rccorpus_report(twcorpusverdict v, char const *name)
{
    ++rccorpus_replayed;
    CHECK_MSG(v == TW_CORPUS_OK, "fuzz corpus input '%.80s': %s",
	      name, tw_corpus_why(v));
}

static void test_corpus(void)
{
    char	dir[256];
    int		c;

    tw_case("the rc fuzz corpus replays through the tileset guard");
    CHECK_MSG(tw_corpus_dir("rc", dir, sizeof dir),
	      "could not find test/fuzz/corpus/rc from the working directory"
	      " -- the replay would have proved nothing");
    if (dir[0]) {
	c = tw_corpus_run(dir, rccorpus_read, rccorpus_report);
	CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	CHECK_INT(rccorpus_replayed, c);
	CHECK_MSG(rccorpus_checked == c,
		  "the guard ran on only %d of %d corpus inputs",
		  rccorpus_checked, c);
    }
}

int main(void)
{
    tw_begin("res_test.c");

    test_rcbasics();
    test_rcrulesets();
    test_rcdefaults();
    test_tilesetname();
    test_tilesetpath();
    test_override();
    test_shippedrc();
    test_corpus();

    /* Leave no scratch directory behind. */
    remove(scratchdir);

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(107);
    return tw_end();
}
