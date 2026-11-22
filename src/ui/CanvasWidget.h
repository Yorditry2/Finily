#pragma once

#include "core/Types.h"
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

class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget* parent = nullptr);
    ~CanvasWidget() override;

    // State getters
    ToolType tool() const { return m_tool; }
    ShapeKind shapeKind() const { return m_shapeKind; }
    bool holdToShapeEnabled() const { return m_holdToShapeEnabled; }
    bool smartGuidesEnabled() const { return m_smartGuidesEnabled; }
    bool mathBeautifyEnabled() const { return m_mathBeautifyEnabled; }
    bool autoColorOperators() const { return m_autoColorOperators; }
    QColor operatorColor() const { return m_operatorColor; }
    bool swapPenButtons() const { return m_swapPenButtons; }
    bool hasSelectedShape() const { return !m_selectedShapeId.isEmpty(); }
    bool hasLassoSelection() const { return !m_selectedStrokeIds.isEmpty() || !m_selectedShapeIds.isEmpty(); }
    bool hasSelectedImage() const { return !m_selectedImageId.isEmpty(); }
    bool hasSelectedText() const { return !m_selectedTextId.isEmpty(); }
    bool isDrawingPolyline() const { return m_polylineActive; }
    int pdfPageCount() const;
    int currentPageIndex() const { return m_currentPageIndex; }
    int pageCount() const { return std::max(1, int(m_pages.size())); }
    QString currentFilePath() const { return m_currentFilePath; }
    EraserMode eraserMode() const { return m_eraserMode; }
    double eraserSize() const { return m_eraserSize; }
    QColor inkColor() const { return m_color; }
    double strokeSize() const { return m_size; }
    LinePattern linePattern() const { return m_linePattern; }
    double stabilizer() const { return m_stabilizer; }
    PressureProfile pressureProfile() const { return m_pressureProfile; }
    ThemeId themeId() const { return m_themeId; }
    BackgroundTemplate backgroundTemplate() const { return m_template; }
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
    void setHoldToShapeEnabled(bool enabled);
    void setSmartGuidesEnabled(bool enabled);
    void setMathBeautifyEnabled(bool enabled);
    void setAutoColorOperators(bool enabled);
    void setOperatorColor(const QColor& color);
    void setSwapPenButtons(bool swapped);
    void setEraserMode(EraserMode mode);
    void setEraserSize(double size);
    void setInkColor(const QColor& color);
    void setStrokeSize(double size);
    void setLinePattern(LinePattern pattern);
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
    void straightenSelection();
    void convertSelectionToText();
    bool saveSelectionAsTemplate(const QString& label);
    void clearSelection();
    bool pasteFromClipboard();
    bool importImageFile(const QString& filePath);
    bool importPdfFile(const QString& filePath);
    bool exportToPdf(const QString& filePath) const;

    // Viewport controls
    void zoomByFactor(double factor, const QPointF& anchorScreenPos);
    void zoomIn();
    void zoomOut();
    void resetViewport();

    // Multi-page notebook navigation
    void addPage();
    void nextPage();
    void prevPage();
    void goToPage(int index);
    void deleteCurrentPage();

    // History & persistence (.finily notebooks + autosave)
    void undo();
    void redo();
    void clearBoard();
    void newDocument();
    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    void saveAutosave() const;   // programa un autoguardado diferido (1.5 s)
    void flushAutosave();        // escribe ya el autoguardado pendiente
    void loadAutosave();

signals:
    void stateChanged();
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
    void leaveEvent(QEvent* event) override;

private:
    enum class InteractionMode {
        None,
        Draw,
        ShapeCreate,
        ShapeDragHandle,
        ShapeMove,
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
    void writeAutosaveNow() const;
    DocumentSnapshot currentSnapshot() const;
    void updateCursorShape();
    void triggerHoldToShape();
    void flushPendingBeautify();
    ShapeItem* findShapeById(const QString& id);
    const ShapeItem* findShapeById(const QString& id) const;

    void animateViewport(const Viewport& target);
    void animateZoom(double factor);
    void clearPenCursor();
    void updatePenCursor(const QPointF& world, bool visible);
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

    // Document & history
    QVector<PageData> m_pages;
    int m_currentPageIndex = 0;
    QVector<Stroke> m_strokes;
    QVector<ShapeItem> m_shapes;
    QVector<ImageItem> m_images;
    QVector<TextItem> m_texts;
    QVector<DocumentSnapshot> m_undoStack;
    QVector<DocumentSnapshot> m_redoStack;
    QString m_currentFilePath;
    bool m_dirty = false;
    QTimer* m_autosaveTimer = nullptr;

    // Active tool & styling
    ToolType m_tool = ToolType::Pen;
    ToolType m_lastWriteTool = ToolType::Pen;
    bool m_swapPenButtons = false;
    Qt::MouseButtons m_lastTabletButtons = Qt::NoButton;
    ShapeKind m_shapeKind = ShapeKind::Triangle;
    bool m_holdToShapeEnabled = true;
    bool m_smartGuidesEnabled = true;
    bool m_mathBeautifyEnabled = false; // Escritura libre por defecto; Modo Ecuación con ∑ (M)
    bool m_autoColorOperators = true;
    QColor m_operatorColor = QColor("#ef4444");
    EraserMode m_eraserMode = EraserMode::Stroke;
    double m_eraserSize = 18.0;
    QColor m_color = QColor("#f8fafc");
    double m_size = 2.6;
    LinePattern m_linePattern = LinePattern::Solid;
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
    DocumentSnapshot m_snapshotBeforeShapeEdit;
    bool m_polylineActive = false;
    qint64 m_lastPolylineTapMs = -1;
    QPointF m_lastPolylineTapScreen;
    QVector<QPointF> m_polylineVertices;
    QPointF m_polylineHoverWorld;
    bool m_polylineSnapClose = false;
    QVector<SmartGuide> m_activeGuides;

    // Lasso & pasted-image selection state
    QPolygonF m_lassoPolygon;
    QSet<QString> m_selectedStrokeIds;
    QSet<QString> m_selectedShapeIds;
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

    // Dual-layer backing cache for <0.5ms live drawing latency
    QImage m_staticCache;
    bool m_staticCacheDirty = true;
};

} // namespace finily
