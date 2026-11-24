#pragma once
#include "core/Types.h"
#include <QPainter>
#include <QPolygonF>
#include <QSet>

namespace finily {
void selectByLasso(const QPolygonF& lassoPoly,const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,QSet<QString>& outStrokeIds,QSet<QString>& outShapeIds);
QRectF computeSelectionBounds(const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds);
void translateSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& delta);
void scaleSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& anchor,double scaleX,double scaleY);
void rotateSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& center,double angleRad);
void flipSelection(QVector<Stroke>& strokes, QVector<ShapeItem>& shapes,
                   const QSet<QString>& strokeIds, const QSet<QString>& shapeIds,
                   const QRectF& bounds, bool horizontal);
bool shiftContentVertically(QVector<Stroke>& strokes, QVector<ShapeItem>& shapes,
                            QVector<ImageItem>& images, QVector<TextItem>& texts,
                            double yThreshold, double deltaY);
bool straightenSelectedStrokes(QVector<Stroke>& strokes,const QSet<QString>& strokeIds);
void drawLassoPath(QPainter& painter,const QPolygonF& poly,const ThemeConfig& theme,double zoom);
void drawLassoSelectionBox(QPainter& painter,const QRectF& bounds,const ThemeConfig& theme,double zoom,bool showHandles = true);
int hitTestLassoHandle(const QRectF& bounds,const QPointF& worldPos,double zoom);
} // namespace finily
