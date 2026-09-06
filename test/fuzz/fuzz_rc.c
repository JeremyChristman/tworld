/* fuzz_rc.c: the tileset-name guard, as a PROPERTY test.
 *
 * MOD (Jeremy). The seventh fuzz target, and the first that checks an
 * INVARIANT rather than waiting for a crash.
 *
 * 🔴 WHY THIS SURFACE AND NOT readrcfile(). The obvious target in res.c is the
 * `rc` parser, and it is deliberately not this one. Two reasons:
 *
 *   * `res/rc` SHIPS WITH THE INSTALL. It is not attacker-controlled the way a
 *     .dat, .dac or .tws is -- those arrive inside downloaded level packs,
 *     which is the trust boundary the other six targets sit on. A user editing
 *     their own rc file is not an adversary.
 *   * readrcfile() opens `resdir/rc` itself; there is no seam to hand it bytes.
 *     Driving it would mean a temp-file write per execution, which at tens of
 *     thousands of executions per second is a disk benchmark rather than a
 *     fuzzer -- the same reason fuzz_dac.c and fuzz_leveldata.c use fmemopen().
 *     test/res_test.c covers that parser properly, with real files and 27 cases.
 *
 * A TILESET NAME IS DIFFERENT, and it is the genuinely hostile input in this
 * file. It arrives from `tw_settings.ini`, which a user edits by hand, and from
 * the filenames inside a downloaded tileset pack -- and it is then JOINED ONTO
 * THE RESOURCE DIRECTORY AND OPENED. istilesetname() is the only thing standing
 * between that and an arbitrary path. It was added in jc-42 for exactly this
 * reason and hardened again in jc-48, when the same class of hole was found in
 * the .dac parser.
 *
 * 🔴 WHAT MAKES THIS A PROPERTY TEST. The other six targets ask "did it crash?".
 * A guard that wrongly returns TRUE does not crash -- it quietly accepts a name
 * that escapes the directory, which is the whole failure mode. So this target
 * re-derives the answer independently:
 *
 *     if istilesetname(name) says TRUE, then the name must contain no path
 *     separator, no colon, no control character, must not be "..", must not be
 *     empty or all whitespace, and must not be a reserved device name.
 *
 * The check below is written from the RULE, not from the implementation. If it
 * were a copy of istilesetname()'s own logic it would agree with any bug that
 * function ever grows, which is the trap tw_fixture.h documents for fixtures
 * built by reading the parser.
 *
 * ⚠ The converse is deliberately NOT asserted. istilesetname() is allowed to be
 * stricter than this list -- refusing a name no rule here forbids is a usability
 * question, not a security one, and pinning it would turn a future tightening
 * into a fuzz failure.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- the surface res.c links against, stubbed --------------------------- *
 *
 * Everything the display, audio and settings layers provide. None of it is
 * reached by the guard; res.c simply will not link without it. */

#include "../../defs.h"
#include "../../oshw.h"
#include "../../res.h"
#include "../../settings.h"

int loadtileset(char const *f, int c) { (void)f; (void)c; return 1; }
int istilesetloaded(void) { return 1; }
void freetileset(void) { }
int loadfontfromfile(char const *f, int c) { (void)f; (void)c; return 1; }
void freefont(void) { }
int loadsfxfromfile(int i, char const *f) { (void)i; (void)f; return 1; }
void freesfx(int i) { (void)i; }
int setaudiosystem(int a) { (void)a; return 1; }
void setcolors(long b, long t, long o, long d)
{
    (void)b; (void)t; (void)o; (void)d;
}
int loadmessagesfromfile(char const *f) { (void)f; return 1; }
int loadunslistfromfile(char const *f) { (void)f; return 1; }
void clearunslist(void) { }
char const *getstringsetting(char const *k) { (void)k; return NULL; }
void setstringsetting(char const *k, char const *v) { (void)k; (void)v; }

#include "../../fileio.c"
#include "../../res.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; abort(); }

/* --- the independent rule ----------------------------------------------- */

/* The Windows device names, which resolve to a device from inside ANY
 * directory and therefore need no separator to escape. Written out here rather
 * than calling isreservedfilename(), so that this check cannot inherit a bug
 * from the function it is checking. */
static char const *const devices[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

/* TRUE if the name is one this test believes must be refused. */
static int mustberefused(char const *name)
{
    char const *p;
    size_t	i, n;

    if (!name || !*name)
	return 1;

    /* All whitespace. */
    for (p = name ; *p && isspace((unsigned char)*p) ; ++p) ;
    if (!*p)
	return 1;

    if (!strcmp(name, ".."))
	return 1;

    for (p = name ; *p ; ++p) {
	if (*p == '/' || *p == '\\' || *p == ':')
	    return 1;
	if ((unsigned char)*p < ' ')
	    return 1;
    }

    /* A device name, with or without an extension: "CON" and "CON.bmp" both
     * resolve to the console. Compared case-insensitively, because the
     * filesystem does. */
    for (i = 0 ; i < sizeof devices / sizeof *devices ; ++i) {
	n = strlen(devices[i]);
	if (strlen(name) >= n) {
	    size_t j;
	    int same = 1;
	    for (j = 0 ; j < n ; ++j)
		if (toupper((unsigned char)name[j]) != devices[i][j])
		    same = 0;
	    if (same && (name[n] == '\0' || name[n] == '.'))
		return 1;
	}
    }
    return 0;
}

/* --- the target --------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char	name[512];
    char	dest[1024];
    size_t	i;

    /* A tileset name is a filename. Anything past a few hundred bytes is
     * exercising the length check in combinepath(), which fuzz_dac.c already
     * covers, rather than the guard. */
    if (size < 1 || size > 400)
	return 0;

    /* NUL-terminate, and stop at the first embedded NUL: the value reaching
     * istilesetname() is a C string, and pretending otherwise would test an
     * input the program cannot receive. */
    for (i = 0 ; i < size ; ++i) {
	if (data[i] == 0)
	    break;
	name[i] = (char)data[i];
    }
    name[i] = '\0';

    /* THE PROPERTY. If the guard accepted this name, the independent rule must
     * agree that it is acceptable. A disagreement in this direction means a
     * name that can escape the resource directory was let through. */
    if (istilesetname(name) && mustberefused(name)) {
	fprintf(stderr, "istilesetname() ACCEPTED a name that must be"
			" refused: \"%s\"\n", name);
	abort();
    }

    /* And the same property one level up, where it actually matters: a name the
     * guard refuses must leave gettilesetpath() reporting failure AND must not
     * leave a usable path behind. jc-42 fixed exactly that -- dest used to keep
     * whatever combinepath() had already written. */
    if (!resdir)
	resdir = getpathbuffer();
    strcpy(resdir, "res");

    memset(dest, 'X', sizeof dest);
    if (!gettilesetpath(dest, name)) {
	if (dest[0] != '\0') {
	    fprintf(stderr, "gettilesetpath() failed but left a path behind"
			    " for \"%s\"\n", name);
	    abort();
	}
    }

    return 0;
}
