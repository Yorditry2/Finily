#pragma once

#include "core/Types.h"
#include "engine/LibraryStore.h"
#include "ui/home/LibraryViews.h"

#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <memory>

class QLabel;
class QPropertyAnimation;
class QScrollArea;
class QSequentialAnimationGroup;

namespace finily {

class ConfirmDialog;
class CreateMenu;
class DragGhostOverlay;
class DropZoneOverlay;
class FolderCard;
class HomeHeader;
class HomeMenu;
class HomeSidebar;
class HomeToast;
class ImportingCard;
class MarqueeSelectionOverlay;
class MoveToSheet;
class NewBoardSheet;
class NewFolderSheet;
class NewNotebookSheet;
class NotebookCard;
class ObjectCard;
class TagPromptOverlay;
class TrashCard;
class TrashInfoBar;
class UpdateBanner;

class HomeScreen : public QWidget {
    Q_OBJECT

public:
    explicit HomeScreen(QWidget* parent = nullptr);
    ~HomeScreen() override;

    void refreshLibrary();
    void startPdfImport(const QString& pdfPath, const QString& folderRelativePath);
    void showErrorToast(const QString& text);
    void showInfoToast(const QString& text, const QString& icon = QStringLiteral("check"));

    void showUpdateBanner(const QString& version, bool canInstallInApp, const QString& summary = {});
    void setUpdateDownloadProgress(int percent);
    void showUpdateReadyToInstall(const QString& version);
    void showUpdateError(const QString& message);
    void resetUpdateBannerState();
    void hideUpdateBanner();

signals:
    void createDocumentRequested(const finily::NewDocumentSpec& spec);
    void openNotebookRequested(const QString& filePath);
    void openNotebookInNewTabRequested(const QString& filePath);
    void openNotebooksInTabsRequested(const QStringList& filePaths);
    void notebookRenamed(const QString& oldPath, const QString& newPath);
    void notebookMoved(const QString& oldPath, const QString& newPath);
    void notebookDeleted(const QString& filePath);
    void folderRenamed(const QString& oldAbsDir, const QString& newAbsDir);
    void folderDeleted(const QString& absDir);
    void boardStyleChanged(const QString& filePath);
    void importPdfFromHomeRequested(const QString& folderRelativePath);
    void openExternalFileRequested();
    void continueDraftRequested();
    void updateInstallRequested();
    void updateCancelRequested();
    void updateReleaseNotesRequested();
    void updateDismissed();
    void updateRestartRequested();
    void pressureSensitivityChanged(double value);
    void palmRejectionChanged(bool enabled);
    void checkUpdatesRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class ViewMode {
        Todos = 0,
        Favoritos = 1,
        FoldersRoot = 2,
        Folder = 3,
        Tag = 4,
        Trash = 5
    };

    struct PendingMoveTarget {
        QStringList docPaths;
        QStringList folderRels;
        QString label;
        QString currentRel;
    };

    void buildLayout();
    void buildOverlays();
    void installShortcuts();
    void openCreateMenu();
    QRect createMenuAnchor() const;
    void setSidebarCollapsed(bool collapsed, bool animated = true);
    void launchQuickNote();
    void selectFilter(int index);
    void openFolder(const QString& relativePath, bool animated = true);
    void openTag(const QString& tagId);
    void applySearchFilter(const QString& text);
    void runAnimatedFolderTransition(const QPointF& pivotInViewport, bool entering,
                                     const std::function<void()>& applyState);

    // Menús contextuales propios (mockup 07) y acciones de la biblioteca.
    void showNotebookMenu(NotebookCard* card, const QPoint& globalPos);
    void showFolderMenu(const FolderMeta& meta, const QPoint& globalPos, ObjectCard* card);
    void showTrashMenu(TrashCard* card, const QPoint& globalPos);
    void showEmptySpaceMenu(const QPoint& globalPos);
    void showMultiSelectMenu(const QPoint& globalPos);
    void showTagMenu(const TagInfo& tag, const QPoint& globalPos);

    void openMoveSheetFor(const QStringList& docPaths, const QStringList& folderRels,
                          const QString& titleLabel, const QString& currentRel);
    void executeMoveTarget(const PendingMoveTarget& target, const QString& destRel,
                           bool showUndoToast = true);
    void renameNotebookTo(const QString& filePath, const QString& newName,
                          NotebookCard* card = nullptr);
    void renameFolderTo(const QString& relativePath, const QString& newName,
                        FolderCard* card = nullptr);
    void duplicateNotebooks(const QStringList& paths);
    void toggleFavoriteForCard(ObjectCard* card);
    void toggleFavoriteForSelection();
    void exportNotebooksToPdf(const QStringList& paths);
    void moveNotebook(const QString& filePath, const QString& targetRelativeFolder);
    void moveToTrash(const QString& absolutePath, const QString& displayName, bool isFolder,
                     ObjectCard* sourceCard = nullptr);
    void moveSelectionToTrash();
    void restoreFromTrash(const QString& id);
    void promptRestoreFromTrash(const TrashEntry& entry);
    void deleteForever(const TrashEntry& entry);
    void askEmptyTrash();
    void promptNewTag(const QStringList& attachDocPaths = {});
    void promptRenameTag(const TagInfo& tag);
    void deleteTagWithUndo(const TagInfo& tag);
    void importExternalImageFile(const QString& imgPath, const QString& folderRelativePath);
    void showError(const QString& text);

