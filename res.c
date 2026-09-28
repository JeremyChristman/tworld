/* res.c: Functions for loading resources from external files.
 *
 * Copyright (C) 2001-2014 by Brian Raiter and Eric Schmidt, under the GNU
 * General Public License. No warranty. See COPYING for details.
 */

#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<sys/types.h>	/* MOD (Jeremy, jc-59): stat(), for a sound pack's files */
#include	<sys/stat.h>
#include	"defs.h"
#include	"fileio.h"
#include	"err.h"
#include	"oshw.h"
#include	"messages.h"
#include	"unslist.h"
#include	"settings.h"
#include	"res.h"

/*
 * The resource ID numbers
 */

#define	RES_IMG_BASE		0
#define	RES_IMG_TILES		(RES_IMG_BASE + 0)
#define	RES_IMG_FONT		(RES_IMG_BASE + 1)
#define	RES_IMG_LAST		RES_IMG_FONT

#define	RES_CLR_BASE		(RES_IMG_LAST + 1)
#define	RES_CLR_BKGND		(RES_CLR_BASE + 0)
#define	RES_CLR_TEXT		(RES_CLR_BASE + 1)
#define	RES_CLR_BOLD		(RES_CLR_BASE + 2)
#define	RES_CLR_DIM		(RES_CLR_BASE + 3)
#define	RES_CLR_LAST		RES_CLR_DIM

#define	RES_TXT_BASE		(RES_CLR_LAST + 1)
#define	RES_TXT_UNSLIST		(RES_TXT_BASE + 0)
#define RES_TXT_MESSAGE		(RES_TXT_BASE + 1)
#define	RES_TXT_LAST		RES_TXT_MESSAGE

#define	RES_SND_BASE		(RES_TXT_LAST + 1)
#define	RES_SND_CHIP_LOSES	(RES_SND_BASE + SND_CHIP_LOSES)
#define	RES_SND_CHIP_WINS	(RES_SND_BASE + SND_CHIP_WINS)
#define	RES_SND_TIME_OUT	(RES_SND_BASE + SND_TIME_OUT)
#define	RES_SND_TIME_LOW	(RES_SND_BASE + SND_TIME_LOW)
#define	RES_SND_DEREZZ		(RES_SND_BASE + SND_DEREZZ)
#define	RES_SND_CANT_MOVE	(RES_SND_BASE + SND_CANT_MOVE)
#define	RES_SND_IC_COLLECTED	(RES_SND_BASE + SND_IC_COLLECTED)
#define	RES_SND_ITEM_COLLECTED	(RES_SND_BASE + SND_ITEM_COLLECTED)
#define	RES_SND_BOOTS_STOLEN	(RES_SND_BASE + SND_BOOTS_STOLEN)
#define	RES_SND_TELEPORTING	(RES_SND_BASE + SND_TELEPORTING)
#define	RES_SND_DOOR_OPENED	(RES_SND_BASE + SND_DOOR_OPENED)
#define	RES_SND_SOCKET_OPENED	(RES_SND_BASE + SND_SOCKET_OPENED)
#define	RES_SND_BUTTON_PUSHED	(RES_SND_BASE + SND_BUTTON_PUSHED)
#define	RES_SND_TILE_EMPTIED	(RES_SND_BASE + SND_TILE_EMPTIED)
#define	RES_SND_WALL_CREATED	(RES_SND_BASE + SND_WALL_CREATED)
#define	RES_SND_TRAP_ENTERED	(RES_SND_BASE + SND_TRAP_ENTERED)
#define	RES_SND_BOMB_EXPLODES	(RES_SND_BASE + SND_BOMB_EXPLODES)
#define	RES_SND_WATER_SPLASH	(RES_SND_BASE + SND_WATER_SPLASH)
#define	RES_SND_SKATING_TURN	(RES_SND_BASE + SND_SKATING_TURN)
#define	RES_SND_BLOCK_MOVING	(RES_SND_BASE + SND_BLOCK_MOVING)
#define	RES_SND_SKATING_FORWARD	(RES_SND_BASE + SND_SKATING_FORWARD)
#define	RES_SND_SLIDING		(RES_SND_BASE + SND_SLIDING)
#define	RES_SND_SLIDEWALKING	(RES_SND_BASE + SND_SLIDEWALKING)
#define	RES_SND_ICEWALKING	(RES_SND_BASE + SND_ICEWALKING)
#define	RES_SND_WATERWALKING	(RES_SND_BASE + SND_WATERWALKING)
#define	RES_SND_FIREWALKING	(RES_SND_BASE + SND_FIREWALKING)
#define	RES_SND_LAST		RES_SND_FIREWALKING

#define	RES_COUNT		(RES_SND_LAST + 1)

/* Structure for enumerating the resource names.
 */
typedef	struct rcitem {
    char const *name;
    int		numeric;
} rcitem;

/* Union for storing the resource values.
 */
typedef union resourceitem {
    int		num;
    char	str[256];
} resourceitem;

/* The complete list of resource names.
 */
