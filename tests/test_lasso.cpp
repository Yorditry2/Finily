#include "TestHelpers.h"
#include "core/Types.h"
#include "engine/InkEngine.h"
#include "engine/LassoEngine.h"

#include <QPainterPath>
#include <QtTest/QtTest>

#include <cmath>
#include <numbers>

using namespace finily;
using namespace finily::test;

class TestLasso : public QObject {
    Q_OBJECT

private slots:
    void lassoTranslateSelectionMovesOnlySelected() {
        Stroke s1;
        Stroke s2;
        s1.id = QStringLiteral("sel");
        s1.points = {{0, 0, 0.5, 0, 0, 0}, {10, 10, 0.5, 10, 0, 0}};
        s2.id = QStringLiteral("unsel");
        s2.points = {{50, 50, 0.5, 0, 0, 0}, {60, 60, 0.5, 10, 0, 0}};
        QVector<Stroke> strokes{s1, s2};

        ShapeItem sh1;
        ShapeItem sh2;
        sh1.id = QStringLiteral("sh_sel");
        sh1.kind = ShapeKind::Line;
        sh1.vertices = {{0, 0}, {20, 0}};
        sh2.id = QStringLiteral("sh_unsel");
        sh2.kind = ShapeKind::Line;
        sh2.vertices = {{0, 40}, {20, 40}};
        QVector<ShapeItem> shapes{sh1, sh2};

        translateSelection(strokes, shapes, QSet<QString>{QStringLiteral("sel")},
                           QSet<QString>{QStringLiteral("sh_sel")}, QPointF(15.0, -5.0));
        QCOMPARE(strokes[0].points[0].x, 15.0);
        QCOMPARE(strokes[0].points[0].y, -5.0);
        QCOMPARE(strokes[1].points[0].x, 50.0);
        QCOMPARE(shapes[0].vertices[0].x(), 15.0);
        QCOMPARE(shapes[1].vertices[0].y(), 40.0);
    }

    void scaleAndRotateTransformShadeProbeTiltAndClearCache() {
        Stroke s;
        s.id = QStringLiteral("s_rot");
        s.size = 3.0;
        s.points = {
            {10.0, 0.0, 0.6, 0, 10.0, 0.0},
            {30.0, 0.0, 0.6, 20, 10.0, 0.0},
        };
        s.bounds = computeBounds(s.points, s.size);
        s.cachedPath.addRect(QRectF(0, 0, 10, 10));
        s.cachedShapeHash = 12345ULL;

        ShapeItem shaded = makeShape(
            ShapeKind::ShadedRegion,
            {QPointF(0, 0), QPointF(10, 0), QPointF(20, 0), QPointF(20, 20), QPointF(0, 20)});
        shaded.id = QStringLiteral("sh_shade");
        shaded.closed = true;
        shaded.shadeProbe = QPointF(10.0, 5.0);

        QVector<Stroke> strokes{s};
        QVector<ShapeItem> shapes{shaded};
        const QSet<QString> sIds{QStringLiteral("s_rot")};
        const QSet<QString> shIds{QStringLiteral("sh_shade")};

        scaleSelection(strokes, shapes, sIds, shIds, QPointF(0.0, 0.0), 2.0, 3.0);
        QVERIFY(strokes[0].cachedPath.isEmpty());
        QCOMPARE(strokes[0].cachedShapeHash, 0ULL);
        QCOMPARE(strokes[0].points[0].x, 20.0);
        QCOMPARE(shapes[0].shadeProbe.x(), 20.0);
        QCOMPARE(shapes[0].shadeProbe.y(), 15.0);

        strokes[0].cachedShapeHash = 999ULL;
        const double halfPi = std::numbers::pi / 2.0;
        rotateSelection(strokes, shapes, sIds, shIds, QPointF(0.0, 0.0), halfPi);
        QCOMPARE(strokes[0].cachedShapeHash, 0ULL);
        // 90 deg CCW transforms tilt (10, 0) -> (~0, ~10) and shadeProbe (20, 15) -> (-15, 20).
        QVERIFY(std::abs(strokes[0].points[0].tiltX) < 1e-6);
        QVERIFY(std::abs(strokes[0].points[0].tiltY - 10.0) < 1e-6);
        QVERIFY(std::abs(shapes[0].shadeProbe.x() + 15.0) < 1e-6);
        QVERIFY(std::abs(shapes[0].shadeProbe.y() - 20.0) < 1e-6);
    }

