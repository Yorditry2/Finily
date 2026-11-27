#include "ui/home/SheetWidgets.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

#include <algorithm>
#include <cmath>

namespace finily {
namespace {

tokens::Palette pal(bool dark) { return tokens::paletteFor(dark); }

// Anillo de selección del mockup: 2 px del color de superficie + 1.5 px de acento.
void paintRing(QPainter& p, const QRectF& r, qreal radius, bool dark) {
    const tokens::Palette tk = pal(dark);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(tk.accent, 1.5));
    p.drawRoundedRect(r.adjusted(-3.2, -3.2, 3.2, 3.2), radius + 3.2, radius + 3.2);
}

} // namespace

// ------------------------------------------------------------------ héroe
SheetHero::SheetHero(const QString& chipText, const QString& chipIcon, QWidget* parent, int height)
    : QWidget(parent)
    , m_chipText(chipText)
    , m_chipIcon(chipIcon) {
    setFixedSize(tokens::space::FloatCardWidth, height);
    setMouseTracking(true);
    m_title = new QLineEdit(this);
    m_title->setFrame(false);
    m_title->setGeometry(14, height - 12 - 6 - 24, width() - 28 - 22, 24);
    m_title->setMaxLength(80);
    setDark(false);
}

void SheetHero::setDark(bool dark) {
    m_dark = dark;
    if (m_folderMode)
        setFolder(m_folderColor, m_folderIcon);
    update();
}

void SheetHero::setFolder(int colorIndex, int iconIndex) {
    m_folderMode = true;
    m_folderColor = colorIndex;
    m_folderIcon = iconIndex;
    const tokens::Palette tk = pal(m_dark);
    m_title->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1; font-size: 15px; font-weight: 600;"
        " selection-background-color: rgba(127,127,127,90); selection-color: %1; padding: 0; }")
        .arg(tk.text.name()));
    update();
}

void SheetHero::setLook(const ThemeConfig& theme, BackgroundTemplate tpl, double scale) {
    m_theme = theme;
    m_tpl = tpl;
    m_scale = scale;
    const QString fg = theme.isDark ? QStringLiteral("#F4F4F5") : QStringLiteral("#18181B");
    m_title->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1; font-size: 16px; font-weight: 700;"
        " selection-background-color: rgba(127,127,127,90); selection-color: %1; padding: 0; }").arg(fg));
    update();
}

QRectF SheetHero::closeRect() const {
    return QRectF(width() - 12 - 26, 10, 26, 26);
}

void SheetHero::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect();
    QPainterPath clip;
    clip.addRoundedRect(r.adjusted(0, 0, 0, 40), 18, 18); // solo redondea arriba
    p.setClipPath(clip);
    if (m_folderMode) {
        p.fillRect(r, pal(m_dark).neutral);
        p.setClipping(false);
        libobj::paintFolder(p, QPointF(r.center().x(), 40.0 + libobj::folderRect(QPointF(0, 0)).height()),
                            m_folderColor, m_folderIcon, 0.0, m_dark);
    } else {
        p.fillRect(r, m_theme.background);
        paintMiniTemplate(p, r, m_theme, m_tpl, m_scale * 1.15);
        p.setClipping(false);
    }

    const QColor fg = m_folderMode ? pal(m_dark).text
                      : m_theme.isDark ? QColor("#F4F4F5") : QColor("#18181B");
    QColor glass = fg;
    glass.setAlpha(m_theme.isDark ? 31 : 20);

    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QFontMetrics fm(f);
    const qreal cw = 9 + 12 + 5 + fm.horizontalAdvance(m_chipText) + 9;
    const QRectF chip(14, 12, cw, 24);
    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawRoundedRect(chip, 12, 12);
    paintUiIcon(&p, QRectF(chip.left() + 9, chip.top() + 6, 12, 12), m_chipIcon, fg);
    p.setPen(fg);
    p.drawText(QRectF(chip.left() + 26, chip.top(), cw - 26, 24), Qt::AlignVCenter | Qt::AlignLeft, m_chipText);

    p.setPen(Qt::NoPen);
    p.setBrush(glass);
    p.drawEllipse(closeRect());
    paintUiIcon(&p, closeRect().adjusted(6.5, 6.5, -6.5, -6.5), QStringLiteral("x"), fg);

    QColor line = fg;
    line.setAlpha(46);
    p.fillRect(QRectF(14, height() - 12, width() - 28, 1), line);
    QColor pen = fg;
    pen.setAlphaF(0.6);
    paintUiIcon(&p, QRectF(width() - 14 - 14, height() - 12 - 6 - 19, 14, 14), QStringLiteral("pencil"), pen);
}

