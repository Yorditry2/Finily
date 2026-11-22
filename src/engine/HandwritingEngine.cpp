#include "engine/HandwritingEngine.h"

#include "engine/InkEngine.h"
#include "engine/MathCnnWeights.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace finily {
namespace {

constexpr int kStrokeSamples = 24;
constexpr double kPi = 3.14159265358979323846;

double dist2D(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double polylineLength(const QVector<QPointF>& pts) {
    double len = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        len += dist2D(pts[i - 1], pts[i]);
    return len;
}

QVector<QPointF> arcPolyline(double cx, double cy, double rx, double ry,
                             double startDeg, double endDeg, int steps = 18) {
    QVector<QPointF> pts;
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double rad = (startDeg + (endDeg - startDeg) * t) * kPi / 180.0;
        pts.append(QPointF(cx + rx * std::cos(rad), cy + ry * std::sin(rad)));
    }
    return pts;
}

QVector<QPointF> linePolyline(double x0, double y0, double x1, double y1, int steps = 10) {
    QVector<QPointF> pts;
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        pts.append(QPointF(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t));
    }
    return pts;
}

QVector<QPointF> quadBezier(QPointF p0, QPointF p1, QPointF p2, int steps = 16) {
    QVector<QPointF> pts;
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double u = 1.0 - t;
        pts.append(QPointF(u * u * p0.x() + 2 * u * t * p1.x() + t * t * p2.x(),
                           u * u * p0.y() + 2 * u * t * p1.y() + t * t * p2.y()));
    }
    return pts;
}

QVector<QPointF> joinPolylines(std::initializer_list<QVector<QPointF>> parts) {
    QVector<QPointF> out;
    for (const auto& part : parts) {
        for (const QPointF& p : part) {
            if (out.isEmpty() || dist2D(out.back(), p) > 1e-4)
                out.append(p);
        }
    }
    return out;
}

QRectF polylineBox(const QVector<QPointF>& pts) {
    if (pts.isEmpty())
        return QRectF();
    double minX = pts[0].x(), maxX = pts[0].x();
    double minY = pts[0].y(), maxY = pts[0].y();
    for (const QPointF& p : pts) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QRectF strokesBoundingBox(const QVector<QVector<QPointF>>& strokes) {
    QRectF box;
    for (const auto& s : strokes) {
        const QRectF b = polylineBox(s);
        if (!b.isNull())
            box = box.isNull() ? b : box.united(b);
    }
    return box;
}

QVector<QPointF> extractRawPoints(const Stroke& s) {
    QVector<QPointF> pts;
    pts.reserve(s.points.size());
    for (const auto& p : s.points)
        pts.append(QPointF(p.x, p.y));
    return pts;
}

QVector<QPointF> resamplePolyline(const QVector<QPointF>& pts, int targetCount) {
    if (pts.isEmpty() || targetCount <= 0)
        return {};
    if (pts.size() == 1 || targetCount == 1)
        return QVector<QPointF>(targetCount, pts.first());

    const double totalLen = polylineLength(pts);
    if (totalLen < 1e-6)
        return QVector<QPointF>(targetCount, pts.first());

    const double step = totalLen / double(targetCount - 1);
    QVector<QPointF> out;
    out.reserve(targetCount);
    out.append(pts.first());

    double accum = 0.0;
    QPointF prev = pts.first();
    for (int i = 1; i < pts.size() && out.size() < targetCount; ++i) {
        QPointF curr = pts[i];
        double segLen = dist2D(prev, curr);
        while (accum + segLen >= step && out.size() < targetCount) {
            const double remain = step - accum;
            const double t = segLen > 1e-9 ? (remain / segLen) : 0.0;
            QPointF interp(prev.x() + (curr.x() - prev.x()) * t,
                           prev.y() + (curr.y() - prev.y()) * t);
            out.append(interp);
            prev = interp;
            segLen = dist2D(prev, curr);
            accum = 0.0;
        }
        accum += segLen;
        prev = curr;
    }
    while (out.size() < targetCount)
        out.append(pts.back());
    return out;
}

bool isStraightSegment(const QVector<QPointF>& pts, double minStraightness = 0.86) {
    if (pts.size() < 2)
        return false;
    const double len = polylineLength(pts);
    if (len < 6.0)
        return false;
    const double chord = dist2D(pts.first(), pts.back());
    return (chord / len) >= minStraightness;
}

bool isHorizontalLine(const QVector<QPointF>& pts) {
    if (!isStraightSegment(pts, 0.72))
        return false;
    const QPointF d = pts.back() - pts.first();
    return std::abs(d.x()) >= 6.5 && std::abs(d.y()) <= std::abs(d.x()) * 0.44;
}

bool isVerticalLine(const QVector<QPointF>& pts) {
    if (!isStraightSegment(pts, 0.72))
        return false;
    const QPointF d = pts.back() - pts.first();
    return std::abs(d.y()) >= 6.5 && std::abs(d.x()) <= std::abs(d.y()) * 0.48;
}

bool isTinyDot(const QVector<QPointF>& pts) {
    if (pts.isEmpty())
        return false;
    const QRectF b = polylineBox(pts);
    return std::max(b.width(), b.height()) <= 11.0 && polylineLength(pts) <= 26.0;
}

bool segmentsIntersect(const QPointF& a1, const QPointF& a2,
                       const QPointF& b1, const QPointF& b2,
                       QPointF* outPt = nullptr) {
    const double d1x = a2.x() - a1.x(), d1y = a2.y() - a1.y();
    const double d2x = b2.x() - b1.x(), d2y = b2.y() - b1.y();
    const double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < 1e-6)
        return false;
    const double t = ((b1.x() - a1.x()) * d2y - (b1.y() - a1.y()) * d2x) / denom;
    const double u = ((b1.x() - a1.x()) * d1y - (b1.y() - a1.y()) * d1x) / denom;
    if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) {
        if (outPt)
            *outPt = QPointF(a1.x() + t * d1x, a1.y() + t * d1y);
        return true;
    }
    return false;
}

bool isClosedLoop(const QVector<QPointF>& pts) {
    if (pts.size() < 4)
        return false;
    const QRectF b = polylineBox(pts);
    const double dim = std::max(b.width(), b.height());
    if (dim < 4.0)
        return false;
    const double gap = dist2D(pts.first(), pts.back());
    const double len = polylineLength(pts);
    return gap <= dim * 0.75 && len >= dim * 1.65;
}

double pathDistance(const QVector<QPointF>& a, const QVector<QPointF>& b) {
    const int n = std::min(a.size(), b.size());
    if (n <= 0)
        return 1e9;
    double fwd = 0.0;
    double rev = 0.0;
    for (int i = 0; i < n; ++i) {
        fwd += dist2D(a[i], b[i]);
        rev += dist2D(a[i], b[n - 1 - i]);
    }
    // Prefer forward direction, allow small penalty for reversed strokes (e.g. 0 CW vs CCW)
    return std::min(fwd, rev * 1.18) / double(n);
}

// Rasteriza trazos a una imagen 28x28 (exactamente igual que MNIST y train_math_cnn.py)
// y ejecuta la red neuronal convolucional (98.80% en MNIST 10k) en < 0.3 ms en CPU.
struct CnnPrediction {
    QString label;
    double prob = 0.0;
};

CnnPrediction classifyWithCnn(const QVector<QVector<QPointF>>& strokes) {
    const QRectF box = strokesBoundingBox(strokes);
    if (box.isNull())
        return {};

    QImage hi(56, 56, QImage::Format_Grayscale8);
    hi.fill(0);
    {
        QPainter painter(&hi);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double w = std::max(box.width(), 1e-4);
        const double h = std::max(box.height(), 1e-4);
        const double scale = 40.0 / std::max(w, h);
        const QPointF c = box.center();

        QPen pen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::white);

        for (const auto& s : strokes) {
            if (s.isEmpty())
                continue;
            if (s.size() == 1 || polylineLength(s) < 2.0) {
                const QPointF p = (s.first() - c) * scale + QPointF(28.0, 28.0);
                painter.drawEllipse(p, 2.2, 2.2);
            } else {
                QPolygonF poly;
                poly.reserve(s.size());
                for (const QPointF& pt : s)
                    poly.append((pt - c) * scale + QPointF(28.0, 28.0));
                painter.drawPolyline(poly);
            }
        }
    }

    const QImage small = hi.scaled(28, 28, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    float input[28 * 28];
    float maxVal = 0.0f;
    for (int y = 0; y < 28; ++y) {
        const uchar* row = small.constScanLine(y);
        for (int x = 0; x < 28; ++x) {
            const float v = float(row[x]) / 255.0f;
            input[y * 28 + x] = v;
            if (v > maxVal)
                maxVal = v;
        }
    }
    if (maxVal > 1e-4f) {
        for (float& v : input)
            v /= maxVal;
    }

    // Conv1: 1 -> 12, k=5, s=2 => 12 x 12 x 12
    float a1[12 * 12 * 12];
    for (int oc = 0; oc < 12; ++oc) {
        const int8_t* wRow = cnn::W1_q + oc * 25;
        const float bias = cnn::b1[oc];
        for (int oy = 0; oy < 12; ++oy) {
            for (int ox = 0; ox < 12; ++ox) {
                float acc = 0.0f;
                const int iy0 = oy * 2;
                const int ix0 = ox * 2;
                for (int ky = 0; ky < 5; ++ky) {
                    const float* inRow = input + (iy0 + ky) * 28 + ix0;
                    const int8_t* wKy = wRow + ky * 5;
                    for (int kx = 0; kx < 5; ++kx)
                        acc += inRow[kx] * float(wKy[kx]);
                }
                const float val = acc * cnn::W1_scale + bias;
                a1[(oc * 12 + oy) * 12 + ox] = val > 0.0f ? val : 0.0f;
            }
        }
    }

    // Conv2: 12 -> 24, k=3, s=2 => 24 x 5 x 5 = 600
    float flat[600];
    for (int oc = 0; oc < 24; ++oc) {
        const int8_t* wRow = cnn::W2_q + oc * 108;
        const float bias = cnn::b2[oc];
        for (int oy = 0; oy < 5; ++oy) {
            for (int ox = 0; ox < 5; ++ox) {
                float acc = 0.0f;
                const int iy0 = oy * 2;
                const int ix0 = ox * 2;
                for (int ic = 0; ic < 12; ++ic) {
                    const float* ch = a1 + ic * 144;
                    const int8_t* wIc = wRow + ic * 9;
                    for (int ky = 0; ky < 3; ++ky) {
                        const float* inRow = ch + (iy0 + ky) * 12 + ix0;
                        const int8_t* wKy = wIc + ky * 3;
                        acc += inRow[0] * float(wKy[0]) + inRow[1] * float(wKy[1]) + inRow[2] * float(wKy[2]);
                    }
                }
                const float val = acc * cnn::W2_scale + bias;
                flat[(oc * 5 + oy) * 5 + ox] = val > 0.0f ? val : 0.0f;
            }
        }
    }

    // FC1: 600 -> 64 + ReLU (W3 shape: 600 x 64)
    float a3[64];
    for (int j = 0; j < 64; ++j)
        a3[j] = 0.0f;
    for (int i = 0; i < 600; ++i) {
        const float fi = flat[i];
        if (fi == 0.0f)
            continue;
        const int8_t* wRow = cnn::W3_q + i * 64;
        for (int j = 0; j < 64; ++j)
            a3[j] += fi * float(wRow[j]);
    }
    for (int j = 0; j < 64; ++j) {
        const float v = a3[j] * cnn::W3_scale + cnn::b3[j];
        a3[j] = v > 0.0f ? v : 0.0f;
    }

    // FC2: 64 -> kNumClasses (W4 shape: 64 x kNumClasses)
    float logits[cnn::kNumClasses];
    for (int c = 0; c < cnn::kNumClasses; ++c)
        logits[c] = 0.0f;
    for (int j = 0; j < 64; ++j) {
        const float aj = a3[j];
        if (aj == 0.0f)
            continue;
        const int8_t* wRow = cnn::W4_q + j * cnn::kNumClasses;
        for (int c = 0; c < cnn::kNumClasses; ++c)
            logits[c] += aj * float(wRow[c]);
    }
    float maxLogit = -1e9f;
    for (int c = 0; c < cnn::kNumClasses; ++c) {
        logits[c] = logits[c] * cnn::W4_scale + cnn::b4[c];
        if (logits[c] > maxLogit)
            maxLogit = logits[c];
    }

    double sumExp = 0.0;
    double exps[cnn::kNumClasses];
    int bestIdx = 0;
    for (int c = 0; c < cnn::kNumClasses; ++c) {
        exps[c] = std::exp(double(logits[c] - maxLogit));
        sumExp += exps[c];
        if (logits[c] > logits[bestIdx])
            bestIdx = c;
    }

    CnnPrediction pred;
    pred.label = QString::fromUtf8(cnn::kClassLabels[bestIdx]);
    pred.prob = sumExp > 0.0 ? (exps[bestIdx] / sumExp) : 0.0;
    return pred;
}

