/* mainwnd_test.cpp: the main window -- the largest unverified file that ships.
 *
 * MOD (Jeremy). TWMainWnd.cpp is 2,543 lines and had no automated coverage of
 * any kind. It is more than half of everything in this repository that ships
 * without a test, and it is the file that runs when somebody actually plays.
 *
 * 🔴 WHY IT WAS NEVER TESTED, AND WHY THAT REASON NO LONGER HOLDS. Both tests
 * already in this directory were written against files chosen for NOT being
 * widgets -- textcoder_test.cpp says so in as many words: the widgets "need a
 * QApplication and a paint device, and asserting on painted pixels is a
 * different and much weaker kind of test."
 *
 * The first half of that is answerable: Qt ships an `offscreen` platform
 * plugin, and under it the real TileWorldMainWnd constructs, runs and destroys
 * with no display at all. This file does exactly that. The second half stands,
 * and this test respects it -- NOTHING HERE ASSERTS ON A PAINTED PIXEL. Every
 * case below reads a value the window DECIDED (a label's text, a palette role,
 * a menu item's visibility, a window title), never how it was drawn.
 *
 * WHAT LINKING THIS COSTS, stated plainly: the window pulls in TWApp, TWTheme,
 * the .ui, the moc output, oshwbind and most of the game core, because its
 * constructor reads settings and asks the core whether the death counter is
 * active. Those translation units are declared in the TESTSRC lines below.
 * tworld.c is the one file that cannot come -- it owns main() -- so its four
 * globals are stubbed here instead, the same thing test/fuzz/fuzz_settings.cpp
 * already does for appdir.
 *
 * ⚠ WHAT THIS DELIBERATELY DOES NOT TOUCH: anything that writes a file.
 * TWTheme::saveBackground() calls savesettings(), so Options > "restore the
 * stock color" and SetBackgroundColor(..., true) are never exercised here.
 * docs/adr/0005 is why: a unit test does not get to write the maintainer's
 * tw_settings.ini. The live-preview path reaches the same code with bSave
 * false, which is what the background cases below use.
 *
 * TESTSRC: ../../oshw-qt/TWMainWnd.cpp ../../oshw-qt/TWApp.cpp
 * TESTSRC: ../../oshw-qt/TWTheme.cpp ../../oshw-qt/TWTextCoder.cpp
 * TESTSRC: ../../oshw-qt/CCMetaData.cpp ../../oshw-qt/TWDisplayWidget.cpp
 * TESTSRC: ../../oshw-qt/TWProgressBar.cpp ../../oshw-qt/oshwbind.cpp
 * TESTSRC: ../../generic/_in.cpp ../../generic/tile.c ../../generic/timer.c
 * TESTSRC: ../../oshw-sdl/sdlsfx.c
 * TESTSRC: ../../messages.cpp ../../score.cpp ../../settings.cpp
 * TESTSRC: ../../cmdline.c ../../encoding.c ../../err.c ../../fileio.c
 * TESTSRC: ../../help.c ../../lxlogic.c ../../mslogic.c ../../play.c
 * TESTSRC: ../../random.c ../../res.c ../../series.c ../../solution.c
 * TESTSRC: ../../unslist.c
 * TESTUIC: ../../oshw-qt/TWMainWnd.ui
 * TESTMOC: ../../oshw-qt/TWMainWnd.h
 * TESTPKG: Qt5Widgets sdl2
 * TESTNOMAIN: ../../oshw-qt/TWApp.cpp
 */

#include	"../tw_test.h"

#include	"TWApp.h"
#include	"TWMainWnd.h"

#include	<QApplication>
#include	<QLabel>
#include	<QAction>
#include	<QPalette>
#include	<QBrush>
#include	<QWidget>

/* --- the globals tworld.c owns -------------------------------------------
 *
 * Linkage matches how each is DECLARED, which is not uniform: oshw.h declares
 * casualinputs OUTSIDE its extern "C" guard and tworld() inside it, and
 * settings.cpp declares appdir as plain C++. Getting one wrong is a link error
 * rather than a silent problem, which is the good case.
 */
int	 ignorepasswds	= FALSE;
int	 casualinputs	= TRUE;
char	*appdir		= NULL;
extern "C" int tworld(int, char **) { return 0; }

