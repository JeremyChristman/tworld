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
#include	<sys/types.h>	/* stat(), for the regular-file guard's stub */
#include	<sys/stat.h>

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
/* MOD (Jeremy, jc-59): the sound stubs RECORD now, for the sound pack cases.
 *
 * loadsfxfromfile() succeeds only for a file that really exists -- that is what
 * SDL_LoadWAV does -- so a case lays real files out on disk and the loader's
 * path-building is exercised for real. fake_badwav names a file that exists and
 * still "will not decode", which is the one failure a real pack author hits
 * that a missing file does not cover. fake_audio is what setaudiosystem(TRUE)
 * answers: FALSE is "started silent, or no device". */
static int	fake_audio = TRUE;
static int	fake_audioclosed = 0;
static char	fake_sfx[SND_COUNT][1024];
static int	fake_freed[SND_COUNT];
static int	fake_sfxcalls = 0;
static char const *fake_badwav = NULL;
static int	fake_notregular = 0;	/* times the decoder was handed a non-file */

int loadsfxfromfile(int index, char const *filename)
{
    FILE       *fp;

    ++fake_sfxcalls;
    if (!filename) {
	fake_sfx[index][0] = '\0';
	return TRUE;
    }
    {
	/* What trypackfile()'s regular-file guard is FOR: the decoder must
	 * never be handed a folder or a pipe. Opening a folder happens to fail
	 * on Windows anyway, so the outcome alone cannot show the guard works;
	 * this count can. */
	struct stat	st;

	if (!stat(filename, &st) && !S_ISREG(st.st_mode))
	    ++fake_notregular;
    }
    if (!(fp = fopen(filename, "rb")))
	return FALSE;
    fclose(fp);
    if (fake_badwav && strstr(filename, fake_badwav))
	return FALSE;
    snprintf(fake_sfx[index], sizeof fake_sfx[index], "%.1000s", filename);
    return TRUE;
}
void freesfx(int index)
{
    fake_sfx[index][0] = '\0';
    ++fake_freed[index];
}
int setaudiosystem(int active)
{
    if (!active) {
	++fake_audioclosed;
	return TRUE;
    }
    return fake_audio;
}
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

/* The settings store, faked: ONE key and its value. gettilesetoverride() and
 * getsoundpackoverride() read through it.
 *
 * MOD (Jeremy, jc-59): keyed now. It used to answer every key with the one
 * value, which was fine while the tileset was the only reader -- with a second
 * reader it would have let a sound pack case pass while reading the TILESET's
 * key, the exact mix-up this fake must be able to catch. */
static char	fake_stringkey[64] = "";

