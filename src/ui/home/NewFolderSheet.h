#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QButtonGroup;
class QLineEdit;

namespace finily {

class FolderHeroPreview;

class NewFolderSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewFolderSheet(QWidget* parent = nullptr);

    void openCreate(const QString& parentRelativePath, const HomePalette& pal, bool dark);
    void openEdit(const FolderMeta& folder, const HomePalette& pal, bool dark);

signals:
    void createSubmitted(const QString& parentRelativePath, const QString& name,
                         int colorIndex, int iconIndex);
    void editSubmitted(const QString& relativePath, const QString& newName,
                       int colorIndex, int iconIndex);

protected:
    void onConfirmed() override;

private:
    void refreshGroups();

    bool m_editMode = false;
    QString m_parentRelPath;
    QString m_editRelPath;
    int m_colorIndex = 0;
    int m_iconIndex = 0;

    FolderHeroPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QButtonGroup* m_colorGroup = nullptr;
    QButtonGroup* m_iconGroup = nullptr;
};

} // namespace finily
