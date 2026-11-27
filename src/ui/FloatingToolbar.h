#pragma once

#include "core/Types.h"
#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPointer>
#include <QRegion>
#include <QString>
#include <QVector>
#include <QWidget>

namespace finily {

class CanvasWidget;
class EditorColorPopover;
class EditorConfirmCard;
class EditorContextCapsule;
class EditorDetachedDock;
class EditorDropdownMenu;
class EditorEdgeMarkCapsule;
class EditorElementsPanel;
class EditorEraserPanel;
class EditorFillPanel;
class EditorFineWidthPopover;
class EditorHighlighterPanel;
class EditorInlineTextBar;
class EditorLaserPanel;
class EditorLassoPanel;
class EditorLibraryModal;
class EditorPageCapsule;
class EditorPagesPanel;
class EditorPaperPanel;
class EditorPenPanel;
class EditorSettingsModal;
class EditorShapesPanel;
class EditorToast;
class EditorTopBar;
class NewBoardSheet;
class NewNotebookSheet;

enum class ToolbarDockPos { Izquierda, Arriba, Abajo };
enum class PageNavDirection { Horizontal, Vertical };

// Capa superpuesta al CanvasWidget (hermana en MainWindow::m_container).
// Todas las barras, docks, paneles, cápsulas y modales son hijos directos
// pintados con QPainter (cero Qt::Popup / QMenu / QDialog del sistema).
class FloatingToolbar : public QWidget {
    Q_OBJECT

public:
    using DockPos = ToolbarDockPos;
    using PageNavDirection = finily::PageNavDirection;

    struct TabEntry {
        QString title;
        bool dirty = false;
        bool active = false;
        bool isBoard = false;
        bool isPdf = false;
        QString filePath;
    };

    explicit FloatingToolbar(CanvasWidget* canvas, QWidget* parent = nullptr);

    void refreshFromCanvas();
    void setNotebookTabs(const QVector<TabEntry>& tabs);

    void toggleCleanUi();
    bool isCleanUi() const { return m_cleanUi; }
    void toggleFullscreenRequested();

    void selectTool(ToolType tool);
    void selectPreset(int index); // 0..4
    QString presetShortcut(int index) const;
    PageNavDirection pageNavDirection() const { return m_pageNavDir; }
    void setPageNavDirection(PageNavDirection dir);
    void openStickersPopover();
    void startInlineRenameActiveTab();
    void openSettingsModal(const QString& section = QStringLiteral("escritura"));
    void openLibraryModal(const QString& section = QStringLiteral("escritura"));
    void openRadialColorPicker(const QPointF& nearScreenPos = {});

    void importPdfDialog();
    void exportPdfDialog();
    void insertImageDialog();

    void newDocumentAction();
    void openNotebookDialog();
    void saveNotebookAction();
    void saveNotebookAsDialog();
    bool confirmDiscardChanges(const QString& action);

    void showToast(const QString& message, bool withUndo = false);
    void setUpdateAvailable(const QString& version);

#ifdef FINILY_DEV_SNAPSHOTS
    void applySnapshotScene(const QString& scene);
#endif

signals:
    void homeRequested();
    void renameRequested(const QString& newName);
    void checkForUpdatesRequested();
    void vectorizeSelectedImageRequested();
    void extractSelectedImageRequested();
    void configureAiApiRequested();
    void newTabRequested();
    void createDocumentRequested(const finily::NewDocumentSpec& spec);
    void openNotebookInNewTabRequested(const QString& filePath);
    void importPdfInNewTabRequested(const QString& filePath);
    void switchTabRequested(int index);
    void closeTabRequested(int index);
    void duplicateTabRequested(int index);
    void revealInLibraryRequested(int index);
    void exportPageImageRequested();
    void fullscreenToggled();
    void padActionsChanged();
    void pageNavDirectionChanged(finily::FloatingToolbar::PageNavDirection dir);
    void presetsChanged();

public slots:
    void setDocumentTitle(const QString& title, bool unsaved);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initPresets();
    void loadPresets();
    void savePresets();
    void saveCurrentToPreset(int index);

