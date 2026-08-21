/* play.h: Functions to drive game-play and manage the game state.
 *
 * Copyright (C) 2001-2010 by Brian Raiter and Madhav Shanbhag,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#ifndef	HEADER_play_h_
#define	HEADER_play_h_

#include	"defs.h"

/* The different modes of the program with respect to gameplay.
 */
enum {
    NormalPlay, EndPlay,
    SuspendPlay, SuspendPlayShuttered,
    NonrenderPlay
};

/* TRUE if the program is running without a user interface.
 */
extern int batchmode;

#ifdef __cplusplus
extern "C"
{
#endif

/* Change the current gameplay mode. This affects the running of the
 * timer and the handling of the keyboard.
 */
extern void setgameplaymode(int mode);

/* Initialize the current state to the starting position of the
 * given level.
 */
extern int initgamestate(gamesetup* game, int ruleset);

/* Set up the current state to play from its prerecorded solution.
 * FALSE is returned if no solution is available for playback.
 */
extern int prepareplayback(void);

extern int setstepping(int stepping, int display);

extern int changestepping(int delta, int display);

extern void advanceinitrandomff(int display);

/* Get a string representing the stepping and (in Lynx mode) initial random
 * force floor direction. */
extern char const* getinitstatestring(void);

/*
 * MOD (Jeremy, jc-37): the death counter.
 *
 * A lifetime total, persisted in tw_settings.ini as "deathcount", displayed in the short-message
 * bar as "Deaths: N" when "showdeathcounter" is on. This module is the SINGLE OWNER of the number:
 * one place clamps it, one place persists it, one place notifies the display. Callers -- the game
 * loop in tworld.c and the Options menu in oshw-qt -- go through these three functions and never
 * touch setintsetting("deathcount") themselves.
 *
 * Modeled on setstepping()/changevolume(): mutate a value, persist it, poke the display.
 */

/* The largest total the counter will hold or accept. Nine digits: "Deaths: 999999999" is 17
 * characters, comfortably inside the bar, which already ships 19-character strings. Chosen over
 * INT_MAX because a readable round number beats squeezing in the last of the int range -- and the
 * counter saturates here rather than wrapping, so the ceiling is a resting place, not an error. */
#define DEATHCOUNT_MAX  999999999

/* TRUE when the death counter should be displayed and counted. FALSE if the user has not opted in,
 * or if the settings file exists but could not be read -- in the latter case the stored total is
 * unavailable and would otherwise be shown, wrongly, as 0. */
extern int deathcounteractive(void);

/* The current lifetime death total, clamped to [0, INT_MAX]. */
extern int getdeathcount(void);

/* Set the lifetime total outright (Options > Set Death Counter..., and Reset, which passes 0).
 * Values outside [0, INT_MAX] are clamped. Persists and updates the display. */
extern void setdeathcount(int count);

/* Add one death to the lifetime total, saturating at INT_MAX rather than wrapping. Does nothing
 * when the counter is not active. Persists and updates the display. */
extern void recorddeath(void);

/* Push the current total at the display. Called when the feature is switched on or off, so the bar
 * starts or stops showing it without waiting for the next death. */
extern void refreshdeathcount(void);

/* Return the amount of time passed in the current game, in seconds.
 */
extern int secondsplayed(void);

/* Handle one tick of the game. cmd is the current keyboard command
 * supplied by the user, or CmdPreserve if any pending command is to
 * be retained. The return value is positive if the game was completed
 * successfully, negative if the game ended unsuccessfully, and zero
 * if the game remains in progress.
 */
extern int doturn(int cmd);

/* Update the display during game play. If showframe is FALSE, then
 * nothing is actually displayed.
 */
extern int drawscreen(int showframe);

/* Quit game play early.
 */
extern int quitgamestate(void);

/* Free any resources associates with the current game state.
 */
extern int endgamestate(void);

/* Free all persistent resources in the module.
 */
extern void shutdowngamestate(void);

/* Initialize the current state to a small level used for display at
 * the completion of a series.
 */
extern void setenddisplay(void);

/* Return TRUE if a solution exists for the given level.
 */
extern int hassolution(gamesetup const* game);

/* Replace the user's solution with the just-executed solution if it
 * beats the existing solution for shortest time. FALSE is returned if
 * nothing was changed.
 */
extern int replacesolution(void);

/* Delete the user's best solution for the current game. FALSE is
 * returned if no solution was present to delete.
 */
extern int deletesolution(void);

/* Double-check the timing for a solution that has just been played
 * back. If the timing is incorrect, but the cause of the discrepancy
 * can be reasonably ascertained to be benign, the timings will be
 * corrected and the return value will be TRUE.
 */
extern int checksolution(void);

/* Turn pedantic mode on. The ruleset will be slightly changed to be
 * as faithful as possible to the original source material.
 */
extern void setpedanticmode(void);

/* Slow down the game clock by the given factor. Used for debugging
 * purposes.
 */
extern int setmudsuckingfactor(int mud);

/* Toggle whether to show stepping/initial random force floor direction
 * during solution playback.
 */
extern void toggleshowinitstate(void);

#ifdef __cplusplus
}
#endif

#endif
