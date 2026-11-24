#include "core/Types.h"
#include "engine/DocumentSerializer.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/LassoEngine.h"
#include "engine/LiveStroke.h"

#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace finily;

class FinilyEngineTests : public QObject {
    Q_OBJECT

private slots:
    void inkSmootherPreservesEndpointsAndSharpCorner() {
        Stroke stroke;
        stroke.tool = ToolType::Pen;
        // L-shape with 90-degree corner at (50, 0).
        for (int i = 0; i <= 10; ++i)
            stroke.points.push_back({double(i) * 5.0, 0.0, 0.5, i * 5, 0.0, 0.0});
        for (int i = 1; i <= 10; ++i)
            stroke.points.push_back({50.0, double(i) * 5.0, 0.5, 50 + i * 5, 0.0, 0.0});

        InkSmoother smoother;
        smoother.reset(8.0, InkSmoother::kMaxRadius);
        Stroke live;
        for (const auto& pt : stroke.points) {
            live.points.push_back(pt);
            smoother.push(live);
        }

        QCOMPARE(live.points.front().x, 0.0);
        QCOMPARE(live.points.front().y, 0.0);
        QCOMPARE(live.points.back().x, 50.0);
        QCOMPARE(live.points.back().y, 50.0);
        // Corner at index 10 should remain sharp (< 2.5 px drift).
        const double cornerDrift = std::hypot(live.points[10].x - 50.0, live.points[10].y - 0.0);
        QVERIFY2(cornerDrift < 2.5, qPrintable(QString("Corner drift too large: %1").arg(cornerDrift)));
    }

