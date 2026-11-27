#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace finily {

class BoardColorPopover;
class CoverColorDots;
class FormatPickerButton;
class FormatPopover;
class InfoChip;
class LocationPopover;
class NotebookPreview;
class OrientationToggle;
class PaperColorRow;
class TagPickPopover;
class TileGrid;

// Galería modal centrada «Nuevo cuaderno» (880×600):
// izquierda: vista previa + título + desplegable de formato + Vertical|Horizontal + Crear;
// derecha: portada (color + estilo/subir/tienda), hoja (6 colores + libre y plantillas) y trama.
class NewNotebookSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewNotebookSheet(QWidget* parent = nullptr);

    void setFolders(const QVector<FolderMeta>& folders);
    void setTags(const QVector<TagInfo>& tags);
    void openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark);
    void openEditCover(const NotebookMeta& meta, const HomePalette& pal, bool dark);

signals:
    void notebookSubmitted(const finily::NewDocumentSpec& spec);
    void coverEditSubmitted(const QString& filePath, finily::CoverStyle style);
    void tagCreateRequested(const QString& name);

protected:
    void onConfirmed() override;
    void onClosing() override;
    void hideEvent(QHideEvent* event) override;

private:
    void closePopovers(bool immediate = false);
    void refreshState();
    void applyDark(bool dark);
    void applyPaperColor(const QColor& color);
    void selectBaseFormat(int baseIndex);
    Q_INVOKABLE void setLandscapeOrientation(bool landscape);
    QString tagChipLabel() const;

    bool m_editCoverOnly = false;
    bool m_landscape = false;
    QString m_editFilePath;
    NewDocumentSpec m_spec;
    QVector<FolderMeta> m_folders;
    QVector<TagInfo> m_tags;

    NotebookPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QWidget* m_formatBlock = nullptr;
    QWidget* m_pageBlock = nullptr;
    QWidget* m_spacingBlock = nullptr;
    FormatPickerButton* m_formatBtn = nullptr;
    OrientationToggle* m_orientToggle = nullptr;
    CoverColorDots* m_coverDots = nullptr;
    TileGrid* m_covers = nullptr;
    TileGrid* m_pages = nullptr;
    TileGrid* m_spacings = nullptr;
    QLabel* m_spacingLabel = nullptr;
    PaperColorRow* m_paperColors = nullptr;
    InfoChip* m_folderChip = nullptr;
    InfoChip* m_tagChip = nullptr;
    QPushButton* m_cta = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_left = nullptr;
    QWidget* m_right = nullptr;
    QWidget* m_divider = nullptr;

    FormatPopover* m_formatPop = nullptr;
    BoardColorPopover* m_colorPop = nullptr;
    LocationPopover* m_locPop = nullptr;
    TagPickPopover* m_tagPop = nullptr;
};

} // namespace finily
