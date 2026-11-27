#include "ui/home/NotebookCards.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QToolButton>

#include <algorithm>

namespace finily {
namespace {

constexpr int kCoverAreaTop = 10;
constexpr int kCoverAreaHeight = 194;
const QRectF kCoverArea(10.0, kCoverAreaTop, NotebookCard::kCardWidth - 20.0, kCoverAreaHeight);

QRectF aspectFittedRect(const QRectF& slot, double ratioWOverH) {
    if (slot.isEmpty() || ratioWOverH <= 0.0)
        return slot;
    double w = slot.width();
    double h = w / ratioWOverH;
    if (h > slot.height()) {
        h = slot.height();
        w = h * ratioWOverH;
    }
    return QRectF(slot.center().x() - w / 2.0, slot.center().y() - h / 2.0, w, h);
}

double formatAspectRatio(PageFormat fmt) {
    switch (fmt) {
        case PageFormat::A4Landscape:
            return 1123.0 / 794.0;
        case PageFormat::Slide16x9:
            return 16.0 / 9.0;
        case PageFormat::A4:
        case PageFormat::Infinite:
            return 794.0 / 1123.0;
    }
    return 794.0 / 1123.0;
}

} // namespace

NotebookCard::NotebookCard(const NotebookMeta& meta, QWidget* parent)
    : QWidget(parent)
    , m_meta(meta)
    , m_cover(std::clamp(meta.coverColorIndex, 0, int(coverColors().size()) - 1)) {
    setObjectName(QStringLiteral("finilyCard"));
    setFixedSize(kCardWidth, kCardHeight);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    m_moreBtn = new QToolButton(this);
    m_moreBtn->setObjectName(QStringLiteral("finilyCardAction"));
    m_moreBtn->setToolTip(QStringLiteral("Opciones"));
    m_moreBtn->setFixedSize(28, 28);
    m_moreBtn->setIconSize(QSize(18, 18));
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFocusPolicy(Qt::NoFocus);
    m_moreBtn->move(int(kCoverArea.right()) - 34, int(kCoverArea.top()) + 8);
    m_moreBtn->hide();
    connect(m_moreBtn, &QToolButton::clicked, this,
            [this] { showMenu(m_moreBtn->mapToGlobal(QPoint(0, 30))); });

    m_anim = new QPropertyAnimation(this, "lift", this);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void NotebookCard::setLift(qreal v) {
    m_lift = v;
    update();
}

void NotebookCard::setPalette(const HomePalette& pal, bool dark) {
    m_pal = pal;
    m_dark = dark;
    if (m_moreBtn)
        m_moreBtn->setIcon(homeGlyph(QStringLiteral("more"), pal.text));
    update();
}

void NotebookCard::setAvailableFolders(const QVector<FolderMeta>& folders) {
    m_folders = folders;
}

bool NotebookCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.title.contains(needle, Qt::CaseInsensitive);
}

bool NotebookCard::matchesFilter(int filter) const {
    if (filter == 1)
        return m_meta.modified.daysTo(QDateTime::currentDateTime()) <= 7;
    return true;
}

void NotebookCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = kCoverArea.translated(0.0, -m_lift);
    paintSoftShadow(painter, frame, 12.0, m_lift, m_dark);

    if (m_meta.pageFormat == PageFormat::Infinite && !m_meta.hasPdf)
        paintInfiniteBoard(painter, frame);
    else
        paintNotebookCover(painter, frame);

    if (m_hovered) {
        painter.setPen(QPen(m_pal.accent, 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame.adjusted(-2.5, -2.5, 2.5, 2.5), 13.5, 13.5);
    }

    paintFooter(painter);
}

void NotebookCard::paintInfiniteBoard(QPainter& painter, const QRectF& frame) const {
    const ThemeConfig tc = getThemeConfig(m_meta.theme);
    QPainterPath clip;
    clip.addRoundedRect(frame, 12.0, 12.0);

    painter.save();
    painter.setClipPath(clip);
    painter.fillRect(frame, tc.background);
    if (!m_meta.preview.isNull()) {
        const QSizeF imgSize = m_meta.preview.deviceIndependentSize();
        const QRectF target = aspectFittedRect(frame.adjusted(6.0, 6.0, -6.0, -6.0),
                                               imgSize.width() / std::max(1.0, imgSize.height()));
        painter.drawImage(target, m_meta.preview);
    } else {
        paintMiniTemplate(painter, frame, tc, m_meta.tpl, m_meta.templateScale);
    }
    painter.restore();

    painter.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 34) : QColor(15, 23, 42, 26), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, 12.0, 12.0);

    const QRectF badge(frame.left() + 8.0, frame.top() + 8.0, 22.0, 17.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tc.isDark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 16));
    painter.drawRoundedRect(badge, 5.0, 5.0);
    QFont bf = painter.font();
    bf.setPixelSize(11);
    bf.setWeight(QFont::DemiBold);
    painter.setFont(bf);
    painter.setPen(tc.isDark ? QColor(255, 255, 255, 195) : QColor(15, 23, 42, 165));
    painter.drawText(badge, Qt::AlignCenter, QStringLiteral("∞"));
}

