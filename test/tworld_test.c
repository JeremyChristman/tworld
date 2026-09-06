/* tworld_test.c: level navigation, the part of tworld.c a player drives.
 *
 * MOD (Jeremy). tworld.c is 2,505 lines and is the largest file in the tree
 * with no unit coverage. The end-to-end layer drives its COMMAND LINE, but the
 * navigation logic below only runs from the pre-level screen, which no
 * automated layer reaches: it needs a GUI and a keystroke.
 *
 * WHY THESE FIVE FUNCTIONS AND NOT THE OTHER TWO THOUSAND LINES. CLAUDE.md
 * section 7 lists the things here that look like bugs and are load-bearing, and
 * two of them are in this file:
 *
 *   item 6  changecurrentgamewrapped() is SEPARATE from changecurrentgame(),
 *           because three of the latter's callers are not the player
 *           navigating and wrapping breaks each of them.
 *   item 7  "last level" is `count - 1`, NOT islastinseries() -- which also
 *           answers TRUE for a .dac's `lastlevel=` line, and stock
 *           CCLP1-MS.dac sets that to 144 over a 149-level .dat.
 *
 * Both are one-line distinctions that a future edit would "simplify" away, and
 * neither had a test. That is what this file is for. Everything else in
 * tworld.c is option parsing (covered by test/run-e2e.ps1 against the real
 * executable) or GUI plumbing.
 *
 * ⚠ THE STUB BLOCK IS LARGE AND THAT IS INHERENT. tworld.c is the program's
 * main translation unit, so including it pulls in references to all 91 symbols
 * the rest of the program provides. They were generated from the real headers
 * rather than hand-written, so a signature cannot drift from its declaration
 * without a compile error. None of them is called by the cases below; a stub
 * that is reached would be a bug in the test, not coverage.
 *
 * 🔴 `main` IS RENAMED, NOT REMOVED. #define main tworld_main before the
 * include, so this file can supply its own. The alternative -- extracting the
 * functions into a new file -- would mean testing a copy of the code instead of
 * the code, which is the whole point of docs/adr/0003.
 *
 * TESTLANG: c
 *
 * tworld.c is compiled only as C by CMake and relies on C's implicit void*
 * conversion. See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-unused-parameter -Wno-unused-function
 *
 * Both are for the STUB BLOCK: every stub ignores its parameters by
 * construction, and tworld.c has file-scope helpers the navigation cases never
 * call. Neither flag covers anything in the cases themselves.
 */

#include	"tw_test.h"

#include	"../defs.h"
#include	"../fileio.h"
#include	"../err.h"
#include	"../series.h"
#include	"../solution.h"
#include	"../play.h"
#include	"../score.h"
#include	"../settings.h"
#include	"../res.h"
#include	"../oshw.h"
#include	"../unslist.h"
#include	"../help.h"
#include	"../cmdline.h"

/* --- the program's other modules, stubbed ------------------------------- *
 *
 * Generated from the declarations in the headers above. See the note in the
 * file header: none of these is reached by the cases below. */

int	batchmode = FALSE;
int	readonly = FALSE;
char   *resdir = NULL;
char   *savedir = NULL;
char   *seriesdatdir = NULL;
char   *seriesdir = NULL;
tablespec const *vourzhon = NULL;
tablespec const *yowzitch = NULL;

