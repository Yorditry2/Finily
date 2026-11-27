#pragma once

#include "core/Types.h"
#include "ui/editor/EditorTheme.h"

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

class QTimer;

namespace finily {

class CanvasWidget;

struct EditorTabItem {
    QString title;
    bool isBoard = false;
    bool isPdf = false;
    bool dirty = false;
    bool active = false;
};

// Menú desplegable propio (sin Qt::Popup / QMenu) para pestañas, exportar, ⋯ Más y selectores.
class EditorDropdownMenu : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorDropdownMenu(QWidget* parent = nullptr);

    void openMenu(const QVector<EditorDropdownItem>& items,
                  const QPoint& posInParent,
                  const QSize& bounds,
                  std::function<void(int id)> callback,
                  int minWidth = 236);
    void applyEditorTheme(bool dark) override;

signals:
    void menuClosed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QVector<QRectF> rowRects() const;
    int totalHeight() const;

    QVector<EditorDropdownItem> m_items;
    std::function<void(int)> m_callback;
    int m_hoverIndex = -1;
    int m_menuWidth = 236;
};

// Aviso flotante inferior con botón «Deshacer» opcional (6 s).
class EditorToast : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorToast(QWidget* parent = nullptr);

    void showToast(const QString& msg, bool withUndo = false, int durationMs = 6000);
    void applyEditorTheme(bool dark) override;

signals:
    void undoRequested();
    void visibilityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF undoRect() const;

    QString m_msg;
    bool m_withUndo = false;
    bool m_hoverUndo = false;
    QTimer* m_timer = nullptr;
};

// Tarjeta de confirmación propia (reemplaza QMessageBox al cerrar pestaña sin guardar).
class EditorConfirmCard : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorConfirmCard(QWidget* parent = nullptr);

    // Devuelve 1 = Guardar, 2 = Descartar, 0 = Cancelar.
    int execModal(QWidget* overlayParent, const QString& title, const QString& body);
    void cancel();
    void applyEditorTheme(bool dark) override;

signals:
    void visibilityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRectF btnRect(int idx) const; // 0=Cancelar, 1=Descartar, 2=Guardar

    QString m_title;
    QString m_body;
    int m_hoverBtn = -1;
    int m_result = 0;
};

// Barra superior tipo navegador:
// - Modo Top (84 px): Fila 1 (Inicio + pestañas + pantalla completa) + Fila 2 (herramientas, grosores, colores, utilidades).
// - Modo Left / Float (38 px): Fila 1 + utilidades integradas a la derecha.
class EditorTopBar : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorTopBar(QWidget* parent = nullptr);

    void setDockMode(ToolbarDockMode mode);
    ToolbarDockMode dockMode() const { return m_dockMode; }

    void setTabs(const QVector<EditorTabItem>& tabs);
    void setElementsPanelOpen(bool open);
    void syncFromCanvas(CanvasWidget* canvas);
    void applyEditorTheme(bool dark) override;

    void startInlineRenameActiveTab();

    QRect anchorRectForTool(ToolType tool) const;
    QRect paperButtonRect() const;
    QRect pagesButtonRect() const;
    QRect exportButtonRect() const;
    QRect activeColorAreaRect() const;
    QRect activeWidthSlotRect() const;

