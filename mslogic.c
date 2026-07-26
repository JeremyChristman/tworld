/* mslogic.c: The game logic for the MS ruleset.
 *
 * Copyright (C) 2001-2006 by Brian Raiter, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#include    <stdlib.h>
#include    <string.h>
#include    "defs.h"
#include    "err.h"
#include    "state.h"
#include    "random.h"
#include    "logic.h"
#include    "encoding.h"	/* MOD (Jeremy): fileidtotileid(), for the row-32 cloner glitch */

/* MOD (Jeremy): the MSCC row-32 cloner glitch is ON by default. It went in as an
 * opt-in flag purely so the same tree could be built both ways for an A/B run;
 * that run is done (269 MS sets / 20,332 solutions plus 909 Lynx solutions:
 * 4 fixed, 0 regressions), so the default build is now the emulating one -- a
 * default nobody actually ships is just a second, untested engine.
 * Build with -DNO_FIX_ROW32_CLONER to get the old discard-the-wiring behaviour.
 */
#if !defined(NO_FIX_ROW32_CLONER) && !defined(FIX_ROW32_CLONER)
#define	FIX_ROW32_CLONER	1
#endif

/* MOD (Jeremy): the two Stage-2 desync fixes, likewise ON by default now that the
 * A/B run they were flagged for is finished (269 MS sets / 21,342 solutions plus
 * 909 Lynx solutions: 2 fixed, 0 regressions). Each makes the MS engine agree with
 * a rule it already applies elsewhere -- see the comments at each site.
 * Build with -DNO_FIX_BROKEN_TELEPORT_SLIDE / -DNO_FIX_TELEPORT_BLOCK_ONTO_CHIP
 * to restore the old behaviour individually.
 */
#if !defined(NO_FIX_BROKEN_TELEPORT_SLIDE) && !defined(FIX_BROKEN_TELEPORT_SLIDE)
#define	FIX_BROKEN_TELEPORT_SLIDE	1
#endif
#if !defined(NO_FIX_TELEPORT_BLOCK_ONTO_CHIP) && !defined(FIX_TELEPORT_BLOCK_ONTO_CHIP)
#define	FIX_TELEPORT_BLOCK_ONTO_CHIP	1
#endif

/* MOD (Jeremy): controller direction from stalled creatures, ON by default.
 * Fixes 11 desyncs. It also costs one previously-valid replay, GAP'sSub#10, and
 * that was a deliberate, evidenced call rather than an oversight: measured
 * against SuperCC on that very level, the old engine diverges at tick 119 and
 * this one at tick 353, and where they differ at 119 SuperCC agrees with THIS
 * engine. GAP'sSub#10 was completing by luck on a run that was already wrong;
 * it now reaches a separate, unrelated bug (a spurious tank clone at (12,1)).
 * Build with -DNO_FIX_CONTROLLERDIR_STALLED to restore the old behaviour.
 */
#if !defined(NO_FIX_CONTROLLERDIR_STALLED) && !defined(FIX_CONTROLLERDIR_STALLED)
#define	FIX_CONTROLLERDIR_STALLED	1
#endif

#ifdef NDEBUG
#define	_assert(test)	((void)0)
#else
#define _assert(test) ((test) || (die("internal error: failed sanity check (%s)\nPlease report this error to https://github.com/SicklySilverMoon/tworld/issues", #test), 0))
#endif

/* A list of ways for Chip to lose.
 */
enum {
    CHIP_OKAY = 0,
    CHIP_DROWNED, CHIP_BURNED, CHIP_BOMBED, CHIP_OUTOFTIME, CHIP_COLLIDED,
    CHIP_SQUISHED, CHIP_SQUISHED_DEATH, /* added with Squish patch */
    CHIP_NOTOKAY
};

/* Forward declaration of a central function.
 */
static int advancecreature(creature* cr, int dir);

#ifdef TRACE_DESYNC
/* MOD (Jeremy): TRUE when the current level/tick is inside the traced window. */
static int tracethistick(void);
#endif

/* The most recently used stepping phase value.
 */
static int laststepping = 0;

/* A pointer to the game state, used so that it doesn't have to be
 * passed to every single function.
 */
static gamestate* state;

/*
 * Accessor macros for various fields in the game state. Many of the
 * macros can be used as an lvalue.
 */

#define setstate(p)         (state = (p)->state)

#define getchip()           (creatures[0])
#define chippos()           (getchip()->pos)
#define chipdir()           (getchip()->dir)

#define chipsneeded()       (state->chipsneeded)

#define clonerlist()        (state->cloners)
#define clonerlistsize()    (state->clonercount)
#define traplist()          (state->traps)
#define traplistsize()      (state->trapcount)

#define timelimit()         (state->timelimit)
#define timeoffset()        (state->timeoffset)
#define stepping()          (state->stepping)
#define currenttime()       (state->currenttime)
#define currentinput()      (state->currentinput)
#define xviewpos()          (state->xviewpos)
#define yviewpos()          (state->yviewpos)

#define mainprng()          (&state->mainprng)

#define lastmove()          (state->lastmove)

#define addsoundeffect(sfx) (state->soundeffects |= 1 << (sfx))

#define cellat(pos)         (&state->map[pos])

#define setnosaving()       (state->statusflags |= SF_NOSAVING)
#define showhint()          (state->statusflags |= SF_SHOWHINT)
#define hidehint()          (state->statusflags &= ~SF_SHOWHINT)

#define getmsstate()        (state->msstate)

#define completed()         (getmsstate().completed)
#define chipstatus()        (getmsstate().chipstatus)
#define chipwait()          (getmsstate().chipwait)
#define controllerdir()     (getmsstate().controllerdir)
#define lastslipdir()       (getmsstate().lastslipdir)
#define xviewoffset()       (getmsstate().xviewoffset)
#define yviewoffset()       (getmsstate().yviewoffset)

#define goalpos()           (getmsstate().goalpos)
#define hasgoal()           (goalpos() >= 0)
#define cancelgoal()        (goalpos() = -1)

#define possession(obj) (*checkpossession(obj))

static short* checkpossession(int obj) {
    switch (obj) {
        case Key_Red:
            return &state->keys[0];
        case Key_Blue:
            return &state->keys[1];
        case Key_Yellow:
            return &state->keys[2];
        case Key_Green:
            return &state->keys[3];
        case Boots_Ice:
            return &state->boots[0];
        case Boots_Slide:
            return &state->boots[1];
        case Boots_Fire:
            return &state->boots[2];
        case Boots_Water:
            return &state->boots[3];
        case Door_Red:
            return &state->keys[0];
        case Door_Blue:
            return &state->keys[1];
        case Door_Yellow:
            return &state->keys[2];
        case Door_Green:
            return &state->keys[3];
        case Ice:
            return &state->boots[0];
        case IceWall_Northwest:
            return &state->boots[0];
        case IceWall_Northeast:
            return &state->boots[0];
        case IceWall_Southwest:
            return &state->boots[0];
        case IceWall_Southeast:
            return &state->boots[0];
        case Slide_North:
            return &state->boots[1];
        case Slide_West:
            return &state->boots[1];
        case Slide_South:
            return &state->boots[1];
        case Slide_East:
            return &state->boots[1];
        case Slide_Random:
            return &state->boots[1];
        case Fire:
            return &state->boots[2];
        case Water:
            return &state->boots[3];
    }
    warn("Invalid object %d handed to possession()", obj);
    _assert(!"possession() called with an invalid object");
    return NULL;
}

/*
 * Memory allocation functions for the various arenas.
 */

/* The creature pool is a linked list of lumps, each lump holding this
 * many creatures.
 */
#define crpoollumpsize 256

/* The data that makes up one lump of the creature pool.
 */
typedef struct crpoollump crpoollump;
struct crpoollump {
    int count;                     /* number of unused creatures */
    crpoollump* prev;              /* the previously allocated lump */
    crpoollump* next;              /* the next lump after this one */
    creature lump[crpoollumpsize]; /* the lump proper */
};

/* The data associated with a sliding object.
 */
typedef struct slipper {
    creature* cr;
    int dir;
} slipper;

/* The linked list of creature pools, forming the creature arena.
 */
static crpoollump* currentcrpoollump = NULL;

/* The list of active creatures.
 */
static creature** creatures = NULL;
static int creaturecount = 0;
static int creaturesallocated = 0;

/* The list of "active" blocks.
 */
static creature** blocks = NULL;
static int blockcount = 0;
static int blocksallocated = 0;

/* The list of sliding creatures.
 */
static slipper* slips = NULL;
static int slipcount = 0;
static int slipsallocated = 0;

/* Mark all entries in the creature arena as unused.
 */
static void resetcreaturepool(void) {
    if (currentcrpoollump)
        while (currentcrpoollump->prev)
            currentcrpoollump = currentcrpoollump->prev;
}

/* Destroy the creature arena.
 */
static void freecreaturepool(void) {
    crpoollump* next;

    resetcreaturepool();
    while (currentcrpoollump) {
        next = currentcrpoollump->next;
        free(currentcrpoollump);
        currentcrpoollump = next;
    }
}

/* Return a pointer to a fresh creature.
 */
static creature* allocatecreature(void) {
    crpoollump* next;
    creature* cr;

    if (!currentcrpoollump || currentcrpoollump->count == 0) {
        if (currentcrpoollump && currentcrpoollump->next) {
            currentcrpoollump = currentcrpoollump->next;
            currentcrpoollump->count = crpoollumpsize;
        } else {
            next = malloc(sizeof *next);
            if (!next)
                memerrexit();
            next->count = crpoollumpsize;
            next->prev = currentcrpoollump;
            next->next = NULL;
            if (currentcrpoollump)
                currentcrpoollump->next = next;
            currentcrpoollump = next;
        }
    }

    --currentcrpoollump->count;
    cr = currentcrpoollump->lump + currentcrpoollump->count;
    cr->id = Nothing;
    cr->pos = -1;
    cr->dir = NIL;
    cr->tdir = NIL;
    cr->state = 0;
    cr->frame = 0;
    cr->hidden = FALSE;
    cr->moving = 0;
    return cr;
}

/* Empty the list of active creatures.
 */
static void resetcreaturelist(void) {
    creaturecount = 0;
}

/* Append the given creature to the end of the creature list.
 */
static creature* addtocreaturelist(creature* cr) {
    if (creaturecount >= creaturesallocated) {
        creaturesallocated = creaturesallocated ? creaturesallocated * 2 : 16;
        creatures = realloc(creatures, creaturesallocated * sizeof *creatures);
        if (!creatures)
            memerrexit();
    }
    creatures[creaturecount++] = cr;
    return cr;
}

/* Empty the list of "active" blocks.
 */
static void resetblocklist(void) {
    blockcount = 0;
}

/* Append the given block to the end of the block list.
 */
static creature* addtoblocklist(creature* cr) {
    if (blockcount >= blocksallocated) {
        blocksallocated = blocksallocated ? blocksallocated * 2 : 16;
        blocks = realloc(blocks, blocksallocated * sizeof *blocks);
        if (!blocks)
            memerrexit();
    }
    blocks[blockcount++] = cr;
    return cr;
}

/* Empty the list of sliding creatures.
 */
static void resetsliplist(void) {
    slipcount = 0;
}

static int msccslippers;

/* Append the given creature to the end of the slip list.
 */
static creature* appendtosliplist(creature* cr, int dir) {
    int n;

    for (n = 0; n < slipcount; ++n) {
        if (slips[n].cr == cr) {
            slips[n].dir = dir;
            return cr;
        }
    }

    if (slipcount >= slipsallocated) {
        slipsallocated = slipsallocated ? slipsallocated * 2 : 16;
        slips = realloc(slips, slipsallocated * sizeof *slips);
        if (!slips)
            memerrexit();
    }
    slips[slipcount].cr = cr;
    slips[slipcount].dir = dir;
    ++slipcount;
    msccslippers++; /* new accounting */
    return cr;
}

/* Add the given creature to the start of the slip list.
 */
static creature* prependtosliplist(creature* cr, int dir) {
    int n;

    if (slipcount && slips[0].cr == cr) {
        slips[0].dir = dir;
        return cr;
    }

    if (slipcount >= slipsallocated) {
        slipsallocated = slipsallocated ? slipsallocated * 2 : 16;
        slips = realloc(slips, slipsallocated * sizeof *slips);
        if (!slips)
            memerrexit();
    }
    for (n = slipcount; n; --n)
        slips[n] = slips[n - 1];
    ++slipcount;
    slips[0].cr = cr;
    slips[0].dir = dir;
    return cr;
}

