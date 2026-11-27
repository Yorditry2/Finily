#include "engine/ImageVectorizer.h"

#include "engine/GeometryEngine.h"

#include <QElapsedTimer>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>
#include <vector>

namespace finily {

namespace {

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

double ptDist(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double ptToSegDist(const QPointF& p, const QPointF& a, const QPointF& b, double* outT = nullptr) {
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-9) {
        if (outT)
            *outT = 0.0;
        return ptDist(p, a);
    }
    const double t = std::clamp(((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2, 0.0, 1.0);
    if (outT)
        *outT = t;
    const QPointF proj(a.x() + t * dx, a.y() + t * dy);
    return ptDist(p, proj);
}

// Umbral de Otsu sobre el histograma de contraste [0..255]
int computeOtsuThreshold(const std::array<int, 256>& hist, int total) {
    if (total <= 0)
        return 48;
    double sumAll = 0.0;
    for (int i = 0; i < 256; ++i)
        sumAll += double(i) * double(hist[i]);

    double sumB = 0.0;
    int wB = 0;
    double maxVar = -1.0;
    int bestT = 48;
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0)
            continue;
        const int wF = total - wB;
        if (wF == 0)
            break;
        sumB += double(t) * double(hist[t]);
        const double mB = sumB / double(wB);
        const double mF = (sumAll - sumB) / double(wF);
        const double diff = mB - mF;
        const double varBetween = double(wB) * double(wF) * diff * diff;
        if (varBetween > maxVar) {
            maxVar = varBetween;
            bestT = t;
        }
    }
    return std::clamp(bestT, 18, 115);
}

// Construye máscara binaria del primer plano usando normalización local de iluminación
// mediante Imagen Integral 2D (inmune a sombras de fotos de libros y degradados, <3 ms).
std::vector<uint8_t> buildForegroundMask(const QImage& gray, int& outW, int& outH) {
    outW = gray.width();
    outH = gray.height();
    if (outW < 4 || outH < 4)
        return {};

    // Mediana de los bordes para detectar polaridad (papel claro vs pizarra oscura)
    std::vector<int> borderSamples;
    borderSamples.reserve(size_t(outW + outH) * 2);
    for (int x = 0; x < outW; x += 2) {
        borderSamples.push_back(gray.constScanLine(0)[x]);
        borderSamples.push_back(gray.constScanLine(outH - 1)[x]);
    }
    for (int y = 0; y < outH; y += 2) {
        const uchar* row = gray.constScanLine(y);
        borderSamples.push_back(row[0]);
        borderSamples.push_back(row[outW - 1]);
    }
    std::nth_element(borderSamples.begin(), borderSamples.begin() + borderSamples.size() / 2, borderSamples.end());
    const int bgLum = borderSamples[borderSamples.size() / 2];
    const bool isLightBg = (bgLum >= 110);

    // Imagen integral 2D (Summed-Area Table) para media local en ventana 35x35 en O(1) por píxel
    const int stride = outW + 1;
    std::vector<uint64_t> sat(size_t(outH + 1) * size_t(stride), 0);
    for (int y = 0; y < outH; ++y) {
        const uchar* row = gray.constScanLine(y);
        uint64_t rowSum = 0;
        uint64_t* dst = sat.data() + size_t(y + 1) * size_t(stride);
        const uint64_t* prev = sat.data() + size_t(y) * size_t(stride);
        for (int x = 0; x < outW; ++x) {
            rowSum += uint64_t(row[x]);
            dst[x + 1] = prev[x + 1] + rowSum;
        }
    }

    constexpr int kRadius = 17; // ventana 35x35
    std::array<int, 256> hist{};
    std::vector<uint8_t> diffImg(size_t(outW) * size_t(outH), 0);
    for (int y = 0; y < outH; ++y) {
        const int y0 = std::max(0, y - kRadius);
        const int y1 = std::min(outH - 1, y + kRadius);
        const uchar* row = gray.constScanLine(y);
        uint8_t* dst = diffImg.data() + size_t(y) * size_t(outW);
        for (int x = 0; x < outW; ++x) {
            const int x0 = std::max(0, x - kRadius);
            const int x1 = std::min(outW - 1, x + kRadius);
            const uint64_t sum = sat[size_t(y1 + 1) * size_t(stride) + size_t(x1 + 1)] -
                                 sat[size_t(y0) * size_t(stride) + size_t(x1 + 1)] -
                                 sat[size_t(y1 + 1) * size_t(stride) + size_t(x0)] +
                                 sat[size_t(y0) * size_t(stride) + size_t(x0)];
            const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
            const int localBg = int(sum / uint64_t(std::max(1, count)));
            const int lum = int(row[x]);
            const int d = isLightBg ? std::max(0, localBg - lum) : std::max(0, lum - localBg);
            const int clamped = std::clamp(d, 0, 255);
            dst[x] = uint8_t(clamped);
            hist[size_t(clamped)]++;
        }
    }

    const int thresh = computeOtsuThreshold(hist, outW * outH);
    std::vector<uint8_t> mask(size_t(outW) * size_t(outH), 0);
    for (size_t i = 0; i < diffImg.size(); ++i) {
        if (int(diffImg[i]) >= thresh)
            mask[i] = 1;
    }
    return mask;
}

// Filtra componentes conexas pequeñas (letras, números, ruido) conservando figuras geométricas
QRect filterGeometryComponents(std::vector<uint8_t>& mask, int W, int H) {
    std::vector<int> labels(size_t(W) * size_t(H), 0);
    int nextLabel = 1;

    struct CompInfo {
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
        int area = 0;
        bool keep = false;
    };
    std::vector<CompInfo> comps(1); // índice 0 sin usar

    static constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    std::queue<int> q;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int idx = y * W + x;
            if (!mask[size_t(idx)] || labels[size_t(idx)] != 0)
                continue;
            const int lbl = nextLabel++;
            CompInfo info{x, y, x, y, 0, false};
            labels[size_t(idx)] = lbl;
            q.push(idx);
            while (!q.empty()) {
                const int cur = q.front();
                q.pop();
                const int cx = cur % W;
                const int cy = cur / W;
                info.minX = std::min(info.minX, cx);
                info.maxX = std::max(info.maxX, cx);
                info.minY = std::min(info.minY, cy);
                info.maxY = std::max(info.maxY, cy);
                info.area++;
                for (int k = 0; k < 8; ++k) {
                    const int nx = cx + dx8[k];
                    const int ny = cy + dy8[k];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                        continue;
                    const int nidx = ny * W + nx;
                    if (mask[size_t(nidx)] && labels[size_t(nidx)] == 0) {
                        labels[size_t(nidx)] = lbl;
                        q.push(nidx);
                    }
                }
            }
            comps.push_back(info);
        }
    }

    const double minSide = double(std::min(W, H));
    const double minDiag = std::max(32.0, 0.065 * minSide);
    double maxKeptDiag = 0.0;

    for (size_t i = 1; i < comps.size(); ++i) {
        CompInfo& c = comps[i];
        const int bw = c.maxX - c.minX + 1;
        const int bh = c.maxY - c.minY + 1;
        const double diag = std::hypot(double(bw), double(bh));
        const double fillRatio = double(c.area) / std::max(1.0, double(bw * bh));

        const bool isThinLine = (bw >= 30 && bh <= 8) || (bh >= 30 && bw <= 8);
        const bool isSolidBlob = (fillRatio > 0.72 && bw > 10 && bh > 10 && diag < 70.0);
        const bool isTextWord = (bh >= 8 && bh <= 26 && bw > bh * 1.4 && fillRatio > 0.24 && diag < minSide * 0.45);
        const bool isBorderFrame = (bw > W * 0.96 && bh > H * 0.96 && fillRatio < 0.06);

        if (!isSolidBlob && !isBorderFrame && (isThinLine || (diag >= minDiag && c.area >= 24 && !isTextWord))) {
            c.keep = true;
            maxKeptDiag = std::max(maxKeptDiag, diag);
        }
    }

    // Segunda pasada: si existe una figura geométrica dominante grande, descartar
    // componentes pequeñas aisladas (letras sueltas como "V", "t", flechitas de anotación)
    const double relMinDiag = std::max(minDiag, maxKeptDiag * 0.24);
    int dMinX = W, dMinY = H, dMaxX = -1, dMaxY = -1;
    for (size_t i = 1; i < comps.size(); ++i) {
        CompInfo& c = comps[i];
        if (!c.keep)
            continue;
        const int bw = c.maxX - c.minX + 1;
        const int bh = c.maxY - c.minY + 1;
        const double diag = std::hypot(double(bw), double(bh));
        if (diag < relMinDiag) {
            c.keep = false;
            continue;
        }
        dMinX = std::min(dMinX, c.minX);
        dMinY = std::min(dMinY, c.minY);
        dMaxX = std::max(dMaxX, c.maxX);
        dMaxY = std::max(dMaxY, c.maxY);
    }

    for (size_t i = 0; i < mask.size(); ++i) {
        const int lbl = labels[i];
        if (lbl == 0 || !comps[size_t(lbl)].keep)
            mask[i] = 0;
    }

    if (dMaxX < dMinX || dMaxY < dMinY)
        return {};
    return QRect(dMinX, dMinY, dMaxX - dMinX + 1, dMaxY - dMinY + 1);
}

