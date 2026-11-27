#include "InkEngine.h"

#include "core/Utils.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace finily {
namespace {

QPointF pos(const StrokePoint& p) {
    return {p.x, p.y};
}

double dot(QPointF a, QPointF b) {
    return a.x() * b.x() + a.y() * b.y();
}

double length(QPointF p) {
    return std::hypot(p.x(), p.y());
}

double painterScale(const QPainter& painter) {
    const QTransform& t = painter.worldTransform();
    return std::hypot(t.m11(), t.m21());
}

bool isStraightHighlighter(const Stroke& stroke) {
    return stroke.tool == ToolType::Highlighter
        && stroke.tipStyle == 2
        && stroke.points.size() >= 2;
}

double distanceSquared(QPointF p, QPointF a, QPointF b) {
    const QPointF d = b - a;
    const double lenSq = dot(d, d);
    const double t = lenSq > 1e-16 ? std::clamp(dot(p - a, d) / lenSq, 0.0, 1.0) : 0.0;
    return dot(p - a - t * d, p - a - t * d);
}

StrokePoint mix(const StrokePoint& a, const StrokePoint& b, double t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.pressure + (b.pressure - a.pressure) * t,
        a.timestampMs + qint64(double(b.timestampMs - a.timestampMs) * t),
        a.tiltX + (b.tiltX - a.tiltX) * t,
        a.tiltY + (b.tiltY - a.tiltY) * t,
    };
}

// Cubic Hermite form of Catmull-Rom; duplicate endpoints give rounded starts.
QPainterPath curve(const QVector<StrokePoint>& points) {
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(pos(points.front()));
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QPointF a = pos(points[std::max<qsizetype>(0, i - 1)]);
        const QPointF b = pos(points[i]);
        const QPointF c = pos(points[i + 1]);
        const QPointF d = pos(points[std::min(i + 2, points.size() - 1)]);
        path.cubicTo(b + (c - a) / 6.0, c - (d - b) / 6.0, c);
    }
    return path;
}

struct InkSample {
    StrokePoint point;
    double speed = 0.0; // World pixels per millisecond, independent of subdivision.
    qsizetype seg = 0;  // Index of the source point this sample starts from.
};

double pointSpeedAt(const QVector<StrokePoint>& points, qsizetype i) {
    const qsizetype before = std::max<qsizetype>(0, i - 1);
    const qsizetype after = std::min(i + 1, points.size() - 1);
    const double elapsed = double(points[after].timestampMs - points[before].timestampMs);
    // Old documents may lack usable timing. Do not invent extreme flicks.
    if (elapsed <= 0.0)
        return 0.0;
    double distance = 0.0;
    for (qsizetype j = before + 1; j <= after; ++j)
        distance += length(pos(points[j]) - pos(points[j - 1]));
    return distance / elapsed;
}

QVector<InkSample> sampledCurveRange(const QVector<StrokePoint>& points, qsizetype startPoint) {
    QVector<InkSample> samples;
    if (points.isEmpty())
        return samples;
    const qsizetype start = std::clamp<qsizetype>(startPoint, 0, points.size() - 1);
    double prevSpeed = pointSpeedAt(points, start);
    samples.append({points[start], prevSpeed, start});
    for (qsizetype i = start; i + 1 < points.size(); ++i) {
        const double nextSpeed = pointSpeedAt(points, i + 1);
        const QPointF a = pos(points[std::max<qsizetype>(0, i - 1)]);
        const QPointF b = pos(points[i]);
        const QPointF c = pos(points[i + 1]);
        const QPointF d = pos(points[std::min(i + 2, points.size() - 1)]);
        const QPointF c1 = b + (c - a) / 6.0;
        const QPointF c2 = c - (d - b) / 6.0;
        const double segLen = length(c1 - b) + length(c2 - c1) + length(c - c2);
        const int count = isFinite(segLen) ? std::clamp(int(std::ceil(segLen / 1.5)), 1, 2048) : 1;
        for (int j = 1; j <= count; ++j) {
            const double t = double(j) / count;
            const double u = 1.0 - t;
            const QPointF p = u * u * u * b + 3.0 * u * u * t * c1 + 3.0 * u * t * t * c2 + t * t * t * c;
            auto point = mix(points[i], points[i + 1], t);
            const double blend = t * t * (3.0 - 2.0 * t);
            point.pressure = points[i].pressure + (points[i + 1].pressure - points[i].pressure) * blend;
            point.x = p.x();
            point.y = p.y();
            samples.append({point, prevSpeed + (nextSpeed - prevSpeed) * blend, i});
        }
        prevSpeed = nextSpeed;
    }
    return samples;
}

double pressureMix(double baseFactor, double dynamicFactor, double sensitivity) {
    return std::lerp(baseFactor, dynamicFactor, std::clamp(sensitivity, 0.0, 1.0));
}

struct LiquidShape {
    QVector<InkSample> samples;
    QVector<double> distances;
    QVector<double> radii;
};