/* --- helpers ------------------------------------------------------------- */

/* The widgets are protected members of the uic-generated base class, so they
 * are reached by object name through the QObject tree instead -- the same
 * handle Qt Designer or an accessibility tool would use. */
template <typename T>
static T named(QObject *root, char const *name)
{
    return root->findChild<T>(QString::fromLatin1(name));
}

static QString labeltext(TileWorldMainWnd *w)
{
    QLabel *lbl = named<QLabel *>(w, "m_pLblShortMsg");
    return lbl ? lbl->text() : QStringLiteral("<no label>");
}

static int labelrole(TileWorldMainWnd *w)
{
    QLabel *lbl = named<QLabel *>(w, "m_pLblShortMsg");
    return lbl ? (int)lbl->foregroundRole() : (int)QPalette::NoRole;
}

static QPalette mainpalette(TileWorldMainWnd *w)
{
    QWidget *mw = named<QWidget *>(w, "m_pMainWidget");
    return mw ? mw->palette() : QPalette();
}

static QByteArray rolename(QPalette const &p, QPalette::ColorRole role)
{
    return p.color(QPalette::Active, role).name().toUtf8();
}

/* SetBackgroundColor() is private; OnBackgroundColorPreview() is a private
 * SLOT, which Qt makes invokable by design. This is the same entry the color
 * picker's currentColorChanged signal uses at run time, and it passes
 * bSave = false -- so no settings file is ever written. */
static bool preview(TileWorldMainWnd *w, QColor const &c)
{
    return QMetaObject::invokeMethod(w, "OnBackgroundColorPreview",
				     Qt::DirectConnection, Q_ARG(QColor, c));
}

/* --- the window exists at all -------------------------------------------- */

static void test_construction(TileWorldMainWnd *w)
{
    tw_case("🔴 the real main window constructs under the offscreen platform");
    /* The constructor alone is ~120 lines: it runs setupUi, captures the stock
     * palette, builds the tileset submenu, restores four settings and seeds the
     * death counter. Reaching the end of it with the widgets findable is the
     * single largest thing this file establishes. */
    CHECK_MSG(w != NULL, "no window");
    CHECK_MSG(named<QLabel *>(w, "m_pLblShortMsg") != NULL,
	      "the short-message label is missing from the object tree");
    CHECK_MSG(named<QWidget *>(w, "m_pMainWidget") != NULL,
	      "the main widget is missing from the object tree");
    CHECK_MSG(named<QAction *>(w, "action_DeathCounter") != NULL,
	      "the Death Counter menu item is missing");

    tw_case("the offscreen platform really is what is in use");
    /* Stated so that a machine which somehow loaded the windows plugin does not
     * quietly run these cases against a real display. */
    CHECK_STR(qApp->platformName().toUtf8().constData(), "offscreen");
}

/* --- the short-message bar (jc-37, jc-38) --------------------------------
 *
 * RefreshShortMsgLabel() is "the ONE place m_pLblShortMsg's text and color are
 * decided" (TWMainWnd.h). Its rule: a message wins if it is STICKY, or if there
 * is no death counter to displace it; otherwise the counter shows. Both inputs
 * arrive through public methods, so the rule is testable directly.
 */

