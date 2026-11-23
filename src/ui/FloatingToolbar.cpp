#include "ui/FloatingToolbar.h"

#include "ui/CanvasWidget.h"
#include "ui/IconLibrary.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QEasingCurve>
#include <QEvent>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QScrollBar>
#include <functional>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantAnimation>

#include <algorithm>
#include <numbers>

namespace finily {
namespace {

QColor tinted(const QColor& color, int alpha) {
    QColor result(color);
    result.setAlpha(alpha);
    return result;
}

QString rgba(const QColor& color, int alpha = -1) {
    const int a = alpha < 0 ? color.alpha() : alpha;
    return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(a);
}

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
        p.drawRoundedRect(QRectF(-3.6, -9.0, 7.2, 10.0), 1.6, 1.6);          // cuerpo
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
        p.drawLine(QPointF(6.5, 10.0), QPointF(14.5, 10.0 + 0.0));
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

QIcon glyphIcon(const QString& kind, const QColor& color, const QColor& onColor = QColor()) {
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
    // Fondo sutil para contrastar el relleno
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

// Ilustraciones reales de puntas de lápiz (0=Bolígrafo, 1=Pluma, 2=Plumón, 3=Pincel).
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
        // Plumón / Marcador: cuerpo grueso con punta biselada/firme de color
        p.setPen(Qt::NoPen);
        p.setBrush(body); p.drawRoundedRect(QRectF(-6.5, 0.0, 13.0, 14.0), 2.5, 2.5);
        QPainterPath tipP;
        tipP.moveTo(-4.5, 0.0); tipP.lineTo(-3.5, -10.5); tipP.lineTo(3.5, -8.0); tipP.lineTo(4.5, 0.0); tipP.closeSubpath();
        p.setBrush(ink); p.drawPath(tipP);
    } else {
        // Pincel: virola metálica + cerdas curvas orgánicas
        p.setPen(Qt::NoPen);
        p.setBrush(body); p.drawRect(QRectF(-4.0, 6.0, 8.0, 8.0));
        p.setBrush(metal); p.drawRect(QRectF(-4.5, 1.5, 9.0, 4.5));
        QPainterPath bristle;
        bristle.moveTo(-4.2, 1.5);
        bristle.quadTo(-5.2, -5.5, 0.5, -12.0);
        bristle.quadTo(5.2, -5.0, 4.2, 1.5);
        bristle.closeSubpath();
        p.setBrush(ink); p.drawPath(bristle);
    }
    p.restore();
    p.end();
    return pm;
}

// Ilustraciones reales de puntas de resaltador (0=Biselado, 1=Redondo, 2=Recto).
QPixmap hlTipPixmap(int tip, const QColor& ink, bool isDark) {
    QPixmap pm(144, 68);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor soft = ink;
    soft.setAlpha(125);
    if (tip == 0) {
        // Punta biselada inclinada
        p.setPen(QPen(soft, 9.0, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(16.0, 22.0), QPointF(56.0, 12.0));
    } else if (tip == 1) {
        // Punta redonda suave
        p.setPen(QPen(soft, 9.0, Qt::SolidLine, Qt::RoundCap));
        QPainterPath wave;
        wave.moveTo(14.0, 20.0);
        wave.quadTo(36.0, 9.0, 58.0, 20.0);
        p.drawPath(wave);
    } else {
        // Subrayado recto automático
        p.setPen(QPen(soft, 8.5, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(12.0, 17.0), QPointF(60.0, 17.0));
        p.setPen(QPen(isDark ? QColor(255, 255, 255, 160) : QColor(15, 23, 42, 150), 1.1, Qt::DashLine));
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
    } else if (kind == "eraser-stroke") {
        p.drawLine(QPointF(3.5, 13.0), QPointF(14.5, 5.0));
        p.drawLine(QPointF(5.5, 5.0), QPointF(12.5, 13.0));
    } else if (kind == "eraser-partial") {
        p.drawEllipse(QPointF(9.0, 9.0), 4.5, 4.5);
        QPen dash(color, 1.3, Qt::DotLine);
        p.setPen(dash);
        p.drawLine(QPointF(2.5, 9.0), QPointF(15.5, 9.0));
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
    }
    return pm;
}

// Curvas de 10 niveles discretos de grosor (Lápiz y Resaltador).
static const double kPenLevels[10] = {1.2, 1.8, 2.4, 3.2, 4.2, 5.5, 7.0, 9.0, 12.0, 16.0};
static const double kHlLevels[10]  = {6.0, 9.0, 12.0, 16.0, 20.0, 25.0, 30.0, 36.0, 44.0, 54.0};
int sizeToLevel(double px, bool isHighlighter) {
    const double* arr = isHighlighter ? kHlLevels : kPenLevels;
    int best = 0;
    double bestDist = 1e9;
    for (int i = 0; i < 10; ++i) {
        const double d = std::abs(arr[i] - px);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best + 1;
}

// Las tintas por defecto de pizarra (claras) y de papel (oscuras) forman
// parejas: al pasar de pizarra a papel la tiza blanca se vuelve tinta negra,
// el amarillo se oscurece, etc. Así ningún color favorito queda invisible.
QColor adaptInkToTheme(const QColor& color, bool dark) {
    static const QVector<QPair<QString, QString>> pairs = {
        {"#f8fafc", "#0f172a"}, {"#fde047", "#ca8a04"}, {"#38bdf8", "#0284c7"}, {"#f87171", "#dc2626"},
        {"#4ade80", "#16a34a"}, {"#c084fc", "#9333ea"}, {"#fb923c", "#ea580c"},
    };
    const QString name = color.name();
    for (const auto& pair : pairs) {
        if (dark && name == pair.second)
            return QColor(pair.first);
        if (!dark && name == pair.first)
            return QColor(pair.second);
    }
    return color;
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

QWidget* makeSeparator(QWidget* parent) {
    auto* separator = new QWidget(parent);
    separator->setObjectName("finilySep");
    separator->setFixedSize(1, 20);
    return separator;
}

// Divisor horizontal nítido entre secciones de un popover.
QWidget* makeDivider(QWidget* parent) {
    auto* divider = new QWidget(parent);
    divider->setObjectName("finilyDivider");
    divider->setFixedHeight(1);
    return divider;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("finilyTitle");
    return label;
}

void polishButton(QToolButton* button) {
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
}

// Convierte un botón "Acción (Atajo)" en una fila de menú: la acción a la
// izquierda y el atajo en gris a la derecha, como en un menú de escritorio.
void makeMenuRow(QToolButton* button) {
    QString text = button->text();
    QString shortcut;
    const int open = text.lastIndexOf(" (");
    if (open > 0 && text.endsWith(')')) {
        shortcut = text.mid(open + 2, text.size() - open - 3);
        text = text.left(open);
    }
    button->setText(QString());
    button->setObjectName("finilyMenuRow");
    auto* row = new QHBoxLayout(button);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(8);
    auto* title = new QLabel(text, button);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(title);
    row->addStretch(1);
    if (!shortcut.isEmpty()) {
        auto* hint = new QLabel(shortcut.replace("Shift", "Mayús"), button);
        hint->setObjectName("finilyShortcut");
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(hint);
    }
}

} // namespace

// Cápsula de cristal antialias pintada a mano: reemplaza el fondo QSS de los
// QWidget planos para que el estilo de sistema Qt6 (Caelestia / KDE / Breeze)
// jamás la sobreescriba con cuadrados gris carbón. Cada tema tiene su propio
// relleno/borde exactos; DarkBoard añade un highlight interior superior de 1px.
class GlassCapsule : public QWidget {
public:
    explicit GlassCapsule(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_StyledBackground, false); // el estilo de sistema NO pinta aquí
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
    }

    void setCapsuleTheme(const ThemeConfig& theme) {
        switch (theme.id) {
            case ThemeId::PaperLight:
                m_fill = QColor(255, 255, 255, 246);
                m_border = QColor(15, 23, 42, 26);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::PaperCream:
                m_fill = QColor(253, 250, 242, 246);
                m_border = QColor(100, 82, 54, 34);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::GreenBoard:
                m_fill = QColor(18, 43, 36, 244);
                m_border = QColor(255, 255, 255, 32);
                m_highlight = QColor(255, 255, 255, 0);
                break;
            case ThemeId::DarkBoard:
            default:
                m_fill = QColor(26, 30, 38, 242);
                m_border = QColor(255, 255, 255, 28);
                m_highlight = QColor(255, 255, 255, 18);
                break;
        }
        update();
    }

    void setCapsuleRadius(int radius) {
        m_radius = radius;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_fill);
        painter.drawRoundedRect(frame, m_radius, m_radius);
        painter.setPen(QPen(m_border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame, m_radius, m_radius);
        if (m_highlight.alpha() > 0) {
            // Highlight interior superior de 1px (vidrio obsidiana).
            QPen highlightPen(m_highlight, 1.0);
            highlightPen.setCapStyle(Qt::FlatCap);
            painter.setPen(highlightPen);
            const double inset = 2.0;
            QPainterPath highlight;
            highlight.moveTo(frame.left() + m_radius * 0.55, frame.top() + inset);
            highlight.lineTo(frame.right() - m_radius * 0.55, frame.top() + inset);
            painter.strokePath(highlight, highlightPen);
        }
    }

private:
    QColor m_fill{26, 30, 38, 242};
    QColor m_border{255, 255, 255, 28};
    QColor m_highlight{255, 255, 255, 0};
    int m_radius = 22;
};

// Fila con interruptor (texto a la izquierda, switch a la derecha).
class SwitchButton : public QToolButton {
public:
    explicit SwitchButton(QWidget* parent) : QToolButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setObjectName("finilySwitch");
    }
    void setColors(const QColor& text, const QColor& accent, bool dark) {
        m_text = text;
        m_accent = accent;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(m_text);
        QFont f = font();
        f.setPixelSize(13);
        p.setFont(f);
        p.drawText(QRect(4, 0, width() - 56, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        const QRectF track(width() - 40, height() / 2.0 - 10, 34, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? m_accent : QColor(128, 128, 128, m_dark ? 90 : 110));
        p.drawRoundedRect(track, 10, 10);
        p.setBrush(isChecked() && m_dark ? QColor(16, 20, 18) : QColor(255, 255, 255));
        p.drawEllipse(QPointF(track.left() + (isChecked() ? 24.0 : 10.0), track.center().y()), 7.5, 7.5);
    }

private:
    QColor m_text{230, 235, 240};
    QColor m_accent{91, 227, 164};
    bool m_dark = true;
};

// Punto de color antialias para presets de tinta y paleta del popover "Trazo":
// círculo perfectamente centrado con rim sutil de 1px (los puntos de tiza
// blanca siguen nítidos sobre papel y los de tinta negra sobre pizarra) y,
// al activarse, anillo exterior de acento de 2px separado por un gap de 2px.
// Cero medias-lunas recortadas: nada de qradialgradient en QSS.
class ColorDotButton : public QToolButton {
public:
    explicit ColorDotButton(QWidget* parent = nullptr)
        : QToolButton(parent) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(28, 28);
    }

    // Dibuja el punto como la punta biselada de un resaltador para que no se
    // confunda con una tinta del mismo color.
    void setMarker(bool marker) {
        if (m_marker == marker)
            return;
        m_marker = marker;
        update();
    }

    void setDotColor(const QColor& color) {
        if (m_color == color)
            return;
        m_color = color;
        update();
    }
    void setAccentColor(const QColor& color) {
        if (m_accent == color)
            return;
        m_accent = color;
        update();
    }
    void setRimColor(const QColor& color) {
        if (m_rim == color)
            return;
        m_rim = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPointF center = QRectF(rect()).center();
        const double r = width() >= 28 ? 9.0 : 8.5;
        if (underMouse() && !isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(m_rim.red(), m_rim.green(), m_rim.blue(), 28));
            painter.drawEllipse(center, r + 3.5, r + 3.5);
        }
        painter.setPen(QPen(m_rim, 1.0));
        if (m_marker) {
            // Punta de resaltador: trazo ancho semitransparente con bisel.
            QColor fill = m_color;
            fill.setAlpha(150);
            painter.setBrush(fill);
            QPainterPath tip;
            tip.moveTo(center + QPointF(-7.5, 4.5));
            tip.lineTo(center + QPointF(-2.5, -7.0));
            tip.lineTo(center + QPointF(7.5, -7.0));
            tip.lineTo(center + QPointF(2.5, 4.5));
            tip.closeSubpath();
            painter.drawPath(tip);
            painter.setPen(QPen(m_color, 2.4, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center + QPointF(-8.0, 7.5), center + QPointF(8.0, 7.5));
        } else {
            painter.setBrush(m_color);
            painter.drawEllipse(center, r, r);
        }
        // Anillo exterior de acento de 2px con gap de 2px al activarse.
        if (isChecked()) {
            QPen ring(m_accent, 2.0);
            painter.setPen(ring);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, r + 3.0, r + 3.0);
        }
    }

private:
    QColor m_color{Qt::white};
    QColor m_accent{"#60a5fa"};
    QColor m_rim{0, 0, 0, 60};
    bool m_marker = false;
};

// Útil de escritura del estuche: un lápiz dibujado en vertical que asoma del
// borde de la bandeja. El seleccionado sube (animado) como si lo tomaras; el
// color de la banda/punta es la tinta actual. En el dock izquierdo se dibuja
// girado 90° (la punta mira a la derecha).
class PenTrayButton : public QToolButton {
public:
    enum class Kind { Fountain, Gel, Marker, Eraser };

    PenTrayButton(Kind kind, QWidget* parent)
        : QToolButton(parent), m_kind(kind) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(170);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_lift = v.toDouble();
            update();
        });
        connect(this, &QToolButton::toggled, this, [this] { animateTo(targetLift()); });
    }

    void setInk(const QColor& ink) {
        if (m_ink == ink)
            return;
        m_ink = ink;
        update();
    }
    void setDarkTheme(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }
    void setSideways(bool sideways) {
        if (m_sideways == sideways)
            return;
        m_sideways = sideways;
        update();
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        m_hover = true;
        animateTo(targetLift());
        QToolButton::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        m_hover = false;
        animateTo(targetLift());
        QToolButton::leaveEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Sistema local: lápiz vertical con la punta arriba; W x H "sin girar".
        qreal W = width(), H = height();
        if (m_sideways) {
            p.translate(width(), 0);
            p.rotate(90);
            std::swap(W, H);
        }
        const qreal cx = W / 2.0;
        const qreal top = 16.0 - m_lift; // punta en reposo a 16 px del borde

        const bool marker = m_kind == Kind::Marker;
        const bool eraser = m_kind == Kind::Eraser;
        const qreal bodyW = marker || eraser ? 20.0 : 15.0;
        const qreal tipH = m_kind == Kind::Fountain ? 17.0 : m_kind == Kind::Gel ? 14.0 : marker ? 9.0 : 0.0;
        const qreal bodyTop = top + tipH;
        const QRectF body(cx - bodyW / 2, bodyTop, bodyW, H - bodyTop + 8);

        // Sombra suave a la derecha.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, m_dark ? 70 : 28));
        p.drawRoundedRect(body.translated(2.0, 2.0), 4, 4);

        // Cuerpo cilíndrico (luz desde la izquierda).
        QColor shell = m_dark ? QColor("#e9e9ee") : QColor("#fbfbfd");
        if (eraser)
            shell = QColor("#f4f1ea");
        QLinearGradient cyl(body.left(), 0, body.right(), 0);
        cyl.setColorAt(0.0, shell.darker(112));
        cyl.setColorAt(0.28, shell.lighter(104));
        cyl.setColorAt(0.55, shell);
        cyl.setColorAt(1.0, shell.darker(122));
        p.setBrush(cyl);
        p.drawRoundedRect(body, eraser ? 6 : 3.5, eraser ? 6 : 3.5);

        auto shaded = [&](const QColor& c) {
            QLinearGradient g(body.left(), 0, body.right(), 0);
            g.setColorAt(0.0, c.darker(118));
            g.setColorAt(0.3, c.lighter(112));
            g.setColorAt(1.0, c.darker(135));
            return g;
        };

        if (eraser) {
            // Goma rosada arriba + funda de color.
            const QRectF rubber(body.left(), body.top() - 2, body.width(), 18);
            p.setBrush(shaded(QColor("#f3a1b0")));
            p.drawRoundedRect(rubber, 6, 6);
            p.setBrush(shaded(QColor("#5b7cfa")));
            p.drawRect(QRectF(body.left(), body.top() + 14, body.width(), H));
            p.setBrush(QColor(255, 255, 255, 120));
            p.drawRect(QRectF(body.left(), body.top() + 22, body.width(), 2));
            return;
        }

        // Banda de color de tinta en el cuerpo.
        QColor band = m_ink;
        if (marker)
            band.setAlpha(255);
        p.setBrush(shaded(band));
        // Contorno fino para que la tinta blanca/clara se distinga del cuerpo.
        p.setPen(QPen(QColor(0, 0, 0, 55), 0.8));
        p.drawRect(QRectF(body.left(), bodyTop + (marker ? 0 : 7), body.width(), marker ? 13 : 7));
        p.setPen(Qt::NoPen);

        QPainterPath tip;
        switch (m_kind) {
        case Kind::Fountain: {
            // Grip oscuro + plumín metálico con hendidura.
            p.setBrush(shaded(QColor("#3a3d46")));
            p.drawRect(QRectF(body.left(), bodyTop, body.width(), 7));
            tip.moveTo(cx - 6.5, bodyTop + 0.5);
            tip.cubicTo(cx - 6.5, bodyTop - 7, cx - 1.5, top + 4, cx, top);
            tip.cubicTo(cx + 1.5, top + 4, cx + 6.5, bodyTop - 7, cx + 6.5, bodyTop + 0.5);
            tip.closeSubpath();
            QLinearGradient metal(cx - 6.5, 0, cx + 6.5, 0);
            metal.setColorAt(0.0, QColor("#b8892f"));
            metal.setColorAt(0.4, QColor("#f3d27a"));
            metal.setColorAt(1.0, QColor("#9a6f22"));
            p.setBrush(metal);
            p.drawPath(tip);
            p.setPen(QPen(QColor(90, 60, 15, 200), 0.9));
            p.drawLine(QPointF(cx, top + 2), QPointF(cx, bodyTop - 5));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(90, 60, 15, 200));
            p.drawEllipse(QPointF(cx, bodyTop - 5), 1.3, 1.3);
            break;
        }
        case Kind::Gel: {
            tip.moveTo(cx - bodyW / 2 + 0.5, bodyTop + 0.5);
            tip.lineTo(cx - 1.6, top + 3);
            tip.lineTo(cx + 1.6, top + 3);
            tip.lineTo(cx + bodyW / 2 - 0.5, bodyTop + 0.5);
            tip.closeSubpath();
            QLinearGradient cone(cx - bodyW / 2, 0, cx + bodyW / 2, 0);
            cone.setColorAt(0.0, QColor("#a9adb8"));
            cone.setColorAt(0.35, QColor("#eef0f4"));
            cone.setColorAt(1.0, QColor("#8b909c"));
            p.setBrush(cone);
            p.drawPath(tip);
            p.setBrush(m_ink);
            p.drawEllipse(QPointF(cx, top + 2.5), 1.9, 2.4);
            break;
        }
        case Kind::Marker: {
            // Punta biselada translúcida.
            QColor nib = m_ink;
            nib.setAlpha(225);
            tip.moveTo(cx - 6, bodyTop + 0.5);
            tip.lineTo(cx - 6, top + 6);
            tip.lineTo(cx + 6, top);
            tip.lineTo(cx + 6, bodyTop + 0.5);
            tip.closeSubpath();
            p.setBrush(nib);
            p.drawPath(tip);
            p.setBrush(QColor(255, 255, 255, 70));
            p.drawRect(QRectF(cx - 6, top + 6, 2.5, bodyTop - top - 6));
            break;
        }
        case Kind::Eraser:
            break;
        }
    }