void advanceinitrandomff(int display) { }
int advancetick(void) { return 0; }
int changestepping(int delta, int display) { return 0; }
int changevolume(int delta, int display) { return 0; }
int checksolution(void) { return 0; }
void cleardisplay(void) { }
void clearsolutions(gameseries *series) { }
int combinepath(char *dest, char const *dir, char const *path) { return 0; }
void copytoclipboard(char const *text) { }
int deletesolution(void) { return 0; }
void ding(void) { }
int doturn(int cmd) { return 0; }
int drawscreen(int showframe) { return 0; }
int endgamestate(void) { return 0; }
void fileclose(fileinfo *file, char const *msg) { }
int filegetline(fileinfo *file, char *buf, int *len, char const *msg) { return 0; }
void freeallresources(void) { }
void freescorelist(int *plevellist, tablespec *table) { }
void freeseriesdata(gameseries *series) { }
void freesolutionfilelist(char const **filelist, tablespec *table) { }
int getintsetting(char const * name) { return 0; }
int getreplaysecondstoskip(void) { return 0; }
char const * getstringsetting(char const * name) { return 0; }
int haspathname(char const *name) { return 0; }
/* 🔴 NOT INERT, UNLIKE EVERY OTHER STUB HERE. The password gate in
 * changecurrentgame() asks whether the PREVIOUS level is solved, so a stub
 * returning 0 silently disables half the logic under test. That is exactly
 * what the first version of this file did: two cases failed and the cause was
 * the stub, not the code. Faithful to play.c:491. */
int hassolution(gamesetup const* game) { return game->besttime != TIME_NIL; }
int initgamestate(gamesetup* game, int ruleset) { return 0; }
void initoptions(cmdlineinfo* opt, int argc, char** argv, char const* list) { }
int initresources(void) { return 0; }
int input(int wait) { return 0; }
tablespec const *keyboardhelp(int context) { return 0; }
char const* leveltimes(gameseries const *series) { return 0; }
void loadsettings(void) { }
void onlinecontexthelp(int topic) { }
void onlinemainhelp(int topic) { }
int prepareplayback(void) { return 0; }
int quitgamestate(void) { return 0; }
void readextensions(struct gameseries *series) { }
int readoption(cmdlineinfo* opt) { return 0; }
int readseriesfile(gameseries *series) { return 0; }
int readsolutions(gameseries *series) { return 0; }
void recorddeath(void) { }
int replacesolution(void) { return 0; }
void savesettings(void) { }
int savesolutions(gameseries *series) { return 0; }
int secondsplayed(void) { return 0; }
int setdisplaymsg(char const *msg, int msecs, int bold) { return 0; }
void setenddisplay(void) { }
void setgameplaymode(int mode) { }
int setkeyboardinputmode(int enable) { return 0; }
int setkeyboardrepeat(int enable) { return 0; }
int setmudsuckingfactor(int mud) { return 0; }
void setpedanticmode(void) { }
void setsoundeffects(int action) { }
int setstepping(int stepping, int display) { return 0; }
void setstringsetting(char const * name, char const * val) { }
void setsubtitle(char const *subtitle) { }
int setvolume(int volume, int display) { return 0; }
void shutdowngamestate(void) { }
char *skippathname(char const *name) { return 0; }
void toggleshowinitstate(void) { }
int waitfortick(void) { return 0; }

/* Real-ish rather than inert: tworld.c stores paths in these buffers during
 * initialization, and a NULL would fault before any case ran. A static arena
 * is enough -- nothing here inspects a path. */
static char pathbufs[8][512];
static int  pathbufnext = 0;
char *getpathbuffer(void)
{
    char *p = pathbufs[pathbufnext % 8];
    ++pathbufnext;
    p[0] = '\0';
    return p;
}
int getpathbufferlen(void) { return 511; }
void clearfileinfo(fileinfo *file) { if (file) memset(file, 0, sizeof *file); }

/* The multi-line declarations, written out by hand for the same reason as the
 * rest: matched to the header so a drift is a compile error. */
