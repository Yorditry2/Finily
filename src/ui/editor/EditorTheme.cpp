#include "ui/editor/EditorTheme.h"

#include "ui/UiIcons.h"
#include "ui/home/HomeTheme.h"

#include <QApplication>
#include <QConicalGradient>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>

namespace finily {

EditorPalette editorPaletteFor(bool dark) {
    const tokens::Palette tk = tokens::paletteFor(dark);
    EditorPalette pal;
    if (dark) {
        pal.bg = QColor("#1A1B22");
        pal.deskNb = QColor("#121318");
        pal.sheet = QColor("#20222B");
        pal.grid = QColor(255, 255, 255, 14);
        pal.surfaceGlass = QColor(30, 31, 40, 230);
        pal.surfaceSolid = QColor("#1E1F28");
        pal.subtle = QColor("#2A2C38");
        pal.tabsBg = QColor("#131419");
        pal.border = QColor(255, 255, 255, 23);
        pal.borderSoft = QColor(255, 255, 255, 15);
        pal.checkOff = QColor(255, 255, 255, 72);
        pal.text = QColor("#F4F4F5");
        pal.muted = QColor("#9397A5");
        pal.icon = QColor("#D4D4D8");
        pal.hover = QColor(244, 244, 245, 20);
        pal.danger = QColor("#F87171");
        pal.glassHi = QColor(255, 255, 255, 22);
    } else {
        pal.bg = QColor("#FBFBFA");
        pal.deskNb = QColor("#F1F1EF");
        pal.sheet = QColor("#FFFFFF");
        pal.grid = QColor(15, 23, 42, 14);
        pal.surfaceGlass = QColor(255, 255, 255, 224);
        pal.surfaceSolid = QColor("#FFFFFF");
        pal.subtle = QColor("#F4F4F5");
        pal.tabsBg = QColor("#F1F1EF");
        pal.border = QColor(15, 23, 42, 20);
        pal.borderSoft = QColor(15, 23, 42, 15);
        pal.checkOff = QColor(0, 0, 0, 64);
        pal.text = QColor("#18181B");
        pal.muted = QColor("#71717A");
        pal.icon = QColor("#3F3F46");
        pal.hover = QColor(24, 24, 27, 15);
        pal.danger = QColor("#DC2626");
        pal.glassHi = QColor(255, 255, 255, 235);
    }
    pal.accent = tk.accent;
    pal.accentFg = tk.accentFg;
    pal.unsavedAmber = QColor("#F59E0B");
    pal.selectionBlue = QColor("#2563EB");
    pal.veil = QColor(15, 23, 42, 72);
    return pal;
}

QFont editorFont(qreal px, int weight) {
    QFont f = QApplication::font();
    f.setPixelSize(std::max(1, int(std::lround(px))));
    f.setWeight(QFont::Weight(weight));
    return f;
}

QColor resolveAdaptableInkColor(const QColor& stored, bool sheetDark) {
    const QString hex = stored.name(QColor::HexRgb).toLower();
    if (sheetDark && (hex == QLatin1String("#18181b") || hex == QLatin1String("#0f172a")))
        return QColor("#F4F4F5");
    if (!sheetDark && (hex == QLatin1String("#f4f4f5") || hex == QLatin1String("#f8fafc")))
        return QColor("#18181B");
    return stored;
}

void paintEditorSwatch(QPainter& p, const QRectF& circleRect, const QColor& color,
                       bool active, const EditorPalette& pal) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (active) {
        p.setPen(QPen(pal.text, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circleRect.adjusted(-2.5, -2.5, 2.5, 2.5));
        p.setPen(QPen(pal.surfaceSolid, 2.0));
        p.drawEllipse(circleRect.adjusted(-0.8, -0.8, 0.8, 0.8));
    }
    p.setPen(QPen(QColor(15, 23, 42, 36), 1.0));
    p.setBrush(color);
    p.drawEllipse(circleRect);
    p.restore();
}

void paintEditorRainbowSwatch(QPainter& p, const QRectF& circleRect, bool active,
                              const EditorPalette& pal) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (active) {
        p.setPen(QPen(pal.text, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(circleRect.adjusted(-2.5, -2.5, 2.5, 2.5));
    }
    QConicalGradient grad(circleRect.center(), 90.0);
    grad.setColorAt(0.00, QColor("#E5484D"));
    grad.setColorAt(0.17, QColor("#F59E0B"));
    grad.setColorAt(0.33, QColor("#22A06B"));
    grad.setColorAt(0.50, QColor("#3B82F6"));
    grad.setColorAt(0.67, QColor("#8B5CF6"));
    grad.setColorAt(0.84, QColor("#EC4899"));
    grad.setColorAt(1.00, QColor("#E5484D"));
    p.setPen(QPen(QColor(15, 23, 42, 36), 1.0));
    p.setBrush(grad);
    p.drawEllipse(circleRect);
    p.restore();
}

void paintEditorWidthSlot(QPainter& p, const QRectF& rect, qreal pillThickness,
                          bool active, bool hovered, const QColor& inkColor,
                          const EditorPalette& pal) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (active) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.accent);
        p.drawRoundedRect(rect, 8.0, 8.0);
    } else if (hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.hover);
        p.drawRoundedRect(rect, 8.0, 8.0);
    }
    const qreal w = 15.0;
    const qreal h = std::clamp(pillThickness, 2.0, 8.0);
    const QRectF pill(rect.center().x() - w * 0.5, rect.center().y() - h * 0.5, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(active ? pal.accentFg : inkColor);
    p.drawRoundedRect(pill, h * 0.5, h * 0.5);
    p.restore();
}

