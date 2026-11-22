#include "ui/FloatingToolbar.h"

#include "ui/CanvasWidget.h"

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
#include <QLabel>
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
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

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

    if (kind == "undo") {
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
    }
    return pixmap;
}

QIcon glyphIcon(const QString& kind, const QColor& color) {
    return QIcon(glyphPixmap(kind, color));
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
    separator->setFixedSize(1, 22);
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
        setFixedSize(24, 24);
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
        // Punto interior de color con rim sutil de 1px.
        painter.setPen(QPen(m_rim, 1.0));
        painter.setBrush(m_color);
        painter.drawEllipse(center, 8.5, 8.5);
        // Anillo exterior de acento de 2px con gap de 2px al activarse.
        if (isChecked()) {
            QPen ring(m_accent, 2.0);
            painter.setPen(ring);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, 10.5, 10.5);
        }
    }

private:
    QColor m_color{Qt::white};
    QColor m_accent{"#60a5fa"};
    QColor m_rim{0, 0, 0, 60};
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

    buildPill();
    buildZoomPill();
    buildPagePill();
    buildRestoreButton();
    buildStrokePopover();
    buildPaperPopover();
    buildShapesPopover();
    buildFilePopover();
    buildContextPill();

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

void FloatingToolbar::buildPill() {
    m_pill = new GlassCapsule(this);
    m_pill->setObjectName("finilyPill");
    auto* row = new QHBoxLayout(m_pill);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(4);

    // Deshacer / Rehacer
    m_undoBtn = new QToolButton(m_pill);
    m_undoBtn->setFixedSize(34, 34);
    m_undoBtn->setIconSize(QSize(20, 20));
    m_undoBtn->setToolTip("Deshacer (Ctrl+Z)");
    m_undoBtn->setStatusTip(m_undoBtn->toolTip());
    polishButton(m_undoBtn);
    connect(m_undoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->undo();
        refreshFromCanvas();
    });
    m_iconSpecs.append(qMakePair(m_undoBtn, QString("undo")));
    row->addWidget(m_undoBtn);

    m_redoBtn = new QToolButton(m_pill);
    m_redoBtn->setFixedSize(34, 34);
    m_redoBtn->setIconSize(QSize(20, 20));
    m_redoBtn->setToolTip("Rehacer (Ctrl+Mayús+Z)");
    polishButton(m_redoBtn);
    connect(m_redoBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->redo();
        refreshFromCanvas();
    });
    m_iconSpecs.append(qMakePair(m_redoBtn, QString("redo")));
    row->addWidget(m_redoBtn);

    row->addWidget(makeSeparator(m_pill));

    // Herramientas: Pluma, Bolígrafo, Resaltador, Texto, Figuras, Polilínea,
    // Lazo, Láser, Borrador, Mano
    const QVector<QPair<ToolType, QString>> tools = {
        {ToolType::Pen, "Pluma"},
        {ToolType::Ballpoint, "Bolígrafo"},
        {ToolType::Highlighter, "Resaltador"},
        {ToolType::Text, "Texto"},
        {ToolType::Shape, "Figuras"},
        {ToolType::Polyline, "Polilínea"},
        {ToolType::Lasso, "Lazo"},
        {ToolType::Laser, "Láser"},
        {ToolType::Eraser, "Borrador"},
        {ToolType::Hand, "Mano"},
    };
    const QVector<QString> iconKinds = {"pen",       "ballpoint", "highlighter", "text", "shapes",
                                        "polyline",  "lasso",     "laser",       "eraser", "hand"};
    const QVector<QString> toolTips = {
        "Pluma fuente (P) — presión, velocidad e inclinación",
        "Bolígrafo gel (B) — trazo limpio para números y fórmulas",
        "Resaltador inteligente (H) — pinta debajo de la tinta",
        "Texto con teclado (T) — clic para escribir, doble clic para editar",
        "Figuras (S) — línea, círculo, elipse, triángulo, rectángulo, curva y raíz √",
        "Polilínea continua (L) — segmento tras segmento; doble clic o Enter para terminar",
        "Lazo de selección (A) — mover, escalar, rotar y enderezar letra",
        "Puntero láser temporal (K) — señala en vivo sin ensuciar la pizarra",
        "Borrador (E)",
        "Mano",
    };
    for (int i = 0; i < tools.size(); ++i) {
        auto* button = new QToolButton(m_pill);
        button->setFixedSize(36, 36);
        button->setIconSize(QSize(20, 20));
        button->setCheckable(true);
        button->setToolTip(toolTips.at(i));
        polishButton(button);
        const ToolType tool = tools.at(i).first;
        if (tool == ToolType::Shape) {
            // 'Figuras' activa la herramienta; si ya está activa, alterna el popover.
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
                    selectTool(ToolType::Shape);
                }
            });
        } else {
            connect(button, &QToolButton::clicked, this, [this, tool] { selectTool(tool); });
        }
        m_toolKinds.append(tool);
        m_toolBtns.append(button);
        m_iconSpecs.append(qMakePair(button, iconKinds.at(i)));
        row->addWidget(button);
    }

    row->addWidget(makeSeparator(m_pill));

    // 5 puntos favoritos de tinta (presets 1..5): ColorDotButton antialias
    // pintado a mano (círculo perfecto + anillo de acento, sin QSS ni recortes).
    for (int i = 0; i < 5; ++i) {
        auto* dot = new ColorDotButton(m_pill);
        polishButton(dot);
        connect(dot, &QToolButton::clicked, this, [this, i] { selectPreset(i); });
        m_presetDots.append(dot);
        row->addWidget(dot, 0, Qt::AlignVCenter);
    }

    row->addWidget(makeSeparator(m_pill));

    // Popovers
    m_strokeBtn = new QToolButton(m_pill);
    m_strokeBtn->setFixedHeight(34);
    m_strokeBtn->setIconSize(QSize(18, 18));
    m_strokeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_strokeBtn->setText("Trazo");
    m_strokeBtn->setToolTip("Propiedades del trazo");
    m_strokeBtn->setContentsMargins(8, 0, 8, 0);
    polishButton(m_strokeBtn);
    connect(m_strokeBtn, &QToolButton::clicked, this, [this] { openPopover(m_strokePopover, m_strokeBtn); });
    m_iconSpecs.append(qMakePair(m_strokeBtn, QString("stroke")));
    row->addWidget(m_strokeBtn);

    m_paperBtn = new QToolButton(m_pill);
    m_paperBtn->setFixedHeight(34);
    m_paperBtn->setIconSize(QSize(18, 18));
    m_paperBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_paperBtn->setText("Papel");
    m_paperBtn->setToolTip("Papel y pizarra");
    m_paperBtn->setContentsMargins(8, 0, 8, 0);
    polishButton(m_paperBtn);
    connect(m_paperBtn, &QToolButton::clicked, this, [this] { openPopover(m_paperPopover, m_paperBtn); });
    m_iconSpecs.append(qMakePair(m_paperBtn, QString("paper")));
    row->addWidget(m_paperBtn);

    // Popover "Archivo / PDF"
    m_fileBtn = new QToolButton(m_pill);
    m_fileBtn->setFixedHeight(34);
    m_fileBtn->setIconSize(QSize(18, 18));
    m_fileBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fileBtn->setText("Archivo");
    m_fileBtn->setToolTip("Archivo — cuaderno .finily, importar PDF/diapos, pegar imagen, exportar PDF");
    m_fileBtn->setContentsMargins(8, 0, 8, 0);
    polishButton(m_fileBtn);
    connect(m_fileBtn, &QToolButton::clicked, this, [this] { openPopover(m_filePopover, m_fileBtn); });
    m_iconSpecs.append(qMakePair(m_fileBtn, QString("file")));
    row->addWidget(m_fileBtn);

    row->addWidget(makeSeparator(m_pill));

    // Modo interfaz limpia (Tab)
    m_cleanBtn = new QToolButton(m_pill);
    m_cleanBtn->setFixedSize(36, 36);
    m_cleanBtn->setIconSize(QSize(20, 20));
    m_cleanBtn->setToolTip("Ocultar interfaz (Tab)");
    polishButton(m_cleanBtn);
    connect(m_cleanBtn, &QToolButton::clicked, this, &FloatingToolbar::toggleCleanUi);
    m_iconSpecs.append(qMakePair(m_cleanBtn, QString("clean")));
    row->addWidget(m_cleanBtn);
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
    m_pagePrevBtn->setText("◀");
    m_pagePrevBtn->setToolTip("Página anterior (Re Pág)");
    polishButton(m_pagePrevBtn);
    connect(m_pagePrevBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->prevPage();
        refreshFromCanvas();
    });
    row->addWidget(m_pagePrevBtn);

    m_pageLabel = new QLabel("Pág 1 / 1", m_pagePill);
    m_pageLabel->setObjectName("finilyPageLabel");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setContentsMargins(4, 0, 4, 0);
    m_pageLabel->setFixedHeight(30);
    row->addWidget(m_pageLabel);

    m_pageNextBtn = new QToolButton(m_pagePill);
    m_pageNextBtn->setFixedSize(30, 30);
    m_pageNextBtn->setText("▶");
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
    m_pageAddBtn->setText("+");
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
        if (m_canvas)
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

    styleColumn->addWidget(makeSectionTitle("Color", m_strokeStyleSection));

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
    m_sizeLabel = new QLabel("Grosor del trazo: 2.6 px", m_strokeStyleSection);
    sizeRow->addWidget(m_sizeLabel);
    sizeRow->addStretch(1);
    m_strokePreview = new StrokePreviewWidget(m_strokeStyleSection);
    sizeRow->addWidget(m_strokePreview);
    styleColumn->addLayout(sizeRow);

    // 3 grosores rápidos en 1 toque (Fino / Medio / Grueso)
    auto* quickSizeRow = new QHBoxLayout();
    quickSizeRow->setSpacing(4);
    const QVector<QPair<QString, double>> quickSizes = {{"Fino", 1.8}, {"Medio", 2.6}, {"Grueso", 4.5}};
    for (const auto& qs : quickSizes) {
        auto* btn = new QToolButton(m_strokeStyleSection);
        btn->setFixedSize(86, 26);
        btn->setText(qs.first);
        btn->setToolTip(QString("%1 (%2 px)").arg(qs.first).arg(qs.second, 0, 'f', 1));
        polishButton(btn);
        const double targetSize = qs.second;
        connect(btn, &QToolButton::clicked, this, [this, targetSize] {
            if (!m_canvas)
                return;
            m_canvas->setStrokeSize(targetSize);
            syncActivePresetFromCanvas();
            updatePresetDots();
            refreshFromCanvas();
        });
        quickSizeRow->addWidget(btn);
    }
    quickSizeRow->addStretch(1);
    styleColumn->addLayout(quickSizeRow);

    m_sizeSlider = new QSlider(Qt::Horizontal, m_strokeStyleSection);
    m_sizeSlider->setRange(10, 200); // 1.0 .. 20.0 px, en décimas
    m_sizeSlider->setCursor(Qt::PointingHandCursor);
    m_sizeSlider->setFocusPolicy(Qt::NoFocus);
    styleColumn->addWidget(m_sizeSlider);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStrokeSize(value / 10.0);
        m_sizeLabel->setText(QString("Grosor del trazo: %1 px").arg(value / 10.0, 0, 'f', 1));
        updateStrokePreview();
        syncActivePresetFromCanvas();
        updatePresetDots();
    });

    styleColumn->addWidget(makeSectionTitle("Estilo de línea", m_strokeStyleSection));
    auto* patternRow = new QHBoxLayout();
    patternRow->setSpacing(4);
    const QVector<QString> patternNames = {"Sólida", "Guiones", "Punteada", "Flecha", "Doble flecha"};
    const QVector<LinePattern> patterns = {LinePattern::Solid, LinePattern::Dashed, LinePattern::Dotted,
                                           LinePattern::Arrow, LinePattern::DoubleArrow};
    for (int i = 0; i < patterns.size(); ++i) {
        auto* button = new QToolButton(m_strokeStyleSection);
        button->setFixedSize(48, 28);
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
    auto* stabilizerColumn = new QVBoxLayout(m_stabilizerRow);
    stabilizerColumn->setContentsMargins(0, 0, 0, 0);
    stabilizerColumn->setSpacing(8);
    m_stabilizerLabel = new QLabel("Estabilizador: 15 %", m_stabilizerRow);
    stabilizerColumn->addWidget(m_stabilizerLabel);
    m_stabilizerSlider = new QSlider(Qt::Horizontal, m_stabilizerRow);
    m_stabilizerSlider->setRange(0, 100);
    m_stabilizerSlider->setCursor(Qt::PointingHandCursor);
    m_stabilizerSlider->setFocusPolicy(Qt::NoFocus);
    stabilizerColumn->addWidget(m_stabilizerSlider);
    connect(m_stabilizerSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updatingUi || !m_canvas)
            return;
        m_canvas->setStabilizer(value / 100.0);
        m_stabilizerLabel->setText(QString("Estabilizador: %1 %").arg(value));
    });
    column->addWidget(m_stabilizerRow);

    // --- Presión Wacom (perfil de respuesta de la tableta) ---
    auto* pressureSection = new QWidget(m_strokePopover);
    auto* pressureColumn = new QVBoxLayout(pressureSection);
    pressureColumn->setContentsMargins(0, 0, 0, 0);
    pressureColumn->setSpacing(8);
    pressureColumn->addWidget(makeSectionTitle("Presión Wacom", pressureSection));

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

    auto* swapBtn = new QToolButton(pressureSection);
    swapBtn->setFixedSize(272, 28);
    swapBtn->setText("Invertir botones del lápiz Wacom");
    swapBtn->setToolTip("Botón grande = Borrar · Botón pequeño = Escribir (pulsa para invertir si vienen al revés)");
    swapBtn->setCheckable(true);
    swapBtn->setChecked(m_canvas && m_canvas->swapPenButtons());
    polishButton(swapBtn);
    connect(swapBtn, &QToolButton::clicked, this, [this, swapBtn] {
        if (m_canvas)
            m_canvas->setSwapPenButtons(swapBtn->isChecked());
    });
    pressureColumn->addWidget(swapBtn);
    column->addWidget(pressureSection);

    // --- Borrador (visible solo con el borrador activo) ---
    m_eraserSection = new QWidget(m_strokePopover);
    auto* eraserColumn = new QVBoxLayout(m_eraserSection);
    eraserColumn->setContentsMargins(0, 0, 0, 0);
    eraserColumn->setSpacing(8);
    eraserColumn->addWidget(makeSectionTitle("Modo de borrado", m_eraserSection));

    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    m_eraserStrokeBtn = new QToolButton(m_eraserSection);
    m_eraserStrokeBtn->setFixedSize(133, 30);
    m_eraserStrokeBtn->setText("Por trazo");
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

    m_eraserSizeLabel = new QLabel("Tamaño del borrador: 18 px", m_eraserSection);
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
        m_eraserSizeLabel->setText(QString("Tamaño del borrador: %1 px").arg(value));
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

    column->addWidget(makeSectionTitle("Tema", m_paperPopover));
    auto* themeGrid = new QGridLayout();
    themeGrid->setHorizontalSpacing(6);
    themeGrid->setVerticalSpacing(6);
    const QVector<ThemeId> themes = {ThemeId::DarkBoard, ThemeId::GreenBoard, ThemeId::PaperLight, ThemeId::PaperCream};
    for (int i = 0; i < themes.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(133, 30);
        button->setText(getThemeConfig(themes.at(i)).name);
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
        themeGrid->addWidget(button, i / 2, i % 2);
    }
    column->addLayout(themeGrid);

    column->addWidget(makeSectionTitle("Plantilla", m_paperPopover));
    auto* templateRow = new QHBoxLayout();
    templateRow->setSpacing(4);
    const QVector<QString> templateNames = {"Liso", "Cuadrícula", "Puntos", "Rayado"};
    const QVector<BackgroundTemplate> templates = {BackgroundTemplate::Blank, BackgroundTemplate::Grid,
                                                   BackgroundTemplate::Dots, BackgroundTemplate::Ruled};
    for (int i = 0; i < templates.size(); ++i) {
        auto* button = new QToolButton(m_paperPopover);
        button->setFixedSize(62, 40);
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

    // Posición acoplable de la barra: toggle segmentado de 2 botones,
    // persistido en QSettings("Finily", "Finily").
    column->addWidget(makeSectionTitle("Posición de barra", m_paperPopover));
    auto* dockRow = new QHBoxLayout();
    dockRow->setSpacing(4);
    m_dockTopBtn = new QToolButton(m_paperPopover);
    m_dockTopBtn->setFixedSize(133, 30);
    m_dockTopBtn->setText("Arriba");
    m_dockTopBtn->setToolTip("Ancla la barra en la parte superior");
    m_dockTopBtn->setCheckable(true);
    polishButton(m_dockTopBtn);
    connect(m_dockTopBtn, &QToolButton::clicked, this, [this] { setDockBottom(false); });
    dockRow->addWidget(m_dockTopBtn);
    m_dockBottomBtn = new QToolButton(m_paperPopover);
    m_dockBottomBtn->setFixedSize(133, 30);
    m_dockBottomBtn->setText("Abajo");
    m_dockBottomBtn->setToolTip("Ancla la barra en la parte inferior");
    m_dockBottomBtn->setCheckable(true);
    polishButton(m_dockBottomBtn);
    connect(m_dockBottomBtn, &QToolButton::clicked, this, [this] { setDockBottom(true); });
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

    column->addWidget(makeSectionTitle("Figuras geométricas", m_shapesPopover));

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    const QVector<QString> names = {"Línea",        "Círculo",    "Elipse",    "Triángulo", "Rectángulo",
                                    "Curva",        "Raíz √",     "Ángulo 90°", "Llave { }", "Ejes X/Y"};
    const QVector<QString> glyphs = {"shape-line", "shape-circle", "shape-ellipse", "shape-triangle",
                                     "shape-rect", "shape-curve",  "shape-radical", "shape-rightangle",
                                     "shape-brace", "shape-axes"};
    const QVector<QString> tips = {"Línea", "Círculo", "Elipse", "Triángulo", "Rectángulo",
                                   "Curva / arco suave editable", "Raíz cuadrada auto-ajustable (R)",
                                   "Marca de ángulo recto (90°)", "Llave editable para esquemas y sistemas",
                                   "Plano cartesiano con ejes X/Y"};
    const QVector<ShapeKind> kinds = {ShapeKind::Line, ShapeKind::Circle, ShapeKind::Ellipse,
                                      ShapeKind::Triangle, ShapeKind::Rectangle, ShapeKind::Curve,
                                      ShapeKind::Radical, ShapeKind::RightAngle, ShapeKind::Brace,
                                      ShapeKind::Axes};
    for (int i = 0; i < kinds.size(); ++i) {
        auto* button = new QToolButton(m_shapesPopover);
        button->setObjectName("finilyShapeBtn");
        button->setFixedSize(133, 36);
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setText(names.at(i));
        button->setToolTip(tips.at(i));
        button->setCheckable(true);
        polishButton(button);
        const ShapeKind kind = kinds.at(i);
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
        m_iconSpecs.append(qMakePair(button, glyphs.at(i)));
        grid->addWidget(button, i / 2, i % 2);
    }
    column->addLayout(grid);

    m_holdToShapeBtn = new QToolButton(m_shapesPopover);
    m_holdToShapeBtn->setFixedSize(272, 30);
    m_holdToShapeBtn->setText("Auto-forma al mantener");
    m_holdToShapeBtn->setToolTip("Mantén pulsado sobre el lienzo y el trazo se ajustará a la forma perfecta");
    m_holdToShapeBtn->setCheckable(true);
    polishButton(m_holdToShapeBtn);
    connect(m_holdToShapeBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setHoldToShapeEnabled(m_holdToShapeBtn->isChecked());
        refreshFromCanvas();
    });
    column->addWidget(m_holdToShapeBtn);

    m_smartGuidesBtn = new QToolButton(m_shapesPopover);
    m_smartGuidesBtn->setFixedSize(272, 30);
    m_smartGuidesBtn->setText("Líneas guía");
    m_smartGuidesBtn->setToolTip("Muestra guías inteligentes de alineación y ángulos al crear figuras");
    m_smartGuidesBtn->setCheckable(true);
    polishButton(m_smartGuidesBtn);
    connect(m_smartGuidesBtn, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->setSmartGuidesEnabled(m_smartGuidesBtn->isChecked());
        refreshFromCanvas();
    });
    column->addWidget(m_smartGuidesBtn);
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
    column->addWidget(makeSectionTitle("Cuaderno (.finily)", m_filePopover));

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
    column->addWidget(makeSectionTitle("PDF e Imágenes", m_filePopover));

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
    m_canvas->newDocument();
    refreshFromCanvas();
}

