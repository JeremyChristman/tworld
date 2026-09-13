/* settings_test.c: the tw_settings.ini reader and writer.
 *
 * MOD (Jeremy). settings.cpp is 367 lines that ship in every release and had NO
 * test -- the largest file in the tree with none, and `tw_settings.ini` was the
 * only file format here with neither a unit test nor a fuzz target while `.dat`,
 * `.dac`, `.tws`, the password encoding, the level data and `rc` all had both.
 * This project's most consistent lesson says that is where the defects are.
 *
 * 🔴 AND IT IS THE ONE FILE THE PROGRAM CAN DESTROY. Every other parser here
 * only READS what a stranger wrote; this module rewrites a file the user owns,
 * in place, many times per session -- savesettings() is called from play.c:340,
 * TWTheme.cpp:72, three sites in TWMainWnd.cpp, and shutdownsystem() in
 * tworld.c. What it does when that write goes wrong is not a display detail.
 *
 * WHAT IS PINNED HERE, and why each one is not obvious:
 *
 * 1. THE PARSER'S TOLERANCES. `volume = 8` and `volume=8` are one key; a value
 *    keeps its interior spacing but loses its trailing spaces (selectedseries is
 *    matched against the enumerated set list by strcmp, so "MO3.dat-ms.dac "
 *    simply fails to reopen the set with no explanation); a leading UTF-8 BOM is
 *    stripped from the first line only; [Section] headers and ; or # comments
 *    are skipped; a line reading "=8" is REFUSED rather than stored under the
 *    key "=8" and then written back forever.
 *
 * 2. THE ROUND TRIP. savesettings() rewrites the WHOLE file from the map, so a
 *    key it does not recognize is only preserved because of the deliberate
 *    "everything else" pass that writes leftovers under [Other]. That is the
 *    half that stops this fork eating a setting a future upstream release adds,
 *    and until now it was asserted only in a comment.
 *
 * 3. THE REFUSAL TO WRITE OVER AN UNREADABLE FILE. loadsettings() leaves the map
 *    empty when the file exists but will not open, and the writer rewrites the
 *    whole file from that map -- so without the settingsUnreadable latch, one
 *    unreadable moment at startup would permanently erase every setting. The
 *    install lives in a Dropbox folder, so "exists but will not open" is a real
 *    state, not a hypothetical.
 *
 * WHAT THIS FILE DOES NOT COVER: nothing here reaches the Qt front end, and
 * these cases deliberately do not assert on warn() text.
 *
 * TESTLANG: c++
 *
 * settings.cpp is C++ -- std::map, std::ifstream, std::string -- and CMake
 * compiles it only as C++. Building it as C is not a translation that exists.
 * See docs/adr/0004.
 */

#include	"tw_test.h"
#include	"tw_corpus.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/stat.h>

#include	<string>
#include	<map>

/* --- the surface settings.cpp links against, stubbed --------------------- *
 *
 * 🔴 fileio.c is NOT compiled in, and that is a constraint rather than a
 * preference: it relies on C's implicit void* conversion through err.h's
 * x_alloc and does not compile as C++ at all (ADR 0004). The one function
 * settings.cpp uses from it is stubbed instead -- with C linkage, because
 * fileio.h declares it inside an extern "C" block and a C++ definition would be
 * a different symbol.
 *
 * The stub is also the seam this file steers: returning NULL from it is how the
 * "settings path is too long" branch is reached without a 260-character path. */

extern "C" {

char	       *savedir = 0;
char	       *appdir  = 0;

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *prefix, char const *fmt, ...)
{
    (void)prefix; (void)fmt;
}
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* When true, the stub reports failure the way getpathforfileindir() does when
 * the path would overrun the buffer. */
static int	fake_pathtoolong = 0;

char *getpathforfileindir(char const *dir, char const *filename)
{
    size_t	n;
    char       *p;

    if (fake_pathtoolong)
	return 0;
    n = strlen(dir) + 1 + strlen(filename) + 1;
    p = (char *)malloc(n);
    if (p)
	snprintf(p, n, "%s/%s", dir, filename);
    return p;
}

/* The size of the buffer fileio.c would have allocated. savesettings() measures
 * the staging path against it, so shrinking this is how the "too long to write
 * safely" branch is reached without building a 260-character path. */
static int	fake_pathbufferlen = 1024;

int getpathbufferlen(void)
{
    return fake_pathbufferlen;
}

}

/* --- the source under test ---------------------------------------------- */

#include	"../settings.cpp"

/* --- the harness -------------------------------------------------------- */

/* The mirror of fileio.c's createdir() macro. remove() deletes a directory on
 * POSIX but not on Windows, where MSVCRT's handles files only -- see the same
 * note in res_test.c. */
#ifdef WIN32
#include	<direct.h>
/* Named explicitly rather than relied upon. FindFirstFileA and CreateFileA are
 * used below, and they resolve today only because ../settings.cpp includes
 * <windows.h> under _WIN32 -- an undeclared dependency that would fail
 * confusingly the moment the code under test stopped needing it. */
#define	WIN32_LEAN_AND_MEAN
#include	<windows.h>
#define	makedir(name)	(mkdir(name) == 0)
#define	removedir(name)	(_rmdir(name) == 0)
#else
#include	<unistd.h>
#include	<dirent.h>	/* counttemps(), below */
#define	makedir(name)	(mkdir(name, 0755) == 0)
#define	removedir(name)	(rmdir(name) == 0)
#endif

static char const *scratchdir = "tw_settings_test_dir";
static char	inipath[512];

static void putfile(char const *bytes, size_t len)
{
    FILE       *f = fopen(inipath, "wb");

    if (f) {
	if (len)
	    fwrite(bytes, 1, len, f);
	fclose(f);
    }
}

static void puttext(char const *bytes)
{
    putfile(bytes, strlen(bytes));
}

static std::string getfile(void)
{
    FILE       *f = fopen(inipath, "rb");
    std::string	s;
    char	buf[4096];
    size_t	n;

    if (!f)
	return std::string("<missing>");
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
	s.append(buf, n);
    fclose(f);
    return s;
}

/* The whole settings map, flattened, so two of them can be compared as one
 * string and a mismatch prints something a person can read. */
static std::string dumpmap(void)
{
    std::string	s;
    std::map<std::string, std::string>::const_iterator i;

    for (i = settings.begin() ; i != settings.end() ; ++i)
	s += "<" + i->first + ">=<" + i->second + ">|";
    return s;
}

