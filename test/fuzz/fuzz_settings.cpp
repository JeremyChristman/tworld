/* fuzz_settings.cpp: the tw_settings.ini reader and writer, as a property.
 *
 * MOD (Jeremy, jc-54). The eighth target, and the second one that is not merely
 * looking for a crash. `.dat`, `.dac`, `.tws` and the password encoding are
 * checked for memory safety; fuzz_rc.c and this file assert that the code MEANS
 * something. See docs/adr/0011.
 *
 * THE PROPERTY: reading is idempotent under writing.
 *
 *     load(render(load(bytes)))  ==  load(bytes)
 *
 * In words: once the program has read a settings file, writing it back out and
 * reading it again must produce exactly the same settings. Both halves of this
 * module have to agree about the format for that to hold, and neither half was
 * tested at all before this release.
 *
 * 🔴 WHY THIS IS THE RIGHT PROPERTY, and not "the bytes come back unchanged".
 * The first save legitimately reorders keys into sections, drops comments and
 * blank lines, and trims whitespace -- that is the file format, not a bug (ADR
 * 0007). Bytes are only stable from the SECOND render onward. Asserting byte
 * equality against the INPUT would fail on every well-formed file. Asserting
 * map equality after a round trip is the claim that actually matters: merely
 * running the game must never quietly change what your settings mean.
 *
 * If it ever fails, the settings file mutates a little more every time the game
 * exits -- the failure mode nobody notices until a level set stops being
 * remembered.
 *
 * WHAT THE PROPERTY DEPENDS ON, spelled out because a future tidy-up could
 * break it silently:
 *
 *   1. rendersettings() ALWAYS emits a [Section] or [Other] header before any
 *      key line, so a key is never on line 1. That matters because the reader
 *      strips a UTF-8 BOM from line 1 only: a key named "\xEF\xBB\xBFfoo" -- a
 *      BOM on a LATER line of the input -- is a perfectly ordinary key, and if
 *      the writer ever put it first, reloading would strip the BOM and rename
 *      it. The header is what makes that unreachable.
 *   2. Duplicate keys collapse on the FIRST load (last wins), so the map going
 *      into the first render already has one entry per key.
 *   3. Every trim the reader performs is idempotent -- trimming twice is the
 *      same as trimming once -- so a value converges after one round.
 *
 * NOT COVERED HERE, deliberately: anything to do with the DISK. This target
 * drives the two seams directly against in-memory streams, so it never opens a
 * file. That keeps it a fuzzer rather than a disk benchmark (the same reasoning
 * as fuzz_leveldata.c's fmemopen), and it keeps byte 0x1A out of the picture --
 * on Windows a text-mode READ treats Ctrl-Z as end of file, so an on-disk round
 * trip legitimately truncates there. The real open/replace path is covered by
 * test/settings_test.c, which uses real files and takes a real lock.
 *
 * INPUT: the raw bytes of a candidate tw_settings.ini. No header, no framing.
 */

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

/* --- the surface settings.cpp links against, stubbed --------------------- *
 *
 * Only the render and parse seams are exercised, so nothing here is reached;
 * the definitions exist because the translation unit has to link. fileio.c is
 * not compiled in -- it does not build as C++ (ADR 0004). */

extern "C" {

char	       *savedir = 0;
char	       *appdir  = 0;

char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;

void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; exit(1); }

char *getpathforfileindir(char const *dir, char const *filename)
{
    (void)dir; (void)filename;
    return 0;
}

int getpathbufferlen(void) { return 1024; }

}

#include "../../settings.cpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
    /* The parsers cap their input at 64 KB; nothing here is quadratic, but a
     * multi-megabyte input buys no coverage and slows the campaign. */
    if (size > 65536)
	return 0;

    std::string const input((char const *)data, size);

    /* 1. Read it. This is the real parser, not a copy of its rules. */
    std::map<std::string, std::string> first;
    {
	std::istringstream in(input);
	parsesettings(in, first);
    }

    /* 2. Write it back. rendersettings() reads the module's own `settings`, so
     *    that is where the map has to go. */
    settings = first;
    std::ostringstream rendered;
    rendersettings(rendered);

    /* 3. Read the result. */
    std::map<std::string, std::string> second;
    {
	std::istringstream in(rendered.str());
	parsesettings(in, second);
    }

    /* 🔴 THE PROPERTY. A difference here is a real defect in one of the two
     * halves, so abort rather than return -- libFuzzer records the input, and
     * the reproducer is the whole value of the run. */
    if (first != second)
	abort();

    /* ⚠ THERE IS DELIBERATELY NO SECOND "THE BYTES ARE STABLE" CHECK HERE.
     *
     * An earlier version rendered `second` and compared it to `rendered`. That
     * assertion is UNREACHABLE, and paying a full render per execution for it
     * slows the campaign for nothing: rendersettings() is a pure function of the
     * settings map -- no other state, its bookkeeping is local, and std::map
     * iteration order is fixed by the keys -- so two equal maps cannot render
     * differently, and the check above has already established that they are
     * equal. Byte stability follows from map equality; it is not independent of
     * it.
     *
     * The genuinely uncovered half is the one an in-memory target cannot reach:
     * the same round trip through a real text-mode FILE, where the CRLF
     * translation applies. test/settings_test.c does that with real files. */
    return 0;
}