static rcitem rclist[RES_COUNT] = {
    { "tileimages",		FALSE },
    { "font",			FALSE },
    { "backgroundcolor",	FALSE },
    { "textcolor",		FALSE },
    { "boldtextcolor",		FALSE },
    { "dimtextcolor",		FALSE },
    { "unsolvablelist",		FALSE },
    { "endmessages",		FALSE },
    { "chipdeathsound",		FALSE },
    { "levelcompletesound",	FALSE },
    { "chipdeathbytimesound",	FALSE },
    { "ticksound",		FALSE },
    { "derezzsound",		FALSE },
    { "blockedmovesound",	FALSE },
    { "pickupchipsound",	FALSE },
    { "pickuptoolsound",	FALSE },
    { "thiefsound",		FALSE },
    { "teleportsound",		FALSE },
    { "opendoorsound",		FALSE },
    { "socketsound",		FALSE },
    { "switchsound",		FALSE },
    { "tileemptiedsound",	FALSE },
    { "wallcreatedsound",	FALSE },
    { "trapenteredsound",	FALSE },
    { "bombsound",		FALSE },
    { "splashsound",		FALSE },
    { "blockmovingsound",	FALSE },
    { "skatingforwardsound",	FALSE },
    { "skatingturnsound",	FALSE },
    { "slidingsound",		FALSE },
    { "slidewalkingsound",	FALSE },
    { "icewalkingsound",	FALSE },
    { "waterwalkingsound",	FALSE },
    { "firewalkingsound",	FALSE }
};

/* The complete collection of resource values.
 */
static resourceitem	allresources[Ruleset_Count][RES_COUNT];

/* The resource values for the current ruleset.
 */
static resourceitem    *resources = NULL;

/* The ruleset-independent resource values.
 */
static resourceitem    *globalresources = allresources[Ruleset_None];

/* The active ruleset.
 */
static int		currentruleset = Ruleset_None;

/* The directory containing all the resource files.
 */
char		       *resdir = NULL;

/*
 * MOD (Jeremy, jc-41): the user-selectable tileset. See res.h.
 */

/* The settings key naming each ruleset's chosen tileset.
 *
 * Indexed by the ruleset enum, the way allresources is, so a third ruleset
 * would need nothing here but a third entry. Designated initializers rather
 * than positional ones ON PURPOSE: defs.h orders the enum Lynx BEFORE MS,
 * which is the reverse of how everyone says it, and a positional literal here
 * would read as correct while being exactly backwards.
 */
/* MOD (Jeremy, jc-42): TRUE when the tiles now loaded came from the user's chosen
 * tileset rather than from the rc file's fallback. Set by loadimages(), read by
 * reloadtileset() -- see the note there for why the distinction matters.
 */
static int overrideloaded = FALSE;

static char const *const tilesetkey[Ruleset_Count] = {
    [Ruleset_None] = NULL,
    [Ruleset_Lynx] = "lynxtileset",
    [Ruleset_MS]   = "mstileset"
};

/* TRUE if name is safe to append to the tileset directory.
 *
 * The value reaches us from tw_settings.ini, which the user is invited to edit
 * by hand, so it is untrusted input. combinepath() treats a path beginning
 * with a separator as ABSOLUTE and discards the directory it was given, so
 * without this an ini could point the loader anywhere on the disk.
 *
 * Both separators are rejected whatever DIRSEP_CHAR happens to be: the file is
 * normally written on Windows and may be carried to a build where the native
 * separator differs. Also rejected: a drive-letter prefix, any "..", control
 * characters, and a name that is empty or nothing but whitespace.
 */
/* MOD (Jeremy, jc-48): the reserved-device-name check that lived here from
 * jc-42 has MOVED to fileio.c as isreservedfilename(), with the opposite
 * (intuitive) polarity -- it now returns TRUE when the name IS reserved.
 *
 * A .dac's `file=` reaches openfileindir() exactly as a tileset name reaches
 * gettilesetpath(), so it needs the identical guard; this fork had it for
 * tilesets and not for level sets. Two copies of a check like this drift, and
 * fileio.c is the file that owns file access. It also gains a unit test there,
 * via test/series_test.c -- res.c has none.
 */

static int istilesetname(char const *name)
{
    char const *p;

    if (!name || !*name)
	return FALSE;
    for (p = name ; *p ; ++p)
	if (!isspace((unsigned char)*p))
	    break;
    if (!*p)
	return FALSE;
    /* MOD (Jeremy, jc-42): ".." only means "parent" when it is the WHOLE name.
     * Separators are rejected below, so the value is always a single path component,
     * and a substring test therefore rejected ordinary filenames -- "x..y.bmp" is
     * legal, and since the menu now filters through this predicate such a file would
     * simply vanish from the list with no explanation. */
    if (!strcmp(name, ".."))
	return FALSE;
    for (p = name ; *p ; ++p) {
	if (*p == '/' || *p == '\\')
	    return FALSE;
	/* A colon ANYWHERE, not just a drive letter at position 1: on Windows
	 * "tiles.bmp:hidden" names an alternate data stream of a file in this
	 * directory. Contained rather than dangerous, but there is no reason for a
	 * tileset name to contain one. */
	if (*p == ':')
	    return FALSE;
	if ((unsigned char)*p < ' ')
	    return FALSE;
    }
    return !isreservedfilename(name);
}

int gettilesetpath(char *dest, char const *name)
{
    /* MOD (Jeremy, jc-42): dest really is cleared on every failure now. It used to be
     * left holding whatever combinepath() had already written -- the tileset directory,
     * or a path with a trailing separator on the ENAMETOOLONG path, since combinepath()
     * appends before it length-checks. Every caller checks the return value, so nothing
     * was broken; the header promised otherwise, and the next caller reads the header. */
    if (!combinepath(dest, resdir, TILESETDIR)) {
	dest[0] = '\0';
	return FALSE;
    }
    if (!name)
	return TRUE;
    if (!istilesetname(name) || !combinepath(dest, dest, name)) {
	dest[0] = '\0';
	return FALSE;
    }
    return TRUE;
}

int getcurrentruleset(void)
{
    return currentruleset;
}