/* Return the sliding direction of a creature on the slip list.
 */
static int getslipdir(creature* cr) {
    int n;

    for (n = 0; n < slipcount; ++n)
        if (slips[n].cr == cr)
            return slips[n].dir;
    return NIL;
}

/* Remove the given creature from the slip list.
 */
static void removefromsliplist(creature* cr) {
    int n;

    for (n = 0; n < slipcount; ++n)
        if (slips[n].cr == cr)
            break;
    if (n == slipcount)
        return;
    --slipcount;
    for (; n < slipcount; ++n)
        slips[n] = slips[n + 1];
}

/*
 * Simple floor functions.
 */

/* Floor state flags.
 */
#define FS_BUTTONDOWN 0x01 /* button press is deferred */
#define FS_CLONING    0x02 /* clone machine is activated */
#define FS_BROKEN     0x04 /* teleport/toggle wall doesn't work */
#define FS_HASMUTANT  0x08 /* beartrap contains mutant block */
#define FS_MARKER     0x10 /* marker used during initialization */

/* Translate a slide floor into the direction it points in. In the
 * case of a random slide floor, a new direction is selected.
 */
static int getslidedir(int floor) {
    switch (floor) {
        case Slide_North:
            return NORTH;
        case Slide_West:
            return WEST;
        case Slide_South:
            return SOUTH;
        case Slide_East:
            return EAST;
        case Slide_Random:
            return 1 << random4(mainprng());
    }
    return NIL;
}

/* Alter a creature's direction if they are at an ice wall.
 */
static int icewallturn(int floor, int dir) {
    switch (floor) {
        case IceWall_Northeast:
            return dir == SOUTH ? EAST : dir == WEST ? NORTH : dir;
        case IceWall_Southwest:
            return dir == NORTH ? WEST : dir == EAST ? SOUTH : dir;
        case IceWall_Northwest:
            return dir == SOUTH ? WEST : dir == EAST ? NORTH : dir;
        case IceWall_Southeast:
            return dir == NORTH ? EAST : dir == WEST ? SOUTH : dir;
    }
    return dir;
}

/* Find the location of a bear trap from one of its buttons.
 */
static int trapfrombutton(int pos) {
    xyconn* traps;
    int i;

    traps = traplist();
    for (i = traplistsize(); i; ++traps, --i)
        if (traps->from == pos)
            return traps->to;
    return -1;
}

/* Find the location of a clone machine from one of its buttons.
 */
static int clonerfrombutton(int pos) {
    xyconn* cloners;
    int i;

    cloners = clonerlist();
    for (i = clonerlistsize(); i; ++cloners, --i)
        if (cloners->from == pos)
            return cloners->to;
    return -1;
}

/* Return the floor tile found at the given location.
 */
static int floorat(int pos) {
    mapcell* cell;

    cell = cellat(pos);
    if (!iskey(cell->top.id) && !isboots(cell->top.id)
        && !iscreature(cell->top.id))
        return cell->top.id;
    if (!iskey(cell->bot.id) && !isboots(cell->bot.id)
        && !iscreature(cell->bot.id))
        return cell->bot.id;
    return Empty;
}

/* Return a pointer to the tile that forms the floor at the given
 * location.
 */
static maptile* getfloorat(int pos) {
    mapcell* cell;

    cell = cellat(pos);
    if (!iskey(cell->top.id) && !isboots(cell->top.id)
        && !iscreature(cell->top.id))
        return &cell->top;
    if (!iskey(cell->bot.id) && !isboots(cell->bot.id)
        && !iscreature(cell->bot.id))
        return &cell->bot;
    return &cell->bot; /* ? */
}

/* Return TRUE if the brown button at the give location is currently
 * held down.
 */
static int istrapbuttondown(int pos) {
    return pos >= 0 && pos < CXGRID * CYGRID && cellat(pos)->top.id != Button_Brown;
}

/* Place a new tile at the given location, causing the current upper
 * tile to become the lower tile.
 */
static void pushtile(int pos, maptile tile) {
    mapcell* cell;

    cell = cellat(pos);
    cell->bot = cell->top;
    cell->top = tile;
}

/* Remove the upper tile from the given location, causing the current
 * lower tile to become uppermost.
 */
static maptile poptile(int pos) {
    maptile tile;
    mapcell* cell;

    cell = cellat(pos);
    tile = cell->top;
    cell->top = cell->bot;
    cell->bot.id = Empty;
    cell->bot.state = 0;
    return tile;
}

/* Return TRUE if a bear trap is currently passable.
 */
static int istrapopen(int pos, int skippos) {
    xyconn* traps;
    int i;

    traps = traplist();
    for (i = traplistsize(); i; ++traps, --i)
        if (traps->to == pos && traps->from != skippos && istrapbuttondown(traps->from))
            return TRUE;
    return FALSE;
}

/* Flip-flop the state of any toggle walls.
 */
static void togglewalls(void) {
    mapcell* cell;
    int pos;

    for (pos = 0; pos < CXGRID * CYGRID; ++pos) {
        cell = cellat(pos);
        if ((cell->top.id == SwitchWall_Open || cell->top.id == SwitchWall_Closed) && !(cell->top.state & FS_BROKEN))
            cell->top.id ^= SwitchWall_Open ^ SwitchWall_Closed;
        if ((cell->bot.id == SwitchWall_Open || cell->bot.id == SwitchWall_Closed) && !(cell->bot.state & FS_BROKEN))
            cell->bot.id ^= SwitchWall_Open ^ SwitchWall_Closed;
    }
}

/*
 * Functions that manage the list of entities.
 */

/* Creature state flags.
 */
#define CS_RELEASED  0x01 /* can leave a beartrap */
#define CS_CLONING   0x02 /* cannot move this tick */
#define CS_HASMOVED  0x04 /* already used current move */
#define CS_TURNING   0x08 /* is turning around */
#define CS_SLIP      0x10 /* is on the slip list */
#define CS_SLIDE     0x20 /* is on the slip list but can move */
#define CS_DEFERPUSH 0x40 /* button pushes will be delayed */
#define CS_MUTANT    0x80 /* block is mutant, looks like Chip */

/* Return the creature located at pos. Ignores Chip unless includechip
 * is TRUE. Return NULL if no such creature is present.
 */
static creature* lookupcreature(int pos, int includechip) {
    int n;

    if (!creatures)
        return NULL;
    for (n = 0; n < creaturecount; ++n) {
        if (creatures[n]->hidden)
            continue;
        if (creatures[n]->pos == pos)
            if (creatures[n]->id != Chip || includechip)
                return creatures[n];
    }
    return NULL;
}

/* Return the block located at pos. If the block in question is not
 * currently "active", it is automatically added to the block list.
 */
static creature* lookupblock(int pos) {
    creature* cr;
    int id, n;

    if (blocks) {
        for (n = 0; n < blockcount; ++n)
            if (blocks[n]->pos == pos && !blocks[n]->hidden)
                return blocks[n];
    }

    cr = allocatecreature();
    cr->id = Block;
    cr->pos = pos;
    id = cellat(pos)->top.id;
    if (id == Block_Static)
        cr->dir = NIL;
    else if (creatureid(id) == Block)
        cr->dir = creaturedirid(id);
    else
        _assert(!"lookupblock() called on blockless location");

    return addtoblocklist(cr);
}

/* Update the given creature's tile on the map to reflect its current
 * state.
 */
static void updatecreature(creature const* cr) {
    maptile* tile;
    int id, dir;

    if (cr->hidden)
        return;
    tile = &cellat(cr->pos)->top;
    id = cr->id;
    if (id == Block) {
        tile->id = Block_Static;
        if (cr->state & CS_MUTANT)
            tile->id = crtile(Chip, NORTH);
        return;
    } else if (id == Chip) {
        if (chipstatus()) {
            switch (chipstatus()) {
                case CHIP_BURNED:
                    tile->id = Burned_Chip;
                    return;
                case CHIP_DROWNED:
                    tile->id = Drowned_Chip;
                    return;
            }
        } else if (cellat(cr->pos)->bot.id == Water) {
            id = Swimming_Chip;
        }
    }

    dir = cr->dir;
    if (cr->state & CS_TURNING)
        dir = right(dir);

    tile->id = crtile(id, dir);
    tile->state = 0;
}

/* Add the given creature's tile to the map.
 */
static void addcreaturetomap(creature const* cr) {
    static maptile const dummy = {Empty, 0};

    if (cr->hidden)
        return;
    pushtile(cr->pos, dummy);
    updatecreature(cr);
}

/* Enervate an inert creature.
 */
static creature* awakencreature(int pos) {
    creature* new;
    int tileid;

    tileid = cellat(pos)->top.id;
    if (!iscreature(tileid) || creatureid(tileid) == Chip)
        return NULL;
    new = allocatecreature();
    new->id = creatureid(tileid);
    new->dir = creaturedirid(tileid);
    new->pos = pos;
    return new->id == Block ? addtoblocklist(new) : addtocreaturelist(new);
}

/* Mark a creature as dead.
 */
static void removecreature(creature* cr) {
    cr->state &= ~(CS_SLIP | CS_SLIDE);
    if (cr->id == Chip) {
        if (chipstatus() == CHIP_OKAY)
            chipstatus() = CHIP_NOTOKAY;
    } else
        cr->hidden = TRUE;
}

/* Turn around any and all tanks. (A tank that is halfway through the
 * process of moving at the time is given special treatment.)
 */
static void turntanks(creature const* inmidmove) {
    int n;

    for (n = 0; n < creaturecount; ++n) {
        creature* cr = creatures[n]; /* convenience, Tank Top Glitch */
        if (cr->hidden || cr->id != Tank)
            continue;
        cr->dir = back(cr->dir);
        if (cr->state & CS_SLIP && !(cr->state & CS_SLIDE)
            && cr->frame != 0 && cr->moving == 0) /* cr->moving: SGG instead */
            cr->dir = back(cr->frame); /* Tank Top Glitch */
        if (!(cr->state & CS_TURNING))
            cr->state |= CS_TURNING | CS_HASMOVED;
        if (cr == inmidmove)
            continue;
        if (creatureid(cellat(cr->pos)->top.id) == Tank) {
            updatecreature(cr);
        } else if (cr->moving != 0) { /* handle Spontaneous Generation */
            if (cr->state & CS_TURNING) { /* always TRUE? */
                cr->state &= ~CS_TURNING;
                updatecreature(cr);
                cr->state |= CS_TURNING;
            }
            cr->dir = back(cr->dir); /* OK with SGG, bad for stacked tanks */
        }
    }
}

/*
 * Maintaining the slip list.
 */

/* Add the given creature to the slip list if it is not already on it
 * (assuming that the given floor is a kind that causes slipping).
 */
static void startfloormovement(creature* cr, int floor, int fdir) {
    int dir = fdir; /* fdir used with tank reversal when stuck on teleporter */

    cr->state &= ~(CS_SLIP | CS_SLIDE);

    if (isice(floor)) {
        if (fdir == NIL) { /* tank reversal patch */
            dir = icewallturn(floor, cr->dir);
        }
    } else if (isslide(floor))
        dir = getslidedir(floor);
    else if (floor == Teleport) {
        if (fdir == NIL)
            dir = cr->dir; /* tank reversal patch */
    } else if (floor == Beartrap && cr->id == Block)
        dir = cr->dir;
    else if (cr->id != Chip) /* new with Convergence Patch */
        return;
    else
        dir = cr->dir; /* new with Convergence Patch */

    if (cr->id == Chip) { /* changed with Convergence Patch */
        /* cr->state |= isslide(floor) ? CS_SLIDE : CS_SLIP; */
        cr->state |= (isice(floor) || (floor == Teleport && dir != NIL)) ? CS_SLIP : CS_SLIDE;
        prependtosliplist(cr, dir);
        cr->dir = dir;
        updatecreature(cr);
    } else {
        cr->state |= CS_SLIP;
        cr->frame = 0; /* safety with Tank Top Glitch */
        appendtosliplist(cr, dir);
    }
}

/* Remove the given creature from the slip list.
 */
static void endfloormovement(creature* cr) {
    cr->state &= ~(CS_SLIP | CS_SLIDE);
    removefromsliplist(cr);
}

