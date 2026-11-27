#pragma once

#include "core/Types.h"
#include "engine/InkEngine.h"
#include "engine/LiveStroke.h"
#include "engine/NotebookDocument.h"
#include "engine/StickerLibrary.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QPolygonF>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <future>
#include <memory>

namespace finily {

class InlineTextEditor;
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
    void setTriangleVariant(int variant) {
        m_triangleVariant = std::clamp(variant, 0, 3);
        emit stateChanged();
    }
    void setPolygonSides(int sides);
    int quadVariant() const { return m_quadVariant; }
    void setQuadVariant(int variant) { m_quadVariant = std::clamp(variant, 0, 4); emit stateChanged(); }
    int solidKind() const { return m_solidKind; }
    void setSolidKind(int kind) { m_solidKind = std::clamp(kind, 0, 9); emit stateChanged(); }
    QString congruenceMarkSpec() const { return m_congruenceMarkSpec; }
    void setCongruenceMarkSpec(const QString& spec) { m_congruenceMarkSpec = spec; emit stateChanged(); }
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
    bool hasLassoSelection() const {
        return !m_selectedStrokeIds.isEmpty() || !m_selectedShapeIds.isEmpty()
            || !m_selectedTextIds.isEmpty() || !m_selectedImageIds.isEmpty();
    }
    bool hasSelectedStrokes() const { return !m_selectedStrokeIds.isEmpty(); }
    bool canGroupSelection() const;
    bool canUngroupSelection() const;
    bool hasSelectedImage() const { return !m_selectedImageId.isEmpty(); }
    bool hasExtractableImage() const { return selectedImage().has_value(); }
    std::optional<ImageItem> selectedImage() const;
    void insertExtractedContent(const QVector<ShapeItem>& shapes,
                                const QVector<TextItem>& texts,
                                bool selectAsGroup = true);
    bool hasSelectedText() const { return !m_selectedTextId.isEmpty(); }
    bool hasSelection() const;
    bool hasLockableSelection() const;
    bool isDrawingPolyline() const { return m_polylineActive; }
    int pdfPageCount() const { return m_doc.pdfPageCount(); }
    int documentPdfPageCount() const { return m_doc.documentPdfPageCount(); }
    int currentPageIndex() const { return m_doc.currentPageIndex(); }
    QImage pageThumbnail(int index, const QSize& size) const;
    int pageCount() const { return m_doc.pageCount(); }
    QString currentFilePath() const { return m_doc.filePath(); }
    void setCurrentFilePath(const QString& path) { m_doc.setFilePath(path); emit stateChanged(); }
    QRectF selectionScreenRect() const;
    bool isActivelyDragging() const { return m_mode != InteractionMode::None; }
    bool lassoRectMode() const { return m_lassoRectMode; }
    void setLassoRectMode(bool rectMode);
    int penTip() const { return m_penTip; }
    int highlighterTip() const { return m_highlighterTip; }
    EraserMode eraserMode() const { return m_eraserMode; }
    double eraserSize() const { return m_eraserSize; }
    bool eraserPreserveShapes() const { return m_eraserPreserveShapes; }
    bool eraserOnlyHighlight() const { return m_eraserOnlyHighlight; }
    void setEraserPreserveShapes(bool preserve);
    void setEraserOnlyHighlight(bool onlyHighlight);
    bool hasSelectedAxes() const;
    QString selectedAxesFunction() const;
    void setSelectedAxesFunction(const QString& expr);
    void createTableShape(int rows, int cols);
    bool insertStickerTemplate(const QString& stickerId, StickerStyle style);
    bool saveSelectionAsSticker(const QString& title, const QString& category = QStringLiteral("Mis ★"));
    QColor inkColor() const { return m_color; }
    double strokeSize() const { return m_size; }
    LinePattern linePattern() const { return m_linePattern; }
    LineEnding lineEnding() const { return m_lineEnding; }
    double stabilizer() const { return m_stabilizer; }
    double highlighterOpacity() const { return m_highlighterOpacity; }
    double pressureSensitivity() const { return m_pressureSensitivity; }
    PressureProfile pressureProfile() const { return m_pressureProfile; }
    ThemeId themeId() const { return m_doc.themeId(); }
    QColor customBackground() const { return m_doc.customBackground(); }
    BackgroundTemplate backgroundTemplate() const { return m_doc.currentTemplate(); }
    BackgroundTemplate defaultBackgroundTemplate() const { return m_doc.defaultTemplate(); }
    double templateScale() const { return m_doc.templateScale(); }
    CoverStyle coverStyle() const { return m_doc.coverStyle(); }
    PageFormat pageFormat() const { return m_doc.pageFormat(); }
    Viewport viewport() const { return m_doc.viewport(); }
    bool canUndo() const { return m_doc.canUndo(); }
    bool canRedo() const { return m_doc.canRedo(); }
    // Hay trabajo que no está en ningún .finily (el autoguardado no cuenta).
    bool hasUnsavedChanges() const { return m_doc.hasUnsavedChanges(); }
    bool currentPageHasContent() const { return m_doc.currentPageHasContent(); }
    bool hasAnyContent() const { return m_doc.hasAnyContent(); }

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
    void setStabilizer(double strength);            // [0.0, 1.0]
    void setHighlighterOpacity(double opacity);     // [0.10, 0.70]
    void setPressureSensitivity(double sensitivity);// [0.0, 1.0]
    void setPressureProfile(PressureProfile profile);
    void setThemeId(ThemeId themeId);
    void setBackgroundTemplate(BackgroundTemplate tpl);
    void setTemplateScale(double scale);
    void setPageFormat(PageFormat format);

