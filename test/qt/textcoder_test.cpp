/* textcoder_test.cpp: the CC1 <-> Unicode text codec.
 *
 * MOD (Jeremy). The second test in oshw-qt/, and the second file there chosen
 * for the same reason as the first: it is not a widget. TWTextCoder is a pure
 * codec -- two static functions, no window, no painting -- and it decides how
 * every LEVEL NAME, PASSWORD and HINT out of a .dat file becomes text on the
 * screen.
 *
 * WHY IT COUNTS AS UNTRUSTED INPUT. Level packs are downloaded and traded. The
 * bytes fed to decode() come straight out of a stranger's .dat, and all 256 of
 * them are legal there: the CC1 format has no character-set validation, so a
 * pack can carry any byte in a level name. A codec that mishandled one would
 * corrupt a name, or -- if it indexed its table wrongly -- read outside it.
 *
 * 🔴 THE TABLE INDEX IS THE INTERESTING PART. decode() does
 *
 *     encodeTable[static_cast<unsigned char>(c)]
 *
 * and the cast is load-bearing: `char` is SIGNED on this toolchain, so without
 * it every byte >= 0x80 would index the array at a negative offset -- reading
 * whatever precedes it in memory, for exactly the accented characters real
 * European level packs are full of. That is the same class of defect as jc-48's
 * ctype calls on a signed char, and it is why every high byte is exercised
 * below rather than a tasteful sample.
 *
 * WHAT THIS DOES NOT COVER: the widgets. TWProgressBar, TWDisplayWidget,
 * TWMainWnd and the score table still have no automated coverage, because they
 * need a QApplication and a paint device and asserting on painted pixels is a
 * different and much weaker kind of test. The gap is named in CLAUDE.md.
 *
 * TESTSRC: ../../oshw-qt/TWTextCoder.cpp
 */

#include	"../tw_test.h"

#include	"../../oshw-qt/TWTextCoder.h"

#include	<QByteArray>
#include	<QString>

/* --- decoding ------------------------------------------------------------ */

static void test_decode_ascii(void)
{
    tw_case("plain ASCII decodes to itself");
    QString s = TWTextCoder::decode(QByteArray("CHIP'S CHALLENGE"));
    CHECK_STR(s.toUtf8().constData(), "CHIP'S CHALLENGE");

    tw_case("an empty input decodes to an empty string");
    CHECK_INT(TWTextCoder::decode(QByteArray()).length(), 0);

    tw_case("🔴 a NUL terminates the decode, it is not decoded through");
    /* Level names in a .dat are fixed-width fields padded with NULs. Decoding
     * past the terminator would put the padding -- and whatever followed the
     * name in the record -- into the name shown on screen. */
    QByteArray padded("NAME", 4);
    padded.append('\0');
    padded.append("GARBAGE", 7);
    QString name = TWTextCoder::decode(padded);
    CHECK_STR(name.toUtf8().constData(), "NAME");
    CHECK_INT(name.length(), 4);

    tw_case("a leading NUL decodes to nothing at all");
    QByteArray leading;
    leading.append('\0');
    leading.append("HIDDEN", 6);
    CHECK_INT(TWTextCoder::decode(leading).length(), 0);
}