void FloatingToolbar::openNotebookDialog() {
    if (!m_canvas)
        return;
    closePopovers();
    const QString path = QFileDialog::getOpenFileName(window(), "Abrir cuaderno", QString(),
                                                      "Cuaderno Finily (*.finily)");
    if (path.isEmpty())
        return;
    m_canvas->loadFromFile(path);
    refreshFromCanvas();
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
#finilySep { background: %8; }
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
QLabel#finilyPageLabel { font-size: 11px; font-weight: 600; }
QToolButton#finilyShapeBtn { font-size: 11px; padding: 0px 5px; border-radius: 10px; }
QToolButton#finilyShapeBtn:hover { background: %5; }
QToolButton#finilyShapeBtn:checked { background: %6; border: 1px solid %9; }
QToolButton#finilyAccentBtn { background: %11; font-weight: 600; border: 1px solid %12; }
QToolButton, QPushButton {
    background: transparent;
    border: none;
    border-radius: 18px;
    color: %3;
    padding: 2px;
    font-size: 12px;
}
QToolButton:hover, QPushButton:hover { background: %5; }
QToolButton:pressed, QPushButton:pressed { background: %6; }
QToolButton:checked, QPushButton:checked {
    background: %7;
    border: 1px solid %9;
}
QToolButton:hover:checked, QPushButton:hover:checked { background: %13; }
QToolButton:disabled { color: %4; }
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
    updatePresetDots();

    // Recolorea TODAS las cápsulas de cristal al instante: DarkBoard <-> 
    // PaperLight cambia cada superficie de vidrio oscuro a blanco luminoso.
    for (GlassCapsule* capsule : m_capsules)
        if (capsule)
            capsule->setCapsuleTheme(theme);

    refreshShadowColors();
    disarmClearButton();
}

