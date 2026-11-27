#pragma once

#include "core/Types.h"
#include "ui/FloatingToolbarInternal.h"

#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPaintEvent;
class QPropertyAnimation;
class QResizeEvent;
class QTabletEvent;
class QTimer;
class QToolButton;

namespace finily {

class CanvasWidget;
class FilePopover;
class PaperPopover;
class ShapesPopover;
class StickersPopover;
class StrokePopover;
class ThumbnailPanel;

// Barra flotante superpuesta al CanvasWidget con cápsulas GlassCapsule
// inmunes al tema del sistema y popovers como hijos directos (Wayland-safe).
class FloatingToolbar : public QWidget {
    Q_OBJECT

public:
    using DockPos = ToolbarDockPos;
    using PageNavDirection = finily::PageNavDirection;

    struct TabEntry {
        QString title;
        bool dirty = false;
        bool active = false;
    };

    explicit FloatingToolbar(CanvasWidget* canvas, QWidget* parent = nullptr);

    void refreshFromCanvas();
    void setNotebookTabs(const QVector<TabEntry>& tabs);

    void toggleCleanUi();
    bool isCleanUi() const { return m_cleanUi; }

    void selectTool(ToolType tool);
    void selectPreset(int index); // 0..4
    QString presetShortcut(int index) const;
    PageNavDirection pageNavDirection() const { return m_pageNavDir; }
    void setPageNavDirection(PageNavDirection dir);
    void openStickersPopover();

    void importPdfDialog();
    void exportPdfDialog();

    void newDocumentAction();
    void openNotebookDialog();
    void saveNotebookAction();
    void saveNotebookAsDialog();
    bool confirmDiscardChanges(const QString& action);

    void showToast(const QString& message);
    void setUpdateAvailable(const QString& version);

signals:
    void homeRequested();
    void renameRequested(const QString& newName);
    void checkForUpdatesRequested();
    void vectorizeSelectedImageRequested();
    void extractSelectedImageRequested();
    void configureAiApiRequested();
    void newTabRequested();
    void switchTabRequested(int index);
    void closeTabRequested(int index);
    void pageNavDirectionChanged(finily::FloatingToolbar::PageNavDirection dir);
    void presetsChanged();

public slots:
    void setDocumentTitle(const QString& title, bool unsaved);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ToolbarCanvasState {
        ToolType tool = ToolType::Pen;
        QRgb inkColor = 0;
        double strokeSize = 0.0;
        LinePattern linePattern = LinePattern::Solid;
        LineEnding lineEnding = LineEnding::None;
        double stabilizer = 0.0;
        double highlighterOpacity = 0.0;
        EraserMode eraserMode = EraserMode::Stroke;
        double eraserSize = 0.0;
        bool eraserPreserveShapes = false;
        bool eraserOnlyHighlight = false;
        double pressureSensitivity = 0.0;
        ThemeId themeId = ThemeId::DarkBoard;
        BackgroundTemplate backgroundTemplate = BackgroundTemplate::Blank;
        PageFormat pageFormat = PageFormat::Infinite;
        double templateScale = 1.0;
        bool canUndo = false;
        bool canRedo = false;
        double zoom = 1.0;
        ShapeKind shapeKind = ShapeKind::Line;
        int polygonSides = 6;
        int triangleVariant = 0;
        int quadVariant = 0;
        int solidKind = 0;
        QString congruenceMarkSpec;
        bool holdToShapeEnabled = true;
        bool smartGuidesEnabled = true;
        bool mathBeautifyEnabled = false;
        bool swapPenButtons = false;
        int cursorStyle = 0;
        bool drawingPolyline = false;
        bool hasSelection = false;
        bool hasSelectedShape = false;
        bool hasSelectedAxes = false;
        bool hasSelectedStrokes = false;
        bool hasSelectedImage = false;
        bool hasSelectedText = false;
        bool hasLassoSelection = false;
        bool canGroupSelection = false;
        bool canUngroupSelection = false;
        bool activelyDragging = false;
        bool lassoRectMode = false;
        int penTip = 0;
        int highlighterTip = 0;
        ShadePattern shadePattern = ShadePattern::Soft;
        int pdfPageCount = 0;
        int currentPageIndex = 0;
        int pageCount = 1;
        bool cleanUi = false;
        DockPos dockPos = DockPos::Izquierda;
        PageNavDirection pageNavDir = PageNavDirection::Horizontal;
        bool valid = false;

        bool operator==(const ToolbarCanvasState&) const = default;
    };

    void initPresets();
    void loadPresets();
    void savePresets();
    void buildUtilityPill();
    void buildTabBarPill();
    void buildTopRight();
    void buildDock();
    void layoutDock();
    void buildZoomPill();
    void buildPagePill();
    void buildRestoreButton();
    void buildContextPill();
    void showSelectionMenu(const QPoint& globalPosition);
    void editSelectionProperties();
    void buildEdgeMarkPill();
    void showEdgeMarkPill(const QPointF& screenPos, const QPointF& worldPos);

    void applyThemeStylesheet();
    QString buildStylesheet(const ThemeConfig& theme) const;
    void refreshIcons();
    void updateTitleDot();