/* MOD (Jeremy, jc-59): the per-ruleset override lookup, shared by the tileset and
 * the sound pack. It was written out inside the two tileset functions until the
 * sound pack needed the same thing; a second copy would have carried the range
 * check twice, and that check is one CLAUDE.md §5 records as caught by the
 * sanitizer layer alone -- two copies means two survivors to keep track of.
 */
static char const *getoverride(char const *const keys[Ruleset_Count], int ruleset)
{
    if (ruleset < 0 || ruleset >= Ruleset_Count || !keys[ruleset])
	return NULL;
    return getstringsetting(keys[ruleset]);
}

static void setoverride(char const *const keys[Ruleset_Count], int ruleset,
			char const *name)
{
    if (ruleset < 0 || ruleset >= Ruleset_Count || !keys[ruleset])
	return;
    setstringsetting(keys[ruleset], name ? name : "");
}

char const *gettilesetoverride(int ruleset)
{
    return getoverride(tilesetkey, ruleset);
}

void settilesetoverride(int ruleset, char const *name)
{
    setoverride(tilesetkey, ruleset, name);
}

/* A few resources have non-empty default values.
 */
static void initresourcedefaults(void)
{
    strcpy(allresources[Ruleset_None][RES_IMG_TILES].str, "tiles.bmp");
    strcpy(allresources[Ruleset_None][RES_IMG_FONT].str, "font.bmp");
    strcpy(allresources[Ruleset_None][RES_CLR_BKGND].str, "000000");
    strcpy(allresources[Ruleset_None][RES_CLR_TEXT].str, "FFFFFF");
    strcpy(allresources[Ruleset_None][RES_CLR_BOLD].str, "FFFF00");
    strcpy(allresources[Ruleset_None][RES_CLR_DIM].str, "C0C0C0");
    memcpy(&allresources[Ruleset_MS], globalresources,
				sizeof allresources[Ruleset_MS]);
    memcpy(&allresources[Ruleset_Lynx], globalresources,
				sizeof allresources[Ruleset_Lynx]);
}

/* Iterate through the lines of the rc file, storing the values in the
 * allresources array. Lines consisting only of whitespace, or with an
 * octothorpe as the first non-whitespace character, are skipped over.
 * Lines containing a ruleset in brackets introduce ruleset-specific
 * resource values. Ruleset-independent values are copied into each of
 * the ruleset-specific entries. FALSE is returned if the rc file
 * could not be opened.
 */
static int readrcfile(void)
{
    resourceitem	item;
    fileinfo		file = {0};
    char		buf[256];
    char		name[256];
    char	       *p;
    int			ruleset;
    int			lineno, i, j;

    if (!openfileindir(&file, resdir, "rc", "r", "can't open"))
	return FALSE;

    ruleset = Ruleset_None;
    for (lineno = 1 ; ; ++lineno) {
	i = sizeof buf - 1;
	if (!filegetline(&file, buf, &i, NULL))
	    break;
	for (p = buf ; isspace((unsigned char)*p) ; ++p) ;
	if (!*p || *p == '#')
	    continue;
	if (sscanf(buf, "[%[^]]]", name) == 1) {
	    for (p = name ; (*p = tolower((unsigned char)*p)) != '\0' ; ++p) ;
	    if (!strcmp(name, "ms"))
		ruleset = Ruleset_MS;
	    else if (!strcmp(name, "lynx"))
		ruleset = Ruleset_Lynx;
	    else if (!strcmp(name, "all"))
		ruleset = Ruleset_None;
	    else
		warn("rc:%d: syntax error", lineno);
	    continue;
	}
	if (sscanf(buf, "%[^=]=%s", name, item.str) != 2) {
	    warn("rc:%d: syntax error", lineno);
	    continue;
	}
	for (p = name ; (*p = tolower((unsigned char)*p)) != '\0' ; ++p) ;
	for (i = sizeof rclist / sizeof *rclist - 1 ; i >= 0 ; --i)
	    if (!strcmp(name, rclist[i].name))
		break;
	if (i < 0) {
	    warn("rc:%d: illegal resource name \"%s\"", lineno, name);
	    continue;
	}
	if (rclist[i].numeric) {
	    i = atoi(item.str);
	    item.num = i;
	}
	allresources[ruleset][i] = item;
	if (ruleset == Ruleset_None)
	    for (j = Ruleset_None ; j < Ruleset_Count ; ++j)
		allresources[j][i] = item;
    }

    fileclose(&file, NULL);
    return TRUE;
}

/*
 * Resource-loading functions
 */

/* Parse the color-definition resource values.
 */
static int loadcolors(void)
{
    long	bkgnd, text, bold, dim;
    char       *end;

    bkgnd = strtol(resources[RES_CLR_BKGND].str, &end, 16);
    if (*end || bkgnd < 0 || bkgnd > 0xFFFFFF) {
	warn("rc: invalid color ID for background");
	bkgnd = -1;
    }
    text = strtol(resources[RES_CLR_TEXT].str, &end, 16);
    if (*end || text < 0 || text > 0xFFFFFF) {
	warn("rc: invalid color ID for text");
	text = -1;
    }
    bold = strtol(resources[RES_CLR_BOLD].str, &end, 16);
    if (*end || bold < 0 || bold > 0xFFFFFF) {
	warn("rc: invalid color ID for bold text");
	bold = -1;
    }
    dim = strtol(resources[RES_CLR_DIM].str, &end, 16);
    if (*end || dim < 0 || dim > 0xFFFFFF) {
	warn("rc: invalid color ID for dim text");
	dim = -1;
    }

    setcolors(bkgnd, text, bold, dim);
    return TRUE;
}

