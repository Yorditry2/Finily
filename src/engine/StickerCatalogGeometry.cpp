#include "engine/StickerCatalogInternal.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace finily::sticker_detail {
namespace {

void appendTriangleStickers(QVector<StickerTemplate>& list) {
    // 1. Ángulo Exterior
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_exterior"), QStringLiteral("Ángulo Exterior"),
            QStringLiteral("Triángulos"), QStringLiteral("x = α + β"),
            {QStringLiteral("triangulo"), QStringLiteral("angulo"), QStringLiteral("exterior")});
        const QPointF A(-86, 42);
        const QPointF B(-14, -48);
        const QPointF C(34, 42);
        const QPointF D(96, 42);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(C, D, kInkMain, 2.2),
            makeAngle(A, B, C, kAccent1, 20.0),
            makeAngle(B, A, C, kAccent1, 20.0),
            makeAngle(C, B, D, kAccent2, 22.0),
        };
        t.texts = {
            makeLabel(QPointF(-54, 30), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(-10, -18), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(58, 25), QStringLiteral("x"), kAccent2, 15.0),
        };
    }

    // 2. Propiedad del Boomerang
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_boomerang"), QStringLiteral("Prop. Boomerang"),
            QStringLiteral("Triángulos"), QStringLiteral("x = α + β + θ"),
            {QStringLiteral("boomerang"), QStringLiteral("pantaloncito"), QStringLiteral("concavo"),
             QStringLiteral("cuadrilatero")});
        const QPointF A(-88, 46);
        const QPointF B(0, -52);
        const QPointF C(88, 46);
        const QPointF D(0, 8);
        t.shapes = {
            makePolyline({A, B, C, D}, true, kInkMain, 2.2),
            makeAngle(A, B, D, kAccent1, 22.0),
            makeAngle(B, A, C, kAccent1, 20.0),
            makeAngle(C, B, D, kAccent1, 22.0),
            makeAngle(D, A, C, kAccent2, 18.0),
        };
        t.texts = {
            makeLabel(QPointF(-56, 30), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(0, -21), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(56, 30), QStringLiteral("θ"), kAccent1, 14.0),
            makeLabel(QPointF(0, 32), QStringLiteral("x"), kAccent2, 15.0),
        };
    }

    // 3. Propiedad de la Corbatita
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_corbatita"), QStringLiteral("Prop. Corbatita"),
            QStringLiteral("Triángulos"), QStringLiteral("α + β = θ + ω"),
            {QStringLiteral("corbatita"), QStringLiteral("mariposa"), QStringLiteral("opuestos")});
        const QPointF A(-86, -42);
        const QPointF B(-86, 42);
        const QPointF C(86, -42);
        const QPointF D(86, 42);
        const QPointF O(0, 0);
        t.shapes = {
            makeTriangle(A, B, O),
            makeTriangle(C, D, O),
            makeAngle(A, B, O, kAccent1, 19.0),
            makeAngle(B, A, O, kAccent1, 19.0),
            makeAngle(C, D, O, kAccent2, 19.0),
            makeAngle(D, C, O, kAccent2, 19.0),
        };
        t.texts = {
            makeLabel(QPointF(-67, -20), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(-67, 20), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(67, -20), QStringLiteral("θ"), kAccent2, 14.0),
            makeLabel(QPointF(67, 20), QStringLiteral("ω"), kAccent2, 14.0),
        };
    }

    // 4. Propiedad del Pescadito
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_pescadito"), QStringLiteral("Prop. Pescadito"),
            QStringLiteral("Triángulos"), QStringLiteral("α + β = x + y"),
            {QStringLiteral("pescadito"), QStringLiteral("cuadrilatero"), QStringLiteral("exteriores")});
        const QPointF A(-88, 0);
        const QPointF B(-8, -46);
        const QPointF C(76, 0);
        const QPointF D(-8, 46);
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
            makeAngle(D, Dext, C, kAccent2, 18.0),
        };
        t.texts = {
            makeLabel(QPointF(-58, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(46, 0), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(24, -44), QStringLiteral("x"), kAccent2, 14.0),
            makeLabel(QPointF(24, 44), QStringLiteral("y"), kAccent2, 14.0),
        };
    }

    // 5. Base Media
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_base_media"), QStringLiteral("Base Media"),
            QStringLiteral("Triángulos"), QStringLiteral("MN = AC / 2"),
            {QStringLiteral("base"), QStringLiteral("media"), QStringLiteral("punto medio"),
             QStringLiteral("paralela")});
        const QPointF A(-84, 46);
        const QPointF B(-6, -50);
        const QPointF C(84, 46);
        const QPointF M = lerpPt(A, B, 0.5);
        const QPointF N = lerpPt(B, C, 0.5);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(M, N, kAccent1, 2.2),
            makeMark(A, M, EdgeMarkKind::Tick1, kAccent2),
            makeMark(M, B, EdgeMarkKind::Tick1, kAccent2),
            makeMark(B, N, EdgeMarkKind::Tick2, kAccent2),
            makeMark(N, C, EdgeMarkKind::Tick2, kAccent2),
            makeMark(M, N, EdgeMarkKind::Parallel1, kAccent3),
            makeMark(A, C, EdgeMarkKind::Parallel1, kAccent3),
        };
        t.texts = {
            makeLabel(QPointF(-95, 48), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(-6, -61), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(95, 48), QStringLiteral("C"), kInkMain, 13.0),
            makeLabel(QPointF(M.x() - 15, M.y() - 4), QStringLiteral("M"), kAccent1, 13.0),
            makeLabel(QPointF(N.x() + 15, N.y() - 4), QStringLiteral("N"), kAccent1, 13.0),
        };
    }

    // 6. Mediana a la Hipotenusa
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_mediana_hip"), QStringLiteral("Mediana a Hipotenusa"),
            QStringLiteral("Triángulos"), QStringLiteral("BM = AC / 2"),
            {QStringLiteral("mediana"), QStringLiteral("hipotenusa"), QStringLiteral("rectangulo")});
        // Triángulo rectángulo inscrito en semicírculo de radio 84 centrado en M(0, 38)
        const QPointF A(-84, 38);
        const QPointF C(84, 38);
        const QPointF M(0, 38);
        // B en el semicírculo: ángulo 120° -> (-42, 38 - 72.7) = (-42, -34.7)
        const QPointF B(-42.0, 38.0 - 84.0 * std::sin(std::numbers::pi / 3.0));
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, M, kAccent1, 2.2),
            makeRightAngle(A, B, C, kAccent2, 34.0),
            makeMark(A, M, EdgeMarkKind::Tick1, kAccent2),
            makeMark(M, C, EdgeMarkKind::Tick1, kAccent2),
            makeMark(B, M, EdgeMarkKind::Tick1, kAccent2),
        };
        t.texts = {
            makeLabel(QPointF(-94, 40), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(-46, -46), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(94, 40), QStringLiteral("C"), kInkMain, 13.0),
            makeLabel(QPointF(0, 52), QStringLiteral("M"), kAccent1, 13.0),
        };
    }

    // 7. Existencia de Triángulo
    {
        auto& t = addTemplate(
            list, QStringLiteral("tri_existencia"), QStringLiteral("Existencia de Triángulo"),
            QStringLiteral("Triángulos"), QStringLiteral("b − c < a < b + c"),
            {QStringLiteral("existencia"), QStringLiteral("desigualdad"), QStringLiteral("triangular"),
             QStringLiteral("lados")});
        const QPointF A(-84, 44);
        const QPointF B(-18, -46);
        const QPointF C(84, 44);
        t.shapes = {
            makeTriangle(A, B, C),
        };
        t.texts = {
            makeLabel(QPointF(0, 58), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF(42, -8), QStringLiteral("b"), kAccent2, 15.0),
            makeLabel(QPointF(-62, -6), QStringLiteral("c"), kAccent3, 15.0),
        };
    }
}

