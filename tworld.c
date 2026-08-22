/* tworld.c: The top-level module.
 *
 * Copyright (C) 2001-2017 by Brian Raiter, Madhav Shanbhag, and Eric Schmidt,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<limits.h>	/* MOD (Jeremy, jc-35): INT_MAX, for the level-number prompt */
#include	"defs.h"
#include	"err.h"
#include	"series.h"
#include	"res.h"
#include	"play.h"
#include	"score.h"
#include	"settings.h"
#include	"solution.h"
#include	"unslist.h"
#include	"help.h"
#include	"oshw.h"
#include	"cmdline.h"
#include	"ver.h"

/* Bell-ringing macro.
 */
#define	bell()	(silence ? (void)0 : ding())

enum { Play_None, Play_Normal, Play_Back, Play_Verify };

/* The data needed to identify what level is being played.
 */
typedef struct gamespec {
    gameseries series; /* the complete set of levels */
    int currentgame; /* which level is currently selected */
    int playmode; /* which mode to play */
    int usepasswds; /* FALSE if passwords are to be ignored */
    int status; /* final status of last game played */
    int enddisplay; /* TRUE if the final level was completed */
    int melindacount; /* count for Melinda's free pass */
} gamespec;

/* Structure used to hold data collected by initoptionswithcmdline().
 */
typedef struct startupdata {
    char* filename; /* which data file to use */
    char* savefilename; /* an alternate solution file */
    int levelnum; /* a selected initial level */
    int listdirs; /* TRUE if directories should be listed */
    int listseries; /* TRUE if the files should be listed */
    int listscores; /* TRUE if the scores should be listed */
    int listtimes; /* TRUE if the times should be listed */
    int batchverify; /* TRUE to enter batch verification */
} startupdata;

/* History of levelsets in order of last used date/time.
 */
static history* historylist = NULL;
static int historycount = 0;

/* Structure used to hold the complete list of available series.
 */
typedef struct seriesdata {
    gameseries* list; /* the array of available series */
    int count; /* size of array */
    mapfileinfo* mflist; /* List of all levelset files */
    int mfcount; /* Number of levelset files */
    tablespec table; /* table for displaying the array */
} seriesdata;

/* TRUE suppresses sound and the console bell.
 */
static int silence = FALSE;

/* TRUE means the program should attempt to run in fullscreen mode.
 */
static int fullscreen = FALSE;

/* FALSE suppresses all password checking.
 */
static int usepasswds = TRUE;

/* MOD (Jeremy, jc-35): TRUE while the "Ignore Passwords" option is on.
 *
 * This is the runtime, user-togglable twin of the -p command-line flag. It is deliberately a
 * SEPARATE variable rather than a way of writing to usepasswds, for two reasons: -p is a startup
 * decision that the user cannot see or undo from inside the program, and gs->usepasswds is a
 * SNAPSHOT taken when a series is loaded (see initgamestate), so writing to either one would not
 * take effect until the next level set was opened. Every gate now asks passwdsactive() instead,
 * which consults this at the moment of the check -- so ticking the menu item frees the whole set
 * immediately, and un-ticking it locks it again, with no restart and no set reload.
 *
 * Not static: the Qt layer owns the menu item and writes this directly. It is declared there with
 * a bare "extern int", which is exactly how this codebase already crosses that boundary --
 * see pedanticmode (defined in lxlogic.c, redeclared in generic/tile.c and TWMainWnd.cpp). */
int ignorepasswds = FALSE;

/* TRUE if the user requested an idle-time histogram.
 */
static int showhistogram = FALSE;

/* Slowdown factor, used for debugging.
 */
static int mudsucking = 1;

/* Frame-skipping disable flag.
 */
static int noframeskip = FALSE;

/* The sound buffer scaling factor.
 */
static int soundbufsize = -1;

/* The initial volume level.
 */
static int volumelevel = -1;

/* The top of the stack of subtitles.
 */
static void** subtitlestack = NULL;

/* If the keyboard inputs should be more lax on repeats for casual players, disabling makes it easier to boost.
 * Linked to the extern declared version in oshw.h
 */
int casualinputs = TRUE;

/*
 * Text-mode output functions.
 */

/* Find a position to break a string inbetween words. The integer at
 * breakpos receives the length of the string prefix less than or
 * equal to len. The string pointer *str is advanced to the first
 * non-whitespace after the break. The original string pointer is
 * returned.
 */
static char* findstrbreak(char const** str, int maxlen, int* breakpos) {
    char const* start;
    int n;

retry:
    start = *str;
    n = strlen(start);
    if (n <= maxlen) {
        *str += n;
        *breakpos = n;
    } else {
        n = maxlen;
        if (isspace(start[n])) {
            *str += n;
            while (isspace(**str))
                ++*str;
            while (n > 0 && isspace(start[n - 1]))
                --n;
            if (n == 0)
                goto retry;
            *breakpos = n;
        } else {
            while (n > 0 && !isspace(start[n - 1]))
                --n;
            if (n == 0) {
                *str += maxlen;
                *breakpos = maxlen;
            } else {
                *str = start + n;
                while (n > 0 && isspace(start[n - 1]))
                    --n;
                if (n == 0)
                    goto retry;
                *breakpos = n;
            }
        }
    }
    return (char*) start;
}

/* Render a table to the given file. This function encapsulates both
 * the process of determining the necessary widths for each column of
 * the table, and then sequentially rendering the table's contents to
 * a stream. On the first pass through the data, single-cell
 * non-word-wrapped entries are measured and each column sized to fit.
 * If the resulting table is too large for the given area, then the
 * collapsible column is reduced as necessary. If there is still
 * space, however, then the entries that span multiple cells are
 * measured in a second pass, and columns are grown to fit them as
 * well where needed. If there is still space after this, the column
 * containing word-wrapped entries may be expanded as well.
 */
void printtable(FILE* out, tablespec const* table) {
    int const maxwidth = 79;
    char const* mlstr;
    char const* p;
    int* colsizes;
    int mlindex, mlwidth, mlpos;
    int diff, pos;
    int i, j, n, i0, c, w, z;

    if (!(colsizes = malloc(table->cols * sizeof *colsizes)))
        return;
    for (i = 0; i < table->cols; ++i)
        colsizes[i] = 0;
    mlindex = -1;
    mlwidth = 0;
    n = 0;
    for (j = 0; j < table->rows; ++j) {
        for (i = 0; i < table->cols; ++n) {
            c = table->items[n][0] - '0';
            if (c == 1) {
                w = strlen(table->items[n] + 2);
                if (table->items[n][1] == '!') {
                    if (w > mlwidth || mlindex != i)
                        mlwidth = w;
                    mlindex = i;
                } else {
                    if (w > colsizes[i])
                        colsizes[i] = w;
                }
            }
            i += c;
        }
    }

    w = -table->sep;
    for (i = 0; i < table->cols; ++i)
        w += colsizes[i] + table->sep;
    diff = maxwidth - w;

    if (diff < 0 && table->collapse >= 0) {
        w = -diff;
        if (colsizes[table->collapse] < w)
            w = colsizes[table->collapse] - 1;
        colsizes[table->collapse] -= w;
        diff += w;
    }

    if (diff > 0) {
        n = 0;
        for (j = 0; j < table->rows && diff > 0; ++j) {
            for (i = 0; i < table->cols; ++n) {
                c = table->items[n][0] - '0';
                if (c > 1 && table->items[n][1] != '!') {
                    w = table->sep + strlen(table->items[n] + 2);
                    for (i0 = i; i0 < i + c; ++i0)
                        w -= colsizes[i0] + table->sep;
                    if (w > 0) {
                        if (table->collapse >= i && table->collapse < i + c)
                            i0 = table->collapse;
                        else if (mlindex >= i && mlindex < i + c)
                            i0 = mlindex;
                        else
                            i0 = i + c - 1;
                        if (w > diff)
                            w = diff;
                        colsizes[i0] += w;
                        diff -= w;
                        if (diff == 0)
                            break;
                    }
                }
                i += c;
            }
        }
    }
    if (diff > 0 && mlindex >= 0 && colsizes[mlindex] < mlwidth) {
        mlwidth -= colsizes[mlindex];
        w = mlwidth < diff ? mlwidth : diff;
        colsizes[mlindex] += w;
        diff -= w;
    }

    n = 0;
    for (j = 0; j < table->rows; ++j) {
        mlstr = NULL;
        mlwidth = mlpos = 0;
        pos = 0;
        for (i = 0; i < table->cols; ++n) {
            if (i)
                pos += fprintf(out, "%*s", table->sep, "");
            c = table->items[n][0] - '0';
            w = -table->sep;
            while (c--)
                w += colsizes[i++] + table->sep;
            if (table->items[n][1] == '-')
                fprintf(out, "%-*.*s", w, w, table->items[n] + 2);
            else if (table->items[n][1] == '+')
                fprintf(out, "%*.*s", w, w, table->items[n] + 2);
            else if (table->items[n][1] == '.') {
                z = (w - strlen(table->items[n] + 2)) / 2;
                if (z < 0)
                    z = w;
                fprintf(out, "%*.*s%*s",
                        w - z, w - z, table->items[n] + 2, z, "");
            } else if (table->items[n][1] == '!') {
                mlwidth = w;
                mlpos = pos;
                mlstr = table->items[n] + 2;
                p = findstrbreak(&mlstr, w, &z);
                fprintf(out, "%.*s%*s", z, p, w - z, "");
            }
            pos += w;
        }
        fputc('\n', out);
        while (mlstr && *mlstr) {
            p = findstrbreak(&mlstr, mlwidth, &w);
            fprintf(out, "%*s%.*s\n", mlpos, "", w, p);
        }
    }
    free(colsizes);
}

/* Display directory settings.
 */
static void printdirectories(void) {
    printf("Resource files read from:        %s\n", resdir);
    printf("Level sets read from:            %s\n", seriesdir);
    printf("Configured data files read from: %s\n", seriesdatdir);
    printf("Solution files saved in:         %s\n", savedir);
}

