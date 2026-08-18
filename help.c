/* help.c: Displaying online help.
 *
 * Copyright (C) 2001-2010 by Brian Raiter and Madhav Shanbhag,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#include	<stdlib.h>
#include	"defs.h"
#include	"err.h"
#include	"state.h"
#include	"oshw.h"
#include	"ver.h"
#include	"fork.h"
#include	"help.h"

#define COUNTOF(a)	(sizeof(a) / sizeof(*a))
#define	ARRAY(a)	a, COUNTOF(a)

/* Help for command-line options.
 */
static char const* yowzitch_items[] = {
    "1-Usage:", "1!tworld [-hvVdlsbtpqrPFa] [-n N] [-DLRS DIR] "
    "[NAME] [SNAME] [LEVEL]",
    "1-   -D", "1!Read data files from DIR instead of the default.",
    "1-   -L", "1!Read level sets from DIR instead of the default.",
    "1-   -R", "1!Read resource files from DIR instead of the default.",
    "1-   -S", "1!Save games and settings in DIR instead of the default.",
    "1-   -p", "1!Disable password checking.",
    "1-   -F", "1!Run in fullscreen mode.",
    "1-   -q", "1!Run quietly.",
    "1-   -r", "1!Run in read-only mode; solutions will not be saved.",
    "1-   -P", "1!Put Lynx ruleset emulation in pedantic mode.",
    "1-   -c", "1!Make repeat inputs more strict for better boosting behavior.",
    "1-   -n", "1!Set initial volume level to N.",
    "1-   -a", "1!Double the size of the sound buffer (can be repeated).",
    "1-   -l", "1!Display the list of available data files and exit.",
    "1-   -s", "1!Display scores for the selected data file and exit.",
    "1-   -t", "1!Display times for the selected data file and exit.",
    "1-   -b", "1!Batch-verify solutions for the selected data file and exit.",
    "1-   -h", "1!Display this help and exit.",
    "1-   -d", "1!Display default directories and exit.",
    "1-   -v", "1!Display version number and exit.",
    "1-   -V", "1!Display version and license information and exit.",
    "2!NAME specifies which data file to use.",
    "2!LEVEL specifies which level to start at.",
    "2!SNAME specifies an alternate solution file."
};
static tablespec const yowzitch_table = {23, 2, 2, -1, yowzitch_items};
tablespec const* yowzitch = &yowzitch_table;

/* Version and license information.
 *
 * MOD (Jeremy, jc-34): this is a FORK, and the About box now says so.
 *
 * What was wrong with the upstream text is not that it was inaccurate about Tile World -- it is
 * that it was accurate about the WRONG PROGRAM. It named only the original authors and sent bug
 * reports to their issue tracker, so anyone running this build who hit a problem caused by a change
 * made HERE would have reported it to people who never wrote it, cannot reproduce it, and would
 * have to spend their time working out that the binary is not theirs. Crediting them is right;
 * billing them for this fork's mistakes is not. Both halves are fixed below: they keep the credit,
 * and the fork takes the blame.
 *
 * The AI-assistance line is deliberate and is not to be trimmed as boilerplate. Jeremy asked for it
 * unprompted, on the grounds that letting a reader assume he wrote these changes unaided would be
 * dishonest. It stays.
 *
 * This table feeds BOTH the GUI's Help > About dialog (TileWorldMainWnd::ShowAbout, which reads
 * column 2 and strips the two formatting characters) and the console's -V output, so it is written
 * as plain text with no markup. ShowAbout() turns the bare URLs into clickable links at display
 * time rather than storing any HTML here, which would print as tag soup on a terminal.
 *
 * The "compiled <timestamp>" line upstream printed here is deliberately GONE (Jeremy's call): the
 * build tag on the line above identifies the build, and a timestamp told a user nothing they could
 * act on. COMPILE_TIME now has no consumer anywhere in the program, so comptime.h is no longer
 * included below -- CMakeLists still generates it, which is harmless and not worth disturbing the
 * upstream build plumbing over.
 *
 * ⚠ ROWS. tablespec's first field is a COUNT, and nothing checks it against the array: set it too
 * high and printtable() walks off the end of vourzhon_items. Adding or removing a paragraph means
 * changing the 7 below. */