private:
    qreal targetLift() const { return isChecked() ? 11.0 : (m_hover ? 4.0 : 0.0); }
    void animateTo(qreal target) {
        m_anim->stop();
        m_anim->setStartValue(m_lift);
        m_anim->setEndValue(target);
        m_anim->start();
    }

    Kind m_kind;
    QColor m_ink{"#1d4ed8"};
    bool m_dark = false;
    bool m_sideways = false;
    bool m_hover = false;
    qreal m_lift = 0.0;
    QVariantAnimation* m_anim = nullptr;
};

// Punto de vista previa en vivo del popover "Trazo": pinta un círculo
// anti-aliased con la tinta y el grosor actuales, a escala real dentro de 28x28.
class StrokePreviewWidget : public QWidget {
public:
    explicit StrokePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(28, 28);
        setFocusPolicy(Qt::NoFocus);
    }

    void setInk(const QColor& color, double size) {
        if (m_color == color && qFuzzyCompare(m_size, size))
            return;
        m_color = color;
        m_size = size;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // Anillo guía tenue que delimita la zona útil del punto.
        const QRectF ring = rect().adjusted(2.0, 2.0, -2.0, -2.0);
        QColor guideColor = m_color;
        guideColor.setAlpha(70);
        QPen guidePen(guideColor, 1.0);
        painter.setPen(guidePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring);
        // Punto de tinta a escala real (grosor acotado al área útil).
        const double diameter = std::clamp(m_size, 1.0, ring.width());
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(ring.center(), diameter / 2.0, diameter / 2.0);
    }

private:
    QColor m_color{Qt::white};
    double m_size = 2.6;
};

// Slider minimalista con puntitos discretos sobre la línea (sin números "3 / 10").
class DotStepSlider : public QSlider {
public:
    explicit DotStepSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent) {
        setFixedHeight(26);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }
    void setColors(const QColor& accent, bool isDark) {
        m_accent = accent;
        m_isDark = isDark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int lo = minimum(), hi = maximum();
        const int steps = std::max(1, hi - lo);
        const double pad = 9.0;
        const double y = height() / 2.0;
        const double span = std::max(1.0, width() - pad * 2.0);
        const double frac = double(value() - lo) / double(steps);
        const double curX = pad + frac * span;

        // Riel base fino
        const QColor trackCol = m_isDark ? QColor(255, 255, 255, 42) : QColor(15, 23, 42, 38);
        p.setPen(QPen(trackCol, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(pad, y), QPointF(pad + span, y));

        // Riel activo hasta el punto elegido
        p.setPen(QPen(m_accent, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(pad, y), QPointF(curX, y));

        // Puntitos discretos en toda la línea
        const QColor idleDot = m_isDark ? QColor(255, 255, 255, 110) : QColor(15, 23, 42, 95);
        for (int v = lo; v <= hi; ++v) {
            const double x = pad + (double(v - lo) / double(steps)) * span;
            const bool active = (v == value());
            const bool filled = (v <= value());
            if (active) {
                p.setPen(QPen(m_isDark ? QColor("#ffffff") : QColor("#0f172a"), 1.6));
                p.setBrush(m_accent);
                p.drawEllipse(QPointF(x, y), 5.6, 5.6);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(filled ? m_accent : idleDot);
                p.drawEllipse(QPointF(x, y), 2.4, 2.4);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        setValueFromX(event->position().x());
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton)
            setValueFromX(event->position().x());
        event->accept();
    }

private:
    void setValueFromX(double x) {
        const int lo = minimum(), hi = maximum();
        const double pad = 9.0;
        const double span = std::max(1.0, width() - pad * 2.0);
        const double t = std::clamp((x - pad) / span, 0.0, 1.0);
        setValue(lo + qRound(t * (hi - lo)));
    }
    QColor m_accent{"#10b981"};
    bool m_isDark = true;
};

FloatingToolbar::FloatingToolbar(CanvasWidget* canvas, QWidget* parent)
    : QWidget(parent), m_canvas(canvas) {
    setObjectName("finilyOverlay");
    // NO usar WA_TransparentForMouseEvents: en Qt6 se hereda a TODOS los hijos y
    // dejaría botones y sliders inciclables. La transparencia al lienzo se logra
    // con setMask() en updateOverlayMask() (solo los rectángulos de los hijos
    // visibles capturan eventos; el resto pasa al CanvasWidget).
    setFocusPolicy(Qt::NoFocus);

    initPresets();
    loadPresets();
    m_palette = getPaletteForTheme(true);
    loadDockPosition();

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        connect(m_canvas, &CanvasWidget::stateChanged, this, &FloatingToolbar::refreshFromCanvas);
        connect(m_canvas, &CanvasWidget::viewportChanged, this, [this](double) { updateZoomLabel(); });
        connect(m_canvas, &CanvasWidget::toastRequested, this, &FloatingToolbar::showToast);
        connect(m_canvas, &CanvasWidget::selectionMenuRequested, this, &FloatingToolbar::showSelectionMenu);
    }

    buildUtilityPill();
    buildTopRight();
    buildDock();
    buildZoomPill();
    buildPagePill();
    buildRestoreButton();
    buildStrokePopover();
    buildPaperPopover();
    buildShapesPopover();
    buildFilePopover();
    buildContextPill();
    buildThumbPanel();

    // Toast flotante: cápsula de cristal + etiqueta centrada. El fundido usa
    // una QGraphicsOpacityEffect sobre la cápsula (único efecto del widget).
    m_toastPill = new GlassCapsule(this);
    m_toastPill->setCapsuleRadius(16);
    auto* toastLayout = new QVBoxLayout(m_toastPill);
    toastLayout->setContentsMargins(16, 9, 16, 9);
    toastLayout->setSpacing(0);
    m_toastLabel = new QLabel(m_toastPill);
    m_toastLabel->setObjectName("finilyToast");
    m_toastLabel->setAlignment(Qt::AlignCenter);
    toastLayout->addWidget(m_toastLabel);
    m_toastPill->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, this, [this] {
        if (!m_toastPill || !m_toastPill->isVisible())
            return;
        auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill->graphicsEffect());
        if (!opacity)
            return;
        if (m_toastAnim) {
            m_toastAnim->stop();
            m_toastAnim->deleteLater();
        }
        m_toastAnim = new QPropertyAnimation(opacity, "opacity", this);
        m_toastAnim->setDuration(220);
        m_toastAnim->setStartValue(opacity->opacity());
        m_toastAnim->setEndValue(0.0);
        m_toastAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_toastAnim, &QPropertyAnimation::finished, this, [this] {
            if (m_toastPill)
                m_toastPill->hide();
            if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill ? m_toastPill->graphicsEffect() : nullptr))
                effect->setOpacity(1.0);
            updateOverlayMask();
        });
        m_toastAnim->start();
    });

    // Cápsulas de cristal antialias (inmunes al estilo de sistema) + sombras
    // suaves reales (blur 22, yOffset 6). El registro m_capsules permite
    // recolorearlas todas al instante en applyThemeStylesheet().
    const ThemeConfig initialTheme = getThemeConfig(m_theme);
    const QVector<QPair<GlassCapsule*, int>> capsuleSpecs = {
        {m_pill, 22},          {m_contextPill, 16}, {m_zoomPill, 18},
        {m_pagePill, 18},      {m_toastPill, 16},   {m_strokePopover, 16},
        {m_paperPopover, 16},  {m_shapesPopover, 16}, {m_filePopover, 16},
        {m_dock, 20},          {m_topRight, 22},    {m_thumbPanel, 16},
    };
    for (const auto& capsule : capsuleSpecs) {
        if (!capsule.first)
            continue;
        capsule.first->setCapsuleRadius(capsule.second);
        capsule.first->setCapsuleTheme(initialTheme);
        m_capsules.append(capsule.first);
        applyShadow(capsule.first);
    }

    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &FloatingToolbar::disarmClearButton);

    applyThemeStylesheet();

    if (m_canvas)
        setGeometry(m_canvas->rect());
    positionChildren();
    refreshFromCanvas();
}

void FloatingToolbar::initPresets() {
    m_presets.clear();
    m_presets.append(PenPreset{0, "Tiza principal", ToolType::Pen, QColor("#f8fafc"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{1, "Amarillo", ToolType::Pen, QColor("#fde047"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{2, "Celeste", ToolType::Pen, QColor("#38bdf8"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{3, "Rojo", ToolType::Pen, QColor("#f87171"), 2.6, LinePattern::Solid});
    m_presets.append(PenPreset{4, "Resaltador", ToolType::Highlighter, QColor("#fde047"), 12.0, LinePattern::Solid});
    m_activePreset = 0;
    m_lastPresetsSig.clear();
}

// Restaura los 5 presets favoritos persistidos en QSettings("Finily", "Finily").
// Solo marca m_presetsRestored si existía al menos una clave guardada, para no
// desactivar innecesariamente la adaptación temática del preset inicial.
void FloatingToolbar::loadPresets() {
    QSettings settings("Finily", "Finily");
    const QString firstKey = QString("presets/%1/tool").arg(m_presets.first().index);
    if (!settings.contains(firstKey))
        return;
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        PenPreset& preset = m_presets[i];
        const int tool = settings.value(prefix + "tool", int(preset.tool)).toInt();
        if (tool >= int(ToolType::Pen) && tool <= int(ToolType::Hand))
            preset.tool = static_cast<ToolType>(tool);
        const QString colorName = settings.value(prefix + "color", preset.color.name(QColor::HexArgb)).toString();
        const QColor color(colorName);
        if (color.isValid())
            preset.color = color;
        const double size = settings.value(prefix + "size", preset.size).toDouble();
        if (size > 0.0 && size <= 50.0)
            preset.size = size;
        const int pattern = settings.value(prefix + "linePattern", int(preset.linePattern)).toInt();
        if (pattern >= int(LinePattern::Solid) && pattern <= int(LinePattern::Dimension))
            preset.linePattern = static_cast<LinePattern>(pattern);
    }
    m_presetsRestored = true;
}

// Persiste los 5 presets favoritos (herramienta, color, grosor y tipo de línea).
void FloatingToolbar::savePresets() {
    QSettings settings("Finily", "Finily");
    for (int i = 0; i < m_presets.size(); ++i) {
        const QString prefix = QString("presets/%1/").arg(i);
        const PenPreset& preset = m_presets.at(i);
        settings.setValue(prefix + "tool", int(preset.tool));
        settings.setValue(prefix + "color", preset.color.name(QColor::HexArgb));
        settings.setValue(prefix + "size", preset.size);
        settings.setValue(prefix + "linePattern", int(preset.linePattern));
    }
    settings.sync();
}

// Pill compacta de utilidades (arriba-izquierda, x=14 y=12): Inicio | Deshacer
// y Rehacer | Trazo, Papel, Archivo | modo interfaz limpia. Las herramientas
// de dibujo viven en el dock de papelería (buildDock) para dejar el cromo
// superior despejado.
void FloatingToolbar::buildUtilityPill() {
    m_pill = new GlassCapsule(this);
    m_pill->setObjectName("finilyPill");
    auto* row = new QHBoxLayout(m_pill);
    row->setContentsMargins(10, 7, 10, 7);
    row->setSpacing(3);

    // 'Inicio' (biblioteca): primer botón a la izquierda, separado por un
    // divisor fino antes de Deshacer/Rehacer.
    m_homeBtn = new QToolButton(m_pill);
    m_homeBtn->setFixedSize(36, 36);
    m_homeBtn->setIconSize(QSize(22, 22));
    m_homeBtn->setToolTip("Inicio");
    m_homeBtn->setStatusTip(m_homeBtn->toolTip());
    polishButton(m_homeBtn);
    connect(m_homeBtn, &QToolButton::clicked, this, &FloatingToolbar::homeRequested);
    m_studioUtilSpecs.append(qMakePair(m_homeBtn, QStringLiteral("home")));
    row->addWidget(m_homeBtn);

    m_titleDot = new QLabel(m_pill);
    m_titleDot->setFixedSize(6, 6);
    row->addSpacing(4);
    row->addWidget(m_titleDot, 0, Qt::AlignVCenter);
    m_titleLabel = new QLabel("Sin título", m_pill);
    m_titleLabel->setMaximumWidth(200);
    m_titleLabel->setCursor(Qt::PointingHandCursor);
    m_titleLabel->setToolTip("Haz clic para renombrar el cuaderno");
    m_titleLabel->installEventFilter(this);
    row->addWidget(m_titleLabel);
    m_titleEdit = new QLineEdit(m_pill);
    m_titleEdit->setMaximumWidth(180);
    m_titleEdit->setFixedHeight(26);
    m_titleEdit->hide();
    connect(m_titleEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_titleEdit || !m_titleEdit->isVisible())
            return;
        const QString next = m_titleEdit->text().trimmed();
        m_titleEdit->hide();
        if (m_titleLabel)
            m_titleLabel->show();
        positionChildren();
        if (!next.isEmpty() && (!m_titleLabel || next != m_titleLabel->text()))
            emit renameRequested(next);
    });
    row->addWidget(m_titleEdit);
    row->addSpacing(4);

    row->addWidget(makeSeparator(m_pill));

    // Deshacer / Rehacer
    m_undoBtn = new QToolButton(m_pill);
    m_undoBtn->setFixedSize(36, 36);
    m_undoBtn->setIconSize(QSize(22, 22));
    m_undoBtn->setToolTip("Deshacer");
    polishButton(m_undoBtn);
    connect(m_undoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->undo();
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_undoBtn, QStringLiteral("undo")));
    row->addWidget(m_undoBtn);

    m_redoBtn = new QToolButton(m_pill);
    m_redoBtn->setFixedSize(36, 36);
    m_redoBtn->setIconSize(QSize(22, 22));
    m_redoBtn->setToolTip("Rehacer");
    polishButton(m_redoBtn);
    connect(m_redoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->redo();
        refreshFromCanvas();
    });
    m_studioUtilSpecs.append(qMakePair(m_redoBtn, QStringLiteral("redo")));
    row->addWidget(m_redoBtn);

    // Modo Ecuación (atajo M): botón oculto, la lógica sigue en refreshFromCanvas.
    m_mathModeBtn = new QToolButton(m_pill);
    m_mathModeBtn->setCheckable(true);
    m_mathModeBtn->hide();
    connect(m_mathModeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathModeBtn)
            m_canvas->setMathBeautifyEnabled(m_mathModeBtn->isChecked());
        refreshFromCanvas();
    });
}

// Cápsula superior derecha: Presentar (interfaz limpia), Páginas y menú ⋯.
void FloatingToolbar::buildTopRight() {
    m_topRight = new GlassCapsule(this);
    m_topRight->setObjectName("finilyPill");
    auto* row = new QHBoxLayout(m_topRight);
    row->setContentsMargins(8, 7, 8, 7);
    row->setSpacing(3);
    auto mk = [&](const QString& tip, const QString& glyph) {
        auto* b = new QToolButton(m_topRight);
        b->setFixedSize(36, 36);
        b->setIconSize(QSize(22, 22));
        b->setToolTip(tip);
        polishButton(b);
        m_iconSpecs.append(qMakePair(b, glyph));
        row->addWidget(b);
        return b;
    };
    m_cleanBtn = mk("Presentar · oculta la interfaz (Tab)", "present");
    connect(m_cleanBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_paperBtn = mk("Papel y plantilla", "paper");
    connect(m_paperBtn, &QToolButton::clicked, this, [this] { openPopover(m_paperPopover, m_paperBtn); });
    m_pagesBtn = mk("Páginas", "pages");
    m_pagesBtn->setCheckable(true);
    connect(m_pagesBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        if (m_thumbPanel) {
            m_thumbPanel->setVisible(m_pagesBtn->isChecked());
            if (m_pagesBtn->isChecked())
                rebuildThumbs();
        }
        positionChildren();
    });
    m_fileBtn = mk("Menú", "more");
    connect(m_fileBtn, &QToolButton::clicked, this, [this] { openPopover(m_filePopover, m_fileBtn); });
}

// Panel lateral de páginas con miniaturas reales.
namespace {
class PageThumbnailList final : public QListWidget {
public:
    explicit PageThumbnailList(QWidget* parent) : QListWidget(parent) {
        m_scrollTimer.setInterval(35);
        connect(&m_scrollTimer, &QTimer::timeout, this, [this] {
            auto* bar = verticalScrollBar();
            if (m_dragPosition.y() < 24)
                bar->setValue(bar->value() - 14);
            else if (m_dragPosition.y() >= viewport()->height() - 24)
                bar->setValue(bar->value() + 14);
            updateInsertionMarker();
        });
    }

    std::function<bool(QVector<int>, int)> movePages;

    QVector<int> selectedRows() const {
        QVector<int> rows;
        for (const auto* entry : selectedItems())
            rows.append(row(entry));
        std::sort(rows.begin(), rows.end());
        return rows;
    }

    void cancelDrag() {
        m_pressedRows.clear();
        m_destination = -1;
        m_scrollTimer.stop();
        viewport()->update();
    }

    void moveRows(const QVector<int>& rows, int destination) {
        if (!movePages || rows.isEmpty() || !movePages(rows, destination))
            return;
        const int first = destination - int(std::lower_bound(rows.cbegin(), rows.cend(), destination) - rows.cbegin());
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (auto* entry = item(first + i))
                entry->setSelected(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        cancelDrag();
        const auto position = event->position().toPoint();
        const auto* clicked = itemAt(position);
        const auto previous = selectedRows();
        QListWidget::mousePressEvent(event);
        if (event->button() != Qt::LeftButton || !clicked)
            return;
        const int index = row(clicked);
        m_pressPosition = position;
        m_pressedRows = event->modifiers() == Qt::NoModifier && previous.contains(index)
            ? previous : selectedRows();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const auto position = event->position().toPoint();
        if (m_pressedRows.isEmpty() || !(event->buttons() & Qt::LeftButton)
            || (m_destination < 0 && (position - m_pressPosition).manhattanLength() < QApplication::startDragDistance())) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        m_dragPosition = position;
        updateInsertionMarker();
        if (!m_scrollTimer.isActive())
            m_scrollTimer.start();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || m_destination < 0) {
            QListWidget::mouseReleaseEvent(event);
            return;
        }
        const bool inside = viewport()->rect().adjusted(-12, -12, 12, 12).contains(event->position().toPoint());
        const int destination = m_destination;
        const auto rows = std::move(m_pressedRows);
        cancelDrag();
        {
            const QSignalBlocker blocker(this);
            QListWidget::mouseReleaseEvent(event);
        }
        if (inside)
            moveRows(rows, destination);
    }

    void hideEvent(QHideEvent* event) override {
        cancelDrag();
        QListWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QListWidget::paintEvent(event);
        if (m_destination < 0 || count() == 0)
            return;
        const int y = m_destination >= count() ? visualItemRect(item(count() - 1)).bottom() + 3
                                               : visualItemRect(item(m_destination)).top() - 3;
        QPainter painter(viewport());
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawLine(6, y, viewport()->width() - 6, y);
    }

private:
    void updateInsertionMarker() {
        const auto* target = itemAt(m_dragPosition);
        m_destination = target ? row(target) + (m_dragPosition.y() >= visualItemRect(target).center().y())
            : std::clamp((m_dragPosition.y() + verticalScrollBar()->value() + gridSize().height() / 2)
                             / gridSize().height(), 0, count());
        viewport()->update();
    }

    QPoint m_pressPosition;
    QPoint m_dragPosition;
    QVector<int> m_pressedRows;
    int m_destination = -1;
    QTimer m_scrollTimer;
};
constexpr QSize thumbnailSize(124, 82);
} // namespace

