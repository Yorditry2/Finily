#pragma once

#include "core/Types.h"
#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPointer>
#include <QRect>
#include <QVector>

namespace finily {

class CanvasWidget;

// Fila de 5 puntos de grosor a tamaño real + valor mm (doble clic abre deslizador fino).
class EditorWidthSection : public QWidget {
    Q_OBJECT

public:
    explicit EditorWidthSection(ToolType tool, double minMm, double maxMm, double stepMm,
                                std::array<double, 5> presets, QWidget* parent = nullptr);

    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark);
    double currentMm() const { return m_currentMm; }

signals:
    void layoutHeightChanged();
    void widthChanged(double mm);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;

private:
    QRectF valueRect() const;
    QRectF dotButtonRect(int i) const;
    void toggleFineSlider();

    ToolType m_tool;
    double m_minMm;
    double m_maxMm;
    double m_stepMm;
    std::array<double, 5> m_presets;
    QPointer<CanvasWidget> m_canvas;
    double m_currentMm = 2.4;
    QColor m_inkColor{"#18181B"};
    int m_hoverSlot = -1;
    bool m_hoverValue = false;
    bool m_expanded = false;
    bool m_dark = false;
    EditorPalette m_pal;
    EditorSlider* m_slider = nullptr;
};

// Fila común de 7 círculos 26 px + anillo cromático.
class EditorColorRow : public QWidget {
    Q_OBJECT

public:
    explicit EditorColorRow(ToolType tool, QWidget* parent = nullptr);

    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark);

signals:
    void customColorRequested(ToolType tool, const QRect& anchorInWindow);
    void colorChanged();

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

// 1. Panel de Pinceles (D1, 360 px): 4 filas con punta recortada e inclinada, nombre y trazo con grosor recordado.
class EditorPenPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPenPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void applyEditorTheme(bool dark) override;

signals:
    void libraryRequested(const QString& section);
    void customColorRequested(ToolType tool, const QRect& anchorRect);
    void panelResized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF rowRect(int idx) const;

    QPointer<CanvasWidget> m_canvas;
    int m_activeTip = 1;
    std::array<double, 4> m_tipWidths{0.6, 1.2, 1.0, 3.5};
    QColor m_activeColor{"#18181B"};
    int m_hoverRow = -1;
    int m_animRow = -1;
    qreal m_drawProgress = 1.0;
    qreal m_openProgress = 1.0;
    QVariantAnimation* m_drawAnim = nullptr;
    QVariantAnimation* m_openAnim = nullptr;
};

// 2. Panel de Resaltador (R1, 300 px): muestra manuscrita resaltada + slider de opacidad sobre damero.
class EditorHighlighterPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorHighlighterPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void applyEditorTheme(bool dark) override;

signals:
    void customColorRequested(ToolType tool, const QRect& anchorRect);
    void panelResized();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<CanvasWidget> m_canvas;
    EditorOpacitySlider* m_opacitySlider = nullptr;
    double m_activeMm = 12.0;
    QColor m_activeColor{"#FACC15"};
    double m_opacity = 0.50;
    qreal m_sweepProgress = 1.0;
    QVariantAnimation* m_sweepAnim = nullptr;
    QVariantAnimation* m_openAnim = nullptr;
};

// 3. Panel de Borrador (B3, 316 px): Trazo completo | Solo lo que toco + interruptores + Borrar toda la página.
class EditorEraserPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorEraserPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void applyEditorTheme(bool dark) override;

signals:
    void clearPageRequested();
    void panelResized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF filterRowRect(int idx) const;
    QRectF clearLinkRect() const;

    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_modeSeg = nullptr;
    std::array<bool, 3> m_targets{true, true, true};
    std::array<qreal, 3> m_swAnimPos{1.0, 1.0, 1.0};
    std::array<QVariantAnimation*, 3> m_swAnims{nullptr, nullptr, nullptr};
    int m_hoverRow = -1;
    bool m_hoverClear = false;
    QVariantAnimation* m_openAnim = nullptr;
};

// 4. Panel de Relleno (F3, 300 px): fila de 5 parches cuadrados + slider de intensidad sobre damero.
class EditorFillPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorFillPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void applyEditorTheme(bool dark) override;
    bool isSegmentMode() const { return false; }

