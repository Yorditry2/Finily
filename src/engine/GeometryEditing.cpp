#include "GeometryEngine.h"
#include "GeometryInternal.h"

#include <QTransform>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finily {
using namespace geometry_detail;

bool isCongruenceMarkShape(const ShapeItem& shape) {
    return shape.kind == ShapeKind::Line && isMarkLabel(shape.label);
}

namespace {

struct CandidateEdge {
    QString shapeId;
    int outlineEdgeIndex = 0;
    QPointF a;
    QPointF b;
    double len = 0.0;
};

QVector<CandidateEdge> collectGeometryEdges(const QVector<ShapeItem>& shapes, double minLenWorld = 16.0) {
    QVector<CandidateEdge> edges;
    for (const auto& s : shapes) {
        if (s.kind == ShapeKind::ShadedRegion || s.kind == ShapeKind::AngleMarker || isMarkLabel(s.label)
            || s.vertices.size() < 2)
            continue;
        if (s.kind == ShapeKind::Line || s.kind == ShapeKind::Triangle || s.kind == ShapeKind::Rectangle
            || s.kind == ShapeKind::Polyline || s.kind == ShapeKind::Solid3D) {
            int edgeIdx = 0;
            for (const auto& outline : shapeOutlines(s)) {
                for (qsizetype j = 1; j < outline.size(); ++j, ++edgeIdx) {
                    const QPointF a = outline[j - 1];
                    const QPointF b = outline[j];
                    const double l = length(b - a);
                    if (l >= minLenWorld)
                        edges.append({s.id, edgeIdx, a, b, l});
                }
            }
        }
    }
    return edges;
}

bool segmentIntersectionInterior(const QPointF& a1, const QPointF& a2,
                                 const QPointF& b1, const QPointF& b2,
                                 QPointF* outHit, double* outT = nullptr, double* outU = nullptr) {
    const QPointF d1 = a2 - a1;
    const QPointF d2 = b2 - b1;
    const double denom = d1.x() * d2.y() - d1.y() * d2.x();
    if (std::abs(denom) < 1e-7)
        return false;
    const QPointF diff = b1 - a1;
    const double t = (diff.x() * d2.y() - diff.y() * d2.x()) / denom;
    const double u = (diff.x() * d1.y() - diff.y() * d1.x()) / denom;
    if (t >= -0.05 && t <= 1.05 && u >= -0.05 && u <= 1.05) {
        if (outHit)
            *outHit = a1 + d1 * std::clamp(t, 0.0, 1.0);
        if (outT)
            *outT = t;
        if (outU)
            *outU = u;
        return true;
    }
    return false;
}

double findParentStrokeSize(const QVector<ShapeItem>& shapes, const QString& id, double fallback) {
    for (const auto& s : shapes) {
        if (s.id == id)
            return std::clamp(s.size * 0.95, 1.4, 4.5);
    }
    return std::clamp(fallback * 0.95, 1.4, 4.5);
}

bool isPartiallyErasableShape(const ShapeItem& shape) {
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Polyline:
    case ShapeKind::Triangle:
    case ShapeKind::Rectangle:
    case ShapeKind::Circle:
    case ShapeKind::Ellipse:
    case ShapeKind::Curve:
    case ShapeKind::Arc:
        return true;
    default:
        return false;
    }
}

struct ShapeChain {
    QVector<QPointF> pts;
    bool closed = false;
};

QVector<ShapeChain> extractShapeChains(const ShapeItem& shape) {
    QVector<ShapeChain> chains;
    if (shape.kind == ShapeKind::Line && shape.vertices.size() >= 2) {
        chains.append({{shape.vertices[0], shape.vertices[1]}, false});
    } else if (shape.kind == ShapeKind::Polyline && shape.vertices.size() >= 2) {
        chains.append({shape.vertices, shape.closed});
    } else if (shape.kind == ShapeKind::Triangle && shape.vertices.size() >= 3) {
        chains.append({{shape.vertices[0], shape.vertices[1], shape.vertices[2]}, true});
    } else if (shape.kind == ShapeKind::Rectangle && shape.vertices.size() >= 2) {
        if (shape.vertices.size() >= 4) {
            chains.append({shape.vertices.mid(0, 4), true});
        } else {
            const QRectF r = QRectF(shape.vertices[0], shape.vertices[1]).normalized();
            chains.append({{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}, true});
        }
    } else {
        const QPainterPath p = shapePath(shape);
        const auto polys = p.toSubpathPolygons(QTransform());
        for (const auto& poly : polys) {
            if (poly.size() < 2)
                continue;
            QVector<QPointF> pts;
            pts.reserve(poly.size());
            for (const auto& pt : poly)
                if (pts.isEmpty() || length(pt - pts.back()) > 1e-4)
                    pts.append(pt);
            if (pts.size() < 2)
                continue;
            bool closed = (shape.kind == ShapeKind::Circle || shape.kind == ShapeKind::Ellipse);
            if (pts.size() >= 3 && length(pts.front() - pts.back()) < 1e-2) {
                pts.removeLast();
                closed = true;
            }
            if (pts.size() >= 2)
                chains.append({pts, closed});
        }
    }
    return chains;
}