    // Shape, polyline, lasso, image & PDF actions
    void stampCongruenceMarkAt(const QPointF& worldPos, const QString& markSpec);
    void highlightSegmentAt(const QPointF& worldPos,
                            const QColor& color = QColor(),
                            int style = 0);
    void finishActivePolyline(bool closeShape = false);
    void cancelActivePolyline();
    void deleteSelectedShape();
    void deleteSelection();
    void duplicateSelection();
    bool copySelection();
    void cutSelection();
    void groupSelection();
    void ungroupSelection();
    void flipSelectionHorizontal();
    void flipSelectionVertical();
    void bringSelectionToFront();
    void sendSelectionToBack();
    void insertVerticalSpaceAtSelection(double deltaY = 140.0);
    void cropSelectionToNewPage();
    void straightenSelection();
    void convertSelectionToText();
    void clearSelection();
    void cancelInteraction();
    bool pasteFromClipboard();
    bool importImageFile(const QString& filePath);
    bool importPdfFile(const QString& filePath, bool newNotebook = false);
    bool exportToPdf(const QString& filePath);

    // Viewport controls
    void zoomByFactor(double factor, const QPointF& anchorScreenPos);
    void zoomIn();
    void zoomOut();
    void resetViewport();

    // Multi-page notebook navigation
    void addPage();
    void insertPage(int index, std::optional<BackgroundTemplate> background = {});
    void insertPages(int index, const QVector<PageData>& pages);
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
    bool createNotebook(const QString& path, ThemeId theme, BackgroundTemplate background,
                        PageFormat format, double templateScale = 1.0, CoverStyle cover = {},
                        const QColor& customBackground = {});
    void startEphemeralDocument(ThemeId theme, BackgroundTemplate background,
                                PageFormat format, double templateScale = 1.0,
                                CoverStyle cover = {},
                                const QColor& customBackground = {});
    bool saveToFile(const QString& filePath, bool newFile = false, bool silent = false);
    bool loadFromFile(const QString& filePath);
    NotebookSession exportSession() const;
    void importSession(const NotebookSession& session);
    void saveAutosave();   // programa un autoguardado diferido (1.5 s)
    void flushAutosave();  // escribe ya el autoguardado pendiente
    void cancelPendingAutosave();
    void loadAutosave();

signals:
    void stateChanged();
    void pageStructureChanged();
    void selectionMenuRequested(const QPoint& globalPosition);
    void edgeMarkMenuRequested(const QPointF& screenPos, const QPointF& worldPos);
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
    void finishTextEditor(bool accept);
    void cancelPendingBeautify();
    NotebookDocument::DocumentToolSettings toolSettingsForDocument() const;
    void applyDocumentToolSettings(const NotebookDocument::DocumentToolSettings& settings);
    void waitForAutosave();
    bool preparePageChange();
    void afterPageLoaded(bool structureChanged);
    void writeAutosaveNow(bool async = true);
    void pushUndoSnapshot(const DocumentSnapshot& snapshot);
    void pushPageHistory();
    DocumentSnapshot currentSnapshot() const { return m_doc.currentSnapshot(); }