signals:
    void fillSubModeChanged(bool segmentMode);
    void customColorRequested(ToolType tool, const QRect& anchorRect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF patternCardRect(int idx) const;

    QPointer<CanvasWidget> m_canvas;
    EditorOpacitySlider* m_opacitySlider = nullptr;
    ShadePattern m_pattern = ShadePattern::Solid;
    QColor m_activeColor{"#2563EB"};
    double m_opacity = 0.60;
    int m_hoverCard = -1;
    std::array<qreal, 5> m_chipScale{1.04, 1.0, 1.0, 1.0, 1.0};
    std::array<QVariantAnimation*, 5> m_scaleAnims{nullptr, nullptr, nullptr, nullptr, nullptr};
    QVariantAnimation* m_openAnim = nullptr;
};

// 5. Panel de Lazo (L1, 290 px): Qué selecciona + 3 interruptores (el último tiembla 240 ms si se intenta apagar).
class EditorLassoPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorLassoPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void applyEditorTheme(bool dark) override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF filterRowRect(int idx) const;

    QPointer<CanvasWidget> m_canvas;
    std::array<bool, 3> m_filters{true, true, true};
    std::array<qreal, 3> m_swAnimPos{1.0, 1.0, 1.0};
    std::array<QVariantAnimation*, 3> m_swAnims{nullptr, nullptr, nullptr};
    int m_hoverRow = -1;
    int m_shakeRow = -1;
    qreal m_shakeOffset = 0.0;
    QVariantAnimation* m_shakeAnim = nullptr;
    QVariantAnimation* m_openAnim = nullptr;
};

// Burbuja flotante de variantes para Figuras M1 (hija directa de FloatingToolbar).
class EditorShapesBubble : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorShapesBubble(QWidget* parent = nullptr);

    void openForFamily(int famIdx, int selectedVar, const QPoint& anchorCenterInParent,
                       int panelBottomY, int hostWidth, bool animateOpen);
    void closeBubble();
    int familyIndex() const { return m_famIdx; }
    void applyEditorTheme(bool dark) override;

signals:
    void variantChosen(int famIdx, int variantVal);
    void hoverVariantChanged(const QString& label);
    void visibilityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF itemRect(int idx) const;
    int itemCount() const;

    int m_famIdx = -1;
    int m_selectedVar = 0;
    int m_hoverIdx = -1;
    qreal m_arrowX = 40.0;
    qreal m_openT = 1.0;
    QVariantAnimation* m_openAnim = nullptr;
    QVariantAnimation* m_slideAnim = nullptr;
};

// 6. Panel de Figuras (M1, 308x82 px): fila fija de 6 familias + nombre + burbuja flotante de variantes.
class EditorShapesPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorShapesPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void playOpenAnimation();
    void closeBubble();
    EditorGlassCard* bubbleWidget() const { return m_bubble; }
    void applyEditorTheme(bool dark) override;

signals:
    void libraryRequested(const QString& section);
    void panelResized();
    void bubbleVisibilityChanged();

protected:
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF familyBtnRect(int idx) const;
    void applyFamilyVariantToCanvas(int famIdx, int varVal);
    void loadSavedVariants();
    void saveVariants() const;

    QPointer<CanvasWidget> m_canvas;
    EditorShapesBubble* m_bubble = nullptr;
    int m_activeFam = 3; // 0=Línea, 1=Círculo, 2=Triángulo, 3=Cuadrilátero, 4=Polígono, 5=3D
    std::array<int, 6> m_lastVar{1, 0, 1, 0, 6, 0};
    int m_hoverFam = -1;
    QString m_hoverBubbleLabel;
    QVariantAnimation* m_openAnim = nullptr;
};

// 8. Panel de Hoja y fondo (312 px):
// - Pizarra infinita: Color de fondo (+ más colores) + Plantillas (3x2).
// - Cuaderno con hojas: Tamaño (A4↕/A4↔/Carta/16:9) + Color de hoja (+ más colores) + Plantillas (4x2) + Trama.
class EditorPaperPanel : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPaperPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void customSheetColorRequested(const QRect& anchorRect);
    void panelResized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    struct TplDef {
        BackgroundTemplate tpl;
        const char* label;
    };
    int templateCount() const;
    TplDef templateAt(int i) const;
    QRectF colorRect(int i) const;
    QRectF templateRect(int i) const;
    void relayout();

    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_formatSeg = nullptr;
    EditorSegControl* m_scaleSeg = nullptr;
    QVector<QColor> m_sheetColors;
    bool m_infinite = true;
    QColor m_currentBg{"#FFFFFF"};
    BackgroundTemplate m_tpl = BackgroundTemplate::Grid;
    int m_hoverTpl = -1;
};

} // namespace finily