ShapeItem makePieceFromPoints(const ShapeItem& source, const QVector<QPointF>& pts) {
    ShapeItem piece = source;
    piece.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    piece.kind = (pts.size() == 2) ? ShapeKind::Line : ShapeKind::Polyline;
    piece.vertices = pts;
    piece.closed = false;
    piece.label.clear();
    piece.attachments.clear();
    piece.attachmentsKnown = false;
    piece.bounds = computeShapeBounds(piece);
    return piece;
}

double segPointDistSq(const QPointF& p, const QPointF& a, const QPointF& b, double* outT = nullptr) {
    const QPointF d = b - a;
    const double dd = dot(d, d);
    const double t = dd > 1e-12 ? std::clamp(dot(p - a, d) / dd, 0.0, 1.0) : 0.0;
    if (outT)
        *outT = t;
    const QPointF diff = p - (a + d * t);
    return dot(diff, diff);
}

std::optional<double> segmentIntersectionT(const QPointF& a, const QPointF& b,
                                           const QPointF& c, const QPointF& d) {
    const QPointF r = b - a;
    const QPointF s = d - c;
    const double denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) < 1e-9)
        return std::nullopt;
    const QPointF ca = c - a;
    const double t = (ca.x() * s.y() - ca.y() * s.x()) / denom;
    const double u = (ca.x() * r.y() - ca.y() * r.x()) / denom;
    if (t > 1e-4 && t < 1.0 - 1e-4 && u >= -1e-4 && u <= 1.0 + 1e-4)
        return t;
    return std::nullopt;
}

} // namespace

