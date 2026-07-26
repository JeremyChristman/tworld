/* encoding.h: Functions to read the level data.
 *
 * Copyright (C) 2001-2006 by Brian Raiter, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#ifndef	HEADER_encoding_h_
#define	HEADER_encoding_h_

#include	"state.h"

/* Initialize the gamestate by reading the level data from the setup.
 * FALSE is returned if the level data is invalid.
 */
extern int expandleveldata(gamestate *state);

/* MOD (Jeremy): translate a raw data-file tile code into the internal tile id,
 * returning Wall for codes the data-file format does not define. Used by the
 * MSCC row-32 cloner glitch, which writes raw MSCC bytes into the map.
 */
extern int fileidtotileid(int id);

/* Return the setup for a small level, created at runtime, that can be
 * displayed at the completion of a series.
 */
extern void getenddisplaysetup(gamestate *state);

#endif
