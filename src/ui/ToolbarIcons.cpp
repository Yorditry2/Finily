#include "ui/ToolbarIcons.h"

#include "ui/IconLibrary.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {

// Iconos vectoriales dibujados en código (sin recursos externos), 22x22 lógicos.
QPixmap glyphPixmap(const QString& kind, const QColor& color) {
    QPixmap pixmap(44, 44);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Trazo ligeramente más grueso (2.1 px) para íconos nítidos y legibles
    // sobre cápsulas claras y oscuras.
    const QPen pen(color, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    if (kind == "home") {
        // Casita nítida: tejado, cuerpo y puerta (2.1 px, trazos redondeados).
        painter.drawPolyline(QPolygonF() << QPointF(3.6, 10.6) << QPointF(11.0, 4.2) << QPointF(18.4, 10.6));
        painter.drawPolyline(QPolygonF() << QPointF(5.6, 9.4) << QPointF(5.6, 18.0) << QPointF(16.4, 18.0)
                                         << QPointF(16.4, 9.4));
        painter.drawPolyline(QPolygonF() << QPointF(9.3, 18.0) << QPointF(9.3, 13.2) << QPointF(13.0, 13.2)
                                         << QPointF(13.0, 18.0));
    } else if (kind == "undo") {
        QPainterPath curve;
        curve.moveTo(16.5, 16.0);
        curve.cubicTo(16.5, 10.5, 13.0, 8.0, 8.0, 8.0);
        painter.drawPath(curve);
        painter.drawPolyline(QPolygonF() << QPointF(11.0, 5.0) << QPointF(7.5, 8.0) << QPointF(11.0, 11.0));
    } else if (kind == "redo") {
        QPainterPath curve;
        curve.moveTo(5.5, 16.0);
        curve.cubicTo(5.5, 10.5, 9.0, 8.0, 14.0, 8.0);
        painter.drawPath(curve);
        painter.drawPolyline(QPolygonF() << QPointF(11.0, 5.0) << QPointF(14.5, 8.0) << QPointF(11.0, 11.0));
    } else if (kind == "pen") {
        QPainterPath nib;
        nib.moveTo(4.0, 18.5);
        nib.lineTo(4.0, 15.5);
        nib.lineTo(7.0, 18.5);
        nib.closeSubpath();
        painter.drawPath(nib);
        painter.drawLine(QPointF(5.6, 16.9), QPointF(16.5, 6.0));
        painter.drawLine(QPointF(16.5, 6.0), QPointF(18.5, 4.0));
    } else if (kind == "ballpoint") {
        painter.drawLine(QPointF(5.0, 17.5), QPointF(17.5, 5.0));
        painter.drawEllipse(QPointF(18.2, 4.2), 1.3, 1.3);
        painter.drawLine(QPointF(8.5, 14.0), QPointF(10.5, 16.0));
    } else if (kind == "highlighter") {
        QPen thick(color, 4.6, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
        painter.setPen(thick);
        painter.drawLine(QPointF(8.0, 14.5), QPointF(14.5, 8.0));
        painter.setPen(pen);
        painter.drawLine(QPointF(4.5, 18.0), QPointF(8.0, 14.5));
    } else if (kind == "eraser") {
        painter.save();
        painter.translate(11.0, 11.0);
        painter.rotate(-42.0);
        painter.drawRoundedRect(QRectF(-6.0, -4.0, 12.0, 8.0), 2.0, 2.0);
        painter.drawLine(QPointF(1.8, -4.0), QPointF(1.8, 4.0));
        painter.restore();
    } else if (kind == "hand") {
        painter.drawLine(QPointF(11.0, 4.5), QPointF(11.0, 17.5));
        painter.drawLine(QPointF(4.5, 11.0), QPointF(17.5, 11.0));
        painter.drawPolyline(QPolygonF() << QPointF(9.0, 6.5) << QPointF(11.0, 4.5) << QPointF(13.0, 6.5));
        painter.drawPolyline(QPolygonF() << QPointF(9.0, 15.5) << QPointF(11.0, 17.5) << QPointF(13.0, 15.5));
        painter.drawPolyline(QPolygonF() << QPointF(6.5, 9.0) << QPointF(4.5, 11.0) << QPointF(6.5, 13.0));
        painter.drawPolyline(QPolygonF() << QPointF(15.5, 9.0) << QPointF(17.5, 11.0) << QPointF(15.5, 13.0));
    } else if (kind == "stroke") {
        QPainterPath stroke;
        stroke.moveTo(3.5, 16.5);
        stroke.cubicTo(8.0, 9.0, 12.0, 18.5, 18.5, 6.5);
        painter.strokePath(stroke, pen);
    } else if (kind == "paper") {
        painter.drawRoundedRect(QRectF(5.5, 3.5, 11.0, 15.0), 2.0, 2.0);
        painter.drawLine(QPointF(8.0, 7.5), QPointF(14.0, 7.5));
        painter.drawLine(QPointF(8.0, 11.0), QPointF(14.0, 11.0));
        painter.drawLine(QPointF(8.0, 14.5), QPointF(12.0, 14.5));
    } else if (kind == "shapes") {
        painter.drawEllipse(QPointF(8.0, 7.5), 4.2, 4.2);
        painter.drawRoundedRect(QRectF(9.0, 9.5, 8.5, 8.5), 1.5, 1.5);
    } else if (kind == "stickers") {
        // Tarjeta/pizarra con triángulo geométrico y rayita de fórmula abajo
        painter.drawRoundedRect(QRectF(3.2, 3.2, 15.6, 15.6), 2.8, 2.8);
        painter.drawPolygon(QPolygonF{{11.0, 6.2}, {15.0, 12.2}, {7.0, 12.2}});
        painter.drawLine(QPointF(6.8, 15.4), QPointF(15.2, 15.4));
    } else if (kind == "polyline") {
        const QVector<QPointF> vertices = {QPointF(3.5, 16.0), QPointF(8.5, 9.0), QPointF(13.0, 13.5),
                                           QPointF(18.5, 6.0)};
        painter.drawPolyline(QPolygonF(vertices));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        for (const QPointF& v : vertices)
            painter.drawEllipse(v, 1.4, 1.4);
        painter.setBrush(Qt::NoBrush);
    } else if (kind == "shape-line") {
        painter.drawLine(QPointF(4.0, 17.5), QPointF(18.0, 4.5));
    } else if (kind == "shape-arrow") {
        painter.drawLine(QPointF(4.0, 17.5), QPointF(17.5, 5.0));
        painter.drawLine(QPointF(17.5, 5.0), QPointF(12.0, 5.6));
        painter.drawLine(QPointF(17.5, 5.0), QPointF(16.6, 10.6));
    } else if (kind == "shape-polygon") {
        QPolygonF hex;
        for (int i = 0; i < 6; ++i) {
            const double a = std::numbers::pi / 6.0 + i * std::numbers::pi / 3.0;
            hex << QPointF(11.0 + 7.4 * std::cos(a), 11.0 + 7.4 * std::sin(a));
        }
        painter.drawPolygon(hex);
    } else if (kind == "shape-arrow2") {
        painter.drawLine(QPointF(4.5, 17.0), QPointF(17.5, 5.0));
        painter.drawPolyline(QPolygonF() << QPointF(12.0, 5.6) << QPointF(17.5, 5.0) << QPointF(16.6, 10.6));
        painter.drawPolyline(QPolygonF() << QPointF(5.4, 11.4) << QPointF(4.5, 17.0) << QPointF(10.0, 16.4));
    } else if (kind == "shape-arrowcurve") {
        QPainterPath arc;
        arc.moveTo(3.5, 16.5);
        arc.cubicTo(6.0, 5.0, 13.0, 4.5, 18.0, 11.0);
        painter.strokePath(arc, pen);
        painter.drawPolyline(QPolygonF() << QPointF(13.6, 11.2) << QPointF(18.0, 11.0) << QPointF(17.6, 6.6));
    } else if (kind == "shape-tri-right") {
        painter.drawPolygon(QPolygonF() << QPointF(4.5, 4.5) << QPointF(18.0, 17.5) << QPointF(4.5, 17.5));
        painter.drawPolyline(QPolygonF() << QPointF(4.5, 13.5) << QPointF(8.5, 13.5) << QPointF(8.5, 17.5));
    } else if (kind == "shape-tri-equi") {
        painter.drawPolygon(QPolygonF() << QPointF(11.0, 4.5) << QPointF(18.5, 17.5) << QPointF(3.5, 17.5));
    } else if (kind.startsWith("shape-poly-")) {
        const int n = kind.mid(11).toInt();
        QPolygonF poly;
        for (int i = 0; i < n; ++i) {
            const double a = -std::numbers::pi / 2.0 + i * 2.0 * std::numbers::pi / n;
            poly << QPointF(11.0 + 7.4 * std::cos(a), 11.0 + 7.4 * std::sin(a));
        }
        painter.drawPolygon(poly);
    } else if (kind == "shape-circle") {
        painter.drawEllipse(QPointF(11.0, 11.0), 6.5, 6.5);
    } else if (kind == "shape-ellipse") {
        painter.drawEllipse(QPointF(11.0, 11.0), 7.5, 4.5);
    } else if (kind == "shape-triangle") {
        painter.drawPolygon(QPolygonF() << QPointF(11.0, 4.0) << QPointF(18.5, 17.5) << QPointF(3.5, 17.5));
    } else if (kind == "shape-rect") {
        painter.drawRoundedRect(QRectF(4.5, 6.0, 13.0, 10.0), 1.5, 1.5);
    } else if (kind == "shape-radical") {
        painter.drawPolyline(QPolygonF() << QPointF(3.5, 12.5) << QPointF(6.0, 16.0) << QPointF(10.5, 5.5)
                                         << QPointF(19.0, 5.5));
    } else if (kind == "shape-rightangle") {
        // Marca de ángulo recto: catetos + cuadradito de 90°.
        painter.drawLine(QPointF(4.0, 17.0), QPointF(17.5, 17.0));
        painter.drawLine(QPointF(4.0, 17.0), QPointF(4.0, 3.5));
        painter.drawRect(QRectF(4.0, 13.0, 4.0, 4.0));
    } else if (kind == "shape-brace") {
        // Llaves { } con curvas Bézier simétricas.
        QPainterPath leftBrace;
        leftBrace.moveTo(9.0, 3.5);
        leftBrace.cubicTo(5.5, 3.5, 6.5, 7.5, 4.5, 11.0);
        leftBrace.cubicTo(6.5, 14.5, 5.5, 18.5, 9.0, 18.5);
        painter.strokePath(leftBrace, pen);
        QPainterPath rightBrace;
        rightBrace.moveTo(13.0, 3.5);
        rightBrace.cubicTo(16.5, 3.5, 15.5, 7.5, 17.5, 11.0);
        rightBrace.cubicTo(15.5, 14.5, 16.5, 18.5, 13.0, 18.5);
        painter.strokePath(rightBrace, pen);
    } else if (kind == "shape-axes") {
        // Plano cartesiano: eje Y, eje X con puntas de flecha.
        painter.drawLine(QPointF(5.0, 4.0), QPointF(5.0, 18.0));
        painter.drawLine(QPointF(5.0, 18.0), QPointF(19.0, 18.0));
        painter.drawPolyline(QPolygonF() << QPointF(3.4, 6.0) << QPointF(5.0, 4.0) << QPointF(6.6, 6.0));
        painter.drawPolyline(QPolygonF() << QPointF(17.2, 16.4) << QPointF(19.0, 18.0) << QPointF(17.2, 19.6));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(11.0, 12.0), 1.3, 1.3);
        painter.setBrush(Qt::NoBrush);
    } else if (kind == "page-delete") {
        // Papelera sutil para eliminar la página actual.
        painter.drawLine(QPointF(5.5, 6.5), QPointF(16.5, 6.5));
        painter.drawLine(QPointF(9.0, 6.5), QPointF(9.0, 4.5));
        painter.drawLine(QPointF(13.0, 6.5), QPointF(13.0, 4.5));
        painter.drawLine(QPointF(9.0, 4.5), QPointF(13.0, 4.5));
        painter.drawPolyline(QPolygonF() << QPointF(7.0, 6.5) << QPointF(7.8, 17.5) << QPointF(14.2, 17.5)
                                         << QPointF(15.0, 6.5));
        painter.drawLine(QPointF(10.0, 9.5), QPointF(10.0, 14.5));
        painter.drawLine(QPointF(12.0, 9.5), QPointF(12.0, 14.5));
    } else if (kind == "lasso") {
        QPen dashed(color, 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(dashed);
        painter.drawEllipse(QRectF(5.0, 3.5, 12.5, 9.0));
        painter.setPen(pen);
        painter.drawLine(QPointF(9.5, 12.0), QPointF(7.5, 16.5));
        painter.drawEllipse(QPointF(7.0, 18.0), 1.7, 1.7);
    } else if (kind == "text") {
        // Glifo 'T' nítido: barra superior + asta central.
        painter.drawLine(QPointF(4.0, 4.5), QPointF(18.0, 4.5));
        painter.drawLine(QPointF(11.0, 4.5), QPointF(11.0, 18.0));
        painter.drawLine(QPointF(8.5, 4.5), QPointF(8.5, 7.0));
        painter.drawLine(QPointF(13.5, 4.5), QPointF(13.5, 7.0));
    } else if (kind == "laser") {
        // Punto láser brillante con haz: núcleo coral-rojo fijo (#f87171)
        // + rayos y estela con el color del tema.
        painter.setBrush(QColor("#f87171"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(7.0, 15.5), 2.6, 2.6);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        for (const QPointF& dir : {QPointF(0.0, -1.0), QPointF(0.87, -0.5), QPointF(1.0, 0.0),
                                   QPointF(0.5, 0.87), QPointF(-0.5, 0.87), QPointF(-1.0, 0.0)}) {
            painter.drawLine(QPointF(7.0, 15.5) + dir * 4.2, QPointF(7.0, 15.5) + dir * 6.6);
        }
        QPen beam(color, 2.4, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(beam);
        painter.drawLine(QPointF(9.6, 12.9), QPointF(18.0, 4.5));
    } else if (kind == "shape-curve") {
        // Arco suave editable.
        QPainterPath arc;
        arc.moveTo(3.5, 16.5);
        arc.cubicTo(7.0, 4.5, 15.0, 4.5, 18.5, 16.5);
        painter.strokePath(arc, pen);
    } else if (kind == "shape-arc") {
        painter.drawArc(QRectF(3.5, 5.0, 15.0, 15.0), 20 * 16, 140 * 16);
    } else if (kind == "shape-sector") {
        painter.drawPie(QRectF(2.5, 2.5, 16.5, 16.5), 10 * 16, 100 * 16);
    } else if (kind == "shape-angle") {
        const QPointF center(4.0, 16.0);
        painter.drawLine(center, QPointF(18.5, 16.0));
        painter.drawLine(center, QPointF(15.0, 5.0));
        painter.drawArc(QRectF(center - QPointF(7, 7), QSizeF(14, 14)), 0, 45 * 16);
    } else if (kind == "shape-dimension") {
        // Línea de cota |——   ——| con topes en extremos y hueco central para medida.
        painter.drawLine(QPointF(3.5, 7.0), QPointF(3.5, 15.0));
        painter.drawLine(QPointF(18.5, 7.0), QPointF(18.5, 15.0));
        painter.drawLine(QPointF(3.5, 11.0), QPointF(8.2, 11.0));
        painter.drawLine(QPointF(13.8, 11.0), QPointF(18.5, 11.0));
    } else if (kind == "shape-parallelogram") {
        painter.drawPolygon(QPolygonF{{6.5, 6.0}, {18.5, 6.0}, {15.5, 16.0}, {3.5, 16.0}});
    } else if (kind == "shape-trap-iso") {
        painter.drawPolygon(QPolygonF{{6.5, 6.0}, {15.5, 6.0}, {18.5, 16.0}, {3.5, 16.0}});
    } else if (kind == "shape-trap-right") {
        painter.drawPolygon(QPolygonF{{4.5, 6.0}, {13.5, 6.0}, {18.0, 16.0}, {4.5, 16.0}});
    } else if (kind == "shape-trapezoid") {
        painter.drawPolygon(QPolygonF{{6.0, 5.5}, {17.0, 8.0}, {18.5, 16.5}, {3.5, 15.0}});
    } else if (kind == "shape-poly-7") {
        QPolygonF poly;
        for (int i = 0; i < 7; ++i) {
            const double a = -1.5708 + i * (6.283185 / 7.0);
            poly << QPointF(11.0 + 7.2 * std::cos(a), 11.0 + 7.2 * std::sin(a));
        }
        painter.drawPolygon(poly);
    } else if (kind == "shape-3d-cube") {
        painter.drawRect(QRectF(4.0, 7.5, 9.5, 9.5));
        painter.drawLine(QPointF(4.0, 7.5), QPointF(8.0, 4.0));
        painter.drawLine(QPointF(13.5, 7.5), QPointF(17.5, 4.0));
        painter.drawLine(QPointF(13.5, 17.0), QPointF(17.5, 13.5));
        painter.drawLine(QPointF(8.0, 4.0), QPointF(17.5, 4.0));
        painter.drawLine(QPointF(17.5, 4.0), QPointF(17.5, 13.5));
    } else if (kind == "shape-3d-prism") {
        painter.drawPolygon(QPolygonF{{11.0, 3.5}, {5.0, 7.5}, {17.0, 7.5}});
        painter.drawLine(QPointF(5.0, 7.5), QPointF(5.0, 17.5));
        painter.drawLine(QPointF(17.0, 7.5), QPointF(17.0, 17.5));
        painter.drawLine(QPointF(5.0, 17.5), QPointF(17.0, 17.5));
    } else if (kind == "shape-3d-cyl") {
        painter.drawEllipse(QRectF(5.0, 3.5, 12.0, 4.2));
        painter.drawLine(QPointF(5.0, 5.6), QPointF(5.0, 16.4));
        painter.drawLine(QPointF(17.0, 5.6), QPointF(17.0, 16.4));
        painter.drawArc(QRectF(5.0, 14.3, 12.0, 4.2), 180 * 16, 180 * 16);
    } else if (kind == "shape-3d-cone") {
        painter.drawLine(QPointF(11.0, 3.5), QPointF(4.5, 16.2));
        painter.drawLine(QPointF(11.0, 3.5), QPointF(17.5, 16.2));
        painter.drawEllipse(QRectF(4.5, 14.2, 13.0, 4.0));
    } else if (kind == "shape-3d-sphere") {
        painter.drawEllipse(QPointF(11.0, 11.0), 7.2, 7.2);
        painter.drawEllipse(QRectF(3.8, 8.6, 14.4, 4.8));
    } else if (kind == "shape-3d-pyr") {
        painter.drawLine(QPointF(11.0, 3.5), QPointF(4.0, 15.5));
        painter.drawLine(QPointF(11.0, 3.5), QPointF(12.5, 18.0));
        painter.drawLine(QPointF(11.0, 3.5), QPointF(18.0, 14.0));
        painter.drawLine(QPointF(4.0, 15.5), QPointF(12.5, 18.0));
        painter.drawLine(QPointF(12.5, 18.0), QPointF(18.0, 14.0));
    } else if (kind == "shape-3d-tetra") {
        painter.drawLine(QPointF(10.5, 3.5), QPointF(4.0, 16.5));
        painter.drawLine(QPointF(10.5, 3.5), QPointF(12.8, 18.0));
        painter.drawLine(QPointF(10.5, 3.5), QPointF(18.0, 14.0));
        painter.drawLine(QPointF(4.0, 16.5), QPointF(12.8, 18.0));
        painter.drawLine(QPointF(12.8, 18.0), QPointF(18.0, 14.0));
    } else if (kind == "shape-3d-octa") {
        painter.drawPolygon(QPolygonF{{11.0, 3.0}, {17.5, 11.0}, {11.0, 19.0}, {4.5, 11.0}});
        painter.drawLine(QPointF(4.5, 11.0), QPointF(10.0, 12.8));
        painter.drawLine(QPointF(10.0, 12.8), QPointF(17.5, 11.0));
        painter.drawLine(QPointF(11.0, 3.0), QPointF(10.0, 12.8));
        painter.drawLine(QPointF(11.0, 19.0), QPointF(10.0, 12.8));
    } else if (kind == "shape-3d-fcone") {
        painter.drawEllipse(QRectF(7.2, 4.0, 7.6, 3.0));
        painter.drawLine(QPointF(7.2, 5.5), QPointF(4.2, 16.2));
        painter.drawLine(QPointF(14.8, 5.5), QPointF(17.8, 16.2));
        painter.drawArc(QRectF(4.2, 14.2, 13.6, 4.0), 180 * 16, 180 * 16);
    } else if (kind == "shape-3d-fpyr") {
        painter.drawPolygon(QPolygonF{{7.0, 5.5}, {13.5, 6.8}, {15.5, 4.5}, {9.0, 3.5}});
        painter.drawLine(QPointF(7.0, 5.5), QPointF(4.0, 15.5));
        painter.drawLine(QPointF(13.5, 6.8), QPointF(12.5, 18.0));
        painter.drawLine(QPointF(15.5, 4.5), QPointF(18.0, 14.0));
        painter.drawLine(QPointF(4.0, 15.5), QPointF(12.5, 18.0));
        painter.drawLine(QPointF(12.5, 18.0), QPointF(18.0, 14.0));
    } else if (kind == "mark-zigzag") {
        QPen faint(color, 1.2, Qt::DotLine);
        painter.setPen(faint);
        painter.drawLine(QPointF(3.0, 16.5), QPointF(19.0, 5.5));
        painter.setPen(pen);
        painter.drawPolyline(QPolygonF{{5.2, 15.0}, {6.4, 10.2}, {9.6, 14.8}, {10.8, 7.2},
                                       {14.0, 11.8}, {15.2, 4.4}, {16.8, 7.0}});
    } else if (kind == "mark-spring") {
        QPen faint(color, 1.2, Qt::DotLine);
        painter.setPen(faint);
        painter.drawLine(QPointF(3.0, 11.0), QPointF(19.0, 11.0));
        painter.setPen(pen);
        QPainterPath sp;
        sp.moveTo(4.0, 11.0);
        sp.quadTo(6.0, 5.0, 8.0, 11.0);
        sp.quadTo(10.0, 17.0, 12.0, 11.0);
        sp.quadTo(14.0, 5.0, 16.0, 11.0);
        sp.quadTo(17.5, 15.5, 18.5, 11.0);
        painter.drawPath(sp);
    } else if (kind == "mark-dot") {
        painter.drawLine(QPointF(3.5, 16.5), QPointF(18.5, 5.5));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(11.0, 11.0), 3.6, 3.6);
        painter.setBrush(Qt::NoBrush);
    } else if (kind == "mark-bar") {
        painter.drawLine(QPointF(3.5, 16.5), QPointF(18.5, 5.5));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.save();
        painter.translate(11.0, 11.0);
        painter.rotate(-36.0);
        painter.drawRoundedRect(QRectF(-5.2, -2.5, 10.4, 5.0), 1.6, 1.6);
        painter.restore();
        painter.setBrush(Qt::NoBrush);
    } else if (kind == "mark-ticks") {
        painter.drawLine(QPointF(3.5, 16.5), QPointF(18.5, 5.5));
        painter.drawLine(QPointF(7.2, 8.2), QPointF(11.6, 15.2));
        painter.drawLine(QPointF(10.4, 6.0), QPointF(14.8, 13.0));
    } else if (kind == "mark-parallel") {
        painter.drawLine(QPointF(3.5, 16.5), QPointF(18.5, 5.5));
        painter.drawPolyline(QPolygonF{{6.8, 9.2}, {11.8, 10.4}, {10.4, 15.4}});
        painter.drawPolyline(QPolygonF{{10.2, 6.8}, {15.2, 8.0}, {13.8, 13.0}});
    } else if (kind == "notebook-new") {
        painter.drawRoundedRect(QRectF(4.5, 3.5, 13.0, 15.0), 2.0, 2.0);
        painter.drawLine(QPointF(4.5, 7.0), QPointF(17.5, 7.0));
        painter.drawLine(QPointF(11.0, 10.5), QPointF(11.0, 16.0));
        painter.drawLine(QPointF(8.5, 13.2), QPointF(13.5, 13.2));
    } else if (kind == "notebook-open") {
        painter.drawPolyline(QPolygonF() << QPointF(3.0, 16.5) << QPointF(3.0, 6.0) << QPointF(8.0, 6.0)
                                         << QPointF(10.0, 8.0) << QPointF(17.0, 8.0));
        painter.drawPolyline(QPolygonF() << QPointF(3.0, 16.5) << QPointF(5.5, 10.0) << QPointF(20.0, 10.0)
                                         << QPointF(17.5, 16.5) << QPointF(3.0, 16.5));
    } else if (kind == "notebook-save") {
        painter.drawPolygon(QPolygonF() << QPointF(4.0, 4.0) << QPointF(16.5, 4.0) << QPointF(18.5, 6.0)
                                        << QPointF(18.5, 18.5) << QPointF(4.0, 18.5));
        painter.drawRect(QRectF(7.0, 12.5, 8.5, 6.0));
        painter.drawRect(QRectF(7.5, 4.5, 7.0, 4.0));
    } else if (kind == "notebook-save-as") {
        painter.drawPolygon(QPolygonF() << QPointF(3.5, 4.5) << QPointF(14.5, 4.5) << QPointF(16.5, 6.5)
                                        << QPointF(16.5, 12.5) << QPointF(3.5, 12.5));
        painter.drawRect(QRectF(6.0, 9.0, 8.0, 3.5));
        QPen edit(color, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(edit);
        painter.drawLine(QPointF(12.5, 18.5), QPointF(19.0, 12.0));
        painter.drawLine(QPointF(19.0, 12.0), QPointF(19.5, 14.5));
        painter.drawLine(QPointF(19.5, 14.5), QPointF(13.0, 19.0));
    } else if (kind == "file") {
        painter.drawLine(QPointF(3.5, 5.5), QPointF(8.0, 5.5));
        painter.drawLine(QPointF(8.0, 5.5), QPointF(10.0, 8.0));
        painter.drawLine(QPointF(10.0, 8.0), QPointF(18.5, 8.0));
        painter.drawRoundedRect(QRectF(3.5, 8.0, 15.0, 9.5), 1.5, 1.5);
        painter.drawLine(QPointF(7.5, 12.0), QPointF(14.5, 12.0));
        painter.drawLine(QPointF(7.5, 14.5), QPointF(12.5, 14.5));
    } else if (kind == "more") {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (double x : {5.0, 11.0, 17.0})
            painter.drawEllipse(QPointF(x, 11.0), 1.5, 1.5);
    } else if (kind == "pages") {
        painter.drawRoundedRect(QRectF(7.0, 3.5, 9.5, 12.0), 2.0, 2.0);
        painter.drawPolyline(QPolygonF() << QPointF(4.5, 7.0) << QPointF(4.5, 17.0) << QPointF(12.0, 17.0));
    } else if (kind == "present") {
        painter.drawRoundedRect(QRectF(3.5, 4.5, 15.0, 10.0), 2.5, 2.5);
        painter.drawLine(QPointF(8.0, 18.0), QPointF(14.0, 18.0));
    } else if (kind == "clean") {
        painter.drawEllipse(QPointF(11.0, 11.0), 7.0, 4.8);
        painter.drawLine(QPointF(5.5, 16.5), QPointF(16.5, 5.5));
    } else if (kind == "restore") {
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(5.0, 8.5, 12.0, 5.0), 2.5, 2.5);
    } else if (kind == "zoom-in" || kind == "zoom-out") {
        painter.drawEllipse(QPointF(9.5, 9.5), 5.5, 5.5);
        painter.drawLine(QPointF(13.8, 13.8), QPointF(18.5, 18.5));
        painter.drawLine(QPointF(7.0, 9.5), QPointF(12.0, 9.5));
        if (kind == "zoom-in")
            painter.drawLine(QPointF(9.5, 7.0), QPointF(9.5, 12.0));
    } else if (kind == "chevron-left") {
        painter.drawPolyline(QPolygonF() << QPointF(13.5, 5.5) << QPointF(8.0, 11.0) << QPointF(13.5, 16.5));
    } else if (kind == "chevron-right") {
        painter.drawPolyline(QPolygonF() << QPointF(8.5, 5.5) << QPointF(14.0, 11.0) << QPointF(8.5, 16.5));
    } else if (kind == "plus") {
        painter.drawLine(QPointF(11.0, 5.0), QPointF(11.0, 17.0));
        painter.drawLine(QPointF(5.0, 11.0), QPointF(17.0, 11.0));
    }
    return pixmap;
}

// Set único de íconos del dock: misma cuadrícula de 24, trazo de 1.7, sin
// colores sueltos. Reposo en el color de texto; activo en el color de acento.
QPixmap dockPixmap(const QString& kind, const QColor& color) {
    QPixmap pixmap(48, 48);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (kind == "pen") { // pluma: cuerpo inclinado + punta
        QPainterPath body;
        body.moveTo(15.5, 4.5);
        body.lineTo(19.5, 8.5);
        body.lineTo(9.5, 18.5);
        body.lineTo(4.5, 19.5);
        body.lineTo(5.5, 14.5);
        body.closeSubpath();
        p.drawPath(body);
        p.drawLine(QPointF(13.0, 7.0), QPointF(17.0, 11.0));
    } else if (kind == "highlighter") { // rotulador con punta biselada y trazo ancho
        p.save();
        p.translate(12.0, 11.0);
        p.rotate(45.0);
        p.drawRoundedRect(QRectF(-3.6, -9.0, 7.2, 10.0), 1.6, 1.6);
        QPainterPath nib;
        nib.moveTo(-3.6, 1.0);
        nib.lineTo(3.6, 1.0);
        nib.lineTo(2.2, 5.6);
        nib.lineTo(-2.2, 5.6);
        nib.closeSubpath();
        p.drawPath(nib);
        p.restore();
        p.setPen(QPen(color, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4.5, 20.0), QPointF(12.0, 20.0));
    } else if (kind == "eraser") {
        p.save();
        p.translate(12.0, 12.0);
        p.rotate(-45.0);
        p.drawRoundedRect(QRectF(-8.0, -4.5, 16.0, 9.0), 2.4, 2.4);
        p.drawLine(QPointF(-1.5, -4.5), QPointF(-1.5, 4.5));
        p.restore();
        p.drawLine(QPointF(12.0, 20.0), QPointF(20.0, 20.0));
    } else if (kind == "paint-bucket") {
        QPainterPath bucket;
        bucket.moveTo(4.5, 12.0);
        bucket.lineTo(11.0, 5.5);
        bucket.lineTo(17.0, 11.5);
        bucket.lineTo(10.5, 18.0);
        bucket.closeSubpath();
        p.drawPath(bucket);
        p.drawLine(QPointF(6.5, 10.0), QPointF(14.5, 10.0));
        QPainterPath drop;
        drop.moveTo(19.5, 14.0);
        drop.cubicTo(19.5, 16.0, 18.0, 17.0, 18.0, 18.0);
        drop.cubicTo(18.0, 19.2, 18.8, 19.8, 19.5, 19.8);
        drop.cubicTo(20.2, 19.8, 21.0, 19.2, 21.0, 18.0);
        drop.cubicTo(21.0, 17.0, 19.5, 16.0, 19.5, 14.0);
        p.drawPath(drop);
    } else if (kind == "select-pointer") {
        QPainterPath arrow;
        arrow.moveTo(6.0, 4.5);
        arrow.lineTo(18.0, 11.0);
        arrow.lineTo(12.2, 12.8);
        arrow.lineTo(9.6, 18.5);
        arrow.closeSubpath();
        p.drawPath(arrow);
    } else if (kind == "shapes") {
        p.drawEllipse(QPointF(9.0, 9.0), 4.8, 4.8);
        p.drawRoundedRect(QRectF(10.5, 10.5, 9.0, 9.0), 2.2, 2.2);
    } else if (kind == "stickers") {
        p.drawRoundedRect(QRectF(3.5, 3.5, 17.0, 17.0), 3.0, 3.0);
        p.drawPolygon(QPolygonF{{12.0, 6.8}, {16.2, 13.4}, {7.8, 13.4}});
        p.drawLine(QPointF(7.4, 16.8), QPointF(16.6, 16.8));
    } else if (kind == "lasso") {
        QPen dashed = pen;
        dashed.setStyle(Qt::CustomDashLine);
        dashed.setDashPattern({2.2, 2.6});
        p.setPen(dashed);
        p.drawEllipse(QRectF(3.5, 5.0, 17.0, 11.0));
        p.setPen(pen);
        p.drawLine(QPointF(9.0, 15.8), QPointF(8.0, 20.0));
    } else if (kind == "text") {
        p.drawLine(QPointF(6.0, 6.5), QPointF(18.0, 6.5));
        p.drawLine(QPointF(12.0, 6.5), QPointF(12.0, 19.0));
        p.drawLine(QPointF(9.5, 19.0), QPointF(14.5, 19.0));
    } else if (kind == "laser") {
        p.drawLine(QPointF(5.0, 19.0), QPointF(13.5, 10.5));
        p.drawEllipse(QPointF(16.0, 8.0), 2.2, 2.2);
        p.drawLine(QPointF(16.0, 2.8), QPointF(16.0, 4.0));
        p.drawLine(QPointF(16.0, 12.0), QPointF(16.0, 13.2));
        p.drawLine(QPointF(10.8, 8.0), QPointF(12.0, 8.0));
        p.drawLine(QPointF(20.0, 8.0), QPointF(21.2, 8.0));
    }
    return pixmap;
}

QIcon dockIcon(const QString& kind, const QColor& rest, const QColor& active) {
    QIcon icon;
    icon.addPixmap(dockPixmap(kind, rest), QIcon::Normal, QIcon::Off);
    icon.addPixmap(dockPixmap(kind, active), QIcon::Normal, QIcon::On);
    icon.addPixmap(dockPixmap(kind, active), QIcon::Active, QIcon::On);
    icon.addPixmap(dockPixmap(kind, rest), QIcon::Active, QIcon::Off);
    return icon;
}

QIcon glyphIcon(const QString& kind, const QColor& color, const QColor& onColor) {
    QIcon icon(glyphPixmap(kind, color));
    if (onColor.isValid())
        icon.addPixmap(glyphPixmap(kind, onColor), QIcon::Normal, QIcon::On);
    return icon;
}

// Miniaturas visuales reales para el tarrito de pintura (sólido, tono suave, rayado, punteado).
QPixmap fillSwatchPixmap(ShadePattern pattern, const QColor& color, bool isDark) {
    QPixmap pm(116, 72);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(3.0, 3.0, 52.0, 30.0);
    QPainterPath clip;
    clip.addRoundedRect(r, 6.0, 6.0);
    p.save();
    p.setClipPath(clip);
    p.fillRect(r, isDark ? QColor(255, 255, 255, 12) : QColor(15, 23, 42, 10));
    QColor fill = color;
    if (pattern == ShadePattern::Solid) {
        fill.setAlpha(235);
        p.fillRect(r, fill);
    } else if (pattern == ShadePattern::Soft) {
        fill.setAlpha(85);
        p.fillRect(r, fill);
    } else if (pattern == ShadePattern::Diagonal || pattern == ShadePattern::Crosshatch) {
        fill.setAlpha(42);
        p.fillRect(r, fill);
        QColor line = color;
        line.setAlpha(215);
        p.setPen(QPen(line, 1.35));
        for (double x = -30.0; x < 70.0; x += 6.0) {
            p.drawLine(QPointF(x, r.bottom()), QPointF(x + 30.0, r.top()));
            if (pattern == ShadePattern::Crosshatch)
                p.drawLine(QPointF(x, r.top()), QPointF(x + 30.0, r.bottom()));
        }
    } else if (pattern == ShadePattern::Grid) {
        fill.setAlpha(38);
        p.fillRect(r, fill);
        QColor line = color;
        line.setAlpha(205);
        p.setPen(QPen(line, 1.1));
        for (double x = r.left() + 6.0; x < r.right(); x += 7.0)
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        for (double y = r.top() + 6.0; y < r.bottom(); y += 7.0)
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    } else if (pattern == ShadePattern::Dotted) {
        fill.setAlpha(38);
        p.fillRect(r, fill);
        QColor dot = color;
        dot.setAlpha(220);
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        for (double y = r.top() + 5.0; y < r.bottom() - 2.0; y += 6.5)
            for (double x = r.left() + 5.0; x < r.right() - 2.0; x += 6.5)
                p.drawEllipse(QPointF(x, y), 1.35, 1.35);
    }
    p.restore();
    p.setPen(QPen(isDark ? QColor(255, 255, 255, 55) : QColor(15, 23, 42, 50), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 6.0, 6.0);
    p.end();
    return pm;
}

// Ilustraciones reales de puntas de lápiz (0=Bolígrafo, 1=Pluma, 2=Lápiz grafito, 3=Tiza).
QPixmap penTipPixmap(int tip, const QColor& ink, bool isDark) {
    QPixmap pm(116, 76);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor body = isDark ? QColor("#d5dde8") : QColor("#334155");
    const QColor metal = isDark ? QColor("#94a3b8") : QColor("#64748b");
    p.save();
    p.translate(29.0, 19.0);
    if (tip == 0) {
        // Bolígrafo: punta cónica fina + bolita de tinta
        QPainterPath barrel;
        barrel.moveTo(-4.0, 14.0); barrel.lineTo(-4.0, 2.0); barrel.lineTo(-1.2, -9.0);
        barrel.lineTo(1.2, -9.0); barrel.lineTo(4.0, 2.0); barrel.lineTo(4.0, 14.0); barrel.closeSubpath();
        p.setPen(Qt::NoPen); p.setBrush(body); p.drawPath(barrel);
        p.setBrush(ink); p.drawRect(QRectF(-4.0, 4.0, 8.0, 4.0));
        p.drawEllipse(QPointF(0.0, -10.5), 1.6, 1.6);
    } else if (tip == 1) {
        // Pluma fuente: plumín clásico con ranura y orificio
        QPainterPath nib;
        nib.moveTo(0.0, -12.0); nib.lineTo(5.2, 2.0); nib.lineTo(3.6, 13.0);
        nib.lineTo(-3.6, 13.0); nib.lineTo(-5.2, 2.0); nib.closeSubpath();
        p.setPen(Qt::NoPen); p.setBrush(metal); p.drawPath(nib);
        p.setPen(QPen(isDark ? QColor("#111827") : QColor("#f8fafc"), 1.2));
        p.drawLine(QPointF(0.0, -11.0), QPointF(0.0, 1.5));
        p.setPen(Qt::NoPen); p.setBrush(ink); p.drawEllipse(QPointF(0.0, 2.0), 1.8, 1.8);
    } else if (tip == 2) {
        // Lápiz grafito: cuerpo con madera cónica y mina del color activo
        p.setPen(Qt::NoPen);
        p.setBrush(body);
        p.drawRoundedRect(QRectF(-4.2, 1.0, 8.4, 13.0), 1.6, 1.6);
        QPainterPath wood;
        wood.moveTo(-4.2, 1.0); wood.lineTo(-1.4, -7.5); wood.lineTo(1.4, -7.5); wood.lineTo(4.2, 1.0); wood.closeSubpath();
        p.setBrush(isDark ? QColor("#d6b88d") : QColor("#e2c799"));
        p.drawPath(wood);
        QPainterPath lead;
        lead.moveTo(-1.4, -7.5); lead.lineTo(0.0, -12.0); lead.lineTo(1.4, -7.5); lead.closeSubpath();
        p.setBrush(ink);
        p.drawPath(lead);
    } else {
        // Tiza: barra de gis ligeramente biselada por el uso + motitas de polvo
        p.save();
        p.rotate(-14.0);
        QPainterPath chalk;
        chalk.moveTo(-4.2, -9.5);
        chalk.lineTo(3.6, -11.5);
        chalk.lineTo(4.6, 12.5);
        chalk.lineTo(-4.6, 12.5);
        chalk.closeSubpath();
        p.setPen(QPen(isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 60), 0.9));
        p.setBrush(ink);
        p.drawPath(chalk);
        p.setPen(Qt::NoPen);
        p.setBrush(isDark ? QColor(15, 23, 42, 95) : QColor(255, 255, 255, 130));
        p.drawRect(QRectF(-4.4, 4.5, 9.0, 6.5));
        p.restore();
        p.setPen(Qt::NoPen);
        QColor dust = ink;
        dust.setAlpha(170);
        p.setBrush(dust);
        p.drawEllipse(QPointF(-6.5, -10.0), 1.1, 1.1);
        p.drawEllipse(QPointF(6.2, -7.5), 0.9, 0.9);
    }
    p.restore();
    p.end();
    return pm;
}

// Ilustraciones minimalistas de resaltador (0=Libre, 1=Recto).
QPixmap hlTipPixmap(int tip, const QColor& ink, bool isDark) {
    QPixmap pm(144, 68);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor soft = ink;
    soft.setAlpha(135);
    if (tip == 0) {
        // Resaltador libre fluido
        p.setPen(QPen(soft, 9.0, Qt::SolidLine, Qt::RoundCap));
        QPainterPath wave;
        wave.moveTo(14.0, 20.0);
        wave.quadTo(36.0, 9.0, 58.0, 20.0);
        p.drawPath(wave);
    } else {
        // Subrayado recto
        p.setPen(QPen(soft, 8.5, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(12.0, 17.0), QPointF(60.0, 17.0));
        p.setPen(QPen(isDark ? QColor(255, 255, 255, 150) : QColor(15, 23, 42, 140), 1.1, Qt::DashLine));
        p.drawLine(QPointF(12.0, 17.0), QPointF(60.0, 17.0));
    }
    p.end();
    return pm;
}

// Íconos limpios 18x18 para la barra contextual pegada al objeto seleccionado.
QPixmap contextIconPixmap(const QString& kind, const QColor& color) {
    QPixmap pm(36, 36);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (kind == "duplicate") {
        p.drawRoundedRect(QRectF(6.5, 6.5, 8.0, 8.0), 1.8, 1.8);
        QPainterPath back;
        back.moveTo(4.0, 11.5);
        back.lineTo(4.0, 4.5);
        back.quadTo(4.0, 3.5, 5.0, 3.5);
        back.lineTo(11.5, 3.5);
        p.drawPath(back);
    } else if (kind == "trash") {
        p.drawLine(QPointF(3.5, 5.0), QPointF(14.5, 5.0));
        p.drawLine(QPointF(7.0, 3.2), QPointF(11.0, 3.2));
        p.drawRoundedRect(QRectF(5.0, 5.0, 8.0, 9.8), 1.4, 1.4);
        p.drawLine(QPointF(7.6, 7.6), QPointF(7.6, 12.2));
        p.drawLine(QPointF(10.4, 7.6), QPointF(10.4, 12.2));
    } else if (kind == "group") {
        p.drawRoundedRect(QRectF(3.5, 3.5, 6.5, 6.5), 1.4, 1.4);
        p.drawRoundedRect(QRectF(8.0, 8.0, 6.5, 6.5), 1.4, 1.4);
    } else if (kind == "ungroup") {
        p.drawRoundedRect(QRectF(3.0, 3.0, 5.5, 5.5), 1.2, 1.2);
        p.drawRoundedRect(QRectF(9.5, 9.5, 5.5, 5.5), 1.2, 1.2);
        p.drawLine(QPointF(11.5, 4.0), QPointF(14.0, 6.5));
        p.drawLine(QPointF(4.0, 11.5), QPointF(6.5, 14.0));
    } else if (kind == "check") {
        p.drawLine(QPointF(3.5, 9.5), QPointF(7.2, 13.2));
        p.drawLine(QPointF(7.2, 13.2), QPointF(14.5, 5.0));
    } else if (kind == "close-poly") {
        p.drawPolygon(QPolygonF{{9.0, 3.5}, {14.5, 13.5}, {3.5, 13.5}});
    } else if (kind == "straighten") {
        p.drawLine(QPointF(3.5, 12.5), QPointF(14.5, 5.5));
        p.drawEllipse(QPointF(3.5, 12.5), 1.4, 1.4);
        p.drawEllipse(QPointF(14.5, 5.5), 1.4, 1.4);
    } else if (kind == "totext") {
        p.drawLine(QPointF(4.5, 5.0), QPointF(13.5, 5.0));
        p.drawLine(QPointF(9.0, 5.0), QPointF(9.0, 14.0));
        p.drawLine(QPointF(7.0, 14.0), QPointF(11.0, 14.0));
    } else if (kind == "bookmark") {
        QPainterPath bm;
        bm.moveTo(5.0, 3.5);
        bm.lineTo(13.0, 3.5);
        bm.lineTo(13.0, 14.5);
        bm.lineTo(9.0, 11.5);
        bm.lineTo(5.0, 14.5);
        bm.closeSubpath();
        p.drawPath(bm);
    } else if (kind == "stabilizer") {
        QPainterPath wave;
        wave.moveTo(2.5, 11.5);
        wave.cubicTo(6.0, 4.0, 12.0, 14.0, 15.5, 6.5);
        p.drawPath(wave);
    } else if (kind == "opacity") {
        p.drawEllipse(QPointF(9.0, 9.0), 5.2, 5.2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPainterPath half;
        half.moveTo(9.0, 3.8);
        half.arcTo(QRectF(3.8, 3.8, 10.4, 10.4), 90.0, -180.0);
        half.closeSubpath();
        p.drawPath(half);
    } else if (kind == "eraser-stroke") {
        QPainterPath wave;
        wave.moveTo(2.5, 12.0);
        wave.cubicTo(6.0, 5.5, 12.0, 12.5, 15.5, 6.0);
        QColor faint = color;
        faint.setAlpha(135);
        p.setPen(QPen(faint, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(wave);
        p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(6.2, 5.8), QPointF(11.8, 12.2));
        p.drawLine(QPointF(11.8, 5.8), QPointF(6.2, 12.2));
    } else if (kind == "eraser-partial") {
        p.drawLine(QPointF(2.0, 9.0), QPointF(5.2, 9.0));
        p.drawLine(QPointF(12.8, 9.0), QPointF(16.0, 9.0));
        QPen dash(color, 1.4, Qt::CustomDashLine, Qt::RoundCap);
        dash.setDashPattern({2.0, 2.0});
        p.setPen(dash);
        p.drawEllipse(QPointF(9.0, 9.0), 3.6, 3.6);
    } else if (kind == "lasso-free") {
        QPen dash(color, 1.5, Qt::CustomDashLine, Qt::RoundCap);
        dash.setDashPattern({2.2, 2.5});
        p.setPen(dash);
        p.drawEllipse(QRectF(3.0, 4.5, 12.0, 8.5));
        p.setPen(pen);
        p.drawLine(QPointF(7.0, 13.0), QPointF(6.0, 15.5));
    } else if (kind == "lasso-rect") {
        QPen dash(color, 1.5, Qt::CustomDashLine, Qt::RoundCap);
        dash.setDashPattern({2.2, 2.5});
        p.setPen(dash);
        p.drawRoundedRect(QRectF(3.5, 4.0, 11.0, 10.0), 1.5, 1.5);
    } else if (kind == "line-style") {
        p.drawLine(QPointF(3.0, 5.5), QPointF(15.0, 5.5));
        QPen dash(color, 1.6, Qt::CustomDashLine, Qt::RoundCap);
        dash.setDashPattern({3.0, 2.5});
        p.setPen(dash);
        p.drawLine(QPointF(3.0, 9.5), QPointF(15.0, 9.5));
        QPen dot(color, 1.8, Qt::CustomDashLine, Qt::RoundCap);
        dot.setDashPattern({0.2, 2.8});
        p.setPen(dot);
        p.drawLine(QPointF(3.0, 13.5), QPointF(15.0, 13.5));
    } else if (kind == "lock") {
        p.drawRoundedRect(QRectF(4.5, 8.5, 9.0, 6.5), 1.6, 1.6);
        p.drawArc(QRectF(6.2, 3.8, 5.6, 7.0), 0, 180 * 16);
    } else if (kind == "minus") {
        p.drawLine(QPointF(4.5, 9.0), QPointF(13.5, 9.0));
    } else if (kind == "plus") {
        p.drawLine(QPointF(4.5, 9.0), QPointF(13.5, 9.0));
        p.drawLine(QPointF(9.0, 4.5), QPointF(9.0, 13.5));
    } else if (kind == "more") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(4.2, 9.0), 1.4, 1.4);
        p.drawEllipse(QPointF(9.0, 9.0), 1.4, 1.4);
        p.drawEllipse(QPointF(13.8, 9.0), 1.4, 1.4);
    } else if (kind == "fx") {
        QPainterPath fn;
        fn.moveTo(9.8, 4.2);
        fn.cubicTo(7.4, 3.8, 7.2, 6.2, 6.8, 9.0);
        fn.cubicTo(6.4, 11.8, 6.0, 14.2, 4.0, 13.8);
        p.drawPath(fn);
        p.drawLine(QPointF(5.0, 8.2), QPointF(9.0, 8.2));
        p.drawLine(QPointF(10.8, 8.5), QPointF(14.4, 13.2));
        p.drawLine(QPointF(14.4, 8.5), QPointF(10.8, 13.2));
    }
    return pm;
}

// Miniaturas para los 5 estilos de línea del popover "Trazo".
QPixmap patternPixmap(LinePattern pattern, const QColor& color) {
    QPixmap pixmap(80, 28);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    switch (pattern) {
        case LinePattern::Solid:
            painter.drawLine(QPointF(4.0, 7.0), QPointF(36.0, 7.0));
            break;
        case LinePattern::Dashed:
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.drawLine(QPointF(4.0, 7.0), QPointF(36.0, 7.0));
            break;
        case LinePattern::Dotted:
            pen.setStyle(Qt::DotLine);
            painter.setPen(pen);
            painter.drawLine(QPointF(4.0, 7.0), QPointF(36.0, 7.0));
            break;
        case LinePattern::Arrow:
            painter.drawLine(QPointF(4.0, 7.0), QPointF(31.0, 7.0));
            painter.drawPolyline(QPolygonF() << QPointF(26.5, 3.5) << QPointF(31.5, 7.0) << QPointF(26.5, 10.5));
            break;
        case LinePattern::DoubleArrow:
            painter.drawLine(QPointF(8.0, 7.0), QPointF(32.0, 7.0));
            painter.drawPolyline(QPolygonF() << QPointF(11.5, 3.5) << QPointF(7.0, 7.0) << QPointF(11.5, 10.5));
            painter.drawPolyline(QPolygonF() << QPointF(28.5, 3.5) << QPointF(33.0, 7.0) << QPointF(28.5, 10.5));
            break;
        case LinePattern::Dimension:
            painter.drawLine(QPointF(4.0, 3.0), QPointF(4.0, 11.0));
            painter.drawLine(QPointF(36.0, 3.0), QPointF(36.0, 11.0));
            painter.drawLine(QPointF(4.0, 7.0), QPointF(14.5, 7.0));
            painter.drawLine(QPointF(25.5, 7.0), QPointF(36.0, 7.0));
            break;
    }
    return pixmap;
}

// Miniaturas para las 4 plantillas del popover "Papel".
QPixmap templatePixmap(BackgroundTemplate tpl, const QColor& color) {
    QPixmap pixmap(88, 60);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawRoundedRect(QRectF(2.0, 2.0, 42.0, 28.0), 3.0, 3.0);

    switch (tpl) {
        case BackgroundTemplate::Blank:
            break;
        case BackgroundTemplate::Grid:
            painter.drawLine(QPointF(15.3, 3.0), QPointF(15.3, 29.0));
            painter.drawLine(QPointF(28.6, 3.0), QPointF(28.6, 29.0));
            painter.drawLine(QPointF(3.0, 11.3), QPointF(43.0, 11.3));
            painter.drawLine(QPointF(3.0, 19.6), QPointF(43.0, 19.6));
            break;
        case BackgroundTemplate::Dots: {
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            for (const double y : {9.0, 16.0, 23.0})
                for (const double x : {10.0, 19.0, 28.0, 37.0})
                    painter.drawEllipse(QPointF(x, y), 1.3, 1.3);
            painter.setBrush(Qt::NoBrush);
            break;
        }
        case BackgroundTemplate::Ruled:
            painter.drawLine(QPointF(5.0, 10.0), QPointF(41.0, 10.0));
            painter.drawLine(QPointF(5.0, 16.0), QPointF(41.0, 16.0));
            painter.drawLine(QPointF(5.0, 22.0), QPointF(41.0, 22.0));
            break;
    }
    return pixmap;
}

} // namespace finily