/* 🔴 PASSING A TEMPORARY'S c_str() TO CHECK_STR IS A DANGLING POINTER, and it
 * does not look like one. Written the obvious way -- handing the macro the
 * result of a function that RETURNS a std::string by value -- the macro
 * evaluates its argument once into a local `char const *` (deliberately, so a
 * check on a call with a side effect means what it reads), and the temporary
 * that pointer came from dies at the end of that assignment, before the
 * comparison runs. It compiles, and it compares freed memory: the first version
 * of this file reported `got "?q*?J"` for four cases that were otherwise
 * correct. These two keep the bytes alive in a static for the length of the
 * check, and every case below goes through them. */
static std::string filebuf, mapbuf, platbuf;

/* 🔴 THE FILE IS WRITTEN IN TEXT MODE, AND THAT IS THE FORMAT, NOT AN ACCIDENT.
 * savesettings() writes '\n' through an ofstream, so on Windows the file on disk
 * is CRLF -- which is what the shipped tw_settings.ini is, and what every user's
 * copy already is. A test that compared against LF everywhere would either fail
 * here or, worse, pass after someone "fixed" the writer to emit exact bytes and
 * silently converted every user's file.
 *
 * So expectations are written once, with '\n', and translated to the platform's
 * form on the way into the comparison. */
static char const *plat(char const *lf)
{
    platbuf.clear();
    for ( ; *lf ; ++lf) {
#ifdef WIN32
	if (*lf == '\n')
	    platbuf += '\r';
#endif
	platbuf += *lf;
    }
    return platbuf.c_str();
}

static char const *filetext(void)
{
    filebuf = getfile();
    return filebuf.c_str();
}

static char const *maptext(void)
{
    mapbuf = dumpmap();
    return mapbuf.c_str();
}

/* Start from a known state: the file holds exactly these bytes and nothing is
 * carried over from the previous case. */
static void loadtext(char const *bytes)
{
    puttext(bytes);
    settings.clear();
    settingsUnreadable = false;
    warn_count = 0;
    loadsettings();
}

static char const *val(char const *key)
{
    std::map<std::string, std::string>::const_iterator i(settings.find(key));

    return i == settings.end() ? "<absent>" : i->second.c_str();
}

static int haskey(char const *key)
{
    return settings.find(key) != settings.end();
}

/* Defined with the staging-file cases further down, where the reasoning for
 * walking the directory lives; declared here because the "will not open" case
 * above it also has to assert that nothing was left behind. */
static int counttemps(void);

/* --- the parser's tolerances -------------------------------------------- */

static void test_parsebasics(void)
{
    tw_case("a key and value are read");
    loadtext("volume=8\n");
    CHECK_STR(val("volume"), "8");
    CHECK_INT((int)settings.size(), 1);

    tw_case("space around the name and the value is trimmed");
    /* "volume = 8" and "volume=8" have to be the same key or a hand-edited file
     * silently stops being read. */
    loadtext("  volume  =  8  \n");
    CHECK_STR(val("volume"), "8");
    CHECK_INT((int)settings.size(), 1);

    tw_case("a value keeps its interior spacing");
    /* selectedseries holds a filename, and level set names contain spaces. */
    loadtext("selectedseries=Chips Challenge.dat-ms.dac\n");
    CHECK_STR(val("selectedseries"), "Chips Challenge.dat-ms.dac");

    tw_case("🔴 trailing space is trimmed from a value, because it is invisible");
    /* A filename with a trailing space fails to open and says nothing about
     * why. This is the trim that stops that. */
    loadtext("selectedseries=CCLP5.dat-ms.dac   \n");
    CHECK_STR(val("selectedseries"), "CCLP5.dat-ms.dac");

    tw_case("an empty value is a value, not an absent key");
    loadtext("mstileset=\n");
    CHECK_INT(haskey("mstileset"), 1);
    CHECK_STR(val("mstileset"), "");

    tw_case("blank lines, comments and section headers are skipped");
    /* 🔴 EVERY COMMENT AND HEADER HERE CONTAINS AN '=', and that is the whole
     * point of the fixture. The obvious version of this case used prose
     * comments -- "; a semicolon comment" -- which contain no '=' and are
     * therefore dropped by the "no equals sign" path no matter what the skip
     * does. Deleting the ;/#/[ skip outright left the entire file green.
     * Measured, not supposed.
     *
     * What that would let through is not hypothetical: a hand-edited
     * "; legacyscores=true", commented out precisely to turn it OFF, would
     * silently become a live setting. */
    loadtext("\n"
	     "; legacyscores=true\n"
	     "# showbuildtag=true\n"
	     "[a=b]\n"
	     "   \n"
	     "volume=8\n");
    CHECK_INT((int)settings.size(), 1);
    CHECK_STR(val("volume"), "8");
    CHECK_INT(haskey("legacyscores"), 0);
    CHECK_INT(haskey("showbuildtag"), 0);

    tw_case("a comment marker only starts a comment at the start of the line");
    /* The check is on the first non-blank character, so these are keys. */
    loadtext("a;b=1\na#b=2\n");
    CHECK_STR(val("a;b"), "1");
    CHECK_STR(val("a#b"), "2");

    tw_case("the value may contain an equals sign; the FIRST one splits");
    loadtext("k=a=b\n");
    CHECK_STR(val("k"), "a=b");
    CHECK_INT((int)settings.size(), 1);

    tw_case("🔴 a line beginning with an equals sign is refused, not stored");
    /* pos == 0 is checked separately: "pos - 1" on 0 wraps to SIZE_MAX, which is
     * string::npos, and find_last_not_of(npos) searches the whole line and
     * happily reports a name. Without that check "=8" becomes a key named "=8"
     * that is then written back forever. */
    loadtext("=8\n");
    CHECK_INT((int)settings.size(), 0);

    tw_case("a line whose name is only whitespace is refused too");
    loadtext("   =8\n");
    CHECK_INT((int)settings.size(), 0);

    tw_case("a line with no equals sign at all is skipped");
    loadtext("novalue\n");
    CHECK_INT((int)settings.size(), 0);

    tw_case("the last of two identical keys wins");
    loadtext("volume=1\nvolume=2\n");
    CHECK_INT((int)settings.size(), 1);
    CHECK_STR(val("volume"), "2");

    tw_case("a file with no trailing newline still yields its last key");
    loadtext("volume=8");
    CHECK_STR(val("volume"), "8");

    tw_case("keys are case-SENSITIVE here, unlike the rc file's");
    /* Stated because res.c's parser is the opposite, and the two files sit next
     * to each other. Nothing lowercases anything in this one. */
    loadtext("Volume=1\nvolume=2\n");
    CHECK_INT((int)settings.size(), 2);
    CHECK_STR(val("Volume"), "1");
    CHECK_STR(val("volume"), "2");
}