int createscorelist(gameseries const *series, int usepasswds, char zchar,
		    int **plevellist, int *pcount, tablespec *table)
{ return 0; }
int createserieslist(char const *preferredfile, gameseries **pserieslist,
		     int *pcount, mapfileinfo **pmflist, int *pmfcount,
		     tablespec *table)
{ return 0; }
int createsolutionfilelist(gameseries const *series, int morethanone,
			   char const ***pfilelist, int *pcount,
			   tablespec *table)
{ return 0; }
int createtimelist(gameseries const *series, int showpartial, char zchar,
		   int **plevellist, int *pcount, tablespec *table)
{ return 0; }
int displayendmessage(int basescore, int timescore, long totalscore,
		      int completed)
{ return 0; }
int displayinputprompt(char const *prompt, char *input, int maxlen,
		       InputPromptType inputtype, int (*inputcallback)(void))
{ return 0; }
int displaylist(char const *title, tablespec const *table, int *index,
		DisplayListType listtype, int (*inputcallback)(int*))
{ return 0; }
int findlevelinseries(gameseries const *series, int number, char const *passwd)
{ return 0; }
void freeserieslist(gameseries *list, int count, mapfileinfo *mflist,
		    int mfcount, tablespec *table)
{ }
int getscoresforlevel(gameseries const *series, int level, int *base,
		      int *bonus, long *total)
{ return 0; }
void getseriesfromlist(gameseries *dest, gameseries const *list, int index)
{ }
int loadsolutionsetname(char const *filename, char *buffer, int buffersize)
{ return 0; }
int openfileindir(fileinfo *file, char const *dir, char const *filename,
		  char const *mode, char const *msg)
{ return 0; }
int oshwinitialize(int silence, int soundbufsize, int showhistogram,
		   int fullscreen)
{ return 0; }

/* --- the source under test ---------------------------------------------- */

#define	main	tworld_main
#include	"../tworld.c"
#undef	main

/* --- the error surface -------------------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *pfx, char const *fmt, ...) { (void)pfx; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

/* --- the harness -------------------------------------------------------- */

static gamespec		gs;
static gamesetup	games[10];

/* Build a series of n levels. `solved` and `haspasswd` are bitmasks over the
 * level index, so a case can describe a partly-played set in one line. */
static void setseries(int n, unsigned solved, unsigned haspasswd, int usepasswds)
{
    int i;

    memset(&gs, 0, sizeof gs);
    memset(games, 0, sizeof games);
    for (i = 0 ; i < n ; ++i) {
	games[i].number = i + 1;
	/* ⚠ TIME_NIL, NOT ZERO, and this is the whole trap. hassolution() is
	 * `besttime != TIME_NIL` (play.c:491), and TIME_NIL is 0x7FFFFFFF --
	 * so the memset above leaves every level reading as SOLVED. The first
	 * version of this harness did exactly that, and the password cases
	 * passed for the wrong reason. Clearing a struct is not the same as
	 * initializing it. */
	games[i].besttime = TIME_NIL;
	if (haspasswd & (1u << i))
	    games[i].sgflags |= SGF_HASPASSWD;
	if (solved & (1u << i))
	    games[i].besttime = 100;
    }
    gs.series.games = games;
    gs.series.count = n;
    gs.series.final = 0;
    gs.currentgame = 0;
    gs.usepasswds = usepasswds;
    ignorepasswds = FALSE;
}

/* --- changecurrentgame: clamping ---------------------------------------- */

static void test_clamping(void)
{
    tw_case("an offset of zero moves nothing and reports FALSE");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 2;
    CHECK_INT(changecurrentgame(&gs, 0), FALSE);
    CHECK_INT(gs.currentgame, 2);

    tw_case("an ordinary step moves and reports TRUE");
    CHECK_INT(changecurrentgame(&gs, +1), TRUE);
    CHECK_INT(gs.currentgame, 3);
    CHECK_INT(changecurrentgame(&gs, -2), TRUE);
    CHECK_INT(gs.currentgame, 1);

    tw_case("🔴 changecurrentgame CLAMPS at both ends, it does not wrap");
    /* The distinction CLAUDE.md section 7 item 6 is about. Three of this
     * function's callers are not the player navigating, and each breaks if it
     * wraps. */
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 0;
    CHECK_MSG(changecurrentgame(&gs, -1) == FALSE,
	      "stepping back from level 1 reported movement");
    CHECK_INT(gs.currentgame, 0);

    gs.currentgame = 4;
    CHECK_MSG(changecurrentgame(&gs, +1) == FALSE,
	      "stepping past the last level reported movement");
    CHECK_INT(gs.currentgame, 4);

    tw_case("a large offset clamps to the end rather than running off it");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 1;
    CHECK_INT(changecurrentgame(&gs, +99), TRUE);
    CHECK_INT(gs.currentgame, 4);
    CHECK_INT(changecurrentgame(&gs, -99), TRUE);
    CHECK_INT(gs.currentgame, 0);

    tw_case("moving resets the Melinda counter");
    /* melindacount tracks consecutive deaths on one level; carrying it across
     * a level change would offer the free pass on the wrong level. */
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 1;
    gs.melindacount = 7;
    changecurrentgame(&gs, +1);
    CHECK_INT(gs.melindacount, 0);
}

