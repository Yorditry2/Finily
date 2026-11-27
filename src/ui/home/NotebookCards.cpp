#include "ui/home/NotebookCards.h"

#include "ui/DesignTokens.h"
#include "ui/home/LibraryObjects.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace finily {

NotebookCard::NotebookCard(const NotebookMeta& meta, QWidget* parent)
    : ObjectCard(parent)
    , m_meta(meta) {
    setObjectName(QStringLiteral("finilyCard"));
}

void NotebookCard::setMeta(const NotebookMeta& meta) {
    m_meta = meta;
    update();
}

bool NotebookCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.title.contains(needle, Qt::CaseInsensitive);
}

bool NotebookCard::matchesFilter(int filter) const {
    if (filter == 1)
        return m_meta.modified.daysTo(QDateTime::currentDateTime()) <= 7;
    return true;
}

QRectF NotebookCard::visualBounds(const QPointF& bc) const {
    return libobj::documentRect(bc, m_meta);
}

void NotebookCard::paintVisual(QPainter& painter, const QPointF& bc) {
    paintVisualWithLift(painter, bc, 0.0);
}

void NotebookCard::paintVisualWithLift(QPainter& painter, const QPointF& bc, qreal lift) {
    libobj::paintDocument(painter, bc, m_meta, m_dark, lift);
}

// El tipo ya se ve en el objeto; debajo solo va la fecha (y páginas si hay varias).
QString NotebookCard::detailText() const {
    const QString when = relativeDateLabel(m_meta.modified);
    if (m_meta.pages > 1) {
        const QString unit = m_meta.pageFormat == PageFormat::Infinite && !m_meta.hasPdf
            ? QStringLiteral("lienzos") : QStringLiteral("pág");
        return QStringLiteral("%1 · %2 %3").arg(when).arg(m_meta.pages).arg(unit);
    }
    return when;
}

ImportingCard::ImportingCard(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title) {
    setFixedSize(ObjectCard::kCardWidth, ObjectCard::kCardHeight);
    setAttribute(Qt::WA_StyledBackground, false);
    if (!tokens::reducedMotion()) {
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(1400);
        anim->setLoopCount(-1);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_phase = v.toReal();
            update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ImportingCard::setDark(bool dark) {
    m_dark = dark;
    update();
}

void ImportingCard::setStatus(const QString& text) {
    m_status = text;
    update();
}

void ImportingCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const tokens::Palette tk = tokens::paletteFor(m_dark);
    const qreal w = 120.0, h = 158.0;
    const QRectF page((width() - w) / 2.0, 12.0 + (libobj::kVisualHeight - h), w, h);
    paintSoftShadow(p, page, 5.0, 0.0, m_dark);

    QPainterPath clip;
    clip.addRoundedRect(page, 5.0, 5.0);
    p.save();
    p.setClipPath(clip);
    p.fillRect(page, m_dark ? QColor(0x2E, 0x30, 0x3A) : Qt::white);
    if (!tokens::reducedMotion()) {
        const qreal sx = page.left() + (-0.6 + m_phase * 2.2) * page.width();
        QLinearGradient sweep(QPointF(sx, page.top()),
                              QPointF(sx + page.width() * 0.6, page.bottom()));
        const QColor c = m_dark ? QColor(255, 255, 255, 18) : QColor(24, 24, 27, 14);
        sweep.setColorAt(0.0, Qt::transparent);
        sweep.setColorAt(0.5, c);
        sweep.setColorAt(1.0, Qt::transparent);
        p.fillRect(page, sweep);
    }
    p.restore();

    const qreal nameTop = 12.0 + libobj::kVisualHeight + 11.0;
    QFont nf = QApplication::font();
    nf.setPixelSize(14);
    nf.setWeight(QFont::Medium);
    p.setFont(nf);
    p.setPen(tk.text);
    p.drawText(QRectF(10, nameTop, width() - 20, 20), Qt::AlignCenter,
               QFontMetrics(nf).elidedText(m_title, Qt::ElideRight, width() - 20));
    QFont df = QApplication::font();
    df.setPixelSize(12);
    p.setFont(df);
    p.setPen(tk.muted);
    p.drawText(QRectF(10, nameTop + 21, width() - 20, 17), Qt::AlignCenter, m_status);
}

} // namespace finily
