#pragma once

#include "core/Utils.h"

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPlainTextEdit>

#include <algorithm>
#include <cmath>
#include <functional>

namespace finily {

inline qint64 nowMs() {
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

class InlineTextEditor final : public QPlainTextEdit {
public:
    explicit InlineTextEditor(QWidget* parent)
        : QPlainTextEdit(parent) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setTabChangesFocus(false);
    }

    TextItem draft;
    double origFontSize = 22.0;
    int origFamilyIndex = 0;
    bool origBold = false;
    bool origItalic = false;
    QString origText;
    std::function<void(const QString&)> commit;
    std::function<void()> onClosed;

    QString text() const { return toPlainText(); }
    void setText(const QString& s) { setPlainText(s); }

    void adjustSizeToText(int minW = 180) {
        const QFontMetricsF fm(font());
        const QStringList lines = toPlainText().split(QLatin1Char('\n'));
        qreal maxW = 0.0;
        for (const QString& ln : lines)
            maxW = std::max(maxW, fm.horizontalAdvance(ln));
        const int lineCount = std::max(1, int(lines.size()));
        const int targetW = std::max(minW, int(std::ceil(maxW)) + 30);
        const int targetH = std::max(32, int(std::ceil(fm.lineSpacing() * lineCount)) + 16);
        resize(targetW, targetH);
    }

    void applyTextStyle(double zoom, const QColor& textColor, const QColor& borderColor) {
        const double pxSize = std::max(10.0, draft.fontSize * zoom);
        setFont(textFont(pxSize, draft.fontFamilyIndex, draft.bold, draft.italic));
        adjustSizeToText(180);
        const auto bg = textColor.lightness() > 140
            ? QStringLiteral("rgba(12,18,32,0.88)")
            : QStringLiteral("rgba(255,255,255,0.92)");
        setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background: %1; color: %2; border: 1.5px solid %3;"
            " border-radius: 6px; padding: 3px 6px; }")
            .arg(bg, textColor.name(), borderColor.name()));
    }

    void finish(bool accept) {
        if (done)
            return;
        done = true;
        const auto value = toPlainText();
        hide();
        if (accept && commit)
            commit(value);
        if (onClosed)
            onClosed();
        deleteLater();
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            const auto* ke = static_cast<QKeyEvent*>(event);
            const auto k = ke->key();
            const bool ctrlTextEdit = ke->modifiers().testFlag(Qt::ControlModifier)
                && !ke->modifiers().testFlag(Qt::AltModifier)
                && (k == Qt::Key_C || k == Qt::Key_V || k == Qt::Key_X
                    || k == Qt::Key_A || k == Qt::Key_Z || k == Qt::Key_Y
                    || k == Qt::Key_Return || k == Qt::Key_Enter);
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Tab
                || k == Qt::Key_Backspace || k == Qt::Key_Delete || ctrlTextEdit
                || (!ke->modifiers().testFlag(Qt::ControlModifier)
                    && !ke->modifiers().testFlag(Qt::AltModifier))) {
                event->accept();
                return true;
            }
        }
        return QPlainTextEdit::event(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            finish(false);
            event->accept();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && event->modifiers().testFlag(Qt::ControlModifier)) {
            finish(true);
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
        QPlainTextEdit::focusOutEvent(event);
        finish(true);
    }

private:
    bool done = false;
};

} // namespace finily
