#include "ui/FloatingToolbar.h"

#include "ui/CanvasWidget.h"
#include "ui/IconLibrary.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantAnimation>

#include <algorithm>

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

QIcon glyphIcon(const QString& kind, const QColor& color, const QColor& onColor = QColor()) {
    QIcon icon(glyphPixmap(kind, color));
    if (onColor.isValid())
        icon.addPixmap(glyphPixmap(kind, onColor), QIcon::Normal, QIcon::On);
    return icon;
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
        if (pattern >= int(LinePattern::Solid) && pattern <= int(LinePattern::DoubleArrow))
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
    row->addWidget(m_titleLabel);
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
    m_paperBtn = m_fileBtn; // el popover de papel se abre desde el menú ⋯
}

// Panel lateral de páginas con miniaturas reales.
void FloatingToolbar::buildThumbPanel() {
    m_thumbPanel = new GlassCapsule(this);
    m_thumbPanel->setObjectName("finilyPopover");
    m_thumbPanel->setFixedWidth(150);
    m_thumbPanel->hide();
    auto* outer = new QVBoxLayout(m_thumbPanel);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    m_thumbScroll = new QScrollArea(m_thumbPanel);
    m_thumbScroll->setFrameShape(QFrame::NoFrame);
    m_thumbScroll->setWidgetResizable(true);
    m_thumbScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbScroll->setStyleSheet("QScrollArea{background:transparent;} QScrollArea>QWidget>QWidget{background:transparent;}");
    m_thumbHost = new QWidget();
    m_thumbHost->setStyleSheet("background:transparent;");
    m_thumbLayout = new QVBoxLayout(m_thumbHost);
    m_thumbLayout->setContentsMargins(0, 0, 0, 0);
    m_thumbLayout->setSpacing(10);
    m_thumbScroll->setWidget(m_thumbHost);
    outer->addWidget(m_thumbScroll, 1);
    auto* add = new QToolButton(m_thumbPanel);
    add->setFixedHeight(32);
    add->setText("+  Nueva página");
    add->setToolTip("Añadir página");
    polishButton(add);
    connect(add, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->addPage();
        refreshFromCanvas();
    });
    outer->addWidget(add);
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(350);
    connect(m_thumbTimer, &QTimer::timeout, this, &FloatingToolbar::rebuildThumbs);
}