// Esqueletización Zhang-Suen (reduce líneas a 1 px de grosor manteniendo topología)
void zhangSuenThinning(std::vector<uint8_t>& img, int W, int H, const QRect& roi) {
    if (roi.isEmpty())
        return;
    const int x0 = std::max(1, roi.left());
    const int y0 = std::max(1, roi.top());
    const int x1 = std::min(W - 2, roi.right());
    const int y1 = std::min(H - 2, roi.bottom());

    std::vector<int> toRemove;
    toRemove.reserve(4096);

    for (int iter = 0; iter < 60; ++iter) {
        bool changed = false;
        for (int step = 0; step < 2; ++step) {
            toRemove.clear();
            for (int y = y0; y <= y1; ++y) {
                const uint8_t* prev = img.data() + size_t(y - 1) * size_t(W);
                const uint8_t* curr = img.data() + size_t(y) * size_t(W);
                const uint8_t* next = img.data() + size_t(y + 1) * size_t(W);
                for (int x = x0; x <= x1; ++x) {
                    if (!curr[x])
                        continue;
                    const uint8_t p2 = prev[x];
                    const uint8_t p3 = prev[x + 1];
                    const uint8_t p4 = curr[x + 1];
                    const uint8_t p5 = next[x + 1];
                    const uint8_t p6 = next[x];
                    const uint8_t p7 = next[x - 1];
                    const uint8_t p8 = curr[x - 1];
                    const uint8_t p9 = prev[x - 1];

                    const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (B < 2 || B > 6)
                        continue;

                    const int A = (p2 == 0 && p3 == 1) + (p3 == 0 && p4 == 1) +
                                  (p4 == 0 && p5 == 1) + (p5 == 0 && p6 == 1) +
                                  (p6 == 0 && p7 == 1) + (p7 == 0 && p8 == 1) +
                                  (p8 == 0 && p9 == 1) + (p9 == 0 && p2 == 1);
                    if (A != 1)
                        continue;

                    if (step == 0) {
                        if (p2 * p4 * p6 != 0 || p4 * p6 * p8 != 0)
                            continue;
                    } else {
                        if (p2 * p4 * p8 != 0 || p2 * p6 * p8 != 0)
                            continue;
                    }
                    toRemove.push_back(y * W + x);
                }
            }
            if (!toRemove.empty()) {
                changed = true;
                for (int idx : toRemove)
                    img[size_t(idx)] = 0;
            }
        }
        if (!changed)
            break;
    }

    // Limpieza de píxeles redundantes en esquinas en L (deja curvas de 1 px estricto sin falsas uniones)
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!img[size_t(y * W + x)])
                continue;
            const uint8_t n = img[size_t((y - 1) * W + x)];
            const uint8_t s = img[size_t((y + 1) * W + x)];
            const uint8_t e = img[size_t(y * W + x + 1)];
            const uint8_t w = img[size_t(y * W + x - 1)];
            const uint8_t ne = img[size_t((y - 1) * W + x + 1)];
            const uint8_t nw = img[size_t((y - 1) * W + x - 1)];
            const uint8_t se = img[size_t((y + 1) * W + x + 1)];
            const uint8_t sw = img[size_t((y + 1) * W + x - 1)];
            if ((n && e && !sw && !s && !w) || (e && s && !nw && !n && !w) ||
                (s && w && !ne && !n && !e) || (w && n && !se && !s && !e)) {
                img[size_t(y * W + x)] = 0;
            }
        }
    }

    // Podar pequeños espolones de 1..4 px causados por antialiasing en bordes curvos
    static constexpr int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int p = 0; p < 4; ++p) {
        toRemove.clear();
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (!img[size_t(y * W + x)])
                    continue;
                int deg = 0;
                for (int k = 0; k < 8; ++k)
                    deg += img[size_t((y + dy8[k]) * W + (x + dx8[k]))];
                if (deg == 1)
                    toRemove.push_back(y * W + x);
            }
        }
        if (toRemove.empty())
            break;
        for (int idx : toRemove)
            img[size_t(idx)] = 0;
    }
}

struct SkeletonChain {
    QVector<QPointF> points;
    bool closedLoop = false;
};

// Extrae cadenas ordenadas de píxeles entre extremos, uniones y lazos cerrados
QVector<SkeletonChain> extractSkeletonChains(const std::vector<uint8_t>& skel, int W, int H, const QRect& roi) {
    QVector<SkeletonChain> chains;
    if (roi.isEmpty())
        return chains;

    static constexpr int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static constexpr int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    const int x0 = std::max(0, roi.left());
    const int y0 = std::max(0, roi.top());
    const int x1 = std::min(W - 1, roi.right());
    const int y1 = std::min(H - 1, roi.bottom());

    auto neighborsOf = [&](int x, int y, int outIdx[8]) -> int {
        int count = 0;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx8[k];
            const int ny = y + dy8[k];
            if (nx >= 0 && nx < W && ny >= 0 && ny < H && skel[size_t(ny * W + nx)])
                outIdx[count++] = ny * W + nx;
        }
        return count;
    };

    // Clasificar nodos usando el Número de Cruces (Crossing Number) en el anillo de 8 vecinos:
    // CN == 1 -> extremo real; CN >= 3 -> unión real (evita falsas uniones en escaleras diagonales/círculos)
    auto crossingNumber = [&](int x, int y) -> int {
        uint8_t r[8];
        for (int k = 0; k < 8; ++k) {
            const int nx = x + dx8[k];
            const int ny = y + dy8[k];
            r[k] = (nx >= 0 && nx < W && ny >= 0 && ny < H && skel[size_t(ny * W + nx)]) ? 1 : 0;
        }
        int cn = 0;
        for (int k = 0; k < 8; ++k) {
            if (r[k] == 0 && r[(k + 1) % 8] == 1)
                ++cn;
        }
        return cn;
    };

    std::vector<uint8_t> isNode(size_t(W) * size_t(H), 0);
    std::vector<int> nodeId(size_t(W) * size_t(H), -1);
    QVector<QPointF> nodeCenters;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int idx = y * W + x;
            if (!skel[size_t(idx)])
                continue;
            const int cn = crossingNumber(x, y);
            if (cn == 1 || cn >= 3)
                isNode[size_t(idx)] = (cn >= 3) ? 2 : 1;
        }
    }

    // Agrupar píxeles adyacentes de unión en un único nodo centroide
    std::queue<int> q;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int idx = y * W + x;
            if (isNode[size_t(idx)] == 0 || nodeId[size_t(idx)] >= 0)
                continue;
            const int nid = int(nodeCenters.size());
            double sx = 0.0, sy = 0.0;
            int cnt = 0;
            if (isNode[size_t(idx)] == 1) {
                nodeId[size_t(idx)] = nid;
                nodeCenters.push_back(QPointF(x, y));
                continue;
            }
            nodeId[size_t(idx)] = nid;
            q.push(idx);
            while (!q.empty()) {
                const int cur = q.front();
                q.pop();
                const int cx = cur % W;
                const int cy = cur / W;
                sx += cx;
                sy += cy;
                cnt++;
                for (int k = 0; k < 8; ++k) {
                    const int nx = cx + dx8[k];
                    const int ny = cy + dy8[k];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
                        continue;
                    const int nidx = ny * W + nx;
                    if (isNode[size_t(nidx)] == 2 && nodeId[size_t(nidx)] < 0) {
                        nodeId[size_t(nidx)] = nid;
                        q.push(nidx);
                    }
                }
            }
            nodeCenters.push_back(QPointF(sx / cnt, sy / cnt));
        }
    }

    std::vector<uint8_t> visitedEdge(size_t(W) * size_t(H), 0);

    // 1) Trazar ramas que salen desde cualquier nodo (extremo o unión)
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int startIdx = y * W + x;
            if (nodeId[size_t(startIdx)] < 0)
                continue;
            const int startNode = nodeId[size_t(startIdx)];
            int nb[8];
            const int deg = neighborsOf(x, y, nb);
            for (int k = 0; k < deg; ++k) {
                const int nextIdx = nb[k];
                if (nodeId[size_t(nextIdx)] == startNode)
                    continue;
                if (nodeId[size_t(nextIdx)] >= 0) {
                    // Arista directa entre dos nodos distintos: solo emitir una vez
                    if (startNode < nodeId[size_t(nextIdx)]) {
                        SkeletonChain ch;
                        ch.points = {nodeCenters[startNode], nodeCenters[nodeId[size_t(nextIdx)]]};
                        chains.push_back(ch);
                    }
                    continue;
                }
                if (visitedEdge[size_t(nextIdx)])
                    continue;

                SkeletonChain ch;
                ch.points.push_back(nodeCenters[startNode]);
                int prev = startIdx;
                int cur = nextIdx;
                while (true) {
                    visitedEdge[size_t(cur)] = 1;
                    const int cx = cur % W;
                    const int cy = cur / W;
                    ch.points.push_back(QPointF(cx, cy));

                    int cnb[8];
                    const int cdeg = neighborsOf(cx, cy, cnb);
                    int nextStep = -1;
                    int hitNode = -1;
                    for (int m = 0; m < cdeg; ++m) {
                        const int cand = cnb[m];
                        if (cand == prev)
                            continue;
                        if (nodeId[size_t(cand)] >= 0) {
                            if (nodeId[size_t(cand)] != startNode || ch.points.size() > 6) {
                                hitNode = nodeId[size_t(cand)];
                                break;
                            }
                            continue;
                        }
                        if (!visitedEdge[size_t(cand)]) {
                            nextStep = cand;
                            break;
                        }
                    }
                    if (hitNode >= 0) {
                        ch.points.push_back(nodeCenters[hitNode]);
                        ch.closedLoop = (hitNode == startNode && ch.points.size() > 8);
                        break;
                    }
                    if (nextStep < 0)
                        break;
                    prev = cur;
                    cur = nextStep;
                }
                if (ch.points.size() >= 3)
                    chains.push_back(ch);
            }
        }
    }

    // 2) Trazar lazos cerrados puros (sin nodos de grado != 2, ej. círculo o triángulo aislado)
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int startIdx = y * W + x;
            if (!skel[size_t(startIdx)] || nodeId[size_t(startIdx)] >= 0 || visitedEdge[size_t(startIdx)])
                continue;

            SkeletonChain ch;
            ch.closedLoop = true;
            int prev = -1;
            int cur = startIdx;
            while (true) {
                visitedEdge[size_t(cur)] = 1;
                const int cx = cur % W;
                const int cy = cur / W;
                ch.points.push_back(QPointF(cx, cy));
                int cnb[8];
                const int cdeg = neighborsOf(cx, cy, cnb);
                int nextStep = -1;
                for (int m = 0; m < cdeg; ++m) {
                    const int cand = cnb[m];
                    if (cand == prev)
                        continue;
                    if (!visitedEdge[size_t(cand)] && nodeId[size_t(cand)] < 0) {
                        nextStep = cand;
                        break;
                    }
                }
                if (nextStep < 0)
                    break;
                prev = cur;
                cur = nextStep;
            }
            if (ch.points.size() >= 8)
                chains.push_back(ch);
        }
    }

    return chains;
}

