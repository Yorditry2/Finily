#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QLineEdit;
class QPushButton;
class QScrollArea;

namespace finily {

class InfoChip;
class NotebookPreview;
class TileGrid;
class FormatRow;
class PaperDots;

// Galería modal «Nuevo cuaderno» (mockup 05, opción A, 880×600):
// izquierda vista previa + título + formato + Crear; derecha portadas y hojas.
class NewNotebookSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewNotebookSheet(QWidget* parent = nullptr);

    void openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark);
    void openEditCover(const NotebookMeta& meta, const HomePalette& pal, bool dark);

signals:
    void notebookSubmitted(const finily::NewDocumentSpec& spec);
    void coverEditSubmitted(const QString& filePath, finily::CoverStyle style);

protected:
    void onConfirmed() override;

private:
    void refreshState();
    void applyDark(bool dark);

    bool m_editCoverOnly = false;
    QString m_editFilePath;
    NewDocumentSpec m_spec;

    NotebookPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QWidget* m_formatBlock = nullptr;
    QWidget* m_pageBlock = nullptr;
    FormatRow* m_formats = nullptr;
    TileGrid* m_covers = nullptr;
    TileGrid* m_pages = nullptr;
    PaperDots* m_papers = nullptr;
    InfoChip* m_folderChip = nullptr;
    QPushButton* m_cta = nullptr;
    QWidget* m_left = nullptr;
    QWidget* m_right = nullptr;
    QWidget* m_divider = nullptr;
};

} // namespace finily