/* --- changecurrentgamewrapped: the jc-39 wrap --------------------------- */

static void test_wrapping(void)
{
    tw_case("wrapping forward from the last level reaches the first");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 4;
    CHECK_INT(changecurrentgamewrapped(&gs, +1), TRUE);
    CHECK_INT(gs.currentgame, 0);

    tw_case("wrapping backward from the first level reaches the last");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 0;
    CHECK_INT(changecurrentgamewrapped(&gs, -1), TRUE);
    CHECK_INT(gs.currentgame, 4);

    tw_case("an ordinary move in the middle does not wrap");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 2;
    CHECK_INT(changecurrentgamewrapped(&gs, +1), TRUE);
    CHECK_INT(gs.currentgame, 3);

    tw_case("🔴 a -10 step from the middle CLAMPS; it does not wrap");
    /* The subtlety in the jc-39 note: the wrap happens only when the move
     * failed BECAUSE we were already against the end we were asked to leave.
     * From index 2 of 5, -10 clamps to index 0 and that is a real move -- it
     * must not then jump to the end of the set. */
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 2;
    CHECK_INT(changecurrentgamewrapped(&gs, -10), TRUE);
    CHECK_MSG(gs.currentgame == 0,
	      "a clamped -10 wrapped to %d instead of stopping at 0",
	      gs.currentgame);

    tw_case("🔴 a PASSWORD-blocked move in the middle must not wrap");
    /* ⚠ THIS CASE EXISTS BECAUSE MUTATION TESTING FOUND IT MISSING, and it is
     * the exact scenario the jc-39 comment calls out: "wrap only when that is
     * because we are already against the end we were asked to move past -- not
     * when a password gate refused a level in the middle of the set."
     *
     * The earlier -10 case cannot reach it: a clamped -10 SUCCEEDS, so
     * changecurrentgamewrapped() returns before the wrap logic runs. Only a
     * move that genuinely fails while away from the end gets there, and the
     * password gate is the only thing that produces one.
     *
     * Without the `currentgame == count - 1` half of the test, this jumps the
     * player to the far end of the set instead of doing nothing -- and the
     * suite stayed green until this case was added. */
    setseries(5, 0, 0, TRUE);
    gs.currentgame = 2;
    CHECK_MSG(changecurrentgamewrapped(&gs, +1) == FALSE,
	      "a password-blocked forward move reported movement");
    CHECK_MSG(gs.currentgame == 2,
	      "a password-blocked move in the middle wrapped to %d",
	      gs.currentgame);

    tw_case("a one-level series cannot move, and does not wrap onto itself");
    setseries(1, 0, 0, FALSE);
    CHECK_INT(changecurrentgamewrapped(&gs, +1), FALSE);
    CHECK_INT(gs.currentgame, 0);
    CHECK_INT(changecurrentgamewrapped(&gs, -1), FALSE);
    CHECK_INT(gs.currentgame, 0);
}

/* --- the password gate --------------------------------------------------- */

