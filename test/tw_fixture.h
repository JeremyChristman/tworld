/* tw_fixture.h: synthesizing level data in memory, for tests.
 *
 * MOD (Jeremy). Builds a CC1 `.dat` LEVEL RECORD -- the bytes that
 * readleveldata() hands to expandleveldata() -- from a 32x32 grid the test
 * fills in by hand.
 *
 * WHY SYNTHESIZE. This repository does ship real level sets (upstream's
 * redistributable CCLP packs; see
 * docs/adr/0005-what-level-data-may-be-committed.md), and the integration tests
 * use them. But a real set cannot provide what a unit test needs: a level with
 * exactly one water tile east of Chip, a cloner wired one cell past the bottom
 * of the map, a map layer whose declared size disagrees with its contents. Those
 * are constructed, not found.
 *
 * 🔴 WRITTEN FROM THE FORMAT SPEC, NOT FROM encoding.c. The layout below is the
 * documented CC1 data-file format. This matters more than it sounds: if this
 * builder were written by reading the parser, a round trip through the two would
 * prove only that they agree with each other, and a misreading in the parser
 * would be faithfully reproduced here and never caught. Same rule SuperCC
 * records in its own ADR 0007.
 *
 * THE LEVEL RECORD, as laid out here:
 *
 *      offset  size  meaning
 *        0      2    level number          (nonzero, or the parser rejects it)
 *        2      2    time limit in seconds (0 = untimed)
 *        4      2    chips required
 *        6      2    map detail level      (must be 0 or 1)
 *        8      2    byte count of the upper layer
 *       10      N    upper layer
 *      10+N     2    byte count of the lower layer
 *        ..     M    lower layer
 *        ..     2    byte count of the optional fields
 *        ..     ..   optional fields: [type][length][data]...
 *
 * Both layers are written UNCOMPRESSED, one byte per cell, 1024 bytes each.
 * The format's run-length escape is 0xFF, and no tile code reaches 0xFF, so an
 * uncompressed stream is unambiguous. Compression is what the format allows, not
 * what it requires, and an uncompressed fixture is one a human can read in a hex
 * dump while debugging. Nothing here emits a run-length-encoded layer yet; a
 * test that needs to exercise the RLE decoder specifically would add one.
 *
 * PASSWORDS. Optional field 6 is XOR-0x99 encoded and readleveldata() REJECTS
 * any level whose decoded password is not exactly four characters. Levels built
 * here always carry one, so the same fixture works for a full .dat later.
 *
 * TILE CODES are the raw data-file codes, not the engine's internal ids -- 0x00
 * floor, 0x01 wall, 0x02 chip, 0x03 water, 0x15 exit, 0x6C..0x6F Chip facing
 * N/W/S/E. The FIX_* names below cover what the tests use.
 */

#ifndef	HEADER_tw_fixture_h_
#define	HEADER_tw_fixture_h_

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#define	FIX_WIDTH	32
#define	FIX_HEIGHT	32
#define	FIX_CELLS	(FIX_WIDTH * FIX_HEIGHT)

/* Raw data-file tile codes. Named from the format, not from encoding.c's
 * internal ids -- see the header note. */
#define	FIX_FLOOR	0x00
#define	FIX_WALL	0x01
#define	FIX_ICCHIP	0x02
#define	FIX_WATER	0x03
#define	FIX_FIRE	0x04
#define	FIX_BLOCK	0x0A
#define	FIX_DIRT	0x0B
#define	FIX_ICE		0x0C
#define	FIX_SLIDE_SOUTH	0x0D
#define	FIX_SLIDE_NORTH	0x12
#define	FIX_SLIDE_EAST	0x13
#define	FIX_SLIDE_WEST	0x14
#define	FIX_EXIT	0x15
#define	FIX_SOCKET	0x22
#define	FIX_BUTTON_RED	0x24
#define	FIX_BUTTON_BROWN 0x27
#define	FIX_TELEPORT	0x29
#define	FIX_BOMB	0x2A
#define	FIX_BEARTRAP	0x2B
#define	FIX_GRAVEL	0x2D
#define	FIX_CLONEMACHINE 0x31
#define	FIX_CHIP_NORTH	0x6C
#define	FIX_CHIP_WEST	0x6D
#define	FIX_CHIP_SOUTH	0x6E
#define	FIX_CHIP_EAST	0x6F