void SheetHero::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && closeRect().contains(e->position()))
        emit closeClicked();
}

void SheetHero::mouseMoveEvent(QMouseEvent* e) {
    setCursor(closeRect().contains(e->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

// ------------------------------------------------------------------ muestras
SwatchRow::SwatchRow(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(32);
    setCursor(Qt::PointingHandCursor);
}

void SwatchRow::setColors(const QVector<QColor>& colors, const QStringList& names) {
    m_colors = colors;
    m_names = names;
    update();
}

void SwatchRow::setCurrent(int i) {
    m_current = i;
    update();
}

void SwatchRow::setDark(bool dark) {
    m_dark = dark;
    update();
}

QRectF SwatchRow::cellRect(int i) const {
    const int n = std::max<int>(1, m_colors.size());
    const qreal d = 26.0;
    const qreal span = width() - d - 6.0; // margen para el anillo
    const qreal x = 3.0 + (n == 1 ? 0.0 : span * i / (n - 1));
    return QRectF(x, (height() - d) / 2.0, d, d);
}

void SwatchRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_colors.size(); ++i) {
        const QRectF r = cellRect(i);
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
        p.setBrush(m_colors.at(i));
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
        if (i == m_current) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(pal(m_dark).accent, 1.5));
            p.drawEllipse(r.adjusted(-3.2, -3.2, 3.2, 3.2));
        }
    }
}

void SwatchRow::mouseReleaseEvent(QMouseEvent* e) {
    for (int i = 0; i < m_colors.size(); ++i) {
        if (cellRect(i).adjusted(-4, -4, 4, 4).contains(e->position())) {
            m_current = i;
            update();
            emit picked(i);
            return;
        }
    }
}

// ------------------------------------------------------------------ plantillas
TemplateRow::TemplateRow(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(60);
    setCursor(Qt::PointingHandCursor);
}

void TemplateRow::setCurrent(BackgroundTemplate t) {
    m_current = t;
    update();
}

void TemplateRow::setDark(bool dark) {
    m_dark = dark;
    update();
}

QRectF TemplateRow::cellRect(int i) const {
    const qreal gap = 8.0, side = 3.0;
    const qreal w = (width() - side * 2 - gap * 3) / 4.0;
    return QRectF(side + i * (w + gap), 3.0, w, 54.0);
}

void TemplateRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const BackgroundTemplate tpls[] = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                       BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
    for (int i = 0; i < 4; ++i) {
        const QRectF r = cellRect(i);
        // Neutro fijo, nunca teñido con el color del tablero (decisión aprobada).
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#F6F6F4"));
        p.drawRoundedRect(r, 10, 10);

        QPainterPath clip;
        clip.addRoundedRect(r, 10, 10);
        p.save();
        p.setClipPath(clip);
        const QColor line(15, 23, 42, 56);
        p.setPen(QPen(line, 1.0));
        const qreal step = 9.0;
        switch (tpls[i]) {
            case BackgroundTemplate::Grid:
                for (qreal x = r.left() + step; x < r.right(); x += step)
                    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
                for (qreal y = r.top() + step; y < r.bottom(); y += step)
                    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
                break;
            case BackgroundTemplate::Dots:
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(15, 23, 42, 90));
                for (qreal y = r.top() + step / 2; y < r.bottom(); y += step)
                    for (qreal x = r.left() + step / 2; x < r.right(); x += step)
                        p.drawEllipse(QPointF(x, y), 1.0, 1.0);
                break;
            case BackgroundTemplate::Ruled:
                for (qreal y = r.top() + step; y < r.bottom(); y += step)
                    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
                break;
            default: break;
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(15, 23, 42, m_dark ? 40 : 26), 1.0));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        if (tpls[i] == m_current)
            paintRing(p, r, 10, m_dark);
    }
}

