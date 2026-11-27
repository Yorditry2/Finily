#include "ui/home/NotebookCards.h"

#include "ui/home/LibraryObjects.h"

namespace finily {

NotebookCard::NotebookCard(const NotebookMeta& meta, QWidget* parent)
    : ObjectCard(parent)
    , m_meta(meta) {
    setObjectName(QStringLiteral("finilyCard"));
}

bool NotebookCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.title.contains(needle, Qt::CaseInsensitive);
}

bool NotebookCard::matchesFilter(int filter) const {
    if (filter == 1)
        return m_meta.modified.daysTo(QDateTime::currentDateTime()) <= 7;
    return true;
}

void NotebookCard::paintVisual(QPainter& painter, const QPointF& bc) {
    libobj::paintDocument(painter, bc, m_meta, m_dark);
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

} // namespace finily