/*
 * Callback functions for oshw.
 */

/* An input callback that only accepts the characters Y and N.
 */
static int yninputcallback(void) {
    switch (input(TRUE)) {
        case 'Y':
        case 'y': return 'Y';
        case 'N':
        case 'n': return 'N';
        case CmdWest: return '\b';
        case CmdProceed: return '\n';
        case CmdQuitLevel: return -1;
        case CmdQuit: exit(0);
    }
    return 0;
}

/* An input callback that accepts only alphabetic characters.
 */
static int keyinputcallback(void) {
    int ch;

    ch = input(TRUE);
    switch (ch) {
        case CmdWest: return '\b';
        case CmdProceed: return '\n';
        case CmdQuitLevel: return -1;
        case CmdQuit: exit(0);
        default:
            if (isalpha(ch))
                return toupper(ch);
    }
    return 0;
}

/* An input callback used while displaying a scrolling list.
 */
static int scrollinputcallback(int* move) {
    int cmd;

    switch ((cmd = input(TRUE))) {
        case CmdPrev10: *move = SCROLL_HALFPAGE_UP;
            break;
        case CmdNorth: *move = SCROLL_UP;
            break;
        case CmdPrev: *move = SCROLL_UP;
            break;
        case CmdPrevLevel: *move = SCROLL_UP;
            break;
        case CmdSouth: *move = SCROLL_DN;
            break;
        case CmdNext: *move = SCROLL_DN;
            break;
        case CmdNextLevel: *move = SCROLL_DN;
            break;
        case CmdNext10: *move = SCROLL_HALFPAGE_DN;
            break;
        case CmdProceed: *move = CmdProceed;
            return FALSE;
        case CmdQuitLevel: *move = CmdQuitLevel;
            return FALSE;
        case CmdHelp: *move = CmdHelp;
            return FALSE;
        case CmdQuit: exit(0);
    }
    return TRUE;
}

/* An input callback used while displaying the scrolling list of scores.
 */
static int scorescrollinputcallback(int* move) {
    int cmd;
    switch ((cmd = input(TRUE))) {
        case CmdPrev10: *move = SCROLL_HALFPAGE_UP;
            break;
        case CmdNorth: *move = SCROLL_UP;
            break;
        case CmdPrev: *move = SCROLL_UP;
            break;
        case CmdPrevLevel: *move = SCROLL_UP;
            break;
        case CmdSouth: *move = SCROLL_DN;
            break;
        case CmdNext: *move = SCROLL_DN;
            break;
        case CmdNextLevel: *move = SCROLL_DN;
            break;
        case CmdNext10: *move = SCROLL_HALFPAGE_DN;
            break;
        case CmdProceed: *move = CmdProceed;
            return FALSE;
        case CmdSeeSolutionFiles: *move = CmdSeeSolutionFiles;
            return FALSE;
        case CmdQuitLevel: *move = CmdQuitLevel;
            return FALSE;
        case CmdHelp: *move = CmdHelp;
            return FALSE;
        case CmdQuit: exit(0);
    }
    return TRUE;
}

/* An input callback used while displaying the scrolling list of solutions.
 */
static int solutionscrollinputcallback(int* move) {
    int cmd;
    switch ((cmd = input(TRUE))) {
        case CmdPrev10: *move = SCROLL_HALFPAGE_UP;
            break;
        case CmdNorth: *move = SCROLL_UP;
            break;
        case CmdPrev: *move = SCROLL_UP;
            break;
        case CmdPrevLevel: *move = SCROLL_UP;
            break;
        case CmdSouth: *move = SCROLL_DN;
            break;
        case CmdNext: *move = SCROLL_DN;
            break;
        case CmdNextLevel: *move = SCROLL_DN;
            break;
        case CmdNext10: *move = SCROLL_HALFPAGE_DN;
            break;
        case CmdProceed: *move = CmdProceed;
            return FALSE;
        case CmdSeeScores: *move = CmdSeeScores;
            return FALSE;
        case CmdQuitLevel: *move = CmdQuitLevel;
            return FALSE;
        case CmdHelp: *move = CmdHelp;
            return FALSE;
        case CmdQuit: exit(0);
    }
    return TRUE;
}

/*
 * Basic game activities.
 */

/* Return TRUE if the given level is a final level.
 */
static int islastinseries(gamespec const* gs, int index) {
    return index == gs->series.count - 1
           || gs->series.games[index].number == gs->series.final;
}

/* Return TRUE if the current level has a solution.
 */
static int issolved(gamespec const* gs, int index) {
    return hassolution(gs->series.games + index);
}

/* Mark the current level's solution as replaceable.
 */
static void replaceablesolution(gamespec* gs, int change) {
    if (change < 0)
        gs->series.games[gs->currentgame].sgflags ^= SGF_REPLACEABLE;
    else if (change > 0)
        gs->series.games[gs->currentgame].sgflags |= SGF_REPLACEABLE;
    else
        gs->series.games[gs->currentgame].sgflags &= ~SGF_REPLACEABLE;
}

/* MOD (Jeremy, jc-35): are passwords being enforced right now?
 *
 * gs->usepasswds is the per-series answer fixed at load time (the -p flag and the .dac file's
 * "ignore-passwords" line); ignorepasswds is the live menu option. Passwords apply only when
 * neither says otherwise. Every gate calls this rather than reading gs->usepasswds, so the option
 * takes effect the instant it is toggled.
 */
static int passwdsactive(gamespec const* gs) {
    return gs->usepasswds && !ignorepasswds;
}

/* Mark the current level's password as known to the user.
 */
static void passwordseen(gamespec* gs, int number) {
    /* MOD (Jeremy, jc-35): record nothing while passwords are being ignored.
     *
     * This flag is PERSISTENT -- setting it writes SGF_HASPASSWD into the solution file (note the
     * savesolutions() below), and there is no way to unset it from inside the program. Without
     * this guard, switching the option on and browsing a set would permanently record that the
     * player knows the password to every level visited, and switching the option back off would
     * NOT undo it: the set would stay unlocked forever, and the option would have quietly rewritten
     * his save files as a side effect of being turned on. An option whose damage outlives it is a
     * trap, so while it is on the save file is left exactly as it was.
     *
     * Note this differs from upstream's -p, which does record. That is defensible for a flag you
     * must opt into on every launch; it is not defensible for a saved setting. */
    if (ignorepasswds)
        return;

    if (!(gs->series.games[number].sgflags & SGF_HASPASSWD)) {
        gs->series.games[number].sgflags |= SGF_HASPASSWD;
        savesolutions(&gs->series);
    }
}

/* Change the current level, ensuring that the user is not granted
 * access to a forbidden level. FALSE is returned if the specified
 * level is not available to the user.
 */
static int setcurrentgame(gamespec* gs, int n) {
    if (n == gs->currentgame)
        return TRUE;
    if (n < 0 || n >= gs->series.count)
        return FALSE;

    if (passwdsactive(gs))
        if (n > 0 && !(gs->series.games[n].sgflags & SGF_HASPASSWD)
            && !issolved(gs, n - 1))
            return FALSE;

    gs->currentgame = n;
    gs->melindacount = 0;
    return TRUE;
}

/* Change the current level by a delta value. If the user cannot go to
 * that level, the "nearest" level in that direction is chosen
 * instead. FALSE is returned if the current level remained unchanged.
 */
static int changecurrentgame(gamespec* gs, int offset) {
    int sign, m, n;

    if (offset == 0)
        return FALSE;

    m = gs->currentgame;
    n = m + offset;
    if (n < 0)
        n = 0;
    else if (n >= gs->series.count)
        n = gs->series.count - 1;

    if (passwdsactive(gs) && n > 0) {
        sign = offset < 0 ? -1 : +1;
        for (; n >= 0 && n < gs->series.count; n += sign) {
            if (!n || (gs->series.games[n].sgflags & SGF_HASPASSWD)
                || issolved(gs, n - 1)) {
                m = n;
                break;
            }
        }
        n = m;
        if (n == gs->currentgame && offset != sign) {
            n = gs->currentgame + offset - sign;
            for (; n != gs->currentgame; n -= sign) {
                if (n < 0 || n >= gs->series.count)
                    continue;
                if (!n || (gs->series.games[n].sgflags & SGF_HASPASSWD)
                    || issolved(gs, n - 1))
                    break;
            }
        }
    }

    if (n == gs->currentgame)
        return FALSE;

    gs->currentgame = n;
    gs->melindacount = 0;
    return TRUE;
}

/* MOD (Jeremy, jc-39): the same thing, with the two ends of the set joined.
 *
 * changecurrentgame() CLAMPS: asked to leave the set at either end it stays put and returns FALSE,
 * and the key did nothing at all -- with a bell on the pre-level screen, which is the one caller
 * that looks at the return value (startinput()'s leveldelta macro), and in silence everywhere else.
 * Previous on level 1 now lands on the last level of the set, Next on the last level lands on level
 * 1, and the ten-level skips do the same once they are parked against an end they cannot move away
 * from.
 *
 * WRAPPING IS DELIBERATELY NOT BUILT INTO changecurrentgame() ITSELF, because three of its callers
 * are not the player navigating and would be actively broken by it:
 *
 *   - findlevelfromhistory() and the -defaultlevel path both call it with -1 purely to BACK DOWN
 *     from a level whose password is not known yet. Wrap that and a locked start level would send
 *     the player to the far end of the set instead of to a legal one.
 *   - endinput() calls it with +1 as Melinda's free pass and again as "Onward!" after a win. A win
 *     on the last level must fall through to the end-of-series screen, not silently restart at
 *     level 1 -- islastinseries() guards that path and knows nothing about this.
 *   - showsolutionfiles() calls it with an arbitrary offset to RESTORE the current level after
 *     switching solution files. Wrapping is meaningless there.
 *
 * Only the keys and menu items that mean "take me to another level" call this.
 *
 * The wrap itself is expressed as an ordinary offset back through changecurrentgame(), so password
 * protection keeps working exactly as it does anywhere else: with passwords enforced, going back off
 * level 1 lands on the furthest level the player has legitimately unlocked (which on a fresh save is
 * level 1 again -- unchanged, so nothing moves). It cannot open a level the same offset typed by
 * hand would not have opened.
 *
 * "THE LAST LEVEL" HERE MEANS THE LAST LEVEL IN THE FILE, count - 1, and deliberately NOT
 * islastinseries(), which also answers TRUE for the level whose number matches a .dac file's
 * lastlevel= line. Those two differ on the stock upstream configurations (CCLP1-MS.dac says
 * lastlevel=144 over a 149-level .dat), and count - 1 is the right one of the pair: lastlevel only
 * decides where the end-of-series screen fires, while Previous and Next have always walked straight
 * through the levels past it as ordinary levels. Wrapping to 144 instead would land the player in
 * the MIDDLE of the navigable range -- Previous from level 1 would reach 144, Next from 144 would
 * go to 145, and 145-149 could never be reached by wrapping at all. count - 1 keeps the two
 * directions exact inverses of each other: whatever Next on the last level leaves you on, Previous
 * on level 1 takes you back to.
 */
