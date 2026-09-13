/* tile_test.c: the tileset loader -- the last file that reads a stranger's file.
 *
 * MOD (Jeremy). generic/tile.c is 1,246 lines that ship in every release and had
 * NO test. It was the last surface in this program that takes a file somebody
 * else made and derives structure from it, and this project's most consistent
 * lesson says that is where the defects are.
 *
 * 🔴 IT IS A PARSER, EVEN THOUGH IT LOOKS LIKE DRAWING CODE. loadtileset() takes
 * a user-supplied bitmap and decides what it is from raw dimension arithmetic:
 *
 *     w odd                      -> the large format
 *     w % 13 == 0 && h % 16 == 0 -> the masked format, tiles are w/13 x h/16
 *     w %  7 == 0 && h % 16 == 0 -> the small format,  tiles are w/7  x h/16
 *     anything else              -> refused
 *
 * The name of that file comes from tw_settings.ini (mstileset / lynxtileset) or
 * from res/rc, which is exactly why istilesetname() was added in jc-42 as a
 * security guard. Dropping third-party sheets into res\tilesets is a FEATURE of
 * this fork, so a wrongly-exported image is expected input, not an accident.
 *
 * WHAT IS PINNED HERE, and why each one is not obvious:
 *
 * 1. THE jc-42 NON-POSITIVE GUARD IN settilesize(). This is the important one.
 *    initlargetileset() derives a tile height by scanning and then decrementing
 *    unconditionally, so a malformed image can produce h == 0 -- and 0 % 4 == 0,
 *    so the divisibility test alone waved it through. A zero tile height then
 *    makes the row-advance loop step by 0 and never terminate: the program HANGS
 *    on the GUI thread. The guard was added in jc-42 and nothing tested it.
 *    ⚠ Note what a regression would look like: removing the guard would make the
 *    test HANG rather than fail, so these cases drive settilesize() directly
 *    instead of going through the loader, which fails fast either way.
 *
 * 2. THE FORMAT DISPATCH. Two of the three shipped tile images take different
 *    branches -- res/tiles.bmp is 336x768 (small) and res/atiles.bmp is 1729x874
 *    (large) -- and the maintainer's own collection adds an 832x1024 masked one.
 *    All three paths are live; none was covered.
 *
 * 3. 🔴 THAT THE TAIL OF tileidmap[] IS INERT. The table is declared [NTILES]
 *    = 128 and only 116 entries are written, so the last twelve are zero-filled:
 *    id 0, coordinates 0, and -- the part that matters -- xtransp 0 rather than
 *    -1, which is the value that MEANS "this tile has a transparent image". The
 *    loaders walk all 128 entries, so those twelve really do execute, extract a
 *    tile from (0,0), and assign it to tileptr[0].
 *
 *    That is harmless ONLY because tile id 0 is Nothing, which is never drawn --
 *    Empty is 0x01. If anyone ever renumbers the ids so that 0 becomes a real
 *    tile, those twelve entries would silently overwrite it with the wrong image
 *    and a NULL opaque surface, and getcellimage() blits tileptr[Empty].opaque[0]
 *    with no NULL check at all (tile.c:362, :374). The case below is what would
 *    catch that, and it is the reason this file exists at all: reading the code
 *    made this look like a live bug, and only running it showed why it is not.
 *
 * WHAT THIS FILE DOES NOT COVER: anything that draws. The surface layer here is
 * a real in-memory implementation so the slicing arithmetic runs for real, but
 * no pixel is compared against a reference image -- what a tile LOOKS like is
 * verified by a person looking at the screen, per the release checklist.
 *
 * TESTLANG: c
 *
 * generic/tile.c is compiled only as C by CMake, and reaches err.h's x_alloc,
 * which relies on C's implicit void* conversion. See docs/adr/0004.
 *
 * TESTFLAGS: -Wno-unused-but-set-variable
 *
 * ⚠ WHY THAT SUPPRESSION, precisely -- it is not blanket cover. extractmaskedtile()
 * at generic/tile.c:610 declares `unsigned char *s`, points it into the source
 * surface, and advances it by src->pitch once per row -- and never reads it. The
 * loop beside it reads pixels through TW_PixelAt() instead, so `s` is a leftover
 * from an earlier implementation: dead arithmetic executed for every row of every
 * masked tile, with no effect on the output. It is UPSTREAM's code and untouched
 * here, because deleting it would be a change to shipped source and this file is
 * a test. The shipped build does not warn about it only because CMake compiles
 * with different flags than the test runner does.
 */

#include	"tw_test.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdint.h>

#include	"../gen.h"
#include	"../state.h"

/* 🔴 PULLED IN AHEAD OF THE FAKES, NOT AFTER. TW_Surface and TW_Rect belong to
 * test/stub/oshwbind.h, which generic.h includes; defining them again here --
 * even identically -- gives the translation unit two structs of the same name,
 * and the error it produces says "expected 'TW_Surface *' but argument is of
 * type 'TW_Surface *'", which is not a message anyone enjoys reading. The types
 * come from the stub; only the FUNCTIONS below are this file's own. */
