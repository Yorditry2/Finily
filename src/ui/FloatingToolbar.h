#pragma once

#include "core/Types.h"

#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
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
// CanvasWidget con el cromo dividido en DOS cápsulas de cristal despejadas:
//   1. Pill compacta de utilidades (arriba-izquierda): Inicio, Deshacer/Rehacer,
//      popovers "Trazo", "Papel" y "Archivo", y modo interfaz limpia.
//   2. Dock vertical de papelería (izquierda por defecto, acoplable
//      Arriba/Abajo desde "Papel"): instrumentos realistas pintados a mano
//      (pluma fuente, 5 bolígrafos de colores favoritos, borrador rosa) con
//      animación táctil de deslizamiento, más un divisor y las herramientas
//      secundarias compactas debajo.
// Todo el cromo visual se deriva de ThemeConfig: las superficies son
// GlassCapsule pintadas a mano (el estilo de sistema Qt6 — Caelestia/KDE —
// jamás las sobreescribe) y los controles usan QSS explícito con background
// transparent. Pill de zoom (abajo-derecha) y pill de páginas (abajo-izquierda)
// completan el cromo.
class FloatingToolbar : public QWidget {
    Q_OBJECT

public:
    // Posición acoplable del dock de papelería ("Posición de barra").
    enum class DockPos { Izquierda, Arriba, Abajo };

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
    // Pregunta Guardar / No guardar / Cancelar si hay cambios sin guardar.
    // Devuelve true si se puede continuar con la acción.
    bool confirmDiscardChanges(const QString& action);

    // Toast flotante estilo StarNote: pill centrada que aparece con fundido,
    // se sostiene 1.2 s y se desvanece sola (también alimenta updateOverlayMask).
    void showToast(const QString& message);

signals:
    // Botón 'Inicio' de la pill: volver a la biblioteca de cuadernos.
    void homeRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initPresets();
    void loadPresets();
    void savePresets();
    void buildUtilityPill();
    void buildDock();
    void layoutDock();
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
    // Posición acoplable del dock ("Posición de barra": Izquierda/Arriba/Abajo).
    void loadDockPosition();
    void setDockPosition(DockPos pos);

    void setColor(const QColor& color);
    void syncActivePresetFromCanvas();
    void updateStationery();
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

    // Pill compacta de utilidades (arriba-izquierda) — cápsula de cristal
    GlassCapsule* m_pill = nullptr;
    QToolButton* m_homeBtn = nullptr;
    QToolButton* m_undoBtn = nullptr;
    QToolButton* m_redoBtn = nullptr;
    QToolButton* m_strokeBtn = nullptr;
    QToolButton* m_paperBtn = nullptr;
    QToolButton* m_cleanBtn = nullptr;

    // Dock vertical de papelería (izquierda por defecto) — cápsula de cristal
    GlassCapsule* m_dock = nullptr;
    DockPos m_dockPos = DockPos::Izquierda;
    QButtonGroup* m_toolGroup = nullptr;     // selección exclusiva de las 10 herramientas
    QVector<QToolButton*> m_writingBtns;     // 4 útiles ilustrados SVG: Pluma, Bolígrafo, Resaltador, Borrador
    QVector<ToolType> m_writingKinds;
    QVector<QString> m_writingIcons;
    QWidget* m_dockDivider = nullptr;
    QVector<QToolButton*> m_secondaryBtns;   // 6 herramientas duotono SVG: Figuras, Polilínea, Lazo, Texto, Láser, Mano
    QVector<ToolType> m_secondaryKinds;
    QVector<QString> m_secondaryIcons;
    QString m_lastInkIconSig;

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
    QVector<QPair<QToolButton*, BackgroundTemplate>> m_templateSpecs;
    // Formato de hoja: Infinita / Hoja A4 / Diapo 16:9
    QVector<QToolButton*> m_formatBtns;
    QVector<PageFormat> m_formatSpecs;
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

    // Botones segmentados "Posición de barra" en el popover "Papel".
    QToolButton* m_dockLeftBtn = nullptr;
    QToolButton* m_dockTopBtn = nullptr;
    QToolButton* m_dockBottomBtn = nullptr;

    // Iconos (re)pintados con el color de texto del tema activo
    QVector<QPair<QToolButton*, QString>> m_studioUtilSpecs;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QVector<QPair<QToolButton*, LinePattern>> m_patternSpecs;
};

} // namespace finily
