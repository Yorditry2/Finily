#include "ui/home/FolderCards.h"

#include "ui/home/LibraryObjects.h"

#include <QMenu>

namespace finily {

QString provisionalMenuStyle(const HomePalette& pal) {
    return QStringLiteral(
               "QMenu { background: %1; border: 1px solid %2; border-radius: 12px; padding: 6px; }"
               "QMenu::item { color: %3; padding: 7px 18px; border-radius: 8px; font-size: 13px; }"
               "QMenu::item:selected { background: %4; }"
               "QMenu::separator { height: 1px; background: %2; margin: 5px 8px; }")
        .arg(pal.surface.name(), pal.border.name(QColor::HexArgb), pal.text.name(),
             pal.hover.name(QColor::HexArgb));
}

FolderCard::FolderCard(const FolderMeta& meta, QWidget* parent)
    : ObjectCard(parent)
    , m_meta(meta) {}

bool FolderCard::matchesSearch(const QString& needle) const {
    return needle.isEmpty() || m_meta.name.contains(needle, Qt::CaseInsensitive);
}

void FolderCard::paintVisual(QPainter& painter, const QPointF& bc) {
    libobj::paintFolder(painter, bc, m_meta.colorIndex, m_meta.iconIndex, hoverProgress(), m_dark);
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

void FolderCard::showMenu(const QPoint& at) {
    QMenu menu(this);
    menu.setStyleSheet(provisionalMenuStyle(m_pal));
    menu.addAction(QStringLiteral("Abrir"), this, [this] { emit openRequested(m_meta.relativePath); });
    menu.addAction(QStringLiteral("Nueva subcarpeta…"), this,
                   [this] { emit newSubfolderRequested(m_meta.relativePath); });
    menu.addAction(QStringLiteral("Editar carpeta…"), this, [this] { emit editRequested(m_meta); });
    menu.addAction(QStringLiteral("Renombrar…"), this, [this] { emit renameRequested(m_meta.relativePath); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Eliminar"), this, [this] { emit deleteRequested(m_meta.relativePath); });
    menu.exec(at);
}

} // namespace finily
