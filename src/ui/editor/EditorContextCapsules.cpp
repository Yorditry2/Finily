#include "ui/editor/EditorContextCapsules.h"

#include "core/ShapeProtocols.h"
#include "engine/StickerLibrary.h"
#include "ui/CanvasWidget.h"
#include "ui/UiIcons.h"

#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace finily {

namespace {

// Botón circular de color 34x34 con círculo de 20 px + doble anillo (DESIGN §7.5.2).
class CapsuleColorSwatchBtn : public QWidget {
public:
    CapsuleColorSwatchBtn(const QColor& col, bool dark, QWidget* parent = nullptr)
        : QWidget(parent), m_color(col), m_dark(dark), m_pal(editorPaletteFor(dark)) {
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(QStringLiteral("Color"));
    }
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (m_hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(rect(), 10.0, 10.0);
        }
        const QPointF c = QRectF(rect()).center();
        const qreal r = 10.0; // diámetro 20 px
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 89) : QColor(15, 23, 42, 70), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r + 2.5, r + 2.5);
        p.setPen(QPen(m_pal.surfaceSolid, 2.0));
        p.setBrush(m_color.isValid() ? m_color : m_pal.text);
        p.drawEllipse(c, r, r);
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClicked)
            onClicked();
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    QColor m_color;
    bool m_dark = false;
    EditorPalette m_pal;
    bool m_hovered = false;
};

// Ficha de variante en el piso superior (alto 34, padding 7 izq / 10 der, glifo 20 px + nombre 12.5 px).
class CapsuleVariantChip : public QWidget {
public:
    CapsuleVariantChip(const QString& label, bool active, bool dark,
                       std::function<void(QPainter&, const QRectF&, const QColor&)> glyphPainter,
                       QWidget* parent = nullptr)
        : QWidget(parent),
          m_label(label),
          m_active(active),
          m_dark(dark),
          m_pal(editorPaletteFor(dark)),
          m_paintGlyph(std::move(glyphPainter)) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        const QFontMetricsF fm(editorFont(12.5, active ? QFont::DemiBold : QFont::Medium));
        const int w = int(std::ceil(7.0 + 20.0 + 6.0 + fm.horizontalAdvance(label) + 10.0));
        setFixedSize(std::max(54, w), 34);
    }
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(rect());
        if (m_active) {
            QColor bg = m_pal.accent;
            bg.setAlphaF(0.16);
            p.setPen(QPen(m_pal.accent, 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 10.0, 10.0);
        } else if (m_hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QColor fg = (m_active || m_hovered) ? m_pal.text : (m_dark ? QColor("#A1A1AA") : m_pal.muted);
        const QRectF glyphR(7.0, (height() - 20.0) * 0.5, 20.0, 20.0);
        if (m_paintGlyph)
            m_paintGlyph(p, glyphR, fg);

        p.setFont(editorFont(12.5, m_active ? QFont::DemiBold : QFont::Medium));
        p.setPen(fg);
        p.drawText(QRectF(glyphR.right() + 6.0, 0.0, width() - glyphR.right() - 14.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClicked)
            onClicked();
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    QString m_label;
    bool m_active = false;
    bool m_dark = false;
    EditorPalette m_pal;
    std::function<void(QPainter&, const QRectF&, const QColor&)> m_paintGlyph;
    bool m_hovered = false;
};

// Segmentado oscuro de estilo de trazo (Sólida / Guiones / Puntos), DESIGN §7.5.2.
class CapsuleStyleSeg : public QWidget {
public:
    CapsuleStyleSeg(LinePattern activePat, bool disabledForDim, bool dark, QWidget* parent = nullptr)
        : QWidget(parent),
          m_activePat(activePat),
          m_disabled(disabledForDim),
          m_dark(dark),
          m_pal(editorPaletteFor(dark)) {
        setFixedSize(2 + 32 * 3 + 2, 34);
        setMouseTracking(true);
        setCursor(disabledForDim ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        if (disabledForDim)
            setToolTip(QStringLiteral("La cota siempre es sólida"));
    }
    std::function<void(LinePattern)> onPicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (m_disabled)
            p.setOpacity(0.35);

        p.setPen(Qt::NoPen);
        p.setBrush(m_dark ? QColor("#12141A") : m_pal.subtle);
        p.drawRoundedRect(rect(), 11.0, 11.0);

        const LinePattern pats[3] = {LinePattern::Solid, LinePattern::Dashed, LinePattern::Dotted};
        const char* icons[3] = {"line-solid", "line-dashed", "line-dotted"};
        for (int i = 0; i < 3; ++i) {
            const QRectF r(2.0 + i * 32.0, 2.0, 32.0, 30.0);
            const bool on = (!m_disabled && m_activePat == pats[i]) || (m_disabled && i == 0);
            if (on) {
                p.setBrush(m_dark ? QColor("#2E323D") : m_pal.surfaceSolid);
                p.drawRoundedRect(r, 9.0, 9.0);
            } else if (!m_disabled && i == m_hover) {
                p.setBrush(m_dark ? QColor(255, 255, 255, 14) : m_pal.hover);
                p.drawRoundedRect(r, 9.0, 9.0);
            }
            const QColor fg = on ? m_pal.text : (m_dark ? QColor("#A1A1AA") : m_pal.muted);
            paintUiIcon(&p, QRectF(r.center().x() - 9.0, r.center().y() - 9.0, 18.0, 18.0),
                        QString::fromLatin1(icons[i]), fg, QStringLiteral("editor"));
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_disabled) return;
        const int idx = std::clamp(int((e->position().x() - 2.0) / 32.0), 0, 2);
        if (idx != m_hover) {
            m_hover = idx;
            static const char* kTips[3] = {"Sólida", "Guiones", "Puntos"};
            setToolTip(QString::fromUtf8(kTips[idx]));
            update();
        }
    }
    void leaveEvent(QEvent*) override { m_hover = -1; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (m_disabled || e->button() != Qt::LeftButton) return;
        const int idx = std::clamp(int((e->position().x() - 2.0) / 32.0), 0, 2);
        const LinePattern pats[3] = {LinePattern::Solid, LinePattern::Dashed, LinePattern::Dotted};
        if (onPicked) onPicked(pats[idx]);
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    LinePattern m_activePat;
    bool m_disabled = false;
    bool m_dark = false;
    EditorPalette m_pal;
    int m_hover = -1;
};

// Icono dinámico de porción de pizza (20x20 px) que refleja el ángulo actual.
class CapsulePizzaIcon : public QWidget {
public:
    CapsulePizzaIcon(double deg, bool dark, QWidget* parent = nullptr)
        : QWidget(parent), m_deg(deg), m_pal(editorPaletteFor(dark)) {
        setFixedSize(24, 34);
    }
    void setDegrees(double deg) { m_deg = deg; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF c(2.0, (height() - 20.0) * 0.5, 20.0, 20.0);
        p.setPen(QPen(m_pal.muted, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c.adjusted(1.5, 1.5, -1.5, -1.5));

        QColor fill = m_pal.accent;
        fill.setAlphaF(0.30);
        p.setPen(QPen(m_pal.accent, 1.6));
        p.setBrush(fill);
        if (m_deg >= 359.0) {
            p.drawEllipse(c.adjusted(1.5, 1.5, -1.5, -1.5));
        } else {
            p.drawPie(c.adjusted(1.5, 1.5, -1.5, -1.5), 90 * 16, -int(std::round(m_deg * 16.0)));
        }
    }

private:
    double m_deg = 360.0;
    EditorPalette m_pal;
};

QFont textFamilyPreviewFont(int idx, qreal px) {
    QFont f;
    if (idx == 1) {
        f.setFamilies({QStringLiteral("Caveat"), QStringLiteral("Segoe Print"),
                       QStringLiteral("Comic Neue"), QStringLiteral("Inter")});
        f.setPixelSize(int(std::round(px * 1.25)));
    } else if (idx == 2) {
        f.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"),
                       QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")});
        f.setPixelSize(int(std::round(px)));
    } else {
        f = editorFont(px, QFont::Medium);
    }
    return f;
}

QString textFamilyLabel(int idx) {
    if (idx == 1) return QStringLiteral("Manuscrita");
    if (idx == 2) return QStringLiteral("Mono");
    return QStringLiteral("Inter");
}

// Botón «Fuente ▾» de T1 (nombre escrito en su propia fuente).
class CapsuleFontButton : public QWidget {
public:
    CapsuleFontButton(int famIdx, bool active, bool dark, QWidget* parent = nullptr)
        : QWidget(parent),
          m_famIdx(famIdx),
          m_active(active),
          m_dark(dark),
          m_pal(editorPaletteFor(dark)) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(QStringLiteral("Fuente"));
        const QFont f = textFamilyPreviewFont(famIdx, 13.0);
        const int tw = int(std::ceil(QFontMetricsF(f).horizontalAdvance(textFamilyLabel(famIdx))));
        setFixedSize(std::max(72, tw + 30), 34);
    }
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(rect());
        if (m_active) {
            QColor bg = m_pal.accent;
            bg.setAlphaF(0.16);
            p.setPen(QPen(m_pal.accent, 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 10.0, 10.0);
        } else if (m_hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }

        const QColor fg = (m_active || m_hovered) ? m_pal.text : (m_dark ? QColor("#D4D4D8") : m_pal.text);
        p.setFont(textFamilyPreviewFont(m_famIdx, 13.0));
        p.setPen(fg);
        p.drawText(QRectF(10.0, 0.0, width() - 24.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, textFamilyLabel(m_famIdx));

        QColor chevCol = fg;
        chevCol.setAlphaF(0.60);
        p.setFont(editorFont(10.0, QFont::Medium));
        p.setPen(chevCol);
        p.drawText(QRectF(width() - 18.0, 0.0, 12.0, height()),
                   Qt::AlignCenter, QStringLiteral("▾"));
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClicked)
            onClicked();
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    int m_famIdx = 0;
    bool m_active = false;
    bool m_dark = false;
    EditorPalette m_pal;
    bool m_hovered = false;
};

// Botón −/+ con repetición automática cada 80 ms tras 400 ms al mantener pulsado (DESIGN §7.7).
class CapsuleRepeatIconBtn : public QWidget {
public:
    CapsuleRepeatIconBtn(const QString& icon, bool dark, QWidget* parent = nullptr)
        : QWidget(parent), m_icon(icon), m_dark(dark), m_pal(editorPaletteFor(dark)) {
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        m_delayTimer = new QTimer(this);
        m_delayTimer->setSingleShot(true);
        m_delayTimer->setInterval(400);
        m_repeatTimer = new QTimer(this);
        m_repeatTimer->setInterval(80);
        QObject::connect(m_delayTimer, &QTimer::timeout, this, [this] {
            if (m_pressed) {
                if (onStep) onStep();
                m_repeatTimer->start();
            }
        });
        QObject::connect(m_repeatTimer, &QTimer::timeout, this, [this] {
            if (m_pressed && onStep) onStep();
        });
    }
    std::function<void()> onStep;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (m_hovered || m_pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, m_pressed ? 24 : 15) : m_pal.hover);
            p.drawRoundedRect(rect(), 10.0, 10.0);
        }
        paintUiIcon(&p, QRectF(8.0, 8.0, 18.0, 18.0), m_icon,
                    m_hovered ? m_pal.text : (m_dark ? QColor("#B4B6BF") : m_pal.icon),
                    QStringLiteral("ui"));
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override {
        m_hovered = false;
        m_pressed = false;
        m_delayTimer->stop();
        m_repeatTimer->stop();
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_pressed = true;
            if (onStep) onStep();
            m_delayTimer->start();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_pressed = false;
            m_delayTimer->stop();
            m_repeatTimer->stop();
            update();
        }
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    QString m_icon;
    bool m_dark = false;
    EditorPalette m_pal;
    bool m_hovered = false;
    bool m_pressed = false;
    QTimer* m_delayTimer = nullptr;
    QTimer* m_repeatTimer = nullptr;
};

// Botón-interruptor B/I de T1 con estilo activo de §7.5 (acento 16% + borde 1.5 px).
class CapsuleToggleIconBtn : public QWidget {
public:
    CapsuleToggleIconBtn(const QString& icon, bool active, bool dark, const QString& tip, QWidget* parent = nullptr)
        : QWidget(parent), m_icon(icon), m_active(active), m_dark(dark), m_pal(editorPaletteFor(dark)) {
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tip);
    }
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(rect());
        if (m_active) {
            QColor bg = m_pal.accent;
            bg.setAlphaF(0.16);
            p.setPen(QPen(m_pal.accent, 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 10.0, 10.0);
        } else if (m_hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 10.0, 10.0);
        }
        const QColor fg = (m_active || m_hovered) ? m_pal.text : (m_dark ? QColor("#B4B6BF") : m_pal.icon);
        paintUiIcon(&p, QRectF(8.0, 8.0, 18.0, 18.0), m_icon, fg, QStringLiteral("editor"));
    }
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClicked)
            onClicked();
    }
    void tabletEvent(QTabletEvent* e) override { e->ignore(); }

