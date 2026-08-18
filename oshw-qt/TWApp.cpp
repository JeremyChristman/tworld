/* Copyright (C) 2001-2010 by Madhav Shanbhag,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#include "TWApp.h"
#include "TWMainWnd.h"

#include "../generic/generic.h"
#include "../oshw-sdl/sdlsfx.h"

#include "../gen.h"
#include "../defs.h"
#include "../fork.h"
#include "../oshw.h"
#include "../settings.h"
#include "TWTextCoder.h"

#include <QClipboard>

#include <cstring>
#include <cstdlib>

/* MOD (Jeremy): when linking against a static Qt, the windows platform
 * plugin must be compiled in explicitly (no platforms/qwindows.dll).
 * No effect on dynamic-Qt builds.
 */
#if defined(QT_STATIC) || defined(QT_STATICPLUGIN)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif


TileWorldApp* g_pApp = nullptr;
TileWorldMainWnd* g_pMainWnd = nullptr;


// MOD (Jeremy): fork build tag shown in the window/dialog title. Bump it on each production
// deploy (jc-1, jc-2, ...) so the running build is identifiable.
// Combined with the pack + level subtitle set in tworld.c, the title reads
// "Tile World [jc-N] - <pack> - <level>".
//
// ⚠ THE TAG ITSELF NOW LIVES IN ../fork.h (jc-34), not here. help.c needs the same string for the
// About box and cannot see a C++ QString, and two independently-edited copies of a version number
// is a bug waiting for the release where only one gets bumped. Bump FORK_BUILD_TAG; this line
// follows automatically. package.ps1 reads that header too.
//
// TOGGLEABLE AT RUNTIME (jc-30). The tag is useful while the engine is under active
// modification and just noise the rest of the time, so it is switched by a setting
// rather than by a rebuild:
//
//     tw_settings.ini         showbuildtag=1      -> "Tile World [jc-N]"
//                             showbuildtag=true   -> same
//                             anything else       -> "Tile World"
//                             (absent)            -> OFF, the default
//
// ⚠ THE DEFAULT WAS INVERTED IN jc-33, deliberately, and it must stay this way.
// Up to jc-32 the rule was "anything but an explicit 0 is ON", so every fresh
// download showed a build number in its title bar until the user found this key.
// Jeremy hands the GitHub link to other people and does not want them seeing one.
// The tag is now strictly OPT-IN, matching SuperCC's ShowBuildTag exactly. The
// acceptance test is a clean-room one: no tw_settings.ini at all -> launch -> no tag.
//
// ⚠ The tag is NOT the only way to identify a build -- the string is in the binary,
// so `strings` / a UTF-16LE search still names the release even with the tag off.
// Turning it off never makes a deployed exe unidentifiable.
const QString TileWorldApp::s_sTitle    = QStringLiteral("Tile World");
const QString TileWorldApp::s_sBuildTag = QStringLiteral("[" FORK_BUILD_TAG "]");

/* MOD (Jeremy, jc-33): ONE definition of "this switch is on", shared by every opt-in setting in
 * tw_settings.ini. Strictly opt-in: only "1" or "true" (any casing). Absent, blank, "0", garbage,
 * and a missing settings file all mean OFF, so a setting can never switch itself on by accident.
 *
 * A STRING read, not getintsetting(), on purpose: the file is meant to be hand-edited and
 * "showbuildtag=true" is what someone reading the README will naturally type. getintsetting()
 * cannot parse that and would silently report -1. Mirrors SuperCC's optedIn(). */
bool TileWorldApp::SettingOptedIn(char const *name)
{
	char const *raw = getstringsetting(name);
	if (raw == nullptr)
		return false;
	QString const s = QString::fromLatin1(raw).trimmed();
	return s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
	    || s == QLatin1String("1");
}

bool TileWorldApp::ShowBuildTag()
{
	return SettingOptedIn("showbuildtag");
}

/* MOD (Jeremy, jc-33): render the Games > Scores list the way Tile World 2.2 did.
 *
 *     tw_settings.ini    legacyscores=true   ->  the 2.2 look
 *                        (absent / anything else) -> today's look, the default
 *
 * 2.3.0 rebuilt Tile World on Qt5 (from Qt4) and SDL2 (from SDL1), and the score list's
 * appearance changed with the toolkit rather than by intent -- boxed column headers became a flat
 * strip, the font lightened, and the rows tightened. This restores that look; see
 * TileWorldMainWnd::ApplyScoreListStyle() for exactly what it changes and the honest limits of
 * reproducing a Qt4 style under Qt5. */
