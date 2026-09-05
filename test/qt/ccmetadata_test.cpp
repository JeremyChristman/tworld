/* ccmetadata_test.cpp: the .ccx level-metadata parser.
 *
 * MOD (Jeremy). The FIRST test of anything in oshw-qt/. Everything in that
 * directory -- the score table, the color picker, the tileset menu, the death
 * counter -- had no automated coverage of any kind; CLAUDE.md said so plainly.
 * This is the smallest useful crack in that, because CCMetaData.cpp is the one
 * file there that is a PARSER OF UNTRUSTED INPUT rather than a widget.
 *
 * WHY IT NEEDS ITS OWN RUNNER. The rest of the unit suite compiles the source
 * under test directly with plain gcc and no Qt (docs/adr/0003). This file
 * cannot: CCMetaData.cpp is built on QDomDocument, QString and QColor, so it
 * needs Qt5 headers and Qt5Xml/Gui/Core on the link line. test/run-qt-tests.ps1
 * is that runner, and it skips cleanly when Qt is not installed rather than
 * failing -- a developer without Qt still gets the other 17,000 checks.
 *
 * WHAT A .ccx IS AND WHY IT COUNTS AS UNTRUSTED. It is an XML sidecar shipped
 * INSIDE a level pack (docs/tworld2.6), naming each level's author and holding
 * the prologue/epilogue story text. Installing a pack installs its .ccx, which
 * is exactly the trust boundary the .dat, .dac and .tws parsers sit on.
 *
 * 🔴 WHAT WAS NOT KNOWN UNTIL THIS TEST EXISTED. `.ccx` is NEVER PARSED IN
 * BATCH MODE -- readextensions() (TWMainWnd.cpp:2149) returns immediately when
 * g_pMainWnd is null, which is precisely the batch case. So no corpus run, no
 * end-to-end case and no fuzz target has ever touched this file, and none can:
 * it is reachable only from a running GUI. That is why a Qt-linked unit test is
 * the only way to cover it at all.
 *
 * THE PROPERTY THAT MATTERS MOST is the caller contract. ReadExtensions() does:
 *
 *     m_ccxLevelset.ReadFile(sFilePath, pSeries->count);
 *     for (int i = 1; i <= pSeries->count; ++i)
 *         CCX::Level& rCCXLevel = m_ccxLevelset.vecLevels[i];
 *
 * -- it indexes vecLevels[1..count] and IGNORES ReadFile's return value. So
 * every early exit inside ReadFile must still leave the vector sized, or a
 * missing/corrupt .ccx becomes an out-of-bounds write in the caller. Several
 * cases below assert exactly that, on each failure path.
 *
 * TESTSRC: ../../oshw-qt/CCMetaData.cpp
 */

#include <cstdio>
#include <cstring>

#include "../tw_test.h"
#include "../../oshw-qt/CCMetaData.h"

/* --- helpers ------------------------------------------------------------ */

static char const *scratchname = "tw_ccx_test.ccx";

/* Write text as a .ccx and parse it. nLevels mirrors what ReadExtensions
 * passes: the level count from the .dat header. */
static bool readccx(char const *xml, int nLevels, CCX::Levelset &ls)
{
    FILE       *f;
    size_t	len;

    len = strlen(xml);
    f = fopen(scratchname, "wb");
    if (!f)
	return false;
    if (fwrite(xml, 1, len, f) != len) {
	fclose(f);
	remove(scratchname);
	return false;
    }
    fclose(f);

    bool ok = ls.ReadFile(QString::fromLocal8Bit(scratchname), nLevels);
    remove(scratchname);
    return ok;
}

/* The invariant ReadExtensions() depends on, checked the way it indexes. */
static void checkcallersafe(CCX::Levelset const &ls, int nLevels, char const *what)
{
    CHECK_MSG((int)ls.vecLevels.size() >= nLevels + 1,
	      "%s: vecLevels is %d for %d levels -- ReadExtensions() indexes"
	      " [1..%d] whatever ReadFile returned, so this is an out-of-bounds"
	      " access in the caller", what, (int)ls.vecLevels.size(), nLevels,
	      nLevels);
}

