#include "TestHelpers.h"
#include "core/ShapeProtocols.h"
#include "core/Types.h"
#include "engine/GeometryEngine.h"
#include "engine/RegionGeometry.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QtTest/QtTest>

#include <cmath>
#include <numbers>

using namespace finily;

class TestGeometryAndRegions : public QObject {
    Q_OBJECT

private slots:
    void eraseShapePartiallyFreeCut() {
        ShapeItem line;
        line.id = "l1";
        line.kind = ShapeKind::Line;
        line.size = 2.0;
        line.vertices = {QPointF(0.0, 0.0), QPointF(100.0, 0.0)};
        line.bounds = computeShapeBounds(line);

        const QVector<ShapeItem> all{line};
        const auto split = eraseShapePartially(line, all, QPointF(50.0, 0.0), 10.0, false);
        QVERIFY(split.has_value());
        QCOMPARE(split->size(), 2);
        QCOMPARE(split->at(0).kind, ShapeKind::Line);
        QCOMPARE(split->at(1).kind, ShapeKind::Line);
        QVERIFY(split->at(0).vertices.back().x() < 41.0);
        QVERIFY(split->at(1).vertices.front().x() > 59.0);
    }

    void eraseShapePartiallyTrimToIntersection() {
        ShapeItem horiz;
        horiz.id = "h1";
        horiz.kind = ShapeKind::Line;
        horiz.size = 2.0;
        horiz.vertices = {QPointF(0.0, 50.0), QPointF(100.0, 50.0)};
        horiz.bounds = computeShapeBounds(horiz);

        ShapeItem vert;
        vert.id = "v1";
        vert.kind = ShapeKind::Line;
        vert.size = 2.0;
        vert.vertices = {QPointF(60.0, 0.0), QPointF(60.0, 100.0)};
        vert.bounds = computeShapeBounds(vert);

        const QVector<ShapeItem> all{horiz, vert};
        const auto trimmed = eraseShapePartially(horiz, all, QPointF(85.0, 50.0), 8.0, true);
        QVERIFY(trimmed.has_value());
        QCOMPARE(trimmed->size(), 1);
        QCOMPARE(trimmed->at(0).kind, ShapeKind::Line);
        QVERIFY(std::abs(trimmed->at(0).vertices.front().x() - 0.0) < 1e-4);
        QVERIFY(std::abs(trimmed->at(0).vertices.back().x() - 60.0) < 1e-4);
    }

    void regionGeometryDetectsClosedSquare() {
        auto makeEdge = [](const QString& id, QPointF a, QPointF b) {
            ShapeItem s;
            s.id = id;
            s.kind = ShapeKind::Line;
            s.size = 2.0;
            s.vertices = {a, b};
            s.bounds = computeShapeBounds(s);
            return s;
        };
        const QVector<ShapeItem> square{
            makeEdge("e1", {0, 0}, {100, 0}),
            makeEdge("e2", {100, 0}, {100, 100}),
            makeEdge("e3", {100, 100}, {0, 100}),
            makeEdge("e4", {0, 100}, {0, 0}),
        };
        const auto region =
            shadedRegionAt(QPointF(50, 50), square, QColor("#ef4444"), ShadePattern::Soft);
        QVERIFY(region.has_value());
        QCOMPARE(region->kind, ShapeKind::ShadedRegion);
        QVERIFY(!region->vertices.isEmpty());
    }