/* Attempt to load the tile images.
 */
static int loadimages(void)
{
    char       *path;
    int		f;

    f = FALSE;
    path = getpathbuffer();
    overrideloaded = FALSE;

    /* MOD (Jeremy, jc-41): the user's chosen tileset, tried FIRST and in
     * silence. complain is FALSE because none of the ways this can fail are
     * errors -- an unset key, a file the user deleted, a JPEG they dropped in
     * by mistake -- and every one of them is answered by falling through to
     * the rc file's tiles below. That fall-through IS the failsafe; the rc
     * tiers are reached exactly as they were before this block existed.
     *
     * Reading the settings table from here is why res.c includes settings.h.
     */
    {
	char const *sel = gettilesetoverride(currentruleset);
	if (sel && *sel && gettilesetpath(path, sel)) {
	    f = loadtileset(path, FALSE);
	    overrideloaded = f;
	}
    }

    /* combinepath() can fail with ENAMETOOLONG, and leaves path stale when it
     * does -- harmless while the only inputs were the short literals in rc,
     * but a tileset name is now user-supplied and can be long. Handing a stale
     * buffer to loadtileset() would load whatever the previous tier built.
     */
    if (!f && *resources[RES_IMG_TILES].str) {
	if (combinepath(path, resdir, resources[RES_IMG_TILES].str))
	    f = loadtileset(path, TRUE);
    }
    if (!f && resources != globalresources
	   && *globalresources[RES_IMG_TILES].str) {
	if (combinepath(path, resdir, globalresources[RES_IMG_TILES].str))
	    f = loadtileset(path, TRUE);
    }
    free(path);

    if (!f)
	errmsg(resdir, "no valid tilesets found");
    return f;
}

/* MOD (Jeremy, jc-41): reload just the tiles for the ruleset already in play.
 *
 * setrulesetbehavior() cannot serve here: it returns early when the ruleset is
 * unchanged, which is every menu-driven swap.
 *
 * The geng.wtile check is the one guard this needs. loadtileset() frees the
 * old tiles once a format branch is chosen, and settilesize() can THEN reject
 * the sheet (its dimensions must divide by four), so a sheet that passes the
 * dimension sniff but fails that rule leaves no tiles and a tile size of zero.
 * If the rc tiers also failed to recover, returning TRUE here would let the
 * caller build a zero-sized display and _windowmappos() would divide by zero
 * on the next mouse movement over the map.
 */
int reloadtileset(void)
{
    char const *sel;

    if (currentruleset == Ruleset_None)
	return FALSE;
    if (!loadimages() || !istilesetloaded())
	return FALSE;

    /* MOD (Jeremy, jc-42): "some tileset loaded" is not "YOUR tileset loaded".
     *
     * loadimages() is a fallback chain, so a chosen tileset that fails still leaves
     * it returning TRUE off the rc tiers. Reporting that as success let the caller
     * write a name that never loaded into tw_settings.ini -- the default tiles on
     * screen, no warning, and a menu checkmark pointing at something the user was
     * not looking at, permanently and on both PCs.
     *
     * Startup must keep tolerating the fallback, or a stale name would stop the game
     * from launching. Only this reload path, which exists to serve a deliberate pick,
     * insists on getting what it asked for. */
    sel = gettilesetoverride(currentruleset);
    if (sel && *sel && !overrideloaded)
	return FALSE;
    return TRUE;
}

/* Load the font resource.
 */
static int loadfont(void)
{
    char       *path;
    int		f;

    f = FALSE;
    path = getpathbuffer();
    if (*resources[RES_IMG_FONT].str) {
	combinepath(path, resdir, resources[RES_IMG_FONT].str);
	f = loadfontfromfile(path, TRUE);
    }
    if (!f && resources != globalresources
	   && *globalresources[RES_IMG_FONT].str) {
	combinepath(path, resdir, globalresources[RES_IMG_FONT].str);
	f = loadfontfromfile(path, TRUE);
    }
    free(path);

    if (!f)
	errmsg(resdir, "no valid font found");
    return f;
}

/* Load the list of unsolvable levels.
 */
typedef int (*txtloader)(char const * fname);
static int loadtxtresource(int resid, txtloader loadfunc)
{
    char const *filename;

    if (*resources[resid].str)
	filename = resources[resid].str;
    else if (resources != globalresources
			&& *globalresources[resid].str)
	filename = globalresources[resid].str;
    else
	return FALSE;

    return loadfunc(filename);
}

/*
 * MOD (Jeremy, jc-59): user-selectable sound packs. See res.h.
 */

/* The settings key naming each ruleset's chosen sound pack. Designated
 * initializers for the same reason as tilesetkey: the enum puts Lynx first.
 */
static char const *const soundpackkey[Ruleset_Count] = {
    [Ruleset_None] = NULL,
    [Ruleset_Lynx] = "lynxsoundpack",
    [Ruleset_MS]   = "mssoundpack"
};

/* Every sound's name as the rc file spells it. A pack supplies a sound either
 * as <name>.wav or through a <name>= line in its sounds.txt.
 *
 * 🔴 THIS IS A SECOND COPY OF rclist[]'s SOUND ROWS, AND IT IS CHECKED, NOT
 * TRUSTED. rclist[] holds the names lowercased, and they cannot simply be
 * derived from it: a pack author writes "PickupChipSound.wav", and on a
 * case-sensitive filesystem only the capitalized spelling names that file.
 * test/res_test.c asserts that every row here lowercases to its rclist[] row,
 * that no row is missing, and that the shipped how-to file names every one --
 * CLAUDE.md §8.1's rule about two tables that must agree.
 */