static int changecurrentgamewrapped(gamespec* gs, int offset) {
    int end;

    if (changecurrentgame(gs, offset))
        return TRUE;

    /* Nothing moved. Wrap only when that is because we are already against the end we were asked
     * to move past -- not when a password gate refused a level in the middle of the set. */
    if (offset < 0 && gs->currentgame == 0)
        end = gs->series.count - 1;
    else if (offset > 0 && gs->currentgame == gs->series.count - 1)
        end = 0;
    else
        return FALSE;

    return changecurrentgame(gs, end - gs->currentgame);
}

/* Return TRUE if Melinda is watching Chip's progress on this level --
 * i.e., if it is possible to earn a pass to the next level.
 */
static int melindawatching(gamespec const* gs) {
    /* MOD (Jeremy, jc-35): passwdsactive() rather than gs->usepasswds, so Melinda's free pass also
     * goes away while passwords are ignored. Offering to skip a level you could already jump past
     * from the menu would be pointless, and upstream's -p suppresses it for the same reason. */
    if (!passwdsactive(gs))
        return FALSE;
    if (islastinseries(gs, gs->currentgame))
        return FALSE;
    if (gs->series.games[gs->currentgame + 1].sgflags & SGF_HASPASSWD)
        return FALSE;
    if (issolved(gs, gs->currentgame))
        return FALSE;
    return TRUE;
}

/*
 * The subtitle stack
 */

static void pushsubtitle(char const* subtitle) {
    void** stk;
    int n;

    if (!subtitle)
        subtitle = "";
    n = strlen(subtitle) + 1;
    stk = NULL;
    x_alloc(stk, sizeof(void**) + n);
    *stk = subtitlestack;
    subtitlestack = stk;
    memcpy(stk + 1, subtitle, n);
    setsubtitle(subtitle);
}

static void popsubtitle(void) {
    void** stk;

    if (subtitlestack) {
        stk = *subtitlestack;
        free(subtitlestack);
        subtitlestack = stk;
    }
    setsubtitle(subtitlestack ? (char*) (subtitlestack + 1) : NULL);
}

static void changesubtitle(char const* subtitle) {
    int n;

    if (!subtitle)
        subtitle = "";
    n = strlen(subtitle) + 1;
    x_alloc(subtitlestack, sizeof(void**) + n);
    memcpy(subtitlestack + 1, subtitle, n);
    setsubtitle(subtitle);
}

/*
 *
 */

static void dohelp(int topic) {
    pushsubtitle("Help");
    switch (topic) {
        case Help_First:
        case Help_FileListKeys:
        case Help_ScoreListKeys:
            onlinecontexthelp(topic);
            break;
        default:
            onlinemainhelp(topic);
            break;
    }
    popsubtitle();
}

/* Display a scrolling list of the available solution files, and allow
 * the user to select one. Return TRUE if the user selected a solution
 * file different from the current one. Do nothing if there is only
 * one solution file available. (If for some reason the new solution
 * file cannot be read, TRUE will still be returned, as the list of
 * solved levels will still need to be updated.)
 */
static int showsolutionfiles(gamespec* gs) {
    tablespec table;
    char const** filelist;
    int readonly = FALSE;
    int count, current, f, n;

    if (haspathname(gs->series.name) || (gs->series.savefilename
                                         && haspathname(gs->series.savefilename))) {
        bell();
        return FALSE;
    } else if (!createsolutionfilelist(&gs->series, FALSE, &filelist,
                                       &count, &table)) {
        bell();
        return FALSE;
    }

    current = -1;
    n = 0;
    if (gs->series.savefilename) {
        for (n = 0; n < count; ++n)
            if (!strcmp(filelist[n], gs->series.savefilename))
                break;
        if (n == count)
            n = 0;
        else
            current = n;
    }

    pushsubtitle(gs->series.name);
    for (;;) {
        f = displaylist("SOLUTION FILES", &table, &n,
                        LIST_SOLUTIONFILES, solutionscrollinputcallback);
        if (f == CmdProceed) {
            readonly = FALSE;
            break;
        } else if (f == CmdSeeScores) {
            readonly = TRUE;
            break;
        } else if (f == CmdQuitLevel) {
            n = -1;
            break;
        } else if (f == CmdHelp) {
            dohelp(Help_FileListKeys);
        }
    }
    popsubtitle();

    f = n >= 0 && n != current;
    if (f) {
        clearsolutions(&gs->series);
        if (!gs->series.savefilename)
            gs->series.savefilename = getpathbuffer();
        sprintf(gs->series.savefilename, "%.*s", getpathbufferlen(),
                filelist[n]);
        if (readsolutions(&gs->series)) {
            if (readonly)
                gs->series.gsflags |= GSF_NOSAVING;
        } else {
            bell();
        }
        n = gs->currentgame;
        gs->currentgame = 0;
        passwordseen(gs, 0);
        changecurrentgame(gs, n);
    }

    freesolutionfilelist(filelist, &table);
    return f;
}

/* Display the scrolling list of the user's current scores, and allow
 * the user to select a current level.
 */
static int showscores(gamespec* gs) {
    tablespec table;
    int* levellist;
    int ret = FALSE;
    int count, f, n;

restart:
    /* MOD (Jeremy, jc-35): with passwords ignored the score list shows every level's name, not
     * just the ones reached -- the same list the player can now actually visit. */
    if (!createscorelist(&gs->series, passwdsactive(gs), CHAR_MZERO,
                         &levellist, &count, &table)) {
        bell();
        return ret;
    }
    for (n = 0; n < count; ++n)
        if (levellist[n] == gs->currentgame)
            break;
    pushsubtitle(gs->series.name);
    for (;;) {
        f = displaylist(gs->series.filebase, &table, &n,
                        LIST_SCORES, scorescrollinputcallback);
        if (f == CmdProceed) {
            n = levellist[n];
            break;
        } else if (f == CmdSeeSolutionFiles) {
            if (!(gs->series.gsflags & GSF_NODEFAULTSAVE)) {
                n = levellist[n];
                break;
            }
        } else if (f == CmdQuitLevel) {
            n = -1;
            break;
        } else if (f == CmdHelp) {
            dohelp(Help_ScoreListKeys);
        }
    }
    popsubtitle();
    freescorelist(levellist, &table);
    if (f == CmdSeeSolutionFiles) {
        setcurrentgame(gs, n);
        ret = showsolutionfiles(gs);
        goto restart;
    }
    if (n < 0)
        return ret;
    return setcurrentgame(gs, n) || ret;
}

/* Obtain a password from the user and move to the requested level.
 */
static int selectlevelbypassword(gamespec* gs) {
    char passwd[5] = "";
    int n;

    setkeyboardinputmode(TRUE);
    n = displayinputprompt("Enter Password", passwd, 4,
                           INPUT_ALPHA, keyinputcallback);
    setkeyboardinputmode(FALSE);
    if (!n)
        return FALSE;

    n = findlevelinseries(&gs->series, 0, passwd);
    if (n < 0) {
        bell();
        return FALSE;
    }
    passwordseen(gs, n);
    return setcurrentgame(gs, n);
}

/* MOD (Jeremy, jc-35): the Ignore Passwords version of Ctrl+G -- ask for a level NUMBER.
 *
 * With passwords ignored, "Enter Password" is a question with no useful answer: there is nothing
 * to unlock, and the player has no reason to know or type the four letters. The number is what he
 * actually has in mind ("take me to 47").
 *
 * INPUT_ALPHA is reused rather than adding an INPUT_NUMBER prompt type. That enum is part of the
 * oshw interface and is switched on by every backend, so a new member would mean touching the SDL
 * layer this fork does not build and cannot test. The only thing INPUT_ALPHA does to the text is
 * upper-case it, which does nothing to digits, and validation belongs here anyway -- the prompt
 * cannot know what a valid level number is for this set.
 *
 * Lookup is by the level's OWN number via findlevelinseries(), not by position, because those are
 * not always the same thing. It falls back to treating the input as a 1-based position when the
 * number lookup fails, which covers the two real cases: a set whose numbering has gaps, and one
 * with duplicate numbers (findlevelinseries deliberately reports -1 rather than guess between
 * them). If both fail the input was simply out of range, and the bell says so.
 */