void FloatingToolbar::rebuildThumbs() {
    if (!m_thumbPanel || m_thumbPanel->isHidden() || !m_canvas)
        return;
    while (QLayoutItem* it = m_thumbLayout->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const ThemeConfig theme = getThemeConfig(m_theme);
    const int cur = m_canvas->currentPageIndex();
    for (int i = 0; i < m_canvas->pageCount(); ++i) {
        auto* b = new QToolButton(m_thumbHost);
        b->setFixedSize(132, 96);
        b->setIconSize(QSize(124, 88));
        b->setIcon(QIcon(QPixmap::fromImage(m_canvas->pageThumbnail(i, QSize(124, 88)))));
        b->setToolTip(QString("Página %1").arg(i + 1));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString("QToolButton{border:%1;border-radius:9px;background:transparent;}"
                                 "QToolButton:hover{border:2px solid %2;}")
                             .arg(i == cur ? QString("2px solid %1").arg(theme.accent.name())
                                           : QString("1px solid %1").arg(theme.uiBorder.name()),
                                  theme.accent.name()));
        connect(b, &QToolButton::clicked, this, [this, i] {
            if (m_canvas)
                m_canvas->goToPage(i);
            refreshFromCanvas();
        });
        auto* wrap = new QWidget(m_thumbHost);
        auto* v = new QVBoxLayout(wrap);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        v->addWidget(b, 0, Qt::AlignHCenter);
        auto* n = new QLabel(QString::number(i + 1), wrap);
        n->setAlignment(Qt::AlignCenter);
        n->setStyleSheet(QString("color:%1;font-size:11px;background:transparent;").arg(theme.uiMuted.name()));
        v->addWidget(n);
        m_thumbLayout->addWidget(wrap);
    }
    m_thumbLayout->addStretch(1);
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

    // 4 útiles de escritura ilustrados SVG (1er clic = activar, 2do clic = abrir "Trazo").
    const QVector<QPair<ToolType, QPair<QString, QString>>> writingTools = {
        {ToolType::Pen, qMakePair(QStringLiteral("Pluma (P)"), QStringLiteral("pen"))},
        {ToolType::Highlighter, qMakePair(QStringLiteral("Resaltador (H)"), QStringLiteral("highlighter"))},
        {ToolType::Eraser, qMakePair(QStringLiteral("Borrador (E)"), QStringLiteral("eraser"))},
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
            if (m_canvas->tool() == tool) {
                // Segundo clic sobre la herramienta ya activa: abrir/cerrar "Trazo"
                if (m_openPopover == m_strokePopover)
                    closePopovers();
                else
                    openPopover(m_strokePopover, button);
            } else {
                closePopovers();
                selectTool(tool);
            }
        });
        m_writingKinds.append(tool);
        m_writingBtns.append(button);
    }

    m_dockDivider = new QWidget(m_dock);
    m_dockDivider->setObjectName("finilySep");

    // 6 herramientas duotono SVG bajo el divisor.
    const QVector<QPair<ToolType, QPair<QString, QString>>> secondary = {
        {ToolType::Shape, qMakePair(QStringLiteral("Figuras (S)"), QStringLiteral("shapes"))},
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

    // Tintas rápidas del estuche: un toque cambia el color sin abrir paneles.
    m_trayDivider = new QWidget(m_dock);
    m_trayDivider->setObjectName("finilySep");
    for (int i = 0; i < kTrayDots; ++i) {
        auto* dot = new ColorDotButton(m_dock);
        dot->setFixedSize(30, 30);
        dot->hide();
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
    colorBtn->setToolTip("Color y grosor");
    polishButton(colorBtn);
    connect(m_trayMoreBtn, &QToolButton::clicked, this, [this] {
        if (m_openPopover == m_strokePopover)
            closePopovers();
        else
            openPopover(m_strokePopover, m_trayMoreBtn);
    });
}

// Coloca los hijos del dock según la orientación (vertical a la izquierda,
// horizontal arriba/abajo) y fija el tamaño de la cápsula.
void FloatingToolbar::layoutDock() {
    if (!m_dock)
        return;
    // Estuche: los lápices asoman desde el borde exterior (abajo en horizontal,
    // izquierda en vertical) y el elegido se eleva. Luego herramientas y tintas.
    const bool vertical = m_dockPos == DockPos::Izquierda;
    const int penLong = 40, penShort = 40, pad = 10;
    const int toolSize = 40, dotSize = 30;
    const int crossLen = 56; // grosor del estuche
    const int along = pad + m_writingBtns.size() * (toolSize + 2) + 12
                      + m_secondaryBtns.size() * (toolSize + 2) + 12
                      + 38 + pad;
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
            m_trayDivider->setGeometry(22, pos, crossLen - 44, 1);
        else
            m_trayDivider->setGeometry(pos, 22, 1, crossLen - 44);
    }
    pos += 8;
    if (m_trayMoreBtn)
        place(m_trayMoreBtn, pos, 38, 38);
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
    m_zoomResetBtn->setToolTip("Restablecer zoom (Ctrl+0)");
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

    row->addWidget(makeSeparator(m_pagePill));

    m_pageAddBtn = new QToolButton(m_pagePill);
    m_pageAddBtn->setFixedSize(30, 30);
    m_pageAddBtn->setIconSize(QSize(16, 16));
    m_iconSpecs.append(qMakePair(m_pageAddBtn, QString("plus")));
    m_pageAddBtn->setToolTip("Nueva página (Ctrl+Shift+N)");
    polishButton(m_pageAddBtn);
    connect(m_pageAddBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->addPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pageAddBtn);

    m_pageDeleteBtn = new QToolButton(m_pagePill);
    m_pageDeleteBtn->setFixedSize(30, 30);
    m_pageDeleteBtn->setIconSize(QSize(16, 16));
    m_pageDeleteBtn->setToolTip("Eliminar página actual");
    polishButton(m_pageDeleteBtn);
    connect(m_pageDeleteBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        if (m_canvas->currentPageHasContent()) {
            const auto answer = QMessageBox::question(
                window(), QStringLiteral("Eliminar página"),
                QStringLiteral("¿Eliminar la página %1 y todo su contenido?\nEsta acción no se puede deshacer.")
                    .arg(m_canvas->currentPageIndex() + 1),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        m_canvas->deleteCurrentPage();
        refreshFromCanvas();
    });
    m_iconSpecs.append(qMakePair(m_pageDeleteBtn, QString("page-delete")));
    row->addWidget(m_pageDeleteBtn);
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
        if (chosen.isValid())
            setColor(chosen);
    });
    swatchRow->addWidget(m_customColorBtn);
    swatchRow->addStretch(1);
    styleColumn->addLayout(swatchRow);

    // Etiqueta + punto de vista previa en vivo (color y grosor reales, se
    // repinta al arrastrar el slider o al pulsar Fino/Medio/Grueso).
    auto* sizeRow = new QHBoxLayout();
    sizeRow->setSpacing(10);
    m_sizeLabel = new QLabel("2.6 px", m_strokeStyleSection);
    m_sizeLabel->setObjectName("finilyValue");
    m_strokePreview = new StrokePreviewWidget(m_strokeStyleSection);
    sizeRow->addWidget(m_strokePreview);
    m_sizeSlider = new QSlider(Qt::Horizontal, m_strokeStyleSection);
    m_sizeSlider->setRange(10, 200); // 1.0 .. 20.0 px, en décimas
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setFocusPolicy(Qt::NoFocus);
    sizeRow->addWidget(m_sizeSlider, 1);
    m_sizeLabel->setMinimumWidth(44);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(m_sizeLabel);
    styleColumn->addLayout(sizeRow);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStrokeSize(value / 10.0);
        m_sizeLabel->setText(QString("%1 px").arg(value / 10.0, 0, 'f', 1));
        updateStrokePreview();
        syncActivePresetFromCanvas();
        updateStationery();
    });

    auto* patternRow = new QHBoxLayout();
    patternRow->setSpacing(4);
    const QVector<QString> patternNames = {"Sólida", "Guiones", "Punteada", "Flecha", "Doble flecha"};
    const QVector<LinePattern> patterns = {LinePattern::Solid, LinePattern::Dashed, LinePattern::Dotted,
                                           LinePattern::Arrow, LinePattern::DoubleArrow};
    patternRow->setSpacing(2);
    for (int i = 0; i < patterns.size(); ++i) {
        auto* button = new QToolButton(m_strokeStyleSection);
        button->setFixedSize(51, 30);
        button->setIconSize(QSize(40, 14));
        button->setCheckable(true);
        button->setToolTip(patternNames.at(i));
        polishButton(button);
        const LinePattern pattern = patterns.at(i);
        connect(button, &QToolButton::clicked, this, [this, pattern] {
            if (m_canvas)
                m_canvas->setLinePattern(pattern);
            syncActivePresetFromCanvas();
            refreshFromCanvas();
        });
        m_patternBtns.append(button);
        m_patternSpecs.append(qMakePair(button, pattern));
        patternRow->addWidget(button);
    }
    patternRow->addStretch(1);
    styleColumn->addLayout(patternRow);
    column->addWidget(m_strokeStyleSection);

    // --- Estabilizador ---
    m_stabilizerRow = new QWidget(m_strokePopover);
    auto* moreOuter = new QVBoxLayout(m_stabilizerRow);
    moreOuter->setContentsMargins(0, 0, 0, 0);
    moreOuter->setSpacing(8);
    auto* moreToggle = new QToolButton(m_stabilizerRow);
    moreToggle->setObjectName("finilyMoreToggle");
    moreToggle->setText("Más ajustes  ▾");
    moreToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    moreToggle->setCursor(Qt::PointingHandCursor);
    moreToggle->setFocusPolicy(Qt::NoFocus);
    moreToggle->setFixedHeight(26);
    moreOuter->addWidget(moreToggle);
    auto* moreBox = new QWidget(m_stabilizerRow);
    moreBox->setVisible(false);
    moreOuter->addWidget(moreBox);
    connect(moreToggle, &QToolButton::clicked, this, [this, moreBox, moreToggle] {
        const bool show = !moreBox->isVisible();
        moreBox->setVisible(show);
        moreToggle->setText(show ? "Más ajustes  ▴" : "Más ajustes  ▾");
        m_strokePopover->adjustSize();
        updateOverlayMask();
    });
    auto* stabilizerColumn = new QVBoxLayout(moreBox);
    stabilizerColumn->setContentsMargins(0, 0, 0, 0);
    stabilizerColumn->setSpacing(8);
    m_stabilizerLabel = new QLabel("Estabilizador · 15 %", moreBox);
    m_stabilizerLabel->setObjectName("finilyValue");
    stabilizerColumn->addWidget(m_stabilizerLabel);
    m_stabilizerSlider = new QSlider(Qt::Horizontal, moreBox);
    m_stabilizerSlider->setRange(0, 100);
    m_stabilizerSlider->setCursor(Qt::PointingHandCursor);
    m_stabilizerSlider->setFocusPolicy(Qt::NoFocus);
    stabilizerColumn->addWidget(m_stabilizerSlider);
    connect(m_stabilizerSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStabilizer(value / 100.0);
        m_stabilizerLabel->setText(QString("Estabilizador · %1 %").arg(value));
    });
    // --- Presión Wacom (perfil de respuesta de la tableta), dentro de "Más ajustes" ---
    auto* pressureSection = new QWidget(moreBox);
    auto* pressureColumn = new QVBoxLayout(pressureSection);
    pressureColumn->setContentsMargins(0, 0, 0, 0);
    pressureColumn->setSpacing(8);
    auto* pressureRow = new QHBoxLayout();
    pressureRow->setSpacing(4);
    const QVector<QString> pressureNames = {"Ligera", "Normal", "Firme"};
    const QVector<QString> pressureTips = {
        "Ideal si escribes rápido sin presionar fuerte",
        "Balance entre control y expresión",
        "Mayor contraste caligráfico",
    };
    const QVector<PressureProfile> profiles = {PressureProfile::Light, PressureProfile::Normal,
                                               PressureProfile::Firm};
    for (int i = 0; i < profiles.size(); ++i) {
        auto* button = new QToolButton(pressureSection);
        button->setFixedSize(86, 30);
        button->setText(pressureNames.at(i));
        button->setToolTip(pressureTips.at(i));
        button->setCheckable(true);
        polishButton(button);
        const PressureProfile profile = profiles.at(i);
        connect(button, &QToolButton::clicked, this, [this, profile] {
            if (m_canvas)
                m_canvas->setPressureProfile(profile);
            refreshFromCanvas();
        });
        m_pressureBtns.append(button);
        m_pressureSpecs.append(profile);
        pressureRow->addWidget(button);
    }
    pressureRow->addStretch(1);
    pressureColumn->addLayout(pressureRow);

    auto* swapBtn = new SwitchButton(pressureSection);
    swapBtn->setFixedSize(272, 32);
    swapBtn->setText("Invertir botones del lápiz");
    swapBtn->setToolTip("Botón grande = Borrar · Botón pequeño = Escribir (pulsa para invertir si vienen al revés)");
    swapBtn->setCheckable(true);
    swapBtn->setChecked(m_canvas && m_canvas->swapPenButtons());
    connect(swapBtn, &QToolButton::clicked, this, [this, swapBtn] {
        if (m_canvas)
            m_canvas->setSwapPenButtons(swapBtn->isChecked());
    });
    pressureColumn->addWidget(swapBtn);

    m_mathBeautifyBtn = new SwitchButton(pressureSection);
    m_mathBeautifyBtn->setFixedSize(272, 32);
    m_mathBeautifyBtn->setText("Pulir signos y números");
    m_mathBeautifyBtn->setToolTip("Embellece números, signos matemáticos y tus plantillas al escribir (Ctrl+Z deshace)");
    m_mathBeautifyBtn->setCheckable(true);
    m_mathBeautifyBtn->setChecked(m_canvas ? m_canvas->mathBeautifyEnabled() : true);
    connect(m_mathBeautifyBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_mathBeautifyBtn)
            m_canvas->setMathBeautifyEnabled(m_mathBeautifyBtn->isChecked());
        refreshFromCanvas();
    });
    pressureColumn->addWidget(m_mathBeautifyBtn);

    auto* opColorRow = new QHBoxLayout();
    opColorRow->setSpacing(6);
    m_autoColorOpsBtn = new SwitchButton(pressureSection);
    m_autoColorOpsBtn->setFixedSize(238, 32);
    m_autoColorOpsBtn->setText("Colorear signos (+ − = √)");
    m_autoColorOpsBtn->setToolTip("Pinta automáticamente los signos operativos y de agrupación con el color elegido");
    m_autoColorOpsBtn->setCheckable(true);
    m_autoColorOpsBtn->setChecked(m_canvas ? m_canvas->autoColorOperators() : true);
    connect(m_autoColorOpsBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas && m_autoColorOpsBtn)
            m_canvas->setAutoColorOperators(m_autoColorOpsBtn->isChecked());
        refreshFromCanvas();
    });
    opColorRow->addWidget(m_autoColorOpsBtn);

    m_opColorBtn = new ColorDotButton(pressureSection);
    m_opColorBtn->setCheckable(false);
    m_opColorBtn->setDotColor(m_canvas ? m_canvas->operatorColor() : QColor("#ef4444"));
    m_opColorBtn->setToolTip("Cambiar color automático de signos");
    polishButton(m_opColorBtn);
    connect(m_opColorBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        const QColor chosen = QColorDialog::getColor(m_canvas->operatorColor(), window(),
                                                     QStringLiteral("Color para signos matemáticos"));
        if (chosen.isValid()) {
            m_canvas->setOperatorColor(chosen);
            refreshFromCanvas();
        }
    });
    opColorRow->addWidget(m_opColorBtn);
    pressureColumn->addLayout(opColorRow);
    stabilizerColumn->addWidget(pressureSection);
    column->addWidget(m_stabilizerRow);

    // --- Borrador (visible solo con el borrador activo) ---
    m_eraserSection = new QWidget(m_strokePopover);
    auto* eraserColumn = new QVBoxLayout(m_eraserSection);
    eraserColumn->setContentsMargins(0, 0, 0, 0);
    eraserColumn->setSpacing(8);
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    m_eraserStrokeBtn = new QToolButton(m_eraserSection);
    m_eraserStrokeBtn->setFixedSize(133, 30);
    m_eraserStrokeBtn->setText("Trazo completo");
    m_eraserStrokeBtn->setToolTip("Elimina trazos completos");
    m_eraserStrokeBtn->setCheckable(true);
    polishButton(m_eraserStrokeBtn);
    connect(m_eraserStrokeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Stroke);
        refreshFromCanvas();
    });
    m_eraserPartialBtn = new QToolButton(m_eraserSection);
    m_eraserPartialBtn->setFixedSize(133, 30);
    m_eraserPartialBtn->setText("Parcial");
    m_eraserPartialBtn->setToolTip("Borra solo la zona rozada");
    m_eraserPartialBtn->setCheckable(true);
    polishButton(m_eraserPartialBtn);
    connect(m_eraserPartialBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setEraserMode(EraserMode::Partial);
        refreshFromCanvas();
    });
    modeRow->addWidget(m_eraserStrokeBtn);
    modeRow->addWidget(m_eraserPartialBtn);
    eraserColumn->addLayout(modeRow);

    m_eraserSizeLabel = new QLabel("18 px", m_eraserSection);
    eraserColumn->addWidget(m_eraserSizeLabel);
    m_eraserSizeSlider = new QSlider(Qt::Horizontal, m_eraserSection);
    m_eraserSizeSlider->setRange(4, 80);
    m_eraserSizeSlider->setCursor(Qt::PointingHandCursor);
    m_eraserSizeSlider->setFocusPolicy(Qt::NoFocus);
    eraserColumn->addWidget(m_eraserSizeSlider);
    connect(m_eraserSizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setEraserSize(value);
        m_eraserSizeLabel->setText(QString("%1 px").arg(value));
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

    m_clearBtn = new QPushButton("Limpiar pizarra", m_paperPopover);
    m_clearBtn->setFixedSize(272, 34);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFocusPolicy(Qt::NoFocus);
    m_clearBtn->setToolTip("Borra todos los trazos (pide confirmación)");
    connect(m_clearBtn, &QPushButton::clicked, this, &FloatingToolbar::onClearClicked);
    column->addWidget(m_clearBtn);
    column->addStretch(1);
}