void TemplateRow::mouseReleaseEvent(QMouseEvent* e) {
    const BackgroundTemplate tpls[] = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                       BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
    for (int i = 0; i < 4; ++i) {
        if (cellRect(i).contains(e->position())) {
            m_current = tpls[i];
            update();
            emit picked(tpls[i]);
            return;
        }
    }
}

// ------------------------------------------------------------------ regla de trama
MmRuler::MmRuler(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(26 + 8 + 34 + 4 + 14);
    setMouseTracking(true);
}

void MmRuler::setValue(double mm) {
    m_value = std::clamp(std::round(mm / kStep) * kStep, kMin, kMax);
    update();
}

void MmRuler::setDark(bool dark) {
    m_dark = dark;
    update();
}

QRectF MmRuler::stepperRect() const {
    return QRectF(width() - 96, 0, 96, 26);
}

QRectF MmRuler::trackRect() const {
    return QRectF(0, 34, width(), 34);
}

double MmRuler::valueAt(qreal x) const {
    const QRectF t = trackRect();
    const qreal usable = t.width() - 16.0 - 14.0; // el pulgar mide 14
    const qreal f = std::clamp((x - 8.0 - 7.0) / usable, 0.0, 1.0);
    return kMin + f * (kMax - kMin);
}

void MmRuler::commit(double mm) {
    const double next = std::clamp(std::round(mm / kStep) * kStep, kMin, kMax);
    if (std::abs(next - m_value) < 1e-6)
        return;
    m_value = next;
    update();
    emit valueChanged(m_value);
}