char const *getstringsetting(char const *key)
{
    if (!fake_stringsetting[0] || strcmp(key, fake_stringkey))
	return NULL;
    return fake_stringsetting;
}
void setstringsetting(char const *key, char const *value)
{
    snprintf(fake_stringkey, sizeof fake_stringkey, "%s", key);
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

/* MOD (Jeremy): the mirror of fileio.c's createdir() macro, and it has to be
 * written out because remove() is not portable for this.
 *
 * POSIX remove() dispatches to rmdir() for a directory, so the cleanup at the
 * bottom of main() worked on the Linux CI job and looked correct there. MSVCRT's
 * remove() handles FILES ONLY and returns -1 for a directory, so on Windows --
 * the platform this program actually ships on -- every run of this test left an
 * empty tw_res_test_dir\ behind in whatever directory it was invoked from,
 * which in practice was the source tree. An empty directory is invisible to
 * git, which is why it went unnoticed: `git status` is clean either way. */
#ifdef WIN32
#include	<direct.h>
#define	removedir(name)	(_rmdir(name) == 0)
#else
#include	<unistd.h>
#define	removedir(name)	(rmdir(name) == 0)
#endif

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
     * read past it. Both ends, and both directions of the accessor.
     *
     * 🔴 THIS CASE CANNOT KILL A MUTATION OF THAT BOUND, AND THAT IS NOT A GAP
     * IN IT. Measured 2026-09-13: relax `ruleset >= Ruleset_Count` to `>` in
     * BOTH accessors and this file still passes, all 108 checks -- while
     * run-tests.ps1 -Sanitize traps on the same build. (MOD, jc-59: the two
     * accessors now share ONE bound, in getoverride(), which the sound pack
     * reads through as well -- so there is one survivor to track, not two.)
     *
     * The reason is in the guard's own shape. The relaxed bound lets
     * ruleset == Ruleset_Count reach `tilesetkey[ruleset]`, one past a
     * `char const *const [Ruleset_Count]` -- and then the THIRD clause,
     * `!tilesetkey[ruleset]`, absorbs whatever it read and returns anyway. The
     * answer is identical; only the out-of-bounds READ differs, and no return
     * value can expose a read.
     *
     * So the sanitize layer is the oracle for this bound, and the job of the
     * assertions below is to DRIVE it -- "not detected" and "not exercised" are
     * different diagnoses, and this file fixes the second one. Do not go looking
     * for a cleverer assertion here; the honest one would have to depend on
     * whatever the linker happened to place after tilesetkey[], which is an
     * accident rather than a contract. A mutation census will report these two
     * as survivors forever; that is the census measuring one layer, not a hole. */
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

/* --- sound packs (jc-59) --------------------------------------------------- *
 *
 * The loader is driven against REAL folders and files under the scratch
 * directory, because what it does is find files: enumerate a pack folder,
 * match names regardless of case, join paths. Only the decode is faked (see
 * loadsfxfromfile() above). */

static char	madefiles[64][600];
static int	madefilecount = 0;
static char	madedirs[16][600];
static int	madedircount = 0;

static void mkdir_(char const *rel)
{
    char	path[600];

    snprintf(path, sizeof path, "%s/%s", scratchdir, rel);
    (void)createdir(path);
    if (madedircount < 16)
	snprintf(madedirs[madedircount++], sizeof *madedirs, "%s", path);
}

static void mkfile_(char const *rel, char const *text)
{
    char	path[600];
    FILE       *fp;

    snprintf(path, sizeof path, "%s/%s", scratchdir, rel);
    if ((fp = fopen(path, "wb"))) {
	fputs(text, fp);
	fclose(fp);
    }
    if (madefilecount < 64)
	snprintf(madefiles[madefilecount++], sizeof *madefiles, "%s", path);
}

static void cleanup_(void)
{
    while (madefilecount > 0)
	remove(madefiles[--madefilecount]);
    while (madedircount > 0)
	(void)removedir(madedirs[--madedircount]);
}

/* The file slot n now holds, without its directory, or "" when it is free. */
static char const *slot(int n)
{
    char const *p = fake_sfx[n];
    char const *q;

    for (q = p ; *q ; ++q)
	if (*q == '/' || *q == '\\')
	    p = q + 1;
    return p;
}

static int hasproblem(int kind, int line, char const *text)
{
    char const *t;
    int		i, l;

    for (i = 0 ; i < getsoundpackproblemcount() ; ++i)
	if (getsoundpackproblem(i, &l, &t) == kind && l == line
				&& (!text || !strcmp(t, text)))
	    return TRUE;
    return FALSE;
}

/* A fresh MS game with the rc file below and no pack chosen. */
static void soundsetup(char const *rctext)
{
    int	n;

    loadrc(rctext);
    currentruleset = Ruleset_MS;
    resources = allresources[Ruleset_MS];
    fake_stringkey[0] = '\0';
    fake_stringsetting[0] = '\0';
    fake_audio = TRUE;
    fake_badwav = NULL;
    fake_audioclosed = 0;
    fake_sfxcalls = 0;
    for (n = 0 ; n < SND_COUNT ; ++n) {
	fake_sfx[n][0] = '\0';
	fake_freed[n] = 0;
    }
}

static char const soundrc[] =
    "PickupChipSound=d-chip.wav\nSplashSound=d-splash.wav\n"
    "[MS]\nTickSound=d-tick.wav\n";

static void test_soundnames(void)
{
    static char	howto[65536];
    char	lower[64];
    char	needle[80];
    FILE       *fp;
    size_t	len;
    int		n, i;

    tw_case("🔴 every sound has a pack name, and it is rclist[]'s name capitalized");
    /* The checked half of a checked duplicate: soundname[] and rclist[]'s sound
     * rows are two tables that must agree, and this is the only thing that says
     * they do. A hole in soundname[]'s designated initializers is a NULL, which
     * would make that sound impossible to supply from a pack. */
    for (n = 0 ; n < SND_COUNT ; ++n) {
	CHECK_MSG(soundname[n] != NULL, "sound %d has no pack name", n);
	if (!soundname[n])
	    continue;
	for (i = 0 ; soundname[n][i] && i < (int)sizeof lower - 1 ; ++i)
	    lower[i] = (char)tolower((unsigned char)soundname[n][i]);
	lower[i] = '\0';
	CHECK_STR(lower, rclist[RES_SND_BASE + n].name);
    }

    tw_case("the shipped how-to lists every sound in its table");
    /* The third copy of the names, and the one a pack author actually reads.
     * Anchored to a table row -- two spaces, the name, a space -- so a name
     * that only appears inside a longer one does not count. */
    if (!(fp = fopen("res/sounds/How to make a sound pack.txt", "rb"))) {
	tw_skip("res/sounds/How to make a sound pack.txt not readable from here");
	return;
    }
    len = fread(howto, 1, sizeof howto - 1, fp);
    fclose(fp);
    howto[len] = '\0';
    for (n = 0 ; n < SND_COUNT ; ++n) {
	if (!soundname[n])
	    continue;
	snprintf(needle, sizeof needle, "\n  %s ", soundname[n]);
	CHECK_MSG(strstr(howto, needle) != NULL,
		  "the how-to's table has no row for %s", soundname[n]);
    }
}

static void test_soundpackpath(void)
{
    static char const *const refused[] = {
	"", "   ", ".", "..", "a/b", "a\\b", "c:x", "CON", "Caf\xc3\xa9", "tab\tname",
	/* "??" is what Qt makes of a name the code page cannot hold, which is how
	 * a folder named in Japanese reached the loader; the rest are its kin,
	 * none of which Windows allows in a file name anyway. */
	"??", "a*b", "a\"b", "a<b", "a>b", "a|b"
    };
    char	dest[1024];
    char	want[1024];
    size_t	i;

    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, scratchdir);

    tw_case("a pack name is appended to the sound pack directory, spaces and all");
    CHECK(getsoundpackpath(dest, "My Pack"));
    snprintf(want, sizeof want, "%s%c%s%c%s", scratchdir, DIRSEP_CHAR,
	     SOUNDPACKDIR, DIRSEP_CHAR, "My Pack");
    CHECK_STR(dest, want);

    tw_case("a NULL name yields the sound pack directory itself");
    CHECK(getsoundpackpath(dest, NULL));
    snprintf(want, sizeof want, "%s%c%s", scratchdir, DIRSEP_CHAR, SOUNDPACKDIR);
    CHECK_STR(dest, want);

    tw_case("🔴 every unsafe pack name is refused and CLEARS the destination");
    /* Everything istilesetname() refuses, plus "." and anything outside ASCII:
     * SDL opens WAV files by UTF-8 name on Windows while these paths are built
     * in the local code page, so "Café" would list and then load nothing. */
    for (i = 0 ; i < sizeof refused / sizeof *refused ; ++i) {
	memset(dest, 'X', sizeof dest);
	CHECK_MSG(!getsoundpackpath(dest, refused[i]),
		  "pack name #%d was accepted", (int)i);
	CHECK_MSG(dest[0] == '\0', "pack name #%d left a path behind", (int)i);
    }

    tw_case("a pack path too long to build is refused, and clears dest");
    {
	char name[400], *saved = resdir, longdir[400];
	memset(name, 'a', 300); name[300] = '\0';
	memset(dest, 'X', sizeof dest);
	CHECK(!getsoundpackpath(dest, name));
	CHECK(dest[0] == '\0');
	memset(longdir, 'd', 300); longdir[300] = '\0';
	resdir = longdir;
	memset(dest, 'X', sizeof dest);
	CHECK(!getsoundpackpath(dest, NULL));
	CHECK(dest[0] == '\0');
	resdir = saved;
    }

    tw_case("the override is stored under the SOUND PACK key, per ruleset");
    fake_stringsetting[0] = '\0';
    setsoundpackoverride(Ruleset_MS, "X");
    CHECK_STR(fake_stringkey, "mssoundpack");
    CHECK_MSG(gettilesetoverride(Ruleset_MS) == NULL,
	      "the tileset read the sound pack's value");
    CHECK_MSG(getsoundpackoverride(Ruleset_None) == NULL,
	      "Ruleset_None read the MS key");
    setsoundpackoverride(Ruleset_Lynx, "Y");
    CHECK_STR(fake_stringkey, "lynxsoundpack");
    CHECK_STR(getsoundpackoverride(Ruleset_Lynx), "Y");
    CHECK_MSG(getsoundpackoverride(Ruleset_Count) == NULL,
	      "an out-of-range ruleset was indexed");
    /* (Ruleset_None is checked above, straight after the MS key is stored:
     * here the fake holds the LYNX key, so a None mapped to "mssoundpack"
     * would read back NULL anyway and the check would prove nothing.) */
    setsoundpackoverride(Ruleset_Lynx, NULL);
    CHECK_STR(fake_stringsetting, "");
}