LiquidShape liquidShapeRange(const Stroke& stroke, qsizetype startPoint,
                             double prefixDistance, double minRadius) {
    const auto samples = sampledCurveRange(stroke.points, startPoint);
    if (samples.isEmpty())
        return {};
    QVector<double> distances(samples.size(), 0.0);
    QVector<double> radii(samples.size(), 0.0);
    distances[0] = prefixDistance;
    for (qsizetype i = 1; i < samples.size(); ++i)
        distances[i] = distances[i - 1] + length(pos(samples[i].point) - pos(samples[i - 1].point));
    const double total = distances.back();
    const double taperLength = std::min(8.0, total * 0.35);
    for (qsizetype i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        const double fast = sample.speed / (sample.speed + 0.7);
        double factor = 0.85;
        if (stroke.tool == ToolType::Ballpoint || stroke.tipStyle == 0) {
            // Bolígrafo: monolínea nítida y predecible para números y fórmulas.
            const double pressure = stroke.hasHardwarePressure
                                        ? std::clamp(sample.point.pressure, 0.0, 1.0)
                                        : 0.5;
            factor = 0.96 + 0.06 * pressure - 0.02 * fast;
            const double ballTaper = std::min(4.0, total * 0.20);
            if (ballTaper > 1e-6) {
                const double edgeDist = std::min(distances[i], total - distances[i]);
                const double t = std::clamp(edgeDist / ballTaper, 0.0, 1.0);
                factor *= 0.65 + 0.35 * t * t * (3.0 - 2.0 * t);
            }
        } else if (stroke.tipStyle == 2) {
            // Lápiz grafito: sensible a presión (según sensibilidad 0..1) y a inclinación (tilt).
            const double p = stroke.hasHardwarePressure ? std::clamp(sample.point.pressure, 0.0, 1.0) : 0.60;
            const double tilt = std::hypot(sample.point.tiltX, sample.point.tiltY);
            const double tiltBoost = 1.0 + 0.45 * std::clamp((tilt - 12.0) / 38.0, 0.0, 1.0);
            const double pFactor = stroke.hasHardwarePressure
                                       ? pressureMix(0.96, 0.45 + 0.80 * p, stroke.pressureSensitivity)
                                       : 0.96;
            factor = pFactor * (1.0 - 0.08 * fast) * tiltBoost;
            if (taperLength > 1e-6) {
                const double edgeDist = std::min(distances[i], total - distances[i]);
                const double t = std::clamp(edgeDist / taperLength, 0.0, 1.0);
                factor *= 0.40 + 0.60 * t * t * (3.0 - 2.0 * t);
            }
        } else if (stroke.tipStyle == 3) {
            // Tiza: trazo firme con micro-rugosidad en el borde y respuesta a la sensibilidad 0..1.
            const double p = stroke.hasHardwarePressure ? std::clamp(sample.point.pressure, 0.0, 1.0) : 0.68;
            const double tilt = std::hypot(sample.point.tiltX, sample.point.tiltY);
            const double tiltBoost = 1.0 + 0.25 * std::clamp((tilt - 15.0) / 35.0, 0.0, 1.0);
            const double edgeNoise = std::sin(distances[i] * 2.3 + double(i) * 1.7)
                                   * std::cos(distances[i] * 0.9 - double(i) * 0.6);
            const double pFactor = stroke.hasHardwarePressure
                                       ? pressureMix(1.00, 0.55 + 0.70 * p, stroke.pressureSensitivity)
                                       : 1.00;
            factor = pFactor * (1.0 - 0.12 * fast) * tiltBoost * (1.0 + 0.13 * edgeNoise);
            if (taperLength > 1e-6) {
                const double edgeDist = std::min(distances[i], total - distances[i]);
                const double t = std::clamp(edgeDist / taperLength, 0.0, 1.0);
                factor *= 0.55 + 0.45 * t * t * (3.0 - 2.0 * t);
            }
        } else {
            // Pluma fuente: caligráfica, contraste por sensibilidad 0..1 y ángulo de plumilla.
            const double p = stroke.hasHardwarePressure ? std::clamp(sample.point.pressure, 0.0, 1.0) : 0.55;
            factor = stroke.hasHardwarePressure
                         ? pressureMix(0.95, 0.20 + 1.18 * p, stroke.pressureSensitivity)
                         : 0.95;
            factor *= 1.0 - 0.22 * fast;
            const qsizetype before = std::max<qsizetype>(0, i - 1);
            const qsizetype after = std::min(i + 1, samples.size() - 1);
            const QPointF direction = pos(samples[after].point) - pos(samples[before].point);
            if (length(direction) > 1e-8) {
                const double heading = std::atan2(direction.y(), direction.x());
                const double tilt = std::hypot(sample.point.tiltX, sample.point.tiltY);
                double nib = std::numbers::pi / 4.0;
                if (tilt > 1e-6) {
                    const double tiltAngle = std::atan2(sample.point.tiltY, sample.point.tiltX);
                    nib += std::min(1.0, tilt / 45.0) * std::remainder(tiltAngle - nib, std::numbers::pi);
                }
                factor *= 1.0 - 0.34 * std::cos(2.0 * (heading - nib)) + 0.18 * std::sin(heading);
            }
            const double nibTaper = std::min(6.0, total * 0.22);
            if (nibTaper > 1e-6) {
                const double edgeDist = std::min(distances[i], total - distances[i]);
                const double t = std::clamp(edgeDist / nibTaper, 0.0, 1.0);
                factor *= 0.45 + 0.55 * t * t * (3.0 - 2.0 * t);
            }
        }
        radii[i] = std::max(0.0005, stroke.size * factor / 2.0);
    }
    // Pasadas [1 2 1]/4: quitan el temblor de la presión sin mover la forma (tiza conserva más rugosidad).
    const int smoothPasses = (stroke.tool == ToolType::Pen && stroke.tipStyle == 3) ? 1 : 3;
    for (int pass = 0; pass < smoothPasses && radii.size() > 2; ++pass) {
        double prev = radii[0];
        for (qsizetype i = 1; i + 1 < radii.size(); ++i) {
            const double current = radii[i];
            radii[i] = 0.25 * prev + 0.5 * current + 0.25 * radii[i + 1];
            prev = current;
        }
    }
    // Una línea más fina que ~0.8 px de pantalla se ve gris y sucia: se limita el radio mínimo.
    for (auto& r : radii)
        r = std::max(r, minRadius);
    return {samples, distances, radii};
}

LiquidShape liquidShape(const Stroke& stroke, double minRadius) {
    return liquidShapeRange(stroke, 0, 0.0, minRadius);
}

// Envelope of samples first..last. With diskAtFirst the disk at `first` is included;
// otherwise only the pieces that connect first->first+1 ... last (no repeated disk).
QPainterPath liquidEnvelope(const LiquidShape& shape, qsizetype first, qsizetype last, bool diskAtFirst) {
    const auto& samples = shape.samples;
    const auto& radii = shape.radii;
    QPainterPath envelope;
    envelope.setFillRule(Qt::WindingFill);
    first = std::max<qsizetype>(0, first);
    last = std::min(last, samples.size() - 1);
    for (qsizetype i = first; i <= last; ++i) {
        const QPointF center = pos(samples[i].point);
        if (i > first || diskAtFirst)
            envelope.addEllipse(center, radii[i], radii[i]);
        if (i == 0 || i == first)
            continue;
        const QPointF previous = pos(samples[i - 1].point);
        const QPointF delta = center - previous;
        const double d = length(delta);
        const double r0 = radii[i - 1];
        const double r1 = radii[i];
        // A contained/coincident disk already has its complete outer envelope.
        if (d <= std::abs(r1 - r0) + 1e-9)
            continue;
        const QPointF axis = delta / d;
        const QPointF normal(-axis.y(), axis.x());
        const double cosine = std::clamp((r0 - r1) / d, -1.0, 1.0);
        const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
        const QPointF n0 = axis * cosine - normal * sine;
        const QPointF n1 = axis * cosine + normal * sine;
        // Same clockwise winding as addEllipse: overlapping loops stay filled.
        envelope.moveTo(previous + n0 * r0);
        envelope.lineTo(center + n0 * r1);
        envelope.lineTo(center + n1 * r1);
        envelope.lineTo(previous + n1 * r0);
        envelope.closeSubpath();
    }
    return envelope;
}