/* Clean out deadwood entries in the slip list.
 */
static void updatesliplist() {
    int n;

    for (n = slipcount - 1; n >= 0; --n)
        if (!(slips[n].cr->state & (CS_SLIP | CS_SLIDE)))
            endfloormovement(slips[n].cr);
}

/*
 * The laws of movement across the various floors.
 *
 * Chip, blocks, and other creatures all have slightly different rules
 * about what sort of tiles they are permitted to move into. The
 * following lookup table encapsulates these rules. Note that these
 * rules are only the first check; a creature may be occasionally
 * permitted a particular type of move but still prevented in a
 * specific situation.
 */

#define    NWSE    (NORTH | WEST | SOUTH | EAST)

static struct {
    unsigned char chip, block, creature;
} const movelaws[] = {
/* Nothing */
    {0,    0,      0},
/* Empty */
 {NWSE, NWSE, NWSE},
/* Slide_North */
 {NWSE, NWSE, NWSE},
/* Slide_West */
 {NWSE, NWSE, NWSE},
/* Slide_South */
 {NWSE, NWSE, NWSE},
/* Slide_East */
 {NWSE, NWSE, NWSE},
/* Slide_Random */
 {NWSE, NWSE,   0},
/* Ice */
 {NWSE, NWSE, NWSE},
/* IceWall_Northwest */
 {SOUTH | EAST, SOUTH | EAST, SOUTH | EAST},
/* IceWall_Northeast */
 {SOUTH | WEST, SOUTH | WEST, SOUTH | WEST},
/* IceWall_Southwest */
 {NORTH | EAST, NORTH | EAST, NORTH | EAST},
/* IceWall_Southeast */
 {NORTH | WEST, NORTH | WEST, NORTH | WEST},
/* Gravel */
 {NWSE, NWSE,   0},
/* Dirt */
 {NWSE, 0,      0},
/* Water */
 {NWSE, NWSE, NWSE},
/* Fire */
 {NWSE, NWSE, NWSE},
/* Bomb */
 {NWSE, NWSE, NWSE},
/* Beartrap */
 {NWSE, NWSE, NWSE},
/* Burglar */
 {NWSE, 0,      0},
/* HintButton */
 {NWSE, NWSE, NWSE},
/* Button_Blue */
 {NWSE, NWSE, NWSE},
/* Button_Green */
 {NWSE, NWSE, NWSE},
/* Button_Red */
 {NWSE, NWSE, NWSE},
/* Button_Brown */
 {NWSE, NWSE, NWSE},
/* Teleport */
 {NWSE, NWSE, NWSE},
/* Wall */
 {0,    0,      0},
/* Wall_North */
 {NORTH | WEST | EAST,
                NORTH | WEST | EAST,
                              NORTH | WEST | EAST},
/* Wall_West */
 {NORTH | WEST | SOUTH,
                NORTH | WEST | SOUTH,
                              NORTH | WEST | SOUTH},
/* Wall_South */
 {WEST | SOUTH | EAST,
                WEST | SOUTH | EAST,
                              WEST | SOUTH | EAST},
/* Wall_East */
 {NORTH | SOUTH | EAST,
                NORTH | SOUTH | EAST,
                              NORTH | SOUTH | EAST},
/* Wall_Southeast */
 {SOUTH | EAST, SOUTH | EAST, SOUTH | EAST},
/* HiddenWall_Perm */
 {0,    0,      0},
/* HiddenWall_Temp */
 {NWSE, 0,      0},
/* BlueWall_Real */
 {NWSE, 0,      0},
/* BlueWall_Fake */
 {NWSE, 0,      0},
/* SwitchWall_Open */
 {NWSE, NWSE, NWSE},
/* SwitchWall_Closed */
 {0,    0,      0},
/* PopupWall */
 {NWSE, 0,      0},
/* CloneMachine */
 {0,    0,      0},
/* Door_Red */
 {NWSE, 0,      0},
/* Door_Blue */
 {NWSE, 0,      0},
/* Door_Yellow */
 {NWSE, 0,      0},
/* Door_Green */
 {NWSE, 0,      0},
/* Socket */
 {NWSE, 0,      0},
/* Exit */
 {NWSE, NWSE,   0},
/* ICChip */
 {NWSE, 0,      0},
/* Key_Red */
 {NWSE, NWSE, NWSE},
/* Key_Blue */
 {NWSE, NWSE, NWSE},
/* Key_Yellow */
 {NWSE, NWSE, NWSE},
/* Key_Green */
 {NWSE, NWSE, NWSE},
/* Boots_Ice */
 {NWSE, NWSE,   0},
/* Boots_Slide */
 {NWSE, NWSE,   0},
/* Boots_Fire */
 {NWSE, NWSE,   0},
/* Boots_Water */
 {NWSE, NWSE,   0},
/* Block_Static */
 {NWSE, 0,      0},
/* Drowned_Chip */
 {0,    0,      0},
/* Burned_Chip */
 {0,    0,      0},
/* Bombed_Chip */
 {0,    0,      0},
/* Exited_Chip */
 {0,    0,      0},
/* Exit_Extra_1 */
 {0,    0,      0},
/* Exit_Extra_2 */
 {0,    0,      0},
/* Overlay_Buffer */
 {0,    0,      0},
/* Floor_Reserved2 */
 {0,    0,      0},
/* Floor_Reserved1 */
 {0,    0,      0},
};

/* Including the flag CMM_NOLEAVECHECK in a call to canmakemove()
 * indicates that the tile the creature is moving out of is
 * automatically presumed to permit such movement. CMM_NOEXPOSEWALLS
 * causes blue and hidden walls to remain unexposed.
 * CMM_CLONECANTBLOCK means that the creature will not be prevented
 * from moving by an identical creature standing in the way.
 * CMM_NOPUSHING prevents Chip from pushing blocks inside this
 * function. CMM_TELEPORTPUSH indicates to the block-pushing logic
 * that Chip is teleporting. This prevents a stack of two blocks from
 * being treated as a single block, and allows Chip to push a slipping
 * block away from him. CMM_NOFIRECHECK causes bugs and walkers to not
 * avoid fire. Finally, CMM_NODEFERBUTTONS causes buttons pressed by
 * pushed blocks to take effect immediately.
 */
#define CMM_NOLEAVECHECK   0x0001
#define CMM_NOEXPOSEWALLS  0x0002
#define CMM_CLONECANTBLOCK 0x0004
#define CMM_NOPUSHING      0x0008
#define CMM_TELEPORTPUSH   0x0010
#define CMM_NOFIRECHECK    0x0020
#define CMM_NODEFERBUTTONS 0x0040

/* Move a block at the given position forward in the given direction.
 * FALSE is returned if the block cannot be pushed.
 */
static int pushblock(int pos, int dir, int flags) {
    creature* cr;
    int slipdir, r;
    int slipping; /* new */
    _assert(cellat(pos)->top.id == Block_Static);
    _assert(dir != NIL);

    cr = lookupblock(pos);
    if (!cr) {
        warn("%d: attempt to push disembodied block!", currenttime());
        return FALSE;
    }
    slipping = (cr->state & (CS_SLIP | CS_SLIDE)); /* accounting */
    if (cr->state & (CS_SLIP | CS_SLIDE)) {
        slipdir = getslipdir(cr);
        if (dir == slipdir || dir == back(slipdir)) {
            if (!(flags & CMM_TELEPORTPUSH)) {
                return FALSE;
            }
        }
    }

    if (!(flags & CMM_TELEPORTPUSH) && cellat(pos)->bot.id == Block_Static)
        cellat(pos)->bot.id = Empty;
    if (!(flags & CMM_NODEFERBUTTONS))
        cr->state |= CS_DEFERPUSH;
    r = advancecreature(cr, dir);
    if (!(flags & CMM_NODEFERBUTTONS))
        cr->state &= ~CS_DEFERPUSH;
    if (!r) {
        cr->state &= ~(CS_SLIP | CS_SLIDE);
        if (slipping) { /* new MSCC-like accounting */
            msccslippers--;
            removefromsliplist(cr);
        }
    }
    return r;
}

/* Return TRUE if the given creature is allowed to attempt to move in
 * the given direction. Side effects can and will occur from calling
 * this function, as indicated by flags.
 */
static int canmakemove(creature const* cr, int dir, int flags) {
    int to;
    int floor;
    int id, y, x;

    _assert(cr);
    _assert(dir != NIL);

    y = cr->pos / CXGRID;
    x = cr->pos % CXGRID;
    y += dir == NORTH ? -1 : dir == SOUTH ? +1 : 0;
    x += dir == WEST ? -1 : dir == EAST ? +1 : 0;
    if (y < 0 || y >= CYGRID || x < 0 || x >= CXGRID)
        return FALSE;
    to = y * CXGRID + x;

    if (!(flags & CMM_NOLEAVECHECK)) {
        switch (cellat(cr->pos)->bot.id) {
            case Wall_North:
                if (dir == NORTH) return FALSE;
                break;
            case Wall_West:
                if (dir == WEST) return FALSE;
                break;
            case Wall_South:
                if (dir == SOUTH) return FALSE;
                break;
            case Wall_East:
                if (dir == EAST) return FALSE;
                break;
            case Wall_Southeast:
                if (dir & (SOUTH | EAST)) return FALSE;
                break;
            case Beartrap:
                if (!(cr->state & CS_RELEASED))
                    return FALSE;
                break;
        }
    }

    if (cr->id == Chip) {
        floor = floorat(to);
        if (!(movelaws[floor].chip & dir))
            return FALSE;
        if (floor == Socket && chipsneeded() > 0)
            return FALSE;
        if (isdoor(floor) && !possession(floor))
            return FALSE;
        if (iscreature(cellat(to)->top.id)) {
            id = creatureid(cellat(to)->top.id);
            if (id == Chip || id == Swimming_Chip || id == Block)
                return FALSE;
        }
        if (floor == HiddenWall_Temp || floor == BlueWall_Real) {
            if (!(flags & CMM_NOEXPOSEWALLS))
                getfloorat(to)->id = Wall;
            return FALSE;
        }
        if (floor == Block_Static) {
            if (!pushblock(to, dir, flags))
                return FALSE;
            else if (flags & CMM_NOPUSHING)
                return FALSE;
            if (cellat(to)->bot.id == CloneMachine)
                return FALSE; /* totally backwards: need to check this first */
            if ((flags & CMM_TELEPORTPUSH) && floorat(to) == Block_Static)
                /* totally backwards: remove "&& cellat(to)->bot.id == Empty)" */
                return TRUE;
            return canmakemove(cr, dir, flags | CMM_NOPUSHING);
        }
    } else if (cr->id == Block) {
        floor = cellat(to)->top.id;
        if (iscreature(floor)) {
            id = creatureid(floor);
#ifdef FIX_TELEPORT_BLOCK_ONTO_CHIP
            /* MOD (Jeremy): a block is normally allowed to shove into Chip --
             * that is how blocks squash him. But when this call is a *teleport
             * destination* test, MSCC judges the exit by what lies UNDER Chip,
             * so a block will not come out of a teleport on top of him if the
             * floor there is solid; it moves on to the next teleport instead.
             * Tile World answered TRUE unconditionally here and so chose a
             * different exit. Monsters already get exactly this treatment in
             * the branch just below; blocks did not.
             * Found on PeterM2#25 "CONFUSING". */
            if ((flags & CMM_TELEPORTPUSH) && (id == Chip || id == Swimming_Chip)) {
                floor = cellat(to)->bot.id;
                if (iscreature(floor)) {
                    id = creatureid(floor);
                    return id == Chip || id == Swimming_Chip;
                }
            } else
#endif
            return id == Chip || id == Swimming_Chip;
        }
        if (!(movelaws[floor].block & dir))
            return FALSE;
    } else {
        floor = cellat(to)->top.id;
        if (iscreature(floor)) {
            id = creatureid(floor);
            if (id == Chip || id == Swimming_Chip) {
                floor = cellat(to)->bot.id;
                if (iscreature(floor)) {
                    id = creatureid(floor);
                    return id == Chip || id == Swimming_Chip;
                }
            }
        }
        if (iscreature(floor)) { /* turning tank cloning patch */
            creature* F = lookupcreature(to, FALSE);
            if (!(flags & CMM_CLONECANTBLOCK)) /* not cloning */
                return FALSE;
            if ((F == NULL || !(F->state & CS_TURNING))
                && floor == crtile(cr->id, cr->dir))
                return TRUE;
            /* must check "floor", so same-dir non-creature tank will clone */
            if (F == NULL)
                return FALSE;
            if (F->dir == cr->dir)
                return TRUE;
            return FALSE;
        }
        if (!(movelaws[floor].creature & dir))
            return FALSE;
        if (floor == Fire && (cr->id == Bug || cr->id == Walker))
            if (!(flags & CMM_NOFIRECHECK))
                return FALSE;
    }

    if (cellat(to)->bot.id == CloneMachine)
        return FALSE;

    return TRUE;
}

