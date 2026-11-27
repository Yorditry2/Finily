#pragma once

#include "core/Types.h"

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPainter;

namespace finily {

struct HomePalette {
    QColor bg;
    QColor sidebar;
    QColor sidebarBorder;
    QColor surface;
    QColor surfaceAlt;
    QColor border;
    QColor text;
    QColor muted;
    QColor accent;
    QColor accentHover;
    QColor tint;
    QColor hover;
    QColor cardActionBg;
    QColor danger;
};

HomePalette homePaletteFor(bool dark);
QString homeScreenStylesheet(const HomePalette& pal, bool dark);

const QVector<QColor>& coverColors();
const QStringList& coverColorNames();
const QStringList& symbolNames();

QString relativeDateLabel(const QDateTime& when);
QIcon homeGlyph(const QString& kind, const QColor& color);
QIcon symbolGlyph(int iconIndex, const QColor& color);

QImage blurBackdropImage(const QImage& source, int radius = 10);
void paintLiquidGlassPanel(QPainter& painter, const QRectF& rect, qreal radius,
                           const QImage& blurredBackdrop, const HomePalette& pal, bool dark);
void paintEditorialFolder(QPainter& painter, const QRectF& bounds, const QColor& baseColor,
                          int iconIndex, bool dark);
void paintMiniTemplate(QPainter& painter, const QRectF& area, const ThemeConfig& theme,
                       BackgroundTemplate tpl, double scale = 1.0);
void paintSoftShadow(QPainter& painter, const QRectF& rect, qreal radius, qreal lift, bool dark);

class FinilyBadge : public QWidget {
public:
    explicit FinilyBadge(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace finily