static void test_shortmsg(TileWorldMainWnd *w)
{
    tw_case("with no message and no counter the bar is empty");
    w->SetDeathCount(-1);
    w->SetDisplayMsg(NULL, 0, 0);
    CHECK_STR(labeltext(w).toUtf8().constData(), "");

    tw_case("a message with no counter is shown");
    w->SetDisplayMsg("hello", 5000, 0);
    CHECK_STR(labeltext(w).toUtf8().constData(), "hello");

    tw_case("🔴 the death counter DISPLACES a finite message");
    /* The reason this rule exists: with the volume at zero every sound effect
     * becomes a message (sdlsfx.c), so without it the counter would strobe on
     * essentially every tick. */
    w->SetDeathCount(3);
    CHECK_STR(labeltext(w).toUtf8().constData(), "Deaths: 3");

    tw_case("...and the counter is on the WindowText role, which is white");
    /* A standing readout, not a notification. The .ui sets that role red and
     * the constructor overrides it to white precisely for this. */
    CHECK_INT(labelrole(w), (int)QPalette::WindowText);

    tw_case("🔴 a STICKY message outranks the counter");
    /* Sticky means pushed with FOREVER -- in practice only "(paused)" and
     * "Verifying ...", which are state indicators rather than notifications. */
    w->SetDisplayMsg("(paused)", FOREVER, 0);
    CHECK_STR(labeltext(w).toUtf8().constData(), "(paused)");

    tw_case("a message inside its bold window uses BrightText");
    w->SetDeathCount(-1);
    w->SetDisplayMsg("bold", 5000, 5000);
    CHECK_INT(labelrole(w), (int)QPalette::BrightText);

    tw_case("a bold window of ZERO is still bold on the tick it is pushed");
    /* The comparison is `nCurTime > nMsgBoldUntil`, strictly greater, and
     * bold = 0 makes those equal -- so setdisplaymsg(msg, n, 0) is BrightText
     * for the millisecond it arrives on and Text thereafter. Pinned because it
     * is a boundary, and because it is why the next case has to wait. */
    w->SetDisplayMsg("plain", 5000, 0);
    CHECK_INT(labelrole(w), (int)QPalette::BrightText);

    tw_case("...and Text once the bold window has lapsed");
    TW_Delay(5);
    w->SetDeathCount(-1);		/* forces a refresh, pushes no message */
    CHECK_INT(labelrole(w), (int)QPalette::Text);

    tw_case("an empty message clears the whole stack, it does not stack up");
    w->SetDisplayMsg("one", 5000, 0);
    w->SetDisplayMsg("two", 5000, 0);
    w->SetDisplayMsg("", 0, 0);
    CHECK_STR(labeltext(w).toUtf8().constData(), "");

    tw_case("a counter of zero still displays -- 0 is not 'off'");
    w->SetDeathCount(0);
    CHECK_STR(labeltext(w).toUtf8().constData(), "Deaths: 0");

    tw_case("only a negative count turns the counter off");
    w->SetDeathCount(-1);
    CHECK_STR(labeltext(w).toUtf8().constData(), "");

    tw_case("🔴 an EXPIRED transient does not hide a live sticky beneath it");
    /* RefreshShortMsgLabel walks the stack from the top DOWN past expired
     * entries rather than testing back(), because it runs from SetDisplayMsg
     * too -- between timer ticks, before timerEvent has popped the dead one.
     * Without the walk, "(paused)" would vanish the moment any transient on top
     * of it lapsed. */
    w->SetDisplayMsg("", 0, 0);
    w->SetDisplayMsg("(paused)", FOREVER, 0);
    w->SetDisplayMsg("blip", 1, 0);
    TW_Delay(40);
    w->SetDeathCount(-1);		/* any refresh; the counter stays off */
    CHECK_STR(labeltext(w).toUtf8().constData(), "(paused)");

    w->SetDisplayMsg("", 0, 0);
    w->SetDeathCount(-1);
}

/* --- the death-counter menu ---------------------------------------------- */

static void test_deathcounter_menu(TileWorldMainWnd *w)
{
    QAction *on	   = named<QAction *>(w, "action_DeathCounter");
    QAction *reset = named<QAction *>(w, "action_ResetDeathCounter");
    QAction *set   = named<QAction *>(w, "action_SetDeathCounter");

    tw_case("Reset and Set appear with the Death Counter checkbox");
    /* They are meaningless when the counter is off, so they are hidden rather
     * than left sitting there grayed out. */
    on->setChecked(true);
    w->UpdateDeathCounterMenu();
    CHECK_INT(reset->isVisible(), 1);
    CHECK_INT(set->isVisible(), 1);

    tw_case("...and are hidden again when it is unchecked");
    on->setChecked(false);
    w->UpdateDeathCounterMenu();
    CHECK_INT(reset->isVisible(), 0);
    CHECK_INT(set->isVisible(), 0);
}

/* --- the window title and the build tag ---------------------------------- */