#include	"../generic/generic.h"

/* generic.h declares geng; oshw-qt/oshwbind.cpp defines it in the real build,
 * and that file needs Qt, so the definition lives here for the test. */
genericglobals	geng;

/* tile.c reads this to decide whether to draw the Lynx pedantic-mode variants;
 * it is defined in lxlogic.c, which is a whole engine this file has no business
 * compiling in. FALSE is the shipped default (lxlogic.c:54). */
int		pedanticmode = FALSE;

/* --- a REAL in-memory surface layer -------------------------------------- *
 *
 * Not stubs that return success: tile.c slices, blits and reads pixels, and a
 * loader that "succeeded" against surfaces that record nothing would prove
 * nothing about the arithmetic. These allocate for real, so an out-of-bounds
 * slice reads memory the harness owns and ASan can see it on the Linux job. */

static int	surfaces_live = 0;	/* to catch a loader that leaks on failure */

/* 🔴 THIS NEVER RETURNS NULL FOR A VALID SIZE, and that is the real front end's
 * contract rather than a convenience. oshw-qt/oshwbind.cpp builds surfaces with
 * `new Qt_Surface()`, which throws on exhaustion; it cannot hand back NULL, and
 * tile.c is written to that contract -- extractkeyedtile() dereferences the
 * result immediately. A fake that returns NULL "to test the failure path" makes
 * tile.c look like it is missing a check when it is not. Do not add one. */
static TW_Surface *tw_make(int w, int h)
{
    TW_Surface	       *s;

    if (w <= 0 || h <= 0)
	return NULL;
    s = calloc(1, sizeof *s);
    if (!s)
	return NULL;
    s->w = w;
    s->h = h;
    s->bytesPerPixel = 4;
    s->pitch = w * 4;
    s->pixels = calloc((size_t)w * (size_t)h, 4);
    if (!s->pixels) {
	free(s);
	return NULL;
    }
    ++surfaces_live;
    return s;
}

TW_Surface *TW_NewSurface(int w, int h, int transparent)
{
    (void)transparent;
    return tw_make(w, h);
}

void TW_FreeSurface(TW_Surface *s)
{
    if (s) {
	--surfaces_live;
	free(s->pixels);
	free(s);
    }
}

/* The seam this whole file steers. Rather than authoring bitmaps on disk, the
 * next load is described by these two variables, so any geometry -- including
 * ones no real exporter would produce -- is one assignment away. */
static int	fake_bmp_w = 0;
static int	fake_bmp_h = 0;
static int	fake_bmp_fail = 0;

TW_Surface *TW_LoadBMP(char const *filename, int required)
{
    (void)filename; (void)required;
    if (fake_bmp_fail)
	return NULL;
    return tw_make(fake_bmp_w, fake_bmp_h);
}

#define	TW_MUSTLOCK(s)		(0)
#define	TW_BytesPerPixel(s)	((s)->bytesPerPixel)

static uint32_t *tw_pixelptr(TW_Surface const *s, int x, int y)
{
    return (uint32_t *)((char *)s->pixels + (size_t)y * s->pitch) + x;
}

uint32_t TW_PixelAt(TW_Surface const *s, int x, int y)
{
    /* Bounds-checked deliberately. An out-of-range read is the defect this file
     * exists to notice, and returning a wrong pixel silently would hide it. */
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) {
	CHECK_MSG(0, "TW_PixelAt out of bounds: (%d,%d) on a %dx%d surface",
		  x, y, s->w, s->h);
	return 0;
    }
    return *tw_pixelptr(s, x, y);
}

/* --- the blit recorder ---------------------------------------------------- *
 *
 * MOD (Jeremy, jc-58). _displaymapview() is half this file and had NO coverage:
 * an audit mutated the viewport geometry 75 times and 57 survived. The reason
 * it had none is that "did it draw the right thing" sounds like it needs a
 * rendering oracle nobody wants to write.
 *
 * It does not. Every tile reaches the screen through exactly one call --
 * TW_BlitSurface(src, srcrect, geng.screen, dstrect) -- so recording the
 * DESTINATION RECTANGLES is a complete account of what was drawn and where,
 * with no pixels involved. That turns the geometry into arithmetic a test can
 * assert on: which cells, at which screen offsets, in which order.
 *
 * Off by default so the existing cases, which blit constantly while building
 * the tile table, are unaffected. */
#define	BLITLOG_MAX	4096
static TW_Rect	blitlog[BLITLOG_MAX];
static int	blitcount = 0;
static int	blitrecording = 0;
static int	blitoverflow = 0;

static void blitlog_start(void)
{
    blitcount = 0;
    blitoverflow = 0;
    blitrecording = 1;
}

static void blitlog_stop(void) { blitrecording = 0; }

/* Was a tile drawn with its top-left corner exactly here? */
static int blitlog_has(int x, int y)
{
    int i;
    for (i = 0 ; i < blitcount ; ++i)
	if (blitlog[i].x == x && blitlog[i].y == y)
	    return TRUE;
    return FALSE;
}

