#include "ui/home/NotebookCards.h"

#include "ui/DesignTokens.h"
#include "ui/UiIcons.h"
#include "ui/home/FolderCards.h"
#include "ui/home/LibraryObjects.h"

#include <QMenu>

namespace finily {

NotebookCard::NotebookCard(const NotebookMeta& meta, QWidget* parent)
    : ObjectCard(parent)
    , m_meta(meta) {
    setObjectName(QStringLiteral("finilyCard"));
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

void NotebookCard::showMenu(const QPoint& at) {
    QMenu menu(this);
    menu.setStyleSheet(provisionalMenuStyle(m_pal));
    menu.addAction(QStringLiteral("Abrir"), this, [this] { emit openRequested(m_meta.path); });
    menu.addAction(QStringLiteral("Renombrar…"), this, &NotebookCard::renameRequested);

    if (m_meta.pageFormat != PageFormat::Infinite || m_meta.hasPdf)
        menu.addAction(QStringLiteral("Editar portada…"), this, [this] { emit editCoverRequested(m_meta); });

    if (!m_folders.isEmpty() || !m_meta.folderRelativePath.isEmpty()) {
        QMenu* moveMenu = menu.addMenu(QStringLiteral("Mover a…"));
        moveMenu->setStyleSheet(menu.styleSheet());
        if (!m_meta.folderRelativePath.isEmpty()) {
            moveMenu->addAction(QStringLiteral("Documentos (sin carpeta)"), this,
                                [this] { emit moveToFolderRequested(m_meta.path, QString()); });
            if (!m_folders.isEmpty())
                moveMenu->addSeparator();
        }
        for (const FolderMeta& f : m_folders) {
            if (f.relativePath == m_meta.folderRelativePath)
                continue;
            const QString prefix = QString(f.depth * 2, QLatin1Char(' '));
            moveMenu->addAction(uiIcon(QStringLiteral("folder"), tokens::folderColor(f.colorIndex).flap),
                                prefix + f.name, this,
                                [this, rel = f.relativePath] { emit moveToFolderRequested(m_meta.path, rel); });
        }
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("Eliminar"), this, &NotebookCard::deleteRequested);
    menu.exec(at);
}

} // namespace finily
