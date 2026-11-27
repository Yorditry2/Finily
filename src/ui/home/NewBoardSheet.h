#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace finily {

class BoardColorPopover;
class BoardPreview;
class InfoChip;
class LocationPopover;
class SwatchRow;
class TagPickPopover;
class TemplateRow;

// Modal centrado «Nueva pizarra infinita» / «Fondo y plantilla» (720×440),
// adaptado al mismo diseño de dos columnas de Nuevo cuaderno.
class NewBoardSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewBoardSheet(QWidget* parent = nullptr);

    void setFolders(const QVector<FolderMeta>& folders);
    void setTags(const QVector<TagInfo>& tags);
    void openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark);
    void openEditStyle(const NotebookMeta& meta, const HomePalette& pal, bool dark);
    void openColorPopover();

signals:
    void boardSubmitted(const finily::NewDocumentSpec& spec);
    void styleEditSubmitted(const QString& path, finily::ThemeId theme, const QColor& customBg,
                            finily::BackgroundTemplate tpl, double scale);
    void tagCreateRequested(const QString& name);

protected:
    void onConfirmed() override;
    void onClosing() override;
    void hideEvent(QHideEvent* event) override;

private:
    void applyBackgroundColor(const QColor& color);
    void closePopovers(bool immediate = false);
    void applyDark(bool dark);
    void refreshState();
    QString tagChipLabel() const;

    bool m_editStyleOnly = false;
    QString m_editFilePath;
    NewDocumentSpec m_spec;
    QVector<FolderMeta> m_folders;
    QVector<TagInfo> m_tags;

    BoardPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    SwatchRow* m_swatches = nullptr;
    QLabel* m_hex = nullptr;
    TemplateRow* m_templates = nullptr;
    InfoChip* m_folderChip = nullptr;
    InfoChip* m_tagChip = nullptr;
    QPushButton* m_cta = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QWidget* m_left = nullptr;
    QWidget* m_right = nullptr;
    QWidget* m_divider = nullptr;

    BoardColorPopover* m_colorPop = nullptr;
    LocationPopover* m_locPop = nullptr;
    TagPickPopover* m_tagPop = nullptr;
};

} // namespace finily
