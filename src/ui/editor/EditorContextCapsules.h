#pragma once

#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QVector>

class QHBoxLayout;
class QVBoxLayout;
class QVariantAnimation;

namespace finily {

class CanvasWidget;

// Popover de Fuente para Cápsula de Texto T1 (DESIGN §7.7): filas de 40 px, cada una escrita en su fuente.
class EditorFontPopover : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorFontPopover(QWidget* parent = nullptr);

    void openAbove(const QRect& anchorInParent, const QSize& bounds, int activeFamilyIdx);
    void closePopover();
    void applyEditorTheme(bool dark) override;

signals:
    void fontPicked(int familyIdx);
    void visibilityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF rowRect(int idx) const;

    int m_activeIdx = 0;
    int m_hoverIdx = -1;
    qreal m_openT = 1.0;
    QVariantAnimation* m_openAnim = nullptr;
};

// Menú «⋯ Más» P1 (DESIGN §7.6): tarjeta propia anclada encima del botón ⋯, hija de FloatingToolbar.
class EditorSelectionMoreMenu : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorSelectionMoreMenu(CanvasWidget* canvas, QWidget* parent = nullptr);

    void openAbove(const QRect& moreBtnInParent, const QSize& bounds);
    void closeMenu();
    void applyEditorTheme(bool dark) override;

signals:
    void closed();
    void visibilityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    enum class HitId { None, Cut, Copy, FlipH, FlipV, Front, Back, Group, Lock };
    struct Zone { HitId id; QRectF rect; QString tip; };

    QVector<Zone> zones() const;
    HitId hitAt(const QPointF& pos, QString* outTip = nullptr) const;

    QPointer<CanvasWidget> m_canvas;
    HitId m_hover = HitId::None;
    qreal m_openT = 1.0;
    QVariantAnimation* m_openAnim = nullptr;
};

// Cápsula contextual adaptativa C3 «dos pisos» (DESIGN §7.5):
// contenedor transparente con dos EditorGlassCard apiladas (piso superior de variantes + piso inferior común).
class EditorContextCapsule : public QWidget {
    Q_OBJECT

public:
    explicit EditorContextCapsule(CanvasWidget* canvas, QWidget* parent = nullptr);

    void setDropdownOpener(DropdownRequestFn fn);
    void syncFromCanvas();
    void applyEditorTheme(bool dark);
    void closeMoreMenu();
    void closeFontPopover();

    EditorGlassCard* topCard() const { return m_topCard; }
    EditorGlassCard* bottomCard() const { return m_bottomCard; }
    EditorGlassCard* moreMenuCard() const { return m_moreMenu; }
    EditorGlassCard* fontPopoverCard() const { return m_fontPopover; }

signals:
    void vectorizeRequested();
    void extractAiRequested();
    void fillColorRequested(const QRect& anchorRect);
    void selectionColorRequested(const QRect& anchorRect);
    void elementSaved();
    void layoutChanged();
    void moreMenuVisibilityChanged();
    void fontPopoverVisibilityChanged();
    void transientPopoverAboutToOpen();

private:
    void clearLayout(QHBoxLayout* lay);
    void addMoreButton(QHBoxLayout* targetLayout, QWidget* parentCard);
    void playRevealAnimation(bool hasTop);

    QPointer<CanvasWidget> m_canvas;
    DropdownRequestFn m_dropdownOpener;
    EditorGlassCard* m_topCard = nullptr;
    EditorGlassCard* m_bottomCard = nullptr;
    EditorSelectionMoreMenu* m_moreMenu = nullptr;
    EditorFontPopover* m_fontPopover = nullptr;
    QPointer<EditorIconButton> m_moreBtn;
    QHBoxLayout* m_topLayout = nullptr;
    QHBoxLayout* m_bottomLayout = nullptr;
    QVariantAnimation* m_revealAnim = nullptr;
    QWidget* m_pizzaIconWidget = nullptr;
    bool m_dark = false;
    EditorPalette m_pal;
    bool m_draggingSlider = false;
    QString m_lastSignature;
};

// Cápsula de marcas geométricas en arista + «Resaltar tramo» con el color activo.
class EditorEdgeMarkCapsule : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorEdgeMarkCapsule(CanvasWidget* canvas, QWidget* parent = nullptr);

    void showAtWorldPoint(const QPointF& worldPos, const QColor& activeColor);
    void applyEditorTheme(bool dark) override;

signals:
    void visibilityChanged();

private:
    QPointer<CanvasWidget> m_canvas;
    QPointF m_worldPos;
    QColor m_activeColor{"#FACC15"};
    QVector<EditorIconButton*> m_markBtns;
    EditorChipButton* m_highlightBtn = nullptr;
};

} // namespace finily
