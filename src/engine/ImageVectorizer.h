#pragma once

#include "core/Types.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QVector>

namespace finily {

struct VectorizeOptions {
    QColor inkColor = QColor(QStringLiteral("#f8fafc"));
    double strokeSize = 2.6;
    bool detectRightAngles = false;
    bool promotePolygons = true;
};

struct VectorizeResult {
    QVector<ShapeItem> shapes;
    QRectF detectedDiagramRect; // En coordenadas de píxeles de la imagen original
    qint64 elapsedMs = 0;
};

// Vectoriza localmente (100% C++, sin internet, <50 ms) las figuras geométricas
// de una captura de problema (triángulos, rectángulos, segmentos, círculos, arcos y 90°)
// filtrando el texto pequeño y mapeando las coordenadas exactas dentro de targetRect.
VectorizeResult vectorizeGeometryFromImage(const QImage& image,
                                           const QRectF& targetRect,
                                           const VectorizeOptions& options = {});

} // namespace finily