void paintEditorSubChevron(QPainter& p, const QRectF& btnRect, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c = color;
    c.setAlphaF(0.52);
    p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal cx = btnRect.right() - 5.5;
    const qreal cy = btnRect.bottom() - 5.0;
    QPainterPath path;
    path.moveTo(cx - 2.2, cy - 1.2);
    path.lineTo(cx, cy + 1.2);
    path.lineTo(cx + 2.2, cy - 1.2);
    p.drawPath(path);
    p.restore();
}

void paintEditorFamilyCorner(QPainter& p, const QRectF& btnRect, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c = color;
    c.setAlphaF(0.55);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPainterPath tri;
    const qreal r = btnRect.right() - 4.0;
    const qreal b = btnRect.bottom() - 4.0;
    tri.moveTo(r, b);
    tri.lineTo(r - 4.5, b);
    tri.lineTo(r, b - 4.5);
    tri.closeSubpath();
    p.drawPath(tri);
    p.restore();
}

void paintEditorGripDots(QPainter& p, const QRectF& rect, const QColor& color, qreal opacity) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c = color;
    c.setAlphaF(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();
    for (int row = 0; row < 4; ++row) {
        const qreal y = cy - 7.5 + row * 5.0;
        p.drawEllipse(QPointF(cx - 2.0, y), 1.1, 1.1);
        p.drawEllipse(QPointF(cx + 2.0, y), 1.1, 1.1);
    }
    p.restore();
}

// ---------------- EditorGlassCard ----------------

EditorGlassCard::EditorGlassCard(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);
}

void EditorGlassCard::setRole(Role role) {
    m_role = role;
    update();
}

void EditorGlassCard::setCornerRadius(qreal radius) {
    m_radius = radius;
    update();
}

void EditorGlassCard::setDark(bool dark) {
    applyEditorTheme(dark);
}

void EditorGlassCard::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

void EditorGlassCard::clearBackdrop() {
    m_blurredBackdrop = QImage();
    update();
}

