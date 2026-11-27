#pragma once

#include "core/Types.h"
#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPointer>
#include <QRect>
#include <QVector>

namespace finily {

class CanvasWidget;

// Barra común de grosor sincronizado con la barra (3 muestras + lector mm + deslizador fino).
class EditorWidthSection : public QWidget {
    Q_OBJECT

public:
    explicit EditorWidthSection(ToolType tool, double minMm, double maxMm, double stepMm,
                                QWidget* parent = nullptr);

    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark);

signals:
    void layoutHeightChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QRectF badgeRect() const;
    QRectF slotRect(int i) const;
    void toggleFineSlider();

    ToolType m_tool;
    double m_minMm;
    double m_maxMm;
    double m_stepMm;
    QPointer<CanvasWidget> m_canvas;
    std::array<double, 3> m_presets{1.2, 2.4, 4.5};
    int m_activeSlot = 1;
    double m_currentMm = 2.4;
    QColor m_inkColor{"#18181B"};
    int m_hoverSlot = -1;
    bool m_hoverBadge = false;
    bool m_expanded = false;
    bool m_dark = false;
    EditorPalette m_pal;
    EditorSlider* m_slider = nullptr;
};

// Fila común de 6 colores + selector arcoíris sincronizada con la barra.
class EditorColorRow : public QWidget {
    Q_OBJECT

public:
    explicit EditorColorRow(ToolType tool, QWidget* parent = nullptr);

    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark);

signals:
    void customColorRequested(ToolType tool, const QRect& anchorInWindow);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QRectF swatchRect(int idx) const;

    ToolType m_tool;
    QPointer<CanvasWidget> m_canvas;
    QVector<QColor> m_colors;
    int m_activeIdx = 0;
    bool m_darkSheet = false;
    EditorPalette m_pal;
};

// 1. Panel de Pluma (318 px): Pluma, Bolígrafo, Grafito, Tiza + grosor + colores.
class EditorPenPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPenPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void libraryRequested(const QString& section);
    void customColorRequested(ToolType tool, const QRect& anchorRect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF linkRect() const;
    QRectF tipRowRect(int idx) const;
    void relayout();

    QPointer<CanvasWidget> m_canvas;
    EditorWidthSection* m_widthSection = nullptr;
    EditorColorRow* m_colorRow = nullptr;
    int m_activeTip = 1;
    QColor m_activeColor{"#18181B"};
    int m_hoverTip = -1;
    bool m_hoverLink = false;
};

// 2. Panel de Resaltador (296 px): Opacidad + grosor + colores.
class EditorHighlighterPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorHighlighterPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void customColorRequested(ToolType tool, const QRect& anchorRect);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void relayout();

    QPointer<CanvasWidget> m_canvas;
    EditorSlider* m_opacitySlider = nullptr;
    EditorWidthSection* m_widthSection = nullptr;
    EditorColorRow* m_colorRow = nullptr;
    int m_opacityPct = 40;
};

// 3. Panel de Borrador (308 px): Trazo/Parcial + grosor + Tinta/Resaltador/Figuras + Borrar página.
class EditorEraserPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorEraserPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void clearPageRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void relayout();

    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_modeSeg = nullptr;
    EditorWidthSection* m_widthSection = nullptr;
    EditorChipButton* m_inkChip = nullptr;
    EditorChipButton* m_hlChip = nullptr;
    EditorChipButton* m_shapeChip = nullptr;
    EditorChipButton* m_clearBtn = nullptr;
};

// 4. Panel de Relleno (332 px): Región | Tramo/Arco + 6 tramas + opacidad.
class EditorFillPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorFillPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;
    bool isSegmentMode() const { return m_segmentMode; }

signals:
    void fillSubModeChanged(bool segmentMode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF patternCardRect(int idx) const;

    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_modeSeg = nullptr;
    EditorSlider* m_opacitySlider = nullptr;
    bool m_segmentMode = false;
    ShadePattern m_pattern = ShadePattern::Soft;
    QColor m_activeColor{"#2563EB"};
    int m_opacityPct = 30;
    int m_hoverCard = -1;
};

// 5. Panel de Lazo (276 px): Libre | Rectángulo + Escritura/Figuras/Texto e imágenes.
class EditorLassoPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorLassoPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_modeSeg = nullptr;
    EditorChipButton* m_inkChip = nullptr;
    EditorChipButton* m_shapesChip = nullptr;
    EditorChipButton* m_mediaChip = nullptr;
};

// 6. Panel de Figuras (304 px): 4 categorías + variantes al 2.º toque + «Más figuras ›».
class EditorShapesPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorShapesPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void libraryRequested(const QString& section);
    void panelResized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    struct ShapeBtnSpec {
        QString label;
        QString icon;
        int category = 0; // 0=Básicas, 1=Matemáticas, 2=Medidas, 3=Sólidos 3D
        ShapeKind kind = ShapeKind::Line;
        bool isArrow = false;
        bool isDimension = false;
        bool isSpring = false;
        int solidKind = 0;
        ShapeFamily family = ShapeFamily::None;
    };

    void rebuildLayout();
    bool isButtonActive(const ShapeBtnSpec& s) const;

    QPointer<CanvasWidget> m_canvas;
    QVector<ShapeBtnSpec> m_specs;
    QVector<QRectF> m_btnRects;
    QRectF m_moreLinkRect;
    QVector<EditorChipButton*> m_variantChips;
    ShapeFamily m_expandedFamily = ShapeFamily::None;
    int m_expandedAfterCat = -1;
    int m_hoverBtn = -1;
    bool m_hoverMore = false;
    QString m_activeLabel = QStringLiteral("Triángulo");
};

// 7. Panel de Láser (224 px): Punto | Estela.
class EditorLaserPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorLaserPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_modeSeg = nullptr;
};

// 8. Panel de Hoja y fondo (296 px): Pizarra | Cuaderno + A4↕/A4↔/Carta/16:9 + colores + plantillas fijas + trama.
class EditorPaperPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPaperPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF colorRect(int i) const;
    QRectF templateRect(int i) const;
    void relayout();

    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_docModeSeg = nullptr;
    EditorSegControl* m_formatSeg = nullptr;
    EditorSegControl* m_scaleSeg = nullptr;
    QVector<QColor> m_sheetColors;
    bool m_infinite = true;
    QColor m_currentBg{"#FFFFFF"};
    BackgroundTemplate m_tpl = BackgroundTemplate::Grid;
    int m_hoverTpl = -1;
};

} // namespace finily