void NotebookCard::paintNotebookCover(QPainter& painter, const QRectF& frame) const {
    const QRectF cover = frame.adjusted(0.0, 0.0, -3.0, -2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#e7e5df"));
    painter.drawRoundedRect(cover.adjusted(5.0, 3.0, 3.0, 2.0), 10.0, 10.0);
    painter.setBrush(QColor("#faf9f5"));
    painter.drawRoundedRect(cover.adjusted(4.0, 1.5, 1.5, 0.8), 10.5, 10.5);

    QColor base = coverColors().at(m_cover);
    if (m_dark && m_cover != coverColors().size() - 1)
        base = base.darker(115);

    QPainterPath shape;
    const qreal rl = 4.0, rr = 11.0;
    shape.moveTo(cover.left() + rl, cover.top());
    shape.lineTo(cover.right() - rr, cover.top());
    shape.quadTo(cover.right(), cover.top(), cover.right(), cover.top() + rr);
    shape.lineTo(cover.right(), cover.bottom() - rr);
    shape.quadTo(cover.right(), cover.bottom(), cover.right() - rr, cover.bottom());
    shape.lineTo(cover.left() + rl, cover.bottom());
    shape.quadTo(cover.left(), cover.bottom(), cover.left(), cover.bottom() - rl);
    shape.lineTo(cover.left(), cover.top() + rl);
    shape.quadTo(cover.left(), cover.top(), cover.left() + rl, cover.top());

    QLinearGradient fill(cover.topLeft(), cover.bottomRight());
    fill.setColorAt(0.0, base.lighter(106));
    fill.setColorAt(1.0, base.darker(106));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(shape);

    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRectF(cover.left(), cover.top(), 10.0, cover.height()), QColor(0, 0, 0, 28));
    painter.fillRect(QRectF(cover.left() + 10.0, cover.top(), 1.0, cover.height()),
                     QColor(255, 255, 255, 65));
    QLinearGradient sheen(cover.topLeft(), QPointF(cover.left(), cover.top() + 56.0));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 38));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(cover, sheen);
    painter.restore();

    const ThemeConfig tc = getThemeConfig(m_meta.theme);
    const QRectF slot = cover.adjusted(17.0, 12.0, -9.0, -14.0);
    if (!m_meta.preview.isNull()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 26));
        painter.drawRoundedRect(slot.translated(0.0, 1.5), 6.0, 6.0);

        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(slot, 6.0, 6.0);
        painter.setClipPath(clip);
        painter.fillRect(slot, tc.background);
        painter.drawImage(slot, m_meta.preview);
        painter.restore();

        painter.setPen(QPen(QColor(0, 0, 0, 32), 0.9));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(slot, 6.0, 6.0);
    } else {
        const QRectF sheet = aspectFittedRect(slot.adjusted(4.0, 6.0, -4.0, -6.0),
                                              formatAspectRatio(m_meta.pageFormat));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 26));
        painter.drawRoundedRect(sheet.translated(0.0, 1.5), 5.0, 5.0);
        painter.setBrush(tc.background);
        painter.drawRoundedRect(sheet, 5.0, 5.0);
        paintMiniTemplate(painter, sheet, tc, m_meta.tpl, m_meta.templateScale);
        painter.setPen(QPen(tc.isDark ? QColor(255, 255, 255, 34) : QColor(0, 0, 0, 26), 0.9));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(sheet, 5.0, 5.0);
    }

    if (m_meta.coverIconIndex > 0) {
        const QRectF badge(cover.left() + 18.0, cover.bottom() - 34.0, 22.0, 22.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(15, 23, 42, 130));
        painter.drawEllipse(badge);
        symbolGlyph(m_meta.coverIconIndex, QColor(255, 255, 255, 238))
            .paint(&painter, badge.adjusted(3.0, 3.0, -3.0, -3.0).toRect());
    }

    if (m_meta.hasPdf && !m_hovered)
        paintPdfBadge(painter, cover);
}

