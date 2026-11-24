#include "engine/StickerLibrary.h"

#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace finily {
namespace {

// Colores centinela para plantillas integradas (adaptables al tema al instanciar)
const QColor kInkMain(QStringLiteral("#f8fafc"));
const QColor kAccent1(QStringLiteral("#38bdf8"));
const QColor kAccent2(QStringLiteral("#facc15"));
const QColor kAccent3(QStringLiteral("#4ade80"));

QString freshId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QFont stickerTextFont(double size) {
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(std::max(1, qRound(size)));
    return font;
}

QRectF computeTextBounds(const TextItem& item) {
    const QFontMetricsF metrics(stickerTextFont(item.fontSize));
    const QRectF ink = metrics.boundingRect(item.text).translated(item.pos + QPointF(0.0, metrics.ascent()));
    return ink.united(QRectF(item.pos, QSizeF(metrics.horizontalAdvance(item.text), metrics.height())));
}

void drawStickerText(QPainter& painter, const TextItem& item) {
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(stickerTextFont(item.fontSize));
    painter.setPen(item.color);
    painter.drawText(item.pos + QPointF(0.0, QFontMetricsF(painter.font()).ascent()), item.text);
    painter.restore();
}

QColor resolveSemanticColor(const QColor& c, const ThemeConfig& theme, bool isCustom) {
    if (isCustom)
        return c;
    const QString hex = c.name(QColor::HexRgb).toLower();
    QColor mapped = c;
    if (hex == QLatin1String("#f8fafc")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#f1f5f9")) : QColor(QStringLiteral("#1e293b"));
    } else if (hex == QLatin1String("#38bdf8")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#38bdf8")) : QColor(QStringLiteral("#0284c7"));
    } else if (hex == QLatin1String("#facc15")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#facc15")) : QColor(QStringLiteral("#d97706"));
    } else if (hex == QLatin1String("#4ade80")) {
        mapped = theme.isDark ? QColor(QStringLiteral("#4ade80")) : QColor(QStringLiteral("#16a34a"));
    }
    mapped.setAlpha(c.alpha());
    return mapped;
}

QPointF unitVec(const QPointF& v) {
    const double len = std::hypot(v.x(), v.y());
    return (len > 1e-9) ? (v / len) : QPointF(1.0, 0.0);
}

QPointF lerpPt(const QPointF& a, const QPointF& b, double t) {
    return a + (b - a) * t;
}

QPointF polarPt(const QPointF& center, double radius, double deg) {
    const double rad = deg * (std::numbers::pi / 180.0);
    return center + QPointF(std::cos(rad) * radius, std::sin(rad) * radius);
}

// Intersección de dos rectas tangentes a una circunferencia en ángulos deg1 y deg2
QPointF tangentIntersect(const QPointF& center, double r, double deg1, double deg2) {
    const double a1 = deg1 * (std::numbers::pi / 180.0);
    const double a2 = deg2 * (std::numbers::pi / 180.0);
    const double c1 = std::cos(a1), s1 = std::sin(a1);
    const double c2 = std::cos(a2), s2 = std::sin(a2);
    const double det = c1 * s2 - s1 * c2;
    if (std::abs(det) < 1e-9)
        return center;
    const double dx = r * (s2 - s1) / det;
    const double dy = r * (c1 - c2) / det;
    return center + QPointF(dx, dy);
}

// Constructores concisos de primitivas para el catálogo
ShapeItem makeLine(const QPointF& a, const QPointF& b, const QColor& color = kInkMain,
                   double size = 2.2, LinePattern pattern = LinePattern::Solid,
                   const QString& label = QString()) {
    ShapeItem s;
    s.kind = ShapeKind::Line;
    s.color = color;
    s.size = size;
    s.linePattern = pattern;
    s.label = label;
    s.vertices = {a, b};
    return s;
}

ShapeItem makeTriangle(const QPointF& a, const QPointF& b, const QPointF& c,
                       const QColor& color = kInkMain, double size = 2.2) {
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

ShapeItem makePolyline(const QVector<QPointF>& pts, bool closed = true,
                       const QColor& color = kInkMain, double size = 2.2,
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

ShapeItem makeCircle(const QPointF& center, double radius,
                     const QColor& color = kInkMain, double size = 2.2,
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

// AngleMarker con radio visual exacto `visualRadius` (arcGeometry usa 0.32 * min(|v1-V|, |v2-V|))
ShapeItem makeAngle(const QPointF& vertex, const QPointF& ray1Target, const QPointF& ray2Target,
                    const QColor& color = kAccent1, double visualRadius = 18.0, double size = 1.55) {
    const double armLen = visualRadius / 0.32;
    ShapeItem s;
    s.kind = ShapeKind::AngleMarker;
    s.color = color;
    s.size = size;
    s.label = QStringLiteral("auto_vertex");
    s.vertices = {
        vertex,
        vertex + unitVec(ray1Target - vertex) * armLen,
        vertex + unitVec(ray2Target - vertex) * armLen
    };
    return s;
}

// RightAngle con brazo controlado para tamaño nítido en diagramas ~220x135
ShapeItem makeRightAngle(const QPointF& arm1Target, const QPointF& corner, const QPointF& arm2Target,
                         const QColor& color = kAccent2, double armLen = 30.0, double size = 1.65) {
    ShapeItem s;
    s.kind = ShapeKind::RightAngle;
    s.color = color;
    s.size = size;
    s.vertices = {
        corner + unitVec(arm1Target - corner) * armLen,
        corner,
        corner + unitVec(arm2Target - corner) * armLen
    };
    return s;
}

// Marca de congruencia centrada en un punto del segmento a->b (t = 0.5 por defecto)
ShapeItem makeMark(const QPointF& a, const QPointF& b, const QString& markLabel,
                   const QColor& color = kAccent2, double t = 0.5, double size = 1.65) {
    const QPointF u = unitVec(b - a);
    const QPointF mid = lerpPt(a, b, t);
    const double halfSpan = std::min(8.5, std::hypot(b.x() - a.x(), b.y() - a.y()) * 0.22);
    ShapeItem s;
    s.kind = ShapeKind::Line;
    s.color = color;
    s.size = size;
    s.label = markLabel;
    s.vertices = {mid - u * halfSpan, mid + u * halfSpan};
    return s;
}

TextItem makeLabel(const QPointF& centerPt, const QString& text,
                   const QColor& color = kInkMain, double fontSize = 14.0) {
    TextItem t;
    t.text = text;
    t.color = color;
    t.fontSize = fontSize;
    const QFontMetricsF m(stickerTextFont(fontSize));
    t.pos = QPointF(centerPt.x() - m.horizontalAdvance(text) * 0.5,
                    centerPt.y() - m.height() * 0.5);
    t.bounds = computeTextBounds(t);
    return t;
}

ShapeItem makeRectShadedRegion(const QRectF& r, const QColor& color, double opacity) {
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
        bl, tl, tl
    };
    return s;
}

// ---------------------------------------------------------------------------
// Catálogo integrado de 31 plantillas vectoriales de Geometría
// ---------------------------------------------------------------------------
QVector<StickerTemplate> buildBuiltinTemplates() {
    QVector<StickerTemplate> list;
    list.reserve(31);

    auto addTpl = [&](const QString& id, const QString& title, const QString& category,
                      const QString& formula, const QStringList& keywords) -> StickerTemplate& {
        StickerTemplate tpl;
        tpl.id = id;
        tpl.title = title;
        tpl.category = category;
        tpl.formula = formula;
        tpl.keywords = keywords;
        tpl.baseSize = QSizeF(220.0, 135.0);
        list.append(tpl);
        return list.last();
    };

    // ==================== 1. TRIÁNGULOS (7) ====================

    // 1. Ángulo Exterior
    {
        auto& t = addTpl(QStringLiteral("tri_exterior"), QStringLiteral("Ángulo Exterior"),
                         QStringLiteral("Triángulos"), QStringLiteral("x = α + β"),
                         {QStringLiteral("triangulo"), QStringLiteral("angulo"), QStringLiteral("exterior")});
        const QPointF A(-86, 42), B(-14, -48), C(34, 42), D(96, 42);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(C, D, kInkMain, 2.2),
            makeAngle(A, B, C, kAccent1, 20.0),
            makeAngle(B, A, C, kAccent1, 20.0),
            makeAngle(C, B, D, kAccent2, 22.0)
        };
        t.texts = {
            makeLabel(QPointF(-54, 30), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(-10, -18), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(58, 25), QStringLiteral("x"), kAccent2, 15.0)
        };
    }

    // 2. Propiedad del Boomerang
    {
        auto& t = addTpl(QStringLiteral("tri_boomerang"), QStringLiteral("Prop. Boomerang"),
                         QStringLiteral("Triángulos"), QStringLiteral("x = α + β + θ"),
                         {QStringLiteral("boomerang"), QStringLiteral("pantaloncito"), QStringLiteral("concavo"), QStringLiteral("cuadrilatero")});
        const QPointF A(-88, 46), B(0, -52), C(88, 46), D(0, 8);
        t.shapes = {
            makePolyline({A, B, C, D}, true, kInkMain, 2.2),
            makeAngle(A, B, D, kAccent1, 22.0),
            makeAngle(B, A, C, kAccent1, 20.0),
            makeAngle(C, B, D, kAccent1, 22.0),
            makeAngle(D, A, C, kAccent2, 18.0)
        };
        t.texts = {
            makeLabel(QPointF(-56, 30), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(0, -21), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(56, 30), QStringLiteral("θ"), kAccent1, 14.0),
            makeLabel(QPointF(0, 32), QStringLiteral("x"), kAccent2, 15.0)
        };
    }

    // 3. Propiedad de la Corbatita
    {
        auto& t = addTpl(QStringLiteral("tri_corbatita"), QStringLiteral("Prop. Corbatita"),
                         QStringLiteral("Triángulos"), QStringLiteral("α + β = θ + ω"),
                         {QStringLiteral("corbatita"), QStringLiteral("mariposa"), QStringLiteral("opuestos")});
        const QPointF A(-86, -42), B(-86, 42), C(86, -42), D(86, 42), O(0, 0);
        t.shapes = {
            makeTriangle(A, B, O),
            makeTriangle(C, D, O),
            makeAngle(A, B, O, kAccent1, 19.0),
            makeAngle(B, A, O, kAccent1, 19.0),
            makeAngle(C, D, O, kAccent2, 19.0),
            makeAngle(D, C, O, kAccent2, 19.0)
        };
        t.texts = {
            makeLabel(QPointF(-67, -20), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(-67, 20), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(67, -20), QStringLiteral("θ"), kAccent2, 14.0),
            makeLabel(QPointF(67, 20), QStringLiteral("ω"), kAccent2, 14.0)
        };
    }

    // 4. Propiedad del Pescadito
    {
        auto& t = addTpl(QStringLiteral("tri_pescadito"), QStringLiteral("Prop. Pescadito"),
                         QStringLiteral("Triángulos"), QStringLiteral("α + β = x + y"),
                         {QStringLiteral("pescadito"), QStringLiteral("cuadrilatero"), QStringLiteral("exteriores")});
        const QPointF A(-88, 0), B(-8, -46), C(76, 0), D(-8, 46);
        // Prolongaciones exteriores en B y D
        const QPointF Bext = B + unitVec(B - A) * 34.0;
        const QPointF Dext = D + unitVec(D - A) * 34.0;
        t.shapes = {
            makePolyline({A, B, C, D}, true, kInkMain, 2.2),
            makeLine(B, Bext, kInkMain, 2.0),
            makeLine(D, Dext, kInkMain, 2.0),
            makeAngle(A, B, D, kAccent1, 20.0),
            makeAngle(C, B, D, kAccent1, 20.0),
            makeAngle(B, Bext, C, kAccent2, 18.0),
            makeAngle(D, Dext, C, kAccent2, 18.0)
        };
        t.texts = {
            makeLabel(QPointF(-58, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(46, 0), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(24, -44), QStringLiteral("x"), kAccent2, 14.0),
            makeLabel(QPointF(24, 44), QStringLiteral("y"), kAccent2, 14.0)
        };
    }

    // 5. Base Media
    {
        auto& t = addTpl(QStringLiteral("tri_base_media"), QStringLiteral("Base Media"),
                         QStringLiteral("Triángulos"), QStringLiteral("MN = AC / 2"),
                         {QStringLiteral("base"), QStringLiteral("media"), QStringLiteral("punto medio"), QStringLiteral("paralela")});
        const QPointF A(-84, 46), B(-6, -50), C(84, 46);
        const QPointF M = lerpPt(A, B, 0.5);
        const QPointF N = lerpPt(B, C, 0.5);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(M, N, kAccent1, 2.2),
            makeMark(A, M, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(M, B, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(B, N, QStringLiteral("mark:tick2"), kAccent2),
            makeMark(N, C, QStringLiteral("mark:tick2"), kAccent2),
            makeMark(M, N, QStringLiteral("mark:parallel1"), kAccent3),
            makeMark(A, C, QStringLiteral("mark:parallel1"), kAccent3)
        };
        t.texts = {
            makeLabel(QPointF(-95, 48), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(-6, -61), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(95, 48), QStringLiteral("C"), kInkMain, 13.0),
            makeLabel(QPointF(M.x() - 15, M.y() - 4), QStringLiteral("M"), kAccent1, 13.0),
            makeLabel(QPointF(N.x() + 15, N.y() - 4), QStringLiteral("N"), kAccent1, 13.0)
        };
    }

    // 6. Mediana a la Hipotenusa
    {
        auto& t = addTpl(QStringLiteral("tri_mediana_hip"), QStringLiteral("Mediana a Hipotenusa"),
                         QStringLiteral("Triángulos"), QStringLiteral("BM = AC / 2"),
                         {QStringLiteral("mediana"), QStringLiteral("hipotenusa"), QStringLiteral("rectangulo")});
        // Triángulo rectángulo inscrito en semicírculo de radio 84 centrado en M(0, 38)
        const QPointF A(-84, 38), C(84, 38), M(0, 38);
        // B en el semicírculo: ángulo 120° -> (-42, 38 - 72.7) = (-42, -34.7)
        const QPointF B(-42.0, 38.0 - 84.0 * std::sin(std::numbers::pi / 3.0));
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, M, kAccent1, 2.2),
            makeRightAngle(A, B, C, kAccent2, 34.0),
            makeMark(A, M, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(M, C, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(B, M, QStringLiteral("mark:tick1"), kAccent2)
        };
        t.texts = {
            makeLabel(QPointF(-94, 40), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(-46, -46), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(94, 40), QStringLiteral("C"), kInkMain, 13.0),
            makeLabel(QPointF(0, 52), QStringLiteral("M"), kAccent1, 13.0)
        };
    }

    // 7. Existencia de Triángulo
    {
        auto& t = addTpl(QStringLiteral("tri_existencia"), QStringLiteral("Existencia de Triángulo"),
                         QStringLiteral("Triángulos"), QStringLiteral("b − c < a < b + c"),
                         {QStringLiteral("existencia"), QStringLiteral("desigualdad"), QStringLiteral("triangular"), QStringLiteral("lados")});
        const QPointF A(-84, 44), B(-18, -46), C(84, 44);
        t.shapes = {
            makeTriangle(A, B, C)
        };
        t.texts = {
            makeLabel(QPointF(0, 58), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF(42, -8), QStringLiteral("b"), kAccent2, 15.0),
            makeLabel(QPointF(-62, -6), QStringLiteral("c"), kAccent3, 15.0)
        };
    }

    // ==================== 2. NOTABLES (6) ====================

    // 8. Notable 30° – 60° (exacto 1 : √3 : 2)
    {
        auto& t = addTpl(QStringLiteral("not_30_60"), QStringLiteral("Notable 30° – 60°"),
                         QStringLiteral("Notables"), QStringLiteral("k, k√3, 2k"),
                         {QStringLiteral("notable"), QStringLiteral("30"), QStringLiteral("60"), QStringLiteral("rectangulo")});
        const double k = 72.0;
        const double base = k * std::sqrt(3.0); // ~124.7
        const QPointF O(-62.0, 44.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + base, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 18.0),
            makeAngle(R, T, O, kAccent1, 24.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("k√3"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12), QStringLiteral("2k"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 15, T.y() + 27), QStringLiteral("60°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 37, R.y() - 9), QStringLiteral("30°"), kAccent1, 11.5)
        };
    }

    // 9. Notable 45° – 45° (exacto 1 : 1 : √2)
    {
        auto& t = addTpl(QStringLiteral("not_45_45"), QStringLiteral("Notable 45° – 45°"),
                         QStringLiteral("Notables"), QStringLiteral("k, k, k√2"),
                         {QStringLiteral("notable"), QStringLiteral("45"), QStringLiteral("isosceles"), QStringLiteral("rectangulo")});
        const double k = 102.0;
        const QPointF O(-52.0, 50.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 21.0),
            makeAngle(R, T, O, kAccent1, 21.0),
            makeMark(O, T, QStringLiteral("mark:tick1"), kAccent2, 0.45),
            makeMark(O, R, QStringLiteral("mark:tick1"), kAccent2, 0.45)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5 + 4), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5 - 4, O.y() + 17), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 18, (T.y() + R.y()) * 0.5 - 12), QStringLiteral("k√2"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 14, T.y() + 31), QStringLiteral("45°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 32, R.y() - 11), QStringLiteral("45°"), kAccent1, 11.5)
        };
    }

    // 10. Notable 37° – 53° (exacto 3 : 4 : 5)
    {
        auto& t = addTpl(QStringLiteral("not_37_53"), QStringLiteral("Notable 37° – 53°"),
                         QStringLiteral("Notables"), QStringLiteral("3k, 4k, 5k"),
                         {QStringLiteral("notable"), QStringLiteral("37"), QStringLiteral("53"), QStringLiteral("3 4 5")});
        const QPointF O(-58.0, 46.0);
        const QPointF T(-58.0, 46.0 - 84.0);  // 3 * 28
        const QPointF R(-58.0 + 112.0, 46.0); // 4 * 28
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 18.0),
            makeAngle(R, T, O, kAccent1, 22.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5), QStringLiteral("3k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("4k"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12), QStringLiteral("5k"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 15, T.y() + 28), QStringLiteral("53°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 34, R.y() - 10), QStringLiteral("37°"), kAccent1, 11.5)
        };
    }

    // 11. Notable 16° – 74° (exacto 7 : 24 : 25)
    {
        auto& t = addTpl(QStringLiteral("not_16_74"), QStringLiteral("Notable 16° – 74°"),
                         QStringLiteral("Notables"), QStringLiteral("7k, 24k, 25k"),
                         {QStringLiteral("notable"), QStringLiteral("16"), QStringLiteral("74"), QStringLiteral("7 24 25")});
        const double u = 7.0;
        const QPointF O(-84.0, 36.0);
        const QPointF T(O.x(), O.y() - 7.0 * u);       // 49.0
        const QPointF R(O.x() + 24.0 * u, O.y());      // 168.0
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 26.0),
            makeAngle(T, O, R, kAccent1, 15.0),
            makeAngle(R, T, O, kAccent1, 28.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5), QStringLiteral("7k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("24k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 12, (T.y() + R.y()) * 0.5 - 14), QStringLiteral("25k"), kAccent3, 13.5),
            makeLabel(QPointF(T.x() + 18, T.y() + 21), QStringLiteral("74°"), kAccent1, 11.0),
            makeLabel(QPointF(R.x() - 44, R.y() - 8), QStringLiteral("16°"), kAccent1, 11.0)
        };
    }

    // 12. Notable 37°/2 (exacto 1 : 3 : √10)
    {
        auto& t = addTpl(QStringLiteral("not_37_2"), QStringLiteral("Notable 37°/2"),
                         QStringLiteral("Notables"), QStringLiteral("k, 3k, k√10"),
                         {QStringLiteral("notable"), QStringLiteral("37/2"), QStringLiteral("18.5"), QStringLiteral("raiz 10")});
        const double k = 52.0;
        const QPointF O(-78.0, 36.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + 3.0 * k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 26.0),
            makeAngle(R, T, O, kAccent1, 28.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("3k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 14), QStringLiteral("k√10"), kAccent3, 13.5),
            makeLabel(QPointF(R.x() - 48, R.y() - 9), QStringLiteral("37°/2"), kAccent1, 11.0)
        };
    }

    // 13. Notable 53°/2 (exacto 1 : 2 : √5)
    {
        auto& t = addTpl(QStringLiteral("not_53_2"), QStringLiteral("Notable 53°/2"),
                         QStringLiteral("Notables"), QStringLiteral("k, 2k, k√5"),
                         {QStringLiteral("notable"), QStringLiteral("53/2"), QStringLiteral("26.5"), QStringLiteral("raiz 5")});
        const double k = 66.0;
        const QPointF O(-66.0, 40.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + 2.0 * k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 28.0),
            makeAngle(R, T, O, kAccent1, 25.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("2k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 16, (T.y() + R.y()) * 0.5 - 13), QStringLiteral("k√5"), kAccent3, 13.5),
            makeLabel(QPointF(R.x() - 44, R.y() - 10), QStringLiteral("53°/2"), kAccent1, 11.0)
        };
    }

    // ==================== 3. CONGRUENCIA (4) ====================

    // 14. Teorema de la Bisectriz
    {
        auto& t = addTpl(QStringLiteral("cong_bisectriz"), QStringLiteral("T. de la Bisectriz"),
                         QStringLiteral("Congruencia"), QStringLiteral("PA = PB, OA = OB"),
                         {QStringLiteral("bisectriz"), QStringLiteral("congruencia"), QStringLiteral("perpendicular")});
        const QPointF O(-86, 0);
        const double halfDeg = 28.0;
        const QPointF ray1 = polarPt(O, 175.0, -halfDeg);
        const QPointF ray2 = polarPt(O, 175.0, halfDeg);
        const QPointF bisEnd = O + QPointF(178.0, 0.0);
        const QPointF P(36.0, 0.0); // Sobre la bisectriz
        // Pies de perpendiculares exactos: proyección de (P - O) sobre cada rayo
        const double distOP = P.x() - O.x();
        const double proj = distOP * std::cos(halfDeg * std::numbers::pi / 180.0);
        const QPointF A = polarPt(O, proj, -halfDeg);
        const QPointF B = polarPt(O, proj, halfDeg);
        t.shapes = {
            makeLine(O, ray1, kInkMain, 2.2),
            makeLine(O, ray2, kInkMain, 2.2),
            makeLine(O, bisEnd, kAccent1, 2.0, LinePattern::Dashed),
            makeLine(P, A, kAccent2, 2.1),
            makeLine(P, B, kAccent2, 2.1),
            makeRightAngle(O, A, P, kAccent2, 28.0),
            makeRightAngle(O, B, P, kAccent2, 28.0),
            makeAngle(O, ray1, bisEnd, kAccent1, 26.0),
            makeAngle(O, bisEnd, ray2, kAccent1, 26.0),
            makeMark(P, A, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(P, B, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(O, A, QStringLiteral("mark:tick2"), kAccent3, 0.55),
            makeMark(O, B, QStringLiteral("mark:tick2"), kAccent3, 0.55)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 13, 0), QStringLiteral("O"), kInkMain, 13.0),
            makeLabel(QPointF(P.x() + 14, 0), QStringLiteral("P"), kAccent1, 13.0),
            makeLabel(QPointF(A.x() - 4, A.y() - 14), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(B.x() - 4, B.y() + 15), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(O.x() + 40, -10), QStringLiteral("α"), kAccent1, 13.0),
            makeLabel(QPointF(O.x() + 40, 10), QStringLiteral("α"), kAccent1, 13.0)
        };
    }

    // 15. Teorema de la Mediatriz
    {
        auto& t = addTpl(QStringLiteral("cong_mediatriz"), QStringLiteral("T. de la Mediatriz"),
                         QStringLiteral("Congruencia"), QStringLiteral("PA = PB"),
                         {QStringLiteral("mediatriz"), QStringLiteral("congruencia"), QStringLiteral("isosceles")});
        const QPointF A(-80, 44), B(80, 44), M(0, 44), P(0, -34);
        t.shapes = {
            makeLine(A, B, kInkMain, 2.2),
            makeLine(QPointF(0, -56), QPointF(0, 58), kAccent1, 2.0, LinePattern::Dashed),
            makeLine(P, A, kInkMain, 2.2),
            makeLine(P, B, kInkMain, 2.2),
            makeRightAngle(P, M, B, kAccent2, 30.0),
            makeMark(A, M, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(M, B, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(P, A, QStringLiteral("mark:tick2"), kAccent3),
            makeMark(P, B, QStringLiteral("mark:tick2"), kAccent3)
        };
        t.texts = {
            makeLabel(QPointF(-91, 46), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(91, 46), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(14, 54), QStringLiteral("M"), kAccent1, 13.0),
            makeLabel(QPointF(14, -38), QStringLiteral("P"), kAccent1, 13.0)
        };
    }

    // 16. Teorema de Tales
    {
        auto& t = addTpl(QStringLiteral("cong_tales"), QStringLiteral("Teorema de Tales"),
                         QStringLiteral("Congruencia"), QStringLiteral("a / b = c / d"),
                         {QStringLiteral("tales"), QStringLiteral("proporcionalidad"), QStringLiteral("paralelas"), QStringLiteral("semejanza")});
        const double y1 = -40.0, y2 = -2.0, y3 = 44.0;
        const QPointF L1a(-94, y1), L1b(94, y1);
        const QPointF L2a(-94, y2), L2b(94, y2);
        const QPointF L3a(-94, y3), L3b(94, y3);
        // Secante 1 (inclinada hacia la izquierda)
        const QPointF S1top(-24, -56), S1bot(-62, 58);
        // Secante 2 (inclinada hacia la derecha)
        const QPointF S2top(24, -56), S2bot(66, 58);
        t.shapes = {
            makeLine(L1a, L1b, kAccent3, 2.0),
            makeLine(L2a, L2b, kAccent3, 2.0),
            makeLine(L3a, L3b, kAccent3, 2.0),
            makeLine(S1top, S1bot, kInkMain, 2.2),
            makeLine(S2top, S2bot, kInkMain, 2.2),
            makeMark(QPointF(-90, y1), QPointF(-60, y1), QStringLiteral("mark:parallel1"), kAccent3),
            makeMark(QPointF(-90, y2), QPointF(-60, y2), QStringLiteral("mark:parallel1"), kAccent3),
            makeMark(QPointF(-90, y3), QPointF(-60, y3), QStringLiteral("mark:parallel1"), kAccent3)
        };
        t.texts = {
            makeLabel(QPointF(-52, -21), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF(-64, 21), QStringLiteral("b"), kAccent1, 15.0),
            makeLabel(QPointF(54, -21), QStringLiteral("c"), kAccent2, 15.0),
            makeLabel(QPointF(68, 21), QStringLiteral("d"), kAccent2, 15.0)
        };
    }

    // 17. Teorema de la Bisectriz Interior
    {
        auto& t = addTpl(QStringLiteral("cong_bisectriz_int"), QStringLiteral("Bisectriz Interior"),
                         QStringLiteral("Congruencia"), QStringLiteral("a / b = m / n"),
                         {QStringLiteral("bisectriz"), QStringLiteral("interior"), QStringLiteral("proporcionalidad")});
        const QPointF A(-84, 44), B(-14, -50), C(84, 44);
        const double lenAB = std::hypot(B.x() - A.x(), B.y() - A.y());
        const double lenBC = std::hypot(C.x() - B.x(), C.y() - B.y());
        // Punto D exacto sobre AC tal que AD / DC = AB / BC
        const QPointF D = lerpPt(A, C, lenAB / (lenAB + lenBC));
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, D, kAccent1, 2.2),
            makeAngle(B, A, D, kAccent1, 20.0),
            makeAngle(B, D, C, kAccent1, 20.0)
        };
        t.texts = {
            makeLabel(QPointF(-62, -8), QStringLiteral("a"), kInkMain, 15.0),
            makeLabel(QPointF(48, -8), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF((A.x() + D.x()) * 0.5, 58), QStringLiteral("m"), kAccent2, 15.0),
            makeLabel(QPointF((D.x() + C.x()) * 0.5, 58), QStringLiteral("n"), kAccent2, 15.0),
            makeLabel(QPointF(-24, -18), QStringLiteral("α"), kAccent1, 13.0),
            makeLabel(QPointF(-1, -18), QStringLiteral("α"), kAccent1, 13.0)
        };
    }

    // ==================== 4. CIRCUNFERENCIA (6) ====================

    // 18. Ángulo Inscrito
    {
        auto& t = addTpl(QStringLiteral("circ_inscrito"), QStringLiteral("Ángulo Inscrito"),
                         QStringLiteral("Circunferencia"), QStringLiteral("α = θ / 2"),
                         {QStringLiteral("circunferencia"), QStringLiteral("inscrito"), QStringLiteral("central"), QStringLiteral("arco")});
        const QPointF O(0, 2);
        const double R = 56.0;
        const QPointF P = polarPt(O, R, -110.0);
        const QPointF A = polarPt(O, R, 135.0);
        const QPointF B = polarPt(O, R, 35.0);
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(P, A, kAccent1, 2.0),
            makeLine(P, B, kAccent1, 2.0),
            makeLine(O, A, kAccent2, 2.0),
            makeLine(O, B, kAccent2, 2.0),
            makeAngle(P, A, B, kAccent1, 20.0),
            makeAngle(O, A, B, kAccent2, 18.0),
            makeMark(O - QPointF(2, 0), O + QPointF(2, 0), QStringLiteral("mark:dot"), kInkMain, 0.5, 2.0)
        };
        t.texts = {
            makeLabel(QPointF(-11, -24), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(2, 25), QStringLiteral("θ"), kAccent2, 14.0),
            makeLabel(QPointF(11, -6), QStringLiteral("O"), kInkMain, 12.0)
        };
    }

    // 19. Ángulo Interior
    {
        auto& t = addTpl(QStringLiteral("circ_interior"), QStringLiteral("Ángulo Interior"),
                         QStringLiteral("Circunferencia"), QStringLiteral("x = (α + β) / 2"),
                         {QStringLiteral("circunferencia"), QStringLiteral("interior"), QStringLiteral("cuerdas"), QStringLiteral("arcos")});
        const QPointF O(0, 0);
        const double R = 56.0;
        const QPointF A = polarPt(O, R, -145.0);
        const QPointF B = polarPt(O, R, 25.0);
        const QPointF C = polarPt(O, R, 140.0);
        const QPointF D = polarPt(O, R, -35.0);
        // Intersección exacta de AB y CD
        const QPointF d1 = B - A, d2 = D - C;
        const double det = d1.x() * d2.y() - d1.y() * d2.x();
        const QPointF diff = C - A;
        const double s = (diff.x() * d2.y() - diff.y() * d2.x()) / det;
        const QPointF X = A + d1 * s;
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(A, B, kInkMain, 2.0),
            makeLine(C, D, kInkMain, 2.0),
            makeAngle(X, A, C, kAccent2, 17.0),
            makeAngle(X, D, B, kAccent2, 17.0)
        };
        t.texts = {
            makeLabel(QPointF(-68, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(68, -4), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(X.x() + 20, X.y() - 3), QStringLiteral("x"), kAccent2, 14.0)
        };
    }

    // 20. Ángulo Exterior
    {
        auto& t = addTpl(QStringLiteral("circ_exterior"), QStringLiteral("Ángulo Exterior"),
                         QStringLiteral("Circunferencia"), QStringLiteral("x = (α − β) / 2"),
                         {QStringLiteral("circunferencia"), QStringLiteral("exterior"), QStringLiteral("secantes"), QStringLiteral("arcos")});
        const QPointF O(-34, 0);
        const double R = 48.0;
        const QPointF P(86, 0);
        // Dos secantes simétricas desde P en ángulos ±18° hacia O
        auto intersectSecant = [&](double degFromLeft) -> std::pair<QPointF, QPointF> {
            const double rad = degFromLeft * (std::numbers::pi / 180.0);
            const QPointF dir(-std::cos(rad), std::sin(rad));
            const QPointF oc = P - O;
            const double b = 2.0 * (oc.x() * dir.x() + oc.y() * dir.y());
            const double c = oc.x() * oc.x() + oc.y() * oc.y() - R * R;
            const double disc = std::sqrt(std::max(0.0, b * b - 4.0 * c));
            const double tNear = (-b - disc) * 0.5;
            const double tFar = (-b + disc) * 0.5;
            return {P + dir * tNear, P + dir * tFar};
        };
        const auto [B1, A1] = intersectSecant(-17.0);
        const auto [B2, A2] = intersectSecant(17.0);
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(P, A1, kInkMain, 2.0),
            makeLine(P, A2, kInkMain, 2.0),
            makeAngle(P, A1, A2, kAccent2, 24.0)
        };
        t.texts = {
            makeLabel(QPointF(-93, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(25, 0), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(54, 0), QStringLiteral("x"), kAccent2, 14.0),
            makeLabel(QPointF(96, 0), QStringLiteral("P"), kInkMain, 13.0)
        };
    }

    // 21. Teorema de Poncelet (3:4:5 con circunferencia EXACTAMENTE inscrita)
    {
        auto& t = addTpl(QStringLiteral("circ_poncelet"), QStringLiteral("Teorema de Poncelet"),
                         QStringLiteral("Circunferencia"), QStringLiteral("a + b = c + 2r"),
                         {QStringLiteral("poncelet"), QStringLiteral("inradio"), QStringLiteral("inscrita"), QStringLiteral("rectangulo")});
        // a = 78 (3*26), b = 104 (4*26), c = 130 (5*26) => r = (a + b - c)/2 = 26
        const double r = 26.0;
        const QPointF O(-56.0, 46.0);
        const QPointF T(O.x(), O.y() - 3.0 * r);
        const QPointF R(O.x() + 4.0 * r, O.y());
        const QPointF incenter(O.x() + r, O.y() - r);
        t.shapes = {
            makeTriangle(O, T, R),
            makeCircle(incenter, r, kAccent2, 2.1),
            makeLine(incenter, QPointF(incenter.x(), O.y()), kAccent1, 1.9, LinePattern::Dashed),
            makeRightAngle(T, O, R, kAccent1, 30.0),
            makeMark(incenter - QPointF(2, 0), incenter + QPointF(2, 0), QStringLiteral("mark:dot"), kAccent2, 0.5, 2.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 16, (O.y() + T.y()) * 0.5), QStringLiteral("a"), kInkMain, 15.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12), QStringLiteral("c"), kAccent3, 15.0),
            makeLabel(QPointF(incenter.x() + 10, incenter.y() + 12), QStringLiteral("r"), kAccent2, 14.0)
        };
    }

    // 22. Teorema de Pitot (cuadrilátero exactamente circunscrito)
    {
        auto& t = addTpl(QStringLiteral("circ_pitot"), QStringLiteral("Teorema de Pitot"),
                         QStringLiteral("Circunferencia"), QStringLiteral("a + c = b + d"),
                         {QStringLiteral("pitot"), QStringLiteral("cuadrilatero"), QStringLiteral("circunscrito")});
        const QPointF C(0, 2);
        const double R = 40.0;
        // Cuatro ángulos de tangencia: inferior 90°, derecho 12°, superior -76°, izquierdo 198°
        const QPointF V1 = tangentIntersect(C, R, 90.0, 198.0);   // inferior izq
        const QPointF V2 = tangentIntersect(C, R, 198.0, -76.0);  // superior izq
        const QPointF V3 = tangentIntersect(C, R, -76.0, 12.0);   // superior der
        const QPointF V4 = tangentIntersect(C, R, 12.0, 90.0);    // inferior der
        t.shapes = {
            makePolyline({V1, V2, V3, V4}, true, kInkMain, 2.2),
            makeCircle(C, R, kAccent2, 2.0)
        };
        t.texts = {
            makeLabel(lerpPt(V1, V2, 0.5) + QPointF(-15, 0), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(lerpPt(V2, V3, 0.5) + QPointF(0, -14), QStringLiteral("b"), kAccent3, 15.0),
            makeLabel(lerpPt(V3, V4, 0.5) + QPointF(15, 0), QStringLiteral("c"), kAccent1, 15.0),
            makeLabel(lerpPt(V4, V1, 0.5) + QPointF(0, 15), QStringLiteral("d"), kAccent3, 15.0)
        };
    }

    // 23. Tangentes Exteriores
    {
        auto& t = addTpl(QStringLiteral("circ_tangentes"), QStringLiteral("Tangentes Exteriores"),
                         QStringLiteral("Circunferencia"), QStringLiteral("PA = PB"),
                         {QStringLiteral("tangente"), QStringLiteral("tangentes"), QStringLiteral("circunferencia"), QStringLiteral("radio")});
        const QPointF O(-36, 0);
        const double R = 42.0;
        const QPointF P(82, 0);
        const double d = P.x() - O.x();
        const double cosPhi = R / d;
        const double sinPhi = std::sqrt(std::max(0.0, 1.0 - cosPhi * cosPhi));
        const QPointF A(O.x() + R * cosPhi, O.y() - R * sinPhi);
        const QPointF B(O.x() + R * cosPhi, O.y() + R * sinPhi);
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(P, A, kAccent1, 2.2),
            makeLine(P, B, kAccent1, 2.2),
            makeLine(O, A, kAccent2, 1.9, LinePattern::Dashed),
            makeLine(O, B, kAccent2, 1.9, LinePattern::Dashed),
            makeRightAngle(O, A, P, kAccent2, 28.0),
            makeRightAngle(O, B, P, kAccent2, 28.0),
            makeMark(P, A, QStringLiteral("mark:tick1"), kAccent3),
            makeMark(P, B, QStringLiteral("mark:tick1"), kAccent3),
            makeMark(O - QPointF(2, 0), O + QPointF(2, 0), QStringLiteral("mark:dot"), kInkMain, 0.5, 2.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 12, 0), QStringLiteral("O"), kInkMain, 13.0),
            makeLabel(QPointF(P.x() + 13, 0), QStringLiteral("P"), kAccent1, 13.0),
            makeLabel(QPointF(A.x() - 2, A.y() - 15), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(B.x() - 2, B.y() + 15), QStringLiteral("B"), kInkMain, 13.0)
        };
    }

    // ==================== 5. MÉTRICAS (4) ====================

    // 24. Teorema de Pitágoras
    {
        auto& t = addTpl(QStringLiteral("met_pitagoras"), QStringLiteral("Teorema de Pitágoras"),
                         QStringLiteral("Métricas"), QStringLiteral("a² + b² = c²"),
                         {QStringLiteral("pitagoras"), QStringLiteral("hipotenusa"), QStringLiteral("catetos"), QStringLiteral("rectangulo")});
        const QPointF O(-56, 44), T(-56, -38), R(56, 44);
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 34.0)
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 16, (O.y() + T.y()) * 0.5), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("b"), kAccent1, 15.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12), QStringLiteral("c"), kAccent3, 15.0)
        };
    }

    // 25. Relaciones Métricas en Triángulo Rectángulo (h² = m·n, a² = c·m)
    {
        auto& t = addTpl(QStringLiteral("met_tri_rect"), QStringLiteral("Métricas en T. Rect."),
                         QStringLiteral("Métricas"), QStringLiteral("h² = m·n · a² = c·m"),
                         {QStringLiteral("metricas"), QStringLiteral("altura"), QStringLiteral("proyeccion"), QStringLiteral("rectangulo")});
        // Exacto: m = 44, n = 99 => h = sqrt(44*99) = 66, c = 143
        const QPointF A(-71.5, 38.0), C(71.5, 38.0);
        const QPointF H(-71.5 + 44.0, 38.0);
        const QPointF B(H.x(), 38.0 - 66.0);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, H, kAccent1, 2.1, LinePattern::Dashed),
            makeRightAngle(A, B, C, kAccent2, 32.0),
            makeRightAngle(B, H, C, kAccent2, 28.0)
        };
        t.texts = {
            makeLabel(QPointF(-62, -6), QStringLiteral("a"), kInkMain, 14.0),
            makeLabel(QPointF(30, -8), QStringLiteral("b"), kInkMain, 14.0),
            makeLabel(QPointF(H.x() + 12, 8), QStringLiteral("h"), kAccent1, 14.0),
            makeLabel(QPointF((A.x() + H.x()) * 0.5, 52), QStringLiteral("m"), kAccent3, 14.0),
            makeLabel(QPointF((H.x() + C.x()) * 0.5, 52), QStringLiteral("n"), kAccent3, 14.0)
        };
    }

    // 26. Teorema de las Cuerdas
    {
        auto& t = addTpl(QStringLiteral("met_cuerdas"), QStringLiteral("T. de las Cuerdas"),
                         QStringLiteral("Métricas"), QStringLiteral("a · b = c · d"),
                         {QStringLiteral("cuerdas"), QStringLiteral("metricas"), QStringLiteral("circunferencia")});
        const QPointF O(0, 0);
        const double R = 56.0;
        const QPointF A = polarPt(O, R, -148.0);
        const QPointF B = polarPt(O, R, 22.0);
        const QPointF C = polarPt(O, R, 132.0);
        const QPointF D = polarPt(O, R, -38.0);
        const QPointF d1 = B - A, d2 = D - C;
        const double det = d1.x() * d2.y() - d1.y() * d2.x();
        const QPointF diff = C - A;
        const double s = (diff.x() * d2.y() - diff.y() * d2.x()) / det;
        const QPointF P = A + d1 * s;
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(A, B, kAccent1, 2.1),
            makeLine(C, D, kAccent2, 2.1),
            makeMark(P - QPointF(2, 0), P + QPointF(2, 0), QStringLiteral("mark:dot"), kInkMain, 0.5, 2.0)
        };
        t.texts = {
            makeLabel(lerpPt(A, P, 0.5) + QPointF(4, -12), QStringLiteral("a"), kAccent1, 14.0),
            makeLabel(lerpPt(P, B, 0.5) + QPointF(4, 13), QStringLiteral("b"), kAccent1, 14.0),
            makeLabel(lerpPt(C, P, 0.5) + QPointF(-12, -6), QStringLiteral("c"), kAccent2, 14.0),
            makeLabel(lerpPt(P, D, 0.5) + QPointF(-11, -9), QStringLiteral("d"), kAccent2, 14.0)
        };
    }

    // 27. Teorema de la Tangente
    {
        auto& t = addTpl(QStringLiteral("met_tangente"), QStringLiteral("T. de la Tangente"),
                         QStringLiteral("Métricas"), QStringLiteral("x² = a · b"),
                         {QStringLiteral("tangente"), QStringLiteral("secante"), QStringLiteral("metricas"), QStringLiteral("potencia")});
        const QPointF O(-34, 4);
        const double R = 44.0;
        const QPointF P(84, 4);
        const double d = P.x() - O.x();
        const double cosPhi = R / d;
        const double sinPhi = std::sqrt(std::max(0.0, 1.0 - cosPhi * cosPhi));
        const QPointF T(O.x() + R * cosPhi, O.y() - R * sinPhi);
        // Secante inferior con inclinación +15° desde P hacia la izquierda
        const double rad = 15.0 * (std::numbers::pi / 180.0);
        const QPointF dir(-std::cos(rad), std::sin(rad));
        const QPointF oc = P - O;
        const double bq = 2.0 * (oc.x() * dir.x() + oc.y() * dir.y());
        const double cq = oc.x() * oc.x() + oc.y() * oc.y() - R * R;
        const double disc = std::sqrt(std::max(0.0, bq * bq - 4.0 * cq));
        const QPointF B = P + dir * ((-bq - disc) * 0.5);
        const QPointF A = P + dir * ((-bq + disc) * 0.5);
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(P, T, kAccent1, 2.3),
            makeLine(P, A, kAccent2, 2.1),
            makeMark(T - QPointF(2, 0), T + QPointF(2, 0), QStringLiteral("mark:dot"), kAccent1, 0.5, 2.0),
            makeMark(B - QPointF(2, 0), B + QPointF(2, 0), QStringLiteral("mark:dot"), kAccent2, 0.5, 2.0)
        };
        t.texts = {
            makeLabel(lerpPt(P, T, 0.5) + QPointF(6, -13), QStringLiteral("x"), kAccent1, 15.0),
            makeLabel(lerpPt(P, B, 0.5) + QPointF(2, 13), QStringLiteral("b"), kAccent2, 14.0),
            makeLabel(lerpPt(P, A, 0.55) + QPointF(-6, 20), QStringLiteral("a (total)"), kAccent3, 12.0),
            makeLabel(QPointF(P.x() + 12, P.y()), QStringLiteral("P"), kInkMain, 13.0)
        };
    }

    // ==================== 6. ÁREAS (4) ====================

    // 28. Mediana del Trapecio
    {
        auto& t = addTpl(QStringLiteral("area_trapecio"), QStringLiteral("Mediana del Trapecio"),
                         QStringLiteral("Áreas"), QStringLiteral("m = (B + b) / 2"),
                         {QStringLiteral("trapecio"), QStringLiteral("mediana"), QStringLiteral("bases"), QStringLiteral("area")});
        const QPointF A(-86, 42), D(86, 42), B(-48, -40), C(48, -40);
        const QPointF M = lerpPt(A, B, 0.5);
        const QPointF N = lerpPt(C, D, 0.5);
        t.shapes = {
            makePolyline({A, B, C, D}, true, kInkMain, 2.2),
            makeLine(M, N, kAccent1, 2.2),
            makeMark(A, M, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(M, B, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(C, N, QStringLiteral("mark:tick2"), kAccent2),
            makeMark(N, D, QStringLiteral("mark:tick2"), kAccent2),
            makeMark(B, C, QStringLiteral("mark:parallel1"), kAccent3, 0.28),
            makeMark(A, D, QStringLiteral("mark:parallel1"), kAccent3, 0.25)
        };
        t.texts = {
            makeLabel(QPointF(0, -53), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF(0, 56), QStringLiteral("B"), kInkMain, 15.0),
            makeLabel(QPointF(0, -10), QStringLiteral("m"), kAccent1, 15.0)
        };
    }

    // 29. Área del Triángulo
    {
        auto& t = addTpl(QStringLiteral("area_triangulo"), QStringLiteral("Área del Triángulo"),
                         QStringLiteral("Áreas"), QStringLiteral("A = (b · h) / 2"),
                         {QStringLiteral("area"), QStringLiteral("triangulo"), QStringLiteral("base"), QStringLiteral("altura")});
        const QPointF A(-86, 44), B(-16, -48), C(86, 44);
        const QPointF H(B.x(), 44.0);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, H, kAccent1, 2.1, LinePattern::Dashed),
            makeRightAngle(B, H, C, kAccent2, 30.0)
        };
        t.texts = {
            makeLabel(QPointF(B.x() + 13, 0), QStringLiteral("h"), kAccent1, 15.0),
            makeLabel(QPointF(6, 58), QStringLiteral("b"), kInkMain, 15.0)
        };
    }

    // 30. Área del Equilátero
    {
        auto& t = addTpl(QStringLiteral("area_equilatero"), QStringLiteral("Área del Equilátero"),
                         QStringLiteral("Áreas"), QStringLiteral("A = L²√3 / 4"),
                         {QStringLiteral("area"), QStringLiteral("equilatero"), QStringLiteral("triangulo")});
        const double L = 108.0;
        const double H = L * std::sqrt(3.0) * 0.5;
        const QPointF A(-L * 0.5, H * 0.5 - 2.0);
        const QPointF C(L * 0.5, H * 0.5 - 2.0);
        const QPointF B(0.0, -H * 0.5 - 2.0);
        t.shapes = {
            makeTriangle(A, B, C),
            makeAngle(A, B, C, kAccent1, 18.0),
            makeMark(A, B, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(B, C, QStringLiteral("mark:tick1"), kAccent2),
            makeMark(A, C, QStringLiteral("mark:tick1"), kAccent2)
        };
        t.texts = {
            makeLabel(QPointF(-42, -8), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(42, -8), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(0, A.y() + 15), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(-24, A.y() - 12), QStringLiteral("60°"), kAccent1, 12.0)
        };
    }

    // 31. Relación de Áreas (Ceviana)
    {
        auto& t = addTpl(QStringLiteral("area_ceviana"), QStringLiteral("Relación de Áreas"),
                         QStringLiteral("Áreas"), QStringLiteral("A₁ / A₂ = m / n"),
                         {QStringLiteral("area"), QStringLiteral("areas"), QStringLiteral("ceviana"), QStringLiteral("relacion")});
        const QPointF A(-86, 44), B(-10, -48), C(86, 44);
        const QPointF D(-22, 44);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, D, kAccent1, 2.2)
        };
        t.texts = {
            makeLabel(QPointF(-42, 16), QStringLiteral("A₁"), kAccent2, 15.0),
            makeLabel(QPointF(20, 16), QStringLiteral("A₂"), kAccent3, 15.0),
            makeLabel(QPointF((A.x() + D.x()) * 0.5, 58), QStringLiteral("m"), kInkMain, 14.0),
            makeLabel(QPointF((D.x() + C.x()) * 0.5, 58), QStringLiteral("n"), kInkMain, 14.0)
        };
    }

    return list;
}

