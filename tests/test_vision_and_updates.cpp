#include "core/Types.h"
#include "engine/ImageVectorizer.h"
#include "engine/UpdateManager.h"
#include "engine/VisionExtractor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QNetworkRequest>
#include <QPainter>
#include <QPolygonF>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <cmath>
#include <numbers>

using namespace finily;

class TestVisionAndUpdates : public QObject {
    Q_OBJECT

private slots:
    void updateManifestParsesAndRejectsUntrustedUrls() {
        const QByteArray validJson = QByteArrayLiteral(
            "{\"version\":\"0.2.1\","
            "\"tag\":\"v0.2.1\","
            "\"published_at\":\"2026-11-24T20:00:00Z\","
            "\"release_url\":\"https://github.com/Yorditry2/Finily/releases/tag/v0.2.1\","
            "\"windows\":{"
            "\"asset\":\"Finily-Setup-x64.exe\","
            "\"url\":\"https://github.com/Yorditry2/Finily/releases/download/v0.2.1/"
            "Finily-Setup-x64.exe\","
            "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\""
            "}}");
        const auto parsed = parseUpdateManifest(validJson);
        QVERIFY(parsed.has_value());
        QVERIFY(parsed->valid);
        QCOMPARE(parsed->version, QStringLiteral("0.2.1"));
        QCOMPARE(parsed->installerAsset, QStringLiteral("Finily-Setup-x64.exe"));
        QCOMPARE(parsed->installerSha256.size(), 64);

        const QByteArray httpRelease = QByteArrayLiteral(
            "{\"version\":\"0.2.1\","
            "\"release_url\":\"http://github.com/Yorditry2/Finily/releases/tag/v0.2.1\"}");
        QVERIFY(!parseUpdateManifest(httpRelease).has_value());

        const QByteArray evilHost = QByteArrayLiteral(
            "{\"version\":\"0.2.1\","
            "\"release_url\":\"https://github.com/Yorditry2/Finily/releases/tag/v0.2.1\","
            "\"windows\":{"
            "\"asset\":\"Finily-Setup-x64.exe\","
            "\"url\":\"https://evil.example.com/Finily-Setup-x64.exe\","
            "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\""
            "}}");
        QVERIFY(!parseUpdateManifest(evilHost).has_value());

        const QByteArray badAssetPath = QByteArrayLiteral(
            "{\"version\":\"0.2.1\","
            "\"release_url\":\"https://github.com/Yorditry2/Finily/releases/tag/v0.2.1\","
            "\"windows\":{"
            "\"asset\":\"../Finily-Setup-x64.exe\","
            "\"url\":\"https://github.com/Yorditry2/Finily/releases/download/v0.2.1/"
            "Finily-Setup-x64.exe\","
            "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\""
            "}}");
        QVERIFY(!parseUpdateManifest(badAssetPath).has_value());

        const QByteArray badSha = QByteArrayLiteral(
            "{\"version\":\"0.2.1\","
            "\"release_url\":\"https://github.com/Yorditry2/Finily/releases/tag/v0.2.1\","
            "\"windows\":{"
            "\"asset\":\"Finily-Setup-x64.exe\","
            "\"url\":\"https://github.com/Yorditry2/Finily/releases/download/v0.2.1/"
            "Finily-Setup-x64.exe\","
            "\"sha256\":\"not-a-valid-sha256\""
            "}}");
        QVERIFY(!parseUpdateManifest(badSha).has_value());
    }

