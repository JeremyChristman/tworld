/* TWTheme.cpp: MOD (Jeremy) -- the user-selectable window background color.
 *
 * See TWTheme.h.
 */

#include "TWTheme.h"

#include "../settings.h"

#include <QBrush>
#include <QByteArray>
#include <QLinearGradient>
#include <QString>

#include <cmath>

namespace
{
	/* The key written to tw_settings.ini, as "#rrggbb". A string rather
	 * than a packed int so the file stays hand-editable, the way the
	 * showbuildtag line is.
	 */
	char const *const BGCOLOR_SETTING = "bgcolor";

	/* The five shades TWMainWnd.ui derives from #285080. These factors
	 * reproduce its literals exactly: Light 60,120,192 / Midlight
	 * 50,100,160 / Dark 20,40,64 / Mid 26,53,85.
	 */
	int const LIGHT_FACTOR    = 150;
	int const MIDLIGHT_FACTOR = 125;
	int const DARK_FACTOR     = 200;
	int const MID_FACTOR      = 150;

	/* The gradient stock Tile World applies over the flat window color. */
	int const GRADIENT_LIGHTEN = 125;
	int const GRADIENT_DARKEN  = 125;
}


QColor TWTheme::loadBackground(const QColor& fallback)
{
	char const *saved = getstringsetting(BGCOLOR_SETTING);
	if (saved == nullptr)
		return fallback;

	/* An unparseable value is treated as "no preference" rather than as an
	 * error: a hand-edited settings file should never be able to stop the
	 * game from starting, and the stock blue is always a safe answer.
	 */
	QColor color(QString::fromLatin1(saved).trimmed());
	if (!color.isValid())
		return fallback;

	/* A translucent window background is not something the picker can
	 * produce, but a hand-edited "#80285080" would be.
	 */
	color.setAlpha(255);
	return color;
}


void TWTheme::saveBackground(const QColor& color)
{
	QByteArray const name = color.name(QColor::HexRgb).toLatin1();
	setstringsetting(BGCOLOR_SETTING, name.constData());

	/* Stock Tile World only flushes settings from shutdownsystem(). The
	 * background color is a deliberate, visible choice, so write it out now
	 * -- this is the same whole-map rewrite that already runs at exit, just
	 * earlier.
	 */
	savesettings();
}


qreal TWTheme::relativeLuminance(const QColor& color)
{
	qreal channel[3] = { color.redF(), color.greenF(), color.blueF() };
	for (int i = 0; i < 3; ++i)
	{
		channel[i] = (channel[i] <= 0.03928)
			? channel[i] / 12.92
			: std::pow((channel[i] + 0.055) / 1.055, 2.4);
	}
	return 0.2126 * channel[0] + 0.7152 * channel[1] + 0.0722 * channel[2];
}


QColor TWTheme::contrastingText(const QColor& background)
{
	/* WCAG contrast ratios against white and against black; whichever wins
	 * is the readable choice. For the stock blue this picks white (8.2:1
	 * versus 2.5:1), so the default look is unchanged.
	 */
	qreal const luminance = relativeLuminance(background);
	qreal const againstWhite = 1.05 / (luminance + 0.05);
	qreal const againstBlack = (luminance + 0.05) / 0.05;
	return (againstWhite >= againstBlack) ? QColor(Qt::white) : QColor(Qt::black);
}


QBrush TWTheme::backgroundBrush(const QColor& background)
{
	QLinearGradient gradient(0, 0, 1, 1);
	gradient.setCoordinateMode(QGradient::StretchToDeviceMode);
	gradient.setColorAt(0, background.lighter(GRADIENT_LIGHTEN));
	gradient.setColorAt(1, background.darker(GRADIENT_DARKEN));
	return QBrush(gradient);
}


QPalette TWTheme::recolor(const QPalette& stock, const QColor& background)
{
	QColor const light    = background.lighter(LIGHT_FACTOR);
	QColor const midlight = background.lighter(MIDLIGHT_FACTOR);
	QColor const dark     = background.darker(DARK_FACTOR);
	QColor const mid      = background.darker(MID_FACTOR);
	QColor const text     = contrastingText(background);

	QPalette palette(stock);

	QPalette::ColorGroup const enabled[2] = { QPalette::Active, QPalette::Inactive };
	for (int i = 0; i < 2; ++i)
	{
		QPalette::ColorGroup const group = enabled[i];

		/* Foregrounds that sit on the background itself follow it. Text
		 * and PlaceholderText are deliberately left alone: they sit on
		 * Base, which stays black.
		 */
		palette.setColor(group, QPalette::WindowText, text);
		palette.setColor(group, QPalette::ButtonText, text);

		palette.setColor(group, QPalette::Button, background);
		palette.setColor(group, QPalette::Light, light);
		palette.setColor(group, QPalette::Midlight, midlight);
		palette.setColor(group, QPalette::Dark, dark);
		palette.setColor(group, QPalette::Mid, mid);
		palette.setColor(group, QPalette::AlternateBase, dark);
	}

	/* Disabled text is a darker shade of the background rather than a fixed
	 * gray, exactly as the stock palette does it, so it reads as grayed out
	 * whatever color is chosen.
	 */
	palette.setColor(QPalette::Disabled, QPalette::WindowText, dark);
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, dark);
	palette.setColor(QPalette::Disabled, QPalette::Text, dark);
	palette.setColor(QPalette::Disabled, QPalette::Button, background);
	palette.setColor(QPalette::Disabled, QPalette::Light, light);
	palette.setColor(QPalette::Disabled, QPalette::Midlight, midlight);
	palette.setColor(QPalette::Disabled, QPalette::Dark, dark);
	palette.setColor(QPalette::Disabled, QPalette::Mid, mid);
	palette.setColor(QPalette::Disabled, QPalette::Base, background);
	palette.setColor(QPalette::Disabled, QPalette::AlternateBase, background);

	/* setBrush(role, brush) covers all three groups, which is what the
	 * stock constructor did with its gradient.
	 */
	palette.setBrush(QPalette::Window, backgroundBrush(background));

	return palette;
}