// ---------------------------------------------------------------------------
// Persistencia JSON de stickers personalizados (stickers.json)
// ---------------------------------------------------------------------------
QString customStickersPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/stickers.json");
}

QJsonObject serializeStroke(const Stroke& s) {
    QJsonObject o;
    o[QStringLiteral("tool")] = static_cast<int>(s.tool);
    o[QStringLiteral("color")] = s.color.name(QColor::HexArgb);
    o[QStringLiteral("size")] = s.size;
    o[QStringLiteral("opacity")] = s.opacity;
    o[QStringLiteral("linePattern")] = static_cast<int>(s.linePattern);
    o[QStringLiteral("pressureProfile")] = static_cast<int>(s.pressureProfile);
    o[QStringLiteral("hasHardwarePressure")] = s.hasHardwarePressure;
    o[QStringLiteral("tipStyle")] = s.tipStyle;
    QJsonArray pts;
    for (const auto& p : s.points) {
        QJsonArray pa;
        pa.append(p.x);
        pa.append(p.y);
        pa.append(p.pressure);
        pts.append(pa);
    }
    o[QStringLiteral("points")] = pts;
    return o;
}

Stroke deserializeStroke(const QJsonObject& o) {
    Stroke s;
    s.tool = static_cast<ToolType>(o[QStringLiteral("tool")].toInt(0));
    s.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    s.size = o[QStringLiteral("size")].toDouble(2.6);
    s.opacity = o[QStringLiteral("opacity")].toDouble(1.0);
    s.linePattern = static_cast<LinePattern>(o[QStringLiteral("linePattern")].toInt(0));
    s.pressureProfile = static_cast<PressureProfile>(o[QStringLiteral("pressureProfile")].toInt(0));
    s.hasHardwarePressure = o[QStringLiteral("hasHardwarePressure")].toBool(false);
    s.tipStyle = o[QStringLiteral("tipStyle")].toInt(0);
    const QJsonArray pts = o[QStringLiteral("points")].toArray();
    s.points.reserve(pts.size());
    for (const auto& v : pts) {
        const QJsonArray pa = v.toArray();
        if (pa.size() >= 2) {
            StrokePoint sp;
            sp.x = pa[0].toDouble();
            sp.y = pa[1].toDouble();
            sp.pressure = pa.size() >= 3 ? pa[2].toDouble(0.5) : 0.5;
            s.points.append(sp);
        }
    }
    s.bounds = computeBounds(s.points, s.size);
    return s;
}

