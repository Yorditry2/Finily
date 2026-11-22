#pragma once
#include "core/Types.h"
#include <QPainter>
#include <optional>

namespace finily {
// Academy shapes share the same path, bounds and hit-testing contract:
// RightAngle uses [arm end, corner, arm end], Brace uses directed endpoints,
// and Axes uses a rectangle with four independently draggable corner handles.
QRectF computeShapeBounds(const ShapeItem& shape);
QVector<QPointF> getShapeHandles(const ShapeItem& shape);
void applyHandleDrag(ShapeItem& shape,int handleIndex,const QPointF& newWorldPos);
void translateShape(ShapeItem& shape,const QPointF& delta);
bool hitTestShape(const ShapeItem& shape,const QPointF& worldPos,double tolerance);
int hitTestHandle(const ShapeItem& shape,const QPointF& worldPos,double handleRadiusWorld);
void drawShape(QPainter& painter,const ShapeItem& shape);
void drawSelectionOverlay(QPainter& painter,const ShapeItem& shape,const ThemeConfig& theme,double zoom);
void drawSmartGuides(QPainter& painter,const QVector<SmartGuide>& guides,const ThemeConfig& theme,double zoom);
struct SnapResult { QPointF point; QVector<SmartGuide> guides; };
SnapResult snapPointWithGuides(const QPointF& rawWorld,const std::optional<QPointF>& anchorWorld,
                              const QVector<ShapeItem>& otherShapes,const QString& excludeShapeId,double zoom);
std::optional<ShapeItem> recognizeFreehandShape(const Stroke& stroke);
} // namespace finily
