/* sdlsfx_test.cpp: the sound loader's guards against a hostile WAV file.
 *
 * MOD (Jeremy, jc-59). Sound packs made every WAV file something a stranger can
 * hand you, like a level set, and the jc-59 review turned up three files that
 * each did real damage through loadsfxfromfile() -- all three measured before
 * they were fixed:
 *
 *   - a WAV claiming a 1 Hz sample rate: 6,088 bytes of it asked SDL's
 *     converter for a 2 GB buffer and the game died reading past its end;
 *   - a WAV whose audio converts to nothing: as a LOOPING sound it hung the
 *     audio thread with the device lock held, freezing the game for good;
 *   - a 144-byte WAV declaring four gigabytes of audio: SDL committed 4 GB of
 *     memory before any length could be checked.
 *
 * Each case below is one of those files, built byte by byte, and each asserts
 * the loader REFUSES it -- plus two ordinary files it must still accept, so the
 * guards cannot pass by refusing everything. Before the fixes, the first case
 * crashed this program and the second would have been caught by the runner's
 * deadline; either way the run fails, which is the point.
 *
 * It lives in the Qt layer only because this runner already knows how to link
 * SDL through pkg-config. Nothing here uses Qt. The audio device is SDL's
 * "dummy" driver, so it needs no sound card and behaves the same on a CI
 * runner as on a desk.
 *
 * TESTSRC: ../../oshw-sdl/sdlsfx.c
 * TESTPKG: sdl2
 */

#include	"../tw_test.h"

#include	<SDL.h>
#include	<cstdio>
#include	<cstring>
#include	<string>

extern "C" {
#include	"../../defs.h"
#include	"../../oshw-sdl/sdlsfx.h"
}

/* --- what sdlsfx.c links against, stubbed --------------------------------- */

extern "C" {
char const     *err_cfile_ = 0;
unsigned long	err_lineno_ = 0;
void warn_(char const *fmt, ...) { (void)fmt; }
void errmsg_(char const *prefix, char const *fmt, ...) { (void)prefix; (void)fmt; }
void die_(char const *fmt, ...) { (void)fmt; std::abort(); }
int setdisplaymsg(char const *msg, int msecs, int bold)
{
    (void)msg; (void)msecs; (void)bold;
    return TRUE;
}
void setintsetting(char const *name, int val) { (void)name; (void)val; }
}

/* --- building WAV files ---------------------------------------------------- */

static std::string scratch(char const *name)
{
    return std::string("tw_sdlsfx_test_") + name;
}

static void put32(std::FILE *f, unsigned v)
{
    unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8),
			   (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    std::fwrite(b, 1, 4, f);
}

static void put16(std::FILE *f, unsigned v)
{
    unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
    std::fwrite(b, 1, 2, f);
}

/* A PCM WAV: rate, bits and channels in its header, `declared` bytes claimed
 * by its data chunk, and `actual` bytes of audio really written. */
static std::string wav(char const *name, unsigned rate, unsigned bits,
		       unsigned channels, unsigned declared, unsigned actual)
{
    std::string const path = scratch(name);
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
	return path;
    unsigned const align = channels * bits / 8;
    std::fwrite("RIFF", 1, 4, f);
    put32(f, 36 + declared);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);			/* PCM */
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * align);
    put16(f, align);
    put16(f, bits);
    std::fwrite("data", 1, 4, f);
    put32(f, declared);
    for (unsigned i = 0 ; i < actual ; ++i)
	std::fputc(0x80 + (int)(i & 7), f);
    std::fclose(f);
    return path;
}

/* --- the cases -------------------------------------------------------------- */

static void test_accepts(void)
{
    tw_case("an ordinary WAV loads");
    std::string const ok = wav("ok.wav", 22050, 16, 1, 4410, 4410);
    CHECK_INT(loadsfxfromfile(SND_IC_COLLECTED, ok.c_str()), TRUE);

    tw_case("...and so does one cut a little short, which SDL has always tolerated");
    /* The size guard must refuse a chunk claiming more than the whole FILE, and
     * nothing gentler: real files that were truncated in transit still play. */
    std::string const shortish = wav("short.wav", 22050, 16, 1, 8820, 4410);
    CHECK_INT(loadsfxfromfile(SND_WATER_SPLASH, shortish.c_str()), TRUE);

    tw_case("an 8-bit 11 kHz WAV -- the MSCC format -- loads");
    std::string const mscc = wav("mscc.wav", 11025, 8, 1, 2000, 2000);
    CHECK_INT(loadsfxfromfile(SND_CHIP_WINS, mscc.c_str()), TRUE);
}