/*
 * How everyone selects their move.
 */

/* This function embodies the movement behavior of all the creatures.
 * Given a creature, this function enumerates its desired direction of
 * movement and selects the first one that is permitted. Note that
 * calling this function also updates the current controller
 * direction.
 */
static void choosecreaturemove(creature* cr) {
    int choices[4] = {NIL, NIL, NIL, NIL};
    int dir, pdir;
    int floor;
    int y, x, m, n;

    cr->tdir = NIL;

    if (cr->hidden)
        return;
    if (cr->id == Block)
        return;
    if (currenttime() & 2)
        return;
    if (cr->id == Teeth || cr->id == Blob) {
        if ((currenttime() + stepping()) & 4)
            return;
    }
    if (cr->state & CS_TURNING) {
        cr->state &= ~(CS_TURNING | CS_HASMOVED);
        updatecreature(cr);
    }
    if (cr->state & CS_HASMOVED) { /* should be a stalled tank */
        int floor = cellat(cr->pos)->top.id; /* stacked tank patch */
        int id = creatureid(floor);
        if (iscreature(floor) && (id == Chip || id == Swimming_Chip))
            floor = cellat(cr->pos)->bot.id;
        if (!iscreature(floor) && movelaws[floor].creature)
            cr->hidden = TRUE; /* hack with (0,0) movement success */
        /* maybe should check if (0,0) move goes on sliplist, but that's UB */
        if (FALSE && cr->hidden && (id == Chip || id == Swimming_Chip) && (floor != Fire && floor != Water && floor != Bomb)) {
            chipstatus() = CHIP_COLLIDED;
            cellat(cr->pos)->bot.id = cellat(cr->pos)->top.id;
            cellat(cr->pos)->top.id = Tank + diridx(cr->dir);
        } /* Actually, successful (0,0) moves don't kill Chip */
    }
    if (cr->state & CS_HASMOVED) {
#ifdef FIX_CONTROLLERDIR_STALLED
        /* MOD (Jeremy): a creature skipped here -- in practice a stalled tank --
         * used to wipe the controller direction. MSCC does not: SuperCC's list
         * walk does `direction = monster.getDirection()` for every creature that
         * is not itself affected by the controller bug, whether or not it goes on
         * to move (MSCreatureList.tick). So a stalled tank still contributes its
         * FACING to the controller direction.
         *
         * That matters because a Bug / Paramecium / Teeth standing on a beartrap
         * or clone machine takes its move direction straight from
         * controllerdir(). Wiping it leaves such a creature with nothing to move
         * on, so it sits still for a cycle it should have escaped.
         *
         * Found on TomP1Fixed#98 "TRAPS": at ct 644 both tanks ahead of the
         * trapped bug are CS_HASMOVED, the bug inherits NIL instead of the second
         * tank's north, misses its escape, so never presses the red button at
         * (21,15) and the glider clone at (19,22) never happens.
         *
         * Only creatures that are NOT themselves subject to the controller bug
         * contribute -- SuperCC guards this with isAffectedByCB(), which is true
         * exactly for Teeth, Bug and Paramecium. Those three leave the direction
         * untouched rather than setting or clearing it. Setting it for them too
         * broke GAP'sSub#10 "Dimension Hole" (95 clone machines, 36 monsters on
         * them), which is how the corpus caught the over-broad first version. */
        if (cr->id != Teeth && cr->id != Bug && cr->id != Paramecium)
            controllerdir() = cr->dir;
#else
        controllerdir() = NIL;
#endif
        return;
    }
    if (cr->state & (CS_SLIP | CS_SLIDE))
        return;

    floor = floorat(cr->pos);

    pdir = dir = cr->dir;

    if (floor == CloneMachine || floor == Beartrap) {
        switch (cr->id) {
            case Tank:
            case Ball:
            case Glider:
            case Fireball:
            case Walker:
                choices[0] = dir;
                break;
            case Blob:
                choices[0] = dir;
                choices[1] = left(dir);
                choices[2] = back(dir);
                choices[3] = right(dir);
                randomp4(mainprng(), choices);
                break;
            case Bug:
            case Paramecium:
            case Teeth:
                choices[0] = controllerdir();
                cr->tdir = controllerdir();
                return;
                break;
            default:
                warn("Non-creature %02X trying to move", cr->id);
                _assert(!"Unknown creature trying to move");
                break;
        }
    } else {
        switch (cr->id) {
            case Tank:
                choices[0] = dir;
                break;
            case Ball:
                choices[0] = dir;
                choices[1] = back(dir);
                break;
            case Glider:
                choices[0] = dir;
                choices[1] = left(dir);
                choices[2] = right(dir);
                choices[3] = back(dir);
                break;
            case Fireball:
                choices[0] = dir;
                choices[1] = right(dir);
                choices[2] = left(dir);
                choices[3] = back(dir);
                break;
            case Walker:
                choices[0] = dir;
                choices[1] = left(dir);
                choices[2] = back(dir);
                choices[3] = right(dir);
                randomp3(mainprng(), choices + 1);
                break;
            case Blob:
                choices[0] = dir;
                choices[1] = left(dir);
                choices[2] = back(dir);
                choices[3] = right(dir);
                randomp4(mainprng(), choices);
                break;
            case Bug:
                choices[0] = left(dir);
                choices[1] = dir;
                choices[2] = right(dir);
                choices[3] = back(dir);
                break;
            case Paramecium:
                choices[0] = right(dir);
                choices[1] = dir;
                choices[2] = left(dir);
                choices[3] = back(dir);
                break;
            case Teeth:
                y = chippos() / CXGRID - cr->pos / CXGRID;
                x = chippos() % CXGRID - cr->pos % CXGRID;
                n = y < 0 ? NORTH : y > 0 ? SOUTH : NIL;
                if (y < 0)
                    y = -y;
                m = x < 0 ? WEST : x > 0 ? EAST : NIL;
                if (x < 0)
                    x = -x;
                if (x > y) {
                    choices[0] = m;
                    choices[1] = n;
                } else {
                    choices[0] = n;
                    choices[1] = m;
                }
                pdir = choices[2] = choices[0];
                break;
            default:
                warn("Non-creature %02X trying to move", cr->id);
                _assert(!"Unknown creature trying to move");
                break;
        }
    }

    for (n = 0; n < 4 && choices[n] != NIL; ++n) {
        cr->tdir = choices[n];
        controllerdir() = cr->tdir;
        if (canmakemove(cr, choices[n], 0))
            return;
    }

    if (cr->id == Tank) {
        if ((cr->state & CS_RELEASED) ||
            (floor != Beartrap /*&& floor != CloneMachine*/)) /* (c) bug: tank clones should stall */
            cr->state |= CS_HASMOVED;
        cr->tdir = NIL; /* handle stacked tanks */
    }

    if (cr->id != Tank) /* handle stacked tanks */
        cr->tdir = pdir;
}

/* Select a direction for Chip to move towards the goal position.
 */
static int chipmovetogoalpos(void) {
    creature* cr;
    int dir, d1, d2;
    int x, y;

    if (!hasgoal())
        return NIL;
    cr = getchip();
    if (goalpos() == cr->pos) {
        cancelgoal();
        return NIL;
    }

    y = goalpos() / CXGRID - cr->pos / CXGRID;
    x = goalpos() % CXGRID - cr->pos % CXGRID;
    d1 = y < 0 ? NORTH : y > 0 ? SOUTH : NIL;
    if (y < 0)
        y = -y;
    d2 = x < 0 ? WEST : x > 0 ? EAST : NIL;
    if (x < 0)
        x = -x;
    if (x > y) {
        dir = d1;
        d1 = d2;
        d2 = dir;
    }
    if (d1 != NIL && d2 != NIL)
        dir = canmakemove(cr, d1, 0) ? d1 : d2;
    else
        dir = d2 == NIL ? d1 : d2;

    return dir;
}

/* Translate a map position into a packed location relative to Chip.
 */
static int makemouserelative(int abspos) {
    int x, y;

    x = abspos % CXGRID - chippos() % CXGRID;
    y = abspos / CXGRID - chippos() / CXGRID;
    _assert(x >= MOUSERANGEMIN && x <= MOUSERANGEMAX);
    _assert(y >= MOUSERANGEMIN && y <= MOUSERANGEMAX);
    return (y - MOUSERANGEMIN) * MOUSERANGE + (x - MOUSERANGEMIN);
}

/* Unpack a Chip-relative map location.
 */
static int makemouseabsolute(int relpos) {
    int x, y;

    x = relpos % MOUSERANGE + MOUSERANGEMIN;
    y = relpos / MOUSERANGE + MOUSERANGEMIN;
    return chippos() + y * CXGRID + x;
}

/* Determine the direction of Chip's next move. If discard is TRUE,
 * then Chip is not currently permitted to select a direction of
 * movement, and the player's input should not be retained.
 */
static void choosechipmove(creature* cr, int discard) {
    int dir;

    cr->tdir = NIL;

    if (cr->hidden)
        return;

    if (!(currenttime() & 3))
        cr->state &= ~CS_HASMOVED;
    if (cr->state & CS_HASMOVED) {
        if (currentinput() != NIL && hasgoal()) {
            cancelgoal();
            lastmove() = CmdMoveNop;
        }
        return;
    }

    dir = currentinput();
    currentinput() = NIL;
    if (discard || ((cr->state & CS_SLIDE) && dir == cr->dir)) {
        if (currenttime() && !(currenttime() & 1))
            cancelgoal();
        return;
    }

    if (dir >= CmdAbsMouseMoveFirst && dir <= CmdAbsMouseMoveLast) {
        goalpos() = dir - CmdAbsMouseMoveFirst;
        lastmove() = CmdMouseMoveFirst + makemouserelative(goalpos());
        dir = NIL;
    } else if (dir >= CmdMouseMoveFirst && dir <= CmdMouseMoveLast) {
        lastmove() = dir;
        goalpos() = makemouseabsolute(dir - CmdMouseMoveFirst);
        dir = NIL;
    } else {
        if ((dir & (NORTH | SOUTH)) && (dir & (EAST | WEST)))
            dir &= NORTH | SOUTH;
        lastmove() = dir;
    }

    if (dir == NIL && hasgoal() && (currenttime() & 3) == 2)
        dir = chipmovetogoalpos();

    cr->tdir = dir;
}

/* Teleport the given creature instantaneously from the teleport tile
 * at start to another teleport tile (if possible).
 */
static int teleportcreature(creature* cr, int start) {
    maptile* tile;
    int dest, origpos, f;
    int origdir = cr->dir; /* tank push IB onto blue button via teleporter */
    _assert(!cr->hidden);
    if (cr->dir == NIL) {
        warn("%d: directionless creature %02X on teleport at (%d %d)",
             currenttime(), cr->id, cr->pos % CXGRID, cr->pos / CXGRID);
        return NIL;
    }

    origpos = cr->pos;
    dest = start;

    for (;;) {
        --dest;
        if (dest < 0)
            dest += CXGRID * CYGRID;
        if (dest == start)
            break;
        tile = &cellat(dest)->top;
        if (tile->id != Teleport || (tile->state & FS_BROKEN))
            continue;
        cr->pos = dest;
        f = canmakemove(cr, cr->dir, CMM_NOLEAVECHECK | CMM_NOEXPOSEWALLS
                                     | CMM_NODEFERBUTTONS
                                     | CMM_NOFIRECHECK
                                     | CMM_TELEPORTPUSH);
        cr->dir = origdir; /* tank push IB onto blue button via teleporter */
        cr->pos = origpos;
        if (f)
            break;
    }

    return dest;
}

/* Determine the move(s) a creature will make on the current tick.
 */
static void choosemove(creature* cr) {
    if (cr->id == Chip) {
        choosechipmove(cr, cr->state & CS_SLIP);
    } else {
        if (cr->state & CS_SLIP)
            cr->tdir = NIL;
        else
            choosecreaturemove(cr);
    }
}

