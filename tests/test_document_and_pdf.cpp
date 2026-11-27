#include "TestHelpers.h"

#include "core/Types.h"
#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/GeometryEngine.h"
#include "engine/InkEngine.h"
#include "engine/NotebookDocument.h"
#include "engine/PdfEngine.h"
#include "engine/StickerLibrary.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using namespace finily;
using finily::test::ScopedTestPaths;
using finily::test::makeLineStroke;
using finily::test::makeShape;
using finily::test::makeText;

class TestDocumentAndPdf : public QObject {
    Q_OBJECT

private slots:
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

        const QJsonObject settings = documentSettings(
            ShapeKind::Triangle, true, true, false, PressureProfile::Normal, ThemeId::DarkBoard,
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

        const QJsonObject settings = documentSettings(
            ShapeKind::Triangle, true, true, false, PressureProfile::Normal, ThemeId::DarkBoard,
            BackgroundTemplate::Dots, Viewport{}, PageFormat::Infinite);
        PageData page1;
        page1.texts.push_back(TextItem{"t1", QPointF(10, 10), "Primero", QColor("#ffffff"), 16.0,
                                       QRectF(10, 10, 50, 20), QString(), false});
        QVERIFY(serializeDocumentToFile(filePath, {page1}, 0, settings, true));

        PageData page2;
        page2.texts.push_back(TextItem{"t2", QPointF(20, 20), "Segundo", QColor("#ffffff"), 16.0,
                                       QRectF(20, 20, 50, 20), QString(), false});
        QVERIFY(!serializeDocumentToFile(filePath, {page2}, 0, settings, true));

        QHash<QString, QVector<ImageItem>> pdfSources;
        const QJsonObject loaded = loadDocumentFromFile(filePath);
        const PageData restored = deserializePage(loaded.value("pages").toArray().at(0).toObject(),
                                                  pdfSources);
        QCOMPARE(restored.texts.size(), 1);
        QCOMPARE(restored.texts[0].text, QStringLiteral("Primero"));
    }