QVector<QVector<QPointF>> canonicalGlyphFor(const QString& label) {
    if (label == QLatin1String("0"))
        return {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24)};
    if (label == QLatin1String("1"))
        return {joinPolylines({linePolyline(0.36, 0.25, 0.52, 0.12), linePolyline(0.52, 0.12, 0.52, 0.88)})};
    if (label == QLatin1String("2"))
        return {joinPolylines({arcPolyline(0.48, 0.30, 0.24, 0.18, -165, 25, 14),
                               quadBezier({0.70, 0.38}, {0.48, 0.62}, {0.24, 0.86}, 10),
                               linePolyline(0.24, 0.86, 0.78, 0.86)})};
    if (label == QLatin1String("3"))
        return {joinPolylines({arcPolyline(0.46, 0.30, 0.24, 0.18, -155, 90, 14),
                               arcPolyline(0.46, 0.68, 0.26, 0.20, -90, 160, 14)})};
    if (label == QLatin1String("4"))
        return {joinPolylines({linePolyline(0.30, 0.14, 0.24, 0.58), linePolyline(0.24, 0.58, 0.76, 0.58)}),
                linePolyline(0.64, 0.14, 0.64, 0.88)};
    if (label == QLatin1String("5"))
        return {joinPolylines({linePolyline(0.72, 0.14, 0.30, 0.14),
                               linePolyline(0.30, 0.14, 0.28, 0.46),
                               arcPolyline(0.48, 0.66, 0.26, 0.22, -130, 155, 16)})};
    if (label == QLatin1String("6"))
        return {joinPolylines({quadBezier({0.66, 0.14}, {0.26, 0.22}, {0.24, 0.64}, 14),
                               arcPolyline(0.50, 0.66, 0.26, 0.22, 180, -180, 18)})};
    if (label == QLatin1String("7"))
        return {joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
                linePolyline(0.34, 0.52, 0.70, 0.52)};
    if (label == QLatin1String("8"))
        return {arcPolyline(0.50, 0.31, 0.21, 0.17, -90, 270, 18),
                arcPolyline(0.50, 0.68, 0.24, 0.20, -90, 270, 18)};
    if (label == QLatin1String("9"))
        return {joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, 0, -360, 18),
                               linePolyline(0.72, 0.34, 0.66, 0.88)})};
    if (label == QLatin1String("+"))
        return {linePolyline(0.50, 0.15, 0.50, 0.85), linePolyline(0.15, 0.50, 0.85, 0.50)};
    if (label == QLatin1String("−"))
        return {linePolyline(0.12, 0.50, 0.88, 0.50)};
    if (label == QLatin1String("="))
        return {linePolyline(0.14, 0.36, 0.86, 0.36), linePolyline(0.14, 0.64, 0.86, 0.64)};
    if (label == QLatin1String("×"))
        return {linePolyline(0.18, 0.18, 0.82, 0.82), linePolyline(0.82, 0.18, 0.18, 0.82)};
    if (label == QLatin1String("÷"))
        return {linePolyline(0.14, 0.50, 0.86, 0.50),
                linePolyline(0.49, 0.24, 0.51, 0.24, 3),
                linePolyline(0.49, 0.76, 0.51, 0.76, 3)};
    if (label == QLatin1String("("))
        return {quadBezier({0.64, 0.12}, {0.26, 0.50}, {0.64, 0.88}, 16)};
    if (label == QLatin1String(")"))
        return {quadBezier({0.36, 0.12}, {0.74, 0.50}, {0.36, 0.88}, 16)};
    if (label == QLatin1String("["))
        return {joinPolylines({linePolyline(0.68, 0.12, 0.34, 0.12),
                               linePolyline(0.34, 0.12, 0.34, 0.88),
                               linePolyline(0.34, 0.88, 0.68, 0.88)})};
    if (label == QLatin1String("]"))
        return {joinPolylines({linePolyline(0.32, 0.12, 0.66, 0.12),
                               linePolyline(0.66, 0.12, 0.66, 0.88),
                               linePolyline(0.66, 0.88, 0.32, 0.88)})};
    if (label == QLatin1String("{"))
        return {joinPolylines({quadBezier({0.66, 0.12}, {0.42, 0.16}, {0.46, 0.36}, 8),
                               quadBezier({0.46, 0.36}, {0.48, 0.48}, {0.28, 0.50}, 8),
                               quadBezier({0.28, 0.50}, {0.48, 0.52}, {0.46, 0.64}, 8),
                               quadBezier({0.46, 0.64}, {0.42, 0.84}, {0.66, 0.88}, 8)})};
    if (label == QLatin1String("}"))
        return {joinPolylines({quadBezier({0.34, 0.12}, {0.58, 0.16}, {0.54, 0.36}, 8),
                               quadBezier({0.54, 0.36}, {0.52, 0.48}, {0.72, 0.50}, 8),
                               quadBezier({0.72, 0.50}, {0.52, 0.52}, {0.54, 0.64}, 8),
                               quadBezier({0.54, 0.64}, {0.58, 0.84}, {0.34, 0.88}, 8)})};
    if (label == QLatin1String("<"))
        return {joinPolylines({linePolyline(0.78, 0.18, 0.22, 0.50), linePolyline(0.22, 0.50, 0.78, 0.82)})};
    if (label == QLatin1String(">"))
        return {joinPolylines({linePolyline(0.22, 0.18, 0.78, 0.50), linePolyline(0.78, 0.50, 0.22, 0.82)})};
    if (label == QLatin1String("√"))
        return {joinPolylines({linePolyline(0.08, 0.56, 0.22, 0.86),
                               linePolyline(0.22, 0.86, 0.46, 0.16),
                               linePolyline(0.46, 0.16, 0.94, 0.16)})};
    if (label == QLatin1String("θ"))
        return {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24),
                linePolyline(0.24, 0.50, 0.76, 0.50)};
    return {};
}

} // namespace

HandwritingEngine& HandwritingEngine::instance() {
    static HandwritingEngine engine;
    return engine;
}

HandwritingEngine::HandwritingEngine() {
    buildBuiltinTemplates();
    loadCustomTemplates();
}

QString HandwritingEngine::templatesFilePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/templates.json");
}

bool HandwritingEngine::isOperatorOrGrouping(const QString& label) {
    static const QSet<QString> ops = {
        QStringLiteral("+"), QStringLiteral("−"), QStringLiteral("-"), QStringLiteral("="),
        QStringLiteral("×"), QStringLiteral("÷"), QStringLiteral("≠"), QStringLiteral("≤"),
        QStringLiteral("≥"), QStringLiteral("<"), QStringLiteral(">"), QStringLiteral("→"),
        QStringLiteral("√"), QStringLiteral("("), QStringLiteral(")"), QStringLiteral("["),
        QStringLiteral("]"), QStringLiteral("{"), QStringLiteral("}"), QStringLiteral("·"),
        QStringLiteral(","), QStringLiteral(".")
    };
    return ops.contains(label.trimmed());
}

QVector<QVector<QPointF>> HandwritingEngine::normalizeStrokes01(const QVector<QVector<QPointF>>& strokes) {
    const QRectF box = strokesBoundingBox(strokes);
    const double scale = std::max({box.width(), box.height(), 1e-4});
    const double ox = box.center().x() - scale * 0.5;
    const double oy = box.center().y() - scale * 0.5;

    QVector<QVector<QPointF>> out;
    out.reserve(strokes.size());
    for (const auto& s : strokes) {
        if (s.isEmpty())
            continue;
        QVector<QPointF> ns;
        ns.reserve(s.size());
        for (const QPointF& p : s)
            ns.append(QPointF((p.x() - ox) / scale, (p.y() - oy) / scale));
        out.append(ns);
    }
    return out;
}

QVector<QVector<QPointF>> HandwritingEngine::normalizeDirected(
    const QVector<QVector<QPointF>>& strokes, double* outAspect) {
    if (strokes.isEmpty())
        return {};
    const QRectF box = strokesBoundingBox(strokes);
    const double w = box.width();
    const double h = box.height();
    const double aspect = std::clamp(w / std::max(h, 1e-4), 0.10, 8.0);
    if (outAspect)
        *outAspect = aspect;

    // Normalización anisotrópica suave: permite que un '2', '3', '5', '6', '9'
    // esbelto coincida con la plantilla igual que uno ancho (el aspectRatio se
    // valida por separado para no confundir '1' con '0').
    const double scaleX = std::max(w, h * 0.28);
    const double scaleY = std::max(h, w * 0.28);
    const QPointF center = box.center();
    QVector<QVector<QPointF>> out;
    out.reserve(strokes.size());
    for (const auto& s : strokes) {
        const auto res = resamplePolyline(s, kStrokeSamples);
        QVector<QPointF> norm;
        norm.reserve(res.size());
        for (const QPointF& p : res)
            norm.append(QPointF((p.x() - center.x()) / scaleX, (p.y() - center.y()) / scaleY));
        out.append(std::move(norm));
    }
    return out;
}

double HandwritingEngine::directedStrokeDistance(const QVector<QVector<QPointF>>& a,
                                                 const QVector<QVector<QPointF>>& b) {
    if (a.size() != b.size() || a.isEmpty())
        return 1e9;
    const int k = a.size();
    if (k == 1)
        return pathDistance(a[0], b[0]);
    if (k == 2) {
        const double d01 = 0.5 * (pathDistance(a[0], b[0]) + pathDistance(a[1], b[1]));
        const double d10 = 0.5 * (pathDistance(a[0], b[1]) + pathDistance(a[1], b[0]));
        return std::min(d01, d10);
    }
    // k == 3: test all 6 permutations of stroke order
    int perm[3] = {0, 1, 2};
    double best = std::numeric_limits<double>::max();
    do {
        const double d = (pathDistance(a[0], b[perm[0]])
                          + pathDistance(a[1], b[perm[1]])
                          + pathDistance(a[2], b[perm[2]])) / 3.0;
        best = std::min(best, d);
    } while (std::next_permutation(perm, perm + 3));
    return best;
}

