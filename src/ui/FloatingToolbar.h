#pragma once

#include "core/Types.h"

#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QFrame;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QSlider;
class QTabletEvent;
class QTimer;
class QToolButton;

namespace finily {

class CanvasWidget;
class GlassCapsule;        // cápsula de cristal antialias, inmune al estilo de sistema (cpp)
class ColorDotButton;      // punto de color antialias con anillo de selección (cpp)
class StrokePreviewWidget; // punto de vista previa del trazo (definido en el .cpp)

// Barra flotante minimalista estilo StarNote/Freenotes que se superpone al
// CanvasWidget: pill superior (deshacer/rehacer, herramientas, presets de
// tinta, popovers "Trazo" y "Papel", modo interfaz limpia) más una pill de
// zoom en la esquina inferior derecha. Todo el cromo visual se deriva de
// ThemeConfig: las superficies son GlassCapsule pintadas a mano (el estilo de
// sistema Qt6 — Caelestia/KDE — jamás las sobreescribe) y los controles usan
// QSS explícito con background transparent.
class FloatingToolbar : public QWidget {
    Q_OBJECT

public:
    explicit FloatingToolbar(CanvasWidget* canvas, QWidget* parent = nullptr);

    // Sincroniza botones, sliders, presets y tema con el estado del lienzo.
    void refreshFromCanvas();

    // Tab: oculta la interfaz dejando solo un pequeño botón de restauración.
    void toggleCleanUi();
    bool isCleanUi() const { return m_cleanUi; }

    void selectTool(ToolType tool);
    void selectPreset(int index); // 0..4

    // Diálogos de importación/exportación (también disparados por Ctrl+O / Ctrl+E).
    void importPdfDialog();
    void exportPdfDialog();

    // Cuaderno .finily (también disparados por Ctrl+N / Ctrl+Mayús+O /
    // Ctrl+S / Ctrl+Mayús+S). Todos cierran los popovers automáticamente.
    void newDocumentAction();
    void openNotebookDialog();
    void saveNotebookAction();
    void saveNotebookAsDialog();