void EditorGlassCard::refreshBackdrop(QWidget* canvasSource) {
    if (m_grabbingBackdrop || !canvasSource || tokens::reducedMotion() || !isVisible()) {
        m_blurredBackdrop = QImage();
        return;
    }
    const QPoint topLeft = mapTo(canvasSource, QPoint(0, 0));
    const QRect srcRect(topLeft, size());
    const QRect bounded = srcRect.intersected(canvasSource->rect());
    if (bounded.isEmpty()) {
        m_blurredBackdrop = QImage();
        return;
    }
    m_grabbingBackdrop = true;
    const QPixmap snap = canvasSource->grab(bounded);
    m_grabbingBackdrop = false;
    m_blurredBackdrop = blurBackdropImage(snap.toImage(), 12);
    update();
}

void EditorGlassCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (frame.isEmpty())
        return;

    const bool useGlass = (m_role == Role::BarGlass || m_role == Role::SidePanel || m_role == Role::ContextCapsule);
    const qreal lift = (m_role == Role::ContextCapsule) ? 3.0 : 5.0;
    paintSoftShadow(p, frame, m_radius, lift, m_dark);

    QPainterPath clip;
    clip.addRoundedRect(frame, m_radius, m_radius);

    p.save();
    p.setClipPath(clip);
    if (useGlass && !m_blurredBackdrop.isNull() && !tokens::reducedMotion()) {
        p.drawImage(frame, m_blurredBackdrop);
        p.fillRect(frame, m_pal.surfaceGlass);
    } else if (useGlass && !tokens::reducedMotion()) {
        p.fillRect(frame, m_pal.surfaceGlass);
    } else {
        p.fillRect(frame, m_pal.surfaceSolid);
    }

    if (m_radius >= 10.0) {
        const qreal inset = std::min(m_radius * 0.45, frame.width() * 0.25);
        p.setPen(QPen(m_pal.glassHi, 1.0));
        p.drawLine(QPointF(frame.left() + inset, frame.top() + 1.0),
                   QPointF(frame.right() - inset, frame.top() + 1.0));
    }
    p.restore();

    p.setPen(QPen(m_pal.border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(frame, m_radius, m_radius);
}

void EditorGlassCard::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

// ---------------- EditorLineEdit ----------------

EditorLineEdit::EditorLineEdit(QWidget* parent)
    : QLineEdit(parent) {
    setFont(editorFont(11.5, QFont::Medium));
    applyEditorTheme(false);
}

void EditorLineEdit::applyEditorTheme(bool dark) {
    const EditorPalette pal = editorPaletteFor(dark);
    setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 7px;"
        "  padding: 4px 8px;"
        "  selection-background-color: %4;"
        "  selection-color: %5;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid %6;"
        "}")
                      .arg(pal.subtle.name(QColor::HexArgb),
                           pal.text.name(QColor::HexRgb),
                           pal.border.name(QColor::HexArgb),
                           pal.accent.name(QColor::HexRgb),
                           pal.accentFg.name(QColor::HexRgb),
                           pal.text.name(QColor::HexRgb)));
}

bool EditorLineEdit::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->modifiers() == Qt::NoModifier || ke->modifiers() == Qt::ShiftModifier) {
            const int k = ke->key();
            if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9) ||
                k == Qt::Key_Space || k == Qt::Key_Backspace || k == Qt::Key_Delete ||
                k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape) {
                ke->accept();
                return true;
            }
        }
    }
    return QLineEdit::event(e);
}

// ---------------- EditorIconButton ----------------

EditorIconButton::EditorIconButton(const QString& iconName, const QString& iconPack, QWidget* parent)
    : QWidget(parent),
      m_iconName(iconName),
      m_iconPack(iconPack) {
    setFixedSize(34, 34);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    m_pal = editorPaletteFor(false);
}

void EditorIconButton::setIcon(const QString& name, const QString& pack) {
    m_iconName = name;
    m_iconPack = pack;
    update();
}

void EditorIconButton::setDynamicPenMode(bool enabled, const QColor& nibColor) {
    m_dynamicPen = enabled;
    m_dynamicHl = false;
    m_dynamicColor = nibColor;
    update();
}