void FloatingToolbar::buildShapesPopover() {
    // Popover hijo normal del overlay (patrón Hyprland-safe, ver buildStrokePopover).
    m_shapesPopover = new GlassCapsule(this);
    m_shapesPopover->setObjectName("finilyPopover");
    m_shapesPopover->setFixedWidth(300);
    m_shapesPopover->hide();

    auto* column = new QVBoxLayout(m_shapesPopover);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    const QVector<QString> names = {"Línea", "Círculo", "Elipse", "Triángulo", "Rectángulo", "Curva"};
    const QVector<QString> glyphs = {"shape-line", "shape-circle", "shape-ellipse",
                                     "shape-triangle", "shape-rect", "shape-curve"};
    const QVector<QString> tips = {"Línea recta", "Círculo perfecto", "Elipse",
                                   "Triángulo", "Rectángulo", "Curva / arco suave editable"};
    const QVector<ShapeKind> kinds = {ShapeKind::Line, ShapeKind::Circle, ShapeKind::Ellipse,
                                      ShapeKind::Triangle, ShapeKind::Rectangle, ShapeKind::Curve};
    auto addShapeBtn = [&](QGridLayout* targetGrid, int idx, const QString& name,
                           const QString& glyph, const QString& tip, ShapeKind kind) {
        auto* button = new QToolButton(m_shapesPopover);
        button->setObjectName("finilyShapeBtn");
        button->setFixedSize(64, 40);
        button->setIconSize(QSize(20, 20));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setToolTip(name + " · " + tip);
        button->setCheckable(true);
        polishButton(button);
        connect(button, &QToolButton::clicked, this, [this, kind] {
            if (!m_canvas)
                return;
            m_canvas->setShapeKind(kind);
            m_canvas->setTool(ToolType::Shape);
            closePopovers();
            refreshFromCanvas();
        });
        m_shapeKindBtns.append(button);
        m_shapeKindSpecs.append(kind);
        m_iconSpecs.append(qMakePair(button, glyph));
        targetGrid->addWidget(button, idx / 4, idx % 4);
    };
    for (int i = 0; i < kinds.size(); ++i)
        addShapeBtn(grid, i, names.at(i), glyphs.at(i), tips.at(i), kinds.at(i));
    column->addLayout(grid);

    auto* mathGrid = grid;
    addShapeBtn(mathGrid, 6, QStringLiteral("Ejes X/Y"), QStringLiteral("shape-axes"),
                QStringLiteral("Plano cartesiano con ejes X/Y"), ShapeKind::Axes);
    addShapeBtn(mathGrid, 7, QStringLiteral("Raíz √"), QStringLiteral("shape-radical"),
                QStringLiteral("Raíz cuadrada auto-ajustable (R)"), ShapeKind::Radical);

    auto* assistRow = new QHBoxLayout();
    assistRow->setSpacing(6);
    {
        auto* poly = new QToolButton(m_shapesPopover);
        poly->setObjectName("finilyShapeBtn");
        poly->setFixedSize(64, 40);
        poly->setIconSize(QSize(20, 20));
        poly->setToolTip("Polilínea · L");
        polishButton(poly);
        connect(poly, &QToolButton::clicked, this, [this] {
            closePopovers();
            selectTool(ToolType::Polyline);
        });
        m_iconSpecs.append(qMakePair(poly, QString("polyline")));
        grid->addWidget(poly, 2, 0);
    }
    column->addLayout(assistRow);
    m_holdToShapeBtn = new QToolButton(m_shapesPopover);
    m_holdToShapeBtn->setFixedSize(133, 30);
    m_holdToShapeBtn->setText("Auto-forma");
    m_holdToShapeBtn->setToolTip("Mantén pulsado sobre el lienzo y el trazo se ajustará a la forma perfecta");
    m_holdToShapeBtn->setCheckable(true);
    polishButton(m_holdToShapeBtn);
    connect(m_holdToShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setHoldToShapeEnabled(m_holdToShapeBtn->isChecked());
        refreshFromCanvas();
    });
    assistRow->addWidget(m_holdToShapeBtn);

    m_smartGuidesBtn = new QToolButton(m_shapesPopover);
    m_smartGuidesBtn->setFixedSize(133, 30);
    m_smartGuidesBtn->setText("Guías");
    m_smartGuidesBtn->setToolTip("Muestra guías inteligentes de alineación y ángulos al crear figuras");
    m_smartGuidesBtn->setCheckable(true);
    polishButton(m_smartGuidesBtn);
    connect(m_smartGuidesBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setSmartGuidesEnabled(m_smartGuidesBtn->isChecked());
        refreshFromCanvas();
    });
    assistRow->addWidget(m_smartGuidesBtn);
    column->addStretch(1);
}

