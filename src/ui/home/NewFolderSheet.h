#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QPushButton;

namespace finily {

class SheetHero;
class DotRow;
class IconGrid;
class LocationPopover;
class LocationButton;

// Tarjeta lateral «Nueva carpeta» / «Editar carpeta» (mockup 06).
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
    void hideEvent(QHideEvent* event) override;

private:
    void refreshState();
    QString pathLabel(const QString& rel) const;

    bool m_editMode = false;
    QString m_parentRelPath;
    QString m_editRelPath;
    int m_colorIndex = 0;
    int m_iconIndex = 0;
    QVector<FolderMeta> m_folders;

    SheetHero* m_hero = nullptr;
    DotRow* m_dots = nullptr;
    IconGrid* m_icons = nullptr;
    QWidget* m_locSection = nullptr;
    LocationButton* m_locBtn = nullptr;
    LocationPopover* m_locPop = nullptr;
    QPushButton* m_cta = nullptr;
};

} // namespace finily
