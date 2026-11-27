#pragma once

#include "ui/FloatingToolbarInternal.h"

#include <QColor>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

class QLabel;
class QSlider;
class QToolButton;
class QWidget;

namespace finily {

class CanvasWidget;

class StrokePopover : public GlassCapsule {
    Q_OBJECT

public:
    explicit StrokePopover(CanvasWidget* canvas, QWidget* parent = nullptr);

    void setPalette(const QVector<QColor>& palette);
    void setActivePresetIndex(int index, const PenPreset* activePreset);
    void syncFromCanvas() override;
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;

signals:
    void colorPicked(const QColor& color);
    void favoritesMenuRequested(QWidget* anchor);
    void layoutAdjusted();
    void refreshToolbarRequested();
    void closeRequested();

private:
    void updateSwatches();
    void updateStrokePreview();
    void updateFillSwatches();
    void updateTipIcons();
    void updateEraserPreview();

    QPointer<CanvasWidget> m_canvas;
    ThemeConfig m_theme = getThemeConfig(ThemeId::DarkBoard);
    QVector<QColor> m_palette;
    bool m_updatingUi = false;

    QWidget* m_strokeStyleSection = nullptr;
    QWidget* m_penTipSection = nullptr;
    QVector<QToolButton*> m_penTipBtns;
    QWidget* m_hlTipSection = nullptr;
    QVector<QToolButton*> m_hlTipBtns;
    QWidget* m_lassoModeSection = nullptr;
    QToolButton* m_lassoFreeBtn = nullptr;
    QToolButton* m_lassoRectBtn = nullptr;
    QWidget* m_strokeGeometrySection = nullptr;
    QWidget* m_fillStyleSection = nullptr;
    QVector<QPair<QToolButton*, ShadePattern>> m_fillStyles;

    QVector<ColorDotButton*> m_swatches;
    QToolButton* m_customColorBtn = nullptr;
    StrokePreviewWidget* m_strokePreview = nullptr;
    QSlider* m_sizeSlider = nullptr;
    QToolButton* m_favMenuBtn = nullptr;

    QWidget* m_stabilizerRow = nullptr;
    QLabel* m_stabilizerIcon = nullptr;
    QSlider* m_stabilizerSlider = nullptr;

    QWidget* m_hlOpacityRow = nullptr;
    QLabel* m_hlOpacityIcon = nullptr;
    QSlider* m_hlOpacitySlider = nullptr;

    QToolButton* m_strokeBeautifyBtn = nullptr;

    QWidget* m_eraserSection = nullptr;
    QToolButton* m_eraserStrokeBtn = nullptr;
    QToolButton* m_eraserPartialBtn = nullptr;
    QVector<QToolButton*> m_eraserFilterBtns;
    StrokePreviewWidget* m_eraserPreview = nullptr;
    QSlider* m_eraserSizeSlider = nullptr;

    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
    QVector<QPair<QToolButton*, QString>> m_ctxIconSpecs;
};

} // namespace finily