void FloatingToolbar::buildContextPill() {
    // Barra contextual diminuta bajo la pill principal: solo aparece mientras se
    // dibuja una polilínea o hay una figura seleccionada.
    m_contextPill = new GlassCapsule(this);
    m_contextPill->setObjectName("finilyContext");
    auto* row = new QHBoxLayout(m_contextPill);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(4);

    m_finishPolylineBtn = new QToolButton(m_contextPill);
    m_finishPolylineBtn->setFixedHeight(30);
    m_finishPolylineBtn->setContentsMargins(8, 0, 8, 0);
    m_finishPolylineBtn->setText("Terminar (Enter)");
    m_finishPolylineBtn->setToolTip("Termina la polilínea (también con doble clic o Enter)");
    polishButton(m_finishPolylineBtn);
    connect(m_finishPolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(false);
        refreshFromCanvas();
    });
    row->addWidget(m_finishPolylineBtn);

    m_closePolylineBtn = new QToolButton(m_contextPill);
    m_closePolylineBtn->setFixedHeight(30);
    m_closePolylineBtn->setContentsMargins(8, 0, 8, 0);
    m_closePolylineBtn->setText("Cerrar polígono");
    m_closePolylineBtn->setToolTip("Cierra la polilínea formando un polígono cerrado");
    polishButton(m_closePolylineBtn);
    connect(m_closePolylineBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->finishActivePolyline(true);
        refreshFromCanvas();
    });
    row->addWidget(m_closePolylineBtn);

    m_deleteShapeBtn = new QToolButton(m_contextPill);
    m_deleteShapeBtn->setFixedHeight(30);
    m_deleteShapeBtn->setContentsMargins(8, 0, 8, 0);
    m_deleteShapeBtn->setText("Eliminar figura (Supr)");
    m_deleteShapeBtn->setToolTip("Elimina la figura seleccionada (Supr)");
    polishButton(m_deleteShapeBtn);
    connect(m_deleteShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->deleteSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_deleteShapeBtn);

    m_deleteImageBtn = new QToolButton(m_contextPill);
    m_deleteImageBtn->setFixedHeight(30);
    m_deleteImageBtn->setContentsMargins(8, 0, 8, 0);
    m_deleteImageBtn->setText("Eliminar imagen (Supr)");
    m_deleteImageBtn->setToolTip("Elimina la imagen seleccionada (Supr)");
    polishButton(m_deleteImageBtn);
    connect(m_deleteImageBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->deleteSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_deleteImageBtn);

    m_deleteTextBtn = new QToolButton(m_contextPill);
    m_deleteTextBtn->setFixedHeight(30);
    m_deleteTextBtn->setContentsMargins(8, 0, 8, 0);
    m_deleteTextBtn->setText("Eliminar texto (Supr)");
    m_deleteTextBtn->setToolTip("Elimina el bloque de texto seleccionado (Supr)");
    polishButton(m_deleteTextBtn);
    connect(m_deleteTextBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->deleteSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_deleteTextBtn);

    // Con selección de lazo: transformar, duplicar y eliminar
    m_straightenBtn = new QToolButton(m_contextPill);
    m_straightenBtn->setObjectName("finilyAccentBtn");
    m_straightenBtn->setFixedHeight(30);
    m_straightenBtn->setContentsMargins(8, 0, 8, 0);
    m_straightenBtn->setText("Enderezar letra");
    m_straightenBtn->setToolTip("Endereza la letra o trazo seleccionado (estilo Samsung Notes)");
    polishButton(m_straightenBtn);
    connect(m_straightenBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->straightenSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_straightenBtn);

    m_toTextBtn = new QToolButton(m_contextPill);
    m_toTextBtn->setObjectName("finilyAccentBtn");
    m_toTextBtn->setFixedHeight(30);
    m_toTextBtn->setContentsMargins(8, 0, 8, 0);
    m_toTextBtn->setText("Pasar a texto");
    m_toTextBtn->setToolTip("Convierte lo encerrado en texto editable");
    polishButton(m_toTextBtn);
    connect(m_toTextBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->convertSelectionToText();
        refreshFromCanvas();
    });
    row->addWidget(m_toTextBtn);

    m_saveTemplateBtn = new QToolButton(m_contextPill);
    m_saveTemplateBtn->setFixedHeight(30);
    m_saveTemplateBtn->setContentsMargins(8, 0, 8, 0);
    m_saveTemplateBtn->setText("Guardar plantilla");
    m_saveTemplateBtn->setToolTip("Guarda tu trazo como plantilla propia para reconocer y embellecer");
    polishButton(m_saveTemplateBtn);
    connect(m_saveTemplateBtn, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        bool ok = false;
        const QString label = QInputDialog::getText(
            window(), QStringLiteral("Guardar mi plantilla"),
            QStringLiteral("¿Qué número, signo o letra es? (ej: 5, +, θ, a):"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !label.trimmed().isEmpty()) {
            m_canvas->saveSelectionAsTemplate(label);
            refreshFromCanvas();
        }
    });
    row->addWidget(m_saveTemplateBtn);

    m_duplicateBtn = new QToolButton(m_contextPill);
    m_duplicateBtn->setFixedHeight(30);
    m_duplicateBtn->setContentsMargins(8, 0, 8, 0);
    m_duplicateBtn->setText("Duplicar (Ctrl+D)");
    m_duplicateBtn->setToolTip("Duplica la selección");
    polishButton(m_duplicateBtn);
    connect(m_duplicateBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->duplicateSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_duplicateBtn);

    m_deleteLassoBtn = new QToolButton(m_contextPill);
    m_deleteLassoBtn->setFixedHeight(30);
    m_deleteLassoBtn->setContentsMargins(8, 0, 8, 0);
    m_deleteLassoBtn->setText("Eliminar (Supr)");
    m_deleteLassoBtn->setToolTip("Elimina la selección del lazo (Supr)");
    polishButton(m_deleteLassoBtn);
    connect(m_deleteLassoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->deleteSelection();
        refreshFromCanvas();
    });
    row->addWidget(m_deleteLassoBtn);

    m_contextPill->hide();
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
    m_importPdfBtn->setText("Importar PDF / PPT (Ctrl+O)");
    m_importPdfBtn->setToolTip("Inserta las páginas de un PDF o diapositivas exportadas como imágenes");
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
        const QString path = QFileDialog::getOpenFileName(window(), "Insertar imagen", QString(),
                                                          "Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)");
        if (!path.isEmpty()) {
            m_canvas->importImageFile(path);
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
    column->addStretch(1);
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
    m_canvas->loadFromFile(path);
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
                                                "Cuaderno Finily (*.finily)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".finily", Qt::CaseInsensitive))
        path += ".finily"; // auto-anexa la extensión del cuaderno
    m_canvas->saveToFile(path);
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
    border: 1.5px solid %13;
}
QToolButton#finilyWritingTool:hover:checked, QToolButton#finilyTool:hover:checked {
    background: %11;
    border: 1.5px solid %13;
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
        m_secondaryBtns.at(i)->setIcon(
            makeStudioIcon(m_secondaryIcons.at(i), 24, theme.uiText, theme.accent, ink, theme.isDark));

    m_lastInkIconSig = QString("%1|%2").arg(int(m_theme)).arg(ink.name(QColor::HexArgb));

    // 4. Mini-iconos auxiliares (zoom, páginas, popovers de figuras/patrones/plantillas)
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_patternSpecs)
        spec.first->setIcon(QIcon(patternPixmap(spec.second, theme.uiText)));
    for (const auto& spec : m_templateSpecs)
        spec.first->setIcon(QIcon(templatePixmap(spec.second, theme.uiText)));
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
        const int x = std::max(6, (width() - m_contextPill->width()) / 2);
        // La pill de utilidades siempre vive arriba: la barra contextual cae debajo.
        const int y = (m_pill && !m_pill->isHidden()) ? m_pill->geometry().bottom() + 6 : 12;
        m_contextPill->move(x, y);
    }
    if (m_toastPill && !m_toastPill->isHidden()) {
        m_toastPill->adjustSize();
        // Dock abajo: toast arriba-centrado; en otro caso, abajo del todo
        // (el dock izquierdo queda centrado y no tropieza con el toast).
        const int y = m_dockPos == DockPos::Abajo ? 18 : height() - m_toastPill->height() - 64;
        m_toastPill->move(std::max(6, (width() - m_toastPill->width()) / 2), y);
    }
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