/* A level under construction. */
typedef struct fixlevel {
    unsigned char	top[FIX_CELLS];
    unsigned char	bot[FIX_CELLS];
    int			number;
    int			time;
    int			chips;
    int			detail;
    char		name[64];
    char		passwd[8];
    /* Optional field 4: beartrap wiring, ten bytes per entry
     * (fromx, 0, fromy, 0, tox, 0, toy, 0, 0, 0). */
    unsigned char	traps[10 * 25];
    int			trapcount;
    /* Optional field 5: cloner wiring, eight bytes per entry
     * (fromx, 0, fromy, 0, tox, 0, toy, 0). */
    unsigned char	cloners[8 * 31];
    int			clonercount;
    /* Optional field 10: the creature list, two bytes per entry (x, y). */
    unsigned char	creatures[2 * 128];
    int			creaturecount;
} fixlevel;

/* Start a level: floor everywhere, no creatures, a valid four-character
 * password, and a nonzero level number -- i.e. the smallest thing the parser
 * will accept, so that a test only has to describe what it actually cares
 * about.
 */
static inline void fix_init(fixlevel *lv)
{
    memset(lv, 0, sizeof *lv);
    memset(lv->top, FIX_FLOOR, sizeof lv->top);
    memset(lv->bot, FIX_FLOOR, sizeof lv->bot);
    lv->number = 1;
    lv->time = 0;
    lv->chips = 0;
    lv->detail = 1;
    strcpy(lv->name, "test level");
    strcpy(lv->passwd, "ABCD");
}

/* Range-checked, and it ABORTS rather than returning quietly.
 *
 * These fixtures are used by tests that deliberately probe out-of-range
 * coordinates -- mslogic_test.c hands x=40 to fix_addcreature on purpose -- so
 * sooner or later somebody passes one of those to a setter. Unchecked,
 * fix_settop(lv, 5, 40, ...) writes index 1285, which lands silently in lv->bot
 * and produces a fixture that is quietly not the level the test describes.
 * Failing loudly in a test helper is free; a corrupted fixture costs an hour.
 */
static inline void fix_settop(fixlevel *lv, int x, int y, unsigned char tile)
{
    if (x < 0 || x >= FIX_WIDTH || y < 0 || y >= FIX_HEIGHT) {
	fprintf(stderr, "fix_settop: (%d,%d) is off a %dx%d map\n",
		x, y, FIX_WIDTH, FIX_HEIGHT);
	abort();
    }
    lv->top[y * FIX_WIDTH + x] = tile;
}

static inline void fix_setbot(fixlevel *lv, int x, int y, unsigned char tile)
{
    if (x < 0 || x >= FIX_WIDTH || y < 0 || y >= FIX_HEIGHT) {
	fprintf(stderr, "fix_setbot: (%d,%d) is off a %dx%d map\n",
		x, y, FIX_WIDTH, FIX_HEIGHT);
	abort();
    }
    lv->bot[y * FIX_WIDTH + x] = tile;
}

/* Enclose the map in a wall, which is what every real level does and what stops
 * a test's creature walking off the edge into whatever is next in memory.
 *
 * Two loops, not one. A single loop bounded by FIX_WIDTH sweeps the vertical
 * edges only as far as the map is wide -- correct today because the map is
 * square, and silently leaving two sides open the moment it is not.
 */
static inline void fix_border(fixlevel *lv)
{
    int i;
    for (i = 0 ; i < FIX_WIDTH ; ++i) {
	fix_settop(lv, i, 0, FIX_WALL);
	fix_settop(lv, i, FIX_HEIGHT - 1, FIX_WALL);
    }
    for (i = 0 ; i < FIX_HEIGHT ; ++i) {
	fix_settop(lv, 0, i, FIX_WALL);
	fix_settop(lv, FIX_WIDTH - 1, i, FIX_WALL);
    }
}

static inline void fix_addcreature(fixlevel *lv, int x, int y)
{
    /* 127, not 128. An optional field's length is a SINGLE BYTE, and the
     * creature list is two bytes per entry -- so 128 entries writes
     * (unsigned char)256 == 0, the list silently vanishes, and the 256 orphaned
     * bytes are re-parsed as two dozen bogus optional fields. Measured: at 127
     * the length byte is 254 and crlistcount comes back 127; at 128 the length
     * byte is 0, crlistcount is 0, and the parser emits 24 warnings. A test that
     * filled the list to its documented maximum would have measured an empty one
     * and still passed.
     *
     * The trap (25 x 10 = 250) and cloner (31 x 8 = 248) caps are safe by
     * arithmetic; only this field could overflow. */
    if (lv->creaturecount >= 127)
	return;
    lv->creatures[lv->creaturecount * 2] = (unsigned char)x;
    lv->creatures[lv->creaturecount * 2 + 1] = (unsigned char)y;
    ++lv->creaturecount;
}

static inline void fix_addcloner(fixlevel *lv, int bx, int by, int cx, int cy)
{
    unsigned char *p;
    if (lv->clonercount >= 31)
	return;
    p = lv->cloners + lv->clonercount * 8;
    p[0] = (unsigned char)bx; p[1] = 0;
    p[2] = (unsigned char)by; p[3] = 0;
    p[4] = (unsigned char)cx; p[5] = 0;
    p[6] = (unsigned char)cy; p[7] = 0;
    ++lv->clonercount;
}