static void test_lineendings(void)
{
    tw_case("a CRLF file parses, and the carriage return stays out of the value");
    /* "1\r" does not parse as an int, so without the strip every numeric
     * setting in a file written on Windows and read elsewhere reads as -1. */
    loadtext("volume=8\r\n");
    CHECK_STR(val("volume"), "8");

    tw_case("🔴 a UTF-8 BOM is stripped from the first line");
    /* PowerShell 5.1's redirection and Out-File -Encoding utf8 both write one,
     * so a scripted edit of this file really does produce a BOM. Without the
     * strip the first key becomes "<BOM>volume", which is ignored and then
     * preserved forever under [Other]. */
    loadtext("\xEF\xBB\xBFvolume=8\n");
    CHECK_INT(haskey("volume"), 1);
    CHECK_STR(val("volume"), "8");
    CHECK_INT((int)settings.size(), 1);

    tw_case("a BOM is stripped from the FIRST line only");
    /* Those bytes mid-file are part of a key name, not a marker. */
    /* Split so that the hex escape cannot swallow the 'b' of "bgcolor" as a
     * fourth hex digit -- which is a compile error, and would otherwise be a
     * silently different byte string. */
    loadtext("volume=8\n\xEF\xBB\xBF" "bgcolor=black\n");
    CHECK_INT(haskey("bgcolor"), 0);
    CHECK_INT((int)settings.size(), 2);
}

/* --- the typed accessors ------------------------------------------------- */

static void test_accessors(void)
{
    tw_case("getintsetting reads a number, and reports -1 for an absent key");
    loadtext("volume=8\n");
    CHECK_INT(getintsetting("volume"), 8);
    CHECK_INT(getintsetting("nosuchkey"), -1);

    tw_case("🔴 a non-numeric value reads as -1, not as garbage");
    /* Callers treat -1 as "unset" and substitute their own default, so a
     * hand-edited "volume=loud" has to land there rather than on 0. */
    loadtext("volume=loud\n");
    CHECK_INT(getintsetting("volume"), -1);

    tw_case("a value with trailing text parses its leading number");
    /* istringstream >> int stops at the first non-digit. Pinned as the behavior
     * that exists, not as one anyone designed. */
    loadtext("volume=8x\n");
    CHECK_INT(getintsetting("volume"), 8);

    tw_case("an empty value is not a number");
    loadtext("volume=\n");
    CHECK_INT(getintsetting("volume"), -1);

    tw_case("setintsetting round-trips through getintsetting");
    loadtext("");
    setintsetting("volume", 11);
    CHECK_INT(getintsetting("volume"), 11);
    setintsetting("volume", -3);
    CHECK_INT(getintsetting("volume"), -3);

    tw_case("getstringsetting reports NULL for an absent key");
    loadtext("selectedseries=CCLP5.dat-ms.dac\n");
    CHECK_STR(getstringsetting("selectedseries"), "CCLP5.dat-ms.dac");
    CHECK(getstringsetting("nosuchkey") == 0);

    tw_case("setstringsetting round-trips, including an empty string");
    loadtext("");
    setstringsetting("mstileset", "tiles.bmp");
    CHECK_STR(getstringsetting("mstileset"), "tiles.bmp");
    setstringsetting("mstileset", "");
    CHECK_STR(getstringsetting("mstileset"), "");
}

/* --- the opt-in predicate ------------------------------------------------- *
 *
 * 🔴 THE LARGEST GAP THIS FILE HAD. settingoptedin() was a whole public function
 * with FOUR shipped consumers -- showbuildtag, showdeathcounter, legacyscores
 * and ignorepasswords -- and zero coverage, in a file whose most-repeated lesson
 * is that the thing with no test is the thing with the bug.
 *
 * It is the guard that makes a switch fail SAFE: anything that is not plainly
 * "true" or "1" leaves the feature off, so a typo in a hand-edited file cannot
 * silently turn something on. The build tag in the window title is one of these,
 * and it must default OFF for a public download. */

static void test_optedin(void)
{
    tw_case("a switch is OFF when the key is absent entirely");
    loadtext("volume=8\n");
    CHECK_INT(settingoptedin("showbuildtag"), 0);

    tw_case("\"true\" and \"1\" are the only things that switch one ON");
    loadtext("showbuildtag=true\nshowdeathcounter=1\n");
    CHECK_INT(settingoptedin("showbuildtag"), 1);
    CHECK_INT(settingoptedin("showdeathcounter"), 1);

    tw_case("the word is matched case-INSENSITIVELY");
    loadtext("showbuildtag=TRUE\nshowdeathcounter=True\nlegacyscores=tRuE\n");
    CHECK_INT(settingoptedin("showbuildtag"), 1);
    CHECK_INT(settingoptedin("showdeathcounter"), 1);
    CHECK_INT(settingoptedin("legacyscores"), 1);

    tw_case("🔴 anything else leaves it OFF, which is the whole point");
    /* A switch that turned on for "yes" or "0" or a near-miss would mean a typo
     * silently enabling something -- and one of these is the build tag that a
     * public download must not show. */
    loadtext("a=false\nb=0\nc=yes\nd=on\ne=truex\nf=tru\ng=2\nh=-1\n");
    CHECK_INT(settingoptedin("a"), 0);
    CHECK_INT(settingoptedin("b"), 0);
    CHECK_INT(settingoptedin("c"), 0);
    CHECK_INT(settingoptedin("d"), 0);
    CHECK_INT(settingoptedin("e"), 0);
    CHECK_INT(settingoptedin("f"), 0);
    CHECK_INT(settingoptedin("g"), 0);
    CHECK_INT(settingoptedin("h"), 0);

    tw_case("an empty or all-whitespace value is OFF, not ON");
    loadtext("a=\n");
    CHECK_INT(settingoptedin("a"), 0);
    /* The parser trims " \t" around a value, so whitespace has to arrive by
     * another route to reach the predicate's own trim. */
    settings["b"] = " \t\r\n\v\f";
    CHECK_INT(settingoptedin("b"), 0);

    tw_case("the predicate trims its own whitespace, Qt's set exactly");
    /* QChar::isSpace()'s set, matching the QString::trimmed() this replaced --
     * vertical tab and form feed included. Set through the map rather than
     * through a file, because the line parser would trim spaces and tabs first
     * and the interesting characters cannot appear in a line at all. */
    settings["a"] = "\v\ftrue\v\f";
    CHECK_INT(settingoptedin("a"), 1);
    settings["b"] = "\r\n1\r\n";
    CHECK_INT(settingoptedin("b"), 1);

    tw_case("settingsarereadable reports the latch");
    loadtext("volume=8\n");
    CHECK_INT(settingsarereadable(), 1);
    settingsUnreadable = true;
    CHECK_INT(settingsarereadable(), 0);
    settingsUnreadable = false;
    CHECK_INT(settingsarereadable(), 1);
}