static void test_title(TileWorldMainWnd *w)
{
    tw_case("🔴 the [jc-N] build tag is OFF unless opted in");
    /* A public build must not carry the fork's build tag in its title. The
     * switch is `showbuildtag` in tw_settings.ini and it is strictly opt-in, so
     * with no setting loaded the title is the bare program name. */
    QByteArray const title = TileWorldApp::WindowTitle().toUtf8();
    CHECK_MSG(!TileWorldApp::WindowTitle().contains(TileWorldApp::s_sBuildTag),
	      "the build tag appears in the default window title: %s",
	      title.constData());
    CHECK_STR(title.constData(), TileWorldApp::s_sTitle.toUtf8().constData());

    tw_case("a subtitle is joined to the title with a dash");
    setsubtitle("Level 5");
    QByteArray const withsub = w->windowTitle().toUtf8();
    QByteArray const expect  =
	    (TileWorldApp::s_sTitle + QStringLiteral(" - Level 5")).toUtf8();
    CHECK_STR(withsub.constData(), expect.constData());

    tw_case("an empty subtitle leaves the bare title");
    setsubtitle("");
    CHECK_STR(w->windowTitle().toUtf8().constData(),
	      TileWorldApp::s_sTitle.toUtf8().constData());

    tw_case("a NULL subtitle does the same");
    setsubtitle(NULL);
    CHECK_STR(w->windowTitle().toUtf8().constData(),
	      TileWorldApp::s_sTitle.toUtf8().constData());
}

/* --- the background color (TWTheme) --------------------------------------
 *
 * The rule in TWMainWnd.h: every retint derives from the palette the .ui built,
 * captured before anything modified it -- NEVER from the widget's current
 * palette, because "deriving from the current one would compound, and its
 * Window role is a gradient brush with no single color to read back."
 */

static void test_background(TileWorldMainWnd *w)
{
    QPalette const atstart = mainpalette(w);

    tw_case("the stock window color is the .ui's #285080");
    CHECK_STR(rolename(atstart, QPalette::Button).constData(), "#285080");

    tw_case("🔴 the Window role is a GRADIENT, not a flat color");
    /* This is the whole reason the stock palette is kept separately: there is
     * no single color to read back out of this brush, so anything that tried to
     * recover the background from the live palette would drift. */
    CHECK_INT(atstart.brush(QPalette::Active, QPalette::Window).style(),
	      (int)Qt::LinearGradientPattern);

    tw_case("TWTheme's factors reproduce three of the .ui's four shade literals");
    /* TWTheme.cpp derives these from the stock blue rather than repeating the
     * .ui's literals, so that the .ui stays the single source of truth. Checked
     * rather than trusted -- and checking it found that one of the four does
     * NOT match. See the Mid case below. */
    CHECK_STR(rolename(atstart, QPalette::Light).constData(),    "#3c78c0");  /* 60,120,192 */
    CHECK_STR(rolename(atstart, QPalette::Midlight).constData(), "#3264a0");  /* 50,100,160 */
    CHECK_STR(rolename(atstart, QPalette::Dark).constData(),     "#142840");  /* 20,40,64   */

    tw_case("⚠ ...but Mid is ONE off in red from the literal the .ui carries");
    /* TWMainWnd.ui says Mid = 26,53,85. What ships is 27,53,85, because the
     * constructor replaces the .ui's palette with TWTheme::recolor()'s, and
     * QColor::darker(150) scales the HSV VALUE (128 -> 85) and derives red from
     * the ratio: 40 * 85/128 = 26.56, which rounds to 27. The .ui's literal is
     * the truncation of 40/1.5 = 26.667. Measured, not reasoned: the other
     * three factors land exactly.
     *
     * Left alone deliberately. One step in 255 in one channel of one role is
     * invisible, and darker(150) is the principled derivation -- the literal is
     * what is approximate here, not the code. It is pinned so that a future
     * change to the factor is noticed, and TWTheme.cpp's comment has been
     * corrected to stop claiming all four are exact. */
    CHECK_STR(rolename(atstart, QPalette::Mid).constData(),      "#1b3555");  /* not 26,53,85 */

    tw_case("white text is chosen over black on the stock blue");
    CHECK_STR(rolename(atstart, QPalette::WindowText).constData(), "#ffffff");

    tw_case("a previewed color reaches the palette");
    CHECK_INT(preview(w, QColor(Qt::red)), 1);
    CHECK_STR(rolename(mainpalette(w), QPalette::Button).constData(), "#ff0000");

    tw_case("black text is chosen over white on a light background");
    preview(w, QColor(Qt::white));
    CHECK_STR(rolename(mainpalette(w), QPalette::WindowText).constData(), "#000000");

    tw_case("previewing the same color twice gives the same palette");
    preview(w, QColor(Qt::red));
    QPalette const red1 = mainpalette(w);
    preview(w, QColor(Qt::blue));
    preview(w, QColor(Qt::red));
    CHECK_MSG(mainpalette(w) == red1,
	      "the palette for a color depended on what was shown before it");

    tw_case("🔴 an INVALID color falls back to the STOCK background");
    /* SetBackgroundColor does `color.isValid() ? color : StockBackground()`,
     * and StockBackground() reads the SAVED stock palette. If it read the live
     * one instead, this would come back as some shade derived from the gradient
     * the previous preview left behind rather than the .ui's blue -- which is
     * the compounding the header warns about, in its one observable form. */
    preview(w, QColor(Qt::blue));
    preview(w, QColor());
    CHECK_STR(rolename(mainpalette(w), QPalette::Button).constData(), "#285080");

    tw_case("...and that reproduces the construction-time palette exactly");
    CHECK_MSG(mainpalette(w) == atstart,
	      "resetting to the stock color did not reproduce the starting palette");
}