static int selectlevelbynumber(gamespec* gs) {
    char buf[8] = "";
    char *end;
    long num;
    int n;

    setkeyboardinputmode(TRUE);
    n = displayinputprompt("Enter Level Number", buf, 4,
                           INPUT_ALPHA, keyinputcallback);
    setkeyboardinputmode(FALSE);
    if (!n)
        return FALSE;

    /* strtol, not atoi: atoi cannot tell "0" from "banana", and both need rejecting. */
    num = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || num <= 0 || num > INT_MAX) {
        bell();
        return FALSE;
    }

    n = findlevelinseries(&gs->series, (int)num, NULL);
    if (n < 0 && num <= gs->series.count)
        n = (int)num - 1;                       /* fall back to position in the set */
    if (n < 0 || n >= gs->series.count) {
        bell();
        return FALSE;
    }

    /* No passwordseen() here on purpose -- see the note in that function. Nothing was unlocked,
     * so nothing is recorded. */
    return setcurrentgame(gs, n);
}

/* MOD (Jeremy, jc-35): what Ctrl+G does, decided when it is pressed.
 */
static int gotolevel(gamespec* gs) {
    return ignorepasswds ? selectlevelbynumber(gs) : selectlevelbypassword(gs);
}

/*
 * The levelset history functions.
 */

/* Load the levelset history.
 */
static int loadhistory(void) {
    fileinfo file;
    char buf[256];
    int n;
    char* hdate,* htime,* hpasswd,* hnumber,* hname;
    int hyear, hmon, hmday, hhour, hmin, hsec;
    history* h;

    historycount = 0;
    free(historylist);

    clearfileinfo(&file);
    if (!openfileindir(&file, savedir, "history", "r", NULL))
        return FALSE;

    for (;;) {
        n = sizeof buf - 1;
        if (!filegetline(&file, buf, &n, NULL))
            break;

        if (buf[0] == '#')
            continue;

        hdate = strtok(buf, " \t");
        htime = strtok(NULL, " \t");
        hpasswd = strtok(NULL, " \t");
        hnumber = strtok(NULL, " \t");
        hname = strtok(NULL, "\r\n");

        if (!(hdate && htime && hpasswd && hnumber && hname &&
              sscanf(hdate, "%d-%d-%d", &hyear, &hmon, &hmday) == 3 &&
              sscanf(htime, "%d:%d:%d", &hhour, &hmin, &hsec) == 3 &&
              *hpasswd && *hnumber && *hname))
            continue;

        ++historycount;
        x_alloc(historylist, historycount * sizeof *historylist);
        h = historylist + historycount - 1;

        sprintf(h->name, "%.*s", (int) (sizeof h->name - 1), hname);
        sprintf(h->passwd, "%.*s", (int) (sizeof h->passwd - 1), hpasswd);
        h->levelnumber = (int) strtol(hnumber, NULL, 0);
        h->dt.tm_year = hyear - 1900;
        h->dt.tm_mon = hmon - 1;
        h->dt.tm_mday = hmday;
        h->dt.tm_hour = hhour;
        h->dt.tm_min = hmin;
        h->dt.tm_sec = hsec;
        h->dt.tm_isdst = -1;
    }

    fileclose(&file, NULL);

    return TRUE;
}

/* Update the levelset history for the set and level being played.
 */
static void updatehistory(char const* name, char const* passwd, int number) {
    time_t t = time(NULL);
    int i, j;
    history* h;

    h = historylist;
    for (i = 0; i < historycount; ++i, ++h) {
        if (stricmp(h->name, name) == 0)
            break;
    }

    if (i == historycount) {
        ++historycount;
        x_alloc(historylist, historycount * sizeof *historylist);
    }

    for (j = i; j > 0; --j) {
        historylist[j] = historylist[j - 1];
    }

    h = historylist;
    sprintf(h->name, "%.*s", (int) (sizeof h->name - 1), name);
    sprintf(h->passwd, "%.*s", (int) (sizeof h->passwd - 1), passwd);
    h->levelnumber = number;
    h->dt = *localtime(&t);
}

/* Save the levelset history.
 */
static void savehistory(void) {
    fileinfo file;
    history* h;
    int i;

    clearfileinfo(&file);
    if (!openfileindir(&file, savedir, "history", "w", NULL))
        return;

    h = historylist;
    for (i = 0; i < historycount; ++i, ++h) {
        fprintf(file.fp, "%04d-%02d-%02d %02d:%02d:%02d\t%s\t%d\t%s\n",
                1900 + h->dt.tm_year, 1 + h->dt.tm_mon, h->dt.tm_mday,
                h->dt.tm_hour, h->dt.tm_min, h->dt.tm_sec,
                h->passwd, h->levelnumber, h->name);
    }

    fileclose(&file, NULL);
}

/*
 * The game-playing functions.
 */

/* MOD (Jeremy, jc-39): changecurrentgamewrapped(), so the ends of the set join up. The bell now
 * rings only when there is genuinely nowhere to go -- a one-level set, or a password gate. */
#define	leveldelta(n)	if (!changecurrentgamewrapped(gs, (n))) { bell(); continue; }

/* Get a key command from the user at the start of the current level.
 */
static int startinput(gamespec* gs) {
    static int lastlevel = -1;
    char yn[2];
    int cmd, n;

    if (gs->currentgame != lastlevel) {
        lastlevel = gs->currentgame;
        setstepping(0, FALSE);
    }
    drawscreen(TRUE);
    gs->playmode = Play_None;
    for (;;) {
        cmd = input(TRUE);
        if (cmd >= CmdMoveFirst && cmd <= CmdMoveLast) {
            gs->playmode = Play_Normal;
            return cmd;
        }
        switch (cmd) {
            case CmdProceed: gs->playmode = Play_Normal;
                return cmd;
            case CmdQuitLevel: return cmd;
            case CmdPrev10: leveldelta(-10);
                return CmdNone;
            case CmdPrev: leveldelta(-1);
                return CmdNone;
            case CmdPrevLevel: leveldelta(-1);
                return CmdNone;
            case CmdNextLevel: leveldelta(+1);
                return CmdNone;
            case CmdNext: leveldelta(+1);
                return CmdNone;
            case CmdNext10: leveldelta(+10);
                return CmdNone;
            case CmdStepping: changestepping(4, TRUE);
                break;
            case CmdSubStepping: changestepping(1, TRUE);
                break;
            case CmdRandomFF: advanceinitrandomff(TRUE);
                break;
            case CmdVolumeUp: changevolume(+2, TRUE);
                break;
            case CmdVolumeDown: changevolume(-2, TRUE);
                break;
            case CmdHelp: dohelp(Help_KeysBetweenGames);
                break;
            case CmdQuit: exit(0);
            case CmdPlayback:
            case CmdAdvanceGame:
            case CmdAdvanceMoveGame:
                if (prepareplayback()) {
                    gs->playmode = Play_Back;
                    return cmd;
                }
                bell();
                break;
            case CmdSeek:
                if (getreplaysecondstoskip() > 0) {
                    gs->playmode = Play_Back;
                    return CmdProceed;
                }
                break;
            case CmdCheckSolution:
                if (prepareplayback()) {
                    gs->playmode = Play_Verify;
                    return CmdProceed;
                }
                bell();
                break;
            case CmdReplSolution:
                if (issolved(gs, gs->currentgame))
                    replaceablesolution(gs, -1);
                else
                    bell();
                break;
            case CmdKillSolution:
                if (!issolved(gs, gs->currentgame)) {
                    bell();
                    break;
                }
                yn[0] = '\0';
                setkeyboardinputmode(TRUE);
                n = displayinputprompt("Really delete solution?",
                                       yn, 1, INPUT_YESNO, yninputcallback);
                setkeyboardinputmode(FALSE);
                if (n && *yn == 'Y')
                    if (deletesolution())
                        savesolutions(&gs->series);
                break;
            case CmdSeeScores:
                if (showscores(gs))
                    return CmdNone;
                break;
            case CmdSeeSolutionFiles:
                if (showsolutionfiles(gs))
                    return CmdNone;
                break;
            case CmdTimesClipboard:
                copytoclipboard(leveltimes(&gs->series));
                break;
            case CmdGotoLevel:
                if (gotolevel(gs))          /* MOD (Jeremy, jc-35) */
                    return CmdNone;
                break;
            case CmdKeys:
                n = 0;
                displaylist("", keyboardhelp(KEYHELP_TWPLUSPLUS), &n, LIST_HELP, NULL);
                return CmdNone;
            default:
                continue;
        }
        drawscreen(TRUE);
    }
}

/* Get a key command from the user at the completion of the current
 * level.
 */
static int endinput(gamespec* gs) {
    char yn[2];
    int bscore = 0, tscore = 0;
    long gscore = 0;
    int n;
    int cmd = CmdNone;

    if (gs->status < 0) {
        if (melindawatching(gs) && secondsplayed() >= 10) {
            ++gs->melindacount;
            if (gs->melindacount >= 10) {
                yn[0] = '\0';
                setkeyboardinputmode(TRUE);
                n = displayinputprompt("Skip level?", yn, 1,
                                       INPUT_YESNO, yninputcallback);
                setkeyboardinputmode(FALSE);
                if (n && *yn == 'Y') {
                    passwordseen(gs, gs->currentgame + 1);
                    changecurrentgame(gs, +1);
                }
                gs->melindacount = 0;
                return TRUE;
            }
        }
    } else {
        getscoresforlevel(&gs->series, gs->currentgame,
                          &bscore, &tscore, &gscore);
    }

    cmd = displayendmessage(bscore, tscore, gscore, gs->status);

    for (;;) {
        if (cmd == CmdNone)
            cmd = input(TRUE);
        switch (cmd) {
            /* MOD (Jeremy, jc-39): wrapped, like every other navigation key. The CmdProceed case
             * below is NOT -- winning the last level still ends the series. */
            case CmdPrev10: changecurrentgamewrapped(gs, -10);
                return TRUE;
            case CmdPrevLevel: changecurrentgamewrapped(gs, -1);
                return TRUE;
            case CmdPrev: changecurrentgamewrapped(gs, -1);
                return TRUE;
            case CmdSameLevel: return TRUE;
            case CmdSame: return TRUE;
            case CmdNextLevel: changecurrentgamewrapped(gs, +1);
                return TRUE;
            case CmdNext: changecurrentgamewrapped(gs, +1);
                return TRUE;
            case CmdNext10: changecurrentgamewrapped(gs, +10);
                return TRUE;
            case CmdGotoLevel: gotolevel(gs);   /* MOD (Jeremy, jc-35) */
                return TRUE;
            case CmdPlayback: return TRUE;
            case CmdSeeScores: showscores(gs);
                return TRUE;
            case CmdSeeSolutionFiles: showsolutionfiles(gs);
                return TRUE;
            case CmdKillSolution: return TRUE;
            case CmdHelp: dohelp(Help_KeysBetweenGames);
                return TRUE;
            case CmdQuitLevel: return FALSE;
            case CmdQuit: exit(0);
            case CmdCheckSolution:
            case CmdProceed:
                if (gs->status > 0) {
                    if (islastinseries(gs, gs->currentgame))
                        gs->enddisplay = TRUE;
                    else
                        changecurrentgame(gs, +1);
                }
                return TRUE;
            case CmdReplSolution:
                if (issolved(gs, gs->currentgame))
                    replaceablesolution(gs, -1);
                else
                    bell();
                return TRUE;
        }
        cmd = CmdNone;
    }
}