private:
    QString m_icon;
    bool m_active = false;
    bool m_dark = false;
    EditorPalette m_pal;
    bool m_hovered = false;
};

} // namespace

// ---------------- EditorFontPopover (T1) ----------------

EditorFontPopover::EditorFontPopover(QWidget* parent)
    : EditorGlassCard(parent) {
    setRole(Role::PopoverSolid);
    setCornerRadius(14.0);
    setFixedSize(168, 132); // 6 + 3*40 + 6
    setMouseTracking(true);
    hide();

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(160);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_openT = v.toReal();
        setRevealProgress(m_openT);
        update();
    });
}

void EditorFontPopover::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

QRectF EditorFontPopover::rowRect(int idx) const {
    return QRectF(6.0, 6.0 + idx * 40.0, width() - 12.0, 40.0);
}

void EditorFontPopover::openAbove(const QRect& anchorInParent, const QSize& bounds, int activeFamilyIdx) {
    m_activeIdx = std::clamp(activeFamilyIdx, 0, 2);
    m_hoverIdx = -1;
    const int margin = 10;
    const int x = std::clamp(anchorInParent.center().x() - width() / 2,
                             margin, std::max(margin, bounds.width() - width() - margin));
    const int y = std::clamp(anchorInParent.top() - height() - 8,
                             margin, std::max(margin, bounds.height() - height() - margin));
    move(x, y);
    show();
    raise();
    if (!tokens::reducedMotion()) {
        m_openAnim->stop();
        m_openT = 0.0;
        setRevealProgress(0.0);
        m_openAnim->setStartValue(0.0);
        m_openAnim->setEndValue(1.0);
        m_openAnim->start();
    } else {
        m_openT = 1.0;
        setRevealProgress(1.0);
    }
    emit visibilityChanged();
    update();
}

void EditorFontPopover::closePopover() {
    if (!isVisible())
        return;
    m_openAnim->stop();
    m_hoverIdx = -1;
    hide();
    emit visibilityChanged();
}

