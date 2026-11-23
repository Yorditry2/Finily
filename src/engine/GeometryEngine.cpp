#include "GeometryEngine.h"
#include "RegionGeometry.h"
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace finily {
namespace {
double length(QPointF p) { return std::hypot(p.x(),p.y()); }
double dot(QPointF a,QPointF b) { return a.x()*b.x()+a.y()*b.y(); }
QPointF projectSegment(QPointF p,QPointF a,QPointF b) {
    const auto d=b-a;const double d2=dot(d,d);
    return a+d*(d2>1e-12?std::clamp(dot(p-a,d)/d2,0.0,1.0):0.0);
}
double segmentDistance(QPointF p,QPointF a,QPointF b) {
    return length(p-projectSegment(p,a,b));
}
QRectF vertexBounds(const QVector<QPointF>& vertices) {
    if(vertices.isEmpty())return {};
    double l=vertices[0].x(),r=l,t=vertices[0].y(),b=t;
    for(auto p:vertices){l=std::min(l,p.x());r=std::max(r,p.x());t=std::min(t,p.y());b=std::max(b,p.y());}
    return {QPointF(l,t),QPointF(r,b)};
}
QVector<QPointF> radicalSpine(const ShapeItem& shape) {
    if(shape.vertices.size()<3)return {};
    const auto base=shape.vertices[0],roof=shape.vertices[1];
    const double h=std::max(1.0,std::abs(base.y()-roof.y()));
    const double direction=base.y()>=roof.y()?1.0:-1.0;
    const QPointF tick=base+QPointF(-h*.23,-direction*h*.36);
    return {tick+QPointF(-h*.13,direction*h*.08),tick,base,roof,QPointF(shape.vertices[2].x(),roof.y())};
}
struct ArcGeometry {
    QPointF center;
    double radius;
    double startDegrees;
    double sweepDegrees;

    QRectF circle() const {
        return {center - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)};
    }

    QPointF midpoint(double offset = 0) const {
        const double direction = (startDegrees + sweepDegrees / 2) * std::numbers::pi / 180;
        return center + (radius + offset) * QPointF(std::cos(direction), -std::sin(direction));
    }
};

std::optional<ArcGeometry> arcGeometry(const ShapeItem& shape) {
    if (shape.vertices.size() < 3)
        return {};
    const auto center = shape.vertices[0];
    const QPointF first = shape.vertices[1] - center, second = shape.vertices[2] - center;
    const double radius = length(first);
    const double otherLength = length(second);
    if (radius <= 1e-8 || otherLength <= 1e-8)
        return {};
    const double start = std::atan2(-first.y(), first.x()) * 180 / std::numbers::pi;
    const double end = std::atan2(-second.y(), second.x()) * 180 / std::numbers::pi;
    if (shape.kind == ShapeKind::AngleMarker)
        return ArcGeometry{center, .32 * std::min(radius, otherLength), start, std::remainder(end - start, 360)};
    const double sweep = std::fmod(end - start + 360, 360);
    return ArcGeometry{center, radius, start, sweep};
}

QFont measurementFont(const ShapeItem& shape) {
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(std::clamp(qRound(12 + shape.size), 14, 28));
    return font;
}

struct ShapeMeasurement {
    QString text;
    QRectF bounds;
};

ShapeMeasurement measurement(const ShapeItem& shape) {
    const QFontMetricsF font(measurementFont(shape));
    QString text = shape.label;
    QPointF position;
    if (shape.kind == ShapeKind::AngleMarker) {
        const auto arc = arcGeometry(shape);
        if (!arc)
            return {};
        if (text.isEmpty()) {
            text = QString::number(std::abs(arc->sweepDegrees), 'f', 1);
            if (text.endsWith(QStringLiteral(".0")))
                text.chop(2);
            text.append(QChar(0x00b0));
        }
        position = arc->midpoint(font.height() * .65);
    } else if (shape.kind == ShapeKind::Line && shape.linePattern == LinePattern::Dimension
               && shape.vertices.size() >= 2 && !text.isEmpty()) {
        const auto delta = shape.vertices[1] - shape.vertices[0];
        const double span = length(delta);
        if (span <= 1e-8)
            return {};
        position = (shape.vertices[0] + shape.vertices[1]) / 2;
        if (font.horizontalAdvance(text) + 12 > span * .72)
            position += QPointF(delta.y(), -delta.x()) / span * font.height();
    } else {
        return {};
    }
    const QSizeF size(font.horizontalAdvance(text) + 4, font.height());
    return {text, QRectF(position - QPointF(size.width() / 2, size.height() / 2), size)};
}

void drawMeasurement(QPainter& painter, const ShapeItem& shape, const ShapeMeasurement& label) {
    if (label.text.isEmpty())
        return;
    painter.setFont(measurementFont(shape));
    painter.setPen(shape.color);
    painter.drawText(label.bounds, Qt::AlignCenter, label.text);
}

struct Solid3DGeometry {
    QPainterPath vis;
    QPainterPath hid;
    QVector<QPointF> vertices;
};

Solid3DGeometry buildSolid3D(const ShapeItem& shape) {
    Solid3DGeometry g;
    if (shape.vertices.size() < 2) return g;
    const QRectF r = QRectF(shape.vertices[0], shape.vertices[1]).normalized();
    if (r.width() <= 1e-4 || r.height() <= 1e-4) return g;
    auto seg = [](QPainterPath& p, QPointF a, QPointF b) { p.moveTo(a); p.lineTo(b); };
    const double x = r.left(), y = r.top(), w = r.width(), h = r.height();
    auto P = [&](double fx, double fy) { return QPointF(x + w * fx, y + h * fy); };
    switch (shape.solidKind) {
    case 0: { // Cubo / Hexaedro
        const QPointF f0=P(0.0,0.28),f1=P(0.72,0.28),f2=P(0.72,1.0),f3=P(0.0,1.0);
        const QPointF b0=P(0.28,0.0),b1=P(1.0,0.0),b2=P(1.0,0.72),b3=P(0.28,0.72);
        seg(g.vis,f0,f1);seg(g.vis,f1,f2);seg(g.vis,f2,f3);seg(g.vis,f3,f0);
        seg(g.vis,f0,b0);seg(g.vis,f1,b1);seg(g.vis,f2,b2);seg(g.vis,b0,b1);seg(g.vis,b1,b2);
        seg(g.hid,f3,b3);seg(g.hid,b3,b0);seg(g.hid,b3,b2);
        g.vertices = {f0,f1,f2,f3,b0,b1,b2,b3,r.center()};
        break;
    }
    case 1: { // Prisma triangular
        const QPointF t0=P(0.5,0.0),t1=P(0.08,0.28),t2=P(0.92,0.28);
        const QPointF b0=P(0.5,0.72),b1=P(0.08,1.0),b2=P(0.92,1.0);
        seg(g.vis,t0,t1);seg(g.vis,t1,t2);seg(g.vis,t2,t0);
        seg(g.vis,t1,b1);seg(g.vis,t2,b2);seg(g.vis,b1,b2);
        seg(g.hid,t0,b0);seg(g.hid,b0,b1);seg(g.hid,b0,b2);
        g.vertices = {t0,t1,t2,b0,b1,b2};
        break;
    }
    case 2: { // Cilindro
        const double ry=std::min(h*0.16,w*0.24);
        const QRectF topR(x,y,w,ry*2.0), botR(x,y+h-ry*2.0,w,ry*2.0);
        g.vis.addEllipse(topR);
        seg(g.vis,QPointF(x,y+ry),QPointF(x,y+h-ry));
        seg(g.vis,QPointF(x+w,y+ry),QPointF(x+w,y+h-ry));
        g.vis.arcMoveTo(botR,180); g.vis.arcTo(botR,180,180);
        g.hid.arcMoveTo(botR,0); g.hid.arcTo(botR,0,180);
        g.vertices = {topR.center(), botR.center(), QPointF(x,y+ry), QPointF(x+w,y+ry), QPointF(x,y+h-ry), QPointF(x+w,y+h-ry)};
        break;
    }
    case 3: { // Cono
        const double ry=std::min(h*0.16,w*0.24);
        const QRectF botR(x,y+h-ry*2.0,w,ry*2.0);
        const QPointF apex=P(0.5,0.0);
        seg(g.vis,apex,QPointF(x,y+h-ry));
        seg(g.vis,apex,QPointF(x+w,y+h-ry));
        g.vis.arcMoveTo(botR,180); g.vis.arcTo(botR,180,180);
        g.hid.arcMoveTo(botR,0); g.hid.arcTo(botR,0,180);
        g.vertices = {apex, botR.center(), QPointF(x,y+h-ry), QPointF(x+w,y+h-ry)};
        break;
    }
    case 4: { // Esfera
        const double d=std::min(w,h);
        const QRectF sph(r.center().x()-d/2.0,r.center().y()-d/2.0,d,d);
        const QRectF eq(sph.left(),sph.center().y()-d*0.18,d,d*0.36);
        g.vis.addEllipse(sph);
        g.vis.arcMoveTo(eq,180); g.vis.arcTo(eq,180,180);
        g.hid.arcMoveTo(eq,0); g.hid.arcTo(eq,0,180);
        g.vertices = {sph.center(), QPointF(sph.center().x(), sph.top()), QPointF(sph.center().x(), sph.bottom()),
                      QPointF(sph.left(), sph.center().y()), QPointF(sph.right(), sph.center().y())};
        break;
    }
    case 5: { // Pirámide cuadrangular
        const QPointF apex=P(0.5,0.0);
        const QPointF b0=P(0.0,0.82),b1=P(0.68,1.0),b2=P(1.0,0.72),b3=P(0.32,0.58);
        seg(g.vis,apex,b0);seg(g.vis,apex,b1);seg(g.vis,apex,b2);
        seg(g.vis,b0,b1);seg(g.vis,b1,b2);
        seg(g.hid,apex,b3);seg(g.hid,b0,b3);seg(g.hid,b2,b3);
        g.vertices = {apex,b0,b1,b2,b3,(b0+b1+b2+b3)*0.25};
        break;
    }
    case 6: { // Tetraedro
        const QPointF apex=P(0.46,0.0),b0=P(0.0,0.86),b1=P(0.66,1.0),b2=P(1.0,0.68);
        seg(g.vis,apex,b0);seg(g.vis,apex,b1);seg(g.vis,apex,b2);
        seg(g.vis,b0,b1);seg(g.vis,b1,b2);
        seg(g.hid,b0,b2);
        g.vertices = {apex,b0,b1,b2,(b0+b1+b2)/3.0};
        break;
    }
    case 7: { // Octaedro
        const QPointF top=P(0.5,0.0),bot=P(0.5,1.0);
        const QPointF m0=P(0.0,0.52),m1=P(0.44,0.64),m2=P(1.0,0.48),m3=P(0.56,0.36);
        seg(g.vis,top,m0);seg(g.vis,top,m1);seg(g.vis,top,m2);
        seg(g.vis,bot,m0);seg(g.vis,bot,m1);seg(g.vis,bot,m2);
        seg(g.vis,m0,m1);seg(g.vis,m1,m2);
        seg(g.hid,top,m3);seg(g.hid,bot,m3);seg(g.hid,m0,m3);seg(g.hid,m2,m3);
        g.vertices = {top,bot,m0,m1,m2,m3,r.center()};
        break;
    }
    case 8: { // Tronco de cono
        const double ryB=std::min(h*0.15,w*0.22), ryT=ryB*0.62;
        const double tw=w*0.56, tx=x+(w-tw)*0.5;
        const QRectF topR(tx,y,tw,ryT*2.0), botR(x,y+h-ryB*2.0,w,ryB*2.0);
        g.vis.addEllipse(topR);
        seg(g.vis,QPointF(tx,y+ryT),QPointF(x,y+h-ryB));
        seg(g.vis,QPointF(tx+tw,y+ryT),QPointF(x+w,y+h-ryB));
        g.vis.arcMoveTo(botR,180); g.vis.arcTo(botR,180,180);
        g.hid.arcMoveTo(botR,0); g.hid.arcTo(botR,0,180);
        g.vertices = {topR.center(), botR.center(), QPointF(tx,y+ryT), QPointF(tx+tw,y+ryT), QPointF(x,y+h-ryB), QPointF(x+w,y+h-ryB)};
        break;
    }
    default: { // 9: Tronco de pirámide
        const QPointF t0=P(0.20,0.14),t1=P(0.62,0.24),t2=P(0.80,0.08),t3=P(0.38,0.0);
        const QPointF b0=P(0.0,0.82),b1=P(0.68,1.0),b2=P(1.0,0.72),b3=P(0.32,0.58);
        seg(g.vis,t0,t1);seg(g.vis,t1,t2);seg(g.vis,t2,t3);seg(g.vis,t3,t0);
        seg(g.vis,t0,b0);seg(g.vis,t1,b1);seg(g.vis,t2,b2);
        seg(g.vis,b0,b1);seg(g.vis,b1,b2);
        seg(g.hid,t3,b3);seg(g.hid,b0,b3);seg(g.hid,b2,b3);
        g.vertices = {t0,t1,t2,t3,b0,b1,b2,b3};
        break;
    }
    }
    return g;
}

QPainterPath shapePath(const ShapeItem& shape) {
    QPainterPath path;const auto& v=shape.vertices;
    if(v.isEmpty())return path;
    if(shape.kind==ShapeKind::ShadedRegion) {
        qsizetype start = 0;
        for (qsizetype contour = 0; contour <= shape.shadeContourStarts.size(); ++contour) {
            const qsizetype end = contour < shape.shadeContourStarts.size() ? shape.shadeContourStarts[contour] : v.size();
            path.moveTo(v[start]);
            for (qsizetype i = start + 1; i + 2 < end; i += 3) {
                if (v[i] == path.currentPosition() && v[i + 1] == v[i + 2])
                    path.lineTo(v[i + 2]);
                else
                    path.cubicTo(v[i], v[i + 1], v[i + 2]);
            }
            path.closeSubpath();
            start = end;
        }
        return path;
    }
    if(shape.kind==ShapeKind::Circle&&v.size()>=2) {
        const double r=length(v[1]-v[0]);path.addEllipse(v[0],r,r);return path;
    }
    if((shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle)&&v.size()>=2) {
        const QRectF rect=QRectF(v[0],v[1]).normalized();
        if(shape.kind==ShapeKind::Ellipse)path.addEllipse(rect);else path.addRect(rect);
        return path;
    }
    if (shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector || shape.kind == ShapeKind::AngleMarker) {
        if (const auto arc = arcGeometry(shape)) {
            if (shape.kind == ShapeKind::Sector)
                path.moveTo(arc->center);
            else
                path.arcMoveTo(arc->circle(), arc->startDegrees);
            path.arcTo(arc->circle(), arc->startDegrees, arc->sweepDegrees);
            if (shape.kind == ShapeKind::Sector)
                path.closeSubpath();
            else if (shape.kind == ShapeKind::AngleMarker) {
                path.moveTo(v[1]);
                path.lineTo(v[0]);
                path.lineTo(v[2]);
            }
        }
        return path;
    }
    if(shape.kind==ShapeKind::RightAngle&&v.size()>=3) {
        const auto a=v[0]-v[1],b=v[2]-v[1];const double la=length(a),lb=length(b);
        if(la<1e-8||lb<1e-8)return path;
        const double side=std::min(22.0,.35*std::min(la,lb));const auto u=a/la*side,w=b/lb*side;
        path.moveTo(v[1]+u);path.lineTo(v[1]+u+w);path.lineTo(v[1]+w);return path;
    }
    if(shape.kind==ShapeKind::Brace&&v.size()>=2) {
        const auto delta=v[1]-v[0];const double span=length(delta);if(span<1e-8)return path;
        const auto axis=delta/span;const QPointF normal(-axis.y(),axis.x());const double depth=std::min(28.0,span*.13);
        auto point=[&](double along,double across){return v[0]+delta*along+normal*(depth*across);};
        path.moveTo(v[0]);
        path.cubicTo(point(0,.5),point(.08,.5),point(.25,.5));
        path.cubicTo(point(.42,.5),point(.45,.5),point(.5,1));
        path.cubicTo(point(.55,.5),point(.58,.5),point(.75,.5));
        path.cubicTo(point(.92,.5),point(1,.5),v[1]);return path;
    }
    if(shape.kind==ShapeKind::Axes&&v.size()>=2) {
        const auto rect=QRectF(v[0],v[1]).normalized();const auto center=rect.center();
        path.moveTo(rect.left(),center.y());path.lineTo(rect.right(),center.y());
        path.moveTo(center.x(),rect.bottom());path.lineTo(center.x(),rect.top());
        const double extent=std::max(rect.width(),rect.height());if(extent<1e-8)return path;
        const double target=std::max(std::min(rect.width(),rect.height())/8,extent/100);
        const double magnitude=std::pow(10.0,std::floor(std::log10(target)));const double ratio=target/magnitude;
        const double spacing=magnitude*(ratio<=1?1:ratio<=2?2:ratio<=5?5:10);
        const double tick=std::min(3.5,std::min(rect.width(),rect.height())*.025);
        const double margin=std::min(12.0,std::min(rect.width(),rect.height())*.15);
        for(double x=spacing;x<rect.width()/2-margin;x+=spacing)for(double direction:{-1.0,1.0}) {
            path.moveTo(center.x()+direction*x,center.y()-tick);path.lineTo(center.x()+direction*x,center.y()+tick);
        }
        for(double y=spacing;y<rect.height()/2-margin;y+=spacing)for(double direction:{-1.0,1.0}) {
            path.moveTo(center.x()-tick,center.y()+direction*y);path.lineTo(center.x()+tick,center.y()+direction*y);
        }
        return path;
    }
    if(shape.kind==ShapeKind::Solid3D&&v.size()>=2) {
        const auto g = buildSolid3D(shape);
        path.addPath(g.vis);
        path.addPath(g.hid);
        return path;
    }
    if(shape.kind==ShapeKind::Curve&&v.size()>=3) {
        // v[1] lies directly ON the curve at t=0.5; derive quadratic Bezier control point.
        const QPointF ctrl = 2.0 * v[1] - 0.5 * (v[0] + v[2]);
        path.moveTo(v[0]);
        path.quadTo(ctrl, v[2]);
        return path;
    }
    const auto points=shape.kind==ShapeKind::Radical?radicalSpine(shape):v;
    if(points.isEmpty())return path;
    path.moveTo(points[0]);for(qsizetype i=1;i<points.size();++i)path.lineTo(points[i]);
    if(shape.kind==ShapeKind::Triangle||(shape.kind==ShapeKind::Polyline&&shape.closed))path.closeSubpath();
    return path;
}
QPainterPath arrowPath(const ShapeItem& shape) {
    QPainterPath path;
    if(shape.kind==ShapeKind::Axes&&shape.vertices.size()>=2) {
        const auto rect=QRectF(shape.vertices[0],shape.vertices[1]).normalized();const auto center=rect.center();
        const double size=std::min(std::max(6.0,shape.size*3),std::min(rect.width(),rect.height())*.18);
        if(size<=0)return path;
        path.moveTo(rect.right(),center.y());path.lineTo(rect.right()-size,center.y()-size*.45);path.lineTo(rect.right()-size,center.y()+size*.45);path.closeSubpath();
        path.moveTo(center.x(),rect.top());path.lineTo(center.x()-size*.45,rect.top()+size);path.lineTo(center.x()+size*.45,rect.top()+size);path.closeSubpath();return path;
    }
    if (!supportsLineEnding(shape) || shape.vertices.size() < 2)
        return path;
    auto ending = shape.lineEnding;
    if (shape.linePattern == LinePattern::Arrow)
        ending = LineEnding::Arrow;
    else if (shape.linePattern == LinePattern::DoubleArrow)
        ending = LineEnding::DoubleArrow;
    if (ending == LineEnding::None)
        return path;
    auto head=[&](QPointF tip,QPointF previous) {
        auto axis=tip-previous;const double d=length(axis);if(d<1e-8)return;
        axis/=d;const QPointF normal(-axis.y(),axis.x());
        const double h=std::max(8.0,shape.size*4.0),w=h*.48;
        path.moveTo(tip);path.lineTo(tip-axis*h+normal*w);path.lineTo(tip-axis*h*.7);
        path.lineTo(tip-axis*h-normal*w);path.closeSubpath();
    };
    const auto& v=shape.vertices;
    if(shape.kind==ShapeKind::Curve&&v.size()>=3) {
        const QPointF ctrl = 2.0 * v[1] - 0.5 * (v[0] + v[2]);
        head(v[2], ctrl);
        if(ending==LineEnding::DoubleArrow) head(v[0], ctrl);
        return path;
    }
    qsizetype end=v.size()-2;
    while(end>=0&&length(v.back()-v[end])<1e-8)--end;
    if(end>=0)head(v.back(),v[end]);
    if(ending==LineEnding::DoubleArrow) {
        qsizetype start=1;while(start<v.size()&&length(v[start]-v.front())<1e-8)++start;
        if(start<v.size())head(v.front(),v[start]);
    }
    return path;
}
QRectF ovalBounds(const ShapeItem& shape) {
    if (shape.kind == ShapeKind::Circle) {
        const double r = length(shape.vertices[1] - shape.vertices[0]);
        return {shape.vertices[0] - QPointF(r, r), QSizeF(r * 2, r * 2)};
    }
    return QRectF(shape.vertices[0], shape.vertices[1]).normalized();
}

QVector<QPolygonF> shapeOutlines(const ShapeItem& shape) {
    if (shape.kind == ShapeKind::Axes) {
        const auto rect = QRectF(shape.vertices[0], shape.vertices[1]).normalized();
        const auto center = rect.center();
        // Graduations and arrowheads are annotations on the axes.
        return {QPolygonF{{rect.left(), center.y()}, {rect.right(), center.y()}},
                QPolygonF{{center.x(), rect.top()}, {center.x(), rect.bottom()}}};
    }
    if (shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse) {
        const auto oval = ovalBounds(shape);
        if (oval.isEmpty())
            return {};
        const double a = oval.width() * .5, b = oval.height() * .5;
        const double steps = std::numbers::pi * std::sqrt(std::max(a, b) / (contourJoinTolerance * .5));
        const int count = int(std::clamp(std::ceil(steps), 32.0, 16384.0));
        QPolygonF outline;
        outline.reserve(count + 1);
        for (int i = 0; i < count; ++i) {
            const double angle = 2 * std::numbers::pi * i / count;
            outline.append(oval.center() + QPointF(a * std::cos(angle), b * std::sin(angle)));
        }
        outline.append(outline.front());
        return {outline};
    }

    // Use the rendered centerline and flatten Béziers at a subpixel precision.
    constexpr double detail = 128.0;
    const auto origin = shape.vertices.front();
    QTransform scaled;
    scaled.scale(detail, detail);
    scaled.translate(-origin.x(), -origin.y());
    auto outlines = shapePath(shape).toSubpathPolygons(scaled);
    for (auto& outline : outlines)
        for (auto& p : outline)
            p = p / detail + origin;
    return outlines;
}

QVector<QPointF> snapTargets(const ShapeItem& shape) {
    const auto& v = shape.vertices;
    switch (shape.kind) {
    case ShapeKind::Line: return {v[0], v[1], (v[0] + v[1]) * .5};
    case ShapeKind::Circle: return v;
    case ShapeKind::Ellipse: return {ovalBounds(shape).center()};
    case ShapeKind::Rectangle: {
        auto corners = getShapeHandles(shape);
        corners.append(QRectF(v[0], v[1]).center());
        return corners;
    }
    case ShapeKind::Solid3D: return buildSolid3D(shape).vertices;
    case ShapeKind::Triangle:
    case ShapeKind::Polyline:
    case ShapeKind::Curve:
    case ShapeKind::Arc:
    case ShapeKind::Sector:
    case ShapeKind::AngleMarker:
    case ShapeKind::Brace: return v;
    default: return {};
    }
}

std::optional<QPointF> closestOutlinePoint(const ShapeItem& shape, QPointF raw) {
    if (shape.kind == ShapeKind::Circle) {
        const auto center = shape.vertices[0];
        const auto direction = raw - center;
        const double distance = length(direction);
        if (distance > 1e-8)
            return center + direction / distance * length(shape.vertices[1] - center);
        return {};
    }
    const bool ellipse = shape.kind == ShapeKind::Ellipse;
    std::optional<QPointF> closest;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& outline : shapeOutlines(shape)) {
        for (qsizetype i = 1; i < outline.size(); ++i) {
            const auto candidate = projectSegment(raw, outline[i - 1], outline[i]);
            const double d = length(candidate - raw);
            if (d < best) {
                best = d;
                closest = candidate;
            }
        }
    }
    if (ellipse && closest) {
        const auto oval = ovalBounds(shape);
        const auto delta = *closest - oval.center();
        const double scaled = std::hypot(2 * delta.x() / oval.width(), 2 * delta.y() / oval.height());
        if (scaled > 1e-8)
            closest = oval.center() + delta / scaled;
    }
    return closest;
}

