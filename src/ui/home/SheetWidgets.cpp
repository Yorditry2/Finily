#include "ui/home/SheetWidgets.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QApplication>
#include <QConicalGradient>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace finily {
namespace {

constexpr int kPopMargin = 14;
constexpr int kLpRow = 30;
constexpr int kLpPad = 6;
constexpr int kLpW = 240;
constexpr int kLpMaxH = 260;

constexpr int kTpRow = 30;
constexpr int kTpPad = 6;
constexpr int kTpW = 236;
constexpr int kTpMaxListH = 210;
constexpr int kTpInputH = 36;

tokens::Palette pal(bool dark) { return tokens::paletteFor(dark); }

QFont appUiFont(qreal px, QFont::Weight weight = QFont::Normal) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

// Anillo de selección del mockup: 2 px del color de superficie + 1.5 px de acento.
void paintRing(QPainter& p, const QRectF& r, qreal radius, bool dark) {
    const tokens::Palette tk = pal(dark);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(tk.accent, 1.5));
    p.drawRoundedRect(r.adjusted(-3.2, -3.2, 3.2, 3.2), radius + 3.2, radius + 3.2);
}

QPoint popoverPosForAnchor(const QRect& anchor, const QSize& cardSize, const QWidget* parent) {
    const int parentW = parent ? parent->width() : 1180;
    const int parentH = parent ? parent->height() : 720;
    int x = anchor.left() - 12 - cardSize.width();
    int y = anchor.top() - 6;
    if (x < 12) {
        // Hoja centrada (ej. Nuevo cuaderno): abrir arriba del chip si no cabe a la izquierda.
        x = std::clamp(anchor.left(), 12, std::max(12, parentW - cardSize.width() - 12));
        const int aboveY = anchor.top() - cardSize.height() - 8;
        y = (aboveY >= 12) ? aboveY : std::min(anchor.bottom() + 8, std::max(12, parentH - cardSize.height() - 12));
    } else {
        y = std::clamp(y, 12, std::max(12, parentH - cardSize.height() - 12));
    }
    return QPoint(x, y);
}

void styleKickerLabel(QLabel* l, bool dark) {
    if (!l)
        return;
    QFont f = appUiFont(10, QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 108);
    l->setFont(f);
    l->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                         .arg(pal(dark).muted.name(QColor::HexArgb)));
}

void styleCtaButton(QPushButton* b, bool dark) {
    if (!b)
        return;
    b->setFont(appUiFont(13, QFont::DemiBold));
    const tokens::Palette tk = pal(dark);
    const HomePalette hp = homePaletteFor(dark);
    b->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 11px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(tk.accent.name(QColor::HexArgb),
             tk.accentFg.name(QColor::HexArgb),
             hp.accentHover.name(QColor::HexArgb),
             tk.field.name(QColor::HexArgb),
             tk.faint.name(QColor::HexArgb)));
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
    m_title->setFont(appUiFont(16, QFont::Bold));
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
    m_title->setFont(appUiFont(15, QFont::DemiBold));
    m_title->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1;"
        " selection-background-color: rgba(127,127,127,90); selection-color: %1; padding: 0; }")
        .arg(tk.text.name()));
    update();
}

void SheetHero::setLook(const ThemeConfig& theme, BackgroundTemplate tpl, double scale) {
    m_folderMode = false;
    m_theme = theme;
    m_tpl = tpl;
    m_scale = scale;
    const QString fg = theme.isDark ? QStringLiteral("#F4F4F5") : QStringLiteral("#18181B");
    m_title->setFont(appUiFont(16, QFont::Bold));
    m_title->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1;"
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
        const QRectF nat = libobj::folderRect(QPointF(0, 0));
        const qreal s = std::min<qreal>(1.0, std::max<qreal>(0.4, (r.height() - 48.0) / std::max<qreal>(1.0, nat.height())));
        const QPointF bc(r.center().x(), 40.0 + nat.height() * s);
        p.save();
        p.translate(bc);
        p.scale(s, s);
        libobj::paintFolder(p, QPointF(0, 0), m_folderColor, m_folderIcon, 0.0, m_dark);
        p.restore();
    } else {
        p.fillRect(r, m_theme.background);
        paintMiniTemplate(p, r, m_theme, m_tpl, m_scale * 1.15);
        p.setClipping(false);
    }

    const QColor fg = m_folderMode ? pal(m_dark).text
                      : m_theme.isDark ? QColor("#F4F4F5") : QColor("#18181B");
    QColor glass = fg;
    glass.setAlpha(m_theme.isDark ? 31 : 20);

    const QFont f = appUiFont(11, QFont::DemiBold);
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