/* --- the opt-OUT predicate ------------------------------------------------ *
 *
 * MOD (Jeremy, jc-56). settingoptedout() is settingoptedin()'s mirror, for a
 * switch whose default is ON -- showlevelname, the level name in the title bar,
 * which is upstream 2.3.1's own behavior and so may only be turned off
 * deliberately.
 *
 * 🔴 THE CASE THAT MATTERS IS "NOT COMPLEMENTS". The obvious implementation is
 * !settingoptedin(), and it is wrong: settingoptedin() answers FALSE for
 * garbage, so its negation answers TRUE, and "showlevelname=yes" would turn the
 * level name off. Both predicates answer 0 for an absent, blank or unparseable
 * value -- each one reading that as "no opinion, keep MY default" -- and that
 * shared FALSE is exactly why neither can stand in for the other. */

static void test_optedout(void)
{
    tw_case("an opt-out switch is ON when the key is absent entirely");
    loadtext("volume=8\n");
    CHECK_INT(settingoptedout("showlevelname"), 0);

    tw_case("\"false\" and \"0\" are the only things that switch one OFF");
    loadtext("showlevelname=false\na=0\n");
    CHECK_INT(settingoptedout("showlevelname"), 1);
    CHECK_INT(settingoptedout("a"), 1);

    tw_case("the off word is matched case-INSENSITIVELY");
    loadtext("a=FALSE\nb=False\nc=fAlSe\n");
    CHECK_INT(settingoptedout("a"), 1);
    CHECK_INT(settingoptedout("b"), 1);
    CHECK_INT(settingoptedout("c"), 1);

    tw_case("🔴 anything else leaves it ON, including \"no\" and \"off\"");
    /* Deliberately as narrow as the on-word set: "yes" does not switch an
     * opt-in on, so "no" does not switch an opt-out off. One vocabulary. */
    loadtext("a=no\nb=off\nc=falsex\nd=fals\ne=2\nf=-1\ng=true\nh=1\n");
    CHECK_INT(settingoptedout("a"), 0);
    CHECK_INT(settingoptedout("b"), 0);
    CHECK_INT(settingoptedout("c"), 0);
    CHECK_INT(settingoptedout("d"), 0);
    CHECK_INT(settingoptedout("e"), 0);
    CHECK_INT(settingoptedout("f"), 0);
    CHECK_INT(settingoptedout("g"), 0);
    CHECK_INT(settingoptedout("h"), 0);

    tw_case("an empty or all-whitespace value leaves it ON");
    loadtext("a=\n");
    CHECK_INT(settingoptedout("a"), 0);
    settings["b"] = " \t\r\n\v\f";
    CHECK_INT(settingoptedout("b"), 0);

    tw_case("it trims its own whitespace, the same set as the opt-in one");
    settings["a"] = "\v\ffalse\v\f";
    CHECK_INT(settingoptedout("a"), 1);
    settings["b"] = "\r\n0\r\n";
    CHECK_INT(settingoptedout("b"), 1);

    tw_case("🔴 the two predicates are NOT complements of each other");
    /* Where they agree is the design: absent, blank and garbage all mean "no
     * opinion", so each predicate falls back to ITS OWN default. Replace
     * settingoptedout() with !settingoptedin() and every row below flips. */
    loadtext("absent_is_not_here=x\nblank=\ngarbage=yes\nonword=true\noffword=false\n");
    CHECK_INT(settingoptedin("absent"), 0);
    CHECK_INT(settingoptedout("absent"), 0);
    CHECK_INT(settingoptedin("blank"), 0);
    CHECK_INT(settingoptedout("blank"), 0);
    CHECK_INT(settingoptedin("garbage"), 0);
    CHECK_INT(settingoptedout("garbage"), 0);
    /* And where they disagree, each answers only for its own word. */
    CHECK_INT(settingoptedin("onword"), 1);
    CHECK_INT(settingoptedout("onword"), 0);
    CHECK_INT(settingoptedin("offword"), 0);
    CHECK_INT(settingoptedout("offword"), 1);
}

/* --- the section table's own shape ---------------------------------------- *
 *
 * MOD (Jeremy, jc-56). SECTIONS[] rows are BOTH sentinel-terminated and
 * length-bounded, and the comment above the table says why: fill every slot
 * with real keys and the terminator quietly disappears, and savesettings()
 * walks into the next row's name.
 *
 * 🔴 THIS IS NOT HYPOTHETICAL, IT IS WHAT jc-56 ALMOST DID. [Display] sat at
 * eleven of twelve slots and this release added two keys to it. The jc-41
 * comment predicting that ("the NEXT [Display] setting must raise
 * SECTION_MAXKEYS") was read and acted on -- but a comment is not a check, and
 * the next person to add a key gets this instead. */

static void test_sectiontable(void)
{
    size_t s, k;

    tw_case("🔴 every SECTIONS row is terminated inside SECTION_MAXKEYS");
    for (s = 0 ; s < sizeof SECTIONS / sizeof *SECTIONS ; ++s) {
        int terminated = 0;
        for (k = 0 ; k < (size_t)SECTION_MAXKEYS ; ++k)
            if (!SECTIONS[s].keys[k]) { terminated = 1; break; }
        CHECK_MSG(terminated,
                  "section [%s] fills all %d slots: raise SECTION_MAXKEYS",
                  SECTIONS[s].name, SECTION_MAXKEYS);
    }

    tw_case("no key is listed under two sections");
    /* A duplicate would be written twice and read back as whichever came last,
     * which is a silent way to lose a setting's grouping. */
    for (s = 0 ; s < sizeof SECTIONS / sizeof *SECTIONS ; ++s)
        for (k = 0 ; SECTIONS[s].keys[k] ; ++k) {
            size_t s2, k2;
            int seen = 0;
            for (s2 = 0 ; s2 < sizeof SECTIONS / sizeof *SECTIONS ; ++s2)
                for (k2 = 0 ; SECTIONS[s2].keys[k2] ; ++k2)
                    if (!strcmp(SECTIONS[s].keys[k], SECTIONS[s2].keys[k2]))
                        ++seen;
            CHECK_MSG(seen == 1, "key \"%s\" appears %d times in SECTIONS",
                      SECTIONS[s].keys[k], seen);
        }

    tw_case("jc-56's two title keys are in the table, so they are written back");
    /* A setting missing from SECTIONS[] still works, but lands under [Other]
     * rather than beside the other display switches -- which is how
     * lynxtileset and mstileset shipped wrong for two releases. */
    {
        int foundpack = 0, foundname = 0;
        for (s = 0 ; s < sizeof SECTIONS / sizeof *SECTIONS ; ++s)
            for (k = 0 ; SECTIONS[s].keys[k] ; ++k) {
                if (!strcmp(SECTIONS[s].keys[k], "showlevelpack"))
                    foundpack = !strcmp(SECTIONS[s].name, "Display");
                if (!strcmp(SECTIONS[s].keys[k], "showlevelname"))
                    foundname = !strcmp(SECTIONS[s].name, "Display");
            }
        CHECK_MSG(foundpack, "showlevelpack is not under [Display] in SECTIONS");
        CHECK_MSG(foundname, "showlevelname is not under [Display] in SECTIONS");
    }
}