void MmRuler::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    p.setOpacity(isEnabled() ? 1.0 : 0.45);

    QFont kf = font();
    kf.setPixelSize(10);
    kf.setWeight(QFont::Bold);
    kf.setLetterSpacing(QFont::PercentageSpacing, 108);
    p.setFont(kf);
    p.setPen(tk.muted);
    p.drawText(QRectF(0, 0, 80, 26), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("TRAMA"));

    const QRectF sr = stepperRect();
    p.setPen(Qt::NoPen);
    p.setBrush(tk.field);
    p.drawRoundedRect(sr, 8, 8);
    QFont vf = font();
    vf.setPixelSize(12);
    vf.setWeight(QFont::DemiBold);
    p.setFont(vf);
    const bool whole = std::abs(m_value - std::round(m_value)) < 1e-6;
    const QString val = (whole ? QString::number(int(m_value)) : QString::number(m_value, 'f', 1).replace('.', ','))
        + QStringLiteral(" mm");
    p.setPen(tk.text);
    p.drawText(sr, Qt::AlignCenter, val);
    p.setPen(tk.muted);
    p.drawText(QRectF(sr.left(), sr.top(), 20, sr.height()), Qt::AlignCenter, QStringLiteral("−"));
    p.drawText(QRectF(sr.right() - 20, sr.top(), 20, sr.height()), Qt::AlignCenter, QStringLiteral("+"));

    const QRectF t = trackRect();
    p.setPen(Qt::NoPen);
    p.setBrush(tk.field);
    p.drawRoundedRect(t, 10, 10);

    // Marcas cada 0,25 mm; más altas cada mm entero.
    const qreal usable = t.width() - 16.0 - 14.0;
    p.setPen(QPen(tk.guide, 1.0));
    for (int i = 0; i <= 36; ++i) {
        const double mm = kMin + i * 0.25;
        const qreal x = t.left() + 8.0 + 7.0 + usable * (mm - kMin) / (kMax - kMin);
        const qreal h = (i % 4 == 0) ? 12.0 : 6.0;
        p.drawLine(QPointF(x, t.bottom()), QPointF(x, t.bottom() - h));
    }

    const qreal tx = t.left() + 8.0 + usable * (m_value - kMin) / (kMax - kMin);
    const QRectF thumb(tx, t.top() + 5, 14, 24);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(thumb.translated(0, 1.2), 6, 6);
    p.setBrush(tk.surface);
    p.drawRoundedRect(thumb, 6, 6);
    p.setPen(QPen(tk.border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(thumb.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    QFont lf = font();
    lf.setPixelSize(10);
    p.setFont(lf);
    for (int mm : {3, 5, 8, 10, 12}) {
        const qreal x = t.left() + 8.0 + 7.0 + usable * (mm - kMin) / (kMax - kMin);
        const bool cur = std::abs(m_value - mm) < 0.01;
        p.setPen(cur ? tk.text : tk.faint);
        p.drawText(QRectF(x - 14, t.bottom() + 3, 28, 14), Qt::AlignCenter, QString::number(mm));
    }
}

void MmRuler::mousePressEvent(QMouseEvent* e) {
    if (!isEnabled() || e->button() != Qt::LeftButton)
        return;
    const QRectF sr = stepperRect();
    if (sr.contains(e->position())) {
        if (e->position().x() < sr.left() + 24)
            commit(m_value - kStep);
        else if (e->position().x() > sr.right() - 24)
            commit(m_value + kStep);
        return;
    }
    if (trackRect().contains(e->position())) {
        m_dragging = true;
        commit(valueAt(e->position().x()));
    }
}

void MmRuler::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging)
        commit(valueAt(e->position().x()));
    const bool hand = isEnabled() && (trackRect().contains(e->position()) || stepperRect().contains(e->position()));
    setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void MmRuler::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
}

// ------------------------------------------------------------------ chip
InfoChip::InfoChip(const QString& icon, const QString& text, bool chevron, bool dashed, QWidget* parent)
    : QWidget(parent)
    , m_icon(icon)
    , m_text(text)
    , m_chevron(chevron)
    , m_dashed(dashed) {
    setFixedHeight(28);
    setCursor(Qt::PointingHandCursor);
    setText(text);
}

void InfoChip::setText(const QString& text) {
    m_text = text;
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    const int w = 10 + 13 + 6 + QFontMetrics(f).horizontalAdvance(text) + (m_chevron ? 6 + 13 : 0) + 10;
    setFixedWidth(std::min(w, 230));
    update();
}

void InfoChip::setDark(bool dark) {
    m_dark = dark;
    update();
}

void InfoChip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (m_dashed) {
        QPen dash(m_dark ? QColor(255, 255, 255, 51) : QColor(15, 23, 42, 51), 1.0, Qt::DashLine);
        p.setPen(dash);
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(tk.field);
    }
    p.drawRoundedRect(r, 14, 14);
    const QColor fg = m_dashed ? tk.muted : tk.icon;
    paintUiIcon(&p, QRectF(10, 7.5, 13, 13), m_icon, fg);
    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(fg);
    const int textW = width() - 10 - 13 - 6 - 10 - (m_chevron ? 19 : 0);
    p.drawText(QRectF(29, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(f).elidedText(m_text, Qt::ElideRight, textW));
    if (m_chevron)
        paintUiIcon(&p, QRectF(width() - 10 - 13, 7.5, 13, 13), QStringLiteral("chevD"), fg);
}

void InfoChip::mouseReleaseEvent(QMouseEvent*) {
    emit clicked();
}

