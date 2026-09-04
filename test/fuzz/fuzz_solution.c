/* fuzz_solution.c: libFuzzer target for the .tws move codec.
 *
 * MOD (Jeremy).
 *
 *     test/run-fuzz.sh solution
 *
 * WHY THIS SURFACE FIRST. expandsolution() is a five-format, bit-packed,
 * variable-length decoder driven by a length field that lives inside the data
 * it is decoding, and the bytes come from a file somebody else made -- players
 * trade solution collections, and dragging a .tws onto the executable is a
 * documented workflow. jc-44 and jc-46 both landed here.
 *
 * Like every test in this suite it compiles the source under test directly
 * (docs/adr/0003), so the fuzzer instruments the real parser and not a copy.
 *
 * 🔴 THE INPUT IS COPIED INTO AN EXACTLY-SIZED HEAP ALLOCATION. libFuzzer hands
 * you a pointer into a buffer that is usually larger than `size`, so a parser
 * that reads a few bytes past the end reads live memory and ASan says nothing.
 * Copying to a malloc of exactly `size` puts a real redzone immediately after
 * the last valid byte, which is the whole reason to run this under ASan.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../fileio.c"
#include "../../solution.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }

/* abort(), not exit(): a fatal error reached from PARSING a file is a finding,
 * and libFuzzer reports an abort as a crash with the input saved. exit() would
 * end the run with a confusing "fuzz target exited" and no reproducer. */
void die_(char const *fmt, ...) { (void)fmt; abort(); }

/* solution.c calls this from createsolutionfilelist(); nothing reached here
 * uses it. Stubbed rather than dragging series.c and six more modules in. */
int findlevelinseries(gameseries const *series, int number, char const *passwd)
{
    (void)series; (void)number; (void)passwd;
    return -1;
}

/* --- the target --------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    gamesetup		game;
    solutioninfo	sol;
    unsigned char      *copy;

    /* 64 KB is far past any real solution record and keeps a pathological
     * length field from turning a memory bug into an out-of-memory report,
     * which would bury the finding it was supposed to surface. */
    if (size < 1 || size > 65536)
	return 0;

    copy = (unsigned char *)malloc(size);
    if (!copy)
	return 0;
    memcpy(copy, data, size);

    memset(&game, 0, sizeof game);
    memset(&sol, 0, sizeof sol);
    game.number = 1;
    game.solutiondata = copy;
    game.solutionsize = (int)size;

    /* sol is zeroed above deliberately: initmovelist() takes its
     * no-allocation branch on a non-zero `allocated` with a garbage `list`,
     * and then writes through it. play.c:147 zeroes for the same reason. */
    expandsolution(&sol, &game);

    /* UNCONDITIONALLY, and mirroring the caller in play.c. expandsolution()
     * has already called initmovelist() by the time it can fail, so freeing
     * only on TRUE leaks 64 bytes per malformed input -- which is exactly the
     * defect LeakSanitizer reported here on this job's first run, in the
     * shipped prepareplayback(). destroymovelist() handles the NULL that the
     * early solutionsize <= 16 exit leaves behind. */
    destroymovelist(&sol.moves);

    free(copy);
    return 0;
}
