/* dirinput.c: Arbitrating between direction keys held at the same time.
 *
 * MOD (Jeremy, jc-43). Split out of in.c so that it can be compiled and
 * exercised on its own: everything here is pure, reading nothing but its
 * arguments -- no keystates[], no geng, no Qt. test/dirinput_test.c compiles
 * this file directly against the real defs.h, so the test sees the same
 * CmdNorth/CmdSouth/CmdWest/CmdEast values the game does rather than a
 * reimplementation of them.
 *
 * This is #included by in.c rather than being a translation unit of its own,
 * following the existing generic/_in.cpp idiom, so nothing here gains external
 * linkage and neither CMake target needs to know it exists. in.c is compiled as
 * C++ by the Qt build and as C by the SDL build, so this file must stay valid
 * in both languages; the test compiling it as C is what keeps that honest.
 *
 * Copyright (C) 2001-2010 by Brian Raiter and Madhav Shanbhag,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#include	"../defs.h"

/* Direction keys are arbitrated one AXIS at a time.
 *
 * This replaces the old mergeable[] table, which answered a different question
 * -- "may these two commands combine?" -- and was being used to answer this
 * one. A player has at most one meaningful vertical and one meaningful
 * horizontal direction down at any moment, and a Lynx diagonal is exactly one
 * of each, so the axis is the natural unit.
 */
enum {
    AXIS_NONE = -1,
    AXIS_VERT = 0,
    AXIS_HORZ = 1,
    AXIS_count = 2
};

/* The direction keys that survived one polling cycle, per axis. Held and
 * struck are tracked separately because they are promoted under different
 * rules; see resolvedirections().
 */
typedef struct dirsurvivors {
    int held[AXIS_count]; /* best held-or-pressed command, or 0 */
    unsigned heldorder[AXIS_count]; /* when that key was pressed */
    int struck[AXIS_count]; /* best struck command, or 0 */
    unsigned struckorder[AXIS_count]; /* when that key was pressed */
    int suppressed[AXIS_count]; /* best held-but-muted command, or 0 */
    unsigned suppressedorder[AXIS_count]; /* when that key was pressed */
} dirsurvivors;

/* Return the axis a command occupies, or AXIS_NONE if it is not a single
 * direction. Switches on the four literal commands on purpose: defs.h's
 * directionalcmd() also answers TRUE for CmdNone and for diagonals, so it
 * would quietly misclassify any table entry carrying one.
 */
static int commandaxis(int cmd) {
    switch (cmd) {
        case CmdNorth:
        case CmdSouth:
            return AXIS_VERT;
        case CmdWest:
        case CmdEast:
            return AXIS_HORZ;
    }
    return AXIS_NONE;
}

/* Reduce one polling cycle's surviving direction keys to a single command,
 * returning 0 if no direction is active.
 *
 * A struck key (pressed AND released inside one cycle) is promoted only when
 * its own axis is empty and the other axis is held. That is the sole case the
 * struck clause exists for: completing a diagonal, so that tapping a
 * perpendicular direction while running still block-slaps instead of being
 * discarded. Two consequences are deliberate. A struck key never displaces a
 * still-held key on its own axis, so tapping South while holding North cannot
 * make Chip flicker backwards for a move. And a lone struck arrow is left
 * alone, to be picked up by input()'s ordinary KS_STRUCK fallback -- promoting
 * it would let it outrank a virtual menu command arriving in the same cycle,
 * since those reach us as KS_STRUCK too (see PulseKey() in the Qt layer).
 */
static int resolvedirections(dirsurvivors const* s, int joystick) {
    int cmds[AXIS_count];
    unsigned orders[AXIS_count];
    int muted[AXIS_count];
    int axis, other;

    /* Step 1. Each axis is claimed by its most recently pressed key, whether or
     * not that key's command may actually be emitted this cycle.
     *
     * Claiming the axis for a SUPPRESSED key is the whole point. Under keyboard
     * behavior a freshly pressed key spends a cycle or two in KS_DOWNBUTOFF1/2
     * -- physically down, deliberately muted, so that one tap yields one move.
     * If such a key simply vanished from the contest, the axis would fall back
     * to the older key still held on it, and the player would get one move in
     * the direction they were trying to leave. Table order never noticed this,
     * because it does not care whether a key is muted; recency does.
     */
    for (axis = 0; axis < AXIS_count; ++axis) {
        cmds[axis] = s->held[axis];
        orders[axis] = s->heldorder[axis];
        muted[axis] = FALSE;
        if (s->suppressed[axis]
            && (!cmds[axis] || s->suppressedorder[axis] > orders[axis])) {
            cmds[axis] = s->suppressed[axis];
            orders[axis] = s->suppressedorder[axis];
            muted[axis] = TRUE;
        }
    }

    /* Step 2. A struck key fills an axis that nothing else claims. Tested
     * against held[other], not cmds[other], so that the two axes cannot
     * promote off each other and the loop order cannot matter. (That guard is
     * defense in depth today -- promotion requires held[other] to be set,
     * which makes the other axis's own !cmds test false -- and becomes load
     * bearing the moment the !cmds test is ever weakened.)
     *
     * The joystick test is deliberately repeated here even though input()
     * already declines to record struck keys under keyboard behavior. It
     * belongs with the rule it enforces -- promoting a struck key exists only
     * to complete a diagonal, and keyboard behavior has no diagonals -- rather
     * than surviving as an invariant the caller has to remember.
     */
    for (axis = 0; axis < AXIS_count; ++axis) {
        other = axis == AXIS_VERT ? AXIS_HORZ : AXIS_VERT;
        if (joystick && !cmds[axis] && s->struck[axis] && s->held[other]) {
            cmds[axis] = s->struck[axis];
            /* Written to keep the arrays coherent; the joystick path below
             * returns before orders[] is ever consulted. */
            orders[axis] = s->struckorder[axis];
        }
    }

    /* Step 3. Emit -- unless the key that won is a muted one, in which case
     * this cycle yields NO direction at all. It must not fall through to the
     * runner-up: input() then reaches its lingerflag and returns CmdPreserve,
     * which tells doturn() to leave currentinput alone, so the command from
     * the previous cycle stays live for exactly as long as the mute lasts.
     * That is what CmdPreserve has always been for.
     */
    if (cmds[AXIS_VERT] && cmds[AXIS_HORZ]) {
        if (joystick) {
            if (muted[AXIS_VERT] || muted[AXIS_HORZ])
                return 0;
            return cmds[AXIS_VERT] | cmds[AXIS_HORZ]; /* the Lynx diagonal */
        }
        /* Keyboard behavior has no diagonals, so one key has to win. The
         * stamps cannot tie: input() stamps every key on the transition into
         * a held state, from a counter that hands out a distinct nonzero value
         * per press, and never reads a stamp for a key that is not down.
         */
        if (orders[AXIS_VERT] > orders[AXIS_HORZ])
            return muted[AXIS_VERT] ? 0 : cmds[AXIS_VERT];
        return muted[AXIS_HORZ] ? 0 : cmds[AXIS_HORZ];
    }
    if (cmds[AXIS_VERT])
        return muted[AXIS_VERT] ? 0 : cmds[AXIS_VERT];
    return muted[AXIS_HORZ] ? 0 : cmds[AXIS_HORZ];
}
