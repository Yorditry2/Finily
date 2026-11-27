#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QLineEdit;
class QPushButton;

namespace finily {

class DotRow;
class FolderPreview;
class IconGrid;
class LocationButton;
class LocationPopover;

// Modal centrado «Nueva carpeta» / «Editar carpeta» (680×410),
// adaptado al mismo diseño de dos columnas de Nuevo cuaderno.
class NewFolderSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewFolderSheet(QWidget* parent = nullptr);

    void setFolders(const QVector<FolderMeta>& folders);
    void openCreate(const QString& parentRelativePath, const HomePalette& pal, bool dark);
    void openEdit(const FolderMeta& folder, const HomePalette& pal, bool dark);

signals:
    void createSubmitted(const QString& parentRelativePath, const QString& name,
                         int colorIndex, int iconIndex);
    void editSubmitted(const QString& relativePath, const QString& newName,
                       int colorIndex, int iconIndex);

protected:
    void onConfirmed() override;
    void onClosing() override;
    void hideEvent(QHideEvent* event) override;

private:
    void applyDark(bool dark);
    void refreshState();

    bool m_editMode = false;
    QString m_parentRelPath;
    QString m_editRelPath;
    int m_colorIndex = 0;
    int m_iconIndex = 0;
    QVector<FolderMeta> m_folders;

    FolderPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    DotRow* m_dots = nullptr;
    IconGrid* m_icons = nullptr;
    QWidget* m_locSection = nullptr;
    LocationButton* m_locBtn = nullptr;
    LocationPopover* m_locPop = nullptr;
    QPushButton* m_cta = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QWidget* m_left = nullptr;
    QWidget* m_right = nullptr;
    QWidget* m_divider = nullptr;
};

} // namespace finily
