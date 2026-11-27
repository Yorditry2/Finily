#pragma once

#include "core/Types.h"

#include <QColor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSlider>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace finily {

enum class ToolbarDockPos { Izquierda, Arriba, Abajo };
enum class PageNavDirection { Horizontal, Vertical };

inline constexpr QSize thumbnailSize(124, 82);

inline constexpr double kPenLevels[10] = {0.6, 1.0, 1.5, 2.2, 3.2, 4.5, 6.5, 9.5, 14.0, 22.0};
inline constexpr double kHlLevels[10]  = {6.0, 9.0, 12.0, 16.0, 20.0, 25.0, 30.0, 36.0, 44.0, 54.0};
inline constexpr double kHlOpacityLevels[10] = {0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.42, 0.50, 0.58, 0.68};

inline int sizeToLevel(double px, bool isHighlighter) {
    const double* arr = isHighlighter ? kHlLevels : kPenLevels;
    int best = 0;
    double bestDist = 1e9;
    for (int i = 0; i < 10; ++i) {
        const double d = std::abs(arr[i] - px);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best + 1;
}

inline QWidget* makeSeparator(QWidget* parent) {
    auto* separator = new QWidget(parent);
    separator->setObjectName("finilySep");
    separator->setFixedSize(1, 20);
    return separator;
}

inline QWidget* makeDivider(QWidget* parent) {
    auto* divider = new QWidget(parent);
    divider->setObjectName("finilyDivider");
    divider->setFixedHeight(1);
    return divider;
}

inline void polishButton(QToolButton* button) {
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
}

inline void makeMenuRow(QToolButton* button) {
    QString text = button->text();
    QString shortcut;
    const int open = text.lastIndexOf(" (");
    if (open > 0 && text.endsWith(')')) {
        shortcut = text.mid(open + 2, text.size() - open - 3);
        text = text.left(open);
    }
    button->setText(QString());
    button->setObjectName("finilyMenuRow");
    auto* row = new QHBoxLayout(button);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(8);
    auto* title = new QLabel(text, button);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(title);
    row->addStretch(1);
    if (!shortcut.isEmpty()) {
        auto* hint = new QLabel(shortcut.replace("Shift", "Mayús"), button);
        hint->setObjectName("finilyShortcut");
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(hint);
    }
}

// Cápsula de cristal antialias pintada a mano: reemplaza el fondo QSS de los
// QWidget planos para que el estilo de sistema Qt6 (Caelestia / KDE / Breeze)
// jamás la sobreescriba con cuadrados gris carbón.
class GlassCapsule : public QWidget {
    Q_OBJECT

public:
    explicit GlassCapsule(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
    }

    ~GlassCapsule() override = default;

    virtual void applyTheme(const ThemeConfig& theme, const QString& qss) {
        setCapsuleTheme(theme);
        setStyleSheet(qss);
    }

    virtual void syncFromCanvas() {}

    void setCapsuleTheme(const ThemeConfig& theme) {
        switch (theme.id) {
            case ThemeId::PaperLight:
                m_fill = QColor(255, 255, 255, 246);
                m_border = QColor(15, 23, 42, 26);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::PaperCream:
                m_fill = QColor(253, 250, 242, 246);
                m_border = QColor(100, 82, 54, 34);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::GreenBoard:
                m_fill = QColor(18, 43, 36, 244);
                m_border = QColor(255, 255, 255, 32);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::DarkBoard:
            default:
                m_fill = QColor(26, 30, 38, 242);
                m_border = QColor(255, 255, 255, 28);
                m_highlight = QColor(255, 255, 255, 18);
                break;
        }
        update();
    }

    void setCapsuleRadius(int radius) {
        m_radius = radius;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_fill);
        painter.drawRoundedRect(frame, m_radius, m_radius);
        painter.setPen(QPen(m_border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame, m_radius, m_radius);
        if (m_highlight.alpha() > 0) {
            QPen highlightPen(m_highlight, 1.0);
            highlightPen.setCapStyle(Qt::FlatCap);
            painter.setPen(highlightPen);
            const double inset = 2.0;
            QPainterPath highlight;
            highlight.moveTo(frame.left() + m_radius * 0.55, frame.top() + inset);
            highlight.lineTo(frame.right() - m_radius * 0.55, frame.top() + inset);
            painter.strokePath(highlight, highlightPen);
        }
    }

private:
    QColor m_fill{26, 30, 38, 242};
    QColor m_border{255, 255, 255, 28};
    QColor m_highlight{255, 255, 255, 0};
    int m_radius = 22;
};

// Fila con interruptor (texto a la izquierda, switch a la derecha).
class SwitchButton : public QToolButton {
public:
    explicit SwitchButton(QWidget* parent) : QToolButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setObjectName("finilySwitch");
    }
    void setColors(const QColor& text, const QColor& accent, bool dark) {
        m_text = text;
        m_accent = accent;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(m_text);
        QFont f = font();
        f.setPixelSize(13);
        p.setFont(f);
        p.drawText(QRect(4, 0, width() - 56, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        const QRectF track(width() - 40, height() / 2.0 - 10, 34, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? m_accent : QColor(128, 128, 128, m_dark ? 90 : 110));
        p.drawRoundedRect(track, 10, 10);
        p.setBrush(isChecked() && m_dark ? QColor(16, 20, 18) : QColor(255, 255, 255));
        p.drawEllipse(QPointF(track.left() + (isChecked() ? 24.0 : 10.0), track.center().y()), 7.5, 7.5);
    }

private:
    QColor m_text{230, 235, 240};
    QColor m_accent{91, 227, 164};
    bool m_dark = true;
};

// Punto de color antialias para presets de tinta y paleta del popover "Trazo".
class ColorDotButton : public QToolButton {
public:
    explicit ColorDotButton(QWidget* parent = nullptr)
        : QToolButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(28, 28);
    }

    void setDotColor(const QColor& color) {
        if (m_color == color)
            return;
        m_color = color;
        update();
    }
    void setAccentColor(const QColor& color) {
        if (m_accent == color)
            return;
        m_accent = color;
        update();
    }
    void setRimColor(const QColor& color) {
        if (m_rim == color)
            return;
        m_rim = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPointF center = QRectF(rect()).center();
        const double r = width() >= 28 ? 9.0 : 8.5;
        if (underMouse() && !isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(m_rim.red(), m_rim.green(), m_rim.blue(), 28));
            painter.drawEllipse(center, r + 3.5, r + 3.5);
        }
        painter.setPen(QPen(m_rim, 1.0));
        painter.setBrush(m_color);
        painter.drawEllipse(center, r, r);
        if (isChecked()) {
            QPen ring(m_accent, 2.0);
            painter.setPen(ring);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, r + 3.0, r + 3.0);
        }
    }