void appendNotableStickers(QVector<StickerTemplate>& list) {
    // 8. Notable 30° – 60° (exacto 1 : √3 : 2)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_30_60"), QStringLiteral("Notable 30° – 60°"),
            QStringLiteral("Notables"), QStringLiteral("k, k√3, 2k"),
            {QStringLiteral("notable"), QStringLiteral("30"), QStringLiteral("60"),
             QStringLiteral("rectangulo")});
        const double k = 72.0;
        const double base = k * std::sqrt(3.0);
        const QPointF O(-62.0, 44.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + base, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 18.0),
            makeAngle(R, T, O, kAccent1, 24.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("k√3"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12),
                      QStringLiteral("2k"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 15, T.y() + 27), QStringLiteral("60°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 37, R.y() - 9), QStringLiteral("30°"), kAccent1, 11.5),
        };
    }

    // 9. Notable 45° – 45° (exacto 1 : 1 : √2)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_45_45"), QStringLiteral("Notable 45° – 45°"),
            QStringLiteral("Notables"), QStringLiteral("k, k, k√2"),
            {QStringLiteral("notable"), QStringLiteral("45"), QStringLiteral("isosceles"),
             QStringLiteral("rectangulo")});
        const double k = 102.0;
        const QPointF O(-52.0, 50.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 21.0),
            makeAngle(R, T, O, kAccent1, 21.0),
            makeMark(O, T, EdgeMarkKind::Tick1, kAccent2, 0.45),
            makeMark(O, R, EdgeMarkKind::Tick1, kAccent2, 0.45),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5 + 4), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5 - 4, O.y() + 17), QStringLiteral("k"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 18, (T.y() + R.y()) * 0.5 - 12),
                      QStringLiteral("k√2"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 14, T.y() + 31), QStringLiteral("45°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 32, R.y() - 11), QStringLiteral("45°"), kAccent1, 11.5),
        };
    }

    // 10. Notable 37° – 53° (exacto 3 : 4 : 5)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_37_53"), QStringLiteral("Notable 37° – 53°"),
            QStringLiteral("Notables"), QStringLiteral("3k, 4k, 5k"),
            {QStringLiteral("notable"), QStringLiteral("37"), QStringLiteral("53"), QStringLiteral("3 4 5")});
        const QPointF O(-58.0, 46.0);
        const QPointF T(-58.0, 46.0 - 84.0);  // 3 * 28
        const QPointF R(-58.0 + 112.0, 46.0); // 4 * 28
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 30.0),
            makeAngle(T, O, R, kAccent1, 18.0),
            makeAngle(R, T, O, kAccent1, 22.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5), QStringLiteral("3k"), kInkMain, 14.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("4k"), kInkMain, 14.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12),
                      QStringLiteral("5k"), kAccent3, 14.0),
            makeLabel(QPointF(T.x() + 15, T.y() + 28), QStringLiteral("53°"), kAccent1, 11.5),
            makeLabel(QPointF(R.x() - 34, R.y() - 10), QStringLiteral("37°"), kAccent1, 11.5),
        };
    }

    // 11. Notable 16° – 74° (exacto 7 : 24 : 25)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_16_74"), QStringLiteral("Notable 16° – 74°"),
            QStringLiteral("Notables"), QStringLiteral("7k, 24k, 25k"),
            {QStringLiteral("notable"), QStringLiteral("16"), QStringLiteral("74"),
             QStringLiteral("7 24 25")});
        const double u = 7.0;
        const QPointF O(-84.0, 36.0);
        const QPointF T(O.x(), O.y() - 7.0 * u);
        const QPointF R(O.x() + 24.0 * u, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 26.0),
            makeAngle(T, O, R, kAccent1, 15.0),
            makeAngle(R, T, O, kAccent1, 28.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 18, (O.y() + T.y()) * 0.5), QStringLiteral("7k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("24k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 12, (T.y() + R.y()) * 0.5 - 14),
                      QStringLiteral("25k"), kAccent3, 13.5),
            makeLabel(QPointF(T.x() + 18, T.y() + 21), QStringLiteral("74°"), kAccent1, 11.0),
            makeLabel(QPointF(R.x() - 44, R.y() - 8), QStringLiteral("16°"), kAccent1, 11.0),
        };
    }

    // 12. Notable 37°/2 (exacto 1 : 3 : √10)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_37_2"), QStringLiteral("Notable 37°/2"),
            QStringLiteral("Notables"), QStringLiteral("k, 3k, k√10"),
            {QStringLiteral("notable"), QStringLiteral("37/2"), QStringLiteral("18.5"),
             QStringLiteral("raiz 10")});
        const double k = 52.0;
        const QPointF O(-78.0, 36.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + 3.0 * k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 26.0),
            makeAngle(R, T, O, kAccent1, 28.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("3k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 14),
                      QStringLiteral("k√10"), kAccent3, 13.5),
            makeLabel(QPointF(R.x() - 48, R.y() - 9), QStringLiteral("37°/2"), kAccent1, 11.0),
        };
    }

    // 13. Notable 53°/2 (exacto 1 : 2 : √5)
    {
        auto& t = addTemplate(
            list, QStringLiteral("not_53_2"), QStringLiteral("Notable 53°/2"),
            QStringLiteral("Notables"), QStringLiteral("k, 2k, k√5"),
            {QStringLiteral("notable"), QStringLiteral("53/2"), QStringLiteral("26.5"),
             QStringLiteral("raiz 5")});
        const double k = 66.0;
        const QPointF O(-66.0, 40.0);
        const QPointF T(O.x(), O.y() - k);
        const QPointF R(O.x() + 2.0 * k, O.y());
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 28.0),
            makeAngle(R, T, O, kAccent1, 25.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 15, (O.y() + T.y()) * 0.5), QStringLiteral("k"), kInkMain, 13.5),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("2k"), kInkMain, 13.5),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 16, (T.y() + R.y()) * 0.5 - 13),
                      QStringLiteral("k√5"), kAccent3, 13.5),
            makeLabel(QPointF(R.x() - 44, R.y() - 10), QStringLiteral("53°/2"), kAccent1, 11.0),
        };
    }
}