static void test_decode_highbytes(void)
{
    tw_case("🔴 every byte 0x01..0xFF decodes to exactly one character");
    /* The whole table, not a sample. A single wrong or missing entry would show
     * up as a length that is not one, and the signed-char bug this guards
     * against would fire on the top half of this range. NUL is excluded because
     * it is the terminator, which the case above covers. */
    int badlength = 0;
    for (int b = 0x01 ; b <= 0xFF ; ++b) {
	QByteArray one(1, static_cast<char>(b));
	if (TWTextCoder::decode(one).length() != 1)
	    ++badlength;
    }
    CHECK_MSG(badlength == 0,
	      "%d of 255 byte values did not decode to a single character",
	      badlength);

    tw_case("the high half maps to the documented CP1252-style characters");
    /* Spot values with known answers, so that "every byte produced something"
     * above cannot be satisfied by a table of the wrong characters. */
    CHECK_INT(TWTextCoder::decode(QByteArray(1, static_cast<char>(0x80)))
		  .at(0).unicode(), 0x20AC);	/* euro sign */
    CHECK_INT(TWTextCoder::decode(QByteArray(1, static_cast<char>(0x99)))
		  .at(0).unicode(), 0x2122);	/* trade mark */
    CHECK_INT(TWTextCoder::decode(QByteArray(1, static_cast<char>(0xE9)))
		  .at(0).unicode(), 0x00E9);	/* e acute */
    CHECK_INT(TWTextCoder::decode(QByteArray(1, static_cast<char>(0xFF)))
		  .at(0).unicode(), 0x00FF);	/* y diaeresis */

    tw_case("a byte above 0x7F is NOT decoded as a negative index");
    /* The direct expression of the signed-char trap. If the cast in decode()
     * were dropped, this byte would index encodeTable[-25] and the result would
     * be whatever happened to sit before the table -- not reliably a crash, and
     * not reliably wrong on every run, which is what makes it worth pinning. */
    QString high = TWTextCoder::decode(QByteArray(1, static_cast<char>(0xE7)));
    CHECK_INT(high.length(), 1);
    CHECK_INT(high.at(0).unicode(), 0x00E7);	/* c cedilla */
}

/* --- encoding ------------------------------------------------------------ */

static void test_encode(void)
{
    tw_case("ASCII encodes to itself, with a terminating NUL appended");
    /* The trailing NUL is deliberate: the result goes back into a fixed-width
     * .dat field that must be terminated. It is also why encode and decode are
     * not each other's exact inverse at the byte level -- see the round-trip
     * cases below, which state the relationship precisely rather than assuming
     * symmetry. */
    QByteArray a = TWTextCoder::encode(QString("ABC"));
    CHECK_INT(a.size(), 4);
    CHECK_INT(static_cast<unsigned char>(a.at(0)), 'A');
    CHECK_INT(static_cast<unsigned char>(a.at(3)), 0x00);

    tw_case("a character at or below U+00FF encodes to that single byte");
    QByteArray e = TWTextCoder::encode(QString(QChar(0x00E9)));
    CHECK_INT(static_cast<unsigned char>(e.at(0)), 0xE9);

    tw_case("the mapped characters above U+00FF encode back to their byte");
    CHECK_INT(static_cast<unsigned char>(
		  TWTextCoder::encode(QString(QChar(0x20AC))).at(0)), 0x80);
    CHECK_INT(static_cast<unsigned char>(
		  TWTextCoder::encode(QString(QChar(0x2122))).at(0)), 0x99);

    tw_case("an unmappable character becomes a space, not a stray byte");
    /* `default: byte = ' '`. Worth pinning because the alternative a reader
     * might assume -- leaving `byte` uninitialized -- would be undefined
     * behavior writing an arbitrary byte into a level file. It is not what the
     * code does, and this case is what keeps it that way. */
    QByteArray cjk = TWTextCoder::encode(QString(QChar(0x4E00)));
    CHECK_INT(cjk.size(), 2);
    CHECK_INT(static_cast<unsigned char>(cjk.at(0)), ' ');

    tw_case("an empty string encodes to just the terminator");
    CHECK_INT(TWTextCoder::encode(QString()).size(), 1);
}

/* --- the round trip ------------------------------------------------------ */

