/* Copyright (C) 2001-2010 by Madhav Shanbhag,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#include "TWApp.h"
#include "TWMainWnd.h"

#include "../generic/generic.h"
#include "../oshw-sdl/sdlsfx.h"

#include "../gen.h"
#include "../defs.h"
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


// MOD (Jeremy): fork build tag shown in the window/dialog title. Bump s_sBuildTag
// on each production deploy (jc-1, jc-2, ...) so the running build is identifiable.
// Combined with the pack + level subtitle set in tworld.c, the title reads
// "Tile World [jc-N] - <pack> - <level>".
//
// TOGGLEABLE AT RUNTIME (jc-30). The tag is useful while the engine is under active
// modification and just noise the rest of the time, so it is switched by a setting
// rather than by a rebuild:
//
//     <CC>\save\settings      showbuildtag=0   -> "Tile World"
//                             showbuildtag=1   -> "Tile World [jc-N]"
//                             (absent)         -> ON, the default
//
// getintsetting() returns -1 for a missing key, so "anything but an explicit 0" is
// ON and a fresh install keeps the tag. The settings file is a plain key=value list
// that round-trips through a map, so hand-adding the line is safe and it survives
// savesettings().
//
// ⚠ The tag is NOT the only way to identify a build -- the string is in the binary,
// so `strings` / a UTF-16LE search still names the release even with the tag off.
// Turning it off never makes a deployed exe unidentifiable.
const QString TileWorldApp::s_sTitle    = QStringLiteral("Tile World");
const QString TileWorldApp::s_sBuildTag = QStringLiteral("[jc-31]");

bool TileWorldApp::ShowBuildTag()
{
	return getintsetting("showbuildtag") != 0;
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
