#include "ui/home/HomeMotion.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/HomeTheme.h"

#include <QAbstractAnimation>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace finily {
namespace {

void initOverlayWidget(QWidget* widget) {
    widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    widget->setAttribute(Qt::WA_StyledBackground, false);
    widget->setAutoFillBackground(false);
    widget->setFocusPolicy(Qt::NoFocus);
}

template <typename T>
void discardPreviousOverlays(QWidget* host) {
    for (T* prev : host->findChildren<T*>(QString(), Qt::FindDirectChildrenOnly)) {
        prev->hide();
        prev->deleteLater();
    }
}

QFont makeUiFont(const QFont& base, qreal px, int weight) {
    QFont f = base;
    f.setPixelSize(int(px + 0.5));
    f.setWeight(QFont::Weight(weight));
    return f;
}

QSizeF logicalPixmapSize(const QPixmap& pixmap) {
    const qreal dpr = std::max<qreal>(1.0, pixmap.devicePixelRatio());
    return QSizeF(pixmap.width() / dpr, pixmap.height() / dpr);
}

qreal maxCornerDistance(const QRectF& bounds, const QPointF& origin) {
    const qreal d1 = std::hypot(origin.x() - bounds.left(), origin.y() - bounds.top());
    const qreal d2 = std::hypot(bounds.right() - origin.x(), origin.y() - bounds.top());
    const qreal d3 = std::hypot(origin.x() - bounds.left(), bounds.bottom() - origin.y());
    const qreal d4 = std::hypot(bounds.right() - origin.x(), bounds.bottom() - origin.y());
    return std::max({d1, d2, d3, d4});
}

qreal piecewiseAtMid(qreal u, qreal v0, qreal vMid, qreal v1, qreal mid = 0.45) {
    if (u <= mid)
        return v0 + (vMid - v0) * (u / mid);
    return vMid + (v1 - vMid) * ((u - mid) / (1.0 - mid));
}

QRectF clampRectInside(QRectF r, const QRectF& bounds) {
    if (r.right() > bounds.right())
        r.moveRight(bounds.right());
    if (r.left() < bounds.left())
        r.moveLeft(bounds.left());
    if (r.bottom() > bounds.bottom())
        r.moveBottom(bounds.bottom());
    if (r.top() < bounds.top())
        r.moveTop(bounds.top());
    return r;
}

} // namespace

// ======================================================= ThemeRevealOverlay

void ThemeRevealOverlay::play(QWidget* host, const QPixmap& oldSnapshot, const QPoint& originInHost) {
    if (!host || oldSnapshot.isNull())
        return;
    discardPreviousOverlays<ThemeRevealOverlay>(host);
    new ThemeRevealOverlay(host, oldSnapshot, originInHost);
}

ThemeRevealOverlay::ThemeRevealOverlay(QWidget* host, const QPixmap& oldSnapshot, const QPoint& originInHost)
    : QWidget(host)
    , m_oldSnapshot(oldSnapshot)
    , m_origin(originInHost)
    , m_reduced(tokens::reducedMotion()) {
    initOverlayWidget(this);
    setGeometry(host->rect());
    raise();
    show();

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(tokens::motion::duration(tokens::motion::Theme));
    anim->setEasingCurve(tokens::motion::curve(tokens::motion::out()));
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_progress = std::clamp(value.toReal(), 0.0, 1.0);
        update();
    });
    connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ThemeRevealOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_reduced) {
        p.setOpacity(1.0 - m_progress);
        p.drawPixmap(QPoint(0, 0), m_oldSnapshot);
        return;
    }

    const QRectF bounds(rect());
    const qreal r = m_progress * maxCornerDistance(bounds, QPointF(m_origin));
    QPainterPath mask;
    mask.setFillRule(Qt::OddEvenFill);
    mask.addRect(bounds);
    if (r > 0.5)
        mask.addEllipse(QPointF(m_origin), r, r);
    p.setClipPath(mask);
    p.drawPixmap(QPoint(0, 0), m_oldSnapshot);
}

// ======================================================== FolderZoomOverlay

void FolderZoomOverlay::play(QWidget* host, const QPixmap& oldGrid, const QPixmap& newGrid,
                             const QRect& gridRectInHost, const QPointF& pivotInGrid,
                             const QColor& bgColor, bool entering) {
    if (!host || gridRectInHost.isEmpty() || (oldGrid.isNull() && newGrid.isNull()))
        return;
    discardPreviousOverlays<FolderZoomOverlay>(host);
    new FolderZoomOverlay(host, oldGrid, newGrid, gridRectInHost, pivotInGrid, bgColor, entering);
}

