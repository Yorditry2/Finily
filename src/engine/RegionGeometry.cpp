#include "RegionGeometry.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace finily {
namespace {
constexpr double tolerance = contourJoinTolerance;

double cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
double squaredLength(QPointF p) { return QPointF::dotProduct(p, p); }
double projection(QPointF point, QPointF a, QPointF b) {
    return QPointF::dotProduct(point - a, b - a) / squaredLength(b - a);
}

struct Cut {
    double t;
    QPointF point;
};

struct Segment {
    QPointF a, b;
    QRectF bounds;
    QVector<Cut> cuts;
    QString sourceId;
};

Segment line(QPointF a, QPointF b, const QString& sourceId = {}) {
    return {a, b, QRectF(a, b).normalized(), {{0, a}, {1, b}}, sourceId};
}

void splitAtContact(Segment& edge, QPointF endpoint) {
    const auto delta = edge.b - edge.a;
    const double t = projection(endpoint, edge.a, edge.b);
    if (t > 0 && t < 1 && squaredLength(endpoint - (edge.a + delta * t)) <= tolerance * tolerance)
        edge.cuts.append({t, endpoint});
}

void splitAtIntersections(Segment& a, Segment& b) {
    const auto ab = a.b - a.a;
    const auto cd = b.b - b.a;
    const auto offset = b.a - a.a;
    const double denominator = cross(ab, cd);
    if (std::abs(denominator) > 1e-12) {
        const double t = cross(offset, cd) / denominator;
        const double u = cross(offset, ab) / denominator;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
            const auto point = a.a + t * ab;
            a.cuts.append({t, point});
            b.cuts.append({u, point});
        }
    }
    splitAtContact(a, b.a);
    splitAtContact(a, b.b);
    splitAtContact(b, a.a);
    splitAtContact(b, a.b);
}

struct Node {
    QPointF point;
    QVector<int> outgoing;
};

struct HalfEdge {
    int end;
    int next = -1;
    int face = -1;
    QString sourceId;
};

struct Face {
    QPainterPath path;
    double twiceArea;
    QHash<QString, int> boundary;
};
}