static void test_passwords(void)
{
    tw_case("passwords are inactive unless the series wants them");
    setseries(5, 0, 0, FALSE);
    CHECK_INT(passwdsactive(&gs), FALSE);

    tw_case("passwords are inactive while the menu option ignores them");
    /* jc-35: every gate calls passwdsactive() rather than reading usepasswds,
     * so toggling the option takes effect immediately. */
    setseries(5, 0, 0, TRUE);
    CHECK_INT(passwdsactive(&gs), TRUE);
    ignorepasswds = TRUE;
    CHECK_INT(passwdsactive(&gs), FALSE);
    ignorepasswds = FALSE;

    tw_case("🔴 with passwords on, an unreachable level is skipped");
    /* Level 1 is always reachable. Beyond it a level needs its own password
     * known, or the previous level solved. With nothing solved and no
     * passwords known, forward navigation cannot leave level 1. */
    setseries(5, 0, 0, TRUE);
    gs.currentgame = 0;
    CHECK_MSG(changecurrentgame(&gs, +1) == FALSE,
	      "the password gate let an unreachable level through");
    CHECK_INT(gs.currentgame, 0);

    tw_case("solving a level opens the next one");
    setseries(5, 1u << 0, 0, TRUE);	  /* level 1 solved */
    gs.currentgame = 0;
    CHECK_INT(changecurrentgame(&gs, +1), TRUE);
    CHECK_INT(gs.currentgame, 1);

    tw_case("a known password opens a level whose predecessor is unsolved");
    setseries(5, 0, 1u << 2, TRUE);	  /* level 3 has a known password */
    gs.currentgame = 0;
    CHECK_INT(changecurrentgame(&gs, +2), TRUE);
    CHECK_INT(gs.currentgame, 2);

    tw_case("with passwords off, navigation ignores all of that");
    setseries(5, 0, 0, FALSE);
    gs.currentgame = 0;
    CHECK_INT(changecurrentgame(&gs, +3), TRUE);
    CHECK_INT(gs.currentgame, 3);
}

/* --- islastinseries: CLAUDE.md section 7 item 7 -------------------------- */

static void test_islastinseries(void)
{
    tw_case("the final index of the array is the last level");
    setseries(5, 0, 0, FALSE);
    CHECK_INT(islastinseries(&gs, 4), TRUE);
    CHECK_INT(islastinseries(&gs, 3), FALSE);

    tw_case("🔴 a .dac lastlevel= ALSO makes a middle level 'last'");
    /* The trap CLAUDE.md records: series.final comes from the .dac, and stock
     * CCLP1-MS.dac sets it to 144 over a 149-level .dat. So islastinseries()
     * answers TRUE for level 144 -- correct for "stop the game here" and WRONG
     * for "is this the end of the array". Anything asking the second question
     * must use count - 1, which is why the two are not interchangeable. */
    setseries(5, 0, 0, FALSE);
    gs.series.final = 3;		  /* level NUMBER 3, i.e. index 2 */
    CHECK_MSG(islastinseries(&gs, 2) == TRUE,
	      "a level matching series.final was not treated as last");
    CHECK_MSG(islastinseries(&gs, 4) == TRUE,
	      "the true end of the array stopped being last");
    CHECK_MSG(islastinseries(&gs, 1) == FALSE,
	      "a level before series.final was treated as last");

    tw_case("with no lastlevel=, only the array end is last");
    /* series.final of 0 must not match anything: levels are numbered from 1,
     * which is what keeps the default harmless. */
    setseries(5, 0, 0, FALSE);
    gs.series.final = 0;
    CHECK_INT(islastinseries(&gs, 0), FALSE);
    CHECK_INT(islastinseries(&gs, 4), TRUE);
}

/* --- issolved ------------------------------------------------------------ */

static void test_issolved(void)
{
    tw_case("a level counts as solved when it has a best time");
    setseries(5, (1u << 1) | (1u << 3), 0, FALSE);
    CHECK_INT(issolved(&gs, 0), FALSE);
    CHECK_INT(issolved(&gs, 1), TRUE);
    CHECK_INT(issolved(&gs, 2), FALSE);
    CHECK_INT(issolved(&gs, 3), TRUE);
}

int main(void)
{
    tw_begin("tworld_test.c");

    test_clamping();
    test_wrapping();
    test_passwords();
    test_islastinseries();
    test_issolved();

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(51);
    return tw_end();
}