struct CircleFit {
    bool valid = false;
    QPointF center;
    double radius = 0.0;
    double rmsError = 0.0;
    double maxError = 0.0;
    double spanRad = 0.0;
};

// Ajuste algebraico de círculo (Kåsa)
CircleFit fitCircleKasa(const QVector<QPointF>& pts, bool closedLoop) {
    CircleFit res;
    if (pts.size() < 8)
        return res;

    double mx = 0.0, my = 0.0;
    for (const QPointF& p : pts) {
        mx += p.x();
        my += p.y();
    }
    mx /= double(pts.size());
    my /= double(pts.size());

    double Suu = 0.0, Suv = 0.0, Svv = 0.0, Suuu = 0.0, Svvv = 0.0, Suvv = 0.0, Svuu = 0.0;
    for (const QPointF& p : pts) {
        const double u = p.x() - mx;
        const double v = p.y() - my;
        const double uu = u * u;
        const double vv = v * v;
        Suu += uu;
        Suv += u * v;
        Svv += vv;
        Suuu += uu * u;
        Svvv += vv * v;
        Suvv += u * vv;
        Svuu += v * uu;
    }

    const double det = Suu * Svv - Suv * Suv;
    if (std::abs(det) < 1e-6)
        return res;

    const double rhs1 = 0.5 * (Suuu + Suvv);
    const double rhs2 = 0.5 * (Svvv + Svuu);
    const double uc = (rhs1 * Svv - rhs2 * Suv) / det;
    const double vc = (Suu * rhs2 - Suv * rhs1) / det;
    const double R = std::sqrt(uc * uc + vc * vc + (Suu + Svv) / double(pts.size()));
    if (!std::isfinite(R) || R < 14.0)
        return res;

    const QPointF center(mx + uc, my + vc);
    double sumSq = 0.0;
    double maxErr = 0.0;
    double totalAngle = 0.0;
    for (int i = 0; i < pts.size(); ++i) {
        const double err = std::abs(ptDist(pts[i], center) - R);
        sumSq += err * err;
        maxErr = std::max(maxErr, err);
        if (i > 0) {
            const double a0 = std::atan2(pts[i - 1].y() - center.y(), pts[i - 1].x() - center.x());
            const double a1 = std::atan2(pts[i].y() - center.y(), pts[i].x() - center.x());
            double da = a1 - a0;
            while (da > std::numbers::pi)
                da -= 2.0 * std::numbers::pi;
            while (da < -std::numbers::pi)
                da += 2.0 * std::numbers::pi;
            totalAngle += da;
        }
    }

    res.valid = true;
    res.center = center;
    res.radius = R;
    res.rmsError = std::sqrt(sumSq / double(pts.size()));
    res.maxError = maxErr;
    res.spanRad = closedLoop ? (2.0 * std::numbers::pi) : std::abs(totalAngle);
    return res;
}

void douglasPeuckerRec(const QVector<QPointF>& pts, int start, int end, double eps, QVector<QPointF>& out) {
    if (end <= start + 1)
        return;
    double maxDist = -1.0;
    int splitIdx = -1;
    for (int i = start + 1; i < end; ++i) {
        const double d = ptToSegDist(pts[i], pts[start], pts[end]);
        if (d > maxDist) {
            maxDist = d;
            splitIdx = i;
        }
    }
    if (maxDist > eps && splitIdx > start && splitIdx < end) {
        douglasPeuckerRec(pts, start, splitIdx, eps, out);
        out.push_back(pts[splitIdx]);
        douglasPeuckerRec(pts, splitIdx, end, eps, out);
    }
}

QVector<QPointF> simplifyPolyline(const QVector<QPointF>& pts, double eps, bool closedLoop) {
    if (pts.size() <= 2)
        return pts;
    if (!closedLoop) {
        QVector<QPointF> out;
        out.push_back(pts.first());
        douglasPeuckerRec(pts, 0, pts.size() - 1, eps, out);
        out.push_back(pts.last());
        return out;
    }

    // Para lazos cerrados, dividir por el par más lejano para evitar vértices artificiales
    int idxA = 0;
    int idxB = pts.size() / 2;
    double bestD = -1.0;
    const int step = std::max<int>(1, pts.size() / 48);
    for (int i = 0; i < pts.size(); i += step) {
        for (int j = i + step; j < pts.size(); j += step) {
            const double d = ptDist(pts[i], pts[j]);
            if (d > bestD) {
                bestD = d;
                idxA = i;
                idxB = j;
            }
        }
    }

    QVector<QPointF> half1, half2;
    for (int i = idxA; i <= idxB; ++i)
        half1.push_back(pts[i]);
    for (int i = idxB; i < pts.size(); ++i)
        half2.push_back(pts[i]);
    for (int i = 0; i <= idxA; ++i)
        half2.push_back(pts[i]);

    QVector<QPointF> s1 = simplifyPolyline(half1, eps, false);
    QVector<QPointF> s2 = simplifyPolyline(half2, eps, false);
    if (!s1.isEmpty())
        s1.pop_back();
    if (!s2.isEmpty())
        s2.pop_back();
    QVector<QPointF> ring = s1 + s2;
    return ring;
}

struct Seg2D {
    QPointF a;
    QPointF b;
};

double angleBetweenDirs(const QPointF& d1, const QPointF& d2) {
    const double l1 = std::hypot(d1.x(), d1.y());
    const double l2 = std::hypot(d2.x(), d2.y());
    if (l1 < 1e-6 || l2 < 1e-6)
        return 0.0;
    const double c = std::clamp((d1.x() * d2.x() + d1.y() * d2.y()) / (l1 * l2), -1.0, 1.0);
    return std::acos(c);
}

// Extiende cada segmento recto siguiendo la tinta real de la máscara binaria original
// (corrige de forma 100% general cortes en uniones gruesas, bolitas o cuadraditos de 90°
// sin depender de heurísticas para figuras concretas).
void extendSegmentsAlongInk(QVector<Seg2D>& segs, const std::vector<uint8_t>& inkMask, int W, int H) {
    auto hasInkNear = [&](const QPointF& pt, const QPointF& perp) -> bool {
        for (int off = -2; off <= 2; ++off) {
            const int x = qRound(pt.x() + perp.x() * off);
            const int y = qRound(pt.y() + perp.y() * off);
            if (x >= 0 && x < W && y >= 0 && y < H && inkMask[size_t(y * W + x)])
                return true;
        }
        return false;
    };

    for (Seg2D& s : segs) {
        const double len = ptDist(s.a, s.b);
        if (len < 16.0)
            continue;
        const QPointF u = (s.b - s.a) / len;
        const QPointF perp(-u.y(), u.x());
        const int maxSteps = int(std::clamp(len * 0.65, 12.0, 65.0));

        auto marchEnd = [&](QPointF& endpoint, const QPointF& dir) {
            QPointF lastInk = endpoint;
            int misses = 0;
            for (int step = 1; step <= maxSteps; ++step) {
                const QPointF cand = endpoint + dir * double(step);
                if (cand.x() < 0 || cand.x() >= W || cand.y() < 0 || cand.y() >= H)
                    break;
                if (hasInkNear(cand, perp)) {
                    lastInk = cand;
                    misses = 0;
                } else {
                    ++misses;
                    if (misses > 3)
                        break;
                }
            }
            endpoint = lastInk;
        };

        marchEnd(s.a, -u);
        marchEnd(s.b, u);
    }
}

