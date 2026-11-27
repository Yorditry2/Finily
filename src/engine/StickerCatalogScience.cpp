#include "engine/StickerCatalogInternal.h"

#include <cmath>
#include <numbers>

namespace finily::sticker_detail {
namespace {

void appendPhysicsStickers(QVector<StickerTemplate>& list) {
    // 32. Plano Inclinado
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_inclined_plane"), QStringLiteral("Plano Inclinado"),
            QStringLiteral("Física"), QStringLiteral("F = m·g·sen θ"),
            {QStringLiteral("fisica"), QStringLiteral("plano"), QStringLiteral("inclinado"),
             QStringLiteral("dinamica"), QStringLiteral("fuerza")});
        const QPointF A(-86, 46);
        const QPointF B(86, 46);
        const QPointF C(86, -42);
        const QPointF slopeDir = unitVec(C - A);
        const QPointF slopeNorm(-slopeDir.y(), slopeDir.x());
        const QPointF upNorm = -slopeNorm;
        const QPointF blkCenter = lerpPt(A, C, 0.48) + upNorm * 16.0;
        const QPointF halfU = slopeDir * 18.0;
        const QPointF halfV = upNorm * 13.0;
        t.shapes = {
            makeTriangle(A, B, C),
            makeLine(QPointF(-96, 46), QPointF(96, 46), kInkMain, 2.0),
            makeLine(QPointF(-96, 46), QPointF(96, 46), kAccent2, 1.5, LinePattern::Solid,
                     edgeMarkLabel(EdgeMarkKind::Wall)),
            makePolyline({blkCenter - halfU - halfV, blkCenter + halfU - halfV,
                          blkCenter + halfU + halfV, blkCenter - halfU + halfV},
                         true, kAccent1, 2.2),
            makeAngle(A, B, C, kAccent2, 24.0),
            makeLine(blkCenter, blkCenter + QPointF(0.0, 42.0), kAccent2, 2.0, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
            makeLine(blkCenter, blkCenter + upNorm * 34.0, kAccent3, 2.0, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
        };
        t.texts = {
            makeLabel(blkCenter, QStringLiteral("m"), kAccent1, 13.0),
            makeLabel(QPointF(-38, 34), QStringLiteral("θ"), kAccent2, 13.0),
            makeLabel(blkCenter + QPointF(14, 34), QStringLiteral("mg"), kAccent2, 12.0),
            makeLabel(blkCenter + upNorm * 38.0 + QPointF(-12, 0), QStringLiteral("N"), kAccent3, 12.0),
        };
    }

    // 33. Polea Fija
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_pulley"), QStringLiteral("Polea Fija"),
            QStringLiteral("Física"), QStringLiteral("T₁ = T₂"),
            {QStringLiteral("fisica"), QStringLiteral("polea"), QStringLiteral("tension"),
             QStringLiteral("atwood"), QStringLiteral("masas")});
        const QPointF ceilL(-60, -52);
        const QPointF ceilR(60, -52);
        const QPointF center(0, -24);
        const double r = 18.0;
        t.shapes = {
            makeLine(ceilL, ceilR, kInkMain, 2.2),
            makeLine(ceilL, ceilR, kAccent2, 1.5, LinePattern::Solid,
                     edgeMarkLabel(EdgeMarkKind::WallFlip)),
            makeLine(QPointF(0, -52), center, kInkMain, 2.2),
            makeCircle(center, r, kAccent1, 2.2),
            makeCircle(center, 2.8, kInkMain, 2.0),
            makeLine(QPointF(-r, -24), QPointF(-r, 18), kInkMain, 1.9),
            makeLine(QPointF(r, -24), QPointF(r, 26), kInkMain, 1.9),
            makePolyline({QPointF(-r - 14, 18), QPointF(-r + 14, 18),
                          QPointF(-r + 14, 42), QPointF(-r - 14, 42)},
                         true, kAccent3, 2.1),
            makePolyline({QPointF(r - 15, 26), QPointF(r + 15, 26),
                          QPointF(r + 15, 52), QPointF(r - 15, 52)},
                         true, kAccent2, 2.1),
        };
        t.texts = {
            makeLabel(QPointF(-r, 30), QStringLiteral("m₁"), kAccent3, 12.5),
            makeLabel(QPointF(r, 39), QStringLiteral("m₂"), kAccent2, 12.5),
            makeLabel(QPointF(-32, -4), QStringLiteral("T"), kAccent1, 12.0),
        };
    }

