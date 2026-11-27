#pragma once

#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QVector>

class QHBoxLayout;

namespace finily {

class CanvasWidget;

// Cápsula contextual adaptativa (reemplaza el diálogo Propiedades… y el menú contextual).
class EditorContextCapsule : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorContextCapsule(CanvasWidget* canvas, QWidget* parent = nullptr);

    void setDropdownOpener(DropdownRequestFn fn);
    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

signals:
    void vectorizeRequested();
    void extractAiRequested();
    void fillColorRequested(const QRect& anchorRect);
    void layoutChanged();

private:
    void clearControls();
    void addMoreButton();

    QPointer<CanvasWidget> m_canvas;
    DropdownRequestFn m_dropdownOpener;
    QHBoxLayout* m_layout = nullptr;
    bool m_saveElementOpen = false;
    bool m_fineThicknessOpen = false;
    QString m_lastSignature;
};

// Cápsula de marcas geométricas en arista + «Resaltar tramo» con el color activo.
class EditorEdgeMarkCapsule : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorEdgeMarkCapsule(CanvasWidget* canvas, QWidget* parent = nullptr);

    void showAtWorldPoint(const QPointF& worldPos, const QColor& activeColor);
    void applyEditorTheme(bool dark) override;

private:
    QPointer<CanvasWidget> m_canvas;
    QPointF m_worldPos;
    QColor m_activeColor{"#FACC15"};
    QVector<EditorIconButton*> m_markBtns;
    EditorChipButton* m_highlightBtn = nullptr;
};

// Barra contextual flotante sobre el editor inline de texto (fuente, tamaño, B, I).
class EditorInlineTextBar : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorInlineTextBar(CanvasWidget* canvas, QWidget* parent = nullptr);

    void syncFromCanvas();
    void applyEditorTheme(bool dark) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<CanvasWidget> m_canvas;
    EditorSegControl* m_fontSeg = nullptr;
    EditorIconButton* m_minusBtn = nullptr;
    EditorIconButton* m_plusBtn = nullptr;
    EditorIconButton* m_boldBtn = nullptr;
    EditorIconButton* m_italicBtn = nullptr;
    int m_family = 0;
    double m_size = 20.0;
    bool m_bold = false;
    bool m_italic = false;
};

} // namespace finily