RegionFace closedFaceAt(QPointF point, const QVector<RegionOutline>& outlines,
                        const QHash<QString, int>& expected) {
    QVector<Segment> segments;
    QVector<qsizetype> starts;
    QVector<QRectF> bounds;
    for (const auto& outline : outlines) {
        starts.append(segments.size());
        bounds.append(outline.points.boundingRect());
        for (qsizetype i = 1; i < outline.points.size(); ++i) {
            const auto a = outline.points[i - 1], b = outline.points[i];
            if (squaredLength(b - a) > tolerance * tolerance) {
                const double t = std::clamp(projection(point, a, b), 0.0, 1.0);
                if (expected.isEmpty() && squaredLength(point - (a + (b - a) * t)) <= tolerance * tolerance)
                    return {};
                segments.append(line(a, b, outline.sourceId));
            }
        }
    }
    starts.append(segments.size());
    // Close terminal near-misses up to one world pixel beyond the visible ink
    // in the fill geometry. Keep the drawn paths untouched.
    constexpr double smallGap = 1.0;
    for (qsizetype i = 0; i < outlines.size(); ++i) {
        const auto& outline = outlines[i];
        if (outline.points.size() < 2 || outline.points.isClosed())
            continue;
        for (const auto endpoint : {outline.points.front(), outline.points.back()}) {
            double best = std::numeric_limits<double>::infinity();
            QPointF target = endpoint;
            for (qsizetype j = 0; j < outlines.size(); ++j) {
                if (i == j)
                    continue;
                const double reach = outline.strokeRadius + outlines[j].strokeRadius + smallGap;
                if (!bounds[j].adjusted(-reach, -reach, reach, reach).contains(endpoint))
                    continue;
                for (qsizetype index = starts[j]; index < starts[j + 1]; ++index) {
                    const auto& segment = segments[index];
                    if (!segment.bounds.adjusted(-reach, -reach, reach, reach).contains(endpoint))
                        continue;
                    const double t = std::clamp(projection(endpoint, segment.a, segment.b), 0.0, 1.0);
                    const auto candidate = segment.a + (segment.b - segment.a) * t;
                    const double distance = squaredLength(endpoint - candidate);
                    if (distance <= reach * reach && distance < best) {
                        best = distance;
                        target = candidate;
                    }
                }
            }
            if (squaredLength(endpoint - target) > tolerance * tolerance)
                segments.append(line(endpoint, target));
        }
    }
    // Sweep horizontally so disjoint figures don't require pairwise intersection tests.
    std::stable_sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
        return a.bounds.left() < b.bounds.left();
    });
    for (qsizetype i = 0; i < segments.size(); ++i) {
        auto& a = segments[i];
        for (qsizetype j = i + 1; j < segments.size(); ++j) {
            auto& b = segments[j];
            if (b.bounds.left() > a.bounds.right() + tolerance)
                break;
            if (b.bounds.top() <= a.bounds.bottom() + tolerance && b.bounds.bottom() >= a.bounds.top() - tolerance)
                splitAtIntersections(a, b);
        }
    }

    QVector<Node> nodes;
    QVector<HalfEdge> edges;
    QHash<QPair<double, double>, QVector<int>> grid;
    QSet<QPair<int, int>> linkedNodes;
    auto nodeAt = [&](QPointF p) {
        const auto x = std::floor(p.x() / tolerance), y = std::floor(p.y() / tolerance);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const auto cell = grid.constFind({x + dx, y + dy});
                if (cell == grid.cend())
                    continue;
                for (int index : *cell)
                    if (squaredLength(nodes[index].point - p) <= tolerance * tolerance)
                        return index;
            }
        }
        const int index = int(nodes.size());
        nodes.append({p, {}});
        grid[{x, y}].append(index);
        return index;
    };
    for (auto& segment : segments) {
        std::stable_sort(segment.cuts.begin(), segment.cuts.end(), [](const Cut& a, const Cut& b) { return a.t < b.t; });
        int from = nodeAt(segment.cuts.front().point);
        for (qsizetype i = 1; i < segment.cuts.size(); ++i) {
            const int to = nodeAt(segment.cuts[i].point);
            if (from == to)
                continue;
            const QPair<int, int> key(std::min(from, to), std::max(from, to));
            if (!linkedNodes.contains(key)) {
                linkedNodes.insert(key);
                nodes[from].outgoing.append(int(edges.size()));
                edges.append(HalfEdge{to, -1, -1, segment.sourceId});
                nodes[to].outgoing.append(int(edges.size()));
                edges.append(HalfEdge{from, -1, -1, segment.sourceId});
            }
            from = to;
        }
    }

    // Walk the face on the same side of each directed edge by taking the
    // previous edge in angular order at the junction.
    for (auto& node : nodes) {
        auto heading = [&](int edge) {
            const auto direction = nodes[edges[edge].end].point - node.point;
            return std::atan2(direction.y(), direction.x());
        };
        std::sort(node.outgoing.begin(), node.outgoing.end(), [&](int a, int b) { return heading(a) < heading(b); });
        for (qsizetype i = 0; i < node.outgoing.size(); ++i) {
            const auto predecessor = i ? i - 1 : node.outgoing.size() - 1;
            edges[node.outgoing[i] ^ 1].next = node.outgoing[predecessor];
        }
    }
    QVector<Face> faces;
    for (int first = 0; first < edges.size(); ++first) {
        if (edges[first].face >= 0)
            continue;
        const auto origin = nodes[edges[first ^ 1].end].point;
        QPainterPath path;
        path.moveTo(origin);
        double twiceArea = 0;
        int at = first;
        auto previous = origin;
        do {
            const auto next = nodes[edges[at].end].point;
            twiceArea += cross(previous - origin, next - origin);
            path.lineTo(next);
            edges[at].face = int(faces.size());
            previous = next;
            at = edges[at].next;
        } while (at != first);
        path.closeSubpath();
        faces.append({std::move(path), twiceArea, {}});
    }
    for (int i = 0; i < edges.size(); ++i) {
        const auto& edge = edges[i];
        if (edge.face != edges[i ^ 1].face && !edge.sourceId.isEmpty())
            faces[edge.face].boundary[edge.sourceId] |= (i & 1) ? 2 : 1;
    }

    RegionFace chosen;
    double winningArea = std::numeric_limits<double>::infinity();
    double winningDistance = std::numeric_limits<double>::infinity();
    for (const auto& face : faces) {
        if (face.twiceArea <= tolerance * tolerance || (expected.isEmpty() && !face.path.contains(point)))
            continue;
        auto boundary = face.boundary;
        QPainterPath holes;
        QVector<const Face*> children;
        for (const auto& other : faces)
            if (other.twiceArea > tolerance * tolerance && other.twiceArea < face.twiceArea
                && face.path.contains(other.path))
                children.append(&other);
        for (const auto* child : children) {
            if (std::any_of(children.cbegin(), children.cend(), [&](auto other) {
                return other->twiceArea > child->twiceArea && other->path.contains(child->path);
            })) continue;
            holes = holes.united(child->path);
            for (auto it = child->boundary.cbegin(); it != child->boundary.cend(); ++it)
                boundary[it.key()] |= ((it.value() & 1) << 1) | ((it.value() & 2) >> 1);
        }
        if (!expected.isEmpty() && boundary != expected)
            continue;
        const auto path = holes.isEmpty() ? face.path : face.path.subtracted(holes);
        const double distance = path.contains(point) ? 0 : squaredLength(path.boundingRect().center() - point);
        if (distance < winningDistance || (distance == winningDistance && face.twiceArea < winningArea)) {
            chosen = {path, boundary};
            winningArea = face.twiceArea;
            winningDistance = distance;
        }
    }
    return chosen;
}
}
