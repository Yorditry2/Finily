#include "TestHelpers.h"
#include "core/Types.h"
#include "core/Utils.h"
#include "engine/InkEngine.h"
#include "engine/LiveStroke.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QtTest/QtTest>

#include <cmath>
#include <limits>

using namespace finily;

class TestInkAndLive : public QObject {
    Q_OBJECT

private slots:
    void inkSmootherPreservesEndpointsAndSharpCorner() {
        Stroke stroke;
        stroke.tool = ToolType::Pen;
        // L-shape with 90-degree corner at (50, 0).
        for (int i = 0; i <= 10; ++i) {
            stroke.points.push_back({double(i) * 5.0, 0.0, 0.5, i * 5, 0.0, 0.0});
        }
        for (int i = 1; i <= 10; ++i) {
            stroke.points.push_back({50.0, double(i) * 5.0, 0.5, 50 + i * 5, 0.0, 0.0});
        }

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
        QVERIFY2(cornerDrift < 2.5,
                 qPrintable(QString("Corner drift too large: %1").arg(cornerDrift)));
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

    void liveStrokePredictsTipAndCapsStep() {
        Stroke pen = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(30.0, 0.0), 3);
        const auto tip = predictTip(pen, 1.0);
        QVERIFY(tip.has_value());
        QVERIFY(tip->x > 30.0);
        QVERIFY(std::abs(tip->y) < 1e-6);
        QCOMPARE(tip->timestampMs, qint64(40));
        QVERIFY(tip->x - 30.0 <= 14.0 + 1e-6);

        // High speed is capped to 14.0 / zoom.
        Stroke fast = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(90.0, 0.0), 3);
        const auto capped = predictTip(fast, 2.0);
        QVERIFY(capped.has_value());
        QVERIFY(std::abs((capped->x - 90.0) - 7.0) < 1e-6);

        // Highlighter, short strokes, stalled timestamps, reversals, and invalid zoom return nullopt.
        Stroke hl = pen;
        hl.tool = ToolType::Highlighter;
        QVERIFY(!predictTip(hl, 1.0).has_value());

