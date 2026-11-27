#include "ui/FloatingToolbar.h"

#include "core/Utils.h"
#include "ui/CanvasWidget.h"
#include "ui/editor/EditorChrome.h"
#include "ui/editor/EditorSidePanels.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QStringList>

namespace finily {

void FloatingToolbar::showToast(const QString& message, bool withUndo) {
    if (!m_toast)
        return;
    m_toast->showToast(message, withUndo, 6000);
    positionAll();
}

void FloatingToolbar::showExportMenu(const QRect& anchorRect) {
    QVector<EditorDropdownItem> items = {
        {1, QStringLiteral("Exportar a PDF…"), QStringLiteral("Ctrl+E"), QStringLiteral("pdf"), QStringLiteral("ui")},
        {2, QStringLiteral("Página como imagen…"), {}, QStringLiteral("img"), QStringLiteral("ui")},
        {3, QStringLiteral("Guardar copia…"), QStringLiteral("Ctrl+Shift+S"), QStringLiteral("save"), QStringLiteral("editor"), true},
        {4, QStringLiteral("Abrir otro archivo…"), QStringLiteral("Ctrl+Shift+O"), QStringLiteral("open"), QStringLiteral("ui")},
        {5, QStringLiteral("Importar PDF…"), QStringLiteral("Ctrl+O"), QStringLiteral("pdf"), QStringLiteral("ui"), true},
    };
    const QPoint pos(std::max(12, anchorRect.right() - 248), anchorRect.bottom() + 8);
    m_dropdownMenu->openMenu(items, pos, size(), [this](int id) {
        updateOverlayMask();
        if (id == 1) exportPdfDialog();
        else if (id == 2) emit exportPageImageRequested();
        else if (id == 3) saveNotebookAsDialog();
        else if (id == 4) openNotebookDialog();
        else if (id == 5) importPdfDialog();
    }, 248);
    updateOverlayMask();
}

void FloatingToolbar::showTabMenu(int index, const QPoint& globalPos) {
    QVector<EditorDropdownItem> items = {
        {1, QStringLiteral("Renombrar"), QStringLiteral("F2"), QStringLiteral("pencil"), QStringLiteral("ui")},
        {2, QStringLiteral("Duplicar"), {}, QStringLiteral("copy"), QStringLiteral("ui")},
        {3, QStringLiteral("Guardar copia…"), QStringLiteral("Ctrl+Shift+S"), QStringLiteral("save"), QStringLiteral("editor")},
        {4, QStringLiteral("Mostrar en la biblioteca"), {}, QStringLiteral("folder"), QStringLiteral("ui")},
        {5, QStringLiteral("Exportar a PDF…"), QStringLiteral("Ctrl+E"), QStringLiteral("pdf"), QStringLiteral("ui"), true},
        {6, QStringLiteral("Cerrar pestaña"), QStringLiteral("Ctrl+W"), QStringLiteral("x"), QStringLiteral("ui"), true, true},
    };
    const QPoint pos = mapFromGlobal(globalPos);
    m_dropdownMenu->openMenu(items, pos, size(), [this, index](int id) {
        updateOverlayMask();
        if (id == 1) {
            emit switchTabRequested(index);
            if (m_topBar) m_topBar->startInlineRenameActiveTab();
        } else if (id == 2) {
            emit duplicateTabRequested(index);
        } else if (id == 3) {
            emit switchTabRequested(index);
            saveNotebookAsDialog();
        } else if (id == 4) {
            emit revealInLibraryRequested(index);
        } else if (id == 5) {
            emit switchTabRequested(index);
            exportPdfDialog();
        } else if (id == 6) {
            emit closeTabRequested(index);
        }
    }, 236);
    updateOverlayMask();
}

void FloatingToolbar::insertImageDialog() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    const QString path = QFileDialog::getOpenFileName(
        window(), tr("Insertar imagen"), QString(),
        tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.svg)"));
    if (path.isEmpty())
        return;
    if (m_canvas->importImageFile(path)) {
        QStringList recents = readAppSetting(QStringLiteral("editor/recentImages")).toStringList();
        recents.removeAll(path);
        recents.prepend(path);
        while (recents.size() > 16)
            recents.removeLast();
        writeAppSetting(QStringLiteral("editor/recentImages"), recents);
        if (m_elementsPanel && m_elementsPanel->isVisible())
            m_elementsPanel->syncFromCanvas();
    }
    refreshFromCanvas();
}

void FloatingToolbar::importPdfDialog() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    const QString path =
        QFileDialog::getOpenFileName(window(), tr("Importar PDF"), QString(), tr("Archivos PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    m_canvas->importPdfFile(path);
    refreshFromCanvas();
}

void FloatingToolbar::exportPdfDialog() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    QString path = QFileDialog::getSaveFileName(window(), tr("Exportar clase a PDF"),
                                                QStringLiteral("finily-clase.pdf"),
                                                tr("Archivos PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");
    if (m_canvas->exportToPdf(path))
        showToast(tr("PDF exportado"));
    refreshFromCanvas();
}

void FloatingToolbar::newDocumentAction() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    if (!confirmDiscardChanges(QStringLiteral("crear un cuaderno nuevo")))
        return;
    m_canvas->newDocument();
    refreshFromCanvas();
}

void FloatingToolbar::openNotebookDialog() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    if (!confirmDiscardChanges(QStringLiteral("abrir otro cuaderno")))
        return;
    const QString path = QFileDialog::getOpenFileName(window(), tr("Abrir cuaderno"), QString(),
                                                      tr("Cuaderno Finily (*.finily)"));
    if (path.isEmpty())
        return;
    if (!m_canvas->loadFromFile(path))
        showToast(tr("No se pudo abrir el cuaderno"));
    refreshFromCanvas();
}

bool FloatingToolbar::confirmDiscardChanges(const QString& action) {
    if (!m_canvas || !m_canvas->hasUnsavedChanges())
        return true;
    const int choice = m_confirmCard->execModal(
        this,
        tr("Cambios sin guardar"),
        tr("El cuaderno actual tiene cambios sin guardar. ¿Quieres guardarlos antes de %1?").arg(action));
    updateOverlayMask();
    if (choice == 1) {
        saveNotebookAction();
        return !m_canvas->hasUnsavedChanges();
    }
    return choice == 2;
}

void FloatingToolbar::saveNotebookAction() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    const QString current = m_canvas->currentFilePath();
    if (current.isEmpty()) {
        saveNotebookAsDialog();
        return;
    }
    m_canvas->saveToFile(current);
    showToast(tr("Cuaderno guardado"));
    refreshFromCanvas();
}

void FloatingToolbar::saveNotebookAsDialog() {
    if (!m_canvas)
        return;
    closeTransientPopovers();
    QString path = QFileDialog::getSaveFileName(window(), tr("Guardar cuaderno"),
                                                QStringLiteral("cuaderno.finily"),
                                                tr("Cuaderno Finily (*.finily)"), nullptr,
                                                QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".finily"), Qt::CaseInsensitive))
        path += QStringLiteral(".finily");
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        const int choice = m_confirmCard->execModal(
            this,
            tr("Reemplazar cuaderno"),
            tr("¿Quieres reemplazar «%1»? Esta acción sobrescribirá el archivo anterior.")
                .arg(QFileInfo(path).fileName()));
        updateOverlayMask();
        if (choice != 1)
            return;
    }
    m_canvas->saveToFile(path, !exists);
    showToast(tr("Copia guardada"));
    refreshFromCanvas();
}

} // namespace finily