void appendCongruenceStickers(QVector<StickerTemplate>& list) {
    // 14. Teorema de la Bisectriz
    {
        auto& t = addTemplate(
            list, QStringLiteral("cong_bisectriz"), QStringLiteral("T. de la Bisectriz"),
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
            makeMark(P, A, EdgeMarkKind::Tick1, kAccent2),
            makeMark(P, B, EdgeMarkKind::Tick1, kAccent2),
            makeMark(O, A, EdgeMarkKind::Tick2, kAccent3, 0.55),
            makeMark(O, B, EdgeMarkKind::Tick2, kAccent3, 0.55),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 13, 0), QStringLiteral("O"), kInkMain, 13.0),
            makeLabel(QPointF(P.x() + 14, 0), QStringLiteral("P"), kAccent1, 13.0),
            makeLabel(QPointF(A.x() - 4, A.y() - 14), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(B.x() - 4, B.y() + 15), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(O.x() + 40, -10), QStringLiteral("α"), kAccent1, 13.0),
            makeLabel(QPointF(O.x() + 40, 10), QStringLiteral("α"), kAccent1, 13.0),
        };
    }

    // 15. Teorema de la Mediatriz
    {
        auto& t = addTemplate(
            list, QStringLiteral("cong_mediatriz"), QStringLiteral("T. de la Mediatriz"),
            QStringLiteral("Congruencia"), QStringLiteral("PA = PB"),
            {QStringLiteral("mediatriz"), QStringLiteral("congruencia"), QStringLiteral("isosceles")});
        const QPointF A(-80, 44);
        const QPointF B(80, 44);
        const QPointF M(0, 44);
        const QPointF P(0, -34);
        t.shapes = {
            makeLine(A, B, kInkMain, 2.2),
            makeLine(QPointF(0, -56), QPointF(0, 58), kAccent1, 2.0, LinePattern::Dashed),
            makeLine(P, A, kInkMain, 2.2),
            makeLine(P, B, kInkMain, 2.2),
            makeRightAngle(P, M, B, kAccent2, 30.0),
            makeMark(A, M, EdgeMarkKind::Tick1, kAccent2),
            makeMark(M, B, EdgeMarkKind::Tick1, kAccent2),
            makeMark(P, A, EdgeMarkKind::Tick2, kAccent3),
            makeMark(P, B, EdgeMarkKind::Tick2, kAccent3),
        };
        t.texts = {
            makeLabel(QPointF(-91, 46), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(91, 46), QStringLiteral("B"), kInkMain, 13.0),
            makeLabel(QPointF(14, 54), QStringLiteral("M"), kAccent1, 13.0),
            makeLabel(QPointF(14, -38), QStringLiteral("P"), kAccent1, 13.0),
        };
    }

    // 16. Teorema de Tales
    {
        auto& t = addTemplate(
            list, QStringLiteral("cong_tales"), QStringLiteral("Teorema de Tales"),
            QStringLiteral("Congruencia"), QStringLiteral("a / b = c / d"),
            {QStringLiteral("tales"), QStringLiteral("proporcionalidad"), QStringLiteral("paralelas"),
             QStringLiteral("semejanza")});
        const double y1 = -40.0;
        const double y2 = -2.0;
        const double y3 = 44.0;
        const QPointF L1a(-94, y1);
        const QPointF L1b(94, y1);
        const QPointF L2a(-94, y2);
        const QPointF L2b(94, y2);
        const QPointF L3a(-94, y3);
        const QPointF L3b(94, y3);
        // Secante 1 (inclinada hacia la izquierda)
        const QPointF S1top(-24, -56);
        const QPointF S1bot(-62, 58);
        // Secante 2 (inclinada hacia la derecha)
        const QPointF S2top(24, -56);
        const QPointF S2bot(66, 58);
        t.shapes = {
            makeLine(L1a, L1b, kAccent3, 2.0),
            makeLine(L2a, L2b, kAccent3, 2.0),
            makeLine(L3a, L3b, kAccent3, 2.0),
            makeLine(S1top, S1bot, kInkMain, 2.2),
            makeLine(S2top, S2bot, kInkMain, 2.2),
            makeMark(QPointF(-90, y1), QPointF(-60, y1), EdgeMarkKind::Parallel1, kAccent3),
            makeMark(QPointF(-90, y2), QPointF(-60, y2), EdgeMarkKind::Parallel1, kAccent3),
            makeMark(QPointF(-90, y3), QPointF(-60, y3), EdgeMarkKind::Parallel1, kAccent3),
        };
        t.texts = {
            makeLabel(QPointF(-52, -21), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF(-64, 21), QStringLiteral("b"), kAccent1, 15.0),
            makeLabel(QPointF(54, -21), QStringLiteral("c"), kAccent2, 15.0),
            makeLabel(QPointF(68, 21), QStringLiteral("d"), kAccent2, 15.0),
        };
    }

    // 17. Teorema de la Bisectriz Interior
    {
        auto& t = addTemplate(
            list, QStringLiteral("cong_bisectriz_int"), QStringLiteral("Bisectriz Interior"),
            QStringLiteral("Congruencia"), QStringLiteral("a / b = m / n"),
            {QStringLiteral("bisectriz"), QStringLiteral("interior"), QStringLiteral("proporcionalidad")});
        const QPointF A(-84, 44);
        const QPointF B(-14, -50);
        const QPointF C(84, 44);
        const double lenAB = std::hypot(B.x() - A.x(), B.y() - A.y());
        const double lenBC = std::hypot(C.x() - B.x(), C.y() - B.y());
        // Punto D exacto sobre AC tal que AD / DC = AB / BC
        const QPointF D = lerpPt(A, C, lenAB / (lenAB + lenBC));
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, D, kAccent1, 2.2),
            makeAngle(B, A, D, kAccent1, 20.0),
            makeAngle(B, D, C, kAccent1, 20.0),
        };
        t.texts = {
            makeLabel(QPointF(-62, -8), QStringLiteral("a"), kInkMain, 15.0),
            makeLabel(QPointF(48, -8), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF((A.x() + D.x()) * 0.5, 58), QStringLiteral("m"), kAccent2, 15.0),
            makeLabel(QPointF((D.x() + C.x()) * 0.5, 58), QStringLiteral("n"), kAccent2, 15.0),
            makeLabel(QPointF(-24, -18), QStringLiteral("α"), kAccent1, 13.0),
            makeLabel(QPointF(-1, -18), QStringLiteral("α"), kAccent1, 13.0),
        };
    }
}

