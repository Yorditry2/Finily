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

    void recognizeFreehandShapeRespectsAllOptions() {
        auto makePolyStroke = [](const QVector<QPointF>& pts, int stepsPerEdge) {
            Stroke s;
            s.tool = ToolType::Pen;
            s.size = 2.4;
            qint64 t = 0;
            for (qsizetype i = 0; i + 1 < pts.size(); ++i) {
                for (int k = (i == 0 ? 0 : 1); k <= stepsPerEdge; ++k) {
                    const double u = double(k) / double(stepsPerEdge);
                    const QPointF p = pts[i] + (pts[i + 1] - pts[i]) * u;
                    s.points.push_back({p.x(), p.y(), 0.5, t, 0.0, 0.0});
                    t += 5;
                }
            }
            return s;
        };

        const Stroke lineStroke = test::makeLineStroke(QPointF(0, 0), QPointF(120, 0), 12);
        const Stroke radicalStroke =
            makePolyStroke({{0, 40}, {16, 80}, {36, 10}, {120, 10}}, 8);
        const Stroke triStroke =
            makePolyStroke({{60, 0}, {120, 100}, {0, 100}, {60, 0}}, 8);
        const Stroke rectStroke =
            makePolyStroke({{0, 0}, {120, 0}, {120, 80}, {0, 80}, {0, 0}}, 8);
        const Stroke quadStroke =
            makePolyStroke({{20, 0}, {100, 0}, {135, 80}, {0, 80}, {20, 0}}, 8);

        QVector<QPointF> hexPts;
        for (int i = 0; i <= 6; ++i) {
            const double a = (double(i % 6) / 6.0) * 2.0 * std::numbers::pi;
            hexPts.append(QPointF(90.0 + 82.0 * std::cos(a), 80.0 + 60.0 * std::sin(a)));
        }
        const Stroke polyStroke = makePolyStroke(hexPts, 3);

        Stroke circleStroke;
        circleStroke.tool = ToolType::Pen;
        circleStroke.size = 2.4;
        for (int i = 0; i <= 36; ++i) {
            const double a = (double(i) / 36.0) * 2.0 * std::numbers::pi;
            circleStroke.points.push_back(
                {80.0 + 60.0 * std::cos(a), 80.0 + 60.0 * std::sin(a), 0.5, i * 4, 0.0, 0.0});
        }

        const Stroke rightAngleLStroke = makePolyStroke({{0, 0}, {0, 80}, {80, 80}}, 8);

        Stroke curveStroke;
        curveStroke.tool = ToolType::Pen;
        curveStroke.size = 2.4;
        for (int i = 0; i <= 20; ++i) {
            const double t = double(i) / 20.0;
            const double u = 1.0 - t;
            const QPointF p = u * u * QPointF(0, 0) + 2.0 * u * t * QPointF(60, -80)
                + t * t * QPointF(120, 0);
            curveStroke.points.push_back({p.x(), p.y(), 0.5, i * 4, 0.0, 0.0});
        }

        // Default options recognize all 9 shape kinds.
        QCOMPARE(recognizeFreehandShape(lineStroke)->kind, ShapeKind::Line);
        QCOMPARE(recognizeFreehandShape(radicalStroke)->kind, ShapeKind::Radical);
        QCOMPARE(recognizeFreehandShape(triStroke)->kind, ShapeKind::Triangle);
        QCOMPARE(recognizeFreehandShape(rectStroke)->kind, ShapeKind::Rectangle);
        const auto qDefault = recognizeFreehandShape(quadStroke);
        QVERIFY(qDefault.has_value());
        QCOMPARE(qDefault->kind, ShapeKind::Polyline);
        QCOMPARE(qDefault->vertices.size(), 4);
        QVERIFY(qDefault->closed);
        const auto pDefault = recognizeFreehandShape(polyStroke);
        QVERIFY(pDefault.has_value());
        QCOMPARE(pDefault->kind, ShapeKind::Polyline);
        QCOMPARE(pDefault->vertices.size(), 6);
        QCOMPARE(recognizeFreehandShape(circleStroke)->kind, ShapeKind::Circle);
        const auto lDefault = recognizeFreehandShape(rightAngleLStroke);
        QVERIFY(lDefault.has_value());
        QCOMPARE(lDefault->kind, ShapeKind::Polyline);
        QCOMPARE(lDefault->vertices.size(), 3);
        QVERIFY(!lDefault->closed);
        QCOMPARE(recognizeFreehandShape(curveStroke)->kind, ShapeKind::Curve);

        {
            AutoShapeOptions opts;
            opts.line = false;
            QVERIFY(!recognizeFreehandShape(lineStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.radical = false;
            QVERIFY(!recognizeFreehandShape(radicalStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.triangle = false;
            QVERIFY(!recognizeFreehandShape(triStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.rectangle = false;
            QVERIFY(!recognizeFreehandShape(rectStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.quadrilateral = false;
            QVERIFY(!recognizeFreehandShape(quadStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.polygon = false;
            QVERIFY(!recognizeFreehandShape(polyStroke, opts).has_value());
        }
        {
            // Very round stroke must return nullopt when opts.circle == false (no polygon/square fallback).
            AutoShapeOptions opts;
            opts.circle = false;
            QVERIFY(!recognizeFreehandShape(circleStroke, opts).has_value());
        }
        {
            AutoShapeOptions opts;
            opts.rightAngleL = false;
            opts.curve = false;
            QVERIFY(!recognizeFreehandShape(rightAngleLStroke, opts).has_value());
            // With curve=true and rightAngleL=false, it must NOT emit the 3-pt L polyline.
            opts.curve = true;
            const auto res = recognizeFreehandShape(rightAngleLStroke, opts);
            QVERIFY(!res.has_value() || res->kind != ShapeKind::Polyline);
        }
        {
            AutoShapeOptions opts;
            opts.curve = false;
            QVERIFY(!recognizeFreehandShape(curveStroke, opts).has_value());
        }
    }

    void trySnapAngleArcAtVertexRespectsIndividualSwitches() {
        ShapeItem tri;
        tri.id = QStringLiteral("t_sw");
        tri.kind = ShapeKind::Triangle;
        tri.vertices = {QPointF(0, 0), QPointF(160, 0), QPointF(0, 160)};
        tri.closed = true;
        tri.bounds = computeShapeBounds(tri);

        Stroke sq;
        sq.tool = ToolType::Pen;
        sq.size = 2.4;
        for (int i = 0; i <= 5; ++i)
            sq.points.push_back({20.0, double(i) * 4.0, 0.5, i * 5, 0.0, 0.0});
        for (int i = 1; i <= 5; ++i)
            sq.points.push_back({20.0 - double(i) * 4.0, 20.0, 0.5, 25 + i * 5, 0.0, 0.0});

        Stroke arc;
        arc.tool = ToolType::Pen;
        arc.size = 2.4;
        for (int i = 0; i <= 10; ++i) {
            const double ang = (double(i) / 10.0) * (std::numbers::pi * 0.5);
            arc.points.push_back({24.0 * std::cos(ang), 24.0 * std::sin(ang), 0.5, i * 5, 0.0, 0.0});
        }

        // Disabling angleArc keeps right-angle corner snapping, blocks curved arc.
        const auto sqNoArc = trySnapAngleArcAtVertex(sq, {tri}, 1.0, false, true);
        QVERIFY(sqNoArc.has_value());
        QCOMPARE(sqNoArc->kind, ShapeKind::RightAngle);
        QVERIFY(!trySnapAngleArcAtVertex(arc, {tri}, 1.0, false, true).has_value());

        // Disabling rightAngle keeps curved arc snapping, blocks square corner.
        QVERIFY(!trySnapAngleArcAtVertex(sq, {tri}, 1.0, true, false).has_value());
        const auto arcNoRight = trySnapAngleArcAtVertex(arc, {tri}, 1.0, true, false);
        QVERIFY(arcNoRight.has_value());
        QCOMPARE(arcNoRight->kind, ShapeKind::AngleMarker);

        // Both false disables both.
        QVERIFY(!trySnapAngleArcAtVertex(sq, {tri}, 1.0, false, false).has_value());
        QVERIFY(!trySnapAngleArcAtVertex(arc, {tri}, 1.0, false, false).has_value());
    }

    void smartGuideOptionsToggleEachStage() {
        const ShapeItem diag = test::makeShape(ShapeKind::Line, {QPointF(0, 0), QPointF(100, 100)});
        const ShapeItem vert = test::makeShape(ShapeKind::Line, {QPointF(200, 0), QPointF(200, 120)});
        const QVector<ShapeItem> shapes{diag, vert};

        // 1. snapKeyPoints: near (200, 0) snaps when true, skips contact when false.
        {
            const SmartGuideOptions onlyKp{true, true, false, false};
            const SnapResult hit = snapPointWithGuides(QPointF(202, 2), std::nullopt, shapes, {}, 1.0, onlyKp);
            QVERIFY(hit.contact);
            QCOMPARE(hit.point, QPointF(200, 0));

            const SmartGuideOptions noKp{true, false, false, false};
            const SnapResult miss = snapPointWithGuides(QPointF(202, 2), std::nullopt, shapes, {}, 1.0, noKp);
            QVERIFY(!miss.contact);
            QCOMPARE(miss.point, QPointF(202, 2));
        }

        // 2. orthoPerpGuides: perpendicular snap and cardinal 0° snap respect orthoPerpGuides.
        {
            const SmartGuideOptions withOrtho{true, true, true, false};
            const SnapResult perp =
                snapPointWithGuides(QPointF(-18, 122), QPointF(50, 50), shapes, {}, 1.0, withOrtho);
            const QPointF d = perp.point - QPointF(50, 50);
            QVERIFY(std::abs(d.x() * 100.0 + d.y() * 100.0) < 1e-4);

            const SmartGuideOptions noOrtho{true, true, false, false};
            const SnapResult rawPerp =
                snapPointWithGuides(QPointF(-18, 122), QPointF(50, 50), shapes, {}, 1.0, noOrtho);
            QCOMPARE(rawPerp.point, QPointF(-18, 122));
            QVERIFY(rawPerp.guides.isEmpty());
        }

        // 3. alignXY: aligns X/Y with distant vertices and self handles when enabled.
        {
            const SmartGuideOptions onlyAlign{true, false, false, true};
            const SnapResult aligned =
                snapPointWithGuides(QPointF(202, 300), std::nullopt, shapes, {}, 1.0, onlyAlign);
            QVERIFY(!aligned.contact);
            QCOMPARE(aligned.point.x(), 200.0);

            const SmartGuideOptions noAlign{true, false, false, false};
            const SnapResult raw =
                snapPointWithGuides(QPointF(202, 300), std::nullopt, shapes, {}, 1.0, noAlign);
            QCOMPARE(raw.point, QPointF(202, 300));

            const ShapeItem rect = test::makeShape(ShapeKind::Rectangle, {QPointF(0, 0), QPointF(100, 80)});
            const SnapResult ext{QPointF(102, 3), {}, false};
            QCOMPARE(snapShapeVertex(rect, 1, ext, QPointF(102, 3), 1.0, onlyAlign).point, QPointF(100, 0));
            QCOMPARE(snapShapeVertex(rect, 1, ext, QPointF(102, 3), 1.0, noAlign).point, QPointF(102, 3));
        }

        // 4. Master switch enabled=false suppresses everything.
        {
            const SmartGuideOptions disabled{false, true, true, true};
            const SnapResult res =
                snapPointWithGuides(QPointF(202, 2), QPointF(50, 50), shapes, {}, 1.0, disabled);
            QVERIFY(!res.contact);
            QCOMPARE(res.point, QPointF(202, 2));
            QVERIFY(res.guides.isEmpty());
        }
    }

    void classifyAndMorphShapeFamiliesAcrossAllVariants() {
        // 1. Triangle family (0=Libre, 1=Isósceles, 2=Rectángulo, 3=Equilátero)
        {
            ShapeItem tri = test::makeShape(
                ShapeKind::Triangle, {QPointF(10, 0), QPointF(110, 20), QPointF(0, 80)});
            tri.closed = true;
            int variant = -1;
            QCOMPARE(classifyShapeFamily(tri, &variant), ShapeFamily::Triangle);
            QCOMPARE(variant, 0);

            QVERIFY(morphShapeToVariant(tri, ShapeFamily::Triangle, 2));
            QCOMPARE(classifyShapeFamily(tri, &variant), ShapeFamily::Triangle);
            QCOMPARE(variant, 2);
            QCOMPARE(tri.vertices.size(), 3);

            QVERIFY(morphShapeToVariant(tri, ShapeFamily::Triangle, 1));
            QCOMPARE(classifyShapeFamily(tri, &variant), ShapeFamily::Triangle);
            QCOMPARE(variant, 1);

            const QRectF preBounds = shapeGeometryBounds(tri);
            QVERIFY(morphShapeToVariant(tri, ShapeFamily::Triangle, 3));
            QCOMPARE(classifyShapeFamily(tri, &variant), ShapeFamily::Triangle);
            QCOMPARE(variant, 3);
            const QRectF eqBounds = shapeGeometryBounds(tri);
            QVERIFY(std::abs(eqBounds.width() - preBounds.width()) < 1e-4);
            QVERIFY(std::abs(eqBounds.center().x() - preBounds.center().x()) < 1e-4);
            QVERIFY(std::abs(eqBounds.center().y() - preBounds.center().y()) < 1e-4);
            const double s0 = QLineF(tri.vertices[0], tri.vertices[1]).length();
            const double s1 = QLineF(tri.vertices[1], tri.vertices[2]).length();
            const double s2 = QLineF(tri.vertices[2], tri.vertices[0]).length();
            QVERIFY(std::abs(s0 - s1) < 1e-4 && std::abs(s1 - s2) < 1e-4);

            QVERIFY(morphShapeToVariant(tri, ShapeFamily::Triangle, 0));
            QCOMPARE(tri.kind, ShapeKind::Triangle);
            QCOMPARE(tri.bounds, computeShapeBounds(tri));
        }

        // 2. Quad family (0=Rectángulo, 1=Cuadrado, 2=Rombo, 3=Paralelogramo, 4=Trapecio iso, 5=Trapecio rect, 6=Trapezoide)
        {
            ShapeItem quad = test::makeShape(
                ShapeKind::Rectangle, {QPointF(0, 0), QPointF(120, 80)});
            quad.closed = true;
            int variant = -1;
            QCOMPARE(classifyShapeFamily(quad, &variant), ShapeFamily::Quad);
            QCOMPARE(variant, 0);

            // Test non-square bounds across 2..6 and back to 0 (Rectángulo), then equalized 1 (Cuadrado) -> 2 (Rombo).
            for (int v : {2, 3, 4, 5, 6, 0}) {
                QVERIFY(morphShapeToVariant(quad, ShapeFamily::Quad, v));
                QCOMPARE(classifyShapeFamily(quad, &variant), ShapeFamily::Quad);
                QCOMPARE(variant, v);
                QCOMPARE(quad.bounds, computeShapeBounds(quad));
            }

            QVERIFY(morphShapeToVariant(quad, ShapeFamily::Quad, 1));
            QCOMPARE(classifyShapeFamily(quad, &variant), ShapeFamily::Quad);
            QCOMPARE(variant, 1);
            const QRectF sqR = shapeGeometryBounds(quad);
            QVERIFY(std::abs(sqR.width() - 100.0) < 1e-4);
            QVERIFY(std::abs(sqR.height() - 100.0) < 1e-4);
            QVERIFY(std::abs(sqR.center().x() - 60.0) < 1e-4);
            QVERIFY(std::abs(sqR.center().y() - 40.0) < 1e-4);

            // Rombo even from a 100x100 square must classify back as variant 2 (not Cuadrado).
            QVERIFY(morphShapeToVariant(quad, ShapeFamily::Quad, 2));
            QCOMPARE(classifyShapeFamily(quad, &variant), ShapeFamily::Quad);
            QCOMPARE(variant, 2);
            QCOMPARE(quad.bounds, computeShapeBounds(quad));
        }

        // 3. Circle family (0=Círculo, 1=Arco, 2=Sector, 3=Elipse)
        {
            ShapeItem circle = test::makeShape(
                ShapeKind::Circle, {QPointF(100, 100), QPointF(150, 100)});
            circle.closed = true;
            int variant = -1;
            QCOMPARE(classifyShapeFamily(circle, &variant), ShapeFamily::Circle);
            QCOMPARE(variant, 0);

            QVERIFY(morphShapeToVariant(circle, ShapeFamily::Circle, 1));
            QCOMPARE(circle.kind, ShapeKind::Arc);
            QVERIFY(!circle.closed);
            QCOMPARE(circle.vertices,
                     (QVector<QPointF>{QPointF(100, 100), QPointF(150, 100), QPointF(100, 50)}));
            QCOMPARE(classifyShapeFamily(circle, &variant), ShapeFamily::Circle);
            QCOMPARE(variant, 1);

            QVERIFY(morphShapeToVariant(circle, ShapeFamily::Circle, 2));
            QCOMPARE(circle.kind, ShapeKind::Sector);
            QVERIFY(circle.closed);
            QCOMPARE(circle.vertices.size(), 3);
            QCOMPARE(classifyShapeFamily(circle, &variant), ShapeFamily::Circle);
            QCOMPARE(variant, 2);

            QVERIFY(morphShapeToVariant(circle, ShapeFamily::Circle, 3));
            QCOMPARE(circle.kind, ShapeKind::Ellipse);
            QVERIFY(circle.closed);
            QCOMPARE(classifyShapeFamily(circle, &variant), ShapeFamily::Circle);
            QCOMPARE(variant, 3);

            QVERIFY(morphShapeToVariant(circle, ShapeFamily::Circle, 0));
            QCOMPARE(circle.kind, ShapeKind::Circle);
            QVERIFY(circle.closed);
            QCOMPARE(circle.vertices[0], QPointF(100, 100));
            QVERIFY(std::abs(QLineF(circle.vertices[0], circle.vertices[1]).length() - 50.0) < 1e-4);
            QCOMPARE(classifyShapeFamily(circle, &variant), ShapeFamily::Circle);
            QCOMPARE(variant, 0);
        }

        // 4. Polygon family (5..12)
        {
            ShapeItem poly = test::makeShape(
                ShapeKind::Rectangle, {QPointF(50, 50), QPointF(150, 150)});
            poly.closed = true;
            const QPointF expectedCenter(100, 100);
            for (int n = 5; n <= 12; ++n) {
                QVERIFY(morphShapeToVariant(poly, ShapeFamily::Polygon, n));
                QCOMPARE(poly.kind, ShapeKind::Polyline);
                QVERIFY(poly.closed);
                QCOMPARE(poly.vertices.size(), n);
                int variant = -1;
                QCOMPARE(classifyShapeFamily(poly, &variant), ShapeFamily::Polygon);
                QCOMPARE(variant, n);
                const QPointF c = shapeGeometryBounds(poly).center();
                QVERIFY(std::abs(c.x() - expectedCenter.x()) < 1e-4);
                QVERIFY(std::abs(c.y() - expectedCenter.y()) < 1e-4);
                const double side0 = QLineF(poly.vertices[0], poly.vertices[1]).length();
                for (int i = 1; i < n; ++i) {
                    const double sideI = QLineF(poly.vertices[i], poly.vertices[(i + 1) % n]).length();
                    QVERIFY(std::abs(sideI - side0) < 1e-4);
                }
            }
        }

        // 5. Solid3D family (0..9)
        {
            ShapeItem solid = test::makeShape(
                ShapeKind::Solid3D, {QPointF(0, 0), QPointF(80, 80)});
            for (int k = 0; k <= 9; ++k) {
                QVERIFY(morphShapeToVariant(solid, ShapeFamily::Solid3D, k));
                QCOMPARE(solid.kind, ShapeKind::Solid3D);
                QCOMPARE(solid.solidKind, k);
                int variant = -1;
                QCOMPARE(classifyShapeFamily(solid, &variant), ShapeFamily::Solid3D);
                QCOMPARE(variant, k);
                QCOMPARE(solid.bounds, computeShapeBounds(solid));
            }
        }

        // 6. None / invalid variant guards
        {
            const ShapeItem line = test::makeShape(ShapeKind::Line, {QPointF(0, 0), QPointF(50, 0)});
            const ShapeItem table =
                test::makeShape(ShapeKind::Rectangle, {QPointF(0, 0), QPointF(100, 80)},
                                QStringLiteral("tabla:3x4"));
            QCOMPARE(classifyShapeFamily(line), ShapeFamily::None);
            QCOMPARE(classifyShapeFamily(table), ShapeFamily::None);

            ShapeItem rect = test::makeShape(ShapeKind::Rectangle, {QPointF(0, 0), QPointF(60, 40)});
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::None, 0));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Triangle, 4));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Quad, 7));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Circle, -1));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Polygon, 4));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Polygon, 13));
            QVERIFY(!morphShapeToVariant(rect, ShapeFamily::Solid3D, 10));
        }
    }
};

QTEST_MAIN(TestGeometryAndRegions)
#include "test_geometry_and_regions.moc"