void FloatingToolbar::buildThumbPanel() {
    m_thumbPanel = new GlassCapsule(this);
    m_thumbPanel->setObjectName("finilyPopover");
    m_thumbPanel->setFixedWidth(164);
    m_thumbPanel->hide();
    auto* outer = new QVBoxLayout(m_thumbPanel);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* list = new PageThumbnailList(m_thumbPanel);
    m_thumbList = list;
    list->setObjectName("finilyPageThumbnails");
    list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet("QListWidget{background:transparent;} QListWidget::item{padding:4px;}");
    list->setIconSize(thumbnailSize);
    list->setViewMode(QListView::IconMode);
    list->setFlow(QListView::TopToBottom);
    list->setWrapping(false);
    list->setMovement(QListView::Static);
    list->setGridSize(QSize(128, 112));
    list->setFocusPolicy(Qt::NoFocus);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->movePages = [this](QVector<int> rows, int index) { return m_canvas && m_canvas->movePages(rows, index); };
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
        if (list->selectionMode() != QAbstractItemView::MultiSelection)
            m_canvas->goToPage(list->row(item));
    });
    connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& point) {
        const auto* item = list->itemAt(point);
        showPageMenu(list->viewport()->mapToGlobal(point), item ? list->row(item) : -1);
    });
    outer->addWidget(list, 1);

    auto* buttons = new QHBoxLayout;
    auto* add = new QToolButton(m_thumbPanel);
    add->setText(tr("+  Nueva página"));
    add->setToolTip(tr("Añadir página después de la actual"));
    polishButton(add);
    connect(add, &QToolButton::clicked, this, [this] { m_canvas->addPage(); });
    buttons->addWidget(add, 1);
    m_thumbActions = new QToolButton(m_thumbPanel);
    m_thumbActions->setText(QStringLiteral("⋯"));
    m_thumbActions->setToolTip(tr("Acciones de página"));
    polishButton(m_thumbActions);
    connect(m_thumbActions, &QToolButton::clicked, this, [this] {
        showPageMenu(m_thumbActions->mapToGlobal(QPoint(0, m_thumbActions->height())));
    });
    buttons->addWidget(m_thumbActions);
    outer->addLayout(buttons);

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(350);
    connect(m_thumbTimer, &QTimer::timeout, this, &FloatingToolbar::refreshPageThumbnail);
    connect(m_canvas, &CanvasWidget::pageStructureChanged, this, &FloatingToolbar::rebuildThumbs);
}

void FloatingToolbar::rebuildThumbs() {
    if (!m_thumbPanel || m_thumbPanel->isHidden() || !m_canvas)
        return;
    QSignalBlocker blocker(m_thumbList);
    static_cast<PageThumbnailList*>(m_thumbList)->cancelDrag();
    m_thumbList->clear();
    for (int i = 0; i < m_canvas->pageCount(); ++i) {
        auto* item = new QListWidgetItem(QString::number(i + 1), m_thumbList);
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(i, thumbnailSize))));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(tr("Página %1").arg(i + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    }
    m_thumbCurrent = m_canvas->currentPageIndex();
    m_thumbList->setCurrentRow(m_thumbCurrent);
}

void FloatingToolbar::refreshPageThumbnail() {
    if (m_thumbPanel->isHidden() || !m_canvas)
        return;
    const int current = m_canvas->currentPageIndex();
    if (auto* item = m_thumbList->item(current))
        item->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(current, thumbnailSize))));
}

void FloatingToolbar::showPageMenu(const QPoint& position, int clickedRow) {
    if (!m_canvas)
        return;
    auto* list = static_cast<PageThumbnailList*>(m_thumbList);
    QVector<int> rows = list->selectedRows();
    if (clickedRow >= 0 && !rows.contains(clickedRow)) {
        list->setCurrentRow(clickedRow, QItemSelectionModel::ClearAndSelect);
        rows = {clickedRow};
    }
    if (rows.isEmpty())
        rows.append(m_canvas->currentPageIndex());

    const int first = rows.first();
    const int last = rows.last();
    QMenu menu(this);
    auto* multi = menu.addAction(tr("Seleccionar varias páginas"));
    multi->setCheckable(true);
    multi->setChecked(list->selectionMode() == QAbstractItemView::MultiSelection);
    connect(multi, &QAction::toggled, this, [list](bool enabled) {
        list->setSelectionMode(enabled ? QAbstractItemView::MultiSelection : QAbstractItemView::ExtendedSelection);
        if (!enabled && list->currentRow() >= 0)
            list->setCurrentRow(list->currentRow(), QItemSelectionModel::ClearAndSelect);
    });
    menu.addSeparator();
    auto* up = menu.addAction(tr("Mover arriba"), [list, rows, first] { list->moveRows(rows, first - 1); });
    auto* down = menu.addAction(tr("Mover abajo"), [list, rows, last] { list->moveRows(rows, last + 2); });
    up->setEnabled(first > 0);
    down->setEnabled(last + 1 < m_canvas->pageCount());
    menu.addSeparator();
    menu.addAction(tr("Insertar página antes"), [this, first] { m_canvas->insertPage(first); });
    menu.addAction(tr("Insertar página después"), [this, last] { m_canvas->insertPage(last + 1); });
    menu.addAction(tr("Insertar página cuadriculada después"), [this, last] {
        m_canvas->insertPage(last + 1, BackgroundTemplate::Grid);
    });
    menu.addAction(tr("Duplicar"), [this, rows] { m_canvas->duplicatePages(rows); });
    menu.addSeparator();
    menu.addAction(tr("Cortar"), [this, rows] {
        if (m_canvas->copyPages(rows))
            m_canvas->deletePages(rows);
    });
    menu.addAction(tr("Copiar"), [this, rows] { m_canvas->copyPages(rows); });
    auto* pasteBefore = menu.addAction(tr("Pegar antes"), [this, first] { m_canvas->pastePages(first); });
    auto* pasteAfter = menu.addAction(tr("Pegar después"), [this, last] { m_canvas->pastePages(last + 1); });
    pasteBefore->setEnabled(m_canvas->hasCopiedPages());
    pasteAfter->setEnabled(m_canvas->hasCopiedPages());
    menu.addSeparator();
    menu.addAction(rows.size() == 1 ? tr("Eliminar página") : tr("Eliminar páginas"),
                   [this, rows] { m_canvas->deletePages(rows); });
    menu.exec(position);
}

void FloatingToolbar::setDocumentTitle(const QString& title, bool unsaved) {
    if (!m_titleLabel)
        return;
    m_titleLabel->setText(title);
    m_titleLabel->setToolTip(title);
    const ThemeConfig theme = getThemeConfig(m_theme);
    m_titleDot->setStyleSheet(QString("background:%1;border-radius:3px;")
                                  .arg(unsaved ? QStringLiteral("#f5b84b") : theme.accent.name()));
    m_titleDot->setToolTip(unsaved ? "Cambios sin guardar" : "Guardado");
    positionChildren();
}

// Dock de herramientas 100% homologado (10 herramientas exclusivas, cero
// duplicados): arriba los 4 útiles ilustrados SVG (Pluma fuente, Bolígrafo gel,
// Resaltador, Borrador en bloque) con color de tinta dinámico y doble toque
// inteligente para abrir "Trazo"; divisor fino; abajo las 6 herramientas
// duotono SVG en la misma cuadrícula óptica 32x32 (Figuras, Polilínea, Lazo,
// Texto, Láser, Mano).
void FloatingToolbar::buildDock() {
    m_dock = new GlassCapsule(this);
    m_dock->setObjectName("finilyDock");
    m_dock->setCapsuleRadius(20);

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    // Primer clic para activar, segundo clic para abrir los ajustes.
    const QVector<QPair<ToolType, QPair<QString, QString>>> writingTools = {
        {ToolType::Pen, qMakePair(QStringLiteral("Pluma (P)"), QStringLiteral("pen"))},
        {ToolType::Highlighter, qMakePair(QStringLiteral("Resaltador (H)"), QStringLiteral("highlighter"))},
        {ToolType::Eraser, qMakePair(QStringLiteral("Borrador (E)"), QStringLiteral("eraser"))},
        {ToolType::Fill, qMakePair(tr("Relleno (F). Toca dentro de una figura cerrada. Segundo clic para elegir el estilo."),
                                   QStringLiteral("paint-bucket"))},
    };
    for (const auto& entry : writingTools) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName("finilyTool");
        button->setFixedSize(42, 40);
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        polishButton(button);
        m_iconSpecs.append(qMakePair(static_cast<QToolButton*>(button), entry.second.second));
        button->setToolTip(entry.second.first);
        m_toolGroup->addButton(button);
        const ToolType tool = entry.first;
        connect(button, &QToolButton::clicked, this, [this, tool, button] {
            if (!m_canvas)
                return;
            const ToolType active = m_canvas->tool();
            const bool sameTool = (active == tool) || (tool == ToolType::Pen && active == ToolType::Ballpoint);
            if (sameTool) {
                // Segundo clic sobre la herramienta ya activa: abrir/cerrar "Trazo"
                if (m_openPopover == m_strokePopover)
                    closePopovers();
                else
                    openPopover(m_strokePopover, button);
            } else {
                closePopovers();
                selectTool(tool == ToolType::Pen && m_canvas->penTip() == 0 ? ToolType::Ballpoint : tool);
            }
        });
        m_writingKinds.append(tool);
        m_writingBtns.append(button);
    }

    m_dockDivider = new QWidget(m_dock);
    m_dockDivider->setObjectName("finilySep");

    // 6 herramientas duotono SVG bajo el divisor.
    const QVector<QPair<ToolType, QPair<QString, QString>>> secondary = {
        {ToolType::Select, qMakePair(tr("Seleccionar (U). Toca para seleccionar, arrastra para mover. Círculos: vértices. Cuadrados: tamaño."),
                                     QStringLiteral("select-pointer"))},
        {ToolType::Shape, qMakePair(tr("Figuras (S). Arrastra para dibujar. Mayús + arrastrar para editar."), QStringLiteral("shapes"))},
        {ToolType::Lasso, qMakePair(QStringLiteral("Lazo (A)"), QStringLiteral("lasso"))},
        {ToolType::Text, qMakePair(QStringLiteral("Texto (T)"), QStringLiteral("text"))},
        {ToolType::Laser, qMakePair(QStringLiteral("Láser (K)"), QStringLiteral("laser"))},
    };
    for (const auto& entry : secondary) {
        auto* button = new QToolButton(m_dock);
        button->setObjectName("finilyTool");
        button->setFixedSize(42, 40);
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        button->setToolTip(entry.second.first);
        polishButton(button);
        m_toolGroup->addButton(button);
        const ToolType tool = entry.first;
        if (tool == ToolType::Shape) {
            m_shapeBtn = button;
            connect(button, &QToolButton::clicked, this, [this] {
                if (!m_canvas)
                    return;
                if (m_canvas->tool() == ToolType::Shape) {
                    if (m_openPopover == m_shapesPopover)
                        closePopovers();
                    else
                        openPopover(m_shapesPopover, m_shapeBtn);
                } else {
                    closePopovers();
                    selectTool(ToolType::Shape);
                    openPopover(m_shapesPopover, m_shapeBtn);
                }
            });
        } else if (tool == ToolType::Lasso) {
            connect(button, &QToolButton::clicked, this, [this, button] {
                if (m_canvas && m_canvas->tool() == ToolType::Lasso) {
                    if (m_openPopover == m_strokePopover)
                        closePopovers();
                    else
                        openPopover(m_strokePopover, button);
                } else {
                    closePopovers();
                    selectTool(ToolType::Lasso);
                }
            });
        } else {
            connect(button, &QToolButton::clicked, this, [this, tool] {
                closePopovers();
                selectTool(tool);
            });
        }
        m_secondaryKinds.append(tool);
        m_secondaryBtns.append(button);
        m_secondaryIcons.append(entry.second.second);
    }

    // 4 tintas rápidas del estuche: 1 toque cambia el color sin abrir menús.
    m_trayDivider = new QWidget(m_dock);
    m_trayDivider->setObjectName("finilySep");
    for (int i = 0; i < 4; ++i) {
        auto* dot = new ColorDotButton(m_dock);
        dot->setFixedSize(26, 26);
        dot->setToolTip("Cambiar color rápido");
        polishButton(dot);
        connect(dot, &QToolButton::clicked, this, [this, i] {
            if (i < m_palette.size())
                setColor(m_palette.at(i));
            if (m_canvas && (m_canvas->tool() == ToolType::Eraser || m_canvas->tool() == ToolType::Hand
                             || m_canvas->tool() == ToolType::Lasso || m_canvas->tool() == ToolType::Laser))
                selectTool(ToolType::Pen);
        });
        m_trayDots.append(dot);
    }
    auto* colorBtn = new ColorDotButton(m_dock);
    m_trayColorBtn = colorBtn;
    m_trayMoreBtn = colorBtn;
    colorBtn->setCheckable(false);
    colorBtn->setFixedSize(38, 38);
    colorBtn->hide();
}

// Coloca los hijos del dock según la orientación (vertical a la izquierda,
// horizontal arriba/abajo) y fija el tamaño de la cápsula.
void FloatingToolbar::layoutDock() {
    if (!m_dock)
        return;
    const bool vertical = m_dockPos == DockPos::Izquierda;
    const int pad = 10;
    const int toolSize = 40;
    const int dotSize = 26;
    const int crossLen = 56;
    const int along = pad + m_writingBtns.size() * (toolSize + 2) + 12
                      + m_secondaryBtns.size() * (toolSize + 2) + 12
                      + m_trayDots.size() * (dotSize + 4) + pad;
    if (vertical)
        m_dock->setFixedSize(crossLen, along);
    else
        m_dock->setFixedSize(along, crossLen);
    m_dock->setCapsuleRadius(24);

    auto place = [&](QWidget* w, int pos, int len, int cross) {
        const int off = (crossLen - cross) / 2;
        if (vertical)
            w->setGeometry(off, pos, cross, len);
        else
            w->setGeometry(pos, off, len, cross);
        w->raise();
    };

    int pos = pad;
    for (QToolButton* button : m_writingBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (vertical)
        m_dockDivider->setGeometry(22, pos, crossLen - 44, 1);
    else
        m_dockDivider->setGeometry(pos, 22, 1, crossLen - 44);
    pos += 8;
    for (QToolButton* button : m_secondaryBtns) {
        place(button, pos, toolSize, toolSize);
        pos += toolSize + 2;
    }
    pos += 4;
    if (m_trayDivider) {
        if (vertical)
            m_trayDivider->setGeometry(20, pos, crossLen - 40, 1);
        else
            m_trayDivider->setGeometry(pos, 20, 1, crossLen - 40);
    }
    pos += 8;
    for (ColorDotButton* dot : m_trayDots) {
        place(dot, pos, dotSize, dotSize);
        pos += dotSize + 4;
    }
    m_dock->update();
}

void FloatingToolbar::buildZoomPill() {
    m_zoomPill = new GlassCapsule(this);
    m_zoomPill->setObjectName("finilyZoom");
    auto* row = new QHBoxLayout(m_zoomPill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_zoomOutBtn = new QToolButton(m_zoomPill);
    m_zoomOutBtn->setFixedSize(30, 30);
    m_zoomOutBtn->setIconSize(QSize(16, 16));
    m_zoomOutBtn->setToolTip("Alejar");
    polishButton(m_zoomOutBtn);
    connect(m_zoomOutBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomOut();
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomOutBtn, QString("zoom-out")));
    row->addWidget(m_zoomOutBtn);

    m_zoomResetBtn = new QToolButton(m_zoomPill);
    m_zoomResetBtn->setObjectName("finilyZoomReset");
    m_zoomResetBtn->setFixedHeight(30);
    m_zoomResetBtn->setText("100 %");
    m_zoomResetBtn->setToolTip(tr("Ajustar página o restablecer zoom (Ctrl+0)"));
    polishButton(m_zoomResetBtn);
    connect(m_zoomResetBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->resetViewport();
        updateZoomLabel();
    });
    row->addWidget(m_zoomResetBtn);

    m_zoomInBtn = new QToolButton(m_zoomPill);
    m_zoomInBtn->setFixedSize(30, 30);
    m_zoomInBtn->setIconSize(QSize(16, 16));
    m_zoomInBtn->setToolTip("Acercar");
    polishButton(m_zoomInBtn);
    connect(m_zoomInBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->zoomIn();
        updateZoomLabel();
    });
    m_iconSpecs.append(qMakePair(m_zoomInBtn, QString("zoom-in")));
    row->addWidget(m_zoomInBtn);
}

void FloatingToolbar::buildPagePill() {
    // Pill de páginas en la esquina inferior izquierda, espejo del estilo de la
    // pill de zoom. Hijo normal del overlay (máscara Hyprland-safe en
    // updateOverlayMask()); todos los controles con Qt::NoFocus para el lápiz.
    m_pagePill = new GlassCapsule(this);
    m_pagePill->setObjectName("finilyPagePill");
    auto* row = new QHBoxLayout(m_pagePill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(2);

    m_pagePrevBtn = new QToolButton(m_pagePill);
    m_pagePrevBtn->setFixedSize(30, 30);
    m_pagePrevBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pagePrevBtn, QString("chevron-left")));
    m_pagePrevBtn->setToolTip("Página anterior (Re Pág)");
    polishButton(m_pagePrevBtn);
    connect(m_pagePrevBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->prevPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pagePrevBtn);

    m_pageLabel = new QLabel("1 / 1", m_pagePill);
    m_pageLabel->setObjectName("finilyPageLabel");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setContentsMargins(4, 0, 4, 0);
    m_pageLabel->setFixedHeight(30);
    row->addWidget(m_pageLabel);

    m_pageNextBtn = new QToolButton(m_pagePill);
    m_pageNextBtn->setFixedSize(30, 30);
    m_pageNextBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageNextBtn, QString("chevron-right")));
    m_pageNextBtn->setToolTip("Página siguiente (Av Pág)");
    polishButton(m_pageNextBtn);
    connect(m_pageNextBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->nextPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pageNextBtn);


}