    void dividerPointSplitsCongruenceAndPartialErase() {
        ShapeItem seg;
        seg.id = QStringLiteral("seg1");
        seg.kind = ShapeKind::Line;
        seg.vertices = {QPointF(0, 0), QPointF(200, 0)};
        seg.bounds = computeShapeBounds(seg);

        QVector<ShapeItem> shapes = {seg};
        QString divId;
        QVERIFY(stampOrCycleCongruenceMark(shapes, QPointF(100, 0), QStringLiteral("mark:divider"),
                                           QColor("#ffffff"), 2.0, 1.0, &divId));
        QVERIFY(!divId.isEmpty());

        QString leftId;
        QString rightId;
        QVERIFY(stampOrCycleCongruenceMark(shapes, QPointF(40, 0), QStringLiteral("mark:tick1"),
                                           QColor("#ffffff"), 2.0, 1.0, &leftId));
        QVERIFY(stampOrCycleCongruenceMark(shapes, QPointF(160, 0), QStringLiteral("mark:tick1"),
                                           QColor("#ffffff"), 2.0, 1.0, &rightId));
        QVERIFY(leftId != rightId);

        const auto erased = eraseShapePartially(seg, shapes, QPointF(40, 0), 8.0, true);
        QVERIFY(erased.has_value());
        QCOMPARE(erased->size(), 1);
        QVERIFY(std::abs(erased->at(0).vertices[0].x() - 100.0) < 1.0);
        QVERIFY(std::abs(erased->at(0).vertices[1].x() - 200.0) < 1.0);
    }

    void perpendicularSnapAlignsToExistingEdge() {
        ShapeItem base;
        base.id = QStringLiteral("base1");
        base.kind = ShapeKind::Line;
        base.vertices = {QPointF(0, 0), QPointF(100, 100)};
        base.bounds = computeShapeBounds(base);

        // Anchor at (50, 50) on the 45° diagonal; raw near (-18, 122) is ~90° to (100, 100).
        const SnapResult snap = snapPointWithGuides(QPointF(-18, 122), QPointF(50, 50), {base},
                                                    QString(), 1.0, true);
        const QPointF d = snap.point - QPointF(50, 50);
        const double dotProduct = d.x() * 100.0 + d.y() * 100.0;
        QVERIFY(std::abs(dotProduct) < 1e-4);
    }

    void highlightSegmentCreatesArcBetweenRadii() {
        ShapeItem circle;
        circle.id = QStringLiteral("c1");
        circle.kind = ShapeKind::Circle;
        circle.vertices = {QPointF(0, 0), QPointF(100, 0)};
        circle.bounds = computeShapeBounds(circle);

        ShapeItem r1;
        r1.id = QStringLiteral("r1");
        r1.kind = ShapeKind::Line;
        r1.vertices = {QPointF(0, 0), QPointF(100, 0)};
        r1.bounds = computeShapeBounds(r1);

        ShapeItem r2;
        r2.id = QStringLiteral("r2");
        r2.kind = ShapeKind::Line;
        r2.vertices = {QPointF(0, 0), QPointF(0, -100)};
        r2.bounds = computeShapeBounds(r2);

        const auto hl = createHighlightedSegment({circle, r1, r2}, QPointF(70.7, -70.7),
                                                 QColor("#fde047"), 2.5, 1.0, 0);
        QVERIFY(hl.has_value());
        QCOMPARE(hl->kind, ShapeKind::Arc);
        QCOMPARE(hl->label, QStringLiteral("highlight:glow"));
        QVERIFY(std::abs(hl->vertices[1].x() - 100.0) < 2.0);
        QVERIFY(std::abs(hl->vertices[2].y() - (-100.0)) < 2.0);

        const auto solid = createHighlightedSegment({circle, r1, r2}, QPointF(70.7, -70.7),
                                                    QColor("#38bdf8"), 2.5, 1.0, 1);
        QVERIFY(solid.has_value());
        QCOMPARE(solid->label, QStringLiteral("highlight:solid"));
        QCOMPARE(solid->opacity, 1.0);

        const auto dashed = createHighlightedSegment({circle, r1, r2}, QPointF(70.7, -70.7),
                                                     QColor("#f87171"), 2.5, 1.0, 2);
        QVERIFY(dashed.has_value());
        QCOMPARE(dashed->linePattern, LinePattern::Dashed);
    }