// Misma silueta que liquidEnvelope, pero como "cinta": un solo contorno por tramo suave
// (borde izquierdo hacia delante, derecho de vuelta) en vez de un disco + trapecio por
// muestra. Rellenar cientos de figuras superpuestas es lo caro; una cinta tiene ~2 bordes
// por línea de barrido. Donde la cinta no es fiable (giro brusco, borde interior que se
// pliega, disco contenido en otro) se corta y se ponen discos/trapecios como antes.
QPainterPath liquidRibbon(const LiquidShape& shape, double pixelScale) {
    const auto& samples = shape.samples;
    const auto& radii = shape.radii;
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    const qsizetype n = samples.size();
    if (n == 0)
        return out;
    auto disk = [&](qsizetype i) {
        out.addEllipse(pos(samples[i].point), radii[i], radii[i]);
    };
    if (n == 1) {
        disk(0);
        return out;
    }
    struct Seg {
        QPointF a0;
        QPointF a1;
        QPointF b0;
        QPointF b1;
        QPointF n0;
        QPointF n1;
        bool ok = false;
    };
    QVector<Seg> seg(n); // seg[i]: muestra i-1 -> i
    for (qsizetype i = 1; i < n; ++i) {
        const QPointF c0 = pos(samples[i - 1].point);
        const QPointF c1 = pos(samples[i].point);
        const QPointF delta = c1 - c0;
        const double d = length(delta);
        const double r0 = radii[i - 1];
        const double r1 = radii[i];
        if (d <= std::abs(r1 - r0) + 1e-9)
            continue;
        const QPointF axis = delta / d;
        const QPointF normal(-axis.y(), axis.x());
        const double cosine = std::clamp((r0 - r1) / d, -1.0, 1.0);
        const double sine = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
        Seg sg;
        sg.n0 = axis * cosine - normal * sine;
        sg.n1 = axis * cosine + normal * sine;
        sg.a0 = c0 + sg.n0 * r0;
        sg.a1 = c1 + sg.n0 * r1;
        sg.b0 = c0 + sg.n1 * r0;
        sg.b1 = c1 + sg.n1 * r1;
        // Bordes que avanzan con el trazo; si uno retrocede, el borde se pliega.
        sg.ok = dot(sg.a1 - sg.a0, axis) > 1e-9 && dot(sg.b1 - sg.b0, axis) > 1e-9;
        seg[i] = sg;
    }
    const double scale = std::max(pixelScale, 1e-6);
    // Unir dos tramos con una recta en vez del arco del disco se desvía ~gap²/(8r):
    // se corta la cinta solo si esa desviación pasaría de 0.08 px de pantalla.
    auto smoothJoint = [&](qsizetype i) { // unión en la muestra i entre seg[i] y seg[i+1]
        const Seg& p = seg[i];
        const Seg& q = seg[i + 1];
        const double gap = std::max(length(p.a1 - q.a0), length(p.b1 - q.b0)) * scale;
        const double r = std::max(radii[i] * scale, 1e-6);
        // En curvas más cerradas que el grosor el borde interior retrocede (se pliega) y la
        // cinta dejaría huecos: ahí también se corta.
        const QPointF axis = pos(samples[i + 1].point) - pos(samples[i].point);
        const bool forward = dot(q.a1 - p.a1, axis) > 1e-12 && dot(q.b1 - p.b1, axis) > 1e-12;
        return forward && gap * gap / (8.0 * r) <= 0.08 && gap <= 2.0 * r;
    };
    auto trapezoid = [&](const Seg& sg) {
        out.moveTo(sg.a0);
        out.lineTo(sg.a1);
        out.lineTo(sg.b1);
        out.lineTo(sg.b0);
        out.closeSubpath();
    };
    qsizetype i = 1;
    while (i < n) {
        if (!seg[i].ok) {
            const QPointF c0 = pos(samples[i - 1].point);
            const QPointF c1 = pos(samples[i].point);
            if (length(c1 - c0) > std::abs(radii[i] - radii[i - 1]) + 1e-9)
                trapezoid(seg[i]);
            disk(i - 1);
            disk(i);
            ++i;
            continue;
        }
        qsizetype e = i; // tramo seg[i]..seg[e]
        while (e + 1 < n && seg[e + 1].ok && smoothJoint(e))
            ++e;
        disk(i - 1);
        disk(e);
        if (e == i) {
            trapezoid(seg[i]);
            ++i;
            continue;
        }
        out.moveTo(seg[i].a0);
        for (qsizetype k = i; k <= e; ++k) {
            if (k > i)
                out.lineTo(seg[k].a0);
            out.lineTo(seg[k].a1);
        }
        for (qsizetype k = e; k >= i; --k) {
            out.lineTo(seg[k].b1);
            out.lineTo(seg[k].b0);
        }
        out.closeSubpath();
        i = e + 1;
    }
    return out;
}

double screenMinRadius(double zoom) {
    return zoom > 1e-6 ? 0.25 / zoom : 0.0005;
}