void appendCircleStickers(QVector<StickerTemplate>& list) {
    // 18. Ángulo Inscrito
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_inscrito"), QStringLiteral("Ángulo Inscrito"),
            QStringLiteral("Circunferencia"), QStringLiteral("α = θ / 2"),
            {QStringLiteral("circunferencia"), QStringLiteral("inscrito"), QStringLiteral("central"),
             QStringLiteral("arco")});
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
            makeMark(O - QPointF(2, 0), O + QPointF(2, 0), EdgeMarkKind::Dot, kInkMain, 0.5, 2.0),
        };
        t.texts = {
            makeLabel(QPointF(-11, -24), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(2, 25), QStringLiteral("θ"), kAccent2, 14.0),
            makeLabel(QPointF(11, -6), QStringLiteral("O"), kInkMain, 12.0),
        };
    }

    // 19. Ángulo Interior
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_interior"), QStringLiteral("Ángulo Interior"),
            QStringLiteral("Circunferencia"), QStringLiteral("x = (α + β) / 2"),
            {QStringLiteral("circunferencia"), QStringLiteral("interior"), QStringLiteral("cuerdas"),
             QStringLiteral("arcos")});
        const QPointF O(0, 0);
        const double R = 56.0;
        const QPointF A = polarPt(O, R, -145.0);
        const QPointF B = polarPt(O, R, 25.0);
        const QPointF C = polarPt(O, R, 140.0);
        const QPointF D = polarPt(O, R, -35.0);
        // Intersección exacta de AB y CD
        const QPointF d1 = B - A;
        const QPointF d2 = D - C;
        const double det = d1.x() * d2.y() - d1.y() * d2.x();
        const QPointF diff = C - A;
        const double s = (diff.x() * d2.y() - diff.y() * d2.x()) / det;
        const QPointF X = A + d1 * s;
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(A, B, kInkMain, 2.0),
            makeLine(C, D, kInkMain, 2.0),
            makeAngle(X, A, C, kAccent2, 17.0),
            makeAngle(X, D, B, kAccent2, 17.0),
        };
        t.texts = {
            makeLabel(QPointF(-68, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(68, -4), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(X.x() + 20, X.y() - 3), QStringLiteral("x"), kAccent2, 14.0),
        };
    }

    // 20. Ángulo Exterior
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_exterior"), QStringLiteral("Ángulo Exterior"),
            QStringLiteral("Circunferencia"), QStringLiteral("x = (α − β) / 2"),
            {QStringLiteral("circunferencia"), QStringLiteral("exterior"), QStringLiteral("secantes"),
             QStringLiteral("arcos")});
        const QPointF O(-34, 0);
        const double R = 48.0;
        const QPointF P(86, 0);
        // Dos secantes simétricas desde P en ángulos ±17° hacia O
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
        Q_UNUSED(B1);
        Q_UNUSED(B2);
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(P, A1, kInkMain, 2.0),
            makeLine(P, A2, kInkMain, 2.0),
            makeAngle(P, A1, A2, kAccent2, 24.0),
        };
        t.texts = {
            makeLabel(QPointF(-93, 0), QStringLiteral("α"), kAccent1, 14.0),
            makeLabel(QPointF(25, 0), QStringLiteral("β"), kAccent1, 14.0),
            makeLabel(QPointF(54, 0), QStringLiteral("x"), kAccent2, 14.0),
            makeLabel(QPointF(96, 0), QStringLiteral("P"), kInkMain, 13.0),
        };
    }

    // 21. Teorema de Poncelet (3:4:5 con circunferencia EXACTAMENTE inscrita)
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_poncelet"), QStringLiteral("Teorema de Poncelet"),
            QStringLiteral("Circunferencia"), QStringLiteral("a + b = c + 2r"),
            {QStringLiteral("poncelet"), QStringLiteral("inradio"), QStringLiteral("inscrita"),
             QStringLiteral("rectangulo")});
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
            makeMark(incenter - QPointF(2, 0), incenter + QPointF(2, 0), EdgeMarkKind::Dot, kAccent2,
                     0.5, 2.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 16, (O.y() + T.y()) * 0.5), QStringLiteral("a"), kInkMain, 15.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12),
                      QStringLiteral("c"), kAccent3, 15.0),
            makeLabel(QPointF(incenter.x() + 10, incenter.y() + 12), QStringLiteral("r"), kAccent2, 14.0),
        };
    }

    // 22. Teorema de Pitot (cuadrilátero exactamente circunscrito)
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_pitot"), QStringLiteral("Teorema de Pitot"),
            QStringLiteral("Circunferencia"), QStringLiteral("a + c = b + d"),
            {QStringLiteral("pitot"), QStringLiteral("cuadrilatero"), QStringLiteral("circunscrito")});
        const QPointF C(0, 2);
        const double R = 40.0;
        // Cuatro ángulos de tangencia: inferior 90°, derecho 12°, superior -76°, izquierdo 198°
        const QPointF V1 = tangentIntersect(C, R, 90.0, 198.0);  // inferior izq
        const QPointF V2 = tangentIntersect(C, R, 198.0, -76.0); // superior izq
        const QPointF V3 = tangentIntersect(C, R, -76.0, 12.0);  // superior der
        const QPointF V4 = tangentIntersect(C, R, 12.0, 90.0);   // inferior der
        t.shapes = {
            makePolyline({V1, V2, V3, V4}, true, kInkMain, 2.2),
            makeCircle(C, R, kAccent2, 2.0),
        };
        t.texts = {
            makeLabel(lerpPt(V1, V2, 0.5) + QPointF(-15, 0), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(lerpPt(V2, V3, 0.5) + QPointF(0, -14), QStringLiteral("b"), kAccent3, 15.0),
            makeLabel(lerpPt(V3, V4, 0.5) + QPointF(15, 0), QStringLiteral("c"), kAccent1, 15.0),
            makeLabel(lerpPt(V4, V1, 0.5) + QPointF(0, 15), QStringLiteral("d"), kAccent3, 15.0),
        };
    }

    // 23. Tangentes Exteriores
    {
        auto& t = addTemplate(
            list, QStringLiteral("circ_tangentes"), QStringLiteral("Tangentes Exteriores"),
            QStringLiteral("Circunferencia"), QStringLiteral("PA = PB"),
            {QStringLiteral("tangente"), QStringLiteral("tangentes"), QStringLiteral("circunferencia"),
             QStringLiteral("radio")});
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
            makeMark(P, A, EdgeMarkKind::Tick1, kAccent3),
            makeMark(P, B, EdgeMarkKind::Tick1, kAccent3),
            makeMark(O - QPointF(2, 0), O + QPointF(2, 0), EdgeMarkKind::Dot, kInkMain, 0.5, 2.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 12, 0), QStringLiteral("O"), kInkMain, 13.0),
            makeLabel(QPointF(P.x() + 13, 0), QStringLiteral("P"), kAccent1, 13.0),
            makeLabel(QPointF(A.x() - 2, A.y() - 15), QStringLiteral("A"), kInkMain, 13.0),
            makeLabel(QPointF(B.x() - 2, B.y() + 15), QStringLiteral("B"), kInkMain, 13.0),
        };
    }
}