int TW_BlitSurface(TW_Surface *src, TW_Rect *srcrect,
		   TW_Surface *dst, TW_Rect *dstrect)
{
    /* 🔴 A NULL here is the failure getcellimage() would produce if the tile
     * table were ever renumbered -- see note 3 in the header. Reported rather
     * than crashed on, so the case that provokes it says something useful. */
    CHECK_MSG(src != NULL, "blit from a NULL surface");
    CHECK_MSG(dst != NULL, "blit to a NULL surface");
    if (blitrecording && dstrect) {
	if (blitcount < BLITLOG_MAX)
	    blitlog[blitcount++] = *dstrect;
	else
	    blitoverflow = 1;
    }
    (void)srcrect;
    return 0;
}

int TW_FillRect(TW_Surface *s, TW_Rect *r, uint32_t color)
{
    (void)r; (void)color;
    CHECK_MSG(s != NULL, "fill of a NULL surface");
    return 0;
}

int TW_SetColorKey(TW_Surface *s, uint32_t key) { (void)s; (void)key; return 0; }
int TW_ResetColorKey(TW_Surface *s) { (void)s; return 0; }
int TW_EnableAlpha(TW_Surface *s) { (void)s; return 0; }
int TW_LockSurface(TW_Surface *s) { (void)s; return 0; }
int TW_UnlockSurface(TW_Surface *s) { (void)s; return 0; }
char const *TW_GetError(void) { return "no error (test surface layer)"; }