/* Get a key command from the user at the completion of the current
 * series.
 */
static int finalinput(gamespec* gs) {
    int cmd;

    for (;;) {
        cmd = input(TRUE);
        switch (cmd) {
            case CmdSameLevel:
            case CmdSame:
                return TRUE;
            case CmdPrevLevel:
            case CmdPrev:
            case CmdNextLevel:
            case CmdNext:
                setcurrentgame(gs, 0);
                return TRUE;
            case CmdQuit:
                exit(0);
            default:
                return FALSE;
        }
    }
}

#define SETPAUSED(paused, shutter) do { \
    if (paused) { \
	setdisplaymsg("(paused)", FOREVER, FOREVER); \
	setgameplaymode((shutter) ? SuspendPlayShuttered : SuspendPlay); \
	if (shutter) drawscreen(TRUE); \
	gamepaused = TRUE; \
    } \
    else { \
	setdisplaymsg(NULL, 0, 0); \
	setgameplaymode(NormalPlay); \
	gamepaused = FALSE; \
    } \
} while (0)

/* Play the current level, using firstcmd as the initial key command,
 * and returning when the level's play ends. The return value is FALSE
 * if play ended because the user restarted or changed the current
 * level (indicating that the program should not prompt the user
 * before continuing). If the return value is TRUE, the gamespec
 * structure's status field will contain the return value of the last
 * call to doturn() -- i.e., positive if the level was completed
 * successfully, negative if the level ended unsuccessfully. Likewise,
 * the gamespec structure will be updated if the user ended play by
 * changing the current level.
 */
static int playgame(gamespec* gs, int firstcmd) {
    int render, lastrendered;
    int cmd, n;

    cmd = firstcmd;
    if (cmd == CmdProceed)
        cmd = CmdNone;

    gs->status = 0;
    setgameplaymode(NormalPlay);
    render = lastrendered = TRUE;

    int gamepaused = FALSE;
    for (;;) {
        if (gamepaused)
            cmd = input(TRUE);
        else {
            n = doturn(cmd);
            drawscreen(render);
            lastrendered = render;
            if (n)
                break;
            render = waitfortick() || noframeskip;
            cmd = input(FALSE);
        }
        if (cmd == CmdQuitLevel) {
            quitgamestate();
            n = -2;
            setdisplaymsg(NULL, 0, 0);
            break;
        }
        if (!(cmd >= CmdMoveFirst && cmd <= CmdMoveLast)) {
            switch (cmd) {
                case CmdPreserve: break;
                case CmdPrevLevel: n = -1;
                    goto quitloop;
                case CmdNextLevel: n = +1;
                    goto quitloop;
                case CmdSameLevel: n = 0;
                    /* MOD (Jeremy, jc-37): restarting a level that is in progress counts as a
                     * death -- Ctrl+R, or Level > Restart. Giving up on a run is a run you lost.
                     *
                     * ONLY from here, inside playgame(), and that is what stops every death from
                     * counting twice. After a real death this function has already returned and
                     * endinput() is running, where EVERY way out restarts the level: R and Ctrl+R
                     * return TRUE, and so does Space, because CmdProceed does not advance while
                     * gs->status < 0. Counting restarts there would double every death.
                     *
                     * Consequences of the same rule, all intended: restarting a level you just
                     * SOLVED is free (also endinput()); restarting from the pre-level screen is
                     * free (startinput() has no CmdSameLevel case at all); and restarting a replay
                     * is free (playbackgame() has its own switch). */
                    recorddeath();
                    goto quitloop;
                case CmdQuit: exit(0);
                case CmdVolumeUp:
                    changevolume(+2, TRUE);
                    cmd = CmdNone;
                    break;
                case CmdVolumeDown:
                    changevolume(-2, TRUE);
                    cmd = CmdNone;
                    break;
                case CmdPauseGame:
                    SETPAUSED(!gamepaused, TRUE);
                    if (!gamepaused)
                        cmd = CmdNone;
                    break;
                case CmdHelp:
                    setgameplaymode(SuspendPlay);
                    dohelp(Help_KeysDuringGame);
                    if (!gamepaused) setgameplaymode(NormalPlay);
                    cmd = CmdNone;
                    break;
#ifndef NDEBUG
                case CmdDebugCmd1: break;
                case CmdDebugCmd2: break;
                case CmdCheatNorth:
                case CmdCheatWest: break;
                case CmdCheatSouth:
                case CmdCheatEast: break;
                case CmdCheatHome: break;
                case CmdCheatKeyRed:
                case CmdCheatKeyBlue: break;
                case CmdCheatKeyYellow:
                case CmdCheatKeyGreen: break;
                case CmdCheatBootsIce:
                case CmdCheatBootsSlide: break;
                case CmdCheatBootsFire:
                case CmdCheatBootsWater: break;
                case CmdCheatICChip: break;
#endif
                default:
                    cmd = CmdNone;
                    break;
            }
        }
    }
    /* MOD (Jeremy, jc-37): Chip died. Count it.
     *
     * THIS BLOCK IS REACHABLE ONLY VIA break, WHICH IS THE WHOLE POINT. The quitloop: exit below
     * also carries a negative n -- CmdPrevLevel sets n = -1 -- so a hook placed at a shared exit,
     * or driven off gs->status, would score a death every time the player pressed Previous Level.
     *
     * n < 0 covers every death in both engines: monsters, water, fire, bombs, block squish and
     * running out of time all arrive here through doturn()'s negative return. Deliberately NOT
     * keyed on SND_CHIP_LOSES, which looks like the death signal and is not one -- Lynx raises a
     * different sound for drowning and bombs and none at all for a timeout (lxlogic.c), and MS
     * raises SND_TIME_OUT rather than SND_CHIP_LOSES on a timeout (mslogic.c).
     *
     * n == -2 is CmdQuitLevel (Escape), which is a decision to leave, not a death.
     *
     * KNOWN AND ACCEPTED: doturn() also returns -1 if the tick counter saturates (play.c), which
     * takes about 9.3 hours on a single level. That scores one spurious death. Telling it apart
     * would mean threading a new return code through advancegame() for a case nobody will hit. */
    if (n < 0 && n != -2)
        recorddeath();

    if (!lastrendered)
        drawscreen(TRUE);
    setgameplaymode(EndPlay);
    if (n > 0)
        if (replacesolution())
            savesolutions(&gs->series);
    gs->status = n;
    return TRUE;

quitloop:
    setdisplaymsg(NULL, 0, 0);
    if (!lastrendered)
        drawscreen(TRUE);
    quitgamestate();
    setgameplaymode(EndPlay);
    if (n)
        /* MOD (Jeremy, jc-39): wrapped -- n is only ever the -1/+1 the level-navigation keys set
         * on their way to this label (CmdSameLevel's 0 never reaches here). */
        changecurrentgamewrapped(gs, n);
    return FALSE;
}

/* Skip past secondstoskip seconds from the beginning of the solution.
 */
static int hideandseek(gamespec* gs, int secondstoskip) {
    int n = 0;

    quitgamestate();
    setgameplaymode(EndPlay);
    gs->playmode = Play_None;
    endgamestate();
    initgamestate(gs->series.games + gs->currentgame,
                  gs->series.ruleset);
    prepareplayback();
    gs->playmode = Play_Back;
    gs->status = 0;
    setgameplaymode(NonrenderPlay);

    while (secondsplayed() < secondstoskip) {
        n = doturn(CmdNone);
        if (n)
            break;
        advancetick();
    }
    drawscreen(TRUE);
    setsoundeffects(-1);
    setgameplaymode(NormalPlay);

    return n;
}

/* Advance play by numticks ticks. */
static int advancegame(gamespec* gs, int numticks) {
    int n = 0;
    setgameplaymode(NonrenderPlay);
    while (numticks--) {
        n = doturn(CmdNone);
        if (n)
            break;
        advancetick();
    }
    drawscreen(TRUE);
    setsoundeffects(-1);
    setgameplaymode(SuspendPlay);
    return n;
}

#define ADVANCEGAME(cmd) do { \
    int skipticks; \
    if ((cmd) == CmdAdvanceMoveGame) \
	skipticks = 4; \
    else if (gs->series.ruleset == Ruleset_MS) \
	skipticks = 2; \
    else \
	skipticks = 1; \
    n = advancegame(gs, skipticks); \
    lastrendered = TRUE; \
    SETPAUSED(TRUE, FALSE); \
} while (0)

/* Play back the user's best solution for the current level in real
 * time. Other than the fact that this function runs from a
 * prerecorded series of moves, it has the same behavior as
 * playgame().
 */