// Abre el popover anclado a su botón de la pill (encima si la barra está
// acoplada abajo, debajo si está arriba), en el espacio de coordenadas del
// propio overlay (posicionamiento nativo, sin ventanas flotantes que
// Wayland/Hyprland malposicione). Desliza 8 px hacia su lugar final con
// 140 ms OutCubic; sin fundido: el QGraphicsDropShadowEffect ya ocupa el
// único slot de efecto del widget y jamás se sobreescribe.
void FloatingToolbar::openPopover(GlassCapsule* popover, QWidget* anchor) {
    Q_UNUSED(anchor); // el anclaje lo decide la posición del dock (positionChildren)
    if (!popover || !anchor)
        return;
    if (m_openPopover == popover) { // segundo clic: cerrar
        closePopovers();
        return;
    }
    closePopovers();
    m_openPopover = popover;

    popover->adjustSize();
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
    const QPoint finalPos(x, y);
    // Desliza 8 px hacia su lugar final (desde el cromo que lo ancla).
    const QPoint startPos = (m_topRight && anchor->parentWidget() == m_topRight) ? QPoint(x, y - 8)
                            : m_dockPos == DockPos::Izquierda ? QPoint(x - 8, y)
                            : m_dockPos == DockPos::Abajo   ? QPoint(x, y - 8)
                                                            : QPoint(x, y + 8);
    popover->move(startPos);
    popover->show();
    popover->raise();
    updateOverlayMask();

    if (m_popoverAnim) {
        m_popoverAnim->stop();
        m_popoverAnim->deleteLater();
    }
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
        m_openPopover->close();
        m_openPopover = nullptr;
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
    closePopovers();
    positionChildren();
    refreshFromCanvas(); // sincroniza los botones Izquierda/Arriba/Abajo
    if (open == m_strokePopover && !m_writingBtns.isEmpty())
        openPopover(m_strokePopover, m_writingBtns.first());
    else if (open == m_paperPopover)
        openPopover(m_paperPopover, m_paperBtn);
    else if (open == m_shapesPopover)
        openPopover(m_shapesPopover, m_shapeBtn);
    else if (open == m_filePopover)
        openPopover(m_filePopover, m_fileBtn);
}

