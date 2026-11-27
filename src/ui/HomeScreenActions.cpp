#include "ui/HomeScreen.h"

#include "engine/LibraryStore.h"
#include "ui/home/FolderCards.h"
#include "ui/home/HomeMenu.h"
#include "ui/home/NewFolderSheet.h"
#include "ui/home/NewNotebookSheet.h"
#include "ui/home/NotebookCards.h"
#include "ui/home/TrashCards.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

namespace finily {

// ------------------------------------------------------------ menús contextuales

void HomeScreen::showNotebookMenu(NotebookCard* card, const QPoint& globalPos) {
    if (!card || !m_menu)
        return;
    const NotebookMeta meta = card->meta();
    const QPointer<NotebookCard> guard(card);

    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("open"), QStringLiteral("Abrir"),
                               [this, path = meta.path] { emit openNotebookRequested(path); },
                               QStringLiteral("↵"));
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("pencil"), QStringLiteral("Renombrar"),
                               [guard] { if (guard) guard->beginRename(); }, QStringLiteral("F2"));

    // «Mover a…»: Documentos (raíz) y el árbol de carpetas, sin la carpeta actual.
    HomeMenuItem move = HomeMenuItem::act(QStringLiteral("folderMove"), QStringLiteral("Mover a…"), {});
    if (!meta.folderRelativePath.isEmpty())
        move.children << HomeMenuItem::act(QStringLiteral("docs"), QStringLiteral("Documentos"),
                                           [this, path = meta.path] { moveNotebook(path, QString()); });
    for (const FolderMeta& f : m_allFolders) {
        if (f.relativePath == meta.folderRelativePath)
            continue;
        move.children << HomeMenuItem::act(QStringLiteral("folder"),
                                           QString(f.depth * 3, QLatin1Char(' ')) + f.name,
                                           [this, path = meta.path, rel = f.relativePath] { moveNotebook(path, rel); });
    }
    if (!move.children.isEmpty())
        items << move;

    if (meta.pageFormat != PageFormat::Infinite || meta.hasPdf)
        items << HomeMenuItem::act(QStringLiteral("cover"), QStringLiteral("Cambiar portada"), [this, meta] {
            m_notebookSheet->openEditCover(meta, homePaletteFor(m_dark), m_dark);
        });
    items << HomeMenuItem::sep();
    HomeMenuItem trash = HomeMenuItem::act(QStringLiteral("trash"), QStringLiteral("Mover a la papelera"),
                                           [this, path = meta.path, name = meta.title] { moveToTrash(path, name, false); },
                                           QStringLiteral("Supr"));
    trash.danger = true;
    items << trash;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showFolderMenu(const FolderMeta& meta, const QPoint& globalPos, ObjectCard* card) {
    if (!m_menu)
        return;
    const QPointer<ObjectCard> guard(card);
    const QString rel = meta.relativePath;

    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("open"), QStringLiteral("Abrir"), [this, rel] { openFolder(rel); },
                               QStringLiteral("↵"));
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("pencil"), QStringLiteral("Renombrar"), [this, guard, meta] {
        if (guard)
            guard->beginRename();
        else
            m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
    }, QStringLiteral("F2"));

    items << HomeMenuItem::header(QStringLiteral("Color"));
    HomeMenuItem swatches;
    swatches.kind = HomeMenuItem::Kind::Swatches;
    swatches.swatchCurrent = meta.colorIndex;
    swatches.swatchPicked = [this, rel, icon = meta.iconIndex](int color) {
        LibraryStore::updateFolderStyle(rel, color, icon);
        refreshLibrary();
    };
    items << swatches;
    items << HomeMenuItem::act(QStringLiteral("palette"), QStringLiteral("Icono…"), [this, meta] {
        m_folderSheet->openEdit(meta, homePaletteFor(m_dark), m_dark);
    });
    items << HomeMenuItem::sep();
    items << HomeMenuItem::act(QStringLiteral("folderPlus"), QStringLiteral("Nueva subcarpeta"), [this, rel] {
        m_folderSheet->openCreate(rel, homePaletteFor(m_dark), m_dark);
    });
    items << HomeMenuItem::sep();
    HomeMenuItem trash = HomeMenuItem::act(QStringLiteral("trash"), QStringLiteral("Mover a la papelera"),
                                           [this, rel, name = meta.name] {
                                               moveToTrash(LibraryStore::resolveDirectory(rel), name, true);
                                           },
                                           QStringLiteral("Supr"));
    trash.danger = true;
    items << trash;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

void HomeScreen::showTrashMenu(TrashCard* card, const QPoint& globalPos) {
    if (!card || !m_menu)
        return;
    const TrashEntry entry = card->entry();
    QVector<HomeMenuItem> items;
    items << HomeMenuItem::act(QStringLiteral("restore"), QStringLiteral("Restaurar"),
                               [this, id = entry.id] { restoreFromTrash(id); });
    items << HomeMenuItem::sep();
    HomeMenuItem del = HomeMenuItem::act(QStringLiteral("trash"), QStringLiteral("Eliminar para siempre…"),
                                         [this, entry] { deleteForever(entry); });
    del.danger = true;
    items << del;
    m_menu->popup(mapFromGlobal(globalPos), items);
}

