/* fuzz_dac.c: libFuzzer target for the .dac configuration parser.
 *
 * MOD (Jeremy).
 *
 *     test/run-fuzz.sh dac
 *
 * THE ONE PARSER HERE WHOSE INTERESTING INPUTS ARE NOT BYTE PATTERNS.
 *
 * A .dac is line-based text -- `file = CCLP1.dat`, `ruleset = ms` -- so this
 * target is shaped differently from the other three. It is still untrusted
 * input by exactly the same argument: a .dac arrives inside a downloaded level
 * pack, and every level set in a sets\ directory is reached through one.
 *
 * readconfigfile() went untested entirely until jc-48, and writing that test
 * immediately turned up two defects: a path check that could not do its job on
 * Windows (haspathname() looks for a BACKSLASH, so a forward slash walked
 * straight past it and openfileindir() then joined the name onto the data
 * directory), and six ctype calls handed a signed char, which is undefined for
 * every byte >= 0x80. Level packs carry accented characters, so that second
 * one is ordinary input rather than an attack.
 *
 * 🔴 WHAT THIS TARGET IS ESPECIALLY GOOD FOR. The parser's two sscanf calls
 * have NO width specifiers:
 *
 *     sscanf(buf, "file = %[^\n\r]", datfilename)     into char[256]
 *     sscanf(buf, "%[^= \t] = %s", name, value)       into char[256] each
 *
 * They are safe only because filegetline() caps the line at 254 characters first -- a
 * bound in a different function, which is precisely the shape of jc-44's third
 * defect (an encoding.c guard that was safe only because of a check in
 * series.c). ASan on this target is the thing that would notice if that
 * relationship ever broke.
 *
 * fmemopen() rather than a temp file, for the same reason as fuzz_leveldata.c:
 * at tens of thousands of executions per second a disk write per input is a
 * disk benchmark, not a fuzzer.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../fileio.c"
#include "../../series.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; abort(); }

int readsolutions(gameseries *series) { (void)series; return TRUE; }
void clearsolutions(gameseries *series) { (void)series; }
int markunsolvablelevels(gameseries *series) { (void)series; return 0; }
void readextensions(gameseries *series) { (void)series; }

/* --- the target --------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    gameseries	series;
    fileinfo	file;
    char       *copy;

    /* A .dac is a handful of short lines; 64 KB is far past anything real and
     * keeps the fuzzer exploring the parser rather than the line reader. */
    if (size < 1 || size > 65536)
	return 0;

    copy = (char *)malloc(size);
    if (!copy)
	return 0;
    memcpy(copy, data, size);

    /* Zeroed every run: readconfigfile() ORs into series->gsflags and assigns
     * series->ruleset and ->final without clearing them first, so carrying the
     * struct between executions would make a reproducer depend on the inputs
     * that came before it. */
    memset(&series, 0, sizeof series);

    clearfileinfo(&file);
    file.name = (char *)"fuzz";
    file.fp = fmemopen(copy, size, "rb");
    if (!file.fp) {
	free(copy);
	return 0;
    }

    /* The return is a pointer to a static buffer inside readconfigfile(); there
     * is nothing to free, and nothing else in the gameseries is allocated on
     * this path -- readconfigfile() only fills scalars and flags. */
    readconfigfile(&file, &series);

    fclose(file.fp);
    free(copy);
    return 0;
}
