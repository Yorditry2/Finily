#pragma once
#include "core/Types.h"
#include <QPainter>
#include <QSet>
#include <optional>

namespace finily {
// Academy shapes share the same path, bounds and hit-testing contract:
// RightAngle uses [arm end, corner, arm end], Brace uses directed endpoints,
// and Axes uses a rectangle with four independently draggable corner handles.
QRectF computeShapeBounds(const ShapeItem& shape);
QVector<QPointF> getShapeHandles(const ShapeItem& shape);
void applyHandleDrag(ShapeItem& shape,int handleIndex,const QPointF& newWorldPos);
void translateShape(ShapeItem& shape,const QPointF& delta);
void scaleShape(ShapeItem& shape, QPointF anchor, double scaleX, double scaleY);
QRectF shapeGeometryBounds(const ShapeItem& shape);
bool containsShapeInterior(const ShapeItem& shape, QPointF point);
bool canFillShape(const ShapeItem& shape);
bool supportsLineEnding(const ShapeItem& shape);
int hitTestResizeHandle(const ShapeItem& shape, QPointF point, double zoom);
bool hitTestShape(const ShapeItem& shape,const QPointF& worldPos,double tolerance);
int hitTestHandle(const ShapeItem& shape,const QPointF& worldPos,double handleRadiusWorld);
void drawShape(QPainter& painter,const ShapeItem& shape);
void drawSelectionOverlay(QPainter& painter,const ShapeItem& shape,const ThemeConfig& theme,double zoom,
                          bool showResizeHandles = false);
void drawSmartGuides(QPainter& painter,const QVector<SmartGuide>& guides,const ThemeConfig& theme,double zoom);
struct SnapResult { QPointF point; QVector<SmartGuide> guides; bool contact = false; };
SnapResult snapShapeVertex(const ShapeItem& shape, int index, const SnapResult& external,
                          QPointF raw, double zoom);
SnapResult snapPointWithGuides(const QPointF& rawWorld,const std::optional<QPointF>& anchorWorld,
                              const QVector<ShapeItem>& otherShapes,const QString& excludeShapeId,double zoom,
                              bool align = true);
std::optional<ShapeItem> shadedRegionAt(const QPointF& point, const QVector<ShapeItem>& shapes,
                                      const QColor& color, ShadePattern pattern);
std::optional<ShapeItem> shadedRegionForShape(const ShapeItem& shape, const QColor& color, ShadePattern pattern);
void initializeShapeLinks(QVector<ShapeItem>& shapes);
void attachNewShape(QVector<ShapeItem>& shapes);
bool updateShapeLinks(QVector<ShapeItem>& shapes, const QVector<ShapeItem>& original,
                      const QSet<QString>& edited);
std::optional<ShapeItem> recognizeFreehandShape(const Stroke& stroke);
} // namespace finily
