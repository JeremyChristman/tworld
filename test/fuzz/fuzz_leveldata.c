/* fuzz_leveldata.c: libFuzzer target for a .dat level record.
 *
 * MOD (Jeremy).
 *
 *     test/run-fuzz.sh leveldata
 *
 * readleveldata() is the FIRST thing that touches a downloaded level set, and
 * it is where jc-44's second defect lived: a pointer advanced by a
 * file-supplied size and then dereferenced. It also owns the password gate that
 * encoding.c's RLE decoder silently depends on for its slack, so a change here
 * can open a hole in a different file -- which is exactly what jc-44's third
 * defect turned out to be.
 *
 * 🔴 fmemopen() RATHER THAN A TEMPORARY FILE. readleveldata() takes a fileinfo,
 * and the obvious harness writes each input to disk and opens it. At tens of
 * thousands of executions per second that is not a fuzzer, it is a disk
 * benchmark -- and every input would share one filename, so a crash reproducer
 * would race itself. fmemopen() is POSIX, and fuzzing here is Linux-only
 * anyway (mingw-w64 has no libFuzzer), so nothing is given up.
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

/* series.c's neighbors, stubbed exactly as test/series_test.c stubs them. */
int readsolutions(gameseries *series) { (void)series; return TRUE; }
void clearsolutions(gameseries *series) { (void)series; }
int markunsolvablelevels(gameseries *series) { (void)series; return 0; }
void readextensions(gameseries *series) { (void)series; }

/* --- the target --------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    gamesetup	game;
    fileinfo	file;
    char       *copy;

    if (size < 1 || size > 65536)
	return 0;

    /* Exactly-sized, so ASan has a redzone immediately after the last byte the
     * parser is allowed to see. See fuzz_solution.c for why this matters. */
    copy = (char *)malloc(size);
    if (!copy)
	return 0;
    memcpy(copy, data, size);

    memset(&game, 0, sizeof game);
    clearfileinfo(&file);
    file.name = (char *)"fuzz";
    file.fp = fmemopen(copy, size, "rb");
    if (!file.fp) {
	free(copy);
	return 0;
    }

    readleveldata(&file, &game);

    fclose(file.fp);
    file.fp = NULL;
    /* readleveldata() frees leveldata on its own failure paths and NULLs it, so
     * this is the success path only -- but free(NULL) is fine either way and
     * leaving it out leaks on every accepted input, which would make the
     * fuzzer run out of memory and report that instead of a real bug. */
    free(game.leveldata);
    free(copy);
    return 0;
}