void FloatingToolbar::buildRestoreButton() {
    m_restoreBtn = new QToolButton(this);
    m_restoreBtn->setObjectName("finilyRestore");
    m_restoreBtn->setFixedSize(26, 26);
    m_restoreBtn->setIconSize(QSize(14, 14));
    m_restoreBtn->setToolTip("Mostrar interfaz (Tab)");
    polishButton(m_restoreBtn);
    connect(m_restoreBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_iconSpecs.append(qMakePair(m_restoreBtn, QString("restore")));
    m_restoreBtn->hide();
}

void FloatingToolbar::buildStrokePopover() {
    // Popover hijo normal del overlay (NO Qt::Popup): en Hyprland/Wayland las
    // ventanas Qt::Popup se malposicionan y pierden entrada del lápiz. Como hijo,
    // vive en el espacio de coordenadas del overlay y su geometría entra en la
    // máscara de eventos de updateOverlayMask().
    m_strokePopover = new GlassCapsule(this);
    m_strokePopover->setObjectName("finilyPopover");
    m_strokePopover->setFixedWidth(300);
    m_strokePopover->hide();

    auto* column = new QVBoxLayout(m_strokePopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    // --- Estilo del trazo (color, grosor, patrón); oculto con el borrador activo ---
    m_strokeStyleSection = new QWidget(m_strokePopover);
    auto* styleColumn = new QVBoxLayout(m_strokeStyleSection);
    styleColumn->setContentsMargins(0, 0, 0, 0);
    styleColumn->setSpacing(8);

    // --- Tipos de lápiz ilustrados (sin texto, solo puntas reales + tooltip) ---
    m_penTipSection = new QWidget(m_strokeStyleSection);
    auto* penTipRow = new QHBoxLayout(m_penTipSection);
    penTipRow->setContentsMargins(0, 0, 0, 0);
    penTipRow->setSpacing(6);
    const QStringList penTipNames = {"Bolígrafo", "Pluma fuente", "Plumón", "Pincel"};
    for (int i = 0; i < penTipNames.size(); ++i) {
        auto* btn = new QToolButton(m_penTipSection);
        btn->setFixedSize(63, 44);
        btn->setIconSize(QSize(58, 38));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(penTipNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas)
                m_canvas->setPenTip(i);
            refreshFromCanvas();
        });
        m_penTipBtns.append(btn);
        penTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_penTipSection);

    // --- Tipos de resaltador ilustrados (sin texto, solo puntas reales + tooltip) ---
    m_hlTipSection = new QWidget(m_strokeStyleSection);
    auto* hlTipRow = new QHBoxLayout(m_hlTipSection);
    hlTipRow->setContentsMargins(0, 0, 0, 0);
    hlTipRow->setSpacing(6);
    const QStringList hlTipNames = {"Resaltador biselado", "Resaltador redondo", "Subrayado recto"};
    for (int i = 0; i < hlTipNames.size(); ++i) {
        auto* btn = new QToolButton(m_hlTipSection);
        btn->setFixedSize(86, 40);
        btn->setIconSize(QSize(72, 34));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setToolTip(hlTipNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas)
                m_canvas->setHighlighterTip(i);
            refreshFromCanvas();
        });
        m_hlTipBtns.append(btn);
        hlTipRow->addWidget(btn);
    }
    styleColumn->addWidget(m_hlTipSection);

    // --- Modo de Lazo minimalista (solo íconos: Lazo libre / Rectangular) ---
    m_lassoModeSection = new QWidget(m_strokePopover);
    auto* lassoRow = new QHBoxLayout(m_lassoModeSection);
    lassoRow->setContentsMargins(0, 0, 0, 0);
    lassoRow->setSpacing(6);
    m_lassoFreeBtn = new QToolButton(m_lassoModeSection);
    m_lassoFreeBtn->setFixedSize(133, 38);
    m_lassoFreeBtn->setIconSize(QSize(20, 20));
    m_lassoFreeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoFreeBtn->setToolTip("Lazo libre");
    m_lassoFreeBtn->setCheckable(true);
    polishButton(m_lassoFreeBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoFreeBtn, QStringLiteral("lasso-free")));
    connect(m_lassoFreeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) m_canvas->setLassoRectMode(false);
        refreshFromCanvas();
        closePopovers();
    });
    m_lassoRectBtn = new QToolButton(m_lassoModeSection);
    m_lassoRectBtn->setFixedSize(133, 38);
    m_lassoRectBtn->setIconSize(QSize(20, 20));
    m_lassoRectBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_lassoRectBtn->setToolTip("Selección rectangular");
    m_lassoRectBtn->setCheckable(true);
    polishButton(m_lassoRectBtn);
    m_ctxIconSpecs.append(qMakePair(m_lassoRectBtn, QStringLiteral("lasso-rect")));
    connect(m_lassoRectBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas) m_canvas->setLassoRectMode(true);
        refreshFromCanvas();
        closePopovers();
    });
    lassoRow->addWidget(m_lassoFreeBtn);
    lassoRow->addWidget(m_lassoRectBtn);
    column->addWidget(m_lassoModeSection);

    auto* swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(6);
    for (int i = 0; i < 7; ++i) {
        auto* swatch = new ColorDotButton(m_strokeStyleSection);
        polishButton(swatch);
        connect(swatch, &QToolButton::clicked, this, [this, i] {
            if (i < m_palette.size())
                setColor(m_palette.at(i));
        });
        m_swatches.append(swatch);
        swatchRow->addWidget(swatch);
    }
    m_customColorBtn = new QToolButton(m_strokeStyleSection);
    m_customColorBtn->setFixedSize(24, 24);
    m_customColorBtn->setText("+");
    m_customColorBtn->setToolTip("Color personalizado…");
    polishButton(m_customColorBtn);
    connect(m_customColorBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        const QColor chosen = QColorDialog::getColor(m_canvas->inkColor(), window(), "Color personalizado");
        if (chosen.isValid()) {
            if (!m_palette.isEmpty())
                m_palette.last() = chosen;
            setColor(chosen);
            refreshFromCanvas();
        }
    });
    swatchRow->addWidget(m_customColorBtn);
    swatchRow->addStretch(1);
    styleColumn->addLayout(swatchRow);

    m_fillStyleSection = new QWidget(m_strokeStyleSection);
    auto* fillLayout = new QGridLayout(m_fillStyleSection);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(6);
    auto* fillGroup = new QButtonGroup(m_fillStyleSection);
    const QPair<ShadePattern, QString> fills[] = {
        {ShadePattern::Solid, tr("Color sólido")},
        {ShadePattern::Soft, tr("Tono suave")},
        {ShadePattern::Diagonal, tr("Rayado diagonal")},
        {ShadePattern::Crosshatch, tr("Rayado cruzado")},
        {ShadePattern::Grid, tr("Cuadrícula fina")},
        {ShadePattern::Dotted, tr("Punteado")},
    };
    for (const auto& [pattern, label] : fills) {
        auto* button = new QToolButton(m_fillStyleSection);
        button->setToolTip(label);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedSize(86, 42);
        button->setIconSize(QSize(58, 36));
        button->setCheckable(true);
        polishButton(button);
        fillGroup->addButton(button);
        const auto index = m_fillStyles.size();
        fillLayout->addWidget(button, index / 3, index % 3);
        m_fillStyles.append({button, pattern});
        connect(button, &QToolButton::clicked, this, [this, pattern] {
            if (m_canvas)
                m_canvas->setShadePattern(pattern);
            refreshFromCanvas();
            closePopovers();
        });
    }
    styleColumn->addWidget(m_fillStyleSection);

    m_strokeGeometrySection = new QWidget(m_strokeStyleSection);
    auto* geometryColumn = new QVBoxLayout(m_strokeGeometrySection);
    geometryColumn->setContentsMargins(0, 0, 0, 0);
    geometryColumn->setSpacing(8);
    styleColumn->addWidget(m_strokeGeometrySection);

    // Grosor dividido en 10 puntos sobre la línea.
    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(10);
    m_sizeLabel = new QLabel("3 / 10", m_strokeStyleSection);
    m_sizeLabel->hide();
    m_strokePreview = new StrokePreviewWidget(m_strokeGeometrySection);
    sizeRow->addWidget(m_strokePreview);
    m_sizeSlider = new DotStepSlider(m_strokeGeometrySection);
    m_sizeSlider->setRange(1, 10);
    m_sizeSlider->setPageStep(1);
    m_sizeSlider->setSingleStep(1);
    m_sizeSlider->setToolTip("Grosor del trazo");
    sizeRow->addWidget(m_sizeSlider, 1);
    geometryColumn->addLayout(sizeRow);
    column->addWidget(m_strokeStyleSection);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        const int idx = std::clamp(level - 1, 0, 9);
        const bool isHl = m_canvas->tool() == ToolType::Highlighter;
        const double px = isHl ? kHlLevels[idx] : kPenLevels[idx];
        m_canvas->setStrokeSize(px);
        m_sizeLabel->setText(QString("%1 / 10").arg(level));
        updateStrokePreview();
        syncActivePresetFromCanvas();
        updateStationery();
    });

    // Los patrones de línea NO aparecen en Lápiz ni Resaltador (solo en Figuras/Polilínea).
    // --- Estabilizador minimalista con puntitos discretos sobre la línea (sin números) ---
    m_stabilizerRow = new QWidget(m_strokePopover);
    auto* stabRow = new QHBoxLayout(m_stabilizerRow);
    stabRow->setContentsMargins(0, 2, 0, 0);
    stabRow->setSpacing(10);
    m_stabilizerIcon = new QLabel(m_stabilizerRow);
    m_stabilizerIcon->setFixedSize(28, 26);
    m_stabilizerIcon->setAlignment(Qt::AlignCenter);
    m_stabilizerIcon->setToolTip("Estabilizador de trazo");
    stabRow->addWidget(m_stabilizerIcon);
    m_stabilizerSlider = new DotStepSlider(m_stabilizerRow);
    m_stabilizerSlider->setRange(0, 9);
    m_stabilizerSlider->setSingleStep(1);
    m_stabilizerSlider->setPageStep(1);
    m_stabilizerSlider->setToolTip("Estabilizador de trazo");
    stabRow->addWidget(m_stabilizerSlider, 1);
    m_stabilizerLabel = new QLabel(m_stabilizerRow);
    m_stabilizerLabel->hide();
    connect(m_stabilizerSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStabilizer(double(level) / m_stabilizerSlider->maximum());
    });
    column->addWidget(m_stabilizerRow);

    // --- Borrador minimalista (solo 2 íconos + grosor en 10 pasos) ---
    m_eraserSection = new QWidget(m_strokePopover);
    auto* eraserColumn = new QVBoxLayout(m_eraserSection);
    eraserColumn->setContentsMargins(0, 0, 0, 0);
    eraserColumn->setSpacing(8);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    m_eraserStrokeBtn = new QToolButton(m_eraserSection);
    m_eraserStrokeBtn->setFixedSize(133, 38);
    m_eraserStrokeBtn->setIconSize(QSize(20, 20));
    m_eraserStrokeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_eraserStrokeBtn->setToolTip("Borrar trazo completo");
    m_eraserStrokeBtn->setCheckable(true);
    polishButton(m_eraserStrokeBtn);
    m_ctxIconSpecs.append(qMakePair(m_eraserStrokeBtn, QStringLiteral("eraser-stroke")));
    connect(m_eraserStrokeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Stroke);
        refreshFromCanvas();
    });
    m_eraserPartialBtn = new QToolButton(m_eraserSection);
    m_eraserPartialBtn->setFixedSize(133, 38);
    m_eraserPartialBtn->setIconSize(QSize(20, 20));
    m_eraserPartialBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_eraserPartialBtn->setToolTip("Borrador parcial (precisión)");
    m_eraserPartialBtn->setCheckable(true);
    polishButton(m_eraserPartialBtn);
    m_ctxIconSpecs.append(qMakePair(m_eraserPartialBtn, QStringLiteral("eraser-partial")));
    connect(m_eraserPartialBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Partial);
        refreshFromCanvas();
    });
    modeRow->addWidget(m_eraserStrokeBtn);
    modeRow->addWidget(m_eraserPartialBtn);
    eraserColumn->addLayout(modeRow);

    auto* erSizeRow = new QHBoxLayout();
    erSizeRow->setSpacing(10);
    m_eraserSizeSlider = new DotStepSlider(m_eraserSection);
    m_eraserSizeSlider->setRange(1, 10);
    m_eraserSizeSlider->setSingleStep(1);
    m_eraserSizeSlider->setPageStep(1);
    m_eraserSizeSlider->setToolTip("Tamaño del borrador");
    erSizeRow->addWidget(m_eraserSizeSlider, 1);
    m_eraserSizeLabel = new QLabel(m_eraserSection);
    m_eraserSizeLabel->hide();
    eraserColumn->addLayout(erSizeRow);
    connect(m_eraserSizeSlider, &QSlider::valueChanged, this, [this](int level) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setEraserSize(level * 8.0);
    });
    column->addWidget(m_eraserSection);
}

void FloatingToolbar::buildPaperPopover() {
    // Popover hijo normal del overlay (NO Qt::Popup): ver buildStrokePopover().
    m_paperPopover = new GlassCapsule(this);
    m_paperPopover->setObjectName("finilyPopover");
    m_paperPopover->setFixedWidth(300);
    m_paperPopover->hide();

    auto* column = new QVBoxLayout(m_paperPopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    auto* themeGrid = new QGridLayout();
    themeGrid->setHorizontalSpacing(4);
    themeGrid->setVerticalSpacing(0);
    const QVector<ThemeId> themes = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight, ThemeId::PaperCream};
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(56, 44);
        button->setIconSize(QSize(34, 34));
        {
            const ThemeConfig tc = getThemeConfig(themes.at(i));
            QPixmap pm(68, 68);
            pm.fill(Qt::transparent);
            QPainter pp(&pm);
            pp.setRenderHint(QPainter::Antialiasing, true);
            pp.setPen(QPen(QColor(128, 128, 128, 110), 2));
            pp.setBrush(tc.background);
            pp.drawEllipse(QRectF(3, 3, 62, 62));
            pp.end();
            pm.setDevicePixelRatio(2.0);
            button->setIcon(QIcon(pm));
            button->setToolTip(tc.name);
        }
        button->setCheckable(true);
        polishButton(button);
        const ThemeId theme = themes.at(i);
        connect(button, &QToolButton::clicked, this, [this, theme] {
            if (m_canvas)
                m_canvas->setThemeId(theme);
            refreshFromCanvas();
        });
        m_themeBtns.append(button);
        m_themeSpecs.append(theme);
        themeGrid->addWidget(button, 0, i);
    }
    column->addLayout(themeGrid);

    auto* templateRow = new QHBoxLayout();
    templateRow->setSpacing(4);
    const QVector<QString> templateNames = {"Liso", "Cuadrícula", "Puntos", "Rayado"};
    const QVector<BackgroundTemplate> templates = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                                   BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
    for (int i = 0; i < templates.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(66, 40);
        button->setIconSize(QSize(44, 30));
        button->setCheckable(true);
        button->setToolTip(templateNames.at(i));
        polishButton(button);
        const BackgroundTemplate tpl = templates.at(i);
        connect(button, &QToolButton::clicked, this, [this, tpl] {
            if (m_canvas)
                m_canvas->setBackgroundTemplate(tpl);
            refreshFromCanvas();
        });
        m_templateBtns.append(button);
        m_templateSpecs.append(qMakePair(button, tpl));
        templateRow->addWidget(button);
    }
    templateRow->addStretch(1);
    column->addLayout(templateRow);

    // Formato de hoja: Infinita / Hoja A4 / Diapo 16:9 (PageFormat), sincronizado
    // con el lienzo en refreshFromCanvas().
    auto* formatRow = new QHBoxLayout();
    formatRow->setSpacing(4);
    const QVector<QPair<QString, PageFormat>> formats = {
        {QStringLiteral("Infinita"), PageFormat::Infinite},
        {QStringLiteral("A4"), PageFormat::A4},
        {QStringLiteral("16:9"), PageFormat::Slide16x9},
    };
    for (int i = 0; i < formats.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(88, 30);
        button->setText(formats.at(i).first);
        button->setCheckable(true);
        polishButton(button);
        const PageFormat format = formats.at(i).second;
        connect(button, &QToolButton::clicked, this, [this, format] {
            if (m_canvas)
                m_canvas->setPageFormat(format);
            refreshFromCanvas();
        });
        m_formatBtns.append(button);
        m_formatSpecs.append(format);
        formatRow->addWidget(button);
    }
    formatRow->addStretch(1);
    column->addLayout(formatRow);

    // Posición acoplable del dock de papelería: segmentado de 3 botones,
    // persistido en QSettings("Finily", "Finily").
    auto* dockRow = new QHBoxLayout();
    dockRow->setSpacing(4);
    m_dockLeftBtn = new QToolButton(m_paperPopover);
    m_dockLeftBtn->setFixedSize(88, 30);
    m_dockLeftBtn->setText("Izquierda");
    m_dockLeftBtn->setToolTip("Ancla el dock a la izquierda");
    m_dockLeftBtn->setCheckable(true);
    polishButton(m_dockLeftBtn);
    connect(m_dockLeftBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Izquierda); });
    dockRow->addWidget(m_dockLeftBtn);
    m_dockTopBtn = new QToolButton(m_paperPopover);
    m_dockTopBtn->setFixedSize(88, 30);
    m_dockTopBtn->setText("Arriba");
    m_dockTopBtn->setToolTip("Ancla el dock arriba");
    m_dockTopBtn->setCheckable(true);
    polishButton(m_dockTopBtn);
    connect(m_dockTopBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Arriba); });
    dockRow->addWidget(m_dockTopBtn);
    m_dockBottomBtn = new QToolButton(m_paperPopover);
    m_dockBottomBtn->setFixedSize(88, 30);
    m_dockBottomBtn->setText("Abajo");
    m_dockBottomBtn->setToolTip("Ancla el dock abajo");
    m_dockBottomBtn->setCheckable(true);
    polishButton(m_dockBottomBtn);
    connect(m_dockBottomBtn, &QToolButton::clicked, this, [this] { setDockPosition(DockPos::Abajo); });
    dockRow->addWidget(m_dockBottomBtn);
    column->addLayout(dockRow);

    m_clearBtn = new QPushButton("Borrar anotaciones de esta página", m_paperPopover);
    m_clearBtn->setFixedSize(272, 34);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFocusPolicy(Qt::NoFocus);
    m_clearBtn->setToolTip("Borra las anotaciones de la página. Conserva el fondo PDF.");
    connect(m_clearBtn, &QPushButton::clicked, this, &FloatingToolbar::onClearClicked);
    column->addWidget(m_clearBtn);
    column->addStretch(1);
}