void EditorFontPopover::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_openT < 0.999) {
        p.setOpacity(m_openT);
        p.translate(0.0, (1.0 - m_openT) * 6.0);
    }

    for (int i = 0; i < 3; ++i) {
        const QRectF r = rowRect(i);
        const bool active = (i == m_activeIdx);
        const bool hovered = (i == m_hoverIdx);
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor("#2E323D") : m_pal.subtle);
            p.drawRoundedRect(r, 9.0, 9.0);
        } else if (hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(r, 9.0, 9.0);
        }

        p.setFont(textFamilyPreviewFont(i, 16.0));
        p.setPen(active ? m_pal.text : (m_dark ? QColor("#D4D4D8") : m_pal.text));
        p.drawText(QRectF(r.left() + 12.0, r.top(), r.width() - 36.0, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, textFamilyLabel(i));

        if (active) {
            p.setFont(editorFont(13.0, QFont::DemiBold));
            p.setPen(m_pal.accent);
            p.drawText(QRectF(r.right() - 28.0, r.top(), 20.0, r.height()),
                       Qt::AlignCenter, QStringLiteral("✓"));
        }
    }
}

void EditorFontPopover::mouseMoveEvent(QMouseEvent* e) {
    int hit = -1;
    for (int i = 0; i < 3; ++i) {
        if (rowRect(i).contains(e->position())) { hit = i; break; }
    }
    if (hit != m_hoverIdx) {
        m_hoverIdx = hit;
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void EditorFontPopover::leaveEvent(QEvent*) {
    if (m_hoverIdx != -1) {
        m_hoverIdx = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

void EditorFontPopover::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < 3; ++i) {
        if (rowRect(i).contains(e->position())) {
            closePopover();
            emit fontPicked(i);
            return;
        }
    }
}

// ---------------- EditorSelectionMoreMenu (P1) ----------------

EditorSelectionMoreMenu::EditorSelectionMoreMenu(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::PopoverSolid);
    setCornerRadius(14.0);
    setFixedSize(192, 232);
    setMouseTracking(true);
    hide();

    m_openAnim = new QVariantAnimation(this);
    m_openAnim->setDuration(160);
    m_openAnim->setEasingCurve(editorEasingOut());
    connect(m_openAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_openT = v.toReal();
        setRevealProgress(m_openT);
        update();
    });
}

void EditorSelectionMoreMenu::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
}

void EditorSelectionMoreMenu::openAbove(const QRect& moreBtnInParent, const QSize& bounds) {
    const int margin = 10;
    const int x = std::clamp(moreBtnInParent.right() - width() + 1,
                             margin, std::max(margin, bounds.width() - width() - margin));
    const int y = std::clamp(moreBtnInParent.top() - height() - 10,
                             margin, std::max(margin, bounds.height() - height() - margin));
    move(x, y);
    m_hover = HitId::None;
    show();
    raise();

    if (!tokens::reducedMotion()) {
        m_openAnim->stop();
        m_openT = 0.0;
        setRevealProgress(0.0);
        m_openAnim->setStartValue(0.0);
        m_openAnim->setEndValue(1.0);
        m_openAnim->start();
    } else {
        m_openT = 1.0;
        setRevealProgress(1.0);
    }
    emit visibilityChanged();
    update();
}

void EditorSelectionMoreMenu::closeMenu() {
    if (!isVisible())
        return;
    m_openAnim->stop();
    m_hover = HitId::None;
    hide();
    emit closed();
    emit visibilityChanged();
}

QVector<EditorSelectionMoreMenu::Zone> EditorSelectionMoreMenu::zones() const {
    const qreal qaW = (width() - 16.0 - 4.0) * 0.5;
    return {
        {HitId::Cut,   QRectF(8.0, 8.0, qaW, 50.0), QStringLiteral("Cortar (Ctrl+X)")},
        {HitId::Copy,  QRectF(8.0 + qaW + 4.0, 8.0, qaW, 50.0), QStringLiteral("Copiar (Ctrl+C)")},
        {HitId::FlipH, QRectF(width() - 80.0, 74.0, 34.0, 30.0), QStringLiteral("Horizontal")},
        {HitId::FlipV, QRectF(width() - 42.0, 74.0, 34.0, 30.0), QStringLiteral("Vertical")},
        {HitId::Front, QRectF(width() - 80.0, 112.0, 34.0, 30.0), QStringLiteral("Traer al frente")},
        {HitId::Back,  QRectF(width() - 42.0, 112.0, 34.0, 30.0), QStringLiteral("Enviar al fondo")},
        {HitId::Group, QRectF(6.0, 156.0, width() - 12.0, 34.0), QString()},
        {HitId::Lock,  QRectF(6.0, 190.0, width() - 12.0, 34.0), QString()},
    };
}

EditorSelectionMoreMenu::HitId EditorSelectionMoreMenu::hitAt(const QPointF& pos, QString* outTip) const {
    for (const auto& z : zones()) {
        if (z.rect.contains(pos)) {
            if (outTip) *outTip = z.tip;
            return z.id;
        }
    }
    if (outTip) outTip->clear();
    return HitId::None;
}

void EditorSelectionMoreMenu::paintEvent(QPaintEvent* event) {
    EditorGlassCard::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_openT < 0.999) {
        p.setOpacity(m_openT);
        const qreal sc = 0.97 + 0.03 * m_openT;
        p.translate(width(), height());
        p.scale(sc, sc);
        p.translate(-width(), -height() + (1.0 - m_openT) * 6.0);
    }

    const QVector<Zone> zs = zones();
    const QColor tileBg = m_dark ? QColor(255, 255, 255, 10) : m_pal.subtle;
    const QColor tileHover = m_dark ? QColor(255, 255, 255, 22) : m_pal.hover;
    const QColor textCol = m_dark ? QColor("#D4D4D8") : m_pal.text;
    const QColor icCol = m_dark ? QColor("#C4C6CF") : m_pal.icon;

    // 1. Accesos rápidos: Cortar y Copiar
    auto drawQuick = [&](const Zone& z, const QString& ic, const QString& pack, const QString& lbl) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_hover == z.id ? tileHover : tileBg);
        p.drawRoundedRect(z.rect, 10.0, 10.0);
        paintUiIcon(&p, QRectF(z.rect.center().x() - 9.5, z.rect.top() + 8.0, 19.0, 19.0), ic, icCol, pack);
        p.setFont(editorFont(11.0, QFont::Medium));
        p.setPen(textCol);
        p.drawText(QRectF(z.rect.left(), z.rect.bottom() - 19.0, z.rect.width(), 16.0), Qt::AlignCenter, lbl);
    };
    drawQuick(zs[0], QStringLiteral("cut"), QStringLiteral("editor"), QStringLiteral("Cortar"));
    drawQuick(zs[1], QStringLiteral("copy"), QStringLiteral("ui"), QStringLiteral("Copiar"));

    // Separador 1
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 18) : m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(12.0, 64.5), QPointF(width() - 12.0, 64.5));

    // 2. Filas Voltear y Orden
    p.setFont(editorFont(13.0, QFont::Medium));
    p.setPen(textCol);
    p.drawText(QRectF(16.0, 70.0, 90.0, 38.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Voltear"));
    p.drawText(QRectF(16.0, 108.0, 90.0, 38.0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Orden"));

    auto drawPairBtn = [&](const Zone& z, const QString& ic) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_hover == z.id ? tileHover : tileBg);
        p.drawRoundedRect(z.rect, 8.0, 8.0);
        paintUiIcon(&p, QRectF(z.rect.center().x() - 8.5, z.rect.center().y() - 8.5, 17.0, 17.0),
                    ic, icCol, QStringLiteral("editor"));
    };
    drawPairBtn(zs[2], QStringLiteral("flip-h"));
    drawPairBtn(zs[3], QStringLiteral("flip-v"));
    drawPairBtn(zs[4], QStringLiteral("front"));
    drawPairBtn(zs[5], QStringLiteral("back"));

    // Separador 2
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 18) : m_pal.borderSoft, 1.0));
    p.drawLine(QPointF(12.0, 150.5), QPointF(width() - 12.0, 150.5));

    // 3. Agrupar / Desagrupar + Bloquear / Desbloquear
    const bool ungroup = m_canvas && m_canvas->canUngroupSelection();
    const bool locked = m_canvas && m_canvas->isSelectionLocked();

    auto drawRow = [&](const Zone& z, const QString& ic, const QString& lbl, const QString& sc) {
        if (m_hover == z.id) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 15) : m_pal.hover);
            p.drawRoundedRect(z.rect, 9.0, 9.0);
        }
        paintUiIcon(&p, QRectF(z.rect.left() + 10.0, z.rect.center().y() - 8.5, 17.0, 17.0),
                    ic, m_dark ? QColor("#9A9CA6") : m_pal.muted, QStringLiteral("editor"));
        p.setFont(editorFont(13.0, QFont::Medium));
        p.setPen(textCol);
        p.drawText(QRectF(z.rect.left() + 37.0, z.rect.top(), z.rect.width() - 48.0, z.rect.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, lbl);
        if (!sc.isEmpty()) {
            p.setFont(editorFont(11.5, QFont::Normal));
            p.setPen(m_dark ? QColor("#6B6E7A") : m_pal.muted);
            p.drawText(QRectF(z.rect.left(), z.rect.top(), z.rect.width() - 10.0, z.rect.height()),
                       Qt::AlignRight | Qt::AlignVCenter, sc);
        }
    };
    drawRow(zs[6], QStringLiteral("group"),
            ungroup ? QStringLiteral("Desagrupar") : QStringLiteral("Agrupar"),
            QStringLiteral("Ctrl+G"));
    drawRow(zs[7], locked ? QStringLiteral("unlock") : QStringLiteral("lock"),
            locked ? QStringLiteral("Desbloquear") : QStringLiteral("Bloquear"),
            QString());
}