    void freehandRightAngleMarkSnapsToRightAngle() {
        ShapeItem tri;
        tri.id = QStringLiteral("t1");
        tri.kind = ShapeKind::Triangle;
        tri.vertices = {QPointF(0, 0), QPointF(160, 0), QPointF(0, 160)};
        tri.closed = true;
        tri.bounds = computeShapeBounds(tri);

        // Cuadrito de 90° dibujado a mano en la esquina (0,0): (20,0) -> (20,20) -> (0,20)
        Stroke sq;
        sq.tool = ToolType::Pen;
        sq.size = 2.4;
        for (int i = 0; i <= 5; ++i) {
            sq.points.push_back({20.0, double(i) * 4.0, 0.5, i * 5, 0.0, 0.0});
        }
        for (int i = 1; i <= 5; ++i) {
            sq.points.push_back({20.0 - double(i) * 4.0, 20.0, 0.5, 25 + i * 5, 0.0, 0.0});
        }

        const auto rightOpt = trySnapAngleArcAtVertex(sq, {tri}, 1.0);
        QVERIFY(rightOpt.has_value());
        QCOMPARE(rightOpt->kind, ShapeKind::RightAngle);

        // Arco curvo suave en la misma esquina -> debe seguir siendo AngleMarker
        Stroke arc;
        arc.tool = ToolType::Pen;
        arc.size = 2.4;
        for (int i = 0; i <= 10; ++i) {
            const double ang = (double(i) / 10.0) * (std::numbers::pi * 0.5);
            arc.points.push_back(
                {24.0 * std::cos(ang), 24.0 * std::sin(ang), 0.5, i * 5, 0.0, 0.0});
        }
        const auto arcOpt = trySnapAngleArcAtVertex(arc, {tri}, 1.0);
        QVERIFY(arcOpt.has_value());
        QCOMPARE(arcOpt->kind, ShapeKind::AngleMarker);
    }

    void freehandDividerAndArrowsAndWallHatchWorkCleanly() {
        // 1. Punto divisor dibujado a mano sobre una arista
        ShapeItem base;
        base.id = QStringLiteral("base");
        base.kind = ShapeKind::Line;
        base.vertices = {QPointF(0, 100), QPointF(200, 100)};
        base.bounds = computeShapeBounds(base);
        QVector<ShapeItem> shapes = {base};

        Stroke dotOnEdge;
        dotOnEdge.tool = ToolType::Pen;
        dotOnEdge.size = 2.5;
        for (int i = 0; i <= 8; ++i) {
            const double ang = double(i) * (2.0 * std::numbers::pi / 8.0);
            dotOnEdge.points.push_back(
                {100.0 + 4.0 * std::cos(ang), 100.0 + 4.0 * std::sin(ang), 0.5, i * 4, 0, 0});
        }
        QVERIFY(trySnapFreehandDividerOnEdge(dotOnEdge, shapes, 1.0));
        QCOMPARE(shapes.size(), 2);
        QCOMPARE(shapes[1].label, QStringLiteral("mark:divider"));

        // Un puntito lejos de la arista NO debe convertirse en divisor
        Stroke dotFar = dotOnEdge;
        for (auto& pt : dotFar.points) {
            pt.y += 60.0;
        }
        QVERIFY(!trySnapFreehandDividerOnEdge(dotFar, shapes, 1.0));

        // 2. Flecha en un solo trazo (sin mantener presionado)
        Stroke oneStrokeArrow;
        oneStrokeArrow.tool = ToolType::Pen;
        oneStrokeArrow.size = 2.5;
        for (int i = 0; i <= 12; ++i) {
            oneStrokeArrow.points.push_back({double(i) * 10.0, 0.0, 0.5, i * 4, 0, 0});
        }
        // Punta > hacia atrás desde (120, 0) a (106, -10)
        oneStrokeArrow.points.push_back({114.0, -4.0, 0.5, 52, 0, 0});
        oneStrokeArrow.points.push_back({106.0, -10.0, 0.5, 56, 0, 0});
        const auto arrow = tryRecognizeFreehandArrow(oneStrokeArrow, 1.0);
        QVERIFY(arrow.has_value());
        QCOMPARE(arrow->kind, ShapeKind::Line);
        QCOMPARE(arrow->lineEnding, LineEnding::Arrow);

        // 3. Doble flecha dibujando '<' en la cola de la flecha existente
        QVector<ShapeItem> arrowList = {*arrow};
        Stroke tailHead;
        tailHead.tool = ToolType::Pen;
        tailHead.points = {
            {14.0, -9.0, 0.5, 0, 0, 0},
            {7.0, -4.5, 0.5, 4, 0, 0},
            {0.0, 0.0, 0.5, 8, 0, 0},
            {7.0, 4.5, 0.5, 12, 0, 0},
            {14.0, 9.0, 0.5, 16, 0, 0},
        };
        QVERIFY(tryAttachArrowheadToLine(tailHead, arrowList, 1.0));
        QCOMPARE(arrowList[0].lineEnding, LineEnding::DoubleArrow);

        // 4. Pared / suelo rugoso (mark:wall) abarca todo el segmento e invierte lado al repetir
        QVector<ShapeItem> wallShapes = {base};
        QVERIFY(stampOrCycleCongruenceMark(wallShapes, QPointF(100, 100), QStringLiteral("mark:wall"),
                                           QColor("#ffffff"), 2.5, 1.0));
        QCOMPARE(wallShapes.size(), 2);
        QCOMPARE(wallShapes[1].label, QStringLiteral("mark:wall"));
        QVERIFY(stampOrCycleCongruenceMark(wallShapes, QPointF(100, 100), QStringLiteral("mark:wall"),
                                           QColor("#ffffff"), 2.5, 1.0));
        QCOMPARE(wallShapes[1].label, QStringLiteral("mark:wall_flip"));
    }

