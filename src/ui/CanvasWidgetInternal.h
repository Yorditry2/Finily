#pragma once

#include "CanvasWidget.h"
#include "engine/InkEngine.h"
#include "engine/PdfEngine.h"

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include <QPainter>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>

namespace finily {

// Transient highlighter layers live with the widget without expanding its public
// interface. Keeping ink separate lets a live highlight move beneath cached ink.
class HighlighterLayers final : public QObject {
public:
    explicit HighlighterLayers(CanvasWidget* owner)
        : QObject(owner) {
        setObjectName(QStringLiteral("_finily_highlighter_layers"));
    }
    QImage background;
    QImage ink;
};

inline HighlighterLayers& highlighterLayers(CanvasWidget* widget) {
    auto* child = widget->findChild<QObject*>(QStringLiteral("_finily_highlighter_layers"),
                                             Qt::FindDirectChildrenOnly);
    return child ? *static_cast<HighlighterLayers*>(child) : *new HighlighterLayers(widget);
}

inline Viewport fittedSheetViewport(PageFormat format, const QSize& size) {
    const auto sheet = sheetRectForPageFormat(format);
    if (sheet.isEmpty() || size.width() <= 0 || size.height() <= 0)
        return {};
    if (format == PageFormat::Slide16x9) {
        // Diapo 16:9: centrada y ocupando el área útil entre el dock y las cápsulas
        const double availW = std::max(320.0, size.width() - 156.0);
        const double availH = std::max(240.0, size.height() - 124.0);
        const double zoom = std::clamp(std::min(availW / sheet.width(), availH / sheet.height()), 0.3, 5.0);
        return {size.width() / 2.0 - sheet.center().x() * zoom,
                size.height() / 2.0 - sheet.center().y() * zoom,
                zoom};
    }
    // Hoja A4 vertical: centrada horizontalmente con ancho de escritura cómodo y alineada arriba
    const double availW = std::max(320.0, size.width() - 180.0);
    const double availH = std::max(240.0, size.height() - 108.0);
    const double fitFullH = availH / sheet.height();
    const double comfortableZoom = std::min(availW / sheet.width(), fitFullH * 1.32);
    const double zoom = std::clamp(std::max(fitFullH, comfortableZoom), 0.3, 5.0);
    const double x = size.width() / 2.0 - sheet.center().x() * zoom;
    const double sheetScreenH = sheet.height() * zoom;
    const double y = (sheetScreenH <= availH)
        ? (size.height() / 2.0 - sheet.center().y() * zoom)
        : (62.0 - sheet.top() * zoom);
    return {x, y, zoom};
}

inline Viewport fittedPdfViewport(const QRectF& bounds, const QSize& size) {
    const double w = std::max(1.0, size.width() - 140.0);
    const double h = std::max(1.0, size.height() - 110.0);
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

inline qint64 nowMs() {
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

inline bool finite(double v) {
    return std::isfinite(v);
}

inline QString freshId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline double distance(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

inline QFont textFont(double size) {
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(qRound(size));
    return font;
}

inline QRectF textBounds(const TextItem& item) {
    const QFontMetricsF metrics(textFont(item.fontSize));
    const QRectF ink = metrics.boundingRect(item.text).translated(item.pos + QPointF(0, metrics.ascent()));
    return ink.united(QRectF(item.pos, QSizeF(metrics.horizontalAdvance(item.text), metrics.height())));
}

inline void drawTextItem(QPainter& painter, const TextItem& item) {
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(textFont(item.fontSize));
    painter.setPen(item.color);
    painter.drawText(item.pos + QPointF(0, QFontMetricsF(painter.font()).ascent()), item.text);
    painter.restore();
}

class InlineTextEditor final : public QLineEdit {
public:
    explicit InlineTextEditor(QWidget* parent)
        : QLineEdit(parent) {
        setObjectName(QStringLiteral("_finily_text_editor"));
    }
    std::function<void(const QString&)> commit;
    void finish(bool accept) {
        if (done)
            return;
        done = true;
        const auto value = text();
        hide();
        if (accept && commit)
            commit(value);
        deleteLater();
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            const auto* ke = static_cast<QKeyEvent*>(event);
            const auto k = ke->key();
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Tab
                || k == Qt::Key_Backspace || k == Qt::Key_Delete
                || (!ke->modifiers().testFlag(Qt::ControlModifier)
                    && !ke->modifiers().testFlag(Qt::AltModifier))) {
                event->accept();
                return true;
            }
        }
        return QLineEdit::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            finish(false);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finish(true);
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        finish(true);
    }

private:
    bool done = false;
};

inline void finishTextEditor(CanvasWidget* widget, bool accept) {
    for (auto* child : widget->findChildren<QLineEdit*>(QStringLiteral("_finily_text_editor"),
                                                        Qt::FindDirectChildrenOnly))
        static_cast<InlineTextEditor*>(child)->finish(accept);
}

inline void syncWorkingPage(QVector<PageData>& pages, int& index,
                            const DocumentSnapshot& document, Viewport viewport) {
    if (pages.isEmpty())
        pages.append(PageData{});
    index = std::clamp(index, 0, int(pages.size()) - 1);
    auto& page = pages[index];
    page.strokes = document.strokes;
    page.shapes = document.shapes;
    page.images = document.images;
    page.texts = document.texts;
    page.viewport = viewport;
}

} // namespace finily
