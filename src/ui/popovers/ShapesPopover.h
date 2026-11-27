#pragma once

#include "ui/FloatingToolbarInternal.h"

#include <QPair>
#include <QPointer>
#include <QString>
#include <QVector>

class QToolButton;

namespace finily {

class CanvasWidget;

class ShapesPopover : public GlassCapsule {
    Q_OBJECT

public:
    explicit ShapesPopover(CanvasWidget* canvas, QWidget* parent = nullptr);

    void syncFromCanvas() override;
    void applyTheme(const ThemeConfig& theme, const QString& qss) override;

signals:
    void layoutAdjusted();
    void refreshToolbarRequested();
    void closeRequested();

private:
    QPointer<CanvasWidget> m_canvas;

    QVector<QToolButton*> m_shapeKindBtns;
    QVector<ShapeKind> m_shapeKindSpecs;
    QVector<bool> m_shapeDimensionSpecs;
    QVector<int> m_shapeSidesSpecs;
    QVector<int> m_shapeTriSpecs;
    QVector<int> m_shapeQuadSpecs;
    QVector<int> m_shapeSolidSpecs;
    QVector<QString> m_shapeMarkSpecs;

    QVector<QToolButton*> m_patternBtns;
    QVector<QPair<QToolButton*, LinePattern>> m_patternSpecs;
    QVector<QToolButton*> m_endingBtns;
    QVector<LineEnding> m_endingSpecs;
    QVector<QPair<QToolButton*, QString>> m_iconSpecs;
};

} // namespace finily