void FloatingToolbar::buildShapesPopover() {
    // Popover hijo normal del overlay (patrón Hyprland-safe, ver buildStrokePopover).
    m_shapesPopover = new GlassCapsule(this);
    m_shapesPopover->setObjectName("finilyPopover");
    m_shapesPopover->setFixedWidth(344);
    m_shapesPopover->hide();

    auto* column = new QVBoxLayout(m_shapesPopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    // Los remates y los patrones se eligen independientemente de la forma.
    struct ShapeSpec { QString name, glyph, tip; ShapeKind kind; bool dimension; int sides; int tri; int quad; int solid; };
    const QVector<QPair<QString, QVector<ShapeSpec>>> pages = {
        {QStringLiteral("Líneas"), {
            {"Segmento", "shape-line", "Arrastra para dibujar. Mayús: ajusta el ángulo", ShapeKind::Line, false, 0, -1, 0, -1},
            {"Curva", "shape-curve", "Curva con punto central editable", ShapeKind::Curve, false, 0, -1, 0, -1},
            {"Arco circular", "shape-arc", "Traza un radio y ajusta sus extremos", ShapeKind::Arc, false, 0, -1, 0, -1},
            {"Cota", "shape-dimension", "Línea de medida con topes y etiqueta editable", ShapeKind::Line, true, 0, -1, 0, -1}}},
        {QStringLiteral("Formas"), {
            {"Óvalo / Círculo", "shape-ellipse", "Mayús para un círculo perfecto", ShapeKind::Ellipse, false, 0, -1, 0, -1},
            {"Rectángulo", "shape-rect", "Con Mayús sale cuadrado perfecto", ShapeKind::Rectangle, false, 0, -1, 0, -1},
            {"Sector circular", "shape-sector", "Traza un radio y ajusta la abertura", ShapeKind::Sector, false, 0, -1, 0, -1},
            {"Rombo", "shape-poly-4", "Rombo con 4 vértices editables", ShapeKind::Polyline, false, 4, -1, 0, -1},
            {"Paralelogramo", "shape-parallelogram", "Lados opuestos paralelos", ShapeKind::Polyline, false, 4, -1, 1, -1},
            {"Trapecio isósceles", "shape-trap-iso", "Trapecio simétrico editable", ShapeKind::Polyline, false, 4, -1, 2, -1},
            {"Trapecio rectángulo", "shape-trap-right", "Trapecio con 2 ángulos rectos", ShapeKind::Polyline, false, 4, -1, 3, -1},
            {"Trapezoide", "shape-trapezoid", "Cuadrilátero libre de 4 vértices", ShapeKind::Polyline, false, 4, -1, 4, -1},
            {"Triángulo isósceles", "shape-triangle", "Triángulo con vértice centrado", ShapeKind::Triangle, false, 0, 0, 0, -1},
            {"Triángulo rectángulo", "shape-tri-right", "Triángulo rectángulo con cuadradito 90° automático", ShapeKind::Triangle, false, 0, 1, 0, -1},
            {"Triángulo equilátero", "shape-tri-equi", "Tres lados iguales", ShapeKind::Triangle, false, 0, 3, 0, -1},
            {"Pentágono", "shape-poly-5", "Polígono regular de 5 lados", ShapeKind::Polyline, false, 5, -1, 0, -1},
            {"Hexágono", "shape-poly-6", "Polígono regular de 6 lados", ShapeKind::Polyline, false, 6, -1, 0, -1},
            {"Heptágono", "shape-poly-7", "Polígono regular de 7 lados", ShapeKind::Polyline, false, 7, -1, 0, -1},
            {"Octágono", "shape-poly-8", "Polígono regular de 8 lados", ShapeKind::Polyline, false, 8, -1, 0, -1}}},
        {QStringLiteral("Sólidos"), {
            {"Cubo / Hexaedro", "shape-3d-cube", "Hexaedro con aristas ocultas punteadas", ShapeKind::Solid3D, false, 0, -1, 0, 0},
            {"Prisma", "shape-3d-prism", "Prisma triangular recto", ShapeKind::Solid3D, false, 0, -1, 0, 1},
            {"Cilindro", "shape-3d-cyl", "Cilindro circular recto", ShapeKind::Solid3D, false, 0, -1, 0, 2},
            {"Cono", "shape-3d-cone", "Cono circular recto", ShapeKind::Solid3D, false, 0, -1, 0, 3},
            {"Esfera", "shape-3d-sphere", "Esfera con ecuador en perspectiva", ShapeKind::Solid3D, false, 0, -1, 0, 4},
            {"Pirámide", "shape-3d-pyr", "Pirámide cuadrangular", ShapeKind::Solid3D, false, 0, -1, 0, 5},
            {"Tetraedro", "shape-3d-tetra", "Tetraedro 3D", ShapeKind::Solid3D, false, 0, -1, 0, 6},
            {"Octaedro", "shape-3d-octa", "Octaedro regular 3D", ShapeKind::Solid3D, false, 0, -1, 0, 7},
            {"Tronco de cono", "shape-3d-fcone", "Tronco de cono", ShapeKind::Solid3D, false, 0, -1, 0, 8},
            {"Tronco de pirámide", "shape-3d-fpyr", "Tronco de pirámide cuadrangular", ShapeKind::Solid3D, false, 0, -1, 0, 9}}},
        {QStringLiteral("Matemáticas"), {
            {"Ejes X/Y", "shape-axes", "Plano cartesiano con ejes X/Y", ShapeKind::Axes, false, 0, -1, 0, -1},
            {"Raíz √", "shape-radical", "Raíz cuadrada auto-ajustable (R)", ShapeKind::Radical, false, 0, -1, 0, -1},
            {"Ángulo", "shape-angle", "Dos lados y una medida automática editable", ShapeKind::AngleMarker, false, 0, -1, 0, -1},
            {"Ángulo recto", "shape-rightangle", "Marca de ángulo de 90°", ShapeKind::RightAngle, false, 0, -1, 0, -1},
            {"Llave", "shape-brace", "Llave { } para agrupar", ShapeKind::Brace, false, 0, -1, 0, -1}}},
    };


    auto* tabsRow = new QHBoxLayout();
    tabsRow->setSpacing(2);
    auto* tabGroup = new QButtonGroup(m_shapesPopover);
    tabGroup->setExclusive(true);
    auto* stack = new QStackedWidget(m_shapesPopover);
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        auto* tab = new QToolButton(m_shapesPopover);
        tab->setObjectName("finilyShapeBtn");
        tab->setText(pages.at(pageIndex).first);
        tab->setCheckable(true);
        tab->setChecked(pageIndex == 0);
        tab->setFixedHeight(26);
        tab->setCursor(Qt::PointingHandCursor);
        QFont tabFont = tab->font();
        tabFont.setPixelSize(11);
        tab->setFont(tabFont);
        polishButton(tab);
        tabGroup->addButton(tab, pageIndex);
        tabsRow->addWidget(tab);

        auto* page = new QWidget(stack);
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(6);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        const auto& specs = pages.at(pageIndex).second;
        for (int i = 0; i < specs.size(); ++i) {
            const ShapeSpec spec = specs.at(i);
            auto* button = new QToolButton(page);
            button->setObjectName("finilyShapeBtn");
            button->setFixedSize(54, 40);
            button->setIconSize(QSize(20, 20));
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setToolTip(spec.name + " · " + spec.tip);
            button->setCheckable(true);
            polishButton(button);
            connect(button, &QToolButton::clicked, this, [this, spec] {
                if (!m_canvas)
                    return;
                m_canvas->setShapeKind(spec.kind);
                if (spec.sides > 0)
                    m_canvas->setPolygonSides(spec.sides);
                m_canvas->setQuadVariant(spec.quad);
                if (spec.solid >= 0)
                    m_canvas->setSolidKind(spec.solid);
                if (spec.tri >= 0)
                    m_canvas->setTriangleVariant(spec.tri);
                if (spec.dimension)
                    m_canvas->setLinePattern(LinePattern::Dimension);
                else if (m_canvas->linePattern() == LinePattern::Dimension)
                    m_canvas->setLinePattern(LinePattern::Solid);
                m_canvas->setTool(ToolType::Shape);
                closePopovers();
                refreshFromCanvas();
            });
            m_shapeKindBtns.append(button);
            m_shapeKindSpecs.append(spec.kind);
            m_shapeDimensionSpecs.append(spec.dimension);
            m_shapeSidesSpecs.append(spec.sides);
            m_shapeTriSpecs.append(spec.tri);
            m_shapeQuadSpecs.append(spec.quad);
            m_shapeSolidSpecs.append(spec.solid);
            m_iconSpecs.append(qMakePair(button, spec.glyph));
            grid->addWidget(button, i / 5, i % 5);
        }
        if (pageIndex == 0) { // polilínea: herramienta aparte, vive junto a las básicas
            auto* poly = new QToolButton(page);
            poly->setObjectName("finilyShapeBtn");
            poly->setFixedSize(54, 40);
            poly->setIconSize(QSize(20, 20));
            poly->setToolTip(tr("Polilínea (L) · puntos libres; Mayús + arrastrar para editar una figura"));
            polishButton(poly);
            connect(poly, &QToolButton::clicked, this, [this] {
                closePopovers();
                selectTool(ToolType::Polyline);
            });
            m_iconSpecs.append(qMakePair(poly, QString("polyline")));
            grid->addWidget(poly, specs.size() / 5, specs.size() % 5);
        }
        stack->addWidget(page);
    }
    // Cada página ocupa solo su alto: el menú se ajusta a la página visible.
    auto fitStack = [this, stack](int index) {
        for (int i = 0; i < stack->count(); ++i)
            stack->widget(i)->setSizePolicy(i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                            i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        stack->setCurrentIndex(index);
        stack->updateGeometry();
        if (m_shapesPopover) {
            if (m_shapesPopover->layout())
                m_shapesPopover->layout()->activate();
            m_shapesPopover->adjustSize();
            if (m_openPopover == m_shapesPopover) {
                positionOpenPopover();
                updateOverlayMask();
            }
        }
    };
    connect(tabGroup, &QButtonGroup::idClicked, this, fitStack);
    fitStack(0);
    column->addLayout(tabsRow);
    column->addWidget(stack);

    // Estilo de trazo para figuras y segmentos (Sólida / Guiones / Punteada)
    auto* lineStyleRow = new QHBoxLayout();
    lineStyleRow->setSpacing(6);
    const QPair<LinePattern, QString> shapeLines[] = {
        {LinePattern::Solid, QStringLiteral("Línea sólida")},
        {LinePattern::Dashed, QStringLiteral("Línea discontinua")},
        {LinePattern::Dotted, QStringLiteral("Línea punteada")},
    };
    for (const auto& [pat, tip] : shapeLines) {
        auto* btn = new QToolButton(m_shapesPopover);
        btn->setFixedSize(86, 28);
        btn->setIconSize(QSize(44, 14));
        btn->setToolTip(tip);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, pat] {
            if (m_canvas && m_canvas->linePattern() != LinePattern::Dimension)
                m_canvas->setLinePattern(pat);
            refreshFromCanvas();
        });
        m_patternBtns.append(btn);
        m_patternSpecs.append(qMakePair(btn, pat));
        lineStyleRow->addWidget(btn);
    }
    column->addLayout(lineStyleRow);
    m_shapeEndings = new QComboBox(m_shapesPopover);
    m_shapeEndings->addItems({tr("Sin punta"), tr("Flecha"), tr("Doble flecha")});
    m_shapeEndings->setToolTip(tr("Remate para segmentos, curvas y polilíneas"));
    connect(m_shapeEndings, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_canvas->setLineEnding(LineEnding(index));
    });
    column->addWidget(m_shapeEndings);
    column->addStretch(1);
}

void FloatingToolbar::buildContextPill() {
    // Mini cápsula flotante de solo íconos pegada al objeto seleccionado.
    m_contextPill = new GlassCapsule(this);
    m_contextPill->setObjectName("finilyContext");
    auto* row = new QHBoxLayout(m_contextPill);
    row->setContentsMargins(6, 5, 6, 5);
    row->setSpacing(3);

    auto makeCtxBtn = [&](const QString& iconKind, const QString& tip, bool accent = false) {
        auto* b = new QToolButton(m_contextPill);
        if (accent)
            b->setObjectName("finilyAccentBtn");
        b->setFixedSize(32, 32);
        b->setIconSize(QSize(18, 18));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(tip);
        polishButton(b);
        m_ctxIconSpecs.append(qMakePair(b, iconKind));
        row->addWidget(b);
        return b;
    };

    m_finishPolylineBtn = makeCtxBtn("check", "Terminar polilínea (Enter)");
    connect(m_finishPolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(false);
        refreshFromCanvas();
    });

    m_closePolylineBtn = makeCtxBtn("close-poly", "Cerrar polígono");
    connect(m_closePolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(true);
        refreshFromCanvas();
    });

    m_selectionActions = makeCtxBtn("more", tr("Acciones de selección (clic derecho)"));
    connect(m_selectionActions, &QToolButton::clicked, this, [this] {
        showSelectionMenu(m_selectionActions->mapToGlobal(QPoint(0, 0)));
    });
    m_contextPill->hide();
}

void FloatingToolbar::showSelectionMenu(const QPoint& position) {
    if (!m_canvas || m_cleanUi || !m_canvas->hasSelection() || m_canvas->isDrawingPolyline())
        return;
    QMenu menu(this);
    const auto style = m_canvas->selectionStyle();
    if (style.color || style.filled)
        menu.addAction(tr("Propiedades…"), this, &FloatingToolbar::editSelectionProperties);
    menu.addAction(tr("Cortar"), this, [this] { m_canvas->cutSelection(); });
    menu.addAction(tr("Copiar"), this, [this] { m_canvas->copySelection(); });
    menu.addAction(tr("Duplicar"), this, [this] { m_canvas->duplicateSelection(); });
    if (m_canvas->canGroupSelection())
        menu.addAction(tr("Agrupar"), this, [this] { m_canvas->groupSelection(); });
    if (m_canvas->canUngroupSelection())
        menu.addAction(tr("Desagrupar"), this, [this] { m_canvas->ungroupSelection(); });
    if (m_canvas->hasLockableSelection())
        menu.addAction(m_canvas->isSelectionLocked() ? tr("Desbloquear") : tr("Bloquear"),
                       this, [this] { m_canvas->toggleLockSelection(); });
    if (m_canvas->hasSelectedStrokes()) {
        menu.addSeparator();
        menu.addAction(tr("Nivelar escritura"), this, [this] { m_canvas->straightenSelection(); });
        menu.addAction(tr("Pasar a texto editable"), this, [this] { m_canvas->convertSelectionToText(); });
        menu.addAction(tr("Guardar símbolo como plantilla…"), this, [this] {
            bool accepted = false;
            const auto name = QInputDialog::getText(window(), tr("Guardar símbolo"),
                tr("Número, signo o letra"), QLineEdit::Normal, {}, &accepted);
            if (accepted)
                m_canvas->saveSelectionAsTemplate(name);
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Eliminar"), this, [this] { m_canvas->deleteSelection(); });
    menu.exec(position);
}

void FloatingToolbar::editSelectionProperties() {
    const auto current = m_canvas->selectionStyle();
    SelectionStyle edits;
    QDialog dialog(window());
    dialog.setWindowTitle(tr("Propiedades de la selección"));
    auto* form = new QFormLayout(&dialog);
    auto colorField = [&](const QString& label, QColor initial, std::optional<QColor>& value) {
        auto* button = new QPushButton(initial.name(), &dialog);
        connect(button, &QPushButton::clicked, &dialog, [button, &dialog, &value] {
            const auto color = QColorDialog::getColor(QColor(button->text()), &dialog);
            if (color.isValid()) {
                value = color;
                button->setText(color.name());
            }
        });
        form->addRow(label, button);
        return button;
    };
    auto options = [&](const QString& label, const QStringList& names, int currentIndex) {
        auto* combo = new QComboBox(&dialog);
        combo->addItems(names);
        combo->setCurrentIndex(currentIndex);
        form->addRow(label, combo);
        return combo;
    };
    if (current.color)
        colorField(current.width ? tr("Color de contorno") : tr("Color"), *current.color, edits.color);
    if (current.width) {
        auto* width = new QDoubleSpinBox(&dialog);
        width->setDecimals(1);
        width->setRange(.1, 200);
        width->setSingleStep(.5);
        width->setSuffix(tr(" px"));
        width->setValue(*current.width);
        connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { edits.width = value; });
        form->addRow(tr("Grosor"), width);
    }
    if (current.pattern) {
        auto* pattern = options(tr("Línea"), {tr("Sólida"), tr("Guiones"), tr("Punteada")}, int(*current.pattern));
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.pattern = LinePattern(index); });
    }
    if (current.ending) {
        auto* ending = options(tr("Remates"), {tr("Sin punta"), tr("Flecha"), tr("Doble flecha")}, int(*current.ending));
        connect(ending, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.ending = LineEnding(index); });
    }
    if (current.label) {
        auto* label = new QLineEdit(*current.label, &dialog);
        label->setMaxLength(80);
        label->setPlaceholderText(tr("Escribe una medida o una etiqueta"));
        connect(label, &QLineEdit::textChanged, &dialog,
                [&](const QString& value) { edits.label = value.trimmed(); });
        form->addRow(tr("Etiqueta"), label);
    }
    if (current.filled) {
        auto* filled = new QCheckBox(tr("Con relleno"), &dialog);
        filled->setChecked(*current.filled);
        form->addRow(filled);
        auto* fillColor = colorField(tr("Color de relleno"),
                                     current.fillColor.value_or(current.color.value_or(m_canvas->inkColor())),
                                     edits.fillColor);
        auto* pattern = options(tr("Patrón de relleno"), {tr("Suave"), tr("Diagonal"), tr("Punteado"),
                                tr("Sólido"), tr("Rayado cruzado"), tr("Cuadrícula")},
                                int(current.fillPattern.value_or(ShadePattern::Soft)));
        fillColor->setEnabled(*current.filled);
        pattern->setEnabled(*current.filled);
        connect(filled, &QCheckBox::toggled, &dialog, [&, fillColor, pattern](bool checked) {
            edits.filled = checked;
            fillColor->setEnabled(checked);
            pattern->setEnabled(checked);
        });
        connect(pattern, &QComboBox::currentIndexChanged, &dialog,
                [&](int index) { edits.fillPattern = ShadePattern(index); });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        m_canvas->setSelectionStyle(edits);
}