bool TileWorldApp::LegacyScores()
{
	return SettingOptedIn("legacyscores");
}

QString TileWorldApp::WindowTitle()
{
	if (!ShowBuildTag())
		return s_sTitle;
	return s_sTitle + QLatin1Char(' ') + s_sBuildTag;
}


TileWorldApp::TileWorldApp(int& argc, char** argv)
	:
	QApplication(argc, argv),
	m_bSilence(false),
	m_bShowHistogram(false),
	m_bFullScreen(false),
	m_argc(argc),
	m_argv(argv)
{
	g_pApp = this;
}


TileWorldApp::~TileWorldApp()
{
	delete g_pMainWnd;
	g_pMainWnd = nullptr;

	g_pApp = nullptr;
}


/* Process all pending events. If wait is TRUE and no events are
 * currently pending, the function blocks until an event arrives.
 */
static void _eventupdate(int wait)
{
	QApplication::processEvents(wait ? QEventLoop::WaitForMoreEvents : QEventLoop::AllEvents);
}


/* Initialize the OS/hardware interface. This function must be called
 * before any others in the oshw library. If silence is TRUE, the
 * sound system will be disabled, as if no soundcard was present. If
 * showhistogram is TRUE, then during shutdown the timer module will
 * send a histogram to stdout describing the amount of time the
 * program explicitly yielded to other processes. (This feature is for
 * debugging purposes.) soundbufsize is a number between 0 and 3 which
 * is used to scale the size of the sound buffer. A larger number is
 * more efficient, but pushes the sound effects farther out of
 * synchronization with the video.
 */
int oshwinitialize(int silence, int soundbufsize,
                   int showhistogram, int fullscreen)
{
	return g_pApp->Initialize(silence, soundbufsize, showhistogram, fullscreen);
}

bool TileWorldApp::Initialize(bool bSilence, int nSoundBufSize,
                              bool bShowHistogram, bool bFullScreen)
{
    geng.eventupdatefunc = _eventupdate;

	m_bSilence = bSilence;
	m_bShowHistogram = bShowHistogram;
	m_bFullScreen = bFullScreen;
	
	g_pMainWnd = new TileWorldMainWnd;
	g_pMainWnd->setWindowTitle(WindowTitle());

	if ( ! (
		_generictimerinitialize(bShowHistogram) &&
		_generictileinitialize() &&
		_genericinputinitialize() &&
		_sdlsfxinitialize(bSilence, nSoundBufSize)
	   ) )
		return false;
	
	if (bFullScreen)
	{
		g_pMainWnd->showFullScreen();
	}
	else
	{
		g_pMainWnd->adjustSize();
		g_pMainWnd->show();
	}
		
	return true;
}


/*
 * Resource-loading functions.
 */

/* Extract the font stored in the given file and make it the current
 * font. FALSE is returned if the attempt was unsuccessful. If
 * complain is FALSE, no error messages will be displayed.
 */
int loadfontfromfile(char const *filename, int complain)
{
	// N/A
	return true;
}

/* Free all memory associated with the current font.
 */
void freefont(void)
{
	// N/A
}

void copytoclipboard(char const *text)
{
	QClipboard* pClipboard = QApplication::clipboard();
	if (pClipboard == nullptr)
		return;
	pClipboard->setText(TWTextCoder::decode(text));
}

int TileWorldApp::RunTWorld()
{
    return tworld(m_argc, m_argv);
}


void TileWorldApp::ExitTWorld()
{
	// Attempt to gracefully destroy application objects
	
	// throw 1;
	// Can't throw C++ exceptions through C code
	
	// longjmp(m_jmpBuf, 1);
	// Works, but needs to be cleaner
	::exit(0);
	// Live with this for now...
}


/* The real main().
 */
int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		const char* szArg = argv[i];
		if (strlen(szArg) == 2  &&  szArg[0] == '-'  &&  strchr("lstbhdvV", szArg[1]) != nullptr)
			return tworld(argc, argv);
	}
	
	TileWorldApp app(argc, argv);
	QApplication::setStyle(QStringLiteral("fusion"));	// Other styles may mess up colors
	QApplication::setWindowIcon(QIcon(QStringLiteral(":/tworld2.ico")));

	return app.RunTWorld();
}