    // Selection helpers (CanvasSelection.cpp)
    void updateCursorShape();
    QSet<QString> selectedItemIds() const;
    void cloneObjects(const DocumentSnapshot& source, const QSet<QString>& selection,
                      const QPointF& offset, bool multiple);
    void expandLassoSelection(const QString& seed = {});
    QRectF lassoSelectionBounds() const;
    void changeSelectionGroup(bool group);
    void flipSelectionAxis(bool horizontal);
    void openInlineTextEditor(const TextItem& draft, const QPointF& anchorScreen,
                              const QString& existingTextId,
                              const QSet<QString>& strokesToReplace);

    // Input state machine helpers (CanvasInput.cpp)
    void beginInput(const QPointF& screenPos, double pressure, double tiltX, double tiltY,
                    bool isHardwarePen, bool isEraserTip, Qt::MouseButton button,
                    Qt::KeyboardModifiers mods);
    void moveInput(const QPointF& screenPos, double pressure, double tiltX, double tiltY,
                   bool isHardwarePen, bool isEraserTip, Qt::KeyboardModifiers mods);
    void endInput(const QPointF& screenPos, Qt::KeyboardModifiers mods);
    void startShapeEdit(const ShapeItem& shape, InteractionMode mode, int handle = -1);
    void startImageEdit(const ImageItem& image, int handle = -1);
    void startSelectionTransform(int handle, const QPointF& world);
    void selectTogether(const QString& id, const QPointF& world);
    void beginFillAt(const QPointF& world);
    bool tryBeginSelectTool(const QPointF& world);
    bool tryBeginLassoTool(const QPointF& world);
    bool tryBeginTextInput(const QPointF& screen, const QPointF& world, bool forceEdit);
    void beginTextEditAt(const QPointF& screen, const QPointF& world);
    void beginPolylineTap(const QPointF& screen, const QPointF& world);
    void beginShapeInput(const QPointF& world);
    void beginFreehandStroke(const QPointF& world, double pressure, double tiltX, double tiltY,
                             bool hardware, Qt::KeyboardModifiers mods);
    void moveText(const QPointF& world);
    void movePan(const QPointF& screen);
    void moveErase(const QPointF& world, Qt::KeyboardModifiers mods);
    void moveLassoDraw(const QPointF& world);
    void moveLassoTransform(const QPointF& world);
    void moveImageTransform(const QPointF& world);
    void moveShapeEdit(const QPointF& world);
    void moveShapeCreate(const QPointF& world, Qt::KeyboardModifiers mods);
    void moveFreehandStroke(const QPointF& world, double pressure, double tiltX, double tiltY,
                            Qt::KeyboardModifiers mods);
    void updateHoverCursors(const QPointF& world, bool eraser);
    bool tryCommitFreehandGeometry(Qt::KeyboardModifiers mods);
    void finishAutoGeometry(const ShapeItem& pulseShape);
    void applyEraseAt(const QPointF& worldPos, bool trimToIntersections = false);
    void updateLinkedPreview(const QVector<ShapeItem>& original, const QSet<QString>& edited);
    void triggerHoldToShape();
    void flushPendingBeautify();
    ShapeItem* findShapeById(const QString& id);
    const ShapeItem* findShapeById(const QString& id) const;

