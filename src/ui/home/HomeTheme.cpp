#include "ui/home/HomeTheme.h"

#include "core/Utils.h"

#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {

void boxBlurPass(QImage& src, QImage& dst, int radius, bool horizontal) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0 || radius <= 0)
        return;
    const int window = radius * 2 + 1;
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;

    for (int i = 0; i < outer; ++i) {
        int sumA = 0, sumR = 0, sumG = 0, sumB = 0;
        auto sample = [&](int k) -> QRgb {
            const int clamped = std::clamp(k, 0, inner - 1);
            return horizontal ? reinterpret_cast<const QRgb*>(src.constScanLine(i))[clamped]
                              : reinterpret_cast<const QRgb*>(src.constScanLine(clamped))[i];
        };
        for (int k = -radius; k <= radius; ++k) {
            const QRgb px = sample(k);
            sumA += qAlpha(px);
            sumR += qRed(px);
            sumG += qGreen(px);
            sumB += qBlue(px);
        }
        for (int j = 0; j < inner; ++j) {
            const QRgb out = qRgba(sumR / window, sumG / window, sumB / window, sumA / window);
            if (horizontal)
                reinterpret_cast<QRgb*>(dst.scanLine(i))[j] = out;
            else
                reinterpret_cast<QRgb*>(dst.scanLine(j))[i] = out;
            const QRgb leaving = sample(j - radius);
            const QRgb entering = sample(j + radius + 1);
            sumA += qAlpha(entering) - qAlpha(leaving);
            sumR += qRed(entering) - qRed(leaving);
            sumG += qGreen(entering) - qGreen(leaving);
            sumB += qBlue(entering) - qBlue(leaving);
        }
    }
}

void boostSaturationPremul(QImage& image, float factor) {
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = row[x];
            const int a = qAlpha(px);
            if (a == 0)
                continue;
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            const int nr = std::clamp(int(std::lround(luma + factor * (r - luma))), 0, a);
            const int ng = std::clamp(int(std::lround(luma + factor * (g - luma))), 0, a);
            const int nb = std::clamp(int(std::lround(luma + factor * (b - luma))), 0, a);
            row[x] = qRgba(nr, ng, nb, a);
        }
    }
}