static int playbackgame(gamespec* gs, int initcmd) {
    int render, lastrendered, n = 0, cmd;
    int secondstoskip;
    int gamepaused = FALSE;

    secondstoskip = getreplaysecondstoskip();
    if (secondstoskip > 0) {
        n = hideandseek(gs, secondstoskip);
        SETPAUSED(TRUE, FALSE);
    } else if ((initcmd == CmdAdvanceGame) || (initcmd == CmdAdvanceMoveGame))
        ADVANCEGAME(initcmd);
    else {
        drawscreen(TRUE);
        gs->status = 0;
        setgameplaymode(NormalPlay);
    }

    render = lastrendered = TRUE;

    while (!n) {
        if (gamepaused) {
            setgameplaymode(SuspendPlay);
            cmd = input(TRUE);
        } else {
            n = doturn(CmdNone);
            drawscreen(render);
            lastrendered = render;
            if (n)
                break;
            render = waitfortick() || noframeskip;
            cmd = input(FALSE);
        }
        switch (cmd) {
            case CmdSeek:
            case CmdPrev10:
            case CmdNext10:
                if (cmd == CmdSeek) {
                    secondstoskip = getreplaysecondstoskip();
                } else {
                    secondstoskip = secondsplayed() + ((cmd == CmdNext10) ? +10 : -10);
                }
                n = hideandseek(gs, secondstoskip);
                lastrendered = TRUE;
                break;
            /* MOD (Jeremy, jc-39): wrapped, so leaving a replay by level works like anywhere else. */
            case CmdPrevLevel: changecurrentgamewrapped(gs, -1);
                goto quitloop;
            case CmdNextLevel: changecurrentgamewrapped(gs, +1);
                goto quitloop;
            case CmdSameLevel: goto quitloop;
            case CmdPlayback: goto quitloop;
            case CmdQuitLevel: goto quitloop;
            case CmdQuit: exit(0);
            case CmdVolumeUp:
                changevolume(+2, TRUE);
                break;
            case CmdVolumeDown:
                changevolume(-2, TRUE);
                break;
            case CmdPauseGame:
                SETPAUSED(!gamepaused, FALSE);
                break;
            case CmdShowInitState:
                toggleshowinitstate();
                drawscreen(TRUE);
                break;
            case CmdAdvanceGame:
            case CmdAdvanceMoveGame:
                ADVANCEGAME(cmd);
                break;
            case CmdHelp:
                setgameplaymode(SuspendPlay);
                dohelp(Help_None);
                if (!gamepaused) setgameplaymode(NormalPlay);
                break;
        }
    }
    setdisplaymsg(NULL, 0, 0);
    if (!lastrendered)
        drawscreen(TRUE);
    setgameplaymode(EndPlay);
    gs->playmode = Play_None;
    if (n < 0)
        replaceablesolution(gs, +1);
    if (n > 0) {
        if (checksolution())
            savesolutions(&gs->series);
    }
    gs->status = n;
    return TRUE;

quitloop:
    setdisplaymsg(NULL, 0, 0);
    if (!lastrendered)
        drawscreen(TRUE);
    quitgamestate();
    setgameplaymode(EndPlay);
    gs->playmode = Play_None;
    return FALSE;
}

#undef ADVANCEGAME
#undef SETPAUSED

/* Quickly play back the user's best solution for the current level
 * without rendering and without using the timer the keyboard. The
 * playback stops when the solution is finished or gameplay has
 * ended.
 */
static int verifyplayback(gamespec* gs) {
    int n;

    gs->status = 0;
    setdisplaymsg("Verifying ...", FOREVER, 0);
    setgameplaymode(NonrenderPlay);
    for (;;) {
        n = doturn(CmdNone);
        if (n)
            break;
        advancetick();
        switch (input(FALSE)) {
            /* MOD (Jeremy, jc-39): wrapped, as in playbackgame() above. */
            case CmdPrevLevel: changecurrentgamewrapped(gs, -1);
                goto quitloop;
            case CmdNextLevel: changecurrentgamewrapped(gs, +1);
                goto quitloop;
            case CmdSameLevel: goto quitloop;
            case CmdPlayback: goto quitloop;
            case CmdQuitLevel: goto quitloop;
            case CmdQuit: exit(0);
        }
    }
    gs->playmode = Play_None;
    quitgamestate();
    setdisplaymsg(NULL, 0, 0);
    drawscreen(TRUE);
    setgameplaymode(EndPlay);
    if (n < 0) {
#ifndef TWORLDPLUSPLUS
        setdisplaymsg("Invalid solution!", 1, 1);
#endif
        replaceablesolution(gs, +1);
    }
    if (n > 0) {
        if (checksolution())
            savesolutions(&gs->series);
    }
    gs->status = n;
    return TRUE;

quitloop:
    setdisplaymsg(NULL, 0, 0);
    gs->playmode = Play_None;
    setgameplaymode(EndPlay);
    return FALSE;
}

/* Manage a single session of playing the current level, from start to
 * finish. A return value of FALSE indicates that the user is done
 * playing levels from the current series; otherwise, the gamespec
 * structure is updated as necessary upon return.
 */
static int runcurrentlevel(gamespec* gs) {
    int ret = TRUE;
    int cmd;
    int valid, f;
    char const* name;

    name = gs->series.filebase;

    updatehistory(skippathname(name),
                  gs->series.games[gs->currentgame].passwd,
                  gs->series.games[gs->currentgame].number);

    if (gs->enddisplay) {
        gs->enddisplay = FALSE;
        changesubtitle(NULL);
        setenddisplay();
        drawscreen(TRUE);
        displayendmessage(0, 0, 0, 0);
        endgamestate();
        return finalinput(gs);
    }

    valid = initgamestate(gs->series.games + gs->currentgame,
                          gs->series.ruleset);
    /* MOD (Jeremy): window title shows the level PACK name AND the current
     * LEVEL name, i.e. "<pack> - <level>". series.name is the set filename
     * (e.g. "Joshie.dat-ms.dac"), so strip known extensions off the end;
     * the level name is games[currentgame].name (upstream's original title).
     */
    {
        static char const *knownexts[] = { "dac", "dat", "ccl",
                                           "dat-ms", "dat-lynx" };
        static char packname[256];
        static char titlebuf[520];
        char const *levelname;
        char *dot;
        unsigned int i;
        int again = 1;
        strcpy(packname, gs->series.name);
        while (again && (dot = strrchr(packname, '.')) != NULL) {
            again = 0;
            for (i = 0 ; i < sizeof knownexts / sizeof *knownexts ; ++i) {
                if (!stricmp(dot + 1, knownexts[i])) {
                    *dot = '\0';
                    again = 1;
                    break;
                }
            }
        }
        levelname = gs->series.games[gs->currentgame].name;
        if (levelname && *levelname)
            snprintf(titlebuf, sizeof titlebuf, "%s - %s", packname, levelname);
        else
            snprintf(titlebuf, sizeof titlebuf, "%s", packname);
        changesubtitle(titlebuf);
    }
    passwordseen(gs, gs->currentgame);
    if (!islastinseries(gs, gs->currentgame))
        if (!valid || gs->series.games[gs->currentgame].unsolvable)
            passwordseen(gs, gs->currentgame + 1);

    cmd = startinput(gs);

    if (cmd == CmdQuitLevel) {
        ret = FALSE;
    } else {
        if (cmd != CmdNone) {
            if (valid) {
                switch (gs->playmode) {
                    case Play_Normal: f = playgame(gs, cmd);
                        break;
                    case Play_Back: f = playbackgame(gs, cmd);
                        break;
                    case Play_Verify: f = verifyplayback(gs);
                        break;
                    default: f = FALSE;
                        break;
                }
                if (f)
                    ret = endinput(gs);
            } else
                bell();
        }
    }

    endgamestate();
    return ret;
}

static int batchverify(gameseries* series, int display) {
    gamesetup* game;
    int valid = 0, invalid = 0;
    int i, f;

    batchmode = TRUE;

    for (i = 0, game = series->games; i < series->count; ++i, ++game) {
        if (!hassolution(game))
            continue;
        if (initgamestate(game, series->ruleset) && prepareplayback()) {
            setgameplaymode(NonrenderPlay);
            while (!(f = doturn(CmdNone)))
                advancetick();
            setgameplaymode(EndPlay);
            if (f > 0) {
                ++valid;
                checksolution();
            } else {
                ++invalid;
                game->sgflags |= SGF_REPLACEABLE;
                if (display)
                    printf("Solution for level %d is invalid\n", game->number);
            }
        }
        endgamestate();
    }

    if (display) {
        if (valid + invalid == 0) {
            printf("No solutions were found.\n");
        } else {
            printf("  Valid solutions:%4d\n", valid);
            printf("Invalid solutions:%4d\n", invalid);
        }
    }
    return invalid;
}

/*
 * Game selection functions
 */

/* Set the current level to that specified in the history. */
static void findlevelfromhistory(gamespec* gs, char const* name) {
    int i, n;
    history* h;

    name = skippathname(name);
    h = historylist;
    for (i = 0; i < historycount; ++i, ++h) {
        if (stricmp(h->name, name) == 0) {
            n = findlevelinseries(&gs->series, h->levelnumber, h->passwd);
            if (n < 0)
                n = findlevelinseries(&gs->series, 0, h->passwd);
            if (n >= 0) {
                gs->currentgame = n;
                if (passwdsactive(gs) &&
                    !(gs->series.games[n].sgflags & SGF_HASPASSWD))
                    changecurrentgame(gs, -1);
            }
            break;
        }
    }
}

#define PRODUCE_SINGLE_COLUMN_TABLE(table, heading, data, count, L, R) do { \
    size_t _alloc = 0; \
    _alloc += 3 + strlen(heading); \
    for (int _n = 0; _n < (count); ++_n) \
	_alloc += 3 + strlen(L(data)[_n] R); \
    char const **_ptrs = malloc(((count) + 1) * sizeof *_ptrs); \
    char *_textheap = malloc(_alloc); \
    if (!_ptrs || !_textheap) memerrexit(); \
    \
    int _n = 0; \
    int _used = 0; \
    _ptrs[_n++] = _textheap + _used; \
    _used += 1 + sprintf(_textheap + _used, "1-" heading); \
    \
    for (int _y = 0 ; _y < (count) ; ++_y) { \
        _ptrs[_n++] = _textheap + _used; \
	_used += 1 + sprintf(_textheap + _used, \
			    "1-%s", L(data)[_y] R); \
    } \
    (table).rows = (count) + 1; \
    (table).cols = 1; \
    (table).sep = 0; \
    (table).collapse = 0; \
    (table).items = _ptrs; \
} while (0)