        Stroke twoPts = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(10.0, 0.0), 1);
        QVERIFY(!predictTip(twoPts, 1.0).has_value());

        Stroke stalled = pen;
        stalled.points.back().timestampMs = 200;
        QVERIFY(!predictTip(stalled, 1.0).has_value());

        Stroke reversed = pen;
        reversed.points.back().x = 15.0;
        QVERIFY(!predictTip(reversed, 1.0).has_value());
        QVERIFY(!predictTip(pen, 0.0).has_value());
    }

    void liveStrokeTrimsPenHooks() {
        Stroke s;
        s.tool = ToolType::Pen;
        s.hasHardwarePressure = true;
        for (int i = 0; i <= 15; ++i) {
            s.points.push_back({double(i) * 3.0, 0.0, 0.7, i * 5, 0.0, 0.0});
        }
        // Append a short sharp 90-degree lift hook with falling pressure.
        s.points.push_back({45.0, 4.0, 0.15, 80, 0.0, 0.0});

        Stroke noHw = s;
        noHw.hasHardwarePressure = false;
        QVERIFY(!trimPenHooks(noHw, 1.0));
        QCOMPARE(noHw.points.size(), 17);

        QVERIFY(trimPenHooks(s, 1.0));
        QCOMPARE(s.points.size(), 16);
        QCOMPARE(s.points.back().x, 45.0);
        QCOMPARE(s.points.back().y, 0.0);
    }

    void liveStrokeTapersPenFlick() {
        Stroke flick;
        flick.tool = ToolType::Pen;
        flick.size = 4.0;
        flick.hasHardwarePressure = true;
        flick.points = {
            {0.0, 0.0, 0.8, 0, 0.0, 0.0},
            {10.0, 0.0, 0.8, 10, 0.0, 0.0},
            {20.0, 0.0, 0.8, 20, 0.0, 0.0},
            {30.0, 0.0, 0.8, 30, 0.0, 0.0},
            {40.0, 0.0, 0.8, 40, 0.0, 0.0},
            {60.0, 0.0, 0.8, 48, 0.0, 0.0},
            {80.0, 0.0, 0.8, 56, 0.0, 0.0},
            {100.0, 0.0, 0.8, 64, 0.0, 0.0},
        };

        QVERIFY(taperPenFlick(flick, 1.0));
        QVERIFY(flick.points.back().pressure < 0.1);
        QCOMPARE(flick.points.front().pressure, 0.8);

        // Slow lift (< 0.5 px/ms) keeps round tip untouched.
        Stroke slow;
        slow.tool = ToolType::Pen;
        slow.size = 4.0;
        slow.hasHardwarePressure = true;
        slow.points = {
            {0.0, 0.0, 0.8, 0, 0.0, 0.0},
            {10.0, 0.0, 0.8, 10, 0.0, 0.0},
            {20.0, 0.0, 0.8, 20, 0.0, 0.0},
            {30.0, 0.0, 0.8, 30, 0.0, 0.0},
            {40.0, 0.0, 0.8, 40, 0.0, 0.0},
            {42.0, 0.0, 0.8, 50, 0.0, 0.0},
            {44.0, 0.0, 0.8, 60, 0.0, 0.0},
            {46.0, 0.0, 0.8, 70, 0.0, 0.0},
        };
        QVERIFY(!taperPenFlick(slow, 1.0));
        QCOMPARE(slow.points.back().pressure, 0.8);
    }

    void drawStrokePreservesWidthUnderRotation() {
        Stroke stroke = test::makeLineStroke(
            QPointF(0.0, 0.0), QPointF(80.0, 0.0), 8, 6.0, ToolType::Pen, Qt::white);
        stroke.tipStyle = 1;

        QImage img(160, 160, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            QTransform xf;
            xf.translate(40.0, 40.0);
            xf.rotate(45.0);
            p.setWorldTransform(xf);
            drawStroke(p, stroke, false);
        }

        int litPixels = 0;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                if (qRed(row[x]) > 128) {
                    ++litPixels;
                }
            }
        }
        QVERIFY(litPixels > 100);

        // Scan perpendicular to the 45-degree diagonal through midpoint (68, 68).
        int perpWidth = 0;
        for (int dx = -10; dx <= 10; ++dx) {
            const int x = 68 + dx;
            const int y = 68 - dx;
            if (qRed(img.pixel(x, y)) > 128) {
                ++perpWidth;
            }
        }
        QVERIFY(perpWidth >= 3);
    }

    void straightHighlighterHitTestAndPartialErase() {
        Stroke hl;
        hl.id = QStringLiteral("hl1");
        hl.tool = ToolType::Highlighter;
        hl.tipStyle = 2;
        hl.size = 12.0;
        hl.color = QColor("#fde047");
        hl.points = {
            {0.0, 0.0, 0.5, 0, 0.0, 0.0},
            {50.0, 80.0, 0.5, 10, 0.0, 0.0},
            {100.0, 0.0, 0.5, 20, 0.0, 0.0},
        };
        hl.bounds = computeBounds(hl.points, hl.size);

        QVERIFY(hitTestStroke(hl, QPointF(50.0, 0.0), 4.0));
        QVERIFY(!hitTestStroke(hl, QPointF(50.0, 80.0), 4.0));
        QVERIFY(!eraseStrokePartially(hl, QPointF(50.0, 80.0), 4.0).has_value());

        const auto split = eraseStrokePartially(hl, QPointF(50.0, 0.0), 10.0);
        QVERIFY(split.has_value());
        QCOMPARE(split->size(), 2);
        QCOMPARE(split->at(0).tool, ToolType::Highlighter);
        QCOMPARE(split->at(0).tipStyle, 2);
        QVERIFY(split->at(0).points.back().x < 35.0);
        QVERIFY(split->at(1).points.front().x > 65.0);
    }

    void eraseStrokePartiallyDropsOnePointFragmentsAndClearsCache() {
        Stroke stroke = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(100.0, 0.0), 4, 2.0);
        QImage canvas(120, 40, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        {
            QPainter p(&canvas);
            drawStroke(p, stroke, false);
        }
        QVERIFY(!stroke.cachedPath.isEmpty());
        QVERIFY(stroke.cachedShapeHash != 0);

        const auto midSplit = eraseStrokePartially(stroke, QPointF(50.0, 0.0), 10.0);
        QVERIFY(midSplit.has_value());
        QCOMPARE(midSplit->size(), 2);
        for (const Stroke& frag : *midSplit) {
            QVERIFY(frag.points.size() >= 2);
            QVERIFY(frag.cachedPath.isEmpty());
            QCOMPARE(frag.cachedShapeHash, quint64(0));
            QCOMPARE(frag.cachedOrigin, QPointF());
            QVERIFY(frag.id != stroke.id);
        }

        // Grazing cut leaving only a sub-epsilon 1-point stub at (0, 0) drops that fragment.
        const auto grazedLong = eraseStrokePartially(stroke, QPointF(11.4 + 5e-9, 0.0), 10.0);
        QVERIFY(grazedLong.has_value());
        QCOMPARE(grazedLong->size(), 1);
        QVERIFY(grazedLong->at(0).points.size() >= 2);
        QVERIFY(grazedLong->at(0).points.front().x > 20.0);
        QVERIFY(grazedLong->at(0).cachedPath.isEmpty());
        QCOMPARE(grazedLong->at(0).cachedShapeHash, quint64(0));

        Stroke shortStroke = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(10.0, 0.0), 1, 2.0);
        const auto grazedShort = eraseStrokePartially(shortStroke, QPointF(11.4 + 5e-9, 0.0), 10.0);
        QVERIFY(grazedShort.has_value());
        QVERIFY(grazedShort->isEmpty());
    }

    void velocityGaussianStabilizerGuardsNonFinitePoints() {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        VelocityGaussianStabilizer stab;
        stab.setStrength(nan);
        const StrokePoint first = stab.filter({nan, inf, nan, 0, inf, -inf});
        QVERIFY(std::isfinite(first.x));
        QVERIFY(std::isfinite(first.y));
        QVERIFY(std::isfinite(first.pressure));
        QVERIFY(std::isfinite(first.tiltX));
        QVERIFY(std::isfinite(first.tiltY));
        QCOMPARE(first.x, 0.0);
        QCOMPARE(first.y, 0.0);

        stab.setStrength(0.6);
        const StrokePoint valid = stab.filter({25.0, -12.0, 0.7, 10, 10.0, -10.0});
        QCOMPARE(valid.x, 25.0);
        QCOMPARE(valid.y, -12.0);

        const StrokePoint bad = stab.filter({nan, inf, nan, 20, nan, inf});
        QVERIFY(std::isfinite(bad.x));
        QVERIFY(std::isfinite(bad.y));
        QVERIFY(std::isfinite(bad.pressure));
        QCOMPARE(bad.x, 25.0);
        QCOMPARE(bad.y, -12.0);
        QCOMPARE(bad.tiltX, 0.0);
        QCOMPARE(bad.tiltY, 0.0);

        const StrokePoint next = stab.filter({30.0, -10.0, 0.8, 30, 5.0, -5.0});
        QVERIFY(std::isfinite(next.x));
        QVERIFY(std::isfinite(next.y));
        QVERIFY(next.pressure >= 0.0 && next.pressure <= 1.0);
    }

    void liveInkRendererDetectsDprMismatch() {
        Stroke stroke = test::makeLineStroke(QPointF(0.0, 0.0), QPointF(190.0, 0.0), 19, 3.0);
        stroke.tipStyle = 1;
        QVERIFY(LiveInkRenderer::eligible(stroke));

        LiveInkRenderer renderer;
        const Viewport vp{0.0, 0.0, 1.0};
        const QSize pixels(400, 120);
        QImage layer;
        QVERIFY(!renderer.usable(vp, pixels, 1.0));

        renderer.advance(stroke, layer, vp, pixels, 1.0);
        QVERIFY(renderer.usable(vp, pixels, 1.0));
        QVERIFY(!renderer.usable(vp, pixels, 2.0));
        QVERIFY(!renderer.usable(Viewport{1.0, 0.0, 1.0}, pixels, 1.0));
        QVERIFY(!renderer.usable(vp, QSize(401, 120), 1.0));

        renderer.advance(stroke, layer, vp, pixels, 2.0);
        QCOMPARE(layer.devicePixelRatio(), 2.0);
        QVERIFY(renderer.usable(vp, pixels, 2.0));
        QVERIFY(!renderer.usable(vp, pixels, 1.0));
    }

    void backgroundTemplateExtremeZoomOutStaysBounded() {
        const ThemeConfig theme = getThemeConfig(ThemeId::DarkBoard);
        QImage img(256, 256, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);

        {
            QPainter p(&img);
            drawBackgroundTemplate(p, BackgroundTemplate::Grid, theme, Viewport{0.0, 0.0, 0.0},
                                   QRectF(0, 0, 256, 256), 1.0);
            drawBackgroundTemplate(
                p, BackgroundTemplate::Grid, theme,
                Viewport{0.0, 0.0, std::numeric_limits<double>::quiet_NaN()},
                QRectF(0, 0, 256, 256), 1.0);
        }
        QCOMPARE(img.pixelColor(128, 128), QColor(Qt::black));

        const Viewport extremeOut{0.0, 0.0, 1e-4};
        const QRectF hugeWorld(-1e7, -1e7, 2e7, 2e7);
        QElapsedTimer timer;
        timer.start();
        for (BackgroundTemplate tpl : {
                 BackgroundTemplate::Dots,
                 BackgroundTemplate::Grid,
                 BackgroundTemplate::Engineering,
                 BackgroundTemplate::Isometric,
             }) {
            QPainter p(&img);
            drawBackgroundTemplate(p, tpl, theme, extremeOut, hugeWorld, 1.0);
        }
        QVERIFY(timer.elapsed() < 1000);
    }

    void utilsTextFontAndMultilineTextBounds() {
        QCOMPARE(textFont(-20.0).pixelSize(), 1);
        QCOMPARE(textFont(0.0).pixelSize(), 1);
        QCOMPARE(textFont(0.4).pixelSize(), 1);
        QCOMPARE(textFont(18.0).pixelSize(), 18);

        const TextItem single = test::makeText(QPointF(16.0, 16.0), QStringLiteral("Linea"), 20.0);
        const TextItem multi =
            test::makeText(QPointF(16.0, 16.0), QStringLiteral("Linea 1\nLinea 2\nLinea 3"), 20.0);
        QVERIFY(multi.bounds.height() > single.bounds.height() * 2.0);
        QVERIFY(multi.bounds.width() >= single.bounds.width());

        QImage img(260, 160, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            drawTextItem(p, multi);
        }

        const int splitY = qRound(single.bounds.bottom()) + 4;
        bool topDrawn = false;
        bool bottomDrawn = false;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                if (qRed(row[x]) > 128) {
                    if (y < splitY) {
                        topDrawn = true;
                    } else {
                        bottomDrawn = true;
                    }
                }
            }
        }
        QVERIFY(topDrawn);
        QVERIFY(bottomDrawn);
    }
};

QTEST_MAIN(TestInkAndLive)
#include "test_ink_and_live.moc"