// ------------------------------------------------------------------ utilidades
QWidget* makeKicker(const QString& text, QWidget* parent, bool dark) {
    auto* l = new QLabel(text.toUpper(), parent);
    QFont f = l->font();
    f.setPixelSize(10);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 108);
    l->setFont(f);
    l->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                         .arg(pal(dark).muted.name()));
    return l;
}

QPushButton* makeCtaButton(const QString& text, QWidget* parent, bool dark) {
    auto* b = new QPushButton(text, parent);
    const tokens::Palette tk = pal(dark);
    b->setFixedHeight(38);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 11px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: %3; }")
        .arg(tk.accent.name(), tk.accentFg.name(), dark ? QStringLiteral("#FFFFFF") : QStringLiteral("#27272A")));
    return b;
}


// ------------------------------------------------------------------ puntos de color
DotRow::DotRow(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(32);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}
void DotRow::setCurrent(int i) { m_current = i; update(); }
void DotRow::setDark(bool d) { m_dark = d; update(); }

QRectF DotRow::cellRect(int i) const {
    const qreal gap = (width() - 8 * 28.0) / 7.0;
    return QRectF(i * (28.0 + gap), 2, 28, 28);
}
int DotRow::hit(const QPointF& pos) const {
    for (int i = 0; i < 8; ++i)
        if (cellRect(i).adjusted(-2, -2, 2, 2).contains(pos))
            return i;
    return -1;
}
void DotRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    for (int i = 0; i < 8; ++i) {
        const tokens::FolderColor& c = tokens::folderColor(i);
        QRectF r = cellRect(i);
        if (i == m_hover && i != m_current)
            r = r.adjusted(-1.1, -1.1, 1.1, 1.1);
        if (i == m_current) {
            p.setPen(QPen(tk.surface, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(r.adjusted(-1, -1, 1, 1));
            p.setPen(QPen(tk.accent, 1.8));
            p.drawEllipse(r.adjusted(-2.9, -2.9, 2.9, 2.9));
        }
        QPainterPath circle;
        circle.addEllipse(r);
        p.setClipPath(circle);
        p.fillRect(r, c.body2);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() * 0.38), c.flap);
        p.setClipping(false);
        p.setPen(QPen(QColor(15, 23, 42, 26), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}
void DotRow::mouseMoveEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h != m_hover) { m_hover = h; update(); }
}
void DotRow::leaveEvent(QEvent*) { m_hover = -1; update(); }
void DotRow::mouseReleaseEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h >= 0) { m_current = h; update(); emit picked(h); }
}

// ------------------------------------------------------------------ rejilla de íconos
IconGrid::IconGrid(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(2 * 40 + 6);
}
void IconGrid::setCurrent(int i) { m_current = i; update(); }
void IconGrid::setDark(bool d) { m_dark = d; update(); }

QRectF IconGrid::cellRect(int i) const {
    const qreal cell = (width() - 6.0 * 6) / 7.0;
    return QRectF((i % 7) * (cell + 6), (i / 7) * (cell + 6), cell, cell);
}
int IconGrid::hit(const QPointF& pos) const {
    for (int i = 0; i < 14; ++i)
        if (cellRect(i).contains(pos))
            return i;
    return -1;
}
void IconGrid::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    static const char* names[] = {"", "book", "shape", "sun", "star", "music", "flask",
                                  "code", "calc", "globe", "heart", "case", "pencil", "bolt"};
    for (int i = 0; i < 14; ++i) {
        QRectF r = cellRect(i);
        const bool on = i == m_current;
        if (i == m_hover && !on)
            r.translate(0, -1);
        p.setPen(on ? QPen(tk.accent, 1.5) : Qt::NoPen);
        p.setBrush(on ? tk.surface : (i == m_hover ? tk.hover : tk.neutral));
        p.drawRoundedRect(on ? r.adjusted(0.75, 0.75, -0.75, -0.75) : r, 10, 10);
        const QRectF ir(r.center().x() - 8.5, r.center().y() - 8.5, 17, 17);
        const QColor ink = on ? tk.text : tk.icon;
        if (i == 0) {
            p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            const QRectF c(ir.left() + 2.5, ir.top() + 2.5, 12, 12);
            p.drawEllipse(c);
            p.drawLine(QPointF(ir.left() + 4.4, ir.bottom() - 3.4), QPointF(ir.right() - 3.4, ir.top() + 4.4));
        } else {
            paintUiIcon(&p, ir, QString::fromLatin1(names[i]), ink, QStringLiteral("folder"));
        }
    }
}
void IconGrid::mouseMoveEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h != m_hover) { m_hover = h; update(); }
}
void IconGrid::leaveEvent(QEvent*) { m_hover = -1; update(); }
void IconGrid::mouseReleaseEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h >= 0) { m_current = h; update(); emit picked(h); }
}

