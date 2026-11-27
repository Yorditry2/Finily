#include "ui/home/FolderCards.h"

#include "ui/home/LibraryObjects.h"

namespace finily {

FolderCard::FolderCard(const FolderMeta& meta, QWidget* parent)
    : ObjectCard(parent)
    , m_meta(meta) {}

void FolderCard::setMeta(const FolderMeta& meta) {
    m_meta = meta;
    update();
}

bool FolderCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.name.contains(needle, Qt::CaseInsensitive);
}

QRectF FolderCard::visualBounds(const QPointF& bc) const {
    return libobj::folderRect(bc);
}

void FolderCard::paintVisual(QPainter& painter, const QPointF& bc) {
    paintVisualWithLift(painter, bc, 0.0);
}

void FolderCard::paintVisualWithLift(QPainter& painter, const QPointF& bc, qreal lift) {
    Q_UNUSED(lift);
    const qreal open = isDropTarget() ? 1.0 : hoverProgress();
    libobj::paintFolder(painter, bc, m_meta.colorIndex, m_meta.iconIndex, open, m_dark);
}

QString FolderCard::detailText() const {
    const int docs = m_meta.documentCount;
    const int subs = m_meta.directSubfolders;
    if (subs > 0 && docs > 0)
        return QStringLiteral("%1 carpetas · %2 docs").arg(subs).arg(docs);
    if (subs > 0)
        return subs == 1 ? QStringLiteral("1 carpeta") : QStringLiteral("%1 carpetas").arg(subs);
    if (docs > 0)
        return docs == 1 ? QStringLiteral("1 documento") : QStringLiteral("%1 documentos").arg(docs);
    return QStringLiteral("Vacía");
}

} // namespace finily
