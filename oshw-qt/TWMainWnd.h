/* Copyright (C) 2001-2017 by Madhav Shanbhag and Eric Schmidt,
 * under the GNU General Public License. No warranty. See COPYING for details.
 */

#ifndef TWMAINWND_H
#define TWMAINWND_H


#include "ui_TWMainWnd.h"

#include "CCMetaData.h"

#include "../generic/generic.h"

#include "../gen.h"
#include "../defs.h"
#include "../state.h"
#include "../series.h"
#include "../oshw.h"

#include <QMainWindow>

#include <QColor>
#include <QLocale>
#include <QPalette>

class QSortFilterProxyModel;
class QMenu;
class QActionGroup;

class TileWorldMainWnd : public QMainWindow, protected Ui::TWMainWnd
{
	Q_OBJECT
	
public:
	enum Page
	{
		PAGE_GAME,
		PAGE_TABLE,
		PAGE_TEXT
	};

	TileWorldMainWnd(QWidget* pParent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());
	~TileWorldMainWnd();

	bool eventFilter(QObject* pObject, QEvent* pEvent) override;
	void closeEvent(QCloseEvent* pCloseEvent) override;
	void timerEvent(QTimerEvent*) override;

	bool SetKeyboardRepeat(bool bEnable);
	uint8_t* GetKeyState(int* pnNumKeys);
	int GetReplaySecondsToSkip() const;
	
	bool CreateGameDisplay();
	void ClearDisplay();
	bool DisplayGame(const gamestate* pState, int nTimeLeft, int nBestTime, bool showinitgamestate);
	bool SetDisplayMsg(const char* szMsg, int nMSecs, int nBoldMSecs);
	/* MOD (Jeremy, jc-37): death total to display, or -1 to stop displaying it. */
	void SetDeathCount(int nCount);
	/* MOD (Jeremy, jc-37): show or hide Reset/Set to match the Death Counter checkbox. */
	void UpdateDeathCounterMenu();
	int DisplayEndMessage(int nBaseScore, int nTimeScore, long lTotalScore, int nCompleted);
	int DisplayList(const char* szTitle, const tablespec* pTableSpec, int* pnIndex,
			DisplayListType eListType, int (*pfnInputCallback)(int*));
	int DisplayInputPrompt(const char* szPrompt, char* pInput, int nMaxLen,
			InputPromptType eInputType, int (*pfnInputCallback)());
	int GetSelectedRuleset();
	void SetSubtitle(const char* szSubtitle);
	
	void ReadExtensions(gameseries* pSeries);
	void Narrate(CCX::Text CCX::Level::*pmTxt, bool bForce = false);
	
	void ShowAbout();

private slots:
	void OnListItemActivated(const QModelIndex& index);
	void OnFindTextChanged(const QString& sText);
	void OnFindReturnPressed();
	void OnRulesetSwitched(bool mschecked);
	void OnPlayback();
	void OnSpeedValueChanged(int nValue);
	void OnSpeedSliderReleased();	
	void OnSeekPosChanged(int nValue);
	void OnTextNext();
	void OnTextPrev();
	void OnTextReturn();
	void OnCopyText();
	void OnMenuActionTriggered(QAction* pAction);
	void OnBackgroundColorPreview(const QColor& color);

	/* MOD (Jeremy, jc-41): user-selectable tileset. */
	void OnTilesetMenuAboutToShow();
	void OnTilesetChosen(QAction* pAction);

private:
	bool HandleEvent(QObject* pObject, QEvent* pEvent);
	void SetCurrentPage(Page ePage);
	void CheckForProblems(const gamestate* pState);
	void DisplayMapView(const gamestate* pState);
	void DisplayShutter();
	void SetSpeed(int nValue);
	void ReleaseAllKeys();
	void PulseKey(int nTWKey);
	int GetTWKeyForAction(QAction* pAction) const;

	/* MOD (Jeremy): user-selectable background color. See TWTheme. */
	QColor StockBackground() const;
	void SetBackgroundColor(const QColor& color, bool bSave);
	void ChooseBackgroundColor();

	/* MOD (Jeremy, jc-41): user-selectable tileset. The submenu is rebuilt every time it
	 * opens, so a file dropped into res\tilesets while the game is running appears without
	 * a restart. m_pTilesetMenu is owned by the Options menu; the group by the menu. */
	void BuildTilesetMenu();
	bool ApplyTileset(const QString& sFilename);
	QMenu* m_pTilesetMenu;
	QActionGroup* m_pTilesetGroup;
	
	enum HintMode { HINT_EMPTY, HINT_TEXT, HINT_INITSTATE };
	bool SetHintMode(HintMode newmode);

	bool m_bSetupUi;
	bool m_bWindowClosed;

	/* MOD (Jeremy, jc-33): legacy (Tile World 2.2) score-list styling. The table widget is shared
	 * with the level-set picker, solution list and help pages, so the style is applied for the
	 * score list only and cleared again afterwards; m_bScoreListStyled tracks which state it is
	 * in, and m_nDefaultRowHeight remembers the stock row height to restore. */
	void ApplyScoreListStyle(bool bLegacy);

	/* MOD (Jeremy, jc-36): apply the tablespec's column spans to the view. Must run after the
	 * column widths are computed, and again after every Find-box filter change. */
	void ApplyTableSpans();
	bool m_bScoreListStyled = false;
	int m_nDefaultRowHeight = 0;
	static int const kLegacyRowHeight = 25;
	
	Qt_Surface* m_pSurface;
	Qt_Surface* m_pInvSurface;
	TW_Rect m_disploc;
	
	uint8_t m_nKeyState[TWK_LAST];

	/* MOD (Jeremy, jc-37): bSticky marks a message pushed with FOREVER -- in practice only
	 * "(paused)" (tworld.c) and "Verifying ..." (tworld.c). Those two are state indicators rather
	 * than notifications, and the death counter yields to them; it overrides everything with a
	 * finite timeout. The flag is captured at PUSH time because nMSecs is not retained, and
	 * comparing nMsgUntil against a clock afterwards cannot tell the two apart. */
	struct MessageData{ QString sMsg; uint32_t nMsgUntil, nMsgBoldUntil; bool bSticky; };
	QVector<MessageData> m_shortMessages;

	/* MOD (Jeremy, jc-37): the death counter, as the display layer sees it. m_nDeathCount is the
	 * lifetime total pushed here by play.c via deathcountchanged(); -1 means "do not display".
	 * The core owns the number -- this is a cache for painting, never the authority. */
	int m_nDeathCount = -1;
	/* The ONE place m_pLblShortMsg's text and color are decided. Every write of that label goes
	 * through here so the precedence rule cannot be bypassed by a caller that paints first. */
	void RefreshShortMsgLabel();
	
	bool m_bKbdRepeatEnabled;

	int m_nRuleset;
	int m_nLevelNum;
	bool m_bProblematic;
	bool m_bOFNT;
	int m_nBestTime;
	HintMode m_hintMode;
	int m_nTimeLeft;
	bool m_bTimedLevel;
	bool m_bReplay;
    QString m_title;
    QString m_author;

	/* MOD (Jeremy): the palette as TWMainWnd.ui built it, captured before any
	 * recoloring. Every retint derives from this, never from the widget's
	 * current palette -- deriving from the current one would compound, and
	 * its Window role is a gradient brush with no single color to read back.
	 */
	QPalette m_stockPalette;
	QColor m_bgColor;

	QSortFilterProxyModel* m_pSortFilterProxyModel;
	QLocale m_locale;
	
	CCX::Levelset m_ccxLevelset;
	
	QString m_sTextToCopy;
};


#endif