#ifdef TWPLUSPLUS
/* Free a table generated by the preceding macro */
static void free_table(tablespec* table) {
    free((void*) table->items[0]);
    free(table->items);
}

/* Determine the index in series->mflist where the gameseries with index idx
 * is found. Returns 0 if there is no such index. */
static int findseries(seriesdata* series, int idx) {
    for (int n = 0; n < series->mfcount; ++n) {
        mapfileinfo* mfi = &series->mflist[n];
        for (int i = Ruleset_First; i < Ruleset_Count; ++i) {
            intlist* gsl = &mfi->sfilelst[i];
            for (int j = 0; j < gsl->count; ++j) {
                if (gsl->list[j] == idx)
                    return n;
            }
        }
    }
    return 0;
}
#endif

/* Helper function for selectseriesandlevel */
static int chooseseries(seriesdata* series, int* pn, int founddefault) {
#ifndef TWPLUSPLUS
    return displaylist("   Welcome to Tile World. Type ? or F1 for help.",
		&series->table, pn, LIST_SERIES, scrollinputcallback);
#else
    tablespec mftable;
    PRODUCE_SINGLE_COLUMN_TABLE(mftable, "Levelset",
                                series->mflist, series->mfcount, , .filename);

    /* Choose mapfile to be selected by default */
    int n = (founddefault ? findseries(series, *pn) : 0);

    int chosenseries = -1;
    while (chosenseries < 0) {
        int f = displaylist("", &mftable, &n, LIST_MAPFILES, scrollinputcallback);
        if (f != CmdProceed) {
            free_table(&mftable);
            return f;
        }
        int ruleset = getselectedruleset();
        intlist* chosengsl = &series->mflist[n].sfilelst[ruleset];

        if (chosengsl->count < 1) /* Can happen if .dac name was taken */
            continue;
        if (chosengsl->count == 1)
            chosenseries = chosengsl->list[0];
        else {
            tablespec gstable;
            PRODUCE_SINGLE_COLUMN_TABLE(gstable, "Profile",
                                        chosengsl->list, chosengsl->count, series->list[, ].filebase);
            int m = 0;
            for (;;) {
                f = displaylist("", &gstable, &m, LIST_SERIES, scrollinputcallback);
                if (f == CmdProceed) {
                    chosenseries = chosengsl->list[m];
                    break;
                } else if (f == CmdQuitLevel)
                    break;
            }
            free_table(&gstable);
        }
    }
    free_table(&mftable);
    *pn = chosenseries;
    return CmdProceed;
#endif
}

/* Display the full selection of available series to the user as a
 * scrolling list, and permit one to be selected. When one is chosen,
 * pick one of levels to be the current level. All fields of the
 * gamespec structure are initialized. If autosel is TRUE, then the
 * function will skip the display if there is only one series
 * available. If defaultseries is not NULL, and matches the name of
 * one of the series in the array, then the scrolling list will be
 * initialized with that series selected. If defaultlevel is not zero,
 * and a level in the selected series that the user is permitted to
 * access matches it, then that level will be the initial current
 * level. The return value is zero if nothing was selected, negative
 * if an error occurred, or positive otherwise.
 */
static int selectseriesandlevel(gamespec* gs, seriesdata* series, int autosel,
                                char const* defaultseries, int defaultlevel) {
    int okay, f, n;

    if (series->count < 1) {
        errmsg(NULL, "no level sets found");
        return -1;
    }

    okay = TRUE;
    if (series->count == 1 && autosel) {
        getseriesfromlist(&gs->series, series->list, 0);
    } else {
        n = 0;
        int founddefault = FALSE;
        if (defaultseries) {
            n = series->count;
            while (n)
                if (!strcmp(series->list[--n].filebase, defaultseries)) {
                    founddefault = TRUE;
                    break;
                }
        }
        for (;;) {
            f = chooseseries(series, &n, founddefault);
            if (f == CmdProceed) {
                getseriesfromlist(&gs->series, series->list, n);
                okay = TRUE;
                break;
            } else if (f == CmdQuitLevel) {
                okay = FALSE;
                break;
            } else if (f == CmdHelp) {
                pushsubtitle("Help");
                dohelp(Help_First);
                popsubtitle();
            }
        }
    }
    freeserieslist(series->list, series->count,
                   series->mflist, series->mfcount, &series->table);
    if (!okay)
        return 0;

    setstringsetting("selectedseries", gs->series.filebase);

    if (!readseriesfile(&gs->series)) {
        errmsg(gs->series.filebase, "cannot read data file");
        freeseriesdata(&gs->series);
        return -1;
    }
    if (gs->series.count < 1) {
        errmsg(gs->series.filebase, "no levels found in data file");
        freeseriesdata(&gs->series);
        return -1;
    }

    gs->enddisplay = FALSE;
    gs->playmode = Play_None;
    gs->usepasswds = usepasswds && !(gs->series.gsflags & GSF_IGNOREPASSWDS);
    gs->currentgame = -1;
    gs->melindacount = 0;

    if (defaultlevel) {
        n = findlevelinseries(&gs->series, defaultlevel, NULL);
        if (n >= 0) {
            gs->currentgame = n;
            if (passwdsactive(gs) &&
                !(gs->series.games[n].sgflags & SGF_HASPASSWD))
                changecurrentgame(gs, -1);
        }
    }

    if (gs->currentgame < 0)
        findlevelfromhistory(gs, gs->series.filebase);

    if (gs->currentgame < 0) {
        gs->currentgame = 0;
        for (n = 0; n < gs->series.count; ++n) {
            if (!issolved(gs, n)) {
                gs->currentgame = n;
                break;
            }
        }
    }

    return +1;
}

/* Get the list of available series and permit the user to select one
 * to play. If lastseries is not NULL, use that series as the default.
 * The return value is zero if nothing was selected, negative if an
 * error occurred, or positive otherwise.
 */
static int choosegame(gamespec* gs, char const* lastseries) {
    seriesdata s;

    if (!createserieslist(NULL, &s.list, &s.count, &s.mflist, &s.mfcount,
                          &s.table))
        return -1;
    return selectseriesandlevel(gs, &s, FALSE, lastseries, 0);
}

/*
 * Initialization functions.
 */

/* MOD (Jeremy): the directory the program itself lives in (or was started from). settings.cpp
 * reads and writes tw_settings.ini here; see the comment on sfname there for why the settings file
 * moved out of the save directory. Defined here because initdirs() below is what resolves it. */
char *appdir = NULL;

/* Set the four directories that the program uses (the series
 * directory, the series data directory, the resource directory, and
 * the save directory).  Any or all of the arguments can be NULL,
 * indicating that the default value should be used. The environment
 * variables TWORLDDIR, TWORLDSAVEDIR, and HOME can define the default
 * values. If any or all of these are unset, the program will use the
 * default values it was compiled with.
 */
static void initdirs(char const* series, char const* seriesdat,
                     char const* res, char const* save) {
    unsigned int maxpath;
    char const* root = NULL;
    char const* dir;

    maxpath = getpathbufferlen();
    if (series && strlen(series) >= maxpath) {
        errmsg(NULL, "Data (-D) directory name is too long;"
               " using default value instead");
        series = NULL;
    }
    if (seriesdat && strlen(seriesdat) >= maxpath) {
        errmsg(NULL, "Configured data (-C) directory name is too long;"
               " using default value instead");
        seriesdat = NULL;
    }
    if (res && strlen(res) >= maxpath) {
        errmsg(NULL, "Resource (-R) directory name is too long;"
               " using default value instead");
        res = NULL;
    }
    if (save && strlen(save) >= maxpath) {
        errmsg(NULL, "Save (-S) directory name is too long;"
               " using default value instead");
        save = NULL;
    }
    if (!save && (dir = getenv("TWORLDSAVEDIR")) && *dir) {
        if (strlen(dir) < maxpath)
            save = dir;
        else
            warn("Value of environment variable TWORLDSAVEDIR is too long");
    }

    if (!res || !series || !seriesdat) {
        if ((dir = getenv("TWORLDDIR")) && *dir) {
            if (strlen(dir) < maxpath - 8)
                root = dir;
            else
                warn("Value of environment variable TWORLDDIR is too long");
        }
        if (!root) {
#ifdef ROOTDIR
	    root = ROOTDIR;
#else
            root = ".";
#endif
        }
    }

    /* MOD (Jeremy): the program's own directory, published so settings.cpp can put
     * tw_settings.ini beside the executable instead of inside the save directory. It is the same
     * "root" the res/sets/data directories hang off -- $TWORLDDIR, or ROOTDIR on a system build,
     * or "." for the portable Windows release, which is the case that matters here. Resolved even
     * when -R/-L/-D were all given, because those override the subdirectories, not the root. */
    appdir = getpathbuffer();
    strcpy(appdir, root ? root : ".");

    resdir = getpathbuffer();
    if (res)
        strcpy(resdir, res);
    else
        combinepath(resdir, root, "res");

    seriesdir = getpathbuffer();
    if (series)
        strcpy(seriesdir, series);
    else
        combinepath(seriesdir, root, "sets");

    seriesdatdir = getpathbuffer();
    if (seriesdat)
        strcpy(seriesdatdir, seriesdat);
    else
        combinepath(seriesdatdir, root, "data");

    savedir = getpathbuffer();
    if (!save) {
#ifdef SAVEDIR
        save = SAVEDIR;
#else
	if ((dir = getenv("HOME")) && *dir && strlen(dir) < maxpath - 8)
	    combinepath(savedir, dir, ".tworld");
	else
	    combinepath(savedir, root, "save");

#endif
    } else {
        strcpy(savedir, save);
    }
}