    void updateVersionComparisonHandlesSemver() {
        QVERIFY(isNewerVersion(QStringLiteral("0.2.1"), QStringLiteral("0.2.0")));
        QVERIFY(isNewerVersion(QStringLiteral("0.10.0"), QStringLiteral("0.2.0")));
        QVERIFY(isNewerVersion(QStringLiteral("v0.3.0"), QStringLiteral("0.2.0")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.0-preview.1")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.0")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.1.9"), QStringLiteral("0.2.0")));
    }

    void updateSha256ComputesExpectedHash() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("payload.bin"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("finily-test"), 11);
        file.close();

        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash("finily-test", QCryptographicHash::Sha256).toHex());
        QCOMPARE(computeFileSha256(filePath), expected);
    }

    void visionExtractionParsesShapesAndSnapsVertices() {
        const QByteArray payload = QByteArrayLiteral(
            "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":"
            "\"```json\\n"
            "{\\\"shapes\\\":["
            "{\\\"kind\\\":\\\"triangle\\\",\\\"points\\\":[[100,800],[500,100],[900,800]]},"
            "{\\\"kind\\\":\\\"line\\\",\\\"points\\\":[[502,102],[500,800]],\\\"dashed\\\":true},"
            "{\\\"kind\\\":\\\"circle\\\",\\\"center\\\":[500,500],\\\"radius\\\":200}"
            "],\\\"texts\\\":["
            "{\\\"text\\\":\\\"A\\\",\\\"x\\\":80,\\\"y\\\":810,\\\"size\\\":\\\"small\\\"},"
            "{\\\"text\\\":\\\"Hallar x\\\",\\\"x\\\":50,\\\"y\\\":30,\\\"size\\\":\\\"medium\\\"}"
            "]}\\n```\""
            "}]}}]}");

        const QRectF target(100.0, 200.0, 400.0, 400.0);
        const auto parsed = parseVisionExtraction(payload, target, QColor("#f8fafc"), 2.4);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->shapes.size(), 3);
        QCOMPARE(parsed->texts.size(), 2);

        // El triángulo debe mapear [500,100] -> (300, 240), y [502,102] debe imantarse al vértice
        QCOMPARE(parsed->shapes[0].kind, ShapeKind::Triangle);
        QCOMPARE(parsed->shapes[1].kind, ShapeKind::Line);
        QCOMPARE(parsed->shapes[1].linePattern, LinePattern::Dashed);
        QCOMPARE(parsed->shapes[1].vertices[0], parsed->shapes[0].vertices[1]);
        QCOMPARE(parsed->shapes[2].kind, ShapeKind::Circle);
        QVERIFY(
            std::abs((parsed->shapes[2].vertices[1].x() - parsed->shapes[2].vertices[0].x()) - 80.0)
            < 0.5);

        QCOMPARE(parsed->texts[0].text, QStringLiteral("A"));
        QCOMPARE(parsed->texts[1].text, QStringLiteral("Hallar x"));
        QVERIFY(!parsed->texts[1].bounds.isEmpty());
    }

    void imageVectorizerExtractsTriangleAndAltitudeIgnoringText() {
        QImage img(420, 320, QImage::Format_ARGB32);
        img.fill(Qt::white);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 3.0));
            // Triángulo + altura interna
            const QPointF a(70, 260), b(210, 60), c(350, 260), h(210, 260);
            p.drawPolygon(QPolygonF{a, b, c});
            p.drawLine(b, h);
            // Texto pequeño alrededor que debe ignorarse
            QFont f(QStringLiteral("Sans Serif"), 12);
            p.setFont(f);
            p.drawText(QPointF(48, 272), QStringLiteral("A"));
            p.drawText(QPointF(206, 46), QStringLiteral("B"));
            p.drawText(QPointF(358, 272), QStringLiteral("C"));
            p.drawText(QPointF(16, 26), QStringLiteral("Hallar x"));
        }

        const QRectF target(0, 0, 420, 320);
        const VectorizeResult res = vectorizeGeometryFromImage(img, target);
        QVERIFY(!res.shapes.isEmpty());
        QVERIFY(res.elapsedMs < 500);
        bool hasTriangleOrLines = false;
        for (const ShapeItem& s : res.shapes) {
            if (s.kind == ShapeKind::Triangle || s.kind == ShapeKind::Line)
                hasTriangleOrLines = true;
        }
        QVERIFY(hasTriangleOrLines);
        // No debe explotar en decenas de segmentos de texto
        QVERIFY(res.shapes.size() <= 8);
    }

    void imageVectorizerExtractsCircleFromDarkImage() {
        QImage img(320, 320, QImage::Format_ARGB32);
        img.fill(QColor(QStringLiteral("#16181d")));
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::white, 3.0));
            p.drawEllipse(QPointF(160, 160), 90, 90);
        }

        const VectorizeResult res = vectorizeGeometryFromImage(img, QRectF(0, 0, 320, 320));
        QCOMPARE(res.shapes.size(), 1);
        QCOMPARE(res.shapes[0].kind, ShapeKind::Circle);
        const QPointF center = res.shapes[0].vertices[0];
        const double radius = std::hypot(res.shapes[0].vertices[1].x() - center.x(),
                                         res.shapes[0].vertices[1].y() - center.y());
        QVERIFY(std::abs(center.x() - 160.0) < 3.0);
        QVERIFY(std::abs(center.y() - 160.0) < 3.0);
        QVERIFY(std::abs(radius - 90.0) < 3.5);
    }

    void imageVectorizerHandlesShadowedPhotoAndGapOnEdge() {
        QImage img(420, 300, QImage::Format_ARGB32);
        {
            QPainter p(&img);
            // Fondo con sombra diagonal fuerte como foto de libro en papel
            QLinearGradient grad(0, 0, 420, 300);
            grad.setColorAt(0.0, QColor(215, 210, 205));
            grad.setColorAt(1.0, QColor(115, 110, 108));
            p.fillRect(img.rect(), grad);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(25, 25, 25), 3.0));
            const QPointF A(45, 170), B(290, 50), C(360, 165), P(210, 255);
            p.drawPolygon(QPolygonF{A, B, C, P});
            // Diagonal AC con un pequeño corte de 12 px en medio (simula marca/punto)
            p.drawLine(A, QPointF(170, 168));
            p.drawLine(QPointF(182, 168), C);
            p.drawLine(B, P);
            // Arquito pequeño de ángulo en el vértice B
            p.drawArc(QRectF(B.x() - 28, B.y() - 28, 56, 56), 210 * 16, 65 * 16);
        }

        const VectorizeResult res = vectorizeGeometryFromImage(img, QRectF(0, 0, 420, 300));
        QVERIFY(!res.shapes.isEmpty());
        QVERIFY(res.elapsedMs < 500);
        // Ningún segmento recto debe haberse clasificado falsamente como Arc
        for (const ShapeItem& s : res.shapes)
            QVERIFY(s.kind != ShapeKind::Arc);
        QVERIFY(res.shapes.size() >= 3 && res.shapes.size() <= 10);
    }

    void imageVectorizerKeepsStraightVerticalSegmentAndConnectsPC() {
        QImage img(400, 320, QImage::Format_ARGB32);
        img.fill(QColor(185, 180, 175));
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(20, 20, 20), 3.0));
            const QPointF B(175, 55), Q(175, 265), A(95, 265), P(175, 160), C(315, 160);
            p.drawLine(A, B);
            p.drawLine(A, Q);
            p.drawLine(B, Q);
            p.drawLine(B, C);
            p.drawLine(P, C);
            // Cuadraditos de 90° en P y Q
            p.drawRect(QRectF(P.x(), P.y() - 14, 14, 14));
            p.drawRect(QRectF(Q.x() - 14, Q.y() - 14, 14, 14));
        }

        VectorizeOptions opts;
        opts.detectRightAngles = true;
        const VectorizeResult res = vectorizeGeometryFromImage(img, QRectF(0, 0, 400, 320), opts);
        QVERIFY(!res.shapes.isEmpty());
        int rightAngleCount = 0;
        for (const ShapeItem& s : res.shapes) {
            QVERIFY(s.kind != ShapeKind::Arc);
            if (s.kind == ShapeKind::RightAngle)
                ++rightAngleCount;
        }
        QVERIFY(rightAngleCount >= 1);
    }

    void imageVectorizerAutoDeskewsTiltedPhoto() {
        QImage img(400, 300, QImage::Format_ARGB32);
        img.fill(Qt::white);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(200, 150);
            p.rotate(4.0); // Foto tomada 4° chueca
            p.setPen(QPen(Qt::black, 3.0));
            p.drawPolygon(QPolygonF{QPointF(-110, 70), QPointF(-110, -70), QPointF(110, 70)});
        }

        const VectorizeResult res = vectorizeGeometryFromImage(img, QRectF(0, 0, 400, 300));
        QVERIFY(!res.shapes.isEmpty());
        // Comprobar que la base horizontal quedó enderezada (< 1.2° de inclinación)
        bool foundLevelBase = false;
        for (const ShapeItem& s : res.shapes) {
            for (qsizetype i = 0; i < s.vertices.size(); ++i) {
                const QPointF a = s.vertices[i];
                const QPointF b = s.vertices[(i + 1) % s.vertices.size()];
                if (std::hypot(b.x() - a.x(), b.y() - a.y()) > 120.0
                    && std::abs(b.x() - a.x()) > 100.0) {
                    const double deg =
                        std::abs(std::atan2(b.y() - a.y(), b.x() - a.x())) * 180.0 / std::numbers::pi;
                    if (deg < 1.2 || std::abs(deg - 180.0) < 1.2)
                        foundLevelBase = true;
                }
            }
        }
        QVERIFY(foundLevelBase);
    }

    void visionExtractorValidatesModelNamesAndRejectsInjection() {
        QVERIFY(isValidGeminiModelName(QStringLiteral("gemini-2.5-flash")));
        QVERIFY(isValidGeminiModelName(QStringLiteral("gemini-flash-latest")));
        QVERIFY(isValidGeminiModelName(QStringLiteral("gemini-2.5-pro")));
        QVERIFY(isValidGeminiModelName(QStringLiteral("gemini_1.5-pro")));

        QVERIFY(!isValidGeminiModelName(QString()));
        QVERIFY(!isValidGeminiModelName(QString(65, QLatin1Char('a'))));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("-gemini-2.5-flash")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral(".gemini-2.5-flash")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini..flash")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini-2.5-flash?key=leak")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini-2.5-flash/extra")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini-2.5-flash#frag")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini 2.5 flash")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini\t2.5")));
        QVERIFY(!isValidGeminiModelName(QStringLiteral("gemini\n2.5")));
    }

    void visionExtractorBuildRequestUsesHeaderNotQueryKey() {
        const auto reqOpt = buildVisionRequest(QStringLiteral("gemini-2.5-flash"),
                                               QStringLiteral("  AIzaTestSecret123  "));
        QVERIFY(reqOpt.has_value());

        const QUrl url = reqOpt->url();
        QCOMPARE(url.scheme(), QStringLiteral("https"));
        QCOMPARE(url.host(), QStringLiteral("generativelanguage.googleapis.com"));
        QCOMPARE(url.path(), QStringLiteral("/v1beta/models/gemini-2.5-flash:generateContent"));
        QVERIFY(!url.hasQuery());
        QVERIFY(!url.hasFragment());
        QVERIFY(!url.toString().contains(QStringLiteral("?key="), Qt::CaseInsensitive));
        QVERIFY(!url.toString().contains(QStringLiteral("AIzaTestSecret123")));

        QVERIFY(reqOpt->hasRawHeader("x-goog-api-key"));
        QCOMPARE(reqOpt->rawHeader("x-goog-api-key"), QByteArray("AIzaTestSecret123"));
        QCOMPARE(reqOpt->header(QNetworkRequest::ContentTypeHeader).toString(),
                 QStringLiteral("application/json"));

        QVERIFY(!buildVisionRequest(QStringLiteral("gemini-2.5-flash?alt=sse"),
                                    QStringLiteral("AIzaTestSecret123"))
                     .has_value());
        QVERIFY(!buildVisionRequest(QStringLiteral("gemini-2.5-flash"), QStringLiteral("   "))
                     .has_value());
        QVERIFY(!buildVisionRequest(QStringLiteral("gemini-2.5-flash"),
                                    QStringLiteral("secret\r\nX-Injected: 1"))
                     .has_value());
        const QString nulKey =
            QStringLiteral("secret") + QChar(QLatin1Char('\0')) + QStringLiteral("tail");
        QVERIFY(!buildVisionRequest(QStringLiteral("gemini-2.5-flash"), nulKey).has_value());
    }

    void updateManagerTrustedUpdateUrlHardening() {
        // Trusted URLs: official repo paths on port 443 / default and GitHub release asset hosts.
        QVERIFY(isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://github.com/Yorditry2/Finily/releases/tag/v0.2.1"))));
        QVERIFY(isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://github.com:443/Yorditry2/Finily/releases/latest/download/update-manifest.json"))));
        QVERIFY(isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://objects.githubusercontent.com/github-production-release-asset/1/Finily.exe"))));
        QVERIFY(isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://release-assets.githubusercontent.com/github-production-release-asset/1/Finily.exe"))));
        QVERIFY(isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://github-releases.githubusercontent.com/1/Finily.exe"))));

        // Reject userinfo, non-443 ports, wrong repo paths, traversal, untrusted hosts, HTTP.
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://evil@github.com/Yorditry2/Finily/releases/tag/v0.2.1"))));
        QVERIFY(!isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://user:pass@objects.githubusercontent.com/release/Finily.exe"))));
        QVERIFY(!isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://github.com:8443/Yorditry2/Finily/releases/tag/v0.2.1"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://objects.githubusercontent.com:8080/Finily.exe"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://github.com/OtherOwner/Finily/releases/tag/v0.2.1"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://github.com/Yorditry2/OtherRepo/releases/tag/v0.2.1"))));
        QVERIFY(!isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://github.com/Yorditry2/Finily/../../OtherOwner/Evil/releases/tag/v1"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://github.com/Yorditry2/Finily"))));
        QVERIFY(!isTrustedUpdateUrl(QUrl(QStringLiteral(
            "https://raw.githubusercontent.com/Yorditry2/Finily/main/Finily.exe"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://gist.githubusercontent.com/evil/123/raw/Finily.exe"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("https://evil.github.com/Yorditry2/Finily/releases/tag/v0.2.1"))));
        QVERIFY(!isTrustedUpdateUrl(
            QUrl(QStringLiteral("http://github.com/Yorditry2/Finily/releases/tag/v0.2.1"))));
    }

    void updateManagerSafeInstallerAssetNameRejectsTraversal() {
        QVERIFY(isSafeInstallerAssetName(QStringLiteral("Finily-Setup-x64.exe")));
        QVERIFY(isSafeInstallerAssetName(QStringLiteral("Finily_0.2.1.msi")));

        QVERIFY(!isSafeInstallerAssetName(QString()));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("   ")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral(" Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("Finily-Setup-x64.exe ")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral(".")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("..")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("../Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("..\\Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("sub/Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("sub\\Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("/tmp/Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("C:Finily-Setup-x64.exe")));
        QVERIFY(!isSafeInstallerAssetName(QStringLiteral("Finily\nSetup.exe")));
    }

    void updateSemverPrereleaseAndBuildOrdering() {
        // Prerelease numeric vs numeric compares numerically, not lexicographically.
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-alpha.10"), QStringLiteral("0.2.0-alpha.2")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0-alpha.2"), QStringLiteral("0.2.0-alpha.10")));

        // Numeric prerelease identifier has lower precedence than non-numeric.
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-alpha.beta"), QStringLiteral("0.2.0-alpha.1")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0-alpha.1"), QStringLiteral("0.2.0-alpha.beta")));

        // Longer prerelease with matching prefix is higher, and normal release > prerelease.
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-alpha.1"), QStringLiteral("0.2.0-alpha")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0-alpha"), QStringLiteral("0.2.0-alpha.1")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-beta"), QStringLiteral("0.2.0-alpha.99")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-rc.1"), QStringLiteral("0.2.0-beta.2")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0-rc.1"), QStringLiteral("0.2.0")));

        // Build metadata (+build) must be ignored in SemVer precedence comparisons.
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0+build.2"), QStringLiteral("0.2.0+build.1")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0+build.1"), QStringLiteral("0.2.0+build.2")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0+20261125"), QStringLiteral("0.2.0")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0+build.1"),
                               QStringLiteral("0.2.0-rc.1+build.99")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.0-beta+build.1"),
                               QStringLiteral("0.2.0-alpha+build.99")));
        QVERIFY(isNewerVersion(QStringLiteral("0.2.1-alpha+1"), QStringLiteral("0.2.0+999")));

        // Malformed empty prerelease is rejected.
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0-"), QStringLiteral("0.1.9")));
    }
};

QTEST_MAIN(TestVisionAndUpdates)
#include "test_vision_and_updates.moc"