void paintGlassBody(QPainter& painter, const QRectF& rect, qreal radius, const QImage& backdrop,
                    const QColor& glassFill, const QColor& solidFill, const QColor& topHighlight,
                    const QColor& rimBorder, qreal shadowLift, bool darkShadow) {
    if (rect.isEmpty())
        return;
    paintSoftShadow(painter, rect, radius, shadowLift, darkShadow);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);

    painter.save();
    painter.setClipPath(clip);
    if (!backdrop.isNull() && !tokens::reducedMotion()) {
        painter.drawImage(rect, backdrop);
        painter.fillRect(rect, glassFill);
    } else {
        painter.fillRect(rect, solidFill);
    }
    const qreal inset = std::min(radius * 0.45, rect.width() * 0.25);
    painter.setPen(QPen(topHighlight, 1.0));
    painter.drawLine(QPointF(rect.left() + inset, rect.top() + 1.5),
                     QPointF(rect.right() - inset, rect.top() + 1.5));
    painter.restore();

    painter.setPen(QPen(rimBorder, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

} // namespace

HomePalette homePaletteFor(bool dark) {
    const tokens::Palette t = tokens::paletteFor(dark);
    const QColor accentHover = (tokens::accentPreset() == 0)
        ? (dark ? QColor("#FFFFFF") : QColor("#27272A"))
        : (dark ? t.accent.lighter(115) : t.accent.lighter(128));
    return {
        t.bg,      t.sidebar, t.border, t.surface,  t.field,
        t.borderPop, t.text,  t.muted,  t.accent,   accentHover,
        t.soft,    t.hover,   t.glass,  t.danger,   t.accentFg,
        t.icon,    t.faint,
    };
}

QString homeScreenStylesheet(const HomePalette& pal, bool dark) {
    const QString selBg = dark ? QStringLiteral("rgba(255,255,255,60)")
                               : QStringLiteral("rgba(24,24,27,36)");
    return QStringLiteral(R"(
QLabel { color: %1; background: transparent; }
QLabel#finilyBrandTitle { color: %2; font-size: 16px; font-weight: 700; }
QLabel#finilySectionTitle { color: %2; font-size: 19px; font-weight: 600; }
QLabel#finilyCountPill { color: %1; font-size: 13.5px; }
QLabel#finilySidebarGroup { color: %1; font-size: 11px; font-weight: 700; letter-spacing: 0.7px; }
QLineEdit#finilySearch {
    background: transparent; border: none;
    color: %2; padding: 0px; font-size: 14.5px;
    selection-background-color: %10; selection-color: %2;
}
QToolButton { background: transparent; border: none; color: %2; font-size: 14px; }
QToolButton#finilyCrumbBtn { color: %1; font-size: 18px; font-weight: 500; padding: 2px 5px; border-radius: 7px; }
QToolButton#finilyCrumbBtn:hover { color: %2; background: %8; }
QToolButton#finilyCardAction { background: %7; border: 1px solid %5; border-radius: 14px; }
QToolButton#finilyCardAction:hover { border: 1px solid %3; }
QScrollArea, QWidget#finilyHomeGrid, QWidget#finilySidebarTree { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 4px 2px 4px 0px; }
QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QWidget#finilyUpdateBanner { background: %4; border: 1px solid %5; border-radius: 12px; }
QLabel#finilyUpdateText { color: %2; font-size: 13px; font-weight: 600; }
QToolButton#finilyUpdatePrimary {
    background: %3; color: %9; border: none; border-radius: 11px;
    padding: 0px 14px; font-size: 12px; font-weight: 600;
}
QToolButton#finilyUpdatePrimary:disabled { background: %5; color: %1; }
QToolButton#finilyUpdateGhost {
    background: %6; color: %2; border: 1px solid %5; border-radius: 11px;
    padding: 0px 12px; font-size: 12px; font-weight: 600;
}
QToolButton#finilyUpdateGhost:hover { border: 1px solid %3; }
QToolButton#finilyUpdateClose {
    background: transparent; color: %1; border: none; border-radius: 11px;
    font-size: 15px; font-weight: 700;
}
QToolButton#finilyUpdateClose:hover { color: %2; }
)")
        .arg(pal.muted.name(QColor::HexArgb))
        .arg(pal.text.name(QColor::HexArgb))
        .arg(pal.accent.name(QColor::HexArgb))
        .arg(pal.tint.name(QColor::HexArgb))
        .arg(pal.border.name(QColor::HexArgb))
        .arg(pal.surface.name(QColor::HexArgb))
        .arg(pal.cardActionBg.name(QColor::HexArgb))
        .arg(pal.hover.name(QColor::HexArgb))
        .arg(pal.accentFg.name(QColor::HexArgb))
        .arg(selBg);
}

const QVector<QColor>& coverColors() {
    // Tapas de cuaderno (mockup 00); mismo orden que tokens::folderColor().
    static const QVector<QColor> colors = {
        QColor("#E799AE"), QColor("#DDBB9C"), QColor("#8FBCA6"), QColor("#94B6DD"),
        QColor("#B3A2DC"), QColor("#EDA984"), QColor("#E3D27C"), QColor("#3A3D47"),
    };
    return colors;
}

const QStringList& coverColorNames() {
    static const QStringList names = {
        QStringLiteral("Rosa"), QStringLiteral("Arena"), QStringLiteral("Salvia"),
        QStringLiteral("Cielo"), QStringLiteral("Lavanda"), QStringLiteral("Durazno"),
        QStringLiteral("Limón"), QStringLiteral("Grafito"),
    };
    return names;
}