    void positionChildren();
    QPoint popoverPosition(const GlassCapsule* popover, const QWidget* anchor) const;
    void positionOpenPopover();
    void updateOverlayMask();
    void openPopover(GlassCapsule* popover, QWidget* anchor);
    void closePopovers();

    void refreshShadowColors();
    void loadDockPosition();
    void setDockPosition(DockPos pos);

    void setColor(const QColor& color);
    void syncActivePresetFromCanvas();
    void updateStationery();
    void updateZoomLabel();
    void showFavoritesMenu(QWidget* anchor);
    void editPresetMenu(int index, const QPoint& globalPos);
    void saveCurrentToPreset(int index);
    void updatePageNavButtons();
    void updatePresetButtons();

    QPointer<CanvasWidget> m_canvas;

    ThemeId m_theme = ThemeId::DarkBoard;
    QVector<PenPreset> m_presets;
    bool m_presetsRestored = false;
    QString m_lastPresetsSig;
    int m_activePreset = 0;
    QVector<QColor> m_palette;
    bool m_cleanUi = false;
    bool m_titleUnsaved = false;
    bool m_updatingUi = false;
    ToolbarCanvasState m_lastCanvasState;

    // Pill superior izquierda (utilidades) y barra de pestañas
    GlassCapsule* m_pill = nullptr;
    GlassCapsule* m_tabBarPill = nullptr;
    QHBoxLayout* m_tabBarLayout = nullptr;
    int m_tabCount = 1;
    GlassCapsule* m_topRight = nullptr;
    QToolButton* m_pagesBtn = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QLabel* m_titleDot = nullptr;
    QToolButton* m_homeBtn = nullptr;
    QToolButton* m_undoBtn = nullptr;
    QToolButton* m_redoBtn = nullptr;
    QToolButton* m_paperBtn = nullptr;
    QToolButton* m_fileBtn = nullptr;
    QToolButton* m_cleanBtn = nullptr;

    // Dock de herramientas y colores rápidos
    GlassCapsule* m_dock = nullptr;
    DockPos m_dockPos = DockPos::Izquierda;
    QButtonGroup* m_toolGroup = nullptr;
    QVector<QToolButton*> m_writingBtns;
    QVector<ToolType> m_writingKinds;
    QWidget* m_dockDivider = nullptr;
    QWidget* m_trayDivider = nullptr;
    QVector<ColorDotButton*> m_trayDots;
    QVector<QToolButton*> m_secondaryBtns;
    QVector<ToolType> m_secondaryKinds;
    QVector<QString> m_secondaryIcons;
    QToolButton* m_shapeBtn = nullptr;
    QToolButton* m_stickersBtn = nullptr;

    // Popovers y panel de miniaturas (hijos directos de FloatingToolbar)
    StrokePopover* m_strokePopover = nullptr;
    PaperPopover* m_paperPopover = nullptr;
    ShapesPopover* m_shapesPopover = nullptr;
    StickersPopover* m_stickersPopover = nullptr;
    FilePopover* m_filePopover = nullptr;
    ThumbnailPanel* m_thumbPanel = nullptr;

    // Pill contextual de selección / polilínea
    GlassCapsule* m_contextPill = nullptr;
    QToolButton* m_finishPolylineBtn = nullptr;
    QToolButton* m_closePolylineBtn = nullptr;
    QToolButton* m_ctxBeautifyBtn = nullptr;
    QToolButton* m_ctxAxesFuncBtn = nullptr;
    QToolButton* m_ctxDuplicateBtn = nullptr;
    QToolButton* m_ctxDeleteBtn = nullptr;
    QToolButton* m_selectionActions = nullptr;

    // Pill contextual de marcas en arista
    GlassCapsule* m_edgeMarkPill = nullptr;
    QVector<ColorDotButton*> m_edgeMarkDots;
    QPointF m_edgeMarkWorldPos;

    // Pills inferiores (zoom y páginas) y botón de restauración
    GlassCapsule* m_zoomPill = nullptr;
    QToolButton* m_zoomOutBtn = nullptr;
    QToolButton* m_zoomResetBtn = nullptr;
    QToolButton* m_zoomInBtn = nullptr;

    GlassCapsule* m_pagePill = nullptr;
    QToolButton* m_pagePrevBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    QToolButton* m_pageNextBtn = nullptr;
    QToolButton* m_pageAddBtn = nullptr;
    QToolButton* m_restoreBtn = nullptr;

    GlassCapsule* m_openPopover = nullptr;
    QWidget* m_popoverAnchor = nullptr;

    // Toast flotante (usa QGraphicsOpacityEffect, sin QGraphicsDropShadowEffect)
    GlassCapsule* m_toastPill = nullptr;
    QLabel* m_toastLabel = nullptr;
    QTimer* m_toastTimer = nullptr;
    QPropertyAnimation* m_toastAnim = nullptr;

    QVector<GlassCapsule*> m_capsules;
    QRegion m_lastOverlayMask;
    QPointer<QPropertyAnimation> m_popoverAnim;
    QPointer<QPropertyAnimation> m_pillAnim;

    QVector<QPair<QToolButton*, QString>> m_studioUtilSpecs;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QVector<QPair<QToolButton*, QString>> m_ctxIconSpecs;

    PageNavDirection m_pageNavDir = PageNavDirection::Horizontal;
};

} // namespace finily
