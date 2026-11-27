#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/HomeTheme.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace finily {

class SidebarItemButton;
class SidebarFolderButton;

class HomeSidebar : public QWidget {
    Q_OBJECT

public:
    explicit HomeSidebar(QWidget* parent = nullptr);

    void setSelectedFilter(int filterIndex);
    void setSelectedFolder(const QString& relativePath);
    void setCounts(int totalCount, int recentCount);
    void setFolders(const QVector<FolderMeta>& folders);
    void setDraftVisible(bool visible);
    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void filterSelected(int filterIndex);
    void folderSelected(const QString& relativePath);
    void createRootFolderRequested();
    void createSubfolderRequested(const QString& parentRelativePath);
    void editFolderRequested(const finily::FolderMeta& meta);
    void renameFolderRequested(const QString& relativePath);
    void deleteFolderRequested(const QString& relativePath);
    void openExternalFileRequested();
    void continueDraftRequested();
    void darkModeToggled(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    HomePalette m_pal = homePaletteFor(false);
    bool m_dark = false;
    int m_selectedFilter = 0;
    QString m_selectedFolder;

    SidebarItemButton* m_allBtn = nullptr;
    SidebarItemButton* m_recentBtn = nullptr;
    QLabel* m_foldersLabel = nullptr;
    QToolButton* m_addFolderBtn = nullptr;
    QScrollArea* m_folderScroll = nullptr;
    QWidget* m_folderContainer = nullptr;
    QVBoxLayout* m_folderLayout = nullptr;
    QVector<SidebarFolderButton*> m_folderBtns;

    SidebarItemButton* m_draftBtn = nullptr;
    SidebarItemButton* m_openBtn = nullptr;
    SidebarItemButton* m_themeBtn = nullptr;
    QWidget* m_midLine = nullptr;
    QWidget* m_footerLine = nullptr;
};

} // namespace finily
