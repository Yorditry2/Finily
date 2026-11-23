#pragma once

#include "core/Types.h"
#include "engine/InkEngine.h"
#include "engine/LiveStroke.h"
#include <QElapsedTimer>
#include <QImage>
#include <QPolygonF>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <QVariantAnimation>
#include <algorithm>
#include <memory>

namespace finily {

class VelocityGaussianStabilizer;

struct SelectionStyle {
    std::optional<QColor> color;
    std::optional<double> width;
    std::optional<LinePattern> pattern;
    std::optional<LineEnding> ending;
    std::optional<QString> label;
    std::optional<QColor> fillColor;
    std::optional<ShadePattern> fillPattern;
    std::optional<bool> filled;
};

class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget* parent = nullptr);
    ~CanvasWidget() override;

    // State getters
    ToolType tool() const { return m_tool; }
    ShapeKind shapeKind() const { return m_shapeKind; }
    int polygonSides() const { return m_polygonSides; }
    int triangleVariant() const { return m_triangleVariant; }
    void setTriangleVariant(int variant) { m_triangleVariant = std::clamp(variant, 0, 3); emit stateChanged(); }
    void setPolygonSides(int sides);
    int quadVariant() const { return m_quadVariant; }
    void setQuadVariant(int variant) { m_quadVariant = std::clamp(variant, 0, 4); emit stateChanged(); }
    int solidKind() const { return m_solidKind; }
    void setSolidKind(int kind) { m_solidKind = std::clamp(kind, 0, 9); emit stateChanged(); }
    bool isSelectionLocked() const;
    void toggleLockSelection();
    SelectionStyle selectionStyle() const;
    void setSelectionStyle(const SelectionStyle& style);
    bool holdToShapeEnabled() const { return m_holdToShapeEnabled; }
    bool smartGuidesEnabled() const { return m_smartGuidesEnabled; }
    bool mathBeautifyEnabled() const { return m_mathBeautifyEnabled; }
    bool autoColorOperators() const { return m_autoColorOperators; }
    QColor operatorColor() const { return m_operatorColor; }
    int cursorStyle() const { return m_cursorStyle; } // 0 = Lapicito, 1 = Aro, 2 = Cruz
    bool swapPenButtons() const { return m_swapPenButtons; }
    bool hasSelectedShape() const { return !m_selectedShapeId.isEmpty(); }
    bool hasLassoSelection() const { return !m_selectedStrokeIds.isEmpty() || !m_selectedShapeIds.isEmpty()
        || !m_selectedTextIds.isEmpty() || !m_selectedImageIds.isEmpty(); }
    bool hasSelectedStrokes() const { return !m_selectedStrokeIds.isEmpty(); }
    bool canGroupSelection() const;
    bool canUngroupSelection() const;
    bool hasSelectedImage() const { return !m_selectedImageId.isEmpty(); }
    bool hasSelectedText() const { return !m_selectedTextId.isEmpty(); }
    bool hasSelection() const;
    bool hasLockableSelection() const;
    bool isDrawingPolyline() const { return m_polylineActive; }
    int pdfPageCount() const;
    int documentPdfPageCount() const;
    int currentPageIndex() const { return m_currentPageIndex; }
    QImage pageThumbnail(int index, const QSize& size) const;
    int pageCount() const { return std::max(1, int(m_pages.size())); }
    QString currentFilePath() const { return m_currentFilePath; }
    void setCurrentFilePath(const QString& path) { m_currentFilePath = path; emit stateChanged(); }
    QRectF selectionScreenRect() const;
    bool isActivelyDragging() const { return m_mode != InteractionMode::None; }
    bool lassoRectMode() const { return m_lassoRectMode; }
    void setLassoRectMode(bool rectMode);
    int penTip() const { return m_penTip; }
    int highlighterTip() const { return m_highlighterTip; }
    EraserMode eraserMode() const { return m_eraserMode; }
    double eraserSize() const { return m_eraserSize; }
    QColor inkColor() const { return m_color; }
    double strokeSize() const { return m_size; }
    LinePattern linePattern() const { return m_linePattern; }
    LineEnding lineEnding() const { return m_lineEnding; }
    double stabilizer() const { return m_stabilizer; }
    PressureProfile pressureProfile() const { return m_pressureProfile; }
    ThemeId themeId() const { return m_themeId; }
    BackgroundTemplate backgroundTemplate() const;
    PageFormat pageFormat() const { return m_pageFormat; }
    Viewport viewport() const { return m_viewport; }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    // Hay trabajo que no está en ningún .finily (el autoguardado no cuenta).
    bool hasUnsavedChanges() const;
    bool currentPageHasContent() const;

    // State setters
    void setTool(ToolType tool);
    void setShapeKind(ShapeKind kind);
    ShadePattern shadePattern() const { return m_shadePattern; }
    void setShadePattern(ShadePattern pattern);
    void setHoldToShapeEnabled(bool enabled);
    void setSmartGuidesEnabled(bool enabled);
    void setMathBeautifyEnabled(bool enabled);
    void setAutoColorOperators(bool enabled);
    void setOperatorColor(const QColor& color);
    void setCursorStyle(int style);
    void setPenTip(int tip);
    void setHighlighterTip(int tip);
    void setSwapPenButtons(bool swapped);
    void setEraserMode(EraserMode mode);
    void setEraserSize(double size);
    void setInkColor(const QColor& color);
    void setStrokeSize(double size);
    void setLinePattern(LinePattern pattern);
    void setLineEnding(LineEnding ending);
    void setStabilizer(double strength); // [0.0, 1.0]
    void setPressureProfile(PressureProfile profile);
    void setThemeId(ThemeId themeId);
    void setBackgroundTemplate(BackgroundTemplate tpl);
    void setPageFormat(PageFormat format);

    // Shape, polyline, lasso, image & PDF actions
    void finishActivePolyline(bool closeShape = false);
    void cancelActivePolyline();
    void deleteSelectedShape();
    void deleteSelection();
    void duplicateSelection();
    bool copySelection();
    void cutSelection();
    void groupSelection();
    void ungroupSelection();
    void straightenSelection();
    void convertSelectionToText();
    bool saveSelectionAsTemplate(const QString& label);
    void clearSelection();
    void cancelInteraction();
    bool pasteFromClipboard();
    bool importImageFile(const QString& filePath);
    bool importPdfFile(const QString& filePath, bool newNotebook = false);
    bool exportToPdf(const QString& filePath) const;

    // Viewport controls
    void zoomByFactor(double factor, const QPointF& anchorScreenPos);
    void zoomIn();
    void zoomOut();
    void resetViewport();

    // Multi-page notebook navigation
    void addPage();
    void insertPage(int index, std::optional<BackgroundTemplate> background = {});
    bool movePages(QVector<int> indices, int destination);
    void duplicatePages(QVector<int> indices);
    bool copyPages(QVector<int> indices);
    void pastePages(int index);
    bool hasCopiedPages() const { return !m_copiedPages.isEmpty(); }
    void deletePages(QVector<int> indices);
    void nextPage();
    void prevPage();
    void goToPage(int index);
    void deleteCurrentPage();

    // History & persistence (.finily notebooks + autosave)
    void undo();
    void redo();
    void clearBoard();
    void newDocument();
    bool saveToFile(const QString& filePath, bool newFile = false);
    bool loadFromFile(const QString& filePath);
    void saveAutosave() const;   // programa un autoguardado diferido (1.5 s)
    void flushAutosave();        // escribe ya el autoguardado pendiente
    void loadAutosave();