static char const *const soundname[SND_COUNT] = {
    [SND_CHIP_LOSES]		= "ChipDeathSound",
    [SND_CHIP_WINS]		= "LevelCompleteSound",
    [SND_TIME_OUT]		= "ChipDeathByTimeSound",
    [SND_TIME_LOW]		= "TickSound",
    [SND_DEREZZ]		= "DerezzSound",
    [SND_CANT_MOVE]		= "BlockedMoveSound",
    [SND_IC_COLLECTED]		= "PickupChipSound",
    [SND_ITEM_COLLECTED]	= "PickupToolSound",
    [SND_BOOTS_STOLEN]		= "ThiefSound",
    [SND_TELEPORTING]		= "TeleportSound",
    [SND_DOOR_OPENED]		= "OpenDoorSound",
    [SND_SOCKET_OPENED]		= "SocketSound",
    [SND_BUTTON_PUSHED]		= "SwitchSound",
    [SND_TILE_EMPTIED]		= "TileEmptiedSound",
    [SND_WALL_CREATED]		= "WallCreatedSound",
    [SND_TRAP_ENTERED]		= "TrapEnteredSound",
    [SND_BOMB_EXPLODES]		= "BombSound",
    [SND_WATER_SPLASH]		= "SplashSound",
    [SND_BLOCK_MOVING]		= "BlockMovingSound",
    [SND_SKATING_FORWARD]	= "SkatingForwardSound",
    [SND_SKATING_TURN]		= "SkatingTurnSound",
    [SND_SLIDING]		= "SlidingSound",
    [SND_SLIDEWALKING]		= "SlideWalkingSound",
    [SND_ICEWALKING]		= "IceWalkingSound",
    [SND_WATERWALKING]		= "WaterWalkingSound",
    [SND_FIREWALKING]		= "FireWalkingSound"
};

/* TRUE when at least one sound now loaded came from the chosen pack. The jc-42
 * lesson again: the pack sits on top of a fallback chain, so "sounds loaded" is
 * not "YOUR pack loaded", and only this flag can tell reloadsounds() which.
 */
static int packloaded = FALSE;

/* What went wrong in the chosen pack, for the menu to show its author.
 * warn() reaches nobody in the GUI build, so this list is the only channel.
 * Bounded: a pack is at most one mapping per sound plus its bad lines, and
 * anything past the bound is dropped rather than grown.
 */
#define	MAXSOUNDPROBLEMS	32

typedef struct soundproblem {
    int		kind;		/* SOUNDPROBLEM_* */
    int		line;		/* sounds.txt line, or 0 */
    char	text[128];	/* the file or name at fault, truncated */
} soundproblem;

static soundproblem	soundproblems[MAXSOUNDPROBLEMS];
static int		soundproblemcount = 0;
/* How many problems did not fit. The menu says "and N more" rather than let a
 * truncated list pass for a complete one. */
static int		soundproblemsdropped = 0;

/* A pack folder as the loader sees it: its path, what is in it, and which
 * file sounds.txt assigned to each sound.
 */
#define	MAXPACKFILES	1024

typedef struct soundpack {
    char       *dir;			/* NULL when no usable pack is chosen */
    char      **files;			/* the folder's entries, spelled as on disk */
    int		filecount;
    int		overflow;		/* TRUE if the folder held more than MAXPACKFILES */
    char       *used;			/* per file: TRUE if anything in the pack names it */
    int		mapped[SND_COUNT];	/* index into files, or -1 */
} soundpack;

static void addsoundproblem(int kind, int line, char const *text)
{
    soundproblem       *p;
    int			i;

    /* The same bad file mapped to three sounds is one problem, not three. */
    for (i = 0 ; i < soundproblemcount ; ++i)
	if (soundproblems[i].kind == kind && soundproblems[i].line == line
				&& !strncmp(soundproblems[i].text, text,
					    sizeof soundproblems[i].text - 1))
	    return;
    if (soundproblemcount >= MAXSOUNDPROBLEMS) {
	++soundproblemsdropped;
	return;
    }
    p = &soundproblems[soundproblemcount++];
    p->kind = kind;
    p->line = line;
    snprintf(p->text, sizeof p->text, "%s", text);
}

/* strcmp, ignoring ASCII case. Pack names are ASCII by the time they get here
 * (see issoundpackname), and a pack must behave the same whether the folder
 * was written on Windows or unpacked on a case-sensitive filesystem.
 */
static int asciicasecmp(char const *a, char const *b)
{
    int	ca, cb;

    for (;;) {
	/* Folded by hand rather than with tolower(), whose answer depends on
	 * the locale: under a Turkish one 'I' does not lower to 'i', and a pack
	 * would stop matching its own file names. */
	ca = (unsigned char)*a++;
	cb = (unsigned char)*b++;
	if (ca >= 'A' && ca <= 'Z')
	    ca += 'a' - 'A';
	if (cb >= 'A' && cb <= 'Z')
	    cb += 'a' - 'A';
	if (ca != cb || !ca)
	    return ca - cb;
    }
}