QJsonObject serializeShape(const ShapeItem& sh) {
    QJsonObject o;
    o[QStringLiteral("kind")] = static_cast<int>(sh.kind);
    o[QStringLiteral("color")] = sh.color.name(QColor::HexArgb);
    o[QStringLiteral("size")] = sh.size;
    o[QStringLiteral("opacity")] = sh.opacity;
    o[QStringLiteral("linePattern")] = static_cast<int>(sh.linePattern);
    o[QStringLiteral("lineEnding")] = static_cast<int>(sh.lineEnding);
    o[QStringLiteral("label")] = sh.label;
    o[QStringLiteral("closed")] = sh.closed;
    o[QStringLiteral("shadePattern")] = static_cast<int>(sh.shadePattern);
    o[QStringLiteral("solidKind")] = sh.solidKind;
    QJsonArray verts;
    for (const auto& p : sh.vertices) {
        QJsonArray pa;
        pa.append(p.x());
        pa.append(p.y());
        verts.append(pa);
    }
    o[QStringLiteral("vertices")] = verts;
    if (!sh.shadeContourStarts.isEmpty()) {
        QJsonArray cs;
        for (int idx : sh.shadeContourStarts)
            cs.append(idx);
        o[QStringLiteral("shadeContourStarts")] = cs;
    }
    return o;
}