static void test_refuses(void)
{
    tw_case("🔴 a 1 Hz WAV is refused, not converted into a 2 GB buffer");
    /* 6,088 bytes at 1 Hz: len_mult 352,800. The first fix bounded the product
     * at 4 GB, which let this through to a crash inside SDL. */
    std::string const slow = wav("1hz.wav", 1, 8, 1, 6088, 6088);
    CHECK_INT(loadsfxfromfile(SND_BOMB_EXPLODES, slow.c_str()), FALSE);

    tw_case("🔴 a WAV whose audio converts to nothing is refused");
    /* As a looping sound, a zero length hung the audio callback with the device
     * lock held. One byte of 16-bit audio is less than a sample. */
    std::string const empty = wav("empty.wav", 22050, 16, 1, 0, 0);
    CHECK_INT(loadsfxfromfile(SND_SKATING_FORWARD, empty.c_str()), FALSE);
    std::string const half = wav("half.wav", 22050, 16, 1, 1, 1);
    CHECK_INT(loadsfxfromfile(SND_SKATING_FORWARD, half.c_str()), FALSE);

    tw_case("🔴 a tiny WAV declaring four gigabytes is refused BEFORE SDL reads it");
    std::string const liar = wav("liar.wav", 22050, 16, 1, 0xFFFFFF00u, 100);
    CHECK_INT(loadsfxfromfile(SND_TELEPORTING, liar.c_str()), FALSE);

    tw_case("a file that is not a WAV at all is refused");
    std::string const text = scratch("text.wav");
    if (std::FILE *f = std::fopen(text.c_str(), "wb")) {
	std::fputs("this is not a sound\n", f);
	std::fclose(f);
    }
    CHECK_INT(loadsfxfromfile(SND_CANT_MOVE, text.c_str()), FALSE);
    CHECK_INT(loadsfxfromfile(SND_CANT_MOVE, scratch("absent.wav").c_str()), FALSE);
}

/* A WAV of fmt, `before` empty LIST chunks, 4410 bytes of data, then
 * optionally a trailing chunk that DECLARES far more than a sound can be. */
static std::string chunky(char const *name, unsigned before, bool trailer)
{
    std::string const path = scratch(name);
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
	return path;
    std::fwrite("RIFF", 1, 4, f);
    put32(f, 4 + 24 + before * 8 + 8 + 4410 + (trailer ? 12 : 0));
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 22050); put32(f, 44100);
    put16(f, 2); put16(f, 16);
    for (unsigned i = 0 ; i < before ; ++i) {
	std::fwrite("LIST", 1, 4, f);
	put32(f, 0);
    }
    std::fwrite("data", 1, 4, f);
    put32(f, 4410);
    for (unsigned i = 0 ; i < 4410 ; ++i)
	std::fputc(0, f);
    if (trailer) {
	std::fwrite("junk", 1, 4, f);
	put32(f, 0xFFFFFFF0u);
	put32(f, 0);
    }
    std::fclose(f);
    return path;
}

static void test_chunks(void)
{
    tw_case("junk after the audio does not get a real file refused");
    std::string const tail = chunky("tail.wav", 0, true);
    CHECK_INT(loadsfxfromfile(SND_DOOR_OPENED, tail.c_str()), TRUE);

    tw_case("a file with many chunks before its audio still loads");
    /* The 10,000-chunk cap itself has no case: SDL refuses such a file on its
     * own, so the cap only saves the time the walk would take, and a result
     * cannot show it. This pins that a lower cap would break real files. */
    std::string const fine = chunky("fine.wav", 100, false);
    CHECK_INT(loadsfxfromfile(SND_SOCKET_OPENED, fine.c_str()), TRUE);
}

/* pkg-config's SDL2 flags carry -Dmain=SDL_main, and this runner links no
 * SDL2main to supply the real entry point -- it strips it, for a console. */
#undef main

int main(void)
{
    static char const *const made[] = {
	"ok.wav", "short.wav", "mscc.wav", "1hz.wav", "empty.wav", "half.wav",
	"liar.wav", "text.wav", "tail.wav", "fine.wav"
    };

    tw_begin("sdlsfx_test.cpp");

    /* The dummy driver: a real SDL audio device that plays into nothing, so the
     * loader runs its whole path -- open, convert, install under the lock --
     * on any machine, sound card or not. */
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    tw_case("the audio system starts on the dummy driver");
    CHECK_INT(_sdlsfxinitialize(FALSE, 0), TRUE);
    CHECK_INT(setaudiosystem(TRUE), TRUE);

    test_accepts();
    test_refuses();
    test_chunks();

    for (size_t i = 0 ; i < sizeof made / sizeof *made ; ++i)
	std::remove(scratch(made[i]).c_str());

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(13);
    return tw_end();
}