static void test_roundtrip(void)
{
    tw_case("🔴 244 of 255 bytes survive decode -> encode; 11 do not (a DEFECT)");
    /* ⚠ THIS CASE PINS A BUG RATHER THAN ASSERTING CORRECTNESS, DELIBERATELY.
     *
     * The first version asserted a clean round trip for all 255 bytes and
     * FAILED, which is how the defect below was found. It is characterized here
     * instead of quietly fixed, because fixing it changes what bytes the
     * program writes and that is the maintainer's call, not a test's.
     *
     * WHAT IS WRONG. encode() is shifted one slot down from decode() across a
     * run of eleven characters:
     *
     *     decode: 0x81 -> U+0020   0x82 -> U+20A1   0x83 -> U+0192  ...
     *     encode: U+20A1 -> 0x81   U+0192 -> 0x82   U+201E -> 0x83  ...
     *
     * decode reserves 0x81 as an undefined slot (it decodes to a space, which
     * is what CP1252 does with it); encode was written against a table with no
     * gap there and packs the characters from 0x81 upward. So U+20A1 through
     * U+0152 -- eleven characters -- each encode to the byte BELOW the one they
     * decoded from.
     *
     * WHICH SIDE IS WRONG: encode. decode agrees with CP1252 exactly for
     * 0x83..0x8C, and the shift is entirely on the encode side.
     *
     * (⚠ One decode entry is separately odd and is NOT part of this shift:
     * 0x82 decodes to U+20A1, the COLON SIGN, where CP1252 has U+201A, a low
     * quotation mark. Those two code points are a digit transposition apart,
     * which is suggestive, but it is decode's business and unrelated to the
     * off-by-one.)
     *
     * HOW MUCH IT MATTERS: almost nothing, which is why it survived. encode()
     * has three callers (TWMainWnd.cpp 1291, 1754, 2164) and the only one that
     * can see user text is the input prompt, where the text is a Chip's
     * Challenge password -- A-Z, four characters. Nobody types a per-mille sign
     * into it. There is no buffer risk either: the codec is one byte per QChar
     * and the prompt truncates to nMaxLen first, so `char passwd[5]` with
     * maxlen 4 fits exactly.
     *
     * Upstream's, from the 2.3.1 import (929d9c6).
     *
     * 🔴 IF THIS CASE STARTS FAILING WITH A LOWER COUNT, THE BUG WAS FIXED.
     * That is good news: update the expectation to 0 and delete this comment.
     * Do not "repair" it by widening the tolerance. */
    int mismatches = 0;
    int offbyone = 0;
    for (int b = 0x01 ; b <= 0xFF ; ++b) {
	QString once = TWTextCoder::decode(QByteArray(1, static_cast<char>(b)));
	QByteArray back = TWTextCoder::encode(once);
	unsigned got = back.size() ? static_cast<unsigned char>(back.at(0)) : 0;
	if (got == static_cast<unsigned>(b))
	    continue;
	++mismatches;
	/* Separate the two causes. The five undefined slots decode to a space
	 * and legitimately encode back to 0x20; only the shifted run is a bug. */
	if (once.at(0).unicode() != 0x0020 && got == static_cast<unsigned>(b) - 1)
	    ++offbyone;
    }
    CHECK_MSG(mismatches == 16,
	      "expected 16 bytes not to survive the round trip, got %d",
	      mismatches);
    CHECK_MSG(offbyone == 11,
	      "expected 11 of them to be the encode() off-by-one, got %d",
	      offbyone);

    tw_case("the five undefined slots collapse to a space, which is not a bug");
    /* 0x81, 0x8D, 0x8F, 0x90 and 0x9D have no character in this encoding.
     * decode gives a space and encode sends a space to 0x20, so they do not
     * round-trip -- correctly. Separated from the case above so the two causes
     * are never conflated by a future reader. */
    int undefslots = 0;
    static int const undefined[] = { 0x81, 0x8D, 0x8F, 0x90, 0x9D };
    for (int i = 0 ; i < 5 ; ++i) {
	QString once = TWTextCoder::decode(
			    QByteArray(1, static_cast<char>(undefined[i])));
	if (once.length() == 1 && once.at(0).unicode() == 0x0020)
	    ++undefslots;
    }
    CHECK_INT(undefslots, 5);

    tw_case("a realistic accented level name survives the round trip");
    QByteArray original("CAF\xE9 D\xC9J\xC0 VU");
    QString shown = TWTextCoder::decode(original);
    QByteArray written = TWTextCoder::encode(shown);
    CHECK_INT(written.size(), original.size() + 1);   /* + the terminator */
    CHECK_MSG(written.startsWith(original),
	      "a name with accented characters did not survive the round trip");

    tw_case("⚠ encode is NOT a byte-exact inverse: it appends a terminator");
    /* Stated as its own case so that nobody later "fixes" the size difference
     * by removing the NUL, which the .dat writer needs. */
    QByteArray in("HI");
    CHECK_INT(TWTextCoder::encode(TWTextCoder::decode(in)).size(),
	      in.size() + 1);
}

int main(void)
{
    tw_begin("textcoder");

    test_decode_ascii();
    test_decode_highbytes();
    test_encode();
    test_roundtrip();

    /* Raise this when cases are added; never lower it to make a run pass. */
    tw_expect_atleast(27);
    return tw_end();
}