static char const* vourzhon_items[] = {
    "1+*", "1-Tile World: version " VERSION " -- " FORK_AUTHOR "'s fork, "
    "build " FORK_BUILD_TAG,
    "1+*", "1!This is a personal fork of Tile World, maintained by "
    FORK_AUTHOR ". It corrects a number of cases where the MS ruleset did not"
    " reproduce the behavior of the original Chip's Challenge, and adds some"
    " display and convenience options. What each build changed is listed in"
    " the README included with this download, and at " FORK_REPO_URL ".",
    "1+*", "1!The changes in this fork were developed by " FORK_AUTHOR
    " together with Claude, Anthropic's AI coding assistant. Jeremy owned the"
    " vision, direction, and final decisions and the code was developed by"
    " Claude.",
    "1+*", "1!Tile World itself is Copyright (c) 2001-2025 by Brian Raiter,"
    " Madhav Shanbhag, Eric Schmidt, Michael Hansen, ChosenID, David Stolp,"
    " A Sickly Silver Moon, G lander, and Eevee. This fork is a set of changes"
    " on top of their work, and the credit for Tile World belongs to them."
    " They did not write these changes, have not reviewed them, and are not"
    " responsible for them. The original project is at " FORK_UPSTREAM_URL ".",
    "1+*", "1!This program is free software; you can redistribute it and/or"
    " modify it under the terms of the GNU General Public License as"
    " published by the Free Software Foundation; either version 2 of"
    " the License, or (at your option) any later version.",
    "1+*", "1!This program is distributed in the hope that it will be"
    " useful, but WITHOUT ANY WARRANTY; without even the implied"
    " warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR"
    " PURPOSE. See the GNU General Public License for more details.",
    "1+*", "1!Bug reports are appreciated. Please report anything you find in"
    " THIS build at " FORK_ISSUES_URL " -- not to the upstream project, whose"
    " maintainers did not make these changes."
};
static tablespec const vourzhon_table = {7, 2, 1, -1, vourzhon_items};
tablespec const* vourzhon = &vourzhon_table;

/* Descriptions of the different surfaces of the levels.
 */
static tiletablerow const help_floors[] = {
    {
        TRUE, Fire, 0,
        "Fire is fatal unless Chip has fire boots."
    },
    {
        TRUE, Water, 0,
        "Chip drowns in Water without water boots."
    },
    {
        TRUE, Ice, 0,
        "Chip slides across Ice without ice boots."
    },
    {
        TRUE, Slide_East, 0,
        "Force floors push Chip along unless he has the force boots."
    },
    {
        TRUE, Gravel, 0,
        "Gravel is safe to walk on, and no one but Chip can touch it."
    },
    {
        TRUE, Dirt, 0,
        "Chip is also the only one that can walk on Dirt, but it"
        " turns into a normal floor when he steps on it."
    }
};

/* Descriptions of the various kinds of wall-like obstacles.
 */
static tiletablerow const help_walls[] = {
    {
        TRUE, Wall_North, Wall,
        "Walls can either take up an entire square,"
        " or just cut off one direction."
    },
    {
        TRUE, BlueWall_Fake, 0,
        "Blue Walls can either be real walls, or just mirages. They show"
        " their true nature when Chip tries to walk through them."
    },
    {
        TRUE, PopupWall, 0,
        "Popup Walls spring up when Chip walks across them, blocking retreat."
    },
    {
        TRUE, Door_Green, Door_Red,
        "Doors can be opened if Chip has a matching key."
    }
};

/* Descriptions of various objects to be found.
 */
