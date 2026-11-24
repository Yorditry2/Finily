#pragma once

#include "core/Types.h"

#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace finily::geometry_detail {

inline double length(QPointF p) { return std::hypot(p.x(), p.y()); }
inline double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }

inline QPointF projectSegment(QPointF p, QPointF a, QPointF b) {
    const auto d = b - a;
    const double d2 = dot(d, d);
    return a + d * (d2 > 1e-12 ? std::clamp(dot(p - a, d) / d2, 0.0, 1.0) : 0.0);
}

inline double segmentDistance(QPointF p, QPointF a, QPointF b) {
    return length(p - projectSegment(p, a, b));
}

inline QRectF vertexBounds(const QVector<QPointF>& vertices) {
    if (vertices.isEmpty())
        return {};
    double l = vertices[0].x(), r = l, t = vertices[0].y(), b = t;
    for (auto p : vertices) {
        l = std::min(l, p.x());
        r = std::max(r, p.x());
        t = std::min(t, p.y());
        b = std::max(b, p.y());
    }
    return {QPointF(l, t), QPointF(r, b)};
}

bool isMarkLabel(const QString& label);
QPainterPath shapePath(const ShapeItem& shape);
QVector<QPolygonF> shapeOutlines(const ShapeItem& shape);

} // namespace finily::geometry_detail
