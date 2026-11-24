#pragma once

#include "core/Types.h"

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace finily {

QPixmap glyphPixmap(const QString& kind, const QColor& color);
QPixmap dockPixmap(const QString& kind, const QColor& color);
QIcon dockIcon(const QString& kind, const QColor& rest, const QColor& active);
QIcon glyphIcon(const QString& kind, const QColor& color, const QColor& onColor = QColor());
QPixmap fillSwatchPixmap(ShadePattern pattern, const QColor& color, bool isDark);
QPixmap penTipPixmap(int tip, const QColor& ink, bool isDark);
QPixmap hlTipPixmap(int tip, const QColor& ink, bool isDark);
QPixmap contextIconPixmap(const QString& kind, const QColor& color);
QPixmap patternPixmap(LinePattern pattern, const QColor& color);
QPixmap templatePixmap(BackgroundTemplate tpl, const QColor& color);

} // namespace finily
