#pragma once

#include <QColor>
#include <QEasingCurve>
#include <QHash>
#include <QPair>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

namespace finily {

// Superposición no bloqueante que revela circularmente el nuevo tema desde el botón luna/sol.
class ThemeRevealOverlay : public QWidget {
    Q_OBJECT
public:
    static void play(QWidget* host, const QPixmap& oldSnapshot, const QPoint& originInHost);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemeRevealOverlay(QWidget* host, const QPixmap& oldSnapshot, const QPoint& originInHost);

    QPixmap m_oldSnapshot;
    QPoint m_origin;
    qreal m_progress = 0.0;
    bool m_reduced = false;
};

// Transición de entrada/salida de carpeta en 2 fases sincronizadas sobre el área de la grilla.
class FolderZoomOverlay : public QWidget {
    Q_OBJECT
public:
    static void play(QWidget* host, const QPixmap& oldGrid, const QPixmap& newGrid,
                     const QRect& gridRectInHost, const QPointF& pivotInGrid,
                     const QColor& bgColor, bool entering);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FolderZoomOverlay(QWidget* host, const QPixmap& oldGrid, const QPixmap& newGrid,
                      const QRect& gridRectInHost, const QPointF& pivotInGrid,
                      const QColor& bgColor, bool entering);

    QPixmap m_oldGrid;
    QPixmap m_newGrid;
    QPointF m_pivot;
    QColor m_bgColor;
    bool m_entering = true;
    bool m_reduced = false;
    qreal m_progress = 0.0;
    QEasingCurve m_inCurve;
    QEasingCurve m_outCurve;
};

// Vuelo parabólico de una miniatura hacia la papelera (o de regreso al deshacer).
class TrashFlyOverlay : public QWidget {
    Q_OBJECT
public:
    static void play(QWidget* host, const QPixmap& visualSnap, const QRectF& startRectInHost,
                     const QPointF& targetCenterInHost, bool reverse,
                     std::function<void()> onArrived);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TrashFlyOverlay(QWidget* host, const QPixmap& visualSnap, const QRectF& startRectInHost,
                    const QPointF& targetCenterInHost, bool reverse,
                    std::function<void()> onArrived);

    QPixmap m_snap;
    QRectF m_startRect;
    QPointF m_targetCenter;
    bool m_reverse = false;
    qreal m_t = 0.0;
    std::function<void()> m_onArrived;
};

// Animación FLIP para reacomodar tarjetas en la grilla tras filtrar, crear o borrar.
void animateGridFlip(const QHash<QString, QPoint>& beforePos,
                     const QVector<QPair<QString, QWidget*>>& afterWidgets,
                     int staggerMs = 20);

// Zona punteada sobre la biblioteca al arrastrar PDF o imágenes desde fuera.
class DropZoneOverlay : public QWidget {
    Q_OBJECT
public:
    explicit DropZoneOverlay(QWidget* parent = nullptr);
    void setDark(bool dark);
    void setTargetName(const QString& targetName);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool m_dark = false;
    QString m_targetName;
};

// Fantasma inclinado (-6°) bajo el cursor al arrastrar documentos sobre una carpeta.
class DragGhostOverlay : public QWidget {
    Q_OBJECT
public:
    explicit DragGhostOverlay(QWidget* parent = nullptr);
    void updateDrag(const QPixmap& snap, const QPoint& posInHost,
                    const QString& targetLabel, int count, bool dark);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap m_snap;
    QPoint m_pos;
    QString m_targetLabel;
    int m_count = 0;
    bool m_dark = false;
};

} // namespace finily
