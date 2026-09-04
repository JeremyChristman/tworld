/* mkfixture.c: writes a complete, playable level set and solution file.
 *
 * MOD (Jeremy). Used by the end-to-end tests (test/run-e2e.ps1), which need
 * something the shipped level sets cannot give them: a level with a KNOWN
 * correct solution, short enough to state in one line.
 *
 *     mkfixture <outdir>
 *
 * writes
 *
 *     <outdir>/data/fixture.dat            two levels
 *     <outdir>/sets/fixture-ms.dac         pointing at it, MS ruleset
 *     <outdir>/save/fixture-ms.dac.tws     one VALID and one INVALID solution
 *
 * so that `tworld2 -b` over that set must report exactly "Valid solutions: 1"
 * and "Invalid solutions: 1". That single assertion exercises the entire
 * stack -- .dac parsing, .dat loading, level expansion, .tws decoding, and the
 * MS engine replaying a solution move for move -- with no GUI and no
 * third-party content.
 *
 * WHY BOTH VERDICTS. A test that only checks the valid case passes just as
 * happily against an engine that calls everything valid, which is the exact
 * failure a solution verifier must not have.
 *
 * It shares test/tw_fixture.h with the unit tests, so the level-record layout
 * has one definition here. The .dat WRAPPER around those records (signature,
 * level count, per-record size word) and the .tws layout are written here from
 * their format specifications -- see the comments at each.
 *
 * TESTLANG: c
 *
 * TESTFLAGS: -Wno-unused-function
 *
 * ⚠ This file is deliberately NOT named *_test.c, so run-tests.ps1 does not
 * pick it up as a test. It is a tool that the e2e script compiles and runs.
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"tw_fixture.h"

/* The MS-ruleset CC1 data-file signature, little-endian: AC AA 02 00. A Lynx
 * set carries 0x0102AAAC instead, which is why a set's ruleset is not simply a
 * matter of which .dac names it. */
#define	DAT_SIGNATURE_MS	0x0002AAACUL

/* The .tws signature, little-endian: 35 33 9B 99. */
#define	TWS_SIGNATURE		0x999B3335UL