void SwatchRow::setShowMoreButton(bool on) {
    m_showMore = on;
    update();
}

void SwatchRow::setMoreActive(bool on) {
    m_moreActive = on;
    update();
}

void SwatchRow::setMoreColor(const QColor& c) {
    m_moreColor = c;
    update();
}

QPointF SwatchRow::moreCenterInWidget() const {
    if (!m_showMore)
        return {};
    return cellRect(int(m_colors.size())).center();
}

QRectF SwatchRow::cellRect(int i) const {
    const int n = std::max<int>(1, int(m_colors.size()) + (m_showMore ? 1 : 0));
    const qreal d = 26.0;
    const qreal span = width() - d - 6.0; // margen para el anillo
    const qreal x = 3.0 + (n == 1 ? 0.0 : span * i / (n - 1));
    return QRectF(x, (height() - d) / 2.0, d, d);
}

void SwatchRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = pal(m_dark);
    for (int i = 0; i < m_colors.size(); ++i) {
        const QRectF r = cellRect(i);
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
        p.setBrush(m_colors.at(i));
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
        if (i == m_current) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(tk.surface, 2.0));
            p.drawEllipse(r.adjusted(-1.2, -1.2, 1.2, 1.2));
            p.setPen(QPen(tk.accent, 1.5));
            p.drawEllipse(r.adjusted(-3.2, -3.2, 3.2, 3.2));
        }
    }
    if (m_showMore) {
        const QRectF r = cellRect(int(m_colors.size()));
        const bool ringOn = m_moreActive || (m_current < 0 && m_moreColor.isValid());
        if (ringOn) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(tk.surface, 2.0));
            p.drawEllipse(r.adjusted(-1.2, -1.2, 1.2, 1.2));
            p.setPen(QPen(tk.accent, 1.5));
            p.drawEllipse(r.adjusted(-3.2, -3.2, 3.2, 3.2));
        }
        QConicalGradient cg(r.center(), 90.0);
        cg.setColorAt(0.0 / 6.0, QColor(0xE7, 0x8F, 0xA8));
        cg.setColorAt(1.0 / 6.0, QColor(0xE1, 0xC0, 0xA2));
        cg.setColorAt(2.0 / 6.0, QColor(0xED, 0xE0, 0x9A));
        cg.setColorAt(3.0 / 6.0, QColor(0x92, 0xBF, 0xA9));
        cg.setColorAt(4.0 / 6.0, QColor(0x92, 0xB5, 0xDD));
        cg.setColorAt(5.0 / 6.0, QColor(0xB3, 0xA2, 0xDC));
        cg.setColorAt(1.0, QColor(0xE7, 0x8F, 0xA8));
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
        p.setBrush(cg);
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));

        const QRectF inner = r.adjusted(4.0, 4.0, -4.0, -4.0);
        const bool customDot = (m_current < 0 && m_moreColor.isValid());
        const QColor innerFill = customDot ? m_moreColor : tk.surface;
        p.setPen(Qt::NoPen);
        p.setBrush(innerFill);
        p.drawEllipse(inner);

        QColor plusColor = tk.icon;
        if (customDot) {
            const double lum = 0.299 * m_moreColor.red() + 0.587 * m_moreColor.green() + 0.114 * m_moreColor.blue();
            plusColor = (lum < 140.0) ? QColor(0xF4, 0xF4, 0xF5) : QColor(0x18, 0x18, 0x1B);
        }
        paintUiIcon(&p, QRectF(r.center().x() - 5.5, r.center().y() - 5.5, 11.0, 11.0),
                    QStringLiteral("plus"), plusColor);
    }
}

