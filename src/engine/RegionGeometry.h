#pragma once

#include <QPainterPath>
#include <QPolygonF>
#include <QHash>

namespace finily {
// Merge numerical discrepancies between flattened contours, up to .06 px at maximum zoom.
inline constexpr double contourJoinTolerance = .012;
struct RegionOutline {
    QPolygonF points;
    double strokeRadius;
    QString sourceId;
};
struct RegionFace {
    QPainterPath path;
    QHash<QString, int> boundary;
};
RegionFace closedFaceAt(QPointF point, const QVector<RegionOutline>& outlines,
                        const QHash<QString, int>& expected = {});
}
