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

#ifdef __cplusplus
}
#endif

#endif
