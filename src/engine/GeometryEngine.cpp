#include "GeometryEngine.h"
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
double segmentDistance(QPointF p,QPointF a,QPointF b) {
    const auto d=b-a;const double d2=dot(d,d);
    return length(p-a-d*(d2>1e-12?std::clamp(dot(p-a,d)/d2,0.0,1.0):0.0));
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
QPainterPath shapePath(const ShapeItem& shape) {
    QPainterPath path;const auto& v=shape.vertices;
    if(v.isEmpty())return path;
    if(shape.kind==ShapeKind::Circle&&v.size()>=2) {
        const double r=length(v[1]-v[0]);path.addEllipse(v[0],r,r);return path;
    }
    if((shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle)&&v.size()>=2) {
        const QRectF rect=QRectF(v[0],v[1]).normalized();
        if(shape.kind==ShapeKind::Ellipse)path.addEllipse(rect);else path.addRect(rect);
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
    const bool supportsArrow = (shape.kind==ShapeKind::Line)
        || (shape.kind==ShapeKind::Polyline&&!shape.closed)
        || (shape.kind==ShapeKind::Curve&&shape.vertices.size()>=3);
    if(!supportsArrow||shape.vertices.size()<2)return path;
    if(shape.linePattern!=LinePattern::Arrow&&shape.linePattern!=LinePattern::DoubleArrow)return path;
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
        if(shape.linePattern==LinePattern::DoubleArrow) head(v[0], ctrl);
        return path;
    }
    qsizetype end=v.size()-2;
    while(end>=0&&length(v.back()-v[end])<1e-8)--end;
    if(end>=0)head(v.back(),v[end]);
    if(shape.linePattern==LinePattern::DoubleArrow) {
        qsizetype start=1;while(start<v.size()&&length(v[start]-v.front())<1e-8)++start;
        if(start<v.size())head(v.front(),v[start]);
    }
    return path;
}
QVector<QPointF> snapTargets(const ShapeItem& shape) {
    auto targets=getShapeHandles(shape);
    if(shape.vertices.isEmpty())return targets;
    if(shape.kind==ShapeKind::Circle)targets.append(shape.vertices[0]);
    else if(shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle)targets.append(QRectF(shape.vertices[0],shape.vertices[1]).center());
    else {QPointF center;for(auto p:shape.vertices)center+=p;targets.append(center/double(shape.vertices.size()));}
    return targets;
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
    const double padding=std::max(0.0,shape.size)/2;
    return bounds.adjusted(-padding,-padding,padding,padding);
}
QVector<QPointF> getShapeHandles(const ShapeItem& shape) {
    const auto& v=shape.vertices;
    if((shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Axes)&&v.size()>=2){const QRectF r=QRectF(v[0],v[1]).normalized();return {r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};}
    return v;
}
void applyHandleDrag(ShapeItem& shape,int index,const QPointF& point) {
    auto& v=shape.vertices;
    if(index<0)return;
    if((shape.kind==ShapeKind::Rectangle||shape.kind==ShapeKind::Ellipse||shape.kind==ShapeKind::Axes)&&v.size()>=2) {
        const auto handles=getShapeHandles(shape);if(index>=handles.size())return;
        const QRectF rect=QRectF(point,handles[(index+2)%4]).normalized();v={rect.topLeft(),rect.bottomRight()};
    } else if(index<v.size()) {
        if(shape.kind==ShapeKind::RightAngle&&v.size()>=3) {
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
bool hitTestShape(const ShapeItem& shape,const QPointF& point,double tolerance) {
    const double radius=std::max(0.0,tolerance);
    if(!computeShapeBounds(shape).adjusted(-radius,-radius,radius,radius).contains(point))return false;
    QPainterPathStroker stroker;stroker.setWidth(std::max(.01,shape.size+2*radius));stroker.setCapStyle(Qt::RoundCap);stroker.setJoinStyle(Qt::RoundJoin);
    const auto path=shapePath(shape);return stroker.createStroke(path).contains(point)||arrowPath(shape).contains(point);
}
int hitTestHandle(const ShapeItem& shape,const QPointF& point,double radius) {
    const auto handles=getShapeHandles(shape);int result=-1;double best=std::max(0.0,radius);
    for(qsizetype i=0;i<handles.size();++i){const double d=length(point-handles[i]);if(d<=best){best=d;result=int(i);}}
    return result;
}
void drawShape(QPainter& painter,const ShapeItem& shape) {
    if(shape.vertices.isEmpty())return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.setOpacity(std::clamp(shape.opacity,0.0,1.0));
    QPen pen(shape.color,std::max(.01,shape.size),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
    if(shape.linePattern==LinePattern::Dashed)pen.setDashPattern({4.0,2.5});
    if(shape.linePattern==LinePattern::Dotted)pen.setDashPattern({.01,2.2});
    painter.setPen(pen);painter.setBrush(Qt::NoBrush);painter.drawPath(shapePath(shape));
    painter.setPen(Qt::NoPen);painter.setBrush(shape.color);painter.drawPath(arrowPath(shape));painter.restore();
}
void drawSelectionOverlay(QPainter& painter,const ShapeItem& shape,const ThemeConfig& theme,double zoom) {
    zoom=std::max(.01,zoom);painter.save();painter.setRenderHint(QPainter::Antialiasing);
    QColor faint=theme.accent;faint.setAlpha(100);QPen guide(faint,1/zoom,Qt::DashLine);painter.setPen(guide);painter.setBrush(Qt::NoBrush);
    painter.drawRect(computeShapeBounds(shape).adjusted(-3/zoom,-3/zoom,3/zoom,3/zoom));
    painter.setPen(QPen(theme.accent,1.6/zoom));painter.setBrush(theme.isDark?theme.uiSurface:QColor(Qt::white));
    for(auto point:getShapeHandles(shape))painter.drawEllipse(point,6/zoom,6/zoom);
    painter.restore();
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
SnapResult snapPointWithGuides(const QPointF& raw,const std::optional<QPointF>& anchor,const QVector<ShapeItem>& shapes,const QString& excluded,double zoom) {
    zoom=std::max(.01,zoom);SnapResult result{raw,{}};QVector<QPointF> targets;
    for(const auto& shape:shapes)if(shape.id!=excluded)targets+=snapTargets(shape);

    // 1. Vertex & circle-center magnet (8px screen)
    double nearest=8.0/zoom;std::optional<QPointF> vertex;
    for(auto target:targets){const double d=length(raw-target);if(d<nearest){nearest=d;vertex=target;}}
    if(vertex){result.point=*vertex;result.guides.append({*vertex,*vertex,QString(),false});return result;}

    // 2. Circumference & shape-edge magnet ("pap!" snap onto ANY point of a circle rim or polygon edge!)
    double bestBoundaryDist = 10.0 / zoom;
    std::optional<QPointF> boundarySnap;
    auto projectSegment = [](QPointF p, QPointF a, QPointF b) -> QPointF {
        const auto d = b - a;
        const double d2 = dot(d, d);
        const double t = d2 > 1e-12 ? std::clamp(dot(p - a, d) / d2, 0.0, 1.0) : 0.0;
        return a + d * t;
    };
    for (const auto& shape : shapes) {
        if (shape.id == excluded || shape.vertices.size() < 2) continue;
        if (shape.kind == ShapeKind::Circle) {
            const QPointF c = shape.vertices[0];
            const double r = length(shape.vertices[1] - c);
            if (r > 2.0) {
                const QPointF dir = raw - c;
                const double distC = length(dir);
                const double rimErr = std::abs(distC - r);
                if (distC > 1e-6 && rimErr < bestBoundaryDist) {
                    bestBoundaryDist = rimErr;
                    boundarySnap = c + (dir / distC) * r;
                }
            }
        } else if (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Triangle || shape.kind == ShapeKind::Polyline) {
            const auto& v = shape.vertices;
            const qsizetype segCount = (shape.kind == ShapeKind::Triangle || (shape.kind == ShapeKind::Polyline && shape.closed)) ? v.size() : v.size() - 1;
            for (qsizetype i = 0; i < segCount; ++i) {
                const QPointF proj = projectSegment(raw, v[i], v[(i + 1) % v.size()]);
                const double d = length(raw - proj);
                if (d < bestBoundaryDist) {
                    bestBoundaryDist = d;
                    boundarySnap = proj;
                }
            }
        } else if (shape.kind == ShapeKind::Rectangle) {
            const auto corners = getShapeHandles(shape);
            for (qsizetype i = 0; i < corners.size(); ++i) {
                const QPointF proj = projectSegment(raw, corners[i], corners[(i + 1) % corners.size()]);
                const double d = length(raw - proj);
                if (d < bestBoundaryDist) {
                    bestBoundaryDist = d;
                    boundarySnap = proj;
                }
            }
        }
    }
    if (boundarySnap) {
        result.point = *boundarySnap;
        result.guides.append({*boundarySnap, *boundarySnap, QString(), false});
        return result;
    }

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
                        return finish(ShapeKind::Rectangle, {bounds.topLeft(), bounds.bottomRight()}, true);
                    }
                    // Tilted quad / trapezoid / diamond -> clean closed 4-vertex editable polygon!
                    return finish(ShapeKind::Polyline, corners, true);
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