    // 34. Sistema Masa–Resorte
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_spring_block"), QStringLiteral("Masa–Resorte"),
            QStringLiteral("Física"), QStringLiteral("F = -k·x"),
            {QStringLiteral("fisica"), QStringLiteral("resorte"), QStringLiteral("hooke"),
             QStringLiteral("masa"), QStringLiteral("elastico")});
        t.shapes = {
            makeLine(QPointF(-86, -40), QPointF(-86, 40), kInkMain, 2.2),
            makeLine(QPointF(-86, -40), QPointF(-86, 40), kAccent2, 1.5, LinePattern::Solid,
                     edgeMarkLabel(EdgeMarkKind::WallFlip)),
            makeLine(QPointF(-86, 40), QPointF(92, 40), kInkMain, 2.2),
            makeLine(QPointF(-86, 40), QPointF(92, 40), kAccent2, 1.5, LinePattern::Solid,
                     edgeMarkLabel(EdgeMarkKind::Wall)),
            makeLine(QPointF(-86, 16), QPointF(18, 16), kAccent1, 2.2, LinePattern::Solid,
                     physicsSpringLabel()),
            makePolyline({QPointF(18, -6), QPointF(68, -6), QPointF(68, 40), QPointF(18, 40)},
                         true, kAccent3, 2.2),
        };
        t.texts = {
            makeLabel(QPointF(-34, -8), QStringLiteral("k"), kAccent1, 14.0),
            makeLabel(QPointF(43, 17), QStringLiteral("m"), kAccent3, 15.0),
        };
    }

    // 35. Péndulo Simple
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_pendulum"), QStringLiteral("Péndulo Simple"),
            QStringLiteral("Física"), QStringLiteral("T = 2π√(L/g)"),
            {QStringLiteral("fisica"), QStringLiteral("pendulo"), QStringLiteral("oscilacion"),
             QStringLiteral("gravedad")});
        const QPointF pivot(0, -48);
        const QPointF bob(44, 34);
        t.shapes = {
            makeLine(QPointF(-64, -48), QPointF(64, -48), kInkMain, 2.2),
            makeLine(QPointF(-64, -48), QPointF(64, -48), kAccent2, 1.5, LinePattern::Solid,
                     edgeMarkLabel(EdgeMarkKind::WallFlip)),
            makeLine(pivot, QPointF(0, 44), kInkMain, 1.5, LinePattern::Dashed),
            makeLine(pivot, bob, kAccent1, 2.1),
            makeAngle(pivot, QPointF(0, 44), bob, kAccent2, 26.0),
            makeCircle(bob, 13.0, kAccent3, 2.2),
        };
        t.texts = {
            makeLabel(QPointF(13, -14), QStringLiteral("θ"), kAccent2, 13.0),
            makeLabel(QPointF(34, -4), QStringLiteral("L"), kAccent1, 13.5),
            makeLabel(bob, QStringLiteral("m"), kAccent3, 13.0),
        };
    }

    // 36. Circuito Eléctrico Básico
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_circuit"), QStringLiteral("Circuito Básico"),
            QStringLiteral("Física"), QStringLiteral("V = I·R"),
            {QStringLiteral("fisica"), QStringLiteral("circuito"), QStringLiteral("ohm"),
             QStringLiteral("resistencia"), QStringLiteral("bateria")});
        t.shapes = {
            // Alambres superior, laterales e inferior (pila izquierda, resistencia arriba)
            makeLine(QPointF(-70, -34), QPointF(-28, -34), kInkMain, 2.0),
            makeLine(QPointF(-28, -34), QPointF(28, -34), kAccent1, 2.0, LinePattern::Solid,
                     physicsSpringLabel()),
            makeLine(QPointF(28, -34), QPointF(70, -34), kInkMain, 2.0),
            makeLine(QPointF(70, -34), QPointF(70, 34), kInkMain, 2.0),
            makeLine(QPointF(70, 34), QPointF(-70, 34), kInkMain, 2.0),
            makeLine(QPointF(-70, 34), QPointF(-70, 7), kInkMain, 2.0),
            makeLine(QPointF(-70, -7), QPointF(-70, -34), kInkMain, 2.0),
            // Símbolo de batería (+ largo / - corto)
            makeLine(QPointF(-84, -7), QPointF(-56, -7), kAccent2, 2.4),
            makeLine(QPointF(-78, 7), QPointF(-62, 7), kAccent2, 2.4),
            // Flecha de corriente
            makeLine(QPointF(38, -22), QPointF(60, -22), kAccent3, 1.8, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
        };
        t.texts = {
            makeLabel(QPointF(0, -50), QStringLiteral("R"), kAccent1, 13.5),
            makeLabel(QPointF(-96, 0), QStringLiteral("V"), kAccent2, 13.5),
            makeLabel(QPointF(50, -10), QStringLiteral("I"), kAccent3, 12.5),
        };
    }

    // 37. Lente Convergente (Óptica)
    {
        auto& t = addTemplate(
            list, QStringLiteral("phys_lens"), QStringLiteral("Lente Convergente"),
            QStringLiteral("Física"), QStringLiteral("1/f = 1/p + 1/q"),
            {QStringLiteral("fisica"), QStringLiteral("optica"), QStringLiteral("lente"),
             QStringLiteral("foco")});
        t.shapes = {
            makeLine(QPointF(-96, 0), QPointF(96, 0), kInkMain, 1.5, LinePattern::Dashed),
            makeLine(QPointF(0, -48), QPointF(0, 48), kAccent1, 2.4, LinePattern::Solid,
                     QString(), LineEnding::DoubleArrow),
            makeLine(QPointF(-46, -4), QPointF(-46, 4), kAccent2, 2.0),
            makeLine(QPointF(46, -4), QPointF(46, 4), kAccent2, 2.0),
            makeLine(QPointF(-74, 0), QPointF(-74, -26), kAccent3, 2.2, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
            makeLine(QPointF(-74, -26), QPointF(0, -26), kAccent3, 1.5),
            makeLine(QPointF(0, -26), QPointF(78, 22), kAccent3, 1.5),
        };
        t.texts = {
            makeLabel(QPointF(-46, 14), QStringLiteral("F"), kAccent2, 13.0),
            makeLabel(QPointF(46, 14), QStringLiteral("F'"), kAccent2, 13.0),
        };
    }
}

