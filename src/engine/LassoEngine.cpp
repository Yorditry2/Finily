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

struct SelectedStroke {
    qsizetype index = 0;
    QRectF bounds;
};

struct LetterCluster {
    QVector<qsizetype> indices;
    QVector<qsizetype> crossbarIndices; // Barras horizontales de 't', 'f', 'ñ', '=', '-'
    QVector<qsizetype> dotIndices;      // Puntos de 'i', 'j', '?', '!', ':'
    QRectF coreBounds;                  // Caja del cuerpo principal (sin barra ni puntos flotantes)
    QRectF fullBounds;                  // Caja completa incluyendo accesorios
    bool isPunctuation = false;         // ':', '.', ',', '?', '!'
    bool isMidOperator = false;         // '-', '=', '+'
    bool isDescender = false;           // 'p', 'q', 'g', 'y', 'j'
};

struct RowMetrics {
    bool valid = false;
    double targetXHeight = 0.0;
    double targetTallHeight = 0.0;
    double targetBaselineY = 0.0;
    double globalStemSlope = 0.0;
};

double length(QPointF p) {
    return std::hypot(p.x(), p.y());
}

void invalidateStrokeCache(Stroke& stroke) {
    stroke.cachedPath = QPainterPath{};
    stroke.cachedShapeHash = 0;
}

QVector<QPointF> handles(QRectF r, double zoom) {
    return {
        r.topLeft(),
        r.topRight(),
        r.bottomRight(),
        r.bottomLeft(),
        r.topLeft() + QPointF(r.width() / 2.0, -24.0 / std::max(0.01, zoom)),
    };
}

template <class Map>
void transform(QVector<Stroke>& strokes,
               QVector<ShapeItem>& shapes,
               const QSet<QString>& strokeIds,
               const QSet<QString>& shapeIds,
               Map map,
               double widthScale = 1.0) {
    // Callers supply a fresh copy of the immutable drag-start document each frame.
    for (auto& s : strokes) {
        if (!strokeIds.contains(s.id)) {
            continue;
        }
        for (auto& p : s.points) {
            const QPointF q = map(QPointF(p.x, p.y));
            p.x = q.x();
            p.y = q.y();
        }
        invalidateStrokeCache(s);
        s.size = std::clamp(s.size * widthScale, 0.1, 200.0);
        s.bounds = computeBounds(s.points, s.size * 0.7);
    }
    for (auto& s : shapes) {
        if (s.locked || !shapeIds.contains(s.id)) {
            continue;
        }
        for (auto& p : s.vertices) {
            p = map(p);
        }
        if (!s.shadeBoundary.isEmpty() || s.kind == ShapeKind::ShadedRegion) {
            s.shadeProbe = map(s.shadeProbe);
        }
        s.size = std::clamp(s.size * widthScale, 0.1, 200.0);
        s.bounds = computeShapeBounds(s);
    }
}

// The shared model has no rotation field for axis-aligned primitives. Preserve
// their actual outline as a closed/open polyline before applying a rotation.
void makeRotatable(ShapeItem& s) {
    if (s.vertices.size() < 2) {
        return;
    }
    if (s.kind == ShapeKind::Rectangle) {
        s.vertices = getShapeHandles(s);
        s.kind = ShapeKind::Polyline;
        s.closed = true;
    } else if (s.kind == ShapeKind::Ellipse) {
        const QRectF r = QRectF(s.vertices[0], s.vertices[1]).normalized();
        s.vertices.clear();
        for (int i = 0; i < 128; ++i) {
            const double a = 2.0 * std::numbers::pi * i / 128.0;
            s.vertices.append(
                r.center() + QPointF(std::cos(a) * r.width() / 2.0, std::sin(a) * r.height() / 2.0));
        }
        s.kind = ShapeKind::Polyline;
        s.closed = true;
    } else if (s.kind == ShapeKind::Radical && s.vertices.size() == 3) {
        const QPointF base = s.vertices[0];
        const QPointF roof = s.vertices[1];
        const double h = std::max(1.0, std::abs(base.y() - roof.y()));
        const double direction = base.y() >= roof.y() ? 1.0 : -1.0;
        const QPointF tick = base + QPointF(-h * 0.23, -direction * h * 0.36);
        s.vertices = {
            tick + QPointF(-h * 0.13, direction * h * 0.08),
            tick,
            base,
            roof,
            QPointF(s.vertices[2].x(), roof.y()),
        };
        s.kind = ShapeKind::Polyline;
        s.closed = false;
    }
}

double medianValue(QVector<double> values) {
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const qsizetype mid = values.size() / 2;
    return (values.size() % 2) ? values[mid] : (values[mid - 1] + values[mid]) * 0.5;
}