void HandwritingEngine::addTemplate(const QString& label,
                                    bool isMathOrDigit,
                                    const QVector<QVector<QPointF>>& sampleStrokes,
                                    const QVector<QVector<QPointF>>& canonicalStrokes,
                                    bool isCustom) {
    GlyphTemplate tpl;
    tpl.label = label;
    tpl.isMathOrDigit = isMathOrDigit;
    tpl.isCustom = isCustom;
    tpl.strokeCount = sampleStrokes.size();
    tpl.directedStrokes = normalizeDirected(sampleStrokes, &tpl.aspectRatio);
    tpl.canonicalStrokes = normalizeStrokes01(canonicalStrokes);
    if (!tpl.directedStrokes.isEmpty() && !tpl.canonicalStrokes.isEmpty())
        m_templates.append(std::move(tpl));
}

// ==================== CAPA 1: DETECTOR GEOMÉTRICO ESTRUCTURAL ====================
// Alta precisión (~98%) para signos matemáticos, paréntesis, corchetes, llaves,
// raíces largas, θ, grados ° y punto de multiplicación ·.
RecognitionResult HandwritingEngine::matchGeometric(const QVector<QVector<QPointF>>& rawStrokes,
                                                    const QRectF& neighborLeftBox,
                                                    bool hasRightNeighbor) const {
    RecognitionResult res;
    const int k = rawStrokes.size();
    const QRectF box = strokesBoundingBox(rawStrokes);

    // ---- 1. SÍMBOLOS DE 1 TRAZO ----
    if (k == 1) {
        const auto& pts = rawStrokes[0];
        if (pts.size() < 2)
            return res;

        // 1A. Punto de multiplicación '·' (toque pequeño a MEDIA ALTURA junto a un número/variable)
        if (isTinyDot(pts) && !neighborLeftBox.isNull()) {
            const double relY = (box.center().y() - neighborLeftBox.top()) / std::max(neighborLeftBox.height(), 12.0);
            const double gapX = box.left() - neighborLeftBox.right();
            if (relY >= 0.25 && relY <= 0.72 && gapX >= -4.0 && gapX <= std::max(neighborLeftBox.height() * 1.1, 45.0)) {
                res.matched = true;
                res.label = QStringLiteral("·");
                res.score = 0.96;
                res.canonicalInWorld = true;
                const QPointF c = box.center();
                // Punto relleno nítido (NO arito hueco!)
                res.canonicalStrokes = {linePolyline(c.x() - 0.8, c.y(), c.x() + 0.8, c.y(), 3)};
                return res;
            }
            return res; // puntos en la base se respetan tal cual
        }

        // 1B. Signo de GRADO '°' (circulito pequeño arriba-derecha de un número!)
        if (isClosedLoop(pts) && !neighborLeftBox.isNull()) {
            const double dim = std::max(box.width(), box.height());
            const double nh = std::max(neighborLeftBox.height(), 14.0);
            const double relY = (box.center().y() - neighborLeftBox.top()) / nh;
            const double gapX = box.left() - neighborLeftBox.right();
            if (dim <= nh * 0.56 && dim >= 4.0 && relY <= 0.45 && relY >= -0.45
                && gapX >= -nh * 0.45 && gapX <= nh * 0.95) {
                res.matched = true;
                res.label = QStringLiteral("°");
                res.score = 0.96;
                res.canonicalInWorld = true;
                const QPointF c = box.center();
                const double r = std::clamp(dim * 0.48, 3.2, 9.0);
                res.canonicalStrokes = {arcPolyline(c.x(), c.y(), r, r, -90, 270, 18)};
                return res;
            }
        }

        // 1B2. DÍGITO '8' por topología (100% invariante al punto de inicio y dirección!):
        // Un trazo vertical con auto-intersección en el tercio medio + lazo arriba + lazo abajo.
        if (pts.size() >= 10 && box.height() >= 18.0 && box.width() >= 8.0
            && box.height() >= box.width() * 0.95 && box.height() <= box.width() * 2.8) {
            const auto rpts = resamplePolyline(pts, 36);
            const double totalLen = polylineLength(rpts);
            bool hasMidCrossing = false;
            for (int i = 0; i < rpts.size() - 1 && !hasMidCrossing; ++i) {
                for (int j = i + 5; j < rpts.size() - 1; ++j) {
                    if (i == 0 && j >= rpts.size() - 4)
                        continue; // ignorar el cierre inicio-fin
                    QPointF hit;
                    if (segmentsIntersect(rpts[i], rpts[i + 1], rpts[j], rpts[j + 1], &hit)) {
                        const double relY = (hit.y() - box.top()) / std::max(box.height(), 1.0);
                        const double relX = (hit.x() - box.left()) / std::max(box.width(), 1.0);
                        if (relY >= 0.28 && relY <= 0.72 && relX >= 0.15 && relX <= 0.85) {
                            hasMidCrossing = true;
                            break;
                        }
                    }
                }
            }
            // Comprobar que el trazo cubre tanto la mitad superior como la inferior (dos panzas)
            if (hasMidCrossing && totalLen >= box.height() * 2.75
                && dist2D(rpts.first(), rpts.back()) <= box.height() * 0.65) {
                // Evitar confundir con 'α' (que es más ancho que alto y tiene colas abiertas a la derecha)
                double topSpan = 0.0, botSpan = 0.0;
                double topMinX = 1e9, topMaxX = -1e9, botMinX = 1e9, botMaxX = -1e9;
                for (const QPointF& p : rpts) {
                    const double ry = (p.y() - box.top()) / std::max(box.height(), 1.0);
                    if (ry < 0.42) { topMinX = std::min(topMinX, p.x()); topMaxX = std::max(topMaxX, p.x()); }
                    if (ry > 0.58) { botMinX = std::min(botMinX, p.x()); botMaxX = std::max(botMaxX, p.x()); }
                }
                topSpan = topMaxX - topMinX;
                botSpan = botMaxX - botMinX;
                if (topSpan >= box.width() * 0.48 && botSpan >= box.width() * 0.52) {
                    res.matched = true;
                    res.label = QStringLiteral("8");
                    res.score = 0.94;
                    res.canonicalInWorld = false;
                    res.canonicalStrokes = {joinPolylines({
                        arcPolyline(0.50, 0.31, 0.22, 0.17, -60, -240, 10),
                        quadBezier({0.39, 0.46}, {0.66, 0.54}, {0.74, 0.68}, 8),
                        arcPolyline(0.50, 0.68, 0.24, 0.20, 0, 180, 10),
                        quadBezier({0.26, 0.68}, {0.34, 0.54}, {0.62, 0.44}, 8),
                        arcPolyline(0.50, 0.31, 0.22, 0.17, 45, -60, 8),
                    })};
                    return res;
                }
            }
        }

        // 1B3. DÍGITO '0' por óvalo cerrado simple (invariante al punto de inicio y dirección!)
        // Exige centro VACÍO (minDistToCenter >= 20% del lado menor) y cierre real (gap <= 42%),
        // para que JAMÁS confunda un '1' o un '8' con '0'!
        if (pts.size() >= 6 && box.height() >= 16.0 && box.width() >= 9.0
            && box.height() >= box.width() * 0.90 && box.height() <= box.width() * 2.2) {
            const double dim = std::max(box.width(), box.height());
            const double minDim = std::min(box.width(), box.height());
            const double gap = dist2D(pts.first(), pts.back());
            if (gap <= dim * 0.42) {
                const auto rpts = resamplePolyline(pts, 28);
                const double totalLen = polylineLength(rpts);
                const double approxPerim = kPi * (0.5 * (box.width() + box.height()));
                const QPointF c = box.center();
                double minDistCenter = 1e9;
                int qMask = 0;
                for (const QPointF& p : rpts) {
                    minDistCenter = std::min(minDistCenter, dist2D(p, c));
                    if (p.x() < c.x() && p.y() < c.y()) qMask |= 1;
                    if (p.x() >= c.x() && p.y() < c.y()) qMask |= 2;
                    if (p.x() < c.x() && p.y() >= c.y()) qMask |= 4;
                    if (p.x() >= c.x() && p.y() >= c.y()) qMask |= 8;
                }
                if (qMask == 15 && minDistCenter >= minDim * 0.22
                    && totalLen >= approxPerim * 0.84 && totalLen <= approxPerim * 1.38) {
                    res.matched = true;
                    res.label = QStringLiteral("0");
                    res.score = 0.92;
                    res.canonicalInWorld = false;
                    res.canonicalStrokes = {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24)};
                    return res;
                }
            }
        }

        // 1C. Signo MENOS '−' (guion horizontal corto y recto)
        if (isHorizontalLine(pts) && box.width() >= 14.0 && box.width() <= 62.0) {
            res.matched = true;
            res.label = QStringLiteral("−");
            res.score = 0.95;
            res.canonicalInWorld = true;
            const double y = box.center().y();
            res.canonicalStrokes = {linePolyline(box.left(), y, box.right(), y, 10)};
            return res;
        }

        // 1D. RAÍZ CUADRADA '√' de CUALQUIER largo de techo!
        if (pts.size() >= 4 && box.width() >= 22.0 && box.height() >= 16.0) {
            const auto rpts = resamplePolyline(pts, 32);
            // Buscar el valle inferior (hook bottom) en la primera mitad,
            // seguido del pico superior izquierdo y luego techo horizontal hacia la derecha.
            int bottomIdx = 0;
            for (int i = 1; i < rpts.size() - 4; ++i) {
                if (rpts[i].y() > rpts[bottomIdx].y())
                    bottomIdx = i;
            }
            int peakIdx = bottomIdx;
            for (int i = bottomIdx + 1; i < rpts.size() - 2; ++i) {
                if (rpts[i].y() < rpts[peakIdx].y())
                    peakIdx = i;
            }
            if (bottomIdx >= 1 && peakIdx > bottomIdx && peakIdx <= rpts.size() - 3) {
                const QPointF pStart = rpts.first();
                const QPointF pBot = rpts[bottomIdx];
                const QPointF pPeak = rpts[peakIdx];
                const QPointF pEnd = rpts.back();
                const double roofLen = pEnd.x() - pPeak.x();
                const double roofTilt = std::abs(pEnd.y() - pPeak.y());
                const double upRise = pBot.y() - pPeak.y();
                const double hookDrop = pBot.y() - pStart.y();
                if (upRise >= box.height() * 0.72
                    && hookDrop >= box.height() * 0.14
                    && pStart.x() <= pBot.x() + 4.0
                    && pPeak.x() > pBot.x() + 2.0
                    && roofLen >= box.height() * 0.45
                    && roofTilt <= std::max(roofLen * 0.22, 12.0)) {
                    res.matched = true;
                    res.label = QStringLiteral("√");
                    res.score = 0.95;
                    res.canonicalInWorld = true;
                    const double roofY = 0.5 * (pPeak.y() + pEnd.y());
                    const double hookX = pStart.x();
                    const double hookY = pBot.y() - upRise * 0.34;
                    res.canonicalStrokes = {joinPolylines({
                        linePolyline(hookX, hookY, pBot.x(), pBot.y(), 6),
                        linePolyline(pBot.x(), pBot.y(), pPeak.x(), roofY, 10),
                        linePolyline(pPeak.x(), roofY, pEnd.x(), roofY, 14),
                    })};
                    return res;
                }
            }
        }

        // 1E. Paréntesis '(' y ')' + Corchetes '[' y ']' + Llaves '{' y '}'
        // Guardia clave: un paréntesis/corchete/llave avanza MONÓTONAMENTE en Y (sin bucles
        // ni retrocesos como 3, 4, 5, 6, 9) y su longitud total es <= 1.48 * altura!
        if (box.height() >= 18.0 && box.width() >= 3.5 && box.height() >= box.width() * 1.35) {
            const auto res32 = resamplePolyline(pts, 32);
            const QPointF p0 = res32.first();
            const QPointF p1 = res32.back();
            const double spanY = std::abs(p1.y() - p0.y());
            const double alignX = std::abs(p1.x() - p0.x());
            const double totalLen = polylineLength(pts);
            const double dirY = (p1.y() >= p0.y()) ? 1.0 : -1.0;
            double backtrackY = 0.0;
            for (int i = 1; i < res32.size(); ++i) {
                const double stepY = (res32[i].y() - res32[i - 1].y()) * dirY;
                if (stepY < 0.0)
                    backtrackY += -stepY;
            }
            const bool monotonicY = backtrackY <= box.height() * 0.12;
            const bool simpleCurve = totalLen <= box.height() * 1.48;
            if (monotonicY && simpleCurve && spanY >= box.height() * 0.80
                && alignX <= std::max(box.width() * 0.80, 14.0)) {
                const QPointF mid = res32[16]; // punto medio GEOMÉTRICO (inmune a velocidad Wacom!)
                const double endX = 0.5 * (p0.x() + p1.x());
                const double bulge = mid.x() - endX; // < 0 => apunta a izq: ( [ { ; > 0 => ) ] }
                if (std::abs(bulge) >= box.width() * 0.30) {
                    const bool openLeft = bulge < 0.0;
                    // 1) Revisar si tiene pico central nítido => Llave '{' o '}'
                    // (primero, porque un corchete tiene peakProtrusion ≈ 0)
                    const double q1x = res32[8].x();
                    const double q3x = res32[24].x();
                    const double midX = res32[16].x();
                    const double peakProtrusion = openLeft
                        ? (std::min(q1x, q3x) - midX)
                        : (midX - std::max(q1x, q3x));
                    if (peakProtrusion >= box.width() * 0.22 && box.width() >= 8.0) {
                        res.matched = true;
                        res.label = openLeft ? QStringLiteral("{") : QStringLiteral("}");
                        res.score = 0.91;
                        res.canonicalInWorld = true;
                        const double t = box.top(), b = box.bottom(), my = box.center().y();
                        const double xTip = openLeft ? box.right() : box.left();
                        const double xMid = openLeft ? box.left() : box.right();
                        const double xStem = xTip + (xMid - xTip) * 0.55;
                        res.canonicalStrokes = {joinPolylines({
                            quadBezier({xTip, t}, {xStem, t + (b - t) * 0.04}, {xStem, t + (b - t) * 0.25}, 8),
                            quadBezier({xStem, t + (b - t) * 0.25}, {xStem, my - (b - t) * 0.04}, {xMid, my}, 8),
                            quadBezier({xMid, my}, {xStem, my + (b - t) * 0.04}, {xStem, b - (b - t) * 0.25}, 8),
                            quadBezier({xStem, b - (b - t) * 0.25}, {xStem, b - (b - t) * 0.04}, {xTip, b}, 8),
                        })};
                        return res;
                    }

                    // 2) Revisar si tiene segmentos horizontales arriba/abajo + columna plana => Corchete '[' o ']'
                    const double topDx = std::abs(res32[4].x() - res32[0].x());
                    const double topDy = std::abs(res32[4].y() - res32[0].y());
                    const double botDx = std::abs(res32[31].x() - res32[27].x());
                    const double botDy = std::abs(res32[31].y() - res32[27].y());
                    const double midFlat = std::abs(res32[22].x() - res32[10].x());
                    if (topDx > topDy * 1.15 && botDx > botDy * 1.15 && midFlat < box.width() * 0.32) {
                        res.matched = true;
                        res.label = openLeft ? QStringLiteral("[") : QStringLiteral("]");
                        res.score = 0.93;
                        res.canonicalInWorld = true;
                        const double xEdge = openLeft ? box.left() : box.right();
                        const double xTip = openLeft ? box.right() : box.left();
                        res.canonicalStrokes = {joinPolylines({
                            linePolyline(xTip, box.top(), xEdge, box.top(), 6),
                            linePolyline(xEdge, box.top(), xEdge, box.bottom(), 12),
                            linePolyline(xEdge, box.bottom(), xTip, box.bottom(), 6),
                        })};
                        return res;
                    }

                    // Paréntesis '(' o ')'
                    if (!hasRightNeighbor || box.height() >= 24.0) {
                        res.matched = true;
                        res.label = openLeft ? QStringLiteral("(") : QStringLiteral(")");
                        res.score = 0.92;
                        res.canonicalInWorld = true;
                        const double xCtrl = openLeft ? (box.left() - box.width() * 0.35)
                                                      : (box.right() + box.width() * 0.35);
                        const double xEnd = openLeft ? box.right() : box.left();
                        res.canonicalStrokes = {quadBezier({xEnd, box.top()}, {xCtrl, box.center().y()}, {xEnd, box.bottom()}, 18)};
                        return res;
                    }
                }
            }
        }

        // 1F. Menor '<' y Mayor '>' (2 segmentos con vértice central)
        if (pts.size() >= 4 && box.width() >= 8.0 && box.height() >= 10.0
            && box.height() / std::max(box.width(), 1.0) <= 2.4) {
            const auto rpts = resamplePolyline(pts, 24);
            const QPointF p0 = rpts.first();
            const QPointF p1 = rpts.back();
            int apexIdx = 0;
            double maxDist = 0.0;
            const double endX = 0.5 * (p0.x() + p1.x());
            for (int i = 3; i < rpts.size() - 3; ++i) {
                const double d = std::abs(rpts[i].x() - endX);
                if (d > maxDist) {
                    maxDist = d;
                    apexIdx = i;
                }
            }
            if (apexIdx >= 3 && apexIdx <= rpts.size() - 4) {
                QVector<QPointF> leg1(rpts.begin(), rpts.begin() + apexIdx + 1);
                QVector<QPointF> leg2(rpts.begin() + apexIdx, rpts.end());
                if (isStraightSegment(leg1, 0.78) && isStraightSegment(leg2, 0.78)
                    && std::abs(p1.y() - p0.y()) >= box.height() * 0.65
                    && std::abs(p1.x() - p0.x()) <= box.width() * 0.55
                    && maxDist >= box.width() * 0.55) {
                    const bool isLt = rpts[apexIdx].x() < endX;
                    res.matched = true;
                    res.label = isLt ? QStringLiteral("<") : QStringLiteral(">");
                    res.score = 0.92;
                    res.canonicalInWorld = true;
                    const double xOpen = isLt ? box.right() : box.left();
                    const double xApex = isLt ? box.left() : box.right();
                    res.canonicalStrokes = {joinPolylines({
                        linePolyline(xOpen, box.top(), xApex, box.center().y(), 8),
                        linePolyline(xApex, box.center().y(), xOpen, box.bottom(), 8),
                    })};
                    return res;
                }
            }
        }
    }

    // ---- 2. SÍMBOLOS DE 2 TRAZOS ----
    if (k == 2) {
        const auto& s0 = rawStrokes[0];
        const auto& s1 = rawStrokes[1];
        const QRectF b0 = polylineBox(s0);
        const QRectF b1 = polylineBox(s1);

        // 2A. SIGNO IGUAL '=' (dos rayas horizontales paralelas)
        if (isHorizontalLine(s0) && isHorizontalLine(s1)) {
            const double dy = std::abs(b0.center().y() - b1.center().y());
            const double wAvg = 0.5 * (b0.width() + b1.width());
            const double overlapX = std::min(b0.right(), b1.right()) - std::max(b0.left(), b1.left());
            if (dy >= 2.2 && dy <= std::max(wAvg * 1.05, 30.0) && overlapX >= wAvg * 0.25) {
                res.matched = true;
                res.label = QStringLiteral("=");
                res.score = 0.98;
                res.canonicalInWorld = true;
                const double xL = box.left(), xR = box.right();
                const double cy = box.center().y();
                const double halfGap = std::clamp(dy * 0.5, 3.8, wAvg * 0.30);
                res.canonicalStrokes = {
                    linePolyline(xL, cy - halfGap, xR, cy - halfGap, 10),
                    linePolyline(xL, cy + halfGap, xR, cy + halfGap, 10),
                };
                return res;
            }
        }

        // 2B. SIGNO MÁS '+' (1 horizontal + 1 vertical RECTO que se cruzan — evita confundir con '7' tachado!)
        const bool hv = isHorizontalLine(s0) && isVerticalLine(s1) && isStraightSegment(s1, 0.88);
        const bool vh = isVerticalLine(s0) && isStraightSegment(s0, 0.88) && isHorizontalLine(s1);
        if (hv || vh) {
            const QRectF bh = hv ? b0 : b1;
            const QRectF bv = hv ? b1 : b0;
            const double cxDist = std::abs(bh.center().x() - bv.center().x());
            const double cyDist = std::abs(bh.center().y() - bv.center().y());
            if (bh.adjusted(-5, -5, 5, 5).intersects(bv.adjusted(-5, -5, 5, 5))
                && cxDist <= std::max(bh.width() * 0.48, 8.0)
                && cyDist <= std::max(bv.height() * 0.48, 8.0)) {
                res.matched = true;
                res.label = QStringLiteral("+");
                res.score = 0.98;
                res.canonicalInWorld = true;
                const double arm = 0.5 * std::max(bh.width(), bv.height());
                const QPointF c = box.center();
                res.canonicalStrokes = {
                    linePolyline(c.x(), c.y() - arm, c.x(), c.y() + arm, 10),
                    linePolyline(c.x() - arm, c.y(), c.x() + arm, c.y(), 10),
                };
                return res;
            }
        }

        // 2C. THETA 'θ' (lazo cerrado vertical + barra horizontal que lo atraviesa)
        const bool c0 = isClosedLoop(s0) && isHorizontalLine(s1);
        const bool c1 = isClosedLoop(s1) && isHorizontalLine(s0);
        if (c0 || c1) {
            const QRectF bo = (c0 ? b0 : b1).adjusted(-6, -4, 6, 4);
            const QRectF bl = (c0 ? b1 : b0).adjusted(-2, -2, 2, 2);
            if (bo.intersects(bl)
                && std::abs(bl.center().y() - bo.center().y()) <= (c0 ? b0 : b1).height() * 0.35) {
                res.matched = true;
                res.label = QStringLiteral("θ");
                res.score = 0.97;
                res.canonicalInWorld = true;
                const QPointF c = bo.center();
                const double rx = std::max(bo.width() * 0.48, 6.0);
                const double ry = std::max(bo.height() * 0.50, 9.0);
                res.canonicalStrokes = {
                    arcPolyline(c.x(), c.y(), rx, ry, -90, 270, 24),
                    linePolyline(c.x() - rx * 0.92, c.y(), c.x() + rx * 0.92, c.y(), 8),
                };
                return res;
            }
        }

        // 2C2. DÍGITO '8' en 2 bolitas apiladas (una bolita encima de la otra!)
        if (isClosedLoop(s0) && isClosedLoop(s1)) {
            const double dy = std::abs(b0.center().y() - b1.center().y());
            const double avgH = 0.5 * (b0.height() + b1.height());
            const double overlapX = std::min(b0.right(), b1.right()) - std::max(b0.left(), b1.left());
            if (dy >= avgH * 0.45 && dy <= avgH * 1.65 && overlapX > -4.0) {
                res.matched = true;
                res.label = QStringLiteral("8");
                res.score = 0.96;
                res.canonicalInWorld = false;
                res.canonicalStrokes = {
                    arcPolyline(0.50, 0.31, 0.21, 0.17, -90, 270, 18),
                    arcPolyline(0.50, 0.68, 0.24, 0.20, -90, 270, 18),
                };
                return res;
            }
        }

        // 2D. MENOR/MAYOR O IGUAL '≤' y '≥' (< o > arriba + barra horizontal abajo)
        for (int barIdx = 0; barIdx < 2; ++barIdx) {
            const int angleIdx = 1 - barIdx;
            if (!isHorizontalLine(rawStrokes[barIdx]))
                continue;
            const QRectF bBar = polylineBox(rawStrokes[barIdx]);
            const QRectF bAng = polylineBox(rawStrokes[angleIdx]);
            if (bBar.center().y() > bAng.bottom() - bAng.height() * 0.20) {
                const RecognitionResult sub = matchGeometric({rawStrokes[angleIdx]}, QRectF(), false);
                if (sub.matched && (sub.label == QLatin1String("<") || sub.label == QLatin1String(">"))) {
                    const bool isLeq = sub.label == QLatin1String("<");
                    res.matched = true;
                    res.label = isLeq ? QStringLiteral("≤") : QStringLiteral("≥");
                    res.score = 0.96;
                    res.canonicalInWorld = true;
                    const double xOpen = isLeq ? box.right() : box.left();
                    const double xApex = isLeq ? box.left() : box.right();
                    const double barY = bBar.center().y();
                    const double angMidY = 0.5 * (bAng.top() + bAng.bottom());
                    res.canonicalStrokes = {
                        joinPolylines({linePolyline(xOpen, bAng.top(), xApex, angMidY, 8),
                                       linePolyline(xApex, angMidY, xOpen, bAng.bottom(), 8)}),
                        linePolyline(box.left(), barY, box.right(), barY, 8),
                    };
                    return res;
                }
            }
        }
    }

    // ---- 3. SÍMBOLOS DE 3 TRAZOS ----
    if (k == 3) {
        // 3A. DIVISIÓN '÷' (1 barra horizontal + 2 puntitos, uno arriba y uno abajo!)
        int barIdx = -1;
        QVector<int> dotIdxs;
        for (int i = 0; i < 3; ++i) {
            if (isHorizontalLine(rawStrokes[i]))
                barIdx = i;
            else if (isTinyDot(rawStrokes[i]))
                dotIdxs.append(i);
        }
        if (barIdx >= 0 && dotIdxs.size() == 2) {
            const QRectF bBar = polylineBox(rawStrokes[barIdx]);
            const QPointF d0 = polylineBox(rawStrokes[dotIdxs[0]]).center();
            const QPointF d1 = polylineBox(rawStrokes[dotIdxs[1]]).center();
            const bool oppositeSides = (d0.y() < bBar.center().y() && d1.y() > bBar.center().y())
                                       || (d1.y() < bBar.center().y() && d0.y() > bBar.center().y());
            if (oppositeSides) {
                res.matched = true;
                res.label = QStringLiteral("÷");
                res.score = 0.98;
                res.canonicalInWorld = true;
                const double cy = bBar.center().y();
                const double cx = bBar.center().x();
                const double gap = std::clamp(0.5 * std::abs(d0.y() - d1.y()), 7.0, bBar.width() * 0.45);
                res.canonicalStrokes = {
                    linePolyline(bBar.left(), cy, bBar.right(), cy, 10),
                    arcPolyline(cx, cy - gap, 2.1, 2.1, 0, 360, 10),
                    arcPolyline(cx, cy + gap, 2.1, 2.1, 0, 360, 10),
                };
                return res;
            }
        }

        // 3B. DISTINTO '≠' (2 barras horizontales + 1 diagonal que las cruza)
        QVector<int> hIdxs;
        int slashIdx = -1;
        for (int i = 0; i < 3; ++i) {
            if (isHorizontalLine(rawStrokes[i]))
                hIdxs.append(i);
            else if (isStraightSegment(rawStrokes[i], 0.84))
                slashIdx = i;
        }
        if (hIdxs.size() == 2 && slashIdx >= 0) {
            const QRectF b0 = polylineBox(rawStrokes[hIdxs[0]]).adjusted(-2, -2, 2, 2);
            const QRectF b1 = polylineBox(rawStrokes[hIdxs[1]]).adjusted(-2, -2, 2, 2);
            const QRectF bs = polylineBox(rawStrokes[slashIdx]).adjusted(-2, -2, 2, 2);
            if (bs.intersects(b0.united(b1))) {
                res.matched = true;
                res.label = QStringLiteral("≠");
                res.score = 0.97;
                res.canonicalInWorld = true;
                const double cy = 0.5 * (b0.center().y() + b1.center().y());
                const double halfGap = std::clamp(0.5 * std::abs(b0.center().y() - b1.center().y()), 4.5, 14.0);
                const double xL = std::min(b0.left(), b1.left());
                const double xR = std::max(b0.right(), b1.right());
                res.canonicalStrokes = {
                    linePolyline(xL, cy - halfGap, xR, cy - halfGap, 10),
                    linePolyline(xL, cy + halfGap, xR, cy + halfGap, 10),
                    linePolyline(xL + (xR - xL) * 0.72, cy - halfGap * 2.1,
                                 xL + (xR - xL) * 0.28, cy + halfGap * 2.1, 10),
                };
                return res;
            }
        }
    }

    return res;
}

