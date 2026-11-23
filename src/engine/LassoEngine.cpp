#include "LassoEngine.h"
#include "GeometryEngine.h"
#include "InkEngine.h"
#include <QElapsedTimer>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {
double length(QPointF p) {return std::hypot(p.x(),p.y());}
QVector<QPointF> handles(QRectF r,double zoom) {return {r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft(),r.topLeft()+QPointF(r.width()/2,-24/std::max(.01,zoom))};}
template<class Map> void transform(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,Map map,double widthScale=1) {
    // Callers supply a fresh copy of the immutable drag-start document each frame.
    for(auto& s:strokes)if(strokeIds.contains(s.id)) {
        for(auto& p:s.points){const auto q=map(QPointF(p.x,p.y));p.x=q.x();p.y=q.y();}
        s.size=std::clamp(s.size*widthScale,.1,200.0);s.bounds=computeBounds(s.points,s.size*.7);
    }
    for(auto& s:shapes)if(!s.locked&&shapeIds.contains(s.id)) {
        for(auto& p:s.vertices)p=map(p);
        s.size=std::clamp(s.size*widthScale,.1,200.0);s.bounds=computeShapeBounds(s);
    }
}
// The shared model has no rotation field for axis-aligned primitives. Preserve
// their actual outline as a closed/open polyline before applying a rotation.
void makeRotatable(ShapeItem& s) {
    if(s.vertices.size()<2)return;
    if(s.kind==ShapeKind::Rectangle){s.vertices=getShapeHandles(s);s.kind=ShapeKind::Polyline;s.closed=true;}
    else if(s.kind==ShapeKind::Ellipse) {
        const auto r=QRectF(s.vertices[0],s.vertices[1]).normalized();s.vertices.clear();
        for(int i=0;i<128;++i){const double a=2*std::numbers::pi*i/128;s.vertices.append(r.center()+QPointF(std::cos(a)*r.width()/2,std::sin(a)*r.height()/2));}
        s.kind=ShapeKind::Polyline;s.closed=true;
    } else if(s.kind==ShapeKind::Radical&&s.vertices.size()==3) {
        const auto base=s.vertices[0],roof=s.vertices[1];const double h=std::max(1.0,std::abs(base.y()-roof.y())),direction=base.y()>=roof.y()?1.0:-1.0;
        const auto tick=base+QPointF(-h*.23,-direction*h*.36);
        s.vertices={tick+QPointF(-h*.13,direction*h*.08),tick,base,roof,QPointF(s.vertices[2].x(),roof.y())};s.kind=ShapeKind::Polyline;s.closed=false;
    }
}
}
void selectByLasso(const QPolygonF& poly,const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,QSet<QString>& strokeIds,QSet<QString>& shapeIds) {
    strokeIds.clear();shapeIds.clear();if(poly.size()<3)return;
    for(const auto& s:strokes) {
        if(s.points.isEmpty())continue;
        qsizetype inside=0;QPointF center;
        for(const auto& p:s.points){const QPointF q(p.x,p.y);center+=q;if(poly.containsPoint(q,Qt::OddEvenFill))++inside;}
        center/=double(s.points.size());
        if(double(inside)/s.points.size()>=.40||poly.containsPoint(center,Qt::OddEvenFill))strokeIds.insert(s.id);
    }
    for(const auto& s:shapes) {
        if(s.vertices.isEmpty()||!s.shadeVisible)continue;
        bool selected=poly.containsPoint(computeShapeBounds(s).center(),Qt::OddEvenFill);
        for(auto p:getShapeHandles(s))selected=selected||poly.containsPoint(p,Qt::OddEvenFill);
        if(selected)shapeIds.insert(s.id);
    }
}
QRectF computeSelectionBounds(const QVector<Stroke>& strokes,const QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds) {
    QRectF bounds;bool first=true;auto add=[&](QRectF r){if(first){bounds=r;first=false;}else bounds=bounds.united(r);};
    for(const auto& s:strokes)if(strokeIds.contains(s.id)&&!s.points.isEmpty())add(computeBounds(s.points,s.size*.7));
    for(const auto& s:shapes)if(shapeIds.contains(s.id)&&!s.vertices.isEmpty()&&s.shadeVisible)add(computeShapeBounds(s));
    return bounds;
}
void translateSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& delta) {
    transform(strokes,shapes,strokeIds,shapeIds,[&](QPointF p){return p+delta;});
}
void scaleSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& anchor,double sx,double sy) {
    if(!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0)return;
    const double widthScale=std::sqrt(sx*sy);
    transform(strokes,shapes,strokeIds,{},[&](QPointF p){return anchor+QPointF((p.x()-anchor.x())*sx,(p.y()-anchor.y())*sy);},widthScale);
    for(auto& s:shapes)if(!s.locked&&shapeIds.contains(s.id)) {
        s.size=std::clamp(s.size*widthScale,.1,200.0);
        scaleShape(s, anchor, sx, sy);
    }
}
void rotateSelection(QVector<Stroke>& strokes,QVector<ShapeItem>& shapes,const QSet<QString>& strokeIds,const QSet<QString>& shapeIds,const QPointF& center,double angle) {
    if(!std::isfinite(angle))return;
    if(std::abs(angle)<1e-12)return;
    for(auto& s:shapes)if(!s.locked&&shapeIds.contains(s.id))makeRotatable(s);
    const double c=std::cos(angle),s=std::sin(angle);
    transform(strokes,shapes,strokeIds,shapeIds,[&](QPointF p){const auto d=p-center;return center+QPointF(d.x()*c-d.y()*s,d.x()*s+d.y()*c);});
}
bool straightenSelectedStrokes(QVector<Stroke>& strokes,const QSet<QString>& ids) {
    struct SelectedStroke { qsizetype index; QRectF bounds; };
    QVector<SelectedStroke> selected;
    QVector<double> allHeights;
    for (qsizetype i = 0; i < strokes.size(); ++i) {
        if (ids.contains(strokes[i].id) && !strokes[i].points.isEmpty()) {
            const QRectF b = computeBounds(strokes[i].points);
            selected.append({i, b});
            if (b.height() > 2.0) allHeights.append(b.height());
        }
    }
    if (selected.isEmpty()) return false;

    auto median = [](QVector<double> values) -> double {
        if (values.isEmpty()) return 0.0;
        std::sort(values.begin(), values.end());
        const qsizetype mid = values.size() / 2;
        return (values.size() % 2) ? values[mid] : (values[mid - 1] + values[mid]) * 0.5;
    };

    const double baseH = std::clamp(allHeights.isEmpty() ? 24.0 : median(allHeights), 10.0, 140.0);

    // 1. Partition selected strokes into horizontal text rows (in case user lassoed 2+ lines).
    std::stable_sort(selected.begin(), selected.end(), [](const SelectedStroke& a, const SelectedStroke& b) {
        return a.bounds.center().y() < b.bounds.center().y();
    });
    QVector<QVector<SelectedStroke>> rows;
    for (const auto& item : selected) {
        bool placed = false;
        for (auto& row : rows) {
            double sumCy = 0.0;
            for (const auto& r : row) sumCy += r.bounds.center().y();
            const double rowCy = sumCy / double(row.size());
            if (std::abs(item.bounds.center().y() - rowCy) <= baseH * 1.35) {
                row.append(item);
                placed = true;
                break;
            }
        }
        if (!placed) rows.append({item});
    }

    auto result = strokes;
    bool changed = false;

    // 2. Within each row, group ONLY overlapping strokes belonging to the same letter
    //    (e.g., t + crossbar, i + dot, ñ + tilde, = top + bottom), NEVER rotating letters
    //    so vertical stems (h, l, t) stay 100% upright, and level every letter's bottom
    //    flush onto ONE crisp horizontal baseline!
    for (auto& row : rows) {
        std::stable_sort(row.begin(), row.end(), [](const SelectedStroke& a, const SelectedStroke& b) {
            return a.bounds.left() < b.bounds.left();
        });

        struct LetterCluster {
            QVector<qsizetype> indices;
            QRectF bounds;
        };
        QVector<LetterCluster> letters;

        for (const auto& item : row) {
            bool merged = false;
            for (auto& letter : letters) {
                const double overlapX = std::min(letter.bounds.right(), item.bounds.right())
                                      - std::max(letter.bounds.left(), item.bounds.left());
                const double minW = std::max(1.0, std::min(letter.bounds.width(), item.bounds.width()));
                const bool centerInside =
                    (item.bounds.center().x() >= letter.bounds.left() && item.bounds.center().x() <= letter.bounds.right()) ||
                    (letter.bounds.center().x() >= item.bounds.left() && letter.bounds.center().x() <= item.bounds.right());

                // Merge only if strokes genuinely overlap horizontally (same character/symbol)
                if (overlapX > minW * 0.25 || centerInside) {
                    letter.indices.append(item.index);
                    const double minL = std::min(letter.bounds.left(), item.bounds.left());
                    const double minT = std::min(letter.bounds.top(), item.bounds.top());
                    const double maxR = std::max(letter.bounds.right(), item.bounds.right());
                    const double maxB = std::max(letter.bounds.bottom(), item.bounds.bottom());
                    letter.bounds = QRectF(QPointF(minL, minT), QPointF(maxR, maxB));
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                letters.append(LetterCluster{{item.index}, item.bounds});
            }
        }

        if (letters.size() < 2) continue;

        QVector<double> letterHeights;
        for (const auto& l : letters) {
            if (l.bounds.height() >= baseH * 0.35) {
                letterHeights.append(l.bounds.height());
            }
        }
        const double rowLetterH = letterHeights.isEmpty() ? baseH : median(letterHeights);

        QVector<double> bottoms;
        for (const auto& l : letters) {
            const bool isMidOperator =
                l.bounds.height() < rowLetterH * 0.38 && l.bounds.width() > l.bounds.height() * 1.2;
            if (!isMidOperator) {
                bottoms.append(l.bounds.bottom());
            }
        }
        if (bottoms.isEmpty()) continue;

        const double targetBaselineY = median(bottoms);

        for (const auto& l : letters) {
            const bool isMidOperator =
                l.bounds.height() < rowLetterH * 0.38 && l.bounds.width() > l.bounds.height() * 1.2;
            const double dy = isMidOperator
                ? (targetBaselineY - rowLetterH * 0.45) - l.bounds.center().y()
                : targetBaselineY - l.bounds.bottom();

            if (std::abs(dy) <= 0.25) continue;

            for (const qsizetype idx : l.indices) {
                auto& stroke = result[idx];
                for (auto& pt : stroke.points) {
                    pt.y += dy;
                }
                stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                changed = true;
            }
        }
    }

    if (changed) strokes = std::move(result);
    return changed;
}
void drawLassoPath(QPainter& p,const QPolygonF& poly,const ThemeConfig& theme,double zoom) {
    if(poly.isEmpty())return;
    zoom=std::max(.01,zoom);p.save();p.setRenderHint(QPainter::Antialiasing);
    static const QElapsedTimer timer=[] {QElapsedTimer t;t.start();return t;}();
    QPen pen(theme.accent,1.4/zoom,Qt::DashLine);pen.setDashOffset(double(timer.elapsed()%1200)/120);p.setPen(pen);
    QColor fill=theme.accent;fill.setAlpha(25);p.setBrush(fill);p.drawPolygon(poly);p.restore();
}
void drawLassoSelectionBox(QPainter& p,const QRectF& bounds,const ThemeConfig& theme,double zoom,bool showHandles) {
    if(bounds.isEmpty())return;
    zoom=std::max(.01,zoom);p.save();p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme.accent,1.2/zoom,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRect(bounds);
    if(!showHandles){p.restore();return;}
    const auto points=handles(bounds,zoom);p.setPen(QPen(theme.accent,1.5/zoom));p.drawLine(QPointF(bounds.center().x(),bounds.top()),points[4]);
    p.setBrush(theme.isDark?theme.background:QColor(Qt::white));for(auto point:points)p.drawEllipse(point,5.5/zoom,5.5/zoom);p.restore();
}
int hitTestLassoHandle(const QRectF& bounds,const QPointF& point,double zoom) {
    if(bounds.isEmpty())return -1;
    const auto points=handles(bounds,zoom);for(int i=4;i>=0;--i)if(length(point-points[i])*std::max(.01,zoom)<=10)return i;
    return -1;
}
} // namespace finily