void NotebookCard::paintPdfBadge(QPainter& painter, const QRectF& area) const {
    QFont tag = painter.font();
    tag.setPixelSize(9);
    tag.setWeight(QFont::Bold);
    painter.setFont(tag);
    const QRectF chip(area.right() - 34.0, area.top() + 8.0, 26.0, 15.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ef4444"));
    painter.drawRoundedRect(chip, 4.0, 4.0);
    painter.setPen(Qt::white);
    painter.drawText(chip, Qt::AlignCenter, QStringLiteral("PDF"));
}

void NotebookCard::paintFooter(QPainter& painter) const {
    const qreal left = 12.0;
    const qreal w = width() - 24.0;

    QFont titleFont = painter.font();
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(m_pal.text);
    const QString elidedTitle =
        QFontMetrics(titleFont).elidedText(m_meta.title, Qt::ElideRight, int(w));
    painter.drawText(QRectF(left, kCoverAreaTop + kCoverAreaHeight + 8.0, w, 20.0),
                     Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

    QFont subFont = painter.font();
    subFont.setPixelSize(11);
    subFont.setWeight(QFont::Normal);
    painter.setFont(subFont);
    painter.setPen(m_pal.muted);
    const QString elidedSub =
        QFontMetrics(subFont).elidedText(summarySubtitle(), Qt::ElideRight, int(w));
    painter.drawText(QRectF(left, kCoverAreaTop + kCoverAreaHeight + 28.0, w, 17.0),
                     Qt::AlignLeft | Qt::AlignVCenter, elidedSub);
}

QString NotebookCard::summarySubtitle() const {
    const QString when = relativeDateLabel(m_meta.modified);
    if (m_meta.hasPdf)
        return QStringLiteral("PDF · %1 pág · %2").arg(m_meta.pages).arg(when);
    switch (m_meta.pageFormat) {
        case PageFormat::Infinite:
            if (m_meta.pages > 1)
                return QStringLiteral("Pizarra · %1 lienzos · %2").arg(m_meta.pages).arg(when);
            return QStringLiteral("Pizarra · %1").arg(when);
        case PageFormat::A4:
            return QStringLiteral("A4 · %1 pág · %2").arg(m_meta.pages).arg(when);
        case PageFormat::A4Landscape:
            return QStringLiteral("A4 ↔ · %1 pág · %2").arg(m_meta.pages).arg(when);
        case PageFormat::Slide16x9:
            return QStringLiteral("16:9 · %1 pág · %2").arg(m_meta.pages).arg(when);
    }
    return when;
}

void NotebookCard::enterEvent(QEnterEvent*) {
    m_hovered = true;
    m_moreBtn->show();
    animateLift(4.0);
}

void NotebookCard::leaveEvent(QEvent*) {
    m_hovered = false;
    m_moreBtn->hide();
    animateLift(0.0);
}

void NotebookCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        const QRectF titleArea(8.0, kCoverAreaTop + kCoverAreaHeight + 6.0, width() - 16.0, 24.0);
        if (titleArea.contains(event->position())) {
            emit renameRequested();
            return;
        }
        emit openRequested(m_meta.path);
    }
    QWidget::mouseReleaseEvent(event);
}

void NotebookCard::contextMenuEvent(QContextMenuEvent* event) {
    showMenu(event->globalPos());
}

void NotebookCard::animateLift(qreal target) {
    m_anim->stop();
    m_anim->setStartValue(m_lift);
    m_anim->setEndValue(target);
    m_anim->start();
}

void NotebookCard::showMenu(const QPoint& at) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
        "QMenu::item { color: %3; padding: 7px 18px; border-radius: 8px; font-size: 13px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
                           .arg(m_pal.surface.name(), m_pal.border.name(QColor::HexArgb),
                                m_pal.text.name(), m_pal.tint.name(QColor::HexArgb)));
    menu.addAction(QStringLiteral("Abrir"), this, [this] { emit openRequested(m_meta.path); });
    menu.addAction(QStringLiteral("Renombrar…"), this, &NotebookCard::renameRequested);

    if (m_meta.pageFormat != PageFormat::Infinite || m_meta.hasPdf) {
        menu.addAction(QStringLiteral("Editar portada…"), this,
                       [this] { emit editCoverRequested(m_meta); });
    }

    if (!m_folders.isEmpty() || !m_meta.folderRelativePath.isEmpty()) {
        QMenu* moveMenu = menu.addMenu(QStringLiteral("Mover a carpeta"));
        moveMenu->setStyleSheet(menu.styleSheet());
        if (!m_meta.folderRelativePath.isEmpty()) {
            moveMenu->addAction(QStringLiteral("Raíz (Sin clasificar)"), this,
                                [this] { emit moveToFolderRequested(m_meta.path, QString()); });
            if (!m_folders.isEmpty())
                moveMenu->addSeparator();
        }
        for (const FolderMeta& f : m_folders) {
            if (f.relativePath == m_meta.folderRelativePath)
                continue;
            const QString prefix = QString(f.depth * 2, QLatin1Char(' '));
            const QColor col = coverColors().at(std::clamp(f.colorIndex, 0, int(coverColors().size()) - 1));
            moveMenu->addAction(homeGlyph(QStringLiteral("folder"), col), prefix + f.name, this,
                                [this, rel = f.relativePath] {
                                    emit moveToFolderRequested(m_meta.path, rel);
                                });
        }
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("Eliminar"), this, &NotebookCard::deleteRequested);
    menu.exec(at);
}

} // namespace finily