#ifdef FIX_ROW32_CLONER

/* MOD (Jeremy) -- the MSCC "row 32" cloner glitch. ---------------------------
 *
 * A cloner wired to (x, 32) addresses one cell past the bottom of the map. In
 * the original CHIPS.EXE that lands in the game's own variable block, so the
 * cloner takes its creature template from the bottom layer of row 0 and, on
 * firing, spills MSCC's internal variables back out into that row. Levels were
 * built on it deliberately -- TLFC3's "BLOCKED" / "REENTRY" / "THROUGH THE
 * GATES" are a whole trilogy about it, and each uses the side effect (cancelling
 * Chip's slide, moving him, blanking a cell) as the actual puzzle mechanic.
 *
 * Tile World has always thrown these wirings away, so SuperCC solutions for
 * such levels cannot replay. This reproduces SuperCC's emulation of the glitch
 * (game/MS/MSCreatureList.addClone + MSLevel.resetData). It only ever runs for
 * a cloner address in row 32; nothing else in the engine is touched.
 */

/* The level's chip requirement as of level start, which is the value MSCC has
 * sitting in its variable block. chipsneeded() decreases during play, so the
 * initial figure has to be captured separately.
 */
static int initialchipsneeded = 0;

/* Write one of MSCC's internal variables into the bottom layer of row 0 at the
 * given column, and apply the side effect that comes with it. Mirrors
 * SuperCC's MSLevel.resetData(). Columns hold a little-endian pair of bytes,
 * so even columns carry the low byte and odd columns the high byte.
 *
 * MSCC skips the write when the byte would be 49 -- that is the data-file code
 * for a clone machine, and writing one there would wreck the map it is reading
 * its own template from.
 */
static void resetdata(int x) {
    mapcell* cell;
    creature* chip;
    int shift, value, byte;

    if (x < 0 || x >= CXGRID)
        return;
    cell = cellat(x); /* row 0 */
    chip = getchip();
    shift = (x & 1) ? 8 : 0;

#define    writevar(v)    do {                                   \
        byte = ((v) >> shift) & 0xFF;                            \
        if (byte != 49)                                          \
            cell->bot.id = fileidtotileid(byte);                 \
    } while (0)
#define    writevar_always(v)    \
        (cell->bot.id = fileidtotileid((((v) >> shift) & 0xFF)))

    switch (x) {
        case 0:
        case 1:
            writevar(state->game->number);
            break;
        case 2:
        case 3:
            /* The number of levels in the set. A gamestate has no way back to
             * the series it came from, so this one variable is not reachable
             * here. No level in the corpus wires a cloner to column 2 or 3. */
            warn("row-32 cloner: column %d (levelset length) is not emulated", x);
            break;
        case 4:
        case 5:
            writevar(state->game->time);
            break;
        case 6:
        case 7:
            writevar(initialchipsneeded);
            break;
        case 8:
        case 9:
            writevar_always(chippos() % CXGRID);
            /* MSCC clears the variable it just read out. Chip's tile stays
             * where it was on the map -- that desynchronisation is the glitch,
             * not an oversight. */
            chip->pos = (chippos() / CXGRID) * CXGRID;
            break;
        case 10:
        case 11:
            writevar_always(chippos() / CXGRID);
            chip->pos = chippos() % CXGRID;
            break;
        case 12:
        case 13:
            writevar_always((chip->state & (CS_SLIP | CS_SLIDE)) ? 1 : 0);
            endfloormovement(chip);
            break;
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
            /* Buffered input, title visibility and held keystrokes: SuperCC
             * does not model any of them and simply blanks the cell. */
            cell->bot.id = Empty;
            break;
        case 22:
        case 23: {
            int cause = 0;
            int id = cellat(chippos())->top.id;
            if (iscreature(id) && (creatureid(id) == Chip || creatureid(id) == Swimming_Chip)) {
                if (creatureid(id) == Chip && cellat(chippos())->bot.id == Bomb)
                    cause = 3;
            } else if (id == Drowned_Chip) {
                cause = 1;
            } else if (id == Burned_Chip || id == Bombed_Chip) {
                cause = 2;
            } else if (id == Block_Static || creatureid(id) == Block) {
                cause = 4;
            } else if (iscreature(id)) {
                cause = 5;
            }
            writevar_always(cause);
            /* Reading the death cause also clears it, which revives Chip. */
            chipstatus() = CHIP_OKAY;
            break;
        }
        case 24:
        case 25:
            if (!(chip->state & (CS_SLIP | CS_SLIDE))) {
                cell->bot.id = Empty;
                break;
            }
            if (chip->dir == EAST) {
                writevar_always(1);
                endfloormovement(chip);
            } else if (chip->dir == WEST) {
                writevar_always(-1);
                endfloormovement(chip);
            } else {
                writevar_always(0);
            }
            break;
        case 26:
        case 27:
            if (!(chip->state & (CS_SLIP | CS_SLIDE))) {
                cell->bot.id = Empty;
                break;
            }
            if (chip->dir == SOUTH) {
                writevar_always(1);
                endfloormovement(chip);
            } else if (chip->dir == NORTH) {
                writevar_always(-1);
                endfloormovement(chip);
            } else {
                writevar_always(0);
            }
            break;
        case 28:
        case 29:
            writevar(state->crlistcount);
            break;
        case 30:
            writevar_always(state->crlistcount ? state->crlist[0] % CXGRID : 0);
            break;
        case 31:
            writevar_always(state->crlistcount ? state->crlist[0] / CXGRID : 0);
            break;
    }

#undef    writevar
#undef    writevar_always
}

/* Fire a cloner whose wiring points into row 32.
 */
static void activaterow32cloner(int pos) {
    creature dummy;
    creature* cr;
    mapcell* cell;
    int x, tileid, dir;

    x = pos - CXGRID * CYGRID;

    /* The template is whatever sits in row 0's bottom layer in this column --
     * that is the memory MSCC is really reading. It has to be read before
     * resetdata() runs, because resetdata() overwrites exactly that cell. */
    tileid = cellat(x)->bot.id;
    if (!iscreature(tileid) || creatureid(tileid) == Chip)
        return;
    dir = creaturedirid(tileid);

    memset(&dummy, 0, sizeof dummy);
    dummy.id = creatureid(tileid);
    dummy.dir = dir;
    dummy.pos = pos;

    /* The variable spill only happens for a north-facing template that could
     * actually step onto row 31. CMM_NOLEAVECHECK keeps this a pure question
     * about the destination, matching SuperCC's canEnter(). */
    if (dir == NORTH && canmakemove(&dummy, NORTH, CMM_NOLEAVECHECK | CMM_NOEXPOSEWALLS))
        resetdata(x);

    /* Now the clone itself enters the map from below. Give it a real cell in
     * the virtual row so the ordinary movement machinery -- buttons, teleports,
     * water, traps at the destination -- all runs exactly as it would for any
     * other clone. */
    cell = cellat(pos);
    cell->top.id = tileid;
    cell->top.state = 0;
    cell->bot.id = Empty;
    cell->bot.state = 0;

    if (creatureid(tileid) == Block) {
        cr = lookupblock(pos);
        if (cr->dir != NIL)
            advancecreature(cr, cr->dir);
        /* If it could not get out, drop it: no live creature is ever left
         * sitting off the map. */
        if (!cr->hidden && cr->pos == pos)
            removecreature(cr);
    } else {
        /* SuperCC parks a non-block clone at (x, 32) and lets it walk in on a
         * later tick. Nothing in the corpus does that, and giving a creature a
         * lasting off-map home would need the rest of the engine to expect it,
         * so this stops at the variable spill. */
        warn("row-32 cloner: non-block template %02X is not emulated", tileid);
    }

    cell->top.id = Nothing;
    cell->top.state = 0;
    cell->bot.id = Nothing;
    cell->bot.state = 0;
}

#endif	/* FIX_ROW32_CLONER */

/* Initiate the cloning of a creature.
 */
static void activatecloner(int buttonpos) {
    creature dummy;
    creature* cr;
    int pos, tileid;

    pos = clonerfrombutton(buttonpos);
    if (pos < 0)
        return;
    if (pos >= CXGRID * CYGRID) {
#ifdef FIX_ROW32_CLONER
        if (pos < CXGRID * CYGRID + CXGRID)
            activaterow32cloner(pos);
#endif
        return;
    }
    tileid = cellat(pos)->top.id;
    if (!iscreature(tileid) || creatureid(tileid) == Chip)
        return;
    if (creatureid(tileid) == Block) {
        cr = lookupblock(pos);
        if (cr->dir != NIL)
            advancecreature(cr, cr->dir);
    } else {
        if (cellat(pos)->bot.state & FS_CLONING)
            return;
        memset(&dummy, 0, sizeof dummy);
        dummy.id = creatureid(tileid);
        dummy.dir = creaturedirid(tileid);
        dummy.pos = pos;
#ifdef TRACE_DESYNC
        /* MOD (Jeremy): why a clone was or was not created. */
        if (tracethistick()) {
            int _to = pos, _ok;
            int _y = pos / CXGRID + (dummy.dir == NORTH ? -1 : dummy.dir == SOUTH ? +1 : 0);
            int _x = pos % CXGRID + (dummy.dir == WEST ? -1 : dummy.dir == EAST ? +1 : 0);
            if (_y >= 0 && _y < CYGRID && _x >= 0 && _x < CXGRID)
                _to = _y * CXGRID + _x;
            _ok = canmakemove(&dummy, dummy.dir, CMM_CLONECANTBLOCK);
            fprintf(stderr, "C\t%d\t%d\tcloner@%d,%d\ttile=%02X\tdir=%d\tdest=%d,%d\tdesttop=%02X\tcanmake=%d\n",
                    (int)state->game->number, (int)currenttime(),
                    (int)(pos % CXGRID), (int)(pos / CXGRID), tileid, (int)dummy.dir,
                    (int)(_to % CXGRID), (int)(_to / CXGRID),
                    (int)cellat(_to)->top.id, _ok);
        }
#endif
        if (!canmakemove(&dummy, dummy.dir, CMM_CLONECANTBLOCK))
            return;
        cr = awakencreature(pos);
        if (!cr)
            return;
        cr->state |= CS_CLONING;
        if (cellat(pos)->bot.id == CloneMachine)
            cellat(pos)->bot.state |= FS_CLONING;
    }
}

/* Open a bear trap. Any creature already in the trap is released.
 */
static void springtrap(int buttonpos) {
    creature* cr;
    int pos, id;

    pos = trapfrombutton(buttonpos);
    if (pos < 0)
        return;
    if (pos >= CXGRID * CYGRID) {
        warn("Off-map trap opening attempted: (%d %d)",
             pos % CXGRID, pos / CXGRID);
        return;
    }
    id = cellat(pos)->top.id;
    if (id == Block_Static || (cellat(pos)->bot.state & FS_HASMUTANT)) {
        cr = lookupblock(pos);
        if (cr)
            cr->state |= CS_RELEASED;
    } else if (iscreature(id)) {
        cr = lookupcreature(pos, TRUE);
        if (cr)
            cr->state |= CS_RELEASED;
    }
}

/* Mark all buttons everywhere as having been handled.
 */
static void resetbuttons(void) {
    int pos;

    for (pos = 0; pos < CXGRID * CYGRID; ++pos) {
        cellat(pos)->top.state &= ~FS_BUTTONDOWN;
        cellat(pos)->bot.state &= ~FS_BUTTONDOWN;
    }
}

/* Apply the effects of all deferred button presses, if any.
 */
static void handlebuttons(void) {
    int pos, id;

    for (pos = 0; pos < CXGRID * CYGRID; ++pos) {
        if (cellat(pos)->top.state & FS_BUTTONDOWN) {
            cellat(pos)->top.state &= ~FS_BUTTONDOWN;
            id = cellat(pos)->top.id;
        } else if (cellat(pos)->bot.state & FS_BUTTONDOWN) {
            cellat(pos)->bot.state &= ~FS_BUTTONDOWN;
            id = cellat(pos)->bot.id;
        } else {
            continue;
        }
        switch (id) {
            case Button_Blue:
                addsoundeffect(SND_BUTTON_PUSHED);
                turntanks(NULL);
                break;
            case Button_Green:
                togglewalls();
                break;
            case Button_Red:
                activatecloner(pos);
                addsoundeffect(SND_BUTTON_PUSHED);
                break;
            case Button_Brown:
                springtrap(pos);
                addsoundeffect(SND_BUTTON_PUSHED);
                break;
            default:
                warn("Fooey! Tile %02X is not a button!", id);
                break;
        }
    }
}