void drawShadedRegion(QPainter& painter, const ShapeItem& region) {
    const auto path = shapePath(region);
    QColor tone = region.color;
    if (region.shadePattern == ShadePattern::Solid) {
        painter.fillPath(path, tone);
        return;
    }
    tone.setAlphaF(tone.alphaF() * (region.shadePattern == ShadePattern::Soft ? .17 : .11));
    painter.fillPath(path, tone);
    if (region.shadePattern == ShadePattern::Soft)
        return;

    const auto bounds = path.boundingRect();
    const double span = bounds.width() + bounds.height();
    if (!std::isfinite(span))
        return;
    const double spacing = std::max(12.0, span / 160.0);
    QColor pattern = region.color;
    pattern.setAlphaF(pattern.alphaF() * .23);
    painter.setClipPath(path, Qt::IntersectClip);
    QPainterPath marks;
    if (region.shadePattern == ShadePattern::Diagonal || region.shadePattern == ShadePattern::Crosshatch) {
        painter.setPen(QPen(pattern, .85));
        painter.setBrush(Qt::NoBrush);
        for (int i = 0; i <= int(span / spacing); ++i) {
            const QPointF start(bounds.left() + i * spacing, bounds.bottom());
            marks.moveTo(start);
            marks.lineTo(start + QPointF(-bounds.height(), -bounds.height()));
            if (region.shadePattern == ShadePattern::Crosshatch) {
                const QPointF s2(bounds.left() + i * spacing, bounds.top());
                marks.moveTo(s2);
                marks.lineTo(s2 + QPointF(-bounds.height(), bounds.height()));
            }
        }
    } else if (region.shadePattern == ShadePattern::Grid) {
        painter.setPen(QPen(pattern, .85));
        painter.setBrush(Qt::NoBrush);
        for (double x = bounds.left(); x <= bounds.right(); x += spacing) {
            marks.moveTo(x, bounds.top());
            marks.lineTo(x, bounds.bottom());
        }
        for (double y = bounds.top(); y <= bounds.bottom(); y += spacing) {
            marks.moveTo(bounds.left(), y);
            marks.lineTo(bounds.right(), y);
        }
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(pattern);
        const int columns = int(bounds.width() / spacing);
        const int rows = int(bounds.height() / spacing);
        for (int row = 0; row <= rows; ++row)
            for (int column = 0; column <= columns; ++column)
                marks.addEllipse(bounds.topLeft() + QPointF((column + (row % 2) * .5) * spacing,
                                                          row * spacing), 1.0, 1.0);
    }
    painter.drawPath(marks);
}
void simplifyRange(const QVector<QPointF>& points,qsizetype first,qsizetype last,double epsilon,QVector<QPointF>& result) {
    double farthest=epsilon;qsizetype split=-1;
    for(qsizetype i=first+1;i<last;++i){const double d=segmentDistance(points[i],points[first],points[last]);if(d>farthest){farthest=d;split=i;}}
    if(split>=0){simplifyRange(points,first,split,epsilon,result);simplifyRange(points,split,last,epsilon,result);}
    else result.append(points[first]);
}
QVector<QPointF> closedCorners(QVector<QPointF> points,double epsilon) {
    if(points.size()<3)return points;
    if(length(points.front()-points.back())<epsilon)points.removeLast();
    qsizetype start=0,opposite=0;
    for(qsizetype i=1;i<points.size();++i)if(length(points[i]-points[0])>length(points[start]-points[0]))start=i;
    for(qsizetype i=0;i<points.size();++i)if(length(points[i]-points[start])>length(points[opposite]-points[start]))opposite=i;
    QVector<QPointF> ring;for(qsizetype j=0;j<points.size();++j)ring.append(points[(start+j)%points.size()]);ring.append(ring[0]);
    const qsizetype mid=(opposite-start+points.size())%points.size();QVector<QPointF> result;
    if(mid==0)return {};
    simplifyRange(ring,0,mid,epsilon,result);simplifyRange(ring,mid,ring.size()-1,epsilon,result);
    bool removed=true;
    while(removed&&result.size()>3){removed=false;for(qsizetype i=0;i<result.size();++i)if(segmentDistance(result[i],result[(i+result.size()-1)%result.size()],result[(i+1)%result.size()])<epsilon){result.removeAt(i);removed=true;break;}}
    return result;
}
}
QRectF computeShapeBounds(const ShapeItem& shape) {
    auto bounds=shapePath(shape).boundingRect();const auto arrows=arrowPath(shape);
    if(!arrows.isEmpty())bounds=bounds.united(arrows.boundingRect());
    if (shape.kind == ShapeKind::Line && shape.linePattern == LinePattern::Dimension) {
        const double cap = std::clamp(shape.size * 3.2, 8.0, 22.0);
        bounds.adjust(-cap, -cap, cap, cap);
    }
    if (shape.kind == ShapeKind::AngleMarker || shape.linePattern == LinePattern::Dimension)
        bounds = bounds.united(measurement(shape).bounds);
    const double padding=std::max(0.0,shape.size)/2;
    return bounds.adjusted(-padding,-padding,padding,padding);
}
QRectF shapeGeometryBounds(const ShapeItem& shape) {
    return shapePath(shape).boundingRect();
}