    void newBackgroundTemplatesRenderAndSerializeRoundTrip() {
        const ThemeConfig theme = getThemeConfig(ThemeId::PaperLight);
        const QVector<BackgroundTemplate> newTpls = {
            BackgroundTemplate::Engineering,
            BackgroundTemplate::Isometric,
            BackgroundTemplate::Cornell,
            BackgroundTemplate::TwoColumn,
            BackgroundTemplate::School,
        };
        for (BackgroundTemplate tpl : newTpls) {
            QImage img(800, 600, QImage::Format_ARGB32);
            img.fill(Qt::white);
            {
                QPainter p(&img);
                drawBackgroundTemplate(p, tpl, theme, Viewport{0, 0, 1.0}, QRectF(0, 0, 800, 600), 1.0);
            }
            bool anyDrawn = false;
            for (int y = 0; y < img.height() && !anyDrawn; y += 2) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); x += 2) {
                    if (row[x] != qRgb(255, 255, 255)) {
                        anyDrawn = true;
                        break;
                    }
                }
            }
            QVERIFY(anyDrawn);
        }

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("templates_test.finily"));
        PageData p0;
        p0.background = BackgroundTemplate::Cornell;
        PageData p1;
        p1.background = BackgroundTemplate::Isometric;
        const QJsonObject settings = documentSettings(
            ShapeKind::Line, true, true, false, PressureProfile::Light, ThemeId::DarkBoard,
            BackgroundTemplate::Engineering, Viewport{}, PageFormat::A4);
        QVERIFY(serializeDocumentToFile(filePath, {p0, p1}, 0, settings, true));
        const QJsonObject loadedRoot = loadDocumentFromFile(filePath);
        QVERIFY(!loadedRoot.isEmpty());
        QCOMPARE(loadedRoot.value("template").toInt(), int(BackgroundTemplate::Engineering));

        QHash<QString, QVector<ImageItem>> pdfSources;
        const QJsonArray pages = loadedRoot.value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        const PageData dp0 = deserializePage(pages[0].toObject(), pdfSources);
        const PageData dp1 = deserializePage(pages[1].toObject(), pdfSources);
        QVERIFY(dp0.background.has_value());
        QCOMPARE(*dp0.background, BackgroundTemplate::Cornell);
        QVERIFY(dp1.background.has_value());
        QCOMPARE(*dp1.background, BackgroundTemplate::Isometric);
    }

    void physicsSpringAndCourseStickersInstantiateCleanly() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());

        ShapeItem spring;
        spring.id = QStringLiteral("spr1");
        spring.kind = ShapeKind::Line;
        spring.size = 2.4;
        spring.label = QStringLiteral("physics:spring");
        spring.vertices = {QPointF(0, 100), QPointF(180, 100)};
        spring.bounds = computeShapeBounds(spring);
        // Las espiras deben sobresalir verticalmente respecto al eje
        QVERIFY(spring.bounds.height() > spring.size * 4.0);
        // Hit-test sobre la parte superior de una espira
        QVERIFY(hitTestShape(spring, QPointF(90, 108), 8.0));

        const QStringList cats = stickerCategories();
        QVERIFY(cats.contains(QStringLiteral("Física")));
        QVERIFY(cats.contains(QStringLiteral("Química")));
        QVERIFY(cats.contains(QStringLiteral("Biología")));

        const QVector<StickerTemplate> all = allStickerTemplates();
        const ThemeConfig theme = getThemeConfig(ThemeId::DarkBoard);
        for (const QString& cat :
             {QStringLiteral("Física"), QStringLiteral("Química"), QStringLiteral("Biología")}) {
            int count = 0;
            for (const auto& tpl : all) {
                if (tpl.category == cat) {
                    ++count;
                    const InstantiatedSticker inst = instantiateSticker(
                        tpl, QPointF(0, 0), theme, StickerStyle::CleanWithTitle, 1.0);
                    QVERIFY(!inst.shapes.isEmpty());
                    const QPixmap thumb =
                        renderStickerThumbnail(tpl, QSize(160, 74), theme, StickerStyle::DiagramOnly);
                    QVERIFY(!thumb.isNull());
                }
            }
            QVERIFY(count >= 3);
        }
    }

    void notebookDocumentAddDeleteReorderPages() {
        auto makeTaggedPage = [](const QString& tag) {
            PageData page;
            page.texts.push_back(makeText(QPointF(10.0, 10.0), tag));
            return page;
        };

        NotebookDocument doc;
        doc.texts().push_back(makeText(QPointF(10.0, 10.0), QStringLiteral("P0")));

        QVERIFY(doc.insertPages(1, {makeTaggedPage(QStringLiteral("P1")),
                                    makeTaggedPage(QStringLiteral("P2"))}));
        QCOMPARE(doc.pageCount(), 3);
        QCOMPARE(doc.currentPageIndex(), 1);
        QCOMPARE(doc.page(0).texts[0].text, QStringLiteral("P0"));
        QCOMPARE(doc.page(1).texts[0].text, QStringLiteral("P1"));
        QCOMPARE(doc.page(2).texts[0].text, QStringLiteral("P2"));

        // Move P2 (index 2) to the front (destination 0).
        QVERIFY(doc.movePages({2}, 0));
        QCOMPARE(doc.currentPageIndex(), 0);
        QCOMPARE(doc.page(0).texts[0].text, QStringLiteral("P2"));
        QCOMPARE(doc.page(1).texts[0].text, QStringLiteral("P0"));
        QCOMPARE(doc.page(2).texts[0].text, QStringLiteral("P1"));

        // Move P2 (index 0) to the end (destination 3 -> target 2).
        QVERIFY(doc.movePages({0}, 3));
        QCOMPARE(doc.currentPageIndex(), 2);
        QCOMPARE(doc.page(0).texts[0].text, QStringLiteral("P0"));
        QCOMPARE(doc.page(1).texts[0].text, QStringLiteral("P1"));
        QCOMPARE(doc.page(2).texts[0].text, QStringLiteral("P2"));

        // Delete middle page while current is last -> current clamps cleanly to index 1.
        QVERIFY(doc.deletePages({1}));
        QCOMPARE(doc.pageCount(), 2);
        QCOMPARE(doc.currentPageIndex(), 1);
        QCOMPARE(doc.page(0).texts[0].text, QStringLiteral("P0"));
        QCOMPARE(doc.page(1).texts[0].text, QStringLiteral("P2"));

        // Deleting all pages restores one blank page with fallback viewport.
        const Viewport fallback{12.0, -34.0, 1.5};
        QVERIFY(doc.deletePages({0, 1}, fallback));
        QCOMPARE(doc.pageCount(), 1);
        QCOMPARE(doc.currentPageIndex(), 0);
        QVERIFY(doc.currentPage().texts.isEmpty());
        QCOMPARE(doc.viewport().zoom, 1.5);
        QCOMPARE(doc.viewport().x, 12.0);
        QCOMPARE(doc.viewport().y, -34.0);
    }

    void notebookDocumentStructuralUndoTruncatesStalePrefix() {
        NotebookDocument doc;
        PageData p1;
        p1.strokes.push_back(makeLineStroke(QPointF(0, 0), QPointF(20, 0)));
        PageData p2;
        p2.strokes.push_back(makeLineStroke(QPointF(0, 10), QPointF(20, 10)));
        QVERIFY(doc.insertPages(1, {p1, p2}));
        QCOMPARE(doc.pageCount(), 3);

        // Create a single-page undo snapshot tied to page index 2.
        QVERIFY(doc.goToPage(2));
        const DocumentSnapshot beforeEdit = doc.currentSnapshot();
        doc.texts().push_back(makeText(QPointF(5.0, 5.0), QStringLiteral("OnPage2")));
        doc.pushUndoSnapshot(beforeEdit);

        // Shrink back to 1 page, then exceed kMaxFullPageHistoryEntries (12) so prefix
        // truncation drops the evicted structural snapshot and the older pageIndex=2 entry.
        QVERIFY(doc.deletePages({1, 2}));
        QCOMPARE(doc.pageCount(), 1);
        for (int i = 0; i < 14; ++i) {
            PageData extra;
            extra.texts.push_back(makeText(QPointF(0.0, double(i)), QString::number(i)));
            QVERIFY(doc.insertPages(1, {extra}));
            QVERIFY(doc.deletePages({1}));
            doc.pushPageHistory();
        }

        int undoCount = 0;
        while (doc.canUndo()) {
            const auto res = doc.undo();
            QVERIFY(res.applied);
            QVERIFY(doc.currentPageIndex() >= 0);
            QVERIFY(doc.currentPageIndex() < doc.pageCount());
            QCOMPARE(res.pageIndex, doc.currentPageIndex());
            ++undoCount;
            QVERIFY(undoCount <= 50);
        }
        QVERIFY(undoCount > 0);
        QVERIFY(undoCount <= 12);

        while (doc.canRedo()) {
            const auto res = doc.redo();
            QVERIFY(res.applied);
            QVERIFY(doc.currentPageIndex() >= 0);
            QVERIFY(doc.currentPageIndex() < doc.pageCount());
        }
    }

    void notebookDocumentAutosaveSessionEpochAndRestore() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());

        const QString nbPath = paths.path(QStringLiteral("notebooks/session.finily"));
        NotebookDocument::DocumentToolSettings ts;
        ts.shapeKind = ShapeKind::Rectangle;
        ts.holdToShapeEnabled = false;
        ts.smartGuidesEnabled = true;
        ts.swapPenButtons = true;
        ts.pressureProfile = PressureProfile::Firm;

        NotebookDocument doc;
        QVERIFY(doc.createNotebook(nbPath, ThemeId::GreenBoard, BackgroundTemplate::Cornell,
                                   PageFormat::A4, Viewport{10.0, 20.0, 1.25}, ts));
        const uint64_t epochAfterOpen = doc.sessionEpoch();
        QCOMPARE(doc.filePath(), nbPath);
        QVERIFY(!doc.isDirty());

        doc.strokes().push_back(makeLineStroke(QPointF(0, 0), QPointF(100, 50)));
        doc.markDirty();
        QVERIFY(doc.isDirty());

        const NotebookDocument::AutosaveJob job = doc.prepareAutosave(ts, std::nullopt, true);
        QCOMPARE(job.autosaveFile, autosavePath());
        QCOMPARE(job.notebookFile, nbPath);
        QVERIFY(job.writeNotebook);
        QCOMPARE(job.sessionEpoch, epochAfterOpen);
        QCOMPARE(job.revision, doc.revision());
        QVERIFY(!job.sessionDirty);
        QVERIFY(!job.coverThumb.isNull());

        QVERIFY(NotebookDocument::executeAutosaveJob(job));
        QVERIFY(QFileInfo::exists(autosavePath()));

        // Simulate switching session before an async autosave completion callback fires:
        // sessionEpoch advances so the stale job's epoch no longer matches.
        doc.resetNew();
        QVERIFY(doc.sessionEpoch() > job.sessionEpoch);
        doc.markDirty();
        const uint64_t newRev = doc.revision();
        if (doc.sessionEpoch() == job.sessionEpoch)
            doc.markSaved(job.revision);
        QVERIFY(doc.isDirty());
        QCOMPARE(doc.revision(), newRev);

        // Restoring from autosave with isAutosaveRestore=true preserves sessionFilePath.
        NotebookDocument restored;
        NotebookDocument::DocumentToolSettings restoredTs;
        QVERIFY(restored.loadFromFile(autosavePath(), &restoredTs, /*isAutosaveRestore=*/true));
        QCOMPARE(restored.filePath(), nbPath);
        QCOMPARE(restored.themeId(), ThemeId::GreenBoard);
        QCOMPARE(restored.defaultTemplate(), BackgroundTemplate::Cornell);
        QCOMPARE(restored.pageFormat(), PageFormat::A4);
        QCOMPARE(restored.strokes().size(), 1);
        QVERIFY(!restored.isDirty());
        QCOMPARE(restoredTs.shapeKind, ShapeKind::Rectangle);
        QCOMPARE(restoredTs.pressureProfile, PressureProfile::Firm);

        // Unsaved document autosave preserves empty sessionFilePath and dirty flag.
        NotebookDocument unsaved;
        unsaved.texts().push_back(makeText(QPointF(15.0, 25.0), QStringLiteral("Draft")));
        unsaved.markDirty();
        const auto unsavedJob = unsaved.prepareAutosave(ts, std::nullopt, false);
        QVERIFY(!unsavedJob.writeNotebook);
        QVERIFY(unsavedJob.sessionDirty);
        QVERIFY(NotebookDocument::executeAutosaveJob(unsavedJob));

        NotebookDocument restoredUnsaved;
        QVERIFY(restoredUnsaved.loadFromFile(autosavePath(), nullptr, /*isAutosaveRestore=*/true));
        QVERIFY(restoredUnsaved.filePath().isEmpty());
        QVERIFY(restoredUnsaved.isDirty());
        QCOMPARE(restoredUnsaved.texts().size(), 1);
        QCOMPARE(restoredUnsaved.texts()[0].text, QStringLiteral("Draft"));
    }

    void documentSerializerReadNotebookMetadataHandlesLargePagesFirst() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("large_meta.finily"));

        PageData page;
        for (int i = 0; i < 120; ++i) {
            page.strokes.push_back(
                makeLineStroke(QPointF(i * 4.0, 0.0), QPointF(i * 4.0 + 30.0, 40.0), 8));
        }

        const QJsonObject settings = documentSettings(
            ShapeKind::Circle, true, true, false, PressureProfile::Normal, ThemeId::PaperCream,
            BackgroundTemplate::Isometric, Viewport{}, PageFormat::Slide16x9);
        QVERIFY(serializeDocumentToFile(filePath, {page}, 0, settings));

        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(raw.size() > 4096);
        const qsizetype pagesIdx = raw.indexOf("\"pages\"");
        const qsizetype themeIdx = raw.indexOf("\"theme\"");
        const qsizetype tplIdx = raw.indexOf("\"template\"");
        QVERIFY(pagesIdx >= 0);
        QVERIFY(themeIdx > pagesIdx);
        QVERIFY(tplIdx > pagesIdx);

        const NotebookFileMeta meta = readNotebookMetadata(filePath);
        QVERIFY(meta.found);
        QCOMPARE(meta.themeId, ThemeId::PaperCream);
        QCOMPARE(meta.tpl, BackgroundTemplate::Isometric);
        QCOMPARE(meta.pageFormat, PageFormat::Slide16x9);
    }

    void documentSerializerSkipsNullImageAndRoundTripsLongShapeLabel() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("images_and_labels.finily"));

        PageData page;
        ImageItem nullImg;
        nullImg.id = freshId();
        nullImg.rect = QRectF(0.0, 0.0, 100.0, 80.0);
        nullImg.isPdfPage = false;

        QImage validBmp(8, 8, QImage::Format_ARGB32);
        validBmp.fill(QColor(QStringLiteral("#38bdf8")));
        ImageItem validImg;
        validImg.id = freshId();
        validImg.rect = QRectF(20.0, 30.0, 64.0, 64.0);
        validImg.image = validBmp;

        page.images = {nullImg, validImg};

        const QString label512 = QString(512, QLatin1Char('L'));
        const QString label600 = QString(600, QLatin1Char('X'));
        page.shapes.push_back(makeShape(ShapeKind::Line, {QPointF(0, 0), QPointF(100, 0)}, label512));
        page.shapes.push_back(makeShape(ShapeKind::Line, {QPointF(0, 50), QPointF(100, 50)}, label600));

        const QJsonObject settings = documentSettings(
            ShapeKind::Line, true, true, false, PressureProfile::Light, ThemeId::DarkBoard,
            BackgroundTemplate::Dots, Viewport{}, PageFormat::Infinite);
        QVERIFY(serializeDocumentToFile(filePath, {page}, 0, settings));

        const QJsonObject loaded = loadDocumentFromFile(filePath);
        QVERIFY(!loaded.isEmpty());
        QHash<QString, QVector<ImageItem>> pdfSources;
        const PageData restored =
            deserializePage(loaded.value(QStringLiteral("pages")).toArray().at(0).toObject(), pdfSources);

        QCOMPARE(restored.images.size(), 1);
        QCOMPARE(restored.images[0].id, validImg.id);
        QVERIFY(!restored.images[0].image.isNull());

        QCOMPARE(restored.shapes.size(), 2);
        QCOMPARE(restored.shapes[0].label, label512);
        QCOMPARE(restored.shapes[0].label.size(), 512);
        QCOMPARE(restored.shapes[1].label.size(), 512);
        QCOMPARE(restored.shapes[1].label, label600.left(512));
    }

    void documentSerializerConcurrentThumbnailsAreRaceFree() {
        auto buildSamplePage = [](int variant) {
            PageData page;
            Stroke graphite = makeLineStroke(QPointF(10, 10), QPointF(180, 90), 10, 4.0,
                                             ToolType::Pen, QColor(QStringLiteral("#fde047")));
            graphite.tipStyle = 2; // Graphite textured brush cache
            Stroke chalk = makeLineStroke(QPointF(15, 110), QPointF(200, 140), 10, 5.0,
                                          ToolType::Pen, QColor(QStringLiteral("#38bdf8")));
            chalk.tipStyle = 3; // Chalk textured brush cache
            page.strokes = {graphite, chalk};

            ShapeItem tri = makeShape(ShapeKind::Triangle,
                                      {QPointF(20, 20), QPointF(140, 20), QPointF(80, 110)});
            tri.closed = true;
            page.shapes.push_back(tri);
            if (auto region = shadedRegionForShape(tri, QColor(QStringLiteral("#4ade80")),
                                                   ShadePattern::Crosshatch)) {
                page.shapes.push_back(*region);
            }
            page.texts.push_back(makeText(QPointF(30.0, 150.0),
                                          QStringLiteral("Thread %1").arg(variant)));
            return page;
        };

        constexpr int kThreads = 4;
        constexpr int kIterations = 8;
        std::vector<std::thread> workers;
        std::vector<int> okCounts(kThreads, 0);

        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([t, &okCounts, &buildSamplePage]() {
                for (int i = 0; i < kIterations; ++i) {
                    const PageData page = buildSamplePage(t * kIterations + i);
                    const ThemeId theme = (i % 2 == 0) ? ThemeId::DarkBoard : ThemeId::PaperLight;
                    const QImage thumb = renderPageThumbnail(
                        page, theme, BackgroundTemplate::Grid, PageFormat::A4, QSize(132, 170));
                    if (!thumb.isNull() && thumb.size() == QSize(264, 340))
                        ++okCounts[size_t(t)];
                }
            });
        }
        for (auto& worker : workers)
            worker.join();

        for (int t = 0; t < kThreads; ++t)
            QCOMPARE(okCounts[size_t(t)], kIterations);
    }

    void stickerLibraryCustomCategoryAndClampingOnLoad() {
        {
            ScopedTestPaths paths;
            QVERIFY(paths.isValid());

            const QVector<ShapeItem> shapes{
                makeShape(ShapeKind::Line, {QPointF(0, 0), QPointF(80, 0)})};
            QString customId;
            QVERIFY(saveCustomSticker(QStringLiteral("Mi Vector"), QStringLiteral("Álgebra"),
                                      {}, shapes, {}, &customId));
            QVERIFY(!customId.isEmpty());

            const auto found = findStickerTemplate(customId);
            QVERIFY(found.has_value());
            QVERIFY(found->isCustom);
            QCOMPARE(found->category, QStringLiteral("Álgebra"));
            QCOMPARE(found->title, QStringLiteral("Mi Vector"));
        }

        // Fresh ScopedTestPaths gives a distinct AppDataLocation path so cache reloads from disk.
        {
            ScopedTestPaths paths;
            QVERIFY(paths.isValid());

            const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QVERIFY(QDir().mkpath(appData));
            const QString stickersFile = appData + QStringLiteral("/stickers.json");

            const QByteArray malformed = QByteArrayLiteral(
                "{\"version\":1,\"stickers\":[{"
                "\"id\":\"custom_clamped\","
                "\"title\":\"Clamped\","
                "\"category\":\"Geometría Custom\","
                "\"strokes\":[{\"tool\":999,\"linePattern\":-5,\"pressureProfile\":99,"
                "\"tipStyle\":42,\"size\":-10.0,\"opacity\":5.0,\"points\":[[0,0,2.5,0,90,-90]]}],"
                "\"shapes\":[{\"kind\":999,\"linePattern\":99,\"lineEnding\":-1,"
                "\"shadePattern\":99,\"shadeLayer\":5,\"solidKind\":99,"
                "\"vertices\":[[0,0],[50,0],[25,40]]}],"
                "\"texts\":["
                "{\"x\":0,\"y\":0,\"text\":\"Small\",\"fontSize\":-50.0},"
                "{\"x\":0,\"y\":20,\"text\":\"Huge\",\"fontSize\":50000.0}"
                "]"
                "}]}");
            QFile f(stickersFile);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(f.write(malformed), malformed.size());
            f.close();

            const auto loaded = findStickerTemplate(QStringLiteral("custom_clamped"));
            QVERIFY(loaded.has_value());
            QCOMPARE(loaded->category, QStringLiteral("Geometría Custom"));

            QCOMPARE(loaded->strokes.size(), 1);
            QCOMPARE(loaded->strokes[0].tool, ToolType::Highlighter);
            QCOMPARE(loaded->strokes[0].linePattern, LinePattern::Solid);
            QCOMPARE(loaded->strokes[0].pressureProfile, PressureProfile::Firm);
            QCOMPARE(loaded->strokes[0].tipStyle, 2);
            QCOMPARE(loaded->strokes[0].size, 0.2);
            QCOMPARE(loaded->strokes[0].opacity, 1.0);
            QCOMPARE(loaded->strokes[0].points[0].pressure, 1.0);
            QCOMPARE(loaded->strokes[0].points[0].tiltX, 60.0);
            QCOMPARE(loaded->strokes[0].points[0].tiltY, -60.0);

            QCOMPARE(loaded->shapes.size(), 1);
            QCOMPARE(loaded->shapes[0].kind, ShapeKind::AngleMarker);
            QCOMPARE(loaded->shapes[0].linePattern, LinePattern::Dimension);
            QCOMPARE(loaded->shapes[0].lineEnding, LineEnding::None);
            QCOMPARE(loaded->shapes[0].shadePattern, ShadePattern::Grid);
            QCOMPARE(loaded->shapes[0].shadeLayer, 2);
            QCOMPARE(loaded->shapes[0].solidKind, 9);

            QCOMPARE(loaded->texts.size(), 2);
            QCOMPARE(loaded->texts[0].fontSize, 1.0);
            QCOMPARE(loaded->texts[1].fontSize, 1000.0);
        }
    }

    void pdfEngineExportsMultiLineTextAndSurvivesDegenerateInputs() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const ThemeConfig theme = getThemeConfig(ThemeId::PaperLight);

        PageData richPage;
        richPage.texts.push_back(
            makeText(QPointF(40.0, 60.0), QStringLiteral("Línea 1\n\nLínea 3 · α² + β²"), 18.0,
                     Qt::black));
        // Degenerate items that must be handled without crashing or producing invalid PDF state.
        richPage.texts.push_back(TextItem{QStringLiteral("empty_txt"), QPointF(10.0, 10.0),
                                          QString(), Qt::black, 0.0, QRectF(), QString(), false});
        richPage.texts.push_back(TextItem{QStringLiteral("zero_font"), QPointF(20.0, 20.0),
                                          QStringLiteral("ZeroFont"), Qt::black, -4.0,
                                          QRectF(), QString(), false});

        Stroke emptyStroke;
        emptyStroke.id = QStringLiteral("empty_st");
        emptyStroke.size = 0.0;

        Stroke nanStroke;
        nanStroke.id = QStringLiteral("nan_st");
        nanStroke.size = 0.0;
        nanStroke.points = {{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.5, 0, 0.0, 0.0}};

        richPage.strokes = {emptyStroke, nanStroke};
        richPage.shapes.push_back(makeShape(ShapeKind::Line, {QPointF(50, 50), QPointF(50, 50)},
                                            QString(), 0.0, Qt::black));

        ImageItem zeroImage;
        zeroImage.id = QStringLiteral("zero_img");
        zeroImage.rect = QRectF(0.0, 0.0, 0.0, 0.0);
        richPage.images.push_back(zeroImage);

        const QString pdfPath = dir.filePath(QStringLiteral("multiline_degenerate.pdf"));
        QVERIFY(exportPagesToPdf(pdfPath, {richPage, PageData{}}, theme,
                                 BackgroundTemplate::Ruled, PageFormat::A4));
        QFile pdfFile(pdfPath);
        QVERIFY(pdfFile.exists());
        QVERIFY(pdfFile.open(QIODevice::ReadOnly));
        const QByteArray header = pdfFile.read(5);
        QCOMPARE(header, QByteArrayLiteral("%PDF-"));
        QVERIFY(pdfFile.size() > 200);

        const QString emptyDocPdf = dir.filePath(QStringLiteral("empty_pages.pdf"));
        QVERIFY(exportPagesToPdf(emptyDocPdf, {}, theme, BackgroundTemplate::Blank,
                                 PageFormat::Infinite));
        QVERIFY(QFileInfo(emptyDocPdf).size() > 100);

        QVERIFY(!exportPagesToPdf(QString(), {richPage}, theme, BackgroundTemplate::Blank));
    }

    void autosaveDeferredCoverThumbAndPageDirtyTracking() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());

        const QString nbPath = paths.path(QStringLiteral("clase_meet.finily"));
        NotebookDocument::DocumentToolSettings ts;
        NotebookDocument doc;
        QVERIFY(doc.createNotebook(nbPath, ThemeId::DarkBoard, BackgroundTemplate::Dots,
                                   PageFormat::Infinite, Viewport{}, ts));

        PageData second;
        QVERIFY(doc.insertPages(1, {second}));
        QVERIFY(doc.goToPage(0));
        doc.pushUndoSnapshot(doc.currentSnapshot());
        doc.strokes().push_back(makeLineStroke(QPointF(10, 10), QPointF(120, 40)));
        QVERIFY(doc.saveToFile(nbPath, ts));
        QVERIFY(!doc.isCoverThumbDirty());

        // Editar página 2 NO ensucia la portada (página 1) durante el autoguardado periódico.
        QVERIFY(doc.goToPage(1));
        doc.pushUndoSnapshot(doc.currentSnapshot());
        doc.strokes().push_back(makeLineStroke(QPointF(20, 20), QPointF(160, 80)));
        QVERIFY(!doc.isCoverThumbDirty());

        const auto p2Job = doc.prepareAutosave(ts, std::nullopt, doc.isCoverThumbDirty(), true);
        QVERIFY(p2Job.writeNotebook);
        QVERIFY(!p2Job.saveCoverThumb);

        // Editar página 1 sí marca portada sucia y difiere el render al hilo trabajador.
        QVERIFY(doc.goToPage(0));
        doc.pushUndoSnapshot(doc.currentSnapshot());
        doc.strokes().push_back(makeLineStroke(QPointF(10, 80), QPointF(150, 90)));
        QVERIFY(doc.isCoverThumbDirty());

        const auto p1Job = doc.prepareAutosave(ts, std::nullopt, true, /*deferCoverRender=*/true);
        QVERIFY(p1Job.saveCoverThumb);
        QVERIFY(p1Job.deferCoverThumb);
        QVERIFY(p1Job.coverThumb.isNull());

        QFile::remove(coverThumbnailPath(nbPath));
        QVERIFY(NotebookDocument::executeAutosaveJob(p1Job));
        QVERIFY(QFileInfo::exists(coverThumbnailPath(nbPath)));
        doc.markCoverThumbSaved(p1Job.revision);
        QVERIFY(!doc.isCoverThumbDirty());
    }

    void textStyleAndImageOpacityRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("styles.finily"));

        PageData page;
        TextItem txt = makeText(QPointF(30.0, 40.0), QStringLiteral("E = mc^2"), 22.0, QColor("#18181B"));
        txt.fontFamilyIndex = 2; // Mono
        txt.bold = true;
        txt.italic = true;
        txt.bounds = textBounds(txt);
        page.texts.push_back(txt);

        ImageItem img;
        img.id = freshId();
        img.rect = QRectF(100.0, 120.0, 80.0, 60.0);
        img.image = QImage(16, 12, QImage::Format_ARGB32);
        img.image.fill(Qt::blue);
        img.opacity = 0.45;
        page.images.push_back(img);

        const auto settings = documentSettings(ShapeKind::Triangle, true, true, false,
                                               PressureProfile::Light, ThemeId::PaperLight,
                                               BackgroundTemplate::Grid, Viewport{},
                                               PageFormat::A4, 1.0, -1, 0, QColor());
        QVERIFY(serializeDocumentToFile(path, {page}, 0, settings));

        QHash<QString, QVector<ImageItem>> pdfSources;
        const QJsonObject loaded = loadDocumentFromFile(path);
        const QJsonArray pagesArr = loaded.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pagesArr.size(), 1);
        const PageData restored = deserializePage(pagesArr.at(0).toObject(), pdfSources);
        QCOMPARE(restored.texts.size(), 1);
        QCOMPARE(restored.texts[0].fontFamilyIndex, 2);
        QCOMPARE(restored.texts[0].bold, true);
        QCOMPARE(restored.texts[0].italic, true);
        QCOMPARE(restored.images.size(), 1);
        QVERIFY(std::abs(restored.images[0].opacity - 0.45) < 1e-4);
    }
};

QTEST_MAIN(TestDocumentAndPdf)
#include "test_document_and_pdf.moc"