void SwatchRow::mouseReleaseEvent(QMouseEvent* e) {
    for (int i = 0; i < m_colors.size(); ++i) {
        if (cellRect(i).adjusted(-4, -4, 4, 4).contains(e->position())) {
            m_current = i;
            m_moreColor = QColor();
            update();
            emit picked(i);
            return;
        }
    }
    if (m_showMore && cellRect(int(m_colors.size())).adjusted(-4, -4, 4, 4).contains(e->position()))
        emit moreClicked();
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
    const qreal h = std::max<qreal>(44.0, height() - 6.0);
    return QRectF(side + i * (w + gap), 3.0, w, h);
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

    QFont kf = appUiFont(10, QFont::Bold);
    kf.setLetterSpacing(QFont::PercentageSpacing, 108);
    p.setFont(kf);
    p.setPen(tk.muted);
    p.drawText(QRectF(0, 0, 80, 26), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("TRAMA"));

    const QRectF sr = stepperRect();
    p.setPen(Qt::NoPen);
    p.setBrush(tk.field);
    p.drawRoundedRect(sr, 8, 8);
    p.setFont(appUiFont(12, QFont::DemiBold));
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
    p.setBrush(m_dark ? QColor("#F4F4F5") : tk.surface);
    p.drawRoundedRect(thumb, 6, 6);
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 90) : tk.border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(thumb.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);

    p.setFont(appUiFont(10, QFont::Normal));
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
    const QFont f = appUiFont(11, QFont::DemiBold);
    const int w = 10 + 13 + 6 + QFontMetrics(f).horizontalAdvance(text) + (m_chevron ? 6 + 13 : 0) + 10;
    setFixedWidth(std::min(w, m_maxWidth));
    update();
}

void InfoChip::setMaxWidth(int maxWidth) {
    m_maxWidth = std::max(60, maxWidth);
    setText(m_text);
}

void InfoChip::setDashed(bool dashed) {
    if (m_dashed == dashed)
        return;
    m_dashed = dashed;
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
    const QFont f = appUiFont(11, QFont::DemiBold);
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
    l->setObjectName(QStringLiteral("finilySheetKicker"));
    styleKickerLabel(l, dark);
    return l;
}

QPushButton* makeCtaButton(const QString& text, QWidget* parent, bool dark) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(QStringLiteral("finilySheetCta"));
    b->setFixedHeight(38);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    styleCtaButton(b, dark);
    return b;
}

void refreshSheetWidgetsTheme(QWidget* root, bool dark) {
    if (!root)
        return;
    for (QLabel* l : root->findChildren<QLabel*>(QStringLiteral("finilySheetKicker")))
        styleKickerLabel(l, dark);
    for (QPushButton* b : root->findChildren<QPushButton*>(QStringLiteral("finilySheetCta")))
        styleCtaButton(b, dark);
}

QPushButton* makeModalCloseButton(QWidget* parent) {
    auto* close = new QPushButton(parent);
    close->setFixedSize(28, 28);
    close->setCursor(Qt::PointingHandCursor);
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(QStringLiteral("Cerrar"));
    return close;
}

void styleModalCloseButton(QPushButton* btn, bool dark) {
    if (!btn)
        return;
    const tokens::Palette tk = pal(dark);
    btn->setIcon(uiIcon(QStringLiteral("x"), tk.icon, 14));
    btn->setIconSize(QSize(14, 14));
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;border:0;border-radius:14px;}QPushButton:hover{background:%2;}")
                           .arg(tk.field.name(), tk.hover.name(QColor::HexArgb)));
}