// Paso 1: Suavizado caligráfico sin encogimiento (Taubin λ/μ + filtro bilateral de ángulos).
// Alternar un paso de suavizado positivo (λ) con un paso de des-encogimiento (μ = -λ*0.58)
// redondea las curvas y limpia el temblor de la mano manteniendo el radio y área exactos.
bool smoothStrokeTaubin(Stroke& stroke, QRectF& bounds, double baseH) {
    const qsizetype n = stroke.points.size();
    const QRectF origB = bounds;
    const bool isTinyDot = std::max(origB.width(), origB.height()) <= baseH * 0.36;
    if (n < 4 || isTinyDot) {
        return false;
    }

    const QPointF firstPt(stroke.points.front().x, stroke.points.front().y);
    const QPointF lastPt(stroke.points.back().x, stroke.points.back().y);
    const bool isClosedLoop = std::hypot(firstPt.x() - lastPt.x(), firstPt.y() - lastPt.y())
                              <= std::max(4.5, std::min(origB.width(), origB.height()) * 0.38);

    QVector<StrokePoint> working = stroke.points;
    // Pesos de Taubin en 3 sub-pasos: suaviza -> infla curvas -> afina micro-jitter
    constexpr double kPasses[3] = {0.26, -0.15, 0.16};
    for (double baseWeight : kPasses) {
        QVector<StrokePoint> nextPts = working;
        for (qsizetype i = 1; i + 1 < n; ++i) {
            const auto& prev = working[i - 1];
            const auto& curr = working[i];
            const auto& next = working[i + 1];
            const QPointF v1(curr.x - prev.x, curr.y - prev.y);
            const QPointF v2(next.x - curr.x, next.y - curr.y);
            const double l1 = std::hypot(v1.x(), v1.y());
            const double l2 = std::hypot(v2.x(), v2.y());
            double cornerGuard = 1.0;
            if (l1 > 0.15 && l2 > 0.15) {
                const double cosAngle = (v1.x() * v2.x() + v1.y() * v2.y()) / (l1 * l2);
                // Preservar vértices agudos y quiebres de dirección ('v', 'w', 'k', 'z', 'r', 'm', 'n')
                if (cosAngle < 0.05) {
                    cornerGuard = 0.04;
                } else if (cosAngle < 0.45) {
                    cornerGuard = 0.28;
                } else if (cosAngle < 0.75) {
                    cornerGuard = 0.72;
                }
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
        working = std::move(nextPts);
    }

    // Si es un bucle cerrado ('o', '0', 'a'), acercar sutilmente el cierre si quedó casi tocándose
    if (isClosedLoop && n >= 6) {
        const QPointF gap(working.front().x - working.back().x,
                          working.front().y - working.back().y);
        working.back().x += gap.x() * 0.45;
        working.back().y += gap.y() * 0.45;
    }

    double maxDelta = 0.0;
    for (qsizetype i = 0; i < n; ++i) {
        maxDelta = std::max(maxDelta, std::abs(working[i].x - stroke.points[i].x));
        maxDelta = std::max(maxDelta, std::abs(working[i].y - stroke.points[i].y));
    }
    if (maxDelta <= 1e-4) {
        return false;
    }

    stroke.points = std::move(working);
    invalidateStrokeCache(stroke);
    stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
    bounds = computeBounds(stroke.points);
    return true;
}

// Paso 2: Separar en renglones horizontales (por si se seleccionaron varias líneas).
QVector<QVector<SelectedStroke>> splitIntoRows(QVector<SelectedStroke> selected, double baseH) {
    std::stable_sort(selected.begin(), selected.end(), [](const SelectedStroke& a, const SelectedStroke& b) {
        return a.bounds.center().y() < b.bounds.center().y();
    });
    QVector<QVector<SelectedStroke>> rows;
    for (const auto& item : selected) {
        bool placed = false;
        for (auto& row : rows) {
            double sumCy = 0.0;
            for (const auto& r : row) {
                sumCy += r.bounds.center().y();
            }
            const double rowCy = sumCy / double(row.size());
            if (std::abs(item.bounds.center().y() - rowCy) <= baseH * 1.35) {
                row.append(item);
                placed = true;
                break;
            }
        }
        if (!placed) {
            rows.append({item});
        }
    }
    return rows;
}

// Paso 3.1-3.3: Clasificar cuerpos de letra, barras horizontales y puntos/acentos.
QVector<LetterCluster> buildRowClusters(QVector<SelectedStroke> row, double baseH) {
    std::stable_sort(row.begin(), row.end(), [](const SelectedStroke& a, const SelectedStroke& b) {
        return a.bounds.left() < b.bounds.left();
    });

    QVector<SelectedStroke> bodyStrokes;
    QVector<SelectedStroke> crossbars;
    QVector<SelectedStroke> dots;
    for (const auto& item : row) {
        const double w = item.bounds.width();
        const double h = item.bounds.height();
        if (std::max(w, h) <= baseH * 0.38) {
            dots.append(item);
        } else if (w > h * 1.80 && h <= baseH * 0.45) {
            crossbars.append(item);
        } else {
            bodyStrokes.append(item);
        }
    }

    QVector<LetterCluster> letters;

    // Formar letras base solo con trazos de cuerpo principal
    for (const auto& item : bodyStrokes) {
        bool merged = false;
        for (auto& letter : letters) {
            const double overlapX = std::min(letter.coreBounds.right(), item.bounds.right())
                                  - std::max(letter.coreBounds.left(), item.bounds.left());
            const double minW = std::max(1.0, std::min(letter.coreBounds.width(), item.bounds.width()));
            const QRectF unitedCore = letter.coreBounds.united(item.bounds);
            if (overlapX > minW * 0.45 && unitedCore.width() <= baseH * 1.05) {
                letter.indices.append(item.index);
                letter.coreBounds = unitedCore;
                letter.fullBounds = letter.fullBounds.united(item.bounds);
                merged = true;
                break;
            }
        }
        if (!merged) {
            letters.append(
                LetterCluster{{item.index}, {}, {}, item.bounds, item.bounds, false, false, false});
        }
    }

    // Asociar barras horizontales (cruce de 't', 'f', 'θ', tilde de 'ñ', o signos '-', '=', '+')
    for (const auto& bar : crossbars) {
        const double barCx = bar.bounds.center().x();
        qsizetype bestLetter = -1;
        double bestDist = 1e9;
        for (qsizetype i = 0; i < letters.size(); ++i) {
            const auto& l = letters[i];
            const bool verticalMatch = (bar.bounds.center().y() >= l.coreBounds.top() - baseH * 0.48)
                                    && (bar.bounds.center().y() <= l.coreBounds.bottom() + baseH * 0.15);
            if (!verticalMatch) {
                continue;
            }
            const bool crossesX = (bar.bounds.left() <= l.coreBounds.right() + 5.0)
                               && (bar.bounds.right() >= l.coreBounds.left() - 5.0);
            if (!crossesX) {
                continue;
            }
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
            if (target.indices.size() == 2 && target.crossbarIndices.size() == 1
                && target.coreBounds.width() <= target.coreBounds.height() * 0.45) {
                const double barRelY = (bar.bounds.center().y() - target.coreBounds.top())
                                     / std::max(1.0, target.coreBounds.height());
                const double ratioWH = bar.bounds.width() / std::max(1.0, target.coreBounds.height());
                if (barRelY >= 0.28 && barRelY <= 0.74 && ratioWH >= 0.55 && ratioWH <= 1.75) {
                    target.isMidOperator = true;
                    target.coreBounds = target.fullBounds;
                }
            }
        } else {
            bool mergedOp = false;
            for (auto& l : letters) {
                if (!l.isMidOperator) {
                    continue;
                }
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
            if (!mergedOp) {
                letters.append(
                    LetterCluster{{bar.index}, {bar.index}, {}, bar.bounds, bar.bounds, false, true, false});
            }
        }
    }

    // Asociar puntos, acentos y signo de grado '°'
    for (const auto& dot : dots) {
        const double dotCx = dot.bounds.center().x();
        bool isDegreeMark = false;
        for (qsizetype i = 0; i < letters.size(); ++i) {
            if (letters[i].isMidOperator || letters[i].isPunctuation) {
                continue;
            }
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
            letters.append(
                LetterCluster{{dot.index}, {}, {dot.index}, dot.bounds, dot.bounds, true, false, false});
            continue;
        }

        qsizetype bestLetter = -1;
        double bestDist = 1e9;
        for (qsizetype i = 0; i < letters.size(); ++i) {
            if (letters[i].isMidOperator) {
                continue;
            }
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
            if (dot.bounds.center().y() > target.coreBounds.bottom() - 2.0) {
                target.coreBounds = target.coreBounds.united(dot.bounds);
                target.isPunctuation = true;
            }
            target.fullBounds = target.fullBounds.united(dot.bounds);
        } else {
            letters.append(
                LetterCluster{{dot.index}, {}, {dot.index}, dot.bounds, dot.bounds, true, false, false});
        }
    }

    std::stable_sort(letters.begin(), letters.end(), [](const LetterCluster& a, const LetterCluster& b) {
        return a.coreBounds.left() < b.coreBounds.left();
    });
    return letters;
}

bool isDescenderCluster(const LetterCluster& l,
                        const QVector<Stroke>& strokes,
                        double roughBaselineY,
                        double roughXTopY,
                        double targetXHeight) {
    if (l.isMidOperator || l.isPunctuation) {
        return false;
    }
    const double h = l.coreBounds.height();
    const bool dropsBelowBaseline = (l.coreBounds.bottom() > roughBaselineY + targetXHeight * 0.20)
                                 && (l.coreBounds.top() > roughBaselineY - targetXHeight * 1.28);
    if (dropsBelowBaseline) {
        return true;
    }

    // Caso "papa" / "que" donde el usuario escribió 'p' o 'q' más arriba pero la pancita está arriba
    if (h > targetXHeight * 1.22 && l.coreBounds.top() >= roughXTopY - targetXHeight * 0.30) {
        int topHalfPts = 0;
        int botHalfPts = 0;
        double topMinX = 1e9;
        double topMaxX = -1e9;
        double botMinX = 1e9;
        double botMaxX = -1e9;
        const double midY = l.coreBounds.center().y();
        for (const qsizetype idx : l.indices) {
            if (l.dotIndices.contains(idx)) {
                continue;
            }
            for (const auto& pt : strokes[idx].points) {
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
        if (topW > botW * 1.28 && topHalfPts > botHalfPts) {
            return true;
        }
    }
    return false;
}

RowMetrics analyzeRowMetrics(QVector<LetterCluster>& letters,
                             const QVector<Stroke>& strokes,
                             double baseH,
                             qsizetype rowCount) {
    // Si un símbolo aislado ('α', 'θ', '60°', 'x') se escribió solo, conservar solo Taubin
    qsizetype nonPunctCount = 0;
    for (const auto& l : letters) {
        if (!l.isPunctuation) {
            ++nonPunctCount;
        }
    }
    if (nonPunctCount <= 1 && rowCount == 1) {
        return {};
    }

    QVector<double> bodyHeights;
    for (const auto& l : letters) {
        if (!l.isPunctuation && !l.isMidOperator && l.coreBounds.height() >= baseH * 0.35) {
            bodyHeights.append(l.coreBounds.height());
        }
    }
    if (bodyHeights.isEmpty()) {
        return {};
    }
    std::sort(bodyHeights.begin(), bodyHeights.end());
    QVector<double> shortHalf;
    const qsizetype halfCount = std::max<qsizetype>(1, (bodyHeights.size() + 1) / 2);
    for (qsizetype i = 0; i < halfCount; ++i) {
        shortHalf.append(bodyHeights[i]);
    }
    const double targetXHeight = medianValue(shortHalf);

    QVector<double> tallSamples;
    for (double h : bodyHeights) {
        if (h > targetXHeight * 1.32) {
            tallSamples.append(h);
        }
    }
    const double targetTallHeight = tallSamples.isEmpty()
        ? targetXHeight * 1.62
        : medianValue(tallSamples);

    QVector<double> shortBottoms;
    QVector<double> shortTops;
    QVector<double> allBottoms;
    for (const auto& l : letters) {
        if (l.isMidOperator || l.isPunctuation) {
            continue;
        }
        allBottoms.append(l.coreBounds.bottom());
        if (l.coreBounds.height() <= targetXHeight * 1.28) {
            shortBottoms.append(l.coreBounds.bottom());
            shortTops.append(l.coreBounds.top());
        }
    }
    if (allBottoms.isEmpty()) {
        return {};
    }
    const double roughBaselineY = shortBottoms.isEmpty()
        ? medianValue(allBottoms)
        : medianValue(shortBottoms);
    const double roughXTopY = shortTops.isEmpty() ? (roughBaselineY - targetXHeight) : medianValue(shortTops);

    struct BaselineSample {
        double x;
        double y;
    };
    QVector<BaselineSample> cleanBaselineSamples;
    QVector<double> cleanBottoms;
    for (auto& l : letters) {
        if (l.isMidOperator || l.isPunctuation) {
            continue;
        }
        l.isDescender = isDescenderCluster(l, strokes, roughBaselineY, roughXTopY, targetXHeight);
        if (!l.isDescender) {
            cleanBottoms.append(l.coreBounds.bottom());
            cleanBaselineSamples.append({l.coreBounds.center().x(), l.coreBounds.bottom()});
        }
    }

    double targetBaselineY = cleanBottoms.isEmpty() ? roughBaselineY : medianValue(cleanBottoms);
    if (cleanBaselineSamples.size() >= 3) {
        const double spanX = cleanBaselineSamples.back().x - cleanBaselineSamples.front().x;
        if (spanX > targetXHeight * 1.8) {
            double sumX = 0.0;
            double sumY = 0.0;
            for (const auto& s : cleanBaselineSamples) {
                sumX += s.x;
                sumY += s.y;
            }
            const double meanX = sumX / double(cleanBaselineSamples.size());
            const double meanY = sumY / double(cleanBaselineSamples.size());
            double num = 0.0;
            double den = 0.0;
            for (const auto& s : cleanBaselineSamples) {
                num += (s.x - meanX) * (s.y - meanY);
                den += (s.x - meanX) * (s.x - meanX);
            }
            const double slope = (den > 1e-4) ? (num / den) : 0.0;
            if (std::abs(slope) > 0.025 && std::abs(slope) < 0.35) {
                const double anchorX = cleanBaselineSamples.front().x + spanX * 0.25;
                const double regressedY = meanY + slope * (anchorX - meanX);
                targetBaselineY = 0.55 * targetBaselineY + 0.45 * regressedY;
            }
        }
    }

    QVector<double> stemSlopes;
    for (const auto& l : letters) {
        if (l.isPunctuation || l.isMidOperator) {
            continue;
        }
        for (const qsizetype idx : l.indices) {
            if (l.crossbarIndices.contains(idx) || l.dotIndices.contains(idx)) {
                continue;
            }
            const auto& stroke = strokes[idx];
            const QRectF sb = computeBounds(stroke.points);
            if (sb.height() > targetXHeight * 0.85 && sb.width() < sb.height() * 0.55
                && stroke.points.size() >= 4) {
                double sumY = 0.0;
                double sumX = 0.0;
                for (const auto& pt : stroke.points) {
                    sumX += pt.x;
                    sumY += pt.y;
                }
                const double mx = sumX / double(stroke.points.size());
                const double my = sumY / double(stroke.points.size());
                double num = 0.0;
                double den = 0.0;
                for (const auto& pt : stroke.points) {
                    num += (pt.y - my) * (pt.x - mx);
                    den += (pt.y - my) * (pt.y - my);
                }
                if (den > 12.0) {
                    const double sl = num / den;
                    if (std::abs(sl) < 0.32) {
                        stemSlopes.append(sl);
                    }
                }
            }
        }
    }
    const double globalStemSlope = stemSlopes.isEmpty() ? 0.0 : medianValue(stemSlopes);

    return {true, targetXHeight, targetTallHeight, targetBaselineY, globalStemSlope};
}

// Paso 3.4: Nivelación firme en la línea base, armonización de altura y enderezado de barras y tallos.
bool alignAndDeskewRow(QVector<LetterCluster>& letters,
                       QVector<Stroke>& strokes,
                       const RowMetrics& metrics) {
    bool changed = false;

    for (auto& l : letters) {
        if (l.isPunctuation) {
            if (l.coreBounds.height() > metrics.targetXHeight * 0.80) {
                const double dy = (metrics.targetBaselineY - l.coreBounds.bottom()) * 0.90;
                if (std::abs(dy) > 0.15) {
                    for (const qsizetype idx : l.indices) {
                        auto& stroke = strokes[idx];
                        for (auto& pt : stroke.points) {
                            pt.y += dy;
                        }
                        invalidateStrokeCache(stroke);
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
        const bool isTall = !l.isMidOperator && !l.isDescender && (h > metrics.targetXHeight * 1.32);

        double dy = 0.0;
        double sy = 1.0;
        double anchorY = metrics.targetBaselineY;

        if (l.isMidOperator) {
            dy = (metrics.targetBaselineY - metrics.targetXHeight * 0.48) - l.coreBounds.center().y();
        } else if (l.isDescender) {
            // Las descendentes ('p', 'q', 'g', 'y', 'j') alinean su TECHO con el techo de las vocales
            const double targetTop = metrics.targetBaselineY - metrics.targetXHeight;
            dy = (targetTop - l.coreBounds.top()) * 0.94;
            anchorY = targetTop;
        } else {
            dy = (metrics.targetBaselineY - l.coreBounds.bottom()) * 0.95;
            anchorY = metrics.targetBaselineY;
            const double idealH = isTall ? metrics.targetTallHeight : metrics.targetXHeight;
            if (h > 4.0) {
                const double rawSy = idealH / h;
                sy = std::clamp(1.0 + (rawSy - 1.0) * 0.55, 0.84, 1.18);
            }
        }

        // Cuando l.isDescender, anchorY es el techo (targetTop) y sy == 1.0, así que la parte superior
        // del cuerpo se mueve exactamente dy y el punto de 'j' conserva su altura relativa sobre el tallo.
        const double newCoreTop = l.isDescender
            ? (l.coreBounds.top() + dy)
            : (anchorY + (l.coreBounds.top() + dy - anchorY) * sy);
        const double topShift = newCoreTop - l.coreBounds.top();

        bool letterMoved = false;
        for (const qsizetype idx : l.indices) {
            auto& stroke = strokes[idx];
            const QRectF sb = computeBounds(stroke.points);
            const bool isBar = l.crossbarIndices.contains(idx);
            const bool isDot = l.dotIndices.contains(idx);

            double deskewSlope = 0.0;
            if (!isBar && !isDot && (sb.height() > metrics.targetXHeight * 0.90 || l.isMidOperator)
                && sb.width() < sb.height() * 0.52 && stroke.points.size() >= 4) {
                double sumY = 0.0;
                double sumX = 0.0;
                for (const auto& pt : stroke.points) {
                    sumX += pt.x;
                    sumY += pt.y;
                }
                const double mx = sumX / double(stroke.points.size());
                const double my = sumY / double(stroke.points.size());
                double num = 0.0;
                double den = 0.0;
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
            } else if (!isBar && !isDot && !l.isMidOperator && std::abs(metrics.globalStemSlope) > 0.04) {
                deskewSlope = metrics.globalStemSlope * 0.32;
            }

            double barLevelCy = sb.center().y() + dy;
            barLevelCy = anchorY + (barLevelCy - anchorY) * sy;
            const double baseRefY = sb.bottom() + dy;

            QVector<StrokePoint> nextPts = stroke.points;
            double maxDelta = 0.0;
            for (auto& pt : nextPts) {
                const double origX = pt.x;
                const double origY = pt.y;
                const double shiftedY = origY + dy;
                if (isDot) {
                    pt.y += (origY < l.coreBounds.center().y()) ? topShift : dy;
                } else if (isBar) {
                    const double scaledY = anchorY + (shiftedY - anchorY) * sy;
                    pt.y = scaledY * 0.35 + barLevelCy * 0.65;
                } else {
                    pt.y = anchorY + (shiftedY - anchorY) * sy;
                    if (std::abs(deskewSlope) > 1e-4) {
                        pt.x -= (pt.y - baseRefY) * deskewSlope;
                    }
                }
                maxDelta = std::max(maxDelta, std::abs(pt.x - origX));
                maxDelta = std::max(maxDelta, std::abs(pt.y - origY));
            }
            if (maxDelta > 1e-4) {
                stroke.points = std::move(nextPts);
                invalidateStrokeCache(stroke);
                stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                letterMoved = true;
                changed = true;
            }
        }

        if (letterMoved) {
            QRectF newCore;
            QRectF newFull;
            bool firstFull = true;
            bool firstCore = true;
            for (const qsizetype idx : l.indices) {
                const QRectF sb = computeBounds(strokes[idx].points);
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
    }

    return changed;
}

// Paso 3.5: Cohesión horizontal dentro de cada palabra y respiro alrededor de operadores.
bool tightenRowSpacing(QVector<LetterCluster>& letters,
                       QVector<Stroke>& strokes,
                       const RowMetrics& metrics) {
    if (letters.size() < 2) {
        return false;
    }

    const double wordBreakGap = std::max(metrics.targetXHeight * 0.48, 13.0);
    const double minOpGap = std::clamp(metrics.targetXHeight * 0.24, 6.0, 16.0);
    QVector<double> intraGaps;
    for (qsizetype i = 1; i < letters.size(); ++i) {
        if (letters[i].isMidOperator || letters[i - 1].isMidOperator) {
            continue;
        }
        const double g = letters[i].coreBounds.left() - letters[i - 1].coreBounds.right();
        if (g >= -2.0 && g < wordBreakGap) {
            intraGaps.append(std::max(0.0, g));
        }
    }
    const double naturalIntraGap = intraGaps.isEmpty()
        ? 2.0
        : std::clamp(medianValue(intraGaps), 0.5, metrics.targetXHeight * 0.14);

    bool changed = false;
    double cumulativeShiftX = 0.0;
    double prevOriginalRight = letters[0].fullBounds.right();
    double prevOriginalCoreRight = letters[0].coreBounds.right();
    double prevShiftedCoreRight = letters[0].coreBounds.right();

    for (qsizetype i = 1; i < letters.size(); ++i) {
        const bool touchesOperator = letters[i].isMidOperator || letters[i - 1].isMidOperator;
        const double rawFullGap = letters[i].fullBounds.left() - prevOriginalRight;
        const double rawCoreGap = letters[i].coreBounds.left() - prevOriginalCoreRight;
        prevOriginalRight = letters[i].fullBounds.right();
        prevOriginalCoreRight = letters[i].coreBounds.right();

        double dx = cumulativeShiftX;
        if (!touchesOperator && rawCoreGap >= wordBreakGap) {
            // Al iniciar una nueva palabra, verificar el borde derecho ya desplazado de la palabra
            // anterior para que el respiro positivo de operadores jamás cause solapamiento entre palabras.
            const double shiftedCoreGap = letters[i].coreBounds.left() - prevShiftedCoreRight;
            if (shiftedCoreGap < wordBreakGap - 0.15) {
                dx = wordBreakGap - shiftedCoreGap;
                cumulativeShiftX = dx;
            } else {
                dx = 0.0;
                cumulativeShiftX = 0.0;
            }
        } else if (touchesOperator) {
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
                auto& stroke = strokes[idx];
                for (auto& pt : stroke.points) {
                    pt.x += dx;
                }
                invalidateStrokeCache(stroke);
                stroke.bounds = computeBounds(stroke.points, stroke.size * 0.7);
                changed = true;
            }
            letters[i].coreBounds.translate(dx, 0.0);
            letters[i].fullBounds.translate(dx, 0.0);
        }
        prevShiftedCoreRight = letters[i].coreBounds.right();
    }

    return changed;
}

} // namespace

void selectByLasso(const QPolygonF& poly,
                   const QVector<Stroke>& strokes,
                   const QVector<ShapeItem>& shapes,
                   QSet<QString>& strokeIds,
                   QSet<QString>& shapeIds) {
    strokeIds.clear();
    shapeIds.clear();
    if (poly.size() < 3) {
        return;
    }
    for (const auto& s : strokes) {
        if (s.points.isEmpty()) {
            continue;
        }
        qsizetype inside = 0;
        QPointF center;
        for (const auto& p : s.points) {
            const QPointF q(p.x, p.y);
            center += q;
            if (poly.containsPoint(q, Qt::OddEvenFill)) {
                ++inside;
            }
        }
        center /= double(s.points.size());
        if (double(inside) / double(s.points.size()) >= 0.40
            || poly.containsPoint(center, Qt::OddEvenFill)) {
            strokeIds.insert(s.id);
        }
    }
    for (const auto& s : shapes) {
        if (s.vertices.isEmpty() || !s.shadeVisible) {
            continue;
        }
        bool selected = poly.containsPoint(computeShapeBounds(s).center(), Qt::OddEvenFill);
        for (const QPointF p : getShapeHandles(s)) {
            selected = selected || poly.containsPoint(p, Qt::OddEvenFill);
        }
        if (selected) {
            shapeIds.insert(s.id);
        }
    }
}

QRectF computeSelectionBounds(const QVector<Stroke>& strokes,
                              const QVector<ShapeItem>& shapes,
                              const QSet<QString>& strokeIds,
                              const QSet<QString>& shapeIds) {
    QRectF bounds;
    bool first = true;
    auto add = [&](const QRectF& r) {
        if (first) {
            bounds = r;
            first = false;
        } else {
            bounds = bounds.united(r);
        }
    };
    for (const auto& s : strokes) {
        if (strokeIds.contains(s.id) && !s.points.isEmpty()) {
            add(computeBounds(s.points, s.size * 0.7));
        }
    }
    for (const auto& s : shapes) {
        if (shapeIds.contains(s.id) && !s.vertices.isEmpty() && s.shadeVisible) {
            add(computeShapeBounds(s));
        }
    }
    return bounds;
}

void translateSelection(QVector<Stroke>& strokes,
                        QVector<ShapeItem>& shapes,
                        const QSet<QString>& strokeIds,
                        const QSet<QString>& shapeIds,
                        const QPointF& delta) {
    transform(strokes, shapes, strokeIds, shapeIds, [&](QPointF p) { return p + delta; });
}

void scaleSelection(QVector<Stroke>& strokes,
                    QVector<ShapeItem>& shapes,
                    const QSet<QString>& strokeIds,
                    const QSet<QString>& shapeIds,
                    const QPointF& anchor,
                    double sx,
                    double sy) {
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0 || sy <= 0.0) {
        return;
    }
    const double widthScale = std::sqrt(sx * sy);
    transform(
        strokes,
        shapes,
        strokeIds,
        {},
        [&](QPointF p) {
            return anchor + QPointF((p.x() - anchor.x()) * sx, (p.y() - anchor.y()) * sy);
        },
        widthScale);
    for (auto& s : shapes) {
        if (s.locked || !shapeIds.contains(s.id)) {
            continue;
        }
        s.size = std::clamp(s.size * widthScale, 0.1, 200.0);
        scaleShape(s, anchor, sx, sy);
        if (!s.shadeBoundary.isEmpty() || s.kind == ShapeKind::ShadedRegion) {
            s.shadeProbe = anchor
                         + QPointF((s.shadeProbe.x() - anchor.x()) * sx,
                                   (s.shadeProbe.y() - anchor.y()) * sy);
        }
    }
}

void rotateSelection(QVector<Stroke>& strokes,
                     QVector<ShapeItem>& shapes,
                     const QSet<QString>& strokeIds,
                     const QSet<QString>& shapeIds,
                     const QPointF& center,
                     double angle) {
    if (!std::isfinite(angle) || std::abs(angle) < 1e-12) {
        return;
    }
    for (auto& s : shapes) {
        if (!s.locked && shapeIds.contains(s.id)) {
            makeRotatable(s);
        }
    }
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    transform(strokes, shapes, strokeIds, shapeIds, [&](QPointF p) {
        const QPointF d = p - center;
        return center + QPointF(d.x() * cosA - d.y() * sinA, d.x() * sinA + d.y() * cosA);
    });
    for (auto& stroke : strokes) {
        if (!strokeIds.contains(stroke.id)) {
            continue;
        }
        for (auto& p : stroke.points) {
            const double tx = p.tiltX;
            const double ty = p.tiltY;
            p.tiltX = tx * cosA - ty * sinA;
            p.tiltY = tx * sinA + ty * cosA;
        }
    }
}

void flipSelection(QVector<Stroke>& strokes,
                   QVector<ShapeItem>& shapes,
                   const QSet<QString>& strokeIds,
                   const QSet<QString>& shapeIds,
                   const QRectF& bounds,
                   bool horizontal) {
    if (bounds.isEmpty()) {
        return;
    }
    const QPointF c = bounds.center();
    auto reflect = [&](const QPointF& p) -> QPointF {
        return horizontal ? QPointF(2.0 * c.x() - p.x(), p.y())
                          : QPointF(p.x(), 2.0 * c.y() - p.y());
    };
    for (auto& s : strokes) {
        if (!strokeIds.contains(s.id)) {
            continue;
        }
        for (auto& p : s.points) {
            const QPointF q = reflect(QPointF(p.x, p.y));
            p.x = q.x();
            p.y = q.y();
            if (horizontal) {
                p.tiltX = -p.tiltX;
            } else {
                p.tiltY = -p.tiltY;
            }
        }
        invalidateStrokeCache(s);
        s.bounds = computeBounds(s.points, s.size * 0.7);
    }
    for (auto& s : shapes) {
        if (s.locked || !shapeIds.contains(s.id)) {
            continue;
        }
        if (s.kind == ShapeKind::Radical && horizontal) {
            makeRotatable(s);
        }
        for (auto& p : s.vertices) {
            p = reflect(p);
        }
        if (!s.shadeBoundary.isEmpty() || s.kind == ShapeKind::ShadedRegion) {
            s.shadeProbe = reflect(s.shadeProbe);
        }
        if ((s.kind == ShapeKind::Rectangle || s.kind == ShapeKind::Ellipse
             || s.kind == ShapeKind::Axes || s.kind == ShapeKind::Solid3D)
            && s.vertices.size() == 2) {
            const QRectF nr = QRectF(s.vertices[0], s.vertices[1]).normalized();
            s.vertices = {nr.topLeft(), nr.bottomRight()};
        } else if ((s.kind == ShapeKind::Arc || s.kind == ShapeKind::Sector
                    || s.kind == ShapeKind::AngleMarker)
                   && s.vertices.size() >= 3) {
            std::swap(s.vertices[1], s.vertices[2]);
        }
        s.bounds = computeShapeBounds(s);
    }
}

bool shiftContentVertically(QVector<Stroke>& strokes,
                            QVector<ShapeItem>& shapes,
                            QVector<ImageItem>& images,
                            QVector<TextItem>& texts,
                            double yThreshold,
                            double deltaY) {
    if (!std::isfinite(yThreshold) || !std::isfinite(deltaY) || std::abs(deltaY) < 0.5) {
        return false;
    }
    bool moved = false;
    for (auto& s : strokes) {
        if (s.points.isEmpty()) {
            continue;
        }
        const QRectF b = s.bounds.isEmpty() ? computeBounds(s.points, s.size * 0.7) : s.bounds;
        if (b.top() >= yThreshold) {
            for (auto& p : s.points) {
                p.y += deltaY;
            }
            invalidateStrokeCache(s);
            s.bounds = b.translated(0.0, deltaY);
            moved = true;
        }
    }
    const QVector<ShapeItem> originalShapes = shapes;
    QSet<QString> movedShapes;
    for (auto& s : shapes) {
        if (s.locked || s.vertices.isEmpty()) {
            continue;
        }
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

bool straightenSelectedStrokes(QVector<Stroke>& strokes, const QSet<QString>& ids) {
    QVector<SelectedStroke> selected;
    QVector<double> allHeights;
    for (qsizetype i = 0; i < strokes.size(); ++i) {
        if (ids.contains(strokes[i].id) && !strokes[i].points.isEmpty()) {
            const QRectF b = computeBounds(strokes[i].points);
            selected.append({i, b});
            if (b.height() > 5.0) {
                allHeights.append(b.height());
            }
        }
    }
    if (selected.isEmpty()) {
        return false;
    }

    QVector<Stroke> result = strokes;
    bool changed = false;
    const double baseH = std::clamp(allHeights.isEmpty() ? 24.0 : medianValue(allHeights), 10.0, 160.0);

    for (auto& item : selected) {
        if (smoothStrokeTaubin(result[item.index], item.bounds, baseH)) {
            changed = true;
        }
    }

    auto rows = splitIntoRows(std::move(selected), baseH);
    for (auto& row : rows) {
        auto letters = buildRowClusters(std::move(row), baseH);
        const RowMetrics metrics = analyzeRowMetrics(letters, result, baseH, rows.size());
        if (!metrics.valid) {
            continue;
        }
        if (alignAndDeskewRow(letters, result, metrics)) {
            changed = true;
        }
        if (tightenRowSpacing(letters, result, metrics)) {
            changed = true;
        }
    }

    if (changed) {
        strokes = std::move(result);
    }
    return changed;
}

void drawLassoPath(QPainter& p, const QPolygonF& poly, const ThemeConfig& theme, double zoom) {
    if (poly.isEmpty()) {
        return;
    }
    zoom = std::max(0.01, zoom);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    QPen pen(theme.accent, 1.4 / zoom, Qt::DashLine);
    pen.setDashOffset(double(timer.elapsed() % 1200) / 120.0);
    p.setPen(pen);
    QColor fill = theme.accent;
    fill.setAlpha(25);
    p.setBrush(fill);
    p.drawPolygon(poly);
    p.restore();
}

void drawLassoSelectionBox(QPainter& p,
                           const QRectF& bounds,
                           const ThemeConfig& theme,
                           double zoom,
                           bool showHandles) {
    if (bounds.isEmpty()) {
        return;
    }
    zoom = std::max(0.01, zoom);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme.accent, 1.2 / zoom, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bounds);
    if (!showHandles) {
        p.restore();
        return;
    }
    const auto points = handles(bounds, zoom);
    p.setPen(QPen(theme.accent, 1.5 / zoom));
    p.drawLine(QPointF(bounds.center().x(), bounds.top()), points[4]);
    p.setBrush(theme.isDark ? theme.background : QColor(Qt::white));
    for (const QPointF point : points) {
        p.drawEllipse(point, 5.5 / zoom, 5.5 / zoom);
    }
    p.restore();
}

int hitTestLassoHandle(const QRectF& bounds, const QPointF& point, double zoom) {
    if (bounds.isEmpty()) {
        return -1;
    }
    const auto points = handles(bounds, zoom);
    const double clampedZoom = std::max(0.01, zoom);
    for (int i = 4; i >= 0; --i) {
        if (length(point - points[i]) * clampedZoom <= 10.0) {
            return i;
        }
    }
    return -1;
}

} // namespace finily