    void setDockMode(ToolbarDockMode mode);
    void applyThemeToAll(bool dark);
    void openToolPanel(ToolType tool, const QRect& anchorRect);
    void openPaperPanel(const QRect& anchorRect);
    void openColorPopover(ToolType tool, const QRect& anchorRect, bool isShapeFill = false);
    void openFineWidthPopover(ToolType tool, const QRect& anchorRect);
    void openDropdown(QWidget* anchor, const QVector<EditorDropdownItem>& items, std::function<void(int)> cb);
    void showExportMenu(const QRect& anchorRect);
    void showNewTabMenu(const QRect& anchorRect);
    void openNotebookInNewTabDialog();
    void importPdfInNewTabDialog();
    void showTabMenu(int index, const QPoint& globalPos);
    void showEdgeMarkCapsule(const QPointF& screenPos, const QPointF& worldPos);

    void positionAll();
    void positionFloatingCard(QWidget* card, const QRect& anchorRect, bool preferLeft = false);
    void positionContextCapsule();
    void updateOverlayMask();
    void closeTransientPopovers();

    QPointer<CanvasWidget> m_canvas;
    ToolbarDockMode m_dockMode = ToolbarDockMode::Top;
    PageNavDirection m_pageNavDir = PageNavDirection::Horizontal;
    bool m_darkUi = false;
    bool m_cleanUi = false;
    bool m_elementsRight = false;
    bool m_shapeFillMode = false;
    ToolType m_colorTargetTool = ToolType::Pen;
    ToolType m_widthTargetTool = ToolType::Pen;
    QRect m_lastColorAnchor;
    QRect m_lastWidthAnchor;
    QString m_availableUpdateVersion;
    QStringList m_openTabPaths;

    QVector<PenPreset> m_presets;
    bool m_presetsRestored = false;
    int m_activePreset = 0;

    EditorTopBar* m_topBar = nullptr;
    EditorDetachedDock* m_detachedDock = nullptr;
    EditorPageCapsule* m_pageCapsule = nullptr;
    EditorIconButton* m_restoreBtn = nullptr;

    EditorColorPopover* m_colorPopover = nullptr;
    EditorFineWidthPopover* m_fineWidthPopover = nullptr;
    EditorPenPanel* m_penPanel = nullptr;
    EditorHighlighterPanel* m_hlPanel = nullptr;
    EditorEraserPanel* m_eraserPanel = nullptr;
    EditorFillPanel* m_fillPanel = nullptr;
    EditorLassoPanel* m_lassoPanel = nullptr;
    EditorShapesPanel* m_shapesPanel = nullptr;
    EditorLaserPanel* m_laserPanel = nullptr;
    EditorPaperPanel* m_paperPanel = nullptr;

    EditorElementsPanel* m_elementsPanel = nullptr;
    EditorPagesPanel* m_pagesPanel = nullptr;

    EditorContextCapsule* m_contextCapsule = nullptr;
    EditorEdgeMarkCapsule* m_edgeMarkCapsule = nullptr;
    EditorInlineTextBar* m_inlineTextBar = nullptr;

    EditorDropdownMenu* m_dropdownMenu = nullptr;
    EditorToast* m_toast = nullptr;
    EditorConfirmCard* m_confirmCard = nullptr;
    EditorSettingsModal* m_settingsModal = nullptr;
    EditorLibraryModal* m_libraryModal = nullptr;
    NewBoardSheet* m_boardSheet = nullptr;
    NewNotebookSheet* m_notebookSheet = nullptr;

    QWidget* m_openPopover = nullptr;
    QRect m_openPopoverAnchor;
    QRegion m_lastOverlayMask;
};

} // namespace finily