std::optional<ShapeItem> trySnapAngleArcAtVertex(const Stroke& stroke, const QVector<ShapeItem>& shapes, double zoom) {
    if (stroke.points.size() < 4 || shapes.isEmpty())
        return std::nullopt;
    zoom = std::max(0.1, zoom);

    QVector<QPointF> pts;
    pts.reserve(stroke.points.size());
    double pathLen = 0.0;
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        const QPointF p(stroke.points[i].x, stroke.points[i].y);
        if (i > 0)
            pathLen += length(p - pts.back());
        pts.append(p);
    }
    const QRectF b = vertexBounds(pts);
    const double dim = std::max(b.width(), b.height());
    if (dim * zoom < 8.0 || dim * zoom > 155.0 || pathLen < dim * 0.78 || pathLen > dim * 2.65)
        return std::nullopt;

    const QPointF pStart = pts.front();
    const QPointF pEnd = pts.back();
    const double chord = length(pEnd - pStart);
    if (chord < std::max(5.0 / zoom, dim * 0.36) || pathLen > chord * 2.35)
        return std::nullopt;

    for (qsizetype i = 0; i + 2 < pts.size(); ++i) {
        for (qsizetype j = i + 3; j + 1 < pts.size(); ++j) {
            QPointF hit;
            double t = 0.0, u = 0.0;
            if (segmentIntersectionInterior(pts[i], pts[i + 1], pts[j], pts[j + 1], &hit, &t, &u)
                && t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0)
                return std::nullopt;
        }
    }

    const QPointF chordVec = pEnd - pStart;
    const QPointF chordNorm(-chordVec.y() / chord, chordVec.x() / chord);
    double maxPosBulge = 0.0, maxNegBulge = 0.0;
    for (const QPointF& pt : pts) {
        const double signedDist = dot(pt - pStart, chordNorm);
        maxPosBulge = std::max(maxPosBulge, signedDist);
        maxNegBulge = std::max(maxNegBulge, -signedDist);
    }
    const double mainBulge = std::max(maxPosBulge, maxNegBulge);
    const double oppBulge = std::min(maxPosBulge, maxNegBulge);
    if (mainBulge < std::max(1.2 / zoom, chord * 0.04) || oppBulge > std::max(3.5 / zoom, mainBulge * 0.38))
        return std::nullopt;

    const auto edges = collectGeometryEdges(shapes, 22.0 / zoom);
    if (edges.size() < 2)
        return std::nullopt;

    const QPointF pMid = pts[pts.size() / 2];
    const double vertexJoinTol = 16.0 / zoom;
    const double endpointOverhangTol = 38.0 / zoom;

    double bestScore = 1e9;
    QPointF bestV, bestArm1, bestArm2;
    bool found = false;

    auto evaluateCorner = [&](const QPointF& V, const QPointF& dir1, double len1, const QPointF& dir2, double len2) {
        const double cosCorner = std::clamp(dot(dir1, dir2), -1.0, 1.0);
        if (cosCorner > 0.978 || cosCorner < -0.990)
            return;

        const QPointF bisectorRaw = dir1 + dir2;
        const double bisLen = length(bisectorRaw);
        if (bisLen < 1e-4)
            return;
        const QPointF bisector = bisectorRaw / bisLen;
        const double halfCornerAngle = std::acos(cosCorner) * 0.5;

        const QPointF vMid = pMid - V;
        const double rMid = length(vMid);
        if (rMid < 8.0 / zoom || rMid > std::min({len1 * 0.85, len2 * 0.85, 130.0 / zoom}))
            return;
        const double midAngleToBis = std::acos(std::clamp(dot(vMid / rMid, bisector), -1.0, 1.0));
        if (midAngleToBis > halfCornerAngle + 0.22)
            return;

        const QPointF chordMid = (pStart + pEnd) * 0.5;
        if (rMid < length(chordMid - V) * 0.84)
            return;
        const double chordDotBis = std::abs(dot(chordVec / chord, bisector));
        if (chordDotBis > 0.65)
            return;

        const QPointF rayEnd1 = V + dir1 * len1;
        const QPointF rayEnd2 = V + dir2 * len2;

        double minStrokeDist1 = 1e9, minStrokeDist2 = 1e9;
        double rSum = 0.0;
        int insideCount = 0;
        for (const QPointF& pt : pts) {
            minStrokeDist1 = std::min(minStrokeDist1, segmentDistance(pt, V, rayEnd1));
            minStrokeDist2 = std::min(minStrokeDist2, segmentDistance(pt, V, rayEnd2));
            const QPointF vPt = pt - V;
            const double rPt = length(vPt);
            if (rPt > 2.0) {
                const double ang = std::acos(std::clamp(dot(vPt / rPt, bisector), -1.0, 1.0));
                if (ang <= halfCornerAngle + 0.15) {
                    rSum += rPt;
                    ++insideCount;
                }
            }
        }
        if (insideCount < 2)
            return;
        const double touchTol = 18.0 / zoom;
        if (minStrokeDist1 > touchTol || minStrokeDist2 > touchTol)
            return;

        const double dStart1 = segmentDistance(pStart, V, rayEnd1);
        const double dEnd2 = segmentDistance(pEnd, V, rayEnd2);
        const double dStart2 = segmentDistance(pStart, V, rayEnd2);
        const double dEnd1 = segmentDistance(pEnd, V, rayEnd1);
        const bool match12 = (dStart1 <= endpointOverhangTol && dEnd2 <= endpointOverhangTol && (dStart1 + dEnd2) <= (dStart2 + dEnd1));
        const bool match21 = (dStart2 <= endpointOverhangTol && dEnd1 <= endpointOverhangTol);
        if (!match12 && !match21)
            return;

        const double rClean = (rSum / insideCount) * 0.5 + rMid * 0.5;
        const double endpointErr = match12 ? (dStart1 + dEnd2) : (dStart2 + dEnd1);
        const double score = (minStrokeDist1 + minStrokeDist2) * 1.5 + endpointErr * 0.4 + midAngleToBis * 12.0;
        if (score < bestScore) {
            bestScore = score;
            bestV = V;
            const double armSpan = rClean / 0.32;
            if (match12) {
                bestArm1 = V + dir1 * armSpan;
                bestArm2 = V + dir2 * armSpan;
            } else {
                bestArm1 = V + dir2 * armSpan;
                bestArm2 = V + dir1 * armSpan;
            }
            found = true;
        }
    };

    for (qsizetype i = 0; i < edges.size(); ++i) {
        for (qsizetype j = i + 1; j < edges.size(); ++j) {
            const QPointF endsI[2] = {edges[i].a, edges[i].b};
            const QPointF endsJ[2] = {edges[j].a, edges[j].b};
            bool sharedEndpoint = false;
            for (int ei = 0; ei < 2; ++ei) {
                for (int ej = 0; ej < 2; ++ej) {
                    if (length(endsI[ei] - endsJ[ej]) <= vertexJoinTol) {
                        sharedEndpoint = true;
                        const QPointF V = (endsI[ei] + endsJ[ej]) * 0.5;
                        const QPointF otherI = endsI[1 - ei];
                        const QPointF otherJ = endsJ[1 - ej];
                        const double lenI = length(otherI - V);
                        const double lenJ = length(otherJ - V);
                        if (lenI >= 20.0 / zoom && lenJ >= 20.0 / zoom)
                            evaluateCorner(V, (otherI - V) / lenI, lenI, (otherJ - V) / lenJ, lenJ);
                    }
                }
            }
            if (!sharedEndpoint) {
                QPointF V;
                if (segmentIntersectionInterior(edges[i].a, edges[i].b, edges[j].a, edges[j].b, &V)) {
                    for (int ei = 0; ei < 2; ++ei) {
                        for (int ej = 0; ej < 2; ++ej) {
                            const double lenI = length(endsI[ei] - V);
                            const double lenJ = length(endsJ[ej] - V);
                            if (lenI >= 18.0 / zoom && lenJ >= 18.0 / zoom)
                                evaluateCorner(V, (endsI[ei] - V) / lenI, lenI, (endsJ[ej] - V) / lenJ, lenJ);
                        }
                    }
                }
            }
        }
    }

    if (!found)
        return std::nullopt;

    ShapeItem marker;
    marker.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    marker.kind = ShapeKind::AngleMarker;
    marker.color = stroke.color;
    marker.size = stroke.size;
    marker.opacity = stroke.opacity;
    marker.linePattern = LinePattern::Solid;
    marker.closed = false;
    marker.label = QStringLiteral("auto_vertex");
    marker.vertices = {bestV, bestArm1, bestArm2};
    marker.bounds = computeShapeBounds(marker);
    return marker;
}