    // Viewport & overlays
    void animateViewport(const Viewport& target);
    void animateZoom(double factor);
    void clampFiniteViewport();
    QRectF activeFiniteSheetRect() const;
    void clearPenCursor();
    void updatePenCursor(const QPointF& world, bool visible);
    void openRadialMenu(const QPointF& centerScreen);
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

    // Document & history (single source of truth)
    NotebookDocument m_doc;
    QVector<PageData> m_copiedPages;
    QTimer* m_autosaveTimer = nullptr;
    std::future<void> m_autosaveFuture;
    QPointer<InlineTextEditor> m_textEditor;

    // Active tool & styling
    ToolType m_tool = ToolType::Pen;
    ToolType m_lastWriteTool = ToolType::Pen;
    bool m_swapPenButtons = false;
    Qt::MouseButtons m_lastTabletButtons = Qt::NoButton;
    ShapeKind m_shapeKind = ShapeKind::Triangle;
    int m_triangleVariant = 0; // 0 libre, 1 rectángulo, 2 isósceles, 3 equilátero
    int m_polygonSides = 6;    // lados del polígono regular (herramienta Figuras > Polígono)
    // 0 polígono regular, 1 paralelogramo, 2 trapecio isósceles, 3 trapecio rectángulo, 4 trapezoide
    int m_quadVariant = 0;
    int m_solidKind = 0;       // 0..9 sólidos 3D
    QString m_congruenceMarkSpec;
    ShadePattern m_shadePattern = ShadePattern::Solid;
    bool m_holdToShapeEnabled = true;
    bool m_smartGuidesEnabled = true;
    bool m_mathBeautifyEnabled = false;
    bool m_autoColorOperators = true;
    QColor m_operatorColor = QColor("#ef4444");
    int m_cursorStyle = 0;      // 0 = Lapicito, 1 = Aro, 2 = Cruz
    int m_penTip = 1;           // 0 = Bolígrafo, 1 = Pluma, 2 = Plumón, 3 = Pincel
    int m_highlighterTip = 0;   // 0 = Biselado, 1 = Redondo, 2 = Recto
    EraserMode m_eraserMode = EraserMode::Stroke;
    double m_eraserSize = 18.0;
    bool m_eraserPreserveShapes = false;
    bool m_eraserOnlyHighlight = false;
    QColor m_color = QColor("#f8fafc");
    double m_size = 2.6;
    LinePattern m_linePattern = LinePattern::Solid;
    LineEnding m_lineEnding = LineEnding::None;
    double m_stabilizer = 0.15;
    double m_highlighterOpacity = 0.25;
    double m_pressureSensitivity = 0.5;
    PressureProfile m_pressureProfile = PressureProfile::Light;

    // Live interaction state
    InteractionMode m_mode = InteractionMode::None;
    bool m_tabletActive = false;
    bool m_spaceHeld = false;
    bool m_hasActiveStroke = false;
    Stroke m_activeStroke;
    QRectF m_liveStrokeBounds;
    bool m_liveInkPending = false;
    StrokePoint m_rawStartPoint;
    std::unique_ptr<VelocityGaussianStabilizer> m_stabilizerFilter;

    // Hold-to-shape state
    QTimer* m_holdTimer = nullptr;
    QPointF m_holdAnchorWorld;
    QPointF m_holdLastWorld;
    bool m_holdSnappedToShape = false;

    // Deferred smart handwriting beautification
    QTimer* m_beautifyTimer = nullptr;
    QRectF m_pendingBeautifyBox;
    QSet<QString> m_pendingBeautifyIds;

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
    QPointF m_lassoPreviewDelta;
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
    QImage m_hlBackground;
    QImage m_hlInk;
    LiveInkRenderer m_liveInk;
    InkSmoother m_inkSmoother;
    QImage m_liveInkLayer;
    bool m_staticCacheDirty = true;
};

} // namespace finily