static void test_soundload(void)
{
    char	longmap[800];
    int		n, i, problems;

    mkfile_("d-chip.wav", "x");
    mkfile_("d-splash.wav", "x");
    mkfile_("d-tick.wav", "x");
    mkdir_("sounds");
    mkdir_("sounds/My Pack");
    mkfile_("sounds/My Pack/PickupChipSound.wav", "x");
    mkfile_("sounds/My Pack/SplashSound.wav", "x");
    mkfile_("sounds/My Pack/Bell File.wav", "x");
    mkfile_("sounds/My Pack/sounds.txt",
	    "# a comment\n"
	    "; another\n"
	    "  SplashSound  =  Bell File.wav  \n"
	    "BogusSound=x.wav\n"
	    "TickSound=missing.wav\n"
	    "this line has no equals sign\n"
	    "ThiefSound=sub/x.wav\n"
	    "BombSound=\n");
    mkdir_("sounds/lower");
    mkfile_("sounds/lower/pickuptoolsound.WAV", "x");
    mkdir_("sounds/Empty");
    mkfile_("sounds/Empty/sounds.txt", "SplashSound=nothing.wav");
    mkdir_("sounds/Twice");
    mkfile_("sounds/Twice/bad.wav", "x");
    mkfile_("sounds/Twice/sounds.txt", "SplashSound=bad.wav\nPickupChipSound=bad.wav\n");
    mkdir_("sounds/Long");
    mkfile_("sounds/Long/ok.wav", "x");
    memset(longmap, 'a', sizeof longmap);
    memcpy(longmap, "PickupChipSound=", 16);
    strcpy(longmap + 700, ".wav\nSplashSound=ok.wav\n");
    mkfile_("sounds/Long/sounds.txt", longmap);

    mkdir_("sounds/Order");
    mkfile_("sounds/Order/a.wav", "x");
    mkfile_("sounds/Order/b.wav", "x");
    mkfile_("sounds/Order/Sounds.TXT",
	    "SplashSound=a.wav\nsplashsound=b.wav\nPickupChipSound=a.wav\n"
	    "PICKUPCHIPSOUND=\nTickSound=.\nSwitchSound=Caf\xc3\xa9.wav\n");
    mkdir_("sounds/Many");
    {
	static char many[2000];
	many[0] = '\0';
	for (i = 0 ; i < 40 ; ++i)
	    sprintf(many + strlen(many), "Bogus%d=x\n", i);
	mkfile_("sounds/Many/sounds.txt", many);
    }
    mkdir_("sounds/Bom");
    mkfile_("sounds/Bom/ok.wav", "x");
    mkfile_("sounds/Bom/sounds.txt",
	    "\xEF\xBB\xBFPickupChipSound=ok.wav\r\nSplashSound=ok.wav\r\n");
    mkdir_("sounds/Odd");
    mkdir_("sounds/Odd/ThiefSound.wav");	/* a FOLDER under a sound's name */
    mkdir_("sounds/Typos");
    mkfile_("sounds/Typos/SplashSound.wav", "x");
    mkfile_("sounds/Typos/ChipDeath.wav", "x");
    mkfile_("sounds/Typos/PickupChipSound.mp3", "x");
    mkfile_("sounds/Typos/readme.txt", "x");

    tw_case("🔴 with no pack chosen, the rc sounds load and every other slot is FREED");
    /* The stale-slot defect: a slot nothing loads for used to keep whatever it
     * held. Seeded here with a leftover the old loader would have kept. */
    soundsetup(soundrc);
    snprintf(fake_sfx[SND_BOMB_EXPLODES], sizeof *fake_sfx, "stale.wav");
    n = loadsounds();
    CHECK_INT(n, 3);
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");
    CHECK_STR(slot(SND_WATER_SPLASH), "d-splash.wav");
    CHECK_STR(slot(SND_TIME_LOW), "d-tick.wav");
    CHECK_STR(slot(SND_BOMB_EXPLODES), "");
    CHECK_INT(fake_freed[SND_BOMB_EXPLODES], 1);
    CHECK_INT(packloaded, FALSE);
    CHECK_INT(getsoundpackproblemcount(), 0);

    tw_case("the chosen pack wins: sounds.txt, then the fixed name, then rc");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "My Pack");
    n = loadsounds();
    CHECK_INT(n, 3);
    CHECK_STR(slot(SND_IC_COLLECTED), "PickupChipSound.wav");
    CHECK_STR(slot(SND_WATER_SPLASH), "Bell File.wav");	/* over SplashSound.wav */
    CHECK_STR(slot(SND_TIME_LOW), "d-tick.wav");	/* mapped file missing */
    CHECK_STR(slot(SND_BOMB_EXPLODES), "");		/* blank value maps nothing */
    CHECK_INT(packloaded, TRUE);

    tw_case("each sounds.txt problem is reported by kind and line, once");
    CHECK(hasproblem(SOUNDPROBLEM_UNKNOWNSOUND, 4, "BogusSound"));
    CHECK(hasproblem(SOUNDPROBLEM_MISSING, 5, "missing.wav"));
    CHECK(hasproblem(SOUNDPROBLEM_BADLINE, 6, NULL));
    CHECK(hasproblem(SOUNDPROBLEM_BADFILENAME, 7, "sub/x.wav"));
    CHECK_INT(getsoundpackproblemcount(), 4);
    CHECK_INT(getsoundpackproblem(-1, NULL, NULL), -1);
    CHECK_INT(getsoundpackproblem(4, NULL, NULL), -1);

    tw_case("a fixed name matches however it is capitalized");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "lower");
    loadsounds();
    CHECK_STR(slot(SND_ITEM_COLLECTED), "pickuptoolsound.WAV");
    CHECK_INT(packloaded, TRUE);

    tw_case("a pack chosen for MS does not reach Lynx");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "My Pack");
    currentruleset = Ruleset_Lynx;
    resources = allresources[Ruleset_Lynx];
    loadsounds();
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");
    CHECK_INT(packloaded, FALSE);

    tw_case("a pack file that will not decode falls through and is reported");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "My Pack");
    fake_badwav = "PickupChipSound.wav";
    loadsounds();
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");
    CHECK(hasproblem(SOUNDPROBLEM_UNREADABLE, 0, "PickupChipSound.wav"));
    CHECK_INT(packloaded, TRUE);	/* Bell File.wav still came from it */

    tw_case("one bad file mapped to two sounds is ONE problem");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Twice");
    fake_badwav = "bad.wav";
    loadsounds();
    for (problems = 0, i = 0 ; i < getsoundpackproblemcount() ; ++i)
	if (getsoundpackproblem(i, NULL, NULL) == SOUNDPROBLEM_UNREADABLE)
	    ++problems;
    CHECK_INT(problems, 1);

    tw_case("a line too long to read is refused, and the NEXT line still counts");
    /* filegetline() cuts an over-long line and discards the rest of it; half a
     * filename must not be looked up as though it were a whole one. */
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Long");
    loadsounds();
    CHECK(hasproblem(SOUNDPROBLEM_BADLINE, 1, ""));
    CHECK_STR(slot(SND_WATER_SPLASH), "ok.wav");
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");

    tw_case("a later sounds.txt line wins, a blank one clears, and names ignore case");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Order");
    loadsounds();
    CHECK_STR(slot(SND_WATER_SPLASH), "b.wav");
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");
    CHECK(hasproblem(SOUNDPROBLEM_BADFILENAME, 5, "."));
    CHECK(hasproblem(SOUNDPROBLEM_BADFILENAME, 6, "Caf\xc3\xa9.wav"));
    CHECK_INT(getsoundpackproblemcount(), 2);

    tw_case("the problem list stops at its bound, 32, and writes nothing past it");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Many");
    loadsounds();
    CHECK_INT(getsoundpackproblemcount(), 32);
    CHECK_INT(getsoundpackproblemsdropped(), 8);

    tw_case("load failures outrank stray files in the bounded list");
    {
	char	stray[600];

	mkdir_("sounds/Strays");
	mkfile_("sounds/Strays/PickupChipSound.wav", "x");
	for (i = 0 ; i < 40 ; ++i) {
	    snprintf(stray, sizeof stray, "%s/sounds/Strays/s%02d.mp3", scratchdir, i);
	    fclose(fopen(stray, "wb"));
	}
	soundsetup(soundrc);
	setsoundpackoverride(Ruleset_MS, "Strays");
	fake_badwav = "PickupChipSound.wav";
	loadsounds();
	CHECK(hasproblem(SOUNDPROBLEM_UNREADABLE, 0, "PickupChipSound.wav"));
	CHECK_INT(getsoundpackproblemsdropped(), 9);
	for (i = 0 ; i < 40 ; ++i) {
	    snprintf(stray, sizeof stray, "%s/sounds/Strays/s%02d.mp3", scratchdir, i);
	    remove(stray);
	}
    }

    tw_case("🔴 a byte-order mark does not cost sounds.txt its first line");
    /* Notepad's "UTF-8 with BOM" and PowerShell 5.1's Out-File both write one,
     * and CRLF line ends with it. Before the fix the first name read as three
     * garbage bytes plus PickupChipSound: its sound lost, and reported as an
     * unknown name that was spelled correctly. */
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Bom");
    loadsounds();
    CHECK_STR(slot(SND_IC_COLLECTED), "ok.wav");
    CHECK_STR(slot(SND_WATER_SPLASH), "ok.wav");
    CHECK_INT(getsoundpackproblemcount(), 0);
    CHECK_INT(getsoundpackproblemsdropped(), 0);

    tw_case("only a regular file is handed to the decoder");
    /* A folder named like a sound. On Linux the same guard keeps a named pipe
     * away from SDL, which would block reading it on the thread that draws. */
    soundsetup(soundrc);
    fake_notregular = 0;
    setsoundpackoverride(Ruleset_MS, "Odd");
    loadsounds();
    CHECK_INT(fake_notregular, 0);
    CHECK_STR(slot(SND_BOOTS_STOLEN), "");
    CHECK(hasproblem(SOUNDPROBLEM_UNREADABLE, 0, "ThiefSound.wav"));

    tw_case("🔴 a sound file nothing refers to is reported -- a misspelling, usually");
    /* The quiet failure: a pack author's ChipDeath.wav or PickupChipSound.mp3
     * used to be ignored without a word while the default played. A file that
     * is not a sound at all (readme.txt) is none of the loader's business. */
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Typos");
    loadsounds();
    CHECK_STR(slot(SND_WATER_SPLASH), "SplashSound.wav");
    CHECK(hasproblem(SOUNDPROBLEM_UNUSED, 0, "ChipDeath.wav"));
    CHECK(hasproblem(SOUNDPROBLEM_UNUSED, 0, "PickupChipSound.mp3"));
    CHECK_INT(getsoundpackproblemcount(), 2);

    tw_case("a pack folder with more files than are read says so");
    {
	char	crowd[600];

	mkdir_("sounds/Crowd");
	for (i = 0 ; i < MAXPACKFILES ; ++i) {
	    snprintf(crowd, sizeof crowd, "%s/sounds/Crowd/f%04d.dat", scratchdir, i);
	    fclose(fopen(crowd, "wb"));
	}
	soundsetup(soundrc);
	setsoundpackoverride(Ruleset_MS, "Crowd");
	loadsounds();
	CHECK(!hasproblem(SOUNDPROBLEM_TOOMANYFILES, 0, ""));
	snprintf(crowd, sizeof crowd, "%s/sounds/Crowd/f%04d.dat", scratchdir, i);
	fclose(fopen(crowd, "wb"));
	loadsounds();
	CHECK(hasproblem(SOUNDPROBLEM_TOOMANYFILES, 0, ""));
	for (i = 0 ; i < MAXPACKFILES + 1 ; ++i) {
	    snprintf(crowd, sizeof crowd, "%s/sounds/Crowd/f%04d.dat", scratchdir, i);
	    remove(crowd);
	}
    }

    tw_case("🔴 reloadsounds(): a pack that supplies NOTHING is a failed pick");
    /* The jc-42 rule. The rc sounds load behind it, so "sounds loaded" is true
     * and says nothing about whether the user's choice did. */
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Empty");
    CHECK_INT(reloadsounds(), SOUNDPACK_FAILED);
    CHECK_STR(slot(SND_WATER_SPLASH), "d-splash.wav");
    CHECK(hasproblem(SOUNDPROBLEM_MISSING, 1, "nothing.wav"));

    tw_case("reloadsounds(): a pack folder that is not there fails the same way");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "Not There");
    CHECK_INT(reloadsounds(), SOUNDPACK_FAILED);
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");
    /* ...and says why, or its author is told to add sounds to a folder that
     * is not there. */
    CHECK(hasproblem(SOUNDPROBLEM_FOLDER, 0, "Not There"));

    tw_case("reloadsounds(): a good pack, and the default, both succeed");
    soundsetup(soundrc);
    setsoundpackoverride(Ruleset_MS, "My Pack");
    CHECK_INT(reloadsounds(), SOUNDPACK_OK);
    setsoundpackoverride(Ruleset_MS, "");
    CHECK_INT(reloadsounds(), SOUNDPACK_OK);
    CHECK_STR(slot(SND_IC_COLLECTED), "d-chip.wav");

    tw_case("🔴 reloadsounds() with no audio touches NOTHING and does not blame the pack");
    /* loadsfxfromfile() fails for "no device" exactly as for "no file". Without
     * this answer, a device hiccup during a pick would have freed every working
     * sound and reported the pack as broken. */
    soundsetup(soundrc);
    snprintf(fake_sfx[SND_IC_COLLECTED], sizeof *fake_sfx, "keep.wav");
    setsoundpackoverride(Ruleset_MS, "My Pack");
    fake_audio = FALSE;
    CHECK_INT(reloadsounds(), SOUNDPACK_NOAUDIO);
    CHECK_STR(slot(SND_IC_COLLECTED), "keep.wav");
    CHECK_INT(fake_sfxcalls, 0);
    for (problems = 0, i = 0 ; i < SND_COUNT ; ++i)
	problems += fake_freed[i];
    CHECK_INT(problems, 0);

    tw_case("reloadsounds() with no ruleset in play fails rather than guessing");
    soundsetup(soundrc);
    currentruleset = Ruleset_None;
    CHECK_INT(reloadsounds(), SOUNDPACK_FAILED);
    CHECK_INT(fake_sfxcalls, 0);

    tw_case("reloadsounds() closes the device on zero sounds, as startup does");
    soundsetup("");
    CHECK_INT(reloadsounds(), SOUNDPACK_OK);
    CHECK_INT(fake_audioclosed, 1);

    cleanup_();
    fake_badwav = NULL;
    fake_stringsetting[0] = '\0';
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
    test_soundnames();
    test_soundpackpath();
    test_soundload();
    test_shippedrc();
    test_corpus();

    /* Leave no scratch directory behind. Checked rather than ignored: a
     * failure here means a later run inherits a directory this one wrote, and
     * the one thing that could put a stale `rc` in it is this same test. */
    CHECK_MSG(removedir(scratchdir),
	      "the scratch directory %s could not be removed", scratchdir);

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(304);
    return tw_end();
}