signals:
    void toolSelected(ToolType tool);
    void toolPanelToggled(ToolType tool, const QRect& anchorRect);
    void elementsPanelToggled();
    void fineWidthRequested(ToolType tool, const QRect& anchorRect);
    void widthSlotSelected(ToolType tool, int slotIndex);
    void colorSwatchSelected(ToolType tool, int colorIndex);
    void customColorRequested(ToolType tool, const QRect& anchorRect);
    void dockModeDragFinished(ToolbarDockMode mode);
    void undoRequested();
    void redoRequested();
    void homeRequested();
    void addTabRequested(const QRect& anchorRect);
    void closeTabRequested(int index);
    void tabSelected(int index);
    void tabRenamed(int index, const QString& newTitle);
    void tabMenuRequested(int index, const QPoint& globalPos);
    void pagesRequested();
    void paperRequested(const QRect& anchorRect);
    void exportRequested(const QRect& anchorRect);
    void gearRequested();
    void fullscreenRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class HitKind {
        None, Home, Tab, TabClose, NewTab, Fullscreen,
        Undo, Redo, Grip, Tool, Elements,
        WidthSlot, ColorSwatch, ColorWheel,
        Pages, Paper, ZoomOut, ZoomReset, ZoomIn, Export, Gear
    };
    struct HitItem {
        QRectF rect;
        HitKind kind = HitKind::None;
        int index = 0;
        ToolType tool = ToolType::Pen;
        bool hasSub = false;
        QString iconName;
        QString label;
    };

    void rebuildHits();
    int hitIndexAt(const QPointF& pos) const;
    void finishInlineRename(bool accept);

    ToolbarDockMode m_dockMode = ToolbarDockMode::Top;
    QVector<EditorTabItem> m_tabs;
    QVector<HitItem> m_hits;
    QVector<qreal> m_row2Dividers;
    int m_hoverHit = -1;
    bool m_draggingGrip = false;
    QPoint m_dragStartGlobal;

    ToolType m_activeTool = ToolType::Pen;
    bool m_elementsOpen = false;
    bool m_canUndo = false;
    bool m_canRedo = false;
    int m_zoomPercent = 100;
    int m_pageCurrent = 1;
    int m_pageCount = 1;
    bool m_infinitePage = true;
    bool m_darkSheet = false;

    std::array<double, 3> m_widths{1.2, 2.4, 4.5};
    int m_activeWidthSlot = 1;
    bool m_showWidths = true;

    QVector<QColor> m_colors;
    int m_activeColorIdx = 0;
    bool m_showColors = true;
    QColor m_penNibColor;
    QColor m_hlBarColor;

    EditorLineEdit* m_renameEdit = nullptr;
    int m_renamingTab = -1;
};

// Dock desacoplado (modos Izquierda vertical y Flotante inferior) con herramientas, grosores y colores.
class EditorDetachedDock : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorDetachedDock(QWidget* parent = nullptr);

    void setElementsPanelOpen(bool open);
    void syncFromCanvas(CanvasWidget* canvas, ToolbarDockMode mode);
    void applyEditorTheme(bool dark) override;

    QRect anchorRectForTool(ToolType tool) const;
    QRect activeColorAreaRect() const;
    QRect activeWidthSlotRect() const;

signals:
    void toolSelected(ToolType tool);
    void toolPanelToggled(ToolType tool, const QRect& anchorRect);
    void elementsPanelToggled();
    void fineWidthRequested(ToolType tool, const QRect& anchorRect);
    void widthSlotSelected(ToolType tool, int slotIndex);
    void colorSwatchSelected(ToolType tool, int colorIndex);
    void customColorRequested(ToolType tool, const QRect& anchorRect);
    void dockModeDragFinished(ToolbarDockMode mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class HitKind { None, Grip, Tool, Elements, WidthSlot, ColorSwatch, ColorWheel };
    struct HitItem {
        QRectF rect;
        HitKind kind = HitKind::None;
        int index = 0;
        ToolType tool = ToolType::Pen;
        bool hasSub = false;
        QString iconName;
    };

    void rebuildLayoutAndHits();
    int hitIndexAt(const QPointF& pos) const;

    ToolbarDockMode m_dockMode = ToolbarDockMode::Left;
    QVector<HitItem> m_hits;
    int m_hoverHit = -1;
    bool m_draggingGrip = false;

    ToolType m_activeTool = ToolType::Pen;
    bool m_elementsOpen = false;
    bool m_darkSheet = false;
    std::array<double, 3> m_widths{1.2, 2.4, 4.5};
    int m_activeWidthSlot = 1;
    bool m_showWidths = true;
    QVector<QColor> m_colors;
    int m_activeColorIdx = 0;
    bool m_showColors = true;
    QColor m_penNibColor;
    QColor m_hlBarColor;
};

// Cápsula inferior de páginas (oculta en pizarra infinita).
class EditorPageCapsule : public EditorGlassCard {
    Q_OBJECT

public:
    explicit EditorPageCapsule(QWidget* parent = nullptr);

    void setPageInfo(int current, int count);

signals:
    void prevPageRequested();
    void nextPageRequested();
    void pagesPanelRequested();
    void addPageRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int m_current = 1;
    int m_count = 1;
    int m_hoverPart = -1; // 0=prev, 1=label, 2=next, 3=add
};

} // namespace finily
