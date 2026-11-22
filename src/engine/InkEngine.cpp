#include "InkEngine.h"
#include <QPainterPath>
#include <QPolygonF>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {
QPointF pos(const StrokePoint& p) { return {p.x, p.y}; }
double dot(QPointF a, QPointF b) { return a.x()*b.x()+a.y()*b.y(); }
double length(QPointF p) { return std::hypot(p.x(), p.y()); }
double distanceSquared(QPointF p, QPointF a, QPointF b) {
    const QPointF d=b-a;
    const double t=dot(d,d)>1e-16 ? std::clamp(dot(p-a,d)/dot(d,d),0.0,1.0) : 0.0;
    return dot(p-a-t*d,p-a-t*d);
}
StrokePoint mix(const StrokePoint& a, const StrokePoint& b, double t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.pressure+(b.pressure-a.pressure)*t,
            a.timestampMs+qint64(double(b.timestampMs-a.timestampMs)*t),
            a.tiltX+(b.tiltX-a.tiltX)*t,a.tiltY+(b.tiltY-a.tiltY)*t};
}
// Cubic Hermite form of Catmull-Rom; duplicate endpoints give rounded starts.
QPainterPath curve(const QVector<StrokePoint>& points) {
    QPainterPath path;
    if (points.isEmpty()) return path;
    path.moveTo(pos(points.front()));
    for (qsizetype i=0;i+1<points.size();++i) {
        const QPointF a=pos(points[std::max<qsizetype>(0,i-1)]), b=pos(points[i]);
        const QPointF c=pos(points[i+1]), d=pos(points[std::min(i+2,points.size()-1)]);
        path.cubicTo(b+(c-a)/6.0,c-(d-b)/6.0,c);
    }
    return path;
}
struct InkSample {
    StrokePoint point;
    double speed = 0.0; // World pixels per millisecond, independent of subdivision.
};
QVector<InkSample> sampledCurve(const QVector<StrokePoint>& points) {
    QVector<InkSample> samples;
    if (points.isEmpty()) return samples;
    QVector<double> speeds(points.size(),0.0);
    for (qsizetype i=0;i<points.size();++i) {
        const qsizetype before=std::max<qsizetype>(0,i-1),after=std::min(i+1,points.size()-1);
        const double elapsed=double(points[after].timestampMs-points[before].timestampMs);
        // Old documents may lack usable timing. Do not invent extreme flicks.
        if (elapsed>0) {
            double distance=0;
            for (qsizetype j=before+1;j<=after;++j) distance+=length(pos(points[j])-pos(points[j-1]));
            speeds[i]=distance/elapsed;
        }
    }
    samples.append({points.front(),speeds.front()});
    for (qsizetype i=0;i+1<points.size();++i) {
        const QPointF a=pos(points[std::max<qsizetype>(0,i-1)]),b=pos(points[i]);
        const QPointF c=pos(points[i+1]),d=pos(points[std::min(i+2,points.size()-1)]);
        const QPointF c1=b+(c-a)/6.0,c2=c-(d-b)/6.0;
        const int count=std::max(1,int(std::ceil((length(c1-b)+length(c2-c1)+length(c-c2))/1.5)));
        for (int j=1;j<=count;++j) {
            const double t=double(j)/count,u=1-t;
            const QPointF p=u*u*u*b+3*u*u*t*c1+3*u*t*t*c2+t*t*t*c;
            auto point=mix(points[i],points[i+1],t);
            const double blend=t*t*(3-2*t);
            point.pressure=points[i].pressure+(points[i+1].pressure-points[i].pressure)*blend;
            point.x=p.x();point.y=p.y();
            samples.append({point,speeds[i]+(speeds[i+1]-speeds[i])*blend});
        }
    }
    return samples;
}
double pressureFactor(double pressure,PressureProfile profile) {
    double p=std::clamp(pressure,0.0,1.0);
    switch (profile) {
    case PressureProfile::Light:
        // Extra light-touch gain gives a confident body at 0.15--0.35 pressure.
        return 0.10+1.02*std::pow(std::min(1.0,p*1.55),0.48);
    case PressureProfile::Normal: return 0.08+1.07*std::pow(p,0.70);
    case PressureProfile::Firm: return 0.06+1.19*std::pow(p,1.05);
    }
    return 0.85;
}
void drawLiquidInk(QPainter& painter,const Stroke& stroke) {
    const auto samples=sampledCurve(stroke.points);
    QVector<double> distances(samples.size(),0.0),radii(samples.size(),0.0);
    for (qsizetype i=1;i<samples.size();++i)
        distances[i]=distances[i-1]+length(pos(samples[i].point)-pos(samples[i-1].point));
    const double total=distances.back(),taperLength=std::min(8.0,total*0.35);
    for (qsizetype i=0;i<samples.size();++i) {
        const auto& sample=samples[i];
        const double fast=sample.speed/(sample.speed+0.7);
        double factor=0.85;
        if (stroke.tool==ToolType::Ballpoint) {
            const double pressure=stroke.hasHardwarePressure?std::clamp(sample.point.pressure,0.0,1.0):0.5;
            factor=0.94+0.08*pressure-0.04*fast;
            if (taperLength>1e-6) {
                const double t=std::clamp(std::min(distances[i],total-distances[i])/taperLength,0.0,1.0);
                factor*=0.22+0.78*t*t*(3-2*t);
            }
        } else {
            if (stroke.hasHardwarePressure) factor=pressureFactor(sample.point.pressure,stroke.pressureProfile);
            factor*=1.0-0.14*fast;
            const qsizetype before=std::max<qsizetype>(0,i-1),after=std::min(i+1,samples.size()-1);
            const QPointF direction=pos(samples[after].point)-pos(samples[before].point);
            if (length(direction)>1e-8) {
                const double heading=std::atan2(direction.y(),direction.x());
                const double tilt=std::hypot(sample.point.tiltX,sample.point.tiltY);
                double nib=std::numbers::pi/4;
                if (tilt>1e-6) nib+=std::min(1.0,tilt/45.0)*std::remainder(std::atan2(sample.point.tiltY,sample.point.tiltX)-nib,std::numbers::pi);
                // At most +/-10%; vertical downstrokes get a gentle extra body.
                factor*=1.0-0.06*std::cos(2*(heading-nib))+0.04*std::sin(heading);
            }
        }
        radii[i]=std::max(0.0005,stroke.size*factor/2);
    }
    QPainterPath envelope;
    envelope.setFillRule(Qt::WindingFill);
    for (qsizetype i=0;i<samples.size();++i) {
        const QPointF center=pos(samples[i].point);
        envelope.addEllipse(center,radii[i],radii[i]);
        if (i==0) continue;
        const QPointF previous=pos(samples[i-1].point),delta=center-previous;
        const double d=length(delta),r0=radii[i-1],r1=radii[i];
        // A contained/coincident disk already has its complete outer envelope.
        if (d<=std::abs(r1-r0)+1e-9) continue;
        const QPointF axis=delta/d,normal(-axis.y(),axis.x());
        const double cosine=std::clamp((r0-r1)/d,-1.0,1.0);
        const double sine=std::sqrt(std::max(0.0,1-cosine*cosine));
        const QPointF n0=axis*cosine-normal*sine,n1=axis*cosine+normal*sine;
        // Same clockwise winding as addEllipse: overlapping loops stay filled.
        envelope.moveTo(previous+n0*r0);
        envelope.lineTo(center+n0*r1);
        envelope.lineTo(center+n1*r1);
        envelope.lineTo(previous+n1*r0);
        envelope.closeSubpath();
    }
    // One fill composites opacity exactly once, including tight cursive crossings.
    painter.setPen(Qt::NoPen);painter.setBrush(stroke.color);painter.drawPath(envelope);
}
void arrowHead(QPainter& painter, QPointF tip, QPointF previous, double size, QColor color) {
    QPointF v=tip-previous; const double n=length(v); if (n<1e-8) return;
    v/=n; const QPointF normal(-v.y(),v.x());
    const double h=std::max(8.0,size*4.0), w=h*0.48;
    painter.setPen(Qt::NoPen); painter.setBrush(color);
    painter.drawPolygon(QPolygonF{tip,tip-v*h+normal*w,tip-v*(h*0.70),tip-v*h-normal*w});
}
}
void VelocityGaussianStabilizer::reset() { m_recent.clear();m_smoothedPressure.reset(); }
void VelocityGaussianStabilizer::setStrength(double s) { m_strength=std::isfinite(s)?std::clamp(s,0.0,1.0):0.0; reset(); }
StrokePoint VelocityGaussianStabilizer::filter(const StrokePoint& point) {
    StrokePoint out=point;
    const double pressure=std::clamp(point.pressure,0.0,1.0);
    m_smoothedPressure=m_smoothedPressure?0.55*pressure+0.45*(*m_smoothedPressure):pressure;
    out.pressure=*m_smoothedPressure;
    if (m_strength==0.0) return out;
    m_recent.append(point);
    while (m_recent.size()>5) m_recent.removeFirst();
    if (m_recent.size()<2) return out;
    const auto& prev=m_recent[m_recent.size()-2];
    const double dt=std::max(1.0,double(point.timestampMs-prev.timestampMs));
    const double velocity=length(pos(point)-pos(prev))/dt;
    // At handwriting speed the window collapses to the current sample. A bounded
    // time window avoids the accumulating phase lag that closes small cursive loops.
    const double sigma=std::max(0.05,3.0*m_strength*std::exp(-velocity*velocity/0.09));
    double sum=0,x=0,y=0;
    for (qsizetype i=0;i<m_recent.size();++i) {
        const auto& p=m_recent[i];
        const double age=std::max(double(m_recent.size()-1-i),double(point.timestampMs-p.timestampMs));
        const double w=std::exp(-0.5*age*age/(sigma*sigma));
        x+=p.x*w;y+=p.y*w;sum+=w;
    }
    out.x=x/sum; out.y=y/sum; return out;
}
QRectF computeBounds(const QVector<StrokePoint>& points,double padding) {
    if (points.isEmpty()) return {};
    double l=points.front().x,r=l,t=points.front().y,b=t;
    for (const auto& p:points) { l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y); }
    padding=std::max(0.0,padding);
    return QRectF(QPointF(l-padding,t-padding),QPointF(r+padding,b+padding));
}
bool hitTestStroke(const Stroke& stroke,const QPointF& p,double radius) {
    if (stroke.points.isEmpty()) return false;
    radius=std::max(0.0,radius)+std::max(0.0,stroke.size)*0.7;
    const QRectF bounds=stroke.bounds.isEmpty()?computeBounds(stroke.points):stroke.bounds;
    if (!bounds.adjusted(-radius,-radius,radius,radius).contains(p)) return false;
    if (stroke.points.size()==1) return distanceSquared(p,pos(stroke.points[0]),pos(stroke.points[0]))<=radius*radius;
    for (qsizetype i=1;i<stroke.points.size();++i)
        if (distanceSquared(p,pos(stroke.points[i-1]),pos(stroke.points[i]))<=radius*radius) return true;
    return false;
}
std::optional<QVector<Stroke>> eraseStrokePartially(const Stroke& stroke,const QPointF& center,double radius) {
    if (!hitTestStroke(stroke,center,radius)) return std::nullopt;
    QVector<Stroke> result;
    if (stroke.points.size()==1) return result;
    const double r=std::max(0.0,radius)+std::max(0.0,stroke.size)*0.7;
    QVector<StrokePoint> current; bool touched=false;
    auto flush=[&] {
        if (current.isEmpty()) return;
        Stroke s=stroke;s.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.points=current;s.bounds=computeBounds(s.points,s.size*0.7);result.append(s);current.clear();
    };
    auto append=[&](const StrokePoint& p) {
        if (current.isEmpty() || length(pos(p)-pos(current.back()))>1e-8) current.append(p);
    };
    for (qsizetype i=1;i<stroke.points.size();++i) {
        const auto& a=stroke.points[i-1]; const auto& b=stroke.points[i];
        const QPointF d=pos(b)-pos(a),f=pos(a)-center;
        const double aa=dot(d,d),bb=2*dot(f,d),cc=dot(f,f)-r*r;
        if (aa<1e-16) {
            if (cc<=0) { touched=true;flush(); } else { append(a);append(b); }
            continue;
        }
        const double disc=bb*bb-4*aa*cc;
        if (disc<0) { append(a);append(b);continue; }
        const double lo=std::max(0.0,(-bb-std::sqrt(disc))/(2*aa));
        const double hi=std::min(1.0,(-bb+std::sqrt(disc))/(2*aa));
        if (hi-lo<1e-10 || hi<0 || lo>1) { append(a);append(b);continue; }
        touched=true;
        if (lo>0) { append(a);append(mix(a,b,lo)); }
        flush();
        if (hi<1) { append(mix(a,b,hi));append(b); }
    }
    flush();
    if (!touched) return std::nullopt;
    return result;
}
void drawStroke(QPainter& painter,const Stroke& stroke) {
    if (stroke.points.isEmpty() || stroke.tool==ToolType::Eraser || stroke.tool==ToolType::Hand) return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setOpacity(std::clamp(stroke.opacity,0.0,1.0)*(stroke.tool==ToolType::Highlighter?0.35:1.0));
    const double size=std::max(0.01,stroke.size);
    if ((stroke.tool==ToolType::Pen || stroke.tool==ToolType::Ballpoint) && stroke.linePattern==LinePattern::Solid) {
        drawLiquidInk(painter,stroke);
    } else {
        QPen pen(stroke.color,size,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
        if (stroke.linePattern==LinePattern::Dashed) pen.setDashPattern({4.0,2.5});
        if (stroke.linePattern==LinePattern::Dotted) pen.setDashPattern({0.01,2.2});
        painter.setPen(pen);painter.setBrush(Qt::NoBrush);
        if (stroke.points.size()==1) painter.drawPoint(pos(stroke.points.front()));
        else painter.drawPath(curve(stroke.points));
        if (stroke.linePattern==LinePattern::Arrow || stroke.linePattern==LinePattern::DoubleArrow) {
            const auto& p=stroke.points;
            qsizetype last=p.size()-2;
            while (last>=0 && length(pos(p.back())-pos(p[last]))<1e-6) --last;
            if (last>=0) arrowHead(painter,pos(p.back()),pos(p[last]),size,stroke.color);
            if (stroke.linePattern==LinePattern::DoubleArrow) {
                qsizetype first=1;while (first<p.size() && length(pos(p.front())-pos(p[first]))<1e-6) ++first;
                if (first<p.size()) arrowHead(painter,pos(p.front()),pos(p[first]),size,stroke.color);
            }
        }
    }
    painter.restore();
}
void drawBackgroundTemplate(QPainter& painter,BackgroundTemplate tpl,const ThemeConfig& theme,const Viewport& vp,const QRectF& rect) {
    if (tpl==BackgroundTemplate::Blank || vp.zoom<=0 || rect.isEmpty()) return;
    painter.save();painter.setRenderHint(QPainter::Antialiasing,false);
    double step=32;while (step*vp.zoom<16) step*=2;
    QColor color=theme.patternColor;
    if (tpl==BackgroundTemplate::Ruled && theme.id==ThemeId::PaperLight) color=QColor(137,181,231,150);
    QPen pen(color);pen.setCosmetic(true);pen.setWidthF(1);painter.setPen(pen);
    const double left=std::floor(rect.left()/step)*step,top=std::floor(rect.top()/step)*step;
    if (tpl==BackgroundTemplate::Dots) {
        painter.setRenderHint(QPainter::Antialiasing,true);painter.setPen(Qt::NoPen);painter.setBrush(color);
        for (double x=left;x<=rect.right();x+=step) for(double y=top;y<=rect.bottom();y+=step)
            painter.drawEllipse(QPointF(x,y),1.0/vp.zoom,1.0/vp.zoom);
    } else {
        for(double y=top;y<=rect.bottom();y+=step) painter.drawLine(QPointF(rect.left(),y),QPointF(rect.right(),y));
        if (tpl==BackgroundTemplate::Grid)
            for(double x=left;x<=rect.right();x+=step) painter.drawLine(QPointF(x,rect.top()),QPointF(x,rect.bottom()));
    }
    painter.restore();
}
} // namespace finily