void EditorSelectionMoreMenu::mouseMoveEvent(QMouseEvent* e) {
    QString tip;
    const HitId h = hitAt(e->position(), &tip);
    if (h != m_hover) {
        m_hover = h;
        setCursor(h != HitId::None ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(tip);
        update();
    }
}

void EditorSelectionMoreMenu::leaveEvent(QEvent*) {
    if (m_hover != HitId::None) {
        m_hover = HitId::None;
        setCursor(Qt::ArrowCursor);
        setToolTip(QString());
        update();
    }
}

void EditorSelectionMoreMenu::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_canvas)
        return;
    const HitId h = hitAt(e->position());
    switch (h) {
    case HitId::Cut:   m_canvas->cutSelection(); closeMenu(); break;
    case HitId::Copy:  m_canvas->copySelection(); closeMenu(); break;
    case HitId::FlipH: m_canvas->flipSelectionHorizontal(); break; // no cierra
    case HitId::FlipV: m_canvas->flipSelectionVertical(); break;   // no cierra
    case HitId::Front: m_canvas->bringSelectionToFront(); break;   // no cierra
    case HitId::Back:  m_canvas->sendSelectionToBack(); break;     // no cierra
    case HitId::Group:
        if (m_canvas->canUngroupSelection()) m_canvas->ungroupSelection();
        else m_canvas->groupSelection();
        closeMenu();
        break;
    case HitId::Lock:
        m_canvas->toggleLockSelection();
        closeMenu();
        break;
    default: break;
    }
}

// ---------------- EditorContextCapsule (C3 dos pisos) ----------------

EditorContextCapsule::EditorContextCapsule(CanvasWidget* canvas, QWidget* parent)
    : QWidget(parent),
      m_canvas(canvas) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(6);

    m_topCard = new EditorGlassCard(this);
    m_topCard->setRole(EditorGlassCard::Role::ContextCapsule);
    m_topCard->setCornerRadius(16.0);
    m_topLayout = new QHBoxLayout(m_topCard);
    m_topLayout->setContentsMargins(5, 5, 5, 5);
    m_topLayout->setSpacing(2);
    m_topCard->hide();

    m_bottomCard = new EditorGlassCard(this);
    m_bottomCard->setRole(EditorGlassCard::Role::ContextCapsule);
    m_bottomCard->setCornerRadius(16.0);
    m_bottomLayout = new QHBoxLayout(m_bottomCard);
    m_bottomLayout->setContentsMargins(5, 5, 5, 5);
    m_bottomLayout->setSpacing(2);

    vbox->addWidget(m_topCard, 0, Qt::AlignHCenter);
    vbox->addWidget(m_bottomCard, 0, Qt::AlignHCenter);

    m_moreMenu = new EditorSelectionMoreMenu(canvas, parent);
    connect(m_moreMenu, &EditorSelectionMoreMenu::closed, this, [this] {
        if (m_moreBtn)
            m_moreBtn->setActive(false);
    });
    connect(m_moreMenu, &EditorSelectionMoreMenu::visibilityChanged,
            this, &EditorContextCapsule::moreMenuVisibilityChanged);

    m_fontPopover = new EditorFontPopover(parent);
    connect(m_fontPopover, &EditorFontPopover::visibilityChanged,
            this, &EditorContextCapsule::fontPopoverVisibilityChanged);
    connect(m_fontPopover, &EditorFontPopover::fontPicked, this, [this](int famIdx) {
        if (!m_canvas) return;
        const InlineTextState cur = m_canvas->inlineTextState();
        m_canvas->setInlineTextStyle(famIdx, cur.fontSize, cur.bold, cur.italic);
        m_lastSignature.clear();
        syncFromCanvas();
    });

    m_revealAnim = new QVariantAnimation(this);
    m_revealAnim->setDuration(240); // 200 ms inferior + 40 ms delay superior
    m_revealAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_revealAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal elapsed = v.toReal() * 240.0;
        const QEasingCurve out = editorEasingOut();
        const qreal botT = out.valueForProgress(std::clamp(elapsed / 200.0, 0.0, 1.0));
        const qreal topT = out.valueForProgress(std::clamp((elapsed - 40.0) / 200.0, 0.0, 1.0));
        m_bottomCard->setRevealProgress(botT);
        m_topCard->setRevealProgress(topT);
    });

    hide();
}

void EditorContextCapsule::setDropdownOpener(DropdownRequestFn fn) {
    m_dropdownOpener = std::move(fn);
}

void EditorContextCapsule::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    m_topCard->applyEditorTheme(dark);
    m_bottomCard->applyEditorTheme(dark);
    if (m_moreMenu)
        m_moreMenu->applyEditorTheme(dark);
    if (m_fontPopover)
        m_fontPopover->applyEditorTheme(dark);
    m_lastSignature.clear();
    syncFromCanvas();
}

void EditorContextCapsule::closeMoreMenu() {
    if (m_moreMenu)
        m_moreMenu->closeMenu();
}

void EditorContextCapsule::closeFontPopover() {
    if (m_fontPopover)
        m_fontPopover->closePopover();
}