FolderZoomOverlay::FolderZoomOverlay(QWidget* host, const QPixmap& oldGrid, const QPixmap& newGrid,
                                     const QRect& gridRectInHost, const QPointF& pivotInGrid,
                                     const QColor& bgColor, bool entering)
    : QWidget(host)
    , m_oldGrid(oldGrid)
    , m_newGrid(newGrid)
    , m_pivot(pivotInGrid)
    , m_bgColor(bgColor)
    , m_entering(entering)
    , m_reduced(tokens::reducedMotion())
    , m_inCurve(tokens::motion::in())
    , m_outCurve(tokens::motion::out()) {
    initOverlayWidget(this);
    setGeometry(gridRectInHost);
    raise();
    show();

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(m_reduced ? tokens::motion::Reduced : 410);
    anim->setEasingCurve(QEasingCurve(QEasingCurve::Linear));
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_progress = std::clamp(value.toReal(), 0.0, 1.0);
        update();
    });
    connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FolderZoomOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), m_bgColor);

    auto drawScaled = [&](const QPixmap& pix, qreal opacity, qreal scale) {
        if (pix.isNull() || opacity <= 0.001)
            return;
        p.save();
        p.setOpacity(opacity);
        p.translate(m_pivot);
        p.scale(scale, scale);
        p.translate(-m_pivot);
        p.drawPixmap(QRect(QPoint(0, 0), size()), pix);
        p.restore();
    };

    if (m_reduced) {
        drawScaled(m_oldGrid, 1.0 - m_progress, 1.0);
        drawScaled(m_newGrid, m_progress, 1.0);
        return;
    }

    const qreal elapsedMs = m_progress * 410.0;
    const qreal uOld = std::clamp(elapsedMs / 200.0, 0.0, 1.0);
    const qreal eOld = m_inCurve.valueForProgress(uOld);
    const qreal oldEndScale = m_entering ? 1.08 : 0.92;
    const qreal oldScale = 1.0 + (oldEndScale - 1.0) * eOld;
    drawScaled(m_oldGrid, 1.0 - eOld, oldScale);

    const qreal uNew = std::clamp((elapsedMs - 90.0) / 320.0, 0.0, 1.0);
    const qreal eNew = m_outCurve.valueForProgress(uNew);
    const qreal newStartScale = m_entering ? 0.92 : 1.08;
    const qreal newScale = newStartScale + (1.0 - newStartScale) * eNew;
    drawScaled(m_newGrid, eNew, newScale);
}

// ========================================================== TrashFlyOverlay

void TrashFlyOverlay::play(QWidget* host, const QPixmap& visualSnap, const QRectF& startRectInHost,
                           const QPointF& targetCenterInHost, bool reverse,
                           std::function<void()> onArrived) {
    if (tokens::reducedMotion() || !host || visualSnap.isNull()) {
        if (onArrived)
            onArrived();
        return;
    }
    new TrashFlyOverlay(host, visualSnap, startRectInHost, targetCenterInHost, reverse, std::move(onArrived));
}