/* TRUE if name is safe as a pack folder name or as a file inside a pack.
 *
 * Everything istilesetname() refuses, plus two more:
 *
 * - ".", which would make the sounds folder itself a "pack".
 * - Any byte outside ASCII. The paths built here are in the local 8-bit code
 *   page, while SDL opens WAV files by name as UTF-8 on Windows, so a folder
 *   called "Café" would list in the menu, read its sounds.txt, and then fail
 *   on every single sound. Refusing it up front keeps the menu honest: it only
 *   offers what the loader can load. (istilesetname() is left alone -- tiles
 *   go through QImage, which takes the local code page, and fuzz_rc.c pins
 *   that predicate's rule exactly.)
 * - Any of * ? " < > |, which Windows never allows in a file name. They matter
 *   because Qt turns a character the local code page cannot hold into "?": a
 *   folder named in Japanese reaches this function as "??", passed every rule
 *   above, and would have been offered in the menu as a pack that cannot load.
 */
static int issoundpackname(char const *name)
{
    char const *p;

    if (!istilesetname(name) || !strcmp(name, ".") || strpbrk(name, "*?\"<>|"))
	return FALSE;
    for (p = name ; *p ; ++p)
	if ((unsigned char)*p >= 0x80)
	    return FALSE;
    return TRUE;
}

int getsoundpackpath(char *dest, char const *name)
{
    if (!combinepath(dest, resdir, SOUNDPACKDIR)) {
	dest[0] = '\0';
	return FALSE;
    }
    if (!name)
	return TRUE;
    if (!issoundpackname(name) || !combinepath(dest, dest, name)) {
	dest[0] = '\0';
	return FALSE;
    }
    return TRUE;
}

char const *getsoundpackoverride(int ruleset)
{
    return getoverride(soundpackkey, ruleset);
}

void setsoundpackoverride(int ruleset, char const *name)
{
    setoverride(soundpackkey, ruleset, name);
}

int getsoundpackproblemcount(void)
{
    return soundproblemcount;
}

int getsoundpackproblemsdropped(void)
{
    return soundproblemsdropped;
}

int getsoundpackproblem(int index, int *line, char const **text)
{
    if (index < 0 || index >= soundproblemcount)
	return -1;
    if (line)
	*line = soundproblems[index].line;
    if (text)
	*text = soundproblems[index].text;
    return soundproblems[index].kind;
}

/* findfiles() callback: keep every entry of the pack folder. */
static int collectpackfile(char const *name, void *data)
{
    soundpack  *pack = data;

    if (pack->filecount >= MAXPACKFILES) {
	pack->overflow = TRUE;
	return -1;
    }
    x_alloc(pack->files, (pack->filecount + 1) * sizeof *pack->files);
    pack->files[pack->filecount++] = (char*)name;
    return 1;	/* findfiles() hands the string over to us */
}

/* TRUE if name ends in an extension a sound file would have. Only used to
 * decide what is worth mentioning when nothing refers to it: a pack folder may
 * also hold a readme, a picture, a license.
 */
static int isaudiofilename(char const *name)
{
    static char const *const exts[] = {
	".wav", ".wave", ".mp3", ".ogg", ".oga", ".opus", ".flac",
	".aif", ".aiff", ".aifc", ".wma", ".m4a", ".mid", ".midi"
    };
    size_t	n, e, i;

    n = strlen(name);
    for (i = 0 ; i < sizeof exts / sizeof *exts ; ++i) {
	e = strlen(exts[i]);
	if (n > e && !asciicasecmp(name + n - e, exts[i]))
	    return TRUE;
    }
    return FALSE;
}

/* The index of the pack file whose name matches, ignoring case, or -1. */
static int findpackfile(soundpack const *pack, char const *name)
{
    int	i;

    for (i = 0 ; i < pack->filecount ; ++i)
	if (!asciicasecmp(pack->files[i], name))
	    return i;
    return -1;
}

/* Read the pack's sounds.txt into pack->mapped.
 *
 * Deliberately NOT readrcfile(): that parser is bound to the rc file, accepts
 * every non-sound resource name, and its "%s" cannot hold a filename with a
 * space in it -- which pack authors will certainly write. The syntax matches
 * tw_settings.ini instead: Name=value, whitespace around both ignored,
 * whole-line comments starting with # or ;, a later line for the same sound
 * winning over an earlier one. A blank value maps nothing, so the fixed name
 * still applies.
 */
static void readsoundmap(soundpack *pack, int mapfile)
{
    fileinfo	file = {0};
    char	buf[512];
    char       *p, *end, *eq, *key, *val;
    int		lineno, len, n, f;

    if (!openfileindir(&file, pack->dir, pack->files[mapfile], "r", NULL)) {
	addsoundproblem(SOUNDPROBLEM_UNREADABLE, 0, pack->files[mapfile]);
	return;
    }
    for (lineno = 1 ; ; ++lineno) {
	len = sizeof buf - 1;
	if (!filegetline(&file, buf, &len, NULL))
	    break;
	/* filegetline() leaves len at strlen - 1 for a whole line and at
	 * strlen when it had to cut the line short and discard the rest. A
	 * truncated line is refused outright: half a filename is a filename
	 * that does not exist, and saying which LINE is more use to an author.
	 * (A final line with no newline that fills the buffer EXACTLY looks
	 * the same and is refused too -- 510 characters, far past any real
	 * mapping, so the two are not worth telling apart.) */
	if (len == (int)strlen(buf)) {
	    addsoundproblem(SOUNDPROBLEM_BADLINE, lineno, "");
	    continue;
	}
	p = buf;
	/* A byte-order mark, which Notepad's "UTF-8 with BOM" and PowerShell
	 * 5.1's Out-File both write. Without this skip the first line's name
	 * reads as three garbage bytes plus the name, and its sound is lost
	 * with a report calling a correctly spelled name unknown. settings.cpp
	 * strips one from tw_settings.ini for the same reason. */
	if (lineno == 1 && !strncmp(p, "\xEF\xBB\xBF", 3))
	    p += 3;
	for ( ; isspace((unsigned char)*p) ; ++p) ;
	end = p + strlen(p);
	while (end > p && isspace((unsigned char)end[-1]))
	    *--end = '\0';
	if (!*p || *p == '#' || *p == ';')
	    continue;
	if (!(eq = strchr(p, '='))) {
	    addsoundproblem(SOUNDPROBLEM_BADLINE, lineno, p);
	    continue;
	}
	key = p;
	for (end = eq ; end > key && isspace((unsigned char)end[-1]) ; --end) ;
	*end = '\0';
	for (val = eq + 1 ; isspace((unsigned char)*val) ; ++val) ;

	for (n = 0 ; n < SND_COUNT ; ++n)
	    if (soundname[n] && !asciicasecmp(key, soundname[n]))
		break;
	if (n == SND_COUNT) {
	    addsoundproblem(SOUNDPROBLEM_UNKNOWNSOUND, lineno, key);
	    continue;
	}
	pack->mapped[n] = -1;
	if (!*val)
	    continue;
	if (!issoundpackname(val)) {
	    addsoundproblem(SOUNDPROBLEM_BADFILENAME, lineno, val);
	    continue;
	}
	if ((f = findpackfile(pack, val)) < 0) {
	    addsoundproblem(SOUNDPROBLEM_MISSING, lineno, val);
	    continue;
	}
	pack->mapped[n] = f;
	pack->used[f] = 1;	/* even if a later line overrides it: it is named */
    }
    fileclose(&file, NULL);
}