static tiletablerow const help_objects[] = {
    {
        TRUE, Bomb, 0,
        "A Bomb is always fatal to whatever steps on it."
    },
    {
        TRUE, CloneMachine, Button_Red,
        "Clone Machines produce duplicates of whatever is shown atop them"
        " at the touch of a Red Button."
    },
    {
        TRUE, Beartrap, Button_Brown,
        "A Bear Trap holds fast whatever steps on it. A Brown Button"
        " resets the trap and permits escape."
    },
    {
        TRUE, Teleport, 0,
        "A Teleport instantly transports you to another teleport."
    },
    {
        TRUE, SwitchWall_Open, Button_Green,
        "Toggle Walls come and go when any Green Button is pressed."
    },
    {
        TRUE, Button_Blue, 0,
        "A Blue Button causes tanks to turn around."
    },
    {
        TRUE, HintButton, 0,
        "The Hint Button provides a brief suggestion on how to proceed."
    },
    {
        TRUE, Burglar, 0,
        "The Thief takes back all your special footgear."
    }
};

/* Descriptions of things that Chip can use.
 */
static tiletablerow const help_tools[] = {
    {
        TRUE, ICChip, 0,
        "IC Chips are what Chip needs to collect in order to pass through"
        " the socket."
    },
    {
        TRUE, Key_Green, Key_Yellow,
        "Keys permit Chip to open doors of the matching color."
    },
    {
        TRUE, Boots_Water, Boots_Fire,
        "Boots allow Chip to get past fire and water, and to traverse"
        " ice and force floors as if they were normal floors."
    },
    {
        TRUE, Block_Static, 0,
        "Blocks are obstacles, but they can be pushed around. When pushed into"
        " water, the water square turns into dirt."
    },
    {
        TRUE, Socket, 0,
        "The Socket can only be passed when Chip has acquired"
        " the necessary number of IC chips."
    },
    {
        TRUE, Exit, 0,
        "The Exit takes Chip out. Finding and reaching the Exit"
        " is the main goal of every level."
    }
};

/* Descriptions of the roaming creatures.
 */
static tiletablerow const help_monsters[] = {
    {
        FALSE, Tank, 0,
        "Tanks only move in one direction, until a blue button"
        " makes them turn around."
    },
    {
        FALSE, Ball, 0,
        "Balls bounce back and forth in a straight line."
    },
    {
        FALSE, Glider, 0,
        "Gliders fly straight until they hit an obstacle,"
        " whereupon they turn left."
    },
    {
        FALSE, Fireball, 0,
        "Fireballs turn to the right upon hitting an obstacle."
    },
    {
        FALSE, Walker, 0,
        "Walkers turn in a random direction when stopped."
    },
    {
        FALSE, Bug, 0,
        "Bugs march around things, keeping the wall to their left-hand side."
    },
    {
        FALSE, Paramecium, 0,
        "Paramecia move along with the wall on their right-hand side."
    },
    {
        FALSE, Blob, 0,
        "Blobs move about completely at random, albeit slowly."
    },
    {
        FALSE, Teeth, 0,
        "Finally, Teeth home in on you; like Blobs, they can be outrun."
    }
};

/* Wrapper function for displaying illustrated help and then waiting
 * for a key command.
 */
static int helptilescreen(char const* title, tiletablerow const* table,
                          int count, int completed) {
    displaytiletable(title, table, count, completed);
    return anykey();
}

/* Display the illustrated help sequence for the game.
 */
int gameplayhelp(void) {
    int ret;

    ret = helptilescreen("FLOORS", ARRAY(help_floors), +1)
          && helptilescreen("WALLS", ARRAY(help_walls), +1)
          && helptilescreen("OBJECTS", ARRAY(help_objects), +1)
          && helptilescreen("TOOLS", ARRAY(help_tools), +1)
          && helptilescreen("MONSTERS", ARRAY(help_monsters), 0);

    cleardisplay();
    return ret;
}

/* An input callback used while displaying the list of help topics.
 */
static int scrollinputcallback(int* move) {
    switch (input(TRUE)) {
        case CmdPrev10: *move = SCROLL_ALLTHEWAY_UP;
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
        case CmdNext10: *move = SCROLL_ALLTHEWAY_DN;
            break;
        case CmdProceed: *move = TRUE;
            return FALSE;
        case CmdQuitLevel: *move = FALSE;
            return FALSE;
        case CmdQuit: exit(0);
    }
    return TRUE;
}