/* --- the round trip ------------------------------------------------------ */

static void test_roundtrip(void)
{
    std::string	first, second, map1;

    tw_case("a known key is written under its own section heading");
    loadtext("volume=8\n");
    savesettings();
    CHECK_STR(filetext(), plat("[Sound]\nvolume=8\n"));

    tw_case("🔴 an UNKNOWN key survives the round trip, under [Other]");
    /* The half that stops this fork silently eating a setting that a future
     * upstream release adds. savesettings() rewrites the whole file from the
     * map, so without the leftovers pass the key would be gone on first exit. */
    loadtext("somefuturekey=42\n");
    savesettings();
    CHECK_STR(filetext(), plat("[Other]\nsomefuturekey=42\n"));
    loadsettings();
    CHECK_STR(val("somefuturekey"), "42");

    tw_case("known and unknown keys are both kept, in that order");
    loadtext("somefuturekey=42\nvolume=8\n");
    savesettings();
    CHECK_STR(filetext(),
	      plat("[Sound]\nvolume=8\n\n[Other]\nsomefuturekey=42\n"));

    tw_case("keys are grouped under the section each one belongs to");
    loadtext("volume=8\nbgcolor=#285080\nselectedruleset=2\n");
    savesettings();
    CHECK_STR(filetext(),
	      plat("[Display]\nbgcolor=#285080\n"
		   "\n[Game]\nselectedruleset=2\n"
		   "\n[Sound]\nvolume=8\n"));

    tw_case("🔴 load then save then load leaves the map identical");
    /* The property the fuzz target generalizes. Every tolerance above is a
     * chance for the writer and the reader to disagree, and a disagreement
     * would mutate the user's file a little more on every exit. */
    loadtext("[Display]\nbgcolor = #285080  \n; comment\n\n"
	     "volume=8\nsomefuturekey=a=b\na;b=1\nvolume=9\n");
    map1 = dumpmap();
    savesettings();
    loadsettings();
    CHECK_STR(maptext(), map1.c_str());

    tw_case("🔴 and writing it twice produces the same bytes");
    savesettings();
    first = getfile();
    loadsettings();
    savesettings();
    second = getfile();
    CHECK_STR(second.c_str(), first.c_str());

    tw_case("a settings file the program wrote re-reads as what it held");
    loadtext("");
    setintsetting("volume", 10);
    setstringsetting("selectedseries", "CCLP5.dat-ms.dac");
    setstringsetting("mstileset", "");
    savesettings();
    settings.clear();
    loadsettings();
    CHECK_INT(getintsetting("volume"), 10);
    CHECK_STR(getstringsetting("selectedseries"), "CCLP5.dat-ms.dac");
    CHECK_STR(getstringsetting("mstileset"), "");

    tw_case("🔴 the shipped stock file comes back BYTE FOR BYTE");
    /* The layout package.ps1 ships, in the platform's line endings. This is the
     * strongest statement the round trip can make and it is the one SuperCC
     * makes about its own settings file (449 bytes in, 449 identical out): the
     * program reproduces its own format exactly, so merely running the game
     * never rewrites a user's file into something else. */
    /* ⚠ THIS LITERAL IS A SECOND COPY OF package.ps1's STOCK FILE, and it had
     * silently drifted from it once already: jc-56 added two keys there and
     * every case here still passed, because the round trip is happy to
     * reproduce whatever it is handed. verify-defaults.ps1 now compares the two
     * character for character, which is the only reason this copy is allowed to
     * exist -- a literal is what makes "byte for byte" a real assertion, and a
     * checked duplicate is not the same hazard as an unchecked one. */
    loadtext(plat("[Display]\n"
		  "bgcolor=#285080\ndeathcount=0\ndisplayccx=1\n"
		  "forceshowtimer=0\nlegacyscores=false\nlynxtileset=\n"
		  "mstileset=\nshowbuildtag=false\nshowdeathcounter=false\n"
		  "showinitstate=0\nshowlevelname=true\nshowlevelpack=false\n"
		  "\n[Game]\n"
		  "ignorepasswords=false\nselectedruleset=2\nselectedseries=\n"
		  "\n[Sound]\nvolume=10\n"));
    CHECK_INT((int)settings.size(), 16);
    first = getfile();
    map1 = dumpmap();
    savesettings();
    CHECK_STR(filetext(), first.c_str());
    loadsettings();
    CHECK_INT((int)settings.size(), 16);
    CHECK_STR(maptext(), map1.c_str());

    tw_case("...and the shipped file's missing final newline is restored");
    /* The copy in the zip ends without one -- package.ps1 writes it that way --
     * and the writer emits one. So the first save a user's install performs adds
     * exactly one line ending and changes nothing else. Recorded so a future
     * "the file changed!" is measured against what is expected. */
    second = first;
#ifdef WIN32
    second.erase(second.size() - 2);	/* the trailing CRLF */
#else
    second.erase(second.size() - 1);	/* the trailing LF */
#endif
    puttext(second.c_str());
    settings.clear();
    settingsUnreadable = false;
    loadsettings();
    CHECK_INT((int)settings.size(), 16);
    savesettings();
    CHECK_STR(filetext(), first.c_str());
}

/* --- refusing to write ---------------------------------------------------- */

