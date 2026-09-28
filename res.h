/* res.h: Functions for loading resources from external files.
 *
 * Copyright (C) 2001-2006 by Brian Raiter, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#ifndef	HEADER_res_h_
#define HEADER_res_h_

/* MOD (Jeremy, jc-41): C linkage, because the Qt layer now includes this header for the
 * tileset menu. Everything declared here is compiled as C in res.c; without the guard the
 * C++ translation unit would look for mangled names and fail to link. Matches fileio.h,
 * settings.h and oshw.h, all of which are included from C++ already.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* The directory containing all the resource files.
 */
extern char	       *resdir;

/* Parse the rc file and initialize the resources that are needed at
 * the start of the program (i.e., the font and color settings).
 * FALSE is returned if the rc file contained errors or if a resource
 * could not be loaded.
 */
extern int initresources(void);

/* Load all resources, using the settings for the given ruleset. FALSE
 * is returned if any critical resources could not be loaded.
 */
extern int loadgameresources(int ruleset);

/* Release all memory allocated for the resources.
 */
extern void freeallresources(void);

/* MOD (Jeremy, jc-41): the user-selectable tileset.
 *
 * A tileset chosen here overrides the rc file's TileImages for one ruleset. It
 * names a file in the tileset directory (see gettilesetpath) and is remembered
 * in tw_settings.ini. Everything below treats a bad value as no value: blank,
 * absent, unsafe, missing or unloadable all fall back to the rc file's tiles,
 * which is the behavior every build before this one had.
 */

/* The subdirectory of resdir holding user-selectable tilesets.
 */
#define	TILESETDIR	"tilesets"

/* Build the path to a tileset file, or to the tileset directory itself when
 * name is NULL. dest must be a buffer of getpathbufferlen() bytes. FALSE is
 * returned -- and dest left untouched -- if name is unsafe (see istilesetname)
 * or the path would be too long.
 *
 * This is the ONE definition of where tilesets live. The menu enumerates the
 * directory this returns; loadimages() loads out of it. If those two ever
 * disagreed the menu would list files the loader could not find, and the only
 * symptom would be a selection that silently does nothing.
 */
extern int gettilesetpath(char *dest, char const *name);

/* The ruleset whose tiles are currently loaded, or Ruleset_None before any
 * game has been started.
 *
 * NOT the same thing as the MS/Lynx radio button: a .dac file can force a
 * ruleset (see series.c), so the button and the ruleset in play can disagree.
 * Callers that mean "the ruleset on screen right now" want this.
 */
extern int getcurrentruleset(void);

/* The tileset chosen for a ruleset, or NULL if none is set. The returned
 * string is owned by the settings table; copy it before setting anything.
 */
extern char const *gettilesetoverride(int ruleset);

/* Choose the tileset for a ruleset. name may be NULL or "" to clear it and
 * return to the rc file's tiles. This only records the choice -- call
 * reloadtileset() to make it visible, and only write the settings file once
 * that has succeeded.
 */
extern void settilesetoverride(int ruleset, char const *name);

/* Re-resolve and reload the tiles for the ruleset already in play, picking up
 * a changed tileset override. FALSE is returned if no usable tileset could be
 * loaded at all, in which case the caller must NOT build a game display: the
 * tile size would be zero and the map-position arithmetic divides by it.
 *
 * Deliberately not loadgameresources(), which would also re-read every sound
 * file from disk mid-level and can switch audio off, and whose only caller
 * treats failure as fatal. A failed tileset pick must never kill the program.
 */
extern int reloadtileset(void);

/* MOD (Jeremy, jc-59): user-selectable sound packs.
 *
 * A sound pack is a FOLDER in the sound pack directory. For each of the game's
 * sounds it may supply a file named after the sound as the rc file spells it
 * (PickupChipSound.wav), or name any file in the folder from a line in an
 * optional sounds.txt (PickupChipSound=Bell.wav). A sound the pack does not
 * supply plays the rc file's sound, exactly as before packs existed. The choice
 * is made per ruleset, like the tileset, and remembered in tw_settings.ini.
 */

/* The subdirectory of resdir holding sound packs, and each pack's optional map.
 */
#define	SOUNDPACKDIR	"sounds"
#define	SOUNDPACKMAP	"sounds.txt"

/* Build the path to a sound pack folder, or to the sound pack directory itself
 * when name is NULL. Same contract as gettilesetpath(): FALSE, with dest
 * cleared, when name is unsafe or the path too long. The ONE definition of
 * which names are packs -- the menu filters through it, the loader loads
 * through it. Stricter than a tileset name: ASCII only, and not ".".
 */
extern int getsoundpackpath(char *dest, char const *name);

/* The sound pack chosen for a ruleset, or NULL if none is set. Owned by the
 * settings table; copy it before setting anything.
 */
extern char const *getsoundpackoverride(int ruleset);

/* Choose the sound pack for a ruleset; NULL or "" returns to the rc file's
 * sounds. Records the choice only -- call reloadsounds() to hear it, and write
 * the settings file only once that has succeeded.
 */
extern void setsoundpackoverride(int ruleset, char const *name);

/* What reloadsounds() returns.
 */
#define	SOUNDPACK_OK		0	/* loaded; the chosen pack supplied >= 1 sound */
#define	SOUNDPACK_FAILED	1	/* the chosen pack supplied nothing */
#define	SOUNDPACK_NOAUDIO	2	/* no audio at all; NOTHING was changed */

/* Re-resolve and reload every sound for the ruleset already in play, picking
 * up a changed sound pack. Never fatal: on SOUNDPACK_FAILED the rc file's
 * sounds are loaded in the pack's place, and on SOUNDPACK_NOAUDIO -- started
 * silent, or no device -- the loaded sounds were not touched at all, so the
 * caller must not blame the pack.
 */
extern int reloadsounds(void);

/* What went wrong in the chosen pack during the last load, for its author:
 * files that are in the folder but will not load, sounds.txt lines that could
 * not be used, and sound files that nothing in the pack refers to (most likely
 * a misspelled name). A sound the pack simply does not supply is NOT a problem.
 */
#define	SOUNDPROBLEM_UNREADABLE		0	/* in the folder, will not load */
#define	SOUNDPROBLEM_MISSING		1	/* named in sounds.txt, not in the folder */
#define	SOUNDPROBLEM_UNKNOWNSOUND	2	/* sounds.txt names no such sound */
#define	SOUNDPROBLEM_BADFILENAME	3	/* sounds.txt value is not a plain filename */
#define	SOUNDPROBLEM_BADLINE		4	/* sounds.txt line with no '=', or too long */
#define	SOUNDPROBLEM_UNUSED		5	/* a sound-like file nothing refers to: a typo? */
#define	SOUNDPROBLEM_FOLDER		6	/* the pack's folder could not be read at all */
#define	SOUNDPROBLEM_TOOMANYFILES	7	/* more files in the folder than are read */

extern int getsoundpackproblemcount(void);

/* How many more problems there were than the list could hold (it keeps 32).
 * Approximate: past the bound, duplicates are not recognized. Nonzero is the
 * signal that matters. */
extern int getsoundpackproblemsdropped(void);

/* Returns the problem's SOUNDPROBLEM_* kind, or -1 for an index out of range.
 * *line gets the sounds.txt line (0 when not about a line) and *text the file
 * or name at fault; either pointer may be NULL.
 */
extern int getsoundpackproblem(int index, int *line, char const **text);

#ifdef __cplusplus
}
#endif

#endif