/* Display the list of help topics and allow the user to select which
 * ones to view.
 */
void onlinemainhelp(int topic) {
    static char const* items[] = {
        "2-",
        "1+\267", "1-Key commands during the game",
        "1+\267", "1-Key commands inbetween games",
        "1+\267", "1-Objects of the game",
        "1+\267", "1-Command-line options",
        "1+\267", "1-About Tile World",
        "1+\267", "1-Return to the program"
    };
    static tablespec const table = {7, 2, 4, 1, items};

    int n;

    switch (topic) {
        case Help_KeysDuringGame: n = 0;
            break;
        case Help_KeysBetweenGames: n = 1;
            break;
        case Help_ObjectsOfGame: n = 2;
            break;
        case Help_CmdlineOptions: n = 3;
            break;
        case Help_AboutGame: n = 4;
            break;
        default: n = 5;
            break;
    }

    while (displaylist("HELP", &table, &n, LIST_HELP, scrollinputcallback)) {
        if (n == 5)
            break;
        switch (n) {
            case 0:
                displaytable("KEYS - DURING THE GAME",
                             keyboardhelp(KEYHELP_INGAME), -1);
                anykey();
                break;
            case 1:
                displaytable("KEYS - INBETWEEN GAMES",
                             keyboardhelp(KEYHELP_TWIXTGAMES), -1);
                anykey();
                break;
            case 2:
                (void) (helptilescreen("FLOORS", ARRAY(help_floors), +1)
                        && helptilescreen("WALLS", ARRAY(help_walls), +1)
                        && helptilescreen("OBJECTS", ARRAY(help_objects), +1)
                        && helptilescreen("TOOLS", ARRAY(help_tools), +1)
                        && helptilescreen("MONSTERS", ARRAY(help_monsters), 0));
                break;
            case 3:
                displaytable("COMMAND-LINE OPTIONS", &yowzitch_table, -1);
                anykey();
                break;
            case 4:
                displaytable("ABOUT TILE WORLD", &vourzhon_table, -1);
                anykey();
                break;
        }
    }

    cleardisplay();
}

/* Display a single online help screen for the given topic.
 */
void onlinecontexthelp(int topic) {
    static char const* firsthelp_items[] = {
        "1!Welcome to Tile World!",
        "1-",
        "1!In order to begin, you must first decide which level set you"
        " wish to play. The opening screen shows you the list of all the"
        " level sets that are currently available. Use the up and down"
        " arrows to move the selection. When the level set of your choice"
        " is selected, press Enter to begin.",
        "1-",
        "1!If the list is long, you may also use the PgUp and PgDn keys to"
        " scroll one windowful at a time.",
        "1-",
        "1!At any point in the program, you may use the Q key to quit the"
        " current activity and go back up one step. Typing Shift-Q will"
        " exit Tile World completely. Typing ? or F1 at any time will bring"
        " up a list of online help topics.",
        "1-",
        "1!Tile World also comes with offline documentation. Please see the"
        " tworld.html file included for more help on playing the game and"
        " using this program.",
        "1-",
        "1!Now, press any key to go back to the list of level sets."
    };
    static tablespec const firsthelp_table = {9, 1, 0, 1, firsthelp_items};

    switch (topic) {
        case Help_First:
            displaytable("HELP", &firsthelp_table, -1);
            break;
        case Help_KeysDuringGame:
            displaytable("KEYS - DURING THE GAME",
                         keyboardhelp(KEYHELP_INGAME), -1);
            break;
        case Help_KeysBetweenGames:
            displaytable("KEYS - INBETWEEN GAMES",
                         keyboardhelp(KEYHELP_TWIXTGAMES), -1);
            break;
        case Help_FileListKeys:
            displaytable("KEYS - FILE LIST",
                         keyboardhelp(KEYHELP_FILELIST), -1);
            break;
        case Help_ScoreListKeys:
            displaytable("KEYS - SCORE LIST",
                         keyboardhelp(KEYHELP_SCORELIST), -1);
            break;
    }
    anykey();
    cleardisplay();
}