/*
 * When something actually moves.
 */

/* Initiate a move by the given creature in the given direction.
 * Return FALSE if the creature cannot initiate the indicated move
 * (side effects may still occur).
 */
static int startmovement(creature* cr, int dir) {
    int floor;
    int odir = cr->dir; /* b2 fix with convergence glitch */

    _assert(dir != NIL);

    floor = cellat(cr->pos)->bot.id;
    if (!canmakemove(cr, dir, 0)) {
        if (cr->id == Chip || (floor != Beartrap && floor != CloneMachine && !(cr->state & CS_SLIP))) {
            if (cr->id != Chip || odir != NIL)
                cr->dir = dir; /* b2 fix */
            updatecreature(cr);
        }
        return FALSE;
    }

    if (floor == Beartrap) {
        _assert(cr->state & CS_RELEASED);
        if (cr->state & CS_MUTANT)
            cellat(cr->pos)->bot.state &= ~FS_HASMUTANT;
    }
    cr->state &= ~CS_RELEASED;

    cr->dir = dir;

    return TRUE;
}

/* Complete the movement of the given creature. Most side effects
 * produced by moving onto a tile occur at this point. This function
 * is also the only place where a creature can be added to the slip
 * list.
 */
static void endmovement(creature* cr, int dir) {
    static int const delta[] = {0, -CXGRID, -1, 0, +CXGRID, 0, 0, 0, +1};
    mapcell* cell;
    maptile* tile;
    int dead = FALSE;
    int wasslipping;
    int oldpos, newpos;
    int id, floor, i;
    int crid; /* Non-existence patch */
    int blockcloning = FALSE; /* Squish patch */

    oldpos = cr->pos;
    newpos = cr->pos + delta[dir];

    cell = cellat(newpos);
    tile = &cell->top;
    floor = tile->id;
    crid = creatureid(cellat(oldpos)->top.id); /* Non-existence patch */
    if (cr->id == Chip) {
        switch (floor) {
            case Empty:
                poptile(newpos);
                break;
            case Water:
                if (!possession(Boots_Water))
                    chipstatus() = CHIP_DROWNED;
                break;
            case Fire:
                if (!possession(Boots_Fire))
                    chipstatus() = CHIP_BURNED;
                break;
            case Dirt:
                poptile(newpos);
                break;
            case BlueWall_Fake:
                poptile(newpos);
                break;
            case PopupWall:
                tile->id = Wall;
                break;
            case Door_Red:
            case Door_Blue:
            case Door_Yellow:
            case Door_Green:
                _assert(possession(floor));
                if (floor != Door_Green)
                    --possession(floor);
                poptile(newpos);
                addsoundeffect(SND_DOOR_OPENED);
                break;
            case Boots_Ice:
            case Boots_Slide:
            case Boots_Fire:
            case Boots_Water:
            case Key_Red:
            case Key_Blue:
            case Key_Yellow:
            case Key_Green:
                if (iscreature(cell->bot.id))
                    chipstatus() = CHIP_COLLIDED;
                ++possession(floor);
                poptile(newpos);
                addsoundeffect(SND_ITEM_COLLECTED);
                break;
            case Burglar:
                possession(Boots_Ice) = 0;
                possession(Boots_Slide) = 0;
                possession(Boots_Fire) = 0;
                possession(Boots_Water) = 0;
                addsoundeffect(SND_BOOTS_STOLEN);
                break;
            case ICChip:
                if (chipsneeded())
                    --chipsneeded();
                poptile(newpos);
                addsoundeffect(SND_IC_COLLECTED);
                break;
            case Socket:
                _assert(chipsneeded() == 0);
                poptile(newpos);
                addsoundeffect(SND_SOCKET_OPENED);
                break;
            case Bomb:
                chipstatus() = CHIP_BOMBED;
                addsoundeffect(SND_BOMB_EXPLODES);
                break;
            default:
                if (iscreature(floor))
                    chipstatus() = CHIP_COLLIDED;
                break;
        }
    } else if (cr->id == Block) {
        switch (floor) {
            case Empty:
                poptile(newpos);
                break;
            case Water:
                tile->id = Dirt;
                dead = TRUE;
                addsoundeffect(SND_WATER_SPLASH);
                break;
            case Bomb:
                tile->id = Empty;
                dead = TRUE;
                addsoundeffect(SND_BOMB_EXPLODES);
                break;
            case Teleport:
                if (!(tile->state & FS_BROKEN))
                    newpos = teleportcreature(cr, newpos);
                break;
        }
        id = cellat(oldpos)->top.id;
        if (iscreature(id) && creatureid(id) == Chip)
            cr->state |= CS_MUTANT;
    } else {
        if (iscreature(cell->top.id)) {
            tile = &cell->bot;
            floor = cell->bot.id;
        }
        switch (floor) {
            case Water:
                if (crid != Glider) /* use crid with Non-existence patch */
                    dead = TRUE;
                break;
            case Fire:
                if (crid != Fireball) /* use crid with Non-existence patch */
                    dead = TRUE;
                break;
            case Bomb:
                cell->top.id = Empty;
                dead = TRUE;
                addsoundeffect(SND_BOMB_EXPLODES);
                break;
            case Teleport:
                if (!(tile->state & FS_BROKEN))
                    newpos = teleportcreature(cr, newpos);
                break;
        }
    }

    if (cellat(oldpos)->bot.id != CloneMachine || cr->id == Chip)
        poptile(oldpos);
    if (dead) {
        removecreature(cr);
        if (cellat(oldpos)->bot.id == CloneMachine)
            cellat(oldpos)->bot.state &= ~FS_CLONING;
        return;
    }

    if (cr->id == Chip && floor == Teleport && !(tile->state & FS_BROKEN)) {
        i = newpos;
        newpos = teleportcreature(cr, newpos);
        if (TRUE || newpos != i) { /* Convergence Patch */
            /* no idea, but Icysanity lvl 1 requires newpos=i to work */
            addsoundeffect(SND_TELEPORTING);
            if (floorat(newpos) == Block_Static) {
                if (lastslipdir() == NIL) {
                    /* // these seem cosmetic/superfluous with new patch
                    cr->dir = NORTH;
                    cellat(newpos)->top.id = crtile(Chip, NORTH);
                    */
                    cr->dir = NIL; /* Convergence Patch */
                    /* floor = Empty; */
                    /* Removed with Convergence Patch, cf Chip on sliplist */
                } else { /* seems ok still, with new Convergence logic */
                    cr->dir = lastslipdir();
                }
            }
        }
    }

    cr->pos = newpos;
    addcreaturetomap(cr);
    cr->pos = oldpos;

    tile = &cell->bot;
    switch (floor) {
        case Button_Blue:
            if (cr->state & CS_DEFERPUSH)
                tile->state |= FS_BUTTONDOWN;
            else
                turntanks(cr);
            addsoundeffect(SND_BUTTON_PUSHED);
            break;
        case Button_Green:
            if (cr->state & CS_DEFERPUSH)
                tile->state |= FS_BUTTONDOWN;
            else
                togglewalls();
            break;
        case Button_Red:
            cr->moving = 1; /* Hack with SGG */
            if (cr->state & CS_DEFERPUSH)
                tile->state |= FS_BUTTONDOWN;
            else
                activatecloner(newpos);
            addsoundeffect(SND_BUTTON_PUSHED);
            cr->moving = 0; /* Hack with SGG */
            break;
        case Button_Brown:
            if (cr->state & CS_DEFERPUSH)
                tile->state |= FS_BUTTONDOWN;
            else
                springtrap(newpos);
            addsoundeffect(SND_BUTTON_PUSHED);
            break;
    }
    cr->pos = newpos;

    if (cellat(oldpos)->bot.id == CloneMachine && cr->id == Block && cellat(oldpos)->top.id != Block_Static)
        blockcloning = TRUE; /* Squish patch */

    if (cellat(oldpos)->bot.id == CloneMachine)
        cellat(oldpos)->bot.state &= ~FS_CLONING;

    if (floor == Beartrap) {
        if (istrapopen(newpos, oldpos))
            cr->state |= CS_RELEASED;
    } else if (cellat(newpos)->bot.id == Beartrap) {
        for (i = 0; i < traplistsize(); ++i) {
            if (traplist()[i].to == newpos) {
                cr->state |= CS_RELEASED;
                break;
            }
        }
    }

    if (cr->id == Chip) {
        if (goalpos() == cr->pos)
            cancelgoal();
        if (chipstatus() != CHIP_OKAY && chipstatus() != CHIP_SQUISHED)
            return; /* CHIP_SQUISHED added with Squish patch */
        if (cell->bot.id == Exit) {
            completed() = TRUE;
            return;
        }
    } else {
        if (iscreature(cell->bot.id)) {
            if (creatureid(cell->bot.id) == Chip
                || creatureid(cell->bot.id) == Swimming_Chip) {
                if (cr->id != Block || !blockcloning) /* Squish patch */
                    chipstatus() = CHIP_COLLIDED;
                else
                    chipstatus() = CHIP_SQUISHED; /* Squish patch */
                return;
            }
        }
    }

    wasslipping = cr->state & (CS_SLIP | CS_SLIDE);

    if (floor == Teleport)
        startfloormovement(cr, floor, NIL); /* NIL for tank reversal patch */
    else if (isice(floor) && (cr->id != Chip || !possession(Boots_Ice)))
        startfloormovement(cr, floor, NIL); /* NIL for tank reversal patch */
    else if (isslide(floor) && (cr->id != Chip || !possession(Boots_Slide)))
        startfloormovement(cr, floor, NIL); /* NIL for tank reversal patch */
    else if (floor == Beartrap && cr->id == Block && wasslipping) {
        startfloormovement(cr, floor, NIL); /* NIL for tank reversal patch */
        if (cr->state & CS_MUTANT)
            cell->bot.state |= FS_HASMUTANT;
    } else { /* changes for MSCC-style sliplist */
        cr->state &= ~(CS_SLIP | CS_SLIDE);
        if (wasslipping && cr->id != Chip) {
            msccslippers--;
            removefromsliplist(cr);
        }
    }
    if (!wasslipping && (cr->state & (CS_SLIP | CS_SLIDE)) && cr->id != Chip)
        controllerdir() = getslipdir(cr);
}

/* Move the given creature in the given direction.
 */
static int advancecreature(creature* cr, int dir) {
    if (dir == NIL)
        return TRUE;

    if (cr->id == Chip)
        chipwait() = 0;

    if (!startmovement(cr, dir)) {
        if (cr->id == Chip) {
            addsoundeffect(SND_CANT_MOVE);
            resetbuttons();
            cancelgoal();
        }
        return FALSE;
    }

    endmovement(cr, dir);
    if (cr->id == Chip)
        handlebuttons();

    return TRUE;
}

/* Return TRUE if gameplay is over.
 */
static int checkforending(void) {
    if (chipstatus() != CHIP_OKAY && chipstatus() != CHIP_SQUISHED) {
        addsoundeffect(SND_CHIP_LOSES); /* Squish patch */
        return -1;
    }
    if (completed()) {
        addsoundeffect(SND_CHIP_WINS);
        return +1;
    }
    return 0;
}

/*
 * Automatic activities.
 */

/* Execute all forced moves for creatures on the slip list. (Note the
 * use of the savedcount variable, which is how slide delay is
 * implemented.)
 */
