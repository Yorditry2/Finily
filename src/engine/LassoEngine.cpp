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
void flipSelection(QVector<Stroke>& strokes, QVector<ShapeItem>& shapes,
                   const QSet<QString>& strokeIds, const QSet<QString>& shapeIds,
                   const QRectF& bounds, bool horizontal) {
    if (bounds.isEmpty()) return;
    const QPointF c = bounds.center();
    auto reflect = [&](const QPointF& p) -> QPointF {
        return horizontal ? QPointF(2.0 * c.x() - p.x(), p.y())
                          : QPointF(p.x(), 2.0 * c.y() - p.y());
    };
    for (auto& s : strokes) {
        if (!strokeIds.contains(s.id)) continue;
        for (auto& p : s.points) {
            const QPointF q = reflect(QPointF(p.x, p.y));
            p.x = q.x();
            p.y = q.y();
            if (horizontal) p.tiltX = -p.tiltX;
            else p.tiltY = -p.tiltY;
        }
        s.cachedShapeHash = 0;
        s.cachedPath = QPainterPath();
        s.bounds = computeBounds(s.points, s.size * 0.7);
    }
    for (auto& s : shapes) {
        if (s.locked || !shapeIds.contains(s.id)) continue;
        if (s.kind == ShapeKind::Radical && horizontal) {
            makeRotatable(s);
        }
        for (auto& p : s.vertices) {
            p = reflect(p);
        }
        if ((s.kind == ShapeKind::Rectangle || s.kind == ShapeKind::Ellipse ||
             s.kind == ShapeKind::Axes || s.kind == ShapeKind::Solid3D) &&
            s.vertices.size() == 2) {
            const QRectF nr = QRectF(s.vertices[0], s.vertices[1]).normalized();
            s.vertices = {nr.topLeft(), nr.bottomRight()};
        } else if ((s.kind == ShapeKind::Arc || s.kind == ShapeKind::Sector ||
                    s.kind == ShapeKind::AngleMarker) &&
                   s.vertices.size() >= 3) {
            std::swap(s.vertices[1], s.vertices[2]);
        }
        s.bounds = computeShapeBounds(s);
    }
}
bool shiftContentVertically(QVector<Stroke>& strokes, QVector<ShapeItem>& shapes,
                            QVector<ImageItem>& images, QVector<TextItem>& texts,
                            double yThreshold, double deltaY) {
    if (!std::isfinite(yThreshold) || !std::isfinite(deltaY) || std::abs(deltaY) < 0.5) {
        return false;
    }
    bool moved = false;
    for (auto& s : strokes) {
        if (s.points.isEmpty()) continue;
        const QRectF b = s.bounds.isEmpty() ? computeBounds(s.points, s.size * 0.7) : s.bounds;
        if (b.top() >= yThreshold) {
            for (auto& p : s.points) {
                p.y += deltaY;
            }
            s.bounds = b.translated(0.0, deltaY);
            moved = true;
        }
    }
    const QVector<ShapeItem> originalShapes = shapes;
    QSet<QString> movedShapes;
    for (auto& s : shapes) {
        if (s.locked || s.vertices.isEmpty()) continue;
        const QRectF b = s.bounds.isEmpty() ? computeShapeBounds(s) : s.bounds;
        if (b.top() >= yThreshold) {
            for (auto& v : s.vertices) {
                v.ry() += deltaY;
            }
            if (!s.shadeBoundary.isEmpty() || s.kind == ShapeKind::ShadedRegion) {
                s.shadeProbe.ry() += deltaY;
            }
            s.bounds = b.translated(0.0, deltaY);
            movedShapes.insert(s.id);
            moved = true;
        }
    }
    if (!movedShapes.isEmpty()) {
        updateShapeLinks(shapes, originalShapes, movedShapes);
    }
    for (auto& img : images) {
        if (!img.isPdfPage && !img.locked && img.rect.top() >= yThreshold) {
            img.rect.translate(0.0, deltaY);
            moved = true;
        }
    }
    for (auto& txt : texts) {
        if (!txt.locked && txt.bounds.top() >= yThreshold) {
            txt.pos.ry() += deltaY;
            txt.bounds.translate(0.0, deltaY);
            moved = true;
        }
    }
    return moved;
}
bool straightenSelectedStrokes(QVector<Stroke>& strokes,const QSet<QString>& ids) {
    struct SelectedStroke { qsizetype index; QRectF bounds; };
    QVector<SelectedStroke> selected;
    QVector<double> allHeights;
    for (qsizetype i = 0; i < strokes.size(); ++i) {
        if (ids.contains(strokes[i].id) && !strokes[i].points.isEmpty()) {
            const QRectF b = computeBounds(strokes[i].points);
            selected.append({i, b});
            if (b.height() > 5.0) allHeights.append(b.height());
        }
    }
    if (selected.isEmpty()) return false;

    auto median = [](QVector<double> values) -> double {
        if (values.isEmpty()) return 0.0;
        std::sort(values.begin(), values.end());
        const qsizetype mid = values.size() / 2;
        return (values.size() % 2) ? values[mid] : (values[mid - 1] + values[mid]) * 0.5;
    };

    auto result = strokes;
    bool changed = false;

    const double baseH = std::clamp(allHeights.isEmpty() ? 24.0 : median(allHeights), 10.0, 160.0);

    // Paso 1: Suavizado caligráfico sin encogimiento (Taubin λ/μ + filtro bilateral de ángulos).
    // El filtro Laplaciano clásico encoge los bucles cerrados ('o', 'a', 'e', '0', '8');
    // alternar un paso de suavizado positivo (λ) con un paso de des-encogimiento (μ = -λ*0.58)
    // redondea las curvas y limpia el temblor de la mano manteniendo el radio y área exactos.
    for (auto& item : selected) {
        auto& stroke = result[item.index];
        const qsizetype n = stroke.points.size();
        const QRectF origB = item.bounds;
        const bool isTinyDot = std::max(origB.width(), origB.height()) <= baseH * 0.36;
        if (n >= 4 && !isTinyDot) {
            const QPointF firstPt(stroke.points.front().x, stroke.points.front().y);
            const QPointF lastPt(stroke.points.back().x, stroke.points.back().y);
            const bool isClosedLoop = std::hypot(firstPt.x() - lastPt.x(), firstPt.y() - lastPt.y())
                                      <= std::max(4.5, std::min(origB.width(), origB.height()) * 0.38);

            // Pesos de Taubin en 3 sub-pasos: suaviza -> infla curvas -> afina micro-jitter
            constexpr double kPasses[3] = {0.26, -0.15, 0.16};
            for (double baseWeight : kPasses) {
                auto nextPts = stroke.points;
                for (qsizetype i = 1; i + 1 < n; ++i) {
                    const auto& prev = stroke.points[i - 1];
                    const auto& curr = stroke.points[i];
                    const auto& next = stroke.points[i + 1];
                    const QPointF v1(curr.x - prev.x, curr.y - prev.y);
                    const QPointF v2(next.x - curr.x, next.y - curr.y);
                    const double l1 = std::hypot(v1.x(), v1.y());
                    const double l2 = std::hypot(v2.x(), v2.y());
                    double cornerGuard = 1.0;
                    if (l1 > 0.15 && l2 > 0.15) {
                        const double cosAngle = (v1.x() * v2.x() + v1.y() * v2.y()) / (l1 * l2);
                        // Preservar vértices agudos y quiebres de dirección ('v', 'w', 'k', 'z', 'r', 'm', 'n')
                        if (cosAngle < 0.05) cornerGuard = 0.04;
                        else if (cosAngle < 0.45) cornerGuard = 0.28;
                        else if (cosAngle < 0.75) cornerGuard = 0.72;
                    }
                    const double w = baseWeight * cornerGuard;
                    const double lapX = 0.5 * (prev.x + next.x) - curr.x;
                    const double lapY = 0.5 * (prev.y + next.y) - curr.y;
                    nextPts[i].x = curr.x + w * lapX;
                    nextPts[i].y = curr.y + w * lapY;
                    if (baseWeight > 0.0) {
                        nextPts[i].pressure = std::clamp(
                            curr.pressure * 0.56 + (prev.pressure + next.pressure) * 0.22, 0.04, 1.0);
                    }
                }
                stroke.points = std::move(nextPts);
            }

            // Si es un bucle cerrado ('o', '0', 'a'), acercar sutilmente el cierre si quedó casi tocándose
            if (isClosedLoop && n >= 6) {
                const QPointF gap(stroke.points.front().x - stroke.points.back().x,
                                  stroke.points.front().y - stroke.points.back().y);
                stroke.points.back().x += gap.x() * 0.45;
                stroke.points.back().y += gap.y() * 0.45;
            }

            stroke.cachedShapeHash = 0;
            stroke.cachedPath = QPainterPath();
            stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
            item.bounds = computeBounds(stroke.points);
            changed = true;
        }
    }

    // Paso 2: Separar en renglones horizontales (por si se seleccionaron varias líneas).
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

    // Paso 3: Dentro de cada renglón:
    //  - Clasificar cuerpos de letra vs. barras horizontales (cruce de 't', 'f', 'ñ') y puntos/acentos ('.', ':', 'i', '?', 'á').
    //  - La barra de la 't' se asocia a su tallo vertical SIN ensanchar la caja de la 't' (así jamás encadena 's-t-a' en "estas")
    //    y además se endereza horizontalmente de forma sutil.
    //  - Los dos puntos ':' o el punto de '?' se agrupan con su signo SIN estirarlos jamás.
    //  - Si el renglón se escribió inclinado hacia arriba o abajo, se endereza su pendiente.
    //  - PROHIBIDO separar letras dentro de una palabra (jamás convertir "como" en "c o mo").
    for (auto& row : rows) {
        std::stable_sort(row.begin(), row.end(), [](const SelectedStroke& a, const SelectedStroke& b) {
            return a.bounds.left() < b.bounds.left();
        });

        struct LetterCluster {
            QVector<qsizetype> indices;
            QVector<qsizetype> crossbarIndices; // Barras horizontales de 't', 'f', 'ñ', '=', '-'
            QVector<qsizetype> dotIndices;      // Puntos de 'i', 'j', '?', '!', ':'
            QRectF coreBounds; // Caja del cuerpo principal (sin la barra ancha de la 't' ni puntos flotantes)
            QRectF fullBounds; // Caja completa incluyendo accesorios
            bool isPunctuation = false; // ':', '.', ',', '?', '!'
            bool isMidOperator = false; // '-', '=', '+'
        };

        QVector<SelectedStroke> bodyStrokes;
        QVector<SelectedStroke> crossbars;
        QVector<SelectedStroke> dots;

        for (const auto& item : row) {
            const double w = item.bounds.width();
            const double h = item.bounds.height();
            // Detectar puntos y tildes/acentos cortos ('i', 'j', ':', '.', 'á', 'é', 'í', 'ó', 'ú')
            if (std::max(w, h) <= baseH * 0.38) {
                dots.append(item);
            } else if (w > h * 1.80 && h <= baseH * 0.45) {
                crossbars.append(item);
            } else {
                bodyStrokes.append(item);
            }
        }

        QVector<LetterCluster> letters;

        // 3.1 Formar letras base solo con trazos de cuerpo principal
        for (const auto& item : bodyStrokes) {
            bool merged = false;
            for (auto& letter : letters) {
                const double overlapX = std::min(letter.coreBounds.right(), item.bounds.right())
                                      - std::max(letter.coreBounds.left(), item.bounds.left());
                const double minW = std::max(1.0, std::min(letter.coreBounds.width(), item.bounds.width()));
                const QRectF unitedCore = letter.coreBounds.united(item.bounds);
                // Solo unir 2 trazos de cuerpo si se solapan fuertemente en X y no forman una caja ancha de 2 letras
                if (overlapX > minW * 0.45 && unitedCore.width() <= baseH * 1.05) {
                    letter.indices.append(item.index);
                    letter.coreBounds = unitedCore;
                    letter.fullBounds = letter.fullBounds.united(item.bounds);
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                letters.append(LetterCluster{{item.index}, {}, {}, item.bounds, item.bounds, false, false});
            }
        }

        // 3.2 Asociar barras horizontales (cruce de 't', 'f', 'θ', tilde de 'ñ', o signos '-', '=', '+')
        for (const auto& bar : crossbars) {
            const double barCx = bar.bounds.center().x();
            qsizetype bestLetter = -1;
            double bestDist = 1e9;
            for (qsizetype i = 0; i < letters.size(); ++i) {
                const auto& l = letters[i];
                // ¿La barra cruza este cuerpo verticalmente o está justo encima (ñ)?
                const bool verticalMatch = (bar.bounds.center().y() >= l.coreBounds.top() - baseH * 0.48)
                                        && (bar.bounds.center().y() <= l.coreBounds.bottom() + baseH * 0.15);
                if (!verticalMatch) continue;
                const bool crossesX = (bar.bounds.left() <= l.coreBounds.right() + 5.0)
                                   && (bar.bounds.right() >= l.coreBounds.left() - 5.0);
                if (!crossesX) continue;
                const double d = std::abs(barCx - l.coreBounds.center().x());
                if (d < bestDist && d <= std::max(baseH * 0.55, l.coreBounds.width() * 0.85)) {
                    bestDist = d;
                    bestLetter = i;
                }
            }
            if (bestLetter >= 0) {
                auto& target = letters[bestLetter];
                target.indices.append(bar.index);
                target.crossbarIndices.append(bar.index);
                target.fullBounds = target.fullBounds.united(bar.bounds);
                // Detectar si esta unión de 1 trazo vertical + 1 barra horizontal es un signo '+' matemático
                // (para distinguirlo de una 't' y darle aire a los lados en ecuaciones como "α + θ = 90°")!
                if (target.indices.size() == 2 && target.crossbarIndices.size() == 1
                    && target.coreBounds.width() <= target.coreBounds.height() * 0.45) {
                    const double barRelY = (bar.bounds.center().y() - target.coreBounds.top())
                                         / std::max(1.0, target.coreBounds.height());
                    const double ratioWH = bar.bounds.width() / std::max(1.0, target.coreBounds.height());
                    // Un '+' tiene proporciones equilibradas (barra ~60..165% de la altura del palo vertical)
                    // y cruce cerca del centro (30..74%), además de que la caja completa es más ancha que el solo palo
                    if (barRelY >= 0.28 && barRelY <= 0.74 && ratioWH >= 0.55 && ratioWH <= 1.75) {
                        target.isMidOperator = true;
                        target.coreBounds = target.fullBounds;
                    }
                }
            } else {
                // Es un signo matemático independiente ('-', '=')
                bool mergedOp = false;
                for (auto& l : letters) {
                    if (l.isMidOperator) {
                        const double overlapX = std::min(l.coreBounds.right(), bar.bounds.right())
                                              - std::max(l.coreBounds.left(), bar.bounds.left());
                        const double minW = std::max(1.0, std::min(l.coreBounds.width(), bar.bounds.width()));
                        if (overlapX > minW * 0.35) {
                            l.indices.append(bar.index);
                            l.crossbarIndices.append(bar.index);
                            l.coreBounds = l.coreBounds.united(bar.bounds);
                            l.fullBounds = l.coreBounds.united(bar.bounds);
                            mergedOp = true;
                            break;
                        }
                    }
                }
                if (!mergedOp) {
                    letters.append(LetterCluster{{bar.index}, {bar.index}, {}, bar.bounds, bar.bounds, false, true});
                }
            }
        }

        // 3.3 Asociar puntos, acentos y signo de grado '°' (punto de 'i', 'j', tilde 'á', punto inferior de '?' o '!', ':', o '90°')
        for (const auto& dot : dots) {
            const double dotCx = dot.bounds.center().x();
            // ¿Es un signo de grado '°' o exponente pequeño arriba a la derecha de una letra/dígito?
            bool isDegreeMark = false;
            for (qsizetype i = 0; i < letters.size(); ++i) {
                if (letters[i].isMidOperator || letters[i].isPunctuation) continue;
                const QRectF& lb = letters[i].coreBounds;
                const double dxRight = dot.bounds.left() - lb.right();
                const double dyTop = dot.bounds.center().y() - lb.top();
                if (dxRight >= -lb.width() * 0.25 && dxRight <= std::max(baseH * 0.55, 18.0)
                    && dyTop >= -baseH * 0.35 && dyTop <= lb.height() * 0.42) {
                    isDegreeMark = true;
                    break;
                }
            }
            if (isDegreeMark) {
                // El signo de grado '°' se mantiene como puntuación superior independiente (no desplaza ni encoge al '0' de '90°')
                letters.append(LetterCluster{{dot.index}, {}, {dot.index}, dot.bounds, dot.bounds, true, false});
                continue;
            }

            qsizetype bestLetter = -1;
            double bestDist = 1e9;
            for (qsizetype i = 0; i < letters.size(); ++i) {
                if (letters[i].isMidOperator) continue;
                const double d = std::abs(dotCx - letters[i].coreBounds.center().x());
                const double maxReach = std::max(baseH * 0.40, letters[i].coreBounds.width() * 0.68);
                if (d <= maxReach && d < bestDist) {
                    bestDist = d;
                    bestLetter = i;
                }
            }
            if (bestLetter >= 0) {
                auto& target = letters[bestLetter];
                target.indices.append(dot.index);
                target.dotIndices.append(dot.index);
                // Si el punto está DEBAJO del trazo (como en '?' o '!'), el fondo de la letra incluye al punto
                // y marcamos isPunctuation para que '?' no se estire deformando el punto.
                if (dot.bounds.center().y() > target.coreBounds.bottom() - 2.0) {
                    target.coreBounds = target.coreBounds.united(dot.bounds);
                    target.isPunctuation = true;
                }
                target.fullBounds = target.fullBounds.united(dot.bounds);
            } else {
                // Punto independiente ('.', ',' o los dos puntos ':')
                letters.append(LetterCluster{{dot.index}, {}, {dot.index}, dot.bounds, dot.bounds, true, false});
            }
        }

        std::stable_sort(letters.begin(), letters.end(), [](const LetterCluster& a, const LetterCluster& b) {
            return a.coreBounds.left() < b.coreBounds.left();
        });

        // Si un símbolo aislado ('α', 'θ', '60°', 'x') se escribió solo (ej. etiqueta de un ángulo dentro de un triángulo),
        // SOLO aplicar el suavizado caligráfico Taubin del Paso 1 y NO desplazarlo verticalmente!
        qsizetype nonPunctCount = 0;
        for (const auto& l : letters) {
            if (!l.isPunctuation) ++nonPunctCount;
        }
        if (nonPunctCount <= 1 && rows.size() == 1) {
            continue;
        }

        // Calcular altura x-height real (mitad inferior de las alturas, correspondiente a o, a, c, e, m, s, u)
        QVector<double> bodyHeights;
        for (const auto& l : letters) {
            if (!l.isPunctuation && !l.isMidOperator && l.coreBounds.height() >= baseH * 0.35) {
                bodyHeights.append(l.coreBounds.height());
            }
        }
        if (bodyHeights.isEmpty()) continue;
        std::sort(bodyHeights.begin(), bodyHeights.end());
        QVector<double> shortHalf;
        const qsizetype halfCount = std::max<qsizetype>(1, (bodyHeights.size() + 1) / 2);
        for (qsizetype i = 0; i < halfCount; ++i) {
            shortHalf.append(bodyHeights[i]);
        }
        const double targetXHeight = median(shortHalf);

        QVector<double> tallSamples;
        for (double h : bodyHeights) {
            if (h > targetXHeight * 1.32) tallSamples.append(h);
        }
        const double targetTallHeight = tallSamples.isEmpty()
            ? targetXHeight * 1.62
            : median(tallSamples);

        // Calcular línea base real y techo de x-height usando las letras de altura corta ('a', 'e', 'o', 'u', 'n', 'c', 's')
        // para que en palabras cortas como "papa" o "que" las colas descendentes ('p', 'q', 'g', 'y', 'j')
        // JAMÁS contaminen la mediana ni sean subidas a la línea base como si fueran mayúsculas!
        QVector<double> shortBottoms;
        QVector<double> shortTops;
        QVector<double> allBottoms;
        for (const auto& l : letters) {
            if (l.isMidOperator || l.isPunctuation) continue;
            allBottoms.append(l.coreBounds.bottom());
            if (l.coreBounds.height() <= targetXHeight * 1.28) {
                shortBottoms.append(l.coreBounds.bottom());
                shortTops.append(l.coreBounds.top());
            }
        }
        if (allBottoms.isEmpty()) continue;
        const double roughBaselineY = shortBottoms.isEmpty() ? median(allBottoms) : median(shortBottoms);
        const double roughXTopY = shortTops.isEmpty() ? (roughBaselineY - targetXHeight) : median(shortTops);

        // Detector anatómico de letras descendentes ('p', 'q', 'g', 'y', 'j'):
        // 1) Su base baja claramente más allá de roughBaselineY, O
        // 2) Su techo está alineado con el techo de las vocales cortas (roughXTopY) pero su altura es > 1.25 * xHeight
        //    y la mayor densidad de puntos/curvas está en la mitad superior (la pancita de 'p', 'q', 'g' está arriba y el palo baja).
        auto isDescenderLetter = [&](const LetterCluster& l) -> bool {
            if (l.isMidOperator || l.isPunctuation) return false;
            const double h = l.coreBounds.height();
            const bool dropsBelowBaseline = (l.coreBounds.bottom() > roughBaselineY + targetXHeight * 0.20)
                                         && (l.coreBounds.top() > roughBaselineY - targetXHeight * 1.28);
            if (dropsBelowBaseline) return true;

            // Caso "papa" / "que" donde el usuario escribió la 'p' o 'q' un poco más arriba pero su pancita está en la mitad superior
            if (h > targetXHeight * 1.22 && l.coreBounds.top() >= roughXTopY - targetXHeight * 0.30) {
                int topHalfPts = 0;
                int botHalfPts = 0;
                double topMinX = 1e9, topMaxX = -1e9;
                double botMinX = 1e9, botMaxX = -1e9;
                const double midY = l.coreBounds.center().y();
                for (const qsizetype idx : l.indices) {
                    if (l.dotIndices.contains(idx)) continue;
                    for (const auto& pt : result[idx].points) {
                        if (pt.y <= midY) {
                            ++topHalfPts;
                            topMinX = std::min(topMinX, pt.x);
                            topMaxX = std::max(topMaxX, pt.x);
                        } else {
                            ++botHalfPts;
                            botMinX = std::min(botMinX, pt.x);
                            botMaxX = std::max(botMaxX, pt.x);
                        }
                    }
                }
                const double topW = (topMaxX > topMinX) ? (topMaxX - topMinX) : 0.0;
                const double botW = (botMaxX > botMinX) ? (botMaxX - botMinX) : 0.0;
                // En 'p', 'q' (con o sin cruce), la cabeza/óvalo superior es mucho más ancha y densa que la cola inferior
                if (topW > botW * 1.28 && topHalfPts > botHalfPts) {
                    return true;
                }
            }
            return false;
        };

        struct BaselineSample { double x; double y; };
        QVector<BaselineSample> cleanBaselineSamples;
        QVector<double> cleanBottoms;
        for (const auto& l : letters) {
            if (l.isMidOperator || l.isPunctuation) continue;
            if (!isDescenderLetter(l)) {
                cleanBottoms.append(l.coreBounds.bottom());
                cleanBaselineSamples.append({l.coreBounds.center().x(), l.coreBounds.bottom()});
            }
        }
        // Anclar la línea base horizontal hacia el inicio del renglón/palabra si hay pendiente,
        // para que al escribir inclinado el texto se enderece sobre la altura donde empezaste a escribir.
        double targetBaselineY = cleanBottoms.isEmpty() ? roughBaselineY : median(cleanBottoms);
        if (cleanBaselineSamples.size() >= 3) {
            const double spanX = cleanBaselineSamples.back().x - cleanBaselineSamples.front().x;
            if (spanX > targetXHeight * 1.8) {
                double sumX = 0.0, sumY = 0.0;
                for (const auto& s : cleanBaselineSamples) { sumX += s.x; sumY += s.y; }
                const double meanX = sumX / cleanBaselineSamples.size();
                const double meanY = sumY / cleanBaselineSamples.size();
                double num = 0.0, den = 0.0;
                for (const auto& s : cleanBaselineSamples) {
                    num += (s.x - meanX) * (s.y - meanY);
                    den += (s.x - meanX) * (s.x - meanX);
                }
                const double slope = (den > 1e-4) ? (num / den) : 0.0;
                // Si el usuario iba subiendo o bajando el renglón, nivelar hacia el primer tercio izquierdo
                if (std::abs(slope) > 0.025 && std::abs(slope) < 0.35) {
                    const double anchorX = cleanBaselineSamples.front().x + spanX * 0.25;
                    const double regressedY = meanY + slope * (anchorX - meanX);
                    targetBaselineY = 0.55 * targetBaselineY + 0.45 * regressedY;
                }
            }
        }

        // Estimar inclinación global de trazos verticales en esta palabra/renglón
        QVector<double> stemSlopes;
        for (const auto& l : letters) {
            if (l.isPunctuation || l.isMidOperator) continue;
            for (const qsizetype idx : l.indices) {
                if (l.crossbarIndices.contains(idx) || l.dotIndices.contains(idx)) continue;
                const auto& stroke = result[idx];
                const QRectF sb = computeBounds(stroke.points);
                if (sb.height() > targetXHeight * 0.85 && sb.width() < sb.height() * 0.55 && stroke.points.size() >= 4) {
                    // Regresión lineal dx/dy de los puntos del trazo
                    double sumY = 0.0, sumX = 0.0;
                    for (const auto& pt : stroke.points) { sumX += pt.x; sumY += pt.y; }
                    const double mx = sumX / stroke.points.size();
                    const double my = sumY / stroke.points.size();
                    double num = 0.0, den = 0.0;
                    for (const auto& pt : stroke.points) {
                        num += (pt.y - my) * (pt.x - mx);
                        den += (pt.y - my) * (pt.y - my);
                    }
                    if (den > 12.0) {
                        const double sl = num / den;
                        if (std::abs(sl) < 0.32) stemSlopes.append(sl);
                    }
                }
            }
        }
        const double globalStemSlope = stemSlopes.isEmpty() ? 0.0 : median(stemSlopes);

        // 3.4 Nivelación firme en la línea base, armonización de altura y enderezado de barras y tallos
        for (auto& l : letters) {
            if (l.isPunctuation) {
                // Para ':', '.', '?', '!' NO escalar (sy = 1.0) para no deformar los puntos!
                // Si es '?' o '!' (alto), asentar su base en la línea base.
                if (l.coreBounds.height() > targetXHeight * 0.80) {
                    const double dy = (targetBaselineY - l.coreBounds.bottom()) * 0.90;
                    if (std::abs(dy) > 0.15) {
                        for (const qsizetype idx : l.indices) {
                            auto& stroke = result[idx];
                            for (auto& pt : stroke.points) pt.y += dy;
                            stroke.cachedShapeHash = 0;
                            stroke.cachedPath = QPainterPath();
                            stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                            changed = true;
                        }
                        l.coreBounds.translate(0.0, dy);
                        l.fullBounds.translate(0.0, dy);
                    }
                }
                continue;
            }

            const double h = l.coreBounds.height();
            const bool isDescender = isDescenderLetter(l);
            const bool isTall = !l.isMidOperator && !isDescender && (h > targetXHeight * 1.32);

            double dy = 0.0;
            double sy = 1.0;
            double anchorY = targetBaselineY;

            if (l.isMidOperator) {
                dy = (targetBaselineY - targetXHeight * 0.48) - l.coreBounds.center().y();
            } else if (isDescender) {
                // Las letras descendentes ('p', 'q', 'g', 'y', 'j') alinean su TECHO con el techo de las vocales
                // (targetBaselineY - targetXHeight), dejando que su cola cuelgue libremente hacia abajo!
                const double targetTop = targetBaselineY - targetXHeight;
                dy = (targetTop - l.coreBounds.top()) * 0.94;
                anchorY = targetTop;
            } else {
                // Asentar con firmeza (95%) sobre la línea base común para que el renglón se vea limpio
                dy = (targetBaselineY - l.coreBounds.bottom()) * 0.95;
                anchorY = targetBaselineY;
                const double idealH = isTall ? targetTallHeight : targetXHeight;
                if (h > 4.0) {
                    const double rawSy = idealH / h;
                    // Armonización equilibrada (55%) para que todas las vocales/minúsculas tengan altura uniforme
                    sy = std::clamp(1.0 + (rawSy - 1.0) * 0.55, 0.84, 1.18);
                }
            }

            for (const qsizetype idx : l.indices) {
                auto& stroke = result[idx];
                const QRectF sb = computeBounds(stroke.points);
                const bool isBar = l.crossbarIndices.contains(idx);
                const bool isDot = l.dotIndices.contains(idx);

                // Si es un tallo vertical ('l', 't', 'f', 'h', 'd', 'b', 'i', '1' o vertical de '+'), enderezar su inclinación
                double deskewSlope = 0.0;
                if (!isBar && !isDot && (sb.height() > targetXHeight * 0.90 || l.isMidOperator)
                    && sb.width() < sb.height() * 0.52 && stroke.points.size() >= 4) {
                    double sumY = 0.0, sumX = 0.0;
                    for (const auto& pt : stroke.points) { sumX += pt.x; sumY += pt.y; }
                    const double mx = sumX / stroke.points.size();
                    const double my = sumY / stroke.points.size();
                    double num = 0.0, den = 0.0;
                    for (const auto& pt : stroke.points) {
                        num += (pt.y - my) * (pt.x - mx);
                        den += (pt.y - my) * (pt.y - my);
                    }
                    if (den > 8.0) {
                        const double rawSlope = num / den;
                        if (std::abs(rawSlope) < 0.34) {
                            deskewSlope = rawSlope * (l.isMidOperator ? 0.72 : 0.58);
                        }
                    }
                } else if (!isBar && !isDot && !l.isMidOperator && std::abs(globalStemSlope) > 0.04) {
                    deskewSlope = globalStemSlope * 0.32;
                }

                // Si es una barra horizontal ('t', 'f', '-', '='), nivelar su inclinación horizontal
                double barLevelCy = sb.center().y() + dy;
                barLevelCy = anchorY + (barLevelCy - anchorY) * sy;

                const double baseRefY = sb.bottom() + dy;
                for (auto& pt : stroke.points) {
                    const double shiftedY = pt.y + dy;
                    if (isDot) {
                        // Los puntos y tildes se desplazan junto con su letra pero NO se estiran con sy
                        const double topShift = (anchorY - l.coreBounds.height() * sy) - (l.coreBounds.top());
                        pt.y += (pt.y < l.coreBounds.center().y()) ? topShift : dy;
                    } else if (isBar) {
                        // Aplanar sutilmente la barra horizontal (65% hacia la horizontal pura)
                        const double scaledY = anchorY + (shiftedY - anchorY) * sy;
                        pt.y = scaledY * 0.35 + barLevelCy * 0.65;
                    } else {
                        pt.y = anchorY + (shiftedY - anchorY) * sy;
                        if (std::abs(deskewSlope) > 1e-4) {
                            pt.x -= (pt.y - baseRefY) * deskewSlope;
                        }
                    }
                }
                stroke.cachedShapeHash = 0;
                stroke.cachedPath = QPainterPath();
                stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                changed = true;
            }

            // Actualizar cajas tras nivelar
            QRectF newCore, newFull;
            bool firstFull = true;
            bool firstCore = true;
            for (const qsizetype idx : l.indices) {
                const QRectF sb = computeBounds(result[idx].points);
                newFull = firstFull ? sb : newFull.united(sb);
                firstFull = false;
                if (!l.crossbarIndices.contains(idx) && !l.dotIndices.contains(idx)) {
                    newCore = firstCore ? sb : newCore.united(sb);
                    firstCore = false;
                }
            }
            l.fullBounds = newFull;
            l.coreBounds = firstCore ? newFull : newCore;
        }

        // 3.5 Cohesión horizontal dentro de cada palabra y respiro de ecuaciones:
        //  - JAMÁS separar letras que ya están juntas dentro de una palabra (para nunca causar "c o mo").
        //  - JAMÁS achicar los espacios entre palabras distintas.
        //  - En ecuaciones ("α+θ=90°"), si un operador matemático (+, -, =) quedó pegado o solapado con
        //    su símbolo vecino (como 'α' pegado a '+'), abrir un respiro limpio alrededor del operador!
        //  - SOLO acercar suavemente una letra si quedó suelta/separada por accidente dentro de la misma palabra.
        if (letters.size() >= 2) {
            const double wordBreakGap = std::max(targetXHeight * 0.48, 13.0);
            const double minOpGap = std::clamp(targetXHeight * 0.24, 6.0, 16.0);
            QVector<double> intraGaps;
            for (qsizetype i = 1; i < letters.size(); ++i) {
                if (letters[i].isMidOperator || letters[i - 1].isMidOperator) continue;
                const double g = letters[i].coreBounds.left() - letters[i - 1].coreBounds.right();
                if (g >= -2.0 && g < wordBreakGap) {
                    intraGaps.append(std::max(0.0, g));
                }
            }
            const double naturalIntraGap = intraGaps.isEmpty()
                ? 2.0
                : std::clamp(median(intraGaps), 0.5, targetXHeight * 0.14);

            double cumulativeShiftX = 0.0;
            double prevOriginalRight = letters[0].fullBounds.right();
            double prevOriginalCoreRight = letters[0].coreBounds.right();
            for (qsizetype i = 1; i < letters.size(); ++i) {
                const bool touchesOperator = letters[i].isMidOperator || letters[i - 1].isMidOperator;
                const double rawFullGap = letters[i].fullBounds.left() - prevOriginalRight;
                const double rawCoreGap = letters[i].coreBounds.left() - prevOriginalCoreRight;
                prevOriginalRight = letters[i].fullBounds.right();
                prevOriginalCoreRight = letters[i].coreBounds.right();

                if (!touchesOperator && rawCoreGap >= wordBreakGap) {
                    // Espacio entre palabras: la nueva palabra arranca en su posición X original intacta
                    cumulativeShiftX = 0.0;
                    continue;
                }

                double dx = cumulativeShiftX;
                if (touchesOperator) {
                    // Si un símbolo ('α', 'θ', '9') quedó muy pegado a '+', '-', '=', darle respiro limpio
                    if (rawFullGap < minOpGap) {
                        const double breathe = minOpGap - rawFullGap;
                        dx += breathe;
                        cumulativeShiftX += breathe;
                    }
                } else if (!letters[i].isPunctuation && rawCoreGap > naturalIntraGap + 3.0) {
                    const double targetGap = naturalIntraGap + (rawCoreGap - naturalIntraGap) * 0.35;
                    const double tighten = targetGap - rawCoreGap; // negativo: junta la letra suelta hacia la palabra
                    dx += tighten;
                    cumulativeShiftX += tighten;
                }

                if (std::abs(dx) > 0.15) {
                    for (const qsizetype idx : letters[i].indices) {
                        auto& stroke = result[idx];
                        for (auto& pt : stroke.points) pt.x += dx;
                        stroke.cachedShapeHash = 0;
                        stroke.cachedPath = QPainterPath();
                        stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                        changed = true;
                    }
                    letters[i].coreBounds.translate(dx, 0.0);
                    letters[i].fullBounds.translate(dx, 0.0);
                }
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