ShapeItem deserializeShape(const QJsonObject& o) {
    ShapeItem sh;
    sh.kind = static_cast<ShapeKind>(o[QStringLiteral("kind")].toInt(0));
    sh.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    sh.size = o[QStringLiteral("size")].toDouble(2.6);
    sh.opacity = o[QStringLiteral("opacity")].toDouble(1.0);
    sh.linePattern = static_cast<LinePattern>(o[QStringLiteral("linePattern")].toInt(0));
    sh.lineEnding = static_cast<LineEnding>(o[QStringLiteral("lineEnding")].toInt(0));
    sh.label = o[QStringLiteral("label")].toString();
    sh.closed = o[QStringLiteral("closed")].toBool(false);
    sh.shadePattern = static_cast<ShadePattern>(o[QStringLiteral("shadePattern")].toInt(0));
    sh.solidKind = o[QStringLiteral("solidKind")].toInt(0);
    const QJsonArray verts = o[QStringLiteral("vertices")].toArray();
    sh.vertices.reserve(verts.size());
    for (const auto& v : verts) {
        const QJsonArray pa = v.toArray();
        if (pa.size() >= 2)
            sh.vertices.append(QPointF(pa[0].toDouble(), pa[1].toDouble()));
    }
    const QJsonArray cs = o[QStringLiteral("shadeContourStarts")].toArray();
    for (const auto& v : cs)
        sh.shadeContourStarts.append(v.toInt());
    sh.bounds = computeShapeBounds(sh);
    return sh;
}