quint64 computeRelativeStrokeHash(const Stroke& s) {
    quint64 h = 14695981039346656037ULL;
    auto mixInt = [&](quint64 v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    auto safeRound = [](double v) -> quint64 {
        return isFinite(v) ? quint64(std::llround(v)) : 0ULL;
    };
    mixInt(quint64(s.tool));
    mixInt(quint64(s.tipStyle));
    mixInt(quint64(s.linePattern));
    mixInt(quint64(s.pressureProfile));
    mixInt(safeRound(s.pressureSensitivity * 100.0));
    mixInt(quint64(s.hasHardwarePressure));
    mixInt(safeRound(s.size * 1000.0));
    mixInt(quint64(s.points.size()));
    if (!s.points.isEmpty()) {
        const double x0 = s.points.front().x;
        const double y0 = s.points.front().y;
        qint64 prevTs = s.points.front().timestampMs;
        for (const auto& p : s.points) {
            mixInt(safeRound((p.x - x0) * 100.0));
            mixInt(safeRound((p.y - y0) * 100.0));
            mixInt(safeRound(p.pressure * 1000.0));
            mixInt(safeRound(p.tiltX * 100.0));
            mixInt(safeRound(p.tiltY * 100.0));
            mixInt(quint64(p.timestampMs) - quint64(prevTs));
            prevTs = p.timestampMs;
        }
    }
    return h == 0 ? 1 : h;
}

QPainterPath cachedLiquidPath(const Stroke& stroke, double zoom) {
    const double minRad = screenMinRadius(zoom);
    // Miniaturas o zoom extremo (< 0.4): nunca contaminar la caché de pantalla del trazo
    if (zoom < 0.4) {
        const auto shape = liquidShape(stroke, minRad);
        return liquidRibbon(shape, zoom);
    }
    quint64 h = computeRelativeStrokeHash(stroke);
    h ^= quint64(std::llround(minRad * 50.0)) + 0x9e3779b97f4a7c15ULL;
    h *= 1099511628211ULL;
    if (h == 0)
        h = 1;
    const QPointF origin = pos(stroke.points.front());
    if (stroke.cachedShapeHash == h && !stroke.cachedPath.isEmpty()) {
        const QPointF delta = origin - stroke.cachedOrigin;
        if (std::abs(delta.x()) > 1e-6 || std::abs(delta.y()) > 1e-6) {
            stroke.cachedPath.translate(delta);
            stroke.cachedOrigin = origin;
        }
        return stroke.cachedPath;
    }
    const auto shape = liquidShape(stroke, minRad);
    stroke.cachedPath = liquidRibbon(shape, zoom);
    stroke.cachedOrigin = origin;
    stroke.cachedShapeHash = h;
    return stroke.cachedPath;
}

const QPainterPath& cachedCurvePath(const Stroke& stroke) {
    const quint64 h = computeRelativeStrokeHash(stroke);
    const QPointF origin = pos(stroke.points.front());
    if (stroke.cachedShapeHash == h && !stroke.cachedPath.isEmpty()) {
        const QPointF delta = origin - stroke.cachedOrigin;
        if (std::abs(delta.x()) > 1e-6 || std::abs(delta.y()) > 1e-6) {
            stroke.cachedPath.translate(delta);
            stroke.cachedOrigin = origin;
        }
        return stroke.cachedPath;
    }
    stroke.cachedPath = curve(stroke.points);
    stroke.cachedOrigin = origin;
    stroke.cachedShapeHash = h;
    return stroke.cachedPath;
}

// Textura tileable cacheada para puntas granuladas (2 = Lápiz grafito, 3 = Tiza).
QBrush texturedStrokeBrush(const Stroke& stroke) {
    if (stroke.tool != ToolType::Pen || (stroke.tipStyle != 2 && stroke.tipStyle != 3))
        return QBrush(stroke.color);
    static QMutex cacheMutex;
    static QHash<quint64, QBrush> cache;
    QMutexLocker locker(&cacheMutex);
    const quint64 key = (quint64(stroke.tipStyle) << 32) | quint64(stroke.color.rgba());
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    if (cache.size() >= 64)
        cache.clear();

    constexpr int kTex = 128;
    QImage img(kTex, kTex, QImage::Format_ARGB32_Premultiplied);
    auto hash2 = [](int x, int y, quint32 seed) -> double {
        quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u + seed * 1442695041u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);
        return double(h & 0xFFFFu) / 65535.0;
    };
    const int baseR = stroke.color.red();
    const int baseG = stroke.color.green();
    const int baseB = stroke.color.blue();
    const int baseA = stroke.color.alpha();
    const bool isChalk = (stroke.tipStyle == 3);

    for (int y = 0; y < kTex; ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < kTex; ++x) {
            const double fine = hash2(x, y, isChalk ? 7u : 3u);
            const double coarse = hash2(x / 2, y / 2, isChalk ? 19u : 11u);
            double mask = 1.0;
            if (isChalk) {
                // Tiza: polvo fino y núcleo brillante con micro-poros (sin manchas grandes).
                const double n = 0.85 * fine + 0.15 * coarse;
                if (n < 0.12)
                    mask = 0.24 + n * 2.0;
                else if (n < 0.37)
                    mask = 0.58 + (n - 0.12) * 0.96;
                else
                    mask = 0.85 + (n - 0.37) * 0.24;
            } else {
                // Grafito: grano microscópico fino y sedoso.
                const double n = 0.86 * fine + 0.14 * coarse;
                mask = 0.64 + 0.36 * n;
            }
            const int a = std::clamp(int(std::lround(baseA * mask)), 0, 255);
            const int pr = (baseR * a + 127) / 255;
            const int pg = (baseG * a + 127) / 255;
            const int pb = (baseB * a + 127) / 255;
            row[x] = qRgba(pr, pg, pb, a);
        }
    }
    QBrush brush(img);
    const double scale = isChalk ? 0.30 : 0.26;
    brush.setTransform(QTransform::fromScale(scale, scale));
    cache.insert(key, brush);
    return brush;
}