    // Toast flotante estilo StarNote: pill centrada que aparece con fundido,
    // se sostiene 1.2 s y se desvanece sola (también alimenta updateOverlayMask).
    void showToast(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initPresets();
    void loadPresets();
    void savePresets();
    void buildPill();
    void buildZoomPill();
    void buildPagePill();
    void buildRestoreButton();
    void buildStrokePopover();
    void buildPaperPopover();
    void buildShapesPopover();
    void buildFilePopover();
    void buildContextPill();

    void applyThemeStylesheet();
    QString buildStylesheet(const ThemeConfig& theme) const;
    void refreshIcons();

    void positionChildren();
    void updateOverlayMask();
    void openPopover(GlassCapsule* popover, QWidget* anchor);
    void closePopovers();

    // Sombras suaves reales (QGraphicsDropShadowEffect, 1 por widget elevado).
    void applyShadow(QWidget* widget);
    void refreshShadowColors();
    // Punto de vista previa en vivo del popover "Trazo".
    void updateStrokePreview();
    // Posición acoplable de la barra ("Barra arriba" / "Barra abajo").
    void loadDockPosition();
    void setDockBottom(bool bottom);

    void setColor(const QColor& color);
    void syncActivePresetFromCanvas();
    void updatePresetDots();
    void updateZoomLabel();
    void onClearClicked();
    void disarmClearButton();

    QPointer<CanvasWidget> m_canvas;

    // Estado
    ThemeId m_theme = ThemeId::DarkBoard;
    QVector<PenPreset> m_presets;
    bool m_presetsRestored = false;   // los presets vinieron de QSettings
    QString m_lastPresetsSig;         // evita reescrituras de QSettings sin cambios
    int m_activePreset = 0;
    QVector<QColor> m_palette;
    bool m_cleanUi = false;
    bool m_clearArmed = false;
    bool m_updatingUi = false;
    QString m_lastStateSig;

    // Pill principal (superior, centrada) — cápsula de cristal antialias
    GlassCapsule* m_pill = nullptr;
    QToolButton* m_undoBtn = nullptr;
    QToolButton* m_redoBtn = nullptr;
    QVector<ToolType> m_toolKinds;
    QVector<QToolButton*> m_toolBtns;
    QVector<ColorDotButton*> m_presetDots;
    QToolButton* m_strokeBtn = nullptr;
    QToolButton* m_paperBtn = nullptr;
    QToolButton* m_cleanBtn = nullptr;

    // Popover "Figuras" y barra contextual de figuras/polilínea
    GlassCapsule* m_shapesPopover = nullptr;
    QToolButton* m_shapeBtn = nullptr;
    QVector<QToolButton*> m_shapeKindBtns;
    QVector<ShapeKind> m_shapeKindSpecs;
    QToolButton* m_holdToShapeBtn = nullptr;
    QToolButton* m_smartGuidesBtn = nullptr;
    GlassCapsule* m_contextPill = nullptr;
    QToolButton* m_finishPolylineBtn = nullptr;
    QToolButton* m_closePolylineBtn = nullptr;
    QToolButton* m_straightenBtn = nullptr;
    QToolButton* m_duplicateBtn = nullptr;
    QToolButton* m_deleteLassoBtn = nullptr;
    QToolButton* m_deleteShapeBtn = nullptr;
    QToolButton* m_deleteImageBtn = nullptr;
    QToolButton* m_deleteTextBtn = nullptr;

    // Popover "Archivo / Cuaderno / PDF"
    GlassCapsule* m_filePopover = nullptr;
    QToolButton* m_fileBtn = nullptr;
    QToolButton* m_newNotebookBtn = nullptr;
    QToolButton* m_openNotebookBtn = nullptr;
    QToolButton* m_saveNotebookBtn = nullptr;
    QToolButton* m_saveNotebookAsBtn = nullptr;
    QToolButton* m_importPdfBtn = nullptr;
    QToolButton* m_importImageBtn = nullptr;
    QToolButton* m_pasteBtn = nullptr;
    QToolButton* m_exportBtn = nullptr;
    QLabel* m_pdfInfoLabel = nullptr;

    // Pill de zoom (inferior derecha) — cápsula de cristal antialias
    GlassCapsule* m_zoomPill = nullptr;
    QToolButton* m_zoomOutBtn = nullptr;
    QToolButton* m_zoomResetBtn = nullptr;
    QToolButton* m_zoomInBtn = nullptr;

    // Pill de páginas (inferior izquierda) — cápsula de cristal antialias
    GlassCapsule* m_pagePill = nullptr;
    QToolButton* m_pagePrevBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    QToolButton* m_pageNextBtn = nullptr;
    QToolButton* m_pageAddBtn = nullptr;
    QToolButton* m_pageDeleteBtn = nullptr;

    // Botón diminuto de restauración (modo interfaz limpia)
    QToolButton* m_restoreBtn = nullptr;

    // Popover "Trazo"
    GlassCapsule* m_strokePopover = nullptr;
    QWidget* m_strokeStyleSection = nullptr;
    QWidget* m_stabilizerRow = nullptr;
    QWidget* m_eraserSection = nullptr;
    QVector<ColorDotButton*> m_swatches;
    QToolButton* m_customColorBtn = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QVector<QToolButton*> m_patternBtns;
    QLabel* m_stabilizerLabel = nullptr;
    QSlider* m_stabilizerSlider = nullptr;
    QVector<QToolButton*> m_pressureBtns;
    QVector<PressureProfile> m_pressureSpecs;
    QToolButton* m_eraserStrokeBtn = nullptr;
    QToolButton* m_eraserPartialBtn = nullptr;
    QLabel* m_eraserSizeLabel = nullptr;
    QSlider* m_eraserSizeSlider = nullptr;

    // Popover "Papel"
    GlassCapsule* m_paperPopover = nullptr;
    QVector<QToolButton*> m_themeBtns;
    QVector<ThemeId> m_themeSpecs;
    QVector<QToolButton*> m_templateBtns;
    QPushButton* m_clearBtn = nullptr;
    QTimer* m_clearTimer = nullptr;

    GlassCapsule* m_openPopover = nullptr;

    // Toast flotante estilo StarNote (cápsula centrada con fundido automático).
    GlassCapsule* m_toastPill = nullptr;
    QLabel* m_toastLabel = nullptr;
    QTimer* m_toastTimer = nullptr;
    QPropertyAnimation* m_toastAnim = nullptr;

    // Registro de todas las cápsulas para recolorearlas al cambiar de tema.
    QVector<GlassCapsule*> m_capsules;

    // Animaciones suaves de popovers (140 ms) y pill (150 ms), OutCubic.
    QPropertyAnimation* m_popoverAnim = nullptr;
    QPropertyAnimation* m_pillAnim = nullptr;

    // Vista previa en vivo del color/grosor de tinta (popover "Trazo").
    StrokePreviewWidget* m_strokePreview = nullptr;

    // Posición acoplable de la barra: false = arriba (por defecto).
    bool m_toolbarBottom = false;
    QToolButton* m_dockTopBtn = nullptr;
    QToolButton* m_dockBottomBtn = nullptr;

    // Iconos (re)pintados con el color de texto del tema activo
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QVector<QPair<QToolButton*, LinePattern>> m_patternSpecs;
    QVector<QPair<QToolButton*, BackgroundTemplate>> m_templateSpecs;
};

} // namespace finily