    void flipSelectionMirrorsSelectedGeometry() {
        Stroke s = makeLineStroke(QPointF(10.0, 10.0), QPointF(30.0, 20.0));
        s.id = QStringLiteral("f1");
        ShapeItem sh = makeShape(ShapeKind::Line, {QPointF(10.0, 10.0), QPointF(50.0, 10.0)});
        sh.id = QStringLiteral("sh1");

        QVector<Stroke> strokes{s};
        QVector<ShapeItem> shapes{sh};
        const QRectF bounds(10.0, 10.0, 40.0, 10.0);
        flipSelection(strokes, shapes, {QStringLiteral("f1")}, {QStringLiteral("sh1")}, bounds, true);

        QCOMPARE(strokes[0].points.front().x, 50.0);
        QCOMPARE(strokes[0].points.back().x, 30.0);
        QCOMPARE(shapes[0].vertices[0].x(), 50.0);
        QCOMPARE(shapes[0].vertices[1].x(), 10.0);
    }

    void straightenSelectedStrokesHandlesJDotAndIsIdempotent() {
        // Two letters: tilted vertical stem 'i'/'j' with dot + '+' operator + 'x' cross.
        Stroke stem = makeLineStroke(QPointF(20.0, 118.0), QPointF(24.0, 158.0), 10);
        stem.id = QStringLiteral("j_stem");
        Stroke dot = makeLineStroke(QPointF(26.0, 100.0), QPointF(28.0, 102.0), 3);
        dot.id = QStringLiteral("j_dot");

        Stroke plusV = makeLineStroke(QPointF(52.0, 114.0), QPointF(52.0, 134.0), 6);
        plusV.id = QStringLiteral("plus_v");
        Stroke plusH = makeLineStroke(QPointF(42.0, 124.0), QPointF(62.0, 124.0), 6);
        plusH.id = QStringLiteral("plus_h");

        Stroke wordR = makeLineStroke(QPointF(80.0, 108.0), QPointF(92.0, 140.0), 8);
        wordR.id = QStringLiteral("word_r");

        QVector<Stroke> strokes{stem, dot, plusV, plusH, wordR};
        const QSet<QString> allIds{QStringLiteral("j_stem"), QStringLiteral("j_dot"),
                                   QStringLiteral("plus_v"), QStringLiteral("plus_h"),
                                   QStringLiteral("word_r")};

        QVERIFY(straightenSelectedStrokes(strokes, allIds));

        // Dot must remain above the stem without colliding or flying away.
        const double stemTop = strokes[0].bounds.top();
        const double dotBottom = strokes[1].bounds.bottom();
        QVERIFY2(dotBottom <= stemTop + 6.0,
                 qPrintable(QString("Dot misaligned vs stem: dotBottom=%1 stemTop=%2")
                                .arg(dotBottom)
                                .arg(stemTop)));

        // Neighbor glyph to the right must not overlap with '+' after spacing adjustments.
        const double plusRight = std::max(strokes[2].bounds.right(), strokes[3].bounds.right());
        const double wordLeft = strokes[4].bounds.left();
        QVERIFY2(plusRight <= wordLeft + 2.0,
                 qPrintable(QString("Operator collision: plusRight=%1 wordLeft=%2")
                                .arg(plusRight)
                                .arg(wordLeft)));

        // Repeating straightenSelectedStrokes must stay nearly idempotent.
        const QRectF firstStemBounds = strokes[0].bounds;
        const QRectF firstDotBounds = strokes[1].bounds;
        straightenSelectedStrokes(strokes, allIds);
        QVERIFY(std::abs(strokes[0].bounds.center().y() - firstStemBounds.center().y()) < 6.0);
        QVERIFY(std::abs(strokes[1].bounds.center().x() - firstDotBounds.center().x()) < 6.0);
    }
};

QTEST_MAIN(TestLasso)
#include "test_lasso.moc"