static void floormovements_of_chip(void) /* split into two */
{
    creature* cr;
    int floor, slipdir, ac;
    int n;

    for (n = 0; n < slipcount; ++n) {
        cr = slips[n].cr;
        if (!(slips[n].cr->state & (CS_SLIP | CS_SLIDE)))
            continue;
        slipdir = slips[n].dir;
        if (slipdir == NIL && cr->id == Chip) /* Convergence Patch */
            cellat(cr->pos)->top.id = crtile(Chip, NORTH);
        if (slipdir == NIL)
            continue;
        if (cr->id != Chip) continue; /* new, non-Chip ignored */
        lastslipdir() = slipdir;
        ac = advancecreature(cr, slipdir); /* useful to have ac */
        if (ac) {
            cr->state &= ~CS_HASMOVED;
        } else {
            int brokentele = 0; /* MOD (Jeremy), see below */
            floor = cellat(cr->pos)->bot.id;
#ifdef FIX_BROKEN_TELEPORT_SLIDE
            /* MOD (Jeremy): a teleport lying under a floor tile is flagged
             * FS_BROKEN when the level loads, and every place that *acts* on a
             * teleport already honours that -- endmovement() and
             * teleportcreature() both skip it. The slide code did not: it kept
             * treating a broken teleport as a working one, so after bouncing
             * Chip off a blocked slide it re-armed his slip state. A slipping
             * Chip has his input discarded by choosechipmove(), so the player's
             * next move was swallowed and Chip slid on until he hit something.
             *
             * SuperCC ends the slide at that point, which is what lets the next
             * keypress land. Noticed on TCCLP#283 "Cool", where the whole level
             * is floor laid over a field of teleports. */
            brokentele = (floor == Teleport)
                      && (cellat(cr->pos)->bot.state & FS_BROKEN);
#endif
            if (isslide(floor)) {
                cr->state &= ~CS_HASMOVED;
            } else if (isice(floor)) {
                slipdir = icewallturn(floor, back(slipdir));
                lastslipdir() = slipdir;
                ac = advancecreature(cr, slipdir); /* again useful with ac */
                if (ac)
                    cr->state &= ~CS_HASMOVED;
            } else if (floor == Teleport || floor == Block_Static) {
                lastslipdir() = slipdir = back(slipdir);
                if (advancecreature(cr, slipdir))
                    cr->state &= ~CS_HASMOVED;
            }
            if (brokentele) {
                endfloormovement(cr);
            } else if (cr->state & (CS_SLIP | CS_SLIDE)) {
                endfloormovement(cr);
                startfloormovement(cr, cellat(cr->pos)->bot.id, NIL); /* 3rd argument with tank reversal patch */
            }
        }
        if (checkforending())
            return;
    }
}

static void floormovements_of_blocks_and_monsters(void) /* split into two */
{
    creature* cr;
    int floor, slipdir, ac;
    int origdir;
    int n;
    int oldmsccslippers;
    int advance = 0;

    for (n = 0; n < slipcount;) {
        oldmsccslippers = msccslippers;
        cr = slips[n].cr;
        if (cr->id == Chip) { /* new splitting */
            n++;
            continue;
        }
        if (advance) {
            advance--;
            n++;
            continue;
        }
        if (!(slips[n].cr->state & (CS_SLIP | CS_SLIDE))) {
            n++;
            continue;
        }
        slipdir = slips[n].dir;
        origdir = slipdir; /* tank reversal patch */
        if (slipdir == NIL) {
            n++;
            continue;
        }
        cr->frame = cr->dir; /* Tank Top Glitch */
        ac = advancecreature(cr, slipdir); /* useful to have ac */
        if (!ac) {
            floor = cellat(cr->pos)->bot.id;
            if (isice(floor)) {
                slipdir = icewallturn(floor, back(slipdir));
                ac = advancecreature(cr, slipdir); /* again useful with ac */
            }
            if (cr->state & (CS_SLIP | CS_SLIDE)) {
                endfloormovement(cr);
                msccslippers--; /* new MSCC accounting */
                startfloormovement(cr, cellat(cr->pos)->bot.id, ac ? NIL : origdir); /* 3rd argument with tank reversal patch */
            }
        }
        if (cr->state & CS_SLIP && ac)
            cr->state |= CS_SLIDE; /* Tank Top Glitch */
        cr->frame = 0; /* Tank Top Glitch */
        if (checkforending())
            return;
        if (msccslippers == oldmsccslippers)
            advance++;
    }
}

static void floormovements(void) /* split version with patch */
{
    floormovements_of_chip();
    updatesliplist(); /* remove deadwood */
    /* TSG stuff, not yet included */
    if (!checkforending()) /* Squish patch (maybe was oversight?) */
        floormovements_of_blocks_and_monsters();
    if (!completed() && chipstatus() == CHIP_SQUISHED)
        chipstatus() = CHIP_SQUISHED_DEATH;
}


static void createclones(void) {
    int n;

    for (n = 0; n < creaturecount; ++n)
        if (creatures[n]->state & CS_CLONING)
            creatures[n]->state &= ~CS_CLONING;
}

#ifndef NDEBUG

/*
 * Debugging functions.
 */

/* Print out a rough image of the level and the list of creatures.
 */
static void dumpmap(void) {
    creature* cr;
    int y, x;

    for (y = 0; y < CXGRID * CYGRID; y += CXGRID) {
        for (x = 0; x < CXGRID; ++x)
            fprintf(stderr, "%c%02x%02X%c",
                    (cellat(y + x)->top.state ? ':' : '.'),
                    cellat(y + x)->top.id,
                    cellat(y + x)->bot.id,
                    (cellat(y + x)->bot.state ? ':' : '.'));
        fputc('\n', stderr);
    }
    fputc('\n', stderr);
    for (y = 0; y < creaturecount; ++y) {
        cr = creatures[y];
        fprintf(stderr, "%02X%c (%d %d)",
                cr->id, "-^<?v?\?\?>"[(int) cr->dir],
                cr->pos % CXGRID, cr->pos / CXGRID);
        for (x = 0; x < slipcount; ++x) {
            if (cr == slips[x].cr) {
                fprintf(stderr, " [%d]", x + 1);
                break;
            }
        }
        fprintf(stderr, "%s%s%s%s%s%s%s%s%s",
                cr->hidden ? " hidden" : "",
                cr->state & CS_RELEASED ? " released" : "",
                cr->state & CS_CLONING ? " cloning" : "",
                cr->state & CS_HASMOVED ? " has-moved" : "",
                cr->state & CS_TURNING ? " turning" : "",
                cr->state & CS_SLIP ? " slipping" : "",
                cr->state & CS_SLIDE ? " sliding" : "",
                cr->state & CS_DEFERPUSH ? " deferred-push" : "",
                cr->state & CS_MUTANT ? " mutant" : "");
        if (x < slipcount)
            fprintf(stderr, " %c", "-^<?v?\?\?>"[(int) slips[x].dir]);
        fputc('\n', stderr);
    }
    for (y = 0; y < blockcount; ++y) {
        cr = blocks[y];
        fprintf(stderr, "block %d: (%d %d) %c", y,
                cr->pos % CXGRID, cr->pos / CXGRID,
                "-^<?v?\?\?>"[(int) cr->dir]);
        for (x = 0; x < slipcount; ++x) {
            if (cr == slips[x].cr) {
                fprintf(stderr, " [%d]", x + 1);
                break;
            }
        }
        fprintf(stderr, "%s%s%s%s%s%s%s%s%s",
                cr->hidden ? " hidden" : "",
                cr->state & CS_RELEASED ? " released" : "",
                cr->state & CS_CLONING ? " cloning" : "",
                cr->state & CS_HASMOVED ? " has-moved" : "",
                cr->state & CS_TURNING ? " turning" : "",
                cr->state & CS_SLIP ? " slipping" : "",
                cr->state & CS_SLIDE ? " sliding" : "",
                cr->state & CS_DEFERPUSH ? " deferred-push" : "",
                cr->state & CS_MUTANT ? " mutant" : "");
        if (x < slipcount)
            fprintf(stderr, " %c", "-^<?v?\?\?>"[(int) slips[x].dir]);
        fputc('\n', stderr);
    }
}

/* Run various sanity checks on the current game state.
 */
static void verifymap(void) {
    creature* cr;
    int n;

    for (n = 0; n < creaturecount; ++n) {
        cr = creatures[n];
        if (cr->id < 0x40 || cr->id >= 0x80)
            warn("%d: Undefined creature %02X at (%d %d)",
                 state->currenttime, cr->id,
                 cr->pos % CXGRID, cr->pos / CXGRID);
        if (!cr->hidden && (cr->pos < 0 || cr->pos >= CXGRID * CYGRID))
            warn("%d: Creature %02X has left the map: (%d %d)",
                 state->currenttime, cr->id,
                 cr->pos % CXGRID, cr->pos / CXGRID);
        if (cr->dir > EAST && (cr->dir != NIL || cr->id != Block))
            warn("%d: Creature %d lacks direction (%d)",
                 state->currenttime, cr->id, cr->dir);
    }
}

#endif

/*
 * Per-tick maintenance functions.
 */

/* Actions and checks that occur at the start of a tick.
 */
static void initialhousekeeping(void) {
    int n;

#ifndef NDEBUG
    if (currentinput() == CmdDebugCmd2) {
        dumpmap();
        exit(0);
    } else if (currentinput() == CmdDebugCmd1) {
        static int mark = 0;
        warn("Mark %d (%d).", ++mark, currenttime());
        currentinput() = NIL;
    }
    verifymap();

    if (currentinput() >= CmdCheatNorth && currentinput() <= CmdCheatICChip) {
        switch (currentinput()) {
            case CmdCheatNorth:
                --yviewoffset();
                break;
            case CmdCheatWest:
                --xviewoffset();
                break;
            case CmdCheatSouth:
                ++yviewoffset();
                break;
            case CmdCheatEast:
                ++xviewoffset();
                break;
            case CmdCheatHome:
                xviewoffset() = yviewoffset() = 0;
                break;
            case CmdCheatKeyRed:
                possession(Key_Red) = 127;
                break;
            case CmdCheatKeyBlue:
                possession(Key_Blue) = 127;
                break;
            case CmdCheatKeyYellow:
                possession(Key_Yellow) = 127;
                break;
            case CmdCheatKeyGreen:
                possession(Key_Green) = 127;
                break;
            case CmdCheatBootsIce:
                possession(Boots_Ice) = 127;
                break;
            case CmdCheatBootsSlide:
                possession(Boots_Slide) = 127;
                break;
            case CmdCheatBootsFire:
                possession(Boots_Fire) = 127;
                break;
            case CmdCheatBootsWater:
                possession(Boots_Water) = 127;
                break;
            case CmdCheatICChip:
                chipsneeded() = chipsneeded() ? 0 : 1;
                break;
        }
        currentinput() = NIL;
        setnosaving();
    }
#endif

    if (currenttime() == 0)
        laststepping = stepping();

    if (!(currenttime() & 3)) {
        for (n = 1; n < creaturecount; ++n) {
            if (creatures[n]->state & CS_TURNING) {
                creatures[n]->state &= ~(CS_TURNING | CS_HASMOVED);
                updatecreature(creatures[n]);
            }
        }
        ++chipwait();
        if (chipwait() > 3) {
            chipwait() = 3;
            if (getchip()->dir != NIL) /* Convergence Glitch patch (a) */
                getchip()->dir = SOUTH;
            updatecreature(getchip());
        }
    }
}

/* Actions and checks that occur at the end of a tick.
 */
static void finalhousekeeping(void) {
    return;
}

static void preparedisplay(void) {
    int pos;

    pos = chippos();
    if (cellat(pos)->bot.id == HintButton)
        showhint();
    else
        hidehint();

    xviewpos() = (pos % CXGRID) * 8 + xviewoffset() * 8;
    yviewpos() = (pos / CYGRID) * 8 + yviewoffset() * 8;
}

/*
 * The functions provided by the gamelogic struct.
 */

/* Initialize the gamestate structure to the state at the beginning of
 * the level, using the data in the associated gamesetup structure.
 * The level map is decoded and assembled, the lists of beartraps,
 * clone machines, and active creatures are drawn up, and other
 * miscellaneous initializations are performed.
 */
