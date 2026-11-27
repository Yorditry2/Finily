#include "engine/LiveStroke.h"

#include "engine/InkEngine.h"

#include <algorithm>
#include <cmath>

namespace finily {
namespace {

constexpr double kTailLength = 90.0; // unidades del mundo que pueden cambiar al final
constexpr double kPredictMs = 10.0;  // cuánto adelantar la punta (~1 cuadro de pantalla)
constexpr double kWindowMs = 24.0;   // ventana para estimar la velocidad

bool predictable(const Stroke& s) {
    return (s.tool == ToolType::Pen || s.tool == ToolType::Ballpoint) && s.points.size() >= 3;
}

double segLen(const StrokePoint& a, const StrokePoint& b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

double angleBetween(double ax, double ay, double bx, double by) {
    const double la = std::hypot(ax, ay);
    const double lb = std::hypot(bx, by);
    if (la < 1e-9 || lb < 1e-9) {
        return 0.0;
    }
    return std::acos(std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0));
}

// Primer índice (contando desde 0) a partir del cual sobra el final de `pts`, o -1.
qsizetype hookStart(const QVector<StrokePoint>& pts, double maxHook, double total) {
    const qsizetype n = pts.size();
    double typical = 0.0; // presión típica: mediana del trazo
    {
        QVector<double> pr;
        pr.reserve(n);
        for (const auto& p : pts) {
            pr.append(p.pressure);
        }
        std::nth_element(pr.begin(), pr.begin() + n / 2, pr.end());
        typical = pr[n / 2];
    }
    if (typical < 0.05) {
        return -1;
    }

    qsizetype best = -1;
    double bestAngle = 0.0;
    double hook = 0.0;
    for (qsizetype k = n - 2; k >= 1; --k) {
        hook += segLen(pts[k], pts[k + 1]);
        if (hook > maxHook || hook > total * 0.2) {
            break;
        }
        // Dirección de entrada: una ventana del doble del gancho antes de k, en dos mitades.
        const double win = std::max(hook * 2.0, maxHook);
        qsizetype j = k;
        double walked = 0.0;
        double half = -1.0;
        qsizetype mid = k;
        while (j > 0 && walked < win) {
            walked += segLen(pts[j - 1], pts[j]);
            --j;
            if (half < 0 && walked >= win / 2) {
                half = walked;
                mid = j;
            }
        }
        if (walked < win * 0.8 || mid == k || mid == j) {
            continue;
        }
        const double inX = pts[k].x - pts[mid].x;
        const double inY = pts[k].y - pts[mid].y;
        const double in0X = pts[mid].x - pts[j].x;
        const double in0Y = pts[mid].y - pts[j].y;
        const double outX = pts[n - 1].x - pts[k].x;
        const double outY = pts[n - 1].y - pts[k].y;
        const double corner = angleBetween(inX, inY, outX, outY);
        const double bend = angleBetween(in0X, in0Y, inX, inY); // curvatura previa (no cortar curvas)
        // El gancho es casi recto (no un rizo) y nace con la presión ya cayendo.
        const double chord = std::hypot(outX, outY);
        double pressure = 0.0;
        for (qsizetype i = k + 1; i < n; ++i) {
            pressure += pts[i].pressure;
        }
        pressure /= double(n - k - 1);
        const double inChord = std::hypot(pts[k].x - pts[j].x, pts[k].y - pts[j].y);
        const bool sharp =
            corner > 0.87 /* 50° */ && bend < corner * 0.35 && inChord > walked * 0.9;
        if (sharp && chord > hook * 0.8 && pressure < typical * 0.75 && corner > bestAngle) {
            best = k;
            bestAngle = corner;
        }
    }
    return best >= 0 ? best + 1 : -1; // se conserva el vértice k
}

} // namespace

std::optional<StrokePoint> predictTip(const Stroke& stroke, double zoom) {
    if (!predictable(stroke) || zoom <= 0.0) {
        return std::nullopt;
    }
    const auto& pts = stroke.points;
    const auto& last = pts.back();
    // Punto más antiguo dentro de la ventana (mínimo 2 intervalos atrás).
    qsizetype k = pts.size() - 3;
    while (k > 0 && last.timestampMs - pts[k - 1].timestampMs <= kWindowMs) {
        --k;
    }
    const double dt = double(last.timestampMs - pts[k].timestampMs);
    if (dt <= 0.0 || dt > 80.0) { // sin tiempos fiables o lápiz detenido: no se adivina
        return std::nullopt;
    }
    const double vx = (last.x - pts[k].x) / dt;
    const double vy = (last.y - pts[k].y) / dt;
    // En curvas cerradas se amortigua según el giro del último tramo.
    const auto& prev = pts[pts.size() - 2];
    const double sx = last.x - prev.x;
    const double sy = last.y - prev.y;
    const double sl = std::hypot(sx, sy);
    const double vl = std::hypot(vx, vy);
    if (sl < 1e-9 || vl < 1e-9) {
        return std::nullopt;
    }
    const double align = (sx * vx + sy * vy) / (sl * vl);
    if (align <= 0.5) {
        return std::nullopt;
    }
    const double damp = (align - 0.5) / 0.5;
    // Predicción en la dirección del último tramo con la rapidez de la ventana.
    double dx = sx / sl * vl * kPredictMs * damp;
    double dy = sy / sl * vl * kPredictMs * damp;
    const double len = std::hypot(dx, dy);
    const double cap = 14.0 / zoom;
    if (len < 1e-6) {
        return std::nullopt;
    }
    if (len > cap) {
        dx *= cap / len;
        dy *= cap / len;
    }
    StrokePoint p = last;
    p.x += dx;
    p.y += dy;
    p.timestampMs = last.timestampMs + qint64(kPredictMs);
    return p;
}

QRectF liveDirtyRect(const Stroke& stroke, double zoom) {
    if (stroke.points.isEmpty()) {
        return {};
    }
    // QRectF::united() ignora rectángulos vacíos, así que se acumula min/max manualmente.
    double l = stroke.points.back().x;
    double r = l;
    double t = stroke.points.back().y;
    double b = t;
    auto add = [&](double x, double y) {
        l = std::min(l, x);
        r = std::max(r, x);
        t = std::min(t, y);
        b = std::max(b, y);
    };
    double walked = 0.0;
    for (qsizetype i = stroke.points.size() - 1; i >= 0; --i) {
        add(stroke.points[i].x, stroke.points[i].y);
        if (i + 1 < stroke.points.size()) {
            walked += std::hypot(stroke.points[i + 1].x - stroke.points[i].x,
                                 stroke.points[i + 1].y - stroke.points[i].y);
        }
        // >= 8 puntos: el suavizado centrado aún mueve los últimos InkSmoother::kRadius.
        if (walked >= kTailLength && stroke.points.size() - i >= 8) {
            break;
        }
    }
    if (const auto tip = predictTip(stroke, zoom)) {
        add(tip->x, tip->y);
    }
    const double pad = stroke.size * 1.5 + 3.0 + 1.0 / std::max(zoom, 1e-6);
    return QRectF(QPointF(l - pad, t - pad), QPointF(r + pad, b + pad));
}

Stroke withPredictedTip(const Stroke& stroke, double zoom) {
    Stroke copy = stroke;
    if (const auto tip = predictTip(stroke, zoom)) {
        copy.points.append(*tip);
    }
    return copy;
}

bool trimPenHooks(Stroke& stroke, double zoom) {
    if (!stroke.hasHardwarePressure || stroke.points.size() < 8) {
        return false;
    }
    double total = 0.0;
    for (qsizetype i = 1; i < stroke.points.size(); ++i) {
        total += segLen(stroke.points[i - 1], stroke.points[i]);
    }
    const double maxHook = 6.0 / std::max(zoom, 1e-6); // ~6 px de pantalla
    if (total < maxHook * 5.0) {                       // puntos y tildes no se recortan
        return false;
    }
    bool trimmed = false;
    if (const qsizetype cut = hookStart(stroke.points, maxHook, total); cut > 0) {
        stroke.points.resize(cut);
        trimmed = true;
    }
    QVector<StrokePoint> rev(stroke.points.rbegin(), stroke.points.rend());
    if (const qsizetype cut = hookStart(rev, maxHook, total); cut > 0) {
        stroke.points.remove(0, stroke.points.size() - cut);
        trimmed = true;
    }
    return trimmed;
}

bool taperPenFlick(Stroke& stroke, double zoom) {
    auto& pts = stroke.points;
    if (!stroke.hasHardwarePressure || pts.size() < 6 || zoom <= 0.0) {
        return false;
    }
    // Velocidad al soltar (px de pantalla/ms) en los últimos ~30 ms.
    const qint64 tEnd = pts.back().timestampMs;
    qsizetype j = pts.size() - 1;
    while (j > 0 && tEnd - pts[j - 1].timestampMs <= 30) {
        --j;
    }
    const qint64 dt = tEnd - pts[j].timestampMs;
    if (dt < 6 || pts.size() - 1 - j < 2) {
        return false;
    }
    double travel = 0.0;
    for (qsizetype k = j + 1; k < pts.size(); ++k) {
        travel += std::hypot(pts[k].x - pts[k - 1].x, pts[k].y - pts[k - 1].y);
    }
    const double speed = travel * zoom / double(dt);
    // < 0.5 px/ms: soltó con calma. >= ~1.7 px/ms: remate completo.
    const double k = std::clamp((speed - 0.5) / 1.2, 0.0, 1.0);
    if (k <= 0.0) {
        return false;
    }
    QVector<double> dist(pts.size(), 0.0);
    for (qsizetype i = 1; i < pts.size(); ++i) {
        dist[i] = dist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    const double total = dist.back();
    const double taper = std::min({k * stroke.size * 5.0, k * 40.0 / zoom, total * 0.3});
    if (taper * zoom < 2.0) {
        return false;
    }
    QVector<double> pressures;
    pressures.reserve(pts.size());
    for (const auto& p : pts) {
        pressures.append(p.pressure);
    }
    std::nth_element(pressures.begin(), pressures.begin() + pressures.size() / 2, pressures.end());
    const double median = pressures[pressures.size() / 2];
    bool changed = false;
    for (qsizetype i = pts.size() - 1; i >= 0 && total - dist[i] < taper; --i) {
        const double t = (total - dist[i]) / taper;
        const double s = t * t * (3.0 - 2.0 * t);
        // Tope en lugar de factor para no adelgazar doblemente si la presión ya bajaba.
        const double cap = median * (0.03 + 0.97 * s);
        if (pts[i].pressure > cap) {
            pts[i].pressure = cap;
            changed = true;
        }
    }
    return changed;
}

void InkSmoother::reset(double sigmaWorld, int radius) {
    m_raw.clear();
    m_sigma = sigmaWorld;
    m_radius = std::clamp(radius, 1, kMaxRadius);
}

void InkSmoother::push(Stroke& stroke) {
    auto& pts = stroke.points;
    if (pts.isEmpty()) {
        m_raw.clear();
        return;
    }
    // Si el trazo fue reescrito por fuera (p. ej. Shift), reconstruir desde pts.
    if (m_raw.size() != pts.size() - 1) {
        m_raw.clear();
        for (qsizetype i = 0; i + 1 < pts.size(); ++i) {
            m_raw.append({pts[i].x, pts[i].y});
        }
    }
    m_raw.append({pts.back().x, pts.back().y});
    if (m_sigma <= 0.0) {
        return;
    }
    const qsizetype n = m_raw.size();
    for (qsizetype i = std::max<qsizetype>(1, n - 1 - m_radius); i < n; ++i) {
        const qsizetype r = std::min<qsizetype>({qsizetype(m_radius), i, n - 1 - i});
        if (r <= 0) {
            pts[i].x = m_raw[i].x();
            pts[i].y = m_raw[i].y();
            continue;
        }

        // Proteger esquinas agudas reduciendo sigma según el giro entre entrante y saliente.
        double localSigma = m_sigma;
        const QPointF vIn = m_raw[i] - m_raw[i - 1];
        const QPointF vOut = m_raw[i + 1] - m_raw[i];
        const double lIn = std::hypot(vIn.x(), vIn.y());
        const double lOut = std::hypot(vOut.x(), vOut.y());
        if (lIn > 1e-6 && lOut > 1e-6) {
            const double cosTheta = (vIn.x() * vOut.x() + vIn.y() * vOut.y()) / (lIn * lOut);
            if (cosTheta < 0.35) {
                const double sharpness = std::clamp((0.35 - cosTheta) / 0.45, 0.0, 1.0);
                localSigma *= (1.0 - 0.90 * sharpness);
            }
        }
        if (localSigma <= 1e-6) {
            pts[i].x = m_raw[i].x();
            pts[i].y = m_raw[i].y();
            continue;
        }

        // Ventana simétrica para no arrastrar puntos cercanos a los extremos hacia adentro.
        double sx = m_raw[i].x();
        double sy = m_raw[i].y();
        double sw = 1.0;
        for (int side : {-1, 1}) {
            double s = 0.0;
            for (qsizetype d = 1; d <= r; ++d) {
                const QPointF a = m_raw[i + side * (d - 1)];
                const QPointF b = m_raw[i + side * d];
                s += std::hypot(b.x() - a.x(), b.y() - a.y());
                const double w = std::exp(-0.5 * s * s / (localSigma * localSigma));
                sx += b.x() * w;
                sy += b.y() * w;
                sw += w;
            }
        }
        pts[i].x = sx / sw;
        pts[i].y = sy / sw;
    }
}

} // namespace finily