// ------------------------------------------------------------ acciones

void HomeScreen::renameNotebookTo(const QString& oldPath, const QString& name) {
    QString newPath, err;
    if (!LibraryStore::renameNotebook(oldPath, name, &newPath, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo renombrar.") : err);
        refreshLibrary();
        return;
    }
    if (newPath != oldPath)
        emit notebookRenamed(oldPath, newPath);
    refreshLibrary();
}

void HomeScreen::renameFolderTo(const QString& relPath, const QString& name) {
    QString newRel, oldAbs, newAbs, err;
    if (!LibraryStore::renameFolder(relPath, name, &newRel, &oldAbs, &newAbs, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo renombrar la carpeta.") : err);
        refreshLibrary();
        return;
    }
    if (newRel != relPath) {
        if (m_currentFolder == relPath)
            m_currentFolder = newRel;
        else if (m_currentFolder.startsWith(relPath + QLatin1Char('/')))
            m_currentFolder = newRel + m_currentFolder.mid(relPath.size());
        emit folderRenamed(oldAbs, newAbs);
    }
    refreshLibrary();
}

void HomeScreen::moveNotebook(const QString& filePath, const QString& targetRelativeFolder) {
    QString newPath, err;
    if (!LibraryStore::moveNotebookToFolder(filePath, targetRelativeFolder, &newPath, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo mover el documento.") : err);
        return;
    }
    if (newPath != filePath)
        emit notebookMoved(filePath, newPath);
    refreshLibrary();
    const QString where = targetRelativeFolder.isEmpty()
        ? QStringLiteral("Documentos") : QFileInfo(targetRelativeFolder).fileName();
    m_toast->showMessage(QStringLiteral("Movido a «%1»").arg(where), QStringLiteral("Deshacer"),
                         [this, newPath, from = QFileInfo(filePath).absolutePath()] {
                             QString back, err2;
                             if (LibraryStore::moveNotebookToDirectory(newPath, from, &back, &err2)) {
                                 emit notebookMoved(newPath, back);
                                 refreshLibrary();
                             } else {
                                 showError(err2);
                             }
                         });
}

// Sin confirmación: el aviso ofrece «Deshacer» durante 6 s (DESIGN.md, principio 7).
void HomeScreen::moveToTrash(const QString& absolutePath, const QString& displayName, bool isFolder) {
    QString id, err;
    if (!LibraryStore::moveToTrash(absolutePath, &id, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo mover a la papelera.") : err);
        return;
    }
    if (isFolder)
        emit folderDeleted(absolutePath);
    else
        emit notebookDeleted(absolutePath);
    refreshLibrary();
    m_toast->showMessage(QStringLiteral("«%1» se movió a la papelera").arg(displayName),
                         QStringLiteral("Deshacer"), [this, id] { restoreFromTrash(id); });
}

void HomeScreen::restoreFromTrash(const QString& id) {
    QString restored, err;
    if (!LibraryStore::restoreFromTrash(id, &restored, &err)) {
        showError(err.isEmpty() ? QStringLiteral("No se pudo restaurar.") : err);
        return;
    }
    refreshLibrary();
    if (m_viewMode == ViewMode::Trash)
        m_toast->showMessage(QStringLiteral("«%1» se restauró").arg(QFileInfo(restored).completeBaseName()));
}

void HomeScreen::deleteForever(const TrashEntry& entry) {
    m_confirm->ask(QStringLiteral("trash"), QStringLiteral("¿Eliminar para siempre?"),
                   QStringLiteral("«%1» se eliminará definitivamente. Esta acción no se puede deshacer.")
                       .arg(entry.name),
                   QStringLiteral("Eliminar"), [this, id = entry.id] {
                       QString err;
                       if (!LibraryStore::deleteFromTrash(id, &err))
                           showError(err.isEmpty() ? QStringLiteral("No se pudo eliminar.") : err);
                       refreshLibrary();
                   });
}

void HomeScreen::askEmptyTrash() {
    const int n = int(LibraryStore::listTrash().size());
    if (n == 0)
        return;
    m_confirm->ask(QStringLiteral("trash"), QStringLiteral("¿Vaciar la papelera?"),
                   n == 1 ? QStringLiteral("Se eliminará para siempre 1 elemento. Esta acción no se puede deshacer.")
                          : QStringLiteral("Se eliminarán para siempre %1 elementos. Esta acción no se puede deshacer.")
                                .arg(n),
                   QStringLiteral("Vaciar"), [this] {
                       LibraryStore::emptyTrash();
                       refreshLibrary();
                   });
}

void HomeScreen::showError(const QString& text) {
    if (m_toast)
        m_toast->showMessage(text);
}

ObjectCard* HomeScreen::hoveredCard() const {
    for (FolderCard* fc : m_folderCards)
        if (fc && fc->isVisible() && fc->underMouse())
            return fc;
    for (NotebookCard* c : m_cards)
        if (c && c->isVisible() && c->underMouse())
            return c;
    return nullptr;
}

FolderCard* HomeScreen::folderCardFor(const QString& relativePath) const {
    for (FolderCard* fc : m_folderCards)
        if (fc && fc->meta().relativePath == relativePath)
            return fc;
    return nullptr;
}

} // namespace finily
