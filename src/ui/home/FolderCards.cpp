#include "ui/home/FolderCards.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QToolButton>

#include <algorithm>

namespace finily {
namespace {

const QRectF kTileFrame(10.0, 10.0, 164.0, 194.0);

} // namespace

CreateTileCard::CreateTileCard(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    m_anim = new QPropertyAnimation(this, "lift", this);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void CreateTileCard::setLift(qreal v) {
    m_lift = v;
    update();
}

void CreateTileCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    update();
}

void CreateTileCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = kTileFrame.translated(0.0, -m_lift);
    paintSoftShadow(painter, frame, 13.0, m_lift, m_dark);

    QColor bg = m_dark ? QColor(34, 37, 48, 220) : QColor(255, 255, 255, 232);
    painter.setBrush(bg);
    QPen pen(m_hovered ? m_pal.accent : m_pal.border, m_hovered ? 1.6 : 1.2, Qt::DashLine);
    painter.setPen(pen);
    painter.drawRoundedRect(frame, 13.0, 13.0);

    const QPointF c = frame.center();
    const qreal r = m_hovered ? 24.0 : 22.0;
    painter.setPen(Qt::NoPen);
    QColor halo = m_pal.accent;
    halo.setAlpha(m_hovered ? 48 : 26);
    painter.setBrush(halo);
    painter.drawEllipse(c, r + 9.0, r + 9.0);
    painter.setBrush(m_pal.accent);
    painter.drawEllipse(c, r, r);