std::optional<ShapeItem> trySnapCongruenceStrokeOnEdge(const Stroke& stroke,
                                                       const QVector<ShapeItem>& shapes,
                                                       double zoom,
                                                       bool allowSimpleMarks) {
    if (stroke.points.size() < (allowSimpleMarks ? 4 : 8) || shapes.isEmpty())
        return std::nullopt;
    zoom = std::max(0.1, zoom);

    QVector<QPointF> pts;
    pts.reserve(stroke.points.size());
    double pathLen = 0.0;
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        const QPointF p(stroke.points[i].x, stroke.points[i].y);
        if (i > 0)
            pathLen += length(p - pts.back());
        pts.append(p);
    }
    const QRectF b = vertexBounds(pts);
    const double dim = std::max(b.width(), b.height());
    const double minDimPx = allowSimpleMarks ? 8.0 : 12.0;
    if (dim * zoom < minDimPx || dim * zoom > 220.0)
        return std::nullopt;

    const auto edges = collectGeometryEdges(shapes, 24.0 / zoom);
    if (edges.isEmpty())
        return std::nullopt;

    const QPointF center = b.center();
    int bestEdgeIdx = -1;
    double bestDist = 30.0 / zoom;
    double bestT = 0.5;
    for (int i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        const QPointF d = e.b - e.a;
        const double t = std::clamp(dot(center - e.a, d) / dot(d, d), 0.0, 1.0);
        if (t < 0.12 || t > 0.88)
            continue;
        const double dist = length(center - (e.a + d * t));
        if (dist < bestDist) {
            bestDist = dist;
            bestEdgeIdx = i;
            bestT = t;
        }
    }
    if (bestEdgeIdx < 0)
        return std::nullopt;

    const auto& edge = edges[bestEdgeIdx];
    const QPointF u = (edge.b - edge.a) / edge.len;
    const QPointF n(-u.y(), u.x());

    QVector<double> alongs, normals;
    alongs.reserve(pts.size());
    normals.reserve(pts.size());
    const QPointF origin = edge.a + (edge.b - edge.a) * bestT;
    double minAlong = std::numeric_limits<double>::max();
    double maxAlong = std::numeric_limits<double>::lowest();
    double minNormal = std::numeric_limits<double>::max();
    double maxNormal = std::numeric_limits<double>::lowest();
    for (const QPointF& pt : pts) {
        const QPointF rel = pt - origin;
        const double av = dot(rel, u);
        const double nv = dot(rel, n);
        alongs.append(av);
        normals.append(nv);
        minAlong = std::min(minAlong, av);
        maxAlong = std::max(maxAlong, av);
        minNormal = std::min(minNormal, nv);
        maxNormal = std::max(maxNormal, nv);
    }

    QVector<double> smoothNormals(normals.size());
    for (qsizetype i = 0; i < normals.size(); ++i) {
        if (i == 0 || i + 1 == normals.size())
            smoothNormals[i] = normals[i];
        else
            smoothNormals[i] = 0.25 * normals[i - 1] + 0.5 * normals[i] + 0.25 * normals[i + 1];
    }

    const double normalSpan = maxNormal - minNormal;
    const double alongSpan = maxAlong - minAlong;
    int reversals = 0;
    QVector<qsizetype> peakIndices;
    int dir = 0;
    double lastExtreme = smoothNormals.front();
    qsizetype lastExtremeIdx = 0;
    const double hyst = std::max(1.5 / zoom, normalSpan * 0.22);
    for (qsizetype i = 1; i < smoothNormals.size(); ++i) {
        const double diff = smoothNormals[i] - lastExtreme;
        if (dir >= 0 && diff < -hyst) {
            if (dir != 0)
                peakIndices.append(lastExtremeIdx);
            ++reversals;
            dir = -1;
            lastExtreme = smoothNormals[i];
            lastExtremeIdx = i;
        } else if (dir <= 0 && diff > hyst) {
            if (dir != 0)
                peakIndices.append(lastExtremeIdx);
            ++reversals;
            dir = 1;
            lastExtreme = smoothNormals[i];
            lastExtremeIdx = i;
        } else {
            if (dir >= 0 && smoothNormals[i] > lastExtreme) {
                lastExtreme = smoothNormals[i];
                lastExtremeIdx = i;
            }
            if (dir <= 0 && smoothNormals[i] < lastExtreme) {
                lastExtreme = smoothNormals[i];
                lastExtremeIdx = i;
            }
        }
    }

    QString markKind;
    const double netAlong = std::abs(alongs.back() - alongs.front());
    const int minReversals = allowSimpleMarks ? 2 : 3;
    const double minPathFactor = allowSimpleMarks ? 1.28 : 1.42;

    if (pathLen >= dim * minPathFactor
        && normalSpan >= 3.5 / zoom && normalSpan <= 95.0 / zoom
        && reversals >= minReversals
        && alongSpan >= std::max(8.0 / zoom, normalSpan * 0.55)
        && netAlong >= std::max(6.0 / zoom, normalSpan * 0.35)) {
        int sharpPeaks = 0;
        double cosSum = 0.0;
        int validPeaks = 0;
        const double stepDist = std::max(2.5 / zoom, normalSpan * 0.18);
        for (qsizetype idx : peakIndices) {
            qsizetype iPrev = idx;
            while (iPrev > 0 && length(pts[idx] - pts[iPrev]) < stepDist)
                --iPrev;
            qsizetype iNext = idx;
            while (iNext + 1 < pts.size() && length(pts[iNext] - pts[idx]) < stepDist)
                ++iNext;
            const QPointF v1 = pts[idx] - pts[iPrev];
            const QPointF v2 = pts[iNext] - pts[idx];
            const double l1 = length(v1), l2 = length(v2);
            if (l1 > 1e-3 && l2 > 1e-3) {
                const double c = std::clamp(dot(v1, v2) / (l1 * l2), -1.0, 1.0);
                cosSum += c;
                ++validPeaks;
                if (c < 0.15)
                    ++sharpPeaks;
            }
        }
        const bool isZigzag = (validPeaks > 0)
            && (sharpPeaks * 2 >= validPeaks || (cosSum / validPeaks) < 0.12);
        markKind = isZigzag ? QStringLiteral("mark:zigzag") : QStringLiteral("mark:spring");
    }

    if (markKind.isEmpty() && allowSimpleMarks && bestDist <= 30.0 / zoom) {
        const QPointF endVec = pts.back() - pts.front();
        const double straightDist = length(endVec);
        const double minBox = std::min(b.width(), b.height());
        const double aspect = dim > 1e-4 ? (minBox / dim) : 0.0;

        if ((straightDist <= std::max(12.0 / zoom, dim * 0.60)
             && aspect >= 0.40
             && dim * zoom <= 52.0
             && pathLen >= dim * 1.65)
            || (dim * zoom <= 18.0 && aspect >= 0.45 && pathLen >= dim * 1.2)) {
            markKind = QStringLiteral("mark:dot");
        }

        if (markKind.isEmpty() && dim * zoom <= 85.0 && straightDist > 1e-4 && pathLen >= straightDist * 1.12) {
            const double endN = std::abs(dot(endVec, n));
            const double endU = std::abs(dot(endVec, u));
            if (endN >= endU * 0.75) {
                qsizetype farIdx = -1;
                double maxChordDist = 0.0;
                for (qsizetype i = 1; i + 1 < pts.size(); ++i) {
                    const double cd = segmentDistance(pts[i], pts.front(), pts.back());
                    if (cd > maxChordDist) {
                        maxChordDist = cd;
                        farIdx = i;
                    }
                }
                bool hasCorner = false;
                if (farIdx > 0 && maxChordDist >= std::max(2.5 / zoom, straightDist * 0.18)) {
                    const double localStep = std::max(2.5 / zoom, dim * 0.22);
                    qsizetype iPrev = farIdx;
                    while (iPrev > 0 && length(pts[farIdx] - pts[iPrev]) < localStep)
                        --iPrev;
                    qsizetype iNext = farIdx;
                    while (iNext + 1 < pts.size() && length(pts[iNext] - pts[farIdx]) < localStep)
                        ++iNext;
                    const QPointF v1 = pts[farIdx] - pts[iPrev];
                    const QPointF v2 = pts[iNext] - pts[farIdx];
                    const double l1 = length(v1), l2 = length(v2);
                    if (l1 > 1e-3 && l2 > 1e-3 && (dot(v1, v2) / (l1 * l2)) < 0.55)
                        hasCorner = true;
                }
                if (hasCorner || (reversals == 1 && maxChordDist >= straightDist * 0.18))
                    markKind = QStringLiteral("mark:parallel1");
            }
        }

        if (markKind.isEmpty()
            && straightDist > 1e-4
            && pathLen <= straightDist * 1.38
            && dim * zoom <= 75.0) {
            const QPointF dirVec = endVec / straightDist;
            if (std::abs(dot(dirVec, n)) >= 0.52
                && minNormal <= 10.0 / zoom && maxNormal >= -10.0 / zoom) {
                markKind = QStringLiteral("mark:tick1");
            }
        }

        if (markKind.isEmpty() && dim * zoom <= 90.0) {
            const bool straightAlong = straightDist > 1e-4
                && pathLen <= straightDist * 1.45
                && std::abs(dot(endVec / straightDist, u)) >= 0.72;
            const bool elongatedBoxAlong = alongSpan > normalSpan * 1.45 && alongSpan >= 6.0 / zoom;
            if (straightAlong || elongatedBoxAlong)
                markKind = QStringLiteral("mark:bar");
        }
    }

    if (markKind.isEmpty())
        return std::nullopt;

    const double baseSize = findParentStrokeSize(shapes, edge.shapeId, stroke.size);
    const double span = std::clamp(edge.len * 0.22, 16.0, 36.0);
    const double halfT = std::clamp((span * 0.5) / edge.len, 0.06, 0.35);
    const double t0 = std::clamp(0.5 - halfT, 0.05, 0.48);
    const double t1 = std::clamp(0.5 + halfT, 0.52, 0.95);

    ShapeItem mark;
    mark.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mark.kind = ShapeKind::Line;
    mark.color = stroke.color;
    mark.size = baseSize;
    mark.opacity = stroke.opacity;
    mark.linePattern = LinePattern::Solid;
    mark.closed = false;
    mark.label = markKind;
    mark.vertices = {edge.a + (edge.b - edge.a) * t0, edge.a + (edge.b - edge.a) * t1};
    mark.attachments = {
        {0, edge.shapeId, edge.outlineEdgeIndex, t0},
        {1, edge.shapeId, edge.outlineEdgeIndex, t1},
    };
    mark.attachmentsKnown = true;
    mark.bounds = computeShapeBounds(mark);
    return mark;
}

