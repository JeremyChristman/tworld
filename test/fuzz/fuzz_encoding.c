/* fuzz_encoding.c: libFuzzer target for the level-record RLE decoder.
 *
 * MOD (Jeremy).
 *
 *     test/run-fuzz.sh encoding
 *
 * WHY THIS IS A SEPARATE TARGET FROM fuzz_leveldata, AND NOT REDUNDANT WITH IT.
 *
 * expandleveldata() normally runs only on records readleveldata() has already
 * accepted, and that gate -- which rejects any level without a valid
 * four-character password -- is what guarantees the RLE loops their slack.
 * jc-44's third defect was a guard in this file that reserved two fewer bytes
 * than its neighbor, and it was NOT reachable through a .dat on disk for
 * exactly that reason.
 *
 * 🔴 So fuzzing only through readleveldata() would never reach it. This target
 * calls expandleveldata() DIRECTLY, with no gate, which is the shape of input
 * getenddisplaysetup()'s built-in level already has -- and that one passes the
 * stricter check with ZERO margin (test/encoding_test.c pins it). Anything that
 * ever calls this function without going through the gate inherits whatever
 * this target finds.
 *
 * Findings here are therefore triaged differently: a crash means the decoder
 * is unsafe on its own terms, which matters for the built-in level and for any
 * future caller, even when no .dat can currently produce it.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../encoding.c"

/* --- the error surface, stubbed ---------------------------------------- */

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; abort(); }

/* --- the target --------------------------------------------------------- */

static gamestate	fuzzstate;
static gamesetup	fuzzsetup;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char      *copy;

    if (size < 1 || size > 65536)
	return 0;

    copy = (unsigned char *)malloc(size);
    if (!copy)
	return 0;
    memcpy(copy, data, size);

    memset(&fuzzsetup, 0, sizeof fuzzsetup);
    fuzzsetup.number = 1;
    fuzzsetup.leveldata = copy;
    fuzzsetup.levelsize = (int)size;

    /* The map is zeroed every run rather than once: expandleveldata() writes
     * into it, and carrying state between executions would make a crash depend
     * on the inputs that came before it -- which is the one thing a fuzzer
     * reproducer must never do. */
    memset(fuzzstate.map, 0, sizeof fuzzstate.map);
    fuzzstate.game = &fuzzsetup;
    fuzzstate.ruleset = Ruleset_MS;
    fuzzstate.statusflags = 0;
    /* expandmsdatlevel() resets map, trapcount, clonercount, crlistcount and
     * hinttext itself at entry, but assigns chipsneeded only AFTER three early
     * `goto badlevel` checks -- so on a rejected input it keeps whatever the
     * previous execution left there. Nothing reads it back for a bounds or
     * pointer decision today, so it is not a live defect; it is reset anyway
     * because carried state is how a fuzzer reproducer stops reproducing. */
    fuzzstate.chipsneeded = 0;

    expandleveldata(&fuzzstate);

    free(copy);
    return 0;
}