uint32_t TW_MapRGB(TW_Surface *s, uint8_t r, uint8_t g, uint8_t b)
{
    (void)s;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
uint32_t TW_MapRGBA(TW_Surface *s, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    (void)s;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
TW_Surface *TW_DisplayFormat(TW_Surface *s)
{
    return s ? tw_make(s->w, s->h) : NULL;
}
TW_Surface *TW_DisplayFormatAlpha(TW_Surface *s)
{
    return s ? tw_make(s->w, s->h) : NULL;
}

/* --- the error surface, stubbed ----------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

static int	warn_count = 0;
static int	errmsg_count = 0;

void warn_(char const *fmt, ...) { (void)fmt; ++warn_count; }
void errmsg_(char const *prefix, char const *fmt, ...)
{
    (void)prefix; (void)fmt; ++errmsg_count;
}
void die_(char const *fmt, ...)
{
    (void)fmt;
    printf("        die() was called -- the loader gave up fatally\n");
    exit(1);
}

/* --- the source under test ---------------------------------------------- */

#include	"../generic/tile.c"

/* --- helpers ------------------------------------------------------------- */

/* Load a tileset of the given geometry and report what the loader made of it.
 * Every case goes through here so that the bookkeeping is never forgotten. */
static int loadgeometry(int w, int h)
{
    fake_bmp_w = w;
    fake_bmp_h = h;
    fake_bmp_fail = 0;
    warn_count = 0;
    errmsg_count = 0;
    return loadtileset("fake.bmp", TRUE);
}

/* --- settilesize(): the guards every format passes through ---------------- */

static void test_tilesize(void)
{
    tw_case("🔴 a NON-POSITIVE tile size is refused (the jc-42 hang guard)");
    /* THE CASE THIS FILE WAS WRITTEN FOR. A zero tile height makes
     * initlargetileset()'s row-advance step by zero and loop forever, hanging
     * the program on the GUI thread -- and 0 % 4 == 0, so the divisibility test
     * below waved it through until jc-42 added this check ahead of it.
     *
     * Driven directly rather than through loadtileset(): if the guard were ever
     * removed, going through the loader would HANG the suite instead of failing
     * it, and a test that hangs teaches nobody anything. */
    warn_count = 0;
    CHECK_INT(settilesize(0, 16), FALSE);
    CHECK_INT(settilesize(16, 0), FALSE);
    CHECK_INT(settilesize(0, 0), FALSE);
    CHECK_INT(settilesize(-4, 16), FALSE);
    CHECK_INT(settilesize(16, -4), FALSE);
    CHECK(warn_count >= 5);

    tw_case("a tile size not divisible by four is refused");
    /* The original guard. Every extraction path assumes quarter-tile steps. */
    warn_count = 0;
    CHECK_INT(settilesize(15, 16), FALSE);
    CHECK_INT(settilesize(16, 15), FALSE);
    CHECK_INT(settilesize(2, 2), FALSE);
    CHECK(warn_count >= 3);

    tw_case("a valid size is accepted and published in geng");
    CHECK_INT(settilesize(48, 48), TRUE);
    CHECK_INT(geng.wtile, 48);
    CHECK_INT(geng.htile, 48);
    CHECK_INT(geng.cptile, 48 * 48);

    tw_case("the refused sizes did not disturb the accepted one");
    /* settilesize() writes geng before it can fail on nothing -- so a refusal
     * after a success must leave the previous size intact, or a bad tileset
     * would corrupt the good one already loaded. */
    CHECK_INT(settilesize(0, 0), FALSE);
    CHECK_INT(geng.wtile, 48);
    CHECK_INT(geng.htile, 48);

    freetileset();
}

/* --- loadtileset(): the format dispatch ----------------------------------- */

static void test_dispatch(void)
{
    tw_case("🔴 the SMALL format: w % 7 == 0 and h % 16 == 0");
    /* res/tiles.bmp, the image this fork actually ships for the MS ruleset, is
     * 336x768 -- so this is the default path, not an exotic one. */
    CHECK_INT(loadgeometry(336, 768), TRUE);
    CHECK_INT(geng.wtile, 336 / 7);
    CHECK_INT(geng.htile, 768 / 16);
    CHECK_INT(istilesetloaded(), TRUE);
    freetileset();

    tw_case("🔴 the MASKED format: w % 13 == 0 and h % 16 == 0");
    /* 832x1024 is "MSCC Black and White Tileset.bmp" from the maintainer's own
     * collection, so this path is live for a real user with real files. */
    CHECK_INT(loadgeometry(832, 1024), TRUE);
    CHECK_INT(geng.wtile, 832 / 13);
    CHECK_INT(geng.htile, 1024 / 16);
    CHECK_INT(istilesetloaded(), TRUE);
    freetileset();

    tw_case("the masked test is taken BEFORE the small one");
    /* 448 is divisible by 7 AND by 13*... no: this pins the ORDER, which is
     * observable only through the resulting tile size. A width divisible by
     * both 7 and 13 (91) must be read as masked, because that branch is first.
     * Without the order, the same file would yield different tile sizes between
     * builds and every extracted image would be wrong. */
    CHECK_INT(loadgeometry(91 * 4, 16 * 4), TRUE);
    CHECK_INT(geng.wtile, (91 * 4) / 13);
    freetileset();

    tw_case("an ODD width goes to the large format, whatever else divides it");
    /* res/atiles.bmp is 1729x874. The odd-width test comes first, so a width
     * that is odd is never considered for the other two formats. */
    fake_bmp_w = 1729;
    fake_bmp_h = 874;
    fake_bmp_fail = 0;
    warn_count = 0;
    errmsg_count = 0;
    /* The large loader reads the image's own layout markers, which a blank
     * surface does not carry, so it is expected to REFUSE this. What is being
     * pinned is that it was DISPATCHED there -- an odd width must never be
     * treated as small or masked. */
    CHECK_INT(loadtileset("fake.bmp", FALSE), FALSE);
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("🔴 dimensions matching no format are REFUSED, with a message");
    /* The user-visible half of jc-41's tileset picker: a wrongly-exported sheet
     * has to say so rather than load something wrong or hang. */
    CHECK_INT(loadgeometry(100, 100), FALSE);
    CHECK(errmsg_count > 0);
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("a height that is not a multiple of 16 is refused");
    CHECK_INT(loadgeometry(336, 700), FALSE);
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("an unreadable file is refused before any geometry is considered");
    fake_bmp_fail = 1;
    errmsg_count = 0;
    CHECK_INT(loadtileset("missing.bmp", TRUE), FALSE);
    CHECK(errmsg_count > 0);
    fake_bmp_fail = 0;
    CHECK_INT(istilesetloaded(), FALSE);
}

/* --- the tile table ------------------------------------------------------- */

static void test_tileidmap(void)
{
    int		n, realentries = 0, zeroentries = 0;
    int		maxxopaque = -1, maxyopaque = -1;
    int		maxxtransp = -1, maxytransp = -1;

    tw_case("the table is walked in full, and its tail is zero-filled");
    /* 116 entries are written into an array declared [NTILES] = 128. That is
     * not a miscount to be fixed -- it is the shape the loaders walk, and the
     * next case is why it is safe. */
    for (n = 0 ; n < NTILES ; ++n) {
	if (tileidmap[n].id == 0 && tileidmap[n].xopaque == 0
				 && tileidmap[n].yopaque == 0
				 && tileidmap[n].xtransp == 0
				 && tileidmap[n].ytransp == 0
				 && tileidmap[n].shape == TILEIMG_IMPLICIT)
	    ++zeroentries;
	else
	    ++realentries;
    }
    CHECK_INT(realentries + zeroentries, NTILES);
    CHECK_INT(realentries, 116);
    CHECK_INT(zeroentries, 12);

    tw_case("🔴 the zero-filled tail is INERT, because tile id 0 is not drawn");
    /* THE INVARIANT THE WHOLE TABLE RESTS ON, and it is a coincidence rather
     * than a design: those twelve entries have xtransp == 0, and 0 is the value
     * that means "this tile HAS a transparent image" (-1 means it does not). So
     * the loaders really do run them, extract a tile from (0,0), and store it --
     * into tileptr[0].
     *
     * That is harmless only because id 0 is Nothing. Empty, the tile actually
     * drawn under everything, is 0x01. Renumber the ids so that 0 becomes a
     * drawable tile and those twelve entries would quietly overwrite it with a
     * NULL opaque surface -- which getcellimage() blits without a NULL check
     * (tile.c:362 and :374). This check is what would catch that. */
    CHECK_INT((int)Empty, 0x01);
    CHECK(Empty != 0);

    tw_case("🔴 every table coordinate is inside the grid it indexes");
    /* An entry pointing outside its sheet reads pixels past the end of the
     * loaded image. The small format is 7 columns wide and the masked format
     * 13, both 16 rows tall; opaque coordinates are used by both, transparent
     * coordinates only by the masked one. */
    for (n = 0 ; n < NTILES ; ++n) {
	if (tileidmap[n].xopaque > maxxopaque) maxxopaque = tileidmap[n].xopaque;
	if (tileidmap[n].yopaque > maxyopaque) maxyopaque = tileidmap[n].yopaque;
	if (tileidmap[n].xtransp > maxxtransp) maxxtransp = tileidmap[n].xtransp;
	if (tileidmap[n].ytransp > maxytransp) maxytransp = tileidmap[n].ytransp;
    }
    CHECK_MSG(maxxopaque <= 6, "an opaque x of %d does not fit the 7-wide small"
			       " sheet", maxxopaque);
    CHECK_MSG(maxyopaque <= 15, "an opaque y of %d does not fit 16 rows",
			       maxyopaque);
    CHECK_MSG(maxxtransp <= 12, "a transparent x of %d does not fit the 13-wide"
			        " masked sheet", maxxtransp);
    CHECK_MSG(maxytransp <= 15, "a transparent y of %d does not fit 16 rows",
			       maxytransp);

    tw_case("every id in the table is a legal tile id");
    for (n = 0 ; n < NTILES ; ++n)
	CHECK_MSG(tileidmap[n].id >= 0 && tileidmap[n].id < NTILES,
		  "entry %d has id %d, outside 0..%d", n, tileidmap[n].id,
		  NTILES - 1);
}

/* --- what a load leaves behind -------------------------------------------- */

static void test_loadedstate(void)
{
    tw_case("🔴 after a small-format load, Empty has a drawable opaque image");
    /* getcellimage() blits tileptr[Empty].opaque[0] with NO null check, twice.
     * If a table change ever left it NULL, every frame would blit from nothing.
     * This is the assertion that turns that from a crash into a red test. */
    CHECK_INT(loadgeometry(336, 768), TRUE);
    CHECK(tileptr[Empty].opaque[0] != NULL);
    CHECK(tileptr[Empty].celcount > 0);
    freetileset();

    tw_case("...and after a masked-format load as well");
    CHECK_INT(loadgeometry(832, 1024), TRUE);
    CHECK(tileptr[Empty].opaque[0] != NULL);
    CHECK(tileptr[Empty].celcount > 0);
    freetileset();

    tw_case("istilesetloaded() answers for the state the loader left");
    CHECK_INT(istilesetloaded(), FALSE);
    CHECK_INT(loadgeometry(336, 768), TRUE);
    CHECK_INT(istilesetloaded(), TRUE);
    freetileset();
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("🔴 all THREE of istilesetloaded()'s clauses are load-bearing");
    {
	/* `geng.wtile > 0 && geng.htile > 0 && tileptr[Empty].celcount != 0`.
	 *
	 * ⚠ GOING THROUGH THE LOADER CANNOT TEST THIS, which is why the case
	 * above does not. loadtileset() sets all three together and freetileset()
	 * clears all three together, so every state the loader can produce agrees
	 * on all three clauses -- and `wtile > 0` relaxed to `>= 0` survives the
	 * whole suite. The only way to tell the clauses apart is to break them
	 * one at a time, which means reaching into geng directly.
	 *
	 * This matters because jc-42 is the reason the third clause exists: a
	 * HALF-BUILT tileset has a valid size and an empty tileptr, and would
	 * die() at getcellimage() on the first tile it could not find. A guard
	 * that answers TRUE for two thirds of itself is the same bug back. */
	int savedw, savedh, savedcels;

	CHECK_INT(loadgeometry(336, 768), TRUE);
	CHECK_INT(istilesetloaded(), TRUE);
	savedw = geng.wtile;
	savedh = geng.htile;
	savedcels = tileptr[Empty].celcount;

	geng.wtile = 0;
	CHECK_MSG(istilesetloaded() == FALSE,
		  "a zero tile WIDTH still reads as a loaded tileset");
	geng.wtile = savedw;

	geng.htile = 0;
	CHECK_MSG(istilesetloaded() == FALSE,
		  "a zero tile HEIGHT still reads as a loaded tileset");
	geng.htile = savedh;

	tileptr[Empty].celcount = 0;
	CHECK_MSG(istilesetloaded() == FALSE,
		  "a tileset with no image for Empty reads as loaded -- this is"
		  " the jc-42 half-built tileset, and getcellimage() dies on it");
	tileptr[Empty].celcount = savedcels;

	CHECK_INT(istilesetloaded(), TRUE);
	freetileset();
    }

    tw_case("🔴 a load refused on DIMENSIONS keeps the tileset already in use");
    /* Not "leaves nothing loaded" -- that was this case's first, wrong guess,
     * and the run corrected it. loadtileset() rejects unusable dimensions before
     * it touches any state, so the tileset you were already playing with
     * survives. That is the behavior you want from a tileset picker: choosing a
     * wrongly-exported sheet says so and changes nothing, rather than blanking
     * the display of a game in progress. */
    CHECK_INT(loadgeometry(336, 768), TRUE);
    CHECK_INT(istilesetloaded(), TRUE);
    CHECK_INT(loadgeometry(100, 100), FALSE);
    CHECK_INT(istilesetloaded(), TRUE);
    CHECK_INT(geng.wtile, 336 / 7);
    CHECK(tileptr[Empty].opaque[0] != NULL);
    freetileset();

    tw_case("⚠ jc-42's free-before-failing paths are DEFENSIVE, not reachable");
    /* Recorded rather than exercised, because trying to exercise it is what
     * showed it cannot be.
     *
     * initsmalltileset() and initmaskedtileset() both guard `if (!s) {
     * freetileset(); return FALSE; }` around each extraction. Reaching that
     * needs extractkeyedtile()/extractopaquetile() to return NULL, and they
     * cannot: they either return a surface or die() on TW_DisplayFormat*
     * failing, and TW_NewSurface -- the allocation before it -- is `new
     * Qt_Surface()` in the shipped front end (oshw-qt/oshwbind.cpp), which
     * THROWS on exhaustion rather than returning NULL.
     *
     * An earlier version of this case starved a fake allocator to force NULL
     * out of TW_NewSurface, and tile.c promptly dereferenced it -- which looked
     * like a missing NULL check until the real implementation was read. It is
     * not: the fake was violating a contract the real one keeps. The guards stay
     * correct to have and the tileset picker is safer for them; nothing here can
     * make them run, so nothing here pretends to. */
    CHECK_INT(loadgeometry(336, 768), TRUE);
    CHECK_INT(istilesetloaded(), TRUE);
    freetileset();
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("freetileset() is safe to call twice, and when nothing is loaded");
    freetileset();
    freetileset();
    CHECK_INT(istilesetloaded(), FALSE);

    tw_case("🔴 no surface is leaked across a load-and-free cycle");
    /* The loaders remember every surface they make so that freetileset() can
     * release them; a path that forgets one leaks a whole tile sheet per
     * tileset change, and the tileset picker makes that a thing users do. */
    {
	int	before, after;

	freetileset();
	before = surfaces_live;
	CHECK_INT(loadgeometry(336, 768), TRUE);
	freetileset();
	after = surfaces_live;
	CHECK_MSG(after == before,
		  "a load-and-free cycle changed the live surface count from"
		  " %d to %d", before, after);
    }

    tw_case("...including a cycle that FAILED partway");
    {
	int	before, after;

	freetileset();
	before = surfaces_live;
	CHECK_INT(loadgeometry(100, 100), FALSE);
	after = surfaces_live;
	CHECK_MSG(after == before,
		  "a refused load changed the live surface count from %d to %d",
		  before, after);
    }
}

/* --- _displaymapview(): the viewport geometry ----------------------------- *
 *
 * MOD (Jeremy, jc-58). THE OTHER HALF OF THIS FILE, and until now the untested
 * half. An audit put the whole tree's worst mutation score here -- 24%, with 57
 * of 75 mutations surviving -- and it was right about where: the 3,270 checks
 * above come from three loops over the tile table, and none of them reaches the
 * map-drawing code at all.
 *
 * What is asserted is the DESTINATION of every blit, via the recorder above.
 * That is enough to pin the view-position clamping, the four loop bounds, the
 * screen-origin arithmetic and the creature pass, without a single pixel.
 *
 * ⚠ TWO GEOMETRY MUTATIONS STILL SURVIVE THESE CASES, AND BOTH ARE EQUIVALENT
 * MUTANTS RATHER THAN GAPS. Recorded so the next person does not spend an
 * afternoon on them:
 *
 *   rmap = ... + NXTILES + 1   an extra column IS walked, but every tile in it
 *                              lands outside displayloc and drawclippedtile()
 *                              returns before blitting (w <= 0). Wasted work,
 *                              no visible or memory effect; `pos` stays inside
 *                              the map because rmap is still <= CXGRID.
 *   y >= CYGRID -> y > CYGRID  y == CYGRID is unreachable. The far clamp holds
 *                              tmap to CYGRID - NYTILES = 23, so bmap is at
 *                              most 32 and the loop condition y < bmap already
 *                              stops at 31. The two forms cannot disagree.
 *
 * The first is a performance nit; the second is only safe BECAUSE of the clamp
 * two cases below assert. Delete that clamp and this bound starts mattering --
 * which is the actual relationship worth knowing. */

static gamestate	viewstate;
static TW_Surface      *viewscreen;

/* ⚠ NOT NULL. _displaymapview() walks `for (cr = state->creatures ; cr->id ; ++cr)`
 * with no null test, so an "empty" list has to be a real one-element array whose
 * single entry is the terminator. Setting the pointer to NULL segfaults on the
 * first read, which is how the first version of this file reported
 * "NO RESULT REPORTED (crashed, or exited before tw_end)". */
static creature		nocreatures[1];

/* Stand up a 32x32 map with the view centered where asked, draw it, and leave
 * the blits in blitlog[]. Returns the display rectangle used. */
static TW_Rect drawview(int xviewpos, int yviewpos)
{
    TW_Rect	displayloc;
    int		i;

    memset(&viewstate, 0, sizeof viewstate);
    for (i = 0 ; i < CXGRID * CYGRID ; ++i) {
	viewstate.map[i].top.id = Wall;
	viewstate.map[i].bot.id = Empty;
    }
    viewstate.xviewpos = (short)xviewpos;
    viewstate.yviewpos = (short)yviewpos;
    viewstate.currenttime = 0;
    viewstate.statusflags = SF_NOANIMATION;
    memset(nocreatures, 0, sizeof nocreatures);
    viewstate.creatures = nocreatures;   /* one entry, id == 0: the terminator */

    displayloc.x = 0;
    displayloc.y = 0;
    displayloc.w = NXTILES * geng.wtile;
    displayloc.h = NYTILES * geng.htile;

    blitlog_start();
    _displaymapview(&viewstate, displayloc);
    blitlog_stop();
    return displayloc;
}

static void test_mapview(void)
{
    TW_Rect	displayloc;
    creature	crlist[4];

    /* A known-good tileset, so geng.wtile/htile are real and getcellimage()
     * has something to return. 9 tiles across at 48px = a 432px view. */
    CHECK_INT(loadgeometry(48 * 13, 48 * 16), TRUE);
    viewscreen = tw_make(NXTILES * geng.wtile, NYTILES * geng.htile);
    CHECK_MSG(viewscreen != NULL, "could not make a destination surface");
    geng.screen = viewscreen;

    tw_case("🔴 the view at the origin draws exactly the top-left 9x9 window");
    {
	int x, y, extra = 0;

	displayloc = drawview(0, 0);
	/* Every cell of the window, at its exact screen offset. */
	for (y = 0 ; y < NYTILES ; ++y)
	    for (x = 0 ; x < NXTILES ; ++x)
		CHECK_MSG(blitlog_has(x * geng.wtile, y * geng.htile),
			  "cell (%d,%d) was not drawn at (%d,%d)",
			  x, y, x * geng.wtile, y * geng.htile);
	/* And nothing outside it. A loop bound that runs one row too far is
	 * invisible to the assertions above; this is what catches it. */
	{
	    int i;
	    for (i = 0 ; i < blitcount ; ++i)
		if (blitlog[i].x < 0 || blitlog[i].y < 0
			|| blitlog[i].x >= NXTILES * geng.wtile
			|| blitlog[i].y >= NYTILES * geng.htile)
		    ++extra;
	}
	CHECK_MSG(extra == 0,
		  "%d tile(s) were drawn outside the %dx%d display rectangle",
		  extra, displayloc.w, displayloc.h);
	CHECK_MSG(!blitoverflow, "the blit recorder overflowed; raise BLITLOG_MAX");
	CHECK_INT(blitcount, NXTILES * NYTILES);
    }

    tw_case("🔴 a NEGATIVE view position clamps to the origin, it does not wrap");
    {
	/* xviewpos/yviewpos are shorts and the arithmetic subtracts half a
	 * screen before clamping, so the intermediate really does go negative
	 * for any view near the left or top edge -- this is the ordinary case,
	 * not a corner one. Without the clamp, lmap/tmap go negative and the
	 * inner `x < 0` tests are all that stop a read at map[-k]. */
	drawview(0, 0);
	CHECK_INT(blitcount, NXTILES * NYTILES);
	CHECK_MSG(blitlog_has(0, 0),
		  "the top-left cell was not drawn from a clamped view");
	CHECK_MSG(geng.mapvieworigin == 0,
		  "mapvieworigin is %d, not 0, for a view clamped to the origin",
		  geng.mapvieworigin);
    }

    tw_case("🔴 a view past the far edge clamps to the last full window");
    {
	/* The other clamp: (CXGRID - NXTILES) * 4. Deleting it walks the draw
	 * loop off the right/bottom of the map, where only the inner
	 * `x >= CXGRID` / `y >= CYGRID` tests stand between it and map[1024+].
	 * viewpos is in half-tiles, so 2*4*CXGRID is far past the edge. */
	drawview(CXGRID * 8, CYGRID * 8);
	CHECK_INT(blitcount, NXTILES * NYTILES);
	/* The window's top-left cell is now (CXGRID-NXTILES, CYGRID-NYTILES)
	 * = (23,23), and it is drawn at screen origin minus the scroll. */
	CHECK_MSG(geng.mapvieworigin
			== (CYGRID - NYTILES) * 4 * CXGRID * 4 + (CXGRID - NXTILES) * 4,
		  "mapvieworigin is %d for a view clamped to the far corner",
		  geng.mapvieworigin);
    }

    /* ⚠ FOUR MUTANTS IN THE CLAMP BLOCK ARE EQUIVALENT. DO NOT SPEND AN
     * AFTERNOON ON THEM. All four clamps have the shape
     *
     *     if (xdisppos < 0)                     xdisppos = 0;
     *     if (xdisppos > (CXGRID - NXTILES) * 4) xdisppos = (CXGRID-NXTILES)*4;
     *
     * so relaxing `<` to `<=` or `>` to `>=` only adds the boundary value
     * itself -- where the assignment stores exactly what the variable already
     * holds. No view position can tell the two forms apart, and the cases above
     * are not missing anything. The clamps themselves ARE tested: deleting one
     * outright, rather than loosening it by one, walks the draw loop off the map
     * and both cases above fail. */

    tw_case("the drawn window MOVES with the view position");
    {
	/* A rejection test that never sees the view move would pass with the
	 * whole scroll calculation deleted. Two different positions must
	 * produce two different origins. */
	int first, second;

	drawview(0, 0);
	first = geng.mapvieworigin;
	drawview(CXGRID * 4, CYGRID * 4);
	second = geng.mapvieworigin;
	CHECK_MSG(first != second,
		  "the view origin did not move between two different view"
		  " positions (both %d) -- the scroll arithmetic is dead", first);
    }

    tw_case("🔴 a creature whose position is off the map is NOT dereferenced");
    {
	/* THE jc-57 GUARD, which had no test and could not have had one before
	 * the recorder existed.
	 *
	 * state->map[cr->pos] is read in the pedanticmode branch, and the only
	 * bounds check in that loop came AFTER it and tested x and y against
	 * the VIEWPORT rather than the array. POS_INVALID is CXGRID*(CYGRID+1)
	 * -- exactly one past the end of map[] -- and it is what readpos()
	 * yields for the malformed creature coordinates that jc-45 and jc-50
	 * were both about.
	 *
	 * ⚠ THE ORACLE IS THE SANITIZE LAYER, AND THAT IS WORTH SAYING OUT
	 * LOUD. There is no behavioral difference to assert: without the guard
	 * the out-of-range creature is read and then skipped by the viewport
	 * test anyway, so it draws nothing either way. What changes is whether
	 * a read happens one past a 1,056-entry array. run-tests.ps1 -Sanitize
	 * is what turns that into a failure; this case's job is to EXECUTE it,
	 * because a sanitizer sees only what a test actually runs. */
	memset(crlist, 0, sizeof crlist);
	crlist[0].id = Ball;
	crlist[0].pos = POS_INVALID;      /* one past the end of map[] */
	crlist[0].hidden = FALSE;
	crlist[1].id = 0;                 /* terminator */

	pedanticmode = 1;
	memset(&viewstate, 0, sizeof viewstate);
	viewstate.currenttime = 0;
	viewstate.statusflags = SF_NOANIMATION;
	viewstate.creatures = crlist;
	displayloc.x = 0; displayloc.y = 0;
	displayloc.w = NXTILES * geng.wtile;
	displayloc.h = NYTILES * geng.htile;
	blitlog_start();
	_displaymapview(&viewstate, displayloc);
	blitlog_stop();
	pedanticmode = 0;

	/* The map cells still draw; the creature must not. */
	CHECK_INT(blitcount, NXTILES * NYTILES);
    }

    tw_case("...and a creature INSIDE the view is still drawn");
    {
	/* The control. A guard that skipped every creature would satisfy the
	 * case above and make the game draw an empty board -- which is a far
	 * louder bug, but nothing here would have caught it. */
	memset(crlist, 0, sizeof crlist);
	crlist[0].id = Ball;
	crlist[0].pos = (short)(4 + CXGRID * 4);   /* well inside the window */
	crlist[0].hidden = FALSE;
	crlist[1].id = 0;

	memset(&viewstate, 0, sizeof viewstate);
	viewstate.currenttime = 0;
	viewstate.statusflags = SF_NOANIMATION;
	viewstate.creatures = crlist;
	displayloc.x = 0; displayloc.y = 0;
	displayloc.w = NXTILES * geng.wtile;
	displayloc.h = NYTILES * geng.htile;
	blitlog_start();
	_displaymapview(&viewstate, displayloc);
	blitlog_stop();

	CHECK_MSG(blitcount == NXTILES * NYTILES + 1,
		  "expected %d map tiles plus one creature, got %d blits",
		  NXTILES * NYTILES, blitcount);
	CHECK_MSG(blitlog_has(4 * geng.wtile, 4 * geng.htile),
		  "the creature at (4,4) was not drawn there");
    }

    geng.screen = NULL;
}

int main(void)
{
    tw_begin("tile_test.c");

    test_tilesize();
    test_dispatch();
    test_tileidmap();
    test_loadedstate();
    test_mapview();

    freetileset();

    /* Raise this when cases are added; never lower it to make a run pass. */
    /* Exact, not a round number with slack: every case here is deterministic
     * and platform-independent -- the large counts come from loops over the
     * 128-entry tile table, which is a compile-time constant. */
    tw_expect_atleast(4885);
    return tw_end();
}
