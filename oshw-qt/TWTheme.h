/* TWTheme.h: MOD (Jeremy) -- the user-selectable window background color.
 *
 * Stock Tile World paints its main widget with a fixed blue (#285080) plus five
 * shades of that blue derived from it, all hardcoded in TWMainWnd.ui. This class
 * reproduces that derivation from an arbitrary color, so Options > Background
 * Color can retint the whole window and still look like one coherent theme.
 *
 * Everything here is pure color math plus two settings accessors -- no widgets,
 * no dialogs. The window class owns the "when" and this class owns the "what".
 */

#ifndef TWORLD_TWTHEME_H
#define TWORLD_TWTHEME_H

#include <QColor>
#include <QPalette>

class TWTheme
{
public:
	/* The stock Tile World blue, #285080. This is what Restore Default
	 * Background goes back to, and the fallback whenever the saved setting
	 * is missing or unparseable.
	 */
	static QColor defaultBackground();

	/* The background color recorded in <CC>\save\settings, or
	 * defaultBackground() if the key is absent or not a valid color.
	 * Only meaningful after loadsettings() has run.
	 */
	static QColor loadBackground();

	/* Record the color in the settings map and flush the file immediately,
	 * so the choice survives even if this session never shuts down cleanly.
	 */
	static void saveBackground(const QColor& color);

	/* White or black, whichever reads better against the given background
	 * (WCAG relative luminance). Keeps text legible when a pale color is
	 * chosen, where stock Tile World's unconditional white would vanish.
	 */
	static QColor contrastingText(const QColor& background);

	/* stock, with every blue-family role rederived from background. Roles
	 * that are not part of that family -- the black Base behind the level
	 * list and find box, the tooltip colors, the list's green Highlight --
	 * are passed through untouched, as are stock's resolve flags.
	 */
	static QPalette recolor(const QPalette& stock, const QColor& background);

private:
	/* Window is painted with the same subtle diagonal gradient stock Tile
	 * World builds in TileWorldMainWnd's constructor: lighter at the top
	 * left, darker at the bottom right.
	 */
	static QBrush backgroundBrush(const QColor& background);

	static qreal relativeLuminance(const QColor& color);
};

#endif //TWORLD_TWTHEME_H
