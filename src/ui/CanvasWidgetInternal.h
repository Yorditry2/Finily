#pragma once

#include "core/Utils.h"

#include <QElapsedTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>

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

class InlineTextEditor final : public QLineEdit {
public:
    explicit InlineTextEditor(QWidget* parent)
        : QLineEdit(parent) {}

    TextItem draft;
    double origFontSize = 16.0;
    int origFamilyIndex = 0;
    bool origBold = false;
    bool origItalic = false;
    QString origText;
    std::function<void(const QString&)> commit;
    std::function<void()> onClosed;

    void applyTextStyle(double zoom, const QColor& textColor, const QColor& borderColor) {
        const double pxSize = std::max(10.0, draft.fontSize * zoom);
        setFont(textFont(pxSize, draft.fontFamilyIndex, draft.bold, draft.italic));
        const QFontMetrics fm(font());
        const int minW = std::max(150, fm.horizontalAdvance(text().isEmpty() ? QStringLiteral("Ag") : text()) + 28);
        const int h = std::max(28, fm.height() + 10);
        resize(std::max(width(), minW), h);
        const auto bg = textColor.lightness() > 140
            ? QStringLiteral("rgba(12,18,32,0.88)")
            : QStringLiteral("rgba(255,255,255,0.92)");
        setStyleSheet(QStringLiteral(
            "QLineEdit { background: %1; color: %2; border: 1.5px solid %3;"
            " border-radius: 6px; padding: 2px 6px; }")
            .arg(bg, textColor.name(), borderColor.name()));
    }

    void finish(bool accept) {
        if (done)
            return;
        done = true;
        const auto value = text();
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
                    || k == Qt::Key_A || k == Qt::Key_Z || k == Qt::Key_Y);
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Tab
                || k == Qt::Key_Backspace || k == Qt::Key_Delete || ctrlTextEdit
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

} // namespace finily