void FloatingToolbar::buildFilePopover() {
    // Popover hijo normal del overlay (patrón Hyprland-safe, ver buildStrokePopover).
    m_filePopover = new GlassCapsule(this);
    m_filePopover->setObjectName("finilyPopover");
    m_filePopover->setFixedWidth(300);
    m_filePopover->hide();

    auto* column = new QVBoxLayout(m_filePopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    // --- Sección 1: Cuaderno (.finily) ---
    auto* paperRow = new QToolButton(m_filePopover);
    paperRow->setFixedSize(272, 34);
    paperRow->setText("Papel y fondo");
    polishButton(paperRow);
    connect(paperRow, &QToolButton::clicked, this, [this] {
        closePopovers();
        openPopover(m_paperPopover, m_fileBtn);
    });
    column->addWidget(paperRow);
    makeMenuRow(paperRow);
    column->addWidget(makeDivider(m_filePopover));

    m_newNotebookBtn = new QToolButton(m_filePopover);
    m_newNotebookBtn->setFixedSize(272, 34);
    m_newNotebookBtn->setText("Nuevo cuaderno (Ctrl+N)");
    m_newNotebookBtn->setToolTip("Empieza un cuaderno en blanco (descarta el contenido actual)");
    polishButton(m_newNotebookBtn);
    connect(m_newNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::newDocumentAction);
    column->addWidget(m_newNotebookBtn);

    m_openNotebookBtn = new QToolButton(m_filePopover);
    m_openNotebookBtn->setFixedSize(272, 34);
    m_openNotebookBtn->setText("Abrir cuaderno... (Ctrl+Shift+O)");
    m_openNotebookBtn->setToolTip("Abre un cuaderno guardado con extensión .finily");
    polishButton(m_openNotebookBtn);
    connect(m_openNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::openNotebookDialog);
    column->addWidget(m_openNotebookBtn);

    m_saveNotebookBtn = new QToolButton(m_filePopover);
    m_saveNotebookBtn->setFixedSize(272, 34);
    m_saveNotebookBtn->setText("Guardar (Ctrl+S)");
    m_saveNotebookBtn->setToolTip("Guarda el cuaderno actual (.finily); si aún no tiene archivo, pide nombre");
    polishButton(m_saveNotebookBtn);
    connect(m_saveNotebookBtn, &QToolButton::clicked, this, &FloatingToolbar::saveNotebookAction);
    column->addWidget(m_saveNotebookBtn);

    m_saveNotebookAsBtn = new QToolButton(m_filePopover);
    m_saveNotebookAsBtn->setFixedSize(272, 34);
    m_saveNotebookAsBtn->setText("Guardar como... (Ctrl+Shift+S)");
    m_saveNotebookAsBtn->setToolTip("Guarda el cuaderno en un archivo .finily nuevo (añade la extensión si falta)");
    polishButton(m_saveNotebookAsBtn);
    connect(m_saveNotebookAsBtn, &QToolButton::clicked, this, &FloatingToolbar::saveNotebookAsDialog);
    column->addWidget(m_saveNotebookAsBtn);

    column->addWidget(makeDivider(m_filePopover));

    // --- Sección 2: PDF e Imágenes ---
    m_importPdfBtn = new QToolButton(m_filePopover);
    m_importPdfBtn->setFixedSize(272, 34);
    m_importPdfBtn->setText("Importar PDF (Ctrl+O)");
    m_importPdfBtn->setToolTip("Inserta las hojas de un PDF como páginas del cuaderno");
    polishButton(m_importPdfBtn);
    connect(m_importPdfBtn, &QToolButton::clicked, this, &FloatingToolbar::importPdfDialog);
    column->addWidget(m_importPdfBtn);

    m_importImageBtn = new QToolButton(m_filePopover);
    m_importImageBtn->setFixedSize(272, 34);
    m_importImageBtn->setText("Insertar imagen...");
    m_importImageBtn->setToolTip("Inserta una imagen PNG, JPG, WEBP o BMP desde el disco");
    polishButton(m_importImageBtn);
    connect(m_importImageBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        closePopovers();
        QSettings settings("Finily", "Finily");
        const QString lastDir = settings.value("files/lastImageDir").toString();
        const QString path = QFileDialog::getOpenFileName(window(), "Insertar imagen", lastDir,
                                                          "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (!path.isEmpty()) {
            settings.setValue("files/lastImageDir", QFileInfo(path).absolutePath());
            m_canvas->importImageFile(path);
            rememberRecentImage(path);
            refreshFromCanvas();
        }
    });
    column->addWidget(m_importImageBtn);

    m_pasteBtn = new QToolButton(m_filePopover);
    m_pasteBtn->setFixedSize(272, 34);
    m_pasteBtn->setText("Pegar imagen (Ctrl+V)");
    m_pasteBtn->setToolTip("Pega la imagen del portapapeles en el lienzo");
    polishButton(m_pasteBtn);
    connect(m_pasteBtn, &QToolButton::clicked, this, [this] {
        closePopovers();
        if (m_canvas)
            m_canvas->pasteFromClipboard();
        refreshFromCanvas();
    });
    column->addWidget(m_pasteBtn);

    // Galería rápida de imágenes recientes (hasta 4 miniaturas clicables).
    m_recentGalleryWrap = new QWidget(m_filePopover);
    auto* galCol = new QVBoxLayout(m_recentGalleryWrap);
    galCol->setContentsMargins(2, 4, 2, 4);
    galCol->setSpacing(4);
    auto* galLbl = new QLabel("Galería rápida", m_recentGalleryWrap);
    galLbl->setObjectName("finilySectionTitle");
    galCol->addWidget(galLbl);
    m_recentGalleryRow = new QHBoxLayout();
    m_recentGalleryRow->setContentsMargins(0, 0, 0, 0);
    m_recentGalleryRow->setSpacing(6);
    galCol->addLayout(m_recentGalleryRow);
    column->addWidget(m_recentGalleryWrap);
    rebuildRecentGallery();

    m_exportBtn = new QToolButton(m_filePopover);
    m_exportBtn->setFixedSize(272, 34);
    m_exportBtn->setText("Exportar clase a PDF (Ctrl+E)");
    m_exportBtn->setToolTip("Guarda toda la clase (trazos, figuras e imágenes) como PDF");
    polishButton(m_exportBtn);
    connect(m_exportBtn, &QToolButton::clicked, this, &FloatingToolbar::exportPdfDialog);
    column->addWidget(m_exportBtn);

    for (QToolButton* item : {m_newNotebookBtn, m_openNotebookBtn, m_saveNotebookBtn, m_saveNotebookAsBtn,
                              m_importPdfBtn, m_importImageBtn, m_pasteBtn, m_exportBtn})
        makeMenuRow(item);
    column->setSpacing(2);

    m_pdfInfoLabel = new QLabel(m_filePopover);
    m_pdfInfoLabel->setWordWrap(true);
    m_pdfInfoLabel->hide();
    column->addWidget(m_pdfInfoLabel);

    column->addWidget(makeDivider(m_filePopover));
    // --- Ajustes del lápiz y tableta (colapsable dentro de Menú ⋯) ---
    auto* penCfgToggle = new QToolButton(m_filePopover);
    penCfgToggle->setObjectName("finilyMoreToggle");
    penCfgToggle->setText("Ajustes de lápiz y tableta  ▾");
    penCfgToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    penCfgToggle->setCursor(Qt::PointingHandCursor);
    penCfgToggle->setFocusPolicy(Qt::NoFocus);
    penCfgToggle->setFixedHeight(28);
    column->addWidget(penCfgToggle);
    auto* penCfgBox = new QWidget(m_filePopover);
    penCfgBox->setVisible(false);
    auto* pressureColumn = new QVBoxLayout(penCfgBox);
    pressureColumn->setContentsMargins(0, 4, 0, 4);
    pressureColumn->setSpacing(6);
    connect(penCfgToggle, &QToolButton::clicked, this, [this, penCfgBox, penCfgToggle] {
        const bool show = !penCfgBox->isVisible();
        penCfgBox->setVisible(show);
        penCfgToggle->setText(show ? "Ajustes de lápiz y tableta  ▴" : "Ajustes de lápiz y tableta  ▾");
        m_filePopover->adjustSize();
        updateOverlayMask();
    });
    auto* pressureRow = new QHBoxLayout();
    pressureRow->setSpacing(4);
    const QVector<QString> pressureNames = {"Ligera", "Normal", "Firme"};
    const QVector<PressureProfile> profiles = {PressureProfile::Light, PressureProfile::Normal, PressureProfile::Firm};
    for (int i = 0; i < profiles.size(); ++i) {
        auto* button = new QToolButton(penCfgBox);
        button->setFixedSize(86, 28);
        button->setText(pressureNames.at(i));
        button->setCheckable(true);
        polishButton(button);
        const PressureProfile profile = profiles.at(i);
        connect(button, &QToolButton::clicked, this, [this, profile] {
            if (m_canvas) m_canvas->setPressureProfile(profile);
            refreshFromCanvas();
        });
        m_pressureBtns.append(button);
        m_pressureSpecs.append(profile);
        pressureRow->addWidget(button);
    }
    pressureColumn->addLayout(pressureRow);

    auto* swapBtn = new SwitchButton(penCfgBox);
    swapBtn->setFixedSize(272, 30);
    swapBtn->setText("Invertir botones del lápiz");
    swapBtn->setCheckable(true);
    swapBtn->setChecked(m_canvas && m_canvas->swapPenButtons());
    connect(swapBtn, &QToolButton::clicked, this, [this, swapBtn] {
        if (m_canvas) m_canvas->setSwapPenButtons(swapBtn->isChecked());
    });
    pressureColumn->addWidget(swapBtn);

    m_mathBeautifyBtn = new SwitchButton(penCfgBox);
    m_mathBeautifyBtn->setFixedSize(272, 30);
    m_mathBeautifyBtn->setText("Modo ecuación (M)");
    m_mathBeautifyBtn->setCheckable(true);
    m_mathBeautifyBtn->setChecked(m_canvas && m_canvas->mathBeautifyEnabled());
    connect(m_mathBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathBeautifyBtn) m_canvas->setMathBeautifyEnabled(m_mathBeautifyBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_mathBeautifyBtn);

    m_holdToShapeBtn = new SwitchButton(penCfgBox);
    m_holdToShapeBtn->setFixedSize(272, 30);
    m_holdToShapeBtn->setText("Auto-forma al mantener");
    m_holdToShapeBtn->setCheckable(true);
    m_holdToShapeBtn->setChecked(m_canvas ? m_canvas->holdToShapeEnabled() : true);
    connect(m_holdToShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_holdToShapeBtn) m_canvas->setHoldToShapeEnabled(m_holdToShapeBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_holdToShapeBtn);

    m_smartGuidesBtn = new SwitchButton(penCfgBox);
    m_smartGuidesBtn->setFixedSize(272, 30);
    m_smartGuidesBtn->setText("Guías inteligentes e imán");
    m_smartGuidesBtn->setCheckable(true);
    m_smartGuidesBtn->setChecked(m_canvas ? m_canvas->smartGuidesEnabled() : true);
    connect(m_smartGuidesBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_smartGuidesBtn) m_canvas->setSmartGuidesEnabled(m_smartGuidesBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_smartGuidesBtn);

    auto* opColorRow = new QHBoxLayout();
    opColorRow->setSpacing(6);
    m_autoColorOpsBtn = new SwitchButton(penCfgBox);
    m_autoColorOpsBtn->setFixedSize(238, 30);
    m_autoColorOpsBtn->setText("Colorear signos (+ − = √)");
    m_autoColorOpsBtn->setCheckable(true);
    m_autoColorOpsBtn->setChecked(m_canvas ? m_canvas->autoColorOperators() : true);
    connect(m_autoColorOpsBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_autoColorOpsBtn) m_canvas->setAutoColorOperators(m_autoColorOpsBtn->isChecked());
        refreshFromCanvas();
    });
    opColorRow->addWidget(m_autoColorOpsBtn);

    m_opColorBtn = new ColorDotButton(penCfgBox);
    m_opColorBtn->setCheckable(false);
    m_opColorBtn->setDotColor(m_canvas ? m_canvas->operatorColor() : QColor("#ef4444"));
    polishButton(m_opColorBtn);
    connect(m_opColorBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas) return;
        const QColor chosen = QColorDialog::getColor(m_canvas->operatorColor(), window(),
                                                     QStringLiteral("Color para signos matemáticos"));
        if (chosen.isValid()) { m_canvas->setOperatorColor(chosen); refreshFromCanvas(); }
    });
    opColorRow->addWidget(m_opColorBtn);
    pressureColumn->addLayout(opColorRow);

    auto* cursorLbl = new QLabel("Puntero de lápiz", penCfgBox);
    cursorLbl->setObjectName("finilySectionTitle");
    pressureColumn->addWidget(cursorLbl);
    auto* cursorRow = new QHBoxLayout();
    cursorRow->setSpacing(6);
    const QStringList cursorNames = {QStringLiteral("Lapicito"), QStringLiteral("Aro"), QStringLiteral("Cruz")};
    for (int i = 0; i < cursorNames.size(); ++i) {
        auto* btn = new QToolButton(penCfgBox);
        btn->setFixedSize(86, 26);
        btn->setText(cursorNames[i]);
        btn->setCheckable(true);
        polishButton(btn);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            if (m_canvas) m_canvas->setCursorStyle(i);
            refreshFromCanvas();
        });
        m_cursorBtns.push_back(btn);
        cursorRow->addWidget(btn);
    }
    pressureColumn->addLayout(cursorRow);
    column->addWidget(penCfgBox);
    column->addStretch(1);
}

void FloatingToolbar::rememberRecentImage(const QString& path) {
    if (path.isEmpty())
        return;
    QSettings settings("Finily", "Finily");
    QStringList recent = settings.value("files/recentImages").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 4)
        recent.removeLast();
    settings.setValue("files/recentImages", recent);
    rebuildRecentGallery();
}

void FloatingToolbar::rebuildRecentGallery() {
    if (!m_recentGalleryWrap || !m_recentGalleryRow)
        return;
    while (QLayoutItem* item = m_recentGalleryRow->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    QSettings settings("Finily", "Finily");
    const QStringList raw = settings.value("files/recentImages").toStringList();
    QStringList valid;
    for (const QString& p : raw) {
        if (QFileInfo::exists(p))
            valid.push_back(p);
        if (valid.size() >= 4)
            break;
    }
    if (valid.isEmpty()) {
        m_recentGalleryWrap->hide();
        return;
    }

    m_recentGalleryWrap->show();
    for (const QString& path : valid) {
        auto* btn = new QToolButton(m_recentGalleryWrap);
        btn->setFixedSize(62, 44);
        btn->setIconSize(QSize(56, 38));
        btn->setToolTip(QStringLiteral("Insertar: ") + QFileInfo(path).fileName());
        polishButton(btn);
        QPixmap px(path);
        if (!px.isNull())
            btn->setIcon(QIcon(px.scaled(56, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        else
            btn->setText(QFileInfo(path).fileName().left(6));
        connect(btn, &QToolButton::clicked, this, [this, path] {
            closePopovers();
            if (m_canvas && QFileInfo::exists(path)) {
                m_canvas->importImageFile(path);
                rememberRecentImage(path);
                refreshFromCanvas();
            }
        });
        m_recentGalleryRow->addWidget(btn);
    }
    m_recentGalleryRow->addStretch(1);
}

void FloatingToolbar::importPdfDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    const QString path = QFileDialog::getOpenFileName(window(), "Importar PDF", QString(), "Archivos PDF (*.pdf)");
    if (path.isEmpty())
        return;
    m_canvas->importPdfFile(path);
    refreshFromCanvas();
}

void FloatingToolbar::exportPdfDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), "Exportar clase a PDF", "finily-clase.pdf",
                                                "Archivos PDF (*.pdf)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    m_canvas->exportToPdf(path);
    refreshFromCanvas();
}

void FloatingToolbar::newDocumentAction() {
    if (!m_canvas)
        return;
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("crear un cuaderno nuevo")))
        return;
    m_canvas->newDocument();
    refreshFromCanvas();
}

void FloatingToolbar::openNotebookDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    if (!confirmDiscardChanges(QStringLiteral("abrir otro cuaderno")))
        return;
    const QString path = QFileDialog::getOpenFileName(window(), "Abrir cuaderno", QString(),
                                                      "Cuaderno Finily (*.finily)");
    if (path.isEmpty())
        return;
    if (!m_canvas->loadFromFile(path))
        showToast(tr("No se pudo abrir el cuaderno"));
    refreshFromCanvas();
}

bool FloatingToolbar::confirmDiscardChanges(const QString& action) {
    if (!m_canvas || !m_canvas->hasUnsavedChanges())
        return true;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Cambios sin guardar"),
                    QStringLiteral("El cuaderno actual tiene cambios sin guardar.\n"
                                   "¿Quieres guardarlos antes de %1?").arg(action),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, window());
    box.setDefaultButton(QMessageBox::Save);
    box.button(QMessageBox::Save)->setText(QStringLiteral("Guardar"));
    box.button(QMessageBox::Discard)->setText(QStringLiteral("No guardar"));
    box.button(QMessageBox::Cancel)->setText(QStringLiteral("Cancelar"));
    switch (box.exec()) {
        case QMessageBox::Save:
            saveNotebookAction();
            return !m_canvas->hasUnsavedChanges(); // si canceló el diálogo, no seguimos
        case QMessageBox::Discard:
            return true;
        default:
            return false;
    }
}

void FloatingToolbar::saveNotebookAction() {
    if (!m_canvas)
        return;
    closePopovers();
    const QString current = m_canvas->currentFilePath();
    if (current.isEmpty()) {
        saveNotebookAsDialog(); // sin archivo asociado: pide destino
        return;
    }
    m_canvas->saveToFile(current);
    refreshFromCanvas();
}

void FloatingToolbar::saveNotebookAsDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    QString path = QFileDialog::getSaveFileName(window(), "Guardar cuaderno", "cuaderno.finily",
                                                "Cuaderno Finily (*.finily)", nullptr,
                                                QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    if (!path.endsWith(".finily", Qt::CaseInsensitive))
        path += ".finily"; // auto-anexa la extensión del cuaderno
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        QMessageBox confirmation(QMessageBox::Question, tr("Reemplazar cuaderno"),
                                 tr("¿Quieres reemplazar «%1»?").arg(QFileInfo(path).fileName()),
                                 QMessageBox::Save | QMessageBox::Cancel, window());
        confirmation.button(QMessageBox::Save)->setText(tr("Reemplazar"));
        confirmation.button(QMessageBox::Cancel)->setText(tr("Cancelar"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Save)
            return;
    }
    m_canvas->saveToFile(path, !exists);
    refreshFromCanvas();
}

QString FloatingToolbar::buildStylesheet(const ThemeConfig& theme) const {
    const QString surface = rgba(theme.uiSurface);
    const QString border = rgba(theme.uiBorder, 55);
    const QString borderElev = rgba(theme.isDark ? QColor(255, 255, 255) : QColor(15, 23, 42),
                                    theme.isDark ? 36 : 42);
    const QString text = rgba(theme.uiText);
    const QString muted = rgba(theme.uiMuted);
    const QString hover = rgba(theme.uiText, 26);
    const QString pressed = rgba(theme.uiText, 40);
    const QString checkedBg = rgba(theme.accent, 46);
    const QString checkedBgSoft = rgba(theme.accent, 24);
    const QString groove = rgba(theme.uiText, 55);
    const QString accent = rgba(theme.accent);
    const QString subPage = rgba(theme.accent, 120);
    const QString accentSoft = rgba(theme.accent, 70);
    const QString divider = rgba(theme.uiBorder, 80);

    return QString(R"(
/* Las superficies (pills y popovers) son GlassCapsule pintadas a mano: sin
   reglas QSS de fondo para que el estilo de sistema (Caelestia/KDE) jamas
   las sobreescriba con cuadrados gris carbon. */
#finilyRestore {
    background: %1;
    border: 1px solid %2;
    border-radius: 13px;
}
#finilySep { background: %14; }
#finilyDivider {
    background: %14;
    min-height: 1px;
    max-height: 1px;
    border: none;
    margin: 3px 2px;
}
QLabel { color: %3; background: transparent; }
QLabel#finilyTitle {
    color: %4;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1px;
}
QLabel#finilyToast { font-size: 12px; font-weight: 600; }
QLabel#finilyPageLabel { font-size: 12px; font-weight: 600; }
QLabel#finilyShortcut { color: %4; font-size: 11px; }
QLabel#finilyValue { font-size: 12px; font-weight: 600; }
QToolButton#finilyMenuRow { border-radius: 9px; font-size: 13px; }
QToolButton#finilyMenuRow QLabel { font-size: 13px; }
QToolButton#finilyMenuRow QLabel#finilyShortcut { font-size: 11px; }
QToolButton#finilyShapeBtn { font-size: 11px; padding: 0px 5px; border-radius: 10px; }
QToolButton#finilyShapeBtn:hover { background: %5; }
QToolButton#finilyShapeBtn:checked { background: %6; border: 1px solid %9; }
QToolButton#finilyAccentBtn { background: %11; font-weight: 600; border: 1px solid %12; }
QToolButton, QPushButton {
    background: transparent;
    border: none;
    border-radius: 11px;
    color: %3;
    padding: 2px;
    font-size: 12px;
}
QToolButton:hover, QPushButton:hover { background: %5; }
QToolButton:pressed, QPushButton:pressed { background: %6; }
QToolButton:checked, QPushButton:checked {
    background: %7;
    border: 1px solid %13;
}
QToolButton#finilyMoreToggle { color: %4; font-size: 11px; font-weight: 600; text-align: left; border-radius: 8px; padding: 0 4px; }
QToolButton#finilyMoreToggle:hover { color: %3; background: transparent; }
QToolButton:hover:checked, QPushButton:hover:checked { background: %13; }
QToolButton:disabled { color: %4; }
/* Herramientas del dock (útiles ilustrados + duotono): cápsula activa refinada
   con tinte suave de acento y borde fino para no tapar jamás el arte SVG. */
QToolButton#finilyWritingTool, QToolButton#finilyTool {
    border-radius: 12px;
    border: 1.5px solid transparent;
    padding: 0px;
}
QToolButton#finilyWritingTool:hover, QToolButton#finilyTool:hover {
    background: %5;
}
QToolButton#finilyWritingTool:checked, QToolButton#finilyTool:checked {
    background: %7;
    border: 1.5px solid transparent;
}
QToolButton#finilyWritingTool:hover:checked, QToolButton#finilyTool:hover:checked {
    background: %11;
    border: 1.5px solid transparent;
}
QToolButton#finilyZoomReset { font-size: 11px; padding: 0px 8px; border-radius: 14px; }
QSlider { background: transparent; min-height: 22px; }
QSlider::groove:horizontal { border: none; height: 4px; border-radius: 2px; background: %8; }
QSlider::sub-page:horizontal { background: %10; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0px; border-radius: 7px; background: %9; }
QSlider::handle:horizontal:hover { background: %3; }
QSlider::handle:horizontal:disabled { background: %4; }
)")
        .arg(surface)
        .arg(border)
        .arg(text)
        .arg(muted)
        .arg(hover)
        .arg(pressed)
        .arg(checkedBg)
        .arg(groove)
        .arg(accent)
        .arg(subPage)
        .arg(accentSoft)
        .arg(borderElev)
        .arg(checkedBgSoft)
        .arg(divider);
}

void FloatingToolbar::applyThemeStylesheet() {
    const ThemeConfig theme = getThemeConfig(m_theme);

    // La primera tinta se adapta automáticamente al tema: tiza (oscuro) / tinta
    // (claro). Si el usuario restauró presets personalizados desde QSettings,
    // se respeta su color guardado en lugar de sobrescribirlo.
    if (!m_presetsRestored) {
        m_presets[0].color = getPaletteForTheme(theme.isDark).first();
        m_presets[0].label = theme.isDark ? "Tiza principal" : "Tinta principal";
    }
    for (PenPreset& preset : m_presets)
        if (preset.tool != ToolType::Highlighter)
            preset.color = adaptInkToTheme(preset.color, theme.isDark);

    const QString qss = buildStylesheet(theme);
    setStyleSheet(qss);
    if (m_strokePopover)
        m_strokePopover->setStyleSheet(qss);
    if (m_paperPopover)
        m_paperPopover->setStyleSheet(qss);
    if (m_shapesPopover)
        m_shapesPopover->setStyleSheet(qss);
    if (m_filePopover)
        m_filePopover->setStyleSheet(qss);

    refreshIcons();
    updateStationery();

    // Recolorea TODAS las cápsulas de cristal al instante: DarkBoard <-> 
    // PaperLight cambia cada superficie de vidrio oscuro a blanco luminoso.
    // Cada cápsula recibe la hoja de estilo directamente: si solo se pone en
    // la barra, los hijos ya pulidos se quedan con el color del tema anterior
    // (texto blanco sobre papel blanco).
    for (GlassCapsule* capsule : m_capsules)
        if (capsule) {
            capsule->setCapsuleTheme(theme);
            capsule->setStyleSheet(qss);
        }

    refreshShadowColors();
    disarmClearButton();
}