void EditorContextCapsule::clearLayout(QHBoxLayout* lay) {
    while (QLayoutItem* item = lay->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void EditorContextCapsule::playRevealAnimation(bool hasTop) {
    if (tokens::reducedMotion()) {
        m_bottomCard->setRevealProgress(1.0);
        m_topCard->setRevealProgress(1.0);
        return;
    }
    m_revealAnim->stop();
    m_revealAnim->setDuration(hasTop ? 240 : 200);
    m_bottomCard->setRevealProgress(0.0);
    m_topCard->setRevealProgress(0.0);
    m_revealAnim->setStartValue(0.0);
    m_revealAnim->setEndValue(1.0);
    m_revealAnim->start();
}

void EditorContextCapsule::addMoreButton(QHBoxLayout* targetLayout, QWidget* parentCard) {
    auto* moreBtn = new EditorIconButton(QStringLiteral("more"), QStringLiteral("ui"), parentCard);
    moreBtn->setFixedSize(34, 34);
    moreBtn->setIconSizePx(20.0);
    moreBtn->setToolTip(QStringLiteral("Más"));
    moreBtn->setActive(m_moreMenu && m_moreMenu->isVisible());
    moreBtn->applyEditorTheme(m_dark);
    m_moreBtn = moreBtn;
    connect(moreBtn, &EditorIconButton::clicked, this, [this, moreBtn] {
        if (!m_moreMenu || !parentWidget())
            return;
        if (m_moreMenu->isVisible()) {
            m_moreMenu->closeMenu();
        } else {
            emit transientPopoverAboutToOpen();
            const QPoint tl = moreBtn->mapTo(parentWidget(), QPoint(0, 0));
            QRect anchorRect(tl, moreBtn->size());
            if (m_topCard && m_topCard->isVisible()) {
                const int capsuleTopY = m_topCard->mapTo(parentWidget(), QPoint(0, 0)).y();
                anchorRect.setTop(std::min(anchorRect.top(), capsuleTopY));
            }
            m_moreMenu->openAbove(anchorRect, parentWidget()->size());
            moreBtn->setActive(true);
        }
    });
    targetLayout->addWidget(moreBtn);
}

void EditorContextCapsule::syncFromCanvas() {
    if (m_draggingSlider)
        return;

    const bool isEditingText = m_canvas && m_canvas->isInlineTextEditing();
    if (!m_canvas || (!m_canvas->isDrawingPolyline() && !m_canvas->hasSelection() && !isEditingText)) {
        m_lastSignature.clear();
        m_pizzaIconWidget = nullptr;
        closeMoreMenu();
        closeFontPopover();
        if (isVisible()) {
            hide();
            emit layoutChanged();
        }
        return;
    }

    int famVal = 0;
    const ShapeFamily fam = m_canvas->hasSelectedShape() ? m_canvas->selectedShapeFamily(&famVal) : ShapeFamily::None;
    const SelectionStyle st = m_canvas->selectionStyle();
    const bool isPolyline = m_canvas->isDrawingPolyline();
    const bool isAxes = m_canvas->hasSelectedAxes();
    const bool isImage = m_canvas->hasExtractableImage();
    const bool isShape = m_canvas->hasSelectedShape();
    const bool isAngle = isShape && st.label.has_value();
    const bool isStrokes = m_canvas->hasLassoSelection() && m_canvas->hasSelectedStrokes();
    const bool isText = isEditingText || (m_canvas->hasSelectedText() && !isShape && !isStrokes && !isImage);
    const bool editableField = isAxes || isAngle;
    const InlineTextState tst = isText ? m_canvas->inlineTextState() : InlineTextState{};
    const bool hasAngles = (isShape && !editableField) ? m_canvas->selectedShapeHasAngles() : false;
    const int sweepRounded = (isShape && fam == ShapeFamily::Circle && famVal != 3)
        ? (qRound(m_canvas->selectedCircleSweepDegrees() / 15.0) * 15) : 360;
    const int elemCount = m_canvas->hasLassoSelection() ? m_canvas->selectedElementCount() : 0;

    QString sig = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
                      .arg(m_dark)
                      .arg(isPolyline)
                      .arg(isAxes)
                      .arg(isImage)
                      .arg(isShape)
                      .arg(int(fam))
                      .arg(isAngle)
                      .arg(isStrokes)
                      .arg(isText)
                      .arg(isEditingText)
                      .arg(elemCount)
                      .arg(st.color ? st.color->rgba() : 0u);
    if (isText) {
        sig += QStringLiteral("|T:%1:%2:%3:%4")
                   .arg(tst.fontFamilyIndex).arg(qRound(tst.fontSize)).arg(tst.bold).arg(tst.italic);
    }
    if (!editableField && isShape) {
        const int sigFamVal = (fam == ShapeFamily::Circle) ? (famVal == 3 ? 3 : 0) : famVal;
        sig += QStringLiteral("|%1|%2|%3|%4|%5")
                   .arg(sigFamVal)
                   .arg(int(st.pattern.value_or(LinePattern::Solid)))
                   .arg(int(st.ending.value_or(LineEnding::None)))
                   .arg(hasAngles ? 1 : 0)
                   .arg(sweepRounded);
    }

    const bool wasVisible = isVisible();
    if (sig == m_lastSignature && wasVisible)
        return;
    m_lastSignature = sig;

    m_pizzaIconWidget = nullptr;
    clearLayout(m_topLayout);
    clearLayout(m_bottomLayout);

    auto makeSep = [this](QHBoxLayout* lay, QWidget* host) {
        auto* wrap = new QWidget(host);
        wrap->setFixedSize(11, 22); // 5 px izq + 1 px divisor + 5 px der
        auto* hl = new QHBoxLayout(wrap);
        hl->setContentsMargins(5, 0, 5, 0);
        auto* div = new QWidget(wrap);
        div->setFixedSize(1, 22);
        div->setStyleSheet(QStringLiteral("background: %1;")
                               .arg(m_dark ? QStringLiteral("rgba(255,255,255,0.08)")
                                           : m_pal.border.name(QColor::HexArgb)));
        hl->addWidget(div);
        lay->addWidget(wrap);
    };

    auto addDupAndDel = [this](QHBoxLayout* lay, QWidget* host) {
        auto* dupBtn = new EditorIconButton(QStringLiteral("copy"), QStringLiteral("ui"), host);
        dupBtn->setFixedSize(34, 34);
        dupBtn->setIconSizePx(20.0);
        dupBtn->applyEditorTheme(m_dark);
        dupBtn->setToolTip(QStringLiteral("Duplicar"));
        connect(dupBtn, &EditorIconButton::clicked, this, [this] {
            if (m_canvas) m_canvas->duplicateSelection();
        });
        lay->addWidget(dupBtn);

        auto* delBtn = new EditorIconButton(QStringLiteral("trash"), QStringLiteral("ui"), host);
        delBtn->setFixedSize(34, 34);
        delBtn->setIconSizePx(20.0);
        delBtn->setDanger(true);
        delBtn->applyEditorTheme(m_dark);
        delBtn->setToolTip(QStringLiteral("Eliminar"));
        connect(delBtn, &EditorIconButton::clicked, this, [this] {
            if (m_canvas) m_canvas->deleteSelection();
        });
        lay->addWidget(delBtn);
    };

    auto addColorAndWedge = [this, &st](QHBoxLayout* lay, QWidget* host, bool withWidth) {
        const QColor curCol = st.color.value_or(m_canvas ? m_canvas->inkColor() : m_pal.text);
        auto* colBtn = new CapsuleColorSwatchBtn(curCol, m_dark, host);
        colBtn->onClicked = [this, colBtn] {
            const QPoint p = colBtn->mapTo(parentWidget(), QPoint(0, 0));
            emit selectionColorRequested(QRect(p, colBtn->size()));
        };
        lay->addWidget(colBtn);

        if (withWidth && st.width.has_value()) {
            auto* wedge = new EditorWedgeSlider(host);
            wedge->setCompact(true); // 78x26 px
            wedge->setRange(0.5, 16.0, 0.5);
            wedge->setThumbColor(curCol);
            wedge->setValue(*st.width, false);
            wedge->applyEditorTheme(m_dark);
            connect(wedge, &EditorWedgeSlider::draggingChanged, this, [this](bool d) {
                m_draggingSlider = d;
            });
            connect(wedge, &EditorWedgeSlider::valueChanged, this, [this](double w) {
                if (!m_canvas) return;
                SelectionStyle s;
                s.width = w;
                m_canvas->setSelectionStyle(s);
            });
            lay->addWidget(wedge);
        }
    };

    bool hasTopFloor = false;

    if (isPolyline) {
        auto* finishBtn = new EditorChipButton(QStringLiteral("Terminar"), m_bottomCard);
        finishBtn->setChecked(true);
        finishBtn->applyEditorTheme(m_dark);
        connect(finishBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->finishActivePolyline(false);
        });
        auto* closeBtn = new EditorChipButton(QStringLiteral("Cerrar"), m_bottomCard);
        closeBtn->applyEditorTheme(m_dark);
        connect(closeBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->finishActivePolyline(true);
        });
        m_bottomLayout->addWidget(finishBtn);
        m_bottomLayout->addWidget(closeBtn);
    } else if (isAxes) {
        auto* lbl = new QLabel(QStringLiteral("f(x) ="), m_bottomCard);
        lbl->setFont(editorFont(12.0, QFont::DemiBold));
        lbl->setStyleSheet(QStringLiteral("color: %1; padding: 0 6px;").arg(m_pal.muted.name()));
        m_bottomLayout->addWidget(lbl);

        auto* edit = new EditorLineEdit(m_bottomCard);
        edit->applyEditorTheme(m_dark);
        edit->setFixedSize(136, 30);
        edit->setText(m_canvas->selectedAxesFunction());
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            if (m_canvas) m_canvas->setSelectedAxesFunction(edit->text());
        });
        connect(edit, &QLineEdit::returnPressed, this, [this, edit] {
            if (m_canvas) m_canvas->setSelectedAxesFunction(edit->text());
        });
        m_bottomLayout->addWidget(edit);
        makeSep(m_bottomLayout, m_bottomCard);
        addDupAndDel(m_bottomLayout, m_bottomCard);
        addMoreButton(m_bottomLayout, m_bottomCard);
    } else if (isImage) {
        auto* cropBtn = new EditorChipButton(QStringLiteral("Recortar"), m_bottomCard);
        cropBtn->setIcon(QStringLiteral("crop"), QStringLiteral("editor"));
        cropBtn->applyEditorTheme(m_dark);
        connect(cropBtn, &EditorChipButton::clicked, this, [this] {
            if (m_canvas) m_canvas->cropSelectedImageInteractiveOrRatio(0.0);
        });
        m_bottomLayout->addWidget(cropBtn);

        auto* vecBtn = new EditorChipButton(QStringLiteral("Vectorizar figura"), m_bottomCard);
        vecBtn->setIcon(QStringLiteral("vector"), QStringLiteral("editor"));
        vecBtn->applyEditorTheme(m_dark);
        connect(vecBtn, &EditorChipButton::clicked, this, &EditorContextCapsule::vectorizeRequested);
        m_bottomLayout->addWidget(vecBtn);

        auto* aiBtn = new EditorChipButton(QStringLiteral("Extraer con IA"), m_bottomCard);
        aiBtn->setIcon(QStringLiteral("sparkle"), QStringLiteral("ui"));
        aiBtn->setChecked(true);
        aiBtn->applyEditorTheme(m_dark);
        connect(aiBtn, &EditorChipButton::clicked, this, &EditorContextCapsule::extractAiRequested);
        m_bottomLayout->addWidget(aiBtn);

        auto* opSlider = new EditorSlider(m_bottomCard);
        opSlider->setRange(15.0, 100.0, 5.0);
        opSlider->setValue(m_canvas->selectedImageOpacity() * 100.0);
        opSlider->setFixedWidth(78);
        opSlider->applyEditorTheme(m_dark);
        connect(opSlider, &EditorSlider::valueChanged, this, [this](double pct) {
            if (m_canvas) m_canvas->setSelectedImageOpacity(pct / 100.0);
        });
        m_bottomLayout->addWidget(opSlider);

        makeSep(m_bottomLayout, m_bottomCard);
        addDupAndDel(m_bottomLayout, m_bottomCard);
        addMoreButton(m_bottomLayout, m_bottomCard);
    } else if (isShape && isAngle) {
        addColorAndWedge(m_bottomLayout, m_bottomCard, true);
        makeSep(m_bottomLayout, m_bottomCard);
        auto* edit = new EditorLineEdit(m_bottomCard);
        edit->applyEditorTheme(m_dark);
        edit->setFixedSize(76, 30);
        edit->setPlaceholderText(QStringLiteral("Auto"));
        edit->setText(st.label.value_or(QString()));
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            if (!m_canvas) return;
            SelectionStyle next;
            next.label = edit->text().trimmed();
            m_canvas->setSelectionStyle(next);
        });
        m_bottomLayout->addWidget(edit);
        makeSep(m_bottomLayout, m_bottomCard);
        addDupAndDel(m_bottomLayout, m_bottomCard);
        addMoreButton(m_bottomLayout, m_bottomCard);
    } else if (isShape) {
        const bool closed = st.filled.has_value() || st.fillColor.has_value()
                         || (fam != ShapeFamily::None && !(fam == ShapeFamily::Circle && famVal == 1));
        const LinePattern curPat = st.pattern.value_or(LinePattern::Solid);
        const LineEnding curEnd = st.ending.value_or(LineEnding::None);
        const bool isDim = (curPat == LinePattern::Dimension);

        // PISO INFERIOR: Color · Cuña · | · Estilo · | · Duplicar · Eliminar · Más
        addColorAndWedge(m_bottomLayout, m_bottomCard, true);
        makeSep(m_bottomLayout, m_bottomCard);
        auto* styleSeg = new CapsuleStyleSeg(curPat, isDim, m_dark, m_bottomCard);
        styleSeg->onPicked = [this](LinePattern pat) {
            if (!m_canvas) return;
            SelectionStyle s;
            s.pattern = pat;
            m_canvas->setSelectionStyle(s);
            m_lastSignature.clear();
            syncFromCanvas();
        };
        m_bottomLayout->addWidget(styleSeg);
        makeSep(m_bottomLayout, m_bottomCard);
        addDupAndDel(m_bottomLayout, m_bottomCard);
        addMoreButton(m_bottomLayout, m_bottomCard);

        // PISO SUPERIOR: variantes con glifo + nombre completo
        auto addAnglesChip = [this, hasAngles] {
            auto* angChip = new CapsuleVariantChip(
                QStringLiteral("Ángulos"), hasAngles, m_dark,
                [](QPainter& p, const QRectF& r, const QColor& c) {
                    paintUiIcon(&p, r, QStringLiteral("shp-angle"), c, QStringLiteral("editor"));
                },
                m_topCard);
            angChip->onClicked = [this] {
                if (m_canvas) m_canvas->toggleSelectedShapeAngles();
                m_lastSignature.clear();
                syncFromCanvas();
            };
            m_topLayout->addWidget(angChip);

            if (hasAngles && m_canvas) {
                auto* angSlider = new EditorSlider(m_topCard);
                angSlider->setRange(14.0, 160.0, 2.0);
                angSlider->setValue(m_canvas->selectedShapeAnglesRadius());
                angSlider->setFixedWidth(74);
                angSlider->setToolTip(QStringLiteral("Tamaño de los ángulos"));
                angSlider->applyEditorTheme(m_dark);
                connect(angSlider, &EditorSlider::valueChanged, this, [this](double px) {
                    if (m_canvas)
                        m_canvas->setSelectedShapeAnglesRadius(px);
                });
                m_topLayout->addWidget(angSlider);
            }
        };

        if (closed && fam == ShapeFamily::Quad) {
            hasTopFloor = true;
            struct QOpt { const char* name; int val; };
            const QOpt opts[6] = {
                {"Rectángulo", 0}, {"Cuadrado", 1}, {"Rombo", 2},
                {"Paralelogramo", 3}, {"Trapecio", 4}, {"Trapezoide", 6},
            };
            for (const auto& o : opts) {
                const bool on = (famVal == o.val) || (o.val == 4 && famVal == 5);
                auto* chip = new CapsuleVariantChip(
                    QString::fromUtf8(o.name), on, m_dark,
                    [v = o.val](QPainter& p, const QRectF& r, const QColor& c) {
                        paintShapeVariantGlyph(p, r, ShapeFamily::Quad, v, c);
                    },
                    m_topCard);
                chip->onClicked = [this, v = o.val] {
                    if (m_canvas) m_canvas->applySelectedShapeVariant(ShapeFamily::Quad, v);
                };
                m_topLayout->addWidget(chip);
            }
            makeSep(m_topLayout, m_topCard);
            addAnglesChip();
        } else if (closed && fam == ShapeFamily::Triangle) {
            hasTopFloor = true;
            struct TOpt { const char* name; int val; };
            const TOpt opts[4] = {
                {"Libre", 0}, {"Isósceles", 1}, {"Rectángulo", 2}, {"Equilátero", 3},
            };
            for (const auto& o : opts) {
                auto* chip = new CapsuleVariantChip(
                    QString::fromUtf8(o.name), famVal == o.val, m_dark,
                    [v = o.val](QPainter& p, const QRectF& r, const QColor& c) {
                        paintShapeVariantGlyph(p, r, ShapeFamily::Triangle, v, c);
                    },
                    m_topCard);
                chip->onClicked = [this, v = o.val] {
                    if (m_canvas) m_canvas->applySelectedShapeVariant(ShapeFamily::Triangle, v);
                };
                m_topLayout->addWidget(chip);
            }
            makeSep(m_topLayout, m_topCard);
            addAnglesChip();
        } else if (closed && fam == ShapeFamily::Polygon) {
            hasTopFloor = true;
            for (int n = 5; n <= 8; ++n) {
                auto* chip = new CapsuleVariantChip(
                    QStringLiteral("%1 lados").arg(n), famVal == n, m_dark,
                    [n](QPainter& p, const QRectF& r, const QColor& c) {
                        paintShapeVariantGlyph(p, r, ShapeFamily::Polygon, n, c);
                    },
                    m_topCard);
                chip->onClicked = [this, n] {
                    if (m_canvas) m_canvas->applySelectedShapeVariant(ShapeFamily::Polygon, n);
                };
                m_topLayout->addWidget(chip);
            }
            makeSep(m_topLayout, m_topCard);
            addAnglesChip();
        } else if (fam == ShapeFamily::Circle) {
            hasTopFloor = true;
            const bool isEllipse = (famVal == 3);
            auto* cChip = new CapsuleVariantChip(
                QStringLiteral("Círculo"), !isEllipse, m_dark,
                [](QPainter& p, const QRectF& r, const QColor& c) {
                    paintShapeVariantGlyph(p, r, ShapeFamily::Circle, 0, c);
                },
                m_topCard);
            cChip->onClicked = [this] {
                if (m_canvas) m_canvas->applySelectedShapeVariant(ShapeFamily::Circle, 0);
            };
            m_topLayout->addWidget(cChip);

            auto* eChip = new CapsuleVariantChip(
                QStringLiteral("Elipse"), isEllipse, m_dark,
                [](QPainter& p, const QRectF& r, const QColor& c) {
                    paintShapeVariantGlyph(p, r, ShapeFamily::Circle, 3, c);
                },
                m_topCard);
            eChip->onClicked = [this] {
                if (m_canvas) m_canvas->applySelectedShapeVariant(ShapeFamily::Circle, 3);
            };
            m_topLayout->addWidget(eChip);

            if (!isEllipse) {
                makeSep(m_topLayout, m_topCard);
                const double curDeg = m_canvas ? m_canvas->selectedCircleSweepDegrees() : 360.0;
                auto* pizzaIc = new CapsulePizzaIcon(curDeg, m_dark, m_topCard);
                m_pizzaIconWidget = pizzaIc;
                m_topLayout->addWidget(pizzaIc);

                auto* pizzaSlider = new EditorSlider(m_topCard);
                pizzaSlider->setRange(15.0, 360.0, 15.0);
                pizzaSlider->setValue(curDeg);
                pizzaSlider->setFixedWidth(90);
                pizzaSlider->applyEditorTheme(m_dark);
                connect(pizzaSlider, &EditorSlider::valueChanged, this, [this, pizzaIc](double deg) {
                    pizzaIc->setDegrees(deg);
                    if (m_canvas)
                        m_canvas->setSelectedCircleSweepDegrees(deg);
                });
                m_topLayout->addWidget(pizzaSlider);
            }
        } else if (!closed && fam == ShapeFamily::None) {
            // Figura abierta: Sin punta · Flecha · Doble flecha · Cota
            hasTopFloor = true;
            struct EOpt { const char* name; int glyph; LineEnding end; bool dim; };
            const EOpt opts[4] = {
                {"Sin punta", 0, LineEnding::None, false},
                {"Flecha", 1, LineEnding::Arrow, false},
                {"Doble flecha", 2, LineEnding::DoubleArrow, false},
                {"Cota", 3, LineEnding::DoubleArrow, true},
            };
            for (const auto& o : opts) {
                const bool on = o.dim ? isDim : (!isDim && curEnd == o.end);
                auto* chip = new CapsuleVariantChip(
                    QString::fromUtf8(o.name), on, m_dark,
                    [g = o.glyph](QPainter& p, const QRectF& r, const QColor& c) {
                        paintLineEndingGlyph(p, r, g, c);
                    },
                    m_topCard);
                chip->onClicked = [this, o] {
                    if (!m_canvas) return;
                    SelectionStyle s;
                    if (o.dim) {
                        s.pattern = LinePattern::Dimension;
                        s.ending = LineEnding::DoubleArrow;
                    } else {
                        s.ending = o.end;
                        if (m_canvas->selectionStyle().pattern == LinePattern::Dimension)
                            s.pattern = LinePattern::Solid;
                    }
                    m_canvas->setSelectionStyle(s);
                    m_lastSignature.clear();
                    syncFromCanvas();
                };
                m_topLayout->addWidget(chip);
            }
        }
    } else if (isText) {
        // Cápsula de Texto T1 (DESIGN §7.7):
        // Escribiendo: color ● | Fuente ▾ | − 24 + | B I
        // Seleccionado: lo mismo + | Duplicar · Eliminar · ⋯
        addColorAndWedge(m_bottomLayout, m_bottomCard, false);
        makeSep(m_bottomLayout, m_bottomCard);

        auto* fontBtn = new CapsuleFontButton(tst.fontFamilyIndex,
                                              m_fontPopover && m_fontPopover->isVisible(),
                                              m_dark, m_bottomCard);
        fontBtn->onClicked = [this, fontBtn, famIdx = tst.fontFamilyIndex] {
            if (!m_fontPopover || !parentWidget())
                return;
            if (m_fontPopover->isVisible()) {
                m_fontPopover->closePopover();
            } else {
                emit transientPopoverAboutToOpen();
                const QPoint tl = fontBtn->mapTo(parentWidget(), QPoint(0, 0));
                m_fontPopover->openAbove(QRect(tl, fontBtn->size()), parentWidget()->size(), famIdx);
            }
        };
        m_bottomLayout->addWidget(fontBtn);
        makeSep(m_bottomLayout, m_bottomCard);

        auto* minusBtn = new CapsuleRepeatIconBtn(QStringLiteral("minus"), m_dark, m_bottomCard);
        minusBtn->setToolTip(QStringLiteral("Reducir tamaño"));
        minusBtn->onStep = [this] {
            if (!m_canvas) return;
            const InlineTextState cur = m_canvas->inlineTextState();
            m_canvas->setInlineTextStyle(cur.fontFamilyIndex, std::max(10.0, cur.fontSize - 2.0), cur.bold, cur.italic);
        };
        m_bottomLayout->addWidget(minusBtn);

        auto* szLbl = new QLabel(QString::number(qRound(tst.fontSize)), m_bottomCard);
        szLbl->setFont(editorTabularFont(13.0, QFont::DemiBold));
        szLbl->setAlignment(Qt::AlignCenter);
        szLbl->setFixedWidth(28);
        szLbl->setStyleSheet(QStringLiteral("color: %1;").arg(m_pal.text.name()));
        m_bottomLayout->addWidget(szLbl);

        auto* plusBtn = new CapsuleRepeatIconBtn(QStringLiteral("plus"), m_dark, m_bottomCard);
        plusBtn->setToolTip(QStringLiteral("Aumentar tamaño"));
        plusBtn->onStep = [this] {
            if (!m_canvas) return;
            const InlineTextState cur = m_canvas->inlineTextState();
            m_canvas->setInlineTextStyle(cur.fontFamilyIndex, std::min(96.0, cur.fontSize + 2.0), cur.bold, cur.italic);
        };
        m_bottomLayout->addWidget(plusBtn);
        makeSep(m_bottomLayout, m_bottomCard);

        auto* boldBtn = new CapsuleToggleIconBtn(QStringLiteral("bold"), tst.bold, m_dark,
                                                 QStringLiteral("Negrita"), m_bottomCard);
        boldBtn->onClicked = [this, tst] {
            if (m_canvas)
                m_canvas->setInlineTextStyle(tst.fontFamilyIndex, tst.fontSize, !tst.bold, tst.italic);
        };
        m_bottomLayout->addWidget(boldBtn);

        auto* italicBtn = new CapsuleToggleIconBtn(QStringLiteral("italic"), tst.italic, m_dark,
                                                   QStringLiteral("Cursiva"), m_bottomCard);
        italicBtn->onClicked = [this, tst] {
            if (m_canvas)
                m_canvas->setInlineTextStyle(tst.fontFamilyIndex, tst.fontSize, tst.bold, !tst.italic);
        };
        m_bottomLayout->addWidget(italicBtn);

        if (!isEditingText) {
            makeSep(m_bottomLayout, m_bottomCard);
            addDupAndDel(m_bottomLayout, m_bottomCard);
            addMoreButton(m_bottomLayout, m_bottomCard);
        }
    } else {
        // Selección de lazo / trazos / múltiple
        if (elemCount > 1) {
            auto* cntLbl = new QLabel(QStringLiteral("%1 elementos").arg(elemCount), m_bottomCard);
            cntLbl->setFont(editorFont(12.0, QFont::Medium));
            cntLbl->setStyleSheet(QStringLiteral("color: %1; padding: 0 8px;").arg(m_pal.muted.name()));
            m_bottomLayout->addWidget(cntLbl);
        }
        if (st.color.has_value() || st.width.has_value()) {
            addColorAndWedge(m_bottomLayout, m_bottomCard, true);
            makeSep(m_bottomLayout, m_bottomCard);
        }
        if (isStrokes) {
            auto* bBtn = new CapsuleVariantChip(
                QStringLiteral("Embellecer y ordenar"), false, m_dark,
                [](QPainter& p, const QRectF& r, const QColor& c) {
                    paintUiIcon(&p, r, QStringLiteral("wand"), c, QStringLiteral("tools"));
                },
                m_bottomCard);
            bBtn->onClicked = [this] {
                if (m_canvas) m_canvas->straightenSelection();
            };
            m_bottomLayout->addWidget(bBtn);
            makeSep(m_bottomLayout, m_bottomCard);
        }
        addDupAndDel(m_bottomLayout, m_bottomCard);
        addMoreButton(m_bottomLayout, m_bottomCard);
    }

    // Mostrar explícitamente todos los hijos recién creados antes de calcular sizeHint()
    // (en Qt, un hijo creado sobre un padre ya visible nace oculto y QLayout::sizeHint() lo ignora).
    auto showAllChildren = [](QHBoxLayout* lay) {
        for (int i = 0; i < lay->count(); ++i) {
            if (QWidget* w = lay->itemAt(i)->widget())
                w->show();
        }
    };
    showAllChildren(m_topLayout);
    showAllChildren(m_bottomLayout);

    m_topCard->setVisible(hasTopFloor);
    if (hasTopFloor) {
        m_topLayout->invalidate();
        m_topLayout->activate();
        m_topCard->setFixedSize(m_topLayout->sizeHint());
    }
    m_bottomLayout->invalidate();
    m_bottomLayout->activate();
    m_bottomCard->setFixedSize(m_bottomLayout->sizeHint());
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
        setFixedSize(layout()->sizeHint());
    } else {
        adjustSize();
    }
    show();
    raise();
    if (!wasVisible)
        playRevealAnimation(hasTopFloor);
    emit layoutChanged();
}