    void shapeProtocolsParseAndCycleEdgeMarks() {
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:zigzag")), EdgeMarkKind::Zigzag);
        QCOMPARE(parseEdgeMark(QStringLiteral("  MARK:SPRING  ")), EdgeMarkKind::Spring);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:dot")), EdgeMarkKind::Dot);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:divider")), EdgeMarkKind::Divider);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:bar")), EdgeMarkKind::Bar);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:tick1")), EdgeMarkKind::Tick1);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:tick2")), EdgeMarkKind::Tick2);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:tick3")), EdgeMarkKind::Tick3);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:parallel1")), EdgeMarkKind::Parallel1);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:parallel2")), EdgeMarkKind::Parallel2);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:wall")), EdgeMarkKind::Wall);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:wall_flip")), EdgeMarkKind::WallFlip);
        QCOMPARE(parseEdgeMark(QStringLiteral("mark:unknown")), EdgeMarkKind::None);
        QCOMPARE(parseEdgeMark(QStringLiteral("spring")), EdgeMarkKind::None);
        QCOMPARE(parseEdgeMark(QString()), EdgeMarkKind::None);

        QCOMPARE(parseEdgeMark(edgeMarkLabel(EdgeMarkKind::Parallel2)), EdgeMarkKind::Parallel2);

        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Tick1, EdgeMarkKind::Tick1), EdgeMarkKind::Tick2);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Tick2, EdgeMarkKind::Tick1), EdgeMarkKind::Tick3);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Tick3, EdgeMarkKind::Tick2), EdgeMarkKind::Tick1);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Parallel1, EdgeMarkKind::Parallel1), EdgeMarkKind::Parallel2);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Parallel2, EdgeMarkKind::Parallel1), EdgeMarkKind::Parallel1);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Wall, EdgeMarkKind::Wall), EdgeMarkKind::WallFlip);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::WallFlip, EdgeMarkKind::Wall), EdgeMarkKind::Wall);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::Tick2, EdgeMarkKind::Zigzag), EdgeMarkKind::Zigzag);
        QCOMPARE(cycleEdgeMark(EdgeMarkKind::None, EdgeMarkKind::Tick1), EdgeMarkKind::Tick1);

        const ShapeItem divLine = test::makeShape(
            ShapeKind::Line, {QPointF(0, 0), QPointF(10, 0)}, QStringLiteral("mark:divider"));
        const ShapeItem rectWithMark = test::makeShape(
            ShapeKind::Rectangle, {QPointF(0, 0), QPointF(10, 10)}, QStringLiteral("mark:divider"));
        QVERIFY(isEdgeMark(divLine));
        QVERIFY(isDividerMark(divLine));
        QVERIFY(!isEdgeMark(rectWithMark));
        QVERIFY(!isDividerMark(rectWithMark));
    }

    void shapeProtocolsHighlightsTablesSpringsAndMeasurementLabels() {
        QCOMPARE(parseHighlightStyle(QStringLiteral("highlight:glow")), HighlightStyle::Glow);
        QCOMPARE(parseHighlightStyle(QStringLiteral("  HIGHLIGHT:SOLID  ")), HighlightStyle::Solid);
        QCOMPARE(parseHighlightStyle(QStringLiteral("highlight:dashed")), HighlightStyle::Dashed);
        QCOMPARE(parseHighlightStyle(QStringLiteral("highlight:unknown")), HighlightStyle::None);
        QCOMPARE(parseHighlightStyle(QStringLiteral("glow")), HighlightStyle::None);

        QCOMPARE(formatTableSpec(3, 4), QStringLiteral("tabla:3x4"));
        QCOMPARE(formatTableSpec(0, 20), QStringLiteral("tabla:1x12"));

        const auto t1 = parseTableSpec(QStringLiteral("tabla:3x4"));
        QVERIFY(t1.has_value());
        QCOMPARE(t1->rows, 3);
        QCOMPARE(t1->cols, 4);

        const auto t2 = parseTableSpec(QStringLiteral("  TABLA:2\u00D75 "));
        QVERIFY(t2.has_value());
        QCOMPARE(t2->rows, 2);
        QCOMPARE(t2->cols, 5);

        const auto tClamped = parseTableSpec(QStringLiteral("tabla:15x20"));
        QVERIFY(tClamped.has_value());
        QCOMPARE(tClamped->rows, 12);
        QCOMPARE(tClamped->cols, 12);

        QVERIFY(!parseTableSpec(QStringLiteral("tabla:0x3")).has_value());
        QVERIFY(!parseTableSpec(QStringLiteral("tabla:3x")).has_value());
        QVERIFY(!parseTableSpec(QStringLiteral("tabla:axb")).has_value());
        QVERIFY(!parseTableSpec(QStringLiteral("3x4")).has_value());

        QVERIFY(isPhysicsSpringSpec(QStringLiteral("physics:spring")));
        QVERIFY(isPhysicsSpringSpec(QStringLiteral("  PHYSICS:SPRING ")));
        QVERIFY(!isPhysicsSpringSpec(QStringLiteral("mark:spring")));
        QVERIFY(!isPhysicsSpringSpec(QStringLiteral("spring")));

        const ShapeItem physSpring = test::makeShape(
            ShapeKind::Line, {QPointF(0, 0), QPointF(80, 0)}, physicsSpringLabel());
        const ShapeItem markSpring = test::makeShape(
            ShapeKind::Line, {QPointF(0, 0), QPointF(80, 0)}, QStringLiteral("mark:spring"));
        const ShapeItem circleSpring = test::makeShape(
            ShapeKind::Circle, {QPointF(0, 0), QPointF(20, 0)}, physicsSpringLabel());
        QVERIFY(isPhysicsSpring(physSpring));
        QVERIFY(!isPhysicsSpring(markSpring));
        QVERIFY(!isPhysicsSpring(circleSpring));

        const ShapeItem autoAngle = test::makeShape(
            ShapeKind::AngleMarker, {QPointF(0, 0), QPointF(30, 0), QPointF(0, 30)},
            autoVertexAngleLabel());
        const ShapeItem userAngle = test::makeShape(
            ShapeKind::AngleMarker, {QPointF(0, 0), QPointF(30, 0), QPointF(0, 30)},
            QStringLiteral("60°"));
        const ShapeItem arcWithAuto = test::makeShape(
            ShapeKind::Arc, {QPointF(0, 0), QPointF(30, 0), QPointF(0, 30)},
            autoVertexAngleLabel());
        QVERIFY(isAutoVertexAngle(autoAngle));
        QVERIFY(!isAutoVertexAngle(userAngle));
        QVERIFY(!isAutoVertexAngle(arcWithAuto));

        QVERIFY(!hasEditableMeasurementLabel(autoAngle));
        QVERIFY(hasEditableMeasurementLabel(userAngle));

        ShapeItem dimLine = test::makeShape(
            ShapeKind::Line, {QPointF(0, 0), QPointF(100, 0)}, QStringLiteral("12 cm"));
        dimLine.linePattern = LinePattern::Dimension;
        QVERIFY(hasEditableMeasurementLabel(dimLine));

        ShapeItem solidLine = dimLine;
        solidLine.linePattern = LinePattern::Solid;
        QVERIFY(!hasEditableMeasurementLabel(solidLine));

        ShapeItem dimOverlay = dimLine;
        dimOverlay.label = QStringLiteral("highlight:solid");
        QVERIFY(!hasEditableMeasurementLabel(dimOverlay));

        ShapeItem dimSpring = dimLine;
        dimSpring.label = physicsSpringLabel();
        QVERIFY(!hasEditableMeasurementLabel(dimSpring));
    }

    void glowHighlightBoundsPaddingAndEdgeCollectionSkipsOverlaysAndSprings() {
        const ShapeItem plain = test::makeShape(
            ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, QString(), 2.0);
        const ShapeItem solid = test::makeShape(
            ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, QStringLiteral("highlight:solid"), 2.0);
        const ShapeItem glow = test::makeShape(
            ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, QStringLiteral("highlight:glow"), 2.0);

        QCOMPARE(solid.bounds, plain.bounds);
        QVERIFY(glow.bounds.width() > plain.bounds.width());
        QVERIFY(glow.bounds.height() > plain.bounds.height());
        QVERIFY(glow.bounds.height() > shapeGeometryBounds(glow).height() + 10.0);

        const ShapeItem thickPlain = test::makeShape(
            ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, QString(), 10.0);
        const ShapeItem thickGlow = test::makeShape(
            ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, QStringLiteral("highlight:glow"), 10.0);
        QVERIFY(thickGlow.bounds.height() > thickPlain.bounds.height());

        const ShapeItem base = test::makeShape(
            ShapeKind::Line, {QPointF(0, 0), QPointF(200, 0)});
        QVERIFY(isPointNearGeometryEdge(QPointF(100, 0), {base}, 1.0));

        const QStringList ignoredLabels = {
            QStringLiteral("highlight:glow"),
            QStringLiteral("highlight:solid"),
            QStringLiteral("highlight:dashed"),
            QStringLiteral("highlight:custom"),
            QStringLiteral("physics:spring"),
            QStringLiteral("mark:spring"),
        };
        for (const QString& label : ignoredLabels) {
            const ShapeItem ignored = test::makeShape(
                ShapeKind::Line, {QPointF(0, 0), QPointF(200, 0)}, label);
            QVERIFY(!isPointNearGeometryEdge(QPointF(100, 0), {ignored}, 1.0));
            QVERIFY(!createHighlightedSegment({ignored}, QPointF(100, 0), Qt::yellow, 2.0, 1.0, 0)
                         .has_value());
        }

        // Crossing overlays and springs must not split the underlying geometry edge.
        const ShapeItem crossGlow = test::makeShape(
            ShapeKind::Line, {QPointF(80, -40), QPointF(80, 40)}, QStringLiteral("highlight:glow"));
        const ShapeItem crossPhysSpring = test::makeShape(
            ShapeKind::Line, {QPointF(120, -40), QPointF(120, 40)}, QStringLiteral("physics:spring"));
        const ShapeItem crossMarkSpring = test::makeShape(
            ShapeKind::Line, {QPointF(150, -40), QPointF(150, 40)}, QStringLiteral("mark:spring"));
        const auto hl = createHighlightedSegment(
            {base, crossGlow, crossPhysSpring, crossMarkSpring},
            QPointF(40, 0), Qt::yellow, 2.0, 1.0, 0);
        QVERIFY(hl.has_value());
        QCOMPARE(hl->kind, ShapeKind::Line);
        QVERIFY(std::abs(hl->vertices[0].x() - 0.0) < 1e-4);
        QVERIFY(std::abs(hl->vertices[1].x() - 200.0) < 1e-4);
    }
};

QTEST_MAIN(TestGeometryAndRegions)
#include "test_geometry_and_regions.moc"
