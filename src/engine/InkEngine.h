#pragma once

#include "core/Types.h"

#include <QImage>
#include <QPainter>
#include <optional>

namespace finily {

class VelocityGaussianStabilizer {
public:
    void reset();
    void setStrength(double strength);
    StrokePoint filter(const StrokePoint& point);

private:
    double m_strength = 0.15;
    QVector<StrokePoint> m_recent;
    std::optional<double> m_smoothedPressure;
};

// Tinta en vivo: la parte ya "congelada" del trazo se pinta una sola vez en una
// capa de píxeles; en cada cuadro solo se vuelve a dibujar la cola.
class LiveInkRenderer {
public:
    static bool eligible(const Stroke& stroke);
    void reset();
    void reset(QImage& layer);
    bool usable(const Viewport& vp, const QSize& pixels, double dpr = 1.0) const;
    qsizetype frozenPointIndex() const { return m_frozenPoint; }
    void advance(const Stroke& stroke, QImage& layer, const Viewport& vp,
                 const QSize& pixels, double dpr);
    void drawTail(QPainter& painter, const Stroke& stroke) const;

private:
    qsizetype m_frozenPoint = -1;
    QVector<double> m_pointDist;
    QRect m_dirtyRect;
    bool m_layerDirty = false;
    Viewport m_vp;
    QSize m_pixels;
    double m_dpr = 1.0;
};

QRectF computeBounds(const QVector<StrokePoint>& points, double padding = 0.0);
bool hitTestStroke(const Stroke& stroke, const QPointF& worldPos, double radius);
std::optional<QVector<Stroke>> eraseStrokePartially(const Stroke& stroke,
                                                    const QPointF& worldPos,
                                                    double radius);
void drawStroke(QPainter& painter, const Stroke& stroke, bool lightBackground = false);
// Grosor relativo (0..~1.25) que da cada perfil a una presión cruda [0,1].
double inkPressureFactor(double pressure, PressureProfile profile);
void drawBackgroundTemplate(QPainter& painter, BackgroundTemplate tpl, const ThemeConfig& theme,
                            const Viewport& vp, const QRectF& visibleWorldRect,
                            double templateScale = 1.0, const QRectF& sheetRect = QRectF());

} // namespace finily