void EditorIconButton::setDynamicHighlighterMode(bool enabled, const QColor& barColor) {
    m_dynamicHl = enabled;
    m_dynamicPen = false;
    m_dynamicColor = barColor;
    update();
}

void EditorIconButton::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    update();
}

void EditorIconButton::setShowSubChevron(bool show) {
    m_showSubChev = show;
    update();
}

void EditorIconButton::setShowFamilyCorner(bool show) {
    m_showFamilyCorner = show;
    update();
}

void EditorIconButton::setDanger(bool danger) {
    m_danger = danger;
    update();
}

void EditorIconButton::setEnabledState(bool enabled) {
    m_enabled = enabled;
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void EditorIconButton::setIconSizePx(qreal px) {
    m_iconPx = px;
    update();
}

void EditorIconButton::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

void EditorIconButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF r(rect());
    if (m_pressed && m_enabled) {
        p.translate(r.center());
        p.scale(0.95, 0.95);
        p.translate(-r.center());
    }

    const qreal rad = (width() <= 28) ? 8.0 : 9.0;
    if (m_active) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_pal.accent);
        p.drawRoundedRect(r, rad, rad);
    } else if (m_hovered && m_enabled) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_pal.hover);
        p.drawRoundedRect(r, rad, rad);
    }

    QColor fg = m_active ? m_pal.accentFg : (m_danger ? m_pal.danger : m_pal.icon);
    if (!m_enabled)
        fg.setAlphaF(0.32);

    const QRectF ir(r.center().x() - m_iconPx * 0.5, r.center().y() - m_iconPx * 0.5, m_iconPx, m_iconPx);
    if (m_dynamicPen) {
        paintDynamicPenIcon(&p, ir, fg, m_dynamicColor);
    } else if (m_dynamicHl) {
        paintDynamicHighlighterIcon(&p, ir, fg, m_dynamicColor);
    } else if (!m_iconName.isEmpty()) {
        paintUiIcon(&p, ir, m_iconName, fg, m_iconPack);
    }

    if (m_showSubChev)
        paintEditorSubChevron(p, r, fg);
    if (m_showFamilyCorner)
        paintEditorFamilyCorner(p, r, fg);
}

void EditorIconButton::enterEvent(QEnterEvent*) {
    m_hovered = true;
    update();
}

void EditorIconButton::leaveEvent(QEvent*) {
    m_hovered = false;
    m_pressed = false;
    update();
}

void EditorIconButton::mousePressEvent(QMouseEvent* e) {
    if (!m_enabled)
        return;
    if (e->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    } else if (e->button() == Qt::RightButton) {
        emit rightClicked(e->globalPosition().toPoint());
    }
}

void EditorIconButton::mouseReleaseEvent(QMouseEvent* e) {
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    if (m_enabled && wasPressed && e->button() == Qt::LeftButton && rect().contains(e->pos()))
        emit clicked();
}

void EditorIconButton::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorSegControl ----------------

EditorSegControl::EditorSegControl(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(30);
    m_pal = editorPaletteFor(false);
}

void EditorSegControl::setOptions(const QStringList& labels) {
    m_labels = labels;
    m_current = std::clamp(m_current, 0, std::max(0, int(m_labels.size()) - 1));
    updateGeometry();
    update();
}

void EditorSegControl::setCurrentIndex(int index) {
    if (m_labels.isEmpty())
        return;
    const int clamped = std::clamp(index, 0, int(m_labels.size()) - 1);
    if (m_current == clamped)
        return;
    m_current = clamped;
    update();
}

void EditorSegControl::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

QSize EditorSegControl::sizeHint() const {
    const QFontMetricsF fm(editorFont(11.0, QFont::DemiBold));
    qreal total = 8.0;
    for (const QString& lbl : m_labels)
        total += std::max<qreal>(44.0, std::ceil(fm.horizontalAdvance(lbl)) + 20.0);
    return {int(std::ceil(total)), 30};
}