void styleModalTitleEdit(QLineEdit* edit, bool dark, qreal fontSizePx) {
    if (!edit)
        return;
    const tokens::Palette tk = pal(dark);
    QFont f = QApplication::font();
    f.setPixelSize(qRound(fontSizePx));
    f.setWeight(QFont::DemiBold);
    edit->setFont(f);
    const QString selBg = dark ? QStringLiteral("rgba(255,255,255,60)")
                               : QStringLiteral("rgba(24,24,27,36)");
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:transparent;border:0;border-bottom:1px solid %1;padding:0 0 6px 0;color:%2;"
        "selection-background-color:%3;selection-color:%2;}QLineEdit:focus{border-bottom:1px solid %4;}")
                            .arg(tk.borderPop.name(QColor::HexArgb), tk.text.name(),
                                 selBg, tk.accent.name()));
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
LocationPopover::LocationPopover(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (!m_closing)
            return;
        m_closing = false;
        m_backdrop = QImage();
        hide();
    });
    hide();
}

void LocationPopover::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    update();
}

void LocationPopover::setDark(bool d) {
    m_dark = d;
    update();
}

void LocationPopover::setCurrent(const QString& rel) {
    m_current = rel;
    update();
}

int LocationPopover::contentHeight() const {
    return int(m_entries.size()) * kLpRow + 2 * kLpPad;
}

int LocationPopover::maxScroll() const {
    return std::max(0, contentHeight() - m_cardRect.height());
}

void LocationPopover::updateCardSize() {
    const int cardH = std::clamp(contentHeight(), kLpRow + 2 * kLpPad, kLpMaxH);
    m_cardRect = QRect(kPopMargin, kPopMargin, kLpW, cardH);
    setFixedSize(kLpW + 2 * kPopMargin, cardH + 2 * kPopMargin);
    m_scrollY = std::clamp(m_scrollY, 0, maxScroll());
}

void LocationPopover::setEntries(const QVector<LocationEntry>& e) {
    m_entries = e;
    m_scrollY = 0;
    updateCardSize();
}