static void test_unreadable(void)
{
    std::string	before;
    char	dirpath[512];
    char const *savedname;

    tw_case("🔴 a settings file that exists but will not open is NOT overwritten");
    /* loadsettings() leaves the map empty on a failed open and savesettings()
     * rewrites the whole file from that map -- so without the latch, one
     * unreadable moment would erase every setting permanently.
     *
     * ⚠ "EXISTS BUT WILL NOT OPEN" IS CONSTRUCTED DIFFERENTLY PER PLATFORM, and
     * an earlier version of this case got that wrong in a way that mattered. It
     * used a DIRECTORY at the settings path and called that portable. It is not:
     * on Windows opening a directory as a file fails, but on POSIX the open
     * SUCCEEDS and the error only surfaces at read, so the stat() branch this
     * case exists to reach would never have run on the Linux CI job.
     *
     * Windows keeps the directory. POSIX writes a real file and chmods it to
     * mode 0, which blocks the open unambiguously for a non-root user -- and the
     * GitHub runner is non-root. (_chmod on Windows only toggles the read-only
     * attribute and does not block reads, which is why this is not shared.) */
    savedname = sfname;
    sfname = "unreadable";
    snprintf(dirpath, sizeof dirpath, "%s/unreadable", scratchdir);
#ifdef WIN32
    (void)removedir(dirpath);
    CHECK_MSG(makedir(dirpath), "could not create %.100s", dirpath);
#else
    {
	FILE   *f = fopen(dirpath, "wb");
	CHECK_MSG(f != NULL, "could not create %.100s", dirpath);
	if (f) {
	    fputs("volume=8\n", f);
	    fclose(f);
	}
	CHECK_MSG(chmod(dirpath, 0) == 0, "could not chmod %.100s to 0", dirpath);
    }
#endif

    settings.clear();
    settingsUnreadable = false;
    warn_count = 0;
    loadsettings();
    CHECK_INT(settingsUnreadable, 1);
    CHECK(warn_count > 0);

    /* 🔴 AND NOW THE POINT, WHICH THIS CASE USED TO MISS ENTIRELY. It asserted
     * that the directory was still a directory -- but the OS refuses to rename
     * over a directory whatever the latch does, so the check passed with the
     * latch REMOVED. Verified by mutation: neutering settingsUnreadable left the
     * whole file green.
     *
     * The observable that actually belongs to the latch is SILENCE. Every other
     * way savesettings() can decline to write warns first; the latch is the one
     * path that returns without a word. So: no warning means the latch stopped
     * it, and a warning means something else did. */
    setintsetting("volume", 3);
    warn_count = 0;
    savesettings();
    CHECK_INT(warn_count, 0);
    CHECK_INT(settingsUnreadable, 1);
    CHECK_INT(counttemps(), 0);
    sfname = savedname;
#ifdef WIN32
    CHECK(removedir(dirpath));
#else
    (void)chmod(dirpath, 0600);
    CHECK_INT(remove(dirpath), 0);
#endif

    tw_case("an ABSENT file is a first run, and is not treated as unreadable");
    /* Absent and unreadable are different things, and only the first may be
     * overwritten. */
    remove(inipath);
    settings.clear();
    settingsUnreadable = false;
    warn_count = 0;
    loadsettings();
    CHECK_INT(settingsUnreadable, 0);
    CHECK_INT((int)settings.size(), 0);
    setintsetting("volume", 7);
    savesettings();
    CHECK_STR(filetext(), plat("[Sound]\nvolume=7\n"));

    tw_case("🔴 a path too long to build refuses to load AND to save");
    /* getpathforfileindir() returns NULL there, and handing that to ifstream is
     * undefined behavior rather than an error. */
    puttext("volume=8\n");
    before = getfile();
    settings.clear();
    settingsUnreadable = false;
    warn_count = 0;
    fake_pathtoolong = 1;
    loadsettings();
    CHECK_INT(settingsUnreadable, 1);
    CHECK(warn_count > 0);
    setintsetting("volume", 3);
    savesettings();
    fake_pathtoolong = 0;
    CHECK_STR(filetext(), before.c_str());
}

/* --- the staging file ----------------------------------------------------- *
 *
 * Counting what is left in the scratch directory is the only way to see the
 * cleanup path from outside, and the temp's name carries a pid and a sequence
 * number, so it cannot simply be guessed. */

/* Both walks below take `unlink` rather than being written twice: counting and
 * sweeping differ only in what they do with each name. */
static int walktemps(int unlink)
{
    int		n = 0;
    char	path[512];
#ifdef WIN32
    WIN32_FIND_DATAA	fd;
    char		pattern[512];
    HANDLE		h;

    snprintf(pattern, sizeof pattern, "%s/tw_settings.ini.tmp-*", scratchdir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
	do {
	    ++n;
	    if (unlink) {
		snprintf(path, sizeof path, "%s/%s", scratchdir, fd.cFileName);
		remove(path);
	    }
	} while (FindNextFileA(h, &fd));
	FindClose(h);
    }
#else
    DIR		       *d = opendir(scratchdir);
    struct dirent      *e;

    if (d) {
	while ((e = readdir(d)) != NULL) {
	    if (strncmp(e->d_name, "tw_settings.ini.tmp-",
			sizeof "tw_settings.ini.tmp-" - 1))
		continue;
	    ++n;
	    if (unlink) {
		snprintf(path, sizeof path, "%s/%s", scratchdir, e->d_name);
		remove(path);
	    }
	}
	closedir(d);
    }
#endif
    return n;
}

static int counttemps(void) { return walktemps(0); }
static void sweeptemps(void) { (void)walktemps(1); }

/* Remove the scratch directory, retrying briefly.
 *
 * ⚠ WITHOUT THE RETRY THIS IS A FLAKY RED. Measured: with a scanner holding the
 * scratch settings file open with FILE_SHARE_READ (or READ|WRITE), rmdir fails
 * ENOTEMPTY -- and that is the very share mode this file's own locked-
 * destination case constructs, so Defender, Windows Search or a sync client
 * touching the directory at the wrong instant would fail a release gate for an
 * environmental hiccup. The same reasoning savesettings() applies to its own
 * replace, applied to the test's cleanup. */
static int removescratch(char const *dir)
{
    int		i;

    for (i = 0 ; i < 5 ; ++i) {
	if (removedir(dir))
	    return 1;
#ifdef WIN32
	Sleep(20);
#endif
    }
    return removedir(dir);
}