static int initgame(gamelogic* logic) {
    static creature dummycrlist;
    mapcell* cell;
    xyconn* xy;
    creature* cr;
    creature* chip;
    int pos, num, n;

    setstate(logic);
    num = state->game->number;
    state->statusflags &= ~SF_BADTILES;
    state->statusflags |= SF_NOANIMATION;

    for (pos = 0, cell = state->map; pos < CXGRID * CYGRID; ++pos, ++cell) {
        if (isfloor(cell->top.id) || creatureid(cell->top.id) == Chip || creatureid(cell->top.id) == Block) {
            if (cell->bot.id == Teleport || cell->bot.id == SwitchWall_Open || cell->bot.id == SwitchWall_Closed) {
                cell->bot.state |= FS_BROKEN;
            }
        }
    }

    chip = allocatecreature();
    chip->pos = 0;
    chip->id = Chip;
    chip->dir = SOUTH;
    addtocreaturelist(chip);
    for (n = 0; n < state->crlistcount; ++n) {
        pos = state->crlist[n];
        if (pos < 0 || pos >= CXGRID * CYGRID) {
            warn("level %d: invalid creature location (%d %d)",
                 num, pos % CXGRID, pos / CXGRID);
            continue;
        }
        cell = cellat(pos);
        if (!iscreature(cell->top.id)) {
            warn("level %d: no creature at location (%d %d)",
                 num, pos % CXGRID, pos / CXGRID);
            continue;
        }
        if (creatureid(cell->top.id) != Block && cell->bot.id != CloneMachine) {
            cr = allocatecreature();
            cr->pos = pos;
            cr->id = creatureid(cell->top.id);
            cr->dir = creaturedirid(cell->top.id);
            addtocreaturelist(cr);
            if (iscreature(cell->bot.id) && creatureid(cell->bot.id) == Chip) {
                chip->pos = pos;
                chip->dir = creaturedirid(cell->bot.id);
            }
        }
        cell->top.state |= FS_MARKER;
    }
    for (pos = 0, cell = state->map; pos < CXGRID * CYGRID; ++pos, ++cell) {
        if (cell->top.state & FS_MARKER) {
            cell->top.state &= ~FS_MARKER;
        } else if (iscreature(cell->top.id) && creatureid(cell->top.id) == Chip) {
            chip->pos = pos;
            chip->dir = creaturedirid(cell->bot.id);
        }
    }

    dummycrlist.id = 0;
    state->creatures = &dummycrlist;
    state->initrndslidedir = NORTH;

    possession(Key_Red) = possession(Key_Blue) = possession(Key_Yellow) = possession(Key_Green) = 0;
    possession(Boots_Ice) = possession(Boots_Slide) = possession(Boots_Fire) = possession(Boots_Water) = 0;

    for (n = traplistsize(), xy = traplist(); n; --n, ++xy) {
        if (xy->to == chippos() || cellat(xy->to)->top.id == Block_Static || istrapbuttondown(xy->from)) {
            springtrap(xy->from);
        }
    }

#ifdef FIX_ROW32_CLONER
    /* MOD (Jeremy): chipsneeded() falls as chips are collected, so the figure
     * MSCC keeps in its variable block has to be noted while it is still the
     * level's own requirement. See resetdata(). */
    initialchipsneeded = chipsneeded();
#endif

    chipwait() = 0;
    completed() = FALSE;
    chipstatus() = CHIP_OKAY;
    controllerdir() = NIL;
    lastslipdir() = NIL;
    stepping() = laststepping;
    cancelgoal();
    xviewoffset() = 0;
    yviewoffset() = 0;

    preparedisplay();
    return TRUE;
}

#ifdef TRACE_DESYNC
/* MOD (Jeremy): TRUE when the current level and tick fall inside the traced
 * window. TW_TRACE_LEVEL picks the level (unset = all); TW_TRACE_TICK_LO/HI
 * bound the ticks (unset = all). Used by the per-creature decision trace. */
static int tracethistick(void) {
    static int only = -2, lo = -2, hi = -2;
    char const* e;

    if (only == -2) {
        e = getenv("TW_TRACE_LEVEL");   only = e ? atoi(e) : -1;
        e = getenv("TW_TRACE_TICK_LO"); lo   = e ? atoi(e) : -1;
        e = getenv("TW_TRACE_TICK_HI"); hi   = e ? atoi(e) : -1;
    }
    if (only >= 0 && (int)state->game->number != only)
        return FALSE;
    if (lo >= 0 && (int)currenttime() < lo)
        return FALSE;
    if (hi >= 0 && (int)currenttime() > hi)
        return FALSE;
    return TRUE;
}
#endif

/* Advance the game state by one tick.
 */
static int advancegame(gamelogic* logic) {
    creature* cr;
    int r = 0;
    int n;

    setstate(logic);

    timeoffset() = -1;
    initialhousekeeping();

    msccslippers = slipcount;
    if (getchip()->state & (CS_SLIP | CS_SLIDE)) /* new accounting */
        msccslippers--;

    if (currenttime() && !(currenttime() & 1)) {
        controllerdir() = NIL;
        for (n = 0; n < creaturecount; ++n) {
            cr = creatures[n];
            if (!cr->hidden && cr->id != Chip && !(currenttime() & 3) && chipstatus() == CHIP_SQUISHED && !completed())
                chipstatus() = CHIP_SQUISHED_DEATH; /* Squish patch */
            if (cr->hidden || (cr->state & CS_CLONING) || cr->id == Chip)
                continue;
#ifdef TRACE_DESYNC
            /* MOD (Jeremy): per-creature decision trace. Shows WHY a creature did
             * or did not move -- its state flags going in, the direction chosen,
             * and whether the move succeeded. Needed because knowing the two
             * engines disagree is not the same as knowing which branch refused
             * the move. Window it with TW_TRACE_TICK_LO / TW_TRACE_TICK_HI. */
            if (tracethistick()) {
                int _bot = cellat(cr->pos)->bot.id;
                fprintf(stderr, "D\t%d\t%d\tn=%d\tcr=%02X@%d,%d\tdir=%d\tstate=%02X%s%s%s\tbot=%02X%s\n",
                        (int)state->game->number, (int)currenttime(), n,
                        cr->id, (int)(cr->pos % CXGRID), (int)(cr->pos / CXGRID),
                        (int)cr->dir, (int)cr->state,
                        (cr->state & CS_RELEASED) ? " REL" : "",
                        (cr->state & CS_HASMOVED) ? " MOVED" : "",
                        (cr->state & (CS_SLIP | CS_SLIDE)) ? " SLIP" : "",
                        _bot, _bot == Beartrap ? " INTRAP" : "");
            }
#endif
            choosemove(cr);
#ifdef TRACE_DESYNC
            if (tracethistick()) {
                int _r;
                fprintf(stderr, "D\t%d\t%d\tn=%d\t  chose tdir=%d",
                        (int)state->game->number, (int)currenttime(), n, (int)cr->tdir);
                if (cr->tdir != NIL) {
                    _r = advancecreature(cr, cr->tdir);
                    fprintf(stderr, "\t-> advance=%d newpos=%d,%d\n", _r,
                            (int)(cr->pos % CXGRID), (int)(cr->pos / CXGRID));
                } else {
                    fprintf(stderr, "\t-> (no move attempted)\n");
                }
                continue;
            }
#endif
            if (cr->tdir != NIL)
                advancecreature(cr, cr->tdir);
        }
        if ((r = checkforending()))
            goto done;
    }

    if (currenttime() && !(currenttime() & 1)) {
        floormovements();
        if ((r = checkforending()))
            goto done;
    }
    updatesliplist();

    timeoffset() = 0;
    if (timelimit()) {
        if (currenttime() >= timelimit()) {
            chipstatus() = CHIP_OUTOFTIME;
            addsoundeffect(SND_TIME_OUT);
            return -1;
        } else if (timelimit() - currenttime() <= 15 * TICKS_PER_SECOND && currenttime() % TICKS_PER_SECOND == 0) {
            addsoundeffect(SND_TIME_LOW);
        }
    }

    cr = getchip();
    choosemove(cr);
    if (cr->tdir != NIL) {
        advancecreature(cr, cr->tdir); /* Squish patch, TW checked this?! */
        if ((r = checkforending())) /* TW checks advancecreature() status */
            goto done; /* guess it's a remnant of Chip starting on exit? */
        cr->state |= CS_HASMOVED;
    }
    updatesliplist();
    createclones();

    done:
    finalhousekeeping();
    preparedisplay();
#ifdef TRACE_DESYNC
    /* MOD (Jeremy): per-tick desync trace. No-op unless built with -DTRACE_DESYNC.
     *
     * Emits one canonical line per engine tick, in the same format SuperCC's
     * TraceLevel.java produces, so the two can be diffed directly to pin the
     * first tick where the engines disagree. Everything on the line is stated in
     * engine-independent terms (grid positions), because the two engines number
     * their tiles and directions differently -- see ..\MSCC Desync RE\.
     *
     * The line carries the LEVEL NUMBER so a single whole-set batch run can be
     * split per level; the earlier trace had to be located by a hand-picked
     * creature signature, which only worked for one level.
     *
     * Blocks are scanned off the map rather than read from blocks[], because
     * that array only holds blocks that have been touched -- an untouched block
     * sitting on the map is absent from it. SuperCC likewise keeps MS blocks out
     * of its monster list, so both sides scan.
     */
    {
        static char const _crletter[] = {
            /* index = (id - Chip) / 4 */
            '@',  /* Chip        0x40 */
            '#',  /* Block       0x44 */
            'K',  /* Tank        0x48 */
            'b',  /* Ball        0x4C */
            'G',  /* Glider      0x50 */
            'F',  /* Fireball    0x54 */
            'W',  /* Walker      0x58 */
            'B',  /* Blob        0x5C */
            'T',  /* Teeth       0x60 */
            'U',  /* Bug         0x64 */
            'P'   /* Paramecium  0x68 */
        };
        int _i;
        creature* _chip = getchip();

        /* Batch mode replays every level in the set, which is megabytes of trace
         * for one level's worth of interest. Set TW_TRACE_LEVEL=<n> to emit only
         * that level. Unset traces everything. */
        {
            static int _only = -2;
            if (_only == -2) {
                char const* _e = getenv("TW_TRACE_LEVEL");
                _only = _e ? atoi(_e) : -1;
            }
            if (_only >= 0 && (int)state->game->number != _only)
                goto _tracedone;
        }

        fprintf(stderr, "T\t%d\t%d\t%lu\tchip=%d,%d,%d\tC:",
                (int)state->game->number, (int)currenttime(),
                (unsigned long)(mainprng()->value),
                (int)(_chip->pos % CXGRID), (int)(_chip->pos / CXGRID),
                (_chip->state & (CS_SLIP | CS_SLIDE)) ? 1 : 0);
        for (_i = 0; _i < creaturecount; ++_i) {
            creature* _c = creatures[_i];
            int _k;
            if (_c->hidden || _c->id == Chip)
                continue;
            _k = ((int)_c->id - Chip) / 4;
            if (_k < 0 || _k >= (int)(sizeof _crletter))
                continue;
            /* Direction as a canonical letter: the two engines encode it
             * differently (bit flags here, enum ordinals in SuperCC), so the
             * trace states it in terms both can agree on. Without this the diff
             * is blind to a creature that is in the right place facing the wrong
             * way -- which is exactly how GAP'sSub#10's real divergence hid. */
            fprintf(stderr, "%c,%d,%d,%c ", _crletter[_k],
                    (int)(_c->pos % CXGRID), (int)(_c->pos / CXGRID),
                    _c->dir == NORTH ? 'N' : _c->dir == WEST ? 'W' :
                    _c->dir == SOUTH ? 'S' : _c->dir == EAST ? 'E' : '-');
        }
        fprintf(stderr, "\tB:");
        for (_i = 0; _i < CXGRID * CYGRID; ++_i) {
            int _id = state->map[_i].top.id;
            if (_id == Block_Static || creatureid(_id) == Block)
                fprintf(stderr, "%d,%d ", _i % CXGRID, _i / CXGRID);
        }
        fprintf(stderr, "\n");
    _tracedone: ;
    }
#endif
    return r;
}

/* Free resources associated with the current game state.
 */
static int endgame(gamelogic* logic) {
    (void) logic;
    resetcreaturepool();
    resetcreaturelist();
    resetblocklist();
    resetsliplist();
    return TRUE;
}

/* Free all allocated resources for this module.
 */
static void shutdown(gamelogic* logic) {
    (void) logic;

    free(creatures);
    creatures = NULL;
    creaturecount = 0;
    creaturesallocated = 0;
    free(blocks);
    blocks = NULL;
    blockcount = 0;
    blocksallocated = 0;
    free(slips);
    slips = NULL;
    slipcount = 0;
    slipsallocated = 0;

    freecreaturepool();
}

/* The exported function: Initialize and return the module's gamelogic
 * structure.
 */
gamelogic* mslogicstartup(void) {
    static gamelogic logic;

    logic.ruleset = Ruleset_MS;
    logic.initgame = initgame;
    logic.advancegame = advancegame;
    logic.endgame = endgame;
    logic.shutdown = shutdown;

    return &logic;
}