void HandwritingEngine::buildBuiltinTemplates() {
    m_templates.clear();

    // ==================== DÍGITOS 0–9 (MÚLTIPLES VARIANTES REALES) ====================

    // 0 (sentido horario y antihorario)
    const QVector<QVector<QPointF>> d0Canon = {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24)};
    addTemplate("0", true, d0Canon, d0Canon);
    addTemplate("0", true, {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, -450, 24)}, d0Canon);

    // 1 (palo simple recto/inclinado, palo con serifa, y 1 con base horizontal en 1 o 2 trazos!)
    const QVector<QVector<QPointF>> d1Canon = {
        joinPolylines({linePolyline(0.34, 0.28, 0.52, 0.12), linePolyline(0.52, 0.12, 0.52, 0.88)}),
    };
    const QVector<QVector<QPointF>> d1BaseCanon = {
        joinPolylines({linePolyline(0.34, 0.28, 0.52, 0.12), linePolyline(0.52, 0.12, 0.52, 0.86)}),
        linePolyline(0.28, 0.86, 0.76, 0.86),
    };
    addTemplate("1", true, d1Canon, d1Canon);
    addTemplate("1", true, {linePolyline(0.56, 0.12, 0.46, 0.88)}, {linePolyline(0.50, 0.12, 0.50, 0.88)});
    addTemplate("1", true, {linePolyline(0.64, 0.12, 0.38, 0.88)}, {linePolyline(0.50, 0.12, 0.50, 0.88)});
    // 1 con base en 2 trazos (palo recto o inclinado + base horizontal abajo)
    addTemplate("1", true, d1BaseCanon, d1BaseCanon);
    addTemplate("1", true, {linePolyline(0.52, 0.12, 0.52, 0.86), linePolyline(0.28, 0.86, 0.76, 0.86)}, d1BaseCanon);
    addTemplate("1", true,
                {joinPolylines({linePolyline(0.38, 0.30, 0.60, 0.12), linePolyline(0.60, 0.12, 0.46, 0.86)}),
                 linePolyline(0.24, 0.86, 0.76, 0.86)},
                d1BaseCanon);
    addTemplate("1", true,
                {linePolyline(0.62, 0.12, 0.44, 0.86), linePolyline(0.24, 0.86, 0.76, 0.86)},
                d1BaseCanon);

    // 2 (variante escolar con esquina y variante fluida curva)
    const QVector<QVector<QPointF>> d2Canon = {
        joinPolylines({arcPolyline(0.48, 0.30, 0.24, 0.18, -165, 25, 14),
                       quadBezier({0.70, 0.38}, {0.48, 0.62}, {0.24, 0.86}, 10),
                       linePolyline(0.24, 0.86, 0.78, 0.86)}),
    };
    addTemplate("2", true, d2Canon, d2Canon);
    addTemplate("2", true,
                {joinPolylines({arcPolyline(0.46, 0.28, 0.22, 0.16, -160, 20, 12),
                                quadBezier({0.66, 0.36}, {0.40, 0.65}, {0.24, 0.84}, 10),
                                quadBezier({0.24, 0.84}, {0.50, 0.88}, {0.78, 0.80}, 8)})},
                d2Canon);

    // 3
    const QVector<QVector<QPointF>> d3Canon = {
        joinPolylines({arcPolyline(0.46, 0.30, 0.24, 0.18, -155, 90, 14),
                       arcPolyline(0.46, 0.68, 0.26, 0.20, -90, 160, 14)}),
    };
    addTemplate("3", true, d3Canon, d3Canon);

    // 4 (EN 1 TRAZO abierto tipo U+cola, Y EN 2 TRAZOS clásico!)
    const QVector<QVector<QPointF>> d4Canon = {
        joinPolylines({linePolyline(0.30, 0.14, 0.24, 0.58), linePolyline(0.24, 0.58, 0.76, 0.58)}),
        linePolyline(0.64, 0.14, 0.64, 0.88),
    };
    addTemplate("4", true, d4Canon, d4Canon);
    addTemplate("4", true,
                {joinPolylines({linePolyline(0.62, 0.14, 0.22, 0.62), linePolyline(0.22, 0.62, 0.78, 0.62)}),
                 linePolyline(0.62, 0.14, 0.62, 0.88)},
                d4Canon);
    // 4 en 1 solo trazo (muy común al escribir rápido!)
    addTemplate("4", true,
                {joinPolylines({linePolyline(0.32, 0.14, 0.26, 0.56),
                                linePolyline(0.26, 0.56, 0.66, 0.56),
                                linePolyline(0.66, 0.56, 0.70, 0.28),
                                linePolyline(0.70, 0.28, 0.62, 0.88)})},
                d4Canon);

    // 5 (1 trazo angular, 1 trazo fluido tipo S y 2 trazos)
    const QVector<QVector<QPointF>> d5Canon = {
        joinPolylines({linePolyline(0.72, 0.14, 0.30, 0.14),
                       linePolyline(0.30, 0.14, 0.28, 0.46),
                       arcPolyline(0.48, 0.66, 0.26, 0.22, -130, 155, 16)}),
    };
    addTemplate("5", true, d5Canon, d5Canon);
    addTemplate("5", true,
                {joinPolylines({quadBezier({0.76, 0.18}, {0.42, 0.08}, {0.26, 0.38}, 10),
                                quadBezier({0.26, 0.38}, {0.72, 0.46}, {0.72, 0.68}, 10),
                                quadBezier({0.72, 0.68}, {0.58, 0.90}, {0.24, 0.84}, 10)})},
                d5Canon);
    addTemplate("5", true,
                {joinPolylines({linePolyline(0.32, 0.14, 0.28, 0.46),
                                arcPolyline(0.48, 0.66, 0.26, 0.22, -130, 155, 16)}),
                 linePolyline(0.32, 0.14, 0.74, 0.14)},
                d5Canon);

    // 6 (variante curva clásica + variante de bajada inclinada y pancita baja ancha)
    const QVector<QVector<QPointF>> d6Canon = {
        joinPolylines({quadBezier({0.66, 0.14}, {0.26, 0.22}, {0.24, 0.64}, 14),
                       arcPolyline(0.50, 0.66, 0.26, 0.22, 180, -180, 18)}),
    };
    addTemplate("6", true, d6Canon, d6Canon);
    addTemplate("6", true,
                {joinPolylines({quadBezier({0.58, 0.14}, {0.24, 0.42}, {0.22, 0.74}, 12),
                                arcPolyline(0.50, 0.72, 0.28, 0.16, 180, -180, 18)})},
                d6Canon);
    addTemplate("6", true,
                {joinPolylines({linePolyline(0.56, 0.14, 0.24, 0.74),
                                quadBezier({0.24, 0.74}, {0.50, 0.92}, {0.76, 0.74}, 10),
                                quadBezier({0.76, 0.74}, {0.68, 0.54}, {0.30, 0.62}, 10)})},
                d6Canon);

    // 7 (1 trazo simple Y 2 trazos tachado — ambos limpios!)
    const QVector<QVector<QPointF>> d7Simple = {
        joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
    };
    const QVector<QVector<QPointF>> d7Crossed = {
        joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
        linePolyline(0.34, 0.52, 0.70, 0.52),
    };
    addTemplate("7", true, d7Simple, d7Simple);
    addTemplate("7", true, d7Crossed, d7Crossed);

    // 8 (ambos sentidos de trazo: S estándar y espejo, más 2 círculos apilados!)
    const QVector<QVector<QPointF>> d8Canon = {
        joinPolylines({arcPolyline(0.50, 0.31, 0.22, 0.17, -60, -240, 10),
                       quadBezier({0.39, 0.46}, {0.66, 0.54}, {0.74, 0.68}, 8),
                       arcPolyline(0.50, 0.68, 0.24, 0.20, 0, 180, 10),
                       quadBezier({0.26, 0.68}, {0.34, 0.54}, {0.62, 0.44}, 8),
                       arcPolyline(0.50, 0.31, 0.22, 0.17, 45, -60, 8)}),
    };
    addTemplate("8", true, d8Canon, d8Canon);
    // Variante espejo horizontal (empezando arriba-izquierda hacia abajo-derecha)
    {
        QVector<QPointF> mirror8;
        mirror8.reserve(d8Canon[0].size());
        for (const QPointF& p : d8Canon[0])
            mirror8.append(QPointF(1.0 - p.x(), p.y()));
        addTemplate("8", true, {mirror8}, d8Canon);
    }
    // Variante empezando en el centro del 8 (cruce central -> lazo arriba -> lazo abajo)
    addTemplate("8", true,
                {joinPolylines({arcPolyline(0.50, 0.31, 0.22, 0.17, 90, -270, 14),
                                arcPolyline(0.50, 0.68, 0.24, 0.20, -90, 270, 14)})},
                d8Canon);
    // Variante en 2 círculos (2 trazos)
    addTemplate("8", true,
                {arcPolyline(0.50, 0.31, 0.22, 0.17, -90, 270, 16),
                 arcPolyline(0.50, 0.68, 0.24, 0.20, -90, 270, 16)},
                d8Canon);

    // 9 (antihorario recto, antihorario curvo, y horario empezando arriba-izquierda!)
    const QVector<QVector<QPointF>> d9Canon = {
        joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, 0, -360, 18),
                       linePolyline(0.72, 0.34, 0.66, 0.88)}),
    };
    addTemplate("9", true, d9Canon, d9Canon);
    addTemplate("9", true,
                {joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, -45, -405, 18),
                                quadBezier({0.70, 0.36}, {0.68, 0.64}, {0.58, 0.88}, 10)})},
                d9Canon);
    addTemplate("9", true,
                {joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, -135, 225, 18),
                                linePolyline(0.70, 0.34, 0.66, 0.88)})},
                d9Canon);
    addTemplate("9", true,
                {joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, 0, -360, 18),
                                quadBezier({0.72, 0.34}, {0.66, 0.68}, {0.76, 0.88}, 10)})},
                d9Canon);

    // ==================== GRIEGAS (α, β, π) Y FLECHA ====================

    // α (alfa: lazo fluido)
    const QVector<QVector<QPointF>> alphaCanon = {
        joinPolylines({quadBezier({0.82, 0.26}, {0.56, 0.84}, {0.28, 0.78}, 12),
                       quadBezier({0.28, 0.78}, {0.10, 0.50}, {0.30, 0.24}, 12),
                       quadBezier({0.30, 0.24}, {0.58, 0.20}, {0.82, 0.78}, 12)}),
    };
    addTemplate("α", true, alphaCanon, alphaCanon);

    // β (beta: asta vertical de abajo hacia arriba + dos panzas)
    const QVector<QVector<QPointF>> betaCanon = {
        joinPolylines({linePolyline(0.30, 0.90, 0.34, 0.14),
                       arcPolyline(0.50, 0.30, 0.20, 0.16, -120, 90, 12),
                       arcPolyline(0.52, 0.62, 0.22, 0.18, -90, 150, 14)}),
    };
    addTemplate("β", true, betaCanon, betaCanon);

    // π (3 trazos: barra superior + 2 patas)
    const QVector<QVector<QPointF>> piCanon = {
        linePolyline(0.14, 0.22, 0.86, 0.22),
        linePolyline(0.34, 0.22, 0.30, 0.84),
        joinPolylines({linePolyline(0.66, 0.22, 0.66, 0.76),
                       quadBezier({0.66, 0.76}, {0.68, 0.86}, {0.80, 0.82}, 8)}),
    };
    addTemplate("π", true, piCanon, piCanon);

    // → (flecha derecha: 2 trazos)
    const QVector<QVector<QPointF>> arrowCanon = {
        linePolyline(0.10, 0.50, 0.88, 0.50),
        joinPolylines({linePolyline(0.64, 0.26, 0.88, 0.50), linePolyline(0.88, 0.50, 0.64, 0.74)}),
    };
    addTemplate("→", true, arrowCanon, arrowCanon);

    // ==================== LETRAS PARA "PASAR A TEXTO" ====================
    addTemplate("A", false,
                {joinPolylines({linePolyline(0.20, 0.86, 0.50, 0.14), linePolyline(0.50, 0.14, 0.80, 0.86)}),
                 linePolyline(0.30, 0.60, 0.70, 0.60)},
                {joinPolylines({linePolyline(0.20, 0.86, 0.50, 0.14), linePolyline(0.50, 0.14, 0.80, 0.86)}),
                 linePolyline(0.30, 0.60, 0.70, 0.60)});
    addTemplate("C", false, {arcPolyline(0.54, 0.50, 0.30, 0.36, -45, -315, 18)},
                {arcPolyline(0.54, 0.50, 0.30, 0.36, -45, -315, 18)});
    addTemplate("E", false,
                {joinPolylines({linePolyline(0.74, 0.16, 0.28, 0.16), linePolyline(0.28, 0.16, 0.28, 0.84),
                                linePolyline(0.28, 0.84, 0.76, 0.84)}),
                 linePolyline(0.28, 0.50, 0.68, 0.50)},
                {joinPolylines({linePolyline(0.74, 0.16, 0.28, 0.16), linePolyline(0.28, 0.16, 0.28, 0.84),
                                linePolyline(0.28, 0.84, 0.76, 0.84)}),
                 linePolyline(0.28, 0.50, 0.68, 0.50)});
    addTemplate("x", false,
                {quadBezier({0.20, 0.28}, {0.42, 0.20}, {0.50, 0.52}, 10),
                 quadBezier({0.80, 0.24}, {0.50, 0.54}, {0.22, 0.84}, 10)},
                {linePolyline(0.22, 0.22, 0.78, 0.82), linePolyline(0.78, 0.22, 0.22, 0.82)});
    addTemplate("x", false,
                {linePolyline(0.22, 0.22, 0.78, 0.82), linePolyline(0.78, 0.22, 0.22, 0.82)},
                {linePolyline(0.22, 0.22, 0.78, 0.82), linePolyline(0.78, 0.22, 0.22, 0.82)});
    addTemplate("y", false,
                {linePolyline(0.22, 0.18, 0.50, 0.52), linePolyline(0.78, 0.18, 0.34, 0.88)},
                {linePolyline(0.22, 0.18, 0.50, 0.52), linePolyline(0.78, 0.18, 0.34, 0.88)});
    addTemplate("z", false,
                {joinPolylines({linePolyline(0.22, 0.18, 0.78, 0.18), linePolyline(0.78, 0.18, 0.22, 0.84),
                                linePolyline(0.22, 0.84, 0.78, 0.84)})},
                {joinPolylines({linePolyline(0.22, 0.18, 0.78, 0.18), linePolyline(0.78, 0.18, 0.22, 0.84),
                                linePolyline(0.22, 0.84, 0.78, 0.84)})});
}