void drawLiquidInk(QPainter& painter, const Stroke& stroke) {
    // One fill composites opacity exactly once, including tight cursive crossings.
    const QPainterPath path = cachedLiquidPath(stroke, painterScale(painter));
    if (stroke.tool == ToolType::Pen && stroke.tipStyle == 3) {
        // Halo polvoriento muy tenue alrededor del trazo de tiza.
        QColor dust = stroke.color;
        dust.setAlpha(std::max(1, int(std::lround(stroke.color.alpha() * 0.18))));
        QPen dustPen(dust, std::max(0.8, stroke.size * 0.26), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(dustPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(texturedStrokeBrush(stroke));
    painter.drawPath(path);
}

void arrowHead(QPainter& painter, QPointF tip, QPointF previous, double size, QColor color) {
    QPointF v = tip - previous;
    const double n = length(v);
    if (n < 1e-8)
        return;
    v /= n;
    const QPointF normal(-v.y(), v.x());
    const double h = std::max(8.0, size * 4.0);
    const double w = h * 0.48;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{tip, tip - v * h + normal * w,
                                  tip - v * (h * 0.70), tip - v * h - normal * w});
}

} // namespace

namespace {

constexpr qsizetype kLiveInkContextPoints = 6;

// Índice en `shape.samples` que coincide exactamente con el punto fuente `ptIdx`.
qsizetype sampleIndexForSourcePoint(const LiquidShape& shape, qsizetype winStart, qsizetype ptIdx) {
    if (ptIdx <= winStart || shape.samples.isEmpty())
        return 0;
    const qsizetype targetSeg = ptIdx - 1;
    qsizetype found = -1;
    for (qsizetype i = 1; i < shape.samples.size(); ++i) {
        if (shape.samples[i].seg == targetSeg)
            found = i;
        else if (shape.samples[i].seg > targetSeg)
            break;
    }
    return found >= 0 ? found : (shape.samples.size() - 1);
}

QBrush cachedDotTileBrush(int tilePx, QColor color, double dotRadiusPx) {
    static QMutex cacheMutex;
    static QHash<quint64, QBrush> cache;
    QMutexLocker locker(&cacheMutex);
    const int radiusTenths = std::clamp(int(std::lround(dotRadiusPx * 10.0)), 4, 40);
    const quint64 key = (quint64(tilePx) << 40)
        | (quint64(radiusTenths) << 32)
        | quint64(color.rgba());
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    if (cache.size() >= 32)
        cache.clear();

    QImage tile(tilePx, tilePx, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    QPainter tp(&tile);
    tp.setRenderHint(QPainter::Antialiasing, true);
    tp.setPen(Qt::NoPen);
    tp.setBrush(color);
    const double r = radiusTenths / 10.0;
    const double center = tilePx * 0.5;
    tp.drawEllipse(QPointF(center, center), r, r);
    tp.end();

    QBrush brush(tile);
    cache.insert(key, brush);
    return brush;
}

} // namespace

bool LiveInkRenderer::eligible(const Stroke& s) {
    return (s.tool == ToolType::Pen || s.tool == ToolType::Ballpoint)
        && s.linePattern == LinePattern::Solid
        && s.tipStyle != 2
        && s.tipStyle != 3
        && s.opacity >= 0.999
        && s.color.alpha() == 255
        && s.points.size() >= 2;
}

void LiveInkRenderer::reset() {
    m_frozenPoint = -1;
    m_pointDist.clear();
    m_layerDirty = !m_dirtyRect.isEmpty();
    m_vp = {};
    m_pixels = {};
    m_dpr = 1.0;
}

void LiveInkRenderer::reset(QImage& layer) {
    if (!layer.isNull() && !m_dirtyRect.isEmpty() && layer.size() == m_pixels
        && layer.devicePixelRatio() == m_dpr) {
        QPainter clr(&layer);
        clr.setCompositionMode(QPainter::CompositionMode_Clear);
        clr.fillRect(m_dirtyRect, Qt::transparent);
        m_dirtyRect = {};
        m_layerDirty = false;
    } else if (!layer.isNull() && (m_frozenPoint >= 0 || m_layerDirty)) {
        layer.fill(Qt::transparent);
        m_dirtyRect = {};
        m_layerDirty = false;
    }
    m_frozenPoint = -1;
    m_pointDist.clear();
    m_vp = {};
    m_pixels = {};
    m_dpr = 1.0;
}

bool LiveInkRenderer::usable(const Viewport& vp, const QSize& pixels, double dpr) const {
    return m_frozenPoint >= 0
        && vp.x == m_vp.x
        && vp.y == m_vp.y
        && vp.zoom == m_vp.zoom
        && pixels == m_pixels
        && m_dpr == dpr;
}

void LiveInkRenderer::advance(const Stroke& stroke, QImage& layer, const Viewport& vp,
                              const QSize& pixels, double dpr) {
    constexpr double kTail = 30.0;
    constexpr double kChunk = 40.0;
    if (!eligible(stroke) || pixels.isEmpty())
        return;

    const bool stale = m_frozenPoint >= 0
        && (m_frozenPoint >= stroke.points.size() || layer.size() != pixels || vp.x != m_vp.x
            || vp.y != m_vp.y || vp.zoom != m_vp.zoom || layer.devicePixelRatio() != dpr);

    // Conservar contexto previo a m_frozenPoint para mantener idénticas las tangentes
    // Catmull-Rom y las 3 pasadas [1 2 1]/4 sin recalcular desde el punto 0.
    const qsizetype winStart = (m_frozenPoint > kLiveInkContextPoints && !stale)
        ? (m_frozenPoint - kLiveInkContextPoints)
        : 0;
    const double prefixDist = (winStart > 0 && winStart < m_pointDist.size()) ? m_pointDist[winStart] : 0.0;
    const auto shape = liquidShapeRange(stroke, winStart, prefixDist, screenMinRadius(vp.zoom));
    if (shape.samples.size() < 2)
        return;

    const double total = shape.distances.back();
    if (total < kTail + kChunk)
        return;

    const qsizetype maxTargetPoint = stroke.points.size() - 9;
    if (maxTargetPoint <= 0)
        return;

    qsizetype targetPoint = -1;
    for (qsizetype i = shape.samples.size() - 1; i >= 0; --i) {
        if (shape.distances[i] <= total - kTail && shape.samples[i].seg <= maxTargetPoint - 1) {
            targetPoint = shape.samples[i].seg + 1;
            break;
        }
    }
    if (targetPoint <= 0 || targetPoint < winStart)
        return;

    const qsizetype fromPoint = (m_frozenPoint < 0 || stale) ? 0 : m_frozenPoint;
    if (targetPoint <= fromPoint)
        return;

    const qsizetype targetSample = sampleIndexForSourcePoint(shape, winStart, targetPoint);
    const double frozenDistBefore = (fromPoint > 0 && fromPoint < m_pointDist.size())
        ? m_pointDist[fromPoint]
        : 0.0;
    if (m_frozenPoint >= 0 && !stale && shape.distances[targetSample] - frozenDistBefore < kChunk)
        return;

    if (m_frozenPoint < 0 || stale) {
        if (layer.size() != pixels || layer.devicePixelRatio() != dpr) {
            layer = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
            layer.setDevicePixelRatio(dpr);
            layer.fill(Qt::transparent);
        } else if (stale || m_layerDirty) {
            if (!stale && !m_dirtyRect.isEmpty()) {
                QPainter clr(&layer);
                clr.setCompositionMode(QPainter::CompositionMode_Clear);
                clr.fillRect(m_dirtyRect, Qt::transparent);
            } else {
                layer.fill(Qt::transparent);
            }
        }
        m_dirtyRect = {};
        m_layerDirty = false;
        m_pointDist.clear();
        m_vp = vp;
        m_pixels = pixels;
        m_dpr = dpr;
    }

    const qsizetype fromSample = sampleIndexForSourcePoint(shape, winStart, fromPoint);
    const QPainterPath chunkPath = liquidEnvelope(shape, fromSample, targetSample, fromPoint == 0);
    QPainter lp(&layer);
    lp.setRenderHint(QPainter::Antialiasing, true);
    lp.translate(vp.x, vp.y);
    lp.scale(vp.zoom, vp.zoom);
    lp.setPen(Qt::NoPen);
    lp.setBrush(stroke.color);
    lp.drawPath(chunkPath);

    const QRectF chunkBounds = chunkPath.boundingRect();
    if (!chunkBounds.isEmpty()) {
        const QRect logicalBounds(0, 0, int(std::ceil(pixels.width() / dpr)) + 1,
                                  int(std::ceil(pixels.height() / dpr)) + 1);
        const QRect screenRect = QRectF(chunkBounds.topLeft() * vp.zoom + QPointF(vp.x, vp.y),
                                        chunkBounds.bottomRight() * vp.zoom + QPointF(vp.x, vp.y))
                                     .adjusted(-4.0, -4.0, 4.0, 4.0)
                                     .toAlignedRect()
                                     .intersected(logicalBounds);
        m_dirtyRect = m_dirtyRect.isEmpty() ? screenRect : m_dirtyRect.united(screenRect);
    }

    m_pointDist.resize(targetPoint + 1);
    for (qsizetype p = fromPoint; p <= targetPoint; ++p)
        m_pointDist[p] = shape.distances[sampleIndexForSourcePoint(shape, winStart, p)];
    m_frozenPoint = targetPoint;
}

void LiveInkRenderer::drawTail(QPainter& painter, const Stroke& stroke) const {
    if (stroke.points.isEmpty())
        return;
    const qsizetype fromPoint = std::max<qsizetype>(0, m_frozenPoint);
    const qsizetype winStart = (fromPoint > kLiveInkContextPoints && fromPoint < m_pointDist.size())
        ? (fromPoint - kLiveInkContextPoints)
        : 0;
    const double prefixDist = (winStart > 0 && winStart < m_pointDist.size()) ? m_pointDist[winStart] : 0.0;
    const auto shape = liquidShapeRange(stroke, winStart, prefixDist, screenMinRadius(painterScale(painter)));
    if (shape.samples.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(stroke.color);
    const qsizetype first = sampleIndexForSourcePoint(shape, winStart, fromPoint);
    painter.drawPath(liquidEnvelope(shape, first, shape.samples.size() - 1, true));
    painter.restore();
}

void VelocityGaussianStabilizer::reset() {
    m_recent.clear();
    m_smoothedPressure.reset();
}

void VelocityGaussianStabilizer::setStrength(double s) {
    m_strength = isFinite(s) ? std::clamp(s, 0.0, 1.0) : 0.0;
    reset();
}

StrokePoint VelocityGaussianStabilizer::filter(const StrokePoint& point) {
    StrokePoint out = point;
    const double pressure = isFinite(point.pressure) ? std::clamp(point.pressure, 0.0, 1.0) : 0.5;
    m_smoothedPressure = m_smoothedPressure ? 0.55 * pressure + 0.45 * (*m_smoothedPressure) : pressure;
    out.pressure = *m_smoothedPressure;
    if (!isFinite(out.tiltX))
        out.tiltX = 0.0;
    if (!isFinite(out.tiltY))
        out.tiltY = 0.0;
    if (!isFinite(point.x) || !isFinite(point.y)) {
        if (!m_recent.isEmpty()) {
            out.x = m_recent.back().x;
            out.y = m_recent.back().y;
        } else {
            out.x = isFinite(point.x) ? point.x : 0.0;
            out.y = isFinite(point.y) ? point.y : 0.0;
        }
        return out;
    }
    if (m_strength == 0.0)
        return out;
    m_recent.append(out);
    while (m_recent.size() > 5)
        m_recent.removeFirst();
    if (m_recent.size() < 2)
        return out;
    const auto& prev = m_recent[m_recent.size() - 2];
    const double dt = std::max(1.0, double(point.timestampMs - prev.timestampMs));
    const double rawVelocity = length(pos(out) - pos(prev)) / dt;
    const double velocity = isFinite(rawVelocity) ? rawVelocity : 0.0;
    // At handwriting speed the window collapses to the current sample. A bounded
    // time window avoids the accumulating phase lag that closes small cursive loops.
    const double sigma = std::max(0.05, 3.0 * m_strength * std::exp(-velocity * velocity / 0.09));
    double sum = 0.0;
    double x = 0.0;
    double y = 0.0;
    for (qsizetype i = 0; i < m_recent.size(); ++i) {
        const auto& p = m_recent[i];
        const double sampleAge = double(m_recent.size() - 1 - i);
        const double elapsedMs = double(point.timestampMs - p.timestampMs);
        const double age = std::max(sampleAge, elapsedMs);
        const double w = std::exp(-0.5 * age * age / (sigma * sigma));
        x += p.x * w;
        y += p.y * w;
        sum += w;
    }
    if (sum > 0.0 && isFinite(x) && isFinite(y) && isFinite(sum)) {
        out.x = x / sum;
        out.y = y / sum;
    }
    return out;
}

QRectF computeBounds(const QVector<StrokePoint>& points, double padding) {
    if (points.isEmpty())
        return {};
    double l = points.front().x;
    double r = l;
    double t = points.front().y;
    double b = t;
    for (const auto& p : points) {
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::min(t, p.y);
        b = std::max(b, p.y);
    }
    padding = std::max(0.0, padding);
    return QRectF(QPointF(l - padding, t - padding), QPointF(r + padding, b + padding));
}

static double strokeHitPad(const Stroke& stroke) {
    if (stroke.tool == ToolType::Pen && stroke.tipStyle == 2)
        return 0.75;
    if (stroke.tool == ToolType::Pen && stroke.tipStyle == 3)
        return 0.80;
    return 0.7;
}

bool hitTestStroke(const Stroke& stroke, const QPointF& p, double radius) {
    if (stroke.points.isEmpty())
        return false;
    radius = std::max(0.0, radius) + std::max(0.0, stroke.size) * strokeHitPad(stroke);
    const QRectF bounds = stroke.bounds.isEmpty() ? computeBounds(stroke.points) : stroke.bounds;
    if (!bounds.adjusted(-radius, -radius, radius, radius).contains(p))
        return false;
    if (stroke.points.size() == 1)
        return distanceSquared(p, pos(stroke.points[0]), pos(stroke.points[0])) <= radius * radius;
    if (isStraightHighlighter(stroke))
        return distanceSquared(p, pos(stroke.points.front()), pos(stroke.points.back())) <= radius * radius;
    for (qsizetype i = 1; i < stroke.points.size(); ++i) {
        if (distanceSquared(p, pos(stroke.points[i - 1]), pos(stroke.points[i])) <= radius * radius)
            return true;
    }
    return false;
}

std::optional<QVector<Stroke>> eraseStrokePartially(const Stroke& stroke, const QPointF& center,
                                                    double radius) {
    if (!hitTestStroke(stroke, center, radius))
        return std::nullopt;
    QVector<Stroke> result;
    if (stroke.points.size() == 1)
        return result;
    const double pad = strokeHitPad(stroke);
    const double r = std::max(0.0, radius) + std::max(0.0, stroke.size) * pad;
    QVector<StrokePoint> current;
    bool touched = false;
    auto flush = [&] {
        if (current.size() < 2) {
            current.clear();
            return;
        }
        Stroke s = stroke;
        s.id = freshId();
        s.points = current;
        s.bounds = computeBounds(s.points, s.size * pad);
        s.cachedPath = QPainterPath{};
        s.cachedOrigin = QPointF{};
        s.cachedShapeHash = 0;
        result.append(s);
        current.clear();
    };
    auto append = [&](const StrokePoint& p) {
        if (current.isEmpty() || length(pos(p) - pos(current.back())) > 1e-8)
            current.append(p);
    };
    auto processSegment = [&](const StrokePoint& a, const StrokePoint& b) {
        const QPointF d = pos(b) - pos(a);
        const QPointF f = pos(a) - center;
        const double aa = dot(d, d);
        const double bb = 2.0 * dot(f, d);
        const double cc = dot(f, f) - r * r;
        if (aa < 1e-16) {
            if (cc <= 0.0) {
                touched = true;
                flush();
            } else {
                append(a);
                append(b);
            }
            return;
        }
        const double disc = bb * bb - 4.0 * aa * cc;
        if (disc < 0.0) {
            append(a);
            append(b);
            return;
        }
        const double sqrtDisc = std::sqrt(disc);
        const double lo = std::max(0.0, (-bb - sqrtDisc) / (2.0 * aa));
        const double hi = std::min(1.0, (-bb + sqrtDisc) / (2.0 * aa));
        if (hi - lo < 1e-10 || hi < 0.0 || lo > 1.0) {
            append(a);
            append(b);
            return;
        }
        touched = true;
        if (lo > 0.0) {
            append(a);
            append(mix(a, b, lo));
        }
        flush();
        if (hi < 1.0) {
            append(mix(a, b, hi));
            append(b);
        }
    };

    if (isStraightHighlighter(stroke)) {
        processSegment(stroke.points.front(), stroke.points.back());
    } else {
        for (qsizetype i = 1; i < stroke.points.size(); ++i)
            processSegment(stroke.points[i - 1], stroke.points[i]);
    }
    flush();
    if (!touched)
        return std::nullopt;
    return result;
}

void drawStroke(QPainter& painter, const Stroke& stroke, bool lightBackground) {
    if (stroke.points.isEmpty() || stroke.tool == ToolType::Eraser || stroke.tool == ToolType::Hand)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool multiplyHl = (stroke.tool == ToolType::Highlighter && lightBackground);
    painter.setCompositionMode(multiplyHl ? QPainter::CompositionMode_Multiply
                                          : QPainter::CompositionMode_SourceOver);
    const double baseOp = (stroke.tool == ToolType::Highlighter
                               ? (stroke.opacity >= 0.99 ? 0.24 : 1.0)
                               : (stroke.tipStyle == 2 ? 0.78 : (stroke.tipStyle == 3 ? 0.94 : 1.0)));
    painter.setOpacity(std::clamp(stroke.opacity, 0.0, 1.0) * baseOp);
    const double size = std::max(0.01, stroke.size);
    if ((stroke.tool == ToolType::Pen || stroke.tool == ToolType::Ballpoint)
        && stroke.linePattern == LinePattern::Solid) {
        drawLiquidInk(painter, stroke);
    } else {
        const Qt::PenCapStyle cap = (stroke.tool == ToolType::Highlighter
                                     && (stroke.tipStyle == 0 || stroke.tipStyle == 2))
                                        ? Qt::FlatCap
                                        : Qt::RoundCap;
        QPen pen(stroke.color, size, Qt::SolidLine, cap, Qt::RoundJoin);
        if (stroke.linePattern == LinePattern::Dashed)
            pen.setDashPattern({4.0, 2.5});
        if (stroke.linePattern == LinePattern::Dotted)
            pen.setDashPattern({0.01, 2.2});
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if (stroke.points.size() == 1)
            painter.drawPoint(pos(stroke.points.front()));
        else if (isStraightHighlighter(stroke))
            painter.drawLine(pos(stroke.points.front()), pos(stroke.points.back()));
        else
            painter.drawPath(cachedCurvePath(stroke));
        if (stroke.linePattern == LinePattern::Arrow || stroke.linePattern == LinePattern::DoubleArrow) {
            const auto& p = stroke.points;
            qsizetype last = p.size() - 2;
            while (last >= 0 && length(pos(p.back()) - pos(p[last])) < 1e-6)
                --last;
            if (last >= 0)
                arrowHead(painter, pos(p.back()), pos(p[last]), size, stroke.color);
            if (stroke.linePattern == LinePattern::DoubleArrow) {
                qsizetype first = 1;
                while (first < p.size() && length(pos(p.front()) - pos(p[first])) < 1e-6)
                    ++first;
                if (first < p.size())
                    arrowHead(painter, pos(p.front()), pos(p[first]), size, stroke.color);
            }
        }
    }
    painter.restore();
}

void drawBackgroundTemplate(QPainter& painter, BackgroundTemplate tpl, const ThemeConfig& theme,
                            const Viewport& vp, const QRectF& rect, double templateScale,
                            const QRectF& sheetRect) {
    if (tpl == BackgroundTemplate::Blank || !isFinite(vp.zoom) || !(vp.zoom > 0.0) || rect.isEmpty()
        || !isFinite(rect.left()) || !isFinite(rect.top())
        || !isFinite(rect.width()) || !isFinite(rect.height())) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const double clampedScale = isFinite(templateScale) ? templateScale : 1.0;
    double step = std::clamp(32.0 * clampedScale, 12.0, 96.0);
    while (step * vp.zoom < 16.0)
        step *= 2.0;

    const double w = rect.width();
    const double h = rect.height();
    auto estimatePrimitives = [&](double candidateStep) -> double {
        const double cols = w / candidateStep + 1.0;
        const double rows = h / candidateStep + 1.0;
        switch (tpl) {
        case BackgroundTemplate::Dots:
            return cols * rows;
        case BackgroundTemplate::Engineering: {
            const double minor = candidateStep * 0.5;
            double count = (w / (minor * 5.0) + 1.0) + (h / (minor * 5.0) + 1.0);
            if (minor * vp.zoom >= 8.0)
                count += (w / minor + 1.0) + (h / minor + 1.0);
            return count;
        }
        case BackgroundTemplate::Isometric: {
            const double dx = candidateStep * 0.86602540378;
            const double slope = 0.57735026919;
            const double diagSpan = h + slope * w;
            return (w / dx + 2.0) + 2.0 * (diagSpan / candidateStep + 2.0);
        }
        case BackgroundTemplate::Grid:
        case BackgroundTemplate::TwoColumn:
            return cols + rows;
        case BackgroundTemplate::Ruled:
        case BackgroundTemplate::Cornell:
        case BackgroundTemplate::School:
            return rows;
        case BackgroundTemplate::Blank:
            return 0.0;
        }
        return cols + rows;
    };
    constexpr double kMaxTemplatePrimitives = 60000.0;
    while (estimatePrimitives(step) > kMaxTemplatePrimitives && isFinite(step * 2.0))
        step *= 2.0;

    QColor color = theme.patternColor;
    if ((tpl == BackgroundTemplate::Ruled || tpl == BackgroundTemplate::School)
        && theme.id == ThemeId::PaperLight) {
        color = QColor(137, 181, 231, 150);
    }
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter.setPen(pen);
    const double left = std::floor(rect.left() / step) * step;
    const double top = std::floor(rect.top() / step) * step;
    const QColor marginColor = theme.isDark ? QColor(248, 113, 113, 95) : QColor(220, 38, 38, 105);

    if (tpl == BackgroundTemplate::Dots) {
        const auto* engine = painter.paintEngine();
        if (engine && engine->type() == QPaintEngine::Raster) {
            const double dpr = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
            const int tilePx = std::clamp(int(std::lround(step * vp.zoom * dpr)), 16, 256);
            const double dotRadiusPx = std::max(0.8, 1.0 * (double(tilePx) / std::max(1e-6, step * vp.zoom)));
            QBrush brush = cachedDotTileBrush(tilePx, color, dotRadiusPx);
            QTransform bt;
            bt.scale(step / double(tilePx), step / double(tilePx));
            bt.translate(-tilePx * 0.5, -tilePx * 0.5);
            brush.setTransform(bt);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.fillRect(rect, brush);
        } else {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            const double radius = 1.0 / vp.zoom;
            for (double x = left; x <= rect.right(); x += step) {
                for (double y = top; y <= rect.bottom(); y += step)
                    painter.drawEllipse(QPointF(x, y), radius, radius);
            }
        }
    } else if (tpl == BackgroundTemplate::Engineering) {
        const double minor = step * 0.5;
        if (minor * vp.zoom >= 8.0) {
            QColor minorCol = color;
            minorCol.setAlpha(std::max(10, int(color.alpha() * 0.5)));
            QPen minorPen(minorCol);
            minorPen.setCosmetic(true);
            minorPen.setWidthF(1.0);
            painter.setPen(minorPen);
            const double mLeft = std::floor(rect.left() / minor) * minor;
            const double mTop = std::floor(rect.top() / minor) * minor;
            for (double y = mTop; y <= rect.bottom(); y += minor)
                painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
            for (double x = mLeft; x <= rect.right(); x += minor)
                painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        const double major = minor * 5.0;
        QColor majorCol = color;
        majorCol.setAlpha(std::min(255, int(color.alpha() * 2.1 + 18)));
        QPen majorPen(majorCol);
        majorPen.setCosmetic(true);
        majorPen.setWidthF(1.3);
        painter.setPen(majorPen);
        const double jLeft = std::floor(rect.left() / major) * major;
        const double jTop = std::floor(rect.top() / major) * major;
        for (double y = jTop; y <= rect.bottom(); y += major)
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        for (double x = jLeft; x <= rect.right(); x += major)
            painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    } else if (tpl == BackgroundTemplate::Isometric) {
        painter.setClipRect(rect, Qt::IntersectClip);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double dx = step * 0.86602540378;
        const double slope = 0.57735026919; // tan(30°)
        const double vLeft = std::floor(rect.left() / dx) * dx;
        for (double x = vLeft; x <= rect.right() + 1.0; x += dx)
            painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        const double cMin1 = std::floor((rect.top() - slope * rect.right()) / step) * step;
        const double cMax1 = std::ceil((rect.bottom() - slope * rect.left()) / step) * step;
        for (double c = cMin1; c <= cMax1; c += step)
            painter.drawLine(QPointF(rect.left(), slope * rect.left() + c),
                             QPointF(rect.right(), slope * rect.right() + c));
        const double cMin2 = std::floor((rect.top() + slope * rect.left()) / step) * step;
        const double cMax2 = std::ceil((rect.bottom() + slope * rect.right()) / step) * step;
        for (double c = cMin2; c <= cMax2; c += step)
            painter.drawLine(QPointF(rect.left(), -slope * rect.left() + c),
                             QPointF(rect.right(), -slope * rect.right() + c));
    } else if (tpl == BackgroundTemplate::Cornell) {
        const bool hasSheet = !sheetRect.isEmpty();
        const double cueX = hasSheet ? (sheetRect.left() + sheetRect.width() * 0.27) : 240.0;
        const double topY = hasSheet ? (sheetRect.top() + sheetRect.height() * 0.11) : 88.0;
        const double botY = hasSheet ? (sheetRect.bottom() - sheetRect.height() * 0.14) : 680.0;
        for (double y = top; y <= rect.bottom(); y += step) {
            if (y > topY && y < botY)
                painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
        QPen divPen(marginColor);
        divPen.setCosmetic(true);
        divPen.setWidthF(1.3);
        painter.setPen(divPen);
        if (cueX >= rect.left() && cueX <= rect.right()) {
            const double yA = std::max(rect.top(), topY);
            const double yB = std::min(rect.bottom(), botY);
            if (yB > yA)
                painter.drawLine(QPointF(cueX, yA), QPointF(cueX, yB));
        }
        if (topY >= rect.top() && topY <= rect.bottom())
            painter.drawLine(QPointF(rect.left(), topY), QPointF(rect.right(), topY));
        if (botY >= rect.top() && botY <= rect.bottom())
            painter.drawLine(QPointF(rect.left(), botY), QPointF(rect.right(), botY));
    } else if (tpl == BackgroundTemplate::TwoColumn) {
        QColor gridCol = color;
        gridCol.setAlpha(std::max(8, int(color.alpha() * 0.65)));
        QPen gridPen(gridCol);
        gridPen.setCosmetic(true);
        gridPen.setWidthF(1.0);
        painter.setPen(gridPen);
        for (double y = top; y <= rect.bottom(); y += step)
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        for (double x = left; x <= rect.right(); x += step)
            painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        const double midX = !sheetRect.isEmpty() ? sheetRect.center().x() : 640.0;
        if (midX >= rect.left() && midX <= rect.right()) {
            QColor divCol = theme.accent;
            divCol.setAlpha(theme.isDark ? 120 : 110);
            QPen divPen(divCol);
            divPen.setCosmetic(true);
            divPen.setWidthF(1.5);
            divPen.setStyle(Qt::DashLine);
            painter.setPen(divPen);
            painter.drawLine(QPointF(midX, rect.top()), QPointF(midX, rect.bottom()));
        }
    } else if (tpl == BackgroundTemplate::School) {
        const bool hasSheet = !sheetRect.isEmpty();
        const double marginX = hasSheet ? (sheetRect.left() + sheetRect.width() * 0.13) : 140.0;
        const double headerY = hasSheet ? (sheetRect.top() + sheetRect.height() * 0.10) : 88.0;
        for (double y = top; y <= rect.bottom(); y += step) {
            if (y > headerY + 6.0)
                painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
        QPen marginPen(marginColor);
        marginPen.setCosmetic(true);
        marginPen.setWidthF(1.3);
        painter.setPen(marginPen);
        if (marginX >= rect.left() && marginX <= rect.right())
            painter.drawLine(QPointF(marginX, rect.top()), QPointF(marginX, rect.bottom()));
        if (headerY >= rect.top() && headerY <= rect.bottom()) {
            painter.drawLine(QPointF(rect.left(), headerY), QPointF(rect.right(), headerY));
            const double secondY = headerY + 4.0 / std::max(0.4, vp.zoom);
            painter.drawLine(QPointF(rect.left(), secondY), QPointF(rect.right(), secondY));
        }
    } else {
        for (double y = top; y <= rect.bottom(); y += step)
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        if (tpl == BackgroundTemplate::Grid) {
            for (double x = left; x <= rect.right(); x += step)
                painter.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
    }
    painter.restore();
}

double inkPressureFactor(double pressure, PressureProfile profile) {
    const double s = (profile == PressureProfile::Light)
                         ? 0.35
                         : (profile == PressureProfile::Firm ? 0.85 : 0.50);
    return pressureMix(0.95, 0.20 + 1.18 * std::clamp(pressure, 0.0, 1.0), s);
}

} // namespace finily