static void put16(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static FILE *opendirfile(char const *dir, char const *sub, char const *name)
{
    char path[1024];
    FILE *f;
    /* The e2e script creates the directories. Failing loudly here rather than
     * writing nowhere is the point: a fixture that silently did not appear
     * would show up later as a mysteriously empty level set. */
    sprintf(path, "%s/%s/%s", dir, sub, name);
    f = fopen(path, "wb");
    if (!f) {
	fprintf(stderr, "mkfixture: cannot write %s\n", path);
	exit(1);
    }
    return f;
}

int main(int argc, char **argv)
{
    fixlevel	lv;
    FILE       *f;
    unsigned char *rec1, *rec2;
    int		size1, size2;
    unsigned char sd[64];
    int		n;

    if (argc != 2) {
	fprintf(stderr, "usage: mkfixture <outdir>\n");
	return 2;
    }

    /* ---- level 1: solvable in exactly one move -----------------------
     * Chip at (5,5) with the exit directly east of him. Password TEST.
     * Untimed, no chips required, so nothing but the single move matters. */
    fix_init(&lv);
    fix_border(&lv);
    lv.number = 1;
    lv.time = 0;
    lv.chips = 0;
    strcpy(lv.name, "ONE STEP EAST");
    strcpy(lv.passwd, "TEST");
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_EXIT);
    rec1 = fix_build(&lv, &size1);

    /* ---- level 2: the same shape, but the exit is out of reach --------
     * A wall sits where level 1 has its exit, so the solution recorded for it
     * below cannot possibly succeed. */
    fix_init(&lv);
    fix_border(&lv);
    lv.number = 2;
    lv.time = 0;
    lv.chips = 0;
    strcpy(lv.name, "NO WAY OUT");
    strcpy(lv.passwd, "WALL");
    fix_settop(&lv, 5, 5, FIX_CHIP_SOUTH);
    fix_settop(&lv, 6, 5, FIX_WALL);
    fix_settop(&lv, 20, 20, FIX_EXIT);
    rec2 = fix_build(&lv, &size2);

    if (!rec1 || !rec2) {
	fprintf(stderr, "mkfixture: out of memory\n");
	return 1;
    }

    /* ---- the .dat wrapper --------------------------------------------
     *   0-3   signature (0x0002AAAC for an MS set)
     *   4-5   level count
     *   6..   each level: a 2-byte record length, then that many bytes
     */
    f = opendirfile(argv[1], "data", "fixture.dat");
    put32(f, DAT_SIGNATURE_MS);
    put16(f, 2);
    put16(f, (unsigned)size1);
    fwrite(rec1, 1, (size_t)size1, f);
    put16(f, (unsigned)size2);
    fwrite(rec2, 1, (size_t)size2, f);
    fclose(f);

    /* ---- the .dac ----------------------------------------------------
     * readconfigfile() accepts only lastlevel, ruleset, usepasswords, fixlynx
     * and fileinsetsdir; ANY other directive is a hard error, not a warning.
     * Keep this minimal. */
    f = opendirfile(argv[1], "sets", "fixture-ms.dac");
    fprintf(f, "file=fixture.dat\nruleset=ms\n");
    fclose(f);

    /* ---- the .tws ----------------------------------------------------
     * Header: signature, ruleset byte, a 16-bit flags field, then a byte
     * counting any extra header bytes (none here).
     *
     * Then one record per solution: a 4-byte length followed by that many
     * bytes, laid out as
     *    0-1  level number
     *    2-5  password, in cleartext
     *     6   flags
     *     7   initial random slide direction (low 3 bits) and stepping (next 3)
     *   8-11  initial PRNG value
     *  12-15  the solution's time in ticks
     *  16..   the move stream
     *
     * The move bytes use the one-byte form of encoding #1: the low two bits
     * are 01, the next three are the direction index (3 = East), and the top
     * three are the tick gap less one -- so 0x0D is "East, immediately".
     *
     * ⚠ THE RECORDED TIME IS 0, AND THAT IS MEASURED, NOT ARBITRARY. It has to
     * equal what the replay actually takes, because checksolution() compares
     * `state.currenttime + state.timeoffset` against it and warns on any
     * mismatch -- "saved game has solution time of N ticks, but replay took M"
     * followed by "reason for difference unknown". Those warnings do not make
     * the solution invalid, but they put noise on stderr that the e2e script
     * would then have to whitelist, and a test that ignores warnings stops
     * noticing new ones. For this one-move level the replay measures 0 ticks;
     * verified by writing 50 first and reading the complaint.
     *
     * It is also not too SMALL to finish: once the move stream runs out,
     * doturn() gives up only when `currenttime + timeoffset - 1` passes this
     * value, and on an untimed level the offset leaves enough room for Chip to
     * complete the step and reach the exit first.
     */
    f = opendirfile(argv[1], "save", "fixture-ms.dac.tws");
    put32(f, TWS_SIGNATURE);
    fputc(2, f);                    /* the ruleset: 1 is Lynx, 2 is MS. A literal, because
                                     * this file is written from the .tws format
                                     * spec and includes no game header. */
    put16(f, 0);                    /* flags */
    fputc(0, f);                    /* no extra header bytes */

    /* Level 1: one step east onto the exit. This one must VERIFY. */
    n = 0;
    sd[n++] = 1; sd[n++] = 0;
    sd[n++] = 'T'; sd[n++] = 'E'; sd[n++] = 'S'; sd[n++] = 'T';
    sd[n++] = 0;                    /* flags */
    sd[n++] = 0;                    /* slide dir 0, stepping 0 */
    sd[n++] = 0; sd[n++] = 0; sd[n++] = 0; sd[n++] = 0;   /* PRNG seed */
    sd[n++] = 0; sd[n++] = 0; sd[n++] = 0; sd[n++] = 0;   /* time, in ticks: see below */
    sd[n++] = 0x0D;                 /* East, at tick 0 */
    put32(f, (unsigned long)n);
    fwrite(sd, 1, (size_t)n, f);

    /* Level 2: the same move, into a wall. This one must NOT verify. */
    n = 0;
    sd[n++] = 2; sd[n++] = 0;
    sd[n++] = 'W'; sd[n++] = 'A'; sd[n++] = 'L'; sd[n++] = 'L';
    sd[n++] = 0;
    sd[n++] = 0;
    sd[n++] = 0; sd[n++] = 0; sd[n++] = 0; sd[n++] = 0;
    sd[n++] = 0; sd[n++] = 0; sd[n++] = 0; sd[n++] = 0;
    sd[n++] = 0x0D;
    put32(f, (unsigned long)n);
    fwrite(sd, 1, (size_t)n, f);
    fclose(f);

    free(rec1);
    free(rec2);
    printf("mkfixture: wrote fixture.dat (2 levels), fixture-ms.dac and fixture-ms.dac.tws under %s\n",
	   argv[1]);
    return 0;
}