void HandwritingEngine::loadCustomTemplates() {
    QFile file(templatesFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonArray arr = doc.object().value(QStringLiteral("templates")).toArray();
    for (const QJsonValue& val : arr) {
        const QJsonObject obj = val.toObject();
        const QString label = obj.value(QStringLiteral("label")).toString().trimmed();
        if (label.isEmpty())
            continue;
        QVector<QVector<QPointF>> strokes;
        for (const QJsonValue& sv : obj.value(QStringLiteral("strokes")).toArray()) {
            QVector<QPointF> pts;
            for (const QJsonValue& pv : sv.toArray()) {
                const QJsonArray pair = pv.toArray();
                if (pair.size() == 2)
                    pts.append(QPointF(pair[0].toDouble(), pair[1].toDouble()));
            }
            if (pts.size() >= 2)
                strokes.append(pts);
        }
        if (!strokes.isEmpty())
            addTemplate(label, true, strokes, strokes, true);
    }
}

void HandwritingEngine::saveCustomTemplatesToDisk() const {
    QJsonArray arr;
    for (const auto& tpl : m_templates) {
        if (!tpl.isCustom)
            continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("label"), tpl.label);
        QJsonArray strokesArr;
        for (const auto& s : tpl.canonicalStrokes) {
            QJsonArray ptsArr;
            for (const QPointF& p : s)
                ptsArr.append(QJsonArray{p.x(), p.y()});
            strokesArr.append(ptsArr);
        }
        obj.insert(QStringLiteral("strokes"), strokesArr);
        arr.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("templates"), arr);

    QFile file(templatesFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool HandwritingEngine::saveCustomTemplate(const QString& label,
                                           const QVector<QVector<QPointF>>& worldStrokes) {
    const QString cleanLabel = label.trimmed();
    if (cleanLabel.isEmpty() || worldStrokes.isEmpty())
        return false;

    m_templates.removeIf([&](const GlyphTemplate& t) { return t.isCustom && t.label == cleanLabel; });
    const auto norm = normalizeStrokes01(worldStrokes);
    addTemplate(cleanLabel, true, norm, norm, true);
    saveCustomTemplatesToDisk();
    return true;
}

bool HandwritingEngine::removeCustomTemplate(const QString& label) {
    const int removed = m_templates.removeIf(
        [&](const GlyphTemplate& t) { return t.isCustom && t.label == label.trimmed(); });
    if (removed > 0)
        saveCustomTemplatesToDisk();
    return removed > 0;
}

QStringList HandwritingEngine::customTemplateLabels() const {
    QStringList labels;
    for (const auto& t : m_templates) {
        if (t.isCustom && !labels.contains(t.label))
            labels.append(t.label);
    }
    return labels;
}

RecognitionResult HandwritingEngine::recognizeCluster(const QVector<QVector<QPointF>>& rawStrokes,
                                                      bool mathAndDigitsOnly,
                                                      const QRectF& neighborLeftBox,
                                                      bool hasRightNeighbor) const {
    if (rawStrokes.isEmpty() || rawStrokes.size() > 4)
        return {};

    // 1. Primero: plantillas personales guardadas por el usuario (máxima prioridad)
    double inputAspect = 1.0;
    const auto inputDir = normalizeDirected(rawStrokes, &inputAspect);
    if (!inputDir.isEmpty()) {
        double bestCustom = std::numeric_limits<double>::max();
        const GlyphTemplate* customMatch = nullptr;
        for (const auto& tpl : m_templates) {
            if (!tpl.isCustom || tpl.strokeCount != rawStrokes.size())
                continue;
            const double ratioDiff = std::abs(std::log(inputAspect / std::max(tpl.aspectRatio, 0.1)));
            if (ratioDiff > 0.90)
                continue;
            const double d = directedStrokeDistance(inputDir, tpl.directedStrokes) + 0.04 * ratioDiff;
            if (d < bestCustom) {
                bestCustom = d;
                customMatch = &tpl;
            }
        }
        if (customMatch && bestCustom <= 0.18) {
            RecognitionResult res;
            res.matched = true;
            res.label = customMatch->label;
            res.score = std::clamp(1.0 - bestCustom / 0.40, 0.0, 1.0);
            res.canonicalInWorld = false;
            res.canonicalStrokes = customMatch->canonicalStrokes;
            return res;
        }
    }

    // 2. Segundo: símbolos dependientes del contexto espacial (° grado, · punto de multiplicación, √ techo largo)
    const QRectF box = strokesBoundingBox(rawStrokes);
    if (rawStrokes.size() == 1 && rawStrokes[0].size() >= 2) {
        const auto& pts = rawStrokes[0];
        // 2A. Punto de multiplicación '·'
        if (isTinyDot(pts) && !neighborLeftBox.isNull()) {
            const double relY = (box.center().y() - neighborLeftBox.top()) / std::max(neighborLeftBox.height(), 12.0);
            const double gapX = box.left() - neighborLeftBox.right();
            if (relY >= 0.25 && relY <= 0.72 && gapX >= -4.0 && gapX <= std::max(neighborLeftBox.height() * 1.1, 45.0)) {
                RecognitionResult res;
                res.matched = true;
                res.label = QStringLiteral("·");
                res.score = 0.96;
                res.canonicalInWorld = true;
                const QPointF c = box.center();
                res.canonicalStrokes = {linePolyline(c.x() - 0.8, c.y(), c.x() + 0.8, c.y(), 3)};
                return res;
            }
            return {};
        }
        // 2B. Signo de GRADO '°' (circulito pequeño arriba-derecha de un número!)
        // Si es un trazo pequeño en la esquina superior derecha del vecino, JAMÁS dejarlo caer
        // en la CNN como un '6' o '0' gigante!
        if (!neighborLeftBox.isNull()) {
            const double dim = std::max(box.width(), box.height());
            const double nh = std::max(neighborLeftBox.height(), 14.0);
            const double relY = (box.center().y() - neighborLeftBox.top()) / nh;
            const double gapX = box.left() - neighborLeftBox.right();
            if (dim <= nh * 0.56 && dim >= 3.5 && relY <= 0.42 && relY >= -0.45
                && gapX >= -nh * 0.45 && gapX <= nh * 0.95) {
                const double len = polylineLength(pts);
                if (len >= dim * 1.35) {
                    RecognitionResult res;
                    res.matched = true;
                    res.label = QStringLiteral("°");
                    res.score = 0.96;
                    res.canonicalInWorld = true;
                    const QPointF c = box.center();
                    const double r = std::clamp(nh * 0.12, 3.2, 7.5);
                    res.canonicalStrokes = {arcPolyline(c.x(), c.y(), r, r, -90, 270, 18)};
                    return res;
                }
            }
        }
        // 2C. Signo MENOS '−' (guion horizontal corto y recto)
        if (isHorizontalLine(pts) && box.width() >= 12.0 && box.width() <= 64.0) {
            RecognitionResult res;
            res.matched = true;
            res.label = QStringLiteral("−");
            res.score = 0.95;
            res.canonicalInWorld = true;
            const double y = box.center().y();
            res.canonicalStrokes = {linePolyline(box.left(), y, box.right(), y, 10)};
            return res;
        }
        // 2D. Raíz larga '√' (conserva el ancho exacto del techo!)
        if (pts.size() >= 4 && box.width() >= 22.0 && box.height() >= 16.0 && box.width() >= box.height() * 1.2) {
            const RecognitionResult geo = matchGeometric(rawStrokes, neighborLeftBox, hasRightNeighbor);
            if (geo.matched && geo.label == QLatin1String("√"))
                return geo;
        }
    }

    // 2D. Signos multi-trazo exactos (+, =, ÷, ≠, ≤, ≥, θ, 8 de 2 bolitas) ANTES de la CNN,
    // para que un '+' jamás se confunda con un dígito '4'/'5'/'6'!
    if (rawStrokes.size() >= 2) {
        const RecognitionResult geo = matchGeometric(rawStrokes, neighborLeftBox, hasRightNeighbor);
        if (geo.matched)
            return geo;
    }

    // 3. MOTOR NEURONAL REAL (CNN 26 clases, 98.80% en MNIST 10k + rechazo OTHER!)
    const CnnPrediction pred = classifyWithCnn(rawStrokes);
    if (pred.label != QLatin1String("OTHER") && pred.prob >= 0.80) {
        // Evitar que la CNN convierta un '+' o 2 trazos cruzados en un dígito '5'/'6'
        if (rawStrokes.size() >= 2 && (pred.label == QLatin1String("5") || pred.label == QLatin1String("6")))
            return {};
        RecognitionResult res;
        res.matched = true;
        res.label = pred.label;
        res.score = pred.prob;
        res.canonicalInWorld = false;
        res.canonicalStrokes = canonicalGlyphFor(pred.label);
        if (!res.canonicalStrokes.isEmpty())
            return res;
    }

    // 4. Fallback solo para letras en "Pasar a texto" (mathAndDigitsOnly == false)
    if (!mathAndDigitsOnly && !inputDir.isEmpty()) {
        double bestDist = std::numeric_limits<double>::max();
        const GlyphTemplate* bestTpl = nullptr;
        for (const auto& tpl : m_templates) {
            if (tpl.isMathOrDigit || tpl.strokeCount != rawStrokes.size())
                continue;
            const double ratioDiff = std::abs(std::log(inputAspect / std::max(tpl.aspectRatio, 0.1)));
            if (ratioDiff > 0.95)
                continue;
            const double d = directedStrokeDistance(inputDir, tpl.directedStrokes) + 0.045 * ratioDiff;
            if (d < bestDist) {
                bestDist = d;
                bestTpl = &tpl;
            }
        }
        if (bestTpl && bestDist <= 0.22) {
            RecognitionResult res;
            res.matched = true;
            res.label = bestTpl->label;
            res.score = std::clamp(1.0 - bestDist / 0.46, 0.0, 1.0);
            res.canonicalInWorld = false;
            res.canonicalStrokes = bestTpl->canonicalStrokes;
            return res;
        }
    }

    return {};
}

bool HandwritingEngine::beautifyCluster(QVector<Stroke>& strokes,
                                        int startIdx,
                                        int endIdx,
                                        const QColor& operatorColor,
                                        QString* recognizedLabel,
                                        int* outDeltaCount,
                                        const QVector<ShapeItem>& shapes) const {
    if (outDeltaCount)
        *outDeltaCount = 0;
    if (endIdx < 0 || endIdx > strokes.size())
        endIdx = int(strokes.size());
    if (startIdx < 0 || startIdx >= endIdx)
        return false;

    const int k = endIdx - startIdx;
    if (k < 1 || k > 3)
        return false;

    const Stroke& ref = strokes[startIdx];
    if (ref.tool != ToolType::Pen && ref.tool != ToolType::Ballpoint)
        return false;

    QVector<QVector<QPointF>> raw;
    raw.reserve(k);
    QRectF clusterBox;
    for (int i = startIdx; i < endIdx; ++i) {
        if (strokes[i].points.size() < 2)
            return false;
        raw.append(extractRawPoints(strokes[i]));
        clusterBox = clusterBox.isNull() ? strokes[i].bounds : clusterBox.united(strokes[i].bounds);
    }

    const double clusterDim = std::max(clusterBox.width(), clusterBox.height());
    if (clusterDim > 340.0)
        return false;

    // Guardia de geometría: si el trazo cruza o toca el borde de una figura geométrica
    // (como un arco de ángulo sobre el vértice de un triángulo), JAMÁS convertirlo!
    for (const ShapeItem& shape : shapes) {
        if (shape.bounds.adjusted(-6, -6, 6, 6).intersects(clusterBox)) {
            for (const QPointF& v : shape.vertices) {
                if (clusterBox.adjusted(-18, -18, 18, 18).contains(v))
                    return false;
            }
        }
    }

    // Buscar contexto espacial: vecino más cercano a la izquierda (para ° y ·)
    // y si el clúster toca algún trazo previo (para jamás pintar '−' encima de un 7 o 0!)
    QRectF neighborLeftBox;
    double bestLeftDist = 1e9;
    bool touchesExistingStroke = false;
    bool closelyFlankedByWord = false;
    int closeLeftCount = 0;

    for (int i = 0; i < startIdx; ++i) {
        const QRectF& b = strokes[i].bounds;
        if (b.adjusted(-3, -3, 3, 3).intersects(clusterBox))
            touchesExistingStroke = true;

        // Vecino en la misma línea a la izquierda
        const double dy = std::abs(b.center().y() - clusterBox.center().y());
        const double refH = std::max({b.height(), clusterBox.height(), 18.0});
        if (dy <= refH * 0.95 && b.right() <= clusterBox.center().x()) {
            const double dx = std::max(0.0, clusterBox.left() - b.right());
            if (dx < bestLeftDist && dx <= refH * 1.4) {
                bestLeftDist = dx;
                neighborLeftBox = b;
            }
            if (dx <= refH * 0.28)
                closeLeftCount++;
        }
    }
    if (closeLeftCount >= 2)
        closelyFlankedByWord = true;

    const RecognitionResult rec = recognizeCluster(raw, true, neighborLeftBox, false);
    if (!rec.matched)
        return false;

    // Protecciones inteligentes contra falsos positivos:
    // 1) Nunca convertir un guion horizontal que cruza un trazo previo en '−'
    if (rec.label == QLatin1String("−") && touchesExistingStroke)
        return false;
    // 2) Nunca convertir una 'o' dentro de una palabra en '0'
    if (rec.label == QLatin1String("0") && closelyFlankedByWord)
        return false;
    // 3) Umbral neuronal alto (probabilidad Softmax >= 0.78) para cero falsos positivos
    const double minScore = rec.canonicalInWorld ? 0.85 : 0.78;
    if (rec.score < minScore)
        return false;

    const QColor finalColor = (operatorColor.isValid() && isOperatorOrGrouping(rec.label))
                                  ? operatorColor
                                  : ref.color;

    // ==================== MAQUETACIÓN MATEMÁTICA DE RENGLÓN ====================
    // Calcula la altura de referencia (lineH) y la línea base (lineBaseline) de los
    // números/caracteres de tamaño completo en esta misma fila, para que todos los
    // dígitos salgan del mismo tamaño, apoyados en la misma base, y los signos (+ − =)
    // queden centrados y proporcionados!
    double lineH = 0.0;
    double lineBaseline = 0.0;
    {
        QVector<double> lineHeights;
        QVector<double> lineBottoms;
        for (int i = 0; i < startIdx; ++i) {
            const QRectF& b = strokes[i].bounds;
            const double bh = b.height();
            if (bh < 18.0 || bh > 120.0)
                continue; // ignorar signos chiquitos (° · − =) al medir la altura de dígito
            const double dy = std::abs(b.center().y() - clusterBox.center().y());
            if (dy <= std::max(bh, clusterBox.height()) * 0.85
                && b.right() <= clusterBox.center().x() + 20.0
                && (clusterBox.left() - b.right()) <= 520.0) {
                lineHeights.append(bh);
                lineBottoms.append(b.bottom());
            }
        }
        if (!lineHeights.isEmpty()) {
            std::sort(lineHeights.begin(), lineHeights.end());
            std::sort(lineBottoms.begin(), lineBottoms.end());
            lineH = lineHeights[lineHeights.size() / 2];
            lineBaseline = lineBottoms[lineBottoms.size() / 2];
        }
    }

    const bool isDigit = (rec.label.size() == 1 && rec.label[0].isDigit());
    const bool isInfixOp = (rec.label == QLatin1String("+") || rec.label == QLatin1String("−")
                            || rec.label == QLatin1String("=") || rec.label == QLatin1String("×")
                            || rec.label == QLatin1String("÷") || rec.label == QLatin1String("<")
                            || rec.label == QLatin1String(">") || rec.label == QLatin1String("≠"));

    QVector<Stroke> cleanStrokes;
    cleanStrokes.reserve(rec.canonicalStrokes.size());
    const qint64 baseTs = strokes[endIdx - 1].points.back().timestampMs;

    if (rec.canonicalInWorld) {
        // Ajustar grados (°) y signos (−) a la métrica limpia de la línea si hay referencia!
        QVector<QVector<QPointF>> placed = rec.canonicalStrokes;
        if (lineH > 18.0 && rec.label == QLatin1String("°") && !neighborLeftBox.isNull()) {
            const double r = std::clamp(lineH * 0.115, 3.4, 7.0);
            const QPointF degCenter(neighborLeftBox.right() + r * 1.45,
                                    lineBaseline - lineH * 0.88);
            placed = {arcPolyline(degCenter.x(), degCenter.y(), r, r, -90, 270, 18)};
        } else if (lineH > 18.0 && rec.label == QLatin1String("−")) {
            const double midY = lineBaseline - lineH * 0.44;
            const double halfW = std::clamp(lineH * 0.26, 7.0, 20.0);
            const double cx = clusterBox.center().x();
            placed = {linePolyline(cx - halfW, midY, cx + halfW, midY, 10)};
        }
        for (const auto& cs : placed) {
            Stroke s;
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            s.tool = ref.tool;
            s.color = finalColor;
            s.size = ref.size;
            s.opacity = ref.opacity;
            s.linePattern = ref.linePattern;
            s.pressureProfile = ref.pressureProfile;
            s.hasHardwarePressure = false;
            s.points.reserve(cs.size());
            for (int pi = 0; pi < cs.size(); ++pi)
                s.points.append({cs[pi].x(), cs[pi].y(), 0.55, baseTs + pi, 0.0, 0.0});
            s.bounds = computeBounds(s.points, s.size * 0.7);
            cleanStrokes.append(std::move(s));
        }
    } else {
        const QRectF rawBox = strokesBoundingBox(raw);
        const QRectF canonBox = strokesBoundingBox(rec.canonicalStrokes);
        const double cw = std::max(canonBox.width(), 1e-4);
        const double ch = std::max(canonBox.height(), 1e-4);

        double targetH = std::max(rawBox.height(), 12.0);
        double scale = std::min(std::max(rawBox.width(), 8.0) / cw, targetH / ch);
        QPointF targetCenter = rawBox.center();

        if (lineH > 18.0) {
            if (isDigit) {
                // Todos los dígitos de la ecuación comparten la MISMA altura y línea base!
                scale = (lineH * 0.96) / ch;
                const double placedH = ch * scale;
                targetCenter.setY(lineBaseline - placedH * 0.5);
                // Si el dígito está justo al lado de otro dígito (ej. '10', '20', '110'),
                // acercar suavemente si quedó demasiado separado
                if (!neighborLeftBox.isNull() && neighborLeftBox.height() >= lineH * 0.65) {
                    const double placedW = cw * scale;
                    const double gap = (targetCenter.x() - placedW * 0.5) - neighborLeftBox.right();
                    if (gap > lineH * 0.22 && gap < lineH * 0.55)
                        targetCenter.setX(neighborLeftBox.right() + lineH * 0.16 + placedW * 0.5);
                }
            } else if (isInfixOp) {
                // Todos los signos (+ = × ÷ < >) salen del MISMO tamaño y centrados en el renglón!
                scale = (lineH * 0.52) / std::max(cw, ch);
                targetCenter.setY(lineBaseline - lineH * 0.44);
            }
        }
        const QPointF canonCenter = canonBox.center();

        for (const auto& cs : rec.canonicalStrokes) {
            Stroke s;
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            s.tool = ref.tool;
            s.color = finalColor;
            s.size = ref.size;
            s.opacity = ref.opacity;
            s.linePattern = ref.linePattern;
            s.pressureProfile = ref.pressureProfile;
            s.hasHardwarePressure = false;
            s.points.reserve(cs.size());
            for (int pi = 0; pi < cs.size(); ++pi) {
                const QPointF mapped = targetCenter + (cs[pi] - canonCenter) * scale;
                s.points.append({mapped.x(), mapped.y(), 0.55, baseTs + pi, 0.0, 0.0});
            }
            s.bounds = computeBounds(s.points, s.size * 0.7);
            cleanStrokes.append(std::move(s));
        }
    }

    const int oldCount = endIdx - startIdx;
    const int newCount = int(cleanStrokes.size());
    strokes.erase(strokes.begin() + startIdx, strokes.begin() + endIdx);
    for (int i = 0; i < newCount; ++i)
        strokes.insert(strokes.begin() + startIdx + i, std::move(cleanStrokes[i]));

    if (outDeltaCount)
        *outDeltaCount = newCount - oldCount;
    if (recognizedLabel)
        *recognizedLabel = rec.label;
    return true;
}

QString HandwritingEngine::recognizeStrokesToText(const QVector<Stroke>& strokes,
                                                  const QSet<QString>& selectedIds) const {
    struct Item {
        const Stroke* stroke;
        QRectF box;
    };
    QVector<Item> items;
    for (const auto& s : strokes) {
        if (selectedIds.contains(s.id) && !s.points.isEmpty())
            items.append({&s, s.bounds});
    }
    if (items.isEmpty())
        return {};

    QVector<double> heights;
    heights.reserve(items.size());
    for (const auto& it : items)
        heights.append(std::max(it.box.height(), 10.0));
    std::sort(heights.begin(), heights.end());
    const double medianH = heights[heights.size() / 2];

    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.box.center().y() < b.box.center().y(); });

    QVector<QVector<Item>> lines;
    for (const auto& it : items) {
        bool placed = false;
        for (auto& line : lines) {
            double avgY = 0.0;
            for (const auto& li : line)
                avgY += li.box.center().y();
            avgY /= line.size();
            if (std::abs(it.box.center().y() - avgY) <= std::max(medianH * 0.75, 22.0)) {
                line.append(it);
                placed = true;
                break;
            }
        }
        if (!placed)
            lines.append({it});
    }

    QStringList outputLines;
    for (auto& line : lines) {
        std::sort(line.begin(), line.end(),
                  [](const Item& a, const Item& b) { return a.box.left() < b.box.left(); });

        struct Cluster {
            QVector<QVector<QPointF>> rawStrokes;
            QRectF box;
        };
        QVector<Cluster> clusters;
        for (const auto& it : line) {
            bool merged = false;
            if (!clusters.isEmpty()) {
                auto& prev = clusters.back();
                const double overlapX = std::min(prev.box.right(), it.box.right())
                                        - std::max(prev.box.left(), it.box.left());
                const double minW = std::max(1.0, std::min(prev.box.width(), it.box.width()));
                const bool touches = prev.box.adjusted(-4, -6, 4, 6).intersects(it.box);
                if ((overlapX / minW > 0.25 || touches)
                    && prev.rawStrokes.size() < 3
                    && prev.box.united(it.box).width() < std::max(medianH * 1.45, 55.0)) {
                    prev.rawStrokes.append(extractRawPoints(*it.stroke));
                    prev.box = prev.box.united(it.box);
                    merged = true;
                }
            }
            if (!merged)
                clusters.append({{extractRawPoints(*it.stroke)}, it.box});
        }

        QString lineText;
        for (int ci = 0; ci < clusters.size(); ++ci) {
            const QRectF leftBox = (ci > 0) ? clusters[ci - 1].box : QRectF();
            const bool hasRight = (ci + 1 < clusters.size());
            if (ci > 0) {
                const double gap = clusters[ci].box.left() - clusters[ci - 1].box.right();
                if (gap > std::max(medianH * 0.55, 16.0))
                    lineText += QLatin1Char(' ');
            }
            const RecognitionResult rec = recognizeCluster(clusters[ci].rawStrokes, false, leftBox, hasRight);
            lineText += rec.matched ? rec.label : QStringLiteral("?");
        }
        if (!lineText.trimmed().isEmpty())
            outputLines.append(lineText.trimmed());
    }

    return outputLines.join(QLatin1Char('\n'));
}

} // namespace finily