QJsonObject serializeText(const TextItem& t) {
    QJsonObject o;
    o[QStringLiteral("x")] = t.pos.x();
    o[QStringLiteral("y")] = t.pos.y();
    o[QStringLiteral("text")] = t.text;
    o[QStringLiteral("color")] = t.color.name(QColor::HexArgb);
    o[QStringLiteral("fontSize")] = t.fontSize;
    return o;
}

TextItem deserializeText(const QJsonObject& o) {
    TextItem t;
    t.pos = QPointF(o[QStringLiteral("x")].toDouble(), o[QStringLiteral("y")].toDouble());
    t.text = o[QStringLiteral("text")].toString();
    t.color = QColor(o[QStringLiteral("color")].toString(QStringLiteral("#f8fafc")));
    t.fontSize = o[QStringLiteral("fontSize")].toDouble(16.0);
    t.bounds = computeTextBounds(t);
    return t;
}

QVector<StickerTemplate> loadCustomTemplates() {
    QVector<StickerTemplate> out;
    QFile f(customStickersPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return out;
    const QJsonArray arr = doc.object()[QStringLiteral("stickers")].toArray();
    out.reserve(arr.size());
    for (const auto& val : arr) {
        const QJsonObject obj = val.toObject();
        StickerTemplate tpl;
        tpl.id = obj[QStringLiteral("id")].toString();
        if (tpl.id.isEmpty())
            continue;
        tpl.title = obj[QStringLiteral("title")].toString(QStringLiteral("Sticker"));
        tpl.category = QStringLiteral("Mis ★");
        tpl.formula = obj[QStringLiteral("formula")].toString();
        tpl.isCustom = true;
        tpl.baseSize = QSizeF(obj[QStringLiteral("baseW")].toDouble(220.0),
                              obj[QStringLiteral("baseH")].toDouble(140.0));
        for (const auto& kw : obj[QStringLiteral("keywords")].toArray())
            tpl.keywords.append(kw.toString());
        for (const auto& sv : obj[QStringLiteral("strokes")].toArray())
            tpl.strokes.append(deserializeStroke(sv.toObject()));
        for (const auto& shv : obj[QStringLiteral("shapes")].toArray())
            tpl.shapes.append(deserializeShape(shv.toObject()));
        for (const auto& tv : obj[QStringLiteral("texts")].toArray())
            tpl.texts.append(deserializeText(tv.toObject()));
        out.append(tpl);
    }
    return out;
}

bool writeCustomTemplates(const QVector<StickerTemplate>& customs) {
    const QString path = customStickersPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray arr;
    for (const auto& tpl : customs) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = tpl.id;
        obj[QStringLiteral("title")] = tpl.title;
        obj[QStringLiteral("category")] = QStringLiteral("Mis ★");
        obj[QStringLiteral("formula")] = tpl.formula;
        obj[QStringLiteral("baseW")] = tpl.baseSize.width();
        obj[QStringLiteral("baseH")] = tpl.baseSize.height();
        QJsonArray kws;
        for (const auto& kw : tpl.keywords)
            kws.append(kw);
        obj[QStringLiteral("keywords")] = kws;
        QJsonArray strokes;
        for (const auto& s : tpl.strokes)
            strokes.append(serializeStroke(s));
        obj[QStringLiteral("strokes")] = strokes;
        QJsonArray shapes;
        for (const auto& sh : tpl.shapes)
            shapes.append(serializeShape(sh));
        obj[QStringLiteral("shapes")] = shapes;
        QJsonArray texts;
        for (const auto& t : tpl.texts)
            texts.append(serializeText(t));
        obj[QStringLiteral("texts")] = texts;
        arr.append(obj);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("stickers")] = arr;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

QRectF unionContentBounds(const QVector<ShapeItem>& shapes,
                          const QVector<Stroke>& strokes,
                          const QVector<TextItem>& texts) {
    QRectF total;
    bool hasAny = false;
    auto unite = [&](const QRectF& r) {
        if (r.isNull() || !std::isfinite(r.x()) || !std::isfinite(r.y()))
            return;
        total = hasAny ? total.united(r) : r;
        hasAny = true;
    };
    for (const auto& sh : shapes)
        unite(sh.bounds.isValid() ? sh.bounds : computeShapeBounds(sh));
    for (const auto& st : strokes)
        unite(st.bounds.isValid() ? st.bounds : computeBounds(st.points, st.size));
    for (const auto& tx : texts)
        unite(tx.bounds.isValid() ? tx.bounds : computeTextBounds(tx));
    return hasAny ? total : QRectF(-100.0, -60.0, 200.0, 120.0);
}

} // namespace