const QStringList& symbolNames() {
    static const QStringList names = {
        QStringLiteral("Sin ícono"), QStringLiteral("Libro"), QStringLiteral("Matemática"),
        QStringLiteral("Ciencias"), QStringLiteral("Geometría"), QStringLiteral("Apuntes"),
        QStringLiteral("Destacado"), QStringLiteral("Ideas"),
    };
    return names;
}

QString relativeDateLabel(const QDateTime& when) {
    if (!when.isValid())
        return {};
    const QDate date = when.date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return QStringLiteral("Hoy ") + when.time().toString(QStringLiteral("HH:mm"));
    if (date == today.addDays(-1))
        return QStringLiteral("Ayer");
    const QLocale spanish(QLocale::Spanish);
    if (date.year() == today.year())
        return spanish.toString(date, QStringLiteral("d MMM"));
    return spanish.toString(date, QStringLiteral("d MMM yyyy"));
}

QImage blurBackdropImage(const QImage& source, int radius) {
    if (tokens::reducedMotion())
        return QImage();
    if (source.isNull() || radius <= 0)
        return source;
    QImage argb = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QSize smallSize(std::max(1, argb.width() / 4), std::max(1, argb.height() / 4));
    QImage a = argb.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage b(smallSize, QImage::Format_ARGB32_Premultiplied);
    const int r = std::clamp(radius / 2, 2, 8);
    for (int pass = 0; pass < 2; ++pass) {
        boxBlurPass(a, b, r, true);
        boxBlurPass(b, a, r, false);
    }
    boostSaturationPremul(a, 1.35f);
    QImage result = a.scaled(argb.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    result.setDevicePixelRatio(source.devicePixelRatio());
    return result;
}

void paintLiquidGlassPanel(QPainter& painter, const QRectF& rect, qreal radius,
                           const QImage& blurredBackdrop, const HomePalette& pal, bool dark) {
    Q_UNUSED(pal);
    const tokens::Palette tk = tokens::paletteFor(dark);
    paintGlassBody(painter, rect, radius, blurredBackdrop, tk.glass, tk.surface,
                   tk.glassHi, tk.borderPop, 5.0, dark);
}

void paintToastGlass(QPainter& painter, const QRectF& rect, qreal radius, const QImage& backdrop) {
    paintGlassBody(painter, rect, radius, backdrop, QColor(24, 24, 27, 219),
                   QColor(0x18, 0x18, 0x1B), QColor(255, 255, 255, 20),
                   QColor(255, 255, 255, 23), 4.0, true);
}

void paintEditorialFolder(QPainter& painter, const QRectF& bounds, const QColor& baseColor,
                          int iconIndex, bool dark) {
    QColor base = dark ? baseColor.darker(114) : baseColor;
    const qreal l = bounds.left(), t = bounds.top(), w = bounds.width(), h = bounds.height();
    const qreal tabH = h * 0.14;
    const qreal tabW = w * 0.40;

    QPainterPath back;
    back.moveTo(l + 8, t);
    back.lineTo(l + tabW - 6, t);
    back.quadTo(l + tabW + 2, t, l + tabW + 8, t + tabH);
    back.lineTo(l + w - 10, t + tabH);
    back.quadTo(l + w, t + tabH, l + w, t + tabH + 10);
    back.lineTo(l + w, t + h - 10);
    back.quadTo(l + w, t + h, l + w - 10, t + h);
    back.lineTo(l + 10, t + h);
    back.quadTo(l, t + h, l, t + h - 10);
    back.lineTo(l, t + 8);
    back.quadTo(l, t, l + 8, t);
    painter.setPen(Qt::NoPen);
    painter.setBrush(base.darker(122));
    painter.drawPath(back);

    // Asomo discreto de hoja blanca en la ranura
    painter.setBrush(QColor(255, 255, 255, 215));
    painter.drawRoundedRect(QRectF(l + 9, t + tabH + 3, w - 18, 12), 4.0, 4.0);

    const QRectF front(l, t + tabH + 8, w, h - tabH - 8);
    QLinearGradient fill(front.topLeft(), front.bottomRight());
    fill.setColorAt(0.0, base.lighter(110));
    fill.setColorAt(1.0, base.darker(104));
    painter.setBrush(fill);
    painter.drawRoundedRect(front, 11.0, 11.0);

    painter.setPen(QPen(QColor(255, 255, 255, 72), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(front.left() + 8, front.top() + 0.8),
                     QPointF(front.right() - 8, front.top() + 0.8));

    if (iconIndex > 0) {
        const qreal sz = std::min(front.width(), front.height()) * 0.42;
        const QRectF iconBox(front.center().x() - sz / 2.0, front.center().y() - sz / 2.0, sz, sz);
        symbolGlyph(iconIndex, QColor(255, 255, 255, 235)).paint(&painter, iconBox.toRect());
    }
}

void paintMiniTemplate(QPainter& painter, const QRectF& area, const ThemeConfig& theme,
                       BackgroundTemplate tpl, double scale) {
    if (tpl == BackgroundTemplate::Blank || area.isEmpty())
        return;
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);

    QColor lineColor = theme.patternColor;
    const bool ruled = (tpl == BackgroundTemplate::Ruled || tpl == BackgroundTemplate::Cornell
                        || tpl == BackgroundTemplate::School);
    if (ruled && theme.id == ThemeId::PaperLight)
        lineColor = QColor(137, 181, 231, 140);

    const qreal step = std::clamp(15.0 * clampTemplateScale(scale), 7.0, 28.0);
    if (tpl == BackgroundTemplate::Dots) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(lineColor);
        for (qreal y = area.top() + step * 0.65; y < area.bottom() - 2.0; y += step) {
            for (qreal x = area.left() + step * 0.65; x < area.right() - 2.0; x += step)
                painter.drawEllipse(QPointF(x, y), 1.0, 1.0);
        }
    } else if (ruled) {
        painter.setPen(QPen(lineColor, 0.85));
        for (qreal y = area.top() + step; y < area.bottom() - 2.0; y += step)
            painter.drawLine(QPointF(area.left() + 3.0, y), QPointF(area.right() - 3.0, y));
    } else {
        painter.setPen(QPen(lineColor, 0.8));
        for (qreal x = area.left() + step; x < area.right() - 1.0; x += step)
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        for (qreal y = area.top() + step; y < area.bottom() - 1.0; y += step)
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    painter.restore();
}

void paintSoftShadow(QPainter& painter, const QRectF& rect, qreal radius, qreal lift, bool dark) {
    painter.save();
    painter.setPen(Qt::NoPen);
    const int base = dark ? 68 : 20;
    for (int i = 0; i < 5; ++i) {
        const qreal spread = 1.2 + i * (1.4 + lift * 0.2);
        const int alpha = std::max(2, int(base * (1.0 - i / 5.0)) / 2);
        painter.setBrush(QColor(8, 10, 20, alpha));
        painter.drawRoundedRect(
            rect.adjusted(-spread * 0.5, 1.5 + lift * 0.5 + i, spread * 0.5, spread + lift),
            radius + spread * 0.4, radius + spread * 0.4);
    }
    painter.restore();
}

FinilyBadge::FinilyBadge(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(32, 32);
    setFocusPolicy(Qt::NoFocus);
}

void FinilyBadge::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF frame = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    // Marcador neutro «Tinta» hasta elegir el logo definitivo (DESIGN.md).
    const tokens::Palette t = tokens::paletteFor(m_dark);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.accent);
    painter.drawRoundedRect(frame, 9.0, 9.0);

    QPainterPath f;
    f.moveTo(20.2, 9.0);
    f.cubicTo(17.4, 7.2, 14.2, 8.2, 14.2, 12.4);
    f.lineTo(14.2, 23.8);
    painter.setPen(QPen(t.accentFg, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(f);
    painter.drawLine(QPointF(10.8, 14.8), QPointF(18.8, 14.8));
}

void FinilyBadge::setDark(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    update();
}

} // namespace finily