void FloatingToolbar::refreshIcons() {
    const ThemeConfig theme = getThemeConfig(m_theme);
    for (const auto& spec : m_iconSpecs)
        spec.first->setIcon(glyphIcon(spec.second, theme.uiText));
    for (const auto& spec : m_patternSpecs)
        spec.first->setIcon(QIcon(patternPixmap(spec.second, theme.uiText)));
    for (const auto& spec : m_templateSpecs)
        spec.first->setIcon(QIcon(templatePixmap(spec.second, theme.uiText)));
}

void FloatingToolbar::positionChildren() {
    if (m_pill && m_pill->isVisible()) {
        m_pill->adjustSize();
        const int x = std::max(6, (width() - m_pill->width()) / 2);
        const int y = m_toolbarBottom ? height() - m_pill->height() - 14 : 12;
        m_pill->move(x, y);
    }
    if (m_zoomPill && m_zoomPill->isVisible()) {
        m_zoomPill->adjustSize();
        m_zoomPill->move(width() - m_zoomPill->width() - 14, height() - m_zoomPill->height() - 14);
    }
    if (m_pagePill && m_pagePill->isVisible()) {
        m_pagePill->adjustSize();
        m_pagePill->move(14, height() - m_pagePill->height() - 14);
    }
    if (m_restoreBtn && m_restoreBtn->isVisible())
        m_restoreBtn->move(width() - 34, 10);
    if (m_contextPill && m_contextPill->isVisible()) {
        m_contextPill->adjustSize();
        const int x = std::max(6, (width() - m_contextPill->width()) / 2);
        int y;
        if (m_pill && m_pill->isVisible()) {
            y = m_toolbarBottom ? m_pill->geometry().top() - m_contextPill->height() - 6
                                : m_pill->geometry().bottom() + 6;
        } else {
            y = m_toolbarBottom ? height() - m_contextPill->height() - 12 : 12;
        }
        m_contextPill->move(x, y);
    }
    if (m_toastPill && m_toastPill->isVisible()) {
        m_toastPill->adjustSize();
        // Barra arriba: toast abajo-centrado; barra abajo: toast arriba-centrado.
        const int y = m_toolbarBottom ? 18 : height() - m_toastPill->height() - 64;
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
    const QVector<QWidget*> overlayChildren = {m_pill,          m_contextPill, m_zoomPill,   m_restoreBtn,
                                               m_strokePopover, m_paperPopover, m_shapesPopover, m_filePopover,
                                               m_pagePill,      m_toastPill};
    for (QWidget* child : overlayChildren) {
        if (!child || !child->isVisible())
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
    const int anchorCenter = anchor->mapTo(this, QPoint(0, 0)).x() + anchor->width() / 2;
    int x = anchorCenter - popover->width() / 2;
    x = std::clamp(x, margin, std::max(margin, width() - popover->width() - margin));
    int y;
    if (m_toolbarBottom) // acoplada abajo: el popover se abre limpio ENCIMA
        y = anchor->mapTo(this, QPoint(0, 0)).y() - popover->height() - 8;
    else
        y = anchor->mapTo(this, QPoint(0, anchor->height())).y() + 8;
    y = std::clamp(y, margin, std::max(margin, height() - popover->height() - margin));
    const QPoint finalPos(x, y);
    const QPoint startPos(x, m_toolbarBottom ? y - 8 : y + 8); // desliza hacia la pill
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
    effect->setBlurRadius(22);
    effect->setOffset(0.0, 6.0);
    effect->setColor(QColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 70 : 35));
    widget->setGraphicsEffect(effect);
}

// Recolorea las sombras al cambiar de tema sin recrear efectos.
void FloatingToolbar::refreshShadowColors() {
    const QColor shadowColor(0, 0, 0, getThemeConfig(m_theme).isDark ? 70 : 35);
    const QVector<QWidget*> elevated = {m_pill,          m_contextPill,   m_zoomPill,      m_pagePill,
                                        m_strokePopover, m_paperPopover,   m_shapesPopover, m_filePopover};
    for (QWidget* widget : elevated) {
        if (!widget)
            continue;
        if (auto* effect = qobject_cast<QGraphicsDropShadowEffect*>(widget->graphicsEffect()))
            effect->setColor(shadowColor);
    }
}

// Restaura la posición acoplable persistida ("toolbar/dockBottom").
void FloatingToolbar::loadDockPosition() {
    QSettings settings("Finily", "Finily");
    m_toolbarBottom = settings.value("toolbar/dockBottom", false).toBool();
}

// Alterna "Barra arriba" / "Barra abajo": reancla pill, popovers (que abren
// encima cuando está abajo), contextPill y toast; persiste y reabre el
// popover activo en su nueva posición.
void FloatingToolbar::setDockBottom(bool bottom) {
    if (m_toolbarBottom == bottom)
        return;
    m_toolbarBottom = bottom;
    QSettings settings("Finily", "Finily");
    settings.setValue("toolbar/dockBottom", bottom);

    GlassCapsule* open = m_openPopover;
    const QWidget* anchor = nullptr;
    if (open == m_strokePopover)
        anchor = m_strokeBtn;
    else if (open == m_paperPopover)
        anchor = m_paperBtn;
    else if (open == m_shapesPopover)
        anchor = m_shapeBtn;
    else if (open == m_filePopover)
        anchor = m_fileBtn;
    closePopovers();
    positionChildren();
    refreshFromCanvas(); // sincroniza los botones Arriba/Abajo
    if (open && anchor) {
        if (open == m_strokePopover)
            openPopover(m_strokePopover, m_strokeBtn);
        else if (open == m_paperPopover)
            openPopover(m_paperPopover, m_paperBtn);
        else if (open == m_shapesPopover)
            openPopover(m_shapesPopover, m_shapeBtn);
        else if (open == m_filePopover)
            openPopover(m_filePopover, m_fileBtn);
    }
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
    updatePresetDots();
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

void FloatingToolbar::updatePresetDots() {
    // ColorDotButton antialias: punto de color real + rim sutil de 1px y
    // anillo exterior de acento de 2px con gap de 2px al activarse. El rim
    // contrasta con el tema (claro -> tinta oscura; oscuro -> luz).
    const ThemeConfig theme = getThemeConfig(m_theme);
    const QColor accent = theme.accent;
    const QColor rim = theme.isDark ? QColor(255, 255, 255, 70) : QColor(15, 23, 42, 80);

    for (int i = 0; i < m_presetDots.size() && i < m_presets.size(); ++i) {
        ColorDotButton* dot = m_presetDots.at(i);
        dot->setDotColor(m_presets.at(i).color);
        dot->setAccentColor(accent);
        dot->setRimColor(rim);
        dot->setToolTip(QString("%1 (%2)").arg(m_presets.at(i).label).arg(i + 1));
    }
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i) {
        ColorDotButton* swatch = m_swatches.at(i);
        swatch->setDotColor(m_palette.at(i));
        swatch->setAccentColor(accent);
        swatch->setRimColor(rim);
        swatch->setToolTip(m_palette.at(i).name());
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
        if (m_activePreset == 0 && m_canvas->tool() != ToolType::Eraser)
            m_canvas->setInkColor(m_presets.at(0).color);
    }

    syncActivePresetFromCanvas();

    // Toggle "Posición de barra" (estado propio del toolbar, fuera de la
    // huella del lienzo para que siempre quede sincronizado).
    if (m_dockTopBtn && m_dockBottomBtn) {
        m_dockTopBtn->setChecked(!m_toolbarBottom);
        m_dockBottomBtn->setChecked(m_toolbarBottom);
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

    // Herramientas
    const ToolType tool = m_canvas->tool();
    for (int i = 0; i < m_toolBtns.size() && i < m_toolKinds.size(); ++i)
        m_toolBtns.at(i)->setChecked(m_toolKinds.at(i) == tool);
    for (int i = 0; i < m_presetDots.size(); ++i)
        m_presetDots.at(i)->setChecked(i == m_activePreset);
    updatePresetDots();

    // Popover "Trazo"
    const int sizeTenths = qRound(m_canvas->strokeSize() * 10.0);
    if (m_sizeSlider->value() != sizeTenths)
        m_sizeSlider->setValue(sizeTenths);
    m_sizeLabel->setText(QString("Grosor del trazo: %1 px").arg(m_canvas->strokeSize(), 0, 'f', 1));
    updateStrokePreview();
    const LinePattern pattern = m_canvas->linePattern();
    for (int i = 0; i < m_patternSpecs.size(); ++i)
        m_patternBtns.at(i)->setChecked(m_patternSpecs.at(i).second == pattern);

    const int stabilizerPercent = qRound(m_canvas->stabilizer() * 100.0);
    if (m_stabilizerSlider->value() != stabilizerPercent)
        m_stabilizerSlider->setValue(stabilizerPercent);
    m_stabilizerLabel->setText(QString("Estabilizador: %1 %").arg(stabilizerPercent));

    // Perfil de presión Wacom
    const PressureProfile profile = m_canvas->pressureProfile();
    for (int i = 0; i < m_pressureBtns.size() && i < m_pressureSpecs.size(); ++i)
        m_pressureBtns.at(i)->setChecked(m_pressureSpecs.at(i) == profile);

    const QColor ink = m_canvas->inkColor();
    for (int i = 0; i < m_swatches.size() && i < m_palette.size(); ++i)
        m_swatches.at(i)->setChecked(m_palette.at(i) == ink);

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
    m_eraserSizeLabel->setText(QString("Tamaño del borrador: %1 px").arg(eraserSize));

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
    m_pageLabel->setText(QString("Pág %1 / %2").arg(pageIndex + 1).arg(pageCount));
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