static void test_staging(void)
{
    std::string	before;

    tw_case("a successful write leaves no staging file behind");
    loadtext("volume=8\n");
    setintsetting("volume", 9);
    savesettings();
    CHECK_INT(counttemps(), 0);
    CHECK_STR(filetext(), plat("[Sound]\nvolume=9\n"));

    tw_case("🔴 a staging path too long to stage FALLS BACK and still saves");
    /* THE "NEVER WORSE THAN BEFORE" CASE. The temp path is the destination plus
     * ".tmp-<pid>-<seq>", so it is always longer than the destination and there
     * is a band of install depths where the settings file loads fine and no
     * staging file can be created at all. Refusing there would mean the program
     * could never save -- where the writer it replaced saved happily. So
     * staging that is structurally IMPOSSIBLE falls back to the direct write.
     *
     * 🔴 THE BUDGET IS SET FROM THE DESTINATION'S OWN LENGTH, and that is the
     * whole point of the case. An earlier version used a flat 8, which is
     * shorter than the destination itself -- so it exercised "nothing fits" and
     * not the band it names, and a mutation measuring the DESTINATION rather
     * than the staging path went undetected. Two more than the destination means
     * the destination fits and the suffix cannot. */
    loadtext("volume=8\n");
    setintsetting("volume", 9);
    fake_pathbufferlen = (int)strlen(inipath) + 2;
    savesettings();
    fake_pathbufferlen = 1024;
    CHECK_STR(filetext(), plat("[Sound]\nvolume=9\n"));
    CHECK_INT(counttemps(), 0);

    tw_case("...and the fallback still writes the file's own format");
    /* The fallback is a different stream from the staged path, so it is its own
     * chance to lose the CRLF translation. */
    loadtext("volume=8\nsomefuturekey=x\n");
    setintsetting("volume", 3);
    fake_pathbufferlen = (int)strlen(inipath) + 2;
    savesettings();
    fake_pathbufferlen = 1024;
    CHECK_STR(filetext(),
	      plat("[Sound]\nvolume=3\n\n[Other]\nsomefuturekey=x\n"));

#ifdef WIN32
    tw_case("🔴 a LOCKED destination: the file survives and nothing is left over");
    /* THE MEASUREMENT THIS WHOLE CHANGE EXISTS FOR. A sync client or scanner
     * opens a file with a permissive share mode rather than exclusively -- and
     * under exactly that mode the OLD truncating writer SUCCEEDED in emptying
     * the file, while MoveFileEx fails with ERROR_ACCESS_DENIED. So this is both
     * the case the fix protects and the case a naive atomic write would turn
     * into a silently dropped write.
     *
     * What must be true after a failed save: the destination still holds every
     * byte it held, no staging file is left in the directory, and the failure
     * was at least recorded.
     *
     * 🔴 IT IS ALSO THE ONLY WITNESS THAT A BLOCKED WRITE DOES NOT FALL BACK.
     * The two cases above prove that staging which is IMPOSSIBLE falls back to a
     * direct write; this one proves that staging which is merely BLOCKED does
     * not -- if it fell back it would write volume=9 here, and the unchanged-
     * bytes check below would fail. That distinction is what keeps the
     * truncating write out of exactly the situations where a torn file is most
     * likely, so it is the assertion to protect if this case is ever edited.
     *
     * ⚠ And it is Windows-only, so on Linux nothing witnesses it. Constructing
     * the equivalent needs a second process, because POSIX rename() cannot fail
     * for a lock at all. */
    loadtext("volume=8\nselectedseries=CCLP5.dat-ms.dac\n");
    before = getfile();
    setintsetting("volume", 9);
    {
	HANDLE	h = CreateFileA(inipath, GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, 0, NULL);

	CHECK(h != INVALID_HANDLE_VALUE);
	if (h != INVALID_HANDLE_VALUE) {
	    warn_count = 0;
	    replaceattempts = 0;
	    savesettings();
	    CloseHandle(h);

	    CHECK_STR(filetext(), before.c_str());
	    CHECK_INT(counttemps(), 0);
	    CHECK(warn_count > 0);

	    /* 🔴 AND THE RETRY ACTUALLY HAPPENED. Without this the backoff table
	     * is unpinned: cutting it to a single attempt left the whole file
	     * green, because every successful write in every other case succeeds
	     * on the first try. The design comment in settings.cpp insists the
	     * retry matters more than the atomicity does -- measured, one attempt
	     * loses 19% of writes and four lose none -- so it needs a witness.
	     *
	     * ⚠ THIS WAS A WALL-CLOCK FLOOR UNTIL jc-56 AND IT FLAKED, on the
	     * release build of a commit whose CI build had just passed. The old
	     * comment argued it could not: "Sleep can only ever overshoot." Sleep
	     * does -- but GetTickCount64 advances in ~15.6 ms steps, so the
	     * MEASUREMENT undershoots regardless, and how long Sleep(2) really
	     * takes depends on a system timer resolution any other process can
	     * change. The maintainer's desktop measured 71-80 ms; the CI runner
	     * measured 16 against a 30 ms floor.
	     *
	     * The count is exact and is the property that was actually meant. Four
	     * is the length of settings.cpp's backoff table; if that table is
	     * deliberately resized, change this number to match it and say why in
	     * both places. It does NOT witness that a lock which clears mid-call
	     * is recovered -- that needs a second thread. */
	    CHECK_MSG(replaceattempts == 4,
		      "the failed replace made %u attempt(s), not 4:"
		      " the backoff loop cannot have run to the end",
		      replaceattempts);
	}
    }

    tw_case("...and the very next write, unlocked, succeeds");
    /* The in-memory map still holds the change, so nothing was lost permanently
     * -- which is the argument for accepting a dropped write as the failure. */
    savesettings();
    CHECK_STR(filetext(), plat("[Game]\nselectedseries=CCLP5.dat-ms.dac\n"
			       "\n[Sound]\nvolume=9\n"));
    CHECK_INT(counttemps(), 0);
#else
    tw_case("a LOCKED destination");
    tw_skip("Windows-only: POSIX rename() cannot fail for an advisory lock,"
	    " so there is no equivalent condition to construct");
#endif
}

/* --- what an interrupted write leaves behind ------------------------------ */

static void test_zerolength(void)
{
    tw_case("⚠ a ZERO-BYTE settings file loads as 'no settings', SILENTLY");
    /* This is the shape a file took when a write was interrupted -- measured,
     * not supposed: killing a process between the old truncating open and the
     * flush turned a 289-byte settings file into a 0-byte one. The atomic write
     * is what stops this program CREATING that state.
     *
     * 🔴 THE SILENCE IS DELIBERATE AND WAS ARRIVED AT THE HARD WAY. A warning
     * here was written first, and test/run-e2e.ps1 rejected it within a minute:
     * `tworld2 -v` sets no settings, so it WRITES a zero-byte file every batch
     * run, and the next run then warned about a file the program had just
     * produced itself. Zero length is not evidence of damage. Nor may loading
     * one be treated as unreadable -- refusing to write would leave a program
     * that had written it unable to ever save again.
     *
     * So this case asserts the absence of a warning, and it is the regression
     * test for adding one back. */
    loadtext("");
    CHECK_INT((int)settings.size(), 0);
    CHECK_INT(settingsUnreadable, 0);
    CHECK_INT(warn_count, 0);

    tw_case("an empty map writes a zero-length file");
    /* So zero length is also a legitimately reachable OUTPUT, which is why
     * "treat a zero-byte file as damaged" is not a safe rule to add. */
    settings.clear();
    savesettings();
    CHECK_INT((int)getfile().size(), 0);
}