void FloatingToolbar::toggleCleanUi() {
    m_cleanUi = !m_cleanUi;
    // Corta cualquier animación previa de la pill (sin disparar su lambda de
    // cierre para no esconder controles que se van a restaurar).
    if (m_pillAnim) {
        QObject::disconnect(m_pillAnim, &QPropertyAnimation::finished, this, nullptr);
        m_pillAnim->stop();
        m_pillAnim->deleteLater();
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
        if (m_thumbPanel && m_pagesBtn && m_pagesBtn->isChecked())
            m_thumbPanel->show();
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
        dot->hide();
        if (i >= m_palette.size())
            continue;
        dot->setDotColor(m_palette.at(i));
        dot->setAccentColor(theme.accent);
        dot->setRimColor(rim);
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
        m_clearBtn->setText("¿Borrar todo? Pulsa de nuevo");
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
        m_clearBtn->setText("Limpiar pizarra");
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

    if (m_thumbPanel && !m_thumbPanel->isHidden() && m_thumbTimer)
        m_thumbTimer->start();
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

    // Huella de estado: evita reflashear la UI en cada punto de un trazo a 200 Hz.
    const QString signature = QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19|%20|%21|%22|%23")
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
                                  .arg(m_canvas->hasLassoSelection())
                                  .arg(m_canvas->hasSelectedImage())
                                  .arg(m_canvas->hasSelectedText())
                                  .arg(m_canvas->pdfPageCount())
                                  .arg(m_canvas->currentPageIndex())
                                  .arg(m_canvas->pageCount());
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
        if (m_writingKinds.at(i) == tool)
            m_writingBtns.at(i)->setChecked(true);
    for (int i = 0; i < m_secondaryBtns.size() && i < m_secondaryKinds.size(); ++i)
        if (m_secondaryKinds.at(i) == tool)
            m_secondaryBtns.at(i)->setChecked(true);
    updateStationery();

    // Popover "Trazo"
    const int sizeTenths = qRound(m_canvas->strokeSize() * 10.0);
    if (m_sizeSlider->value() != sizeTenths)
        m_sizeSlider->setValue(sizeTenths);
    m_sizeLabel->setText(QString("%1 px").arg(m_canvas->strokeSize(), 0, 'f', 1));
    updateStrokePreview();
    const LinePattern pattern = m_canvas->linePattern();
    for (int i = 0; i < m_patternSpecs.size(); ++i)
        m_patternBtns.at(i)->setChecked(m_patternSpecs.at(i).second == pattern);

    const int stabilizerPercent = qRound(m_canvas->stabilizer() * 100.0);
    if (m_stabilizerSlider->value() != stabilizerPercent)
        m_stabilizerSlider->setValue(stabilizerPercent);
    m_stabilizerLabel->setText(QString("Estabilizador · %1 %").arg(stabilizerPercent));

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
                        || tool == ToolType::Polyline || tool == ToolType::Text;
    for (int i = 0; i < m_trayDots.size() && i < m_palette.size(); ++i)
        m_trayDots.at(i)->setChecked(inking && m_palette.at(i) == ink);

    // Controles del borrador
    const bool erasing = tool == ToolType::Eraser;
    m_strokeStyleSection->setVisible(!erasing);
    m_stabilizerRow->setVisible(!erasing);
    m_eraserSection->setVisible(erasing);
    m_eraserStrokeBtn->setChecked(m_canvas->eraserMode() == EraserMode::Stroke);
    m_eraserPartialBtn->setChecked(m_canvas->eraserMode() == EraserMode::Partial);
    const int eraserSize = qRound(m_canvas->eraserSize());
    if (m_eraserSizeSlider->value() != eraserSize)
        m_eraserSizeSlider->setValue(eraserSize);
    m_eraserSizeLabel->setText(QString("%1 px").arg(eraserSize));

    // Popover "Papel"
    const ThemeId activeTheme = m_canvas->themeId();
    for (int i = 0; i < m_themeBtns.size() && i < m_themeSpecs.size(); ++i)
        m_themeBtns.at(i)->setChecked(m_themeSpecs.at(i) == activeTheme);
    const BackgroundTemplate tpl = m_canvas->backgroundTemplate();
    for (int i = 0; i < m_templateSpecs.size(); ++i)
        m_templateBtns.at(i)->setChecked(m_templateSpecs.at(i).second == tpl);

    // Popover "Figuras": tipo de figura y toggles
    const ShapeKind shapeKind = m_canvas->shapeKind();
    for (int i = 0; i < m_shapeKindBtns.size() && i < m_shapeKindSpecs.size(); ++i)
        m_shapeKindBtns.at(i)->setChecked(m_shapeKindSpecs.at(i) == shapeKind);
    m_holdToShapeBtn->setChecked(m_canvas->holdToShapeEnabled());
    m_smartGuidesBtn->setChecked(m_canvas->smartGuidesEnabled());

    // Barra contextual con prioridad: polilínea > lazo > figura > imagen > texto
    const bool drawingPolyline = m_canvas->isDrawingPolyline();
    const bool lassoSelection = m_canvas->hasLassoSelection();
    const bool shapeSelected = m_canvas->hasSelectedShape();
    const bool imageSelected = m_canvas->hasSelectedImage();
    const bool textSelected = m_canvas->hasSelectedText();
    m_finishPolylineBtn->setVisible(drawingPolyline);
    m_closePolylineBtn->setVisible(drawingPolyline);
    m_straightenBtn->setVisible(lassoSelection);
    m_toTextBtn->setVisible(lassoSelection);
    m_saveTemplateBtn->setVisible(lassoSelection);
    m_duplicateBtn->setVisible(lassoSelection);
    m_deleteLassoBtn->setVisible(lassoSelection);
    m_deleteShapeBtn->setVisible(!lassoSelection && shapeSelected);
    m_deleteImageBtn->setVisible(!lassoSelection && !shapeSelected && imageSelected);
    m_deleteTextBtn->setVisible(!lassoSelection && !shapeSelected && !imageSelected && textSelected);
    const bool contextDesired = !m_cleanUi &&
        (drawingPolyline || lassoSelection || shapeSelected || imageSelected || textSelected);
    if (m_contextPill->isVisible() != contextDesired)
        m_contextPill->setVisible(contextDesired);
    if (contextDesired)
        positionChildren(); // recalcula posición y máscara con el contenido actual

    // Etiqueta informativa del PDF cargado
    const int pdfPages = m_canvas->pdfPageCount();
    m_pdfInfoLabel->setVisible(pdfPages > 0);
    if (pdfPages > 0)
        m_pdfInfoLabel->setText(QString("PDF cargado: %1 págs.").arg(pdfPages));

    // Pill de páginas (inferior izquierda): indicador y habilitaciones
    const int pageIndex = m_canvas->currentPageIndex();
    const int pageCount = m_canvas->pageCount();
    m_pageLabel->setText(QString("%1 / %2").arg(pageIndex + 1).arg(pageCount));
    m_pagePrevBtn->setEnabled(pageIndex > 0);
    m_pageNextBtn->setEnabled(pageIndex < pageCount - 1);
    const bool deletablePage = pageCount > 1;
    m_pageDeleteBtn->setEnabled(deletablePage);
    if (m_pageDeleteBtn->isVisible() != deletablePage) {
        m_pageDeleteBtn->setVisible(deletablePage);
        if (m_pagePill->isVisible())
            positionChildren(); // recalcula tamaño, posición y máscara
    }

    m_updatingUi = false;

    updateZoomLabel();
}

void FloatingToolbar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionChildren();
}

bool FloatingToolbar::eventFilter(QObject* watched, QEvent* event) {
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