QStringList stickerCategories() {
    return {
        QStringLiteral("Todos"),
        QStringLiteral("Triángulos"),
        QStringLiteral("Notables"),
        QStringLiteral("Congruencia"),
        QStringLiteral("Circunferencia"),
        QStringLiteral("Métricas"),
        QStringLiteral("Áreas"),
        QStringLiteral("Mis ★")
    };
}

QVector<StickerTemplate> allStickerTemplates() {
    static const QVector<StickerTemplate> builtins = buildBuiltinTemplates();
    QVector<StickerTemplate> result = builtins;
    const QVector<StickerTemplate> customs = loadCustomTemplates();
    result.reserve(result.size() + customs.size());
    for (const auto& c : customs)
        result.append(c);
    return result;
}

std::optional<StickerTemplate> findStickerTemplate(const QString& id) {
    if (id.isEmpty())
        return std::nullopt;
    const auto all = allStickerTemplates();
    for (const auto& tpl : all) {
        if (tpl.id == id)
            return tpl;
    }
    return std::nullopt;
}

InstantiatedSticker instantiateSticker(const StickerTemplate& tpl,
                                       const QPointF& centerWorld,
                                       const ThemeConfig& theme,
                                       StickerStyle style,
                                       double scale) {
    InstantiatedSticker out;
    out.groupId = freshId();
    out.title = tpl.title;

    const double sc = std::clamp(scale, 0.2, 5.0);

    // 1. Copiar, escalar y adaptar colores del diagrama base
    out.shapes.reserve(tpl.shapes.size() + 3);
    for (ShapeItem sh : tpl.shapes) {
        sh.color = resolveSemanticColor(sh.color, theme, tpl.isCustom);
        sh.size = std::max(0.8, sh.size * sc);
        for (QPointF& v : sh.vertices)
            v *= sc;
        sh.bounds = computeShapeBounds(sh);
        out.shapes.append(sh);
    }

    out.strokes.reserve(tpl.strokes.size());
    for (Stroke st : tpl.strokes) {
        st.color = resolveSemanticColor(st.color, theme, tpl.isCustom);
        st.size = std::max(0.8, st.size * sc);
        for (StrokePoint& pt : st.points) {
            pt.x *= sc;
            pt.y *= sc;
        }
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
        out.strokes.append(st);
    }

    out.texts.reserve(tpl.texts.size() + 2);
    for (TextItem tx : tpl.texts) {
        tx.color = resolveSemanticColor(tx.color, theme, tpl.isCustom);
        const QFontMetricsF oldM(stickerTextFont(tx.fontSize));
        const QPointF oldCenter(tx.pos.x() + oldM.horizontalAdvance(tx.text) * 0.5,
                                tx.pos.y() + oldM.height() * 0.5);
        tx.fontSize = std::max(8.0, tx.fontSize * sc);
        const QFontMetricsF newM(stickerTextFont(tx.fontSize));
        const QPointF newCenter = oldCenter * sc;
        tx.pos = QPointF(newCenter.x() - newM.horizontalAdvance(tx.text) * 0.5,
                         newCenter.y() - newM.height() * 0.5);
        tx.bounds = computeTextBounds(tx);
        out.texts.append(tx);
    }

    // Orden de capas limpio para plantillas integradas:
    // 0: rellenos -> 1: arcos de ángulo y ángulos rectos -> 2: figuras principales -> 3: marcas de congruencia
    if (!tpl.isCustom) {
        auto layerOf = [](const ShapeItem& s) -> int {
            if (s.kind == ShapeKind::ShadedRegion) return 0;
            if (s.kind == ShapeKind::AngleMarker || s.kind == ShapeKind::RightAngle) return 1;
            if (s.label.startsWith(QLatin1String("mark:"))) return 3;
            return 2;
        };
        std::stable_sort(out.shapes.begin(), out.shapes.end(),
                         [&](const ShapeItem& a, const ShapeItem& b) { return layerOf(a) < layerOf(b); });
    }

    const QRectF diagramBounds = unionContentBounds(out.shapes, out.strokes, out.texts);
    const QColor mainInk = resolveSemanticColor(kInkMain, theme, false);
    const QColor accent1 = resolveSemanticColor(kAccent1, theme, false);

    // 2. Decoración según estilo
    if (style == StickerStyle::CleanWithTitle) {
        if (!tpl.title.isEmpty()) {
            const double titleSize = 13.5 * sc;
            const QFontMetricsF m(stickerTextFont(titleSize));
            TextItem titleItem;
            titleItem.text = tpl.title;
            titleItem.color = accent1;
            titleItem.fontSize = titleSize;
            titleItem.pos = QPointF(diagramBounds.center().x() - m.horizontalAdvance(tpl.title) * 0.5,
                                    diagramBounds.top() - 9.0 * sc - m.height());
            titleItem.bounds = computeTextBounds(titleItem);
            out.texts.append(titleItem);
        }
        if (!tpl.formula.isEmpty()) {
            const double formSize = 13.5 * sc;
            const QFontMetricsF m(stickerTextFont(formSize));
            TextItem formItem;
            formItem.text = tpl.formula;
            formItem.color = mainInk;
            formItem.fontSize = formSize;
            formItem.pos = QPointF(diagramBounds.center().x() - m.horizontalAdvance(tpl.formula) * 0.5,
                                   diagramBounds.bottom() + 9.0 * sc);
            formItem.bounds = computeTextBounds(formItem);
            out.texts.append(formItem);
        }
    } else if (style == StickerStyle::Card) {
        double topY = diagramBounds.top();
        double bottomY = diagramBounds.bottom();
        double minX = diagramBounds.left();
        double maxX = diagramBounds.right();

        if (!tpl.title.isEmpty()) {
            const double titleSize = 13.0 * sc;
            const QFontMetricsF m(stickerTextFont(titleSize));
            const double w = m.horizontalAdvance(tpl.title);
            TextItem titleItem;
            titleItem.text = tpl.title;
            titleItem.color = mainInk;
            titleItem.fontSize = titleSize;
            titleItem.pos = QPointF(diagramBounds.center().x() - w * 0.5,
                                    topY - 9.0 * sc - m.height());
            titleItem.bounds = computeTextBounds(titleItem);
            out.texts.append(titleItem);
            topY = titleItem.bounds.top();
            minX = std::min(minX, titleItem.bounds.left());
            maxX = std::max(maxX, titleItem.bounds.right());
        }

        const bool hasFormula = !tpl.formula.isEmpty();
        double sepY = bottomY + 9.0 * sc;
        if (hasFormula) {
            const double formSize = 13.5 * sc;
            const QFontMetricsF m(stickerTextFont(formSize));
            const double w = m.horizontalAdvance(tpl.formula);
            TextItem formItem;
            formItem.text = tpl.formula;
            formItem.color = accent1;
            formItem.fontSize = formSize;
            formItem.pos = QPointF(diagramBounds.center().x() - w * 0.5,
                                   sepY + 9.0 * sc);
            formItem.bounds = computeTextBounds(formItem);
            out.texts.append(formItem);
            bottomY = formItem.bounds.bottom();
            minX = std::min(minX, formItem.bounds.left());
            maxX = std::max(maxX, formItem.bounds.right());
        }

        const double padX = 18.0 * sc;
        const double padY = 14.0 * sc;
        const QRectF cardRect(QPointF(minX - padX, topY - padY),
                              QPointF(maxX + padX, bottomY + padY));

        const QColor fillColor = theme.isDark ? QColor(QStringLiteral("#94a3b8"))
                                              : QColor(QStringLiteral("#64748b"));
        const QColor borderColor = theme.isDark ? QColor(148, 163, 184, 130)
                                                : QColor(100, 116, 139, 130);

        ShapeItem bg = makeRectShadedRegion(cardRect, fillColor, 0.72);
        bg.bounds = computeShapeBounds(bg);

        ShapeItem border;
        border.kind = ShapeKind::Rectangle;
        border.color = borderColor;
        border.size = std::max(1.0, 1.4 * sc);
        border.closed = true;
        border.vertices = {cardRect.topLeft(), cardRect.bottomRight()};
        border.bounds = computeShapeBounds(border);

        // Insertar fondo y borde al inicio para que se pinten detrás del diagrama
        out.shapes.prepend(border);
        out.shapes.prepend(bg);

        if (hasFormula) {
            ShapeItem sep = makeLine(QPointF(cardRect.left() + 12.0 * sc, sepY),
                                     QPointF(cardRect.right() - 12.0 * sc, sepY),
                                     borderColor, std::max(0.9, 1.1 * sc));
            sep.bounds = computeShapeBounds(sep);
            out.shapes.insert(2, sep);
        }
    }

    // 3. Centrar exactamente en centerWorld y asignar UUIDs + groupId compartido
    const QRectF totalBounds = unionContentBounds(out.shapes, out.strokes, out.texts);
    const QPointF delta = centerWorld - totalBounds.center();

    for (ShapeItem& sh : out.shapes) {
        translateShape(sh, delta);
        sh.bounds = computeShapeBounds(sh);
        sh.id = freshId();
        sh.groupId = out.groupId;
        sh.locked = false;
    }

    for (Stroke& st : out.strokes) {
        for (StrokePoint& pt : st.points) {
            pt.x += delta.x();
            pt.y += delta.y();
        }
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
        st.id = freshId();
        st.groupId = out.groupId;
    }

    for (TextItem& tx : out.texts) {
        tx.pos += delta;
        tx.bounds = computeTextBounds(tx);
        tx.id = freshId();
        tx.groupId = out.groupId;
        tx.locked = false;
    }

    return out;
}