/* Release what opensoundpack() gathered. Safe on a pack it never filled. */
static void closesoundpack(soundpack *pack)
{
    int	i;

    for (i = 0 ; i < pack->filecount ; ++i)
	free(pack->files[i]);
    free(pack->files);
    free(pack->used);
    free(pack->dir);
    pack->files = NULL;
    pack->used = NULL;
    pack->dir = NULL;
    pack->filecount = 0;
}

/* Gather the chosen pack for the ruleset in play. FALSE -- with the pack left
 * empty -- when none is chosen or the choice cannot be a pack at all.
 */
static int opensoundpack(soundpack *pack)
{
    char const *sel;
    int		n, m;

    pack->dir = NULL;
    pack->files = NULL;
    pack->filecount = 0;
    pack->overflow = FALSE;
    pack->used = NULL;
    for (n = 0 ; n < SND_COUNT ; ++n)
	pack->mapped[n] = -1;

    sel = getoverride(soundpackkey, currentruleset);
    if (!sel || !*sel)
	return FALSE;
    pack->dir = getpathbuffer();
    if (!getsoundpackpath(pack->dir, sel)) {
	closesoundpack(pack);
	return FALSE;
    }
    if (!findfiles(pack->dir, pack, collectpackfile)) {
	/* Deleted, renamed or unreadable since the menu listed it. Said out
	 * loud, or the author is told to go and add sounds to a folder that is
	 * not there. */
	addsoundproblem(SOUNDPROBLEM_FOLDER, 0, sel);
	closesoundpack(pack);
	return FALSE;
    }
    if (pack->overflow)
	addsoundproblem(SOUNDPROBLEM_TOOMANYFILES, 0, "");
    if (!(pack->used = calloc(pack->filecount ? pack->filecount : 1, 1)))
	memerrexit();
    if ((m = findpackfile(pack, SOUNDPACKMAP)) >= 0) {
	pack->used[m] = 1;
	readsoundmap(pack, m);
    }
    return TRUE;
}

/* MOD (Jeremy, jc-59): report every sound-like file NOTHING in the pack refers
 * to. Called AFTER the load, so that files which failed to load are listed
 * first: a folder of stray .mp3s must not push the real failures out of a
 * bounded problem list.
 */
static void reportunused(soundpack *pack)
{
    char	fixed[64];
    int		n, f;

    /* A sound the pack leaves out is not a problem, but a file
     * called ChipDeath.wav or PickupChipSound.mp3 almost certainly is one: the
     * author meant it to be used, and without this it would be ignored in
     * silence, the default would play, and the menu would show the pack as
     * working. "Refers to" means named on ANY sounds.txt line (readsoundmap()
     * marks those, including one a later line overrides -- the message would
     * otherwise say sounds.txt does not mention a file it plainly does) or
     * matching a sound's fixed name: a fixed-name file that sounds.txt
     * overrides is still a deliberate file, not a typo. */
    for (n = 0 ; n < SND_COUNT ; ++n) {
	if (!soundname[n])
	    continue;
	snprintf(fixed, sizeof fixed, "%s.wav", soundname[n]);
	if ((f = findpackfile(pack, fixed)) >= 0)
	    pack->used[f] = 1;
    }
    for (f = 0 ; f < pack->filecount ; ++f)
	if (!pack->used[f] && isaudiofilename(pack->files[f]))
	    addsoundproblem(SOUNDPROBLEM_UNUSED, 0, pack->files[f]);
}

/* Load one pack file into slot n. A file that is in the folder and still will
 * not load is an author's problem worth reporting; path is scratch space.
 *
 * Only a REGULAR file is handed to the decoder. A folder can hold other things
 * under a sound's name -- a subfolder, or, unpacked from an archive on Linux, a
 * named pipe, which SDL would sit reading forever on the thread that draws the
 * game.
 */