// Detecta marcas reales de ángulo recto (cuadradito 90°) consultando la tinta original
// en cada esquina o T-junction de ~90° entre dos aristas. 100% general para cualquier orientación.
QVector<std::array<QPointF, 3>> detectRightAngleMarks(const QVector<Seg2D>& segs,
                                                      const std::vector<uint8_t>& inkMask,
                                                      int W,
                                                      int H) {
    QVector<std::array<QPointF, 3>> marks;
    if (segs.size() < 2)
        return marks;

    auto inkAt = [&](const QPointF& pt) -> bool {
        const int cx = qRound(pt.x());
        const int cy = qRound(pt.y());
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x >= 0 && x < W && y >= 0 && y < H && inkMask[size_t(y * W + x)])
                    return true;
            }
        }
        return false;
    };

    auto lineSupport = [&](const QPointF& a, const QPointF& b) -> double {
        constexpr int kSamples = 7;
        int hits = 0;
        for (int k = 1; k <= kSamples; ++k) {
            const double t = double(k) / double(kSamples + 1);
            if (inkAt(a + (b - a) * t))
                ++hits;
        }
        return double(hits) / double(kSamples);
    };

    // Recopilar vértices candidatos (extremos y T-junctions) con todos los rayos salientes >= 22 px
    struct CornerRay {
        QPointF dir;
        double len = 0.0;
    };
    QVector<QPointF> vertices;
    auto addVertex = [&](const QPointF& p) {
        for (const QPointF& v : vertices) {
            if (ptDist(v, p) < 4.0)
                return;
        }
        vertices.push_back(p);
    };
    for (const Seg2D& s : segs) {
        addVertex(s.a);
        addVertex(s.b);
    }

    constexpr double kHalfPi = std::numbers::pi * 0.5;
    constexpr double kRightTol = 15.0 * std::numbers::pi / 180.0;

    for (const QPointF& v : vertices) {
        QVector<CornerRay> rays;
        for (const Seg2D& s : segs) {
            const double dA = ptDist(v, s.a);
            const double dB = ptDist(v, s.b);
            if (dA < 4.5 && ptDist(s.a, s.b) >= 22.0) {
                const QPointF d = s.b - v;
                const double l = std::hypot(d.x(), d.y());
                if (l >= 20.0)
                    rays.push_back({d / l, l});
            } else if (dB < 4.5 && ptDist(s.a, s.b) >= 22.0) {
                const QPointF d = s.a - v;
                const double l = std::hypot(d.x(), d.y());
                if (l >= 20.0)
                    rays.push_back({d / l, l});
            } else {
                // T-junction: v cae en el interior de s -> aporta dos rayos opuestos
                double t = 0.0;
                if (ptToSegDist(v, s.a, s.b, &t) < 4.5 && t > 0.05 && t < 0.95) {
                    const QPointF d1 = s.a - v;
                    const QPointF d2 = s.b - v;
                    const double l1 = std::hypot(d1.x(), d1.y());
                    const double l2 = std::hypot(d2.x(), d2.y());
                    if (l1 >= 20.0)
                        rays.push_back({d1 / l1, l1});
                    if (l2 >= 20.0)
                        rays.push_back({d2 / l2, l2});
                }
            }
        }

        for (int i = 0; i < rays.size(); ++i) {
            for (int j = i + 1; j < rays.size(); ++j) {
                const double ang = angleBetweenDirs(rays[i].dir, rays[j].dir);
                if (std::abs(ang - kHalfPi) > kRightTol)
                    continue;

                const double maxD = std::min({28.0, rays[i].len * 0.45, rays[j].len * 0.45});
                double bestScore = 0.0;
                double bestD = 0.0;
                for (double d = 8.0; d <= maxD; d += 2.0) {
                    const QPointF p1 = v + rays[i].dir * d;
                    const QPointF p2 = v + rays[j].dir * d;
                    const QPointF corner = v + (rays[i].dir + rays[j].dir) * d;
                    // Comprobar 4 puntos interiores para descartar manchas sólidas sin rechazar
                    // ángulos de 90° atravesados por una mediana/ceviana (como MP en ∠APB)
                    const double offsets[4][2] = {{0.30, 0.30}, {0.65, 0.30}, {0.30, 0.65}, {0.65, 0.65}};
                    int interiorInk = 0;
                    for (const auto& o : offsets) {
                        const QPointF ip = v + rays[i].dir * (o[0] * d) + rays[j].dir * (o[1] * d);
                        const int ix = qRound(ip.x());
                        const int iy = qRound(ip.y());
                        if (ix >= 0 && ix < W && iy >= 0 && iy < H && inkMask[size_t(iy * W + ix)])
                            ++interiorInk;
                    }
                    if (interiorInk >= 3)
                        continue;
                    const double s1 = lineSupport(p1, corner);
                    const double s2 = lineSupport(p2, corner);
                    if (s1 >= 0.65 && s2 >= 0.65) {
                        const double score = s1 + s2;
                        if (score > bestScore) {
                            bestScore = score;
                            bestD = d;
                        }
                    }
                }
                if (bestD > 0.0) {
                    // Usar los extremos reales de los brazos (igual que ShapeKind::RightAngle al dibujarlo en Finily),
                    // para que shapePath calcule el tamaño estándar (hasta 22 px) y attachNewShape lo enlace a las aristas.
                    const QPointF arm1 = v + rays[i].dir * rays[i].len;
                    const QPointF arm2 = v + rays[j].dir * rays[j].len;
                    marks.push_back({arm1, v, arm2});
                }
            }
        }
    }
    return marks;
}

// Calcula el ángulo de inclinación residual (en radianes) de una foto chueca
// a partir de los segmentos casi horizontales o casi verticales (ponderado por longitud).
double estimateDeskewAngleRad(const QVector<Seg2D>& segs) {
    constexpr double kMaxTiltRad = 8.5 * std::numbers::pi / 180.0;
    constexpr double kHalfPi = std::numbers::pi * 0.5;
    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (const Seg2D& s : segs) {
        const double len = ptDist(s.a, s.b);
        if (len < 28.0)
            continue;
        const double ang = std::atan2(s.b.y() - s.a.y(), s.b.x() - s.a.x());
        // Distancia angular al múltiplo de 90° más cercano
        const double nearestAxis = std::round(ang / kHalfPi) * kHalfPi;
        const double err = ang - nearestAxis;
        if (std::abs(err) <= kMaxTiltRad) {
            weightedSum += err * len;
            totalWeight += len;
        }
    }
    if (totalWeight < 40.0)
        return 0.0;
    const double skew = weightedSum / totalWeight;
    constexpr double kMinTiltRad = 0.45 * std::numbers::pi / 180.0;
    return (std::abs(skew) >= kMinTiltRad) ? skew : 0.0;
}

QPointF rotatePointAround(const QPointF& pt, const QPointF& pivot, double cosA, double sinA) {
    const double dx = pt.x() - pivot.x();
    const double dy = pt.y() - pivot.y();
    return QPointF(pivot.x() + dx * cosA - dy * sinA, pivot.y() + dx * sinA + dy * cosA);
}

// Regularización ortogonal suave (±3.5°): nivela a 0.0° y 90.0° exactos únicamente
// las aristas que ya son casi horizontales o verticales (corrige cizalladura/perspectiva
// de fotos de libros sin tocar cevianas ni lados inclinados), manteniendo vértices compartidos.
void regularizeNearOrthogonalSegments(QVector<Seg2D>& segs) {
    if (segs.isEmpty())
        return;
    constexpr double kMaxOrthoRad = 3.5 * std::numbers::pi / 180.0;

    // Construir lista de vértices únicos compartidos
    QVector<QPointF> verts;
    auto vertIdx = [&](const QPointF& p) -> int {
        for (int i = 0; i < verts.size(); ++i) {
            if (ptDist(verts[i], p) < 2.5)
                return i;
        }
        verts.push_back(p);
        return int(verts.size()) - 1;
    };
    QVector<QPair<int, int>> edges;
    edges.reserve(segs.size());
    for (const Seg2D& s : segs)
        edges.push_back({vertIdx(s.a), vertIdx(s.b)});

    // Iterar 4 pasadas de relajación de restricciones H/V (converge exactamente en vértices compartidos)
    for (int iter = 0; iter < 4; ++iter) {
        for (const auto& e : edges) {
            const QPointF a = verts[e.first];
            const QPointF b = verts[e.second];
            const double dx = b.x() - a.x();
            const double dy = b.y() - a.y();
            const double len = std::hypot(dx, dy);
            if (len < 18.0)
                continue;
            const double angH = std::atan2(std::abs(dy), std::abs(dx));
            if (angH <= kMaxOrthoRad) {
                const double avgY = 0.5 * (a.y() + b.y());
                verts[e.first].setY(avgY);
                verts[e.second].setY(avgY);
            } else if (std::abs(angH - std::numbers::pi * 0.5) <= kMaxOrthoRad) {
                const double avgX = 0.5 * (a.x() + b.x());
                verts[e.first].setX(avgX);
                verts[e.second].setX(avgX);
            }
        }
    }

    for (int i = 0; i < segs.size(); ++i) {
        segs[i].a = verts[edges[i].first];
        segs[i].b = verts[edges[i].second];
    }
}