/* --- what batch mode relies on ------------------------------------------- */

static void test_batch_guards(void)
{
    /* ⚠ ONLY TWO of the C entry points in TWMainWnd.cpp guard against a null
     * window, and that is deliberate rather than an oversight: the rest
     * (displaygame, displaylist, cleardisplay, getselectedruleset, ...) sit on
     * GUI-only paths batch mode never reaches, and they dereference g_pMainWnd
     * unconditionally. So this asserts the two that ARE contracted, and does
     * not pretend the others are safe.
     */
    tw_case("🔴 setsubtitle() is safe with no window -- batch mode has none");
    /* jc-30's re-title after loadsettings() is on the common path and crashed
     * every batch run instantly. The corpus guard caught it, which is exactly
     * why that guard exists. */
    CHECK_MSG(g_pMainWnd == NULL, "this case requires that no window exists yet");
    setsubtitle("anything");
    setsubtitle(NULL);
    CHECK_MSG(true, "setsubtitle returned without a window");

    tw_case("readextensions() is safe with no window, for the same reason");
    /* This is also why no .ccx is ever parsed in batch mode, and therefore why
     * ccmetadata_test.cpp has to exist at all. */
    readextensions(NULL);
    CHECK_MSG(true, "readextensions returned without a window");
}

/* --- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    tw_begin("mainwnd");

    /* Set BEFORE the QApplication is constructed. Done here rather than left to
     * the runner so the binary behaves the same when a person runs it by hand
     * to debug a case, which is the whole reason tw_test.h keeps its markers
     * off by default. */
    if (qgetenv("QT_QPA_PLATFORM").isEmpty())
	qputenv("QT_QPA_PLATFORM", "offscreen");

    /* Before any window exists, which is the state batch mode runs in. */
    test_batch_guards();

    TileWorldApp app(argc, argv);
    g_pApp = &app;

    TileWorldMainWnd *w = new TileWorldMainWnd;
    g_pMainWnd = w;

    test_construction(w);
    test_shortmsg(w);
    test_deathcounter_menu(w);
    test_title(w);
    test_background(w);

    tw_case("the window tears down without crashing");
    /* ~TileWorldMainWnd releases the two surfaces and the tileset menu. A
     * destructor that faulted would take the process out AFTER every check had
     * passed, and tw_end() would never get to report it -- so it is its own
     * case, reached before the count is printed. */
    delete w;
    g_pMainWnd = NULL;
    CHECK_MSG(true, "destroyed");

    /* Raise this when cases are added; never lower it to make a run pass.
     * Exact, and both runners now check that it is -- see run-tests.ps1 and
     * run-sanitizers.sh. Nothing here is platform-dependent: the offscreen
     * platform is the same everywhere, which is most of why it was used. */
    tw_expect_atleast(43);
    return tw_end();
}