static inline void fix_addtrap(fixlevel *lv, int bx, int by, int tx, int ty)
{
    unsigned char *p;
    if (lv->trapcount >= 25)
	return;
    p = lv->traps + lv->trapcount * 10;
    memset(p, 0, 10);
    p[0] = (unsigned char)bx;
    p[2] = (unsigned char)by;
    p[4] = (unsigned char)tx;
    p[6] = (unsigned char)ty;
    ++lv->trapcount;
}

static inline void fix_putword(unsigned char *p, int value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
}

/* Serialize into a freshly allocated level record. The caller owns the buffer
 * and frees it. Returns NULL only if allocation fails.
 */
static inline unsigned char *fix_build(fixlevel const *lv, int *size)
{
    unsigned char      *data;
    int			fieldsize;
    int			total;
    int			pos, n, i;

    /* Optional fields: name (3), password (6), and whichever wiring lists the
     * test asked for. Two bytes of [type][length] each. */
    fieldsize = 2 + (int)strlen(lv->name) + 1;
    fieldsize += 2 + (int)strlen(lv->passwd) + 1;
    if (lv->trapcount)     fieldsize += 2 + lv->trapcount * 10;
    if (lv->clonercount)   fieldsize += 2 + lv->clonercount * 8;
    if (lv->creaturecount) fieldsize += 2 + lv->creaturecount * 2;

    total = 10 + FIX_CELLS + 2 + FIX_CELLS + 2 + fieldsize;
    data = (unsigned char*)malloc(total);
    if (!data)
	return NULL;

    fix_putword(data + 0, lv->number);
    fix_putword(data + 2, lv->time);
    fix_putword(data + 4, lv->chips);
    fix_putword(data + 6, lv->detail);
    fix_putword(data + 8, FIX_CELLS);
    memcpy(data + 10, lv->top, FIX_CELLS);
    pos = 10 + FIX_CELLS;
    fix_putword(data + pos, FIX_CELLS);
    pos += 2;
    memcpy(data + pos, lv->bot, FIX_CELLS);
    pos += FIX_CELLS;
    fix_putword(data + pos, fieldsize);
    pos += 2;

    /* Field 3: the level's name, NUL-terminated -- the terminator is inside the
     * declared length, which is how real files store it. */
    n = (int)strlen(lv->name) + 1;
    data[pos++] = 3;
    data[pos++] = (unsigned char)n;
    memcpy(data + pos, lv->name, n);
    pos += n;

    /* Field 6: the password, XOR-0x99 -- but the TERMINATOR IS A RAW 0x00.
     *
     * ⚠ This used to encode the terminator too, writing 0x00 ^ 0x99 = 0x99. Real
     * CC1 files do not: dumping data/CCLP1.dat shows every field 6 as four
     * encoded bytes followed by a plain 00. The encoded form survived only by
     * accident -- series.c:274's decode loop stops on a ZERO byte, so with 0x99
     * there it ran one extra iteration, wrote 0x99 ^ 0x99 into passwd[4], and
     * strlen still came out 4. A stricter reader, or anything that decodes all
     * `size` bytes, would see a five-character password. mkfixture.c writes real
     * .dat files from this builder, so that would have shipped.
     *
     * Contrast field 3 above, whose NUL genuinely is inside the declared length
     * and is written raw -- which is what made the difference easy to miss.
     */
    n = (int)strlen(lv->passwd) + 1;
    data[pos++] = 6;
    data[pos++] = (unsigned char)n;
    for (i = 0 ; i < n - 1 ; ++i)
	data[pos + i] = (unsigned char)(lv->passwd[i] ^ 0x99);
    data[pos + n - 1] = 0x00;
    pos += n;

    if (lv->trapcount) {
	data[pos++] = 4;
	data[pos++] = (unsigned char)(lv->trapcount * 10);
	memcpy(data + pos, lv->traps, lv->trapcount * 10);
	pos += lv->trapcount * 10;
    }
    if (lv->clonercount) {
	data[pos++] = 5;
	data[pos++] = (unsigned char)(lv->clonercount * 8);
	memcpy(data + pos, lv->cloners, lv->clonercount * 8);
	pos += lv->clonercount * 8;
    }
    if (lv->creaturecount) {
	data[pos++] = 10;
	data[pos++] = (unsigned char)(lv->creaturecount * 2);
	memcpy(data + pos, lv->creatures, lv->creaturecount * 2);
	pos += lv->creaturecount * 2;
    }

    *size = total;
    return data;
}

#endif