void appendMetricStickers(QVector<StickerTemplate>& list) {
    // 24. Teorema de Pitágoras
    {
        auto& t = addTemplate(
            list, QStringLiteral("met_pitagoras"), QStringLiteral("Teorema de Pitágoras"),
            QStringLiteral("Métricas"), QStringLiteral("a² + b² = c²"),
            {QStringLiteral("pitagoras"), QStringLiteral("hipotenusa"), QStringLiteral("catetos"),
             QStringLiteral("rectangulo")});
        const QPointF O(-56, 44);
        const QPointF T(-56, -38);
        const QPointF R(56, 44);
        t.shapes = {
            makeTriangle(O, T, R),
            makeRightAngle(T, O, R, kAccent2, 34.0),
        };
        t.texts = {
            makeLabel(QPointF(O.x() - 16, (O.y() + T.y()) * 0.5), QStringLiteral("a"), kAccent1, 15.0),
            makeLabel(QPointF((O.x() + R.x()) * 0.5, O.y() + 15), QStringLiteral("b"), kAccent1, 15.0),
            makeLabel(QPointF((T.x() + R.x()) * 0.5 + 14, (T.y() + R.y()) * 0.5 - 12),
                      QStringLiteral("c"), kAccent3, 15.0),
        };
    }

    // 25. Relaciones Métricas en Triángulo Rectángulo (h² = m·n, a² = c·m)
    {
        auto& t = addTemplate(
            list, QStringLiteral("met_tri_rect"), QStringLiteral("Métricas en T. Rect."),
            QStringLiteral("Métricas"), QStringLiteral("h² = m·n · a² = c·m"),
            {QStringLiteral("metricas"), QStringLiteral("altura"), QStringLiteral("proyeccion"),
             QStringLiteral("rectangulo")});
        // Exacto: m = 44, n = 99 => h = sqrt(44*99) = 66, c = 143
        const QPointF A(-71.5, 38.0);
        const QPointF C(71.5, 38.0);
        const QPointF H(-71.5 + 44.0, 38.0);
        const QPointF B(H.x(), 38.0 - 66.0);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, H, kAccent1, 2.1, LinePattern::Dashed),
            makeRightAngle(A, B, C, kAccent2, 32.0),
            makeRightAngle(B, H, C, kAccent2, 28.0),
        };
        t.texts = {
            makeLabel(QPointF(-62, -6), QStringLiteral("a"), kInkMain, 14.0),
            makeLabel(QPointF(30, -8), QStringLiteral("b"), kInkMain, 14.0),
            makeLabel(QPointF(H.x() + 12, 8), QStringLiteral("h"), kAccent1, 14.0),
            makeLabel(QPointF((A.x() + H.x()) * 0.5, 52), QStringLiteral("m"), kAccent3, 14.0),
            makeLabel(QPointF((H.x() + C.x()) * 0.5, 52), QStringLiteral("n"), kAccent3, 14.0),
        };
    }

    // 26. Teorema de las Cuerdas
    {
        auto& t = addTemplate(
            list, QStringLiteral("met_cuerdas"), QStringLiteral("T. de las Cuerdas"),
            QStringLiteral("Métricas"), QStringLiteral("a · b = c · d"),
            {QStringLiteral("cuerdas"), QStringLiteral("metricas"), QStringLiteral("circunferencia")});
        const QPointF O(0, 0);
        const double R = 56.0;
        const QPointF A = polarPt(O, R, -148.0);
        const QPointF B = polarPt(O, R, 22.0);
        const QPointF C = polarPt(O, R, 132.0);
        const QPointF D = polarPt(O, R, -38.0);
        const QPointF d1 = B - A;
        const QPointF d2 = D - C;
        const double det = d1.x() * d2.y() - d1.y() * d2.x();
        const QPointF diff = C - A;
        const double s = (diff.x() * d2.y() - diff.y() * d2.x()) / det;
        const QPointF P = A + d1 * s;
        t.shapes = {
            makeCircle(O, R, kInkMain, 2.2),
            makeLine(A, B, kAccent1, 2.1),
            makeLine(C, D, kAccent2, 2.1),
            makeMark(P - QPointF(2, 0), P + QPointF(2, 0), EdgeMarkKind::Dot, kInkMain, 0.5, 2.0),
        };
        t.texts = {
            makeLabel(lerpPt(A, P, 0.5) + QPointF(4, -12), QStringLiteral("a"), kAccent1, 14.0),
            makeLabel(lerpPt(P, B, 0.5) + QPointF(4, 13), QStringLiteral("b"), kAccent1, 14.0),
            makeLabel(lerpPt(C, P, 0.5) + QPointF(-12, -6), QStringLiteral("c"), kAccent2, 14.0),
            makeLabel(lerpPt(P, D, 0.5) + QPointF(-11, -9), QStringLiteral("d"), kAccent2, 14.0),
        };
    }

    // 27. Teorema de la Tangente
    {
        auto& t = addTemplate(
            list, QStringLiteral("met_tangente"), QStringLiteral("T. de la Tangente"),
            QStringLiteral("Métricas"), QStringLiteral("x² = a · b"),
            {QStringLiteral("tangente"), QStringLiteral("secante"), QStringLiteral("metricas"),
             QStringLiteral("potencia")});
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
            makeMark(T - QPointF(2, 0), T + QPointF(2, 0), EdgeMarkKind::Dot, kAccent1, 0.5, 2.0),
            makeMark(B - QPointF(2, 0), B + QPointF(2, 0), EdgeMarkKind::Dot, kAccent2, 0.5, 2.0),
        };
        t.texts = {
            makeLabel(lerpPt(P, T, 0.5) + QPointF(6, -13), QStringLiteral("x"), kAccent1, 15.0),
            makeLabel(lerpPt(P, B, 0.5) + QPointF(2, 13), QStringLiteral("b"), kAccent2, 14.0),
            makeLabel(lerpPt(P, A, 0.55) + QPointF(-6, 20), QStringLiteral("a (total)"), kAccent3, 12.0),
            makeLabel(QPointF(P.x() + 12, P.y()), QStringLiteral("P"), kInkMain, 13.0),
        };
    }
}