void FloatingToolbar::refreshIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor ink = m_canvas ? m_canvas->inkColor() : theme.accent;

    // 1. Barra superior de utilidades (SVG homologado 22px)
    for (const auto& spec : m_studioUtilSpecs)
        spec.first->setIcon(makeStudioIcon(spec.second, 22, theme.uiText, theme.accent, ink, theme.isDark));

    // 2. 4 útiles ilustrados del dock (SVG 32px con tinta activa)

    for (QToolButton* b : findChildren<QToolButton*>("finilySwitch"))
        if (auto* sw = dynamic_cast<SwitchButton*>(b))
            sw->setColors(theme.uiText, theme.accent, theme.isDark);
    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QString("color:%1;font-size:13px;font-weight:600;background:transparent;")
                                        .arg(theme.uiText.name()));
    if (m_titleDot)
        m_titleDot->setStyleSheet(QString("background:%1;border-radius:3px;").arg(theme.accent.name()));

    // 3. 6 herramientas duotono del dock (SVG 24px)
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryIcons.size(); ++i)
        m_secondaryBtns.at(i)->setIcon(dockIcon(m_secondaryIcons.at(i), theme.uiText, theme.accent));

    m_lastInkIconSig = QString("%1|%2").arg(int(m_theme)).arg(ink.name(QColor::HexArgb));

    // 4. Mini-iconos auxiliares (zoom, páginas, popovers de figuras/patrones/plantillas)
    static const QStringList dockKinds = {"pen", "highlighter", "eraser", "paint-bucket"};
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(dockKinds.contains(spec.second) ? dockIcon(spec.second, theme.uiText, theme.accent)
                                                            : glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_ctxIconSpecs)
        spec.first->setIcon(QIcon(contextIconPixmap(spec.second, theme.uiText)));
    if (m_stabilizerIcon)
        m_stabilizerIcon->setPixmap(contextIconPixmap(QStringLiteral("stabilizer"), theme.uiText));
    for (QSlider* sl : {m_sizeSlider, m_stabilizerSlider, m_eraserSizeSlider})
        if (auto* ds = dynamic_cast<DotStepSlider*>(sl))
            ds->setColors(theme.accent, theme.isDark);
    for (const auto& spec : m_patternSpecs)
        spec.first->setIcon(QIcon(patternPixmap(spec.second, theme.uiText)));
    for (const auto& spec : m_templateSpecs)
        spec.first->setIcon(QIcon(templatePixmap(spec.second, theme.uiText)));
    updateFillSwatches();
    updateTipIcons();
}

void FloatingToolbar::updateFillSwatches() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor col = m_canvas ? m_canvas->inkColor() : theme.accent;
    for (const auto& [button, pattern] : m_fillStyles)
        button->setIcon(QIcon(fillSwatchPixmap(pattern, col, theme.isDark)));
}

void FloatingToolbar::updateTipIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor col = m_canvas ? m_canvas->inkColor() : theme.accent;
    for (int i = 0; i < m_penTipBtns.size(); ++i)
        m_penTipBtns.at(i)->setIcon(QIcon(penTipPixmap(i, col, theme.isDark)));
    for (int i = 0; i < m_hlTipBtns.size(); ++i)
        m_hlTipBtns.at(i)->setIcon(QIcon(hlTipPixmap(i, col, theme.isDark)));
}

void FloatingToolbar::positionChildren() {
    // Pill de utilidades: siempre arriba-izquierda (x=14, y=12).
    if (m_pill && !m_pill->isHidden()) {
        m_pill->adjustSize();
        m_pill->move(14, 12);
    }
    if (m_topRight && !m_topRight->isHidden()) {
        m_topRight->adjustSize();
        m_topRight->move(width() - m_topRight->width() - 14, 12);
    }
    if (m_thumbPanel && !m_thumbPanel->isHidden()) {
        const int top = m_topRight ? m_topRight->geometry().bottom() + 10 : 60;
        const int bottom = height() - (m_zoomPill ? m_zoomPill->height() : 40) - 28;
        m_thumbPanel->setGeometry(width() - m_thumbPanel->width() - 14, top, m_thumbPanel->width(),
                                  std::max(160, bottom - top));
    }
    if (m_zoomPill && !m_zoomPill->isHidden()) {
        m_zoomPill->adjustSize();
        m_zoomPill->move(width() - m_zoomPill->width() - 14, height() - m_zoomPill->height() - 14);
    }
    if (m_pagePill && !m_pagePill->isHidden()) {
        m_pagePill->adjustSize();
        const int zx = (m_zoomPill && !m_zoomPill->isHidden()) ? m_zoomPill->x() - 8 : width() - 14;
        m_pagePill->move(zx - m_pagePill->width(), height() - m_pagePill->height() - 14);
    }
    // Dock de papelería: vertical centrado en el borde izquierdo (x=12) o
    // horizontal arriba/abajo, esquivando las pills de esquinas.
    if (m_dock && !m_dock->isHidden()) {
        layoutDock();
        if (m_dockPos == DockPos::Izquierda) {
            m_dock->move(12, std::max(12, (height() - m_dock->height()) / 2));
        } else if (m_dockPos == DockPos::Arriba) {
            const int centeredX = (width() - m_dock->width()) / 2;
            const int minX = m_pill && !m_pill->isHidden() ? m_pill->geometry().right() + 14 : 14;
            m_dock->move(std::max(minX, centeredX), 12);
        } else {
            int x = (width() - m_dock->width()) / 2;
            if (m_pagePill && !m_pagePill->isHidden())
                x = std::min(x, m_pagePill->geometry().left() - m_dock->width() - 14);
            m_dock->move(std::max(12, x), height() - m_dock->height() - 12);
        }
    }
    if (m_restoreBtn && !m_restoreBtn->isHidden())
        m_restoreBtn->move(width() - 34, 10);
    if (m_contextPill && !m_contextPill->isHidden()) {
        m_contextPill->adjustSize();
        // Fija abajo-centro: nunca tapa vértices ni se atraviesa al dibujar o editar.
        const int x = std::max(76, (width() - m_contextPill->width()) / 2);
        const int y = std::max(12, height() - m_contextPill->height() - 16);
        m_contextPill->move(x, y);
    }
    if (m_toastPill && !m_toastPill->isHidden()) {
        m_toastPill->adjustSize();
        // Dock abajo: toast arriba-centrado; en otro caso, abajo del todo
        // (el dock izquierdo queda centrado y no tropieza con el toast).
        const int y = m_dockPos == DockPos::Abajo ? 18 : height() - m_toastPill->height() - 64;
        m_toastPill->move(std::max(6, (width() - m_toastPill->width()) / 2), y);
    }
    positionOpenPopover();
    updateOverlayMask();
}

// Máscara de entrada del overlay: únicamente los rectángulos de los hijos
// visibles capturan eventos (pill, pill de zoom, botón de restauración y
// popovers abiertos); todo lo demás pasa al CanvasWidget debajo, cuyo
// eventFilter cierra cualquier popover abierto.
void FloatingToolbar::updateOverlayMask() {
    QRegion region;
    const QVector<QWidget*> overlayChildren = {m_pill,          m_topRight,     m_thumbPanel,   m_contextPill, m_zoomPill,   m_restoreBtn,
                                               m_strokePopover, m_paperPopover, m_shapesPopover, m_filePopover,
                                               m_pagePill,      m_toastPill,    m_dock};
    for (QWidget* child : overlayChildren) {
        if (!child || child->isHidden())
            continue;
        // Margen estrecho (-8,-6,+8,+12): deja respirar la sombra suave sin
        // robar clics de dibujo lejos del cromo (el hit-test queda pegado a la
        // pill; solo el halo de la sombra se expande).
        region += child->geometry().adjusted(-8, -6, 8, 12);
    }
    setMask(region);
}

// Con lápiz Wacom, los QTabletEvent no manejados por los hijos (botones,
// sliders) suben hasta aquí: al ignorarlos, siguen hacia el contenedor plano
// de MainWindow (nunca CanvasWidget, que es hermano) y quedan sin manejar,
// por lo que Qt sintetiza eventos de mouse nítidos para los controles.
void FloatingToolbar::tabletEvent(QTabletEvent* event) {
    event->ignore();
}

QPoint FloatingToolbar::popoverPosition(const GlassCapsule* popover, const QWidget* anchor) const {
    const int margin = 12;
    // Los popovers abren LIMPIOS junto al cromo: a la derecha del dock
    // vertical (bajo la pill de utilidades), debajo del dock acoplado arriba,
    // o bajo la pill en cualquier otro caso; nunca tapados ni recortados.
    int x, y;
    if (m_topRight && anchor->parentWidget() == m_topRight) {
        x = m_topRight->geometry().right() - popover->width();
        y = m_topRight->geometry().bottom() + 10;
    } else if (m_dockPos == DockPos::Izquierda && m_dock && !m_dock->isHidden()) {
        x = m_dock->geometry().right() + 10;
        if (anchor->parentWidget() == m_dock) {
            // Al abrir desde un útil o Figuras del dock, alinear verticalmente junto a ese botón
            y = m_dock->y() + anchor->y() - 12;
        } else {
            y = (m_pill && !m_pill->isHidden() ? m_pill->geometry().bottom() : 40) + 10;
        }
    } else if (m_dockPos == DockPos::Arriba && m_dock && !m_dock->isHidden()) {
        x = margin;
        y = m_dock->geometry().bottom() + 10;
    } else {
        x = margin;
        y = (m_pill && !m_pill->isHidden() ? m_pill->geometry().bottom() : 40) + 10;
    }
    if (m_dockPos == DockPos::Abajo && m_dock && !m_dock->isHidden())
        // Con el dock abajo, no invadir el dock: ciñe el popover por encima.
        y = std::min(y, std::max(margin, m_dock->geometry().top() - popover->height() - 10));
    x = std::clamp(x, margin, std::max(margin, width() - popover->width() - margin));
    y = std::clamp(y, margin, std::max(margin, height() - popover->height() - margin));
    return QPoint(x, y);
}

void FloatingToolbar::positionOpenPopover() {
    if (!m_openPopover)
        return;
    m_openPopover->adjustSize();
    const QPoint target = popoverPosition(m_openPopover, m_popoverAnchor);
    if (m_popoverAnim && m_popoverAnim->state() == QAbstractAnimation::Running) {
        if (m_popoverAnim->endValue().toPoint() == target)
            return;
        m_popoverAnim->stop();
    }
    m_openPopover->move(target);
}

// Abre el panel junto a su botón en las coordenadas del overlay.
void FloatingToolbar::openPopover(GlassCapsule* popover, QWidget* anchor) {
    if (!popover || !anchor)
        return;
    const bool wasOpen = m_openPopover == popover;
    closePopovers();
    if (wasOpen)
        return;
    m_openPopover = popover;
    m_popoverAnchor = anchor;

    // Al mostrar el panel, Qt vuelve a medir las secciones que cambiaron mientras estaba oculto.
    popover->show();
    popover->adjustSize();
    const QPoint finalPos = popoverPosition(popover, anchor);
    // Desliza 8 px hacia su lugar final (desde el cromo que lo ancla).
    QPoint offset(0, 8);
    if ((m_topRight && anchor->parentWidget() == m_topRight) || m_dockPos == DockPos::Abajo)
        offset = QPoint(0, -8);
    else if (m_dockPos == DockPos::Izquierda)
        offset = QPoint(-8, 0);
    const QPoint startPos = finalPos + offset;
    popover->move(startPos);
    popover->raise();
    updateOverlayMask();

    m_popoverAnim = new QPropertyAnimation(popover, "pos", this);
    m_popoverAnim->setDuration(140);
    m_popoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_popoverAnim->setStartValue(startPos);
    m_popoverAnim->setEndValue(finalPos);
    connect(m_popoverAnim, &QPropertyAnimation::valueChanged, this,
            [this](const QVariant&) { updateOverlayMask(); });
    m_popoverAnim->start();
}

void FloatingToolbar::closePopovers() {
    if (m_openPopover) {
        if (m_popoverAnim) {
            m_popoverAnim->stop();
            m_popoverAnim->deleteLater();
            m_popoverAnim = nullptr;
        }
        m_openPopover->close();
        m_openPopover = nullptr;
        m_popoverAnchor = nullptr;
        updateOverlayMask();
    }
}

// Toast flotante estilo StarNote: cápsula de cristal centrada (abajo, o arriba
// si la barra está acoplada abajo) con fundido de entrada 140 ms, hold de
// 1.2 s y salida con fundido 220 ms. La QGraphicsOpacityEffect vive en la
// cápsula y es su único efecto, así no pisa ninguna sombra.
void FloatingToolbar::showToast(const QString& message) {
    if (!m_toastPill || !m_toastLabel)
        return;
    if (m_toastAnim) {
        m_toastAnim->stop();
        m_toastAnim->deleteLater();
    }
    if (m_toastTimer)
        m_toastTimer->stop();
    m_toastLabel->setText(message);
    m_toastPill->adjustSize();
    m_toastPill->show();
    m_toastPill->raise();
    positionChildren();

    auto* opacity = qobject_cast<QGraphicsOpacityEffect*>(m_toastPill->graphicsEffect());
    if (!opacity) {
        opacity = new QGraphicsOpacityEffect(m_toastPill);
        m_toastPill->setGraphicsEffect(opacity);
    }
    opacity->setOpacity(0.0);
    m_toastAnim = new QPropertyAnimation(opacity, "opacity", this);
    m_toastAnim->setDuration(140);
    m_toastAnim->setStartValue(0.0);
    m_toastAnim->setEndValue(1.0);
    m_toastAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_toastAnim->start();
    if (m_toastTimer)
        m_toastTimer->start(1200);
}

// Repinta el punto de vista previa con la tinta y el grosor reales del lienzo.
void FloatingToolbar::updateStrokePreview() {
    if (m_canvas && m_strokePreview)
        m_strokePreview->setInk(m_canvas->inkColor(), m_canvas->strokeSize());
}

// Sombra suave real: blur 22, yOffset 6, alfa 70 (tema oscuro) / 35 (claro).
void FloatingToolbar::applyShadow(QWidget* widget) {
    if (!widget || widget->graphicsEffect())
        return;
    auto* effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(28);
    effect->setOffset(0.0, 8.0);
    effect->setColor(QColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 90 : 30));
    widget->setGraphicsEffect(effect);
}

// Recolorea las sombras al cambiar de tema sin recrear efectos.
void FloatingToolbar::refreshShadowColors() {
    const QColor shadowColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 90 : 30);
    const QVector<QWidget*> elevated = {m_pill,          m_contextPill,   m_zoomPill,      m_pagePill,
                                        m_strokePopover, m_paperPopover,   m_shapesPopover, m_filePopover,
                                        m_dock};
    for (QWidget* widget : elevated) {
        if (!widget)
            continue;
        if (auto* effect = qobject_cast<QGraphicsDropShadowEffect*>(widget->graphicsEffect()))
            effect->setColor(shadowColor);
    }
}

// Restaura la posición acoplable persistida ("toolbar/dockPos": left/top/
// bottom, con migración desde el antiguo "toolbar/dockBottom" booleano).
void FloatingToolbar::loadDockPosition() {
    QSettings settings("Finily", "Finily");
    const QString pos = settings.value("toolbar/trayPos").toString();
    if (pos == QLatin1String("top"))
        m_dockPos = DockPos::Arriba;
    else if (pos == QLatin1String("bottom"))
        m_dockPos = DockPos::Abajo;
    else if (pos == QLatin1String("left"))
        m_dockPos = DockPos::Izquierda;
    else // por defecto a la izquierda (pensado para zurdos: la mano no tapa la barra)
        m_dockPos = DockPos::Izquierda;
}

// Alterna "Posición de barra" (Izquierda/Arriba/Abajo): reorienta el dock,
// reancla pills, popovers y toast; persiste y reabre el popover activo.
void FloatingToolbar::setDockPosition(DockPos pos) {
    if (m_dockPos == pos)
        return;
    m_dockPos = pos;
    QSettings settings("Finily", "Finily");
    settings.setValue("toolbar/trayPos", pos == DockPos::Izquierda ? "left"
                                         : pos == DockPos::Arriba  ? "top"
                                                                   : "bottom");

    GlassCapsule* open = m_openPopover;
    QWidget* anchor = m_popoverAnchor;
    closePopovers();
    positionChildren();
    refreshFromCanvas(); // sincroniza los botones Izquierda/Arriba/Abajo
    if (open)
        openPopover(open, anchor);
}

void FloatingToolbar::toggleCleanUi() {
    m_cleanUi = !m_cleanUi;
    // Corta cualquier animación previa de la pill (sin disparar su lambda de
    // cierre para no esconder controles que se van a restaurar).
    if (m_pillAnim) {
        QObject::disconnect(m_pillAnim, &QPropertyAnimation::finished, this, nullptr);
        m_pillAnim->stop();
        m_pillAnim->deleteLater();
        m_pillAnim = nullptr;
    }
    if (m_cleanUi) {
        closePopovers();
        if (m_zoomPill)
            m_zoomPill->hide();
        if (m_contextPill)
            m_contextPill->hide();
        if (m_pagePill)
            m_pagePill->hide();
        if (m_topRight)
            m_topRight->hide();
        if (m_thumbPanel)
            m_thumbPanel->hide();
        if (m_dock)
            m_dock->hide();
        if (m_restoreBtn)
            m_restoreBtn->show();
        positionChildren();
        // Desliza la pill 18 px hacia arriba (150 ms OutCubic) y la esconde.
        if (m_pill && m_pill->isVisible()) {
            const QPoint start = m_pill->pos();
            m_pillAnim = new QPropertyAnimation(m_pill, "pos", this);
            m_pillAnim->setDuration(150);
            m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_pillAnim->setStartValue(start);
            m_pillAnim->setEndValue(QPoint(start.x(), start.y() - 18));
            connect(m_pillAnim, &QPropertyAnimation::valueChanged, this,
                    [this](const QVariant&) { updateOverlayMask(); });
            connect(m_pillAnim, &QPropertyAnimation::finished, this, [this] {
                if (m_pill)
                    m_pill->hide();
                positionChildren();
            });
            m_pillAnim->start();
        }
    } else {
        if (m_restoreBtn)
            m_restoreBtn->hide();
        if (m_pill)
            m_pill->show();
        if (m_zoomPill)
            m_zoomPill->show();
        if (m_pagePill)
            m_pagePill->show();
        if (m_topRight)
            m_topRight->show();
        if (m_thumbPanel && m_pagesBtn && m_pagesBtn->isChecked()) {
            m_thumbPanel->show();
            rebuildThumbs();
        }
        if (m_dock)
            m_dock->show();
        positionChildren();
        // La pill entra deslizándose desde arriba (150 ms OutCubic).
        if (m_pill) {
            const QPoint target = m_pill->pos();
            m_pillAnim = new QPropertyAnimation(m_pill, "pos", this);
            m_pillAnim->setDuration(150);
            m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_pillAnim->setStartValue(QPoint(target.x(), target.y() - 16));
            m_pillAnim->setEndValue(target);
            connect(m_pillAnim, &QPropertyAnimation::valueChanged, this,
                    [this](const QVariant&) { updateOverlayMask(); });
            m_pillAnim->start();
        }
    }
    updateOverlayMask();
}

void FloatingToolbar::selectTool(ToolType tool) {
    if (!m_canvas)
        return;
    // Seleccionar una herramienta distinta libera el preset activo.
    if (m_activePreset >= 0 && m_activePreset < m_presets.size() && m_presets.at(m_activePreset).tool != tool)
        m_activePreset = -1;
    m_canvas->setTool(tool);
    refreshFromCanvas();
}