// Agrupa vértices cercanos, elimina duplicados, poda colitas/anotaciones, fusiona colineales y ajusta T-junctions
void cleanAndSnapSegments(QVector<Seg2D>& segs, double snapRadius, double diagramDiag) {
    // 1) Agrupar extremos cercanos a centroides compartidos
    QVector<QPointF> clusters;
    QVector<int> clusterCounts;
    auto assignCluster = [&](QPointF& p) {
        double bestD = snapRadius;
        int bestIdx = -1;
        for (int i = 0; i < clusters.size(); ++i) {
            const double d = ptDist(p, clusters[i]);
            if (d <= bestD) {
                bestD = d;
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) {
            const int n = clusterCounts[bestIdx];
            clusters[bestIdx] = QPointF((clusters[bestIdx].x() * n + p.x()) / (n + 1),
                                        (clusters[bestIdx].y() * n + p.y()) / (n + 1));
            clusterCounts[bestIdx] = n + 1;
        } else {
            clusters.push_back(p);
            clusterCounts.push_back(1);
        }
    };
    for (Seg2D& s : segs) {
        assignCluster(s.a);
        assignCluster(s.b);
    }
    auto snapToCluster = [&](QPointF& p) {
        double bestD = snapRadius * 1.25;
        int bestIdx = -1;
        for (int i = 0; i < clusters.size(); ++i) {
            const double d = ptDist(p, clusters[i]);
            if (d <= bestD) {
                bestD = d;
                bestIdx = i;
            }
        }
        if (bestIdx >= 0)
            p = clusters[bestIdx];
    };
    for (Seg2D& s : segs) {
        snapToCluster(s.a);
        snapToCluster(s.b);
    }

    // 2) T-junction snapping: extremos que casi tocan el interior de otro segmento
    const double tSnap = std::max(6.0, snapRadius * 0.9);
    for (int i = 0; i < segs.size(); ++i) {
        for (int endIdx = 0; endIdx < 2; ++endIdx) {
            QPointF& p = (endIdx == 0) ? segs[i].a : segs[i].b;
            for (int j = 0; j < segs.size(); ++j) {
                if (i == j)
                    continue;
                double t = 0.0;
                const double d = ptToSegDist(p, segs[j].a, segs[j].b, &t);
                if (d <= tSnap && t > 0.04 && t < 0.96) {
                    p = segs[j].a + (segs[j].b - segs[j].a) * t;
                    break;
                }
            }
        }
    }

    // 3) Eliminar segmentos muy cortos o duplicados
    auto prune = [&]() {
        QVector<Seg2D> out;
        for (const Seg2D& s : segs) {
            if (ptDist(s.a, s.b) < 8.0)
                continue;
            bool dup = false;
            for (const Seg2D& o : out) {
                if ((ptDist(s.a, o.a) < 2.5 && ptDist(s.b, o.b) < 2.5) ||
                    (ptDist(s.a, o.b) < 2.5 && ptDist(s.b, o.a) < 2.5)) {
                    dup = true;
                    break;
                }
            }
            if (!dup)
                out.push_back(s);
        }
        segs = out;
    };
    prune();

    auto degreeAt = [&](const QPointF& p) -> int {
        int deg = 0;
        for (const Seg2D& s : segs) {
            if (ptDist(s.a, p) < 3.0 || ptDist(s.b, p) < 3.0)
                ++deg;
        }
        return deg;
    };

    // 4) Podar colitas sueltas (letras que tocaban un vértice, ej. "C") y flechas flotantes aisladas
    const double maxSpurLen = std::max(22.0, diagramDiag * 0.068);
    const double maxFloatingLen = std::max(40.0, diagramDiag * 0.28);
    bool prunedSpur = true;
    while (prunedSpur) {
        prunedSpur = false;
        for (int i = segs.size() - 1; i >= 0; --i) {
            const double len = ptDist(segs[i].a, segs[i].b);
            const int da = degreeAt(segs[i].a);
            const int db = degreeAt(segs[i].b);
            // Flecha o línea de anotación completamente desconectada y corta
            if (da == 1 && db == 1 && segs.size() >= 3 && len < maxFloatingLen) {
                segs.removeAt(i);
                prunedSpur = true;
                continue;
            }
            // Colita corta que cuelga de un vértice (un extremo libre deg==1, otro unido deg>=2)
            if (((da == 1 && db >= 2) || (db == 1 && da >= 2)) && len < maxSpurLen) {
                segs.removeAt(i);
                prunedSpur = true;
            }
        }
    }

    // 5) Fusionar segmentos colineales (incluso corrigiendo pequeños bultos por puntos de lápiz sobre una arista)
    constexpr double kMaxCollinearRad = 12.0 * std::numbers::pi / 180.0;
    const double maxBumpOffset = std::max(6.5, diagramDiag * 0.022);
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < segs.size() && !merged; ++i) {
            for (int j = i + 1; j < segs.size() && !merged; ++j) {
                QPointF shared, otherI, otherJ;
                bool share = false;
                if (ptDist(segs[i].a, segs[j].a) < 2.5) {
                    shared = segs[i].a; otherI = segs[i].b; otherJ = segs[j].b; share = true;
                } else if (ptDist(segs[i].a, segs[j].b) < 2.5) {
                    shared = segs[i].a; otherI = segs[i].b; otherJ = segs[j].a; share = true;
                } else if (ptDist(segs[i].b, segs[j].a) < 2.5) {
                    shared = segs[i].b; otherI = segs[i].a; otherJ = segs[j].b; share = true;
                } else if (ptDist(segs[i].b, segs[j].b) < 2.5) {
                    shared = segs[i].b; otherI = segs[i].a; otherJ = segs[j].a; share = true;
                }
                if (!share)
                    continue;

                if (degreeAt(shared) != 2)
                    continue;

                const QPointF dir1 = shared - otherI;
                const QPointF dir2 = otherJ - shared;
                const double devFromLine = ptToSegDist(shared, otherI, otherJ);
                const bool isCollinear = angleBetweenDirs(dir1, dir2) <= kMaxCollinearRad;
                const bool isSmallBump = (std::min(ptDist(otherI, shared), ptDist(shared, otherJ)) < 26.0 &&
                                          devFromLine <= maxBumpOffset);
                if (isCollinear || isSmallBump) {
                    segs[i] = {otherI, otherJ};
                    segs.removeAt(j);
                    merged = true;
                }
            }
        }
    }

    // 5b) Puenteo de huecos colineales (cierra cortes provocados por puntos/marcas sobre una arista como AC)
    constexpr double kGapAngleRad = 9.5 * std::numbers::pi / 180.0;
    const double maxGapDist = std::max(28.0, diagramDiag * 0.095);
    bool bridged = true;
    while (bridged) {
        bridged = false;
        for (int i = 0; i < segs.size() && !bridged; ++i) {
            for (int j = i + 1; j < segs.size() && !bridged; ++j) {
                const QPointF di = segs[i].b - segs[i].a;
                const QPointF dj = segs[j].b - segs[j].a;
                const double ang = angleBetweenDirs(di, dj);
                if (ang > kGapAngleRad && std::abs(ang - std::numbers::pi) > kGapAngleRad)
                    continue;

                // Encontrar el par de extremos más cercano entre segs[i] y segs[j]
                struct PairCand { QPointF cI, fI, cJ, fJ; double d; };
                const PairCand cands[4] = {
                    {segs[i].a, segs[i].b, segs[j].a, segs[j].b, ptDist(segs[i].a, segs[j].a)},
                    {segs[i].a, segs[i].b, segs[j].b, segs[j].a, ptDist(segs[i].a, segs[j].b)},
                    {segs[i].b, segs[i].a, segs[j].a, segs[j].b, ptDist(segs[i].b, segs[j].a)},
                    {segs[i].b, segs[i].a, segs[j].b, segs[j].a, ptDist(segs[i].b, segs[j].b)},
                };
                int best = 0;
                for (int k = 1; k < 4; ++k) {
                    if (cands[k].d < cands[best].d)
                        best = k;
                }
                const PairCand& bc = cands[best];
                if (bc.d <= 2.5 || bc.d > maxGapDist)
                    continue;

                // Ambos extremos cercanos deben estar alineados sobre la recta farI—farJ y en orden
                double tI = 0.0, tJ = 0.0;
                const double offI = ptToSegDist(bc.cI, bc.fI, bc.fJ, &tI);
                const double offJ = ptToSegDist(bc.cJ, bc.fI, bc.fJ, &tJ);
                if (offI > 5.5 || offJ > 5.5 || !(tI > 0.02 && tI < tJ && tJ < 0.98))
                    continue;

                const int degI = degreeAt(bc.cI);
                const int degJ = degreeAt(bc.cJ);
                if (degI == 1 && degJ == 1) {
                    segs[i] = {bc.fI, bc.fJ};
                    segs.removeAt(j);
                    bridged = true;
                } else if (degI == 1 && degJ >= 2) {
                    if (ptDist(segs[i].a, bc.cI) < 1.0)
                        segs[i].a = bc.cJ;
                    else
                        segs[i].b = bc.cJ;
                    bridged = true;
                } else if (degJ == 1 && degI >= 2) {
                    if (ptDist(segs[j].a, bc.cJ) < 1.0)
                        segs[j].a = bc.cI;
                    else
                        segs[j].b = bc.cI;
                    bridged = true;
                }
            }
        }
    }

    // 5c) Proyección de rayo T-junction + imán en cono hacia vértices cercanos (ej. B-P -> B y A-Q -> Q)
    const double maxRayLen = std::max(36.0, diagramDiag * 0.13);
    constexpr double kConeMaxRad = 22.0 * std::numbers::pi / 180.0;
    for (int i = 0; i < segs.size(); ++i) {
        const double lenI = ptDist(segs[i].a, segs[i].b);
        if (lenI < 20.0)
            continue;
        for (int endIdx = 0; endIdx < 2; ++endIdx) {
            QPointF& p = (endIdx == 0) ? segs[i].a : segs[i].b;
            const QPointF q = (endIdx == 0) ? segs[i].b : segs[i].a;
            if (degreeAt(p) != 1)
                continue;
            const QPointF u = (p - q) / lenI;
            double bestDist = maxRayLen + 1.0;
            QPointF bestTarget;

            // Primero buscar vértices existentes dentro del cono hacia adelante
            for (int j = 0; j < segs.size(); ++j) {
                if (i == j)
                    continue;
                for (const QPointF& candV : {segs[j].a, segs[j].b}) {
                    const QPointF w = candV - p;
                    const double d = std::hypot(w.x(), w.y());
                    if (d > 1.0 && d <= maxRayLen && angleBetweenDirs(u, w) <= kConeMaxRad && d < bestDist) {
                        bestDist = d;
                        bestTarget = candV;
                    }
                }
            }

            // También comprobar intersección directa del rayo contra el interior de otro segmento
            for (int j = 0; j < segs.size(); ++j) {
                if (i == j)
                    continue;
                const QPointF v = segs[j].b - segs[j].a;
                const double det = u.x() * (-v.y()) - u.y() * (-v.x());
                if (std::abs(det) < 1e-6)
                    continue;
                const QPointF rhs = segs[j].a - p;
                const double s = (rhs.x() * (-v.y()) - rhs.y() * (-v.x())) / det;
                const double t = (u.x() * rhs.y() - u.y() * rhs.x()) / det;
                if (s > 1.0 && s <= maxRayLen && t >= -0.02 && t <= 1.02 && s < bestDist) {
                    bestDist = s;
                    bestTarget = p + u * s;
                }
            }
            if (bestDist <= maxRayLen)
                p = bestTarget;
        }
    }

    // 5d) Descartar micro-segmentos residuales de uniones gruesas (< 4.5% de la diagonal)
    const double minCleanSegLen = std::max(14.0, diagramDiag * 0.045);
    {
        QVector<Seg2D> filtered;
        for (const Seg2D& s : segs) {
            if (ptDist(s.a, s.b) >= minCleanSegLen)
                filtered.push_back(s);
        }
        if (!filtered.isEmpty())
            segs = filtered;
    }

    // 6) Fusión global de segmentos colineales INCLUSO a través de uniones (deg >= 3),
    // para que aristas largas como AC, BP o BQ queden como UN solo segmento continuo
    // en vez de quedar troceadas en pedacitos al editar por vértices.
    constexpr double kFullMergeRad = 8.5 * std::numbers::pi / 180.0;
    bool mergedAcross = true;
    while (mergedAcross) {
        mergedAcross = false;
        for (int i = 0; i < segs.size() && !mergedAcross; ++i) {
            for (int j = i + 1; j < segs.size() && !mergedAcross; ++j) {
                QPointF shared, otherI, otherJ;
                bool share = false;
                if (ptDist(segs[i].a, segs[j].a) < 3.0) {
                    shared = segs[i].a; otherI = segs[i].b; otherJ = segs[j].b; share = true;
                } else if (ptDist(segs[i].a, segs[j].b) < 3.0) {
                    shared = segs[i].a; otherI = segs[i].b; otherJ = segs[j].a; share = true;
                } else if (ptDist(segs[i].b, segs[j].a) < 3.0) {
                    shared = segs[i].b; otherI = segs[i].a; otherJ = segs[j].b; share = true;
                } else if (ptDist(segs[i].b, segs[j].b) < 3.0) {
                    shared = segs[i].b; otherI = segs[i].a; otherJ = segs[j].a; share = true;
                }
                if (!share)
                    continue;
                const QPointF dir1 = shared - otherI;
                const QPointF dir2 = otherJ - shared;
                if (angleBetweenDirs(dir1, dir2) <= kFullMergeRad) {
                    double t = 0.0;
                    const double d = ptToSegDist(shared, otherI, otherJ, &t);
                    if (d <= 6.5 && t > 0.03 && t < 0.97) {
                        const QPointF snapped = otherI + (otherJ - otherI) * t;
                        // Alinear cualquier otro segmento que terminaba en `shared` sobre la recta fusionada
                        for (int k = 0; k < segs.size(); ++k) {
                            if (k == i || k == j)
                                continue;
                            if (ptDist(segs[k].a, shared) < 3.0)
                                segs[k].a = snapped;
                            if (ptDist(segs[k].b, shared) < 3.0)
                                segs[k].b = snapped;
                        }
                        segs[i] = {otherI, otherJ};
                        segs.removeAt(j);
                        mergedAcross = true;
                    }
                }
            }
        }
    }
    // 7) Eliminar barritas diagonales cortas en esquinas (marcas de ángulo α/θ que sobrevivieron como cuerda corta)
    const double maxCornerChord = std::max(26.0, diagramDiag * 0.16);
    const double maxCornerLeg = std::max(34.0, diagramDiag * 0.21);
    const double maxCornerArea2 = 0.036 * diagramDiag * diagramDiag;
    for (int i = segs.size() - 1; i >= 0; --i) {
        const double len = ptDist(segs[i].a, segs[i].b);
        if (len > maxCornerChord)
            continue;
        if (degreeAt(segs[i].a) < 2 || degreeAt(segs[i].b) < 2)
            continue;
        bool isTinyCornerChord = false;
        for (int j = 0; j < segs.size() && !isTinyCornerChord; ++j) {
            if (j == i)
                continue;
            QPointF v;
            if (ptDist(segs[j].a, segs[i].a) < 3.0)
                v = segs[j].b;
            else if (ptDist(segs[j].b, segs[i].a) < 3.0)
                v = segs[j].a;
            else
                continue;
            const double leg1 = ptDist(v, segs[i].a);
            if (leg1 > maxCornerLeg || leg1 < 5.0)
                continue;
            for (int k = 0; k < segs.size(); ++k) {
                if (k == i || k == j)
                    continue;
                const bool connects = (ptDist(segs[k].a, v) < 3.0 && ptDist(segs[k].b, segs[i].b) < 3.0) ||
                                      (ptDist(segs[k].b, v) < 3.0 && ptDist(segs[k].a, segs[i].b) < 3.0);
                if (!connects)
                    continue;
                const double leg2 = ptDist(v, segs[i].b);
                if (leg2 > maxCornerLeg || leg2 < 5.0)
                    continue;
                const double area2 = std::abs((segs[i].a.x() - v.x()) * (segs[i].b.y() - v.y()) -
                                              (segs[i].b.x() - v.x()) * (segs[i].a.y() - v.y()));
                if (area2 < maxCornerArea2) {
                    isTinyCornerChord = true;
                    break;
                }
            }
        }
        if (isTinyCornerChord)
            segs.removeAt(i);
    }
    prune();
}

} // namespace