signals:
    void stateChanged();
    void pageStructureChanged();
    void selectionMenuRequested(const QPoint& globalPosition);
    void viewportChanged(double zoom);
    void toastRequested(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class InteractionMode {
        None,
        Draw,
        ShapeCreate,
        ShapeDragHandle,
        ShapeMove,
        ShapeScale,
        LassoDraw,
        LassoMove,
        LassoScale,
        LassoRotate,
        ImageMove,
        ImageResize,
        TextMove,
        LaserTrail,
        Erase,
        Pan
    };

    QPointF screenToWorld(const QPointF& screen) const;
    void rebuildStaticCache();
    void invalidateStaticCache();
    void beginInput(const QPointF& screenPos, double pressure, double tiltX, double tiltY, bool isHardwarePen, bool isEraserTip, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void moveInput(const QPointF& screenPos, double pressure, double tiltX, double tiltY, bool isHardwarePen, bool isEraserTip, Qt::KeyboardModifiers mods);
    void endInput(const QPointF& screenPos, Qt::KeyboardModifiers mods);
    void applyEraseAt(const QPointF& worldPos);
    void pushUndoSnapshot(const DocumentSnapshot& snapshot);
    void pushPageHistory();
    bool preparePageChange();
    void loadWorkingPage(int index, bool structureChanged);
    void insertPages(int index, const QVector<PageData>& pages);
    void writeAutosaveNow() const;
    DocumentSnapshot currentSnapshot() const;
    void updateCursorShape();
    QSet<QString> selectedItemIds() const;
    void cloneObjects(const DocumentSnapshot& source, const QSet<QString>& selection,
                      const QPointF& offset, bool multiple);
    void expandLassoSelection(const QString& seed = {});
    QRectF lassoSelectionBounds() const;
    void changeSelectionGroup(bool group);
    void updateLinkedPreview(const QVector<ShapeItem>& original, const QSet<QString>& edited);
    void triggerHoldToShape();
    void flushPendingBeautify();
    ShapeItem* findShapeById(const QString& id);
    const ShapeItem* findShapeById(const QString& id) const;

    void animateViewport(const Viewport& target);
    void animateZoom(double factor);
    void clearPenCursor();
    void updatePenCursor(const QPointF& world, bool visible);
    void updateRadialHover(const QPointF& screenPos);
    void commitRadialSelection(bool quickTap);
    QRect shapePulseRect() const;
    void stopShapePulse();
    void switchTabletTool(ToolType tool);

    QVariantAnimation* m_zoomAnimation = nullptr;
    Viewport m_zoomStart;
    Viewport m_zoomTarget;
    QVariantAnimation* m_shapePulseAnimation = nullptr;
    double m_shapePulseAlpha = 0.0;
    ShapeItem m_shapePulseShape;
    QPointF m_hoverWorldPos;
    bool m_showPenCursor = false;
    QRect m_penCursorRect;
    // Menú radial rápido (doble toque rápido de P o botón del lápiz al aire)
    bool m_radialOpen = false;
    QPointF m_radialCenter;
    QPointF m_radialCursorPos;
    qint64 m_lastPenTriggerMs = 0;
    int m_radialHoveredColor = -1;
    int m_radialHoveredTool = -1;

    // Document & history
    QVector<PageData> m_pages;
    int m_currentPageIndex = 0;
    QVector<Stroke> m_strokes;
    QVector<ShapeItem> m_shapes;
    QVector<ImageItem> m_images;
    QVector<TextItem> m_texts;
    struct HistoryEntry {
        int pageIndex;
        DocumentSnapshot content;
        QVector<PageData> pages;
    };
    void restoreHistoryEntry(QVector<HistoryEntry>& source, QVector<HistoryEntry>& destination);
    QVector<HistoryEntry> m_undoStack;
    QVector<HistoryEntry> m_redoStack;
    QVector<PageData> m_copiedPages;
    QString m_currentFilePath;
    bool m_dirty = false;
    QTimer* m_autosaveTimer = nullptr;

    // Active tool & styling
    ToolType m_tool = ToolType::Pen;
    ToolType m_lastWriteTool = ToolType::Pen;
    bool m_swapPenButtons = false;
    Qt::MouseButtons m_lastTabletButtons = Qt::NoButton;
    ShapeKind m_shapeKind = ShapeKind::Triangle;
    int m_triangleVariant = 0; // 0 libre, 1 rectángulo, 2 isósceles, 3 equilátero
    int m_polygonSides = 6; // lados del polígono regular (herramienta Figuras > Polígono)
    int m_quadVariant = 0;  // 0 polígono regular, 1 paralelogramo, 2 trapecio isósceles, 3 trapecio rectángulo, 4 trapezoide
    int m_solidKind = 0;    // 0..9 sólidos 3D (Cubo, Prisma, Cilindro, Cono, Esfera, Pirámide, Tetraedro, Octaedro, TroncoCono, TroncoPirámide)
    ShadePattern m_shadePattern = ShadePattern::Solid;
    bool m_holdToShapeEnabled = true;
    bool m_smartGuidesEnabled = true;
    bool m_mathBeautifyEnabled = false; // Escritura libre por defecto; Modo Ecuación con ∑ (M)
    bool m_autoColorOperators = true;
    QColor m_operatorColor = QColor("#ef4444");
    int m_cursorStyle = 0; // 0 = Lapicito, 1 = Aro, 2 = Cruz
    int m_penTip = 1; // 0 = Bolígrafo, 1 = Pluma, 2 = Plumón, 3 = Pincel
    int m_highlighterTip = 0; // 0 = Biselado, 1 = Redondo, 2 = Recto
    EraserMode m_eraserMode = EraserMode::Stroke;
    double m_eraserSize = 18.0;
    QColor m_color = QColor("#f8fafc");
    double m_size = 2.6;
    LinePattern m_linePattern = LinePattern::Solid;
    LineEnding m_lineEnding = LineEnding::None;
    double m_stabilizer = 0.15;
    PressureProfile m_pressureProfile = PressureProfile::Light;
    ThemeId m_themeId = ThemeId::DarkBoard;
    BackgroundTemplate m_template = BackgroundTemplate::Dots;
    PageFormat m_pageFormat = PageFormat::Infinite;
    Viewport m_viewport;

    // Live interaction state
    InteractionMode m_mode = InteractionMode::None;
    bool m_tabletActive = false;
    bool m_spaceHeld = false;
    bool m_hasActiveStroke = false;
    Stroke m_activeStroke;
    StrokePoint m_rawStartPoint;
    std::unique_ptr<VelocityGaussianStabilizer> m_stabilizerFilter;

    // Hold-to-shape state
    QTimer* m_holdTimer = nullptr;
    QPointF m_holdAnchorWorld;
    QPointF m_holdLastWorld;
    bool m_holdSnappedToShape = false;

    // Deferred smart handwriting beautification (waits ~420ms or until next stroke moves away)
    QTimer* m_beautifyTimer = nullptr;
    int m_pendingBeautifyStart = -1;
    QRectF m_pendingBeautifyBox;

    // Shape creation / editing / polyline / smart guides state
    bool m_hasActiveShape = false;
    ShapeItem m_activeShape;
    QPointF m_shapeCreateOrigin;
    QString m_selectedShapeId;
    int m_activeHandleIndex = -1;
    QPointF m_shapeDragStartWorld;
    QVector<QPointF> m_shapeDragOriginalVertices;
    ShapeKind m_shapeDragOriginalKind = ShapeKind::Line;
    DocumentSnapshot m_snapshotBeforeShapeEdit;
    bool m_polylineActive = false;
    qint64 m_lastPolylineTapMs = -1;
    QPointF m_lastPolylineTapScreen;
    QVector<QPointF> m_polylineVertices;
    QPointF m_polylineHoverWorld;
    bool m_polylineSnapClose = false;
    QVector<SmartGuide> m_activeGuides;
    bool m_linkedPreview = false;

    // Lasso & pasted-image selection state
    bool m_lassoRectMode = false;
    QPointF m_lassoRectOrigin;
    QPolygonF m_lassoPolygon;
    QSet<QString> m_selectedStrokeIds;
    QSet<QString> m_selectedShapeIds;
    QSet<QString> m_selectedTextIds;
    QSet<QString> m_selectedImageIds;
    QRectF m_lassoBounds;
    QPointF m_lassoDragStartWorld;
    QPointF m_lassoTransformCenter;
    double m_lassoStartAngle = 0.0;
    int m_lassoHandleIndex = -1;
    DocumentSnapshot m_snapshotBeforeLassoTransform;
    QString m_selectedImageId;
    int m_imageHandleIndex = -1;
    QRectF m_imageOriginalRect;
    QPointF m_imageDragStartWorld;
    DocumentSnapshot m_snapshotBeforeImageEdit;

    // Text & temporary Laser pointer state
    QString m_selectedTextId;
    QPointF m_textDragStartWorld;
    QPointF m_textOriginalPos;
    DocumentSnapshot m_snapshotBeforeTextEdit;
    struct LaserSample {
        QPointF pos;
        qint64 timestampMs = 0;
        bool breakBefore = false;
    };
    QVector<LaserSample> m_laserTrail;
    QTimer* m_laserTimer = nullptr;

    // Eraser sweep state (1 Undo per sweep)
    DocumentSnapshot m_snapshotBeforeErase;
    bool m_erasedAnything = false;
    bool m_showEraserCursor = false;
    QPointF m_eraserWorldPos;

    // Pan state
    QPointF m_panStartScreen;
    QPointF m_panStartViewport;
    bool m_rightClickOnSelection = false;

    // Dual-layer backing cache for <0.5ms live drawing latency
    QImage m_staticCache;
    LiveInkRenderer m_liveInk;
    InkSmoother m_inkSmoother;
    QImage m_liveInkLayer;
    bool m_staticCacheDirty = true;
};

} // namespace finily
