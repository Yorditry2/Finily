#pragma once

#include "core/Types.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>

namespace finily {

inline QString freshId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline bool isFinite(double value) {
    return std::isfinite(value);
}

inline double vecLength(const QPointF& p) {
    return std::hypot(p.x(), p.y());
}

inline double pointDistance(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

inline std::unique_ptr<QSettings> appSettings() {
    return std::make_unique<QSettings>(QStringLiteral("Finily"), QStringLiteral("Finily"));
}

inline QVariant readAppSetting(const QString& key, const QVariant& defaultValue = {}) {
    QSettings settings(QStringLiteral("Finily"), QStringLiteral("Finily"));
    return settings.value(key, defaultValue);
}

inline void writeAppSetting(const QString& key, const QVariant& value) {
    QSettings settings(QStringLiteral("Finily"), QStringLiteral("Finily"));
    settings.setValue(key, value);
}

inline void removeAppSetting(const QString& key) {
    QSettings settings(QStringLiteral("Finily"), QStringLiteral("Finily"));
    settings.remove(key);
}

inline QFont textFont(double size, int familyIndex = 0, bool bold = false, bool italic = false) {
    QFont font;
    if (familyIndex == 1)
        font.setFamilies({QStringLiteral("Caveat"), QStringLiteral("Segoe Print"), QStringLiteral("Comic Sans MS"), QStringLiteral("cursive")});
    else if (familyIndex == 2)
        font.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"), QStringLiteral("Consolas"), QStringLiteral("monospace")});
    else
        font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Sans Serif")});
    font.setPixelSize(std::max(1, qRound(size)));
    font.setBold(bold);
    font.setItalic(italic);
    return font;
}

inline QFont textFont(const TextItem& item) {
    return textFont(item.fontSize, item.fontFamilyIndex, item.bold, item.italic);
}

inline QRectF textBounds(const TextItem& item) {
    const QFontMetricsF metrics(textFont(item));
    const QStringList lines = item.text.split(QLatin1Char('\n'));
    const double lineStep = std::max(metrics.height(), metrics.lineSpacing());
    QRectF total;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        const QPointF topLeft = item.pos + QPointF(0.0, double(i) * lineStep);
        const QRectF ink = metrics.boundingRect(line).translated(topLeft + QPointF(0.0, metrics.ascent()));
        const QRectF adv(topLeft, QSizeF(metrics.horizontalAdvance(line), metrics.height()));
        const QRectF lineRect = ink.united(adv);
        total = (i == 0) ? lineRect : total.united(lineRect);
    }
    return total;
}

inline void drawTextItem(QPainter& painter, const TextItem& item) {
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QFont font = textFont(item);
    painter.setFont(font);
    painter.setPen(item.color);
    const QFontMetricsF metrics(font);
    const QStringList lines = item.text.split(QLatin1Char('\n'));
    const double lineStep = std::max(metrics.height(), metrics.lineSpacing());
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QPointF baseline = item.pos + QPointF(0.0, double(i) * lineStep + metrics.ascent());
        painter.drawText(baseline, lines.at(i));
    }
    painter.restore();
}

inline bool isValidPageFormat(int value) {
    return value >= int(PageFormat::Infinite) && value <= int(PageFormat::Square);
}

inline PageFormat pageFormatFromInt(int value, PageFormat fallback = PageFormat::Infinite) {
    return isValidPageFormat(value) ? static_cast<PageFormat>(value) : fallback;
}

inline double clampTemplateScale(double scale) {
    return isFinite(scale) ? std::clamp(scale, 0.5, 2.0) : 1.0;
}

// An empty rectangle denotes the unbounded canvas.
inline QRectF sheetRectForPageFormat(PageFormat format) {
    switch (format) {
    case PageFormat::A4:
        return QRectF(-397.0, -561.5, 794.0, 1123.0);
    case PageFormat::A4Landscape:
        return QRectF(-561.5, -397.0, 1123.0, 794.0);
    case PageFormat::Slide16x9:
        return QRectF(-640.0, -360.0, 1280.0, 720.0);
    case PageFormat::A5:
        return QRectF(-280.0, -397.0, 560.0, 794.0);
    case PageFormat::A5Landscape:
        return QRectF(-397.0, -280.0, 794.0, 560.0);
    case PageFormat::Letter:
        return QRectF(-408.0, -528.0, 816.0, 1056.0);
    case PageFormat::LetterLandscape:
        return QRectF(-528.0, -408.0, 1056.0, 816.0);
    case PageFormat::Legal:
        return QRectF(-408.0, -672.0, 816.0, 1344.0);
    case PageFormat::LegalLandscape:
        return QRectF(-672.0, -408.0, 1344.0, 816.0);
    case PageFormat::Square:
        return QRectF(-450.0, -450.0, 900.0, 900.0);
    case PageFormat::Infinite:
        return {};
    }
    return {};
}

inline Viewport fittedSheetViewport(PageFormat format, const QSize& size) {
    const auto sheet = sheetRectForPageFormat(format);
    if (sheet.isEmpty() || size.width() <= 0 || size.height() <= 0)
        return {};
    constexpr double topClearance = 104.0;
    if (sheet.width() > sheet.height()) {
        // Formatos apaisados: centrados y dejando >= 94 px arriba para librar la barra (84 px)
        const double availW = std::max(320.0, size.width() - 156.0);
        const double availH = std::max(220.0, size.height() - 196.0);
        const double zoom = std::clamp(std::min(availW / sheet.width(), availH / sheet.height()), 0.3, 5.0);
        return {size.width() / 2.0 - sheet.center().x() * zoom,
                size.height() / 2.0 - sheet.center().y() * zoom,
                zoom};
    }
    // Hoja vertical (A4/Carta): centrada horizontalmente y con el borde superior libre bajo la barra (84 px)
    const double availW = std::max(320.0, size.width() - 180.0);
    const double availH = std::max(220.0, size.height() - 196.0);
    const double fitFullH = availH / sheet.height();
    const double comfortableZoom = std::min(availW / sheet.width(), fitFullH * 1.32);
    const double zoom = std::clamp(std::max(fitFullH, comfortableZoom), 0.3, 5.0);
    const double x = size.width() / 2.0 - sheet.center().x() * zoom;
    const double sheetScreenH = sheet.height() * zoom;
    const double y = (sheetScreenH <= availH)
        ? (size.height() / 2.0 - sheet.center().y() * zoom)
        : (topClearance - sheet.top() * zoom);
    return {x, y, zoom};
}

inline Viewport fittedPdfViewport(const QRectF& bounds, const QSize& size) {
    const double w = std::max(1.0, size.width() - 140.0);
    const double h = std::max(1.0, size.height() - 196.0);
    const double zoom = std::clamp(std::min(w / bounds.width(), h / bounds.height()), 0.2, 5.0);
    return {size.width() / 2.0 - bounds.center().x() * zoom,
            size.height() / 2.0 - bounds.center().y() * zoom,
            zoom};
}

inline QVector<int> validPageIndices(QVector<int> indices, int count) {
    indices.removeIf([count](int index) { return index < 0 || index >= count; });
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

} // namespace finily