void appendChemistryStickers(QVector<StickerTemplate>& list) {
    // 38. Anillo de Benceno
    {
        auto& t = addTemplate(
            list, QStringLiteral("chem_benzene"), QStringLiteral("Anillo de Benceno"),
            QStringLiteral("Química"), QStringLiteral("C₆H₆ (Aromático)"),
            {QStringLiteral("quimica"), QStringLiteral("benceno"), QStringLiteral("organica"),
             QStringLiteral("hexagono"), QStringLiteral("aromatico")});
        QVector<QPointF> hex;
        const double R = 48.0;
        for (int i = 0; i < 6; ++i)
            hex.append(polarPt(QPointF(0, 0), R, -90.0 + i * 60.0));
        t.shapes = {
            makePolyline(hex, true, kInkMain, 2.4),
            makeCircle(QPointF(0, 0), R * 0.56, kAccent1, 2.0),
        };
    }

    // 39. Vaso de Precipitado
    {
        auto& t = addTemplate(
            list, QStringLiteral("chem_beaker"), QStringLiteral("Vaso de Precipitado"),
            QStringLiteral("Química"), QStringLiteral("V (mL) · Disolución"),
            {QStringLiteral("quimica"), QStringLiteral("vaso"), QStringLiteral("precipitado"),
             QStringLiteral("laboratorio")});
        t.shapes = {
            makePolyline({QPointF(-44, -44), QPointF(-36, -34), QPointF(-36, 44),
                          QPointF(36, 44), QPointF(36, -42)},
                         false, kInkMain, 2.3),
            makeLine(QPointF(-36, 6), QPointF(36, 6), kAccent1, 2.0),
            makeLine(QPointF(18, -12), QPointF(34, -12), kAccent2, 1.5),
            makeLine(QPointF(22, 22), QPointF(34, 22), kAccent2, 1.5),
            makeRectShadedRegion(QRectF(-35, 7, 70, 36), kAccent1, 0.25),
        };
    }

    // 40. Matraz Erlenmeyer
    {
        auto& t = addTemplate(
            list, QStringLiteral("chem_flask"), QStringLiteral("Matraz Erlenmeyer"),
            QStringLiteral("Química"), QStringLiteral("n = m / M"),
            {QStringLiteral("quimica"), QStringLiteral("matraz"), QStringLiteral("erlenmeyer"),
             QStringLiteral("molaridad")});
        t.shapes = {
            makePolyline({QPointF(-18, -48), QPointF(-14, -40), QPointF(-14, -14),
                          QPointF(-46, 40), QPointF(46, 40), QPointF(14, -14),
                          QPointF(14, -40), QPointF(18, -48)},
                         false, kInkMain, 2.3),
            makeLine(QPointF(-30, 14), QPointF(30, 14), kAccent3, 2.0),
            makeCircle(QPointF(-8, 26), 3.5, kAccent1, 1.6),
            makeCircle(QPointF(10, 22), 2.5, kAccent1, 1.6),
        };
    }
}