    // Selección múltiple y arrastre interno.
    static QString keyForDoc(const QString& absPath);
    static QString keyForFolder(const QString& relPath);
    QString keyForCard(const ObjectCard* card) const;
    QVector<ObjectCard*> orderedCards() const;
    void handleCardSelectionClick(ObjectCard* card, Qt::KeyboardModifiers mods);
    void selectAllVisible();
    void clearSelection();
    void syncCardSelectionVisuals();
    void startInternalDrag(ObjectCard* sourceCard, const QPoint& globalPos);
    void updateInternalDrag(const QPoint& globalPos);
    void finishInternalDrag(const QPoint& globalPos, bool cancelled);
    void updateDropTargetHighlight(const QPoint& globalPos, QString* outFolderRel,
                                   QString* outLabel);

    // Utilidades de grilla y animación FLIP.
    QHash<QString, QPoint> captureCardPositions() const;
    QVector<QPair<QString, QWidget*>> currentCardWidgets() const;
    void refreshLibraryWithFlip(const QHash<QString, QPoint>& beforePos, int staggerMs = 20);
    ObjectCard* hoveredCard() const;
    FolderCard* folderCardFor(const QString& relativePath) const;
    NotebookCard* notebookCardFor(const QString& filePath) const;

    void setDarkMode(bool dark, bool animated = true);
    void applyThemeMode(int mode);
    void applyAppearancePreferences();
    void openSettings();
    void leaveSettings();
    void applyTheme();
    void updateHeaderAndEmptyState();
    void relayoutGrid();
    void syncToastArea();

    // Búsqueda, filtro y estados vacíos.
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
    MoveToSheet* m_moveSheet = nullptr;
    UpdateBanner* m_updateBanner = nullptr;
    HomeMenu* m_menu = nullptr;
    HomeToast* m_toast = nullptr;
    ConfirmDialog* m_confirm = nullptr;
    TagPromptOverlay* m_tagPrompt = nullptr;

    QScrollArea* m_scroll = nullptr;
    QWidget* m_gridContent = nullptr;
    EmptyStateView* m_emptyState = nullptr;
    FirstRunHint* m_firstRunHint = nullptr;
    FilterChipsBar* m_chipsBar = nullptr;
    FilterPanel* m_filterPanel = nullptr;
    DropZoneOverlay* m_dropZone = nullptr;
    DragGhostOverlay* m_dragGhost = nullptr;
    MarqueeSelectionOverlay* m_marquee = nullptr;
    class SettingsView* m_settings = nullptr;

    bool m_settingsOpen = false;
    bool m_sidebarCollapsed = false;
    bool m_skipNextFlip = false;
    bool m_updateDismissed = false;

    QVector<ResultGroupLabel*> m_groupLabels;
    LibraryFilter m_filter;
    bool m_searchWhole = false;
    bool m_libraryEmpty = false;
    QVector<FolderCard*> m_folderCards;
    QVector<NotebookCard*> m_cards;
    QVector<TrashCard*> m_trashCards;
    QVector<ImportingCard*> m_importingCards;
    TrashInfoBar* m_trashBar = nullptr;

    QVector<FolderMeta> m_allFolders;
    QVector<TagInfo> m_allTags;
    QSet<QString> m_selectedKeys;
    QString m_lastClickedKey;
    QString m_justCreatedFolderRel;
    QString m_pendingFlyRestorePath;
    PendingMoveTarget m_pendingMove;

    // Estado de arrastre interno y recuadro de selección.
    bool m_internalDragging = false;
    ObjectCard* m_dragSourceCard = nullptr;
    QStringList m_dragDocPaths;
    QStringList m_dragFolderRels;
    QPixmap m_dragSnap;
    bool m_marqueeActive = false;
    QPoint m_marqueeOrigin;

    std::shared_ptr<std::atomic_bool> m_pdfCancelFlag;

    ViewMode m_viewMode = ViewMode::Todos;
    QString m_currentFolder;
    QString m_currentTagId;
    bool m_dark = false;
};

} // namespace finily