TrashFlyOverlay::TrashFlyOverlay(QWidget* host, const QPixmap& visualSnap, const QRectF& startRectInHost,
                                 const QPointF& targetCenterInHost, bool reverse,
                                 std::function<void()> onArrived)
    : QWidget(host)
    , m_snap(visualSnap)
    , m_startRect(startRectInHost)
    , m_targetCenter(targetCenterInHost)
    , m_reverse(reverse)
    , m_onArrived(std::move(onArrived)) {
    initOverlayWidget(this);
    setGeometry(host->rect());
    raise();
    show();

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(460);
    anim->setEasingCurve(tokens::motion::bezier(0.5, 0.0, 0.3, 1.0));
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_t = std::clamp(value.toReal(), 0.0, 1.0);
        update();
    });
    connect(anim, &QVariantAnimation::finished, this, [this]() {
        hide();
        if (m_onArrived)
            m_onArrived();
        deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TrashFlyOverlay::paintEvent(QPaintEvent*) {
    if (m_snap.isNull())
        return;
    const qreal u = m_reverse ? (1.0 - m_t) : m_t;
    constexpr qreal kMid = 0.45;
    const qreal arcY = -60.0 * (u * (1.0 - u)) / (kMid * (1.0 - kMid));
    const QPointF startCenter = m_startRect.center();
    const QPointF center = startCenter + (m_targetCenter - startCenter) * u + QPointF(0.0, arcY);
    const qreal scale = piecewiseAtMid(u, 1.0, 0.55, 0.12, kMid);
    const qreal angleDeg = piecewiseAtMid(u, 0.0, -8.0, -14.0, kMid);
    const qreal opacity = piecewiseAtMid(u, 1.0, 1.0, 0.2, kMid);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(opacity);
    p.translate(center);
    p.rotate(angleDeg);
    p.scale(scale, scale);
    const QRectF localRect(-m_startRect.width() / 2.0, -m_startRect.height() / 2.0,
                           m_startRect.width(), m_startRect.height());
    p.drawPixmap(localRect, m_snap, QRectF(m_snap.rect()));
}

// ========================================================= animateGridFlip

void animateGridFlip(const QHash<QString, QPoint>& beforePos,
                     const QVector<QPair<QString, QWidget*>>& afterWidgets,
                     int staggerMs) {
    if (tokens::reducedMotion())
        return;

    int movedIdx = 0;
    for (const auto& entry : afterWidgets) {
        const QString& key = entry.first;
        QWidget* w = entry.second;
        if (!w)
            continue;

        if (auto* prev = w->findChild<QAbstractAnimation*>(
                QStringLiteral("finilyFlipAnim"), Qt::FindDirectChildrenOnly)) {
            prev->stop();
            prev->setObjectName(QString());
            prev->deleteLater();
        }

        const auto it = beforePos.constFind(key);
        if (it == beforePos.constEnd())
            continue;
        const QPoint oldPos = *it;
        const QPoint newPos = w->pos();
        if (oldPos == newPos)
            continue;

        w->move(oldPos);
        auto* group = new QSequentialAnimationGroup(w);
        group->setObjectName(QStringLiteral("finilyFlipAnim"));
        if (staggerMs > 0 && movedIdx > 0)
            group->addPause(staggerMs * movedIdx);

        auto* anim = new QPropertyAnimation(w, "pos", group);
        anim->setDuration(tokens::motion::Slow);
        anim->setEasingCurve(tokens::motion::out());
        anim->setStartValue(oldPos);
        anim->setEndValue(newPos);
        group->addAnimation(anim);
        group->start(QAbstractAnimation::DeleteWhenStopped);
        ++movedIdx;
    }
}

// ========================================================== DropZoneOverlay

DropZoneOverlay::DropZoneOverlay(QWidget* parent)
    : QWidget(parent) {
    initOverlayWidget(this);
    hide();
}

void DropZoneOverlay::setDark(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    update();
}

void DropZoneOverlay::setTargetName(const QString& targetName) {
    if (m_targetName == targetName)
        return;
    m_targetName = targetName;
    update();
}

void DropZoneOverlay::paintEvent(QPaintEvent*) {
    const QRectF panel = QRectF(rect()).adjusted(14.0, 14.0, -14.0, -14.0);
    if (panel.width() < 40.0 || panel.height() < 40.0)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);

    QColor fill = tk.sidebar;
    fill.setAlpha(m_dark ? 225 : 220);
    const QColor dashColor = m_dark ? QColor(244, 244, 245, 90) : QColor(24, 24, 27, 90);
    QPen dashPen(dashColor, 1.5, Qt::CustomDashLine, Qt::RoundCap);
    dashPen.setDashPattern({5.0, 4.0});
    p.setPen(dashPen);
    p.setBrush(fill);
    p.drawRoundedRect(panel.adjusted(0.75, 0.75, -0.75, -0.75), 16.0, 16.0);

    constexpr qreal kCardSize = 54.0;
    constexpr qreal kTotalHeight = 108.0;
    const qreal topY = panel.center().y() - kTotalHeight / 2.0;
    const QRectF iconCard(panel.center().x() - kCardSize / 2.0, topY, kCardSize, kCardSize);

    paintSoftShadow(p, iconCard, 16.0, 4.0, m_dark);
    p.setPen(QPen(tk.borderPop, 1.0));
    p.setBrush(tk.surface);
    p.drawRoundedRect(iconCard.adjusted(0.5, 0.5, -0.5, -0.5), 16.0, 16.0);

    const QRectF iconRect(iconCard.center().x() - 11.0, iconCard.center().y() - 11.0, 22.0, 22.0);
    paintUiIcon(&p, iconRect, QStringLiteral("download"), tk.text);

    p.setFont(makeUiFont(font(), 15.0, QFont::DemiBold));
    p.setPen(tk.text);
    p.drawText(QRectF(panel.left() + 24.0, iconCard.bottom() + 12.0, panel.width() - 48.0, 22.0),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Suelta para importar"));

    const QString destination = m_targetName.isEmpty() ? QStringLiteral("Documentos") : m_targetName;
    p.setFont(makeUiFont(font(), 12.0, QFont::Normal));
    p.setPen(tk.muted);
    p.drawText(QRectF(panel.left() + 24.0, iconCard.bottom() + 36.0, panel.width() - 48.0, 20.0),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("PDF, PNG o JPG · se guardarán en %1").arg(destination));
}

// ========================================================= DragGhostOverlay

DragGhostOverlay::DragGhostOverlay(QWidget* parent)
    : QWidget(parent) {
    initOverlayWidget(this);
    if (parent)
        setGeometry(parent->rect());
    hide();
}

void DragGhostOverlay::updateDrag(const QPixmap& snap, const QPoint& posInHost,
                                  const QString& targetLabel, int count, bool dark) {
    m_snap = snap;
    m_pos = posInHost;
    m_targetLabel = targetLabel;
    m_count = count;
    m_dark = dark;
    if (parentWidget() && geometry() != parentWidget()->rect())
        setGeometry(parentWidget()->rect());
    if (m_snap.isNull()) {
        hide();
        return;
    }
    if (!isVisible()) {
        raise();
        show();
    }
    update();
}

void DragGhostOverlay::clear() {
    m_snap = QPixmap();
    m_targetLabel.clear();
    m_count = 0;
    hide();
}

void DragGhostOverlay::paintEvent(QPaintEvent*) {
    if (m_snap.isNull())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);

    const QSizeF sz = logicalPixmapSize(m_snap);
    const QRectF localGhost(-sz.width() / 2.0, -sz.height() / 2.0, sz.width(), sz.height());

    p.save();
    p.setOpacity(0.96);
    p.translate(m_pos);
    p.rotate(-6.0);
    paintSoftShadow(p, localGhost, 8.0, 6.0, m_dark);
    p.drawPixmap(localGhost, m_snap, QRectF(m_snap.rect()));
    p.restore();

    const QRectF safeBounds = QRectF(rect()).adjusted(8.0, 8.0, -8.0, -8.0);

    if (m_count > 1) {
        const QString badgeText = QString::number(m_count);
        const QFont badgeFont = makeUiFont(font(), 11.5, QFont::Bold);
        const qreal badgeW = std::max(22.0, QFontMetricsF(badgeFont).horizontalAdvance(badgeText) + 12.0);
        const QPointF badgeCenter(m_pos.x() + sz.width() * 0.34, m_pos.y() - sz.height() * 0.38);
        const QRectF badgeRect = clampRectInside(
            QRectF(badgeCenter.x() - badgeW / 2.0, badgeCenter.y() - 11.0, badgeW, 22.0), safeBounds);

        paintSoftShadow(p, badgeRect, 11.0, 2.5, m_dark);
        p.setPen(QPen(tk.surface, 1.5));
        p.setBrush(tk.accent);
        p.drawRoundedRect(badgeRect, 11.0, 11.0);
        p.setFont(badgeFont);
        p.setPen(tk.accentFg);
        p.drawText(badgeRect, Qt::AlignCenter, badgeText);
    }

    if (!m_targetLabel.isEmpty()) {
        const QString text = m_targetLabel.startsWith(QStringLiteral("Mover "))
            ? m_targetLabel
            : QStringLiteral("Mover a %1").arg(m_targetLabel);
        const QFont pillFont = makeUiFont(font(), 11.5, QFont::DemiBold);
        const qreal textW = QFontMetricsF(pillFont).horizontalAdvance(text);
        const qreal pillW = 10.0 + 12.0 + 5.0 + textW + 10.0;
        const QRectF pillRect = clampRectInside(
            QRectF(m_pos.x() + 14.0, m_pos.y() + 18.0, pillW, 24.0), safeBounds);

        paintSoftShadow(p, pillRect, 12.0, 3.0, m_dark);
        p.setPen(Qt::NoPen);
        p.setBrush(tk.accent);
        p.drawRoundedRect(pillRect, 12.0, 12.0);

        const QRectF iconRect(pillRect.left() + 10.0, pillRect.center().y() - 6.0, 12.0, 12.0);
        paintUiIcon(&p, iconRect, QStringLiteral("folderMove"), tk.accentFg);

        p.setFont(pillFont);
        p.setPen(tk.accentFg);
        p.drawText(QRectF(iconRect.right() + 5.0, pillRect.top(), textW + 2.0, pillRect.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}

} // namespace finily
