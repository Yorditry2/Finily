#include "engine/HandwritingEngine.h"

#include "engine/InkEngine.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finily {
namespace {

constexpr int kCloudSamples = 32;
constexpr double kPi = 3.14159265358979323846;

double dist2D(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
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

QRectF strokesBoundingBox(const QVector<QVector<QPointF>>& strokes) {
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    bool any = false;
    for (const auto& s : strokes) {
        for (const QPointF& p : s) {
            minX = std::min(minX, p.x());
            minY = std::min(minY, p.y());
            maxX = std::max(maxX, p.x());
            maxY = std::max(maxY, p.y());
            any = true;
        }
    }
    if (!any)
        return QRectF();
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
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

    double totalLen = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        totalLen += dist2D(pts[i - 1], pts[i]);
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

QVector<QVector<QPointF>> HandwritingEngine::normalizeStrokes01(const QVector<QVector<QPointF>>& strokes) {
    const QRectF box = strokesBoundingBox(strokes);
    const double w = std::max(box.width(), 1e-4);
    const double h = std::max(box.height(), 1e-4);
    const double scale = std::max(w, h);
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

QVector<HandwritingEngine::CloudPoint> HandwritingEngine::normalizeToCloud(
    const QVector<QVector<QPointF>>& strokes, int sampleCount, double* outAspect) {
    QVector<double> lengths;
    double totalLen = 0.0;
    for (const auto& s : strokes) {
        double len = 0.0;
        for (int i = 1; i < s.size(); ++i)
            len += dist2D(s[i - 1], s[i]);
        lengths.append(std::max(len, 1e-3));
        totalLen += std::max(len, 1e-3);
    }
    if (totalLen <= 1e-6 || strokes.isEmpty())
        return {};

    QVector<CloudPoint> cloud;
    cloud.reserve(sampleCount);

    int remaining = sampleCount;
    for (int si = 0; si < strokes.size(); ++si) {
        int count = (si == strokes.size() - 1)
                        ? remaining
                        : std::max(2, int(std::round(sampleCount * (lengths[si] / totalLen))));
        count = std::min(count, std::max(1, remaining - int(strokes.size() - 1 - si)));
        remaining -= count;
        const auto resampled = resamplePolyline(strokes[si], count);
        for (const QPointF& p : resampled)
            cloud.append({p.x(), p.y(), si});
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    double cx = 0.0;
    double cy = 0.0;
    for (const auto& p : cloud) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
        cx += p.x;
        cy += p.y;
    }
    cx /= cloud.size();
    cy /= cloud.size();

    const double w = maxX - minX;
    const double h = maxY - minY;
    const double scale = std::max({w, h, 1e-4});
    if (outAspect)
        *outAspect = std::clamp(w / std::max(h, 1e-4), 0.12, 8.0);

    for (auto& p : cloud) {
        p.x = (p.x - cx) / scale;
        p.y = (p.y - cy) / scale;
    }
    return cloud;
}

double HandwritingEngine::cloudDistance(const QVector<CloudPoint>& a, const QVector<CloudPoint>& b) {
    const int n = std::min(a.size(), b.size());
    if (n <= 0)
        return 1e9;

    auto oneWay = [n](const QVector<CloudPoint>& p, const QVector<CloudPoint>& q) {
        QVector<bool> matched(n, false);
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            double bestDist = std::numeric_limits<double>::max();
            int bestJ = -1;
            for (int j = 0; j < n; ++j) {
                if (matched[j])
                    continue;
                const double d = std::hypot(p[i].x - q[j].x, p[i].y - q[j].y);
                if (d < bestDist) {
                    bestDist = d;
                    bestJ = j;
                }
            }
            if (bestJ >= 0) {
                matched[bestJ] = true;
                sum += bestDist;
            }
        }
        return sum / double(n);
    };

    return 0.5 * (oneWay(a, b) + oneWay(b, a));
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
    tpl.cloud = normalizeToCloud(sampleStrokes, kCloudSamples, &tpl.aspectRatio);
    tpl.canonicalStrokes = normalizeStrokes01(canonicalStrokes);
    if (!tpl.cloud.isEmpty() && !tpl.canonicalStrokes.isEmpty())
        m_templates.append(std::move(tpl));
}

void HandwritingEngine::buildBuiltinTemplates() {
    m_templates.clear();

    // ==================== SIGNOS MATEMÁTICOS ====================

    // + (2 trazos: vertical + horizontal)
    const QVector<QVector<QPointF>> plusCanon = {
        linePolyline(0.5, 0.15, 0.5, 0.85),
        linePolyline(0.15, 0.5, 0.85, 0.5),
    };
    addTemplate("+", true, plusCanon, plusCanon);
    addTemplate("+", true, {linePolyline(0.15, 0.5, 0.85, 0.5), linePolyline(0.5, 0.15, 0.5, 0.85)}, plusCanon);

    // = (2 trazos horizontales paralelos)
    const QVector<QVector<QPointF>> eqCanon = {
        linePolyline(0.12, 0.36, 0.88, 0.36),
        linePolyline(0.12, 0.64, 0.88, 0.64),
    };
    addTemplate("=", true, eqCanon, eqCanon);

    // ≠ (igual tachado: 3 trazos)
    const QVector<QVector<QPointF>> neqCanon = {
        linePolyline(0.12, 0.36, 0.88, 0.36),
        linePolyline(0.12, 0.64, 0.88, 0.64),
        linePolyline(0.75, 0.14, 0.25, 0.86),
    };
    addTemplate("≠", true, neqCanon, neqCanon);

    // × (por: 2 diagonales)
    const QVector<QVector<QPointF>> mulCanon = {
        linePolyline(0.18, 0.18, 0.82, 0.82),
        linePolyline(0.82, 0.18, 0.18, 0.82),
    };
    addTemplate("×", true, mulCanon, mulCanon);

    // ≤ y ≥ (2 trazos)
    const QVector<QVector<QPointF>> leqCanon = {
        joinPolylines({linePolyline(0.80, 0.18, 0.20, 0.44), linePolyline(0.20, 0.44, 0.80, 0.68)}),
        linePolyline(0.20, 0.84, 0.80, 0.84),
    };
    addTemplate("≤", true, leqCanon, leqCanon);

    const QVector<QVector<QPointF>> geqCanon = {
        joinPolylines({linePolyline(0.20, 0.18, 0.80, 0.44), linePolyline(0.80, 0.44, 0.20, 0.68)}),
        linePolyline(0.20, 0.84, 0.80, 0.84),
    };
    addTemplate("≥", true, geqCanon, geqCanon);

    // < y > (1 trazo)
    const QVector<QVector<QPointF>> ltCanon = {
        joinPolylines({linePolyline(0.78, 0.20, 0.22, 0.50), linePolyline(0.22, 0.50, 0.78, 0.80)}),
    };
    addTemplate("<", true, ltCanon, ltCanon);
    const QVector<QVector<QPointF>> gtCanon = {
        joinPolylines({linePolyline(0.22, 0.20, 0.78, 0.50), linePolyline(0.78, 0.50, 0.22, 0.80)}),
    };
    addTemplate(">", true, gtCanon, gtCanon);

    // → (flecha derecha: 2 trazos)
    const QVector<QVector<QPointF>> arrowCanon = {
        linePolyline(0.10, 0.50, 0.88, 0.50),
        joinPolylines({linePolyline(0.64, 0.26, 0.88, 0.50), linePolyline(0.88, 0.50, 0.64, 0.74)}),
    };
    addTemplate("→", true, arrowCanon, arrowCanon);

    // √ (raíz cuadrada)
    const QVector<QVector<QPointF>> sqrtCanon = {
        joinPolylines({linePolyline(0.08, 0.56, 0.22, 0.86),
                       linePolyline(0.22, 0.86, 0.46, 0.16),
                       linePolyline(0.46, 0.16, 0.94, 0.16)}),
    };
    addTemplate("√", true, sqrtCanon, sqrtCanon);

    // π (3 trazos: barra superior + 2 patas)
    const QVector<QVector<QPointF>> piCanon = {
        linePolyline(0.14, 0.22, 0.86, 0.22),
        linePolyline(0.34, 0.22, 0.30, 0.84),
        joinPolylines({linePolyline(0.66, 0.22, 0.66, 0.76),
                       quadBezier({0.66, 0.76}, {0.68, 0.86}, {0.80, 0.82}, 8)}),
    };
    addTemplate("π", true, piCanon, piCanon);

    // θ (óvalo + barra central horizontal)
    const QVector<QVector<QPointF>> thetaCanon = {
        arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24),
        linePolyline(0.24, 0.50, 0.76, 0.50),
    };
    addTemplate("θ", true, thetaCanon, thetaCanon);

    // α (alfa: 1 trazo lazo)
    const QVector<QVector<QPointF>> alphaCanon = {
        joinPolylines({quadBezier({0.82, 0.24}, {0.58, 0.86}, {0.30, 0.80}, 12),
                       quadBezier({0.30, 0.80}, {0.10, 0.50}, {0.32, 0.22}, 12),
                       quadBezier({0.32, 0.22}, {0.58, 0.18}, {0.82, 0.78}, 12)}),
    };
    addTemplate("α", true, alphaCanon, alphaCanon);

    // △ (triángulo)
    const QVector<QVector<QPointF>> triCanon = {
        joinPolylines({linePolyline(0.50, 0.14, 0.14, 0.86),
                       linePolyline(0.14, 0.86, 0.86, 0.86),
                       linePolyline(0.86, 0.86, 0.50, 0.14)}),
    };
    addTemplate("△", true, triCanon, triCanon);

    // ( y )
    const QVector<QVector<QPointF>> lpCanon = {quadBezier({0.64, 0.12}, {0.26, 0.50}, {0.64, 0.88}, 16)};
    const QVector<QVector<QPointF>> rpCanon = {quadBezier({0.36, 0.12}, {0.74, 0.50}, {0.36, 0.88}, 16)};
    addTemplate("(", true, lpCanon, lpCanon);
    addTemplate(")", true, rpCanon, rpCanon);

    // ==================== DÍGITOS 0–9 ====================

    // 0
    const QVector<QVector<QPointF>> d0Canon = {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, 270, 24)};
    addTemplate("0", true, d0Canon, d0Canon);
    addTemplate("0", true, {arcPolyline(0.50, 0.50, 0.28, 0.40, -90, -450, 24)}, d0Canon);

    // 1 (con serifa superior pequeña)
    const QVector<QVector<QPointF>> d1Canon = {
        joinPolylines({linePolyline(0.34, 0.26, 0.52, 0.12), linePolyline(0.52, 0.12, 0.52, 0.88)}),
    };
    addTemplate("1", true, d1Canon, d1Canon);

    // 2
    const QVector<QVector<QPointF>> d2Canon = {
        joinPolylines({arcPolyline(0.48, 0.30, 0.24, 0.18, -165, 20, 14),
                       linePolyline(0.70, 0.36, 0.24, 0.86),
                       linePolyline(0.24, 0.86, 0.78, 0.86)}),
    };
    addTemplate("2", true, d2Canon, d2Canon);

    // 3
    const QVector<QVector<QPointF>> d3Canon = {
        joinPolylines({arcPolyline(0.46, 0.30, 0.24, 0.18, -155, 90, 14),
                       arcPolyline(0.46, 0.68, 0.26, 0.20, -90, 160, 14)}),
    };
    addTemplate("3", true, d3Canon, d3Canon);

    // 4 (2 trazos: L + vertical)
    const QVector<QVector<QPointF>> d4Canon = {
        joinPolylines({linePolyline(0.62, 0.14, 0.22, 0.62), linePolyline(0.22, 0.62, 0.78, 0.62)}),
        linePolyline(0.62, 0.14, 0.62, 0.88),
    };
    addTemplate("4", true, d4Canon, d4Canon);
    addTemplate("4", true,
                {joinPolylines({linePolyline(0.32, 0.14, 0.26, 0.56), linePolyline(0.26, 0.56, 0.76, 0.56)}),
                 linePolyline(0.64, 0.14, 0.64, 0.88)},
                d4Canon);

    // 5 (1 trazo y 2 trazos)
    const QVector<QVector<QPointF>> d5Canon = {
        joinPolylines({linePolyline(0.72, 0.14, 0.30, 0.14),
                       linePolyline(0.30, 0.14, 0.28, 0.46),
                       arcPolyline(0.48, 0.66, 0.26, 0.22, -130, 155, 16)}),
    };
    addTemplate("5", true, d5Canon, d5Canon);
    addTemplate("5", true,
                {joinPolylines({linePolyline(0.30, 0.14, 0.28, 0.46),
                                arcPolyline(0.48, 0.66, 0.26, 0.22, -130, 155, 16)}),
                 linePolyline(0.30, 0.14, 0.74, 0.14)},
                d5Canon);

    // 6
    const QVector<QVector<QPointF>> d6Canon = {
        joinPolylines({quadBezier({0.68, 0.14}, {0.26, 0.22}, {0.24, 0.64}, 14),
                       arcPolyline(0.50, 0.66, 0.26, 0.22, 180, -180, 18)}),
    };
    addTemplate("6", true, d6Canon, d6Canon);

    // 7 (1 trazo y 2 trazos tachado)
    const QVector<QVector<QPointF>> d7Canon = {
        joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
    };
    addTemplate("7", true, d7Canon, d7Canon);
    addTemplate("7", true,
                {joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
                 linePolyline(0.36, 0.52, 0.68, 0.52)},
                {joinPolylines({linePolyline(0.24, 0.16, 0.76, 0.16), linePolyline(0.76, 0.16, 0.38, 0.88)}),
                 linePolyline(0.36, 0.52, 0.68, 0.52)});

    // 8
    const QVector<QVector<QPointF>> d8Canon = {
        joinPolylines({arcPolyline(0.50, 0.31, 0.22, 0.17, -90, 90, 12),
                       arcPolyline(0.50, 0.68, 0.25, 0.20, -90, -270, 14),
                       arcPolyline(0.50, 0.68, 0.25, 0.20, 90, -90, 12),
                       arcPolyline(0.50, 0.31, 0.22, 0.17, 90, 270, 12)}),
    };
    addTemplate("8", true, d8Canon, d8Canon);

    // 9
    const QVector<QVector<QPointF>> d9Canon = {
        joinPolylines({arcPolyline(0.48, 0.34, 0.24, 0.20, 0, -360, 18),
                       linePolyline(0.72, 0.34, 0.66, 0.88)}),
    };
    addTemplate("9", true, d9Canon, d9Canon);

    // ==================== LETRAS COMUNES PARA "PASAR A TEXTO" ====================
    // (Solo usadas en recognizeStrokesToText; mathAndDigitsOnly=false)
    const QVector<QVector<QPointF>> lA = {
        joinPolylines({linePolyline(0.20, 0.86, 0.50, 0.14), linePolyline(0.50, 0.14, 0.80, 0.86)}),
        linePolyline(0.30, 0.60, 0.70, 0.60),
    };
    addTemplate("A", false, lA, lA);

    const QVector<QVector<QPointF>> lC = {arcPolyline(0.54, 0.50, 0.30, 0.36, -45, -315, 18)};
    addTemplate("C", false, lC, lC);

    const QVector<QVector<QPointF>> lE = {
        joinPolylines({linePolyline(0.74, 0.16, 0.28, 0.16),
                       linePolyline(0.28, 0.16, 0.28, 0.84),
                       linePolyline(0.28, 0.84, 0.76, 0.84)}),
        linePolyline(0.28, 0.50, 0.68, 0.50),
    };
    addTemplate("E", false, lE, lE);

    const QVector<QVector<QPointF>> lH = {
        linePolyline(0.26, 0.14, 0.26, 0.86),
        linePolyline(0.74, 0.14, 0.74, 0.86),
        linePolyline(0.26, 0.50, 0.74, 0.50),
    };
    addTemplate("H", false, lH, lH);

    const QVector<QVector<QPointF>> lL = {
        joinPolylines({linePolyline(0.30, 0.14, 0.30, 0.86), linePolyline(0.30, 0.86, 0.76, 0.86)}),
    };
    addTemplate("L", false, lL, lL);

    const QVector<QVector<QPointF>> lM = {
        joinPolylines({linePolyline(0.18, 0.86, 0.24, 0.16),
                       linePolyline(0.24, 0.16, 0.50, 0.58),
                       linePolyline(0.50, 0.58, 0.76, 0.16),
                       linePolyline(0.76, 0.16, 0.82, 0.86)}),
    };
    addTemplate("M", false, lM, lM);

    const QVector<QVector<QPointF>> lN = {
        joinPolylines({linePolyline(0.24, 0.86, 0.24, 0.16),
                       linePolyline(0.24, 0.16, 0.76, 0.86),
                       linePolyline(0.76, 0.86, 0.76, 0.16)}),
    };
    addTemplate("N", false, lN, lN);

    const QVector<QVector<QPointF>> lO = {arcPolyline(0.50, 0.50, 0.34, 0.36, -90, 270, 20)};
    addTemplate("O", false, lO, lO);

    const QVector<QVector<QPointF>> lS = {
        joinPolylines({arcPolyline(0.50, 0.31, 0.24, 0.17, -30, -250, 12),
                       arcPolyline(0.50, 0.68, 0.25, 0.19, -80, 155, 14)}),
    };
    addTemplate("S", false, lS, lS);

    const QVector<QVector<QPointF>> lT = {
        linePolyline(0.20, 0.18, 0.80, 0.18),
        linePolyline(0.50, 0.18, 0.50, 0.86),
    };
    addTemplate("T", false, lT, lT);

    const QVector<QVector<QPointF>> lU = {
        joinPolylines({linePolyline(0.24, 0.16, 0.24, 0.64),
                       arcPolyline(0.50, 0.64, 0.26, 0.22, 180, 0, 12),
                       linePolyline(0.76, 0.64, 0.76, 0.16)}),
    };
    addTemplate("U", false, lU, lU);

    const QVector<QVector<QPointF>> lV = {
        joinPolylines({linePolyline(0.22, 0.18, 0.50, 0.86), linePolyline(0.50, 0.86, 0.78, 0.18)}),
    };
    addTemplate("V", false, lV, lV);

    const QVector<QVector<QPointF>> lX = {
        linePolyline(0.22, 0.18, 0.78, 0.84),
        linePolyline(0.78, 0.18, 0.22, 0.84),
    };
    addTemplate("x", false, lX, lX);

    const QVector<QVector<QPointF>> lY = {
        linePolyline(0.22, 0.18, 0.50, 0.52),
        linePolyline(0.78, 0.18, 0.34, 0.88),
    };
    addTemplate("y", false, lY, lY);

    const QVector<QVector<QPointF>> lZ = {
        joinPolylines({linePolyline(0.22, 0.18, 0.78, 0.18),
                       linePolyline(0.78, 0.18, 0.22, 0.84),
                       linePolyline(0.22, 0.84, 0.78, 0.84)}),
    };
    addTemplate("z", false, lZ, lZ);
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

    // Replace previous custom template with the same label if any.
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
                                                      bool mathAndDigitsOnly) const {
    RecognitionResult result;
    if (rawStrokes.isEmpty() || rawStrokes.size() > 4)
        return result;

    // Special ultra-clean geometric check for simple minus '−' (1 flat horizontal line)
    if (rawStrokes.size() == 1 && rawStrokes[0].size() >= 2) {
        const QRectF box = strokesBoundingBox(rawStrokes);
        if (box.width() > 14.0 && box.height() <= box.width() * 0.22) {
            const double direct = dist2D(rawStrokes[0].first(), rawStrokes[0].back());
            if (direct > box.width() * 0.85) {
                result.matched = true;
                result.label = QStringLiteral("−");
                result.score = 0.94;
                result.canonicalStrokes = {linePolyline(0.05, 0.50, 0.95, 0.50)};
                return result;
            }
        }
    }

    double inputAspect = 1.0;
    const auto inputCloud = normalizeToCloud(rawStrokes, kCloudSamples, &inputAspect);
    if (inputCloud.isEmpty())
        return result;

    double bestDist = std::numeric_limits<double>::max();
    const GlyphTemplate* bestTpl = nullptr;

    for (const auto& tpl : m_templates) {
        if (mathAndDigitsOnly && !tpl.isMathOrDigit)
            continue;
        if (tpl.strokeCount != rawStrokes.size())
            continue;

        // Aspect ratio compatibility check prevents confusing e.g. '1' with '0' or '='
        const double ratioDiff = std::abs(std::log(inputAspect / std::max(tpl.aspectRatio, 0.1)));
        if (ratioDiff > 1.15)
            continue;

        double d = cloudDistance(inputCloud, tpl.cloud) + 0.04 * ratioDiff;
        // Slight preference for user's own custom templates
        if (tpl.isCustom)
            d *= 0.90;

        if (d < bestDist) {
            bestDist = d;
            bestTpl = &tpl;
        }
    }

    if (!bestTpl)
        return result;

    // Convert normalized cloud distance (~0.06..0.25) to intuitive confidence [0, 1]
    const double confidence = std::clamp(1.0 - (bestDist / 0.34), 0.0, 1.0);
    if (confidence >= 0.45) {
        result.matched = true;
        result.label = bestTpl->label;
        result.score = confidence;
        result.canonicalStrokes = bestTpl->canonicalStrokes;
    }
    return result;
}

bool HandwritingEngine::beautifyRecentStrokes(QVector<Stroke>& strokes,
                                              QString* recognizedLabel) const {
    if (strokes.isEmpty())
        return false;

    const Stroke& newest = strokes.back();
    if (newest.tool != ToolType::Pen && newest.tool != ToolType::Ballpoint)
        return false;
    if (newest.points.size() < 2)
        return false;

    // Never beautify huge drawings (diagrams/boxes) or tiny accidental dots
    const QRectF newestBox = newest.bounds;
    const double newestDim = std::max(newestBox.width(), newestBox.height());
    if (newestDim < 12.0 || newestDim > 180.0)
        return false;

    // Check 3-stroke, 2-stroke, and 1-stroke clusters ending at strokes.back()
    // Prioritize multi-stroke symbols (=, +, ×, ≠, ≤, ≥, →, π, θ, 4) over single strokes
    for (int k = std::min(3, int(strokes.size())); k >= 1; --k) {
        const int startIdx = int(strokes.size()) - k;
        bool clusterValid = true;
        QRectF clusterBox = strokes[startIdx].bounds;

        for (int i = startIdx + 1; i < int(strokes.size()); ++i) {
            const Stroke& prev = strokes[i - 1];
            const Stroke& curr = strokes[i];
            if (curr.tool != newest.tool || curr.color != newest.color) {
                clusterValid = false;
                break;
            }
            const qint64 tPrev = prev.points.isEmpty() ? 0 : prev.points.back().timestampMs;
            const qint64 tCurr = curr.points.isEmpty() ? 0 : curr.points.first().timestampMs;
            if (tPrev > 0 && tCurr > 0 && (tCurr - tPrev) > 1300) {
                clusterValid = false;
                break;
            }
            const double pad = std::max(14.0, std::max(clusterBox.width(), clusterBox.height()) * 0.45);
            if (!clusterBox.adjusted(-pad, -pad, pad, pad).intersects(curr.bounds)) {
                clusterValid = false;
                break;
            }
            clusterBox = clusterBox.united(curr.bounds);
        }
        if (!clusterValid)
            continue;

        const double clusterDim = std::max(clusterBox.width(), clusterBox.height());
        if (clusterDim < 14.0 || clusterDim > 185.0)
            continue;

        QVector<QVector<QPointF>> raw;
        raw.reserve(k);
        for (int i = startIdx; i < int(strokes.size()); ++i)
            raw.append(extractRawPoints(strokes[i]));

        const RecognitionResult rec = recognizeCluster(raw, true);
        // High confidence threshold: never mangle freehand sketches or cursive words!
        // Single strokes require slightly higher confidence than multi-stroke math symbols.
        const double minScore = (k == 1) ? 0.68 : 0.62;
        if (!rec.matched || rec.score < minScore)
            continue;

        // Skip single-stroke minus '−' during live writing so underline / fraction bars aren't touched
        if (k == 1 && rec.label == QLatin1String("−"))
            continue;

        // Map canonical normalized strokes [0,1]x[0,1] into the cluster's exact world bounding box
        const QRectF rawBox = strokesBoundingBox(raw);
        const QRectF canonBox = strokesBoundingBox(rec.canonicalStrokes);
        const double cw = std::max(canonBox.width(), 1e-4);
        const double ch = std::max(canonBox.height(), 1e-4);
        const double targetW = std::max(rawBox.width(), 8.0);
        const double targetH = std::max(rawBox.height(), 8.0);
        const double scale = std::min(targetW / cw, targetH / ch);
        const QPointF targetCenter = rawBox.center();
        const QPointF canonCenter = canonBox.center();

        QVector<Stroke> cleanStrokes;
        cleanStrokes.reserve(rec.canonicalStrokes.size());
        const qint64 baseTs = newest.points.back().timestampMs;

        for (const auto& cs : rec.canonicalStrokes) {
            Stroke s;
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            s.tool = newest.tool;
            s.color = newest.color;
            s.size = newest.size;
            s.opacity = newest.opacity;
            s.linePattern = newest.linePattern;
            s.pressureProfile = newest.pressureProfile;
            s.hasHardwarePressure = false;
            s.points.reserve(cs.size());
            for (int pi = 0; pi < cs.size(); ++pi) {
                const QPointF mapped = targetCenter + (cs[pi] - canonCenter) * scale;
                StrokePoint sp;
                sp.x = mapped.x();
                sp.y = mapped.y();
                sp.pressure = 0.55;
                sp.timestampMs = baseTs + pi;
                s.points.append(sp);
            }
            s.bounds = computeBounds(s.points, s.size * 0.7);
            cleanStrokes.append(std::move(s));
        }

        strokes.erase(strokes.begin() + startIdx, strokes.end());
        for (auto& cs : cleanStrokes)
            strokes.append(std::move(cs));

        if (recognizedLabel)
            *recognizedLabel = rec.label;
        return true;
    }

    return false;
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

    // Compute median stroke height to guide line & character grouping
    QVector<double> heights;
    heights.reserve(items.size());
    for (const auto& it : items)
        heights.append(std::max(it.box.height(), 10.0));
    std::sort(heights.begin(), heights.end());
    const double medianH = heights[heights.size() / 2];

    // Group strokes into lines by vertical center proximity
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

        // Cluster strokes belonging to the same character (overlapping or very close horizontally)
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
                // Merge if horizontal overlap > 25% OR boxes touch & combined width stays character-sized
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
            if (ci > 0) {
                const double gap = clusters[ci].box.left() - clusters[ci - 1].box.right();
                if (gap > std::max(medianH * 0.55, 16.0))
                    lineText += QLatin1Char(' ');
            }
            const RecognitionResult rec = recognizeCluster(clusters[ci].rawStrokes, false);
            if (rec.matched)
                lineText += rec.label;
            else
                lineText += QLatin1Char('?');
        }
        if (!lineText.trimmed().isEmpty())
            outputLines.append(lineText.trimmed());
    }

    return outputLines.join(QLatin1Char('\n'));
}

} // namespace finily
