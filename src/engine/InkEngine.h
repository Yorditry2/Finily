#pragma once
#include "core/Types.h"
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
QRectF computeBounds(const QVector<StrokePoint>& points, double padding = 0.0);
bool hitTestStroke(const Stroke& stroke, const QPointF& worldPos, double radius);
std::optional<QVector<Stroke>> eraseStrokePartially(const Stroke& stroke, const QPointF& worldPos, double radius);
void drawStroke(QPainter& painter, const Stroke& stroke);
void drawBackgroundTemplate(QPainter& painter, BackgroundTemplate tpl, const ThemeConfig& theme, const Viewport& vp, const QRectF& visibleWorldRect);
} // namespace finily