bool canFillShape(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Rectangle
        || shape.kind == ShapeKind::Ellipse || shape.kind == ShapeKind::Triangle || shape.kind == ShapeKind::Sector
        || (shape.kind == ShapeKind::Polyline && shape.closed);
}

bool supportsLineEnding(const ShapeItem& shape) {
    return (shape.kind == ShapeKind::Line && shape.linePattern != LinePattern::Dimension)
        || (shape.kind == ShapeKind::Polyline && !shape.closed) || shape.kind == ShapeKind::Curve;
}

bool containsShapeInterior(const ShapeItem& shape, QPointF point) {
    return (canFillShape(shape) || shape.kind == ShapeKind::ShadedRegion)
        && shape.shadeVisible && shape.bounds.contains(point) && shapePath(shape).contains(point);
}

namespace {
QRectF resizeFrame(QRectF bounds, double zoom) {
    const double margin = 14 / zoom;
    return bounds.adjusted(-margin, -margin, margin, margin);
}
QVector<QPointF> resizeHandles(QRectF frame) {
    return {{frame.left(), frame.center().y()}, {frame.right(), frame.center().y()},
            {frame.center().x(), frame.top()}, {frame.center().x(), frame.bottom()}};
}
bool hasResizeHandle(QRectF box, int index) {
    return (index < 2 ? box.width() : box.height()) > 1e-6;
}
}