/* --- the fuzz corpus ------------------------------------------------------ *
 *
 * ADR 0011: a fuzz finding is not fixed until its input is committed and a case
 * replays it here, so the fix is checked on every platform and every push --
 * not only on Linux, where libFuzzer runs. */

static int	settingscorpus_replayed = 0;
static int	settingscorpus_failed = 0;

/* The same property fuzz_settings.cpp asserts, run against one committed input:
 * reading is idempotent under writing. */
static void settingscorpus_read(twcorpusinput const *in)
{
    std::map<std::string, std::string>	firstmap, secondmap;
    std::string				rendered, again;

    ++settingscorpus_replayed;

    {
	std::string const	bytes((char const *)in->data, (size_t)in->size);
	std::istringstream	s(bytes);
	parsesettings(s, firstmap);
    }
    settings = firstmap;
    {
	std::ostringstream	s;
	rendersettings(s);
	rendered = s.str();
    }
    {
	std::istringstream	s(rendered);
	parsesettings(s, secondmap);
    }
    if (firstmap != secondmap) {
	++settingscorpus_failed;
	return;
    }
    settings = secondmap;
    {
	std::ostringstream	s;
	rendersettings(s);
	again = s.str();
    }
    if (rendered != again)
	++settingscorpus_failed;
}

static void settingscorpus_report(twcorpusverdict v, char const *name)
{
    CHECK_MSG(v == TW_CORPUS_OK, "corpus input %.60s: %s",
	      name, tw_corpus_why(v));
}

static void test_corpus(void)
{
    char	dir[256];
    int		c;

    tw_case("🔴 every committed fuzz input still round-trips");
    /* One of these is the four-byte reproducer for the trailing-carriage-return
     * defect that the fuzz target found the day it was written -- a value ending
     * in a CR came back different, so merely running the game changed a setting.
     * See test/fuzz/corpus/settings/cr-before-space. */
    CHECK_MSG(tw_corpus_dir("settings", dir, sizeof dir),
	      "could not find test/fuzz/corpus/settings from the working"
	      " directory -- the replay would have proved nothing");
    if (dir[0]) {
	c = tw_corpus_run(dir, settingscorpus_read, settingscorpus_report);
	CHECK_MSG(c > 0, "corpus directory %.100s held no inputs", dir);
	CHECK_INT(settingscorpus_replayed, c);
	CHECK_MSG(settingscorpus_failed == 0,
		  "%d of %d committed input(s) did not round-trip",
		  settingscorpus_failed, c);
    }
}

/* --- the defect the corpus above exists for ------------------------------- */

static void test_carriagereturn(void)
{
    tw_case("🔴 a value ending in a CARRIAGE RETURN survives the round trip");
    /* THE DEFECT, in four bytes. "k=v\r   " puts the CR in the INTERIOR of the
     * line, so the line-level CR strip does not fire -- the line ends in spaces.
     * Trimming those spaces then exposed the CR as the last byte of the value.
     * The writer emitted "k=v\r" followed by its own newline, and the next load
     * stripped the CR and got a different value: one round trip through the
     * program silently changed a setting.
     *
     * Found by test/fuzz/fuzz_settings.cpp on its first real run, on an input
     * no person would have thought to write. */
    loadtext("k=v\r   \n");
    CHECK_STR(val("k"), "v");
    savesettings();
    loadsettings();
    CHECK_STR(val("k"), "v");

    tw_case("...and so does a NAME with a carriage return before the equals");
    loadtext("k\r =v\n");
    CHECK_STR(val("k"), "v");
    CHECK_INT((int)settings.size(), 1);

    tw_case("a line of nothing but a carriage return is blank, not a key");
    loadtext("\r\nvolume=8\n");
    CHECK_INT((int)settings.size(), 1);
    CHECK_STR(val("volume"), "8");
}

int main(void)
{
    tw_begin("settings_test.c");

    /* Everything runs inside a scratch directory: settingsdir() returns appdir,
     * so pointing appdir here is what keeps the test away from any real
     * tw_settings.ini -- including the one in the repository root. */
    (void)makedir(scratchdir);
    appdir = (char *)scratchdir;
    savedir = (char *)scratchdir;
    snprintf(inipath, sizeof inipath, "%s/tw_settings.ini", scratchdir);

    /* Sweep any staging file a PREVIOUS run left behind. Without this a single
     * failed run poisons every run after it -- the cases below assert that no
     * staging file exists, and they mean "none that THIS run created". Found the
     * hard way while mutation-testing: a deliberately broken build leaked one,
     * and the next, correct build then failed for the wrong reason. */
    sweeptemps();

    test_parsebasics();
    test_lineendings();
    test_accessors();
    test_optedin();
    test_optedout();
    test_sectiontable();
    test_roundtrip();
    test_staging();
    test_unreadable();
    test_zerolength();
    test_carriagereturn();
    test_corpus();

    /* Its own case, so a failure here names the cleanup rather than being
     * attributed to whichever case ran last -- a red line reading "every
     * committed fuzz input still round-trips" when the corpus was fine sends
     * the next reader somewhere useless. */
    tw_case("the scratch directory is removed");
    remove(inipath);
    CHECK_MSG(removescratch(scratchdir),
	      "the scratch directory %s could not be removed", scratchdir);

    /* Raise this when cases are added; never lower it to make a run pass.
     *
     * 177, not the 183 a Windows run reports, and the arithmetic is worth
     * writing down because the first version of this comment got it wrong twice.
     * Two regions differ by platform:
     *
     *   the locked-destination block   7 checks on Windows, 0 on POSIX  (-7)
     *   the "will not open" setup      2 checks on Windows, 3 on POSIX  (+1)
     *
     * so POSIX reaches 183 - 7 + 1 = 177, and that is the number every platform
     * is guaranteed to hit. A floor with slack in it is the failure this
     * mechanism exists to report: it would let a case stop running while the
     * suite stayed green.
     *
     * jc-56 raised this from 128: test_optedout() adds 28 checks and
     * test_sectiontable() 21, none of them platform-dependent. ⚠ Twelve of
     * test_sectiontable()'s are derived from the NUMBER OF KEYS in SECTIONS[],
     * so adding a setting raises the real count on its own -- which is fine for
     * a floor, but do not read this number as an exact total. */
    tw_expect_atleast(183);
    return tw_end();
}