QPixmap renderStickerThumbnail(const StickerTemplate& tpl,
                               const QSize& pixelSize,
                               const ThemeConfig& theme,
                               StickerStyle style) {
    constexpr double dpr = 2.0;
    const QSize safeSize(std::max(16, pixelSize.width()), std::max(16, pixelSize.height()));
    QPixmap pm(qRound(safeSize.width() * dpr), qRound(safeSize.height() * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const InstantiatedSticker inst = instantiateSticker(tpl, QPointF(0.0, 0.0), theme, style, 1.0);
    const QRectF bounds = unionContentBounds(inst.shapes, inst.strokes, inst.texts);

    const double margin = 8.0;
    const double availW = std::max(4.0, safeSize.width() - 2.0 * margin);
    const double availH = std::max(4.0, safeSize.height() - 2.0 * margin);
    const double bw = std::max(1.0, bounds.width());
    const double bh = std::max(1.0, bounds.height());
    const double fitScale = std::min(availW / bw, availH / bh);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    painter.translate(safeSize.width() * 0.5, safeSize.height() * 0.5);
    painter.scale(fitScale, fitScale);
    painter.translate(-bounds.center());

    for (const ShapeItem& sh : inst.shapes)
        drawShape(painter, sh);
    for (const Stroke& st : inst.strokes)
        drawStroke(painter, st);
    for (const TextItem& tx : inst.texts)
        drawStickerText(painter, tx);

    painter.end();
    return pm;
}

bool saveCustomSticker(const QString& title,
                       const QString& category,
                       const QVector<Stroke>& strokes,
                       const QVector<ShapeItem>& shapes,
                       const QVector<TextItem>& texts,
                       QString* outId) {
    if (strokes.isEmpty() && shapes.isEmpty() && texts.isEmpty())
        return false;

    StickerTemplate tpl;
    tpl.id = QStringLiteral("custom_") + freshId();
    tpl.title = title.trimmed().isEmpty() ? QStringLiteral("Mi sticker") : title.trimmed();
    tpl.category = category.trimmed().isEmpty() ? QStringLiteral("Mis ★") : QStringLiteral("Mis ★");
    tpl.isCustom = true;
    tpl.keywords = {tpl.title.toLower(), QStringLiteral("custom"), QStringLiteral("personalizado")};

    QVector<ShapeItem> normShapes = shapes;
    QVector<Stroke> normStrokes = strokes;
    QVector<TextItem> normTexts = texts;

    for (auto& sh : normShapes)
        sh.bounds = computeShapeBounds(sh);
    for (auto& st : normStrokes)
        st.bounds = computeBounds(st.points, st.size);
    for (auto& tx : normTexts)
        tx.bounds = computeTextBounds(tx);

    const QRectF bounds = unionContentBounds(normShapes, normStrokes, normTexts);
    const QPointF offset = -bounds.center();

    for (auto& sh : normShapes) {
        translateShape(sh, offset);
        sh.id.clear();
        sh.groupId.clear();
        sh.attachments.clear();
        sh.attachmentsKnown = false;
        sh.locked = false;
        sh.bounds = computeShapeBounds(sh);
    }
    for (auto& st : normStrokes) {
        for (auto& pt : st.points) {
            pt.x += offset.x();
            pt.y += offset.y();
        }
        st.id.clear();
        st.groupId.clear();
        st.cachedPath = QPainterPath();
        st.cachedShapeHash = 0;
        st.bounds = computeBounds(st.points, st.size);
    }
    for (auto& tx : normTexts) {
        tx.pos += offset;
        tx.id.clear();
        tx.groupId.clear();
        tx.locked = false;
        tx.bounds = computeTextBounds(tx);
    }

    tpl.shapes = std::move(normShapes);
    tpl.strokes = std::move(normStrokes);
    tpl.texts = std::move(normTexts);
    tpl.baseSize = QSizeF(std::max(40.0, bounds.width()), std::max(40.0, bounds.height()));

    QVector<StickerTemplate> customs = loadCustomTemplates();
    customs.append(tpl);
    if (!writeCustomTemplates(customs))
        return false;
    if (outId)
        *outId = tpl.id;
    return true;
}

bool deleteCustomSticker(const QString& id) {
    if (id.isEmpty())
        return false;
    QVector<StickerTemplate> customs = loadCustomTemplates();
    const qsizetype before = customs.size();
    customs.erase(std::remove_if(customs.begin(), customs.end(),
                                 [&](const StickerTemplate& t) { return t.id == id; }),
                  customs.end());
    if (customs.size() == before)
        return false;
    return writeCustomTemplates(customs);
}

} // namespace finily