QSize EditorSegControl::minimumSizeHint() const {
    return {std::max(80, int(m_labels.size()) * 38), 30};
}

int EditorSegControl::indexAt(const QPointF& pos) const {
    if (m_labels.isEmpty() || width() <= 4)
        return -1;
    const qreal cellW = (width() - 4.0) / qreal(m_labels.size());
    const int idx = int((pos.x() - 2.0) / cellW);
    return (idx >= 0 && idx < m_labels.size()) ? idx : -1;
}

void EditorSegControl::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(m_pal.subtle);
    p.drawRoundedRect(track, 9.0, 9.0);

    if (m_labels.isEmpty())
        return;

    const qreal cellW = (track.width() - 4.0) / qreal(m_labels.size());
    p.setFont(editorFont(11.0, QFont::DemiBold));

    for (int i = 0; i < m_labels.size(); ++i) {
        const QRectF cell(2.0 + i * cellW, 2.0, cellW, track.height() - 4.0);
        const bool on = (i == m_current);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, m_dark ? 70 : 24));
            p.drawRoundedRect(cell.translated(0.0, 1.0), 7.0, 7.0);
            p.setBrush(m_pal.surfaceSolid);
            p.drawRoundedRect(cell, 7.0, 7.0);
        } else if (i == m_hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_pal.hover);
            p.drawRoundedRect(cell, 7.0, 7.0);
        }
        p.setPen(on ? m_pal.text : m_pal.muted);
        p.drawText(cell, Qt::AlignCenter, m_labels.at(i));
    }
}

void EditorSegControl::mouseMoveEvent(QMouseEvent* e) {
    const int idx = indexAt(e->position());
    if (idx != m_hovered) {
        m_hovered = idx;
        update();
    }
}

void EditorSegControl::leaveEvent(QEvent*) {
    m_hovered = -1;
    update();
}

void EditorSegControl::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const int idx = indexAt(e->position());
    if (idx >= 0 && idx != m_current) {
        m_current = idx;
        update();
        emit currentIndexChanged(idx);
    }
}

void EditorSegControl::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorChipButton ----------------

EditorChipButton::EditorChipButton(const QString& text, QWidget* parent)
    : QWidget(parent),
      m_text(text) {
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(27);
    m_pal = editorPaletteFor(false);
}

void EditorChipButton::setText(const QString& text) {
    m_text = text;
    updateGeometry();
    update();
}

void EditorChipButton::setChecked(bool checked) {
    if (m_checked == checked)
        return;
    m_checked = checked;
    update();
}

void EditorChipButton::setIcon(const QString& name, const QString& pack) {
    m_iconName = name;
    m_iconPack = pack;
    updateGeometry();
    update();
}

void EditorChipButton::setDotColor(const QColor& color) {
    m_dotColor = color;
    updateGeometry();
    update();
}

void EditorChipButton::setDanger(bool danger) {
    m_danger = danger;
    update();
}

void EditorChipButton::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

QSize EditorChipButton::sizeHint() const {
    const QFontMetricsF fm(editorFont(11.0, QFont::DemiBold));
    qreal w = 20.0 + std::ceil(fm.horizontalAdvance(m_text));
    if (!m_iconName.isEmpty())
        w += 18.0;
    if (m_dotColor.isValid())
        w += 15.0;
    return {int(std::ceil(w)), 27};
}

QSize EditorChipButton::minimumSizeHint() const {
    return sizeHint();
}

void EditorChipButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r(rect());
    p.setPen(Qt::NoPen);
    if (m_checked)
        p.setBrush(m_pal.accent);
    else if (m_hovered)
        p.setBrush(m_pal.hover);
    else
        p.setBrush(m_pal.subtle);
    p.drawRoundedRect(r, r.height() * 0.5, r.height() * 0.5);

    QColor fg = m_checked ? m_pal.accentFg : (m_danger ? m_pal.danger : m_pal.muted);
    if (!m_checked && m_hovered && !m_danger)
        fg = m_pal.text;

    qreal x = r.left() + 10.0;
    if (m_dotColor.isValid()) {
        p.setPen(QPen(QColor(15, 23, 42, 40), 1.0));
        p.setBrush(m_dotColor);
        p.drawEllipse(QRectF(x, r.center().y() - 5.0, 10.0, 10.0));
        x += 15.0;
    }
    if (!m_iconName.isEmpty()) {
        paintUiIcon(&p, QRectF(x, r.center().y() - 6.5, 13.0, 13.0), m_iconName, fg, m_iconPack);
        x += 18.0;
    }
    p.setFont(editorFont(11.0, QFont::DemiBold));
    p.setPen(fg);
    p.drawText(QRectF(x, r.top(), r.right() - 10.0 - x, r.height()),
               Qt::AlignCenter, m_text);
}

void EditorChipButton::enterEvent(QEnterEvent*) {
    m_hovered = true;
    update();
}

void EditorChipButton::leaveEvent(QEvent*) {
    m_hovered = false;
    update();
}

void EditorChipButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
        return;
    if (m_checkable) {
        m_checked = !m_checked;
        update();
        emit toggled(m_checked);
    }
    emit clicked();
}

void EditorChipButton::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorSwitch ----------------

EditorSwitch::EditorSwitch(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(32, 18);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);
}

void EditorSwitch::setChecked(bool checked) {
    if (m_checked == checked)
        return;
    m_checked = checked;
    update();
}

void EditorSwitch::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

void EditorSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(rect());
    p.setPen(Qt::NoPen);
    p.setBrush(m_checked ? m_pal.accent : m_pal.border);
    p.drawRoundedRect(r, 9.0, 9.0);

    const qreal kx = m_checked ? 16.0 : 2.0;
    p.setBrush(m_checked ? m_pal.accentFg : QColor("#FFFFFF"));
    p.drawEllipse(QRectF(kx, 2.0, 14.0, 14.0));
}

void EditorSwitch::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
        return;
    m_checked = !m_checked;
    update();
    emit toggled(m_checked);
}

void EditorSwitch::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorCheckBox ----------------

EditorCheckBox::EditorCheckBox(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent),
      m_title(title),
      m_subtitle(subtitle) {
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);
}

void EditorCheckBox::setText(const QString& title, const QString& subtitle) {
    m_title = title;
    m_subtitle = subtitle;
    updateGeometry();
    update();
}

void EditorCheckBox::setChecked(bool checked) {
    if (m_checked == checked)
        return;
    m_checked = checked;
    update();
}

void EditorCheckBox::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

QSize EditorCheckBox::sizeHint() const {
    const int h = m_subtitle.isEmpty() ? 28 : 42;
    return {200, h};
}

QSize EditorCheckBox::minimumSizeHint() const {
    return {120, m_subtitle.isEmpty() ? 26 : 38};
}