void LocationPopover::popup(const QRect& anchor) {
    if (!parentWidget())
        return;
    m_anim->stop();
    m_closing = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    if (isVisible())
        hide();

    updateCardSize();
    const QPoint topLeft = popoverPosForAnchor(anchor, m_cardRect.size(), parentWidget());
    move(topLeft.x() - kPopMargin, topLeft.y() - kPopMargin);

    if (parentWidget()->isVisible() && !tokens::reducedMotion()) {
        m_grabbingBackdrop = true;
        m_backdrop = blurBackdropImage(parentWidget()->grab(QRect(topLeft, m_cardRect.size())).toImage(), 11);
        m_grabbingBackdrop = false;
    } else {
        m_backdrop = QImage();
    }

    m_hover = -1;
    m_progress = 0.0;
    show();
    raise();

    m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void LocationPopover::closePopover() {
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_hover = -1;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

int LocationPopover::hit(const QPointF& pos) const {
    if (!m_cardRect.contains(pos.toPoint()))
        return -1;
    const qreal localY = pos.y() - m_cardRect.top() - kLpPad + m_scrollY;
    const int i = int(localY / kLpRow);
    return (localY >= 0 && i >= 0 && i < m_entries.size()) ? i : -1;
}

void LocationPopover::paintEvent(QPaintEvent*) {
    if (m_grabbingBackdrop)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool reduced = tokens::reducedMotion();
    if (!reduced) {
        const qreal dx = (1.0 - m_progress) * 10.0;
        const qreal sc = m_closing ? (0.98 + 0.02 * m_progress) : (0.97 + 0.03 * m_progress);
        const QPointF origin(m_cardRect.right(), m_cardRect.center().y());
        p.translate(origin.x() + dx, origin.y());
        p.scale(sc, sc);
        p.translate(-origin);
    }
    p.setOpacity(m_progress);

    const tokens::Palette tk = pal(m_dark);
    paintLiquidGlassPanel(p, QRectF(m_cardRect), tokens::radius::Menu, m_backdrop, homePaletteFor(m_dark), m_dark);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(m_cardRect).adjusted(1, 1, -1, -1), tokens::radius::Menu - 1, tokens::radius::Menu - 1);
    p.save();
    p.setClipPath(clip);

    for (int i = 0; i < m_entries.size(); ++i) {
        const LocationEntry& en = m_entries[i];
        const QRectF row(m_cardRect.left() + kLpPad,
                         m_cardRect.top() + kLpPad + i * kLpRow - m_scrollY,
                         m_cardRect.width() - 2 * kLpPad, kLpRow - 2);
        if (row.bottom() < m_cardRect.top() || row.top() > m_cardRect.bottom())
            continue;
        const bool on = en.relativePath == m_current;
        if (on || i == m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(on ? tk.soft : tk.hover);
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
        const QFont f = appUiFont(12, on ? QFont::DemiBold : QFont::Medium);
        p.setFont(f);
        p.setPen(on ? tk.text : tk.icon);
        const qreal textW = std::max<qreal>(24, row.right() - x - 46);
        p.drawText(QRectF(x + 22, row.top(), textW, row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(en.name, Qt::ElideRight, qRound(textW)));
        if (on)
            paintUiIcon(&p, QRectF(row.right() - 22, row.center().y() - 7, 14, 14), QStringLiteral("check"), tk.text);
    }
    p.restore();
}

void LocationPopover::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const int h = hit(e->position());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void LocationPopover::wheelEvent(QWheelEvent* e) {
    if (maxScroll() <= 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_scrollY = std::clamp(m_scrollY - e->angleDelta().y() / 4, 0, maxScroll());
    update();
    e->accept();
}

void LocationPopover::leaveEvent(QEvent*) {
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

void LocationPopover::mouseReleaseEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const int h = hit(e->position());
    if (h >= 0) {
        m_current = m_entries[h].relativePath;
        emit picked(m_current);
        closePopover();
    }
}

// ------------------------------------------------------------------ selector de etiquetas
TagPickPopover::TagPickPopover(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_newEdit = new QLineEdit(this);
    m_newEdit->setFrame(false);
    m_newEdit->setMaxLength(40);
    m_newEdit->setPlaceholderText(QStringLiteral("Nueva etiqueta"));
    m_newEdit->installEventFilter(this);
    m_editOpacity = new QGraphicsOpacityEffect(m_newEdit);
    m_editOpacity->setOpacity(1.0);
    m_editOpacity->setEnabled(false);
    m_newEdit->setGraphicsEffect(m_editOpacity);

    connect(m_newEdit, &QLineEdit::returnPressed, this, [this] {
        const QString name = m_newEdit->text().trimmed();
        if (name.isEmpty())
            return;
        m_newEdit->clear();
        emit newTagRequested(name);
    });

    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (m_editOpacity)
            m_editOpacity->setEnabled(false);
        if (!m_closing)
            return;
        m_closing = false;
        m_backdrop = QImage();
        hide();
    });

    updateInputStyle();
    updateGeometryAndInput();
    hide();
}

void TagPickPopover::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    if (m_editOpacity) {
        if (m_progress < 0.999) {
            if (!m_editOpacity->isEnabled())
                m_editOpacity->setEnabled(true);
            m_editOpacity->setOpacity(m_progress);
        } else {
            m_editOpacity->setOpacity(1.0);
            m_editOpacity->setEnabled(false);
        }
    }
    update();
}

int TagPickPopover::totalListHeight() const {
    return m_tags.isEmpty() ? 26 : int(m_tags.size()) * kTpRow;
}

int TagPickPopover::listHeight() const {
    return std::min(totalListHeight(), kTpMaxListH);
}

int TagPickPopover::maxScroll() const {
    return std::max(0, totalListHeight() - listHeight());
}

void TagPickPopover::updateGeometryAndInput() {
    const int cardH = kTpPad + listHeight() + 1 + kTpInputH + kTpPad;
    m_cardRect = QRect(kPopMargin, kPopMargin, kTpW, cardH);
    setFixedSize(kTpW + 2 * kPopMargin, cardH + 2 * kPopMargin);
    m_scrollY = std::clamp(m_scrollY, 0, maxScroll());
    const int inputY = m_cardRect.bottom() - kTpPad - kTpInputH + 6;
    m_newEdit->setGeometry(m_cardRect.left() + kTpPad + 28, inputY,
                           m_cardRect.width() - 2 * kTpPad - 34, kTpInputH - 12);
}

void TagPickPopover::updateInputStyle() {
    const tokens::Palette tk = pal(m_dark);
    m_newEdit->setFont(appUiFont(12, QFont::Medium));
    QPalette ep = m_newEdit->palette();
    ep.setColor(QPalette::PlaceholderText, tk.faint);
    m_newEdit->setPalette(ep);
    m_newEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1; padding: 0; }")
        .arg(tk.text.name()));
}

