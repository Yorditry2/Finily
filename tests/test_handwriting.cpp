#include "TestHelpers.h"
#include "core/Types.h"
#include "engine/HandwritingEngine.h"

#include <QtTest/QtTest>

using namespace finily;
using namespace finily::test;

namespace {

Stroke makeCustomLineStroke(QPointF from, QPointF to, int tipStyle, double pressureSens,
                            const QString& groupId) {
    Stroke s = makeLineStroke(from, to, 12, 3.0, ToolType::Pen, QColor("#38bdf8"));
    s.tipStyle = tipStyle;
    s.pressureSensitivity = pressureSens;
    s.groupId = groupId;
    return s;
}

} // namespace

class TestHandwriting : public QObject {
    Q_OBJECT

private slots:
    void beautifyClusterPreservesStrokeProperties() {
        ScopedTestPaths scoped;
        QVERIFY(scoped.isValid());

        Stroke vert = makeCustomLineStroke(QPointF(100.0, 75.0), QPointF(100.0, 125.0), 2, 0.85,
                                           QStringLiteral("eq-group"));
        Stroke horiz = makeCustomLineStroke(QPointF(75.0, 100.0), QPointF(125.0, 100.0), 2, 0.85,
                                            QStringLiteral("eq-group"));
        QVector<Stroke> strokes{vert, horiz};

        QString label;
        int deltaCount = 0;
        const bool ok =
            HandwritingEngine::instance().beautifyCluster(strokes, 0, 2, QColor(), &label, &deltaCount);
        QVERIFY(ok);
        QCOMPARE(label, QStringLiteral("+"));
        QVERIFY(!strokes.isEmpty());
        for (const Stroke& out : strokes) {
            QCOMPARE(out.tipStyle, 2);
            QCOMPARE(out.pressureSensitivity, 0.85);
            QCOMPARE(out.groupId, QStringLiteral("eq-group"));
        }
    }

    void recognizeClusterRecognizesDivisionSignAndColon() {
        ScopedTestPaths scoped;
        QVERIFY(scoped.isValid());

        // Division '÷': one horizontal bar plus one dot above and one dot below.
        const QVector<QVector<QPointF>> divStrokes = {
            {{80.0, 100.0}, {100.0, 100.0}, {120.0, 100.0}},
            {{99.0, 84.0}, {101.0, 84.0}, {101.0, 86.0}, {99.0, 86.0}},
            {{99.0, 114.0}, {101.0, 114.0}, {101.0, 116.0}, {99.0, 116.0}},
        };
        const RecognitionResult divRes =
            HandwritingEngine::instance().recognizeCluster(divStrokes, true);
        QVERIFY(divRes.matched);
        QCOMPARE(divRes.label, QStringLiteral("÷"));

        // Colon ':' next to a normal-height digit stem establishes standard x-height scale.
        Stroke leftDigit = makeLineStroke(QPointF(16.0, 76.0), QPointF(16.0, 118.0), 8, 2.0);
        leftDigit.id = QStringLiteral("c0");
        Stroke dTop = makeLineStroke(QPointF(40.0, 90.0), QPointF(42.0, 92.0), 2, 1.5);
        dTop.id = QStringLiteral("c1");
        Stroke dBot = makeLineStroke(QPointF(40.0, 106.0), QPointF(42.0, 108.0), 2, 1.5);
        dBot.id = QStringLiteral("c2");
        const QString colonText = HandwritingEngine::instance().recognizeStrokesToText(
            {leftDigit, dTop, dBot}, {QStringLiteral("c0"), QStringLiteral("c1"), QStringLiteral("c2")});
        QVERIFY2(colonText.contains(QLatin1Char(':')),
                 qPrintable(QString("Expected colon in '%1'").arg(colonText)));

        Stroke divBar = makeLineStroke(QPointF(140.0, 100.0), QPointF(180.0, 100.0), 6, 2.0);
        divBar.id = QStringLiteral("d_bar");
        Stroke divUp = makeLineStroke(QPointF(159.0, 86.0), QPointF(161.0, 88.0), 2, 1.5);
        divUp.id = QStringLiteral("d_up");
        Stroke divDn = makeLineStroke(QPointF(159.0, 112.0), QPointF(161.0, 114.0), 2, 1.5);
        divDn.id = QStringLiteral("d_dn");
        Stroke divStem = makeLineStroke(QPointF(110.0, 76.0), QPointF(110.0, 118.0), 8, 2.0);
        divStem.id = QStringLiteral("d_stem");
        const QString divText = HandwritingEngine::instance().recognizeStrokesToText(
            {divStem, divBar, divUp, divDn},
            {QStringLiteral("d_stem"), QStringLiteral("d_bar"), QStringLiteral("d_up"),
             QStringLiteral("d_dn")});
        QVERIFY2(divText.contains(QStringLiteral("÷")),
                 qPrintable(QString("Expected '÷' in '%1'").arg(divText)));
    }

    void customTemplatesSupportFourStrokesAndIsolatePerScope() {
        const QVector<QVector<QPointF>> fourStrokeGlyph = {
            {{0.0, 0.0}, {0.0, 40.0}},
            {{0.0, 0.0}, {28.0, 0.0}},
            {{0.0, 20.0}, {22.0, 20.0}},
            {{0.0, 40.0}, {28.0, 40.0}},
        };

        {
            ScopedTestPaths scopeA;
            QVERIFY(scopeA.isValid());
            auto& engine = HandwritingEngine::instance();
            QVERIFY(engine.saveCustomTemplate(QStringLiteral("testE4"), fourStrokeGlyph));
            QVERIFY(engine.customTemplateLabels().contains(QStringLiteral("testE4")));

            const RecognitionResult rec = engine.recognizeCluster(fourStrokeGlyph, false);
            QVERIFY(rec.matched);
            QCOMPARE(rec.label, QStringLiteral("testE4"));

            QVERIFY(engine.removeCustomTemplate(QStringLiteral("testE4")));
            QVERIFY(!engine.customTemplateLabels().contains(QStringLiteral("testE4")));
        }

        // Switching to a clean isolated path must start with zero lingering templates.
        {
            ScopedTestPaths scopeB;
            QVERIFY(scopeB.isValid());
            QVERIFY(!HandwritingEngine::instance().customTemplateLabels().contains(
                QStringLiteral("testE4")));
        }
    }
};

QTEST_MAIN(TestHandwriting)
#include "test_handwriting.moc"