// ------------------------------------------------------------------ selector de ubicación
namespace { constexpr int kLpRow = 30, kLpPad = 6, kLpW = 240; }

LocationPopover::LocationPopover(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    hide();
}
void LocationPopover::setDark(bool d) { m_dark = d; update(); }
void LocationPopover::setCurrent(const QString& rel) { m_current = rel; update(); }
void LocationPopover::setEntries(const QVector<LocationEntry>& e) {
    m_entries = e;
    setFixedSize(kLpW, int(e.size()) * kLpRow + 2 * kLpPad);
}
void LocationPopover::popup(const QRect& anchor) {
    const int x = anchor.left() - 12 - width();
    int y = anchor.top() - 6;
    if (parentWidget())
        y = std::clamp(y, 8, std::max(8, parentWidget()->height() - height() - 8));
    move(x, y);
    raise();
    show();
}
int LocationPopover::hit(const QPointF& pos) const {
    const int i = int((pos.y() - kLpPad) / kLpRow);
    return (pos.y() >= kLpPad && i >= 0 && i < m_entries.size()) ? i : -1;
}
void LocationPopover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    p.setPen(QPen(tk.borderPop, 1));
    p.setBrush(tk.surface);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    QFont f = font();
    f.setPixelSize(12);
    for (int i = 0; i < m_entries.size(); ++i) {
        const LocationEntry& en = m_entries[i];
        const QRectF row(kLpPad, kLpPad + i * kLpRow, width() - 2 * kLpPad, kLpRow - 2);
        const bool on = en.relativePath == m_current;
        if (on || i == m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(on ? tk.soft : tk.neutral);
            p.drawRoundedRect(row, 8, 8);
        }
        const qreal x = row.left() + 8 + en.depth * 12;
        if (en.depth == 0 && en.relativePath.isEmpty()) {
            paintUiIcon(&p, QRectF(x, row.center().y() - 7, 14, 14), QStringLiteral("folder"), tk.icon);
        } else {
            const tokens::FolderColor& c = tokens::folderColor(en.colorIndex);
            p.setPen(Qt::NoPen);
            p.setBrush(c.body2);
            p.drawRoundedRect(QRectF(x + 2, row.center().y() - 4.5, 9, 9), 3, 3);
        }
        f.setWeight(on ? QFont::DemiBold : QFont::Medium);
        p.setFont(f);
        p.setPen(on ? tk.text : tk.icon);
        p.drawText(QRectF(x + 22, row.top(), row.right() - x - 46, row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   en.name);
        if (on)
            paintUiIcon(&p, QRectF(row.right() - 22, row.center().y() - 7, 14, 14), QStringLiteral("check"), tk.text);
    }
}
void LocationPopover::mouseMoveEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h != m_hover) { m_hover = h; update(); }
}
void LocationPopover::leaveEvent(QEvent*) { m_hover = -1; update(); }
void LocationPopover::mouseReleaseEvent(QMouseEvent* e) {
    const int h = hit(e->position());
    if (h >= 0) {
        m_current = m_entries[h].relativePath;
        emit picked(m_current);
        hide();
    }
}

} // namespace finily
