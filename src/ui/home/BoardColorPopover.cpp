#include "ui/home/BoardColorPopover.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

#include <algorithm>
#include <array>

namespace finily {
namespace {

constexpr int kMargin = 14;
constexpr int kCardW = 236;
constexpr int kCardH = 316;
constexpr int kPad = 14;
constexpr int kSwatchSize = 26;
constexpr int kSwatchRowGap = 10;

const std::array<QColor, 12>& curatedSwatches() {
    static const std::array<QColor, 12> kSwatches = {
        QColor("#0F1014"), QColor("#1A1B22"), QColor("#16302A"), QColor("#1E2A44"), QColor("#2A1E33"), QColor("#2B2520"),
        QColor("#FFFFFF"), QColor("#FAF7EE"), QColor("#F3EEE3"), QColor("#EEF0F3"), QColor("#E8EAE4"), QColor("#E6E2DA"),
    };
    return kSwatches;
}

QFont popFont(qreal px, QFont::Weight weight) {
    QFont f = QApplication::font();
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

} // namespace

BoardColorPopover::BoardColorPopover(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_cardRect = QRect(kMargin, kMargin, kCardW, kCardH);
    setFixedSize(kCardW + 2 * kMargin, kCardH + 2 * kMargin);

    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setFrame(false);
    m_hexEdit->setMaxLength(7);
    m_hexEdit->installEventFilter(this);
    m_hexOpacity = new QGraphicsOpacityEffect(m_hexEdit);
    m_hexOpacity->setOpacity(1.0);
    m_hexOpacity->setEnabled(false);
    m_hexEdit->setGraphicsEffect(m_hexOpacity);

    const QRectF fr = hexFieldRect();
    m_hexEdit->setGeometry(qRound(fr.left() + 34), qRound(fr.top() + 5),
                           qRound(fr.width() - 44), qRound(fr.height() - 10));

    connect(m_hexEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        QString s = text.trimmed();
        if (!s.startsWith(QLatin1Char('#')))
            s.prepend(QLatin1Char('#'));
        if (s.size() != 7)
            return;
        const QColor parsed = QColor::fromString(s);
        if (parsed.isValid())
            setColorInternal(parsed, false, true);
    });
    connect(m_hexEdit, &QLineEdit::editingFinished, this, [this] {
        m_hexEdit->setText(m_color.name(QColor::HexRgb).toUpper());
    });

    m_anim = new QPropertyAnimation(this, "progress", this);
    connect(m_anim, &QPropertyAnimation::finished, this, [this] {
        if (m_hexOpacity)
            m_hexOpacity->setEnabled(false);
        if (!m_closing)
            return;
        m_closing = false;
        m_backdrop = QImage();
        hide();
        emit closed();
    });

    updateHexEditStyle();
    hide();
}

void BoardColorPopover::setProgress(qreal p) {
    m_progress = std::clamp(p, 0.0, 1.0);
    if (m_hexOpacity) {
        if (m_progress < 0.999) {
            if (!m_hexOpacity->isEnabled())
                m_hexOpacity->setEnabled(true);
            m_hexOpacity->setOpacity(m_progress);
        } else {
            m_hexOpacity->setOpacity(1.0);
            m_hexOpacity->setEnabled(false);
        }
    }
    update();
}

void BoardColorPopover::setDark(bool dark) {
    m_dark = dark;
    updateHexEditStyle();
    update();
}

void BoardColorPopover::updateHexEditStyle() {
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    m_hexEdit->setFont(popFont(12.5, QFont::Medium));
    m_hexEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; color: %1; padding: 0;"
        " selection-background-color: %2; selection-color: %3; }")
        .arg(tk.text.name(), tk.accent.name(), tk.accentFg.name()));
}

