#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace finily {

class CreateMenu;
class CreateTileCard;
class FolderCard;
class HomeHeader;
class HomeSidebar;
class NewBoardSheet;
class NewFolderSheet;
class NewNotebookSheet;
class NotebookCard;
class UpdateBanner;

class HomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit HomeScreen(QWidget* parent = nullptr);

    void refreshLibrary();

    void showUpdateBanner(const QString& version, bool canInstallInApp);
    void setUpdateDownloadProgress(int percent);
    void resetUpdateBannerState();
    void hideUpdateBanner();

signals:
    void createDocumentRequested(const finily::NewDocumentSpec& spec);
    void openNotebookRequested(const QString& filePath);
    void notebookRenamed(const QString& oldPath, const QString& newPath);
    void notebookMoved(const QString& oldPath, const QString& newPath);
    void notebookDeleted(const QString& filePath);
    void folderRenamed(const QString& oldAbsDir, const QString& newAbsDir);
    void folderDeleted(const QString& absDir);
    void importPdfFromHomeRequested(const QString& folderRelativePath);
    void openExternalFileRequested();
    void continueDraftRequested();
    void updateInstallRequested();
    void updateCancelRequested();
    void updateReleaseNotesRequested();
    void updateDismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class ViewMode { Todos = 0, Recientes = 1, Folder = 2 };

    void buildLayout();
    void buildOverlays();
    void openCreateMenu();
    void launchQuickNote();
    void selectFilter(int index);
    void openFolder(const QString& relativePath);
    void applySearchFilter(const QString& text);
    void renameNotebook(NotebookCard* card);
    void deleteNotebook(NotebookCard* card);
    void moveNotebook(const QString& filePath, const QString& targetRelativeFolder);
    void renameFolderByPath(const QString& relativePath);
    void deleteFolderByPath(const QString& relativePath);
    void setDarkMode(bool dark);
    void applyTheme();
    void updateHeaderAndEmptyState();
    void relayoutGrid();

    HomeSidebar* m_sidebar = nullptr;
    HomeHeader* m_header = nullptr;
    CreateMenu* m_createMenu = nullptr;
    NewFolderSheet* m_folderSheet = nullptr;
    NewBoardSheet* m_boardSheet = nullptr;
    NewNotebookSheet* m_notebookSheet = nullptr;
    UpdateBanner* m_updateBanner = nullptr;

    QScrollArea* m_scroll = nullptr;
    QWidget* m_gridContent = nullptr;
    CreateTileCard* m_createCard = nullptr;
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QVector<FolderCard*> m_folderCards;
    QVector<NotebookCard*> m_cards;
    QVector<FolderMeta> m_allFolders;

    ViewMode m_viewMode = ViewMode::Todos;
    QString m_currentFolder;
    bool m_dark = false;
};

} // namespace finily
