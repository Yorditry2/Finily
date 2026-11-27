#include "ui/home/ObjectCard.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/LibraryObjects.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

namespace finily {
namespace {
constexpr qreal kVisTop = 10.0;
constexpr qreal kNameTop = kVisTop + libobj::kVisualHeight + 9.0;
} // namespace

ObjectCard::ObjectCard(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_Hover, true);
    setAutoFillBackground(false);
    setMouseTracking(true);

    m_anim = new QPropertyAnimation(this, "hoverProgress", this);
    m_anim->setDuration(tokens::motion::Fast);
    m_anim->setEasingCurve(tokens::motion::out());
}

void ObjectCard::setHoverProgress(qreal v) {
    m_hover = v;
    update();
}

void ObjectCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

QPointF ObjectCard::visualBottomCenter() const {
    return QPointF(width() / 2.0, kVisTop + libobj::kVisualHeight - 2.0 * m_hover);
}

QRectF ObjectCard::moreButtonRect() const {
    return QRectF(width() - 32.0, 8.0, 24.0, 24.0);
}

void ObjectCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_hover > 0.01) {
        QColor bg = m_pal.hover;
        bg.setAlphaF(bg.alphaF() * m_hover);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()), 14, 14);
    }

    paintVisual(painter, visualBottomCenter());

    const qreal w = width() - 20.0;
    QFont nf = font();
    nf.setPixelSize(12);
    nf.setWeight(QFont::Medium);
    painter.setFont(nf);
    painter.setPen(m_pal.text);
    painter.drawText(QRectF(10, kNameTop, w, 17), Qt::AlignHCenter | Qt::AlignVCenter,
                     QFontMetrics(nf).elidedText(nameText(), Qt::ElideRight, int(w)));

    QFont df = font();
    df.setPixelSize(11);
    painter.setFont(df);
    painter.setPen(m_pal.muted);
    painter.drawText(QRectF(10, kNameTop + 18, w, 15), Qt::AlignHCenter | Qt::AlignVCenter,
                     QFontMetrics(df).elidedText(detailText(), Qt::ElideRight, int(w)));

    if (m_hover > 0.5) {
        const QRectF mb = moreButtonRect();
        painter.setOpacity((m_hover - 0.5) * 2.0);
        painter.setPen(QPen(m_pal.border, 1));
        painter.setBrush(m_pal.surface);
        painter.drawEllipse(mb);
        paintUiIcon(&painter, mb.adjusted(5, 5, -5, -5), QStringLiteral("more"), m_pal.icon);
        painter.setOpacity(1.0);
    }
}

void ObjectCard::enterEvent(QEnterEvent*) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void ObjectCard::leaveEvent(QEvent*) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void ObjectCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        if (moreButtonRect().adjusted(-3, -3, 3, 3).contains(event->position()))
            showMenu(mapToGlobal(moreButtonRect().bottomLeft().toPoint() + QPoint(0, 4)));
        else
            activate();
    }
    QWidget::mouseReleaseEvent(event);
}

void ObjectCard::mouseDoubleClickEvent(QMouseEvent* event) {
    // Un clic ya abre; ignorar el doble clic evita abrir dos veces.
    event->accept();
}

void ObjectCard::contextMenuEvent(QContextMenuEvent* event) {
    showMenu(event->globalPos());
}

} // namespace finily
