#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
class QApplication;

namespace StudioTheme {
// OKLCH D65 to sRGB: OKLab/LMS matrix, sRGB transfer, channel clipping.
// L normally lies in [0,1], C >= 0, hue is in degrees. Inputs must be finite.
// RGB out-of-gamut components and alpha are clipped to [0,1].
QColor fromOklch(double L, double C, double degrees, double alpha = 1);
// Case-sensitive Lovable names; unknown names return an invalid QColor.
// background foreground card popover primary primaryFG secondary muted mutedFG
// accent border input ok warn err power surface1 surface2 surface3
QColor token(const QString &name);
QPalette palette();
QString stylesheet();
// Call on the GUI thread before creating widgets; selects Fusion and installs
// the palette and QSS globally. Remove local legacy QSS that would override it.
void apply(QApplication &app);
}