int hitTestResizeHandle(const ShapeItem& shape, QPointF point, double zoom) {
    const auto bounds = shapeGeometryBounds(shape);
    const auto handles = resizeHandles(resizeFrame(bounds, zoom));
    for (int i = 0; i < handles.size(); ++i)
        if (hasResizeHandle(bounds, i) && length(point - handles[i]) * zoom <= 8)
            return i;
    return -1;
}
QVector<QPointF> getShapeHandles(const ShapeItem& shape) {
    const auto& v=shape.vertices;
    if(shape.kind==ShapeKind::ShadedRegion)return {};
    if((shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Axes||shape.kind==ShapeKind::Solid3D)&&v.size()>=2){const QRectF r=QRectF(v[0],v[1]).normalized();return {r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};}
    return v;
}
void applyHandleDrag(ShapeItem& shape,int index,const QPointF& point) {
    auto& v=shape.vertices;
    if(index<0)return;
    if((shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Axes||shape.kind==ShapeKind::Solid3D)&&v.size()>=2) {
        const auto handles=getShapeHandles(shape);if(index>=handles.size())return;
        const QRectF rect=QRectF(point,handles[(index+2)%4]).normalized();v={rect.topLeft(),rect.bottomRight()};
    } else if(index<v.size()) {
        if ((shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector) && v.size() >= 3) {
            if (index == 0) {
                const auto delta = point - v[0];
                for (auto& vertex : v)
                    vertex += delta;
            } else {
                const auto ray = point - v[0];
                if (length(ray) > 1e-8) {
                    if (index == 1) {
                        const auto direction = v[2] - v[0];
                        if (length(direction) > 1e-8)
                            v[2] = v[0] + direction / length(direction) * length(ray);
                        v[1] = point;
                    } else {
                        v[2] = v[0] + ray / length(ray) * length(v[1] - v[0]);
                    }
                }
            }
        } else if(shape.kind==ShapeKind::RightAngle&&v.size()>=3) {
            if(index==1){const auto delta=point-v[1];for(auto& vertex:v)vertex+=delta;}
            else {
                const auto other=v[index==0?2:0]-v[1];const double span=length(other);
                if(span>1e-8){const QPointF axis(-other.y()/span,other.x()/span);v[index]=v[1]+axis*dot(point-v[1],axis);}
                else v[index]=point;
            }
        } else if(shape.kind==ShapeKind::Circle&&index==0&&v.size()>=2){const auto delta=point-v[0];v[0]=point;v[1]+=delta;}
        else if(shape.kind==ShapeKind::Radical&&v.size()>=3) {
            if(index==0)v[0]=point;
            else if(index==1){v[1]=point;v[2].setY(point.y());v[2].setX(std::max(v[2].x(),point.x()+.5));}
            else if(index==2){v[2]=QPointF(std::max(v[1].x()+.5,point.x()),v[1].y());}
        } else v[index]=point;
    }
    shape.bounds=computeShapeBounds(shape);
}
void translateShape(ShapeItem& shape,const QPointF& delta) {for(auto& p:shape.vertices)p+=delta;shape.bounds=computeShapeBounds(shape);}
void scaleShape(ShapeItem& shape, QPointF anchor, double sx, double sy) {
    if (shape.kind == ShapeKind::Circle && shape.vertices.size() == 2 && std::abs(sx - sy) > 1e-8) {
        const auto oval = ovalBounds(shape);
        shape.vertices = {oval.topLeft(), oval.bottomRight()};
        shape.kind = ShapeKind::Ellipse;
    }
    for (auto& point : shape.vertices)
        point = anchor + QPointF((point.x() - anchor.x()) * sx, (point.y() - anchor.y()) * sy);
    shape.bounds = computeShapeBounds(shape);
}
bool hitTestShape(const ShapeItem& shape,const QPointF& point,double tolerance) {
    if(!shape.shadeVisible)return false;
    const double radius=std::max(0.0,tolerance);
    if(!computeShapeBounds(shape).adjusted(-radius,-radius,radius,radius).contains(point))return false;
    if(shape.kind==ShapeKind::ShadedRegion)return shapePath(shape).contains(point);
    QPainterPathStroker stroker;stroker.setWidth(std::max(.01,shape.size+2*radius));stroker.setCapStyle(Qt::RoundCap);stroker.setJoinStyle(Qt::RoundJoin);
    if ((shape.kind == ShapeKind::AngleMarker || shape.linePattern == LinePattern::Dimension)
        && measurement(shape).bounds.contains(point))
        return true;
    const auto path=shapePath(shape);return stroker.createStroke(path).contains(point)||arrowPath(shape).contains(point);
}
int hitTestHandle(const ShapeItem& shape,const QPointF& point,double radius) {
    const auto handles=getShapeHandles(shape);int result=-1;double best=std::max(0.0,radius);
    for(qsizetype i=0;i<handles.size();++i){const double d=length(point-handles[i]);if(d<=best){best=d;result=int(i);}}
    return result;
}
void drawShape(QPainter& painter,const ShapeItem& shape) {
    if(shape.vertices.isEmpty()||!shape.shadeVisible)return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setOpacity(std::clamp(shape.opacity,0.0,1.0));
    if(shape.kind==ShapeKind::ShadedRegion) {
        drawShadedRegion(painter,shape);
        painter.restore();
        return;
    }
    QPen pen(shape.color,std::max(.01,shape.size),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
    if(shape.linePattern==LinePattern::Dashed)pen.setDashPattern({4.0,2.5});
    if(shape.linePattern==LinePattern::Dotted)pen.setDashPattern({.01,2.2});
    if(shape.linePattern==LinePattern::Dimension&&shape.kind==ShapeKind::Line&&shape.vertices.size()>=2) {
        // Cota de medida |——   ——|: dos topes perpendiculares en extremos + hueco central para anotar medida (ej. "8 m")
        const QPointF a=shape.vertices[0], b=shape.vertices[1];
        const QPointF d=b-a;
        const double len=length(d);
        if(len>1e-3) {
            const QPointF u=d/len;
            const QPointF n(-u.y(),u.x());
            const double cap=std::clamp(shape.size*3.2,8.0,22.0);
            const auto label = measurement(shape);
            const QPointF mid=(a+b)*0.5;
            double gapHalf = std::min(len * .18, std::max(16.0, shape.size * 6.5));
            if (!label.text.isEmpty() && label.bounds.contains(mid))
                gapHalf = std::max(gapHalf, (std::abs(u.x()) * label.bounds.width()
                    + std::abs(u.y()) * label.bounds.height()) / 2 + 4);
            QPainterPath dimPath;
            // Topes perpendiculares en ambos extremos
            dimPath.moveTo(a-n*cap); dimPath.lineTo(a+n*cap);
            dimPath.moveTo(b-n*cap); dimPath.lineTo(b+n*cap);
            if(len>gapHalf*2.4) {
                dimPath.moveTo(a); dimPath.lineTo(mid-u*gapHalf);
                dimPath.moveTo(mid+u*gapHalf); dimPath.lineTo(b);
            } else {
                dimPath.moveTo(a); dimPath.lineTo(b);
            }
            painter.setPen(pen);painter.setBrush(Qt::NoBrush);painter.drawPath(dimPath);
            drawMeasurement(painter, shape, label);
            painter.restore();
            return;
        }
    }
    if(shape.kind==ShapeKind::Solid3D&&shape.vertices.size()>=2) {
        const QRectF r=QRectF(shape.vertices[0],shape.vertices[1]).normalized();
        if(r.width()>1.0&&r.height()>1.0) {
            const auto g = buildSolid3D(shape);
            painter.setPen(pen);painter.setBrush(Qt::NoBrush);painter.drawPath(g.vis);
            QColor hidCol=shape.color; hidCol.setAlphaF(hidCol.alphaF()*0.58);
            QPen hidPen(hidCol,std::max(1.0,shape.size*0.85),Qt::CustomDashLine,Qt::RoundCap,Qt::RoundJoin);
            hidPen.setDashPattern({4.0,3.5});
            painter.setPen(hidPen);painter.drawPath(g.hid);
        }
        painter.restore();
        return;
    }
    painter.setPen(pen);painter.setBrush(Qt::NoBrush);painter.drawPath(shapePath(shape));
    // Cuadradito automático de ángulo recto (90°) en triángulos rectángulos
    if(shape.kind==ShapeKind::Triangle&&shape.vertices.size()>=3) {
        for(int i=0;i<3;++i) {
            const QPointF c=shape.vertices[i];
            const QPointF u=shape.vertices[(i+1)%3]-c;
            const QPointF v=shape.vertices[(i+2)%3]-c;
            const double lu=length(u), lv=length(v);
            if(lu>18.0&&lv>18.0&&std::abs(dot(u,v)/(lu*lv))<=0.016) {
                const double sq=std::clamp(std::min(lu,lv)*0.14,10.0,22.0);
                const QPointF eu=u/lu*sq, ev=v/lv*sq;
                QPen sqPen(shape.color,std::max(1.1,shape.size*0.65),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
                painter.setPen(sqPen);
                QPainterPath mark;
                mark.moveTo(c+eu);
                mark.lineTo(c+eu+ev);
                mark.lineTo(c+ev);
                painter.drawPath(mark);
                break;
            }
        }
    }
    if (shape.kind == ShapeKind::AngleMarker)
        drawMeasurement(painter, shape, measurement(shape));
    painter.setPen(Qt::NoPen);painter.setBrush(shape.color);painter.drawPath(arrowPath(shape));painter.restore();
}
void drawSelectionOverlay(QPainter& painter,const ShapeItem& shape,const ThemeConfig& theme,double zoom,bool showResizeHandles) {
    zoom=std::max(.01,zoom);painter.save();painter.setRenderHint(QPainter::Antialiasing);
    const auto bounds = showResizeHandles ? shapeGeometryBounds(shape) : computeShapeBounds(shape);
    const auto frame = showResizeHandles ? resizeFrame(bounds, zoom) : bounds.adjusted(-3/zoom,-3/zoom,3/zoom,3/zoom);
    QColor faint=theme.accent;faint.setAlpha(100);QPen guide(faint,1/zoom,Qt::DashLine);painter.setPen(guide);painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame);
    painter.setPen(QPen(theme.accent,1.6/zoom));painter.setBrush(theme.isDark?theme.uiSurface:QColor(Qt::white));
    for(auto point:getShapeHandles(shape))painter.drawEllipse(point,6/zoom,6/zoom);
    if (showResizeHandles) {
        const auto handles = resizeHandles(frame);
        const double half = 4.5 / zoom;
        for (int i = 0; i < handles.size(); ++i)
            if (hasResizeHandle(bounds, i))
                painter.drawRect(QRectF(handles[i] - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    }
    painter.restore();
}

SnapResult snapShapeVertex(const ShapeItem& shape, int index, const SnapResult& external,
                          QPointF raw, double zoom) {
    if (external.contact)
        return external;
    zoom = std::max(.01, zoom);
    const auto handles = getShapeHandles(shape);
    if (index < 0 || index >= handles.size())
        return external;
    const auto original = handles[index];
    auto findAxis = [&](auto coordinate) -> std::optional<QPointF> {
        double best = 7 / zoom;
        std::optional<QPointF> match;
        for (int i = 0; i < handles.size(); ++i) {
            const double error = std::abs(coordinate(raw) - coordinate(handles[i]));
            if (i != index && error < best) {
                best = error;
                match = handles[i];
            }
        }
        if (!match && std::abs(coordinate(raw) - coordinate(original)) < best)
            match = original;
        return match;
    };
    const auto x = findAxis([](QPointF p) { return p.x(); });
    const auto y = findAxis([](QPointF p) { return p.y(); });
    if (!x && !y)
        return external;
    SnapResult result = external;
    if (x) result.point.setX(x->x());
    if (y) result.point.setY(y->y());
    if (x) result.guides.append({*x, result.point, {}, false});
    if (y) result.guides.append({*y, result.point, {}, false});
    return result;
}
void drawSmartGuides(QPainter& painter,const QVector<SmartGuide>& guides,const ThemeConfig& theme,double zoom) {
    zoom=std::max(.01,zoom);painter.save();painter.setRenderHint(QPainter::Antialiasing);
    QFont font=painter.font();font.setPixelSize(10);painter.setFont(font);
    QColor guideColor = theme.accent;
    guideColor.setAlpha(140);
    for(const auto& guide:guides) {
        if (length(guide.a - guide.b) < 1e-4) {
            // Point/circumference/edge snap indicator ("pap!" absorption dot)
            painter.setPen(QPen(theme.accent, 1.8 / zoom));
            painter.setBrush(theme.isDark ? QColor("#0f172a") : QColor(Qt::white));
            painter.drawEllipse(guide.a, 5.5 / zoom, 5.5 / zoom);
            continue;
        }
        painter.setPen(QPen(guideColor,1.0/zoom,Qt::DashLine));painter.drawLine(guide.a,guide.b);
        // Only show compact angle badges (never clutter the canvas with "alineado" pills)
        if(!guide.isAngleGuide || guide.label.isEmpty())continue;
        const QPointF world=(guide.a+guide.b)/2;
        painter.save();painter.translate(world);painter.scale(1/zoom,1/zoom);
        const QFontMetricsF metrics(font);const QRectF badge(-metrics.horizontalAdvance(guide.label)/2-4,-19,metrics.horizontalAdvance(guide.label)+8,15);
        QColor badgeBg = theme.accent;
        badgeBg.setAlpha(210);
        painter.setPen(Qt::NoPen);painter.setBrush(badgeBg);painter.drawRoundedRect(badge,5,5);
        painter.setPen(theme.isDark?QColor("#0f172a"):QColor(Qt::white));painter.drawText(badge,Qt::AlignCenter,guide.label);painter.restore();
    }
    painter.restore();
}
namespace {
void setRegionPath(ShapeItem& region, const QPainterPath& chosen) {
    region.vertices.clear();
    region.shadeContourStarts.clear();
    // One start point, followed by cubic control pairs and their endpoints.
    // Straight edges use their endpoints as controls and stay exact.
    region.vertices.append(chosen.elementAt(0));
    for (int i = 1; i < chosen.elementCount(); ++i) {
        const auto element = chosen.elementAt(i);
        if (element.isMoveTo()) {
            region.shadeContourStarts.append(int(region.vertices.size()));
            region.vertices.append(element);
        } else if (element.isLineTo()) {
            const QPointF previous = region.vertices.back();
            region.vertices.append({previous, QPointF(element), QPointF(element)});
        } else {
            region.vertices.append({QPointF(element), QPointF(chosen.elementAt(i + 1)),
                                    QPointF(chosen.elementAt(i + 2))});
            i += 2;
        }
    }
    region.bounds = computeShapeBounds(region);
}

std::optional<QPointF> interiorPoint(const QPainterPath& path) {
    const auto box = path.boundingRect();
    const auto polygons = path.toSubpathPolygons();
    for (double row : {.5, .25, .75, .125, .375, .625, .875}) {
        const double y = box.top() + row * box.height();
        QVector<double> cuts;
        for (const auto& polygon : polygons) {
            for (qsizetype i = 1; i < polygon.size(); ++i) {
                const auto a = polygon[i - 1], b = polygon[i];
                if ((a.y() > y) != (b.y() > y))
                    cuts.append(a.x() + (b.x() - a.x()) * (y - a.y()) / (b.y() - a.y()));
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (qsizetype i = 1; i < cuts.size(); ++i) {
            const QPointF p((cuts[i - 1] + cuts[i]) * .5, y);
            if (path.contains(p)) return p;
        }
    }
    return {};
}

RegionFace regionAt(QPointF point, const QVector<ShapeItem>& shapes, const QHash<QString, int>& boundary = {}) {
    QVector<RegionOutline> outlines;
    for (const auto& shape : shapes)
        if (shape.vertices.size() >= 2 && shape.kind != ShapeKind::ShadedRegion
            && (boundary.isEmpty() || boundary.contains(shape.id)))
            for (const auto& outline : shapeOutlines(shape))
                outlines.append({outline, shape.size * .5, shape.id});
    return closedFaceAt(point, outlines, boundary);
}

bool hasAttachableEnds(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Curve || shape.kind == ShapeKind::Brace
        || (shape.kind == ShapeKind::Polyline && !shape.closed);
}

constexpr int centerAnchor = -1;
constexpr int ovalAnchor = -2;
constexpr int curveAnchor = -3;
bool isOval(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse;
}
bool hasCenterAnchor(const ShapeItem& shape) {
    return isOval(shape) || shape.kind == ShapeKind::Rectangle;
}
double circleDirection(const ShapeItem& shape) {
    const auto radius = shape.vertices[1] - shape.vertices[0];
    return std::atan2(radius.y(), radius.x());
}

std::optional<QPointF> attachmentPoint(const ShapeItem& target, const ShapeAttachment& attachment) {
    if (target.vertices.size() < 2) return {};
    if (attachment.edge == centerAnchor) {
        if (hasCenterAnchor(target)) return ovalBounds(target).center();
        if (target.kind == ShapeKind::Polyline && target.closed) return shapeGeometryBounds(target).center();
        return {};
    }
    if (attachment.edge == ovalAnchor) {
        // Lasso rotation represents an ellipse as 128 points in angular order.
        if (target.kind == ShapeKind::Polyline && target.closed && target.vertices.size() == 128) {
            constexpr double turn = 2 * std::numbers::pi;
            const double angle = std::fmod(std::fmod(attachment.position, turn) + turn, turn);
            const double edge = angle / turn * target.vertices.size();
            const int from = int(edge) % target.vertices.size();
            return target.vertices[from] + (target.vertices[(from + 1) % target.vertices.size()]
                                            - target.vertices[from]) * (edge - std::floor(edge));
        }
        if (!isOval(target)) return {};
        const auto box = ovalBounds(target);
        const double angle = attachment.position + (target.kind == ShapeKind::Circle ? circleDirection(target) : 0);
        return box.center() + QPointF(std::cos(angle) * box.width() * .5, std::sin(angle) * box.height() * .5);
    }
    if (attachment.edge == curveAnchor)
        return shapePath(target).pointAtPercent(attachment.position);
    const auto outlines = shapeOutlines(target);
    int edge = attachment.edge;
    for (const auto& outline : outlines) {
        if (edge >= 0 && edge + 1 < outline.size())
            return outline[edge] + (outline[edge + 1] - outline[edge]) * attachment.position;
        edge -= std::max<qsizetype>(0, outline.size() - 1);
    }
    return {};
}

std::optional<ShapeAttachment> nearestAttachment(const QVector<ShapeItem>& shapes, qsizetype source, int vertex) {
    const auto& connector = shapes[source];
    const auto point = connector.vertices[vertex];
    double nearest = std::numeric_limits<double>::infinity();
    std::optional<ShapeAttachment> result;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        const auto& target = shapes[i];
        // References between connectors follow creation order to keep the graph acyclic.
        if (i == source || (i > source && hasAttachableEnds(target))
            || target.kind == ShapeKind::ShadedRegion || target.vertices.size() < 2 || target.id.isEmpty())
            continue;
        const double reach = std::min(3.0, 1.0 + (connector.size + target.size) * .5);
        if (!target.bounds.adjusted(-reach, -reach, reach, reach).contains(point))
            continue;
        auto consider = [&](QPointF candidate, int edge, double position) {
            const double d = length(candidate - point);
            if (d <= reach && d < nearest - 1e-8) {
                result = ShapeAttachment{vertex, target.id, edge, position};
                nearest = d;
            }
        };
        if (hasCenterAnchor(target))
            consider(ovalBounds(target).center(), centerAnchor, 0);
        if (isOval(target)) {
            if (const auto p = closestOutlinePoint(target, point)) {
                const auto box = ovalBounds(target);
                const auto delta = *p - box.center();
                if (!box.isEmpty()) {
                    const double angle = std::atan2(delta.y() / box.height(), delta.x() / box.width());
                    consider(*p, ovalAnchor, angle - (target.kind == ShapeKind::Circle ? circleDirection(target) : 0));
                }
            }
        } else if (target.kind == ShapeKind::Curve || target.kind == ShapeKind::Brace) {
            const auto path = shapePath(target);
            double bestT = 0, bestDistance = std::numeric_limits<double>::infinity();
            for (int sample = 0; sample <= 32; ++sample) {
                const double t = sample / 32.0;
                const double d = length(path.pointAtPercent(t) - point);
                if (d < bestDistance) { bestT = t; bestDistance = d; }
            }
            double low = std::max(0.0, bestT - 1 / 32.0), high = std::min(1.0, bestT + 1 / 32.0);
            for (int step = 0; step < 20; ++step) {
                const double a = (2 * low + high) / 3, b = (low + 2 * high) / 3;
                if (length(path.pointAtPercent(a) - point) < length(path.pointAtPercent(b) - point)) high = b;
                else low = a;
            }
            const double t = (low + high) * .5;
            consider(path.pointAtPercent(t), curveAnchor, t);
            consider(path.pointAtPercent(0), curveAnchor, 0);
            consider(path.pointAtPercent(1), curveAnchor, 1);
        } else {
            int edge = 0;
            for (const auto& outline : shapeOutlines(target)) {
                for (qsizetype j = 1; j < outline.size(); ++j, ++edge) {
                    const auto a = outline[j - 1], b = outline[j];
                    const auto delta = b - a;
                    if (dot(delta, delta) <= 1e-12) continue;
                    const double t = std::clamp(dot(point - a, delta) / dot(delta, delta), 0.0, 1.0);
                    consider(a + delta * t, edge, t);
                }
            }
        }
    }
    return result;
}

void bindEndpoints(QVector<ShapeItem>& shapes, qsizetype index, const ShapeItem* before = nullptr,
                   const QSet<QString>& edited = {}) {
    auto& shape = shapes[index];
    if (!hasAttachableEnds(shape) || shape.vertices.size() < 2) return;
    QVector<ShapeAttachment> attachments;
    for (int vertex : {0, int(shape.vertices.size() - 1)}) {
        if (before && vertex < before->vertices.size()) {
            const auto old = std::find_if(before->attachments.cbegin(), before->attachments.cend(),
                                         [&](const auto& link) { return link.vertex == vertex; });
            if (shape.vertices[vertex] == before->vertices[vertex] || (old != before->attachments.cend() && edited.contains(old->targetId))) {
                if (old != before->attachments.cend()) attachments.append(*old);
                continue;
            }
        }
        if (auto link = nearestAttachment(shapes, index, vertex)) attachments.append(*link);
    }
    shape.attachments = attachments;
    shape.attachmentsKnown = true;
}

bool sameGeometry(const ShapeItem& a, const ShapeItem& b) {
    return a.kind == b.kind && a.vertices == b.vertices && a.closed == b.closed && a.size == b.size;
}
}

std::optional<ShapeItem> shadedRegionAt(const QPointF& point, const QVector<ShapeItem>& shapes,
                                      const QColor& color, ShadePattern pattern) {
    const auto face = regionAt(point, shapes);
    if (face.path.isEmpty()) return {};
    ShapeItem region;
    region.kind = ShapeKind::ShadedRegion;
    region.closed = true;
    region.color = color;
    region.shadePattern = pattern;
    region.shadeBoundary = face.boundary;
    region.shadeProbe = point;
    setRegionPath(region, face.path);
    return region;
}

std::optional<ShapeItem> shadedRegionForShape(const ShapeItem& shape, const QColor& color, ShadePattern pattern) {
    if (!canFillShape(shape))
        return {};
    const auto point = interiorPoint(shapePath(shape));
    return point ? shadedRegionAt(*point, {shape}, color, pattern) : std::nullopt;
}

void attachNewShape(QVector<ShapeItem>& shapes) {
    if (shapes.isEmpty()) return;
    bindEndpoints(shapes, shapes.size() - 1);
    const auto replacement = shapes.back().id;
    QSet<QString> present;
    for (const auto& shape : std::as_const(shapes)) present.insert(shape.id);
    for (auto& fill : shapes) {
        if (fill.shadeVisible || fill.shadeBoundary.size() < 2 || fill.shadeBoundary.contains(replacement)) continue;
        QString missing;
        for (auto it = fill.shadeBoundary.keyBegin(); it != fill.shadeBoundary.keyEnd(); ++it) {
            if (present.contains(*it)) continue;
            if (!missing.isEmpty()) { missing.clear(); break; }
            missing = *it;
        }
        if (missing.isEmpty()) continue;
        auto boundary = fill.shadeBoundary;
        const int side = boundary.take(missing);
        for (int direction : {side, ((side & 1) << 1) | ((side & 2) >> 1)}) {
            boundary.insert(replacement, direction);
            const auto face = regionAt(fill.shadeProbe, shapes, boundary);
            if (!face.path.contains(fill.shadeProbe)) continue;
            fill.shadeBoundary = boundary;
            fill.shadeVisible = true;
            setRegionPath(fill, face.path);
            break;
        }
    }
}

void initializeShapeLinks(QVector<ShapeItem>& shapes) {
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        auto& shape = shapes[i];
        if (!shape.attachmentsKnown) bindEndpoints(shapes, i);
        if (shape.kind != ShapeKind::ShadedRegion || !shape.shadeBoundary.isEmpty()) continue;
        const auto savedPath = shapePath(shape);
        const auto point = interiorPoint(savedPath);
        if (!point) continue;
        const auto face = regionAt(*point, shapes);
        if (face.path == savedPath) {
            shape.shadeBoundary = face.boundary;
            shape.shadeProbe = *point;
        }
    }
}

bool updateShapeLinks(QVector<ShapeItem>& shapes, const QVector<ShapeItem>& original,
                      const QSet<QString>& edited) {
    QHash<QString, qsizetype> indices;
    QHash<QString, const ShapeItem*> before;
    for (const auto& shape : original) before.insert(shape.id, &shape);
    for (qsizetype i = 0; i < shapes.size(); ++i) indices.insert(shapes[i].id, i);
    QSet<QString> changedByGesture;
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        const auto* old = before.value(shapes[i].id);
        if (old && edited.contains(shapes[i].id) && !sameGeometry(shapes[i], *old)) {
            bindEndpoints(shapes, i, old, edited);
            changedByGesture.insert(shapes[i].id);
        }
    }
    QSet<QString> changed;
    for (const auto& shape : original) if (!indices.contains(shape.id)) changed.insert(shape.id);
    bool dependentChanged = false;
    // Closed figures are targets only. Connectors can depend on earlier connectors.
    for (int connectors = 0; connectors < 2; ++connectors) {
        for (auto& shape : shapes) {
            if (shape.kind == ShapeKind::ShadedRegion || hasAttachableEnds(shape) != bool(connectors)) continue;
            if (shape.vertices.size() >= 2) {
                QPointF firstDelta, lastDelta;
                for (auto& link : shape.attachments) {
                    const qsizetype target = indices.value(link.targetId, -1);
                    if (target < 0 || link.vertex < 0 || link.vertex >= shape.vertices.size()
                        || (!changed.contains(link.targetId) && !changedByGesture.contains(shape.id))) continue;
                    const auto* oldTarget = before.value(link.targetId);
                    if (link.edge == ovalAnchor && oldTarget && oldTarget->kind == ShapeKind::Circle
                        && shapes[target].kind == ShapeKind::Ellipse) {
                        const auto* oldSource = before.value(shape.id);
                        if (oldSource && std::any_of(oldSource->attachments.cbegin(), oldSource->attachments.cend(),
                            [&](const auto& previous) {
                                return previous.vertex == link.vertex && previous.targetId == link.targetId
                                    && previous.edge == link.edge && previous.position == link.position;
                            })) link.position += circleDirection(*oldTarget);
                    }
                    if (const auto point = attachmentPoint(shapes[target], link)) {
                        const auto delta = *point - shape.vertices[link.vertex];
                        if (link.vertex == 0) firstDelta = delta;
                        if (link.vertex == shape.vertices.size() - 1) lastDelta = delta;
                    }
                }
                if (firstDelta != QPointF() || lastDelta != QPointF()) {
                    for (qsizetype i = 0; i < shape.vertices.size(); ++i) {
                        const double t = double(i) / (shape.vertices.size() - 1);
                        shape.vertices[i] += (1 - t) * firstDelta + t * lastDelta;
                    }
                    shape.bounds = computeShapeBounds(shape);
                    dependentChanged = true;
                }
            }
            const auto* old = before.value(shape.id);
            if (!old || !sameGeometry(shape, *old)) changed.insert(shape.id);
        }
    }
    for (auto& shape : shapes) {
        if (shape.shadeBoundary.isEmpty()) continue;
        const auto* old = before.value(shape.id);
        if (!old || !std::any_of(shape.shadeBoundary.keyBegin(), shape.shadeBoundary.keyEnd(),
                                  [&](const auto& id) { return changed.contains(id); })) continue;
        QPointF translation;
        for (auto it = shape.shadeBoundary.cbegin(); it != shape.shadeBoundary.cend(); ++it) {
            const auto* boundaryBefore = before.value(it.key());
            const auto index = indices.value(it.key(), -1);
            if (boundaryBefore && index >= 0)
                translation += shapes[index].bounds.center() - boundaryBefore->bounds.center();
        }
        const auto probe = old->shadeProbe + translation / shape.shadeBoundary.size();
        const auto face = regionAt(probe, shapes, shape.shadeBoundary);
        shape.shadeVisible = !face.path.isEmpty();
        if (shape.shadeVisible) {
            setRegionPath(shape, face.path);
            shape.shadeProbe = face.path.contains(probe) ? probe : interiorPoint(face.path).value_or(probe);
        }
        dependentChanged = true;
    }
    return dependentChanged;
}

SnapResult snapPointWithGuides(const QPointF& raw,const std::optional<QPointF>& anchor,const QVector<ShapeItem>& shapes,const QString& excluded,double zoom,bool align) {
    zoom=std::max(.01,zoom);SnapResult result{raw,{}};
    QVector<QPointF> targets;
    for(const auto& shape:shapes)
        if((excluded.isEmpty()||shape.id!=excluded) && shape.vertices.size()>=2)targets+=snapTargets(shape);
    auto joined = [](QPointF point) {
        SnapResult snap{point, {}, true};
        snap.guides.append({point, point, QString(), false});
        return snap;
    };
    // Radios precisos y táctiles (8px vértices, 7px contornos) para que el imán
    // solo se active al acercarse de verdad y nunca "secuestre" el trazo desde lejos.
    double nearest=8.0/zoom;std::optional<QPointF> vertex;
    for(auto target:targets){const double d=length(raw-target);if(d<nearest){nearest=d;vertex=target;}}
    double bestBoundaryDist = 7.0 / zoom;
    double anchoredDistance = 9.0 / zoom;
    const QRectF contactArea(raw - QPointF(anchoredDistance, anchoredDistance),
                             QSizeF(anchoredDistance * 2, anchoredDistance * 2));
    std::optional<QPointF> boundarySnap;
    std::optional<QPointF> anchoredBoundary;
    for (const auto& shape : shapes) {
        if ((!excluded.isEmpty() && shape.id == excluded) || shape.kind == ShapeKind::ShadedRegion || shape.vertices.size() < 2
            || !shape.bounds.intersects(contactArea))
            continue;
        const auto boundary = closestOutlinePoint(shape, raw);
        if (!boundary)
            continue;
        const double d = length(raw - *boundary);
        if (d < bestBoundaryDist) {
            bestBoundaryDist = d;
            boundarySnap = boundary;
        }
        // When drawing outward from a center, favor its own contour over
        // nearby endpoints from other figures that don't reach that contour.
        const bool centeredShape = shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse
            || shape.kind == ShapeKind::Rectangle;
        if (anchor && centeredShape && length(*anchor - ovalBounds(shape).center()) < 1e-6
            && d < std::min(anchoredDistance, length(*boundary - *anchor) * .55)) {
            anchoredDistance = d;
            anchoredBoundary = boundary;
        }
    }
    if (anchoredBoundary) return joined(*anchoredBoundary);
    if (boundarySnap && bestBoundaryDist < contourJoinTolerance && nearest > contourJoinTolerance)
        return joined(*boundarySnap);
    if (vertex && nearest <= bestBoundaryDist + 3.0 / zoom) return joined(*vertex);
    if (boundarySnap) return joined(*boundarySnap);
    if (!align) return result;

    // 3. Smooth cardinal angle snap ONLY (0, 90, 180, 270 deg) so free rotation never feels bumpy!
    bool angleSnapped=false;
    if(anchor&&length(raw-*anchor)>10.0/zoom) {
        const auto delta=raw-*anchor;const double heading=std::atan2(delta.y(),delta.x())*180/std::numbers::pi;
        double bestDifference=3.0,bestAngle=0;
        for(double angle : {0.0, 90.0, 180.0, 270.0}) {
            const double difference=std::abs(std::remainder(heading-angle,360.0));
            if(difference<=bestDifference){bestDifference=difference;bestAngle=angle;angleSnapped=true;}
        }
        if(angleSnapped){
            const double radians=bestAngle*std::numbers::pi/180;
            result.point=*anchor+QPointF(std::cos(radians),std::sin(radians))*length(delta);
            const QString lbl = (int(bestAngle) % 180 == 0) ? QStringLiteral("0°") : QStringLiteral("90°");
            result.guides.append({*anchor,result.point,lbl,true});
        }
    }
    if(!angleSnapped) {
        double bestX=5.0/zoom,bestY=5.0/zoom;std::optional<QPointF> alignX,alignY;
        for(auto target:targets){const double dx=std::abs(raw.x()-target.x()),dy=std::abs(raw.y()-target.y());if(dx<bestX){bestX=dx;alignX=target;}if(dy<bestY){bestY=dy;alignY=target;}}
        if(alignX){result.point.setX(alignX->x());result.guides.append({*alignX,QPointF(alignX->x(),result.point.y()),QString(),false});}
        if(alignY){result.point.setY(alignY->y());result.guides.append({*alignY,QPointF(result.point.x(),alignY->y()),QString(),false});}
    }
    return result;
}
std::optional<ShapeItem> recognizeFreehandShape(const Stroke& stroke) {
    if(stroke.points.size()<2)return std::nullopt;
    QVector<QPointF> p;double pathLength=0;
    for(const auto& sample:stroke.points){const QPointF point(sample.x,sample.y);if(p.isEmpty()||length(point-p.back())>.1){if(!p.isEmpty())pathLength+=length(point-p.back());p.append(point);}}
    if(p.size()<2||pathLength<12)return std::nullopt;
    ShapeItem shape;shape.id=QUuid::createUuid().toString(QUuid::WithoutBraces);shape.color=stroke.color;shape.size=stroke.size;shape.opacity=stroke.opacity;shape.linePattern=stroke.linePattern;
    auto finish=[&](ShapeKind kind,QVector<QPointF> vertices,bool closed=false)->std::optional<ShapeItem>{shape.kind=kind;shape.vertices=vertices;shape.closed=closed;shape.bounds=computeShapeBounds(shape);return shape;};

    // 1. Straight Line
    const double chord=length(p.back()-p.front());double deviation=0;for(auto point:p)deviation=std::max(deviation,segmentDistance(point,p.front(),p.back()));
    if(chord/pathLength>.91&&deviation<std::max(3.5,chord*.065))return finish(ShapeKind::Line,{p.front(),p.back()});

    const QRectF bounds=vertexBounds(p);const double diagonal=std::hypot(bounds.width(),bounds.height());

    // 2. Square Root / Radical (down-up hook + horizontal roof)
    if(bounds.height()>8&&bounds.width()>15)for(qsizetype roof=2;roof+1<p.size();++roof) {
        if(p.back().x()-p[roof].x()<bounds.width()*.30)continue;
        double roofError=0;for(qsizetype j=roof;j<p.size();++j)roofError=std::max(roofError,std::abs(p[j].y()-p[roof].y()));
        if(roofError>std::max(4.0,bounds.height()*.16))continue;
        qsizetype valley=0;for(qsizetype j=1;j<roof;++j)if(p[j].y()>p[valley].y())valley=j;
        if(valley==0||p[valley].y()-p.front().y()<bounds.height()*.15||p[valley].y()-p[roof].y()<bounds.height()*.60)continue;
        if(p[valley].x()<=p.front().x()||p[roof].x()-p[valley].x()<bounds.height()*.08)continue;
        return finish(ShapeKind::Radical,{p[valley],p[roof],QPointF(p.back().x(),p[roof].y())});
    }

    // 3. Closed Shapes: Triangle, Rectangle/Quad, Circle
    const bool isClosed = chord <= std::max(18.0, diagonal * 0.36);
    if (isClosed && diagonal > 14.0) {
        // Compute circularity metrics first so we can distinguish a true circle from a rounded triangle/rect
        const QPointF center = bounds.center();
        double mean = 0;
        for (auto point : p) mean += length(point - center);
        mean /= double(p.size());
        double variance = 0, maxError = 0;
        for (auto point : p) {
            const double error = length(point - center) - mean;
            variance += error * error;
            maxError = std::max(maxError, std::abs(error));
        }
        const double rmsCircle = mean > 1e-4 ? std::sqrt(variance / double(p.size())) / mean : 1.0;
        const double aspect = bounds.height() > 1e-4 ? bounds.width() / bounds.height() : 10.0;
        const bool isVeryRound = (mean > 4 && aspect >= 0.78 && aspect <= 1.28 && rmsCircle < 0.105 && maxError / mean < 0.20);

        // Adaptive RDP sweep for Triangles (3 corners) and Rectangles/Quads (4 corners)
        if (!isVeryRound) {
            auto pruneShallowCorners = [](QVector<QPointF> poly) {
                bool changed = true;
                while (changed && poly.size() > 3) {
                    changed = false;
                    for (qsizetype i = 0; i < poly.size(); ++i) {
                        const QPointF prev = poly[(i + poly.size() - 1) % poly.size()];
                        const QPointF curr = poly[i];
                        const QPointF next = poly[(i + 1) % poly.size()];
                        const QPointF u = curr - prev, v = next - curr;
                        const double lu = length(u), lv = length(v);
                        if (lu < 1e-4 || lv < 1e-4 || dot(u, v) / (lu * lv) > 0.88) { // turning < ~28 deg
                            poly.removeAt(i);
                            changed = true;
                            break;
                        }
                    }
                }
                return poly;
            };

            for (double epsFactor : {0.055, 0.075, 0.095, 0.12, 0.145}) {
                auto corners = pruneShallowCorners(closedCorners(p, std::max(2.5, diagonal * epsFactor)));
                if (corners.size() == 3) {
                    const auto a = corners[1] - corners[0], b = corners[2] - corners[0];
                    if (std::abs(a.x() * b.y() - a.y() * b.x()) > bounds.width() * bounds.height() * 0.12) {
                        // 1. Si una esquina está cerca de 90° (~78°..102°), la deja exacta: triángulo rectángulo limpio.
                        for (int i = 0; i < 3; ++i) {
                            const QPointF c = corners[i], pa = corners[(i + 1) % 3], pb = corners[(i + 2) % 3];
                            const QPointF u = pa - c, v = pb - c;
                            const double lu = length(u), lv = length(v);
                            if (lu < 1e-3 || lv < 1e-3 || std::abs(dot(u, v) / (lu * lv)) > 0.21)
                                continue;
                            QPointF eu = u / lu;
                            // Si un cateto está cerca de horizontal/vertical (< 6°), nivelarlo exacto
                            if (std::abs(eu.y()) < 0.105) eu = QPointF(eu.x() >= 0 ? 1.0 : -1.0, 0.0);
                            else if (std::abs(eu.x()) < 0.105) eu = QPointF(0.0, eu.y() >= 0 ? 1.0 : -1.0);
                            const QPointF perp = QPointF(-eu.y(), eu.x());
                            QVector<QPointF> snapped = corners;
                            snapped[(i + 1) % 3] = c + eu * lu;
                            snapped[(i + 2) % 3] = c + (dot(perp, v) >= 0 ? perp : -perp) * lv;
                            return finish(ShapeKind::Triangle, snapped, true);
                        }
                        // 2. Si dos lados son casi iguales (< 13%), centrar el ápice: triángulo isósceles limpio.
                        for (int apex = 0; apex < 3; ++apex) {
                            const QPointF pApex = corners[apex];
                            const QPointF b0 = corners[(apex + 1) % 3];
                            const QPointF b1 = corners[(apex + 2) % 3];
                            const double l0 = length(pApex - b0), l1 = length(pApex - b1);
                            const double baseLen = length(b1 - b0);
                            if (baseLen < 1e-3 || std::max(l0, l1) < 1e-3) continue;
                            if (std::abs(l0 - l1) / std::max(l0, l1) <= 0.13) {
                                QPointF baseDir = (b1 - b0) / baseLen;
                                if (std::abs(baseDir.y()) < 0.105) baseDir = QPointF(baseDir.x() >= 0 ? 1.0 : -1.0, 0.0);
                                const QPointF mid = (b0 + b1) * 0.5;
                                const QPointF nb0 = mid - baseDir * (baseLen * 0.5);
                                const QPointF nb1 = mid + baseDir * (baseLen * 0.5);
                                const QPointF perp(-baseDir.y(), baseDir.x());
                                const double h = dot(pApex - mid, perp);
                                QVector<QPointF> snapped = corners;
                                snapped[apex] = mid + perp * h;
                                snapped[(apex + 1) % 3] = nb0;
                                snapped[(apex + 2) % 3] = nb1;
                                return finish(ShapeKind::Triangle, snapped, true);
                            }
                        }
                        return finish(ShapeKind::Triangle, corners, true);
                    }
                }
                if (corners.size() == 4) {
                    bool rightAngles = true;
                    for (int i = 0; i < 4; ++i) {
                        const auto a = corners[(i + 3) % 4] - corners[i];
                        const auto b = corners[(i + 1) % 4] - corners[i];
                        if (length(a) * length(b) < 1 || std::abs(dot(a, b) / (length(a) * length(b))) > 0.42)
                            rightAngles = false;
                    }
                    bool aligned = true;
                    for (int i = 0; i < 4; ++i) {
                        const auto d = corners[(i + 1) % 4] - corners[i];
                        if (std::min(std::abs(d.x()), std::abs(d.y())) > length(d) * 0.30)
                            aligned = false;
                    }
                    if (rightAngles && aligned) {
                        // Casi cuadrado (±10%) -> cuadrado exacto.
                        const double ratio = bounds.height() > 1e-4 ? bounds.width() / bounds.height() : 0.0;
                        if (ratio > 0.90 && ratio < 1.10) {
                            const double side = (bounds.width() + bounds.height()) * 0.5;
                            const QPointF tl = bounds.center() - QPointF(side, side) * 0.5;
                            return finish(ShapeKind::Rectangle, {tl, tl + QPointF(side, side)}, true);
                        }
                        return finish(ShapeKind::Rectangle, {bounds.topLeft(), bounds.bottomRight()}, true);
                    }
                    // Tilted quad / trapezoid / diamond -> clean closed 4-vertex editable polygon!
                    return finish(ShapeKind::Polyline, corners, true);
                }
                // 5-8 esquinas casi iguales (lados y giros parejos) -> polígono regular exacto.
                if (corners.size() >= 5 && corners.size() <= 8) {
                    const int n = int(corners.size());
                    QPointF centroid;
                    for (const QPointF& c : corners) centroid += c;
                    centroid /= double(n);
                    double meanR = 0, meanSide = 0;
                    for (int i = 0; i < n; ++i) {
                        meanR += length(corners[i] - centroid);
                        meanSide += length(corners[(i + 1) % n] - corners[i]);
                    }
                    meanR /= n;
                    meanSide /= n;
                    bool regular = meanR > 6.0;
                    for (int i = 0; regular && i < n; ++i) {
                        if (std::abs(length(corners[i] - centroid) - meanR) > meanR * 0.22
                            || std::abs(length(corners[(i + 1) % n] - corners[i]) - meanSide) > meanSide * 0.28)
                            regular = false;
                    }
                    if (regular) {
                        const QPointF d0 = corners[0] - centroid;
                        const double a0 = std::atan2(d0.y(), d0.x());
                        QVector<QPointF> poly;
                        for (int i = 0; i < n; ++i) {
                            const double a = a0 + 2.0 * M_PI * i / n;
                            poly.append(centroid + QPointF(std::cos(a), std::sin(a)) * meanR);
                        }
                        return finish(ShapeKind::Polyline, poly, true);
                    }
                }
            }
        }

        // Circle fallback
        if (mean > 3 && aspect >= 0.70 && aspect <= 1.42 && rmsCircle < 0.16 && maxError / mean < 0.30) {
            return finish(ShapeKind::Circle, {center, center + QPointF(mean, 0)}, true);
        }
    }

    // 4. Smooth Open Curve / Arc (e.g. curved arrows, angle arcs, smooth curved lines!)
    if (!isClosed && chord > 14.0 && deviation >= std::max(6.0, chord * 0.06)) {
        // Pick the point at 50% arc length as the on-curve midpoint handle
        double halfLen = pathLength * 0.5, acc = 0.0;
        QPointF midPt = p[p.size() / 2];
        for (qsizetype i = 1; i < p.size(); ++i) {
            const double seg = length(p[i] - p[i - 1]);
            if (acc + seg >= halfLen && seg > 1e-6) {
                const double t = (halfLen - acc) / seg;
                midPt = p[i - 1] + (p[i] - p[i - 1]) * t;
                break;
            }
            acc += seg;
        }
        const QPointF p0 = p.front(), p2 = p.back();
        const QPointF ctrl = 2.0 * midPt - 0.5 * (p0 + p2);
        // Sample the fitted quadratic curve and verify the freehand stroke stays close to it
        QVector<QPointF> fitted;
        constexpr int kSamples = 24;
        fitted.reserve(kSamples + 1);
        for (int i = 0; i <= kSamples; ++i) {
            const double t = double(i) / kSamples, u = 1.0 - t;
            fitted.append(u * u * p0 + 2.0 * u * t * ctrl + t * t * p2);
        }
        double maxCurveErr = 0.0;
        for (const auto& pt : p) {
            double minD = std::numeric_limits<double>::max();
            for (int i = 0; i < kSamples; ++i) minD = std::min(minD, segmentDistance(pt, fitted[i], fitted[i + 1]));
            maxCurveErr = std::max(maxCurveErr, minD);
        }
        if (maxCurveErr <= std::max(10.0, pathLength * 0.11)) {
            return finish(ShapeKind::Curve, {p0, midPt, p2}, false);
        }
    }

    return std::nullopt;
}
} // namespace finily