bool stampOrCycleCongruenceMark(QVector<ShapeItem>& shapes,
                                const QPointF& worldPos,
                                const QString& markSpec,
                                const QColor& color,
                                double strokeSize,
                                double zoom,
                                QString* outCreatedId) {
    zoom = std::max(0.1, zoom);
    const auto edges = collectGeometryEdges(shapes, 20.0 / zoom);
    if (edges.isEmpty())
        return false;

    int bestIdx = -1;
    double bestDist = 28.0 / zoom;
    for (int i = 0; i < edges.size(); ++i) {
        const double d = segmentDistance(worldPos, edges[i].a, edges[i].b);
        if (d < bestDist) {
            bestDist = d;
            bestIdx = i;
        }
    }
    if (bestIdx < 0)
        return false;

    const auto& edge = edges[bestIdx];
    const QString cleanSpec = markSpec.trimmed().toLower();
    const double baseSize = findParentStrokeSize(shapes, edge.shapeId, strokeSize);

    const QPointF edgeVec = edge.b - edge.a;
    const double edgeLenSq = dot(edgeVec, edgeVec);
    const double tTouch = edgeLenSq > 1e-6
        ? std::clamp(dot(worldPos - edge.a, edgeVec) / edgeLenSq, 0.0, 1.0)
        : 0.5;
    QVector<double> cuts = {0.0, 1.0};
    const double touchTol = std::max(6.0 / zoom, 4.0);
    for (int i = 0; i < edges.size(); ++i) {
        if (i == bestIdx || edges[i].shapeId == edge.shapeId)
            continue;
        const auto& other = edges[i];
        QPointF hit;
        double tEdge = 0.0, uOther = 0.0;
        if (segmentIntersectionInterior(edge.a, edge.b, other.a, other.b, &hit, &tEdge, &uOther)
            && tEdge > 0.04 && tEdge < 0.96 && uOther >= -0.06 && uOther <= 1.06) {
            cuts.append(tEdge);
        }
        for (const QPointF& ep : {other.a, other.b}) {
            if (edgeLenSq <= 1e-6)
                continue;
            const double tEp = dot(ep - edge.a, edgeVec) / edgeLenSq;
            if (tEp > 0.04 && tEp < 0.96 && length(ep - (edge.a + edgeVec * tEp)) <= touchTol)
                cuts.append(tEp);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    QVector<double> uniqueCuts;
    for (double c : cuts) {
        if (uniqueCuts.isEmpty() || c - uniqueCuts.back() > 0.02)
            uniqueCuts.append(c);
    }
    double tLo = 0.0, tHi = 1.0;
    for (qsizetype i = 0; i + 1 < uniqueCuts.size(); ++i) {
        if (tTouch >= uniqueCuts[i] - 1e-4 && tTouch <= uniqueCuts[i + 1] + 1e-4) {
            tLo = uniqueCuts[i];
            tHi = uniqueCuts[i + 1];
            break;
        }
    }

    const double subSpanT = std::max(0.06, tHi - tLo);
    const double subMid = 0.5 * (tLo + tHi);
    for (qsizetype i = 0; i < shapes.size(); ++i) {
        auto& s = shapes[i];
        if (!isCongruenceMarkShape(s) || s.attachments.size() < 2)
            continue;
        if (s.attachments[0].targetId == edge.shapeId && s.attachments[0].edge == edge.outlineEdgeIndex) {
            const double loEx = std::min(s.attachments[0].position, s.attachments[1].position);
            const double hiEx = std::max(s.attachments[0].position, s.attachments[1].position);
            const double tMidExisting = 0.5 * (loEx + hiEx);
            const bool insideSubSegment = (loEx >= tLo - 0.015) && (hiEx <= tHi + 0.015);
            const bool centeredInSub = std::abs(tMidExisting - subMid) <= subSpanT * 0.24;
            if (!insideSubSegment || !centeredInSub)
                continue;
            if (cleanSpec.startsWith(QLatin1String("mark:tick")) && s.label.startsWith(QLatin1String("mark:tick"))) {
                s.label = (s.label == QLatin1String("mark:tick1")) ? QStringLiteral("mark:tick2")
                        : (s.label == QLatin1String("mark:tick2")) ? QStringLiteral("mark:tick3")
                                                                   : QStringLiteral("mark:tick1");
            } else if (cleanSpec.startsWith(QLatin1String("mark:parallel")) && s.label.startsWith(QLatin1String("mark:parallel"))) {
                s.label = (s.label == QLatin1String("mark:parallel1")) ? QStringLiteral("mark:parallel2")
                                                                       : QStringLiteral("mark:parallel1");
            } else {
                s.label = cleanSpec;
            }
            s.color = color;
            s.size = baseSize;
            s.bounds = computeShapeBounds(s);
            if (outCreatedId)
                *outCreatedId = s.id;
            return true;
        }
    }

    const double subLen = subSpanT * edge.len;
    const double span = std::clamp(subLen * 0.28, 16.0, 40.0);
    const double halfT = std::clamp((span * 0.5) / edge.len, 0.03, subSpanT * 0.44);
    const double t0 = std::clamp(subMid - halfT, tLo + 0.015, subMid - 0.01);
    const double t1 = std::clamp(subMid + halfT, subMid + 0.01, tHi - 0.015);

    ShapeItem mark;
    mark.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mark.kind = ShapeKind::Line;
    mark.color = color;
    mark.size = baseSize;
    mark.opacity = 1.0;
    mark.linePattern = LinePattern::Solid;
    mark.closed = false;
    mark.label = cleanSpec;
    mark.vertices = {edge.a + edgeVec * t0, edge.a + edgeVec * t1};
    mark.attachments = {
        {0, edge.shapeId, edge.outlineEdgeIndex, t0},
        {1, edge.shapeId, edge.outlineEdgeIndex, t1},
    };
    mark.attachmentsKnown = true;
    mark.bounds = computeShapeBounds(mark);
    if (outCreatedId)
        *outCreatedId = mark.id;
    shapes.append(std::move(mark));
    return true;
}

bool constrainCongruenceMarkToEdge(ShapeItem& mark,
                                   const QVector<ShapeItem>& shapes,
                                   int handleIndex,
                                   const QPointF& targetWorld,
                                   const QPointF& dragDelta) {
    if (!isCongruenceMarkShape(mark) || mark.vertices.size() < 2 || mark.attachments.size() < 2)
        return false;
    const QString targetId = mark.attachments[0].targetId;
    const int targetEdgeIdx = mark.attachments[0].edge;
    const auto edges = collectGeometryEdges(shapes, 8.0);
    const CandidateEdge* parentEdge = nullptr;
    for (const auto& e : edges) {
        if (e.shapeId == targetId && e.outlineEdgeIndex == targetEdgeIdx) {
            parentEdge = &e;
            break;
        }
    }
    if (!parentEdge || parentEdge->len < 1e-3)
        return false;

    const QPointF d = parentEdge->b - parentEdge->a;
    const double dd = dot(d, d);
    double t0 = std::clamp(dot(mark.vertices[0] - parentEdge->a, d) / dd, 0.02, 0.98);
    double t1 = std::clamp(dot(mark.vertices[1] - parentEdge->a, d) / dd, 0.02, 0.98);
    const double minSpanT = std::min(0.4, 12.0 / parentEdge->len);

    if (handleIndex == 0 || handleIndex == 1) {
        double tNew = std::clamp(dot(targetWorld - parentEdge->a, d) / dd, 0.02, 0.98);
        const double otherT = (handleIndex == 0) ? t1 : t0;
        if (std::abs(tNew - otherT) < minSpanT) {
            const double dir = (tNew >= otherT) ? 1.0 : -1.0;
            tNew = std::clamp(otherT + dir * minSpanT, 0.02, 0.98);
        }
        if (handleIndex == 0)
            t0 = tNew;
        else
            t1 = tNew;
    } else {
        const double dt = dot(dragDelta, d) / dd;
        const double lo = std::min(t0, t1);
        const double hi = std::max(t0, t1);
        const double clampedDt = std::clamp(dt, 0.02 - lo, 0.98 - hi);
        t0 += clampedDt;
        t1 += clampedDt;
    }

    mark.vertices[0] = parentEdge->a + d * t0;
    mark.vertices[1] = parentEdge->a + d * t1;
    mark.attachments[0].position = t0;
    mark.attachments[1].position = t1;
    mark.bounds = computeShapeBounds(mark);
    return true;
}

bool isPointNearGeometryEdge(const QPointF& worldPos,
                             const QVector<ShapeItem>& shapes,
                             double zoom,
                             double maxDistPx,
                             QPointF* outEdgePoint) {
    zoom = std::max(0.1, zoom);
    const auto edges = collectGeometryEdges(shapes, 24.0 / zoom);
    double bestDist = maxDistPx / zoom;
    QPointF bestPt;
    bool found = false;
    for (const auto& e : edges) {
        const QPointF d = e.b - e.a;
        const double dd = dot(d, d);
        if (dd < 1e-6)
            continue;
        const double t = std::clamp(dot(worldPos - e.a, d) / dd, 0.0, 1.0);
        if (t < 0.08 || t > 0.92)
            continue;
        const QPointF proj = e.a + d * t;
        const double dist = length(worldPos - proj);
        if (dist < bestDist) {
            bestDist = dist;
            bestPt = proj;
            found = true;
        }
    }
    if (found && outEdgePoint)
        *outEdgePoint = bestPt;
    return found;
}

std::optional<QVector<ShapeItem>> eraseShapePartially(const ShapeItem& shape,
                                                      const QVector<ShapeItem>& allShapes,
                                                      const QPointF& center,
                                                      double radius,
                                                      bool trimToIntersections) {
    if (!isPartiallyErasableShape(shape))
        return std::nullopt;
    const double r = std::max(0.0, radius) + std::max(0.0, shape.size) * 0.55;
    if (!computeShapeBounds(shape).adjusted(-r, -r, r, r).contains(center))
        return std::nullopt;

    const auto chains = extractShapeChains(shape);
    if (chains.isEmpty())
        return std::nullopt;

    if (trimToIntersections) {
        int hitChain = -1, hitSeg = -1;
        double bestDistSq = r * r;
        double hitT = 0.5;
        for (int ci = 0; ci < chains.size(); ++ci) {
            const auto& ch = chains[ci];
            const int segCount = ch.closed ? int(ch.pts.size()) : int(ch.pts.size()) - 1;
            for (int si = 0; si < segCount; ++si) {
                const QPointF a = ch.pts[si];
                const QPointF b = ch.pts[(si + 1) % ch.pts.size()];
                double t = 0.0;
                const double d2 = segPointDistSq(center, a, b, &t);
                if (d2 <= bestDistSq) {
                    bestDistSq = d2;
                    hitChain = ci;
                    hitSeg = si;
                    hitT = t;
                }
            }
        }
        if (hitChain < 0)
            return std::nullopt;

        const auto& targetChain = chains[hitChain];
        const QPointF segA = targetChain.pts[hitSeg];
        const QPointF segB = targetChain.pts[(hitSeg + 1) % targetChain.pts.size()];

        QVector<double> cuts = {0.0, 1.0};
        for (const auto& other : allShapes) {
            if (other.kind == ShapeKind::ShadedRegion || !isPartiallyErasableShape(other))
                continue;
            const auto otherChains = extractShapeChains(other);
            for (int oci = 0; oci < otherChains.size(); ++oci) {
                const auto& och = otherChains[oci];
                const int oSegs = och.closed ? int(och.pts.size()) : int(och.pts.size()) - 1;
                for (int osi = 0; osi < oSegs; ++osi) {
                    if (other.id == shape.id && oci == hitChain && osi == hitSeg)
                        continue;
                    const QPointF c = och.pts[osi];
                    const QPointF d = och.pts[(osi + 1) % och.pts.size()];
                    if (const auto t = segmentIntersectionT(segA, segB, c, d))
                        cuts.append(*t);
                }
            }
        }
        std::sort(cuts.begin(), cuts.end());
        double tLo = 0.0, tHi = 1.0;
        for (int i = 0; i + 1 < cuts.size(); ++i) {
            if (hitT >= cuts[i] - 1e-5 && hitT <= cuts[i + 1] + 1e-5) {
                tLo = cuts[i];
                tHi = cuts[i + 1];
                break;
            }
        }

        QVector<ShapeItem> out;
        for (int ci = 0; ci < chains.size(); ++ci) {
            const auto& ch = chains[ci];
            const int segCount = ch.closed ? int(ch.pts.size()) : int(ch.pts.size()) - 1;
            QVector<QVector<QPointF>> polylines;
            QVector<QPointF> cur;
            auto flushCur = [&] {
                if (cur.size() >= 2)
                    polylines.append(cur);
                cur.clear();
            };
            auto appendPt = [&](const QPointF& pt) {
                if (cur.isEmpty() || length(pt - cur.back()) > 1e-4)
                    cur.append(pt);
            };
            for (int si = 0; si < segCount; ++si) {
                const QPointF a = ch.pts[si];
                const QPointF b = ch.pts[(si + 1) % ch.pts.size()];
                if (ci == hitChain && si == hitSeg) {
                    if (tLo > 1e-3) {
                        appendPt(a);
                        appendPt(a + (b - a) * tLo);
                    }
                    flushCur();
                    if (tHi < 1.0 - 1e-3) {
                        appendPt(a + (b - a) * tHi);
                        appendPt(b);
                    }
                } else {
                    appendPt(a);
                    appendPt(b);
                }
            }
            flushCur();
            if (ch.closed && polylines.size() >= 2 && length(polylines.front().front() - polylines.back().back()) < 1e-3) {
                QVector<QPointF> merged = polylines.back();
                for (qsizetype k = 1; k < polylines.front().size(); ++k)
                    merged.append(polylines.front()[k]);
                polylines.front() = merged;
                polylines.removeLast();
            }
            for (const auto& pl : polylines)
                out.append(makePieceFromPoints(shape, pl));
        }
        return out;
    }

    QVector<ShapeItem> out;
    bool touched = false;
    for (const auto& ch : chains) {
        const int segCount = ch.closed ? int(ch.pts.size()) : int(ch.pts.size()) - 1;
        QVector<QVector<QPointF>> polylines;
        QVector<QPointF> cur;
        auto flushCur = [&] {
            if (cur.size() >= 2)
                polylines.append(cur);
            cur.clear();
        };
        auto appendPt = [&](const QPointF& pt) {
            if (cur.isEmpty() || length(pt - cur.back()) > 1e-4)
                cur.append(pt);
        };
        for (int si = 0; si < segCount; ++si) {
            const QPointF a = ch.pts[si];
            const QPointF b = ch.pts[(si + 1) % ch.pts.size()];
            const QPointF d = b - a;
            const QPointF f = a - center;
            const double aa = dot(d, d);
            const double bb = 2.0 * dot(f, d);
            const double cc = dot(f, f) - r * r;
            if (aa < 1e-12) {
                if (cc <= 0.0) { touched = true; flushCur(); }
                else { appendPt(a); appendPt(b); }
                continue;
            }
            const double disc = bb * bb - 4.0 * aa * cc;
            if (disc < 0.0) {
                appendPt(a);
                appendPt(b);
                continue;
            }
            const double sq = std::sqrt(disc);
            const double lo = std::max(0.0, (-bb - sq) / (2.0 * aa));
            const double hi = std::min(1.0, (-bb + sq) / (2.0 * aa));
            if (hi - lo < 1e-9 || hi < 0.0 || lo > 1.0) {
                appendPt(a);
                appendPt(b);
                continue;
            }
            touched = true;
            if (lo > 1e-4) {
                appendPt(a);
                appendPt(a + d * lo);
            }
            flushCur();
            if (hi < 1.0 - 1e-4) {
                appendPt(a + d * hi);
                appendPt(b);
            }
        }
        flushCur();
        if (ch.closed && polylines.size() >= 2 && length(polylines.front().front() - polylines.back().back()) < 1e-3) {
            QVector<QPointF> merged = polylines.back();
            for (qsizetype k = 1; k < polylines.front().size(); ++k)
                merged.append(polylines.front()[k]);
            polylines.front() = merged;
            polylines.removeLast();
        }
        for (const auto& pl : polylines)
            out.append(makePieceFromPoints(shape, pl));
    }
    if (!touched)
        return std::nullopt;
    return out;
}

} // namespace finily