void EditorCheckBox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r(rect());
    if (m_hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_pal.hover);
        p.drawRoundedRect(r, 8.0, 8.0);
    }

    const QRectF box(r.left() + 6.0, m_subtitle.isEmpty() ? (r.center().y() - 8.0) : (r.top() + 7.0), 16.0, 16.0);
    if (m_checked) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_pal.accent);
        p.drawRoundedRect(box, 5.0, 5.0);
        p.setPen(QPen(m_pal.accentFg, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chk;
        chk.moveTo(box.left() + 3.5, box.center().y() + 0.3);
        chk.lineTo(box.left() + 6.7, box.bottom() - 4.0);
        chk.lineTo(box.right() - 3.5, box.top() + 4.3);
        p.drawPath(chk);
    } else {
        p.setPen(QPen(m_pal.checkOff, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(0.75, 0.75, -0.75, -0.75), 4.5, 4.5);
    }

    const qreal tx = box.right() + 9.0;
    const qreal tw = std::max<qreal>(40.0, r.right() - tx - 6.0);
    p.setFont(editorFont(12.0, QFont::Medium));
    p.setPen(m_pal.text);
    if (m_subtitle.isEmpty()) {
        p.drawText(QRectF(tx, r.top(), tw, r.height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    } else {
        p.drawText(QRectF(tx, r.top() + 4.0, tw, 17.0), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        p.setFont(editorFont(10.5, QFont::Normal));
        p.setPen(m_pal.muted);
        p.drawText(QRectF(tx, r.top() + 21.0, tw, 15.0), Qt::AlignLeft | Qt::AlignVCenter, m_subtitle);
    }
}

void EditorCheckBox::enterEvent(QEnterEvent*) {
    m_hovered = true;
    update();
}

void EditorCheckBox::leaveEvent(QEvent*) {
    m_hovered = false;
    update();
}

void EditorCheckBox::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !rect().contains(e->pos()))
        return;
    m_checked = !m_checked;
    update();
    emit toggled(m_checked);
}

void EditorCheckBox::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

// ---------------- EditorSlider ----------------

EditorSlider::EditorSlider(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(20);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    m_pal = editorPaletteFor(false);
}

void EditorSlider::setRange(double minVal, double maxVal, double step) {
    m_min = minVal;
    m_max = std::max(minVal + 0.0001, maxVal);
    m_step = std::max(0.0001, step);
    setValue(m_value);
}

void EditorSlider::setValue(double value) {
    const double clamped = std::clamp(value, m_min, m_max);
    if (std::abs(clamped - m_value) < 1e-6)
        return;
    m_value = clamped;
    update();
}

void EditorSlider::applyEditorTheme(bool dark) {
    m_dark = dark;
    m_pal = editorPaletteFor(dark);
    update();
}

void EditorSlider::updateFromX(qreal x) {
    const qreal left = 7.0;
    const qreal right = std::max<qreal>(left + 1.0, width() - 7.0);
    const double t = std::clamp((x - left) / (right - left), 0.0, 1.0);
    double raw = m_min + t * (m_max - m_min);
    if (m_step > 0.0)
        raw = m_min + std::round((raw - m_min) / m_step) * m_step;
    raw = std::clamp(raw, m_min, m_max);
    if (std::abs(raw - m_value) >= 1e-6) {
        m_value = raw;
        update();
        emit valueChanged(m_value);
    }
}

void EditorSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal left = 7.0;
    const qreal right = std::max<qreal>(left + 1.0, width() - 7.0);
    const qreal cy = height() * 0.5;
    const qreal trackW = right - left;
    const qreal frac = std::clamp((m_value - m_min) / (m_max - m_min), 0.0, 1.0);
    const qreal knobX = left + frac * trackW;

    p.setPen(Qt::NoPen);
    p.setBrush(m_pal.border);
    p.drawRoundedRect(QRectF(left, cy - 2.0, trackW, 4.0), 2.0, 2.0);

    p.setBrush(m_pal.accent);
    p.drawRoundedRect(QRectF(left, cy - 2.0, std::max<qreal>(0.0, knobX - left), 4.0), 2.0, 2.0);

    p.setBrush(m_pal.surfaceSolid);
    p.setPen(QPen(m_pal.accent, 2.0));
    p.drawEllipse(QPointF(knobX, cy), 6.0, 6.0);
}

void EditorSlider::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        updateFromX(e->position().x());
    }
}

void EditorSlider::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging)
        updateFromX(e->position().x());
}

void EditorSlider::mouseReleaseEvent(QMouseEvent* e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        updateFromX(e->position().x());
        m_dragging = false;
    }
}

void EditorSlider::tabletEvent(QTabletEvent* e) {
    e->ignore();
}

} // namespace finily
