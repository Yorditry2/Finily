#pragma once

#include "core/ShapeProtocols.h"
#include "core/Types.h"
#include "core/Utils.h"
#include "engine/StickerLibrary.h"

#include <QColor>
#include <QFontMetricsF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace finily::sticker_detail {

inline constexpr int kGeometryStickerCount = 31;
inline constexpr int kScienceStickerCount = 12;
inline constexpr int kBuiltinStickerCount = kGeometryStickerCount + kScienceStickerCount;

// Colores centinela para plantillas integradas (adaptables al tema al instanciar)
inline const QColor kInkMain(QStringLiteral("#f8fafc"));
inline const QColor kAccent1(QStringLiteral("#38bdf8"));
inline const QColor kAccent2(QStringLiteral("#facc15"));
inline const QColor kAccent3(QStringLiteral("#4ade80"));

inline QPointF unitVec(const QPointF& v) {
    const double len = std::hypot(v.x(), v.y());
    return (len > 1e-9) ? (v / len) : QPointF(1.0, 0.0);
}

inline QPointF lerpPt(const QPointF& a, const QPointF& b, double t) {
    return a + (b - a) * t;
}

inline QPointF polarPt(const QPointF& center, double radius, double deg) {
    const double rad = deg * (std::numbers::pi / 180.0);
    return center + QPointF(std::cos(rad) * radius, std::sin(rad) * radius);
}

// Intersección de dos rectas tangentes a una circunferencia en ángulos deg1 y deg2
inline QPointF tangentIntersect(const QPointF& center, double r, double deg1, double deg2) {
    const double a1 = deg1 * (std::numbers::pi / 180.0);
    const double a2 = deg2 * (std::numbers::pi / 180.0);
    const double c1 = std::cos(a1);
    const double s1 = std::sin(a1);
    const double c2 = std::cos(a2);
    const double s2 = std::sin(a2);
    const double det = c1 * s2 - s1 * c2;
    if (std::abs(det) < 1e-9)
        return center;
    const double dx = r * (s2 - s1) / det;
    const double dy = r * (c1 - c2) / det;
    return center + QPointF(dx, dy);
}

inline ShapeItem makeLine(const QPointF& a,
                          const QPointF& b,
                          const QColor& color = kInkMain,
                          double size = 2.2,
                          LinePattern pattern = LinePattern::Solid,
                          const QString& label = QString(),
                          LineEnding ending = LineEnding::None) {
    ShapeItem s;
    s.kind = ShapeKind::Line;
    s.color = color;
    s.size = size;
    s.linePattern = pattern;
    s.lineEnding = ending;
    s.label = label;
    s.vertices = {a, b};
    return s;
}

inline ShapeItem makeTriangle(const QPointF& a,
                              const QPointF& b,
                              const QPointF& c,
                              const QColor& color = kInkMain,
                              double size = 2.2) {
    // Polilínea cerrada de 3 vértices: evita el cuadradito blanco automático de
    // ShapeKind::Triangle (que se duplicaba sobre nuestro RightAngle de color acento).
    ShapeItem s;
    s.kind = ShapeKind::Polyline;
    s.color = color;
    s.size = size;
    s.closed = true;
    s.vertices = {a, b, c};
    return s;
}

inline ShapeItem makePolyline(const QVector<QPointF>& pts,
                              bool closed = true,
                              const QColor& color = kInkMain,
                              double size = 2.2,
                              LinePattern pattern = LinePattern::Solid) {
    ShapeItem s;
    s.kind = ShapeKind::Polyline;
    s.color = color;
    s.size = size;
    s.closed = closed;
    s.linePattern = pattern;
    s.vertices = pts;
    return s;
}

inline ShapeItem makeCircle(const QPointF& center,
                            double radius,
                            const QColor& color = kInkMain,
                            double size = 2.2,
                            LinePattern pattern = LinePattern::Solid) {
    ShapeItem s;
    s.kind = ShapeKind::Circle;
    s.color = color;
    s.size = size;
    s.linePattern = pattern;
    s.closed = true;
    s.vertices = {center, center + QPointF(radius, 0.0)};
    return s;
}

inline ShapeItem makeEllipse(const QPointF& center,
                             double rx,
                             double ry,
                             const QColor& color = kInkMain,
                             double size = 2.2) {
    ShapeItem s;
    s.kind = ShapeKind::Ellipse;
    s.color = color;
    s.size = size;
    s.closed = true;
    s.vertices = {center - QPointF(rx, ry), center + QPointF(rx, ry)};
    return s;
}