void TagPickPopover::setTags(const QVector<TagInfo>& tags) {
    m_tags = tags;
    updateGeometryAndInput();
    if (isVisible() && !m_closing && m_hasAnchor && parentWidget()) {
        const QPoint topLeft = popoverPosForAnchor(m_lastAnchor, m_cardRect.size(), parentWidget());
        move(topLeft.x() - kPopMargin, topLeft.y() - kPopMargin);
        if (parentWidget()->isVisible() && !tokens::reducedMotion()) {
            m_grabbingBackdrop = true;
            m_backdrop = blurBackdropImage(parentWidget()->grab(QRect(topLeft, m_cardRect.size())).toImage(), 11);
            m_grabbingBackdrop = false;
        }
    }
    update();
}

void TagPickPopover::setSelectedTags(const QStringList& ids) {
    m_selected = ids;
    update();
}

void TagPickPopover::setDark(bool dark) {
    m_dark = dark;
    updateInputStyle();
    update();
}

void TagPickPopover::popup(const QRect& anchorInParent) {
    if (!parentWidget())
        return;
    m_anim->stop();
    m_closing = false;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    if (isVisible())
        hide();

    m_lastAnchor = anchorInParent;
    m_hasAnchor = true;
    m_scrollY = 0;
    m_hover = -1;
    updateGeometryAndInput();

    const QPoint topLeft = popoverPosForAnchor(anchorInParent, m_cardRect.size(), parentWidget());
    move(topLeft.x() - kPopMargin, topLeft.y() - kPopMargin);

    if (parentWidget()->isVisible() && !tokens::reducedMotion()) {
        m_grabbingBackdrop = true;
        m_backdrop = blurBackdropImage(parentWidget()->grab(QRect(topLeft, m_cardRect.size())).toImage(), 11);
        m_grabbingBackdrop = false;
    } else {
        m_backdrop = QImage();
    }

    setProgress(0.0);
    show();
    raise();

    m_anim->setDuration(tokens::motion::duration(tokens::motion::Base));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void TagPickPopover::closePopover() {
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_hover = -1;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

int TagPickPopover::hitRow(const QPointF& pos) const {
    const QRect listRect(m_cardRect.left() + kTpPad, m_cardRect.top() + kTpPad,
                         m_cardRect.width() - 2 * kTpPad, listHeight());
    if (!listRect.contains(pos.toPoint()) || m_tags.isEmpty())
        return -1;
    const qreal localY = pos.y() - listRect.top() + m_scrollY;
    const int i = int(localY / kTpRow);
    return (i >= 0 && i < m_tags.size()) ? i : -1;
}

void TagPickPopover::paintEvent(QPaintEvent*) {
    if (m_grabbingBackdrop)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool reduced = tokens::reducedMotion();
    if (!reduced) {
        const qreal dx = (1.0 - m_progress) * 10.0;
        const qreal sc = m_closing ? (0.98 + 0.02 * m_progress) : (0.97 + 0.03 * m_progress);
        const QPointF origin(m_cardRect.right(), m_cardRect.center().y());
        p.translate(origin.x() + dx, origin.y());
        p.scale(sc, sc);
        p.translate(-origin);
    }
    p.setOpacity(m_progress);

    const tokens::Palette tk = pal(m_dark);
    paintLiquidGlassPanel(p, QRectF(m_cardRect), tokens::radius::Menu, m_backdrop, homePaletteFor(m_dark), m_dark);

    const QRect listRect(m_cardRect.left() + kTpPad, m_cardRect.top() + kTpPad,
                         m_cardRect.width() - 2 * kTpPad, listHeight());
    p.save();
    p.setClipRect(listRect);

    if (m_tags.isEmpty()) {
        p.setFont(appUiFont(11.5, QFont::Medium));
        p.setPen(tk.faint);
        p.drawText(listRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Sin etiquetas"));
    } else {
        for (int i = 0; i < m_tags.size(); ++i) {
            const TagInfo& tag = m_tags.at(i);
            const QRectF row(listRect.left(), listRect.top() + i * kTpRow - m_scrollY,
                             listRect.width(), kTpRow - 2);
            if (row.bottom() < listRect.top() || row.top() > listRect.bottom())
                continue;
            const bool checked = m_selected.contains(tag.id);
            const bool hov = (i == m_hover);
            if (hov) {
                p.setPen(Qt::NoPen);
                p.setBrush(tk.hover);
                p.drawRoundedRect(row, 8, 8);
            }
            const QRectF box(row.left() + 8, row.center().y() - 7.5, 15, 15);
            if (checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(tk.accent);
                p.drawRoundedRect(box, 4, 4);
                paintUiIcon(&p, box.adjusted(2.0, 2.0, -2.0, -2.0), QStringLiteral("check"), tk.accentFg);
            } else {
                p.setPen(QPen(m_dark ? QColor(255, 255, 255, 70) : QColor(0xC4, 0xC4, 0xCB), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
            }

            const qreal dotX = box.right() + 9;
            p.setPen(Qt::NoPen);
            p.setBrush(tokens::folderColor(tag.colorIndex).flap);
            p.drawEllipse(QRectF(dotX, row.center().y() - 4.0, 8, 8));

            const qreal textX = dotX + 15;
            const qreal textW = std::max<qreal>(20, row.right() - 8 - textX);
            const QFont f = appUiFont(12, checked ? QFont::DemiBold : QFont::Medium);
            p.setFont(f);
            p.setPen(checked || hov ? tk.text : tk.icon);
            p.drawText(QRectF(textX, row.top(), textW, row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(f).elidedText(tag.name, Qt::ElideRight, qRound(textW)));
        }
    }
    p.restore();

    const int sepY = m_cardRect.bottom() - kTpPad - kTpInputH;
    p.fillRect(QRect(m_cardRect.left() + 10, sepY, m_cardRect.width() - 20, 1), tk.border);
    paintUiIcon(&p, QRectF(m_cardRect.left() + kTpPad + 8, sepY + (kTpInputH - 13) / 2.0, 13, 13),
                QStringLiteral("plus"), tk.muted);
}

void TagPickPopover::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const int h = hitRow(e->position());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TagPickPopover::wheelEvent(QWheelEvent* e) {
    if (maxScroll() <= 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_scrollY = std::clamp(m_scrollY - e->angleDelta().y() / 4, 0, maxScroll());
    update();
    e->accept();
}

void TagPickPopover::leaveEvent(QEvent*) {
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

void TagPickPopover::mouseReleaseEvent(QMouseEvent* e) {
    if (m_closing || e->button() != Qt::LeftButton)
        return;
    const int h = hitRow(e->position());
    if (h >= 0) {
        const QString& id = m_tags.at(h).id;
        if (m_selected.contains(id))
            m_selected.removeAll(id);
        else
            m_selected.append(id);
        update();
        emit selectionChanged(m_selected);
    }
}

void TagPickPopover::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        closePopover();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool TagPickPopover::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_newEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            closePopover();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace finily