private:
    QColor m_color{Qt::white};
    QColor m_accent{"#60a5fa"};
    QColor m_rim{0, 0, 0, 60};
};

// Punto de vista previa en vivo del popover "Trazo".
class StrokePreviewWidget : public QWidget {
public:
    explicit StrokePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(28, 28);
        setFocusPolicy(Qt::NoFocus);
    }

    void setInk(const QColor& color, double size) {
        if (m_color == color && qFuzzyCompare(m_size, size))
            return;
        m_color = color;
        m_size = size;
        update();
    }
    void setHollow(bool hollow) {
        if (m_hollow == hollow)
            return;
        m_hollow = hollow;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF ring = rect().adjusted(2.0, 2.0, -2.0, -2.0);
        if (m_hollow) {
            const double diameter = std::clamp(m_size, 5.0, ring.width());
            QColor fill = m_color;
            fill.setAlpha(36);
            painter.setPen(QPen(m_color, 1.5));
            painter.setBrush(fill);
            painter.drawEllipse(ring.center(), diameter / 2.0, diameter / 2.0);
            return;
        }
        QColor guideColor = m_color;
        guideColor.setAlpha(70);
        QPen guidePen(guideColor, 1.0);
        painter.setPen(guidePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring);
        const double diameter = std::clamp(m_size, 0.8, ring.width());
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(ring.center(), diameter / 2.0, diameter / 2.0);
    }

private:
    QColor m_color{Qt::white};
    double m_size = 2.6;
    bool m_hollow = false;
};

// Slider minimalista con puntitos discretos sobre la línea.
class DotStepSlider : public QSlider {
public:
    explicit DotStepSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent) {
        setFixedHeight(26);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }
    void setColors(const QColor& accent, bool isDark) {
        m_accent = accent;
        m_isDark = isDark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int lo = minimum(), hi = maximum();
        const int steps = std::max(1, hi - lo);
        const double pad = 9.0;
        const double y = height() / 2.0;
        const double span = std::max(1.0, width() - pad * 2.0);
        const double frac = double(value() - lo) / double(steps);
        const double curX = pad + frac * span;

        const QColor trackCol = m_isDark ? QColor(255, 255, 255, 42) : QColor(15, 23, 42, 38);
        p.setPen(QPen(trackCol, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(pad, y), QPointF(pad + span, y));

        p.setPen(QPen(m_accent, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(pad, y), QPointF(curX, y));

        const QColor idleDot = m_isDark ? QColor(255, 255, 255, 110) : QColor(15, 23, 42, 95);
        for (int v = lo; v <= hi; ++v) {
            const double x = pad + (double(v - lo) / double(steps)) * span;
            const bool active = (v == value());
            const bool filled = (v <= value());
            if (active) {
                p.setPen(QPen(m_isDark ? QColor("#ffffff") : QColor("#0f172a"), 1.6));
                p.setBrush(m_accent);
                p.drawEllipse(QPointF(x, y), 5.6, 5.6);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(filled ? m_accent : idleDot);
                p.drawEllipse(QPointF(x, y), 2.4, 2.4);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        setValueFromX(event->position().x());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton)
            setValueFromX(event->position().x());
        event->accept();
    }

private:
    void setValueFromX(double x) {
        const int lo = minimum(), hi = maximum();
        const double pad = 9.0;
        const double span = std::max(1.0, width() - pad * 2.0);
        const double t = std::clamp((x - pad) / span, 0.0, 1.0);
        setValue(lo + qRound(t * (hi - lo)));
    }
    QColor m_accent{"#10b981"};
    bool m_isDark = true;
};

} // namespace finily