int main(void)
{
    tw_begin("ccmetadata");
    tw_expect_atleast(90);

    /* ================================================================== *
     * The caller contract: vecLevels is sized on EVERY path out of
     * ReadFile, including the ones that report failure.
     * ================================================================== */

    tw_case("a MISSING .ccx succeeds and still sizes the vector");
    {
	/* Not an error: most level packs have no .ccx at all, which is why
	 * ReadFile returns true for a file that does not exist. */
	CCX::Levelset ls;
	bool ok = ls.ReadFile(QStringLiteral("tw_ccx_does_not_exist.ccx"), 5);
	CHECK_MSG(ok, "a missing .ccx was reported as a failure");
	checkcallersafe(ls, 5, "missing file");
    }

    tw_case("every FAILING parse still leaves the vector sized");
    {
	CCX::Levelset ls;
	/* Malformed XML. */
	CHECK_MSG(!readccx("<levelset", 5, ls), "unclosed XML was accepted");
	checkcallersafe(ls, 5, "unclosed XML");
	/* Well-formed XML, wrong root element. */
	CHECK_MSG(!readccx("<?xml version=\"1.0\"?><notlevelset/>", 5, ls),
		  "a wrong root element was accepted");
	checkcallersafe(ls, 5, "wrong root");
	/* Empty file. */
	CHECK_MSG(!readccx("", 5, ls), "an empty .ccx was accepted");
	checkcallersafe(ls, 5, "empty file");
	/* Not XML at all. */
	CHECK_MSG(!readccx("this is not xml", 5, ls), "plain text was accepted");
	checkcallersafe(ls, 5, "plain text");
    }

    tw_case("a level count of zero or one is still safe to index");
    {
	/* readseriesheader() refuses a .dat claiming zero levels, so count is
	 * always >= 1 in practice -- but the vector must be sized for whatever
	 * it is handed, and 0 is the boundary. */
	CCX::Levelset ls;
	CHECK_INT((int)(readccx("<?xml version=\"1.0\"?><levelset/>", 0, ls)), 1);
	checkcallersafe(ls, 0, "zero levels");
	CHECK_INT((int)(readccx("<?xml version=\"1.0\"?><levelset/>", 1, ls)), 1);
	checkcallersafe(ls, 1, "one level");
    }

    /* ================================================================== *
     * A level number out of range must be ignored, not indexed.
     * ================================================================== */

    tw_case("an out-of-range level number is skipped, not written through");
    {
	/* 🔴 THE ONE MEMORY-SAFETY QUESTION IN THIS FILE. The number comes
	 * straight out of the .ccx, and vecLevels is only 1+nLevels long. The
	 * guard is `nNumber >= 1 && nNumber < vecLevels.size()`. Each of these
	 * must be dropped rather than used as an index. */
	CCX::Levelset ls;
	static char const *const bad[] = {
	    "<level number=\"0\" author=\"X\"/>",
	    "<level number=\"-1\" author=\"X\"/>",
	    "<level number=\"6\" author=\"X\"/>",
	    "<level number=\"99999\" author=\"X\"/>",
	    "<level number=\"-99999\" author=\"X\"/>",
	    "<level number=\"2147483647\" author=\"X\"/>",
	    "<level number=\"notanumber\" author=\"X\"/>",
	    "<level author=\"X\"/>"
	};
	char buf[512];
	int i;

	for (i = 0 ; i < (int)(sizeof bad / sizeof *bad) ; ++i) {
	    snprintf(buf, sizeof buf,
		     "<?xml version=\"1.0\"?><levelset author=\"Set\">%s</levelset>",
		     bad[i]);
	    CHECK_MSG(readccx(buf, 5, ls), "case %d made the whole file fail", i);
	    checkcallersafe(ls, 5, "out-of-range level number");
	    /* And it must not have landed on some other level either: every
	     * level should still carry the set-wide author. */
	    CHECK_MSG(ls.vecLevels[5].sAuthor == QStringLiteral("Set"),
		      "case %d wrote 'X' somewhere it should not have", i);
	}
    }

    /* ================================================================== *
     * The data it is actually for.
     * ================================================================== */

    tw_case("levelset attributes are read, and levels inherit the author");
    {
	CCX::Levelset ls;
	bool ok = readccx(
	    "<?xml version=\"1.0\"?>"
	    "<levelset author=\"Alice\" description=\"A set\" copyright=\"2026\">"
	    "  <level number=\"2\" author=\"Bob\"/>"
	    "  <level number=\"3\"/>"
	    "</levelset>", 5, ls);
	CHECK_MSG(ok, "a well-formed .ccx was refused");
	CHECK_MSG(ls.sAuthor == QStringLiteral("Alice"), "set author not read");
	CHECK_MSG(ls.sDescription == QStringLiteral("A set"), "description not read");
	CHECK_MSG(ls.sCopyright == QStringLiteral("2026"), "copyright not read");
	/* Level 2 overrides; level 3 and the untouched levels inherit. */
	CHECK_MSG(ls.vecLevels[2].sAuthor == QStringLiteral("Bob"),
		  "a per-level author did not override the set author");
	CHECK_MSG(ls.vecLevels[3].sAuthor == QStringLiteral("Alice"),
		  "a level with no author did not inherit the set author");
	CHECK_MSG(ls.vecLevels[5].sAuthor == QStringLiteral("Alice"),
		  "an unmentioned level did not inherit the set author");
    }

    tw_case("ruleset compatibility is read and inherited");
    {
	CCX::Levelset ls;
	bool ok = readccx(
	    "<?xml version=\"1.0\"?>"
	    "<levelset ms=\"yes\" lynx=\"no\">"
	    "  <level number=\"2\" lynx=\"yes\"/>"
	    "</levelset>", 5, ls);
	CHECK_MSG(ok, "a .ccx with compatibility flags was refused");
	CHECK_INT(ls.ruleCompat.eMS, CCX::COMPAT_YES);
	CHECK_INT(ls.ruleCompat.eLynx, CCX::COMPAT_NO);
	CHECK_INT(ls.vecLevels[2].ruleCompat.eMS, CCX::COMPAT_YES);
	CHECK_MSG(ls.vecLevels[2].ruleCompat.eLynx == CCX::COMPAT_YES,
		  "a per-level lynx flag did not override the set's");
	CHECK_INT(ls.vecLevels[3].ruleCompat.eLynx, CCX::COMPAT_NO);
	/* An unrecognized value must not become YES by accident. */
	CHECK_MSG(readccx("<?xml version=\"1.0\"?><levelset ms=\"maybe\"/>", 5, ls),
		  "an unknown compatibility value made the file fail");
	CHECK_NE_INT(ls.ruleCompat.eMS, CCX::COMPAT_YES);
    }

    tw_case("prologue and epilogue text is read into pages");
    {
	/* ⚠ Story text lives in <page> elements INSIDE <prologue>, not as the
	 * prologue's own text -- Text::ReadXML looks for elementsByTagName
	 *("page"). The first draft of this case wrote <prologue>Before</prologue>
	 * and expected a page; the parser was right and the test was wrong. Both
	 * shapes are pinned below so the next person does not repeat it. */
	CCX::Levelset ls;
	bool ok = readccx(
	    "<?xml version=\"1.0\"?>"
	    "<levelset>"
	    "  <level number=\"1\">"
	    "    <prologue><page>Before</page></prologue>"
	    "    <epilogue><page>After</page><page>Then</page></epilogue>"
	    "  </level>"
	    "</levelset>", 3, ls);
	CHECK_MSG(ok, "a .ccx with story text was refused");
	CHECK_INT((int)ls.vecLevels[1].txtPrologue.vecPages.size(), 1);
	CHECK_INT((int)ls.vecLevels[1].txtEpilogue.vecPages.size(), 2);
	if (!ls.vecLevels[1].txtPrologue.vecPages.empty())
	    CHECK_MSG(ls.vecLevels[1].txtPrologue.vecPages[0].sText
			    .contains(QStringLiteral("Before")),
		      "the prologue text was lost");
	/* bSeen is reset by the caller after every read; it must start false. */
	CHECK_MSG(!ls.vecLevels[1].txtPrologue.bSeen,
		  "bSeen started true, so the prologue would be skipped");

	/* A prologue with no <page> yields no pages. That is the documented
	 * behavior, not a bug -- pinned so it stays deliberate. */
	CCX::Levelset ls2;
	CHECK_MSG(readccx("<?xml version=\"1.0\"?><levelset><level number=\"1\">"
			  "<prologue>bare text</prologue></level></levelset>",
			  3, ls2), "a page-less prologue made the file fail");
	CHECK_INT((int)ls2.vecLevels[1].txtPrologue.vecPages.size(), 0);
    }

    tw_case("every .ccx this repository ships still parses");
    {
	/* 🔴 THE CASE MOST LIKELY TO CATCH A REAL REGRESSION, and the cheapest.
	 * data/ carries six real .ccx files from the CCLP packs -- 224 KB of
	 * CDATA, HTML, entities and non-ASCII that no hand-written fixture
	 * would think to imitate. Until this test existed nothing had ever
	 * parsed them: readextensions() is GUI-only, so no corpus run, no
	 * end-to-end case and no fuzz target reaches this code.
	 *
	 * The level counts are the real ones, so vecLevels is sized as the game
	 * sizes it. Paths are resolved the way test/tw_corpus.h does, because
	 * the working directory is a convention rather than a guarantee. */
	static struct { char const *name; int levels; } const sets[] = {
	    { "CCLP1.ccx",  149 }, { "CCLP2.ccx",  149 },
	    { "CCLP3.ccx",  149 }, { "CCLP4.ccx",  149 },
	    { "CCLP5.ccx",  149 }, { "CCLXP2.ccx", 149 }
	};
	static char const *const bases[] = { "data", "../data", "../../data" };
	char path[512];
	int i, b, found = 0;

	for (i = 0 ; i < (int)(sizeof sets / sizeof *sets) ; ++i) {
	    FILE *probe = NULL;
	    for (b = 0 ; b < (int)(sizeof bases / sizeof *bases) ; ++b) {
		snprintf(path, sizeof path, "%s/%s", bases[b], sets[i].name);
		probe = fopen(path, "rb");
		if (probe)
		    break;
	    }
	    if (!probe)
		continue;
	    fclose(probe);
	    ++found;

	    CCX::Levelset ls;
	    bool ok = ls.ReadFile(QString::fromLocal8Bit(path), sets[i].levels);
	    CHECK_MSG(ok, "the shipped %s failed to parse", sets[i].name);
	    checkcallersafe(ls, sets[i].levels, sets[i].name);
	    /* Every one of them names authors, so a set that comes back with
	     * nothing at all has silently stopped being read. */
	    CHECK_MSG(!ls.sDescription.isEmpty() || !ls.sAuthor.isEmpty()
			    || !ls.vecLevels[1].sAuthor.isEmpty(),
		      "%s parsed to nothing at all", sets[i].name);
	}

	/* Finding none is a FAILURE, not a skip: a green run that read no files
	 * is the false green this suite exists to prevent. */
	CHECK_MSG(found == (int)(sizeof sets / sizeof *sets),
		  "only %d of %d shipped .ccx files were found from the working"
		  " directory -- the rest were not tested",
		  found, (int)(sizeof sets / sizeof *sets));
    }

    tw_case("a style sheet is taken only from a DIRECT child");
    {
	/* elmStyle.parentNode() == elm is the guard. A <style> buried inside a
	 * level must not become the set-wide stylesheet. */
	CCX::Levelset ls;
	CHECK_MSG(readccx("<?xml version=\"1.0\"?>"
			  "<levelset><style>body{}</style></levelset>", 3, ls),
		  "a direct-child style was refused");
	CHECK_MSG(ls.sStyleSheet.contains(QStringLiteral("body")),
		  "a direct-child style sheet was not read");

	CCX::Levelset ls2;
	CHECK_MSG(readccx("<?xml version=\"1.0\"?>"
			  "<levelset><level number=\"1\">"
			  "<style>nested{}</style></level></levelset>", 3, ls2),
		  "a nested style made the file fail");
	CHECK_MSG(!ls2.sStyleSheet.contains(QStringLiteral("nested")),
		  "a style sheet nested inside a level leaked to the set");
    }

    tw_case("Clear() resets everything, including the vector");
    {
	CCX::Levelset ls;
	CHECK_MSG(readccx("<?xml version=\"1.0\"?><levelset author=\"Alice\"/>",
			  5, ls), "setup parse failed");
	CHECK_MSG(ls.sAuthor == QStringLiteral("Alice"), "setup did not take");
	ls.Clear();
	CHECK_MSG(ls.sAuthor.isEmpty(), "Clear() left the author behind");
	CHECK_INT((int)ls.vecLevels.size(), 0);
    }

    tw_case("a hostile .ccx does not run away with memory or time");
    {
	/* Deep nesting and a large attribute: QDom does the parsing, so this is
	 * really a check that nothing here amplifies it. Bounded and quick is
	 * the whole assertion -- if this case hangs, that is the finding. */
	CCX::Levelset ls;
	QString deep = QStringLiteral("<?xml version=\"1.0\"?><levelset>");
	int i;
	for (i = 0 ; i < 200 ; ++i)
	    deep += QStringLiteral("<level number=\"1\">");
	for (i = 0 ; i < 200 ; ++i)
	    deep += QStringLiteral("</level>");
	deep += QStringLiteral("</levelset>");
	readccx(deep.toLocal8Bit().constData(), 5, ls);
	checkcallersafe(ls, 5, "deeply nested");

	QString big = QStringLiteral("<?xml version=\"1.0\"?><levelset author=\"");
	big += QString(100000, QLatin1Char('A'));
	big += QStringLiteral("\"/>");
	readccx(big.toLocal8Bit().constData(), 5, ls);
	checkcallersafe(ls, 5, "huge attribute");
	CHECK_MSG(ls.sAuthor.length() == 100000 || ls.sAuthor.isEmpty(),
		  "a 100000-character author came back as %d characters",
		  ls.sAuthor.length());
    }

    return tw_end();
}