// AngleMarker con radio visual exacto `visualRadius` (arcGeometry usa 0.32 * min(|v1-V|, |v2-V|))
inline ShapeItem makeAngle(const QPointF& vertex,
                           const QPointF& ray1Target,
                           const QPointF& ray2Target,
                           const QColor& color = kAccent1,
                           double visualRadius = 18.0,
                           double size = 1.55) {
    const double armLen = visualRadius / 0.32;
    ShapeItem s;
    s.kind = ShapeKind::AngleMarker;
    s.color = color;
    s.size = size;
    s.label = autoVertexAngleLabel();
    s.vertices = {
        vertex,
        vertex + unitVec(ray1Target - vertex) * armLen,
        vertex + unitVec(ray2Target - vertex) * armLen,
    };
    return s;
}

// RightAngle con brazo controlado para tamaño nítido en diagramas ~220x135
inline ShapeItem makeRightAngle(const QPointF& arm1Target,
                                const QPointF& corner,
                                const QPointF& arm2Target,
                                const QColor& color = kAccent2,
                                double armLen = 30.0,
                                double size = 1.65) {
    ShapeItem s;
    s.kind = ShapeKind::RightAngle;
    s.color = color;
    s.size = size;
    s.vertices = {
        corner + unitVec(arm1Target - corner) * armLen,
        corner,
        corner + unitVec(arm2Target - corner) * armLen,
    };
    return s;
}

// Marca de congruencia centrada en un punto del segmento a->b (t = 0.5 por defecto)
inline ShapeItem makeMark(const QPointF& a,
                          const QPointF& b,
                          EdgeMarkKind kind,
                          const QColor& color = kAccent2,
                          double t = 0.5,
                          double size = 1.65) {
    const QPointF u = unitVec(b - a);
    const QPointF mid = lerpPt(a, b, t);
    const double halfSpan = std::min(8.5, std::hypot(b.x() - a.x(), b.y() - a.y()) * 0.22);
    ShapeItem s;
    s.kind = ShapeKind::Line;
    s.color = color;
    s.size = size;
    s.label = edgeMarkLabel(kind);
    s.vertices = {mid - u * halfSpan, mid + u * halfSpan};
    return s;
}

inline TextItem makeLabel(const QPointF& centerPt,
                          const QString& text,
                          const QColor& color = kInkMain,
                          double fontSize = 14.0) {
    TextItem t;
    t.text = text;
    t.color = color;
    t.fontSize = fontSize;
    const QFontMetricsF m(textFont(fontSize));
    t.pos = QPointF(centerPt.x() - m.horizontalAdvance(text) * 0.5,
                    centerPt.y() - m.height() * 0.5);
    t.bounds = textBounds(t);
    return t;
}

inline ShapeItem makeRectShadedRegion(const QRectF& r, const QColor& color, double opacity) {
    ShapeItem s;
    s.kind = ShapeKind::ShadedRegion;
    s.color = color;
    s.opacity = opacity;
    s.shadePattern = ShadePattern::Soft;
    s.closed = true;
    const QPointF tl = r.topLeft();
    const QPointF tr = r.topRight();
    const QPointF br = r.bottomRight();
    const QPointF bl = r.bottomLeft();
    // Formato de ShadedRegion: punto inicial seguido de ternas [ctrl1, ctrl2, end]
    s.vertices = {
        tl,
        tl, tr, tr,
        tr, br, br,
        br, bl, bl,
        bl, tl, tl,
    };
    return s;
}

inline StickerTemplate& addTemplate(QVector<StickerTemplate>& list,
                                    const QString& id,
                                    const QString& title,
                                    const QString& category,
                                    const QString& formula,
                                    const QStringList& keywords) {
    StickerTemplate tpl;
    tpl.id = id;
    tpl.title = title;
    tpl.category = category;
    tpl.formula = formula;
    tpl.keywords = keywords;
    tpl.baseSize = QSizeF(220.0, 135.0);
    list.append(std::move(tpl));
    return list.last();
}

void appendGeometryStickers(QVector<StickerTemplate>& list);
void appendScienceStickers(QVector<StickerTemplate>& list);

} // namespace finily::sticker_detail