VectorizeResult vectorizeGeometryFromImage(const QImage& image,
                                           const QRectF& targetRect,
                                           const VectorizeOptions& options) {
    QElapsedTimer timer;
    timer.start();

    VectorizeResult result;
    if (image.isNull() || targetRect.width() <= 1.0 || targetRect.height() <= 1.0)
        return result;

    // 1) Escalar a resolución de trabajo <= 800 px para velocidad (< 40 ms) y precisión
    QImage work = image;
    constexpr int kMaxWorkDim = 800;
    if (work.width() > kMaxWorkDim || work.height() > kMaxWorkDim)
        work = work.scaled(kMaxWorkDim, kMaxWorkDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QImage gray = work.convertToFormat(QImage::Format_Grayscale8);

    int W = 0, H = 0;
    std::vector<uint8_t> mask = buildForegroundMask(gray, W, H);
    if (mask.empty())
        return result;

    // 2) Separar figuras geométricas de texto/ruido
    const QRect diagRoi = filterGeometryComponents(mask, W, H);
    if (diagRoi.isEmpty()) {
        result.elapsedMs = timer.elapsed();
        return result;
    }

    const double origScaleX = double(image.width()) / double(W);
    const double origScaleY = double(image.height()) / double(H);
    result.detectedDiagramRect = QRectF(diagRoi.left() * origScaleX, diagRoi.top() * origScaleY,
                                        diagRoi.width() * origScaleX, diagRoi.height() * origScaleY);

    // 3) Guardar copia de la máscara de tinta real antes de esqueletizar
    const std::vector<uint8_t> inkMask = mask;
    zhangSuenThinning(mask, W, H, diagRoi);

    // 4) Extraer cadenas del esqueleto
    const QVector<SkeletonChain> chains = extractSkeletonChains(mask, W, H, diagRoi);

    const double minDim = double(std::min(W, H));
    const double dpEps = std::clamp(0.008 * minDim, 2.6, 5.2);

    QVector<Seg2D> rawSegs;
    QVector<ShapeItem> circleShapes;

    const double mapX = targetRect.width() / double(W);
    const double mapY = targetRect.height() / double(H);
    auto toWorld = [&](const QPointF& p) {
        return QPointF(targetRect.left() + p.x() * mapX, targetRect.top() + p.y() * mapY);
    };

    const QColor ink = options.inkColor.isValid() ? options.inkColor : QColor(QStringLiteral("#f8fafc"));
    const double strokeW = std::clamp(options.strokeSize, 1.5, 6.0);

    for (const SkeletonChain& ch : chains) {
        // Evaluar si es círculo o arco limpio
        double maxChordDev = 0.0;
        if (ch.points.size() >= 3) {
            for (const QPointF& p : ch.points)
                maxChordDev = std::max(maxChordDev, ptToSegDist(p, ch.points.first(), ch.points.last()));
        }
        const double chordLen = ptDist(ch.points.first(), ch.points.last());

        bool matchedCurve = false;
        if (ch.points.size() >= 14 && (ch.closedLoop || maxChordDev > std::max(4.5, 0.065 * chordLen))) {
            const CircleFit fit = fitCircleKasa(ch.points, ch.closedLoop);
            if (fit.valid && fit.radius <= double(std::max(W, H)) * 0.95) {
                const bool isFullCircle = (ch.closedLoop || fit.spanRad >= 5.2) &&
                                          fit.rmsError < std::max(2.2, 0.032 * fit.radius) &&
                                          fit.maxError < std::max(5.5, 0.075 * fit.radius);
                // Criterio estricto para arcos abiertos: curvatura real (sagitta >= 14%) y
                // consistencia de lado (>=85% de los puntos interiores del mismo lado de la cuerda)
                // para nunca confundir un segmento recto con marca de lápiz (ej. B-P) con un arco.
                bool consistentArcSide = false;
                if (!isFullCircle && !ch.closedLoop && ch.points.size() >= 22 && chordLen >= 28.0 &&
                    maxChordDev >= chordLen * 0.14) {
                    const QPointF p0 = ch.points.first();
                    const QPointF d = ch.points.last() - p0;
                    int posCnt = 0, negCnt = 0;
                    for (int k = 2; k + 2 < ch.points.size(); ++k) {
                        const double cross = (ch.points[k].x() - p0.x()) * d.y() -
                                             (ch.points[k].y() - p0.y()) * d.x();
                        const double signedDist = cross / std::max(1.0, chordLen);
                        if (signedDist > 1.5)
                            ++posCnt;
                        else if (signedDist < -1.5)
                            ++negCnt;
                    }
                    const int total = posCnt + negCnt;
                    if (total >= 10 && double(std::max(posCnt, negCnt)) / double(total) >= 0.85)
                        consistentArcSide = true;
                }
                const bool isArc = consistentArcSide &&
                                   fit.spanRad >= (50.0 * std::numbers::pi / 180.0) &&
                                   fit.spanRad <= (310.0 * std::numbers::pi / 180.0) &&
                                   fit.rmsError < std::max(1.4, 0.020 * fit.radius) &&
                                   fit.maxError < std::max(3.2, 0.045 * fit.radius);
                if (isFullCircle) {
                    ShapeItem c;
                    c.id = newId();
                    c.kind = ShapeKind::Circle;
                    c.color = ink;
                    c.size = strokeW;
                    c.closed = true;
                    const QPointF cW = toWorld(fit.center);
                    const QPointF rimW = toWorld(QPointF(fit.center.x() + fit.radius, fit.center.y()));
                    c.vertices = {cW, rimW};
                    c.bounds = computeShapeBounds(c);
                    circleShapes.push_back(c);
                    matchedCurve = true;
                } else if (isArc) {
                    ShapeItem arc;
                    arc.id = newId();
                    arc.kind = ShapeKind::Arc;
                    arc.color = ink;
                    arc.size = strokeW;
                    const QPointF cW = toWorld(fit.center);
                    const QPointF p1W = toWorld(ch.points.first());
                    const QPointF p2W = toWorld(ch.points.last());
                    arc.vertices = {cW, p1W, p2W};
                    arc.bounds = computeShapeBounds(arc);
                    circleShapes.push_back(arc);
                    matchedCurve = true;
                }
            }
        }

        if (matchedCurve)
            continue;

        // Ignorar pequeños arquitos curvos de ángulo dibujados a mano (ej. arcos de α, θ cerca de un vértice)
        const double diagRoiLen = std::hypot(double(diagRoi.width()), double(diagRoi.height()));
        if (!ch.closedLoop && chordLen < diagRoiLen * 0.16 && chordLen > 6.0 &&
            maxChordDev / chordLen > 0.12) {
            continue;
        }

        // Polilínea -> segmentos rectos
        const QVector<QPointF> poly = simplifyPolyline(ch.points, dpEps, ch.closedLoop);
        if (poly.size() < 2)
            continue;
        for (int i = 0; i + 1 < poly.size(); ++i)
            rawSegs.push_back({poly[i], poly[i + 1]});
        if (ch.closedLoop && poly.size() >= 3)
            rawSegs.push_back({poly.last(), poly.first()});
    }

    // 5) Extender cada segmento recto siguiendo la tinta real + limpieza geométrica general
    extendSegmentsAlongInk(rawSegs, inkMask, W, H);
    const double diagramDiag = std::hypot(double(diagRoi.width()), double(diagRoi.height()));
    const double snapRadius = std::clamp(0.020 * minDim, 7.0, 15.0);
    cleanAndSnapSegments(rawSegs, snapRadius, diagramDiag);

    // 5b) Detectar marcas reales de ángulo recto (90°) sobre la tinta original antes de enderezar
    QVector<std::array<QPointF, 3>> rightAngleMarks;
    if (options.detectRightAngles)
        rightAngleMarks = detectRightAngleMarks(rawSegs, inkMask, W, H);

    // 5c) Enderezado automático general (corrige fotos tomadas ligeramente chuecas <= ±8.5°)
    const double skewRad = estimateDeskewAngleRad(rawSegs);
    if (std::abs(skewRad) > 1e-4) {
        const QPointF pivot(diagRoi.center().x(), diagRoi.center().y());
        const double cosA = std::cos(-skewRad);
        const double sinA = std::sin(-skewRad);
        for (Seg2D& s : rawSegs) {
            s.a = rotatePointAround(s.a, pivot, cosA, sinA);
            s.b = rotatePointAround(s.b, pivot, cosA, sinA);
        }
        for (auto& m : rightAngleMarks) {
            m[0] = rotatePointAround(m[0], pivot, cosA, sinA);
            m[1] = rotatePointAround(m[1], pivot, cosA, sinA);
            m[2] = rotatePointAround(m[2], pivot, cosA, sinA);
        }
        const QPointF pivotW = toWorld(pivot);
        for (ShapeItem& cs : circleShapes) {
            for (QPointF& v : cs.vertices)
                v = rotatePointAround(v, pivotW, cosA, sinA);
            cs.bounds = computeShapeBounds(cs);
        }
    }

    // 5d) Regularización ortogonal suave (±3.5°) para alinear horizontales/verticales exactos a la cuadrícula
    regularizeNearOrthogonalSegments(rawSegs);

    // Ordenar segmentos de mayor a menor longitud para que los polígonos exteriores/principales
    // se promuevan primero y las cevianas/segmentos internos se enlacen encima con attachNewShape.
    std::sort(rawSegs.begin(), rawSegs.end(), [](const Seg2D& a, const Seg2D& b) {
        return ptDist(a.a, a.b) > ptDist(b.a, b.b);
    });

    // 6) Promoción de polígonos (Triángulo, Rectángulo) y conservación de segmentos internos
    std::vector<bool> used(size_t(rawSegs.size()), false);

    if (options.promotePolygons && rawSegs.size() >= 3) {
        // Mapa de vértices únicos
        QVector<QPointF> verts;
        auto vertIndex = [&](const QPointF& p) -> int {
            for (int i = 0; i < verts.size(); ++i) {
                if (ptDist(verts[i], p) < 2.5)
                    return i;
            }
            verts.push_back(p);
            return int(verts.size()) - 1;
        };
        QVector<QPair<int, int>> edges;
        edges.reserve(rawSegs.size());
        for (const Seg2D& s : rawSegs)
            edges.push_back({vertIndex(s.a), vertIndex(s.b)});

        auto findUnusedEdge = [&](int u, int v) -> int {
            for (int i = 0; i < edges.size(); ++i) {
                if (used[size_t(i)])
                    continue;
                if ((edges[i].first == u && edges[i].second == v) ||
                    (edges[i].first == v && edges[i].second == u))
                    return i;
            }
            return -1;
        };

        // Promover rectángulos alineados a ejes (4 ciclos con ángulos ~90°)
        for (int e0 = 0; e0 < edges.size(); ++e0) {
            if (used[size_t(e0)])
                continue;
            const int v0 = edges[e0].first;
            const int v1 = edges[e0].second;
            for (int e1 = e0 + 1; e1 < edges.size(); ++e1) {
                if (used[size_t(e1)])
                    continue;
                int v2 = -1;
                if (edges[e1].first == v1 && edges[e1].second != v0)
                    v2 = edges[e1].second;
                else if (edges[e1].second == v1 && edges[e1].first != v0)
                    v2 = edges[e1].first;
                if (v2 < 0)
                    continue;
                for (int e2 = 0; e2 < edges.size(); ++e2) {
                    if (e2 == e0 || e2 == e1 || used[size_t(e2)])
                        continue;
                    int v3 = -1;
                    if (edges[e2].first == v2 && edges[e2].second != v1 && edges[e2].second != v0)
                        v3 = edges[e2].second;
                    else if (edges[e2].second == v2 && edges[e2].first != v1 && edges[e2].first != v0)
                        v3 = edges[e2].first;
                    if (v3 < 0)
                        continue;
                    const int e3 = findUnusedEdge(v3, v0);
                    if (e3 < 0 || e3 == e0 || e3 == e1 || e3 == e2)
                        continue;

                    // Comprobar si es rectángulo alineado a ejes
                    const QPointF p0 = verts[v0], p1 = verts[v1], p2 = verts[v2], p3 = verts[v3];
                    const double ang0 = angleBetweenDirs(p1 - p0, p3 - p0);
                    const double ang1 = angleBetweenDirs(p2 - p1, p0 - p1);
                    const double ang2 = angleBetweenDirs(p3 - p2, p1 - p2);
                    const double halfPi = std::numbers::pi * 0.5;
                    constexpr double tol = 8.0 * std::numbers::pi / 180.0;
                    const bool rightCorners = std::abs(ang0 - halfPi) < tol &&
                                              std::abs(ang1 - halfPi) < tol &&
                                              std::abs(ang2 - halfPi) < tol;
                    const double d0x = std::abs(p1.x() - p0.x());
                    const double d0y = std::abs(p1.y() - p0.y());
                    const bool axisAligned = (d0x < 4.0 || d0y < 4.0);
                    if (rightCorners && axisAligned) {
                        const double minX = std::min({p0.x(), p1.x(), p2.x(), p3.x()});
                        const double maxX = std::max({p0.x(), p1.x(), p2.x(), p3.x()});
                        const double minY = std::min({p0.y(), p1.y(), p2.y(), p3.y()});
                        const double maxY = std::max({p0.y(), p1.y(), p2.y(), p3.y()});
                        if (maxX - minX >= 22.0 && maxY - minY >= 22.0) {
                            ShapeItem rect;
                            rect.id = newId();
                            rect.kind = ShapeKind::Rectangle;
                            rect.color = ink;
                            rect.size = strokeW;
                            rect.closed = true;
                            rect.vertices = {toWorld(QPointF(minX, minY)), toWorld(QPointF(maxX, maxY))};
                            rect.bounds = computeShapeBounds(rect);
                            result.shapes.push_back(rect);
                            used[size_t(e0)] = used[size_t(e1)] = used[size_t(e2)] = used[size_t(e3)] = true;
                            break;
                        }
                    }
                }
                if (used[size_t(e0)])
                    break;
            }
        }

        // Promover triángulos (3 ciclos con área significativa)
        for (int e0 = 0; e0 < edges.size(); ++e0) {
            if (used[size_t(e0)])
                continue;
            const int v0 = edges[e0].first;
            const int v1 = edges[e0].second;
            for (int e1 = e0 + 1; e1 < edges.size(); ++e1) {
                if (used[size_t(e1)])
                    continue;
                int v2 = -1;
                if (edges[e1].first == v1 && edges[e1].second != v0)
                    v2 = edges[e1].second;
                else if (edges[e1].second == v1 && edges[e1].first != v0)
                    v2 = edges[e1].first;
                if (v2 < 0)
                    continue;
                const int e2 = findUnusedEdge(v2, v0);
                if (e2 < 0 || e2 == e0 || e2 == e1)
                    continue;

                const QPointF p0 = verts[v0], p1 = verts[v1], p2 = verts[v2];
                const double area2 = std::abs((p1.x() - p0.x()) * (p2.y() - p0.y()) -
                                              (p2.x() - p0.x()) * (p1.y() - p0.y()));
                if (area2 >= 500.0) {
                    ShapeItem tri;
                    tri.id = newId();
                    tri.kind = ShapeKind::Triangle;
                    tri.color = ink;
                    tri.size = strokeW;
                    tri.closed = true;
                    tri.vertices = {toWorld(p0), toWorld(p1), toWorld(p2)};
                    tri.bounds = computeShapeBounds(tri);
                    result.shapes.push_back(tri);
                    used[size_t(e0)] = used[size_t(e1)] = used[size_t(e2)] = true;
                    break;
                }
            }
        }
    }

    // Añadir segmentos restantes (alturas, diagonales, radios, cuerdas, lados abiertos)
    for (int i = 0; i < rawSegs.size(); ++i) {
        if (used[size_t(i)])
            continue;
        ShapeItem line;
        line.id = newId();
        line.kind = ShapeKind::Line;
        line.color = ink;
        line.size = strokeW;
        line.vertices = {toWorld(rawSegs[i].a), toWorld(rawSegs[i].b)};
        line.bounds = computeShapeBounds(line);
        result.shapes.push_back(line);
    }

    // Fusionar arcos concéntricos que juntos forman una circunferencia completa
    // (ej. una circunferencia cortada por radios, cuerdas o un triángulo inscrito)
    QVector<ShapeItem> mergedCurves;
    std::vector<bool> arcUsed(size_t(circleShapes.size()), false);
    for (int i = 0; i < circleShapes.size(); ++i) {
        if (arcUsed[size_t(i)])
            continue;
        const ShapeItem& base = circleShapes[i];
        if (base.kind != ShapeKind::Arc || base.vertices.size() < 3) {
            mergedCurves.push_back(base);
            arcUsed[size_t(i)] = true;
            continue;
        }
        const QPointF c0 = base.vertices[0];
        const double r0 = ptDist(c0, base.vertices[1]);
        QVector<int> group = {i};
        double sumCx = c0.x(), sumCy = c0.y(), sumR = r0;
        for (int j = i + 1; j < circleShapes.size(); ++j) {
            if (arcUsed[size_t(j)] || circleShapes[j].kind != ShapeKind::Arc || circleShapes[j].vertices.size() < 3)
                continue;
            const QPointF cj = circleShapes[j].vertices[0];
            const double rj = ptDist(cj, circleShapes[j].vertices[1]);
            const double tol = std::max(8.0, 0.10 * std::max(r0, rj));
            if (ptDist(c0, cj) <= tol && std::abs(r0 - rj) <= tol) {
                group.push_back(j);
                sumCx += cj.x();
                sumCy += cj.y();
                sumR += rj;
            }
        }
        if (group.size() >= 2) {
            const QPointF avgC(sumCx / double(group.size()), sumCy / double(group.size()));
            const double avgR = sumR / double(group.size());
            ShapeItem fullCircle;
            fullCircle.id = newId();
            fullCircle.kind = ShapeKind::Circle;
            fullCircle.color = ink;
            fullCircle.size = strokeW;
            fullCircle.closed = true;
            fullCircle.vertices = {avgC, QPointF(avgC.x() + avgR, avgC.y())};
            fullCircle.bounds = computeShapeBounds(fullCircle);
            mergedCurves.push_back(fullCircle);
            for (int idx : group)
                arcUsed[size_t(idx)] = true;
        } else {
            mergedCurves.push_back(base);
            arcUsed[size_t(i)] = true;
        }
    }

    // Añadir círculos y arcos
    for (const ShapeItem& c : mergedCurves)
        result.shapes.push_back(c);

    // Añadir marcas nativas de ángulo recto (90°) detectadas en la tinta real,
    // evitando duplicar si un triángulo rectángulo promovido ya dibuja su propio cuadradito automático
    for (const auto& m : rightAngleMarks) {
        const QPointF w0 = toWorld(m[0]);
        const QPointF w1 = toWorld(m[1]);
        const QPointF w2 = toWorld(m[2]);
        bool coveredByRightTriangle = false;
        for (const ShapeItem& existing : result.shapes) {
            if (existing.kind != ShapeKind::Triangle || existing.vertices.size() < 3)
                continue;
            for (int i = 0; i < 3; ++i) {
                const QPointF c = existing.vertices[i];
                if (ptDist(c, w1) > 6.0)
                    continue;
                const QPointF u = existing.vertices[(i + 1) % 3] - c;
                const QPointF v = existing.vertices[(i + 2) % 3] - c;
                const double lu = std::hypot(u.x(), u.y());
                const double lv = std::hypot(v.x(), v.y());
                if (lu > 18.0 && lv > 18.0 &&
                    std::abs((u.x() * v.x() + u.y() * v.y()) / (lu * lv)) <= 0.016) {
                    coveredByRightTriangle = true;
                    break;
                }
            }
            if (coveredByRightTriangle)
                break;
        }
        if (coveredByRightTriangle)
            continue;

        ShapeItem ra;
        ra.id = newId();
        ra.kind = ShapeKind::RightAngle;
        ra.color = ink;
        ra.size = strokeW;
        ra.vertices = {w0, w1, w2};
        ra.bounds = computeShapeBounds(ra);
        result.shapes.push_back(ra);
    }

    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace finily