static int trypackfile(soundpack const *pack, int n, int f, char *path)
{
    struct stat	st;

    if (combinepath(path, pack->dir, pack->files[f])
			&& !stat(path, &st) && S_ISREG(st.st_mode)
			&& loadsfxfromfile(n, path))
	return TRUE;
    addsoundproblem(SOUNDPROBLEM_UNREADABLE, 0, pack->files[f]);
    return FALSE;
}

/* The pack's sound for slot n: its sounds.txt entry, then <name>.wav. */
static int loadpacksound(soundpack const *pack, int n, char *path)
{
    char	fixed[64];
    int		f;

    if (!soundname[n])
	return FALSE;
    if (pack->mapped[n] >= 0 && trypackfile(pack, n, pack->mapped[n], path))
	return TRUE;
    snprintf(fixed, sizeof fixed, "%s.wav", soundname[n]);
    f = findpackfile(pack, fixed);
    if (f >= 0 && f != pack->mapped[n] && trypackfile(pack, n, f, path))
	return TRUE;
    return FALSE;
}

/* Load all of the sound resources.
 *
 * MOD (Jeremy, jc-59): three changes.
 *
 * 1. The chosen sound pack is tried FIRST, per sound, over the rc file's
 *    names -- the same place the chosen tileset sits in loadimages().
 *
 * 2. A slot nothing loads for is now FREED. It used to keep whatever it held
 *    before: inaudible with the stock rc file (no ruleset switch leaves a slot
 *    the new engine plays unfilled), but with packs, pack A's sound would have
 *    outlived switching to pack B. Sound never reaches the engine, so this
 *    cannot touch a replay.
 *
 * 3. Returns -1, touching NO slot, when there is no audio at all -- started
 *    silent, or no device opens. loadsfxfromfile() fails for those exactly as
 *    it fails for a missing file, and without this every working sound would
 *    have been freed because of the DEVICE, and the user told their pack was
 *    at fault. setaudiosystem(TRUE) is what loadsfxfromfile() already calls
 *    first; asking once here only moves that question to where it can be
 *    answered for the whole pass.
 *
 * Also: combinepath()'s result is checked on every tier now. It leaves the
 * buffer stale when a path is too long (see loadimages()), which was harmless
 * while every name was a short literal in rc and is not with a user-named
 * pack in the path -- a stale buffer would load the PREVIOUS sound's file.
 */
static int loadsounds(void)
{
    soundpack	pack;
    char       *path;
    int		count, havepack;
    int		n, f;

    soundproblemcount = 0;
    soundproblemsdropped = 0;
    packloaded = FALSE;
    if (!setaudiosystem(TRUE))
	return -1;

    havepack = opensoundpack(&pack);
    path = getpathbuffer();
    count = 0;
    for (n = 0 ; n < SND_COUNT ; ++n) {
	f = FALSE;
	if (havepack && loadpacksound(&pack, n, path)) {
	    f = TRUE;
	    packloaded = TRUE;
	}
	if (!f && *resources[RES_SND_BASE + n].str
	       && combinepath(path, resdir, resources[RES_SND_BASE + n].str))
	    f = loadsfxfromfile(n, path);
	if (!f && resources != globalresources
	       && *globalresources[RES_SND_BASE + n].str
	       && combinepath(path, resdir, globalresources[RES_SND_BASE + n].str))
	    f = loadsfxfromfile(n, path);
	if (f)
	    ++count;
	else
	    freesfx(n);
    }
    free(path);
    if (havepack)
	reportunused(&pack);
    closesoundpack(&pack);
    return count;
}

/* MOD (Jeremy, jc-59): reload just the sounds for the ruleset in play,
 * picking up a changed sound pack. Not loadgameresources(), for the reason
 * reloadtileset() gives: its caller treats failure as fatal.
 */
int reloadsounds(void)
{
    char const *sel;
    int		n;

    if (currentruleset == Ruleset_None)
	return SOUNDPACK_FAILED;
    n = loadsounds();
    if (n < 0)
	return SOUNDPACK_NOAUDIO;
    /* The same rule loadgameresources() applies, so the two paths agree. */
    if (n == 0)
	setaudiosystem(FALSE);
    /* "Some sounds loaded" is not "the chosen pack loaded" -- see packloaded. */
    sel = getoverride(soundpackkey, currentruleset);
    if (sel && *sel && !packloaded)
	return SOUNDPACK_FAILED;
    return SOUNDPACK_OK;
}

/* Load all resources that are available. FALSE is returned if the
 * tile images could not be loaded. (Sounds are not required in order
 * to run, and by this point we should already have a valid font and
 * color scheme set.)
 */
int loadgameresources(int ruleset)
{
    currentruleset = ruleset;
    resources = allresources[ruleset];
    loadcolors();
    loadfont();
    if (!loadimages())
	return FALSE;
    /* MOD (Jeremy, jc-59): -1 is "no audio at all" (see loadsounds), which
     * closes the device exactly as zero sounds always has. */
    if (loadsounds() <= 0)
	setaudiosystem(FALSE);
    return TRUE;
}

/* Parse the rc file and load the font and color scheme. FALSE is returned
 * if an error occurs.
 */
int initresources(void)
{
    initresourcedefaults();
    resources = allresources[Ruleset_None];
    if (!readrcfile() || !loadcolors() || !loadfont())
	return FALSE;
    loadtxtresource(RES_TXT_UNSLIST, loadunslistfromfile);
    loadtxtresource(RES_TXT_MESSAGE, loadmessagesfromfile);
    return TRUE;
}

/* Free all resources.
 */
void freeallresources(void)
{
    int	n;
    freefont();
    freetileset();
    clearunslist();
    for (n = 0 ; n < SND_COUNT ; ++n)
	 freesfx(n);
}
