#include "GeometryEngine.h"
#include "GeometryInternal.h"

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace finily {
using namespace geometry_detail;

bool isCongruenceMarkShape(const ShapeItem& shape) {
    return isEdgeMark(shape);
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
        if (s.kind == ShapeKind::ShadedRegion || s.kind == ShapeKind::AngleMarker || isEdgeMark(s)
            || isHighlightOverlay(s) || isPhysicsSpring(s) || s.vertices.size() < 2)
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
    piece.id = freshId();
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

std::optional<ShapeItem> trySnapAngleArcAtVertex(const Stroke& stroke,
                                                 const QVector<ShapeItem>& shapes,
                                                 double zoom,
                                                 bool allowAngleArc,
                                                 bool allowRightAngle) {
    if ((!allowAngleArc && !allowRightAngle) || stroke.points.size() < 4 || shapes.isEmpty())
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
    qsizetype peakIdx = pts.size() / 2;
    double maxAbsBulge = -1.0;
    for (qsizetype i = 0; i < pts.size(); ++i) {
        const double signedDist = dot(pts[i] - pStart, chordNorm);
        maxPosBulge = std::max(maxPosBulge, signedDist);
        maxNegBulge = std::max(maxNegBulge, -signedDist);
        if (std::abs(signedDist) > maxAbsBulge) {
            maxAbsBulge = std::abs(signedDist);
            peakIdx = i;
        }
    }
    const double mainBulge = std::max(maxPosBulge, maxNegBulge);
    const double oppBulge = std::min(maxPosBulge, maxNegBulge);
    if (mainBulge < std::max(1.2 / zoom, chord * 0.04) || oppBulge > std::max(3.5 / zoom, mainBulge * 0.38))
        return std::nullopt;

    // Detectar si el trazo tiene forma de esquina cuadrada ("┐", "└", etc.) en vez de arco curvo
    const QPointF pPeak = pts[peakIdx];
    bool looksLikeSquareCorner = false;
    if (peakIdx > 0 && peakIdx + 1 < pts.size() && mainBulge / chord >= 0.30) {
        const double l1 = length(pPeak - pStart);
        const double l2 = length(pEnd - pPeak);
        if (l1 >= chord * 0.28 && l2 >= chord * 0.28) {
            double maxSegErr = 0.0;
            for (qsizetype i = 1; i < peakIdx; ++i)
                maxSegErr = std::max(maxSegErr, segmentDistance(pts[i], pStart, pPeak));
            for (qsizetype i = peakIdx + 1; i + 1 < pts.size(); ++i)
                maxSegErr = std::max(maxSegErr, segmentDistance(pts[i], pPeak, pEnd));
            const QPointF u1 = (pPeak - pStart) / l1;
            const QPointF u2 = (pEnd - pPeak) / l2;
            const double turnDot = dot(u1, u2);
            if (maxSegErr <= std::max(5.5 / zoom, std::min(l1, l2) * 0.32) && turnDot <= 0.65)
                looksLikeSquareCorner = true;
        }
    }

    const auto edges = collectGeometryEdges(shapes, 22.0 / zoom);
    if (edges.size() < 2)
        return std::nullopt;

    const QPointF pMid = pPeak;
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

    const double meanEndDist = 0.5 * (length(pStart - bestV) + length(pEnd - bestV));
    const bool isRightAngle = looksLikeSquareCorner && meanEndDist > 1e-3
        && length(pPeak - bestV) > meanEndDist * 1.10;

    if (isRightAngle && !allowRightAngle)
        return std::nullopt;
    if (!isRightAngle && !allowAngleArc)
        return std::nullopt;

    ShapeItem marker;
    marker.id = freshId();
    marker.color = stroke.color;
    marker.size = stroke.size;
    marker.opacity = stroke.opacity;
    marker.linePattern = LinePattern::Solid;
    marker.closed = false;
    if (isRightAngle) {
        marker.kind = ShapeKind::RightAngle;
        const QPointF d1 = bestArm1 - bestV;
        const QPointF d2 = bestArm2 - bestV;
        const double l1 = std::max(1e-6, length(d1));
        const double l2 = std::max(1e-6, length(d2));
        const double sideDesired = std::clamp(meanEndDist, 10.0, 22.0);
        const double armSpan = sideDesired / 0.35;
        marker.vertices = {bestV + (d1 / l1) * armSpan, bestV, bestV + (d2 / l2) * armSpan};
    } else {
        marker.kind = ShapeKind::AngleMarker;
        marker.label = autoVertexAngleLabel();
        marker.vertices = {bestV, bestArm1, bestArm2};
    }
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

    EdgeMarkKind markKind = EdgeMarkKind::None;
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
        markKind = isZigzag ? EdgeMarkKind::Zigzag : EdgeMarkKind::Spring;
    }

    if (markKind == EdgeMarkKind::None && allowSimpleMarks && bestDist <= 30.0 / zoom) {
        const QPointF endVec = pts.back() - pts.front();
        const double straightDist = length(endVec);
        const double minBox = std::min(b.width(), b.height());
        const double aspect = dim > 1e-4 ? (minBox / dim) : 0.0;

        if ((straightDist <= std::max(12.0 / zoom, dim * 0.60)
             && aspect >= 0.40
             && dim * zoom <= 52.0
             && pathLen >= dim * 1.65)
            || (dim * zoom <= 18.0 && aspect >= 0.45 && pathLen >= dim * 1.2)) {
            markKind = EdgeMarkKind::Dot;
        }

        if (markKind == EdgeMarkKind::None && dim * zoom <= 85.0 && straightDist > 1e-4 && pathLen >= straightDist * 1.12) {
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
                    markKind = EdgeMarkKind::Parallel1;
            }
        }

        if (markKind == EdgeMarkKind::None
            && straightDist > 1e-4
            && pathLen <= straightDist * 1.38
            && dim * zoom <= 75.0) {
            const QPointF dirVec = endVec / straightDist;
            if (std::abs(dot(dirVec, n)) >= 0.52
                && minNormal <= 10.0 / zoom && maxNormal >= -10.0 / zoom) {
                markKind = EdgeMarkKind::Tick1;
            }
        }

        if (markKind == EdgeMarkKind::None && dim * zoom <= 90.0) {
            const bool straightAlong = straightDist > 1e-4
                && pathLen <= straightDist * 1.45
                && std::abs(dot(endVec / straightDist, u)) >= 0.72;
            const bool elongatedBoxAlong = alongSpan > normalSpan * 1.45 && alongSpan >= 6.0 / zoom;
            if (straightAlong || elongatedBoxAlong)
                markKind = EdgeMarkKind::Bar;
        }
    }

    if (markKind == EdgeMarkKind::None)
        return std::nullopt;

    const double baseSize = findParentStrokeSize(shapes, edge.shapeId, stroke.size);
    const double span = std::clamp(edge.len * 0.22, 16.0, 36.0);
    const double halfT = std::clamp((span * 0.5) / edge.len, 0.06, 0.35);
    const double t0 = std::clamp(0.5 - halfT, 0.05, 0.48);
    const double t1 = std::clamp(0.5 + halfT, 0.52, 0.95);

    ShapeItem mark;
    mark.id = freshId();
    mark.kind = ShapeKind::Line;
    mark.color = stroke.color;
    mark.size = baseSize;
    mark.opacity = stroke.opacity;
    mark.linePattern = LinePattern::Solid;
    mark.closed = false;
    mark.label = edgeMarkLabel(markKind);
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
    const EdgeMarkKind requested = parseEdgeMark(markSpec);
    if (requested == EdgeMarkKind::None)
        return false;
    const double baseSize = findParentStrokeSize(shapes, edge.shapeId, strokeSize);

    const QPointF edgeVec = edge.b - edge.a;
    const double edgeLenSq = dot(edgeVec, edgeVec);
    const double tTouch = edgeLenSq > 1e-6
        ? std::clamp(dot(worldPos - edge.a, edgeVec) / edgeLenSq, 0.0, 1.0)
        : 0.5;

    if (requested == EdgeMarkKind::Divider) {
        double tPlace = std::clamp(tTouch, 0.04, 0.96);
        if (std::abs(tPlace - 0.5) < 0.055)
            tPlace = 0.5;
        const QPointF u = edgeVec / std::max(1e-6, edge.len);
        const QPointF pt = edge.a + edgeVec * tPlace;
        for (auto& s : shapes) {
            if (isDividerMark(s)
                && s.attachments.size() >= 2 && s.attachments[0].targetId == edge.shapeId
                && s.attachments[0].edge == edge.outlineEdgeIndex
                && std::abs(s.attachments[0].position - tPlace) < 0.05) {
                s.color = color;
                s.size = baseSize;
                s.vertices = {pt - u * 2.0, pt + u * 2.0};
                s.attachments[0].position = tPlace;
                s.attachments[1].position = tPlace;
                s.bounds = computeShapeBounds(s);
                if (outCreatedId)
                    *outCreatedId = s.id;
                return true;
            }
        }
        ShapeItem div;
        div.id = freshId();
        div.kind = ShapeKind::Line;
        div.color = color;
        div.size = baseSize;
        div.opacity = 1.0;
        div.linePattern = LinePattern::Solid;
        div.closed = false;
        div.label = edgeMarkLabel(EdgeMarkKind::Divider);
        div.vertices = {pt - u * 2.0, pt + u * 2.0};
        div.attachments = {
            {0, edge.shapeId, edge.outlineEdgeIndex, tPlace},
            {1, edge.shapeId, edge.outlineEdgeIndex, tPlace},
        };
        div.attachmentsKnown = true;
        div.bounds = computeShapeBounds(div);
        if (outCreatedId)
            *outCreatedId = div.id;
        shapes.append(std::move(div));
        return true;
    }

    QVector<double> cuts = {0.0, 1.0};
    const double touchTol = std::max(6.0 / zoom, 4.0);
    for (const auto& s : shapes) {
        if (isDividerMark(s)
            && s.attachments.size() >= 1 && s.attachments[0].targetId == edge.shapeId
            && s.attachments[0].edge == edge.outlineEdgeIndex) {
            const double td = s.attachments[0].position;
            if (td > 0.04 && td < 0.96)
                cuts.append(td);
        }
    }
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
            s.label = edgeMarkLabel(cycleEdgeMark(edgeMarkKind(s), requested));
            s.color = color;
            s.size = baseSize;
            s.bounds = computeShapeBounds(s);
            if (outCreatedId)
                *outCreatedId = s.id;
            return true;
        }
    }

    const bool isFullSpanMark = (requested == EdgeMarkKind::Wall || requested == EdgeMarkKind::WallFlip);
    const double subLen = subSpanT * edge.len;
    const double span = std::clamp(subLen * 0.28, 16.0, 40.0);
    const double halfT = std::clamp((span * 0.5) / edge.len, 0.03, subSpanT * 0.44);
    const double t0 = isFullSpanMark ? tLo : std::clamp(subMid - halfT, tLo + 0.015, subMid - 0.01);
    const double t1 = isFullSpanMark ? tHi : std::clamp(subMid + halfT, subMid + 0.01, tHi - 0.015);

    ShapeItem mark;
    mark.id = freshId();
    mark.kind = ShapeKind::Line;
    mark.color = color;
    mark.size = baseSize;
    mark.opacity = 1.0;
    mark.linePattern = LinePattern::Solid;
    mark.closed = false;
    mark.label = edgeMarkLabel(requested);
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
    if (isDividerMark(mark)) {
        const double tCur = std::clamp(dot(((mark.vertices[0] + mark.vertices[1]) * 0.5) - parentEdge->a, d) / dd, 0.03, 0.97);
        double tNew = (handleIndex == 0 || handleIndex == 1)
            ? std::clamp(dot(targetWorld - parentEdge->a, d) / dd, 0.03, 0.97)
            : std::clamp(tCur + dot(dragDelta, d) / dd, 0.03, 0.97);
        if (std::abs(tNew - 0.5) < 0.03)
            tNew = 0.5;
        const QPointF u = d / std::max(1e-6, parentEdge->len);
        const QPointF pt = parentEdge->a + d * tNew;
        mark.vertices = {pt - u * 2.0, pt + u * 2.0};
        mark.attachments[0].position = tNew;
        mark.attachments[1].position = tNew;
        mark.bounds = computeShapeBounds(mark);
        return true;
    }
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
    for (const auto& s : shapes) {
        if (s.kind == ShapeKind::Circle && s.vertices.size() >= 2) {
            const double r = length(s.vertices[1] - s.vertices[0]);
            if (r > 8.0) {
                const QPointF dir = worldPos - s.vertices[0];
                const double distCenter = length(dir);
                const double dist = std::abs(distCenter - r);
                if (dist < bestDist && distCenter > 1e-4) {
                    bestDist = dist;
                    bestPt = s.vertices[0] + dir / distCenter * r;
                    found = true;
                }
            }
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
            if (isDividerMark(other)
                && other.vertices.size() >= 2 && !other.attachments.isEmpty()
                && other.attachments[0].targetId == shape.id) {
                const QPointF divPt = (other.vertices[0] + other.vertices[1]) * 0.5;
                double tDiv = 0.0;
                if (segPointDistSq(divPt, segA, segB, &tDiv) <= 36.0 && tDiv > 0.02 && tDiv < 0.98)
                    cuts.append(tDiv);
                continue;
            }
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

std::optional<ShapeItem> createHighlightedSegment(const QVector<ShapeItem>& shapes,
                                                  const QPointF& worldPos,
                                                  const QColor& color,
                                                  double baseStrokeSize,
                                                  double zoom,
                                                  int style) {
    zoom = std::max(0.1, zoom);
    const double maxDist = 24.0 / zoom;
    const auto edges = collectGeometryEdges(shapes, 12.0);

    int bestEdgeIdx = -1;
    double bestEdgeDist = maxDist;
    double bestEdgeT = 0.5;
    for (int i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        const QPointF d = e.b - e.a;
        const double dd = dot(d, d);
        if (dd < 1e-6)
            continue;
        const double t = std::clamp(dot(worldPos - e.a, d) / dd, 0.0, 1.0);
        const double dist = length(worldPos - (e.a + d * t));
        if (dist < bestEdgeDist) {
            bestEdgeDist = dist;
            bestEdgeIdx = i;
            bestEdgeT = t;
        }
    }

    int bestCircleIdx = -1;
    double bestCircleDist = maxDist;
    for (int i = 0; i < shapes.size(); ++i) {
        const auto& s = shapes[i];
        if (s.kind != ShapeKind::Circle || s.vertices.size() < 2)
            continue;
        const double r = length(s.vertices[1] - s.vertices[0]);
        if (r < 8.0)
            continue;
        const double dist = std::abs(length(worldPos - s.vertices[0]) - r);
        if (dist < bestCircleDist) {
            bestCircleDist = dist;
            bestCircleIdx = i;
        }
    }

    if (bestEdgeIdx < 0 && bestCircleIdx < 0)
        return std::nullopt;

    ShapeItem hl;
    hl.id = freshId();
    hl.color = color;
    hl.closed = false;
    if (style == 1) {
        // Cambio de color sólido limpio
        hl.size = std::max(baseStrokeSize * 1.15, 2.8);
        hl.opacity = 1.0;
        hl.linePattern = LinePattern::Solid;
        hl.label = highlightLabel(HighlightStyle::Solid);
    } else if (style == 2) {
        // Tramo en guiones de color
        hl.size = std::max(baseStrokeSize * 1.15, 2.8);
        hl.opacity = 1.0;
        hl.linePattern = LinePattern::Dashed;
        hl.label = highlightLabel(HighlightStyle::Dashed);
    } else {
        // Resaltado luminoso (halo + núcleo vivo)
        hl.size = std::max(7.0, baseStrokeSize * 2.4);
        hl.opacity = 1.0;
        hl.linePattern = LinePattern::Solid;
        hl.label = highlightLabel(HighlightStyle::Glow);
    }

    if (bestCircleIdx >= 0 && (bestEdgeIdx < 0 || bestCircleDist < bestEdgeDist)) {
        const auto& circle = shapes[bestCircleIdx];
        const QPointF center = circle.vertices[0];
        const double radius = length(circle.vertices[1] - center);
        constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
        auto qtAngle = [&](QPointF pt) {
            double a = std::atan2(-(pt.y() - center.y()), pt.x() - center.x());
            while (a < 0.0)
                a += kTwoPi;
            while (a >= kTwoPi)
                a -= kTwoPi;
            return a;
        };

        const double alphaTouch = qtAngle(worldPos);
        QVector<double> angles;
        for (const auto& e : edges) {
            if (e.shapeId == circle.id)
                continue;
            const QPointF f = e.a - center;
            const QPointF d = e.b - e.a;
            const double aa = dot(d, d);
            if (aa < 1e-9)
                continue;
            const double bb = 2.0 * dot(f, d);
            const double cc = dot(f, f) - radius * radius;
            const double disc = bb * bb - 4.0 * aa * cc;
            if (disc < -1e-4)
                continue;
            const double sq = std::sqrt(std::max(0.0, disc));
            for (const double rootT : {(-bb - sq) / (2.0 * aa), (-bb + sq) / (2.0 * aa)}) {
                if (rootT >= -0.04 && rootT <= 1.04) {
                    const QPointF hit = e.a + d * std::clamp(rootT, 0.0, 1.0);
                    if (std::abs(length(hit - center) - radius) <= std::max(6.0, radius * 0.06))
                        angles.append(qtAngle(hit));
                }
            }
        }
        std::sort(angles.begin(), angles.end());
        QVector<double> uniqueAngles;
        for (double a : angles) {
            if (uniqueAngles.isEmpty() || std::abs(a - uniqueAngles.back()) > 0.03)
                uniqueAngles.append(a);
        }
        if (uniqueAngles.size() >= 2 && (kTwoPi - uniqueAngles.back() + uniqueAngles.front()) < 0.03)
            uniqueAngles.removeLast();

        double aLo = alphaTouch - 0.52;
        double aHi = alphaTouch + 0.52;
        if (uniqueAngles.size() >= 2) {
            bool foundSpan = false;
            for (qsizetype i = 0; i + 1 < uniqueAngles.size(); ++i) {
                if (alphaTouch >= uniqueAngles[i] && alphaTouch <= uniqueAngles[i + 1]) {
                    aLo = uniqueAngles[i];
                    aHi = uniqueAngles[i + 1];
                    foundSpan = true;
                    break;
                }
            }
            if (!foundSpan) {
                aLo = uniqueAngles.back();
                aHi = uniqueAngles.front();
            }
        }
        hl.kind = ShapeKind::Arc;
        hl.vertices = {
            center,
            center + QPointF(std::cos(aLo), -std::sin(aLo)) * radius,
            center + QPointF(std::cos(aHi), -std::sin(aHi)) * radius,
        };
        hl.bounds = computeShapeBounds(hl);
        return hl;
    }

    const auto& edge = edges[bestEdgeIdx];
    const QPointF edgeVec = edge.b - edge.a;
    QVector<double> cuts = {0.0, 1.0};
    for (const auto& s : shapes) {
        if (isDividerMark(s)
            && !s.attachments.isEmpty() && s.attachments[0].targetId == edge.shapeId
            && s.attachments[0].edge == edge.outlineEdgeIndex) {
            const double td = s.attachments[0].position;
            if (td > 0.02 && td < 0.98)
                cuts.append(td);
        }
    }
    const double touchTol = std::max(6.0 / zoom, 4.0);
    const double edgeLenSq = dot(edgeVec, edgeVec);
    for (int i = 0; i < edges.size(); ++i) {
        if (i == bestEdgeIdx || edges[i].shapeId == edge.shapeId)
            continue;
        const auto& other = edges[i];
        QPointF hit;
        double tEdge = 0.0, uOther = 0.0;
        if (segmentIntersectionInterior(edge.a, edge.b, other.a, other.b, &hit, &tEdge, &uOther)
            && tEdge > 0.02 && tEdge < 0.98 && uOther >= -0.06 && uOther <= 1.06) {
            cuts.append(tEdge);
        }
        for (const QPointF& ep : {other.a, other.b}) {
            if (edgeLenSq <= 1e-6)
                continue;
            const double tEp = dot(ep - edge.a, edgeVec) / edgeLenSq;
            if (tEp > 0.02 && tEp < 0.98 && length(ep - (edge.a + edgeVec * tEp)) <= touchTol)
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
        if (bestEdgeT >= uniqueCuts[i] - 1e-4 && bestEdgeT <= uniqueCuts[i + 1] + 1e-4) {
            tLo = uniqueCuts[i];
            tHi = uniqueCuts[i + 1];
            break;
        }
    }
    hl.kind = ShapeKind::Line;
    hl.vertices = {edge.a + edgeVec * tLo, edge.a + edgeVec * tHi};
    hl.bounds = computeShapeBounds(hl);
    return hl;
}

bool trySnapFreehandDividerOnEdge(const Stroke& stroke,
                                  QVector<ShapeItem>& shapes,
                                  double zoom,
                                  QString* outCreatedId) {
    if (shapes.isEmpty() || stroke.points.size() < 3)
        return false;
    zoom = std::max(0.1, zoom);
    double minX = stroke.points[0].x, maxX = minX;
    double minY = stroke.points[0].y, maxY = minY;
    double pathLen = 0.0;
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        minX = std::min(minX, stroke.points[i].x);
        maxX = std::max(maxX, stroke.points[i].x);
        minY = std::min(minY, stroke.points[i].y);
        maxY = std::max(maxY, stroke.points[i].y);
        if (i > 0)
            pathLen += length(QPointF(stroke.points[i].x - stroke.points[i - 1].x,
                                      stroke.points[i].y - stroke.points[i - 1].y));
    }
    const double bw = maxX - minX;
    const double bh = maxY - minY;
    const double dim = std::max(bw, bh);
    if (dim > 18.0 / zoom)
        return false;

    const bool isTinyDot = (dim <= 6.0 / zoom);
    const double minSide = std::max(0.5 / zoom, std::min(bw, bh));
    const bool isSmallCircleOrFill = (bw >= 2.5 / zoom && bh >= 2.5 / zoom &&
                                      dim / minSide <= 2.2 && pathLen >= 1.6 * dim);
    if (!isTinyDot && !isSmallCircleOrFill)
        return false;

    const QPointF center(0.5 * (minX + maxX), 0.5 * (minY + maxY));
    const auto edges = collectGeometryEdges(shapes, 16.0 / zoom);
    double bestDist = 9.5 / zoom;
    bool onInteriorEdge = false;
    for (const auto& edge : edges) {
        const QPointF v = edge.b - edge.a;
        const double l2 = dot(v, v);
        if (l2 < 1e-6)
            continue;
        const double t = dot(center - edge.a, v) / l2;
        if (t <= 0.06 || t >= 0.94)
            continue;
        const double d = length(center - (edge.a + v * t));
        if (d < bestDist) {
            bestDist = d;
            onInteriorEdge = true;
        }
    }
    if (!onInteriorEdge)
        return false;

    return stampOrCycleCongruenceMark(shapes, center, edgeMarkLabel(EdgeMarkKind::Divider),
                                      stroke.color, stroke.size, zoom, outCreatedId);
}

std::optional<ShapeItem> tryRecognizeFreehandArrow(const Stroke& stroke, double zoom) {
    if (stroke.points.size() < 10)
        return std::nullopt;
    zoom = std::max(0.1, zoom);

    const QPointF pStart(stroke.points.front().x, stroke.points.front().y);
    QVector<double> cumLen(stroke.points.size(), 0.0);
    for (qsizetype i = 1; i < stroke.points.size(); ++i) {
        cumLen[i] = cumLen[i - 1] + length(QPointF(stroke.points[i].x - stroke.points[i - 1].x,
                                                   stroke.points[i].y - stroke.points[i - 1].y));
    }
    const double totalLen = cumLen.back();
    if (totalLen < 55.0 / zoom)
        return std::nullopt;

    // Encontrar la punta (punto más lejano del inicio)
    qsizetype tipIdx = 0;
    double shaftLen = 0.0;
    for (qsizetype i = 1; i < stroke.points.size(); ++i) {
        const double d = length(QPointF(stroke.points[i].x, stroke.points[i].y) - pStart);
        if (d > shaftLen) {
            shaftLen = d;
            tipIdx = i;
        }
    }
    if (shaftLen < 48.0 / zoom || tipIdx < 4 || tipIdx >= stroke.points.size() - 1)
        return std::nullopt;

    const double tipFrac = cumLen[tipIdx] / totalLen;
    if (tipFrac < 0.50 || tipFrac > 0.91)
        return std::nullopt;

    const QPointF tip(stroke.points[tipIdx].x, stroke.points[tipIdx].y);
    const QPointF uShaft = (tip - pStart) / shaftLen;

    // Verificar que el cuerpo [0..tipIdx] sea una recta limpia
    const double maxShaftDev = std::max(6.0 / zoom, 0.085 * shaftLen);
    for (qsizetype i = 1; i < tipIdx; ++i) {
        const QPointF pt(stroke.points[i].x, stroke.points[i].y);
        if (segmentDistance(pt, pStart, tip) > maxShaftDev)
            return std::nullopt;
    }

    // Verificar la punta de flecha [tipIdx..end]: debe retroceder en ángulo agudo (16°..65°)
    double maxBarbDist = 0.0;
    QPointF farthestBarb = tip;
    for (qsizetype i = tipIdx + 1; i < stroke.points.size(); ++i) {
        const QPointF pt(stroke.points[i].x, stroke.points[i].y);
        const double d = length(pt - tip);
        if (d > maxBarbDist) {
            maxBarbDist = d;
            farthestBarb = pt;
        }
    }
    const double maxAllowedBarb = std::min(34.0 / zoom, 0.38 * shaftLen);
    if (maxBarbDist < 7.0 / zoom || maxBarbDist > maxAllowedBarb)
        return std::nullopt;

    const QPointF barbDir = (farthestBarb - tip) / maxBarbDist;
    const double backDot = dot(barbDir, -uShaft);
    // cos(65°) ≈ 0.42, cos(16°) ≈ 0.96 -> descarta L/7 (90°, dot≈0) y trazos ida-y-vuelta (dot≈1)
    if (backDot < 0.42 || backDot > 0.96)
        return std::nullopt;

    ShapeItem arrow;
    arrow.id = freshId();
    arrow.kind = ShapeKind::Line;
    arrow.color = stroke.color;
    arrow.size = stroke.size;
    arrow.opacity = stroke.opacity;
    arrow.linePattern = stroke.linePattern;
    arrow.lineEnding = LineEnding::Arrow;
    arrow.vertices = {pStart, tip};
    arrow.bounds = computeShapeBounds(arrow);
    return arrow;
}

bool tryAttachArrowheadToLine(const Stroke& stroke,
                              QVector<ShapeItem>& shapes,
                              double zoom,
                              QString* outUpdatedId) {
    if (shapes.isEmpty() || stroke.points.size() < 5)
        return false;
    zoom = std::max(0.1, zoom);

    double minX = stroke.points[0].x, maxX = minX;
    double minY = stroke.points[0].y, maxY = minY;
    for (const auto& sp : stroke.points) {
        minX = std::min(minX, sp.x);
        maxX = std::max(maxX, sp.x);
        minY = std::min(minY, sp.y);
        maxY = std::max(maxY, sp.y);
    }
    const double dim = std::max(maxX - minX, maxY - minY);
    if (dim < 7.0 / zoom || dim > 34.0 / zoom)
        return false;

    const QPointF pFirst(stroke.points.front().x, stroke.points.front().y);
    const QPointF pLast(stroke.points.back().x, stroke.points.back().y);
    qsizetype cornerIdx = -1;
    double maxDev = 0.0;
    for (qsizetype i = 1; i + 1 < stroke.points.size(); ++i) {
        const QPointF pt(stroke.points[i].x, stroke.points[i].y);
        const double d = segmentDistance(pt, pFirst, pLast);
        if (d > maxDev) {
            maxDev = d;
            cornerIdx = i;
        }
    }
    if (cornerIdx < 1 || maxDev < 3.0 / zoom)
        return false;

    const QPointF corner(stroke.points[cornerIdx].x, stroke.points[cornerIdx].y);
    const QPointF leg1 = pFirst - corner;
    const QPointF leg2 = pLast - corner;
    const double l1 = length(leg1);
    const double l2 = length(leg2);
    if (l1 < 5.0 / zoom || l2 < 5.0 / zoom)
        return false;
    if (std::max(l1, l2) / std::min(l1, l2) > 2.3)
        return false;

    const QPointF u1 = leg1 / l1;
    const QPointF u2 = leg2 / l2;
    const double cosV = std::clamp(dot(u1, u2), -1.0, 1.0);
    // Ángulo de la V entre 25° (cos≈0.91) y 120° (cos=-0.50)
    if (cosV > 0.91 || cosV < -0.50)
        return false;

    const QPointF bisRaw = u1 + u2;
    const double bisLen = length(bisRaw);
    if (bisLen < 1e-4)
        return false;
    const QPointF bis = bisRaw / bisLen;

    const double maxSnapDist = 16.0 / zoom;
    for (qsizetype i = shapes.size() - 1; i >= 0; --i) {
        auto& s = shapes[i];
        if (s.kind != ShapeKind::Line || s.vertices.size() < 2 || isCongruenceMarkShape(s)
            || isHighlightOverlay(s))
            continue;
        const double lineLen = length(s.vertices[1] - s.vertices[0]);
        if (lineLen < 28.0 / zoom)
            continue;

        const double d0 = length(corner - s.vertices[0]);
        const double d1 = length(corner - s.vertices[1]);
        int endHit = -1;
        if (d1 <= maxSnapDist && d1 <= d0)
            endHit = 1;
        else if (d0 <= maxSnapDist)
            endHit = 0;
        if (endHit < 0)
            continue;

        const QPointF shaftBack = (endHit == 1) ? ((s.vertices[0] - s.vertices[1]) / lineLen)
                                                : ((s.vertices[1] - s.vertices[0]) / lineLen);
        if (dot(bis, shaftBack) < 0.78)
            continue;

        if (s.lineEnding == LineEnding::None) {
            if (endHit == 0)
                std::swap(s.vertices[0], s.vertices[1]);
            s.lineEnding = LineEnding::Arrow;
        } else if (s.lineEnding == LineEnding::Arrow) {
            if (endHit == 0)
                s.lineEnding = LineEnding::DoubleArrow;
            else
                return true; // Ya tiene flecha en ese extremo
        } else {
            return true;
        }
        s.bounds = computeShapeBounds(s);
        if (outUpdatedId)
            *outUpdatedId = s.id;
        return true;
    }
    return false;
}

namespace {

int classifyTriangleVariant(const QVector<QPointF>& v) {
    if (v.size() < 3)
        return 0;
    const double lab = length(v[1] - v[0]);
    const double lbc = length(v[2] - v[1]);
    const double lca = length(v[0] - v[2]);
    const double maxL = std::max({lab, lbc, lca});
    if (maxL <= 1e-4)
        return 0;
    if (std::max({std::abs(lab - lbc), std::abs(lbc - lca), std::abs(lca - lab)}) <= maxL * 0.025)
        return 3; // Equilátero
    for (int i = 0; i < 3; ++i) {
        const QPointF u = v[(i + 1) % 3] - v[i];
        const QPointF w = v[(i + 2) % 3] - v[i];
        const double lu = length(u);
        const double lw = length(w);
        if (lu > 1e-4 && lw > 1e-4 && std::abs(dot(u, w) / (lu * lw)) <= 0.025)
            return 2; // Rectángulo
    }
    if ((std::max(lab, lbc) > 1e-4 && std::abs(lab - lbc) <= std::max(lab, lbc) * 0.025)
        || (std::max(lbc, lca) > 1e-4 && std::abs(lbc - lca) <= std::max(lbc, lca) * 0.025)
        || (std::max(lca, lab) > 1e-4 && std::abs(lca - lab) <= std::max(lca, lab) * 0.025))
        return 1; // Isósceles
    return 0; // Libre
}

int classifyQuadPolylineVariant(const QVector<QPointF>& v) {
    if (v.size() < 4)
        return 6;
    QPointF edges[4];
    double len[4];
    double maxLen = 0.0;
    for (int i = 0; i < 4; ++i) {
        edges[i] = v[(i + 1) % 4] - v[i];
        len[i] = length(edges[i]);
        maxLen = std::max(maxLen, len[i]);
    }
    if (maxLen <= 1e-4)
        return 6;

    int rightCount = 0;
    bool axisAligned = true;
    for (int i = 0; i < 4; ++i) {
        const QPointF a = v[(i + 3) % 4] - v[i];
        const QPointF b = v[(i + 1) % 4] - v[i];
        const double la = length(a);
        const double lb = length(b);
        if (la > 1e-4 && lb > 1e-4 && std::abs(dot(a, b) / (la * lb)) <= 0.035)
            ++rightCount;
        if (len[i] <= 1e-4 || std::min(std::abs(edges[i].x()), std::abs(edges[i].y())) > len[i] * 0.035)
            axisAligned = false;
    }

    const bool allEqual = std::max({std::abs(len[0] - len[1]), std::abs(len[1] - len[2]),
                                    std::abs(len[2] - len[3]), std::abs(len[3] - len[0])})
        <= maxLen * 0.025;
    if (axisAligned && rightCount == 4)
        return allEqual ? 1 : 0;
    if (allEqual)
        return 2; // Rombo

    auto areParallel = [](QPointF u, double lu, QPointF w, double lw) {
        return lu > 1e-4 && lw > 1e-4 && std::abs(u.x() * w.y() - u.y() * w.x()) / (lu * lw) <= 0.035;
    };
    const bool p02 = areParallel(edges[0], len[0], edges[2], len[2]);
    const bool p13 = areParallel(edges[1], len[1], edges[3], len[3]);
    if (p02 && p13)
        return (rightCount == 4) ? 0 : 3; // Paralelogramo
    if (p02 || p13) {
        if (rightCount >= 2)
            return 5; // Trapecio rectángulo
        const double legA = p02 ? len[1] : len[0];
        const double legB = p02 ? len[3] : len[2];
        if (std::max(legA, legB) > 1e-4 && std::abs(legA - legB) <= std::max(legA, legB) * 0.025)
            return 4; // Trapecio isósceles
    }
    return 6; // Trapezoide
}

double regularPolygonRadiusIfUniform(const ShapeItem& shape) {
    if (shape.kind != ShapeKind::Polyline || !shape.closed
        || shape.vertices.size() < 5 || shape.vertices.size() > 12) {
        return -1.0;
    }
    const int n = int(shape.vertices.size());
    QPointF centroid;
    for (const QPointF& pt : shape.vertices)
        centroid += pt;
    centroid /= double(n);
    double meanR = 0.0;
    for (const QPointF& pt : shape.vertices)
        meanR += length(pt - centroid);
    meanR /= double(n);
    if (meanR <= 1e-4)
        return -1.0;
    for (const QPointF& pt : shape.vertices) {
        if (std::abs(length(pt - centroid) - meanR) > meanR * 0.03)
            return -1.0;
    }
    return meanR;
}

} // namespace

ShapeFamily classifyShapeFamily(const ShapeItem& shape, int* outVariant) {
    auto finish = [&](ShapeFamily family, int variant) {
        if (outVariant)
            *outVariant = variant;
        return family;
    };

    switch (shape.kind) {
    case ShapeKind::Triangle:
        if (shape.vertices.size() >= 3)
            return finish(ShapeFamily::Triangle, classifyTriangleVariant(shape.vertices));
        break;
    case ShapeKind::Rectangle:
        if (shape.vertices.size() >= 2 && !parseTableSpec(shape.label)) {
            const QRectF r = QRectF(shape.vertices[0], shape.vertices[1]).normalized();
            const double maxSide = std::max(r.width(), r.height());
            const bool isSquare = maxSide > 1e-4 && std::abs(r.width() - r.height()) <= maxSide * 0.02;
            return finish(ShapeFamily::Quad, isSquare ? 1 : 0);
        }
        break;
    case ShapeKind::Circle:
        if (shape.vertices.size() >= 2)
            return finish(ShapeFamily::Circle, 0);
        break;
    case ShapeKind::Arc:
        if (shape.vertices.size() >= 3)
            return finish(ShapeFamily::Circle, 1);
        break;
    case ShapeKind::Sector:
        if (shape.vertices.size() >= 3)
            return finish(ShapeFamily::Circle, 2);
        break;
    case ShapeKind::Ellipse:
        if (shape.vertices.size() >= 2)
            return finish(ShapeFamily::Circle, 3);
        break;
    case ShapeKind::Polyline:
        if (shape.closed && shape.vertices.size() == 4)
            return finish(ShapeFamily::Quad, classifyQuadPolylineVariant(shape.vertices));
        if (shape.closed && shape.vertices.size() >= 5 && shape.vertices.size() <= 12)
            return finish(ShapeFamily::Polygon, int(shape.vertices.size()));
        break;
    case ShapeKind::Solid3D:
        if (shape.vertices.size() >= 2)
            return finish(ShapeFamily::Solid3D, std::clamp(shape.solidKind, 0, 9));
        break;
    default:
        break;
    }
    return finish(ShapeFamily::None, 0);
}

bool morphShapeToVariant(ShapeItem& shape, ShapeFamily family, int variant) {
    if (family == ShapeFamily::None || shape.vertices.isEmpty())
        return false;

    QRectF r;
    if ((shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector) && shape.vertices.size() >= 3) {
        const double rad = length(shape.vertices[1] - shape.vertices[0]);
        if (rad > 1e-4)
            r = QRectF(shape.vertices[0] - QPointF(rad, rad), QSizeF(2.0 * rad, 2.0 * rad));
    }
    if (r.isEmpty()) {
        r = shapeGeometryBounds(shape);
        if (r.isEmpty())
            r = vertexBounds(shape.vertices);
    }
    if (!isFinite(r.left()) || !isFinite(r.top()) || !isFinite(r.width()) || !isFinite(r.height()))
        return false;
    if (r.width() < 1e-4 && r.height() < 1e-4)
        return false;
    if (r.width() < 1e-4)
        r.setWidth(1.0);
    if (r.height() < 1e-4)
        r.setHeight(1.0);

    switch (family) {
    case ShapeFamily::Triangle:
        if (variant < 0 || variant > 3)
            return false;
        if (variant == 0) {
            if (shape.kind == ShapeKind::Triangle && shape.vertices.size() >= 3)
                shape.vertices = shape.vertices.mid(0, 3);
            else
                shape.vertices = {{r.center().x(), r.top()}, r.bottomRight(), r.bottomLeft()};
        } else if (variant == 1) {
            shape.vertices = {{r.center().x(), r.top()}, r.bottomRight(), r.bottomLeft()};
        } else if (variant == 2) {
            shape.vertices = {r.topLeft(), r.bottomRight(), r.bottomLeft()};
        } else {
            const double w = r.width();
            const double h = w * std::sqrt(3.0) * 0.5;
            const QPointF c = r.center();
            shape.vertices = {
                {c.x(), c.y() - h * 0.5},
                {r.right(), c.y() + h * 0.5},
                {r.left(), c.y() + h * 0.5},
            };
        }
        shape.kind = ShapeKind::Triangle;
        shape.closed = true;
        break;

    case ShapeFamily::Quad:
        if (variant < 0 || variant > 6)
            return false;
        if (variant == 0) {
            shape.kind = ShapeKind::Rectangle;
            shape.vertices = {r.topLeft(), r.bottomRight()};
        } else if (variant == 1) {
            const double side = (r.width() + r.height()) * 0.5;
            const QPointF half(side * 0.5, side * 0.5);
            const QPointF c = r.center();
            shape.kind = ShapeKind::Rectangle;
            shape.vertices = {c - half, c + half};
        } else if (variant == 2) {
            shape.kind = ShapeKind::Polyline;
            shape.vertices = {
                {r.center().x(), r.top()},
                {r.right(), r.center().y()},
                {r.center().x(), r.bottom()},
                {r.left(), r.center().y()},
            };
        } else if (variant == 3) {
            shape.kind = ShapeKind::Polyline;
            shape.vertices = {
                {r.left() + r.width() * 0.22, r.top()},
                r.topRight(),
                {r.right() - r.width() * 0.22, r.bottom()},
                r.bottomLeft(),
            };
        } else if (variant == 4) {
            shape.kind = ShapeKind::Polyline;
            shape.vertices = {
                {r.left() + r.width() * 0.20, r.top()},
                {r.right() - r.width() * 0.20, r.top()},
                r.bottomRight(),
                r.bottomLeft(),
            };
        } else if (variant == 5) {
            shape.kind = ShapeKind::Polyline;
            shape.vertices = {
                r.topLeft(),
                {r.right() - r.width() * 0.28, r.top()},
                r.bottomRight(),
                r.bottomLeft(),
            };
        } else {
            shape.kind = ShapeKind::Polyline;
            shape.vertices = {
                {r.left() + r.width() * 0.18, r.top()},
                {r.right() - r.width() * 0.08, r.top() + r.height() * 0.22},
                r.bottomRight(),
                {r.left(), r.bottom() - r.height() * 0.08},
            };
        }
        shape.closed = true;
        break;

    case ShapeFamily::Circle: {
        if (variant < 0 || variant > 3)
            return false;
        const bool hasRadialOrigin = (shape.kind == ShapeKind::Circle && shape.vertices.size() >= 2)
            || ((shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector) && shape.vertices.size() >= 3);
        const QPointF c = hasRadialOrigin ? shape.vertices[0] : r.center();
        const double rad = hasRadialOrigin
            ? std::max(1.0, length(shape.vertices[1] - shape.vertices[0]))
            : std::max(1.0, (r.width() + r.height()) * 0.25);
        if (variant == 0) {
            shape.kind = ShapeKind::Circle;
            shape.closed = true;
            shape.vertices = {c, c + QPointF(rad, 0.0)};
        } else if (variant == 1 || variant == 2) {
            const bool keepRays = (shape.kind == ShapeKind::Arc || shape.kind == ShapeKind::Sector)
                && shape.vertices.size() >= 3
                && length(shape.vertices[1] - shape.vertices[0]) > 1e-4
                && length(shape.vertices[2] - shape.vertices[0]) > 1e-4;
            if (keepRays)
                shape.vertices = shape.vertices.mid(0, 3);
            else
                shape.vertices = {c, c + QPointF(rad, 0.0), c + QPointF(0.0, -rad)};
            shape.kind = (variant == 1) ? ShapeKind::Arc : ShapeKind::Sector;
            shape.closed = (variant == 2);
        } else {
            if (shape.kind == ShapeKind::Ellipse && shape.vertices.size() >= 2)
                shape.vertices = {r.topLeft(), r.bottomRight()};
            else
                shape.vertices = {c - QPointF(rad, rad), c + QPointF(rad, rad)};
            shape.kind = ShapeKind::Ellipse;
            shape.closed = true;
        }
        break;
    }

    case ShapeFamily::Polygon: {
        if (variant < 5 || variant > 12)
            return false;
        const double uniformR = regularPolygonRadiusIfUniform(shape);
        const double rad = (uniformR > 1.0) ? uniformR : std::max(1.0, std::min(r.width(), r.height()) * 0.5);
        QVector<QPointF> poly;
        poly.reserve(variant);
        constexpr double start = -std::numbers::pi * 0.5;
        for (int i = 0; i < variant; ++i) {
            const double a = start + 2.0 * std::numbers::pi * i / double(variant);
            poly.append(QPointF(std::cos(a), std::sin(a)) * rad);
        }
        const QPointF shift = r.center() - vertexBounds(poly).center();
        for (QPointF& pt : poly)
            pt += shift;
        shape.kind = ShapeKind::Polyline;
        shape.closed = true;
        shape.vertices = std::move(poly);
        break;
    }

    case ShapeFamily::Solid3D:
        if (variant < 0 || variant > 9)
            return false;
        shape.kind = ShapeKind::Solid3D;
        shape.closed = false;
        shape.solidKind = variant;
        shape.vertices = {r.topLeft(), r.bottomRight()};
        break;

    case ShapeFamily::None:
        return false;
    }

    shape.bounds = computeShapeBounds(shape);
    return true;
}

} // namespace finily
