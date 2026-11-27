#include "ui/HomeScreen.h"

#include "engine/LibraryStore.h"
#include "ui/home/NotebookCards.h"

#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace finily {

void HomeScreen::renameNotebook(NotebookCard* card) {
    if (!card) return;
    const QString oldPath = card->filePath();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Renombrar"),
                                               QStringLiteral("Nuevo nombre:"), QLineEdit::Normal,
                                               card->title(), &ok);
    if (!ok) return;
    QString newPath, err;
    if (!LibraryStore::renameNotebook(oldPath, name, &newPath, &err)) {
        QMessageBox::warning(this, QStringLiteral("Renombrar"),
                             err.isEmpty() ? QStringLiteral("No se pudo renombrar.") : err);
        return;
    }
    if (newPath != oldPath)
        emit notebookRenamed(oldPath, newPath);
    refreshLibrary();
}

void HomeScreen::deleteNotebook(NotebookCard* card) {
    if (!card) return;
    const QString deletedPath = card->filePath();
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Eliminar documento"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("¿Eliminar «%1»?\nEsta acción no se puede deshacer.").arg(card->title()));
    QPushButton* removeBtn = box.addButton(QStringLiteral("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != removeBtn) return;

    QString err;
    if (!LibraryStore::deleteNotebook(deletedPath, &err)) {
        QMessageBox::warning(this, tr("Eliminar documento"),
                             err.isEmpty() ? tr("No se pudo eliminar el archivo.") : err);
        return;
    }
    emit notebookDeleted(deletedPath);
    refreshLibrary();
}

void HomeScreen::moveNotebook(const QString& filePath, const QString& targetRelativeFolder) {
    QString newPath, err;
    if (!LibraryStore::moveNotebookToFolder(filePath, targetRelativeFolder, &newPath, &err)) {
        QMessageBox::warning(this, tr("Mover documento"),
                             err.isEmpty() ? tr("No se pudo mover el documento.") : err);
        return;
    }
    if (newPath != filePath)
        emit notebookMoved(filePath, newPath);
    refreshLibrary();
}

void HomeScreen::renameFolderByPath(const QString& relativePath) {
    const QString currentName = QFileInfo(relativePath).fileName();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Renombrar carpeta"),
                                               QStringLiteral("Nuevo nombre:"), QLineEdit::Normal,
                                               currentName, &ok);
    if (!ok) return;
    QString newRel, oldAbs, newAbs, err;
    if (!LibraryStore::renameFolder(relativePath, name, &newRel, &oldAbs, &newAbs, &err)) {
        QMessageBox::warning(this, QStringLiteral("Renombrar carpeta"),
                             err.isEmpty() ? QStringLiteral("No se pudo renombrar la carpeta.") : err);
        return;
    }
    if (newRel != relativePath) {
        if (m_currentFolder == relativePath)
            m_currentFolder = newRel;
        else if (m_currentFolder.startsWith(relativePath + QLatin1Char('/')))
            m_currentFolder = newRel + m_currentFolder.mid(relativePath.size());
        emit folderRenamed(oldAbs, newAbs);
    }
    refreshLibrary();
}

void HomeScreen::deleteFolderByPath(const QString& relativePath) {
    const QString folderName = QFileInfo(relativePath).fileName();
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Eliminar carpeta"));
    box.setIcon(QMessageBox::Warning);
    box.setText(QStringLiteral("¿Eliminar la carpeta «%1» y todo su contenido?\nEsta acción no se puede deshacer.")
                    .arg(folderName));
    QPushButton* removeBtn = box.addButton(QStringLiteral("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != removeBtn) return;

    QString absPath, err;
    if (!LibraryStore::deleteFolder(relativePath, &absPath, &err)) {
        QMessageBox::warning(this, tr("Eliminar carpeta"),
                             err.isEmpty() ? tr("No se pudo eliminar la carpeta.") : err);
        return;
    }
    if (m_currentFolder == relativePath
        || m_currentFolder.startsWith(relativePath + QLatin1Char('/'))) {
        m_viewMode = ViewMode::Todos;
        m_currentFolder.clear();
    }
    emit folderDeleted(absPath);
    refreshLibrary();
}

} // namespace finily