void appendAreaStickers(QVector<StickerTemplate>& list) {
    // 28. Mediana del Trapecio
    {
        auto& t = addTemplate(
            list, QStringLiteral("area_trapecio"), QStringLiteral("Mediana del Trapecio"),
            QStringLiteral("Áreas"), QStringLiteral("m = (B + b) / 2"),
            {QStringLiteral("trapecio"), QStringLiteral("mediana"), QStringLiteral("bases"),
             QStringLiteral("area")});
        const QPointF A(-86, 42);
        const QPointF D(86, 42);
        const QPointF B(-48, -40);
        const QPointF C(48, -40);
        const QPointF M = lerpPt(A, B, 0.5);
        const QPointF N = lerpPt(C, D, 0.5);
        t.shapes = {
            makePolyline({A, B, C, D}, true, kInkMain, 2.2),
            makeLine(M, N, kAccent1, 2.2),
            makeMark(A, M, EdgeMarkKind::Tick1, kAccent2),
            makeMark(M, B, EdgeMarkKind::Tick1, kAccent2),
            makeMark(C, N, EdgeMarkKind::Tick2, kAccent2),
            makeMark(N, D, EdgeMarkKind::Tick2, kAccent2),
            makeMark(B, C, EdgeMarkKind::Parallel1, kAccent3, 0.28),
            makeMark(A, D, EdgeMarkKind::Parallel1, kAccent3, 0.25),
        };
        t.texts = {
            makeLabel(QPointF(0, -53), QStringLiteral("b"), kInkMain, 15.0),
            makeLabel(QPointF(0, 56), QStringLiteral("B"), kInkMain, 15.0),
            makeLabel(QPointF(0, -10), QStringLiteral("m"), kAccent1, 15.0),
        };
    }

    // 29. Área del Triángulo
    {
        auto& t = addTemplate(
            list, QStringLiteral("area_triangulo"), QStringLiteral("Área del Triángulo"),
            QStringLiteral("Áreas"), QStringLiteral("A = (b · h) / 2"),
            {QStringLiteral("area"), QStringLiteral("triangulo"), QStringLiteral("base"),
             QStringLiteral("altura")});
        const QPointF A(-86, 44);
        const QPointF B(-16, -48);
        const QPointF C(86, 44);
        const QPointF H(B.x(), 44.0);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, H, kAccent1, 2.1, LinePattern::Dashed),
            makeRightAngle(B, H, C, kAccent2, 30.0),
        };
        t.texts = {
            makeLabel(QPointF(B.x() + 13, 0), QStringLiteral("h"), kAccent1, 15.0),
            makeLabel(QPointF(6, 58), QStringLiteral("b"), kInkMain, 15.0),
        };
    }

    // 30. Área del Equilátero
    {
        auto& t = addTemplate(
            list, QStringLiteral("area_equilatero"), QStringLiteral("Área del Equilátero"),
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
            makeMark(A, B, EdgeMarkKind::Tick1, kAccent2),
            makeMark(B, C, EdgeMarkKind::Tick1, kAccent2),
            makeMark(A, C, EdgeMarkKind::Tick1, kAccent2),
        };
        t.texts = {
            makeLabel(QPointF(-42, -8), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(42, -8), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(0, A.y() + 15), QStringLiteral("L"), kInkMain, 15.0),
            makeLabel(QPointF(-24, A.y() - 12), QStringLiteral("60°"), kAccent1, 12.0),
        };
    }

    // 31. Relación de Áreas (Ceviana)
    {
        auto& t = addTemplate(
            list, QStringLiteral("area_ceviana"), QStringLiteral("Relación de Áreas"),
            QStringLiteral("Áreas"), QStringLiteral("A₁ / A₂ = m / n"),
            {QStringLiteral("area"), QStringLiteral("areas"), QStringLiteral("ceviana"),
             QStringLiteral("relacion")});
        const QPointF A(-86, 44);
        const QPointF B(-10, -48);
        const QPointF C(86, 44);
        const QPointF D(-22, 44);
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(B, D, kAccent1, 2.2),
        };
        t.texts = {
            makeLabel(QPointF(-42, 16), QStringLiteral("A₁"), kAccent2, 15.0),
            makeLabel(QPointF(20, 16), QStringLiteral("A₂"), kAccent3, 15.0),
            makeLabel(QPointF((A.x() + D.x()) * 0.5, 58), QStringLiteral("m"), kInkMain, 14.0),
            makeLabel(QPointF((D.x() + C.x()) * 0.5, 58), QStringLiteral("n"), kInkMain, 14.0),
        };
    }
}

} // namespace

void appendGeometryStickers(QVector<StickerTemplate>& list) {
    appendTriangleStickers(list);
    appendNotableStickers(list);
    appendCongruenceStickers(list);
    appendCircleStickers(list);
    appendMetricStickers(list);
    appendAreaStickers(list);
}

} // namespace finily::sticker_detail
