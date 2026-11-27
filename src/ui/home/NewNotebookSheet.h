#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

class QButtonGroup;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace finily {

class NotebookHeroPreview;

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
    void setActiveTab(int index);
    void refreshState();

    bool m_editCoverOnly = false;
    QString m_editFilePath;
    NewDocumentSpec m_spec;
    int m_activeTab = 0;

    NotebookHeroPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QWidget* m_tabBar = nullptr;
    QToolButton* m_coverTabBtn = nullptr;
    QToolButton* m_paperTabBtn = nullptr;
    QStackedWidget* m_stack = nullptr;

    QButtonGroup* m_colorGroup = nullptr;
    QButtonGroup* m_iconGroup = nullptr;
    QButtonGroup* m_formatGroup = nullptr;
    QButtonGroup* m_themeGroup = nullptr;
    QButtonGroup* m_tplGroup = nullptr;
    QButtonGroup* m_scaleGroup = nullptr;
    QVector<QToolButton*> m_scaleBtns;
};

} // namespace finily
