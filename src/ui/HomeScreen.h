#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/LibraryViews.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace finily {

class ConfirmDialog;
class CreateMenu;
class FolderCard;
class HomeHeader;
class HomeMenu;
class HomeToast;
class ObjectCard;
class HomeSidebar;
class NewBoardSheet;
class NewFolderSheet;
class NewNotebookSheet;
class NotebookCard;
class TrashCard;
class TrashInfoBar;
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
    void pressureSensitivityChanged(double value);
    void checkUpdatesRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class ViewMode { Todos = 0, Recientes = 1, Folder = 2, Trash = 3 };

    void buildLayout();
    void buildOverlays();
    void openCreateMenu();
    QRect createMenuAnchor() const;
    void setSidebarCollapsed(bool collapsed);
    void launchQuickNote();
    void selectFilter(int index);
    void openFolder(const QString& relativePath);
    void applySearchFilter(const QString& text);
    // Menús contextuales propios (mockup 07) y acciones de la biblioteca.
    void showNotebookMenu(NotebookCard* card, const QPoint& globalPos);
    void showFolderMenu(const FolderMeta& meta, const QPoint& globalPos, ObjectCard* card);
    void showTrashMenu(TrashCard* card, const QPoint& globalPos);
    void renameNotebookTo(const QString& filePath, const QString& newName);
    void renameFolderTo(const QString& relativePath, const QString& newName);
    void moveNotebook(const QString& filePath, const QString& targetRelativeFolder);
    void moveToTrash(const QString& absolutePath, const QString& displayName, bool isFolder);
    void restoreFromTrash(const QString& id);
    void deleteForever(const TrashEntry& entry);
    void askEmptyTrash();
    void showError(const QString& text);
    ObjectCard* hoveredCard() const;
    FolderCard* folderCardFor(const QString& relativePath) const;
    void setDarkMode(bool dark);
    void applyThemeMode(int mode);
    void openSettings();
    void leaveSettings();
    void applyTheme();
    void updateHeaderAndEmptyState();
    void relayoutGrid();
    // Búsqueda y filtro (mockup 09) y estados vacíos (mockup 08).
    bool isSearching() const;
    bool searchIsScoped() const;
    void onSearchTextChanged(const QString& text);
    void setLibraryFilter(const LibraryFilter& filter);
    void openFilterPanel();
    void openFirstResult();
    void configureEmptyState(int totalVisible);
    void clearGroupLabels();

    HomeSidebar* m_sidebar = nullptr;
    HomeHeader* m_header = nullptr;
    CreateMenu* m_createMenu = nullptr;
    NewFolderSheet* m_folderSheet = nullptr;
    NewBoardSheet* m_boardSheet = nullptr;
    NewNotebookSheet* m_notebookSheet = nullptr;
    UpdateBanner* m_updateBanner = nullptr;
    HomeMenu* m_menu = nullptr;
    HomeToast* m_toast = nullptr;
    ConfirmDialog* m_confirm = nullptr;

    QScrollArea* m_scroll = nullptr;
    QWidget* m_gridContent = nullptr;
    EmptyStateView* m_emptyState = nullptr;
    FirstRunHint* m_firstRunHint = nullptr;
    FilterChipsBar* m_chipsBar = nullptr;
    FilterPanel* m_filterPanel = nullptr;
    class SettingsView* m_settings = nullptr;
    bool m_settingsOpen = false;
    QVector<ResultGroupLabel*> m_groupLabels;
    LibraryFilter m_filter;
    bool m_searchWhole = false;
    bool m_libraryEmpty = false;
    QVector<FolderCard*> m_folderCards;
    QVector<NotebookCard*> m_cards;
    QVector<TrashCard*> m_trashCards;
    TrashInfoBar* m_trashBar = nullptr;
    QVector<FolderMeta> m_allFolders;

    ViewMode m_viewMode = ViewMode::Todos;
    QString m_currentFolder;
    bool m_dark = false;
};

} // namespace finily