// ---------------- EditorEdgeMarkCapsule ----------------

EditorEdgeMarkCapsule::EditorEdgeMarkCapsule(CanvasWidget* canvas, QWidget* parent)
    : EditorGlassCard(parent),
      m_canvas(canvas) {
    setRole(Role::ContextCapsule);
    setCornerRadius(12.0);
    hide();

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    struct MarkDef { const char* icon; EdgeMarkKind kind; const char* tip; };
    const MarkDef defs[8] = {
        {"mark-tick1", EdgeMarkKind::Tick1, "Rayita"},
        {"mark-tick2", EdgeMarkKind::Tick2, "Dos rayitas"},
        {"mark-parallel", EdgeMarkKind::Parallel1, "Paralelas"},
        {"mark-circle", EdgeMarkKind::Bar, "Bolita / Tope"},
        {"mark-dot", EdgeMarkKind::Divider, "Punto divisor"},
        {"mark-saw", EdgeMarkKind::Zigzag, "Serrucho"},
        {"mark-spring", EdgeMarkKind::Spring, "Resorte"},
        {"mark-wall", EdgeMarkKind::Wall, "Pared rugosa"},
    };

    for (const auto& d : defs) {
        auto* btn = new EditorIconButton(QString::fromLatin1(d.icon), QStringLiteral("editor"), this);
        btn->setFixedSize(30, 28);
        btn->setIconSizePx(15.0);
        btn->setToolTip(QString::fromUtf8(d.tip));
        const QString spec = edgeMarkLabel(d.kind);
        connect(btn, &EditorIconButton::clicked, this, [this, spec] {
            if (m_canvas)
                m_canvas->stampCongruenceMarkAt(m_worldPos, spec);
            hide();
            emit visibilityChanged();
        });
        m_markBtns.append(btn);
        row->addWidget(btn);
    }

    auto* div = new QWidget(this);
    div->setFixedSize(1, 18);
    row->addWidget(div);

    m_highlightBtn = new EditorChipButton(QStringLiteral("Resaltar tramo"), this);
    m_highlightBtn->setDotColor(m_activeColor);
    connect(m_highlightBtn, &EditorChipButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->highlightSegmentAt(m_worldPos, m_activeColor, 0);
        hide();
        emit visibilityChanged();
    });
    row->addWidget(m_highlightBtn);
    adjustSize();
}

void EditorEdgeMarkCapsule::applyEditorTheme(bool dark) {
    EditorGlassCard::applyEditorTheme(dark);
    for (auto* b : m_markBtns)
        b->applyEditorTheme(dark);
    if (m_highlightBtn)
        m_highlightBtn->applyEditorTheme(dark);
}

void EditorEdgeMarkCapsule::showAtWorldPoint(const QPointF& worldPos, const QColor& activeColor) {
    m_worldPos = worldPos;
    if (activeColor.isValid())
        m_activeColor = activeColor;
    if (m_highlightBtn)
        m_highlightBtn->setDotColor(m_activeColor);
    adjustSize();
    show();
    raise();
}

} // namespace finily
