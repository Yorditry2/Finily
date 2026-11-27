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

} // namespace finily