    void eraseStrokePartiallySplitsLine() {
        Stroke stroke;
        stroke.id = "s1";
        stroke.tool = ToolType::Pen;
        stroke.size = 2.0;
        stroke.points = {
            {0.0, 0.0, 0.5, 0, 0.0, 0.0},
            {100.0, 0.0, 0.5, 100, 0.0, 0.0},
        };
        stroke.bounds = computeBounds(stroke.points, stroke.size);

        const auto result = eraseStrokePartially(stroke, QPointF(50.0, 0.0), 10.0);
        QVERIFY(result.has_value());
        QCOMPARE(result->size(), 2);
        QVERIFY(result->at(0).points.back().x < 42.0);
        QVERIFY(result->at(1).points.front().x > 58.0);
    }

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
        const auto region = shadedRegionAt(QPointF(50, 50), square, QColor("#ef4444"), ShadePattern::Soft);
        QVERIFY(region.has_value());
        QCOMPARE(region->kind, ShapeKind::ShadedRegion);
        QVERIFY(!region->vertices.isEmpty());
    }

    void lassoTranslateSelectionMovesOnlySelected() {
        Stroke s1, s2;
        s1.id = "sel";
        s1.points = {{0, 0, 0.5, 0, 0, 0}, {10, 10, 0.5, 10, 0, 0}};
        s2.id = "unsel";
        s2.points = {{50, 50, 0.5, 0, 0, 0}, {60, 60, 0.5, 10, 0, 0}};
        QVector<Stroke> strokes{s1, s2};

        ShapeItem sh1, sh2;
        sh1.id = "sh_sel";
        sh1.kind = ShapeKind::Line;
        sh1.vertices = {{0, 0}, {20, 0}};
        sh2.id = "sh_unsel";
        sh2.kind = ShapeKind::Line;
        sh2.vertices = {{0, 40}, {20, 40}};
        QVector<ShapeItem> shapes{sh1, sh2};

        translateSelection(strokes, shapes, QSet<QString>{"sel"}, QSet<QString>{"sh_sel"}, QPointF(15.0, -5.0));
        QCOMPARE(strokes[0].points[0].x, 15.0);
        QCOMPARE(strokes[0].points[0].y, -5.0);
        QCOMPARE(strokes[1].points[0].x, 50.0);
        QCOMPARE(shapes[0].vertices[0].x(), 15.0);
        QCOMPARE(shapes[1].vertices[0].y(), 40.0);
    }

    void documentSerializerRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath("roundtrip.finily");

        PageData page;
        Stroke s;
        s.id = "st1";
        s.tool = ToolType::Pen;
        s.tipStyle = 3; // Tiza
        s.pressureSensitivity = 0.8;
        s.opacity = 0.9;
        s.color = QColor("#fde047");
        s.size = 4.5;
        s.points = {{10, 20, 0.6, 1, 0, 0}, {40, 25, 0.8, 20, 0, 0}};
        page.strokes.push_back(s);

        ShapeItem tri;
        tri.id = "tri1";
        tri.kind = ShapeKind::Triangle;
        tri.size = 2.0;
        tri.vertices = {{0, 0}, {100, 0}, {50, 80}};
        tri.closed = true;
        tri.bounds = computeShapeBounds(tri);

        auto regionOpt = shadedRegionForShape(tri, QColor("#38bdf8"), ShadePattern::Diagonal);
        QVERIFY(regionOpt.has_value());
        ShapeItem region = *regionOpt;
        region.id = "reg1";
        region.shadeLayer = 1;
        page.shapes.push_back(region);

        const QJsonObject settings = documentSettings(ShapeKind::Triangle, true, true, false,
                                                      PressureProfile::Normal, ThemeId::DarkBoard,
                                                      BackgroundTemplate::Dots, Viewport{}, PageFormat::Infinite);
        QVERIFY(serializeDocumentToFile(filePath, {page}, 0, settings));

        const QJsonObject loaded = loadDocumentFromFile(filePath);
        QVERIFY(!loaded.isEmpty());
        QHash<QString, QVector<ImageItem>> pdfSources;
        const auto pagesArray = loaded.value("pages").toArray();
        QCOMPARE(pagesArray.size(), 1);
        const PageData restored = deserializePage(pagesArray.at(0).toObject(), pdfSources);

        QCOMPARE(restored.strokes.size(), 1);
        QCOMPARE(restored.strokes[0].tipStyle, 3);
        QVERIFY(std::abs(restored.strokes[0].pressureSensitivity - 0.8) < 1e-6);
        QVERIFY(std::abs(restored.strokes[0].opacity - 0.9) < 1e-6);

        QCOMPARE(restored.shapes.size(), 1);
        QCOMPARE(restored.shapes[0].shadePattern, ShadePattern::Diagonal);
        QCOMPARE(restored.shapes[0].shadeLayer, 1);
    }

    void documentSerializerNewFileDoesNotOverwriteExisting() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath("cuaderno.finily");

        const QJsonObject settings = documentSettings(ShapeKind::Triangle, true, true, false,
                                                      PressureProfile::Normal, ThemeId::DarkBoard,
                                                      BackgroundTemplate::Dots, Viewport{}, PageFormat::Infinite);
        PageData page1;
        page1.texts.push_back(TextItem{"t1", QPointF(10, 10), "Primero", QColor("#ffffff"), 16.0, QRectF(10, 10, 50, 20)});
        QVERIFY(serializeDocumentToFile(filePath, {page1}, 0, settings, true));

        PageData page2;
        page2.texts.push_back(TextItem{"t2", QPointF(20, 20), "Segundo", QColor("#ffffff"), 16.0, QRectF(20, 20, 50, 20)});
        QVERIFY(!serializeDocumentToFile(filePath, {page2}, 0, settings, true));

        QHash<QString, QVector<ImageItem>> pdfSources;
        const QJsonObject loaded = loadDocumentFromFile(filePath);
        const PageData restored = deserializePage(loaded.value("pages").toArray().at(0).toObject(), pdfSources);
        QCOMPARE(restored.texts.size(), 1);
        QCOMPARE(restored.texts[0].text, QStringLiteral("Primero"));
    }
};

QTEST_MAIN(FinilyEngineTests)
#include "test_engines.moc"