void BoardColorPopover::openAnchoredTo(const QRect& cardRectInParent, const QColor& current, bool dark) {
    if (!parentWidget())
        return;
    m_anim->stop();
    m_closing = false;
    m_drag = DragMode::None;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    if (isVisible())
        hide();

    m_dark = dark;
    updateHexEditStyle();
    setColorInternal(current.isValid() ? current : QColor("#1A1B22"), true, false);

    const int parentW = parentWidget()->width();
    const int parentH = parentWidget()->height();
    int x = cardRectInParent.left() - 14 - kCardW;
    if (x < 12) {
        if (cardRectInParent.right() + 14 + kCardW <= parentW - 12)
            x = cardRectInParent.right() + 14;
        else
            x = std::clamp(cardRectInParent.left() + 16, 12, std::max(12, parentW - kCardW - 12));
    }
    const int y = std::clamp(cardRectInParent.top() + 22, 12, std::max(12, parentH - kCardH - 12));
    move(x - kMargin, y - kMargin);

    if (parentWidget()->isVisible() && !tokens::reducedMotion()) {
        m_grabbingBackdrop = true;
        m_backdrop = blurBackdropImage(parentWidget()->grab(QRect(x, y, kCardW, kCardH)).toImage(), 11);
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

void BoardColorPopover::closePopover() {
    if (!isVisible() || m_closing)
        return;
    m_closing = true;
    m_drag = DragMode::None;
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_anim->stop();
    m_anim->setDuration(tokens::motion::duration(tokens::motion::Fast));
    m_anim->setEasingCurve(tokens::motion::curve(tokens::motion::in()));
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

QRectF BoardColorPopover::closeRect() const {
    return QRectF(m_cardRect.right() - kPad - 24, m_cardRect.top() + kPad, 24, 24);
}

QRectF BoardColorPopover::swatchRect(int index) const {
    const int col = index % 6;
    const int row = index / 6;
    const qreal innerW = kCardW - 2 * kPad;
    const qreal gap = (innerW - 6.0 * kSwatchSize) / 5.0;
    const qreal x = m_cardRect.left() + kPad + col * (kSwatchSize + gap);
    const qreal y = m_cardRect.top() + kPad + 24 + 12 + row * (kSwatchSize + kSwatchRowGap);
    return QRectF(x, y, kSwatchSize, kSwatchSize);
}

QRectF BoardColorPopover::svRect() const {
    const qreal y = m_cardRect.top() + kPad + 24 + 12 + (2 * kSwatchSize + kSwatchRowGap) + 12;
    return QRectF(m_cardRect.left() + kPad, y, kCardW - 2 * kPad, 110.0);
}

QRectF BoardColorPopover::hueRect() const {
    return QRectF(m_cardRect.left() + kPad, svRect().bottom() + 12.0, kCardW - 2 * kPad, 10.0);
}

QRectF BoardColorPopover::hexFieldRect() const {
    return QRectF(m_cardRect.left() + kPad, hueRect().bottom() + 12.0, kCardW - 2 * kPad, 34.0);
}

void BoardColorPopover::setColorInternal(const QColor& c, bool syncHex, bool emitChange) {
    m_color = c.toRgb();
    const qreal h = m_color.hsvHueF();
    if (h >= 0.0)
        m_hue = std::clamp(h, 0.0, 0.9999);
    m_sat = std::clamp(double(m_color.hsvSaturationF()), 0.0, 1.0);
    m_val = std::clamp(double(m_color.valueF()), 0.0, 1.0);
    if (syncHex && m_hexEdit)
        m_hexEdit->setText(m_color.name(QColor::HexRgb).toUpper());
    update();
    if (emitChange)
        emit colorChanged(m_color);
}

void BoardColorPopover::updateFromSv(const QPointF& pos) {
    const QRectF r = svRect();
    m_sat = std::clamp((pos.x() - r.left()) / r.width(), 0.0, 1.0);
    m_val = 1.0 - std::clamp((pos.y() - r.top()) / r.height(), 0.0, 1.0);
    m_color = QColor::fromHsvF(m_hue, m_sat, m_val).toRgb();
    m_hexEdit->setText(m_color.name(QColor::HexRgb).toUpper());
    update();
    emit colorChanged(m_color);
}

void BoardColorPopover::updateFromHue(const QPointF& pos) {
    const QRectF r = hueRect();
    m_hue = std::clamp((pos.x() - r.left()) / r.width(), 0.0, 0.9999);
    m_color = QColor::fromHsvF(m_hue, m_sat, m_val).toRgb();
    m_hexEdit->setText(m_color.name(QColor::HexRgb).toUpper());
    update();
    emit colorChanged(m_color);
}

void BoardColorPopover::paintEvent(QPaintEvent*) {
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

    const tokens::Palette tk = tokens::paletteFor(m_dark);
    paintLiquidGlassPanel(p, QRectF(m_cardRect), tokens::radius::Popover, m_backdrop, homePaletteFor(m_dark), m_dark);

    // Cabecera
    p.setFont(popFont(12.5, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(QRectF(m_cardRect.left() + kPad, m_cardRect.top() + kPad, kCardW - 2 * kPad - 30, 24),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color de fondo"));

    const QRectF cr = closeRect();
    p.setPen(Qt::NoPen);
    p.setBrush(m_hoverClose ? tk.hover : tk.field);
    p.drawEllipse(cr);
    paintUiIcon(&p, cr.adjusted(5.5, 5.5, -5.5, -5.5), QStringLiteral("x"), tk.icon);

    // Rejilla 2×6
    const auto& swatches = curatedSwatches();
    const QRgb curRgb = m_color.rgb();
    for (int i = 0; i < int(swatches.size()); ++i) {
        const QRectF r = swatchRect(i);
        p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
        p.setBrush(swatches[i]);
        p.drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
        if (swatches[i].rgb() == curRgb) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(tk.surface, 2.0));
            p.drawEllipse(r.adjusted(-1.2, -1.2, 1.2, 1.2));
            p.setPen(QPen(tk.accent, 1.5));
            p.drawEllipse(r.adjusted(-3.2, -3.2, 3.2, 3.2));
        }
    }

    // Cuadrado SV
    const QRectF sv = svRect();
    QPainterPath svClip;
    svClip.addRoundedRect(sv, 10, 10);
    p.save();
    p.setClipPath(svClip);
    QLinearGradient satGrad(sv.topLeft(), sv.topRight());
    satGrad.setColorAt(0.0, Qt::white);
    satGrad.setColorAt(1.0, QColor::fromHsvF(m_hue, 1.0, 1.0));
    p.fillRect(sv, satGrad);
    QLinearGradient valGrad(sv.topLeft(), sv.bottomLeft());
    valGrad.setColorAt(0.0, QColor(0, 0, 0, 0));
    valGrad.setColorAt(1.0, QColor(0, 0, 0, 255));
    p.fillRect(sv, valGrad);
    p.restore();
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 28) : QColor(15, 23, 42, 20), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(sv.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QPointF svThumb(sv.left() + m_sat * sv.width(), sv.top() + (1.0 - m_val) * sv.height());
    p.setPen(QPen(QColor(0, 0, 0, 76), 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(svThumb, 6.0, 6.0);
    p.setPen(QPen(Qt::white, 2.0));
    p.drawEllipse(svThumb, 5.5, 5.5);

    // Barra Hue arcoíris
    const QRectF hr = hueRect();
    QLinearGradient hueGrad(hr.left(), hr.center().y(), hr.right(), hr.center().y());
    hueGrad.setColorAt(0.0 / 6.0, QColor(255, 0, 0));
    hueGrad.setColorAt(1.0 / 6.0, QColor(255, 255, 0));
    hueGrad.setColorAt(2.0 / 6.0, QColor(0, 255, 0));
    hueGrad.setColorAt(3.0 / 6.0, QColor(0, 255, 255));
    hueGrad.setColorAt(4.0 / 6.0, QColor(0, 0, 255));
    hueGrad.setColorAt(5.0 / 6.0, QColor(255, 0, 255));
    hueGrad.setColorAt(1.0, QColor(255, 0, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(hueGrad);
    p.drawRoundedRect(hr, 5.0, 5.0);

    const QPointF hueThumb(hr.left() + m_hue * hr.width(), hr.center().y());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 88));
    p.drawEllipse(hueThumb + QPointF(0.0, 1.0), 7.2, 7.2);
    p.setBrush(Qt::white);
    p.drawEllipse(hueThumb, 6.8, 6.8);

    // Campo #HEX
    const QRectF fr = hexFieldRect();
    const bool focused = m_hexEdit && m_hexEdit->hasFocus();
    p.setPen(QPen(focused ? tk.accent : tk.border, focused ? 1.5 : 1.0));
    p.setBrush(focused ? tk.surface : tk.field);
    p.drawRoundedRect(fr.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QRectF dot(fr.left() + 10.0, fr.center().y() - 8.0, 16.0, 16.0);
    p.setPen(QPen(m_dark ? QColor(255, 255, 255, 41) : QColor(15, 23, 42, 31), 1.0));
    p.setBrush(m_color);
    p.drawEllipse(dot);
}

void BoardColorPopover::mousePressEvent(QMouseEvent* e) {
    if (m_closing || e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    if (closeRect().contains(pos)) {
        closePopover();
        return;
    }
    const auto& swatches = curatedSwatches();
    for (int i = 0; i < int(swatches.size()); ++i) {
        if (swatchRect(i).adjusted(-3, -3, 3, 3).contains(pos)) {
            setColorInternal(swatches[i], true, true);
            return;
        }
    }
    if (svRect().adjusted(-4, -4, 4, 4).contains(pos)) {
        m_drag = DragMode::SvSquare;
        updateFromSv(pos);
        return;
    }
    if (hueRect().adjusted(-6, -6, 6, 6).contains(pos)) {
        m_drag = DragMode::HueBar;
        updateFromHue(pos);
        return;
    }
}

void BoardColorPopover::mouseMoveEvent(QMouseEvent* e) {
    if (m_closing)
        return;
    const QPointF pos = e->position();
    if (m_drag == DragMode::SvSquare) {
        updateFromSv(pos);
        return;
    }
    if (m_drag == DragMode::HueBar) {
        updateFromHue(pos);
        return;
    }
    const bool hc = closeRect().contains(pos);
    if (hc != m_hoverClose) {
        m_hoverClose = hc;
        update();
    }
    const bool overInteractive = hc || svRect().contains(pos) || hueRect().adjusted(-4, -5, 4, 5).contains(pos);
    setCursor(overInteractive ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void BoardColorPopover::mouseReleaseEvent(QMouseEvent*) {
    m_drag = DragMode::None;
}

void BoardColorPopover::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        closePopover();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool BoardColorPopover::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_hexEdit && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        update();
    if (watched == m_hexEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            closePopover();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace finily