void FloatingToolbar::selectPreset(int index) {
    if (index < 0 || index >= m_presets.size() || !m_canvas)
        return;
    m_activePreset = index;
    const PenPreset& preset = m_presets[index];
    m_canvas->setTool(preset.tool);
    m_canvas->setInkColor(preset.color);
    m_canvas->setStrokeSize(preset.size);
    m_canvas->setLinePattern(preset.linePattern);
    refreshFromCanvas();
}

void FloatingToolbar::setColor(const QColor& color) {
    if (!m_canvas || !color.isValid())
        return;
    m_canvas->setInkColor(color);
    syncActivePresetFromCanvas();
    updateStationery();
    refreshFromCanvas();
}

void FloatingToolbar::syncActivePresetFromCanvas() {
    if (!m_canvas || m_activePreset < 0 || m_activePreset >= m_presets.size())
        return;
    if (m_canvas->tool() == ToolType::Eraser) // el borrador no tiene preset de tinta
        return;
    PenPreset& preset = m_presets[m_activePreset];
    preset.tool = m_canvas->tool();
    preset.color = m_canvas->inkColor();
    preset.size = m_canvas->strokeSize();
    preset.linePattern = m_canvas->linePattern();
    // Persistir los presets favoritos solo cuando realmente cambian.
    const QString sig = QString("%1|%2|%3|%4|%5")
                            .arg(m_activePreset)
                            .arg(int(preset.tool))
                            .arg(preset.color.name(QColor::HexArgb))
                            .arg(preset.size)
                            .arg(int(preset.linePattern));
    if (sig != m_lastPresetsSig) {
        m_lastPresetsSig = sig;
        savePresets();
    }
}

void FloatingToolbar::updateStationery() {
    // Actualiza el color de tinta en los 4 útiles ilustrados SVG + el botón
    // "Trazo" de la pill superior (solo si el color o tema realmente cambió).
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor ink = m_canvas ? m_canvas->inkColor() : theme.accent;
    const QString sig = QString("%1|%2").arg(int(m_theme)).arg(ink.name(QColor::HexArgb));
    // Los lápices del estuche se repintan solo si su tinta cambia (setInk es barato).
    for (QToolButton* button : m_writingBtns)
        if (auto* pen = dynamic_cast<PenTrayButton*>(button)) {
            pen->setInk(ink);
            pen->setDarkTheme(theme.isDark);
        }
    if (sig != m_lastInkIconSig) {
        m_lastInkIconSig = sig;
    }

    // Muestras de la paleta del popover "Trazo" (ColorDotButton antialias):
    // punto de color real + rim sutil de 1px que contrasta con el tema.
    const QColor rim = theme.isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 80);
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i) {
        ColorDotButton* swatch = m_swatches.at(i);
        swatch->setDotColor(m_palette.at(i));
        swatch->setAccentColor(theme.accent);
        swatch->setRimColor(rim);
        swatch->setToolTip(m_palette.at(i).name());
    }
    if (m_trayColorBtn && m_canvas) {
        m_trayColorBtn->setDotColor(m_canvas->inkColor());
        m_trayColorBtn->setAccentColor(theme.accent);
        m_trayColorBtn->setRimColor(rim);
    }
    for (int i = 0; i < m_trayDots.size(); ++i) {
        ColorDotButton* dot = m_trayDots.at(i);
        if (i >= m_palette.size()) {
            dot->hide();
            continue;
        }
        dot->setDotColor(m_palette.at(i));
        dot->setAccentColor(theme.accent);
        dot->setRimColor(rim);
        dot->show();
    }
}

void FloatingToolbar::updateZoomLabel() {
    if (!m_canvas || !m_zoomResetBtn)
        return;
    const int percent = qRound(m_canvas->viewport().zoom * 100.0);
    m_zoomResetBtn->setText(QString("%1 %").arg(percent));
}

void FloatingToolbar::onClearClicked() {
    if (!m_clearBtn)
        return;
    if (!m_clearArmed) {
        // Primer paso: armar la confirmación (se desarma sola a los 3 s).
        m_clearArmed = true;
        m_clearBtn->setText("¿Borrar anotaciones? Pulsa de nuevo");
        m_clearBtn->setStyleSheet(
            "QPushButton { background: rgba(239, 68, 68, 70); color: #fecaca; border-radius: 14px; }");
        if (m_clearTimer)
            m_clearTimer->start(3000);
        return;
    }
    disarmClearButton();
    if (m_canvas)
        m_canvas->clearBoard();
    closePopovers();
    refreshFromCanvas();
}

void FloatingToolbar::disarmClearButton() {
    m_clearArmed = false;
    if (m_clearTimer && m_clearTimer->isActive())
        m_clearTimer->stop();
    if (m_clearBtn) {
        m_clearBtn->setText("Borrar anotaciones de esta página");
        m_clearBtn->setStyleSheet(QString());
    }
}

void FloatingToolbar::refreshFromCanvas() {
    if (!m_canvas)
        return;

    // Cambio de tema: rederivar todo el cromo (QSS, iconos, paleta, presets).
    const ThemeId theme = m_canvas->themeId();
    if (theme != m_theme) {
        m_theme = theme;
        m_palette = getPaletteForTheme(getThemeConfig(theme).isDark);
        applyThemeStylesheet();
        if (m_canvas->tool() != ToolType::Highlighter)
            m_canvas->setInkColor(adaptInkToTheme(m_canvas->inkColor(), getThemeConfig(theme).isDark));
    }

    if (m_thumbPanel && !m_thumbPanel->isHidden() && m_thumbTimer) {
        const int active = m_canvas->currentPageIndex();
        if (m_thumbCurrent != active) {
            if (auto* previous = m_thumbList->item(m_thumbCurrent))
                previous->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(m_thumbCurrent, thumbnailSize))));
            m_thumbCurrent = active;
            if (m_thumbList->currentRow() != active)
                m_thumbList->setCurrentRow(active, QItemSelectionModel::ClearAndSelect);
        }
        m_thumbTimer->start();
    }
    syncActivePresetFromCanvas();

    // Toggle "Posición de barra" (estado propio del toolbar, fuera de la
    // huella del lienzo para que siempre quede sincronizado).
    if (m_dockLeftBtn && m_dockTopBtn && m_dockBottomBtn) {
        m_dockLeftBtn->setChecked(m_dockPos == DockPos::Izquierda);
        m_dockTopBtn->setChecked(m_dockPos == DockPos::Arriba);
        m_dockBottomBtn->setChecked(m_dockPos == DockPos::Abajo);
    }
    // Formato de hoja (Infinita / Hoja A4 / Diapo 16:9).
    const PageFormat pageFormat = m_canvas->pageFormat();
    for (int i = 0; i < m_formatBtns.size() && i < m_formatSpecs.size(); ++i)
        m_formatBtns.at(i)->setChecked(m_formatSpecs.at(i) == pageFormat);
    if (m_mathBeautifyBtn)
        m_mathBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_mathModeBtn)
        m_mathModeBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_autoColorOpsBtn)
        m_autoColorOpsBtn->setChecked(m_canvas->autoColorOperators());
    if (m_opColorBtn) {
        const ThemeConfig theme = getThemeConfig(m_theme);
        m_opColorBtn->setDotColor(m_canvas->operatorColor());
        m_opColorBtn->setRimColor(theme.isDark ? QColor(255, 255, 255, 80) : QColor(15, 23, 42, 80));
    }
    const int curStyle = m_canvas->cursorStyle();
    for (int i = 0; i < m_cursorBtns.size(); ++i)
        m_cursorBtns.at(i)->setChecked(i == curStyle);

    // Huella de estado: evita reflashear la UI en cada punto de un trazo a 200 Hz.
    const int pdfPageCount = m_canvas->documentPdfPageCount();
    const QString signature = QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19|%20|%21|%22|%23|%24|%25|%26|%27|%28|%29|%30")
                                  .arg(int(m_canvas->tool()))
                                  .arg(m_canvas->inkColor().name(QColor::HexArgb))
                                  .arg(m_canvas->strokeSize())
                                  .arg(int(m_canvas->linePattern()))
                                  .arg(m_canvas->stabilizer())
                                  .arg(int(m_canvas->eraserMode()))
                                  .arg(m_canvas->eraserSize())
                                  .arg(int(m_canvas->pressureProfile()))
                                  .arg(int(m_canvas->themeId()))
                                  .arg(int(m_canvas->backgroundTemplate()))
                                  .arg(m_canvas->canUndo())
                                  .arg(m_canvas->canRedo())
                                  .arg(m_canvas->viewport().zoom)
                                  .arg(int(m_canvas->shapeKind()))
                                  .arg(m_canvas->holdToShapeEnabled())
                                  .arg(m_canvas->smartGuidesEnabled())
                                  .arg(int(m_canvas->isDrawingPolyline()) * 2 + int(m_canvas->hasSelectedShape()))
                                  .arg(int(m_canvas->hasLassoSelection()) + 2 * int(m_canvas->hasSelectedStrokes())
                                       + 4 * int(m_canvas->canGroupSelection()) + 8 * int(m_canvas->canUngroupSelection()))
                                  .arg(m_canvas->hasSelectedImage())
                                  .arg(m_canvas->hasSelectedText())
                                  .arg(pdfPageCount)
                                  .arg(m_canvas->currentPageIndex())
                                  .arg(m_canvas->pageCount())
                                  .arg(int(m_canvas->lineEnding()))
                                  .arg(m_canvas->polygonSides())
                                  .arg(m_canvas->triangleVariant())
                                  .arg(m_canvas->quadVariant())
                                  .arg(m_canvas->solidKind())
                                  .arg(m_canvas->isActivelyDragging())
                                  .arg(m_canvas->penTip() * 64 + m_canvas->highlighterTip() * 16
                                       + int(m_canvas->lassoRectMode()) * 8 + int(m_canvas->shadePattern()));
    if (signature == m_lastStateSig)
        return;
    m_lastStateSig = signature;

    m_updatingUi = true;

    // Historial
    m_undoBtn->setEnabled(m_canvas->canUndo());
    m_redoBtn->setEnabled(m_canvas->canRedo());

    // Herramientas (10 exclusivas: 4 útiles ilustrados + 6 duotono)
    const ToolType tool = m_canvas->tool();
    for (int i = 0; i < m_writingBtns.size() && i < m_writingKinds.size(); ++i)
        if (m_writingKinds.at(i) == tool || (m_writingKinds.at(i) == ToolType::Pen && tool == ToolType::Ballpoint))
            m_writingBtns.at(i)->setChecked(true);
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryKinds.size(); ++i)
        if (m_secondaryKinds.at(i) == tool)
            m_secondaryBtns.at(i)->setChecked(true);
    updateStationery();

    // Popover "Trazo" (10 niveles discretos + puntas de lápiz/resaltador + cuadritos de relleno)
    const bool isHl = tool == ToolType::Highlighter;
    const bool isPen = tool == ToolType::Pen || tool == ToolType::Ballpoint;
    const int sizeLevel = sizeToLevel(m_canvas->strokeSize(), isHl);
    if (m_sizeSlider->value() != sizeLevel)
        m_sizeSlider->setValue(sizeLevel);
    m_sizeLabel->setText(QString("%1 / 10").arg(sizeLevel));
    updateStrokePreview();
    updateFillSwatches();
    if (m_penTipSection)
        m_penTipSection->setVisible(isPen);
    for (int i = 0; i < m_penTipBtns.size(); ++i)
        m_penTipBtns.at(i)->setChecked(i == m_canvas->penTip());
    if (m_hlTipSection)
        m_hlTipSection->setVisible(isHl);
    for (int i = 0; i < m_hlTipBtns.size(); ++i)
        m_hlTipBtns.at(i)->setChecked(i == m_canvas->highlighterTip());
    const LinePattern pattern = m_canvas->linePattern();
    for (int i = 0; i < m_patternSpecs.size(); ++i) {
        m_patternBtns.at(i)->setChecked(m_patternSpecs.at(i).second == pattern);
        m_patternBtns.at(i)->setEnabled(pattern != LinePattern::Dimension);
    }

    const int stabilizerLevel = qRound(m_canvas->stabilizer() * m_stabilizerSlider->maximum());
    if (m_stabilizerSlider->value() != stabilizerLevel)
        m_stabilizerSlider->setValue(stabilizerLevel);
    m_stabilizerLabel->setText(QString("%1 / 10").arg(stabilizerLevel));

    // Perfil de presión Wacom y embellecedor matemático
    const PressureProfile profile = m_canvas->pressureProfile();
    for (int i = 0; i < m_pressureBtns.size() && i < m_pressureSpecs.size(); ++i)
        m_pressureBtns.at(i)->setChecked(m_pressureSpecs.at(i) == profile);
    if (m_mathBeautifyBtn)
        m_mathBeautifyBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_mathModeBtn)
        m_mathModeBtn->setChecked(m_canvas->mathBeautifyEnabled());
    if (m_autoColorOpsBtn)
        m_autoColorOpsBtn->setChecked(m_canvas->autoColorOperators());
    if (m_opColorBtn) {
        const ThemeConfig theme = getThemeConfig(m_theme);
        m_opColorBtn->setDotColor(m_canvas->operatorColor());
        m_opColorBtn->setRimColor(theme.isDark ? QColor(255, 255, 255, 80) : QColor(15, 23, 42, 80));
    }

    const QColor ink = m_canvas->inkColor();
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i)
        m_swatches.at(i)->setChecked(m_palette.at(i) == ink);
    const bool inking = tool == ToolType::Pen || tool == ToolType::Ballpoint
                        || tool == ToolType::Highlighter || tool == ToolType::Shape
                        || tool == ToolType::Polyline || tool == ToolType::Text || tool == ToolType::Fill;
    for (int i = 0; i < m_trayDots.size() && i < m_palette.size(); ++i)
        m_trayDots.at(i)->setChecked(inking && m_palette.at(i) == ink);

    // Controles del borrador / lazo / relleno
    const bool erasing = tool == ToolType::Eraser;
    const bool filling = tool == ToolType::Fill;
    const bool lassoTool = tool == ToolType::Lasso;
    updateTipIcons();
    if (m_lassoModeSection)
        m_lassoModeSection->setVisible(lassoTool);
    if (m_lassoFreeBtn)
        m_lassoFreeBtn->setChecked(!m_canvas->lassoRectMode());
    if (m_lassoRectBtn)
        m_lassoRectBtn->setChecked(m_canvas->lassoRectMode());
    m_strokeStyleSection->setVisible(!erasing && !lassoTool);
    m_strokeGeometrySection->setVisible(!filling && !lassoTool);
    m_fillStyleSection->setVisible(filling);
    for (const auto& [button, pattern] : m_fillStyles)
        if (pattern == m_canvas->shadePattern())
            button->setChecked(true);
    m_stabilizerRow->setVisible(!erasing && !filling && !lassoTool);
    m_eraserSection->setVisible(erasing);
    m_eraserStrokeBtn->setChecked(m_canvas->eraserMode() == EraserMode::Stroke);
    m_eraserPartialBtn->setChecked(m_canvas->eraserMode() == EraserMode::Partial);
    const int eraserLevel = std::clamp(qRound(m_canvas->eraserSize() / 8.0), 1, 10);
    if (m_eraserSizeSlider->value() != eraserLevel)
        m_eraserSizeSlider->setValue(eraserLevel);
    m_eraserSizeLabel->setText(QString("%1 / 10").arg(eraserLevel));
    if (m_openPopover == m_strokePopover) {
        const QRect oldGeometry = m_strokePopover->geometry();
        positionOpenPopover();
        if (m_strokePopover->geometry() != oldGeometry)
            updateOverlayMask();
    }

    // Popover "Papel"
    const ThemeId activeTheme = m_canvas->themeId();
    for (int i = 0; i < m_themeBtns.size() && i < m_themeSpecs.size(); ++i)
        m_themeBtns.at(i)->setChecked(m_themeSpecs.at(i) == activeTheme);
    const BackgroundTemplate tpl = m_canvas->backgroundTemplate();
    for (int i = 0; i < m_templateSpecs.size(); ++i)
        m_templateBtns.at(i)->setChecked(m_templateSpecs.at(i).second == tpl);

    // Popover "Figuras": tipo de figura y toggles
    const ShapeKind shapeKind = m_canvas->shapeKind();
    const LinePattern pat = m_canvas->linePattern();
    const bool dimension = pat == LinePattern::Dimension;
    {
        const QSignalBlocker blocker(m_shapeEndings);
        m_shapeEndings->setCurrentIndex(int(m_canvas->lineEnding()));
        m_shapeEndings->setEnabled(!dimension);
    }
    for (int i = 0; i < m_shapeKindBtns.size(); ++i) {
        bool on = m_shapeKindSpecs.at(i) == shapeKind;
        if (on && m_shapeKindSpecs.at(i) == ShapeKind::Line)
            on = m_shapeDimensionSpecs.at(i) == dimension;
        if (on && m_shapeSidesSpecs.at(i) > 0)
            on = m_shapeSidesSpecs.at(i) == m_canvas->polygonSides()
                 && (i < m_shapeQuadSpecs.size() ? m_shapeQuadSpecs.at(i) == m_canvas->quadVariant() : true);
        if (on && i < m_shapeSolidSpecs.size() && m_shapeSolidSpecs.at(i) >= 0)
            on = m_shapeSolidSpecs.at(i) == m_canvas->solidKind();
        if (on && m_shapeTriSpecs.at(i) >= 0)
            on = m_shapeTriSpecs.at(i) == m_canvas->triangleVariant();
        m_shapeKindBtns.at(i)->setChecked(on);
    }
    m_holdToShapeBtn->setChecked(m_canvas->holdToShapeEnabled());
    m_smartGuidesBtn->setChecked(m_canvas->smartGuidesEnabled());

    const bool drawingPolyline = m_canvas->isDrawingPolyline();
    m_finishPolylineBtn->setVisible(drawingPolyline);
    m_closePolylineBtn->setVisible(drawingPolyline);
    m_selectionActions->setVisible(!drawingPolyline && m_canvas->hasSelection());
    const bool contextDesired = !m_cleanUi && !m_canvas->isActivelyDragging()
        && (drawingPolyline || m_canvas->hasSelection());
    const bool contextChanged = (m_contextPill->isVisible() != contextDesired);
    if (contextChanged)
        m_contextPill->setVisible(contextDesired);
    if (contextChanged || contextDesired)
        positionChildren(); // recalcula posición y máscara con el contenido actual

    // Etiqueta informativa del PDF cargado
    m_pdfInfoLabel->setVisible(pdfPageCount > 0);
    if (pdfPageCount > 0)
        m_pdfInfoLabel->setText(tr("Páginas de PDF en el cuaderno · %1").arg(pdfPageCount));

    // Pill de páginas (inferior izquierda): indicador y habilitaciones
    const int pageIndex = m_canvas->currentPageIndex();
    const int pageCount = m_canvas->pageCount();
    m_pageLabel->setText(QString("%1 / %2").arg(pageIndex + 1).arg(pageCount));
    m_pagePrevBtn->setEnabled(pageIndex > 0);
    m_pageNextBtn->setEnabled(pageIndex < pageCount - 1);
    m_updatingUi = false;

    updateZoomLabel();
}

void FloatingToolbar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionChildren();
}

bool FloatingToolbar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleLabel && event->type() == QEvent::MouseButtonRelease && m_titleEdit) {
        closePopovers();
        m_titleEdit->setText(m_titleLabel->text());
        m_titleLabel->hide();
        m_titleEdit->show();
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
        positionChildren();
        return true;
    }
    if (watched == m_canvas) {
        if (event->type() == QEvent::Resize) {
            setGeometry(m_canvas->rect());
            positionChildren();
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TabletPress) {
            // Un clic o toque del lápiz sobre el lienzo cierra cualquier popover abierto.
            if (m_openPopover)
                closePopovers();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace finily