void appendBiologyStickers(QVector<StickerTemplate>& list) {
    // 41. Célula Eucariota
    {
        auto& t = addTemplate(
            list, QStringLiteral("bio_cell"), QStringLiteral("Célula Eucariota"),
            QStringLiteral("Biología"), QStringLiteral("Membrana · Núcleo · Organelos"),
            {QStringLiteral("biologia"), QStringLiteral("celula"), QStringLiteral("nucleo"),
             QStringLiteral("mitocondria")});
        t.shapes = {
            makeEllipse(QPointF(0, 0), 82.0, 48.0, kInkMain, 2.3),
            makeCircle(QPointF(-18, -4), 21.0, kAccent1, 2.1),
            makeCircle(QPointF(-18, -4), 6.5, kAccent2, 1.8),
            makeEllipse(QPointF(36, -12), 16.0, 8.0, kAccent3, 1.8),
            makeEllipse(QPointF(26, 20), 14.0, 7.0, kAccent3, 1.8),
            makeEllipse(QPointF(-52, 18), 11.0, 6.0, kAccent2, 1.6),
        };
        t.texts = {
            makeLabel(QPointF(-18, 24), QStringLiteral("Núcleo"), kAccent1, 11.0),
            makeLabel(QPointF(38, -28), QStringLiteral("Mitocondria"), kAccent3, 10.5),
        };
    }

    // 42. Doble Hélice ADN
    {
        auto& t = addTemplate(
            list, QStringLiteral("bio_dna"), QStringLiteral("Doble Hélice ADN"),
            QStringLiteral("Biología"), QStringLiteral("A=T  ·  C≡G"),
            {QStringLiteral("biologia"), QStringLiteral("adn"), QStringLiteral("dna"),
             QStringLiteral("genetica"), QStringLiteral("helice")});
        QVector<QPointF> s1;
        QVector<QPointF> s2;
        constexpr int kPts = 28;
        for (int i = 0; i <= kPts; ++i) {
            const double u = double(i) / double(kPts);
            const double x = -86.0 + u * 172.0;
            const double phase = u * 2.5 * std::numbers::pi;
            s1.append(QPointF(x, 26.0 * std::sin(phase)));
            s2.append(QPointF(x, -26.0 * std::sin(phase)));
        }
        t.shapes = {
            makePolyline(s1, false, kAccent1, 2.3),
            makePolyline(s2, false, kAccent2, 2.3),
        };
        for (int i = 2; i < kPts; i += 3)
            t.shapes.append(makeLine(s1[i], s2[i], kInkMain, 1.5));
    }

    // 43. Ciclo Biológico (4 Fases)
    {
        auto& t = addTemplate(
            list, QStringLiteral("bio_cycle"), QStringLiteral("Ciclo (4 Fases)"),
            QStringLiteral("Biología"), QStringLiteral("1 → 2 → 3 → 4"),
            {QStringLiteral("biologia"), QStringLiteral("ciclo"), QStringLiteral("fases"),
             QStringLiteral("mitosis"), QStringLiteral("proceso")});
        auto box = [&](const QPointF& c, const QColor& col) {
            return makePolyline({c + QPointF(-24, -14), c + QPointF(24, -14),
                                 c + QPointF(24, 14), c + QPointF(-24, 14)},
                                true, col, 2.0);
        };
        const QPointF c1(0, -40);
        const QPointF c2(64, 0);
        const QPointF c3(0, 40);
        const QPointF c4(-64, 0);
        t.shapes = {
            box(c1, kAccent1),
            box(c2, kAccent2),
            box(c3, kAccent3),
            box(c4, kAccent1),
            makeLine(QPointF(26, -34), QPointF(54, -18), kInkMain, 1.8, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
            makeLine(QPointF(54, 18), QPointF(26, 34), kInkMain, 1.8, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
            makeLine(QPointF(-26, 34), QPointF(-54, 18), kInkMain, 1.8, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
            makeLine(QPointF(-54, -18), QPointF(-26, -34), kInkMain, 1.8, LinePattern::Solid,
                     QString(), LineEnding::Arrow),
        };
        t.texts = {
            makeLabel(c1, QStringLiteral("1"), kAccent1, 13.0),
            makeLabel(c2, QStringLiteral("2"), kAccent2, 13.0),
            makeLabel(c3, QStringLiteral("3"), kAccent3, 13.0),
            makeLabel(c4, QStringLiteral("4"), kAccent1, 13.0),
        };
    }
}

} // namespace

void appendScienceStickers(QVector<StickerTemplate>& list) {
    appendPhysicsStickers(list);
    appendChemistryStickers(list);
    appendBiologyStickers(list);
}

} // namespace finily::sticker_detail