    painter.setPen(QPen(m_pal.accentFg, 2.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(c + QPointF(-7.5, 0.0), c + QPointF(7.5, 0.0));
    painter.drawLine(c + QPointF(0.0, -7.5), c + QPointF(0.0, 7.5));

    QFont tf = painter.font();
    tf.setPixelSize(13);
    tf.setWeight(QFont::DemiBold);
    painter.setFont(tf);
    painter.setPen(m_pal.text);
    painter.drawText(QRectF(12.0, 212.0, width() - 24.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Crear"));

    QFont sf = painter.font();
    sf.setPixelSize(11);
    sf.setWeight(QFont::Normal);
    painter.setFont(sf);
    painter.setPen(m_pal.muted);
    painter.drawText(QRectF(12.0, 232.0, width() - 24.0, 17.0), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Nota, pizarra o carpeta"));
}

void CreateTileCard::enterEvent(QEnterEvent*) {
    m_hovered = true;
    animateLift(4.0);
}

void CreateTileCard::leaveEvent(QEvent*) {
    m_hovered = false;
    animateLift(0.0);
}

void CreateTileCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        emit createClicked();
    QWidget::mouseReleaseEvent(event);
}

void CreateTileCard::animateLift(qreal target) {
    m_anim->stop();
    m_anim->setStartValue(m_lift);
    m_anim->setEndValue(target);
    m_anim->start();
}

FolderCard::FolderCard(const FolderMeta& meta, QWidget* parent)
    : QWidget(parent)
    , m_meta(meta) {
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    m_moreBtn = new QToolButton(this);
    m_moreBtn->setObjectName(QStringLiteral("finilyCardAction"));
    m_moreBtn->setToolTip(QStringLiteral("Opciones de carpeta"));
    m_moreBtn->setFixedSize(28, 28);
    m_moreBtn->setIconSize(QSize(18, 18));
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFocusPolicy(Qt::NoFocus);
    m_moreBtn->move(int(kTileFrame.right()) - 34, int(kTileFrame.top()) + 34);
    m_moreBtn->hide();
    connect(m_moreBtn, &QToolButton::clicked, this,
            [this] { showMenu(m_moreBtn->mapToGlobal(QPoint(0, 30))); });

    m_anim = new QPropertyAnimation(this, "lift", this);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void FolderCard::setLift(qreal v) {
    m_lift = v;
    update();
}

void FolderCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    if (m_moreBtn)
        m_moreBtn->setIcon(homeGlyph(QStringLiteral("more"), pal.text));
    update();
}

bool FolderCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.name.contains(needle, Qt::CaseInsensitive);
}

void FolderCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF folderBounds = kTileFrame.adjusted(4.0, 26.0, -4.0, -14.0).translated(0.0, -m_lift);
    paintSoftShadow(painter, folderBounds, 12.0, m_lift, m_dark);
    const int cidx = std::clamp(m_meta.colorIndex, 0, int(coverColors().size()) - 1);
    paintEditorialFolder(painter, folderBounds, coverColors().at(cidx), m_meta.iconIndex, m_dark);

    if (m_hovered) {
        painter.setPen(QPen(m_pal.accent, 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(folderBounds.adjusted(-3.0, -3.0, 3.0, 3.0), 13.5, 13.5);
    }

    QFont tf = painter.font();
    tf.setPixelSize(13);
    tf.setWeight(QFont::DemiBold);
    painter.setFont(tf);
    painter.setPen(m_pal.text);
    const QString title = QFontMetrics(tf).elidedText(m_meta.name, Qt::ElideRight, width() - 24);
    painter.drawText(QRectF(12.0, 212.0, width() - 24.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter,
                     title);

    QFont sf = painter.font();
    sf.setPixelSize(11);
    sf.setWeight(QFont::Normal);
    painter.setFont(sf);
    painter.setPen(m_pal.muted);
    const QString sub = QFontMetrics(sf).elidedText(summarySubtitle(), Qt::ElideRight, width() - 24);
    painter.drawText(QRectF(12.0, 232.0, width() - 24.0, 17.0), Qt::AlignLeft | Qt::AlignVCenter,
                     sub);
}

QString FolderCard::summarySubtitle() const {
    const int docs = m_meta.documentCount;
    const int subs = m_meta.directSubfolders;
    if (subs > 0 && docs > 0)
        return QStringLiteral("%1 sub · %2 doc").arg(subs).arg(docs);
    if (subs > 0)
        return subs == 1 ? QStringLiteral("1 subcarpeta") : QStringLiteral("%1 subcarpetas").arg(subs);
    if (docs > 0)
        return docs == 1 ? QStringLiteral("1 documento") : QStringLiteral("%1 documentos").arg(docs);
    return QStringLiteral("Carpeta vacía");
}

void FolderCard::enterEvent(QEnterEvent*) {
    m_hovered = true;
    m_moreBtn->show();
    animateLift(4.0);
}

void FolderCard::leaveEvent(QEvent*) {
    m_hovered = false;
    m_moreBtn->hide();
    animateLift(0.0);
}

void FolderCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        const QRectF titleArea(8.0, 210.0, width() - 16.0, 24.0);
        if (titleArea.contains(event->position())) {
            emit renameRequested(m_meta.relativePath);
            return;
        }
        emit openRequested(m_meta.relativePath);
    }
    QWidget::mouseReleaseEvent(event);
}

void FolderCard::contextMenuEvent(QContextMenuEvent* event) {
    showMenu(event->globalPos());
}

void FolderCard::animateLift(qreal target) {
    m_anim->stop();
    m_anim->setStartValue(m_lift);
    m_anim->setEndValue(target);
    m_anim->start();
}

void FolderCard::showMenu(const QPoint& at) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
        "QMenu::item { color: %3; padding: 7px 18px; border-radius: 8px; font-size: 13px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
                           .arg(m_pal.surface.name(), m_pal.border.name(QColor::HexArgb),
                                m_pal.text.name(), m_pal.tint.name(QColor::HexArgb)));
    menu.addAction(QStringLiteral("Abrir"), this,
                   [this] { emit openRequested(m_meta.relativePath); });
    menu.addAction(QStringLiteral("Nueva subcarpeta…"), this,
                   [this] { emit newSubfolderRequested(m_meta.relativePath); });
    menu.addAction(QStringLiteral("Editar carpeta…"), this, [this] { emit editRequested(m_meta); });
    menu.addAction(QStringLiteral("Renombrar…"), this,
                   [this] { emit renameRequested(m_meta.relativePath); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Eliminar"), this,
                   [this] { emit deleteRequested(m_meta.relativePath); });
    menu.exec(at);
}

} // namespace finily