/* Parse the command-line options and arguments, and initialize the
 * user-controlled options.
 */
static int initoptionswithcmdline(int argc, char* argv[], startupdata* start) {
    cmdlineinfo opts;
    char const* optresdir = NULL;
    char const* optseriesdir = NULL;
    char const* optseriesdatdir = NULL;
    char const* optsavedir = NULL;
    char buf[256];
    int listdirs, pedantic;
    int ch, n;
    char* p;

    start->filename = getpathbuffer();
    *start->filename = '\0';
    start->savefilename = NULL;
    start->levelnum = 0;
    start->listseries = FALSE;
    start->listscores = FALSE;
    start->listtimes = FALSE;
    start->batchverify = FALSE;
    listdirs = FALSE;
    pedantic = FALSE;
    mudsucking = 1;
    soundbufsize = 0;
    volumelevel = -1;

    initoptions(&opts, argc - 1, argv + 1, "abD:dFfHhL:lm:n:PpqR:rS:stVv:c");
    while ((ch = readoption(&opts)) >= 0) {
        switch (ch) {
            case 0:
                if (start->savefilename && start->levelnum) {
                    fprintf(stderr, "too many arguments: %s\n", opts.val);
                    printtable(stderr, yowzitch);
                    return FALSE;
                }
                if (!start->levelnum && (n = (int) strtol(opts.val, &p, 10)) > 0
                    && *p == '\0') {
                    start->levelnum = n;
                } else if (*start->filename) {
                    start->savefilename = getpathbuffer();
                    sprintf(start->savefilename, "%.*s", getpathbufferlen(),
                            opts.val);
                } else {
                    sprintf(start->filename, "%.*s", getpathbufferlen(), opts.val);
                }
                break;
            case 'D': optseriesdatdir = opts.val;
                break;
            case 'L': optseriesdir = opts.val;
                break;
            case 'R': optresdir = opts.val;
                break;
            case 'S': optsavedir = opts.val;
                break;
            case 'H': showhistogram = !showhistogram;
                break;
            case 'f': noframeskip = !noframeskip;
                break;
            case 'F': fullscreen = !fullscreen;
                break;
            case 'p': usepasswds = !usepasswds;
                break;
            case 'q': silence = !silence;
                break;
            case 'r': readonly = !readonly;
                break;
            case 'P': pedantic = !pedantic;
                break;
            case 'c': casualinputs = !casualinputs;
                break;
            case 'a': ++soundbufsize;
                break;
            case 'd': listdirs = TRUE;
                break;
            case 'l': start->listseries = TRUE;
                break;
            case 's': start->listscores = TRUE;
                break;
            case 't': start->listtimes = TRUE;
                break;
            case 'b': start->batchverify = TRUE;
                break;
            case 'm': mudsucking = atoi(opts.val);
                break;
            case 'n': volumelevel = atoi(opts.val);
                break;
            case 'h': printtable(stdout, yowzitch);
                exit(EXIT_SUCCESS);
            case 'v': puts(VERSION);
                exit(EXIT_SUCCESS);
            case 'V': printtable(stdout, vourzhon);
                exit(EXIT_SUCCESS);
            case ':':
                fprintf(stderr, "option requires an argument: -%c\n", opts.opt);
                printtable(stderr, yowzitch);
                return FALSE;
            case '?':
                fprintf(stderr, "unrecognized option: -%c\n", opts.opt);
                printtable(stderr, yowzitch);
                return FALSE;
            default:
                printtable(stderr, yowzitch);
                return FALSE;
        }
    }

    if (pedantic)
        setpedanticmode();

    initdirs(optseriesdir, optseriesdatdir, optresdir, optsavedir);
    if (listdirs) {
        printdirectories();
        exit(EXIT_SUCCESS);
    }

    if (*start->filename && !start->savefilename) {
        if (loadsolutionsetname(start->filename, buf) > 0) {
            start->savefilename = getpathbuffer();
            strcpy(start->savefilename, start->filename);
            strcpy(start->filename, buf);
        }
    }

    if (start->listscores || start->listtimes || start->batchverify
        || start->levelnum)
        if (!*start->filename)
            strcpy(start->filename, "chips.dat");

    return TRUE;
}

/* Run the initialization routines of oshw and the resource module.
 */
static int initializesystem(void) {
#ifdef NDEBUG
    mudsucking = 1;
#endif
    setmudsuckingfactor(mudsucking);
    if (!oshwinitialize(silence, soundbufsize, showhistogram, fullscreen))
        return FALSE;
    if (!initresources())
        return FALSE;
    setkeyboardrepeat(TRUE);
    if (volumelevel < 0)
        volumelevel = getintsetting("volume");
    if (volumelevel >= 0)
        setvolume(volumelevel, FALSE);

    return TRUE;
}

/* Time for everyone to clean up and go home.
 */
static void shutdownsystem(void) {
    savesettings();
    savehistory();
    shutdowngamestate();
    freeallresources();
    free(resdir);
    free(seriesdir);
    free(seriesdatdir);
    free(savedir);
}

/* Determine what to play. A list of available series is drawn up; if
 * only one is found, it is selected automatically. Otherwise, if the
 * listseries option is TRUE, the available series are displayed on
 * stdout and the program exits. Otherwise, if listscores or listtimes
 * is TRUE, the scores or times for a single series is display on
 * stdout and the program exits. (These options need to be checked for
 * before initializing the graphics subsystem.) Otherwise, the
 * selectseriesandlevel() function handles the rest of the work. Note
 * that this function is only called during the initial startup; if
 * the user returns to the series list later on, the choosegame()
 * function is called instead.
 */
static int choosegameatstartup(gamespec* gs, char const* lastseries,
                               startupdata const* start) {
    seriesdata series;
    tablespec table;
    int n;

    if (!createserieslist(start->filename,
                          &series.list, &series.count,
                          &series.mflist, &series.mfcount,
                          &series.table))
        return -1;

    free(start->filename);

    if (series.count <= 0) {
        errmsg(NULL, "no level sets found");
        return -1;
    }

    if (start->listseries) {
        printtable(stdout, &series.table);
        if (!series.count)
            puts("(no files)");
        return 0;
    }

    if (series.count == 1) {
        if (start->savefilename)
            series.list[0].savefilename = start->savefilename;
        if (!readseriesfile(series.list)) {
            errmsg(series.list[0].filebase, "cannot read level set");
            return -1;
        }
        if (start->batchverify) {
            n = batchverify(series.list, !silence && !start->listtimes
                                         && !start->listscores);
            if (silence)
                exit(n > 100 ? 100 : n);
            else if (!start->listtimes && !start->listscores)
                return 0;
        }
        if (start->listscores) {
            if (!createscorelist(series.list, usepasswds, '0',
                                 NULL, NULL, &table))
                return -1;
            freeserieslist(series.list, series.count,
                           series.mflist, series.mfcount, &series.table);
            printtable(stdout, &table);
            freescorelist(NULL, &table);
            return 0;
        }
        if (start->listtimes) {
            if (!createtimelist(series.list,
                                series.list->ruleset == Ruleset_MS ? 10 : 100,
                                '0', NULL, NULL, &table))
                return -1;
            freeserieslist(series.list, series.count,
                           series.mflist, series.mfcount, &series.table);
            printtable(stdout, &table);
            freetimelist(NULL, &table);
            return 0;
        }
    }

    if (!initializesystem()) {
        errmsg(NULL, "cannot initialize program due to previous errors");
        return -1;
    }

    /* extensions cannot be read until the system is initialized */
    if (series.count == 1)
        readextensions(series.list);

    return selectseriesandlevel(gs, &series, TRUE, lastseries, start->levelnum);
}

/*
 * The main function.
 */

int tworld(int argc, char* argv[]) {
    startupdata start;
    gamespec spec;
    char lastseries[sizeof spec.series.filebase];
    int f;

    if (!initoptionswithcmdline(argc, argv, &start))
        return EXIT_FAILURE;

    loadhistory();
    loadsettings();

    /* MOD (Jeremy): re-apply the window title now that the settings are in, so
     * the runtime "showbuildtag" toggle is reflected as early as possible.
     *
     * CORRECTED 2026-08-12: this comment used to claim the window already
     * existed here, via initoptionswithcmdline() -> initializesystem(). It does
     * not. initializesystem() is called from choosegameatstartup() BELOW, so
     * g_pMainWnd is still null on this line and setsubtitle() returns
     * immediately -- the call is a harmless no-op kept for the day that
     * ordering changes. The build tag toggles correctly regardless, because
     * every later setsubtitle() recomposes the title through
     * TileWorldApp::WindowTitle(), which reads the by-then-loaded setting.
     *
     * The useful consequence: settings ARE loaded before the main window is
     * constructed, so the window can read them directly in its constructor
     * (see TileWorldMainWnd, which does exactly that for the background color
     * and the Options state). */
    setsubtitle(NULL);

    atexit(shutdownsystem);

    char const* selectedseries = getstringsetting("selectedseries");
    if (selectedseries && strlen(selectedseries) < sizeof lastseries)
        strcpy(lastseries, selectedseries);
    else
        lastseries[0] = '\0';

    if (getintsetting("showinitstate") > 0)
        toggleshowinitstate();

    f = choosegameatstartup(&spec, lastseries, &start);
    if (f < 0)
        return EXIT_FAILURE;
    else if (f == 0)
        return EXIT_SUCCESS;

    while (f > 0) {
        pushsubtitle(NULL);
        while (runcurrentlevel(&spec)) {
        }
        savehistory();
        popsubtitle();
        cleardisplay();
        strcpy(lastseries, spec.series.filebase);
        freeseriesdata(&spec.series);
        f = choosegame(&spec, lastseries);
    };

    return (f == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
